/*
 * RoboFontCoverage — Unicode character coverage analysis (Goal 5.14).
 *
 * Answers character→glyph availability questions from a font's cmap.
 * Does NOT implement fallback, shaping, language support, or rendering.
 *
 * Coverage ≠ shaping support ≠ language support ≠ fallback suitability.
 */

#ifndef ROBO_FONT_COVERAGE_H
#define ROBO_FONT_COVERAGE_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FontFace FontFace;

/* ---- Limits ---- */

#define RoboFontCoverageMaxRanges 65536u
#define RoboFontCoverageMaxScripts 128u
#define RoboFontCoverageMaxVisit 1048576u /* 2^20 scalars during build */

/* ---- Query status ---- */

typedef enum FontCoverageStatus {
    /** No cmap mapping for this scalar. */
    RoboFontCoverageUnsupported = 0,
    /** Mapped in cmap to glyph ID 0 (`.notdef`). */
    RoboFontCoverageMappedNotdef = 1,
    /** Mapped to a non-zero glyph ID within numGlyphs. */
    RoboFontCoverageMappedUsable = 2,
    /** cmap claimed a mapping but glyph ID is out of range / malformed. */
    RoboFontCoverageInvalidMapping = 3,
    /** Input is not a Unicode scalar value. */
    RoboFontCoverageInvalidCodePoint = 4,
    /** Face/map has no usable primary cmap. */
    RoboFontCoverageNoCmap = 5
} FontCoverageStatus;

/**
 * Outline emptiness is not part of default coverage.
 * When probed via face outlines: UNKNOWN if outlines unavailable.
 */
typedef enum FontCoverageOutlineHint {
    RoboFontCoverageOutlineUnknown = 0,
    RoboFontCoverageOutlineNonempty = 1,
    RoboFontCoverageOutlineEmpty = 2
} FontCoverageOutlineHint;

typedef struct FontCoverageQueryResult {
    FontCoverageStatus status;
    GlyphId glyphId; /* valid for MAPPED_* */
    FontCoverageOutlineHint outlineHint;
} FontCoverageQueryResult;

/** Contiguous usable coverage (MAPPED_USABLE only). Inclusive bounds. */
typedef struct FontCoverageRange {
    uint32_t first;
    uint32_t last;
} FontCoverageRange;

typedef struct FontCoverageStats {
    uint32_t mappedScalars;       /* includes explicit .notdef mappings */
    uint32_t usableMappings;      /* GID > 0 */
    uint32_t notdefMappings;      /* explicit GID 0 */
    uint32_t invalidMappings;     /* malformed GIDs observed at build */
    uint32_t rangeCount;
    uint32_t scriptCount;         /* content scripts (excludes Common/Inherited) */
    uint32_t emojiScalars;        /* usable mappings with Emoji property */
    uint32_t minCodePoint;       /* among usable; 0xFFFFFFFF if none */
    uint32_t maxCodePoint;       /* among usable; 0 if none */
    uint32_t categoryBits;        /* bit i set → general category i seen (usable) */
    int hasUvs;
    int hasCommon;                /* Script or SCX includes Common */
    int hasInherited;             /* Script or SCX includes Inherited */
    int truncated;                 /* visit/range limits hit during build */
} FontCoverageStats;

typedef struct FontCoverageOptions {
    int analyzeScripts;    /* default 1 */
    int analyzeCategories; /* default 1 */
    int analyzeEmoji;      /* default 1 */
    uint32_t maxRanges;    /* 0 → RoboFontCoverageMaxRanges */
    uint32_t maxVisit;     /* 0 → RoboFontCoverageMaxVisit */
} FontCoverageOptions;

typedef struct FontCoverage FontCoverage;

void FontCoverageOptionsInit(FontCoverageOptions *opt);

/**
 * Build immutable coverage from a cmap map. Retains @p map.
 * Does not load glyph outlines.
 */
Result FontCoverageCreateFromCmap(
    const FontCmapMap *map,
    const FontCoverageOptions *opt,
    FontCoverage **out);

/** Ensure face cmap, then build/attach coverage (lazy, idempotent). */
Result FontCoverageCreateFromFace(
    FontFace *face,
    const FontCoverageOptions *opt,
    FontCoverage **out);

void FontCoverageRetain(FontCoverage *cov);
void FontCoverageRelease(FontCoverage *cov);

const FontCmapMap *FontCoverageCmap(const FontCoverage *cov);
const FontCoverageStats *GetFontCoverageStats(
    const FontCoverage *cov);

uint32_t FontCoverageRangeCount(const FontCoverage *cov);
const FontCoverageRange *GetFontCoverageRange(
    const FontCoverage *cov, uint32_t index);

uint32_t FontCoverageScriptCount(const FontCoverage *cov);
UnicodeScript FontCoverageScript(const FontCoverage *cov,
                                                uint32_t index);
/** 1 if content-script list or Common/Inherited flags include @p script. */
int FontCoverageHasScript(const FontCoverage *cov,
                                  UnicodeScript script);

/**
 * Exact membership for a usable glyph (MAPPED_USABLE).
 * Invalid scalars → 0. Does not treat .notdef as covered.
 */
int FontCoverageContains(const FontCoverage *cov,
                                uint32_t codePoint);

/**
 * Full query: unsupported / notdef / usable / invalid / no cmap.
 * Thread-safe for concurrent readers after construction.
 */
Result FontCoverageQuery(const FontCoverage *cov,
                                       uint32_t codePoint,
                                       FontCoverageQueryResult *out);

/**
 * Glyph ID when MAPPED_USABLE; otherwise returns RoboNotFound and sets
 * *outGid = 0. Never invents arbitrary glyph IDs.
 */
Result FontCoverageGlyph(const FontCoverage *cov,
                                       uint32_t codePoint,
                                       GlyphId *outGid);

/** UVS coverage wrapper (format 14). */
Result FontCoverageQueryUvs(const FontCoverage *cov,
                                           uint32_t baseCodePoint,
                                           uint32_t variationSelector,
                                           FontCmapUvsResult *out);

/**
 * Optional: refine outlineHint using face outlines (loads one glyph outline).
 * Empty outline ≠ unsupported — still a valid mapping.
 */
Result FontCoverageProbeOutline(FontFace *face,
                                               GlyphId glyphId,
                                               FontCoverageOutlineHint *out);

/** Approximate heap bytes for cache accounting. */
uint64_t FontCoverageApproxBytes(const FontCoverage *cov);

/** Fuzz/fuzz-ready: build coverage from arbitrary bytes without crashing. */
Result FontCoverageFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_COVERAGE_H */
