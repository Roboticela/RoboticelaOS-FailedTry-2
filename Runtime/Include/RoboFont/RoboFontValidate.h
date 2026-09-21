/*
 * RoboFontValidate — font security & validation (Goal 5.20).
 *
 * Treats every font as untrusted input. Orchestrates staged structural
 * validation, centralized resource limits, checksum policy, and diagnostics.
 *
 * Individual parsers remain defensive; validation never disables
 * memory-safety bounds checks.
 *
 * Does NOT implement: shaping, rasterization, web-font download policy,
 * OS trust stores, DRM, or browser sandboxing.
 *
 * Chromium OTS is reference-only — no OTS/Chromium dependency.
 */

#ifndef ROBO_FONT_VALIDATE_H
#define ROBO_FONT_VALIDATE_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontLoader.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Levels (extend loader policies) ---- */

typedef enum FontValidationLevel {
    /** Parsers' mandatory bounds only; skip optional probes. */
    RoboFontValidationNone = 0,
    /** SFNT container + required table presence/consistency. */
    RoboFontValidationBasic = 1,
    /** BASIC + checksums + TT/maxp consistency. */
    RoboFontValidationStrict = 2,
    /** STRICT + color/variation/OT/CFF structural probes. */
    RoboFontValidationParanoid = 3
} FontValidationLevel;

typedef enum FontValidationStage {
    RoboFontValidationStageContainer = 1,
    RoboFontValidationStageMetadata = 2,
    RoboFontValidationStageFeatures = 3,
    RoboFontValidationStageGlyphColorVar = 4
} FontValidationStage;

typedef enum FontValidationSeverity {
    RoboFontValidationSeverityInfo = 0,
    RoboFontValidationSeverityWarning = 1,
    RoboFontValidationSeverityError = 2,
    RoboFontValidationSeverityFatal = 3
} FontValidationSeverity;

typedef enum FontChecksumPolicy {
    RoboFontChecksumAccept = 0, /* record mismatch as info/warning */
    RoboFontChecksumWarn = 1,
    RoboFontChecksumReject = 2
} FontChecksumPolicy;

typedef enum FontValidationIssueCode {
    RoboFontValidationOk = 0,
    RoboFontValidationSourceTooLarge = 1,
    RoboFontValidationInvalidSignature = 2,
    RoboFontValidationTableCount = 3,
    RoboFontValidationTableOffset = 4,
    RoboFontValidationTableOverlap = 5,
    RoboFontValidationDuplicateTag = 6,
    RoboFontValidationChecksumMismatch = 7,
    RoboFontValidationCollectionFaces = 8,
    RoboFontValidationGlyphCount = 9,
    RoboFontValidationMissingRequired = 10,
    RoboFontValidationParseFailed = 11,
    RoboFontValidationUnsupported = 12,
    RoboFontValidationColorLimit = 13,
    RoboFontValidationVariationLimit = 14,
    RoboFontValidationNameLimit = 15,
    RoboFontValidationUnicodeInvalid = 16,
    RoboFontValidationWorkBudget = 17,
    RoboFontValidationInternal = 18
} FontValidationIssueCode;

#define RoboFontValidationMaxIssues 64u

/* Hard ceilings — applications may request lower, never higher. */
#define RoboFontValidationHardMaxSourceSize RoboFontSfntMaxSourceSize
#define RoboFontValidationHardMaxTables RoboFontSfntMaxTables
#define RoboFontValidationHardMaxFaces 256u
#define RoboFontValidationHardMaxGlyphs 65535u
#define RoboFontValidationHardMaxAlloc (16u * 1024u * 1024u)
#define RoboFontValidationHardMaxWork 1000000u

typedef struct FontValidationLimits {
    uint64_t maxSourceSize;
    uint32_t maxTables;
    uint32_t maxTableSize;
    uint32_t maxCollectionFaces;
    uint32_t maxGlyphs;
    uint32_t maxNameRecords;
    uint32_t maxVariationAxes;
    uint32_t maxVariationInstances;
    uint32_t maxColorPalettes;
    uint32_t maxColorLayers;
    uint32_t maxPaintDepth;
    uint32_t maxSvgPayload;
    uint32_t maxBitmapPayload;
    uint32_t maxRecursionDepth;
    uint32_t maxAllocation;
    uint32_t maxWorkBudget; /* abstract units consumed during probes */
} FontValidationLimits;

typedef struct FontValidationPolicy {
    FontValidationLevel level;
    FontChecksumPolicy checksumPolicy;
    uint8_t allowUnsupportedTables; /* 1 = advisory only */
    uint8_t rejectOversized;         /* 1 = FATAL on limit breach */
    uint8_t stopOnFatal;            /* 1 = abort remaining stages */
    FontValidationLimits limits;
} FontValidationPolicy;

typedef struct FontValidationIssue {
    FontValidationIssueCode code;
    FontValidationSeverity severity;
    FontValidationStage stage;
    FontTag tableTag; /* 0 if N/A */
    uint32_t byteOffset;      /* UINT32_MAX if N/A */
    uint32_t glyphId;         /* UINT32_MAX if N/A */
    FontTag axisTag;  /* 0 if N/A */
    uint8_t recoverable;       /* 1 if non-fatal / continue */
    char message[96];
} FontValidationIssue;

typedef struct FontValidationResult FontValidationResult;

void FontValidationLimitsInit(FontValidationLimits *lim);
void FontValidationLimitsClamp(FontValidationLimits *lim);
void FontValidationPolicyInit(FontValidationPolicy *pol);
void FontValidationPolicyFromLoader(FontValidatePolicy loaderPol,
                                             FontValidationPolicy *out);

FontValidationLevel FontValidationLevelFromLoader(
    FontValidatePolicy loaderPol);

/** Map extended levels into loader enum (PARANOID preserved). */
FontValidatePolicy FontValidationLevelToLoader(
    FontValidationLevel level);

const char *FontValidationIssueCodeString(FontValidationIssueCode code);
const char *FontValidationSeverityString(FontValidationSeverity sev);
const char *FontValidationStageString(FontValidationStage stage);

Result FontValidationResultCreate(FontValidationResult **out);
void FontValidationResultRetain(FontValidationResult *r);
void FontValidationResultRelease(FontValidationResult *r);

int FontValidationResultOk(const FontValidationResult *r);
int FontValidationResultHasFatal(const FontValidationResult *r);
uint32_t FontValidationResultIssueCount(const FontValidationResult *r);
Result FontValidationResultIssue(const FontValidationResult *r,
                                                uint32_t index,
                                                FontValidationIssue *out);
FontValidationStage FontValidationResultCompletedStage(
    const FontValidationResult *r);
uint32_t FontValidationResultWorkUnits(const FontValidationResult *r);

/**
 * Stage-1: validate source bytes as SFNT/TTC container (may parse SFNT).
 * On success may return a retained sfnt via out_sfnt (optional).
 */
Result FontValidateSource(FontSource *source,
                                        uint32_t faceIndex,
                                        const FontValidationPolicy *policy,
                                        FontValidationResult **outResult,
                                        FontSfnt **outSfntOpt);

/** Validate an already-parsed SFNT (stages 1–N per policy level). */
Result FontValidateSfnt(FontSfnt *sfnt,
                                      const FontValidationPolicy *policy,
                                      FontValidationResult **outResult);

/**
 * Validate a face through staged probes. Face must already exist.
 * Does not replace parser defenses. Optional stages parse feature tables.
 */
Result FontValidateFace(FontFace *face,
                                      const FontValidationPolicy *policy,
                                      FontValidationResult **outResult);

/**
 * Loader integration: map loader validate policy → full validate_face.
 * Returns RoboOk if no fatal issues (warnings allowed).
 */
Result FontValidateForLoad(FontFace *face,
                                          FontValidatePolicy loaderPol,
                                          FontValidationResult **outResultOpt);

/** Fuzz entry — never crashes. */
Result FontValidateFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_VALIDATE_H */
