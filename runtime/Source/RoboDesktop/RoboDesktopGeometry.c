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
 * wide and `2 * border + title_height + 8` high, which at these metrics is **10 by 34** -- a title bar
 * with eight pixels of client area under it, and a border with eight beside it. That is a *refusal to draw
 * something smaller* rather than a refusal of the request -- `robo_desktop_window_add` grows a smaller
 * window instead of failing, because a caller asking for a 4x4 window is asking for something the shell
 * cannot express.
 *
 * ## The panel's thirds
 *
 * The panel's three labels are laid out on thirds of its width between the two insets: identity at the
 * left of the left third, applications centred in the whole width, status right-aligned in the right
 * third. Thirds rather than measured widths because the areas have to exist *before* a font does -- this
 * header has no font, and a geometry that could only be computed with one would make a face-less desktop
 * unlayoutable. `robo_desktop_geometry_panel_area` narrows an area to a measured label afterwards.
 */

#include "RoboDesktop/RoboDesktopGeometry.h"

#include "RoboDesktopInternal.h"

/* ------------------------------------------------------------------ */
/* Metrics                                                             */
/* ------------------------------------------------------------------ */

void robo_desktop_panel_metrics_init(robo_desktop_panel_metrics_t *metrics)
{
    if (!metrics) {
        return;
    }
    metrics->height = 30;
    metrics->identity_inset = 14;
    metrics->identity_mark_width = 3;
    metrics->identity_mark_height = 12;
    metrics->label_inset = 16;
    metrics->separator_height = 1;
    /* Eleven rather than nine: a baseline sits half its cap height above the middle of its band, and an
     * eleven-pixel label's cap is eight, so `(30 + 8) / 2 = 19` from the top -- eleven from the bottom. */
    metrics->baseline_from_bottom = 11;
}

int robo_desktop_panel_metrics_is_valid(const robo_desktop_panel_metrics_t *metrics)
{
    if (!metrics) {
        return 0;
    }
    if (metrics->height <= 0 || metrics->height > (int32_t)ROBO_DESKTOP_MAX_DIMENSION) {
        return 0;
    }
    /*
     * The insets and the mark have to leave a panel with something in the middle: two insets, the mark and
     * its gap must fit inside an eighth of the panel's *length* (which is a width the metrics cannot see),
     * so what is checkable here is that none of them is negative and that the mark fits the height.
     */
    if (metrics->identity_inset < 0 || metrics->label_inset < 0) {
        return 0;
    }
    if (metrics->identity_mark_width < 0 || metrics->identity_mark_height < 0) {
        return 0;
    }
    if (metrics->identity_mark_height > metrics->height) {
        return 0;
    }
    if (metrics->separator_height < 0 || metrics->separator_height >= metrics->height) {
        return 0;
    }
    if (metrics->baseline_from_bottom <= 0 || metrics->baseline_from_bottom >= metrics->height) {
        return 0;
    }
    return 1;
}

void robo_desktop_window_metrics_init(robo_desktop_window_metrics_t *metrics)
{
    if (!metrics) {
        return;
    }
    metrics->border = 1;
    metrics->title_height = 24;
    metrics->title_inset = 10;
    metrics->close_size = 10;
    metrics->close_inset = 6;
    metrics->content_inset = 0;
    /*
     * The smallest window this build will draw: its own decorations plus eight pixels of client area on
     * each axis. Computed here from the fields above rather than written as a literal, so a caller that
     * restyles the decorations gets a consistent minimum for free -- which is the one thing a hard-coded
     * 42 would get wrong.
     */
    metrics->min_width = 2 * metrics->border + 8;
    metrics->min_height = 2 * metrics->border + metrics->title_height + 8;
}

int robo_desktop_window_metrics_is_valid(const robo_desktop_window_metrics_t *metrics)
{
    if (!metrics) {
        return 0;
    }
    if (metrics->border < 0 || metrics->title_height < 0) {
        return 0;
    }
    if (metrics->close_size < 0 || metrics->close_inset < 0) {
        return 0;
    }
    if (metrics->min_width <= 0 || metrics->min_height <= 0) {
        return 0;
    }
    if (metrics->min_width > (int32_t)ROBO_DESKTOP_MAX_DIMENSION ||
        metrics->min_height > (int32_t)ROBO_DESKTOP_MAX_DIMENSION) {
        return 0;
    }
    /* The close button must fit in the title bar's band, or it would be drawn over the contents. */
    if (metrics->close_size > metrics->title_height) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

void robo_desktop_geometry_init(robo_desktop_geometry_t *geometry)
{
    if (!geometry) {
        return;
    }
    geometry->screen = robo_gfx_rect_empty();
    geometry->panel = robo_gfx_rect_empty();
    geometry->work_area = robo_gfx_rect_empty();
    geometry->identity = robo_gfx_rect_empty();
    geometry->applications = robo_gfx_rect_empty();
    geometry->status = robo_gfx_rect_empty();
}

robo_result_t robo_desktop_geometry_compute(robo_gfx_size_t screen,
                                            const robo_desktop_panel_metrics_t *metrics,
                                            robo_desktop_geometry_t *geometry)
{
    int32_t work_height;
    int32_t third;

    if (!geometry || !metrics) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (screen.width <= 0 || screen.height <= 0 ||
        screen.width > (int32_t)ROBO_DESKTOP_MAX_DIMENSION ||
        screen.height > (int32_t)ROBO_DESKTOP_MAX_DIMENSION) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (metrics->height <= 0 || metrics->height >= screen.height) {
        return ROBO_INVALID_ARGUMENT;
    }

    robo_desktop_geometry_init(geometry);
    geometry->screen = robo_gfx_rect(0, 0, screen.width, screen.height);
    geometry->panel = robo_gfx_rect(0, 0, screen.width, metrics->height);
    /* Every rectangle below is inside the screen by construction: the work area starts where the panel
     * ends, so no caller can be handed a rectangle that leaves the surface it describes. */
    work_height = screen.height - metrics->height;
    geometry->work_area = robo_gfx_rect(0, metrics->height, screen.width, work_height);

    /* The panel's thirds, between the two insets. */
    third = screen.width / 3;
    if (third < 0) {
        third = 0;
    }
    geometry->identity = robo_gfx_rect(metrics->identity_inset, 0,
                                       (third > metrics->identity_inset)
                                           ? (third - metrics->identity_inset)
                                           : 0,
                                       metrics->height);
    /* The applications label is centred on the *panel*, so its area is the whole band. */
    geometry->applications = geometry->panel;
    {
        int32_t right = screen.width - third - metrics->label_inset;

        if (right < 0) {
            right = 0;
        }
        geometry->status = robo_gfx_rect(right, 0, screen.width - metrics->label_inset - right,
                                         metrics->height);
    }
    return ROBO_OK;
}

robo_result_t robo_desktop_geometry_panel_area(const robo_desktop_geometry_t *geometry,
                                               robo_desktop_panel_area_t area,
                                               int32_t text_width,
                                               robo_gfx_rect_t *out)
{
    robo_gfx_rect_t band;

    if (!geometry || !out || text_width < 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    switch (area) {
    case ROBO_DESKTOP_PANEL_IDENTITY:
        band = geometry->identity;
        break;
    case ROBO_DESKTOP_PANEL_APPLICATIONS:
        band = geometry->applications;
        break;
    case ROBO_DESKTOP_PANEL_STATUS:
        band = geometry->status;
        break;
    default:
        return ROBO_INVALID_ARGUMENT;
    }
    if (text_width <= 0) {
        /* No measurement: the area stays as reserved, which is the face-less case. */
        *out = band;
        return ROBO_OK;
    }
    {
        int32_t width = (text_width > band.width) ? band.width : text_width;

        switch (area) {
        case ROBO_DESKTOP_PANEL_IDENTITY:
            /* Left-aligned at the band's own left edge (the mark is drawn before it, and the painter
             * advances by the mark plus its gap). */
            *out = robo_gfx_rect(band.x, band.y, width, band.height);
            break;
        case ROBO_DESKTOP_PANEL_APPLICATIONS:
            *out = robo_gfx_rect(band.x + (band.width - width) / 2, band.y, width, band.height);
            break;
        default:
            /* Right-aligned inside the band, so a clock keeps a fixed distance from the screen's edge
             * whatever it says. */
            *out = robo_gfx_rect(band.x + band.width - width, band.y, width, band.height);
            break;
        }
    }
    return ROBO_OK;
}

robo_result_t robo_desktop_geometry_place(robo_gfx_size_t frame_size,
                                          const robo_gfx_rect_t *work_area,
                                          robo_gfx_point_t *out)
{
    int32_t x;
    int32_t y;

    if (!work_area || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (frame_size.width < 0 || frame_size.height < 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    x = work_area->x + (work_area->width - frame_size.width) / 2;
    /*
     * A third of the way down rather than centred: a window at the exact middle of a screen reads as
     * lower than it is, because the space below it carries the eye. The third is integer division on the
     * *remaining* space, so a window taller than the work area lands at the work area's top edge instead
     * of above it.
     */
    y = work_area->y + (work_area->height - frame_size.height) / 3;
    if (frame_size.width > work_area->width || x < work_area->x) {
        x = work_area->x;
    }
    if (frame_size.height > work_area->height || y < work_area->y) {
        y = work_area->y;
    }
    out->x = x;
    out->y = y;
    return ROBO_OK;
}

robo_result_t robo_desktop_geometry_contain(robo_gfx_rect_t frame,
                                            const robo_gfx_rect_t *work_area,
                                            robo_gfx_rect_t *out)
{
    robo_gfx_rect_t result;

    if (!work_area || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_gfx_rect_is_valid(frame)) {
        return ROBO_INVALID_ARGUMENT;
    }

    result = frame;
    /* An empty work area contains nothing; the answer is the empty rectangle rather than a window
     * pretending to be somewhere. */
    if (robo_gfx_rect_is_empty(*work_area)) {
        *out = robo_gfx_rect_empty();
        return ROBO_OK;
    }

    /* Too big: shrink to the work area, which is the largest a window may be on this screen. */
    if (result.width > work_area->width) {
        result.width = work_area->width;
    }
    if (result.height > work_area->height) {
        result.height = work_area->height;
    }
    /* Then move it inside. Shrinking first is what makes the two repairs order-independent: a window that
     * was both too large and too far right ends up at the work area's edge rather than outside it. */
    if (result.x < work_area->x) {
        result.x = work_area->x;
    }
    if (result.y < work_area->y) {
        result.y = work_area->y;
    }
    if (result.x + result.width > work_area->x + work_area->width) {
        result.x = work_area->x + work_area->width - result.width;
    }
    if (result.y + result.height > work_area->y + work_area->height) {
        result.y = work_area->y + work_area->height - result.height;
    }
    *out = result;
    return ROBO_OK;
}
