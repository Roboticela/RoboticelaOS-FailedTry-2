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

#define RoboFontVarMaxAxes 64u
#define RoboFontVarMaxInstances 512u
#define RoboFontVarMaxAvarSegments 256u
#define RoboFontVarMaxNameBytes 256u
#define RoboFontVarCoordsCacheMax 64u

#define RoboFontTagWght RoboFontTag('w', 'g', 'h', 't')
#define RoboFontTagWdth RoboFontTag('w', 'd', 't', 'h')
#define RoboFontTagItal RoboFontTag('i', 't', 'a', 'l')
#define RoboFontTagSlnt RoboFontTag('s', 'l', 'n', 't')
#define RoboFontTagOpsz RoboFontTag('o', 'p', 's', 'z')

/** Axis flag: hidden from UI (OpenType AXIS_FLAG_HIDDEN). */
#define RoboFontVarAxisFlagHidden 0x0001u

typedef enum FontVarAxisKind {
    RoboFontVarAxisUnknown = 0,
    RoboFontVarAxisRegistered = 1, /* wght/wdth/ital/slnt/opsz */
    RoboFontVarAxisCustom = 2
} FontVarAxisKind;

typedef enum FontVarCapabilities {
    RoboFontVarCapNone = 0,
    RoboFontVarCapFvar = 1u << 0,
    RoboFontVarCapAvar = 1u << 1,
    RoboFontVarCapStat = 1u << 2,
    RoboFontVarCapGvar = 1u << 3,  /* presence only */
    RoboFontVarCapHvar = 1u << 4,
    RoboFontVarCapMvar = 1u << 5,
    RoboFontVarCapCff2 = 1u << 6,
    RoboFontVarCapNamedInstances = 1u << 7
} FontVarCapabilities;

/**
 * Whether variation-aware metrics deltas have been applied.
 * Goal 5.18 exposes PENDING when coords are set but HVAR/MVAR are not executed.
 */
typedef enum FontMetricsVariationState {
    RoboFontMetricsVarStatic = 0,  /* no variation / default instance */
    RoboFontMetricsVarPending = 1, /* coords present; deltas not applied */
    RoboFontMetricsVarApplied = 2  /* reserved for later goals */
} FontMetricsVariationState;

typedef struct FontVariationAxis {
    FontTag tag;
    float minValue;
    float defaultValue;
    float maxValue;
    uint16_t flags;
    uint16_t nameId;
    FontVarAxisKind kind;
    char nameUtf8[RoboFontVarMaxNameBytes]; /* may be empty */
    uint8_t nameLen;
} FontVariationAxis;

typedef struct FontVariationInstance {
    uint32_t index;
    uint16_t subfamilyNameId;
    uint16_t flags;
    uint16_t postscriptNameId; /* 0xFFFF if absent */
    char nameUtf8[RoboFontVarMaxNameBytes];
    uint8_t nameLen;
    float *userCoords; /* axisCount values; owned by model */
} FontVariationInstance;

typedef struct FontVariationModel FontVariationModel;
typedef struct FontVariationCoords FontVariationCoords;
typedef struct FontOutline FontOutline;

typedef struct FontVariationRequest {
    /** Tagged user-space settings (borrowed). */
    const FontAxisSetting *axes;
    uint32_t axisCount;
    /** Named instance selection (UINT32_MAX = none). */
    uint32_t namedInstanceIndex;
    uint8_t hasNamedInstance;
    /** Optical size (opsz) — explicit. */
    uint8_t hasOpticalSize;
    float opticalSize;
    /** Automatic opsz request flag (no CSS policy applied). */
    uint8_t opticalSizeAuto;
    /** Weight / stretch / style mapped onto registered axes when present. */
    uint8_t applyStyleAxes;
    FontWeight weight;
    FontStretch stretch;
    FontStyle style;
} FontVariationRequest;

void FontVariationRequestInit(FontVariationRequest *req);

/* ---- Axis helpers ---- */

FontVarAxisKind FontVariationAxisKind(FontTag tag);
int FontVariationTagIsRegistered(FontTag tag);

/** Clamp user value into [min, max]; rejects NaN/Inf → RoboInvalidArgument. */
Result FontVariationClampUser(float minV,
                                             float defV,
                                             float maxV,
                                             float requested,
                                             float *outClamped);

/**
 * Default normalization: min→[-1], default→0, max→[+1].
 * Input should already be clamped. NaN/Inf → error.
 */
Result FontVariationNormalize(float minV,
                                            float defV,
                                            float maxV,
                                            float user,
                                            float *outNorm);

/* ---- Model (immutable after construction) ---- */

/**
 * Parse fvar (+ optional avar) from SFNT. Optional STAT/name for enrichment.
 * Missing fvar → RoboNotFound (out unchanged / NULL).
 */
Result FontVariationModelCreate(FontSfnt *sfnt,
                                               const FontStat *statOpt,
                                               const FontNameInfo *nameOpt,
                                               FontVariationModel **out);

Result FontVariationModelCreateFromFace(
    FontFace *face,
    FontVariationModel **out);

void FontVariationModelRetain(FontVariationModel *m);
void FontVariationModelRelease(FontVariationModel *m);

uint32_t FontVariationModelCapabilities(const FontVariationModel *m);
int FontVariationModelIsVariable(const FontVariationModel *m);

uint32_t FontVariationModelAxisCount(const FontVariationModel *m);
const FontVariationAxis *FontVariationModelAxis(
    const FontVariationModel *m,
    uint32_t index);

Result FontVariationModelFindAxis(const FontVariationModel *m,
                                                  FontTag tag,
                                                  uint32_t *outIndex,
                                                  const FontVariationAxis **outAxis);

uint32_t FontVariationModelInstanceCount(const FontVariationModel *m);
const FontVariationInstance *FontVariationModelInstance(
    const FontVariationModel *m,
    uint32_t index);

int FontVariationModelHasAvar(const FontVariationModel *m);

/** STAT axis tags that agree with fvar (informational; never overrides ranges). */
uint32_t FontVariationModelStatAxisCount(const FontVariationModel *m);
FontTag FontVariationModelStatAxisTag(
    const FontVariationModel *m,
    uint32_t index);

/* ---- Coordinates ---- */

/**
 * Build immutable coordinate snapshot from a request.
 * Named instance fills defaults then overlays instance coords.
 * Style axes (wght/wdth/ital/slnt) applied when applyStyleAxes≠0.
 * Explicit axis settings override. Optical size fills opsz when present.
 */
Result FontVariationCoordsCreate(
    const FontVariationModel *model,
    const FontVariationRequest *req,
    FontVariationCoords **out);

/** Defaults at each axis default (normalized 0, avar-mapped). */
Result FontVariationCoordsCreateDefault(
    const FontVariationModel *model,
    FontVariationCoords **out);

void FontVariationCoordsRetain(FontVariationCoords *c);
void FontVariationCoordsRelease(FontVariationCoords *c);

uint32_t FontVariationCoordsAxisCount(const FontVariationCoords *c);
float FontVariationCoordsUser(const FontVariationCoords *c, uint32_t i);
float FontVariationCoordsNormalized(const FontVariationCoords *c,
                                            uint32_t i);
/** After avar (equals normalized when avar absent). */
float FontVariationCoordsAdjusted(const FontVariationCoords *c,
                                          uint32_t i);

Result FontVariationCoordsUserForTag(
    const FontVariationCoords *c,
    FontTag tag,
    float *out);

/* ---- Selection / instance helpers ---- */

/**
 * Map weight/stretch/style (+ optional axes/opsz/named instance) onto a
 * variation request. Does not invent missing axes.
 */
void FontVariationRequestFromStyle(const FontStyleDesc *style,
                                            float opticalSize,
                                            uint8_t hasOpticalSize,
                                            uint8_t opticalSizeAuto,
                                            uint32_t namedInstanceIndex,
                                            uint8_t hasNamedInstance,
                                            FontVariationRequest *out);

/**
 * True when a variable face can satisfy the request via axes (exact or clamp).
 * Used by selection to prefer variation over another physical face.
 */
int FontVariationCanSatisfyWeight(const FontVariationModel *m,
                                           FontWeight weight);
int FontVariationCanSatisfyStretch(const FontVariationModel *m,
                                            FontStretch stretch);
int FontVariationCanSatisfyStyle(const FontVariationModel *m,
                                          FontStyle style);

/** Apply avar segment map for one axis (public for tests). */
Result FontVariationAvarMap(const FontVariationModel *m,
                                           uint32_t axisIndex,
                                           float normalized,
                                           float *outAdjusted);

/* ---- Variation-aware metrics / outline foundations ---- */

typedef struct FontMetricsVariationContext {
    const FontVariationCoords *coords; /* borrowed; may be NULL */
    FontMetricsVariationState state;
} FontMetricsVariationContext;

void FontMetricsVariationContextInit(FontMetricsVariationContext *ctx);
void FontMetricsVariationContextSet(
    FontMetricsVariationContext *ctx,
    const FontVariationCoords *coords);

/**
 * Outline request accepting optional variation coordinates.
 * Goal 5.18: returns default (static) outline; variation_applied stays 0.
 */
typedef struct FontOutlineRequest {
    GlyphId glyphId;
    const FontVariationCoords *coords; /* NULL → default */
} FontOutlineRequest;

void FontOutlineRequestInit(FontOutlineRequest *req);

/** 1 if outline was interpolated for coords (always 0 in Goal 5.18). */
int FontOutlineVariationApplied(const FontOutline *o);

/* ---- Fuzz ---- */

Result FontVariationFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_VARIATION_H */
