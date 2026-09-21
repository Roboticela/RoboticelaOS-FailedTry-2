#include "server.h"
#include <robosys/syscall.h>
#include <robosys/display.h>

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int DisplayPresent(RoboOutput *out, const uint32_t *pixels,
                           uint32_t width, uint32_t height, uint32_t stride,
                           int32_t destX, int32_t destY)
{
    RoboDisplayPresent req;
    if (!out || !out->ready || !pixels)
        return 0;
    req.pixels = (uint64_t)(uintptr_t)pixels;
    req.width = width;
    req.height = height;
    req.stride = stride;
    req.pixfmt = out->pixfmt;
    req.destX = destX;
    req.destY = destY;
    if (IsErr(Display((int)RoboDispPresent, 0, &req, sizeof(req), 0)))
        return 0;
    return 1;
}

int OutputPresent(RoboOutput *out, const uint32_t *pixels,
                        uint32_t width, uint32_t height, uint32_t stride,
                        int32_t destX, int32_t destY)
{
    if (!out || !out->present)
        return 0;
    return out->present(out, pixels, width, height, stride, destX, destY);
}

int OutputOpenDisplay(RoboOutput *out)
{
    Long r;
    RoboUserDisplay info;
    if (!out)
        return 0;
    out->width = 0;
    out->height = 0;
    out->stride = 0;
    out->pixfmt = 0;
    out->ready = 0;
    out->present = DisplayPresent;
    r = Display((int)RoboDispClaim, 0, 0, 0, (int)RoboDispRoleOutput);
    if (IsErr(r))
        return 0;
    if (IsErr(Display((int)RoboDispInfo, 0, &info, sizeof(info), 0)))
        return 1;
    if (!info.ready || !info.width || !info.height)
        return 1;
    out->width = info.width;
    out->height = info.height;
    out->stride = info.width;
    out->pixfmt = info.pixfmt;
    out->ready = 1;
    return 1;
}
