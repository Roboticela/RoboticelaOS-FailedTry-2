/*
 * The shell's input integration: the loop, the devices, the measurement and the lifecycle
 * (Phase 13.66 / 13.76 / 13.77.7 / 13.77.8 / 13.77.12 / 13.77.13).
 *
 * Four claims, one file, because each is about *the system as a whole* rather than about a gesture or a
 * target: the shell consumes a normalized stream and nothing raw (13.66), one iteration is a bounded pump
 * plus a drain (13.76), what happened can be measured (13.77.12) and what has gone stops being referred to
 * (13.77.13). The pointer geometry and the document delivery are the two suites beside this one.
 *
 * The window cases are here as well as in `desktop.window.*` for the reason 13.66 exists: those assert what
 * the shell does with a *point*, and these assert that the same decisions are reached from an event that
 * travelled through RoboInput -- which is a different claim with a different failure mode (a bridge that
 * dropped the event, a queue that refused it, a route that resolved to nothing).
 */

#include "RoboDesktopInputTestSupport.h"

/* ------------------------------------------------------------------ */
/* 1. The loop (13.66 / 13.76)                                         */
/* ------------------------------------------------------------------ */

static void TestInputLoop(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    DesktopWindowId window = DesktopAddWindow(desktop, "Loop", 100, 100, 400, 300);
    DesktopInputStep step;
    DesktopInputStats stats;
    GfxPoint title = DesktopInputTitlePoint(desktop, window);
    GfxPoint client = DesktopInputClientPoint(desktop, window);
    char line[512];
    size_t length = 0u;

    /* A fresh system holds nothing and has dispatched nothing. */
    RoboCheckEqInt(DesktopInputIsQuiescent(input), 1);
    RoboCheckEqInt(DesktopInputIsDragging(input), 0);
    RoboCheckEqInt(DesktopInputIsResizingWindow(input), 0);
    RoboCheckEqInt(DesktopInputIsMovingWindow(input), 0);
    RoboCheckEqInt((int)DesktopInputDragPhase(input), (int)RoboInputDragIdle);
    RoboCheckEqInt(DesktopInputFocusedTarget(input, NULL), 0);
    RoboCheckEqInt((int)DesktopInputCursor(input), (int)RoboInputCursorDefault);
    RoboCheckEqInt((int)DesktopInputEventSpace(input), (int)RoboInputSpaceScreen);

    /*
     * A move is one window event's worth of nothing: it moves the pointer, changes no target and performs no
     * action. `pointerMoves` is the readback the *software cursor* needs, and it is counted here rather than
     * inferred from the pointer's record.
     */
    {
        InputEvent move = DesktopInputMoveEvent((int32_t)client.x, (int32_t)client.y, 0u);

        DesktopInputFeed(input, &move, &step);
    }
    RoboCheckEqInt((int)step.dispatched, 1);
    RoboCheckEqInt((int)step.pointerMoves, 1);
    RoboCheckEqInt((int)step.actions, 0);
    RoboCheckEqInt(DesktopInputIsQuiescent(input), 1);
    RoboCheckEqInt(DesktopInputStepIsEmpty(&step), 0);

    /*
     * A press on the title bar is the shell's own two decisions reached through the model: raise, and take
     * focus. Both are *actions* -- identified by RoboInput's default-action table and performed here -- which
     * is what makes "the shell consumes the model" a claim about behaviour rather than about inclusion.
     */
    {
        InputEvent down = DesktopInputDownEvent((int32_t)title.x, (int32_t)title.y, 0u);

        DesktopInputFeed(input, &down, &step);
    }
    RoboCheckEqInt((int)step.actions, 1);
    RoboCheckEqInt((int)step.focusChanges, 1);
    RoboCheckEqInt((int)step.dispatched, 1);
    RoboCheckEqInt(step.dragsBegan, 1);
    RoboCheckEqInt((int)DesktopWindowFocused(desktop), (int)window);
    {
        InputTarget focused;

        RoboCheckEqInt(DesktopInputFocusedTarget(input, &focused), 1);
        RoboCheckEqInt((int)focused.kind, (int)RoboInputTargetWindow);
        RoboCheckEqInt((int)focused.window, (int)window);
    }

    /* A release ends the gesture and leaves the pointer captured by nobody. */
    {
        InputEvent up = DesktopInputUpEvent((int32_t)title.x, (int32_t)title.y, 0u);

        DesktopInputFeed(input, &up, &step);
    }
    RoboCheckEqInt(step.dragsEnded, 1);
    RoboCheckEqInt(DesktopInputIsDragging(input), 0);
    RoboCheckEqInt(DesktopInputIsQuiescent(input), 1);

    /* A press on the close button asks the window to close -- still through a bridged event. */
    {
        DesktopWindowMetrics metrics;
        GfxRect bounds;
        InputEvent down;

        DesktopWindowMetricsInit(&metrics);
        RoboCheckEqInt(DesktopWindowBounds(desktop, window, &bounds), RoboOk);
        down = DesktopInputDownEvent(bounds.x + bounds.width - metrics.border - metrics.closeInset -
                                         metrics.closeSize / 2,
                                     bounds.y + metrics.border + metrics.titleHeight / 2, 0u);
        DesktopInputFeed(input, &down, &step);
    }
    {
        DesktopWindowState state = RoboDesktopWindowOpen;

        RoboCheckEqInt(GetDesktopWindowState(desktop, window, &state), RoboOk);
        RoboCheckEqInt((int)state, (int)RoboDesktopWindowClosing);
    }

    /* The counters accumulate, and the step line is complete or absent rather than truncated. */
    RoboCheckEqInt(GetDesktopInputStats(input, &stats), RoboOk);
    RoboCheck(stats.steps >= 4u);
    RoboCheck(stats.dispatched >= 4u);
    RoboCheck(stats.actions >= 1u);
    RoboCheckEqInt((int)stats.dropped, 0);
    RoboCheckEqInt(DesktopInputStepWrite(&step, line, sizeof(line), &length, NULL), RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(line, "dispatched=") != NULL);
    RoboCheckEqInt(DesktopInputStepWrite(&step, line, 8u, &length, NULL), RoboNotSupported);
    RoboCheckEqInt(DesktopInputStepWrite(NULL, line, sizeof(line), &length, NULL), RoboInvalidArgument);

    /* The whole system as one block, which is what a reader diffs. */
    {
        char dump[RoboDesktopInputDumpMax];
        size_t dumpLength = 0u;

        RoboCheckEqInt(DesktopInputWrite(input, dump, sizeof(dump), &dumpLength, NULL), RoboOk);
        RoboCheck(dumpLength > 0u);
        RoboCheck(strstr(dump, "desktop-input ") != NULL);
        RoboCheck(strstr(dump, "\ngesture moving=") != NULL);
        RoboCheck(strstr(dump, "\nlatency traced=") != NULL);
        RoboCheckEqInt(DesktopInputWrite(input, dump, 16u, &dumpLength, NULL), RoboNotSupported);
    }

    /* Refusals: every door that needs a system says so rather than crashing. */
    RoboCheckEqInt(GetDesktopInputStep(NULL, 0u, &step, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopInputPush(NULL, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopInputCreate(NULL, NULL, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopInputCreate(desktop, NULL, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(GetDesktopInputStats(NULL, &stats), RoboInvalidArgument);
    RoboCheckEqInt(GetDesktopInputStats(input, NULL), RoboInvalidArgument);
    RoboCheckEqInt(GetDesktopInputLatency(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(DesktopInputIsQuiescent(NULL), 1);
    RoboCheckEqInt(DesktopInputDeviceCount(NULL), 0u);
    RoboCheckEqInt(DesktopInputListenerCount(NULL), 0u);
    RoboCheckEqInt((int)DesktopInputCursor(NULL), (int)RoboInputCursorDefault);
    RoboCheckEqInt(DesktopInputIsLive(NULL), 0);
    RoboCheck(DesktopInputDesktop(NULL) == NULL);
    RoboCheck(DesktopInputDesktop(input) == desktop);
    DesktopInputDestroy(NULL);

    DesktopInputClose(input);
    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 2. Devices (13.44 / 13.77.7)                                        */
/* ------------------------------------------------------------------ */

static void TestInputDevices(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    InputSessionSweep sweep;
    InputDeviceId keyboard = RoboInputDeviceIdNone;
    InputDeviceId mouse = RoboInputDeviceIdNone;

    /* The constructor connected one of each; a second pair is what a hotplug looks like from here. */
    RoboCheckEqInt((int)DesktopInputDeviceCount(input), 2);
    RoboCheckEqInt(DesktopInputConnectKeyboard(input, &keyboard, NULL), RoboOk);
    RoboCheck(keyboard != RoboInputDeviceIdNone);
    RoboCheckEqInt(DesktopInputConnectMouse(input, &mouse, NULL), RoboOk);
    RoboCheck(mouse != RoboInputDeviceIdNone);
    RoboCheckEqInt((int)DesktopInputDeviceCount(input), 4);

    /* Disconnecting sweeps what it held, and an id the session does not own is refused with no sweep. */
    InputSessionSweepInit(&sweep);
    RoboCheckEqInt(DesktopInputDisconnect(input, mouse, &sweep, NULL), RoboOk);
    RoboCheckEqInt((int)DesktopInputDeviceCount(input), 3);
    RoboCheckEqInt(DesktopInputDisconnect(input, 4096u, &sweep, NULL), RoboNotFound);
    RoboCheckEqInt(DesktopInputDisconnect(NULL, 1u, &sweep, NULL), RoboInvalidArgument);

    /* A reset takes every transient thing and leaves the devices and the focus alone. */
    {
        InputEvent down = DesktopInputDownEvent(200, 150, 0u);
        DesktopInputStep step;

        DesktopInputFeed(input, &down, &step);
        RoboCheckEqInt(DesktopInputIsQuiescent(input), 0);
        RoboCheckEqInt(DesktopInputResetTransient(input, &sweep, NULL), RoboOk);
        RoboCheckEqInt(DesktopInputIsQuiescent(input), 1);
        RoboCheckEqInt((int)DesktopInputDeviceCount(input), 3);
    }

    DesktopInputClose(input);
    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 3. Measurement (13.77.12)                                           */
/* ------------------------------------------------------------------ */

static void TestInputLatency(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInputClock clock;
    DesktopInput *input = DesktopInputOpenWithClock(desktop, &clock);
    DesktopInputLatency latency;
    DesktopInputStep step;
    char line[256];
    size_t length = 0u;
    uint32_t index;

    DesktopAddWindow(desktop, "Measured", 100, 100, 400, 300);

    /*
     * A hundred events with a hand-advanced clock, so the numbers are the *test's* rather than the machine's:
     * each event's hardware stamp is one millisecond behind the clock the loop reads, so queue-to-dispatch is
     * exactly one. Handler-to-dispatch is zero because the handler returns within the same millisecond -- the
     * separation the four numbers exist to expose, and a build that folded them would report a nonzero
     * handler here.
     */
    for (index = 0u; index < 100u; index++) {
        InputEvent move = DesktopInputMoveEvent(300, 200, 0u);

        InputEventSetTimestamp(&move, clock.ms);
        DesktopInputClockAdvance(&clock, 1u);
        DesktopInputFeed(input, &move, &step);
        RoboCheckEqInt((int)step.traced, 1);
    }

    RoboCheckEqInt(GetDesktopInputLatency(input, &latency), RoboOk);
    RoboCheckEqInt((int)latency.traced, 100);
    RoboCheckEqInt((int)latency.completed, 100);
    /* Every event took exactly one millisecond from the queue to its handler's return. */
    RoboCheckEqInt((int)latency.handlerLastMs, 0);
    RoboCheckEqInt((int)latency.handlerMaxMs, 0);
    RoboCheckEqInt((int)latency.handlerSumMs, 0);
    RoboCheckEqInt((int)latency.dispatchLastMs, 1);
    RoboCheckEqInt((int)latency.frameLastMs, 1);

    RoboCheckEqInt(DesktopInputLatencyWrite(&latency, line, sizeof(line), &length, NULL), RoboOk);
    RoboCheck(length > 0u);
    RoboCheck(strstr(line, "latency traced=100 completed=100") != NULL);
    RoboCheck(strstr(line, "handler last=0 max=0 sum=0") != NULL);
    RoboCheckEqInt(DesktopInputLatencyWrite(&latency, line, 4u, &length, NULL), RoboNotSupported);
    RoboCheckEqInt(DesktopInputLatencyWrite(NULL, line, sizeof(line), &length, NULL),
                   RoboInvalidArgument);

    DesktopInputClose(input);
    DesktopClose(desktop);
}

/*
 * The absence beside the claim: a system built with **no clock** measured nothing, and says so with zeroes
 * rather than with a plausible small number. That is the rule `RoboInputLatency.h` states one module down
 * and this layer keeps instead of normalizing.
 */
static void TestInputLatencyWithoutClock(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    DesktopInputLatency latency;
    DesktopInputStep step;
    InputEvent move = DesktopInputMoveEvent(300, 200, 0u);

    DesktopAddWindow(desktop, "Unmeasured", 100, 100, 400, 300);
    DesktopInputFeed(input, &move, &step);
    RoboCheckEqInt((int)step.traced, 0);
    RoboCheckEqInt((int)step.slowestHandlerMs, 0);
    RoboCheckEqInt((int)step.dispatched, 1);

    RoboCheckEqInt(GetDesktopInputLatency(input, &latency), RoboOk);
    RoboCheckEqInt((int)latency.traced, 0);
    RoboCheckEqInt((int)latency.completed, 0);
    RoboCheckEqInt((int)latency.handlerMaxMs, 0);
    RoboCheckEqInt((int)latency.frameMaxMs, 0);

    DesktopInputClose(input);
    DesktopClose(desktop);
}

/* ------------------------------------------------------------------ */
/* 4. Capabilities (13.77)                                             */
/* ------------------------------------------------------------------ */

static void TestInputCapabilities(void)
{
    DesktopInputCapabilities caps;
    DesktopInputCapabilities other;

    RoboCheckEqInt(GetDesktopInputCapabilities(NULL), RoboInvalidArgument);
    RoboCheckEqInt(GetDesktopInputCapabilities(&caps), RoboOk);

    /* Every seam 13.66--13.77 names, and each is a door this build has. */
    RoboCheckEqInt(caps.shellConsumption, 1);
    RoboCheckEqInt(caps.windowManagement, 1);
    RoboCheckEqInt(caps.windowMoving, 1);
    RoboCheckEqInt(caps.windowResizing, 1);
    RoboCheckEqInt(caps.documentDelivery, 1);
    RoboCheckEqInt(caps.focusDelivery, 1);
    RoboCheckEqInt(caps.pointerHitTesting, 1);
    RoboCheckEqInt(caps.pointerCapture, 1);
    RoboCheckEqInt(caps.coordinateSpaces, 1);
    RoboCheckEqInt(caps.keyboardDelivery, 1);
    RoboCheckEqInt(caps.textDelivery, 1);
    RoboCheckEqInt(caps.shortcutRouting, 1);
    RoboCheckEqInt(caps.commandRouting, 1);
    RoboCheckEqInt(caps.clipboard, 1);
    RoboCheckEqInt(caps.dragDrop, 1);
    RoboCheckEqInt(caps.cursorManagement, 1);
    RoboCheckEqInt(caps.eventLoop, 1);
    RoboCheckEqInt(caps.inputCoalescing, 1);
    RoboCheckEqInt(caps.inputLatency, 1);
    RoboCheckEqInt(caps.lifecycleSweep, 1);
    RoboCheckEqInt(caps.introspection, 1);

    /* The three absences, named. A build that grew one would flip one of these and nothing else. */
    RoboCheckEqInt(caps.windowSnapping, 0);
    RoboCheckEqInt(caps.frameScheduling, 0);
    RoboCheckEqInt(caps.cursorImages, 0);

    RoboCheckEqInt((int)caps.defaultQueueCapacity, (int)RoboDesktopInputDefaultQueueCapacity);
    RoboCheckEqInt((int)caps.maxContents, (int)RoboDesktopInputMaxContents);
    RoboCheckEqInt((int)caps.maxListeners, (int)RoboDesktopInputMaxListeners);
    /* Thirteen shapes since 13.75 added `grabbing`, and eight resize zones since 13.67. */
    RoboCheckEqInt((int)caps.cursorShapes, (int)RoboInputCursorTypeCount);
    RoboCheckEqInt((int)caps.cursorShapes, 13);
    RoboCheckEqInt((int)caps.resizeZones, 8);
    RoboCheck(caps.version != NULL);
    RoboCheck(strcmp(caps.version, RoboInputVersionString) == 0);

    /* Two reads agree field for field: the table is a value and not a counter somebody advances. */
    RoboCheckEqInt(GetDesktopInputCapabilities(&other), RoboOk);
    RoboCheck(memcmp(&caps, &other, sizeof(caps)) == 0);
}

/* ------------------------------------------------------------------ */
/* 5. Lifecycle (13.77.8 / 13.77.13)                                   */
/* ------------------------------------------------------------------ */

static void TestInputLifecycle(void)
{
    Desktop *desktop = DesktopOpen(1280, 800);
    DesktopInput *input = DesktopInputOpen(desktop);
    DesktopWindowId window = DesktopAddWindow(desktop, "Life", 100, 100, 400, 300);
    DesktopWindowId other = DesktopAddWindow(desktop, "Other", 700, 400, 400, 300);
    InputTarget target = DesktopInputShellWindowTarget(window);
    DesktopInputLog log;
    DesktopInputForget forget;
    GfxPoint title = DesktopInputTitlePoint(desktop, window);
    GfxPoint client = DesktopInputClientPoint(desktop, window);

    DesktopInputLogInit(&log);
    DesktopInputLogOn(input, &target, RoboInputPhaseTarget, &log);

    /* A listener registered and then forgotten hears nothing more. */
    {
        InputEvent move = DesktopInputMoveEvent((int32_t)client.x, (int32_t)client.y, 0u);
        DesktopInputStep step;
        uint32_t removed;

        DesktopInputFeed(input, &move, &step);
        RoboCheck(log.count > 0u);
        RoboCheckEqInt((int)DesktopInputListenerCount(input), 3); /* the shell's own two, plus this one */
        removed = DesktopInputRemoveListeners(input, &target);
        RoboCheckEqInt((int)removed, 1);
        RoboCheckEqInt((int)DesktopInputListenerCount(input), 2);
        DesktopInputLogInit(&log);
        DesktopInputFeed(input, &move, &step);
        RoboCheckEqInt((int)log.count, 0);
        RoboCheckEqInt(DesktopInputRemoveListeners(NULL, &target), 0u);
        RoboCheckEqInt(DesktopInputRemoveListeners(input, NULL), 0u);
    }

    /* A target that is destroyed everywhere it was held reports both places. */
    {
        InputTarget released;

        DesktopInputLogInit(&log);
        InputTargetInit(&released);
        RoboCheckEqInt(InputTargetSetWindow(&released, (WindowId)other, RoboInputSpaceScreen, NULL),
                       RoboOk);
        DesktopInputForgetInit(&forget);
        RoboCheckEqInt(forget.listenersRemoved + forget.focusReleased + forget.capturesReleased, 0u);
        RoboCheckEqInt(DesktopInputForgetTarget(input, &released, &forget, NULL), RoboOk);
        RoboCheckEqInt((int)forget.listenersRemoved, 0);
        RoboCheckEqInt(DesktopInputForgetTarget(NULL, &released, &forget, NULL), RoboInvalidArgument);
        RoboCheckEqInt(DesktopInputForgetTarget(input, NULL, &forget, NULL), RoboInvalidArgument);
    }

    /*
     * A window that has gone takes its listeners, its focus, its captures and its contents with it. The
     * gesture is the interesting half: a *move* drag is running when the window is destroyed, and a gesture
     * that outlived its window would be a pointer captured for good.
     */
    {
        InputEvent down = DesktopInputDownEvent((int32_t)title.x, (int32_t)title.y, 0u);
        InputEvent move;
        DesktopInputStep step;
        DesktopInputContent content;

        DesktopInputLogInit(&log);
        DesktopInputLogOn(input, &target, RoboInputPhaseTarget, &log);
        DesktopInputFeed(input, &down, &step);
        RoboCheckEqInt(DesktopInputIsMovingWindow(input), 1);
        RoboCheckEqInt((int)DesktopInputGestureWindow(input), (int)window);
        RoboCheckEqInt(DesktopInputIsQuiescent(input), 0);

        DesktopInputContentInit(&content);
        content.window = window;
        RoboCheckEqInt(DesktopInputSetContent(input, &content, NULL), RoboOk);

        move = DesktopInputMoveEvent((int32_t)title.x + 40, (int32_t)title.y + 20, 0u);
        DesktopInputFeed(input, &move, &step);
        RoboCheckEqInt((int)step.windowMoves, 1);

    DesktopInputForgetInit(&forget);
    RoboCheckEqInt(DesktopInputForgetWindow(input, window, &forget, NULL), RoboOk);
    RoboCheckEqInt((int)forget.listenersRemoved >= 1, 1);
    RoboCheckEqInt((int)forget.dragCancelled, 1);
        RoboCheckEqInt((int)forget.contentCleared, 1);
        RoboCheckEqInt((int)forget.focusReleased >= 1, 1);
        RoboCheckEqInt(DesktopInputIsMovingWindow(input), 0);
        RoboCheckEqInt(DesktopInputIsDragging(input), 0);
        RoboCheckEqInt((int)DesktopInputGestureWindow(input), (int)RoboDesktopWindowIdNone);
        RoboCheckEqInt(DesktopWindowExists(desktop, window), 1); /* forgetting is not destroying */
        RoboCheckEqInt(DesktopInputClearContent(input, window, NULL), RoboNotFound);
    }

    /* Refusals for a window that is not one, and for a NULL system. */
    RoboCheckEqInt(DesktopInputForgetWindow(input, RoboDesktopWindowIdNone, &forget, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(DesktopInputForgetWindow(NULL, window, &forget, NULL), RoboInvalidArgument);

    /*
     * Focus on a window the shell has since destroyed: a key must not be routed at it. Nobody calls
     * `DesktopInputForgetWindow` in this case, which is the point -- the layer notices.
     */
    {
        InputEvent key = DesktopInputKeyDown(RoboPhysicalKeyA, 0u);
        DesktopInputStep step;
        InputTarget focused;
        InputEvent down;

        /* The earlier drag moved the window, so the title point is recomputed rather than reused. */
        title = DesktopInputTitlePoint(desktop, window);
        down = DesktopInputDownEvent((int32_t)title.x, (int32_t)title.y, 0u);
        {
            InputEvent up = DesktopInputUpEvent((int32_t)title.x, (int32_t)title.y, 0u);

            DesktopInputFeed(input, &down, &step);
            DesktopInputFeed(input, &up, &step);
        }
        RoboCheckEqInt((int)DesktopInputFocusedTarget(input, &focused), 1);
        RoboCheckEqInt((int)focused.window, (int)window);
        RoboCheckEqInt(DesktopWindowDestroy(desktop, window), RoboOk);

        DesktopInputFeed(input, &key, &step);
        /* The key still went somewhere -- the window left, and the front one is where a key goes. */
        RoboCheckEqInt((int)step.dropped, 0);
        RoboCheckEqInt((int)DesktopInputFocusedTarget(input, &focused), 0);
    }

    /* And the front window still takes a key. */
    {
        InputEvent key = DesktopInputKeyDown(RoboPhysicalKeyB, 0u);
        DesktopInputStep step;
        InputTarget focused;

        DesktopInputFeed(input, &key, &step);
        RoboCheckEqInt((int)step.dropped, 0);
        RoboCheckEqInt((int)step.dispatched, 1);
        RoboCheckEqInt((int)DesktopInputFocusedTarget(input, &focused), 0);
        /* Nothing is focused, so the event went to the front window -- and the window is `other`. */
        RoboCheckEqInt(DesktopWindowExists(desktop, other), 1);
    }

    /* The dump is still obtainable after all of that, and reports the gesture state it ended in. */
    {
        char dump[RoboDesktopInputDumpMax];
        size_t length = 0u;

        RoboCheckEqInt(DesktopInputWrite(input, dump, sizeof(dump), &length, NULL), RoboOk);
        RoboCheck(strstr(dump, "gesture moving=0 resizing=0") != NULL);
    }

    DesktopInputClose(input);
    DesktopClose(desktop);
}

void TestRegisterDesktopInput(void)
{
    TestRegister("desktop.input.loop", TestInputLoop);
    TestRegister("desktop.input.devices", TestInputDevices);
    TestRegister("desktop.input.latency", TestInputLatency);
    TestRegister("desktop.input.latency_without_clock", TestInputLatencyWithoutClock);
    TestRegister("desktop.input.capabilities", TestInputCapabilities);
    TestRegister("desktop.input.lifecycle", TestInputLifecycle);
}
