/*
 * RoboFont instance — sized / configured use of a face (Goal 5.1).
 *
 * Face = resource identity. Instance = face + pixel size + style request
 * (+ reserved variation slots for later variable fonts).
 *
 * Does not rasterize glyphs. Metrics from SFNT are unavailable until later
 * goals; callers may attach test metrics via robo_font_instance_set_metrics
 * for foundation testing only.
 */

#ifndef ROBO_FONT_INSTANCE_H
#define ROBO_FONT_INSTANCE_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_font_instance robo_font_instance_t;

/**
 * Create an instance of @p face at @p pixel_size (> 0).
 * @p style NULL → defaults. Non-zero reserved_axis_count → ROBO_NOT_SUPPORTED.
 * Retains @p face.
 */
robo_result_t robo_font_instance_create(robo_font_face_t *face,
                                        robo_font_pixel_size_t pixel_size,
                                        const robo_font_style_desc_t *style,
                                        robo_font_instance_t **out);

void robo_font_instance_retain(robo_font_instance_t *inst);
void robo_font_instance_release(robo_font_instance_t *inst);

robo_font_instance_id_t robo_font_instance_id(const robo_font_instance_t *inst);
robo_font_pixel_size_t robo_font_instance_pixel_size(const robo_font_instance_t *inst);

/** Borrowed face. */
const robo_font_face_t *robo_font_instance_face(const robo_font_instance_t *inst);

robo_result_t robo_font_instance_style(const robo_font_instance_t *inst,
                                       robo_font_style_desc_t *out);

/**
 * Attach metrics (e.g. for tests). Copies @p metrics.
 * Real fonts will populate this from tables in later goals.
 */
robo_result_t robo_font_instance_set_metrics(robo_font_instance_t *inst,
                                             const robo_font_metrics_t *metrics);

/**
 * Read metrics. ROBO_NOT_FOUND if none set / not yet parsed from the face.
 */
robo_result_t robo_font_instance_metrics(const robo_font_instance_t *inst,
                                         robo_font_metrics_t *out);

/**
 * Scale a font-unit length using this instance's pixel size and metrics upem.
 * Requires valid metrics with units_per_em > 0.
 */
robo_result_t robo_font_instance_units_to_pixels(const robo_font_instance_t *inst,
                                                 robo_font_unit_t units,
                                                 float *out_pixels);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_INSTANCE_H */
