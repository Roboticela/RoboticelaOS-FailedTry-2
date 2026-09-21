/*
 * RoboFont metrics model (Goals 5.1 + 5.7).
 *
 * Distinguishes:
 *   font-space units (FUnits)     — design units; depend on units-per-em
 *   device-independent pixels     — CSS px / logical pixels
 *   device pixels                 — physical framebuffer (later DPI)
 *
 * Goal 5.7 builds an immutable metrics info object from SFNT tables
 * (head/hhea/hmtx/OS/2/post + optional vhea/vmtx). Metrics are measurements;
 * they are not shaping, layout, or kerning.
 *
 * Outline technology (glyf / CFF / CFF2) does not change the hmtx-based
 * horizontal metrics API. Glyph bounding boxes are available for TrueType
 * when glyf is present; CFF/CFF2 bbox awaits Goal 5.8 outlines.
 */

#ifndef ROBO_FONT_METRICS_H
#define ROBO_FONT_METRICS_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FontTt FontTt;
typedef struct FontSfnt FontSfnt;

#define RoboFontUpemMin 1u
#define RoboFontUpemMax 16384u
#define RoboFontOs2UseTypoMetrics 0x0080u

/* ---- Availability ---- */

typedef enum FontMetricAvail {
    RoboFontMetricPresent = 0,     /* font-authored */
    RoboFontMetricUnavailable = 1, /* table/field absent */
    RoboFontMetricFallback = 2,    /* runtime default (not font-authored) */
    RoboFontMetricMalformed = 3    /* present but invalid */
} FontMetricAvail;

typedef struct FontOptionalUnit {
    FontUnit value;
    FontMetricAvail avail;
} FontOptionalUnit;

typedef struct FontOptionalU16 {
    uint16_t value;
    FontMetricAvail avail;
} FontOptionalU16;

/* ---- Canonical line metrics (layout) ---- */

/**
 * Typographic line metrics in font units (+Y up).
 * ascent: above baseline (typically positive).
 * descent: below baseline as non-negative magnitude (design y = -descent).
 * lineGap: extra recommended spacing between lines.
 *
 * This is the canonical layout line-metrics snapshot (see Metrics.md policy).
 */
typedef struct FontMetrics {
    uint16_t unitsPerEm; /* typically 1000 or 2048; 0 = unset */
    FontUnit ascent;
    FontUnit descent; /* magnitude ≥ 0 */
    FontUnit lineGap;
    FontBbox bbox; /* font bounding box in font units; zero if unset */
    uint8_t hasBbox;
    uint8_t valid; /* 1 if ascent/descent/upem were set intentionally */
} FontMetrics;

typedef enum FontLineMetricSource {
    RoboFontLineFromHhea = 0,
    RoboFontLineFromOs2Typo = 1 /* fsSelection USE_TYPO_METRICS */
} FontLineMetricSource;

typedef enum FontRoundMode {
    RoboFontRoundNearest = 0, /* ties away from zero */
    RoboFontRoundFloor = 1,
    RoboFontRoundCeil = 2,
    RoboFontRoundTowardZero = 3
} FontRoundMode;

/* ---- Glyph metrics ---- */

typedef enum FontGlyphMetricsStatus {
    RoboFontGlyphMetricsOk = 0,
    RoboFontGlyphMetricsInvalidId = 1,
    RoboFontGlyphMetricsUnavailable = 2,
    RoboFontGlyphMetricsMalformed = 3
} FontGlyphMetricsStatus;

typedef struct FontGlyphMetrics {
    FontGlyphMetricsStatus status;
    GlyphId glyphId;
    uint16_t advanceWidth; /* FUnits; unshaped */
    int16_t leftSideBearing;
    int16_t rightSideBearing; /* valid only if hasRsb */
    uint8_t hasRsb;
    FontBbox bbox; /* glyph outline bbox when hasBbox */
    uint8_t hasBbox;
} FontGlyphMetrics;

/* ---- Vertical foundation ---- */

typedef struct FontVhea {
    uint16_t majorVersion;
    uint16_t minorVersion;
    int16_t ascent;
    int16_t descent;
    int16_t lineGap;
    int16_t advanceHeightMax;
    int16_t minTopSideBearing;
    int16_t minBottomSideBearing;
    int16_t yMaxExtent;
    int16_t caretSlopeRise;
    int16_t caretSlopeRun;
    int16_t caretOffset;
    int16_t metricDataFormat;
    uint16_t numberOfVMetrics;
} FontVhea;

typedef struct FontVerticalGlyphMetrics {
    FontGlyphMetricsStatus status;
    GlyphId glyphId;
    uint16_t advanceHeight;
    int16_t topSideBearing;
} FontVerticalGlyphMetrics;

/* ---- Full immutable metrics info ---- */

/**
 * Global metrics snapshot (copied out of the opaque info object).
 * Optional fields use avail; do not invent values when UNAVAILABLE.
 */
typedef struct FontMetricsGlobal {
    uint16_t unitsPerEm;
    FontMetricAvail upemAvail;

    /* Canonical layout line metrics (policy-selected) */
    FontMetrics layout;
    FontLineMetricSource layoutSource;

    /* Raw competing categories (never silently collapsed) */
    FontUnit hheaAscender;
    FontUnit hheaDescenderMag; /* magnitude */
    FontUnit hheaLineGap;
    uint8_t hasHheaLine;

    FontUnit typoAscender;
    FontUnit typoDescenderMag;
    FontUnit typoLineGap;
    uint8_t hasTypoLine; /* OS/2 present (fields always in v0+) */

    uint16_t winAscent;
    uint16_t winDescent;
    uint8_t hasWinClip; /* clipping metrics; not default line spacing */

    FontOptionalUnit capHeight;
    FontOptionalUnit xHeight;
    FontOptionalUnit averageCharWidth; /* OS/2 xAvgCharWidth */

    uint16_t maxAdvanceWidth;
    int16_t minLeftSideBearing;
    int16_t minRightSideBearing;
    int16_t maxXExtent;
    uint8_t hasHheaExtrema;

    int16_t caretSlopeRise;
    int16_t caretSlopeRun;
    int16_t caretOffset;
    FontMetricAvail caretAvail; /* PRESENT or FALLBACK */

    FontOptionalUnit underlinePosition;
    FontOptionalUnit underlineThickness;
    FontOptionalUnit strikeoutPosition;
    FontOptionalUnit strikeoutThickness;

    FontOptionalU16 usWeightClass;
    FontOptionalU16 usWidthClass;

    FontBbox fontBbox;
    FontMetricAvail fontBboxAvail;

    uint16_t numGlyphs;
    uint16_t numberOfHMetrics;
    uint8_t hasVertical; /* vhea+vmtx parsed */
} FontMetricsGlobal;

typedef struct FontMetricsInfo FontMetricsInfo;

void FontMetricsClear(FontMetrics *m);

/**
 * Initialize layout metrics. descent must be ≥ 0; unitsPerEm must be > 0.
 * bbox optional (pass NULL → hasBbox=0).
 */
Result FontMetricsInit(FontMetrics *m,
                                     uint16_t unitsPerEm,
                                     FontUnit ascent,
                                     FontUnit descent,
                                     FontUnit lineGap,
                                     const FontBbox *bbox);

/** Line height in font units: ascent + descent + lineGap. */
FontUnit FontMetricsLineHeight(const FontMetrics *m);

/**
 * Scale a font-unit length to CSS pixels given pixel size and upem.
 * Returns RoboInvalidArgument if upem==0 or out NULL.
 */
Result FontUnitsToPixels(FontUnit units,
                                        uint16_t unitsPerEm,
                                        FontPixelSize pixelSize,
                                        float *outPixels);

/**
 * Scale CSS pixels to font units (rounded toward nearest, ties away from 0).
 */
Result FontPixelsToUnits(float pixels,
                                        uint16_t unitsPerEm,
                                        FontPixelSize pixelSize,
                                        FontUnit *outUnits);

/** Approximate: points → CSS px assuming 96 CSS px per inch (1pt = 96/72 px). */
FontPixelSize FontPointsToPixels(FontPointSize points);

/**
 * Safe scale with overflow checks (int64 intermediate) and rounding policy.
 * Device-pixel conversion is intentionally not performed here.
 */
Result FontScaleFontUnits(FontUnit value,
                                         uint16_t unitsPerEm,
                                         FontPixelSize fontSize,
                                         FontRoundMode roundMode,
                                         float *outScaled);

/** Validate unitsPerEm (non-zero and within OT-reasonable bounds). */
int FontUnitsPerEmIsValid(uint16_t unitsPerEm);

/* ---- vhea / vmtx foundation ---- */

Result FontVheaParse(FontSpan span, FontVhea *out);

/**
 * Build immutable metrics info from a parsed TrueType bundle (retains @p tt).
 * Works for glyf, CFF, and CFF2 SFNT fonts that share head/hhea/hmtx/maxp.
 */
Result FontMetricsInfoCreateFromTt(FontTt *tt,
                                                    FontMetricsInfo **out);

/** Parse SFNT → TT (no glyf required) → metrics info. */
Result FontMetricsInfoCreate(FontSfnt *sfnt,
                                            FontMetricsInfo **out);

void FontMetricsInfoRetain(FontMetricsInfo *info);
void FontMetricsInfoRelease(FontMetricsInfo *info);

const FontMetricsGlobal *FontMetricsInfoGlobal(
    const FontMetricsInfo *info);

/** Canonical layout metrics (same as global->layout). */
Result FontMetricsInfoLayout(const FontMetricsInfo *info,
                                            FontMetrics *out);

Result FontMetricsInfoGlyph(const FontMetricsInfo *info,
                                           GlyphId glyphId,
                                           FontGlyphMetrics *out);

/**
 * Vertical glyph metrics when vhea/vmtx present.
 * Otherwise status = UNAVAILABLE.
 */
Result FontMetricsInfoGlyphVertical(
    const FontMetricsInfo *info,
    GlyphId glyphId,
    FontVerticalGlyphMetrics *out);

const FontVhea *FontMetricsInfoVhea(
    const FontMetricsInfo *info); /* NULL if absent */

/** Fuzz-ready: parse metrics from arbitrary SFNT-ish bytes. */
Result FontMetricsFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_METRICS_H */
