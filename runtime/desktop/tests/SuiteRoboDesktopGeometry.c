/*
 * RoboDesktop geometry suite (Phase 11.13) — `desktop.geometry.*`.
 *
 * Every case here is arithmetic on a size and a rectangle: no display, no window, no frame, no document.
 * That is the phase's §9 made checkable — "the desktop must know screen width, screen height, panel
 * bounds, work-area bounds; use explicit geometry; do not hard-code one resolution" — and the way a claim
 * like that becomes a fact rather than an intention is for the arithmetic to be asserted at three
 * different sizes, including one where the panel eats most of the screen and one where the work area is
 * wider than it is tall.
 *
 * The numbers are stated rather than derived from the constants on purpose. A case that computed its own
 * expectation from the metrics would pass with the metrics wrong; `desktop.geometry.metrics` is the case
 * that pins the metrics, and the rest pin what follows from them.
 */

#include "RoboDesktopTestSupport.h"

static robo_desktop_geometry_t geometry_of(int32_t width, int32_t height)
{
    robo_desktop_geometry_t geometry;
    robo_desktop_panel_metrics_t metrics = desktop_panel_metrics();

    ROBO_CHECK_EQ_INT(robo_desktop_geometry_compute(robo_gfx_size(width, height), &metrics, &geometry),
                      ROBO_OK);
    return geometry;
}

static int rect_is(robo_gfx_rect_t rect, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return rect.x == x && rect.y == y && rect.width == w && rect.height == h;
}

static void check_rect(robo_gfx_rect_t rect, int32_t x, int32_t y, int32_t w, int32_t h,
                       const char *what)
{
    if (!rect_is(rect, x, y, w, h)) {
        robo_test_fail(__FILE__, __LINE__, "%s: expected %d,%d %dx%d, got %d,%d %dx%d", what, (int)x,
                       (int)y, (int)w, (int)h, (int)rect.x, (int)rect.y, (int)rect.width,
                       (int)rect.height);
    }
}

/* ------------------------------------------------------------------ */
/* 1. The metrics                                                      */
/* ------------------------------------------------------------------ */

static void test_geometry_metrics(void)
{
    robo_desktop_panel_metrics_t panel = desktop_panel_metrics();
    robo_desktop_window_metrics_t window = desktop_window_metrics();
    robo_desktop_theme_t theme = desktop_theme();

    desktop_fixture();
    ROBO_CHECK_EQ_INT(robo_desktop_panel_metrics_is_valid(&panel), 1);
    ROBO_CHECK_EQ_INT(panel.height, 30);
    ROBO_CHECK(panel.identity_inset > 0 && panel.label_inset > 0);
    ROBO_CHECK_EQ_INT(panel.separator_height, 1);
    ROBO_CHECK(panel.baseline_from_bottom > 0 && panel.baseline_from_bottom < panel.height);

    ROBO_CHECK_EQ_INT(robo_desktop_window_metrics_is_valid(&window), 1);
    ROBO_CHECK_EQ_INT(window.border, 1);
    ROBO_CHECK_EQ_INT(window.title_height, 24);
    /* The minimum is the decorations plus client area, computed rather than literal. */
    ROBO_CHECK_EQ_INT(window.min_width, 2 * window.border + 8);
    ROBO_CHECK_EQ_INT(window.min_height, 2 * window.border + window.title_height + 8);

    ROBO_CHECK_EQ_INT(robo_desktop_theme_is_valid(&theme), 1);

    /* Refusals: a missing record, a panel with no height, a close button too tall for its bar. */
    ROBO_CHECK_EQ_INT(robo_desktop_panel_metrics_is_valid(NULL), 0);
    panel.height = 0;
    ROBO_CHECK_EQ_INT(robo_desktop_panel_metrics_is_valid(&panel), 0);
    panel = desktop_panel_metrics();
    panel.identity_inset = -1;
    ROBO_CHECK_EQ_INT(robo_desktop_panel_metrics_is_valid(&panel), 0);

    ROBO_CHECK_EQ_INT(robo_desktop_window_metrics_is_valid(NULL), 0);
    window.close_size = window.title_height + 1;
    ROBO_CHECK_EQ_INT(robo_desktop_window_metrics_is_valid(&window), 0);
    window = desktop_window_metrics();
    window.min_width = 0;
    ROBO_CHECK_EQ_INT(robo_desktop_window_metrics_is_valid(&window), 0);
}

/* ------------------------------------------------------------------ */
/* 2. The layout                                                       */
/* ------------------------------------------------------------------ */

static void test_geometry_compute(void)
{
    robo_desktop_geometry_t geometry = geometry_of(1280, 800);

    check_rect(geometry.screen, 0, 0, 1280, 800, "screen");
    check_rect(geometry.panel, 0, 0, 1280, 30, "panel");
    check_rect(geometry.work_area, 0, 30, 1280, 770, "work area");
    /* The panel's thirds, between the two insets: a third of the width, the identity band on the left and
     * the status band on the right. */
    check_rect(geometry.identity, 14, 0, 426 - 14, 30, "identity area");
    check_rect(geometry.applications, 0, 0, 1280, 30, "applications area");
    check_rect(geometry.status, 1280 - 426 - 16, 0, 426, 30, "status area");

    /* A second resolution, to prove nothing is hard-coded. */
    geometry = geometry_of(640, 480);
    check_rect(geometry.screen, 0, 0, 640, 480, "screen 640x480");
    check_rect(geometry.panel, 0, 0, 640, 30, "panel 640x480");
    check_rect(geometry.work_area, 0, 30, 640, 450, "work area 640x480");
    check_rect(geometry.applications, 0, 0, 640, 30, "applications 640x480");
}

static void test_geometry_small_screen(void)
{
    robo_desktop_geometry_t geometry;
    robo_desktop_panel_metrics_t panel = desktop_panel_metrics();

    desktop_fixture();
    /* A screen with room for the panel and almost nothing else: the work area is thin rather than
     * negative, and every rectangle is still inside the screen. */
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_compute(robo_gfx_size(64, 40), &panel, &geometry), ROBO_OK);
    check_rect(geometry.screen, 0, 0, 64, 40, "screen 64x40");
    check_rect(geometry.panel, 0, 0, 64, 30, "panel 64x40");
    check_rect(geometry.work_area, 0, 30, 64, 10, "work area 64x40");
    ROBO_CHECK(geometry.work_area.height > 0);
    /* The identity band collapses rather than inverting when a third is narrower than its inset. */
    ROBO_CHECK(geometry.identity.width >= 0);
    ROBO_CHECK(geometry.status.width >= 0);

    /* Refusals: no size, a panel as tall as the screen, a NULL argument. */
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_compute(robo_gfx_size(0, 40), &panel, &geometry),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_compute(robo_gfx_size(64, -1), &panel, &geometry),
                      ROBO_INVALID_ARGUMENT);
    panel.height = 40;
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_compute(robo_gfx_size(64, 40), &panel, &geometry),
                      ROBO_INVALID_ARGUMENT);
    panel = desktop_panel_metrics();
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_compute(robo_gfx_size(64, 40), NULL, &geometry),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_compute(robo_gfx_size(64, 40), &panel, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_compute(
                          robo_gfx_size((int32_t)ROBO_DESKTOP_MAX_DIMENSION + 1, 40), &panel, &geometry),
                      ROBO_INVALID_ARGUMENT);

    robo_desktop_geometry_init(NULL);
}

/* ------------------------------------------------------------------ */
/* 3. The panel's labels                                               */
/* ------------------------------------------------------------------ */

static void test_geometry_panel_areas(void)
{
    robo_desktop_geometry_t geometry = geometry_of(1280, 800);
    robo_gfx_rect_t area;

    desktop_fixture();

    /* The identity label is left-aligned at its band's own left edge. */
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_panel_area(&geometry, ROBO_DESKTOP_PANEL_IDENTITY, 84,
                                                      &area),
                      ROBO_OK);
    check_rect(area, 14, 0, 84, 30, "identity label");

    /* The applications label is centred on the panel. */
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_panel_area(&geometry, ROBO_DESKTOP_PANEL_APPLICATIONS, 100,
                                                      &area),
                      ROBO_OK);
    check_rect(area, (1280 - 100) / 2, 0, 100, 30, "applications label");

    /* The status label is right-aligned inside its band, so a clock keeps a fixed distance from the
     * screen's edge whatever it says. */
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_panel_area(&geometry, ROBO_DESKTOP_PANEL_STATUS, 40, &area),
                      ROBO_OK);
    check_rect(area, 1280 - 16 - 40, 0, 40, 30, "status label");

    /* No measurement: the band stays as reserved, which is the face-less case. */
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_panel_area(&geometry, ROBO_DESKTOP_PANEL_STATUS, 0, &area),
                      ROBO_OK);
    check_rect(area, geometry.status.x, 0, geometry.status.width, 30, "unmeasured status");

    /* Text wider than its band is clamped to the band rather than leaving it. */
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_panel_area(&geometry, ROBO_DESKTOP_PANEL_STATUS, 100000,
                                                      &area),
                      ROBO_OK);
    ROBO_CHECK(area.width == geometry.status.width && area.x == geometry.status.x);

    ROBO_CHECK_EQ_INT(robo_desktop_geometry_panel_area(&geometry, (robo_desktop_panel_area_t)9, 10,
                                                      &area),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_panel_area(&geometry, ROBO_DESKTOP_PANEL_STATUS, -1, &area),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_panel_area(NULL, ROBO_DESKTOP_PANEL_STATUS, 1, &area),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_panel_area(&geometry, ROBO_DESKTOP_PANEL_STATUS, 1, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* 4. Placement and containment                                        */
/* ------------------------------------------------------------------ */

static void test_geometry_place(void)
{
    robo_desktop_geometry_t geometry = geometry_of(1280, 800);
    robo_gfx_point_t point;

    desktop_fixture();
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_place(robo_gfx_size(560, 360), &geometry.work_area, &point),
                      ROBO_OK);
    /* Centred horizontally; a third of the way down the remaining space vertically. */
    ROBO_CHECK_EQ_INT(point.x, (1280 - 560) / 2);
    ROBO_CHECK_EQ_INT(point.y, 30 + (770 - 360) / 3);

    /* A frame larger than the work area lands at its origin rather than above or left of it. */
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_place(robo_gfx_size(2000, 2000), &geometry.work_area,
                                                 &point),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(point.x, 0);
    ROBO_CHECK_EQ_INT(point.y, 30);

    ROBO_CHECK_EQ_INT(robo_desktop_geometry_place(robo_gfx_size(1, 1), NULL, &point),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_place(robo_gfx_size(-1, 1), &geometry.work_area, &point),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_place(robo_gfx_size(1, 1), &geometry.work_area, NULL),
                      ROBO_INVALID_ARGUMENT);
}

static void test_geometry_contain(void)
{
    robo_desktop_geometry_t geometry = geometry_of(1280, 800);
    robo_gfx_rect_t frame;
    robo_gfx_rect_t out;

    desktop_fixture();

    /* Inside already: unchanged, and that is what makes a `set_bounds` of the same rectangle a no-op. */
    frame = robo_gfx_rect(100, 100, 300, 200);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_contain(frame, &geometry.work_area, &out), ROBO_OK);
    check_rect(out, 100, 100, 300, 200, "already inside");

    /* Past the right and bottom edges: **moved**, not shrunk -- a window dragged past an edge is pulled
     * back. */
    frame = robo_gfx_rect(1200, 700, 300, 200);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_contain(frame, &geometry.work_area, &out), ROBO_OK);
    check_rect(out, 1280 - 300, 800 - 200, 300, 200, "moved in from the corner");

    /* Above the panel and left of the screen: pulled to the work area's own origin. */
    frame = robo_gfx_rect(-40, -40, 300, 200);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_contain(frame, &geometry.work_area, &out), ROBO_OK);
    check_rect(out, 0, 30, 300, 200, "pulled to the origin");

    /* Larger than the work area: **shrunk** to it, because a work area is the largest a window may be. */
    frame = robo_gfx_rect(10, 40, 2000, 2000);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_contain(frame, &geometry.work_area, &out), ROBO_OK);
    check_rect(out, 0, 30, 1280, 770, "shrunk to the work area");

    /* Both at once: the shrink happens first, so the result is inside rather than merely smaller. */
    frame = robo_gfx_rect(900, 500, 2000, 2000);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_contain(frame, &geometry.work_area, &out), ROBO_OK);
    check_rect(out, 0, 30, 1280, 770, "shrunk and moved");

    /* An empty work area contains nothing, and says so rather than pretending. */
    {
        robo_gfx_rect_t empty = robo_gfx_rect_empty();

        ROBO_CHECK_EQ_INT(robo_desktop_geometry_contain(frame, &empty, &out), ROBO_OK);
        ROBO_CHECK(robo_gfx_rect_is_empty(out));
    }

    ROBO_CHECK_EQ_INT(robo_desktop_geometry_contain(frame, NULL, &out), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_contain(frame, &geometry.work_area, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_geometry_contain(robo_gfx_rect(0, 0, -1, 10), &geometry.work_area,
                                                   &out),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* 5. The desktop's own geometry                                       */
/* ------------------------------------------------------------------ */

static void test_geometry_follows_screen(void)
{
    robo_desktop_t *desktop = desktop_open(400, 300);
    robo_desktop_geometry_t geometry;
    robo_gfx_rect_t work;

    geometry = robo_desktop_geometry_of(desktop);
    check_rect(geometry.screen, 0, 0, 400, 300, "desktop screen");
    work = robo_desktop_work_area(desktop);
    check_rect(work, 0, 30, 400, 270, "desktop work area");
    ROBO_CHECK_EQ_INT(robo_desktop_screen_size(desktop).width, 400);
    ROBO_CHECK_EQ_INT(robo_desktop_screen_size(desktop).height, 300);

    /* A resize recomputes the whole layout, which is what the panel and the work area follow. */
    ROBO_CHECK_EQ_INT(robo_desktop_resize(desktop, 800, 600, NULL), ROBO_OK);
    geometry = robo_desktop_geometry_of(desktop);
    check_rect(geometry.screen, 0, 0, 800, 600, "resized screen");
    check_rect(geometry.panel, 0, 0, 800, 30, "resized panel");
    check_rect(geometry.work_area, 0, 30, 800, 570, "resized work area");

    /* A resize to the same size allocates nothing and reports success, so a caller polling a window's
     * size does not reallocate every iteration. */
    ROBO_CHECK_EQ_INT(robo_desktop_resize(desktop, 800, 600, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_resize(desktop, 0, 600, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_resize(desktop, 800, (int32_t)ROBO_DESKTOP_MAX_DIMENSION + 1, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_resize(NULL, 800, 600, NULL), ROBO_INVALID_ARGUMENT);

    /* NULL answers are empty rather than a crash. */
    geometry = robo_desktop_geometry_of(NULL);
    ROBO_CHECK(robo_gfx_rect_is_empty(geometry.screen));
    ROBO_CHECK(robo_gfx_rect_is_empty(robo_desktop_work_area(NULL)));
    ROBO_CHECK_EQ_INT(robo_desktop_screen_size(NULL).width, 0);

    desktop_close(desktop);
    desktop_close(NULL);
}

void robo_test_register_desktop_geometry(void)
{
    robo_test_register("desktop.geometry.metrics", test_geometry_metrics);
    robo_test_register("desktop.geometry.compute", test_geometry_compute);
    robo_test_register("desktop.geometry.small_screen", test_geometry_small_screen);
    robo_test_register("desktop.geometry.panel_areas", test_geometry_panel_areas);
    robo_test_register("desktop.geometry.place", test_geometry_place);
    robo_test_register("desktop.geometry.contain", test_geometry_contain);
    robo_test_register("desktop.geometry.follows_screen", test_geometry_follows_screen);
}
