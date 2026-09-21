/*
 * RoboDesktop geometry and metrics (Phase 11.13) — see RoboDesktopGeometry.h.
 *
 * ## The numbers, and why each one is that number
 *
 * A panel is 30 pixels: two pixels of separator, twelve of identity mark, and enough room for a
 * capitalled 11px label to sit with six pixels above and below it. 30 is also the height at which a panel
 * stops competing with a window's title bar (24) -- a panel that was the same height as a title bar would
 * read as a third of a window rather than as a system band.
 *
 * A window is bordered by **one** pixel, its title bar is 24, and its close button is 10 square inset 6
 * from the frame -- so the button's centre is 11 pixels from the top-right corner of the window, which is
 * where a person's hand expects it. One pixel of border is the cheapest possible frame that still reads as
 * a frame; a 24-pixel title bar is one pixel taller than the panel's label band, which is what stops the
 * two from looking like the same element repeated.
 *
 * The minimum a window may be is the decorations' own minimum plus the smallest client area worth
 * compositing into, computed from the fields above rather than written as a literal: `2 * border + 8`
 * wide and `2 * border + titleHeight + 8` high, which at these metrics is **10 by 34** -- a title bar
 * with eight pixels of client area under it, and a border with eight beside it. That is a *refusal to draw
 * something smaller* rather than a refusal of the request -- `DesktopWindowAdd` grows a smaller
 * window instead of failing, because a caller asking for a 4x4 window is asking for something the shell
 * cannot express.
 *
 * ## The panel's thirds
 *
 * The panel's three labels are laid out on thirds of its width between the two insets: identity at the
 * left of the left third, applications centred in the whole width, status right-aligned in the right
 * third. Thirds rather than measured widths because the areas have to exist *before* a font does -- this
 * header has no font, and a geometry that could only be computed with one would make a face-less desktop
 * unlayoutable. `DesktopGeometryPanelArea` narrows an area to a measured label afterwards.
 */

#include "RoboDesktop/RoboDesktopGeometry.h"

#include "RoboDesktopInternal.h"

/* ------------------------------------------------------------------ */
/* Metrics                                                             */
/* ------------------------------------------------------------------ */

void DesktopPanelMetricsInit(DesktopPanelMetrics *metrics)
{
    if (!metrics) {
        return;
    }
    metrics->height = 30;
    metrics->identityInset = 14;
    metrics->identityMarkWidth = 3;
    metrics->identityMarkHeight = 12;
    metrics->labelInset = 16;
    metrics->separatorHeight = 1;
    /* Eleven rather than nine: a baseline sits half its cap height above the middle of its band, and an
     * eleven-pixel label's cap is eight, so `(30 + 8) / 2 = 19` from the top -- eleven from the bottom. */
    metrics->baselineFromBottom = 11;
}

int DesktopPanelMetricsIsValid(const DesktopPanelMetrics *metrics)
{
    if (!metrics) {
        return 0;
    }
    if (metrics->height <= 0 || metrics->height > (int32_t)RoboDesktopMaxDimension) {
        return 0;
    }
    /*
     * The insets and the mark have to leave a panel with something in the middle: two insets, the mark and
     * its gap must fit inside an eighth of the panel's *length* (which is a width the metrics cannot see),
     * so what is checkable here is that none of them is negative and that the mark fits the height.
     */
    if (metrics->identityInset < 0 || metrics->labelInset < 0) {
        return 0;
    }
    if (metrics->identityMarkWidth < 0 || metrics->identityMarkHeight < 0) {
        return 0;
    }
    if (metrics->identityMarkHeight > metrics->height) {
        return 0;
    }
    if (metrics->separatorHeight < 0 || metrics->separatorHeight >= metrics->height) {
        return 0;
    }
    if (metrics->baselineFromBottom <= 0 || metrics->baselineFromBottom >= metrics->height) {
        return 0;
    }
    return 1;
}

void DesktopWindowMetricsInit(DesktopWindowMetrics *metrics)
{
    if (!metrics) {
        return;
    }
    metrics->border = 1;
    metrics->titleHeight = 24;
    metrics->titleInset = 10;
    metrics->closeSize = 10;
    metrics->closeInset = 6;
    metrics->contentInset = 0;
    /*
     * The smallest window this build will draw: its own decorations plus eight pixels of client area on
     * each axis. Computed here from the fields above rather than written as a literal, so a caller that
     * restyles the decorations gets a consistent minimum for free -- which is the one thing a hard-coded
     * 42 would get wrong.
     */
    metrics->minWidth = 2 * metrics->border + 8;
    metrics->minHeight = 2 * metrics->border + metrics->titleHeight + 8;
}

int DesktopWindowMetricsIsValid(const DesktopWindowMetrics *metrics)
{
    if (!metrics) {
        return 0;
    }
    if (metrics->border < 0 || metrics->titleHeight < 0) {
        return 0;
    }
    if (metrics->closeSize < 0 || metrics->closeInset < 0) {
        return 0;
    }
    if (metrics->minWidth <= 0 || metrics->minHeight <= 0) {
        return 0;
    }
    if (metrics->minWidth > (int32_t)RoboDesktopMaxDimension ||
        metrics->minHeight > (int32_t)RoboDesktopMaxDimension) {
        return 0;
    }
    /* The close button must fit in the title bar's band, or it would be drawn over the contents. */
    if (metrics->closeSize > metrics->titleHeight) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

void DesktopGeometryInit(DesktopGeometry *geometry)
{
    if (!geometry) {
        return;
    }
    geometry->screen = GfxRectEmpty();
    geometry->panel = GfxRectEmpty();
    geometry->workArea = GfxRectEmpty();
    geometry->identity = GfxRectEmpty();
    geometry->applications = GfxRectEmpty();
    geometry->status = GfxRectEmpty();
}

Result DesktopGeometryCompute(GfxSize screen,
                                            const DesktopPanelMetrics *metrics,
                                            DesktopGeometry *geometry)
{
    int32_t workHeight;
    int32_t third;

    if (!geometry || !metrics) {
        return RoboInvalidArgument;
    }
    if (screen.width <= 0 || screen.height <= 0 ||
        screen.width > (int32_t)RoboDesktopMaxDimension ||
        screen.height > (int32_t)RoboDesktopMaxDimension) {
        return RoboInvalidArgument;
    }
    if (metrics->height <= 0 || metrics->height >= screen.height) {
        return RoboInvalidArgument;
    }

    DesktopGeometryInit(geometry);
    geometry->screen = MakeGfxRect(0, 0, screen.width, screen.height);
    geometry->panel = MakeGfxRect(0, 0, screen.width, metrics->height);
    /* Every rectangle below is inside the screen by construction: the work area starts where the panel
     * ends, so no caller can be handed a rectangle that leaves the surface it describes. */
    workHeight = screen.height - metrics->height;
    geometry->workArea = MakeGfxRect(0, metrics->height, screen.width, workHeight);

    /* The panel's thirds, between the two insets. */
    third = screen.width / 3;
    if (third < 0) {
        third = 0;
    }
    geometry->identity = MakeGfxRect(metrics->identityInset, 0,
                                       (third > metrics->identityInset)
                                           ? (third - metrics->identityInset)
                                           : 0,
                                       metrics->height);
    /* The applications label is centred on the *panel*, so its area is the whole band. */
    geometry->applications = geometry->panel;
    {
        int32_t right = screen.width - third - metrics->labelInset;

        if (right < 0) {
            right = 0;
        }
        geometry->status = MakeGfxRect(right, 0, screen.width - metrics->labelInset - right,
                                         metrics->height);
    }
    return RoboOk;
}

Result DesktopGeometryPanelArea(const DesktopGeometry *geometry,
                                               DesktopPanelArea area,
                                               int32_t textWidth,
                                               GfxRect *out)
{
    GfxRect band;

    if (!geometry || !out || textWidth < 0) {
        return RoboInvalidArgument;
    }
    switch (area) {
    case RoboDesktopPanelIdentity:
        band = geometry->identity;
        break;
    case RoboDesktopPanelApplications:
        band = geometry->applications;
        break;
    case RoboDesktopPanelStatus:
        band = geometry->status;
        break;
    default:
        return RoboInvalidArgument;
    }
    if (textWidth <= 0) {
        /* No measurement: the area stays as reserved, which is the face-less case. */
        *out = band;
        return RoboOk;
    }
    {
        int32_t width = (textWidth > band.width) ? band.width : textWidth;

        switch (area) {
        case RoboDesktopPanelIdentity:
            /* Left-aligned at the band's own left edge (the mark is drawn before it, and the painter
             * advances by the mark plus its gap). */
            *out = MakeGfxRect(band.x, band.y, width, band.height);
            break;
        case RoboDesktopPanelApplications:
            *out = MakeGfxRect(band.x + (band.width - width) / 2, band.y, width, band.height);
            break;
        default:
            /* Right-aligned inside the band, so a clock keeps a fixed distance from the screen's edge
             * whatever it says. */
            *out = MakeGfxRect(band.x + band.width - width, band.y, width, band.height);
            break;
        }
    }
    return RoboOk;
}

Result DesktopGeometryPlace(GfxSize frameSize,
                                          const GfxRect *workArea,
                                          GfxPoint *out)
{
    int32_t x;
    int32_t y;

    if (!workArea || !out) {
        return RoboInvalidArgument;
    }
    if (frameSize.width < 0 || frameSize.height < 0) {
        return RoboInvalidArgument;
    }
    x = workArea->x + (workArea->width - frameSize.width) / 2;
    /*
     * A third of the way down rather than centred: a window at the exact middle of a screen reads as
     * lower than it is, because the space below it carries the eye. The third is integer division on the
     * *remaining* space, so a window taller than the work area lands at the work area's top edge instead
     * of above it.
     */
    y = workArea->y + (workArea->height - frameSize.height) / 3;
    if (frameSize.width > workArea->width || x < workArea->x) {
        x = workArea->x;
    }
    if (frameSize.height > workArea->height || y < workArea->y) {
        y = workArea->y;
    }
    out->x = x;
    out->y = y;
    return RoboOk;
}

Result DesktopGeometryContain(GfxRect frame,
                                            const GfxRect *workArea,
                                            GfxRect *out)
{
    GfxRect result;

    if (!workArea || !out) {
        return RoboInvalidArgument;
    }
    if (!GfxRectIsValid(frame)) {
        return RoboInvalidArgument;
    }

    result = frame;
    /* An empty work area contains nothing; the answer is the empty rectangle rather than a window
     * pretending to be somewhere. */
    if (GfxRectIsEmpty(*workArea)) {
        *out = GfxRectEmpty();
        return RoboOk;
    }

    /* Too big: shrink to the work area, which is the largest a window may be on this screen. */
    if (result.width > workArea->width) {
        result.width = workArea->width;
    }
    if (result.height > workArea->height) {
        result.height = workArea->height;
    }
    /* Then move it inside. Shrinking first is what makes the two repairs order-independent: a window that
     * was both too large and too far right ends up at the work area's edge rather than outside it. */
    if (result.x < workArea->x) {
        result.x = workArea->x;
    }
    if (result.y < workArea->y) {
        result.y = workArea->y;
    }
    if (result.x + result.width > workArea->x + workArea->width) {
        result.x = workArea->x + workArea->width - result.width;
    }
    if (result.y + result.height > workArea->y + workArea->height) {
        result.y = workArea->y + workArea->height - result.height;
    }
    *out = result;
    return RoboOk;
}
