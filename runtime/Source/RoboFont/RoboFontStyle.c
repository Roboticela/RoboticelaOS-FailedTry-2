#include "RoboFont/RoboFontStyle.h"

#include <string.h>

static int style_float_ok(float v)
{
    /* Freestanding-safe finite check (no math.h). */
    union {
        float f;
        uint32_t u;
    } bits;
    bits.f = v;
    return ((bits.u >> 23) & 0xFFu) != 0xFFu;
}

int robo_font_weight_is_valid(robo_font_weight_t w)
{
    return w >= 1u && w <= 1000u;
}

robo_font_weight_category_t robo_font_weight_category(robo_font_weight_t w)
{
    if (!robo_font_weight_is_valid(w)) {
        return ROBO_FONT_WEIGHT_CAT_UNKNOWN;
    }
    if (w <= 149u) {
        return ROBO_FONT_WEIGHT_CAT_THIN;
    }
    if (w <= 249u) {
        return ROBO_FONT_WEIGHT_CAT_EXTRA_LIGHT;
    }
    if (w <= 349u) {
        return ROBO_FONT_WEIGHT_CAT_LIGHT;
    }
    if (w <= 449u) {
        return ROBO_FONT_WEIGHT_CAT_NORMAL;
    }
    if (w <= 549u) {
        return ROBO_FONT_WEIGHT_CAT_MEDIUM;
    }
    if (w <= 649u) {
        return ROBO_FONT_WEIGHT_CAT_SEMI_BOLD;
    }
    if (w <= 749u) {
        return ROBO_FONT_WEIGHT_CAT_BOLD;
    }
    if (w <= 849u) {
        return ROBO_FONT_WEIGHT_CAT_EXTRA_BOLD;
    }
    if (w <= 949u) {
        return ROBO_FONT_WEIGHT_CAT_BLACK;
    }
    return ROBO_FONT_WEIGHT_CAT_EXTRA_BLACK;
}

int robo_font_stretch_is_valid(robo_font_stretch_t s)
{
    return s >= 50u && s <= 200u;
}

int robo_font_width_class_is_valid(uint16_t wc)
{
    return wc >= 1u && wc <= 9u;
}

robo_font_stretch_t robo_font_stretch_from_width_class(uint16_t us_width_class)
{
    static const robo_font_stretch_t map[10] = {
        ROBO_FONT_STRETCH_NORMAL, /* unused index 0 */
        ROBO_FONT_STRETCH_ULTRA_CONDENSED,
        ROBO_FONT_STRETCH_EXTRA_CONDENSED,
        ROBO_FONT_STRETCH_CONDENSED,
        ROBO_FONT_STRETCH_SEMI_CONDENSED,
        ROBO_FONT_STRETCH_NORMAL,
        ROBO_FONT_STRETCH_SEMI_EXPANDED,
        ROBO_FONT_STRETCH_EXPANDED,
        ROBO_FONT_STRETCH_EXTRA_EXPANDED,
        ROBO_FONT_STRETCH_ULTRA_EXPANDED};
    if (!robo_font_width_class_is_valid(us_width_class)) {
        return ROBO_FONT_STRETCH_NORMAL;
    }
    return map[us_width_class];
}

robo_font_width_class_t robo_font_width_class_from_stretch(robo_font_stretch_t s)
{
    if (!robo_font_stretch_is_valid(s)) {
        return ROBO_FONT_WIDTH_UNKNOWN;
    }
    if (s < 56u) {
        return ROBO_FONT_WIDTH_ULTRA_CONDENSED;
    }
    if (s < 69u) {
        return ROBO_FONT_WIDTH_EXTRA_CONDENSED;
    }
    if (s < 81u) {
        return ROBO_FONT_WIDTH_CONDENSED;
    }
    if (s < 94u) {
        return ROBO_FONT_WIDTH_SEMI_CONDENSED;
    }
    if (s < 106u) {
        return ROBO_FONT_WIDTH_NORMAL;
    }
    if (s < 119u) {
        return ROBO_FONT_WIDTH_SEMI_EXPANDED;
    }
    if (s < 138u) {
        return ROBO_FONT_WIDTH_EXPANDED;
    }
    if (s < 175u) {
        return ROBO_FONT_WIDTH_EXTRA_EXPANDED;
    }
    return ROBO_FONT_WIDTH_ULTRA_EXPANDED;
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
    out->axis_count = 0;
}

robo_result_t robo_font_style_desc_validate(const robo_font_style_desc_t *desc)
{
    uint32_t i;

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
        desc->style != ROBO_FONT_STYLE_OBLIQUE && desc->style != ROBO_FONT_STYLE_UNKNOWN) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (desc->axis_count > ROBO_FONT_STYLE_MAX_AXES) {
        return ROBO_INVALID_ARGUMENT;
    }
    for (i = 0u; i < desc->axis_count; i++) {
        if (desc->axes[i].tag == 0u) {
            return ROBO_INVALID_ARGUMENT;
        }
        if (!style_float_ok(desc->axes[i].value)) {
            return ROBO_INVALID_ARGUMENT;
        }
    }
    return ROBO_OK;
}
