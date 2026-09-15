#include "RoboDisplay/RoboDisplayInfo.h"

#include <string.h>

void robo_display_info_init(robo_display_info_t *info)
{
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->id = ROBO_DISPLAY_ID_NONE;
    info->bounds = robo_gfx_rect(0, 0, 0, 0);
    info->work_area = robo_gfx_rect(0, 0, 0, 0);
    info->orientation = ROBO_DISPLAY_ORIENTATION_UNKNOWN;
    info->connection = ROBO_DISPLAY_CONNECTION_UNKNOWN;
}

void robo_display_mode_init(robo_display_mode_t *mode)
{
    if (!mode) {
        return;
    }
    memset(mode, 0, sizeof(*mode));
}

robo_result_t robo_display_refresh_hz_validate(float hz)
{
    if (!robo_display_float_is_finite(hz) || hz < 0.0f) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

robo_result_t robo_display_info_effective_scale(const robo_display_info_t *info,
                                                float *out_scale)
{
    if (!info || !out_scale) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_display_scale_is_known(info->scale)) {
        *out_scale = info->scale;
        return ROBO_OK;
    }
    if (robo_display_scale_from_dpi(info->dpi_x, out_scale) == ROBO_OK) {
        return ROBO_OK;
    }
    return ROBO_NOT_FOUND;
}

robo_result_t robo_display_info_get_dpi(const robo_display_info_t *info,
                                        robo_display_dpi_t *out)
{
    if (!info || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_display_dpi_validate(info->dpi_x) != ROBO_OK ||
        robo_display_dpi_validate(info->dpi_y) != ROBO_OK) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->dpi_x = info->dpi_x;
    out->dpi_y = info->dpi_y;
    return ROBO_OK;
}

robo_result_t robo_display_info_physical_bounds(const robo_display_info_t *info,
                                                robo_gfx_rect_t *out)
{
    float scale;
    robo_result_t r;

    if (!info || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_display_info_effective_scale(info, &scale);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_display_rect_logical_to_physical(&info->bounds, scale, out);
}

robo_result_t robo_display_info_physical_work_area(
    const robo_display_info_t *info,
    robo_gfx_rect_t *out)
{
    float scale;
    robo_result_t r;

    if (!info || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    r = robo_display_info_effective_scale(info, &scale);
    if (r != ROBO_OK) {
        return r;
    }
    return robo_display_rect_logical_to_physical(&info->work_area, scale, out);
}
