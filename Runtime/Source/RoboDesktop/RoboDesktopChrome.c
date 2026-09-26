/*
 * RoboDesktop's chrome (Phase 11.13) — the shell's own painting, and the whole of what makes this a
 * desktop rather than a page.
 *
 * ## Everything here is a fill or a glyph, and that is the design
 *
 * No CSS, no layout tree, no display list: the shell draws with `GfxRasterFillRect`,
 * `GfxRasterDrawLine` and the glyph bridge, in an order this file states and the frame record
 * counts. The consequence is the one `RoboDesktopVersion.h` names: the shell cannot be broken by a
 * document, because it never reads one.
 *
 * ## Where the glyphs come from, and where they are drawn
 *
 * **Shaping happens above this file** (Phase 12.24). `DesktopChromeLabelWidth` measures a label through
 * `PaintRunMeasure` and `DesktopChromeLabel` shapes it once with `PaintRunShape` and draws the resulting run
 * with `PaintRunDraw` -- the same doors `RoboPaintContext.c` and `RoboPaintDecoration.c` use, so a shell
 * label and a document's text are one implementation meeting three callers rather than separate
 * implementations that happen to agree today.
 *
 * What that replaced is the whole argument for the phase: this file used to decode UTF-8 itself, look each
 * code point up in the face's cmap itself, sum `advanceWidth` itself and stroke each glyph itself. All four
 * are places where a label could disagree with a window two windows away, and only the last is the shell's
 * business.
 *
 * ## The baseline rule, and the two of them
 *
 * Before Phase 12.3 there was one rule: `(band + cap) / 2`, with `cap` eight pixels — half the cap height
 * above the middle of the band. Both bands used it, because a panel label and a window title that did not
 * agree about where a baseline goes would read as two different typesettings on one screen.
 *
 * Since 12.3 there are two, and **which is in force is a property of the style rather than of the
 * surface**: a label resolved from a typography *role* is placed typographically (the role's line box
 * centred in the band, then dropped to the baseline by the face's own ascent), and a label drawn with the
 * caller's single face keeps the old arithmetic to the pixel. `DesktopTextBaseline` is the one door both
 * come through, so the four surfaces cannot disagree about which rule they are under.
 *
 * ## Where the face and the size come from
 *
 * `DesktopChromeTextStyle` resolves a **role** for each surface — `DesktopTitle` for the panel's identity,
 * `ChromeLabel` for the applications label, `ChromeStatus` for the status label and `WindowTitle` for a
 * window's title bar — and falls back to `config->face`/`labelSizePx` for a role the resolver cannot name
 * a face for. This file therefore knows *which role a surface speaks* and nothing about which family that
 * is: the font file, the weight and the size are the typography module's, and the manifest's.
 *
 * ## What is counted, and why a count and not a flag
 *
 * `record->fills` counts every fill *and* the two strokes of a close button's cross, because they are all
 * drawing operations this file performs and a count a reader can check against the code is worth more than
 * a category a reader has to guess the boundary of. `record->labels` counts labels drawn and
 * `record->labelsUnshaped` counts the ones a missing face made impossible — the pair is how a caller
 * learns that the desktop it is looking at has no text in it *because there is no font*, rather than
 * because the shell decided not to draw any.
 */

#include "RoboDesktopInternal.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsRaster.h"
#include "RoboMem/RoboMem.h"
#include "RoboPaint/RoboPaintRun.h"
#include "RoboTypography/RoboTypography.h"

/* The gap between the identity mark and the identity label: three pixels of mark, eight of space, which
 * is enough for the mark to read as a mark rather than as part of the lettering. */
#define RoboDesktopIdentityGap 8

/* Half the cap height, in pixels: the constant in the baseline rule above. */
#define RoboDesktopCapHalf 4

/* The inset of a close button's cross inside its own square. */
#define RoboDesktopCloseCrossInset 3

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

/*
 * The shaping setting a label is drawn at, in the painter's own vocabulary (Phase 12.24).
 *
 * The shell has no per-surface weight or slope -- a panel label is upright text at its role's size, and a
 * surface that is italicised would be a new look rather than a new measurement -- so this is one number and
 * `PaintRunStyleInit`'s three defaults. It is a function rather than two assignments at each call site
 * because a width and a drawing that disagreed about the size would be a label whose box and whose glyphs
 * came from different typesettings.
 */
static void DesktopRunStyle(PaintRunStyle *run, const DesktopTextStyle *style)
{
    PaintRunStyleInit(run);
    run->sizePx = style->sizePx;
}

DesktopTextStyle DesktopChromeTextStyle(const Desktop *desktop, RoboTypographyRole role)
{
    DesktopTextStyle style;

    style.face = NULL;
    style.sizePx = 0.0f;
    style.lineHeightPx = 0.0f;
    style.ascentPx = 0.0f;
    style.fromRole = 0;
    if (!desktop) {
        return style;
    }

    /*
     * The role wins when the resolver can name a face for it, and the caller's own pair is the
     * fallback. The order is deliberate: Phase 12.3's claim is that the shell draws from the system
     * typeface pack, so a resolver that answers must be the answer -- and a resolver that cannot (a
     * family that is not installed) must degrade to *something* rather than to no text, because the
     * frame record already counts what it could not shape and a blank panel is worse evidence than a
     * fallback one.
     */
    if (desktop->typography) {
        RoboTypographyStyle resolved;

        if (RoboTypographyResolve(desktop->typography, role, &resolved, NULL) == RoboOk &&
            resolved.face) {
            style.face = resolved.face;
            style.sizePx = resolved.sizePx;
            style.lineHeightPx = resolved.lineHeightPx;
            style.ascentPx = resolved.ascentPx;
            style.fromRole = 1;
            return style;
        }
    }
    style.face = desktop->face;
    style.sizePx = desktop->labelSizePx;
    return style;
}

int32_t DesktopTextBaseline(const DesktopTextStyle *style,
                            int32_t bandY,
                            int32_t bandHeight,
                            int32_t legacyBaselinePx)
{
    if (!style || !style->fromRole) {
        return legacyBaselinePx;
    }
    /*
     * Centre the role's *line box* in the band, then drop from the line box's top to the baseline by
     * the face's own ascent. That is the ordinary typographic placement -- the one CSS's own line box
     * model reduces to for a single line -- and it is stated once here rather than as a per-surface
     * pixel offset, which is the thing the phase forbids.
     *
     * A band shorter than the line box yields a *clamped* zero offset rather than a negative one: the
     * label is then drawn on the band's own top edge, which is what a window too short for its title
     * bar looks like rather than glyphs escaping the decoration above it.
     */
    {
        float offset = ((float)bandHeight - style->lineHeightPx) * 0.5f + style->ascentPx;

        if (offset < 0.0f) {
            offset = 0.0f;
        }
        return bandY + (int32_t)(offset + 0.5f);
    }
}

int32_t DesktopChromeLabelWidth(const DesktopTextStyle *style,
                                       const char *text,
                                       size_t length)
{
    PaintRunStyle run;
    float width = 0.0f;

    if (!style || !text || length == 0u || !style->face || style->sizePx <= 0.0f) {
        return 0;
    }
    DesktopRunStyle(&run, style);
    /*
     * The width is **the shaping measurement**, not a sum over the label's glyphs: `PaintRunMeasure` runs the
     * text layer's own engine (Phase 12.15), so a ligature, a kern or a mark's zero advance is in this number
     * exactly as it is in the box the same text would occupy in a document. A face that will not answer is a
     * width of zero, which centres the label on its band's midpoint rather than leaving it unplaced.
     */
    if (PaintRunMeasure(style->face, text, length, &run, &width, NULL) != RoboOk) {
        return 0;
    }
    return (int32_t)(width + 0.5f);
}

void DesktopChromeLabel(const DesktopTextStyle *style,
                               GfxRaster *raster,
                               int32_t x,
                               int32_t baselinePx,
                               const char *text,
                               size_t length,
                               GfxColorRgba8 color,
                               size_t *outDrawn,
                               size_t *outUnshaped)
{
    PaintRunStyle run;
    PaintGlyph *glyphs = NULL;
    PaintRunDrawStats stats;
    size_t count = 0u;

    /*
     * The two counters are **accumulators**, not outputs: a caller passes the frame record's own fields and
     * each label adds one to exactly one of them on its way through. That is why nothing is zeroed here.
     * Counting per *glyph* instead was the first version of this function and it was wrong for the thing the
     * record is read for: a caller asking "did my desktop draw its labels?" needs the number of labels, and
     * `labels` holding the last label's glyph count would answer a different question -- and would have
     * clobbered the two labels before it.
     *
     * Since 12.24 the pair is also the record's **complete** account of the labels it attempted: a label is
     * drawn (some ink reached the raster) or it is unshaped (none did), and no third outcome is counted twice
     * or dropped. That is what keeps `labels + labelsUnshaped` the number of labels asked for.
     */
    if (!style || !raster || !text || length == 0u) {
        return;
    }
    if (!style->face || style->sizePx <= 0.0f) {
        /* No face: the label is *counted* rather than silently skipped, so a frame's record says which
         * of the two reasons it has no text (see the file comment). */
        if (outUnshaped) {
            (*outUnshaped)++;
        }
        return;
    }

    DesktopRunStyle(&run, style);
    /*
     * One shape per label, and the glyph array is the shell's to release (`RoboPaintRun.h`). A label nothing
     * could be shaped for -- text that decoded to nothing, a face whose tables will not parse -- is counted
     * unshaped, which is the honest answer: there is no ink, and the reason is the font rather than a
     * decision this file made not to ask for any.
     */
    if (PaintRunShape(style->face, text, length, &run, &glyphs, &count, NULL, NULL) != RoboOk) {
        glyphs = NULL;
        count = 0u;
    }
    if (count == 0u) {
        RoboFree(glyphs);
        if (outUnshaped) {
            (*outUnshaped)++;
        }
        return;
    }

    stats.drawn = 0u;
    stats.missing = 0u;
    stats.controls = 0u;
    (void)PaintRunDraw(raster, style->face, style->sizePx, glyphs, count,
                       (float)x, (float)baselinePx, color, &stats, NULL);
    RoboFree(glyphs);

    if (stats.drawn == 0u) {
        /* The run shaped and nothing reached the raster -- a setting the bridge refuses, or a raster that will
         * not take a glyph. Counted as *unshaped* rather than as drawn, because a caller asking "is there text
         * on my desktop" is asking about ink rather than about intent, and the alternative would be a label
         * counted as drawn with nothing to show for it. */
        if (outUnshaped) {
            (*outUnshaped)++;
        }
        return;
    }
    if (outDrawn) {
        (*outDrawn)++;
    }
}

/* ------------------------------------------------------------------ */
/* Fills                                                               */
/* ------------------------------------------------------------------ */

void DesktopChromeFill(GfxRaster *raster,
                              GfxRect rect,
                              GfxColorRgba8 color,
                              DesktopFrameRecord *record)
{
    GfxRect clip;
    GfxRect meeting;

    if (!raster || GfxRectIsEmpty(rect)) {
        return;
    }
    /* Clipped is counted, not assumed: a window partly off the screen is a legitimate state, and the count
     * is how a caller learns its placement is showing less than it asked for. The graphics raster's own
     * clip is what does the cutting -- this file only *notices*. */
    if (GfxRectIsValid(rect) && GfxRasterClip(raster, &clip) == RoboOk) {
        if (GfxRectIntersect(rect, clip, &meeting) != RoboOk ||
            !GfxRectEqual(meeting, rect)) {
            if (record) {
                record->clipped++;
            }
        }
    }
    if (GfxRasterFillRect(raster, rect, color) == RoboOk && record) {
        record->fills++;
    }
}

/* ------------------------------------------------------------------ */
/* Bounds                                                              */
/* ------------------------------------------------------------------ */

GfxRect DesktopChromeTitleBounds(const Desktop *desktop,
                                                const DesktopWindow *window)
{
    const DesktopWindowMetrics *m = &desktop->windowMetrics;

    return MakeGfxRect(window->bounds.x + m->border,
                         window->bounds.y + m->border,
                         window->bounds.width - 2 * m->border,
                         m->titleHeight);
}

GfxRect DesktopChromeClientBounds(const Desktop *desktop,
                                                 const DesktopWindow *window)
{
    const DesktopWindowMetrics *m = &desktop->windowMetrics;
    GfxRect title = DesktopChromeTitleBounds(desktop, window);

    return MakeGfxRect(window->bounds.x + m->border,
                         title.y + title.height + m->contentInset,
                         window->bounds.width - 2 * m->border,
                         window->bounds.height - 2 * m->border - m->titleHeight -
                             m->contentInset);
}

DesktopArea DesktopChromeResizeArea(const Desktop *desktop,
                                    const DesktopWindow *window,
                                    GfxPoint point)
{
    const DesktopWindowMetrics *m = &desktop->windowMetrics;
    GfxRect frame = window->bounds;
    int32_t bandX = m->resizeBand;
    int32_t bandY = m->resizeBand;
    int leftEdge;
    int rightEdge;
    int topEdge;
    int bottomEdge;

    if (m->resizeBand <= 0) {
        return RoboDesktopAreaNone;
    }
    if (!GfxRectContainsPoint(frame, point)) {
        return RoboDesktopAreaNone;
    }

    /*
     * Inside-ness first, then each edge band: a corner is where two bands *overlap*, so testing the
     * horizontal band and the vertical band separately and combining them answers the corner without four
     * more rectangles -- and answers it the same way at every band size.
     *
     * Each band is clamped to half its axis so a frame narrower than twice the band still has a middle:
     * without that, a band wider than the window would make every point on an axis an edge and the title bar
     * would stop being draggable. `DesktopWindowMetricsIsValid` refuses a band that wide for the *style's*
     * minimum window, and this clamp is what keeps a hand-built or transiently resized frame honest.
     */
    if (bandX * 2 > frame.width) {
        bandX = frame.width / 2;
    }
    if (bandY * 2 > frame.height) {
        bandY = frame.height / 2;
    }

    leftEdge = point.x < frame.x + bandX;
    rightEdge = point.x >= frame.x + frame.width - bandX;
    topEdge = point.y < frame.y + bandY;
    bottomEdge = point.y >= frame.y + frame.height - bandY;

    if (topEdge && leftEdge) {
        return RoboDesktopAreaWindowResizeTopLeft;
    }
    if (topEdge && rightEdge) {
        return RoboDesktopAreaWindowResizeTopRight;
    }
    if (bottomEdge && leftEdge) {
        return RoboDesktopAreaWindowResizeBottomLeft;
    }
    if (bottomEdge && rightEdge) {
        return RoboDesktopAreaWindowResizeBottomRight;
    }
    if (leftEdge) {
        return RoboDesktopAreaWindowResizeLeft;
    }
    if (rightEdge) {
        return RoboDesktopAreaWindowResizeRight;
    }
    if (topEdge) {
        return RoboDesktopAreaWindowResizeTop;
    }
    if (bottomEdge) {
        return RoboDesktopAreaWindowResizeBottom;
    }
    return RoboDesktopAreaNone;
}

GfxRect DesktopChromeCloseBounds(const Desktop *desktop,
                                                const DesktopWindow *window)
{
    const DesktopWindowMetrics *m = &desktop->windowMetrics;
    GfxRect title = DesktopChromeTitleBounds(desktop, window);

    return MakeGfxRect(window->bounds.x + window->bounds.width - m->border - m->closeInset -
                             m->closeSize,
                         title.y + (title.height - m->closeSize) / 2,
                         m->closeSize,
                         m->closeSize);
}

/* ------------------------------------------------------------------ */
/* The shell                                                           */
/* ------------------------------------------------------------------ */

Result DesktopChromePaintWallpaper(Desktop *desktop,
                                           GfxRaster *raster,
                                           DesktopFrameRecord *record,
                                           DesktopDiag *diag)
{
    const DesktopGeometry *geometry = &desktop->geometry;
    const DesktopTheme *theme = &desktop->theme;

    (void)diag;

    DesktopChromeFill(raster, geometry->screen, theme->desktopSurface, record);

    /* Falloff rows below the panel: a stepped soft edge rather than a blur. */
    if (geometry->workArea.y < geometry->screen.y + geometry->screen.height) {
        DesktopChromeFill(raster,
                                 MakeGfxRect(geometry->screen.x, geometry->workArea.y,
                                               geometry->screen.width, 1),
                                 theme->shadowNear, record);
        if (geometry->workArea.y + 1 < geometry->screen.y + geometry->screen.height) {
            DesktopChromeFill(raster,
                                     MakeGfxRect(geometry->screen.x, geometry->workArea.y + 1,
                                                   geometry->screen.width, 1),
                                     theme->shadowFar, record);
        }
    }
    return RoboOk;
}

Result DesktopChromePaintPanel(Desktop *desktop,
                                       GfxRaster *raster,
                                       DesktopFrameRecord *record,
                                       DesktopDiag *diag)
{
    const DesktopGeometry *geometry = &desktop->geometry;
    const DesktopTheme *theme = &desktop->theme;
    DesktopTextStyle identityStyle = DesktopChromeTextStyle(desktop, RoboTypographyRoleDesktopTitle);
    DesktopTextStyle metaStyle = DesktopChromeTextStyle(desktop, RoboTypographyRoleChromeLabel);
    DesktopTextStyle statusStyle = DesktopChromeTextStyle(desktop, RoboTypographyRoleChromeStatus);
    GfxRect panel = geometry->panel;
    GfxRect label;
    int32_t ox = panel.x;
    int32_t legacyPanelBaseline = panel.height - desktop->panel.baselineFromBottom;
    int32_t baseline;
    size_t identityLength;
    size_t applicationsLength;
    size_t statusLength;
    int32_t identityWidth;

    (void)diag;
    if (panel.width <= 0 || panel.height <= 0) {
        return RoboOk;
    }

    /* Panel-local fill: the compositor surface's origin is the panel's top-left. */
    DesktopChromeFill(raster, MakeGfxRect(0, 0, panel.width, panel.height),
                             theme->panelSurface, record);
    {
        int32_t separator = desktop->panel.separatorHeight;

        if (separator > 0) {
            DesktopChromeFill(raster,
                                     MakeGfxRect(0, panel.height - separator, panel.width,
                                                   separator),
                                     theme->panelSeparator, record);
        }
    }

    identityLength = DesktopStringLength(desktop->identity);
    applicationsLength = DesktopStringLength(desktop->applications);
    statusLength = DesktopStringLength(desktop->status);
    identityWidth = DesktopChromeLabelWidth(&identityStyle, desktop->identity, identityLength);

    {
        int32_t markHeight = desktop->panel.identityMarkHeight;
        int32_t markWidth = desktop->panel.identityMarkWidth;
        int32_t markY = (panel.height - markHeight) / 2;

        DesktopChromeFill(raster,
                                 MakeGfxRect(geometry->identity.x - ox, markY, markWidth,
                                               markHeight),
                                 theme->identityAccent, record);
    }

    baseline = DesktopTextBaseline(&identityStyle, 0, panel.height, legacyPanelBaseline);
    if (identityLength > 0u &&
        DesktopGeometryPanelArea(geometry, RoboDesktopPanelIdentity, identityWidth,
                                         &label) == RoboOk) {
        DesktopChromeLabel(
            &identityStyle, raster,
            (label.x - ox) + desktop->panel.identityMarkWidth + RoboDesktopIdentityGap,
            baseline, desktop->identity, identityLength, theme->identityText, &record->labels,
            &record->labelsUnshaped);
    }
    if (applicationsLength > 0u) {
        int32_t width = DesktopChromeLabelWidth(&metaStyle, desktop->applications,
                                                       applicationsLength);

        if (DesktopGeometryPanelArea(geometry, RoboDesktopPanelApplications, width,
                                             &label) == RoboOk) {
            baseline = DesktopTextBaseline(&metaStyle, 0, panel.height, legacyPanelBaseline);
            DesktopChromeLabel(&metaStyle, raster, label.x - ox, baseline,
                                      desktop->applications, applicationsLength,
                                      theme->panelMetaText, &record->labels,
                                      &record->labelsUnshaped);
        }
    }
    if (statusLength > 0u) {
        int32_t width = DesktopChromeLabelWidth(&statusStyle, desktop->status, statusLength);

        if (DesktopGeometryPanelArea(geometry, RoboDesktopPanelStatus, width, &label) ==
            RoboOk) {
            baseline = DesktopTextBaseline(&statusStyle, 0, panel.height, legacyPanelBaseline);
            DesktopChromeLabel(&statusStyle, raster, label.x - ox, baseline, desktop->status,
                                      statusLength, theme->panelMetaText, &record->labels,
                                      &record->labelsUnshaped);
        }
    }
    return RoboOk;
}

Result DesktopChromePaintShell(Desktop *desktop,
                                             GfxRaster *raster,
                                             DesktopFrameRecord *record,
                                             DesktopDiag *diag)
{
    Result status;

    status = DesktopChromePaintWallpaper(desktop, raster, record, diag);
    if (status != RoboOk) {
        return status;
    }
    /*
     * Direct paint draws the panel in screen space. Re-use the panel helper by
     * temporarily painting into the same raster with absolute coordinates: the
     * composed path uses a panel-local surface; this path offsets fills itself.
     */
    {
        const DesktopGeometry *geometry = &desktop->geometry;
        const DesktopTheme *theme = &desktop->theme;
        DesktopTextStyle identityStyle =
            DesktopChromeTextStyle(desktop, RoboTypographyRoleDesktopTitle);
        DesktopTextStyle metaStyle =
            DesktopChromeTextStyle(desktop, RoboTypographyRoleChromeLabel);
        DesktopTextStyle statusStyle =
            DesktopChromeTextStyle(desktop, RoboTypographyRoleChromeStatus);
        GfxRect label;
        int32_t legacyPanelBaseline =
            geometry->panel.y + geometry->panel.height - desktop->panel.baselineFromBottom;
        int32_t baseline;
        size_t identityLength = DesktopStringLength(desktop->identity);
        size_t applicationsLength = DesktopStringLength(desktop->applications);
        size_t statusLength = DesktopStringLength(desktop->status);
        int32_t identityWidth =
            DesktopChromeLabelWidth(&identityStyle, desktop->identity, identityLength);

        DesktopChromeFill(raster, geometry->panel, theme->panelSurface, record);
        {
            int32_t separator = desktop->panel.separatorHeight;

            if (separator > 0) {
                DesktopChromeFill(
                    raster,
                    MakeGfxRect(geometry->panel.x,
                                  geometry->panel.y + geometry->panel.height - separator,
                                  geometry->panel.width, separator),
                    theme->panelSeparator, record);
            }
        }
        {
            int32_t markHeight = desktop->panel.identityMarkHeight;
            int32_t markWidth = desktop->panel.identityMarkWidth;
            int32_t markY = geometry->panel.y + (geometry->panel.height - markHeight) / 2;

            DesktopChromeFill(raster,
                                     MakeGfxRect(geometry->identity.x, markY, markWidth,
                                                   markHeight),
                                     theme->identityAccent, record);
        }
        baseline = DesktopTextBaseline(&identityStyle, geometry->panel.y, geometry->panel.height,
                                       legacyPanelBaseline);
        if (identityLength > 0u &&
            DesktopGeometryPanelArea(geometry, RoboDesktopPanelIdentity, identityWidth,
                                             &label) == RoboOk) {
            DesktopChromeLabel(
                &identityStyle, raster,
                label.x + desktop->panel.identityMarkWidth + RoboDesktopIdentityGap, baseline,
                desktop->identity, identityLength, theme->identityText, &record->labels,
                &record->labelsUnshaped);
        }
        if (applicationsLength > 0u) {
            int32_t width = DesktopChromeLabelWidth(&metaStyle, desktop->applications,
                                                           applicationsLength);

            if (DesktopGeometryPanelArea(geometry, RoboDesktopPanelApplications, width,
                                                 &label) == RoboOk) {
                baseline = DesktopTextBaseline(&metaStyle, geometry->panel.y,
                                               geometry->panel.height, legacyPanelBaseline);
                DesktopChromeLabel(&metaStyle, raster, label.x, baseline,
                                          desktop->applications, applicationsLength,
                                          theme->panelMetaText, &record->labels,
                                          &record->labelsUnshaped);
            }
        }
        if (statusLength > 0u) {
            int32_t width =
                DesktopChromeLabelWidth(&statusStyle, desktop->status, statusLength);

            if (DesktopGeometryPanelArea(geometry, RoboDesktopPanelStatus, width, &label) ==
                RoboOk) {
                baseline = DesktopTextBaseline(&statusStyle, geometry->panel.y,
                                               geometry->panel.height, legacyPanelBaseline);
                DesktopChromeLabel(&statusStyle, raster, label.x, baseline, desktop->status,
                                          statusLength, theme->panelMetaText, &record->labels,
                                          &record->labelsUnshaped);
            }
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* A window                                                            */
/* ------------------------------------------------------------------ */

Result DesktopChromePaintWindowFrame(Desktop *desktop,
                                                    const DesktopWindow *window,
                                                    int focused,
                                                    GfxRaster *raster,
                                                    DesktopFrameRecord *record,
                                                    DesktopDiag *diag)
{
    const DesktopTheme *theme = &desktop->theme;
    GfxRect title = DesktopChromeTitleBounds(desktop, window);
    GfxRect client = DesktopChromeClientBounds(desktop, window);

    (void)diag;

    /*
     * The border is the *whole* outer rectangle, painted first and then covered by the title bar and the
     * client area -- which is why a one-pixel border reads as a ring without four separate side fills, and
     * why a zero-pixel border (an allowed restyle) simply has nothing to show.
     */
    DesktopChromeFill(raster, window->bounds,
                             focused ? theme->windowBorderFocused : theme->windowBorder, record);
    DesktopChromeFill(raster, title, theme->windowTitleSurface, record);
    /* The body, so that a window whose contents could not be rasterised is still a window rather than a
     * hole in the screen. */
    DesktopChromeFill(raster, client, theme->windowSurface, record);
    return RoboOk;
}

void DesktopChromePaintWindowTitle(Desktop *desktop,
                                           const DesktopWindow *window,
                                           GfxRaster *raster,
                                           DesktopFrameRecord *record)
{
    GfxRect title = DesktopChromeTitleBounds(desktop, window);
    DesktopTextStyle style = DesktopChromeTextStyle(desktop, RoboTypographyRoleWindowTitle);
    size_t length = DesktopStringLength(window->title);

    if (length == 0u) {
        return;
    }
    DesktopChromeLabel(&style, raster, title.x + desktop->windowMetrics.titleInset,
                              DesktopTextBaseline(&style, title.y, title.height,
                                                  title.y + (title.height / 2) +
                                                      RoboDesktopCapHalf),
                              window->title, length, desktop->theme.windowTitleText,
                              &record->labels, &record->labelsUnshaped);
}

void DesktopChromePaintCloseButton(Desktop *desktop,
                                           const DesktopWindow *window,
                                           GfxRaster *raster,
                                           DesktopFrameRecord *record)
{
    GfxRect button = DesktopChromeCloseBounds(desktop, window);
    int32_t inset = RoboDesktopCloseCrossInset;

    if (GfxRectIsEmpty(button)) {
        return;
    }
    DesktopChromeFill(raster, button, desktop->theme.windowClose, record);

    /*
     * The cross is drawn as two strokes rather than as a glyph, and that is the one place this build
     * deliberately does not need a font: a close button that vanished on a face-less desktop would be the
     * one control a person looks for first. Both strokes are counted in `fills` (see the file comment).
     */
    if (button.width > 2 * inset && button.height > 2 * inset) {
        GfxPoint a;
        GfxPoint b;

        a = MakeGfxPoint(button.x + inset, button.y + inset);
        b = MakeGfxPoint(button.x + button.width - 1 - inset,
                           button.y + button.height - 1 - inset);
        if (GfxRasterDrawLine(raster, a, b, desktop->theme.windowCloseMark) == RoboOk &&
            record) {
            record->fills++;
        }
        a = MakeGfxPoint(button.x + button.width - 1 - inset, button.y + inset);
        b = MakeGfxPoint(button.x + inset, button.y + button.height - 1 - inset);
        if (GfxRasterDrawLine(raster, a, b, desktop->theme.windowCloseMark) == RoboOk &&
            record) {
            record->fills++;
        }
    }
}

void DesktopChromePaintCursor(Desktop *desktop,
                                     GfxRaster *raster,
                                     DesktopFrameRecord *record)
{
    if (!desktop || !raster || !desktop->showCursor || !desktop->cursorVisible) {
        (void)record;
        return;
    }
    DesktopChromeDrawCursorAt(GfxRasterTarget(raster), desktop->pointer.point);
    (void)record;
}

GfxRect DesktopChromeCursorBounds(GfxPoint origin, GfxRect screen)
{
    GfxRect raw;
    GfxRect clipped;

    raw = MakeGfxRect(origin.x, origin.y, RoboDesktopCursorWidth, RoboDesktopCursorHeight);
    clipped = GfxRectEmpty();
    if (GfxRectClip(raw, screen, &clipped) != RoboOk) {
        return GfxRectEmpty();
    }
    return clipped;
}

void DesktopChromeDrawCursorAt(GfxSurface *surface, GfxPoint origin)
{
    static const uint8_t bits[16][11] = {
        {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0},
        {1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0},
        {1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0},
        {1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0},
        {1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0},
        {1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0},
        {1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0},
        {1, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0},
        {1, 2, 2, 1, 2, 1, 0, 0, 0, 0, 0},
        {1, 2, 1, 0, 1, 2, 1, 0, 0, 0, 0},
        {1, 1, 0, 0, 1, 2, 1, 0, 0, 0, 0},
        {1, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0},
        {0, 0, 0, 0, 0, 1, 2, 1, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0},
    };
    GfxColorRgba8 outline;
    GfxColorRgba8 fill;
    int row;
    int col;
    uint32_t sw;
    uint32_t sh;

    if (!surface) {
        return;
    }
    sw = GfxSurfaceWidth(surface);
    sh = GfxSurfaceHeight(surface);
    outline = GfxRgb8(29u, 29u, 31u);
    fill = GfxRgb8(255u, 255u, 255u);

    for (row = 0; row < RoboDesktopCursorHeight; row++) {
        for (col = 0; col < RoboDesktopCursorWidth; col++) {
            uint8_t cell = bits[row][col];
            int32_t x;
            int32_t y;

            if (cell == 0u) {
                continue;
            }
            x = origin.x + col;
            y = origin.y + row;
            if (x < 0 || y < 0 || (uint32_t)x >= sw || (uint32_t)y >= sh) {
                continue;
            }
            (void)GfxSurfaceSetPixel(surface, (uint32_t)x, (uint32_t)y,
                                            cell == 1u ? outline : fill);
        }
    }
}
