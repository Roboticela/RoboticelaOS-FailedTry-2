#include "RoboWindow/RoboWindowNative.h"

#include <stddef.h>

void WindowNativeHandleClear(WindowNativeHandle *h)
{
    if (!h) {
        return;
    }
    h->token = 0u;
    h->generation = 0u;
}

int WindowNativeHandleIsValid(const WindowNativeHandle *h)
{
    return h != NULL && h->token != 0u && h->generation != 0u;
}

int WindowNativeHandleIsEmpty(const WindowNativeHandle *h)
{
    return !WindowNativeHandleIsValid(h);
}

int WindowNativeHandleEqual(const WindowNativeHandle *a,
                                    const WindowNativeHandle *b)
{
    if (!WindowNativeHandleIsValid(a) ||
        !WindowNativeHandleIsValid(b)) {
        return 0;
    }
    return a->token == b->token && a->generation == b->generation;
}

Result WindowNativeHandleAssign(WindowNativeHandle *h,
                                               uint64_t token,
                                               uint32_t generation)
{
    if (!h || token == 0u || generation == 0u) {
        return RoboInvalidArgument;
    }
    h->token = token;
    h->generation = generation;
    return RoboOk;
}

void WindowNativeHandleInvalidate(WindowNativeHandle *h)
{
    WindowNativeHandleClear(h);
}
