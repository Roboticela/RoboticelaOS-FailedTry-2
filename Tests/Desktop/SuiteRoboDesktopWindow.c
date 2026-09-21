/*
 * RoboDesktop shell window suite (Phase 11.13) — `desktop.window.*`.
 *
 * What a window *is*, without a frame: where it lands, how it is clamped, what its client rectangle is,
 * its title and contents, its z-order and focus, its close, and what a point resolves to. The frame suite
 * beside it is what a window *looks like*; this one is the model.
 *
 * Three of these cases are the phase's §11 read as assertions rather than as prose:
 *
 *   a caller that does not know the work area can ask for a window and get a placed one
 *   (`desktop.window.add.defaults`, which pins the placement arithmetic end to end);
 *   a window smaller than its own decorations is **grown** rather than refused
 *   (`desktop.window.add.minimum`), because the minimum is a property of the decorations the shell
 *   draws, not of the caller's request;
 *   and closing is a **request**: the window stops painting, stays in the list, and the caller destroys
 *   it (`desktop.window.close`), which is what lets an application decide what a close means.
 */

#include "RoboDesktopTestSupport.h"

static size_t VisibleWindows(const Desktop *desktop)
{
    size_t count = DesktopWindowCount(desktop);
    size_t index;
    size_t visible = 0u;

    for (index = 0u; index < count; index++) {
        DesktopWindowId id = RoboDesktopWindowIdNone;

        if (DesktopWindowAt(desktop, index, &id) == RoboOk && DesktopWindowIsVisible(desktop, id)) {
            visible++;
        }
    }
    return visible;
}

static void AssertBounds(Desktop *desktop, DesktopWindowId id, int32_t x, int32_t y,
                          int32_t w, int32_t h, const char *what)
{
    GfxRect bounds;

    RoboCheckEqInt(DesktopWindowBounds(desktop, id, &bounds), RoboOk);
    if (bounds.x != x || bounds.y != y || bounds.width != w || bounds.height != h) {
        TestFail(__FILE__, __LINE__, "%s: expected %d,%d %dx%d, got %d,%d %dx%d", what, (int)x,
                       (int)y, (int)w, (int)h, (int)bounds.x, (int)bounds.y, (int)bounds.width,
                       (int)bounds.height);
    }
}

/* ------------------------------------------------------------------ */
/* 1. Creation and placement                                           */
/* ------------------------------------------------------------------ */

static void TestWindowAddDefaults(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopWindowDesc desc;
    DesktopWindowId id = RoboDesktopWindowIdNone;
    GfxRect client;
    DesktopStats stats;

    DesktopWindowDescInit(&desc);
    RoboCheck(desc.x < 0 && desc.y < 0);
    RoboCheck(desc.visible == 1);
    RoboCheck(desc.title == NULL && desc.content == NULL && desc.contentFace == NULL);
    DesktopWindowDescInit(NULL);
    RoboCheckEqInt(DesktopWindowDescValidate(&desc), RoboOk);
    desc.width = -1;
    RoboCheckEqInt(DesktopWindowDescValidate(&desc), RoboInvalidArgument);
    RoboCheckEqInt(DesktopWindowDescValidate(NULL), RoboInvalidArgument);
    desc.width = 560;

    RoboCheckEqInt(DesktopWindowAdd(desktop, &desc, &id, NULL), RoboOk);
    RoboCheck(id != RoboDesktopWindowIdNone);
    RoboCheckEqInt((int)DesktopWindowCount(desktop), 1);
    RoboCheckEqInt(DesktopWindowExists(desktop, id), 1);
    RoboCheckEqInt(DesktopWindowExists(desktop, 99u), 0);
    RoboCheckEqInt(DesktopWindowExists(NULL, id), 0);

    /* "Place me" is the deterministic centred-nudged-up placement, then the work-area clamp. */
    AssertBounds(desktop, id, 360, 166, 560, 360, "placed window");

    /* The client rectangle is the decorations subtracted, which is the door a document is built for. */
    RoboCheckEqInt(DesktopWindowClientBounds(desktop, id, &client), RoboOk);
    RoboCheckEqInt(client.x, 361);
    RoboCheckEqInt(client.y, 166 + 1 + 24);
    RoboCheckEqInt(client.width, 558);
    RoboCheckEqInt(client.height, 360 - 2 - 24);

    /* A new window is focused and front: on a desktop with no window manager that is the only defensible
     * default. */
    RoboCheckEqInt((int)DesktopWindowFocused(desktop), (int)id);
    RoboCheckEqInt(DesktopWindowIsVisible(desktop, id), 1);
    RoboCheckEqInt(VisibleWindows(desktop), 1u);

    RoboCheckEqInt(GetDesktopStats(desktop, &stats), RoboOk);
    RoboCheckEqInt((int)stats.windowsAdded, 1);

    RoboCheckEqInt(DesktopWindowAdd(desktop, NULL, &id, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopWindowAdd(desktop, &desc, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopWindowAdd(NULL, &desc, &id, NULL), RoboInvalidArgument);

    DesktopClose(desktop);
}

static void TestWindowAddMinimumAndBounds(void)
{
    DesktopWindowMetrics metrics = GetRoboDesktopWindowMetrics();
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopWindowId id;
    GfxRect bounds;

    DesktopFixture();
    /* A four-by-four window is asking for something the shell cannot draw: the honest answer is the
     * smallest window that can be drawn, not an error and not a frame whose title bar is outside itself. */
    id = DesktopAddWindow(desktop, "Tiny", 100, 100, 4, 4);
    AssertBounds(desktop, id, 100, 100, metrics.minWidth, metrics.minHeight, "grown to the minimum");

    /* A window larger than the work area is shrunk to it, and one past its edges is moved inside. */
    RoboCheckEqInt(DesktopWindowSetBounds(desktop, id, MakeGfxRect(10, 40, 4000, 4000),
                                                     NULL),
                      RoboOk);
    RoboCheckEqInt(DesktopWindowBounds(desktop, id, &bounds), RoboOk);
    RoboCheck(bounds.width <= 1280 && bounds.height <= 770);
    RoboCheck(bounds.x >= 0 && bounds.y >= 30);

    RoboCheckEqInt(DesktopWindowSetBounds(desktop, id, MakeGfxRect(1270, 780, 300, 200),
                                                     NULL),
                      RoboOk);
    RoboCheckEqInt(DesktopWindowBounds(desktop, id, &bounds), RoboOk);
    RoboCheck(bounds.x + bounds.width <= 1280);
    RoboCheck(bounds.y + bounds.height <= 800);

    RoboCheckEqInt(DesktopWindowSetBounds(desktop, id, MakeGfxRect(0, 0, -5, 10), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(DesktopWindowSetBounds(desktop, 4242u, MakeGfxRect(0, 40, 10, 10), NULL),
                      RoboNotFound);
    RoboCheckEqInt(DesktopWindowBounds(desktop, 4242u, &bounds), RoboNotFound);
    RoboCheckEqInt(DesktopWindowClientBounds(desktop, 4242u, &bounds), RoboNotFound);

    DesktopClose(desktop);
}

/*
 * A work area too thin to place a window in is refused rather than filled with a window the shell cannot
 * draw, and `RoboDesktopMinDimension` is the threshold -- tested at the threshold from both sides,
 * because a rule asserted only on its failing side is a rule that could be a different rule.
 */
static void TestWindowNoWorkArea(void)
{
    DesktopWindowDesc desc;
    DesktopDiag diag;
    DesktopWindowId id = RoboDesktopWindowIdNone;
    Desktop *thin;
    Desktop *exact;

    DesktopFixture();
    DesktopWindowDescInit(&desc);
    DesktopDiagReset(&diag);

    /* 64x40 leaves a 64x10 work area: ten pixels is less than the smallest a window may be placed in,
     * so the shell refuses and says which object refused -- `SHELL`, because the *screen* is too small
     * rather than the window being wrong. */
    thin = DesktopOpen(64, 40);
    RoboCheckEqInt(DesktopWindowAdd(thin, &desc, &id, &diag), RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboDesktopDomainShell);
    RoboCheckEqInt((int)DesktopWindowCount(thin), 0);
    RoboCheckEqInt((int)id, (int)RoboDesktopWindowIdNone);
    DesktopClose(thin);

    /* 64x46 leaves exactly 64x16: the threshold itself is placeable, so the rule is a bound and not an
     * approximation. The window is grown to its decoration minimum and clamped into the work area. */
    exact = DesktopOpen(64, 46);
    RoboCheckEqInt(DesktopWindowAdd(exact, &desc, &id, NULL), RoboOk);
    RoboCheck(id != RoboDesktopWindowIdNone);
    RoboCheckEqInt((int)DesktopWindowCount(exact), 1);
    DesktopClose(exact);
}

static void TestWindowCapacity(void)
{
    Desktop *desktop = DesktopOpen(800, 600);
    DesktopWindowDesc desc;
    size_t index;

    DesktopWindowDescInit(&desc);
    for (index = 0u; index < RoboDesktopMaxWindows; index++) {
        DesktopWindowId id = RoboDesktopWindowIdNone;

        RoboCheckEqInt(DesktopWindowAdd(desktop, &desc, &id, NULL), RoboOk);
        RoboCheck(id != RoboDesktopWindowIdNone);
    }
    RoboCheckEqInt((int)DesktopWindowCount(desktop), (int)RoboDesktopMaxWindows);
    RoboCheckEqInt(DesktopWindowAdd(desktop, &desc, &(DesktopWindowId){ 0 }, NULL),
                      RoboAlreadyExists);
    /* Every id is distinct: a wrap that handed out the "none" token would be a window nobody could name. */
    for (index = 0u; index < RoboDesktopMaxWindows; index++) {
        DesktopWindowId id = RoboDesktopWindowIdNone;

        RoboCheckEqInt(DesktopWindowAt(desktop, index, &id), RoboOk);
        RoboCheck(id != RoboDesktopWindowIdNone);
    }
    RoboCheckEqInt(DesktopWindowAt(desktop, RoboDesktopMaxWindows,
                                            &(DesktopWindowId){ 0 }),
                      RoboNotFound);
    RoboCheckEqInt(DesktopWindowAt(desktop, 0u, NULL), RoboInvalidArgument);

    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 2. Title, contents, visibility                                      */
/* ------------------------------------------------------------------ */

static void TestWindowTitleAndContent(void)
{
    Desktop *desktop = DesktopOpen(800, 600);
    DesktopWindowId id = DesktopAddWindow(desktop, "Hello", 40, 40, 400, 300);
    char tooLong[RoboDesktopTitleMax + 2u];
    size_t index;

    RoboCheck(strcmp(DesktopWindowTitle(desktop, id), "Hello") == 0);
    RoboCheck(strcmp(DesktopWindowTitle(desktop, 4242u), "") == 0);
    RoboCheck(strcmp(DesktopWindowTitle(NULL, id), "") == 0);

    RoboCheckEqInt(DesktopWindowSetTitle(desktop, id, "Roboticela Demo", NULL), RoboOk);
    RoboCheck(strcmp(DesktopWindowTitle(desktop, id), "Roboticela Demo") == 0);
    RoboCheckEqInt(DesktopWindowSetTitle(desktop, id, NULL, NULL), RoboOk);
    RoboCheck(strcmp(DesktopWindowTitle(desktop, id), "") == 0);

    for (index = 0u; index < sizeof(tooLong) - 1u; index++) {
        tooLong[index] = 'x';
    }
    tooLong[sizeof(tooLong) - 1u] = '\0';
    RoboCheckEqInt(DesktopWindowSetTitle(desktop, id, "Kept", NULL), RoboOk);
    RoboCheckEqInt(DesktopWindowSetTitle(desktop, id, tooLong, NULL), RoboInvalidArgument);
    /* Nothing was copied on the refusal, so the caller still holds its old title. */
    RoboCheck(strcmp(DesktopWindowTitle(desktop, id), "Kept") == 0);

    /* Contents are borrowed: this desktop has no document, and a window with no contents is a window. */
    RoboCheck(DesktopWindowContent(desktop, id) == NULL);
    RoboCheck(DesktopWindowContent(desktop, 4242u) == NULL);
    RoboCheckEqInt(DesktopWindowSetContent(desktop, 4242u, NULL, NULL, NULL), RoboNotFound);
    RoboCheckEqInt(DesktopWindowSetContent(NULL, id, NULL, NULL, NULL), RoboInvalidArgument);

    /* Visibility: a hidden window keeps its bounds, contents and z-order and is not painted. */
    RoboCheckEqInt(DesktopWindowSetVisible(desktop, id, 0, NULL), RoboOk);
    RoboCheckEqInt(DesktopWindowIsVisible(desktop, id), 0);
    RoboCheckEqInt(VisibleWindows(desktop), 0u);
    RoboCheckEqInt((int)DesktopWindowCount(desktop), 1);
    AssertBounds(desktop, id, 40, 40, 400, 300, "hidden window keeps its bounds");
    RoboCheckEqInt(DesktopWindowSetVisible(desktop, id, 1, NULL), RoboOk);
    RoboCheckEqInt(DesktopWindowIsVisible(desktop, id), 1);
    RoboCheckEqInt(DesktopWindowSetVisible(desktop, 4242u, 1, NULL), RoboNotFound);
    RoboCheckEqInt(DesktopWindowIsVisible(desktop, 4242u), 0);

    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 3. Z-order and focus                                                */
/* ------------------------------------------------------------------ */

static void TestWindowRaiseAndFocus(void)
{
    Desktop *desktop = DesktopOpen(1000, 700);
    DesktopWindowId first = DesktopAddWindow(desktop, "First", 60, 60, 300, 200);
    DesktopWindowId second = DesktopAddWindow(desktop, "Second", 80, 80, 300, 200);
    DesktopWindowId front = RoboDesktopWindowIdNone;

    RoboCheckEqInt((int)DesktopWindowFocused(desktop), (int)second);
    RoboCheckEqInt(DesktopWindowAt(desktop, 0u, &front), RoboOk);
    RoboCheckEqInt((int)front, (int)first);
    RoboCheckEqInt(DesktopWindowAt(desktop, 1u, &front), RoboOk);
    RoboCheckEqInt((int)front, (int)second);

    /* The last window in the list is the front one, which is the whole of this build's z-order. */
    RoboCheckEqInt(DesktopWindowRaise(desktop, first), RoboOk);
    RoboCheckEqInt(DesktopWindowAt(desktop, 1u, &front), RoboOk);
    RoboCheckEqInt((int)front, (int)first);
    /* Focus follows the raise, so the two cannot drift. */
    RoboCheckEqInt((int)DesktopWindowFocused(desktop), (int)first);

    /* Focusing without raising: the accent border can be moved without reordering. */
    RoboCheckEqInt(DesktopWindowFocus(desktop, second), RoboOk);
    RoboCheckEqInt((int)DesktopWindowFocused(desktop), (int)second);
    RoboCheckEqInt(DesktopWindowAt(desktop, 1u, &front), RoboOk);
    RoboCheckEqInt((int)front, (int)first);

    RoboCheckEqInt(DesktopWindowRaise(desktop, 4242u), RoboNotFound);
    RoboCheckEqInt(DesktopWindowFocus(desktop, 4242u), RoboNotFound);
    /* A NULL desktop is a caller mistake rather than a missing window, and the two answers differ. */
    RoboCheckEqInt(DesktopWindowRaise(NULL, first), RoboInvalidArgument);
    RoboCheckEqInt((int)DesktopWindowFocused(NULL), (int)RoboDesktopWindowIdNone);

    DesktopClose(desktop);
}

static void TestWindowCloseAndDestroy(void)
{
    Desktop *desktop = DesktopOpen(1000, 700);
    DesktopWindowId first = DesktopAddWindow(desktop, "First", 60, 60, 300, 200);
    DesktopWindowId second = DesktopAddWindow(desktop, "Second", 80, 80, 300, 200);
    DesktopWindowState state = RoboDesktopWindowOpen;
    DesktopStats stats;

    RoboCheckEqInt(GetDesktopWindowState(desktop, first, &state), RoboOk);
    RoboCheckEqInt((int)state, (int)RoboDesktopWindowOpen);

    /* Close is a **request**: the window stops being painted and stays in the list for its owner. */
    RoboCheckEqInt(DesktopWindowRequestClose(desktop, second), RoboOk);
    RoboCheckEqInt(GetDesktopWindowState(desktop, second, &state), RoboOk);
    RoboCheckEqInt((int)state, (int)RoboDesktopWindowClosing);
    RoboCheckEqInt((int)DesktopWindowCount(desktop), 2);
    RoboCheckEqInt(VisibleWindows(desktop), 1u);
    /* Idempotent, like the request it is. */
    RoboCheckEqInt(DesktopWindowRequestClose(desktop, second), RoboOk);
    RoboCheckEqInt(DesktopWindowRequestClose(desktop, 4242u), RoboNotFound);
    RoboCheckEqInt(GetDesktopWindowState(desktop, 4242u, &state), RoboNotFound);

    /* Destroying the focused window moves focus to the front one that is left. */
    RoboCheckEqInt(DesktopWindowDestroy(desktop, second), RoboOk);
    RoboCheckEqInt(DesktopWindowDestroy(desktop, second), RoboNotFound);
    RoboCheckEqInt((int)DesktopWindowCount(desktop), 1);
    RoboCheckEqInt((int)DesktopWindowFocused(desktop), (int)first);
    RoboCheckEqInt(DesktopWindowExists(desktop, second), 0);
    RoboCheckEqInt(DesktopWindowDestroy(NULL, first), RoboInvalidArgument);

    RoboCheckEqInt(GetDesktopStats(desktop, &stats), RoboOk);
    RoboCheckEqInt((int)stats.windowsAdded, 2);
    RoboCheckEqInt((int)stats.windowsClosed, 1);
    RoboCheckEqInt((int)stats.windowsDestroyed, 1);

    /* Destroying the last window leaves a desktop with no focused window rather than a stale index. */
    RoboCheckEqInt(DesktopWindowDestroy(desktop, first), RoboOk);
    RoboCheckEqInt((int)DesktopWindowFocused(desktop), (int)RoboDesktopWindowIdNone);
    RoboCheckEqInt((int)DesktopWindowCount(desktop), 0);

    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 4. Hit testing and the pointer                                      */
/* ------------------------------------------------------------------ */

static void TestWindowHitTest(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopWindowId id = DesktopAddWindow(desktop, "Target", 100, 100, 400, 300);
    DesktopGeometry geometry = DesktopGeometryOf(desktop);
    DesktopHit hit;

    DesktopHitInit(&hit);
    RoboCheckEqInt((int)hit.area, (int)RoboDesktopAreaNone);
    RoboCheckEqInt(hit.inside, 0);
    DesktopHitInit(NULL);

    /* The title bar: `bounds.y + border` through the title's height, minus the close button. */
    RoboCheckEqInt(DesktopHitTest(desktop, MakeGfxPoint(160, 104), &hit), RoboOk);
    RoboCheckEqInt((int)hit.area, (int)RoboDesktopAreaWindowTitle);
    RoboCheckEqInt((int)hit.window, (int)id);
    RoboCheckEqInt(hit.inside, 1);

    /* The close button: ten pixels square, six in from the frame's right edge, centred in the title bar. */
    RoboCheckEqInt(DesktopHitTest(desktop, MakeGfxPoint(400 + 100 - 12, 100 + 8 + 5), &hit),
                      RoboOk);
    RoboCheckEqInt((int)hit.area, (int)RoboDesktopAreaWindowClose);
    RoboCheckEqInt((int)hit.window, (int)id);

    /* The client area. */
    RoboCheckEqInt(DesktopHitTest(desktop, MakeGfxPoint(300, 300), &hit), RoboOk);
    RoboCheckEqInt((int)hit.area, (int)RoboDesktopAreaWindowContent);

    /* The panel, then the desktop's own surface, then outside the screen entirely -- which is not a
     * failure, because a pointer can leave a window. */
    RoboCheckEqInt(DesktopHitTest(desktop, MakeGfxPoint(10, 10), &hit), RoboOk);
    RoboCheckEqInt((int)hit.area, (int)RoboDesktopAreaPanel);
    RoboCheckEqInt((int)hit.window, (int)RoboDesktopWindowIdNone);
    RoboCheckEqInt(DesktopHitTest(desktop, MakeGfxPoint(geometry.workArea.width - 1,
                                                                   geometry.workArea.height + 20), &hit),
                      RoboOk);
    RoboCheckEqInt((int)hit.area, (int)RoboDesktopAreaBackground);
    RoboCheckEqInt(hit.inside, 1);
    RoboCheckEqInt(DesktopHitTest(desktop, MakeGfxPoint(-5, -5), &hit), RoboOk);
    RoboCheckEqInt((int)hit.area, (int)RoboDesktopAreaNone);
    RoboCheckEqInt(hit.inside, 0);

    /* Front to back: a point over two windows belongs to the front one, which is the answer a user would
     * give. */
    {
        DesktopWindowId front = DesktopAddWindow(desktop, "Front", 100, 100, 400, 300);

        RoboCheckEqInt(DesktopHitTest(desktop, MakeGfxPoint(300, 300), &hit), RoboOk);
        RoboCheckEqInt((int)hit.window, (int)front);
    }

    RoboCheckEqInt(DesktopHitTest(desktop, MakeGfxPoint(0, 0), NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopHitTest(NULL, MakeGfxPoint(0, 0), &hit), RoboInvalidArgument);

    DesktopClose(desktop);
}

static void TestWindowPointerActions(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopWindowId first = DesktopAddWindow(desktop, "First", 100, 100, 400, 300);
    DesktopWindowId second = DesktopAddWindow(desktop, "Second", 150, 150, 400, 300);
    DesktopPointer pointer;
    DesktopStats stats;

    /* A move records where the pointer is and what is under it. */
    RoboCheckEqInt(DesktopObservePointer(desktop, MakeGfxPoint(300, 300), 0, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopPointer(desktop, &pointer), RoboOk);
    RoboCheckEqInt(pointer.point.x, 300);
    RoboCheckEqInt(pointer.point.y, 300);
    RoboCheckEqInt(pointer.inside, 1);
    RoboCheckEqInt(pointer.pressed, 0);
    RoboCheckEqInt((int)pointer.area, (int)RoboDesktopAreaWindowContent);
    RoboCheckEqInt((int)pointer.window, (int)second);

    /* A press on a window's title bar raises it -- read at a point in `first`'s title band that `second`
     * does not cover, which is what makes the assertion about the press rather than about z-order. Its
     * title band is rows 101..124; `second` starts at (150, 150). */
    RoboCheckEqInt(DesktopObservePointer(desktop, MakeGfxPoint(200, 110), 1, NULL), RoboOk);
    RoboCheckEqInt((int)DesktopWindowFocused(desktop), (int)first);
    RoboCheckEqInt(GetDesktopPointer(desktop, &pointer), RoboOk);
    RoboCheckEqInt(pointer.pressed, 1);
    RoboCheckEqInt((int)pointer.area, (int)RoboDesktopAreaWindowTitle);
    RoboCheckEqInt((int)pointer.window, (int)first);
    /* And a *release* is the same door with `pressed` 0. */
    RoboCheckEqInt(DesktopObservePointer(desktop, MakeGfxPoint(200, 110), 0, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopPointer(desktop, &pointer), RoboOk);
    RoboCheckEqInt(pointer.pressed, 0);

    /* A press on a close button requests that window's close: `second` is at (150, 150, 400, 300), so its
     * button is ten pixels square with its left edge at 150 + 400 - 1 - 6 - 10. */
    RoboCheckEqInt(DesktopObservePointer(desktop, MakeGfxPoint(537, 162), 1, NULL), RoboOk);
    {
        DesktopWindowState state = RoboDesktopWindowOpen;

        RoboCheckEqInt(GetDesktopWindowState(desktop, second, &state), RoboOk);
        RoboCheckEqInt((int)state, (int)RoboDesktopWindowClosing);
    }

    /* A press that hit nothing is a legitimate press: a shell that reported an error for a click on its
     * own surface would be reporting an error for the most ordinary thing a user does. */
    RoboCheckEqInt(DesktopObservePointer(desktop, MakeGfxPoint(900, 700), 1, NULL), RoboOk);
    RoboCheckEqInt(GetDesktopPointer(desktop, &pointer), RoboOk);
    RoboCheckEqInt((int)pointer.area, (int)RoboDesktopAreaBackground);
    RoboCheckEqInt(pointer.inside, 1);

    RoboCheckEqInt(GetDesktopStats(desktop, &stats), RoboOk);
    RoboCheckEqInt((int)stats.pointerActions, 2);
    RoboCheckEqInt((int)stats.pointerMoves >= 2, 1);

    RoboCheckEqInt(DesktopObservePointer(NULL, MakeGfxPoint(0, 0), 0, NULL),
                      RoboInvalidArgument);

    DesktopClose(desktop);
}

void TestRegisterDesktopWindow(void)
{
    TestRegister("desktop.window.add.defaults", TestWindowAddDefaults);
    TestRegister("desktop.window.add.minimum_and_bounds", TestWindowAddMinimumAndBounds);
    TestRegister("desktop.window.no_work_area", TestWindowNoWorkArea);
    TestRegister("desktop.window.capacity", TestWindowCapacity);
    TestRegister("desktop.window.title_and_content", TestWindowTitleAndContent);
    TestRegister("desktop.window.raise_and_focus", TestWindowRaiseAndFocus);
    TestRegister("desktop.window.close_and_destroy", TestWindowCloseAndDestroy);
    TestRegister("desktop.window.hit_test", TestWindowHitTest);
    TestRegister("desktop.window.pointer_actions", TestWindowPointerActions);
}
