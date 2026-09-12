#include "server.h"
#include <robosys/syscall.h>
#include <robosys/display.h>

static int is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int display_present(RoboOutput *out, const uint32_t *pixels,
                           uint32_t width, uint32_t height, uint32_t stride,
                           int32_t dest_x, int32_t dest_y)
{
    RoboDisplayPresent req;
    if (!out || !out->ready || !pixels)
        return 0;
    req.pixels = (uint64_t)(uintptr_t)pixels;
    req.width = width;
    req.height = height;
    req.stride = stride;
    req.pixfmt = out->pixfmt;
    req.dest_x = dest_x;
    req.dest_y = dest_y;
    if (is_err(robo_display((int)ROBO_DISP_PRESENT, 0, &req, sizeof(req), 0)))
        return 0;
    return 1;
}

int robo_output_present(RoboOutput *out, const uint32_t *pixels,
                        uint32_t width, uint32_t height, uint32_t stride,
                        int32_t dest_x, int32_t dest_y)
{
    if (!out || !out->present)
        return 0;
    return out->present(out, pixels, width, height, stride, dest_x, dest_y);
}

int robo_output_open_display(RoboOutput *out)
{
    robo_long r;
    RoboUserDisplay info;
    if (!out)
        return 0;
    out->width = 0;
    out->height = 0;
    out->stride = 0;
    out->pixfmt = 0;
    out->ready = 0;
    out->present = display_present;
    r = robo_display((int)ROBO_DISP_CLAIM, 0, 0, 0, (int)ROBO_DISP_ROLE_OUTPUT);
    if (is_err(r))
        return 0;
    if (is_err(robo_display((int)ROBO_DISP_INFO, 0, &info, sizeof(info), 0)))
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
