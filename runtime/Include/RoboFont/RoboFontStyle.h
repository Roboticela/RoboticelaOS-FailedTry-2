/*
 * RoboFont style model foundation (Goal 5.1).
 *
 * Not full CSS font matching — that belongs to later CSS/font integration.
 * Values follow CSS Fonts Module conventions so later matching can map 1:1.
 */

#ifndef ROBO_FONT_STYLE_H
#define ROBO_FONT_STYLE_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Font weight: integer 1..1000 (CSS Fonts 4).
 * Common named values provided as macros.
 */
typedef uint16_t robo_font_weight_t;

#define ROBO_FONT_WEIGHT_THIN        ((robo_font_weight_t)100u)
#define ROBO_FONT_WEIGHT_EXTRA_LIGHT ((robo_font_weight_t)200u)
#define ROBO_FONT_WEIGHT_LIGHT       ((robo_font_weight_t)300u)
#define ROBO_FONT_WEIGHT_NORMAL      ((robo_font_weight_t)400u)
#define ROBO_FONT_WEIGHT_MEDIUM      ((robo_font_weight_t)500u)
#define ROBO_FONT_WEIGHT_SEMI_BOLD   ((robo_font_weight_t)600u)
#define ROBO_FONT_WEIGHT_BOLD        ((robo_font_weight_t)700u)
#define ROBO_FONT_WEIGHT_EXTRA_BOLD  ((robo_font_weight_t)800u)
#define ROBO_FONT_WEIGHT_BLACK       ((robo_font_weight_t)900u)

/** True if weight is in the CSS-legal 1..1000 range. */
int robo_font_weight_is_valid(robo_font_weight_t w);

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

int robo_font_stretch_is_valid(robo_font_stretch_t s);

typedef enum robo_font_style {
    ROBO_FONT_STYLE_NORMAL = 0,
    ROBO_FONT_STYLE_ITALIC = 1,
    ROBO_FONT_STYLE_OBLIQUE = 2
} robo_font_style_t;

/**
 * Generic family concept (CSS generic-family). Matching is later.
 * Stored as an enum for API completeness; unused by Goal 5.1 loaders.
 */
typedef enum robo_font_generic_family {
    ROBO_FONT_GENERIC_NONE = 0,
    ROBO_FONT_GENERIC_SERIF = 1,
    ROBO_FONT_GENERIC_SANS_SERIF = 2,
    ROBO_FONT_GENERIC_MONOSPACE = 3,
    ROBO_FONT_GENERIC_CURSIVE = 4,
    ROBO_FONT_GENERIC_FANTASY = 5,
    ROBO_FONT_GENERIC_SYSTEM_UI = 6
} robo_font_generic_family_t;

/**
 * Face/instance style request (selection parameters).
 * Variation axes reserved for later (variable fonts) — unused slots must be 0.
 */
typedef struct robo_font_style_desc {
    robo_font_weight_t weight;   /* default NORMAL */
    robo_font_stretch_t stretch; /* default NORMAL */
    robo_font_style_t style;     /* default NORMAL */
    /**
     * Reserved for future variable-font axis coordinates.
     * Goal 5.1: must be zeroed; non-zero → ROBO_NOT_SUPPORTED on instantiate.
     */
    float reserved_axes[8];
    uint8_t reserved_axis_count; /* must be 0 in Goal 5.1 */
} robo_font_style_desc_t;

void robo_font_style_desc_init(robo_font_style_desc_t *out);

/** Validate weight/stretch/style; axes must be empty in Goal 5.1. */
robo_result_t robo_font_style_desc_validate(const robo_font_style_desc_t *desc);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_STYLE_H */
