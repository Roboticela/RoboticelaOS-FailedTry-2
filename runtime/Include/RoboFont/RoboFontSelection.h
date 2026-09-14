/*
 * RoboFont selection — match a request to loaded faces (Goal 5.11).
 *
 * Operates on already-loaded RoboFontFace objects with metadata +
 * classification (Goals 5.9–5.10). Does not discover, install, or fall back
 * fonts. Does not reshape or scale glyphs.
 *
 * Matching order within a family match set (CSS Fonts–inspired):
 *   1. stretch distance
 *   2. style distance
 *   3. weight distance
 *   4. deterministic tie-break (exactness, WWS flag, face_id)
 *
 * Family identity is a gate: faces that do not match the requested family
 * (or aliases) are never selected.
 *
 * See docs/Selection.md for full rules.
 */

#ifndef ROBO_FONT_SELECTION_H
#define ROBO_FONT_SELECTION_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontClassification.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_FONT_SELECT_MAX_CANDIDATES 4096u
#define ROBO_FONT_SELECT_MAX_ALIASES 32u
#define ROBO_FONT_SELECT_MAX_FAMILY_BYTES 1024u

/** Which name field produced the family identity match (lower rank = better). */
typedef enum robo_font_select_family_kind {
    ROBO_FONT_SELECT_FAMILY_NONE = 0,
    ROBO_FONT_SELECT_FAMILY_TYPOGRAPHIC = 1, /* name ID 16 */
    ROBO_FONT_SELECT_FAMILY_WWS = 2,         /* name ID 21 */
    ROBO_FONT_SELECT_FAMILY_LEGACY = 3,      /* name ID 1 */
    ROBO_FONT_SELECT_FAMILY_FULL = 4,        /* name ID 4 */
    ROBO_FONT_SELECT_FAMILY_POSTSCRIPT = 5,  /* name ID 6 */
    ROBO_FONT_SELECT_FAMILY_ALIAS = 6        /* matched via request alias */
} robo_font_select_family_kind_t;

typedef enum robo_font_select_flags {
    ROBO_FONT_SELECT_FLAGS_NONE = 0,
    /**
     * Prefer WWS family (21) over typographic (16) when both match.
     * Default: typographic > WWS > legacy > full > PostScript.
     */
    ROBO_FONT_SELECT_FLAG_PREFER_WWS = 1,
    /** Allow full name (ID 4) as a family identity match. */
    ROBO_FONT_SELECT_FLAG_ALLOW_FULL_NAME = 2,
    /** Allow PostScript name (ID 6) as a family identity match. */
    ROBO_FONT_SELECT_FLAG_ALLOW_POSTSCRIPT = 4
} robo_font_select_flags_t;

/**
 * Selection request. Not a CSS type — reusable by RoboText / future CSS.
 * Strings are borrowed UTF-8 (not owned). Optional axes (Goal 5.18) allow
 * explicit variation settings and named-instance / optical-size hints.
 */
typedef struct robo_font_select_request {
    const char *family;
    size_t family_len; /* 0 → strlen(family) */

    const char *const *aliases; /* optional; each NUL-terminated */
    uint32_t alias_count;       /* ≤ ROBO_FONT_SELECT_MAX_ALIASES */

    robo_font_weight_t weight;
    robo_font_stretch_t stretch;
    robo_font_style_t style;

    /** Optional requested oblique angle (millidegrees). Used when style=OBLIQUE. */
    uint8_t has_oblique_angle;
    int32_t oblique_angle_millidegrees;

    uint32_t flags; /* robo_font_select_flags_t */

    /** Explicit variation axis settings (tagged user coords). */
    robo_font_axis_setting_t axes[ROBO_FONT_STYLE_MAX_AXES];
    uint8_t axis_count;

    /** Named instance index when has_named_instance≠0. */
    uint8_t has_named_instance;
    uint32_t named_instance_index;

    /** Optical size (opsz) preference. */
    uint8_t has_optical_size;
    float optical_size;
    uint8_t optical_size_auto;
} robo_font_select_request_t;

/**
 * Per-candidate score. Lower distance keys are better.
 * `eligible` is 0 when the family gate fails (face never selected).
 */
typedef struct robo_font_match_score {
    uint8_t eligible;
    robo_font_select_family_kind_t family_kind;
    uint32_t family_rank;   /* 0 = best family kind among policy */
    uint32_t stretch_dist;  /* stretch distance key */
    uint32_t style_dist;    /* style distance key */
    uint32_t weight_dist;   /* weight distance key */
    uint32_t exact_bits;    /* bit0 stretch exact, bit1 style, bit2 weight */
    uint8_t wws_flag;       /* candidate fsSelection WWS */
    robo_font_face_id_t face_id;
} robo_font_match_score_t;

typedef struct robo_font_selection {
    uint32_t cookie; /* ROBO_FONT_SELECTION_COOKIE when initialized */
    uint8_t found;
    robo_font_face_t *face; /* retained when found; caller must release via
                               robo_font_selection_clear or face_release */
    robo_font_face_id_t face_id;
    robo_font_match_score_t score;
    robo_font_style_desc_t requested;
    robo_font_style_desc_t selected;
    robo_font_select_family_kind_t family_kind;
} robo_font_selection_t;

/** Cookie written by init/clear; select accepts zeroed or cookied out. */
#define ROBO_FONT_SELECTION_COOKIE 0x53454C31u /* 'SEL1' */

void robo_font_select_request_init(robo_font_select_request_t *req);
robo_result_t robo_font_select_request_validate(const robo_font_select_request_t *req);

void robo_font_selection_init(robo_font_selection_t *out);
void robo_font_selection_clear(robo_font_selection_t *sel);

/**
 * Compare two scores. Returns <0 if `a` is better, >0 if `b` is better, 0 if
 * identical keys (should not happen when face_id differs).
 */
int robo_font_match_score_compare(const robo_font_match_score_t *a,
                                  const robo_font_match_score_t *b);

/** Stretch / style / weight distance keys (lower better). Public for tests. */
uint32_t robo_font_select_stretch_dist(robo_font_stretch_t requested,
                                       robo_font_stretch_t candidate);
uint32_t robo_font_select_weight_dist(robo_font_weight_t requested,
                                      robo_font_weight_t candidate);
uint32_t robo_font_select_style_dist(robo_font_style_t requested,
                                     robo_font_style_t candidate,
                                     uint8_t req_has_angle,
                                     int32_t req_angle_mdeg,
                                     uint8_t cand_has_angle,
                                     int32_t cand_angle_mdeg);

/**
 * Score one face against a request. Ensures metadata + classification are
 * parsed. Sets score->eligible=0 when family does not match.
 */
robo_result_t robo_font_select_score_face(const robo_font_select_request_t *req,
                                          robo_font_face_t *face,
                                          robo_font_match_score_t *out_score);

/**
 * Select the best face among candidates.
 *
 * - `out` may be zeroed or previously initialized; a prior retained face is
 *   released when `out` carries ROBO_FONT_SELECTION_COOKIE.
 * - Skips NULL entries.
 * - Does not select a face that fails the family gate.
 * - Same request + same candidate set (by face_id multiset) → same winner
 *   regardless of input order.
 * - On success with no eligible face: ROBO_OK and out->found == 0.
 */
robo_result_t robo_font_select(const robo_font_select_request_t *req,
                               robo_font_face_t *const *candidates,
                               uint32_t candidate_count,
                               robo_font_selection_t *out);

/** Fuzz entry: arbitrary request-ish bytes. Never crashes. */
robo_result_t robo_font_selection_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_SELECTION_H */
