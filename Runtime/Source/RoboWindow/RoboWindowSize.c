#include "RoboWindow/RoboWindowSize.h"

#include <string.h>

void WindowSizeConstraintsInit(WindowSizeConstraints *c)
{
    if (!c) {
        return;
    }
    memset(c, 0, sizeof(*c));
}

Result WindowSizeConstraintsValidate(
    const WindowSizeConstraints *c)
{
    if (!c) {
        return RoboInvalidArgument;
    }
    if (c->minWidth < 0 || c->minHeight < 0 || c->maxWidth < 0 ||
        c->maxHeight < 0) {
        return RoboInvalidArgument;
    }
    if (c->minWidth > RoboWindowMaxDim || c->minHeight > RoboWindowMaxDim) {
        return RoboInvalidArgument;
    }
    if (c->maxWidth > RoboWindowMaxDim || c->maxHeight > RoboWindowMaxDim) {
        return RoboInvalidArgument;
    }
    if (c->minWidth > 0 && c->maxWidth > 0 && c->minWidth > c->maxWidth) {
        return RoboInvalidArgument;
    }
    if (c->minHeight > 0 && c->maxHeight > 0 &&
        c->minHeight > c->maxHeight) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

Result WindowSizeValidate(int32_t width, int32_t height)
{
    if (width <= 0 || height <= 0) {
        return RoboInvalidArgument;
    }
    if (width > RoboWindowMaxDim || height > RoboWindowMaxDim) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

Result WindowSizeValidateAgainstConstraints(
    int32_t width,
    int32_t height,
    const WindowSizeConstraints *c)
{
    Result r = WindowSizeValidate(width, height);
    if (r != RoboOk) {
        return r;
    }
    if (!c) {
        return RoboOk;
    }
    r = WindowSizeConstraintsValidate(c);
    if (r != RoboOk) {
        return r;
    }
    if (c->minWidth > 0 && width < c->minWidth) {
        return RoboInvalidArgument;
    }
    if (c->minHeight > 0 && height < c->minHeight) {
        return RoboInvalidArgument;
    }
    if (c->maxWidth > 0 && width > c->maxWidth) {
        return RoboInvalidArgument;
    }
    if (c->maxHeight > 0 && height > c->maxHeight) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}
