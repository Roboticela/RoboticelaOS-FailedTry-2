#include "RoboDisplay/RoboDisplayScale.h"

#include <string.h>

void DisplayDpiInit(DisplayDpi *dpi)
{
    if (!dpi) {
        return;
    }
    dpi->dpiX = 0.0f;
    dpi->dpiY = 0.0f;
}

int DisplayFloatIsFinite(float v)
{
    union {
        float f;
        uint32_t u;
    } x;

    x.f = v;
    /* IEEE-754: exponent all-ones → NaN or Inf. */
    return ((x.u >> 23) & 0xFFu) != 0xFFu;
}

int DisplayScaleIsKnown(float scale)
{
    return DisplayScaleValidate(scale) == RoboOk;
}

Result DisplayScaleValidate(float scale)
{
    if (!DisplayFloatIsFinite(scale)) {
        return RoboInvalidArgument;
    }
    if (!(scale >= RoboDisplayScaleMin) ||
        !(scale <= RoboDisplayScaleMax)) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

int DisplayDpiIsUnavailable(float dpi)
{
    return dpi == 0.0f;
}

int DisplayDpiIsKnown(float dpi)
{
    return !DisplayDpiIsUnavailable(dpi) &&
           DisplayDpiValidate(dpi) == RoboOk;
}

Result DisplayDpiValidate(float dpi)
{
    if (!DisplayFloatIsFinite(dpi)) {
        return RoboInvalidArgument;
    }
    if (dpi == 0.0f) {
        return RoboOk; /* unavailable */
    }
    if (!(dpi >= RoboDisplayDpiMin) || !(dpi <= RoboDisplayDpiMax)) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

Result DisplayDpiPairValidate(const DisplayDpi *dpi)
{
    Result r;

    if (!dpi) {
        return RoboInvalidArgument;
    }
    r = DisplayDpiValidate(dpi->dpiX);
    if (r != RoboOk) {
        return r;
    }
    return DisplayDpiValidate(dpi->dpiY);
}

Result DisplayScaleFromDpi(float dpi, float *outScale)
{
    float s;

    if (!outScale) {
        return RoboInvalidArgument;
    }
    if (DisplayDpiIsUnavailable(dpi) ||
        DisplayDpiValidate(dpi) != RoboOk) {
        return RoboInvalidArgument;
    }
    s = dpi / RoboDisplayDpiReference;
    if (DisplayScaleValidate(s) != RoboOk) {
        return RoboInvalidArgument;
    }
    *outScale = s;
    return RoboOk;
}

static Result ConvertMul(int32_t value, float scale, int32_t *out)
{
    double v;
    double rounded;

    if (!out || DisplayScaleValidate(scale) != RoboOk) {
        return RoboInvalidArgument;
    }
    v = (double)value * (double)scale;
    if (v >= 0.0) {
        rounded = v + 0.5;
    } else {
        rounded = v - 0.5;
    }
    if (rounded > 2147483647.0 || rounded < -2147483648.0) {
        return RoboInvalidArgument;
    }
    *out = (int32_t)rounded;
    return RoboOk;
}

static Result ConvertDiv(int32_t value, float scale, int32_t *out)
{
    double v;
    double rounded;

    if (!out || DisplayScaleValidate(scale) != RoboOk) {
        return RoboInvalidArgument;
    }
    v = (double)value / (double)scale;
    if (v >= 0.0) {
        rounded = v + 0.5;
    } else {
        rounded = v - 0.5;
    }
    if (rounded > 2147483647.0 || rounded < -2147483648.0) {
        return RoboInvalidArgument;
    }
    *out = (int32_t)rounded;
    return RoboOk;
}

Result DisplayLogicalToPhysical(int32_t logical,
                                               float scale,
                                               int32_t *outPhysical)
{
    return ConvertMul(logical, scale, outPhysical);
}

Result DisplayPhysicalToLogical(int32_t physical,
                                               float scale,
                                               int32_t *outLogical)
{
    return ConvertDiv(physical, scale, outLogical);
}

Result DisplaySizeLogicalToPhysical(int32_t logicalW,
                                                    int32_t logicalH,
                                                    float scale,
                                                    int32_t *outPhysW,
                                                    int32_t *outPhysH)
{
    Result r;

    if (!outPhysW || !outPhysH) {
        return RoboInvalidArgument;
    }
    r = ConvertMul(logicalW, scale, outPhysW);
    if (r != RoboOk) {
        return r;
    }
    return ConvertMul(logicalH, scale, outPhysH);
}

Result DisplaySizePhysicalToLogical(int32_t physW,
                                                    int32_t physH,
                                                    float scale,
                                                    int32_t *outLogicalW,
                                                    int32_t *outLogicalH)
{
    Result r;

    if (!outLogicalW || !outLogicalH) {
        return RoboInvalidArgument;
    }
    r = ConvertDiv(physW, scale, outLogicalW);
    if (r != RoboOk) {
        return r;
    }
    return ConvertDiv(physH, scale, outLogicalH);
}

Result DisplayPointLogicalToPhysical(int32_t logicalX,
                                                     int32_t logicalY,
                                                     float scale,
                                                     int32_t *outPhysX,
                                                     int32_t *outPhysY)
{
    Result r;

    if (!outPhysX || !outPhysY) {
        return RoboInvalidArgument;
    }
    r = ConvertMul(logicalX, scale, outPhysX);
    if (r != RoboOk) {
        return r;
    }
    return ConvertMul(logicalY, scale, outPhysY);
}

Result DisplayPointPhysicalToLogical(int32_t physX,
                                                     int32_t physY,
                                                     float scale,
                                                     int32_t *outLogicalX,
                                                     int32_t *outLogicalY)
{
    Result r;

    if (!outLogicalX || !outLogicalY) {
        return RoboInvalidArgument;
    }
    r = ConvertDiv(physX, scale, outLogicalX);
    if (r != RoboOk) {
        return r;
    }
    return ConvertDiv(physY, scale, outLogicalY);
}

Result DisplayRectLogicalToPhysical(const GfxRect *in,
                                                    float scale,
                                                    GfxRect *out)
{
    Result r;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;

    if (!in || !out) {
        return RoboInvalidArgument;
    }
    r = ConvertMul(in->x, scale, &x);
    if (r != RoboOk) {
        return r;
    }
    r = ConvertMul(in->y, scale, &y);
    if (r != RoboOk) {
        return r;
    }
    r = ConvertMul(in->width, scale, &w);
    if (r != RoboOk) {
        return r;
    }
    r = ConvertMul(in->height, scale, &h);
    if (r != RoboOk) {
        return r;
    }
    *out = MakeGfxRect(x, y, w, h);
    return RoboOk;
}

Result DisplayRectPhysicalToLogical(const GfxRect *in,
                                                    float scale,
                                                    GfxRect *out)
{
    Result r;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;

    if (!in || !out) {
        return RoboInvalidArgument;
    }
    r = ConvertDiv(in->x, scale, &x);
    if (r != RoboOk) {
        return r;
    }
    r = ConvertDiv(in->y, scale, &y);
    if (r != RoboOk) {
        return r;
    }
    r = ConvertDiv(in->width, scale, &w);
    if (r != RoboOk) {
        return r;
    }
    r = ConvertDiv(in->height, scale, &h);
    if (r != RoboOk) {
        return r;
    }
    *out = MakeGfxRect(x, y, w, h);
    return RoboOk;
}
