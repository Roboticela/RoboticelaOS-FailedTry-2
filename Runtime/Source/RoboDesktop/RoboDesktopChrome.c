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
 * ## Why the labels are drawn one glyph at a time
 *
 * Because that is what the pipeline below does, and a shell that measured its own text would be a second
 * font-metrics implementation that would disagree with the first the day a face changed. The three calls
 * per character are the same three `RoboPaintDecoration.c` makes: the face's cmap resolves a code point to
 * a glyph, the graphics bridge scales that glyph's advance and outline, and the raster strokes the
 * outline. The pen is accumulated in `float` and the *drawn* origin is snapped by the graphics module's own
 * policy, so a shell label and a document's text are placed by one rule.
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

#include "RoboFont/RoboFontMetrics.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsText.h"
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

uint32_t DesktopChromeUtf8Next(const char *text, size_t length, size_t *offset)
{
    const uint8_t *bytes = (const uint8_t *)text;
    size_t at = offset ? *offset : 0u;
    uint32_t codePoint = 0xFFFDu;
    unsigned need = 0u;
    unsigned index;

    if (!text || at >= length) {
        if (offset) {
            *offset = length;
        }
        return 0xFFFDu;
    }

    if (bytes[at] < 0x80u) {
        codePoint = bytes[at];
        if (offset) {
            *offset = at + 1u;
        }
        return codePoint;
    } else if ((bytes[at] & 0xE0u) == 0xC0u) {
        codePoint = (uint32_t)(bytes[at] & 0x1Fu);
        need = 1u;
    } else if ((bytes[at] & 0xF0u) == 0xE0u) {
        codePoint = (uint32_t)(bytes[at] & 0x0Fu);
        need = 2u;
    } else if ((bytes[at] & 0xF8u) == 0xF0u) {
        codePoint = (uint32_t)(bytes[at] & 0x07u);
        need = 3u;
    } else {
        /* A continuation byte or an over-long lead: one replacement character, one byte consumed, which
         * is the recovery every decoder in this tree uses. */
        if (offset) {
            *offset = at + 1u;
        }
        return 0xFFFDu;
    }

    for (index = 1u; index <= need; index++) {
        if (at + index >= length || (bytes[at + index] & 0xC0u) != 0x80u) {
            if (offset) {
                *offset = at + 1u;
            }
            return 0xFFFDu;
        }
        codePoint = (codePoint << 6) | (uint32_t)(bytes[at + index] & 0x3Fu);
    }
    if (offset) {
        *offset = at + need + 1u;
    }
    return codePoint;
}

/** The glyph a code point maps to on @p face, or `RoboGlyphIdNone`. */
static GlyphId DesktopGlyphFor(FontFace *face, uint32_t codePoint)
{
    FontCmapLookupResult lookup;

    if (!face) {
        return RoboGlyphIdNone;
    }
    if (FontFaceLookupGlyph(face, codePoint, &lookup) != RoboOk) {
        return RoboGlyphIdNone;
    }
    return lookup.glyphId;
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
    size_t offset = 0u;
    float width = 0.0f;

    if (!style || !text || length == 0u || !style->face || style->sizePx <= 0.0f) {
        return 0;
    }
    while (offset < length) {
        GfxGlyphMetricsDevice metrics;
        uint32_t codePoint = DesktopChromeUtf8Next(text, length, &offset);
        GlyphId glyph = DesktopGlyphFor(style->face, codePoint);

        if (glyph == RoboGlyphIdNone) {
            /* `.notdef` is still *space*: a label whose glyph is missing advances by the font's own
             * notdef width rather than collapsing, which is what keeps a title's box the width the
             * document's font would have given it. */
            width += style->sizePx * 0.5f;
            continue;
        }
        if (GetGfxGlyphMetricsDevice(style->face, glyph, style->sizePx, &metrics) == RoboOk) {
            width += metrics.advanceWidth;
        } else {
            width += style->sizePx * 0.5f;
        }
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
    size_t offset = 0u;
    float pen;

    /*
     * The two counters are **accumulators**, not outputs: a caller passes the frame record's own fields and
     * each label adds one to exactly one of them on its way through. That is why nothing is zeroed here.
     * Counting per *glyph* instead was the first version of this function and it was wrong for the thing the
     * record is read for: a caller asking "did my desktop draw its labels?" needs the number of labels, and
     * `labels` holding the last label's glyph count would answer a different question -- and would have
     * clobbered the two labels before it.
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

    pen = (float)x;
    while (offset < length) {
        GfxGlyphDraw draw;
        GfxGlyphMetricsDevice metrics;
        uint32_t codePoint = DesktopChromeUtf8Next(text, length, &offset);
        GlyphId glyph = DesktopGlyphFor(style->face, codePoint);

        GfxGlyphDrawInit(&draw);
        draw.face = style->face;
        draw.glyphId = glyph;
        draw.sizePx = style->sizePx;
        draw.color = color;
        /*
         * **Coverage antialiasing** (Phase 12.1), named here rather than inherited: the shell's
         * own labels are UI text, and `OUTLINE_STROKE` -- the bridge's default -- traces a glyph's
         * contour with one-pixel lines instead of filling it.
         *
         * The origin is passed at the pen's own fraction, and that is a change of posture rather
         * than of arithmetic: the coverage rasterizer quantizes to a 1/256 pixel internally, so
         * rounding here first would throw away the sub-pixel precision the new path exists to
         * keep -- and a long label's glyphs would drift by up to half a pixel each time the pen's
         * fraction crossed a rounding boundary. `GfxGlyphSnapPoint` remains the module's policy
         * for callers that want whole-pixel placement; this label no longer needs it.
         */
        draw.mode = RoboGfxGlyphModeAlpha;
        draw.origin = MakeGfxPointF(pen, (float)baselinePx);
        (void)GfxRasterDrawGlyph(raster, &draw);
        if (GetGfxGlyphMetricsDevice(style->face, glyph, style->sizePx, &metrics) == RoboOk) {
            pen += metrics.advanceWidth;
        } else {
            pen += style->sizePx * 0.5f;
        }
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

Result DesktopChromePaintShell(Desktop *desktop,
                                             GfxRaster *raster,
                                             DesktopFrameRecord *record,
                                             DesktopDiag *diag)
{
    const DesktopGeometry *geometry = &desktop->geometry;
    const DesktopTheme *theme = &desktop->theme;
    /*
     * The four surfaces' roles (Phase 12.3), resolved once per frame rather than once per label: the
     * resolver caches, so this is four lookups and the frame's own cost is unchanged -- and resolving
     * here rather than inside the label walk is what lets the *measurement* and the *drawing* of one
     * label provably read one style.
     */
    DesktopTextStyle identityStyle = DesktopChromeTextStyle(desktop, RoboTypographyRoleDesktopTitle);
    DesktopTextStyle metaStyle = DesktopChromeTextStyle(desktop, RoboTypographyRoleChromeLabel);
    DesktopTextStyle statusStyle = DesktopChromeTextStyle(desktop, RoboTypographyRoleChromeStatus);
    GfxRect label;
    /* The pre-12.3 placement, kept verbatim for a caller that supplies a single face. */
    int32_t legacyPanelBaseline =
        geometry->panel.y + geometry->panel.height - desktop->panel.baselineFromBottom;
    int32_t baseline;
    size_t identityLength;
    size_t applicationsLength;
    size_t statusLength;
    int32_t identityWidth;

    (void)diag;

    /* 1. The desktop surface: the frame's explicit baseline, over the whole screen. */
    DesktopChromeFill(raster, geometry->screen, theme->desktopSurface, record);

    /* 2. The panel, and its last row as the hairline. */
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

    /* 3. The falloff rows below the panel: a stepped soft edge rather than a blur (see the theme). */
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

    identityLength = DesktopStringLength(desktop->identity);
    applicationsLength = DesktopStringLength(desktop->applications);
    statusLength = DesktopStringLength(desktop->status);
    identityWidth = DesktopChromeLabelWidth(&identityStyle, desktop->identity, identityLength);

    /* 4. The identity mark: the shell's one accent, always drawn -- which is what keeps a face-less
     * desktop looking like a desktop rather than like a blank panel. */
    {
        int32_t markHeight = desktop->panel.identityMarkHeight;
        int32_t markWidth = desktop->panel.identityMarkWidth;
        int32_t markY = geometry->panel.y + (geometry->panel.height - markHeight) / 2;

        DesktopChromeFill(raster,
                                 MakeGfxRect(geometry->identity.x, markY, markWidth,
                                               markHeight),
                                 theme->identityAccent, record);
    }

    /* 5. The three labels, each narrowed into its own area by its measured width. */
    baseline = DesktopTextBaseline(&identityStyle, geometry->panel.y, geometry->panel.height,
                                   legacyPanelBaseline);
    if (identityLength > 0u &&
        DesktopGeometryPanelArea(geometry, RoboDesktopPanelIdentity, identityWidth,
                                         &label) == RoboOk) {
        DesktopChromeLabel(
            &identityStyle, raster,
            label.x + desktop->panel.identityMarkWidth + RoboDesktopIdentityGap, baseline,
            desktop->identity, identityLength, theme->identityText,
            &record->labels, &record->labelsUnshaped);
    }
    if (applicationsLength > 0u) {
        int32_t width = DesktopChromeLabelWidth(&metaStyle, desktop->applications,
                                                       applicationsLength);

        if (DesktopGeometryPanelArea(geometry, RoboDesktopPanelApplications, width,
                                             &label) == RoboOk) {
            baseline = DesktopTextBaseline(&metaStyle, geometry->panel.y,
                                           geometry->panel.height, legacyPanelBaseline);
            DesktopChromeLabel(&metaStyle, raster, label.x, baseline, desktop->applications,
                                      applicationsLength, theme->panelMetaText,
                                      &record->labels, &record->labelsUnshaped);
        }
    }
    if (statusLength > 0u) {
        int32_t width = DesktopChromeLabelWidth(&statusStyle, desktop->status, statusLength);

        if (DesktopGeometryPanelArea(geometry, RoboDesktopPanelStatus, width, &label) ==
            RoboOk) {
            baseline = DesktopTextBaseline(&statusStyle, geometry->panel.y,
                                           geometry->panel.height, legacyPanelBaseline);
            DesktopChromeLabel(&statusStyle, raster, label.x, baseline, desktop->status,
                                      statusLength, theme->panelMetaText,
                                      &record->labels, &record->labelsUnshaped);
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
    int32_t originX;
    int32_t originY;
    int row;
    int col;

    if (!desktop || !raster || !desktop->showCursor || !desktop->cursorVisible) {
        (void)record;
        return;
    }

    outline = GfxRgb8(29u, 29u, 31u);
    fill = GfxRgb8(255u, 255u, 255u);
    originX = desktop->pointer.point.x;
    originY = desktop->pointer.point.y;

    for (row = 0; row < 16; row++) {
        for (col = 0; col < 11; col++) {
            uint8_t cell = bits[row][col];
            GfxRect pixel;

            if (cell == 0u) {
                continue;
            }
            pixel = MakeGfxRect(originX + col, originY + row, 1, 1);
            /* The cursor is input chrome, not part of the shell's counted fill record. */
            (void)GfxRasterFillRect(raster, pixel, cell == 1u ? outline : fill);
        }
    }
    (void)record;
}
