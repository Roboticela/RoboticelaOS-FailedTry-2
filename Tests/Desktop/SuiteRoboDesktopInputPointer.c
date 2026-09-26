/*
 * The shell's pointer integration: hit testing, the window manager's gestures, the coordinate spaces and
 * the cursor (Phase 13.67 / 13.70 / 13.71 / 13.75).
 *
 * Four claims that belong together because they are all about *where*: which window (and which part of it)
 * a point is in (13.70), what a press does about it (13.67's move and resize), which plane the arithmetic
 * happens in (13.71), and which shape the pointer shows while it is there (13.75).
 *
 * The precedence the cases pin is the phase's one real subtlety, and it is asserted from both sides: the
 * **close button wins over the resize corner** (a resizable window is still closable), and the **edge band
 * wins over the title bar** (the only way the top edge is reachable at all, since the band is inside the
 * frame). A build that got either backwards would still pass every case that only asked for "a resize".
 */

#include "RoboDesktopInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* 1. Hit testing (13.70)                                              */
/* ------------------------------------------------------------------ */

static void TestInputHitTest(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    DesktopWindowId back = DesktopAddWindow(desktop, "Back", 100, 100, 400, 300);
    DesktopWindowId front = DesktopAddWindow(desktop, "Front", 700, 400, 400, 300);
    GfxRect bounds;
    DesktopInputStep step;
    DesktopHit hit;
    InputTarget target;
    GfxPoint docPoint;
    DesktopWindowId window = RoboDesktopWindowIdNone;

    RoboCheckEqInt(DesktopInputEventSpace(input), RoboInputSpaceScreen);

    /* Every area of a window resolves to a target naming that window, in screen space. */
    RoboCheckEqInt(DesktopWindowBounds(desktop, back, &bounds), RoboOk);
    RoboCheckEqInt(DesktopInputResolvePoint(input, MakeGfxPoint(bounds.x + 20, bounds.y + 20),
                                           RoboInputSpaceScreen, &target, &docPoint, &hit, &window,
                                           NULL),
                   RoboOk);
    RoboCheckEqInt((int)target.kind, (int)RoboInputTargetWindow);
    RoboCheckEqInt((int)target.window, (int)back);
    RoboCheckEqInt((int)target.space, (int)RoboInputSpaceScreen);
    RoboCheckEqInt((int)window, (int)back);
    RoboCheckEqInt((int)hit.area, (int)RoboDesktopAreaWindowTitle);
    RoboCheckEqInt(hit.inside, 1);

    /* Front to back: a point over two windows belongs to the front one. */
    RoboCheckEqInt(DesktopInputResolvePoint(input, MakeGfxPoint(750, 412), RoboInputSpaceScreen,
                                           &target, NULL, &hit, &window, NULL),
                   RoboOk);
    RoboCheckEqInt((int)window, (int)front);
    RoboCheckEqInt((int)hit.area, (int)RoboDesktopAreaWindowTitle);

    /*
     * Outside every window the answer is still a target -- the shell itself, when this build has a native
     * window -- and the point's own plane is echoed. A point outside the *screen* is `RoboNotFound`, because
     * there is nothing this layer can name, which is more useful than inventing one.
     */
    RoboCheckEqInt(DesktopInputResolvePoint(input, MakeGfxPoint(10, 10), RoboInputSpaceScreen, &target,
                                           NULL, &hit, &window, NULL),
                   RoboOk);
    RoboCheckEqInt((int)hit.area, (int)RoboDesktopAreaPanel);
    RoboCheckEqInt((int)window, (int)RoboDesktopWindowIdNone);
    RoboCheckEqInt(DesktopInputResolvePoint(input, MakeGfxPoint(-20, -20), RoboInputSpaceScreen,
                                           &target, NULL, &hit, &window, NULL),
                   RoboNotFound);
    RoboCheckEqInt((int)hit.inside, 0);

    /* Refusals. */
    RoboCheckEqInt(DesktopInputResolvePoint(NULL, MakeGfxPoint(0, 0), RoboInputSpaceScreen, &target,
                                           NULL, NULL, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(DesktopInputResolvePoint(input, MakeGfxPoint(0, 0), RoboInputSpaceScreen, NULL, NULL,
                                           NULL, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(DesktopInputResolvePoint(input, MakeGfxPoint(0, 0), RoboInputSpaceUnknown, &target,
                                           NULL, NULL, NULL, NULL),
                   RoboOk);

    /*
     * An event that resolves to nothing is *dropped* and counted, not delivered to a stranger: that is the
     * difference between "the pointer is over the shell" and "the queue lost the event".
     */
    {
        InputEvent outside = DesktopInputMoveEvent(-40, -40, 0u);

        DesktopInputFeed(input, &outside, &step);
        RoboCheckEqInt((int)step.dropped, 1);
        RoboCheckEqInt((int)step.dispatched, 0);
    }

    DesktopInputClose(input);
    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 2. The window manager's move (13.67)                                */
/* ------------------------------------------------------------------ */

static void TestInputWindowMove(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    DesktopWindowId first = DesktopAddWindow(desktop, "First", 100, 100, 400, 300);
    DesktopWindowId second = DesktopAddWindow(desktop, "Second", 700, 400, 400, 300);
    GfxPoint title = DesktopInputTitlePoint(desktop, first);
    GfxRect before;
    GfxRect after;
    DesktopInputStep step;
    uint32_t moves = 0u;

    RoboCheckEqInt(DesktopWindowBounds(desktop, first, &before), RoboOk);

    /* A press on the title bar raises, focuses and begins the drag; a move carries the window with it. */
    {
        InputEvent down = DesktopInputDownEvent((int32_t)title.x, (int32_t)title.y, 0u);
        InputEvent move = DesktopInputMoveEvent((int32_t)title.x + 60, (int32_t)title.y + 40, 0u);

        DesktopInputFeed(input, &down, &step);
        RoboCheckEqInt((int)DesktopWindowFocused(desktop), (int)first);
        RoboCheckEqInt(DesktopInputIsMovingWindow(input), 1);
        RoboCheckEqInt(step.dragsBegan, 1);
        RoboCheckEqInt((int)DesktopInputGestureWindow(input), (int)first);

        DesktopInputFeed(input, &move, &step);
        moves = step.windowMoves;
        RoboCheckEqInt((int)moves, 1);
    }
    RoboCheckEqInt(DesktopWindowBounds(desktop, first, &after), RoboOk);
    RoboCheckEqInt((int)after.x, (int)(before.x + 60));
    RoboCheckEqInt((int)after.y, (int)(before.y + 40));
    /* A move does not resize. */
    RoboCheckEqInt((int)after.width, (int)before.width);
    RoboCheckEqInt((int)after.height, (int)before.height);

    /*
     * The capture is the whole point: the pointer is now over `second`, and possibly outside `first`
     * entirely, and the *moves* still belong to `first`. A build that re-hit-tested would move whichever
     * window the pointer happened to be over -- which is the class of bug 13.70 names.
     */
    {
        InputEvent move = DesktopInputMoveEvent(760, 450, 0u);
        GfxRect overSecond;

        DesktopInputFeed(input, &move, &step);
        RoboCheckEqInt((int)step.windowMoves, 1);
        RoboCheckEqInt(DesktopWindowBounds(desktop, first, &after), RoboOk);
        RoboCheckEqInt(DesktopWindowBounds(desktop, second, &overSecond), RoboOk);
        RoboCheckEqInt((int)after.x, (int)(760 - (title.x - before.x)));
        /* `second` did not move: a captured pointer manipulates exactly one window. */
        RoboCheckEqInt((int)overSecond.x, 700);
        RoboCheckEqInt((int)overSecond.y, 400);
    }

    /* A release ends it, and a later move moves nothing. */
    {
        InputEvent up = DesktopInputUpEvent(760, 450, 0u);
        InputEvent move;
        GfxRect settled;

        DesktopInputFeed(input, &up, &step);
        RoboCheckEqInt(step.dragsEnded, 1);
        RoboCheckEqInt(DesktopInputIsMovingWindow(input), 0);
        RoboCheckEqInt(DesktopInputIsDragging(input), 0);
        RoboCheckEqInt(DesktopInputIsQuiescent(input), 1);
        RoboCheckEqInt(DesktopWindowBounds(desktop, first, &settled), RoboOk);

        move = DesktopInputMoveEvent(200, 200, 0u);
        DesktopInputFeed(input, &move, &step);
        RoboCheckEqInt((int)step.windowMoves, 0);
        RoboCheckEqInt(DesktopWindowBounds(desktop, first, &after), RoboOk);
        RoboCheckEqInt((int)after.x, (int)settled.x);
        RoboCheckEqInt((int)after.y, (int)settled.y);
    }

    /* A press on the *content* raises and focuses but does not begin a move. */
    {
        GfxPoint client = DesktopInputClientPoint(desktop, first);
        InputEvent down = DesktopInputDownEvent((int32_t)client.x, (int32_t)client.y, 0u);
        InputEvent move = DesktopInputMoveEvent((int32_t)client.x + 30, (int32_t)client.y, 0u);
        GfxRect held;

        DesktopInputFeed(input, &down, &step);
        RoboCheckEqInt(DesktopInputIsMovingWindow(input), 0);
        RoboCheckEqInt(DesktopWindowBounds(desktop, first, &held), RoboOk);
        DesktopInputFeed(input, &move, &step);
        RoboCheckEqInt((int)step.windowMoves, 0);
        RoboCheckEqInt(DesktopWindowBounds(desktop, first, &after), RoboOk);
        RoboCheckEqInt((int)after.x, (int)held.x);
    }

    DesktopInputClose(input);
    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 3. The window manager's resize (13.67)                              */
/* ------------------------------------------------------------------ */

/** Drive one resize and return the frame it ended at. Asserts the zone was recognised. */
static GfxRect DesktopInputResizeOnce(Desktop *desktop,
                                      DesktopInput *input,
                                      DesktopWindowId window,
                                      GfxPoint grab,
                                      int32_t dx,
                                      int32_t dy,
                                      DesktopInputStep *outStep)
{
    GfxRect after;
    DesktopInputStep step;
    InputEvent down = DesktopInputDownEvent((int32_t)grab.x, (int32_t)grab.y, 0u);
    InputEvent move = DesktopInputMoveEvent((int32_t)grab.x + dx, (int32_t)grab.y + dy, 0u);
    InputEvent up = DesktopInputUpEvent((int32_t)grab.x + dx, (int32_t)grab.y + dy, 0u);

    DesktopInputFeed(input, &down, &step);
    RoboCheckEqInt(DesktopInputIsResizingWindow(input), 1);
    RoboCheckEqInt((int)DesktopInputGestureWindow(input), (int)window);
    /* The *move* is the step that carried the resize, so that is the one reported to the caller. */
    DesktopInputFeed(input, &move, &step);
    if (outStep) {
        *outStep = step;
    }
    DesktopInputFeed(input, &up, &step);
    RoboCheckEqInt(DesktopInputIsResizingWindow(input), 0);
    RoboCheckEqInt((int)DesktopInputResizeArea(input), (int)RoboDesktopAreaNone);
    RoboCheckEqInt(DesktopWindowBounds(desktop, window, &after), RoboOk);
    return after;
}

static void TestInputWindowResize(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    DesktopWindowId window = DesktopAddWindow(desktop, "Resizable", 200, 150, 400, 300);
    DesktopWindowMetrics metrics;
    GfxRect bounds;
    DesktopInputStep step;
    GfxRect after;

    DesktopWindowMetricsInit(&metrics);
    RoboCheckEqInt(DesktopWindowBounds(desktop, window, &bounds), RoboOk);
    RoboCheckEqInt((int)metrics.resizeBand, 6);

    /* The right edge: the opposite edge does not move. */
    after = DesktopInputResizeOnce(desktop, input, window,
                                   MakeGfxPoint(bounds.x + bounds.width - 1, bounds.y + 150), 60, 0,
                                   &step);
    RoboCheckEqInt((int)after.x, (int)bounds.x);
    RoboCheckEqInt((int)after.width, (int)(bounds.width + 60));
    RoboCheckEqInt((int)after.height, (int)bounds.height);
    RoboCheckEqInt((int)step.windowResizes, 1);

    /* The left edge: the origin moves and the right edge stays. */
    RoboCheckEqInt(DesktopWindowBounds(desktop, window, &bounds), RoboOk);
    after = DesktopInputResizeOnce(desktop, input, window, MakeGfxPoint(bounds.x, bounds.y + 150), 50, 0,
                                   &step);
    RoboCheckEqInt((int)after.x, (int)(bounds.x + 50));
    RoboCheckEqInt((int)after.width, (int)(bounds.width - 50));
    RoboCheckEqInt((int)(after.x + after.width), (int)(bounds.x + bounds.width));

    /* The bottom edge. */
    RoboCheckEqInt(DesktopWindowBounds(desktop, window, &bounds), RoboOk);
    after = DesktopInputResizeOnce(desktop, input, window,
                                   MakeGfxPoint(bounds.x + 200, bounds.y + bounds.height - 1), 0, 40,
                                   &step);
    RoboCheckEqInt((int)after.y, (int)bounds.y);
    RoboCheckEqInt((int)after.height, (int)(bounds.height + 40));

    /* A corner resizes both axes, and which corner is the *press's* answer rather than the pointer's. */
    RoboCheckEqInt(DesktopWindowBounds(desktop, window, &bounds), RoboOk);
    after = DesktopInputResizeOnce(desktop, input, window,
                                   MakeGfxPoint(bounds.x + bounds.width - 1,
                                                bounds.y + bounds.height - 1),
                                   25, 35, &step);
    RoboCheckEqInt((int)after.width, (int)(bounds.width + 25));
    RoboCheckEqInt((int)after.height, (int)(bounds.height + 35));

    /*
     * The zone is decided once, and it is not the pointer's: a resize that began on the right edge and whose
     * pointer wandered *below* the window's bottom edge still only widens. A build that re-derived the zone
     * would grow the window downwards here, which is the bug this case exists for.
     */
    RoboCheckEqInt(DesktopWindowBounds(desktop, window, &bounds), RoboOk);
    {
        InputEvent down = DesktopInputDownEvent(bounds.x + bounds.width - 1, bounds.y + 100, 0u);
        InputEvent move = DesktopInputMoveEvent(bounds.x + bounds.width + 39,
                                                bounds.y + bounds.height + 200, 0u);
        InputEvent up = DesktopInputUpEvent(bounds.x + bounds.width + 39,
                                            bounds.y + bounds.height + 200, 0u);

        DesktopInputFeed(input, &down, &step);
        RoboCheckEqInt((int)DesktopInputResizeArea(input), (int)RoboDesktopAreaWindowResizeRight);
        DesktopInputFeed(input, &move, &step);
        DesktopInputFeed(input, &up, &step);
        RoboCheckEqInt(DesktopWindowBounds(desktop, window, &after), RoboOk);
        RoboCheckEqInt((int)after.width, (int)(bounds.width + 40));
        RoboCheckEqInt((int)after.height, (int)bounds.height);
        RoboCheckEqInt((int)after.y, (int)bounds.y);
    }

    /* The minimum is a wall, not an inversion: dragging the left edge past the right one stops. */
    RoboCheckEqInt(DesktopWindowBounds(desktop, window, &bounds), RoboOk);
    after = DesktopInputResizeOnce(desktop, input, window, MakeGfxPoint(bounds.x, bounds.y + 100), 4000, 0,
                                   &step);
    RoboCheckEqInt((int)after.width, (int)metrics.minWidth);
    RoboCheckEqInt((int)after.x, (int)(bounds.x + bounds.width - metrics.minWidth));
    /* Dragging the left edge *further right*, past the right one, still stops at the minimum. */
    after = DesktopInputResizeOnce(desktop, input, window, MakeGfxPoint(after.x, after.y + 100), 4000, 0,
                                   &step);
    RoboCheckEqInt((int)after.width, (int)metrics.minWidth);
    RoboCheckEqInt((int)after.x, (int)(bounds.x + bounds.width - metrics.minWidth));

    /* A press in the middle of the title bar is a move, not a resize -- the band is the outer six pixels. */
    {
        GfxPoint title = DesktopInputTitlePoint(desktop, window);
        InputEvent down = DesktopInputDownEvent((int32_t)title.x, (int32_t)title.y, 0u);
        InputEvent up = DesktopInputUpEvent((int32_t)title.x, (int32_t)title.y, 0u);

        DesktopInputFeed(input, &down, &step);
        RoboCheckEqInt(DesktopInputIsResizingWindow(input), 0);
        RoboCheckEqInt(DesktopInputIsMovingWindow(input), 1);
        DesktopInputFeed(input, &up, &step);
    }

    /* And the close button is still reachable on a window that resizes from the top-right corner. */
    {
        GfxRect frame;
        InputEvent down;

        RoboCheckEqInt(DesktopWindowBounds(desktop, window, &frame), RoboOk);
        down = DesktopInputDownEvent(frame.x + frame.width - metrics.border - metrics.closeInset -
                                         metrics.closeSize / 2,
                                     frame.y + metrics.border + metrics.titleHeight / 2, 0u);
        DesktopInputFeed(input, &down, &step);
        RoboCheckEqInt(DesktopInputIsResizingWindow(input), 0);
        RoboCheckEqInt(DesktopInputIsMovingWindow(input), 0);
    }

    DesktopInputClose(input);
    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 4. Coordinate spaces (13.71)                                        */
/* ------------------------------------------------------------------ */

static void TestInputCoordinates(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    DesktopWindowId window = DesktopAddWindow(desktop, "Space", 300, 200, 400, 300);
    InputRoute route;
    GfxRect frame;
    GfxRect client;
    GfxPoint screen = MakeGfxPoint(410, 320);
    GfxPoint windowPoint;
    GfxPoint document;
    GfxPoint back;
    DesktopInputStep step;

    /* The route is the shell's own geometry, read through one door rather than re-derived. */
    RoboCheckEqInt(DesktopInputRouteForWindow(input, window, &route, NULL), RoboOk);
    RoboCheckEqInt(DesktopWindowBounds(desktop, window, &frame), RoboOk);
    RoboCheckEqInt(DesktopWindowClientBounds(desktop, window, &client), RoboOk);
    RoboCheckEqInt(route.hasWindowBounds, 1);
    RoboCheckEqInt(route.hasClientBounds, 1);
    RoboCheckEqInt((int)route.window, (int)window);
    RoboCheckEqInt((int)route.windowBounds.x, (int)frame.x);
    RoboCheckEqInt((int)route.windowBounds.y, (int)frame.y);
    RoboCheckEqInt((int)route.windowBounds.width, (int)frame.width);
    RoboCheckEqInt((int)route.windowBounds.height, (int)frame.height);
    /* The route stores the client rectangle in *window* space: the screen-space rectangle less the frame. */
    RoboCheckEqInt((int)route.clientBounds.x, (int)(client.x - frame.x));
    RoboCheckEqInt((int)route.clientBounds.y, (int)(client.y - frame.y));
    RoboCheckEqInt((int)route.clientBounds.width, (int)client.width);
    RoboCheckEqInt((int)route.clientBounds.height, (int)client.height);

    /* Screen to window moves the origin; window back to screen is the exact inverse. */
    RoboCheckEqInt(InputConvertPoint(screen, RoboInputSpaceScreen, RoboInputSpaceWindow, &route,
                                     &windowPoint, NULL),
                   RoboOk);
    RoboCheckEqInt((int)windowPoint.x, (int)(screen.x - frame.x));
    RoboCheckEqInt((int)windowPoint.y, (int)(screen.y - frame.y));
    RoboCheckEqInt(InputConvertPoint(windowPoint, RoboInputSpaceWindow, RoboInputSpaceScreen, &route,
                                     &back, NULL),
                   RoboOk);
    RoboCheckEqInt((int)back.x, (int)screen.x);
    RoboCheckEqInt((int)back.y, (int)screen.y);

    /* Window to document is the client's offset, and document back to screen round-trips too. */
    RoboCheckEqInt(InputConvertPoint(windowPoint, RoboInputSpaceWindow, RoboInputSpaceDocument, &route,
                                     &document, NULL),
                   RoboOk);
    RoboCheckEqInt((int)document.x, (int)(windowPoint.x - (client.x - frame.x)));
    RoboCheckEqInt((int)document.y, (int)(windowPoint.y - (client.y - frame.y)));
    RoboCheckEqInt(InputConvertPoint(document, RoboInputSpaceDocument, RoboInputSpaceScreen, &route,
                                     &back, NULL),
                   RoboOk);
    RoboCheckEqInt((int)back.x, (int)screen.x);
    RoboCheckEqInt((int)back.y, (int)screen.y);

    /* A no-op pair copies the point through, which is a legitimate question with an obvious answer. */
    RoboCheckEqInt(InputConvertPoint(screen, RoboInputSpaceScreen, RoboInputSpaceScreen, NULL, &back,
                                     NULL),
                   RoboOk);
    RoboCheckEqInt((int)back.x, (int)screen.x);
    RoboCheckEqInt((int)back.y, (int)screen.y);

    /* The plane conversions that need geometry are refused without it rather than guessed at. */
    RoboCheckEqInt(InputConvertPoint(screen, RoboInputSpaceScreen, RoboInputSpaceWindow, NULL, &back,
                                     NULL),
                   RoboInvalidArgument);
    {
        InputRoute frameOnly = DesktopInputRouteFrameOnly(frame.x, frame.y, frame.width, frame.height);

        RoboCheckEqInt(InputConvertPoint(screen, RoboInputSpaceScreen, RoboInputSpaceDocument,
                                        &frameOnly, &back, NULL),
                       RoboNotSupported);
    }

    /*
     * A resolve reports the point in the hit window's *document* plane, which with no registered content is
     * the screen point translated by the client's origin -- the same arithmetic `InputConvertPoint` does, and
     * the same answer, because there is one implementation of it.
     */
    {
        InputTarget target;
        GfxPoint docPoint;
        GfxPoint expected;

        RoboCheckEqInt(DesktopInputResolvePoint(input, screen, RoboInputSpaceScreen, &target,
                                               &docPoint, NULL, NULL, NULL),
                       RoboOk);
        RoboCheckEqInt((int)target.kind, (int)RoboInputTargetWindow);
        RoboCheckEqInt(InputConvertPoint(screen, RoboInputSpaceScreen, RoboInputSpaceDocument, &route,
                                         &expected, NULL),
                       RoboOk);
        RoboCheckEqInt((int)docPoint.x, (int)expected.x);
        RoboCheckEqInt((int)docPoint.y, (int)expected.y);
    }

    /* A window the shell does not hold has no route, which is the honest answer for a gone window. */
    RoboCheckEqInt(DesktopInputRouteForWindow(input, 4242u, &route, NULL), RoboNotFound);
    RoboCheckEqInt(DesktopInputRouteForWindow(input, window, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopInputRouteForWindow(NULL, window, &route, NULL), RoboInvalidArgument);

    /* A move in the *document* plane still lands on the same window, because the point is converted first. */
    {
        InputEvent move = DesktopInputPointerEventIn(RoboInputEventPointerMove,
                                                     (int32_t)windowPoint.x, (int32_t)windowPoint.y,
                                                     RoboMouseButtonNone, 0u, 0u,
                                                     RoboInputSpaceWindow);

        DesktopInputFeed(input, &move, &step);
        RoboCheckEqInt((int)step.dropped, 0);
        RoboCheckEqInt((int)step.dispatched, 1);
    }

    DesktopInputClose(input);
    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 5. The cursor (13.75)                                               */
/* ------------------------------------------------------------------ */

static void TestInputCursor(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    DesktopWindowId window = DesktopAddWindow(desktop, "Shape", 200, 150, 400, 300);
    DesktopWindowMetrics metrics;
    GfxRect bounds;
    GfxPoint title;
    DesktopInputStep step;

    DesktopWindowMetricsInit(&metrics);
    RoboCheckEqInt(DesktopWindowBounds(desktop, window, &bounds), RoboOk);
    title = DesktopInputTitlePoint(desktop, window);

    /* The arrow until something asks otherwise, and the shape follows the *hit test* rather than the path. */
    {
        InputEvent move = DesktopInputMoveEvent(10, 10, 0u);

        DesktopInputFeed(input, &move, &step);
        RoboCheckEqInt((int)DesktopInputCursor(input), (int)RoboInputCursorDefault);
    }

    /*
     * A **window** target's default is the arrow even under its title bar: a window's chrome is the shell's
     * business, which is exactly why the shell declares the shape for the gestures it owns.
     */
    {
        InputEvent move = DesktopInputMoveEvent((int32_t)title.x, (int32_t)title.y, 0u);

        DesktopInputFeed(input, &move, &step);
        RoboCheckEqInt((int)DesktopInputCursor(input), (int)RoboInputCursorDefault);
    }

    /* Dragging a window is a *carry*: the closed hand, and it stays that hand wherever the pointer goes. */
    {
        InputEvent down = DesktopInputDownEvent((int32_t)title.x, (int32_t)title.y, 0u);
        InputEvent move = DesktopInputMoveEvent(1200, 700, 0u);

        DesktopInputFeed(input, &down, &step);
        RoboCheckEqInt((int)DesktopInputCursor(input), (int)RoboInputCursorGrabbing);
        /* The declaration is not re-decided by the hit test as the pointer crosses other surfaces. */
        DesktopInputFeed(input, &move, &step);
        RoboCheckEqInt((int)DesktopInputCursor(input), (int)RoboInputCursorGrabbing);
    }

    /* Releasing restores the shape the hit test asks for, and the override is *removed*, not left behind. */
    {
        InputEvent up = DesktopInputUpEvent(1200, 700, 0u);

        DesktopInputFeed(input, &up, &step);
        RoboCheckEqInt((int)DesktopInputCursor(input), (int)RoboInputCursorDefault);
    }

    /* Each zone asks for the shape of the *drag* rather than of the edge it sits on. */
    RoboCheckEqInt(DesktopWindowBounds(desktop, window, &bounds), RoboOk);
    {
        static const struct {
            int32_t x;
            int32_t y;
            InputCursorType shape;
            DesktopArea area;
        } zones[] = {
            { 0, 150, RoboInputCursorResizeHorizontal, RoboDesktopAreaWindowResizeLeft },
            { -1, 150, RoboInputCursorResizeHorizontal, RoboDesktopAreaWindowResizeRight },
            { 200, 0, RoboInputCursorResizeVertical, RoboDesktopAreaWindowResizeTop },
            { 200, -1, RoboInputCursorResizeVertical, RoboDesktopAreaWindowResizeBottom },
            { 0, 0, RoboInputCursorResizeDiagonal, RoboDesktopAreaWindowResizeTopLeft },
            { -1, 0, RoboInputCursorResizeDiagonal, RoboDesktopAreaWindowResizeTopRight },
            { 0, -1, RoboInputCursorResizeDiagonal, RoboDesktopAreaWindowResizeBottomLeft },
            { -1, -1, RoboInputCursorResizeDiagonal, RoboDesktopAreaWindowResizeBottomRight }
        };
        size_t index;

        for (index = 0u; index < sizeof(zones) / sizeof(zones[0]); index++) {
            int32_t x = bounds.x + (zones[index].x < 0 ? bounds.width - 1 : zones[index].x);
            int32_t y = bounds.y + (zones[index].y < 0 ? bounds.height - 1 : zones[index].y);
            InputEvent down = DesktopInputDownEvent(x, y, 0u);
            InputEvent up = DesktopInputUpEvent(x, y, 0u);

            DesktopInputFeed(input, &down, &step);
            if ((int)DesktopInputResizeArea(input) != (int)zones[index].area) {
                TestFail(__FILE__, __LINE__, "zone %u: area is %s, expected %s", (unsigned)index,
                         DesktopAreaName(DesktopInputResizeArea(input)),
                         DesktopAreaName(zones[index].area));
            }
            if ((int)DesktopInputCursor(input) != (int)zones[index].shape) {
                TestFail(__FILE__, __LINE__, "zone %u: cursor is %s, expected %s", (unsigned)index,
                         InputCursorTypeName(DesktopInputCursor(input)),
                         InputCursorTypeName(zones[index].shape));
            }
            DesktopInputFeed(input, &up, &step);
            RoboCheckEqInt((int)DesktopInputCursor(input), (int)RoboInputCursorDefault);
        }
    }

    /* A move with no gesture is a hit test again, so a stale declaration cannot outlive its gesture. */
    {
        InputEvent move = DesktopInputMoveEvent((int32_t)title.x, (int32_t)title.y, 0u);

        DesktopInputFeed(input, &move, &step);
        RoboCheckEqInt((int)DesktopInputCursor(input), (int)RoboInputCursorDefault);
    }

    /* The dump names the shape in force, which is what a reader checks after a gesture. */
    {
        char dump[RoboDesktopInputDumpMax];
        size_t length = 0u;

        RoboCheckEqInt(DesktopInputWrite(input, dump, sizeof(dump), &length, NULL), RoboOk);
        RoboCheck(strstr(dump, "cursor shape=default") != NULL);
    }

    DesktopInputClose(input);
    DesktopClose(desktop);
}

void TestRegisterDesktopInputPointer(void)
{
    TestRegister("desktop.input.hit_test", TestInputHitTest);
    TestRegister("desktop.input.window_move", TestInputWindowMove);
    TestRegister("desktop.input.window_resize", TestInputWindowResize);
    TestRegister("desktop.input.coordinates", TestInputCoordinates);
    TestRegister("desktop.input.cursor", TestInputCursor);
}
