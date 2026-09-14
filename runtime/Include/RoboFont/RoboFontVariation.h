/*
 * RoboFontVariation — OpenType variable fonts (Goal 5.18).
 *
 * Parses fvar / avar, represents axes and named instances, normalizes
 * user coordinates, integrates with STAT / selection / instance model.
 *
 * Does NOT implement:
 *   gvar / HVAR / VVAR / MVAR delta execution, CFF2 blend evaluation,
 *   variable glyph/metric interpolation, CSS font-variation-settings,
 *   or CSS font-optical-sizing.
 */

#ifndef ROBO_FONT_VARIATION_H
#define ROBO_FONT_VARIATION_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboFont/RoboFontName.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_FONT_VAR_MAX_AXES 64u
#define ROBO_FONT_VAR_MAX_INSTANCES 512u
#define ROBO_FONT_VAR_MAX_AVAR_SEGMENTS 256u
#define ROBO_FONT_VAR_MAX_NAME_BYTES 256u
#define ROBO_FONT_VAR_COORDS_CACHE_MAX 64u

#define ROBO_FONT_TAG_WGHT ROBO_FONT_TAG('w', 'g', 'h', 't')
#define ROBO_FONT_TAG_WDTH ROBO_FONT_TAG('w', 'd', 't', 'h')
#define ROBO_FONT_TAG_ITAL ROBO_FONT_TAG('i', 't', 'a', 'l')
#define ROBO_FONT_TAG_SLNT ROBO_FONT_TAG('s', 'l', 'n', 't')
#define ROBO_FONT_TAG_OPSZ ROBO_FONT_TAG('o', 'p', 's', 'z')

/** Axis flag: hidden from UI (OpenType AXIS_FLAG_HIDDEN). */
#define ROBO_FONT_VAR_AXIS_FLAG_HIDDEN 0x0001u

typedef enum robo_font_var_axis_kind {
    ROBO_FONT_VAR_AXIS_UNKNOWN = 0,
    ROBO_FONT_VAR_AXIS_REGISTERED = 1, /* wght/wdth/ital/slnt/opsz */
    ROBO_FONT_VAR_AXIS_CUSTOM = 2
} robo_font_var_axis_kind_t;

typedef enum robo_font_var_capabilities {
    ROBO_FONT_VAR_CAP_NONE = 0,
    ROBO_FONT_VAR_CAP_FVAR = 1u << 0,
    ROBO_FONT_VAR_CAP_AVAR = 1u << 1,
    ROBO_FONT_VAR_CAP_STAT = 1u << 2,
    ROBO_FONT_VAR_CAP_GVAR = 1u << 3,  /* presence only */
    ROBO_FONT_VAR_CAP_HVAR = 1u << 4,
    ROBO_FONT_VAR_CAP_MVAR = 1u << 5,
    ROBO_FONT_VAR_CAP_CFF2 = 1u << 6,
    ROBO_FONT_VAR_CAP_NAMED_INSTANCES = 1u << 7
} robo_font_var_capabilities_t;

/**
 * Whether variation-aware metrics deltas have been applied.
 * Goal 5.18 exposes PENDING when coords are set but HVAR/MVAR are not executed.
 */
typedef enum robo_font_metrics_variation_state {
    ROBO_FONT_METRICS_VAR_STATIC = 0,  /* no variation / default instance */
    ROBO_FONT_METRICS_VAR_PENDING = 1, /* coords present; deltas not applied */
    ROBO_FONT_METRICS_VAR_APPLIED = 2  /* reserved for later goals */
} robo_font_metrics_variation_state_t;

typedef struct robo_font_variation_axis {
    robo_font_tag_t tag;
    float min_value;
    float default_value;
    float max_value;
    uint16_t flags;
    uint16_t name_id;
    robo_font_var_axis_kind_t kind;
    char name_utf8[ROBO_FONT_VAR_MAX_NAME_BYTES]; /* may be empty */
    uint8_t name_len;
} robo_font_variation_axis_t;

typedef struct robo_font_variation_instance {
    uint32_t index;
    uint16_t subfamily_name_id;
    uint16_t flags;
    uint16_t postscript_name_id; /* 0xFFFF if absent */
    char name_utf8[ROBO_FONT_VAR_MAX_NAME_BYTES];
    uint8_t name_len;
    float *user_coords; /* axis_count values; owned by model */
} robo_font_variation_instance_t;

typedef struct robo_font_variation_model robo_font_variation_model_t;
typedef struct robo_font_variation_coords robo_font_variation_coords_t;
typedef struct robo_font_outline robo_font_outline_t;

typedef struct robo_font_variation_request {
    /** Tagged user-space settings (borrowed). */
    const robo_font_axis_setting_t *axes;
    uint32_t axis_count;
    /** Named instance selection (UINT32_MAX = none). */
    uint32_t named_instance_index;
    uint8_t has_named_instance;
    /** Optical size (opsz) — explicit. */
    uint8_t has_optical_size;
    float optical_size;
    /** Automatic opsz request flag (no CSS policy applied). */
    uint8_t optical_size_auto;
    /** Weight / stretch / style mapped onto registered axes when present. */
    uint8_t apply_style_axes;
    robo_font_weight_t weight;
    robo_font_stretch_t stretch;
    robo_font_style_t style;
} robo_font_variation_request_t;

void robo_font_variation_request_init(robo_font_variation_request_t *req);

/* ---- Axis helpers ---- */

robo_font_var_axis_kind_t robo_font_variation_axis_kind(robo_font_tag_t tag);
int robo_font_variation_tag_is_registered(robo_font_tag_t tag);

/** Clamp user value into [min, max]; rejects NaN/Inf → ROBO_INVALID_ARGUMENT. */
robo_result_t robo_font_variation_clamp_user(float min_v,
                                             float def_v,
                                             float max_v,
                                             float requested,
                                             float *out_clamped);

/**
 * Default normalization: min→[-1], default→0, max→[+1].
 * Input should already be clamped. NaN/Inf → error.
 */
robo_result_t robo_font_variation_normalize(float min_v,
                                            float def_v,
                                            float max_v,
                                            float user,
                                            float *out_norm);

/* ---- Model (immutable after construction) ---- */

/**
 * Parse fvar (+ optional avar) from SFNT. Optional STAT/name for enrichment.
 * Missing fvar → ROBO_NOT_FOUND (out unchanged / NULL).
 */
robo_result_t robo_font_variation_model_create(robo_font_sfnt_t *sfnt,
                                               const robo_font_stat_t *stat_opt,
                                               const robo_font_name_info_t *name_opt,
                                               robo_font_variation_model_t **out);

robo_result_t robo_font_variation_model_create_from_face(
    robo_font_face_t *face,
    robo_font_variation_model_t **out);

void robo_font_variation_model_retain(robo_font_variation_model_t *m);
void robo_font_variation_model_release(robo_font_variation_model_t *m);

uint32_t robo_font_variation_model_capabilities(const robo_font_variation_model_t *m);
int robo_font_variation_model_is_variable(const robo_font_variation_model_t *m);

uint32_t robo_font_variation_model_axis_count(const robo_font_variation_model_t *m);
const robo_font_variation_axis_t *robo_font_variation_model_axis(
    const robo_font_variation_model_t *m,
    uint32_t index);

robo_result_t robo_font_variation_model_find_axis(const robo_font_variation_model_t *m,
                                                  robo_font_tag_t tag,
                                                  uint32_t *out_index,
                                                  const robo_font_variation_axis_t **out_axis);

uint32_t robo_font_variation_model_instance_count(const robo_font_variation_model_t *m);
const robo_font_variation_instance_t *robo_font_variation_model_instance(
    const robo_font_variation_model_t *m,
    uint32_t index);

int robo_font_variation_model_has_avar(const robo_font_variation_model_t *m);

/** STAT axis tags that agree with fvar (informational; never overrides ranges). */
uint32_t robo_font_variation_model_stat_axis_count(const robo_font_variation_model_t *m);
robo_font_tag_t robo_font_variation_model_stat_axis_tag(
    const robo_font_variation_model_t *m,
    uint32_t index);

/* ---- Coordinates ---- */

/**
 * Build immutable coordinate snapshot from a request.
 * Named instance fills defaults then overlays instance coords.
 * Style axes (wght/wdth/ital/slnt) applied when apply_style_axes≠0.
 * Explicit axis settings override. Optical size fills opsz when present.
 */
robo_result_t robo_font_variation_coords_create(
    const robo_font_variation_model_t *model,
    const robo_font_variation_request_t *req,
    robo_font_variation_coords_t **out);

/** Defaults at each axis default (normalized 0, avar-mapped). */
robo_result_t robo_font_variation_coords_create_default(
    const robo_font_variation_model_t *model,
    robo_font_variation_coords_t **out);

void robo_font_variation_coords_retain(robo_font_variation_coords_t *c);
void robo_font_variation_coords_release(robo_font_variation_coords_t *c);

uint32_t robo_font_variation_coords_axis_count(const robo_font_variation_coords_t *c);
float robo_font_variation_coords_user(const robo_font_variation_coords_t *c, uint32_t i);
float robo_font_variation_coords_normalized(const robo_font_variation_coords_t *c,
                                            uint32_t i);
/** After avar (equals normalized when avar absent). */
float robo_font_variation_coords_adjusted(const robo_font_variation_coords_t *c,
                                          uint32_t i);

robo_result_t robo_font_variation_coords_user_for_tag(
    const robo_font_variation_coords_t *c,
    robo_font_tag_t tag,
    float *out);

/* ---- Selection / instance helpers ---- */

/**
 * Map weight/stretch/style (+ optional axes/opsz/named instance) onto a
 * variation request. Does not invent missing axes.
 */
void robo_font_variation_request_from_style(const robo_font_style_desc_t *style,
                                            float optical_size,
                                            uint8_t has_optical_size,
                                            uint8_t optical_size_auto,
                                            uint32_t named_instance_index,
                                            uint8_t has_named_instance,
                                            robo_font_variation_request_t *out);

/**
 * True when a variable face can satisfy the request via axes (exact or clamp).
 * Used by selection to prefer variation over another physical face.
 */
int robo_font_variation_can_satisfy_weight(const robo_font_variation_model_t *m,
                                           robo_font_weight_t weight);
int robo_font_variation_can_satisfy_stretch(const robo_font_variation_model_t *m,
                                            robo_font_stretch_t stretch);
int robo_font_variation_can_satisfy_style(const robo_font_variation_model_t *m,
                                          robo_font_style_t style);

/** Apply avar segment map for one axis (public for tests). */
robo_result_t robo_font_variation_avar_map(const robo_font_variation_model_t *m,
                                           uint32_t axis_index,
                                           float normalized,
                                           float *out_adjusted);

/* ---- Variation-aware metrics / outline foundations ---- */

typedef struct robo_font_metrics_variation_context {
    const robo_font_variation_coords_t *coords; /* borrowed; may be NULL */
    robo_font_metrics_variation_state_t state;
} robo_font_metrics_variation_context_t;

void robo_font_metrics_variation_context_init(robo_font_metrics_variation_context_t *ctx);
void robo_font_metrics_variation_context_set(
    robo_font_metrics_variation_context_t *ctx,
    const robo_font_variation_coords_t *coords);

/**
 * Outline request accepting optional variation coordinates.
 * Goal 5.18: returns default (static) outline; variation_applied stays 0.
 */
typedef struct robo_font_outline_request {
    robo_glyph_id_t glyph_id;
    const robo_font_variation_coords_t *coords; /* NULL → default */
} robo_font_outline_request_t;

void robo_font_outline_request_init(robo_font_outline_request_t *req);

/** 1 if outline was interpolated for coords (always 0 in Goal 5.18). */
int robo_font_outline_variation_applied(const robo_font_outline_t *o);

/* ---- Fuzz ---- */

robo_result_t robo_font_variation_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_VARIATION_H */
