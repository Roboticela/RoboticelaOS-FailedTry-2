#include "RoboFont/RoboFontStyle.h"

#include <string.h>

int robo_font_weight_is_valid(robo_font_weight_t w)
{
    return w >= 1u && w <= 1000u;
}

int robo_font_stretch_is_valid(robo_font_stretch_t s)
{
    return s >= 50u && s <= 200u;
}

void robo_font_style_desc_init(robo_font_style_desc_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->weight = ROBO_FONT_WEIGHT_NORMAL;
    out->stretch = ROBO_FONT_STRETCH_NORMAL;
    out->style = ROBO_FONT_STYLE_NORMAL;
    out->reserved_axis_count = 0;
}

robo_result_t robo_font_style_desc_validate(const robo_font_style_desc_t *desc)
{
    if (!desc) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_font_weight_is_valid(desc->weight)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!robo_font_stretch_is_valid(desc->stretch)) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (desc->style != ROBO_FONT_STYLE_NORMAL && desc->style != ROBO_FONT_STYLE_ITALIC &&
        desc->style != ROBO_FONT_STYLE_OBLIQUE) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (desc->reserved_axis_count != 0u) {
        return ROBO_NOT_SUPPORTED;
    }
    return ROBO_OK;
}
