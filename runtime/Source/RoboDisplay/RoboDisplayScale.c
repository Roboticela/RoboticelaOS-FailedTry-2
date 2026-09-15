#include "RoboDisplay/RoboDisplayScale.h"

#include <string.h>

void robo_display_dpi_init(robo_display_dpi_t *dpi)
{
    if (!dpi) {
        return;
    }
    dpi->dpi_x = 0.0f;
    dpi->dpi_y = 0.0f;
}

int robo_display_float_is_finite(float v)
{
    union {
        float f;
        uint32_t u;
    } x;

    x.f = v;
    /* IEEE-754: exponent all-ones → NaN or Inf. */
    return ((x.u >> 23) & 0xFFu) != 0xFFu;
}

int robo_display_scale_is_known(float scale)
{
    return robo_display_scale_validate(scale) == ROBO_OK;
}

robo_result_t robo_display_scale_validate(float scale)
{
    if (!robo_display_float_is_finite(scale)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!(scale >= ROBO_DISPLAY_SCALE_MIN) ||
        !(scale <= ROBO_DISPLAY_SCALE_MAX)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

int robo_display_dpi_is_unavailable(float dpi)
{
    return dpi == 0.0f;
}

int robo_display_dpi_is_known(float dpi)
{
    return !robo_display_dpi_is_unavailable(dpi) &&
           robo_display_dpi_validate(dpi) == ROBO_OK;
}

robo_result_t robo_display_dpi_validate(float dpi)
{
    if (!robo_display_float_is_finite(dpi)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (dpi == 0.0f) {
        return ROBO_OK; /* unavailable */
    }
    if (!(dpi >= ROBO_DISPLAY_DPI_MIN) || !(dpi <= ROBO_DISPLAY_DPI_MAX)) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

robo_result_t robo_display_dpi_pair_validate(const robo_display_dpi_t *dpi)
{
    robo_result_t r;

    if (!dpi) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_display_dpi_validate(dpi->dpi_x);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_display_dpi_validate(dpi->dpi_y);
}

robo_result_t robo_display_scale_from_dpi(float dpi, float *out_scale)
{
    float s;

    if (!out_scale) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_display_dpi_is_unavailable(dpi) ||
        robo_display_dpi_validate(dpi) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    s = dpi / ROBO_DISPLAY_DPI_REFERENCE;
    if (robo_display_scale_validate(s) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_scale = s;
    return ROBO_OK;
}

static robo_result_t convert_mul(int32_t value, float scale, int32_t *out)
{
    double v;
    double rounded;

    if (!out || robo_display_scale_validate(scale) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    v = (double)value * (double)scale;
    if (v >= 0.0) {
        rounded = v + 0.5;
    } else {
        rounded = v - 0.5;
    }
    if (rounded > 2147483647.0 || rounded < -2147483648.0) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = (int32_t)rounded;
    return ROBO_OK;
}

static robo_result_t convert_div(int32_t value, float scale, int32_t *out)
{
    double v;
    double rounded;

    if (!out || robo_display_scale_validate(scale) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    v = (double)value / (double)scale;
    if (v >= 0.0) {
        rounded = v + 0.5;
    } else {
        rounded = v - 0.5;
    }
    if (rounded > 2147483647.0 || rounded < -2147483648.0) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = (int32_t)rounded;
    return ROBO_OK;
}

robo_result_t robo_display_logical_to_physical(int32_t logical,
                                               float scale,
                                               int32_t *out_physical)
{
    return convert_mul(logical, scale, out_physical);
}

robo_result_t robo_display_physical_to_logical(int32_t physical,
                                               float scale,
                                               int32_t *out_logical)
{
    return convert_div(physical, scale, out_logical);
}

robo_result_t robo_display_size_logical_to_physical(int32_t logical_w,
                                                    int32_t logical_h,
                                                    float scale,
                                                    int32_t *out_phys_w,
                                                    int32_t *out_phys_h)
{
    robo_result_t r;

    if (!out_phys_w || !out_phys_h) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = convert_mul(logical_w, scale, out_phys_w);
    if (r != ROBO_OK) {
        return r;
    }
    return convert_mul(logical_h, scale, out_phys_h);
}

robo_result_t robo_display_size_physical_to_logical(int32_t phys_w,
                                                    int32_t phys_h,
                                                    float scale,
                                                    int32_t *out_logical_w,
                                                    int32_t *out_logical_h)
{
    robo_result_t r;

    if (!out_logical_w || !out_logical_h) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = convert_div(phys_w, scale, out_logical_w);
    if (r != ROBO_OK) {
        return r;
    }
    return convert_div(phys_h, scale, out_logical_h);
}

robo_result_t robo_display_point_logical_to_physical(int32_t logical_x,
                                                     int32_t logical_y,
                                                     float scale,
                                                     int32_t *out_phys_x,
                                                     int32_t *out_phys_y)
{
    robo_result_t r;

    if (!out_phys_x || !out_phys_y) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = convert_mul(logical_x, scale, out_phys_x);
    if (r != ROBO_OK) {
        return r;
    }
    return convert_mul(logical_y, scale, out_phys_y);
}

robo_result_t robo_display_point_physical_to_logical(int32_t phys_x,
                                                     int32_t phys_y,
                                                     float scale,
                                                     int32_t *out_logical_x,
                                                     int32_t *out_logical_y)
{
    robo_result_t r;

    if (!out_logical_x || !out_logical_y) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = convert_div(phys_x, scale, out_logical_x);
    if (r != ROBO_OK) {
        return r;
    }
    return convert_div(phys_y, scale, out_logical_y);
}

robo_result_t robo_display_rect_logical_to_physical(const robo_gfx_rect_t *in,
                                                    float scale,
                                                    robo_gfx_rect_t *out)
{
    robo_result_t r;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;

    if (!in || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = convert_mul(in->x, scale, &x);
    if (r != ROBO_OK) {
        return r;
    }
    r = convert_mul(in->y, scale, &y);
    if (r != ROBO_OK) {
        return r;
    }
    r = convert_mul(in->width, scale, &w);
    if (r != ROBO_OK) {
        return r;
    }
    r = convert_mul(in->height, scale, &h);
    if (r != ROBO_OK) {
        return r;
    }
    *out = robo_gfx_rect(x, y, w, h);
    return ROBO_OK;
}

robo_result_t robo_display_rect_physical_to_logical(const robo_gfx_rect_t *in,
                                                    float scale,
                                                    robo_gfx_rect_t *out)
{
    robo_result_t r;
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;

    if (!in || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = convert_div(in->x, scale, &x);
    if (r != ROBO_OK) {
        return r;
    }
    r = convert_div(in->y, scale, &y);
    if (r != ROBO_OK) {
        return r;
    }
    r = convert_div(in->width, scale, &w);
    if (r != ROBO_OK) {
        return r;
    }
    r = convert_div(in->height, scale, &h);
    if (r != ROBO_OK) {
        return r;
    }
    *out = robo_gfx_rect(x, y, w, h);
    return ROBO_OK;
}
