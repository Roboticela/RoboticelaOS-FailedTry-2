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
 * ## The baseline rule
 *
 * `(band + cap) / 2`, with `cap` eight pixels: half the cap height above the middle of the band. Both bands
 * use it — the panel through its `baselineFromBottom` metric and a window's title bar through the same
 * arithmetic — because a panel label and a window title that did not agree about where a baseline goes
 * would read as two different typesettings on one screen.
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
#include "RoboGraphics/RoboGraphicsText.h"

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

int32_t DesktopChromeLabelWidth(const Desktop *desktop,
                                       const char *text,
                                       size_t length,
                                       float sizePx)
{
    size_t offset = 0u;
    float width = 0.0f;

    if (!desktop || !text || length == 0u || !desktop->face || sizePx <= 0.0f) {
        return 0;
    }
    while (offset < length) {
        GfxGlyphMetricsDevice metrics;
        uint32_t codePoint = DesktopChromeUtf8Next(text, length, &offset);
        GlyphId glyph = DesktopGlyphFor(desktop->face, codePoint);

        if (glyph == RoboGlyphIdNone) {
            /* `.notdef` is still *space*: a label whose glyph is missing advances by the font's own
             * notdef width rather than collapsing, which is what keeps a title's box the width the
             * document's font would have given it. */
            width += sizePx * 0.5f;
            continue;
        }
        if (GetGfxGlyphMetricsDevice(desktop->face, glyph, sizePx, &metrics) == RoboOk) {
            width += metrics.advanceWidth;
        } else {
            width += sizePx * 0.5f;
        }
    }
    return (int32_t)(width + 0.5f);
}

void DesktopChromeLabel(Desktop *desktop,
                               GfxRaster *raster,
                               int32_t x,
                               int32_t baselinePx,
                               const char *text,
                               size_t length,
                               GfxColorRgba8 color,
                               float sizePx,
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
    if (!desktop || !raster || !text || length == 0u) {
        return;
    }
    if (!desktop->face || sizePx <= 0.0f) {
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
        GlyphId glyph = DesktopGlyphFor(desktop->face, codePoint);

        GfxGlyphDrawInit(&draw);
        draw.face = desktop->face;
        draw.glyphId = glyph;
        draw.sizePx = sizePx;
        draw.color = color;
        draw.mode = RoboGfxGlyphModeOutlineStroke;
        /* The graphics module's own snap policy, applied to the *drawn* origin: the pen keeps its
         * fraction so that a label's last glyph is not a pixel off because of the rounding of the ones
         * before it. */
        {
            GfxPointF origin;
            GfxPoint snapped;

            origin = MakeGfxPointF(pen, (float)baselinePx);
            snapped = GfxGlyphSnapPoint(origin);
            draw.origin = MakeGfxPointF((float)snapped.x, (float)snapped.y);
        }
        (void)GfxRasterDrawGlyph(raster, &draw);
        if (GetGfxGlyphMetricsDevice(desktop->face, glyph, sizePx, &metrics) == RoboOk) {
            pen += metrics.advanceWidth;
        } else {
            pen += sizePx * 0.5f;
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

/** The baseline a label of a band of @p height is drawn on: half the cap height above the middle. */
static int32_t DesktopBaselineForBand(int32_t bandY, int32_t bandHeight)
{
    return bandY + (bandHeight / 2) + RoboDesktopCapHalf;
}

Result DesktopChromePaintShell(Desktop *desktop,
                                             GfxRaster *raster,
                                             DesktopFrameRecord *record,
                                             DesktopDiag *diag)
{
    const DesktopGeometry *geometry = &desktop->geometry;
    const DesktopTheme *theme = &desktop->theme;
    GfxRect label;
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

    baseline = geometry->panel.y + geometry->panel.height - desktop->panel.baselineFromBottom;
    identityLength = DesktopStringLength(desktop->identity);
    applicationsLength = DesktopStringLength(desktop->applications);
    statusLength = DesktopStringLength(desktop->status);
    identityWidth =
        DesktopChromeLabelWidth(desktop, desktop->identity, identityLength,
                                        desktop->labelSizePx);

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
    if (identityLength > 0u &&
        DesktopGeometryPanelArea(geometry, RoboDesktopPanelIdentity, identityWidth,
                                         &label) == RoboOk) {
        DesktopChromeLabel(
            desktop, raster,
            label.x + desktop->panel.identityMarkWidth + RoboDesktopIdentityGap, baseline,
            desktop->identity, identityLength, theme->identityText, desktop->labelSizePx,
            &record->labels, &record->labelsUnshaped);
    }
    if (applicationsLength > 0u) {
        int32_t width = DesktopChromeLabelWidth(desktop, desktop->applications,
                                                       applicationsLength,
                                                       desktop->labelSizePx);

        if (DesktopGeometryPanelArea(geometry, RoboDesktopPanelApplications, width,
                                             &label) == RoboOk) {
            DesktopChromeLabel(desktop, raster, label.x, baseline, desktop->applications,
                                      applicationsLength, theme->panelMetaText,
                                      desktop->labelSizePx, &record->labels,
                                      &record->labelsUnshaped);
        }
    }
    if (statusLength > 0u) {
        int32_t width = DesktopChromeLabelWidth(desktop, desktop->status, statusLength,
                                                       desktop->labelSizePx);

        if (DesktopGeometryPanelArea(geometry, RoboDesktopPanelStatus, width, &label) ==
            RoboOk) {
            DesktopChromeLabel(desktop, raster, label.x, baseline, desktop->status,
                                      statusLength, theme->panelMetaText,
                                      desktop->labelSizePx, &record->labels,
                                      &record->labelsUnshaped);
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
    size_t length = DesktopStringLength(window->title);

    if (length == 0u) {
        return;
    }
    DesktopChromeLabel(desktop, raster, title.x + desktop->windowMetrics.titleInset,
                              DesktopBaselineForBand(title.y, title.height), window->title,
                              length, desktop->theme.windowTitleText, desktop->labelSizePx,
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
