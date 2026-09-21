#include "RoboWindow/RoboWindowThread.h"
#include "RoboWindow/RoboWindow.h"

#include "RoboBase/RoboLog.h"

static WindowThreadId gOwner = RoboWindowThreadNone;
static WindowThreadId gCurrent = RoboWindowThreadNone;

void WindowThreadReset(void)
{
    gOwner = RoboWindowThreadNone;
    gCurrent = RoboWindowThreadNone;
}

Result WindowThreadBindOwner(WindowThreadId tid)
{
    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    gOwner = tid;
    if (tid == RoboWindowThreadNone) {
        LogDebug("robo_window: owner thread unbound");
    } else {
        LogDebug("robo_window: owner thread bound id=%llu",
                       (unsigned long long)tid);
    }
    return RoboOk;
}

WindowThreadId WindowThreadOwner(void)
{
    return gOwner;
}

Result WindowThreadEnter(WindowThreadId tid)
{
    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    gCurrent = tid;
    return RoboOk;
}

WindowThreadId WindowThreadCurrent(void)
{
    return gCurrent;
}

Result WindowThreadRequireOwner(void)
{
    if (gOwner == RoboWindowThreadNone) {
        return RoboOk;
    }
    if (gCurrent != gOwner) {
        LogWarn("robo_window: wrong thread (owner=%llu current=%llu)",
                      (unsigned long long)gOwner,
                      (unsigned long long)gCurrent);
        return RoboPermissionDenied;
    }
    return RoboOk;
}
