#include "RoboFont/RoboFontMetrics.h"

#include <string.h>

void robo_font_metrics_clear(robo_font_metrics_t *m)
{
    if (!m) {
        return;
    }
    memset(m, 0, sizeof(*m));
}

robo_result_t robo_font_metrics_init(robo_font_metrics_t *m,
                                     uint16_t units_per_em,
                                     robo_font_unit_t ascent,
                                     robo_font_unit_t descent,
                                     robo_font_unit_t line_gap,
                                     const robo_font_bbox_t *bbox)
{
    if (!m || units_per_em == 0u || descent < 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(m, 0, sizeof(*m));
    m->units_per_em = units_per_em;
    m->ascent = ascent;
    m->descent = descent;
    m->line_gap = line_gap;
    if (bbox) {
        m->bbox = *bbox;
        m->has_bbox = 1u;
    }
    m->valid = 1u;
    return ROBO_OK;
}

robo_font_unit_t robo_font_metrics_line_height(const robo_font_metrics_t *m)
{
    if (!m || !m->valid) {
        return 0;
    }
    return m->ascent + m->descent + m->line_gap;
}

robo_result_t robo_font_units_to_pixels(robo_font_unit_t units,
                                        uint16_t units_per_em,
                                        robo_font_pixel_size_t pixel_size,
                                        float *out_pixels)
{
    if (!out_pixels || units_per_em == 0u || !(pixel_size > 0.0f)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_pixels = ((float)units * pixel_size) / (float)units_per_em;
    return ROBO_OK;
}

robo_result_t robo_font_pixels_to_units(float pixels,
                                        uint16_t units_per_em,
                                        robo_font_pixel_size_t pixel_size,
                                        robo_font_unit_t *out_units)
{
    double v;
    if (!out_units || units_per_em == 0u || !(pixel_size > 0.0f)) {
        return ROBO_INVALID_ARGUMENT;
    }
    v = ((double)pixels * (double)units_per_em) / (double)pixel_size;
    if (v >= 0.0) {
        *out_units = (robo_font_unit_t)(v + 0.5);
    } else {
        *out_units = (robo_font_unit_t)(v - 0.5);
    }
    return ROBO_OK;
}

robo_font_pixel_size_t robo_font_points_to_pixels(robo_font_point_size_t points)
{
    /* 96 CSS px / inch, 72 pt / inch → px = pt * 96/72 */
    return points * (96.0f / 72.0f);
}
