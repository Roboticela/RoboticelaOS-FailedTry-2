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

static DesktopGeometry GeometryOf(int32_t width, int32_t height)
{
    DesktopGeometry geometry;
    DesktopPanelMetrics metrics = GetRoboDesktopPanelMetrics();

    RoboCheckEqInt(DesktopGeometryCompute(MakeGfxSize(width, height), &metrics, &geometry),
                      RoboOk);
    return geometry;
}

static int RectIs(GfxRect rect, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return rect.x == x && rect.y == y && rect.width == w && rect.height == h;
}

static void CheckRect(GfxRect rect, int32_t x, int32_t y, int32_t w, int32_t h,
                       const char *what)
{
    if (!RectIs(rect, x, y, w, h)) {
        TestFail(__FILE__, __LINE__, "%s: expected %d,%d %dx%d, got %d,%d %dx%d", what, (int)x,
                       (int)y, (int)w, (int)h, (int)rect.x, (int)rect.y, (int)rect.width,
                       (int)rect.height);
    }
}

/* ------------------------------------------------------------------ */
/* 1. The metrics                                                      */
/* ------------------------------------------------------------------ */

static void TestGeometryMetrics(void)
{
    DesktopPanelMetrics panel = GetRoboDesktopPanelMetrics();
    DesktopWindowMetrics window = GetRoboDesktopWindowMetrics();
    DesktopTheme theme = GetRoboDesktopTheme();

    DesktopFixture();
    RoboCheckEqInt(DesktopPanelMetricsIsValid(&panel), 1);
    RoboCheckEqInt(panel.height, 30);
    RoboCheck(panel.identityInset > 0 && panel.labelInset > 0);
    RoboCheckEqInt(panel.separatorHeight, 1);
    RoboCheck(panel.baselineFromBottom > 0 && panel.baselineFromBottom < panel.height);

    RoboCheckEqInt(DesktopWindowMetricsIsValid(&window), 1);
    RoboCheckEqInt(window.border, 1);
    RoboCheckEqInt(window.titleHeight, 24);
    /* The minimum is the decorations plus client area, computed rather than literal. */
    RoboCheckEqInt(window.minWidth, 2 * window.border + 8);
    RoboCheckEqInt(window.minHeight, 2 * window.border + window.titleHeight + 8);

    RoboCheckEqInt(DesktopThemeIsValid(&theme), 1);

    /* Refusals: a missing record, a panel with no height, a close button too tall for its bar. */
    RoboCheckEqInt(DesktopPanelMetricsIsValid(NULL), 0);
    panel.height = 0;
    RoboCheckEqInt(DesktopPanelMetricsIsValid(&panel), 0);
    panel = GetRoboDesktopPanelMetrics();
    panel.identityInset = -1;
    RoboCheckEqInt(DesktopPanelMetricsIsValid(&panel), 0);

    RoboCheckEqInt(DesktopWindowMetricsIsValid(NULL), 0);
    window.closeSize = window.titleHeight + 1;
    RoboCheckEqInt(DesktopWindowMetricsIsValid(&window), 0);
    window = GetRoboDesktopWindowMetrics();
    window.minWidth = 0;
    RoboCheckEqInt(DesktopWindowMetricsIsValid(&window), 0);
}

/* ------------------------------------------------------------------ */
/* 2. The layout                                                       */
/* ------------------------------------------------------------------ */

static void TestGeometryCompute(void)
{
    DesktopGeometry geometry = GeometryOf(1280, 800);

    CheckRect(geometry.screen, 0, 0, 1280, 800, "screen");
    CheckRect(geometry.panel, 0, 0, 1280, 30, "panel");
    CheckRect(geometry.workArea, 0, 30, 1280, 770, "work area");
    /* The panel's thirds, between the two insets: a third of the width, the identity band on the left and
     * the status band on the right. */
    CheckRect(geometry.identity, 14, 0, 426 - 14, 30, "identity area");
    CheckRect(geometry.applications, 0, 0, 1280, 30, "applications area");
    CheckRect(geometry.status, 1280 - 426 - 16, 0, 426, 30, "status area");

    /* A second resolution, to prove nothing is hard-coded. */
    geometry = GeometryOf(640, 480);
    CheckRect(geometry.screen, 0, 0, 640, 480, "screen 640x480");
    CheckRect(geometry.panel, 0, 0, 640, 30, "panel 640x480");
    CheckRect(geometry.workArea, 0, 30, 640, 450, "work area 640x480");
    CheckRect(geometry.applications, 0, 0, 640, 30, "applications 640x480");
}

static void TestGeometrySmallScreen(void)
{
    DesktopGeometry geometry;
    DesktopPanelMetrics panel = GetRoboDesktopPanelMetrics();

    DesktopFixture();
    /* A screen with room for the panel and almost nothing else: the work area is thin rather than
     * negative, and every rectangle is still inside the screen. */
    RoboCheckEqInt(DesktopGeometryCompute(MakeGfxSize(64, 40), &panel, &geometry), RoboOk);
    CheckRect(geometry.screen, 0, 0, 64, 40, "screen 64x40");
    CheckRect(geometry.panel, 0, 0, 64, 30, "panel 64x40");
    CheckRect(geometry.workArea, 0, 30, 64, 10, "work area 64x40");
    RoboCheck(geometry.workArea.height > 0);
    /* The identity band collapses rather than inverting when a third is narrower than its inset. */
    RoboCheck(geometry.identity.width >= 0);
    RoboCheck(geometry.status.width >= 0);

    /* Refusals: no size, a panel as tall as the screen, a NULL argument. */
    RoboCheckEqInt(DesktopGeometryCompute(MakeGfxSize(0, 40), &panel, &geometry),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopGeometryCompute(MakeGfxSize(64, -1), &panel, &geometry),
                      RoboInvalidArgument);
    panel.height = 40;
    RoboCheckEqInt(DesktopGeometryCompute(MakeGfxSize(64, 40), &panel, &geometry),
                      RoboInvalidArgument);
    panel = GetRoboDesktopPanelMetrics();
    RoboCheckEqInt(DesktopGeometryCompute(MakeGfxSize(64, 40), NULL, &geometry),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopGeometryCompute(MakeGfxSize(64, 40), &panel, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopGeometryCompute(
                          MakeGfxSize((int32_t)RoboDesktopMaxDimension + 1, 40), &panel, &geometry),
                      RoboInvalidArgument);

    DesktopGeometryInit(NULL);
}

/* ------------------------------------------------------------------ */
/* 3. The panel's labels                                               */
/* ------------------------------------------------------------------ */

static void TestGeometryPanelAreas(void)
{
    DesktopGeometry geometry = GeometryOf(1280, 800);
    GfxRect area;

    DesktopFixture();

    /* The identity label is left-aligned at its band's own left edge. */
    RoboCheckEqInt(DesktopGeometryPanelArea(&geometry, RoboDesktopPanelIdentity, 84,
                                                      &area),
                      RoboOk);
    CheckRect(area, 14, 0, 84, 30, "identity label");

    /* The applications label is centred on the panel. */
    RoboCheckEqInt(DesktopGeometryPanelArea(&geometry, RoboDesktopPanelApplications, 100,
                                                      &area),
                      RoboOk);
    CheckRect(area, (1280 - 100) / 2, 0, 100, 30, "applications label");

    /* The status label is right-aligned inside its band, so a clock keeps a fixed distance from the
     * screen's edge whatever it says. */
    RoboCheckEqInt(DesktopGeometryPanelArea(&geometry, RoboDesktopPanelStatus, 40, &area),
                      RoboOk);
    CheckRect(area, 1280 - 16 - 40, 0, 40, 30, "status label");

    /* No measurement: the band stays as reserved, which is the face-less case. */
    RoboCheckEqInt(DesktopGeometryPanelArea(&geometry, RoboDesktopPanelStatus, 0, &area),
                      RoboOk);
    CheckRect(area, geometry.status.x, 0, geometry.status.width, 30, "unmeasured status");

    /* Text wider than its band is clamped to the band rather than leaving it. */
    RoboCheckEqInt(DesktopGeometryPanelArea(&geometry, RoboDesktopPanelStatus, 100000,
                                                      &area),
                      RoboOk);
    RoboCheck(area.width == geometry.status.width && area.x == geometry.status.x);

    RoboCheckEqInt(DesktopGeometryPanelArea(&geometry, (DesktopPanelArea)9, 10,
                                                      &area),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopGeometryPanelArea(&geometry, RoboDesktopPanelStatus, -1, &area),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopGeometryPanelArea(NULL, RoboDesktopPanelStatus, 1, &area),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopGeometryPanelArea(&geometry, RoboDesktopPanelStatus, 1, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* 4. Placement and containment                                        */
/* ------------------------------------------------------------------ */

static void TestGeometryPlace(void)
{
    DesktopGeometry geometry = GeometryOf(1280, 800);
    GfxPoint point;

    DesktopFixture();
    RoboCheckEqInt(DesktopGeometryPlace(MakeGfxSize(560, 360), &geometry.workArea, &point),
                      RoboOk);
    /* Centred horizontally; a third of the way down the remaining space vertically. */
    RoboCheckEqInt(point.x, (1280 - 560) / 2);
    RoboCheckEqInt(point.y, 30 + (770 - 360) / 3);

    /* A frame larger than the work area lands at its origin rather than above or left of it. */
    RoboCheckEqInt(DesktopGeometryPlace(MakeGfxSize(2000, 2000), &geometry.workArea,
                                                 &point),
                      RoboOk);
    RoboCheckEqInt(point.x, 0);
    RoboCheckEqInt(point.y, 30);

    RoboCheckEqInt(DesktopGeometryPlace(MakeGfxSize(1, 1), NULL, &point),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopGeometryPlace(MakeGfxSize(-1, 1), &geometry.workArea, &point),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopGeometryPlace(MakeGfxSize(1, 1), &geometry.workArea, NULL),
                      RoboInvalidArgument);
}

static void TestGeometryContain(void)
{
    DesktopGeometry geometry = GeometryOf(1280, 800);
    GfxRect frame;
    GfxRect out;

    DesktopFixture();

    /* Inside already: unchanged, and that is what makes a `set_bounds` of the same rectangle a no-op. */
    frame = MakeGfxRect(100, 100, 300, 200);
    RoboCheckEqInt(DesktopGeometryContain(frame, &geometry.workArea, &out), RoboOk);
    CheckRect(out, 100, 100, 300, 200, "already inside");

    /* Past the right and bottom edges: **moved**, not shrunk -- a window dragged past an edge is pulled
     * back. */
    frame = MakeGfxRect(1200, 700, 300, 200);
    RoboCheckEqInt(DesktopGeometryContain(frame, &geometry.workArea, &out), RoboOk);
    CheckRect(out, 1280 - 300, 800 - 200, 300, 200, "moved in from the corner");

    /* Above the panel and left of the screen: pulled to the work area's own origin. */
    frame = MakeGfxRect(-40, -40, 300, 200);
    RoboCheckEqInt(DesktopGeometryContain(frame, &geometry.workArea, &out), RoboOk);
    CheckRect(out, 0, 30, 300, 200, "pulled to the origin");

    /* Larger than the work area: **shrunk** to it, because a work area is the largest a window may be. */
    frame = MakeGfxRect(10, 40, 2000, 2000);
    RoboCheckEqInt(DesktopGeometryContain(frame, &geometry.workArea, &out), RoboOk);
    CheckRect(out, 0, 30, 1280, 770, "shrunk to the work area");

    /* Both at once: the shrink happens first, so the result is inside rather than merely smaller. */
    frame = MakeGfxRect(900, 500, 2000, 2000);
    RoboCheckEqInt(DesktopGeometryContain(frame, &geometry.workArea, &out), RoboOk);
    CheckRect(out, 0, 30, 1280, 770, "shrunk and moved");

    /* An empty work area contains nothing, and says so rather than pretending. */
    {
        GfxRect empty = GfxRectEmpty();

        RoboCheckEqInt(DesktopGeometryContain(frame, &empty, &out), RoboOk);
        RoboCheck(GfxRectIsEmpty(out));
    }

    RoboCheckEqInt(DesktopGeometryContain(frame, NULL, &out), RoboInvalidArgument);
    RoboCheckEqInt(DesktopGeometryContain(frame, &geometry.workArea, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopGeometryContain(MakeGfxRect(0, 0, -1, 10), &geometry.workArea,
                                                   &out),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* 5. The desktop's own geometry                                       */
/* ------------------------------------------------------------------ */

static void TestGeometryFollowsScreen(void)
{
    Desktop *desktop = DesktopOpen(400, 300);
    DesktopGeometry geometry;
    GfxRect work;

    geometry = DesktopGeometryOf(desktop);
    CheckRect(geometry.screen, 0, 0, 400, 300, "desktop screen");
    work = DesktopWorkArea(desktop);
    CheckRect(work, 0, 30, 400, 270, "desktop work area");
    RoboCheckEqInt(DesktopScreenSize(desktop).width, 400);
    RoboCheckEqInt(DesktopScreenSize(desktop).height, 300);

    /* A resize recomputes the whole layout, which is what the panel and the work area follow. */
    RoboCheckEqInt(DesktopResize(desktop, 800, 600, NULL), RoboOk);
    geometry = DesktopGeometryOf(desktop);
    CheckRect(geometry.screen, 0, 0, 800, 600, "resized screen");
    CheckRect(geometry.panel, 0, 0, 800, 30, "resized panel");
    CheckRect(geometry.workArea, 0, 30, 800, 570, "resized work area");

    /* A resize to the same size allocates nothing and reports success, so a caller polling a window's
     * size does not reallocate every iteration. */
    RoboCheckEqInt(DesktopResize(desktop, 800, 600, NULL), RoboOk);
    RoboCheckEqInt(DesktopResize(desktop, 0, 600, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopResize(desktop, 800, (int32_t)RoboDesktopMaxDimension + 1, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopResize(NULL, 800, 600, NULL), RoboInvalidArgument);

    /* NULL answers are empty rather than a crash. */
    geometry = DesktopGeometryOf(NULL);
    RoboCheck(GfxRectIsEmpty(geometry.screen));
    RoboCheck(GfxRectIsEmpty(DesktopWorkArea(NULL)));
    RoboCheckEqInt(DesktopScreenSize(NULL).width, 0);

    DesktopClose(desktop);
    DesktopClose(NULL);
}

void TestRegisterDesktopGeometry(void)
{
    TestRegister("desktop.geometry.metrics", TestGeometryMetrics);
    TestRegister("desktop.geometry.compute", TestGeometryCompute);
    TestRegister("desktop.geometry.small_screen", TestGeometrySmallScreen);
    TestRegister("desktop.geometry.panel_areas", TestGeometryPanelAreas);
    TestRegister("desktop.geometry.place", TestGeometryPlace);
    TestRegister("desktop.geometry.contain", TestGeometryContain);
    TestRegister("desktop.geometry.follows_screen", TestGeometryFollowsScreen);
}
