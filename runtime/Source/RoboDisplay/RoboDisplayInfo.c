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
    if (hz != hz || hz < 0.0f) { /* NaN or negative */
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

robo_result_t robo_display_logical_to_physical(int32_t logical,
                                               float scale,
                                               int32_t *out_physical)
{
    double v;

    if (!out_physical || !(scale > 0.0f) || scale != scale) {
        return ROBO_INVALID_ARGUMENT;
    }
    v = (double)logical * (double)scale;
    if (v > 2147483647.0) {
        *out_physical = 2147483647;
    } else if (v < -2147483648.0) {
        *out_physical = (int32_t)(-2147483647 - 1);
    } else {
        *out_physical = (int32_t)(v + (v >= 0.0 ? 0.5 : -0.5));
    }
    return ROBO_OK;
}

robo_result_t robo_display_physical_to_logical(int32_t physical,
                                               float scale,
                                               int32_t *out_logical)
{
    double v;

    if (!out_logical || !(scale > 0.0f) || scale != scale) {
        return ROBO_INVALID_ARGUMENT;
    }
    v = (double)physical / (double)scale;
    if (v > 2147483647.0) {
        *out_logical = 2147483647;
    } else if (v < -2147483648.0) {
        *out_logical = (int32_t)(-2147483647 - 1);
    } else {
        *out_logical = (int32_t)(v + (v >= 0.0 ? 0.5 : -0.5));
    }
    return ROBO_OK;
}
