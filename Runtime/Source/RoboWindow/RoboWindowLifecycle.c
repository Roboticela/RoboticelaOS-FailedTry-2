#include "RoboWindow/RoboWindowLifecycle.h"

#include <stddef.h>

int WindowLifecycleIsKnown(WindowLifecycle life)
{
    return life == RoboWindowLifecycleUninitialized ||
           life == RoboWindowLifecycleCreated ||
           life == RoboWindowLifecycleClosing ||
           life == RoboWindowLifecycleClosed;
}

int WindowLifecycleCanTransition(WindowLifecycle from,
                                         WindowLifecycle to)
{
    if (!WindowLifecycleIsKnown(from) ||
        !WindowLifecycleIsKnown(to)) {
        return 0;
    }
    if (from == RoboWindowLifecycleUninitialized &&
        to == RoboWindowLifecycleCreated) {
        return 1;
    }
    if (from == RoboWindowLifecycleCreated &&
        (to == RoboWindowLifecycleClosing ||
         to == RoboWindowLifecycleClosed)) {
        return 1;
    }
    if (from == RoboWindowLifecycleClosing &&
        to == RoboWindowLifecycleClosed) {
        return 1;
    }
    if (from == RoboWindowLifecycleClosed &&
        to == RoboWindowLifecycleClosed) {
        return 1;
    }
    return 0;
}

Result WindowLifecycleCheckTransition(WindowLifecycle from,
                                                     WindowLifecycle to)
{
    return WindowLifecycleCanTransition(from, to) ? RoboOk
                                                          : RoboInvalidArgument;
}

const char *WindowLifecycleName(WindowLifecycle life)
{
    switch (life) {
    case RoboWindowLifecycleUninitialized:
        return "uninitialized";
    case RoboWindowLifecycleCreated:
        return "created";
    case RoboWindowLifecycleClosing:
        return "closing";
    case RoboWindowLifecycleClosed:
        return "closed";
    default:
        return "unknown";
    }
}

int WindowLifecycleAllowsMutation(WindowLifecycle life)
{
    return life == RoboWindowLifecycleCreated;
}
