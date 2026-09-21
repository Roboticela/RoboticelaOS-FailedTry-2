#include "RoboWindow/RoboWindowEventLoop.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackend.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static int gLoopRunning;
static int gStopRequested;
static int gPumpDepth;
static int gWakePending;

void WindowEventLoopReset(void)
{
    gLoopRunning = 0;
    gStopRequested = 0;
    gPumpDepth = 0;
    gWakePending = 0;
}

int WindowEventLoopIsRunning(void)
{
    return gLoopRunning;
}

int WindowEventLoopIsPumping(void)
{
    return gPumpDepth > 0;
}

Result WindowEventLoopStart(void)
{
    Result r;

    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    r = WindowThreadRequireOwner();
    if (r != RoboOk) {
        return r;
    }
    gStopRequested = 0;
    gLoopRunning = 1;
    LogDebug("robo_window: event loop start");
    return RoboOk;
}

Result WindowEventLoopRequestStop(void)
{
    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    gStopRequested = 1;
    gLoopRunning = 0;
    gWakePending = 1;
    (void)WindowEventLoopWakeup();
    LogDebug("robo_window: event loop stop requested");
    return RoboOk;
}

Result WindowEventLoopWakeup(void)
{
    const WindowBackendVtbl *vtbl;
    Result r = RoboOk;

    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    gWakePending = 1;
    vtbl = GetWindowBackendVtbl();
    if (vtbl && vtbl->wakeup) {
        r = vtbl->wakeup(WindowBackendUserdata());
        if (r != RoboOk && r != RoboNotSupported) {
            LogDebug("robo_window: backend wakeup failed (%d)", (int)r);
            return r;
        }
    }
    return RoboOk;
}

static Result PumpNative(void)
{
    const WindowBackendVtbl *vtbl;
    WindowBackendCaps caps;

    if (WindowBackendCapabilities(&caps) != RoboOk ||
        !caps.ProcessEvents) {
        return RoboNotSupported;
    }
    vtbl = GetWindowBackendVtbl();
    if (!vtbl || !vtbl->ProcessEvents) {
        return RoboNotSupported;
    }
    return vtbl->ProcessEvents(WindowBackendUserdata());
}

static Result WaitNative(uint32_t timeoutMs)
{
    const WindowBackendVtbl *vtbl;
    WindowBackendCaps caps;
    Result r;

    if (gWakePending || gStopRequested) {
        gWakePending = 0;
        return RoboOk;
    }
    vtbl = GetWindowBackendVtbl();
    if (vtbl && vtbl->WaitEvents) {
        if (WindowBackendCapabilities(&caps) == RoboOk &&
            caps.WaitEvents) {
            r = vtbl->WaitEvents(WindowBackendUserdata(), timeoutMs);
            gWakePending = 0;
            return r;
        }
    }
    /* Fallback: non-blocking poll only (no Sleep in freestanding core). */
    (void)timeoutMs;
    gWakePending = 0;
    return RoboOk;
}

Result WindowEventLoopPoll(void)
{
    Result r;

    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    r = WindowThreadRequireOwner();
    if (r != RoboOk) {
        return r;
    }
    if (gPumpDepth > 0) {
        LogDebug("robo_window: event loop poll reentrant → BUSY");
        return RoboBusy;
    }
    gPumpDepth++;
    r = PumpNative();
    gPumpDepth--;
    return r;
}

Result WindowEventLoopRunOnce(uint32_t timeoutMs)
{
    Result r;

    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    r = WindowThreadRequireOwner();
    if (r != RoboOk) {
        return r;
    }
    if (gPumpDepth > 0) {
        return RoboBusy;
    }
    if (gStopRequested) {
        return RoboOk;
    }
    gPumpDepth++;
    if (timeoutMs != 0u) {
        r = WaitNative(timeoutMs);
        if (r != RoboOk) {
            gPumpDepth--;
            return r;
        }
    }
    r = PumpNative();
    gPumpDepth--;
    return r;
}

Result WindowEventLoopRun(void)
{
    Result r;

    r = WindowEventLoopStart();
    if (r != RoboOk) {
        return r;
    }
    while (gLoopRunning && !gStopRequested) {
        r = WindowEventLoopRunOnce(RoboWindowEventWaitForever);
        if (r != RoboOk) {
            gLoopRunning = 0;
            return r;
        }
    }
    gLoopRunning = 0;
    return RoboOk;
}
