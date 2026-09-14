/*
 * RoboFont instance — sized / configured use of a face (Goals 5.1 + 5.7 + 5.18).
 *
 * Face = resource identity. Instance = face + pixel size + style request
 * (+ optional variation coordinates for variable fonts).
 *
 * Does not rasterize glyphs. Call robo_font_instance_sync_metrics after
 * face parse_metrics, or attach metrics manually for tests.
 */

#ifndef ROBO_FONT_INSTANCE_H
#define ROBO_FONT_INSTANCE_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboFont/RoboFontVariation.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_font_instance robo_font_instance_t;

/**
 * Create an instance of @p face at @p pixel_size (> 0).
 * @p style NULL → defaults.
 * When the face is variable, weight/stretch/style (+ style.axes) produce a
 * shared variation-coordinate snapshot (no duplicate font load).
 * Retains @p face. If the face already has metrics info, layout metrics are
 * copied onto the instance automatically (static; variation deltas pending).
 */
robo_result_t robo_font_instance_create(robo_font_face_t *face,
                                        robo_font_pixel_size_t pixel_size,
                                        const robo_font_style_desc_t *style,
                                        robo_font_instance_t **out);

/**
 * Create with an explicit variation request (named instance / opsz / axes).
 * @p var_req NULL → behave like create() using style only.
 */
robo_result_t robo_font_instance_create_variation(
    robo_font_face_t *face,
    robo_font_pixel_size_t pixel_size,
    const robo_font_style_desc_t *style,
    const robo_font_variation_request_t *var_req,
    robo_font_instance_t **out);

void robo_font_instance_retain(robo_font_instance_t *inst);
void robo_font_instance_release(robo_font_instance_t *inst);

robo_font_instance_id_t robo_font_instance_id(const robo_font_instance_t *inst);
robo_font_pixel_size_t robo_font_instance_pixel_size(const robo_font_instance_t *inst);

/** Borrowed face. */
const robo_font_face_t *robo_font_instance_face(const robo_font_instance_t *inst);

robo_result_t robo_font_instance_style(const robo_font_instance_t *inst,
                                       robo_font_style_desc_t *out);

/** Borrowed variation coordinates (NULL if static face or default unused). */
const robo_font_variation_coords_t *robo_font_instance_variation_coords(
    const robo_font_instance_t *inst);

robo_font_metrics_variation_state_t robo_font_instance_metrics_variation_state(
    const robo_font_instance_t *inst);

/**
 * Attach metrics (e.g. for tests). Copies @p metrics.
 */
robo_result_t robo_font_instance_set_metrics(robo_font_instance_t *inst,
                                             const robo_font_metrics_t *metrics);

/**
 * Copy canonical layout metrics from the face's metrics info (if parsed).
 * Does not apply HVAR/MVAR; state becomes PENDING when coords are present.
 */
robo_result_t robo_font_instance_sync_metrics(robo_font_instance_t *inst);

/**
 * Read metrics. ROBO_NOT_FOUND if none set / not yet synced from the face.
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

/**
 * Scale glyph advance (font units) to pixels with rounding policy.
 * Requires synced/attached metrics.
 */
robo_result_t robo_font_instance_scale_advance(const robo_font_instance_t *inst,
                                               uint16_t advance_width,
                                               robo_font_round_mode_t round_mode,
                                               float *out_pixels);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_INSTANCE_H */
