#include "RoboWindow/RoboWindowState.h"

#include <string.h>

void WindowStateInit(WindowState *st)
{
    if (!st) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->lifecycle = RoboWindowLifecycleUninitialized;
    st->x = RoboWindowCoordUnknown;
    st->y = RoboWindowCoordUnknown;
    st->requestedX = RoboWindowCoordUnknown;
    st->requestedY = RoboWindowCoordUnknown;
    st->scale = 0.0f;
    st->displayId = RoboDisplayIdNone;
}
