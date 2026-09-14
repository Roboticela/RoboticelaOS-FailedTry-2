#include "RoboFont/RoboFontInstance.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_font_instance {
    uint32_t refs;
    robo_font_instance_id_t id;
    robo_font_face_t *face;
    robo_font_pixel_size_t pixel_size;
    robo_font_style_desc_t style;
    robo_font_variation_coords_t *coords;
    robo_font_metrics_variation_state_t metrics_var_state;
    robo_font_metrics_t metrics;
    uint8_t has_metrics;
};

static uint64_t g_next_instance_id = 1u;

static robo_result_t instance_finish(robo_font_face_t *face,
                                     robo_font_pixel_size_t pixel_size,
                                     const robo_font_style_desc_t *local,
                                     const robo_font_variation_request_t *var_req,
                                     robo_font_instance_t **out)
{
    robo_font_instance_t *inst;
    robo_font_variation_coords_t *coords = NULL;
    robo_result_t r;

    *out = NULL;

    r = robo_font_face_parse_variation(face);
    if (r == ROBO_OK) {
        const robo_font_variation_model_t *model = robo_font_face_variation(face);
        robo_font_variation_request_t req;
        if (var_req) {
            req = *var_req;
        } else {
            robo_font_variation_request_from_style(local, 0.0f, 0u, 0u, 0xFFFFFFFFu, 0u,
                                                   &req);
        }
        if (model) {
            r = robo_font_variation_coords_create(model, &req, &coords);
            if (r != ROBO_OK) {
                return r;
            }
        }
    } else if (r != ROBO_NOT_FOUND) {
        return r;
    }

    inst = (robo_font_instance_t *)robo_calloc(1, sizeof(*inst));
    if (!inst) {
        if (coords) {
            robo_font_variation_coords_release(coords);
        }
        return ROBO_OUT_OF_MEMORY;
    }
    inst->refs = 1u;
    inst->id = g_next_instance_id++;
    if (inst->id == 0u) {
        inst->id = g_next_instance_id++;
    }
    robo_font_face_retain(face);
    inst->face = face;
    inst->pixel_size = pixel_size;
    inst->style = *local;
    inst->coords = coords;
    inst->metrics_var_state =
        coords ? ROBO_FONT_METRICS_VAR_PENDING : ROBO_FONT_METRICS_VAR_STATIC;
    robo_font_metrics_clear(&inst->metrics);
    inst->has_metrics = 0u;
    if (robo_font_face_metrics_info(face)) {
        robo_font_metrics_t lm;
        if (robo_font_face_layout_metrics(face, &lm) == ROBO_OK && lm.valid) {
            inst->metrics = lm;
            inst->has_metrics = 1u;
        }
    }
    *out = inst;
    return ROBO_OK;
}

robo_result_t robo_font_instance_create(robo_font_face_t *face,
                                        robo_font_pixel_size_t pixel_size,
                                        const robo_font_style_desc_t *style,
                                        robo_font_instance_t **out)
{
    robo_font_style_desc_t local;
    robo_result_t r;

    if (!out || !face || !(pixel_size > 0.0f)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    if (style) {
        local = *style;
    } else {
        robo_font_style_desc_init(&local);
    }
    r = robo_font_style_desc_validate(&local);
    if (r != ROBO_OK) {
        return r;
    }
    return instance_finish(face, pixel_size, &local, NULL, out);
}

robo_result_t robo_font_instance_create_variation(
    robo_font_face_t *face,
    robo_font_pixel_size_t pixel_size,
    const robo_font_style_desc_t *style,
    const robo_font_variation_request_t *var_req,
    robo_font_instance_t **out)
{
    robo_font_style_desc_t local;
    robo_result_t r;

    if (!out || !face || !(pixel_size > 0.0f)) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    if (style) {
        local = *style;
    } else {
        robo_font_style_desc_init(&local);
    }
    r = robo_font_style_desc_validate(&local);
    if (r != ROBO_OK) {
        return r;
    }
    return instance_finish(face, pixel_size, &local, var_req, out);
}

void robo_font_instance_retain(robo_font_instance_t *inst)
{
    if (!inst) {
        return;
    }
    inst->refs++;
}

void robo_font_instance_release(robo_font_instance_t *inst)
{
    if (!inst) {
        return;
    }
    if (inst->refs == 0u) {
        return;
    }
    inst->refs--;
    if (inst->refs != 0u) {
        return;
    }
    if (inst->coords) {
        robo_font_variation_coords_release(inst->coords);
        inst->coords = NULL;
    }
    robo_font_face_release(inst->face);
    inst->face = NULL;
    robo_free(inst);
}

robo_font_instance_id_t robo_font_instance_id(const robo_font_instance_t *inst)
{
    return inst ? inst->id : 0u;
}

robo_font_pixel_size_t robo_font_instance_pixel_size(const robo_font_instance_t *inst)
{
    return inst ? inst->pixel_size : 0.0f;
}

const robo_font_face_t *robo_font_instance_face(const robo_font_instance_t *inst)
{
    return inst ? inst->face : NULL;
}

robo_result_t robo_font_instance_style(const robo_font_instance_t *inst,
                                       robo_font_style_desc_t *out)
{
    if (!inst || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = inst->style;
    return ROBO_OK;
}

const robo_font_variation_coords_t *robo_font_instance_variation_coords(
    const robo_font_instance_t *inst)
{
    return inst ? inst->coords : NULL;
}

robo_font_metrics_variation_state_t robo_font_instance_metrics_variation_state(
    const robo_font_instance_t *inst)
{
    return inst ? inst->metrics_var_state : ROBO_FONT_METRICS_VAR_STATIC;
}

robo_result_t robo_font_instance_set_metrics(robo_font_instance_t *inst,
                                             const robo_font_metrics_t *metrics)
{
    if (!inst || !metrics || !metrics->valid) {
        return ROBO_INVALID_ARGUMENT;
    }
    inst->metrics = *metrics;
    inst->has_metrics = 1u;
    return ROBO_OK;
}

robo_result_t robo_font_instance_sync_metrics(robo_font_instance_t *inst)
{
    robo_font_metrics_t lm;
    if (!inst) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_font_face_layout_metrics(inst->face, &lm) != ROBO_OK || !lm.valid) {
        return ROBO_NOT_FOUND;
    }
    inst->metrics = lm;
    inst->has_metrics = 1u;
    inst->metrics_var_state =
        inst->coords ? ROBO_FONT_METRICS_VAR_PENDING : ROBO_FONT_METRICS_VAR_STATIC;
    return ROBO_OK;
}

robo_result_t robo_font_instance_metrics(const robo_font_instance_t *inst,
                                         robo_font_metrics_t *out)
{
    if (!inst || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!inst->has_metrics) {
        robo_font_metrics_clear(out);
        return ROBO_NOT_FOUND;
    }
    *out = inst->metrics;
    return ROBO_OK;
}

robo_result_t robo_font_instance_units_to_pixels(const robo_font_instance_t *inst,
                                                 robo_font_unit_t units,
                                                 float *out_pixels)
{
    if (!inst || !out_pixels) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!inst->has_metrics || !inst->metrics.valid) {
        return ROBO_NOT_FOUND;
    }
    return robo_font_units_to_pixels(units, inst->metrics.units_per_em, inst->pixel_size,
                                     out_pixels);
}

robo_result_t robo_font_instance_scale_advance(const robo_font_instance_t *inst,
                                               uint16_t advance_width,
                                               robo_font_round_mode_t round_mode,
                                               float *out_pixels)
{
    if (!inst || !out_pixels) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!inst->has_metrics || !inst->metrics.valid) {
        return ROBO_NOT_FOUND;
    }
    return robo_font_scale_font_units((robo_font_unit_t)advance_width,
                                      inst->metrics.units_per_em, inst->pixel_size,
                                      round_mode, out_pixels);
}
