/*
 * RoboFont style / weight / stretch primitives (Goals 5.1 + 5.10).
 *
 * These are normalized CSS-Fonts-aligned value types. Full face classification
 * (raw OS/2 + precedence) lives in RoboFontClassification.h.
 *
 * Not font selection / matching — that is Goals 5.11+.
 */

#ifndef ROBO_FONT_STYLE_H
#define ROBO_FONT_STYLE_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Font weight: integer 1..1000 (CSS Fonts 4 / OpenType usWeightClass).
 */
typedef uint16_t robo_font_weight_t;

#define ROBO_FONT_WEIGHT_THIN         ((robo_font_weight_t)100u)
#define ROBO_FONT_WEIGHT_EXTRA_LIGHT  ((robo_font_weight_t)200u)
#define ROBO_FONT_WEIGHT_LIGHT        ((robo_font_weight_t)300u)
#define ROBO_FONT_WEIGHT_NORMAL       ((robo_font_weight_t)400u)
#define ROBO_FONT_WEIGHT_MEDIUM       ((robo_font_weight_t)500u)
#define ROBO_FONT_WEIGHT_SEMI_BOLD    ((robo_font_weight_t)600u)
#define ROBO_FONT_WEIGHT_BOLD         ((robo_font_weight_t)700u)
#define ROBO_FONT_WEIGHT_EXTRA_BOLD   ((robo_font_weight_t)800u)
#define ROBO_FONT_WEIGHT_BLACK        ((robo_font_weight_t)900u)
#define ROBO_FONT_WEIGHT_EXTRA_BLACK  ((robo_font_weight_t)1000u)

/** Named weight bucket derived from a numeric weight (ranges, not rewrite). */
typedef enum robo_font_weight_category {
    ROBO_FONT_WEIGHT_CAT_UNKNOWN = 0,
    ROBO_FONT_WEIGHT_CAT_THIN = 1,
    ROBO_FONT_WEIGHT_CAT_EXTRA_LIGHT = 2,
    ROBO_FONT_WEIGHT_CAT_LIGHT = 3,
    ROBO_FONT_WEIGHT_CAT_NORMAL = 4,
    ROBO_FONT_WEIGHT_CAT_MEDIUM = 5,
    ROBO_FONT_WEIGHT_CAT_SEMI_BOLD = 6,
    ROBO_FONT_WEIGHT_CAT_BOLD = 7,
    ROBO_FONT_WEIGHT_CAT_EXTRA_BOLD = 8,
    ROBO_FONT_WEIGHT_CAT_BLACK = 9,
    ROBO_FONT_WEIGHT_CAT_EXTRA_BLACK = 10
} robo_font_weight_category_t;

/** True if weight is in the CSS-legal 1..1000 range. */
int robo_font_weight_is_valid(robo_font_weight_t w);

/** Map a valid weight to a named category; invalid → UNKNOWN. */
robo_font_weight_category_t robo_font_weight_category(robo_font_weight_t w);

/**
 * Width / stretch as a percentage of normal (CSS font-stretch).
 * 100 = normal. Typical range 50..200.
 */
typedef uint16_t robo_font_stretch_t;

#define ROBO_FONT_STRETCH_ULTRA_CONDENSED ((robo_font_stretch_t)50u)
#define ROBO_FONT_STRETCH_EXTRA_CONDENSED ((robo_font_stretch_t)62u)
#define ROBO_FONT_STRETCH_CONDENSED       ((robo_font_stretch_t)75u)
#define ROBO_FONT_STRETCH_SEMI_CONDENSED  ((robo_font_stretch_t)87u)
#define ROBO_FONT_STRETCH_NORMAL          ((robo_font_stretch_t)100u)
#define ROBO_FONT_STRETCH_SEMI_EXPANDED   ((robo_font_stretch_t)112u)
#define ROBO_FONT_STRETCH_EXPANDED        ((robo_font_stretch_t)125u)
#define ROBO_FONT_STRETCH_EXTRA_EXPANDED  ((robo_font_stretch_t)150u)
#define ROBO_FONT_STRETCH_ULTRA_EXPANDED  ((robo_font_stretch_t)200u)

/**
 * OpenType usWidthClass 1..9 (0 = unknown / invalid for normalized use).
 */
typedef enum robo_font_width_class {
    ROBO_FONT_WIDTH_UNKNOWN = 0,
    ROBO_FONT_WIDTH_ULTRA_CONDENSED = 1,
    ROBO_FONT_WIDTH_EXTRA_CONDENSED = 2,
    ROBO_FONT_WIDTH_CONDENSED = 3,
    ROBO_FONT_WIDTH_SEMI_CONDENSED = 4,
    ROBO_FONT_WIDTH_NORMAL = 5,
    ROBO_FONT_WIDTH_SEMI_EXPANDED = 6,
    ROBO_FONT_WIDTH_EXPANDED = 7,
    ROBO_FONT_WIDTH_EXTRA_EXPANDED = 8,
    ROBO_FONT_WIDTH_ULTRA_EXPANDED = 9
} robo_font_width_class_t;

int robo_font_stretch_is_valid(robo_font_stretch_t s);
int robo_font_width_class_is_valid(uint16_t wc);

/** Map OpenType width class 1..9 → stretch percent; invalid → NORMAL. */
robo_font_stretch_t robo_font_stretch_from_width_class(uint16_t us_width_class);

/** Inverse: nearest OpenType width class for a stretch percent. */
robo_font_width_class_t robo_font_width_class_from_stretch(robo_font_stretch_t s);

typedef enum robo_font_style {
    ROBO_FONT_STYLE_NORMAL = 0,
    ROBO_FONT_STYLE_ITALIC = 1,
    ROBO_FONT_STYLE_OBLIQUE = 2,
    ROBO_FONT_STYLE_UNKNOWN = 3
} robo_font_style_t;

/**
 * Generic font-family identifiers (Goal 5.16).
 * Semantic categories — not concrete faces. Matching/config in
 * RoboFontGenericFamily.
 */
typedef enum robo_font_generic_family {
    ROBO_FONT_GENERIC_NONE = 0,
    ROBO_FONT_GENERIC_SERIF = 1,
    ROBO_FONT_GENERIC_SANS_SERIF = 2,
    ROBO_FONT_GENERIC_MONOSPACE = 3,
    ROBO_FONT_GENERIC_CURSIVE = 4,
    ROBO_FONT_GENERIC_FANTASY = 5,
    ROBO_FONT_GENERIC_SYSTEM_UI = 6,
    ROBO_FONT_GENERIC_EMOJI = 7,
    ROBO_FONT_GENERIC_MATH = 8,
    ROBO_FONT_GENERIC_UI_SERIF = 9,
    ROBO_FONT_GENERIC_UI_SANS_SERIF = 10,
    ROBO_FONT_GENERIC_UI_MONOSPACE = 11,
    ROBO_FONT_GENERIC_UI_ROUNDED = 12,
    ROBO_FONT_GENERIC_COUNT = 13
} robo_font_generic_family_t;

/** Alias for invalid / unset (same as NONE). */
#define ROBO_FONT_GENERIC_UNKNOWN ROBO_FONT_GENERIC_NONE

/**
 * Face/instance style request (selection parameters).
 * Variation axes reserved for later (Goal 5.18) — unused slots must be 0.
 */
typedef struct robo_font_style_desc {
    robo_font_weight_t weight;   /* default NORMAL */
    robo_font_stretch_t stretch; /* default NORMAL */
    robo_font_style_t style;     /* default NORMAL */
    /**
     * Reserved for future variable-font axis coordinates.
     * Non-zero → ROBO_NOT_SUPPORTED on instantiate until Goal 5.18.
     */
    float reserved_axes[8];
    uint8_t reserved_axis_count; /* must be 0 until variable fonts */
} robo_font_style_desc_t;

void robo_font_style_desc_init(robo_font_style_desc_t *out);

/** Validate weight/stretch/style; axes must be empty until Goal 5.18. */
robo_result_t robo_font_style_desc_validate(const robo_font_style_desc_t *desc);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_STYLE_H */
