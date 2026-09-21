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
typedef uint16_t FontWeight;

#define RoboFontWeightThin         ((FontWeight)100u)
#define RoboFontWeightExtraLight  ((FontWeight)200u)
#define RoboFontWeightLight        ((FontWeight)300u)
#define RoboFontWeightNormal       ((FontWeight)400u)
#define RoboFontWeightMedium       ((FontWeight)500u)
#define RoboFontWeightSemiBold    ((FontWeight)600u)
#define RoboFontWeightBold         ((FontWeight)700u)
#define RoboFontWeightExtraBold   ((FontWeight)800u)
#define RoboFontWeightBlack        ((FontWeight)900u)
#define RoboFontWeightExtraBlack  ((FontWeight)1000u)

/** Named weight bucket derived from a numeric weight (ranges, not rewrite). */
typedef enum FontWeightCategory {
    RoboFontWeightCatUnknown = 0,
    RoboFontWeightCatThin = 1,
    RoboFontWeightCatExtraLight = 2,
    RoboFontWeightCatLight = 3,
    RoboFontWeightCatNormal = 4,
    RoboFontWeightCatMedium = 5,
    RoboFontWeightCatSemiBold = 6,
    RoboFontWeightCatBold = 7,
    RoboFontWeightCatExtraBold = 8,
    RoboFontWeightCatBlack = 9,
    RoboFontWeightCatExtraBlack = 10
} FontWeightCategory;

/** True if weight is in the CSS-legal 1..1000 range. */
int FontWeightIsValid(FontWeight w);

/** Map a valid weight to a named category; invalid → UNKNOWN. */
FontWeightCategory MakeFontWeightCategory(FontWeight w);

/**
 * Width / stretch as a percentage of normal (CSS font-stretch).
 * 100 = normal. Typical range 50..200.
 */
typedef uint16_t FontStretch;

#define RoboFontStretchUltraCondensed ((FontStretch)50u)
#define RoboFontStretchExtraCondensed ((FontStretch)62u)
#define RoboFontStretchCondensed       ((FontStretch)75u)
#define RoboFontStretchSemiCondensed  ((FontStretch)87u)
#define RoboFontStretchNormal          ((FontStretch)100u)
#define RoboFontStretchSemiExpanded   ((FontStretch)112u)
#define RoboFontStretchExpanded        ((FontStretch)125u)
#define RoboFontStretchExtraExpanded  ((FontStretch)150u)
#define RoboFontStretchUltraExpanded  ((FontStretch)200u)

/**
 * OpenType usWidthClass 1..9 (0 = unknown / invalid for normalized use).
 */
typedef enum FontWidthClass {
    RoboFontWidthUnknown = 0,
    RoboFontWidthUltraCondensed = 1,
    RoboFontWidthExtraCondensed = 2,
    RoboFontWidthCondensed = 3,
    RoboFontWidthSemiCondensed = 4,
    RoboFontWidthNormal = 5,
    RoboFontWidthSemiExpanded = 6,
    RoboFontWidthExpanded = 7,
    RoboFontWidthExtraExpanded = 8,
    RoboFontWidthUltraExpanded = 9
} FontWidthClass;

int FontStretchIsValid(FontStretch s);
int FontWidthClassIsValid(uint16_t wc);

/** Map OpenType width class 1..9 → stretch percent; invalid → NORMAL. */
FontStretch FontStretchFromWidthClass(uint16_t usWidthClass);

/** Inverse: nearest OpenType width class for a stretch percent. */
FontWidthClass FontWidthClassFromStretch(FontStretch s);

typedef enum FontStyle {
    RoboFontStyleNormal = 0,
    RoboFontStyleItalic = 1,
    RoboFontStyleOblique = 2,
    RoboFontStyleUnknown = 3
} FontStyle;

/**
 * Generic font-family identifiers (Goal 5.16).
 * Semantic categories — not concrete faces. Matching/config in
 * RoboFontGenericFamily.
 */
typedef enum FontGenericFamily {
    RoboFontGenericNone = 0,
    RoboFontGenericSerif = 1,
    RoboFontGenericSansSerif = 2,
    RoboFontGenericMonospace = 3,
    RoboFontGenericCursive = 4,
    RoboFontGenericFantasy = 5,
    RoboFontGenericSystemUi = 6,
    RoboFontGenericEmoji = 7,
    RoboFontGenericMath = 8,
    RoboFontGenericUiSerif = 9,
    RoboFontGenericUiSansSerif = 10,
    RoboFontGenericUiMonospace = 11,
    RoboFontGenericUiRounded = 12,
    RoboFontGenericCount = 13
} FontGenericFamily;

/** Alias for invalid / unset (same as NONE). */
#define RoboFontGenericUnknown RoboFontGenericNone

/** Max tagged variation settings carried on a style/select request. */
#define RoboFontStyleMaxAxes 8u

/**
 * Explicit variable-font axis setting (user-space coordinate).
 * tag == 0 → unused slot.
 */
typedef struct FontAxisSetting {
    uint32_t tag;
    float value;
} FontAxisSetting;

/**
 * Face/instance style request (selection parameters).
 * Optional tagged axes (Goal 5.18) override registered-axis mapping from
 * weight/stretch/style when a matching tag is present.
 */
typedef struct FontStyleDesc {
    FontWeight weight;   /* default NORMAL */
    FontStretch stretch; /* default NORMAL */
    FontStyle style;     /* default NORMAL */
    FontAxisSetting axes[RoboFontStyleMaxAxes];
    uint8_t axisCount; /* ≤ RoboFontStyleMaxAxes; 0 = none */
} FontStyleDesc;

void FontStyleDescInit(FontStyleDesc *out);

/** Validate weight/stretch/style and axis settings (finite values, valid tags). */
Result FontStyleDescValidate(const FontStyleDesc *desc);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_STYLE_H */
