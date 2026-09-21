/*
 * RoboFont instance — sized / configured use of a face (Goals 5.1 + 5.7 + 5.18).
 *
 * Face = resource identity. Instance = face + pixel size + style request
 * (+ optional variation coordinates for variable fonts).
 *
 * Does not rasterize glyphs. Call FontInstanceSyncMetrics after
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

typedef struct FontInstance FontInstance;

/**
 * Create an instance of @p face at @p pixelSize (> 0).
 * @p style NULL → defaults.
 * When the face is variable, weight/stretch/style (+ style.axes) produce a
 * shared variation-coordinate snapshot (no duplicate font load).
 * Retains @p face. If the face already has metrics info, layout metrics are
 * copied onto the instance automatically (static; variation deltas pending).
 */
Result FontInstanceCreate(FontFace *face,
                                        FontPixelSize pixelSize,
                                        const FontStyleDesc *style,
                                        FontInstance **out);

/**
 * Create with an explicit variation request (named instance / opsz / axes).
 * @p varReq NULL → behave like create() using style only.
 */
Result FontInstanceCreateVariation(
    FontFace *face,
    FontPixelSize pixelSize,
    const FontStyleDesc *style,
    const FontVariationRequest *varReq,
    FontInstance **out);

void FontInstanceRetain(FontInstance *inst);
void FontInstanceRelease(FontInstance *inst);

FontInstanceId GetFontInstanceId(const FontInstance *inst);
FontPixelSize FontInstancePixelSize(const FontInstance *inst);

/** Borrowed face. */
const FontFace *FontInstanceFace(const FontInstance *inst);

Result FontInstanceStyle(const FontInstance *inst,
                                       FontStyleDesc *out);

/** Borrowed variation coordinates (NULL if static face or default unused). */
const FontVariationCoords *FontInstanceVariationCoords(
    const FontInstance *inst);

FontMetricsVariationState FontInstanceMetricsVariationState(
    const FontInstance *inst);

/**
 * Attach metrics (e.g. for tests). Copies @p metrics.
 */
Result FontInstanceSetMetrics(FontInstance *inst,
                                             const FontMetrics *metrics);

/**
 * Copy canonical layout metrics from the face's metrics info (if parsed).
 * Does not apply HVAR/MVAR; state becomes PENDING when coords are present.
 */
Result FontInstanceSyncMetrics(FontInstance *inst);

/**
 * Read metrics. RoboNotFound if none set / not yet synced from the face.
 */
Result FontInstanceMetrics(const FontInstance *inst,
                                         FontMetrics *out);

/**
 * Scale a font-unit length using this instance's pixel size and metrics upem.
 * Requires valid metrics with unitsPerEm > 0.
 */
Result FontInstanceUnitsToPixels(const FontInstance *inst,
                                                 FontUnit units,
                                                 float *outPixels);

/**
 * Scale glyph advance (font units) to pixels with rounding policy.
 * Requires synced/attached metrics.
 */
Result FontInstanceScaleAdvance(const FontInstance *inst,
                                               uint16_t advanceWidth,
                                               FontRoundMode roundMode,
                                               float *outPixels);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_INSTANCE_H */
