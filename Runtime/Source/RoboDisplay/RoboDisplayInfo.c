#include "RoboDisplay/RoboDisplayInfo.h"

#include <string.h>

void DisplayInfoInit(DisplayInfo *info)
{
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->id = RoboDisplayIdNone;
    info->bounds = MakeGfxRect(0, 0, 0, 0);
    info->workArea = MakeGfxRect(0, 0, 0, 0);
    info->orientation = RoboDisplayOrientationUnknown;
    info->connection = RoboDisplayConnectionUnknown;
}

void DisplayModeInit(DisplayMode *mode)
{
    if (!mode) {
        return;
    }
    memset(mode, 0, sizeof(*mode));
}

Result DisplayRefreshHzValidate(float hz)
{
    if (!DisplayFloatIsFinite(hz) || hz < 0.0f) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

Result DisplayInfoEffectiveScale(const DisplayInfo *info,
                                                float *outScale)
{
    if (!info || !outScale) {
        return RoboInvalidArgument;
    }
    if (DisplayScaleIsKnown(info->scale)) {
        *outScale = info->scale;
        return RoboOk;
    }
    if (DisplayScaleFromDpi(info->dpiX, outScale) == RoboOk) {
        return RoboOk;
    }
    return RoboNotFound;
}

Result DisplayInfoGetDpi(const DisplayInfo *info,
                                        DisplayDpi *out)
{
    if (!info || !out) {
        return RoboInvalidArgument;
    }
    if (DisplayDpiValidate(info->dpiX) != RoboOk ||
        DisplayDpiValidate(info->dpiY) != RoboOk) {
        return RoboInvalidArgument;
    }
    out->dpiX = info->dpiX;
    out->dpiY = info->dpiY;
    return RoboOk;
}

Result DisplayInfoPhysicalBounds(const DisplayInfo *info,
                                                GfxRect *out)
{
    float scale;
    Result r;

    if (!info || !out) {
        return RoboInvalidArgument;
    }
    r = DisplayInfoEffectiveScale(info, &scale);
    if (r != RoboOk) {
        return r;
    }
    return DisplayRectLogicalToPhysical(&info->bounds, scale, out);
}

Result DisplayInfoPhysicalWorkArea(
    const DisplayInfo *info,
    GfxRect *out)
{
    float scale;
    Result r;

    if (!info || !out) {
        return RoboInvalidArgument;
    }
    r = DisplayInfoEffectiveScale(info, &scale);
    if (r != RoboOk) {
        return r;
    }
    return DisplayRectLogicalToPhysical(&info->workArea, scale, out);
}
