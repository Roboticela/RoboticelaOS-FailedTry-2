/*
 * RoboFont OpenType common structures (Goal 5.4).
 *
 * Shared layout formats: Coverage, ClassDef, Device, ScriptList, FeatureList,
 * LookupList. Structural only — no substitution/positioning execution.
 */

#ifndef ROBO_FONT_OT_COMMON_H
#define ROBO_FONT_OT_COMMON_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboFontOtMaxScripts 256u
#define RoboFontOtMaxLangsys 256u
#define RoboFontOtMaxFeatures 1024u
#define RoboFontOtMaxLookups 2048u
#define RoboFontOtMaxSubtables 256u
#define RoboFontOtMaxCoverageGlyphs 65535u
#define RoboFontOtMaxClassRanges 8192u
#define RoboFontOtMaxFeatureLookups 256u

/** Resolve Offset16 from @p base within @p table; result span starts at base+off. */
Result FontOtResolveOffset16(FontSpan table,
                                            size_t base,
                                            uint16_t offset,
                                            size_t *outAbs);

Result FontOtResolveOffset32(FontSpan table,
                                            size_t base,
                                            uint32_t offset,
                                            size_t *outAbs);

/* ---- Coverage ---- */

typedef enum FontOtCoverageFormat {
    RoboFontOtCoverageFormat1 = 1,
    RoboFontOtCoverageFormat2 = 2
} FontOtCoverageFormat;

typedef struct FontOtCoverageRange {
    uint16_t startGlyph;
    uint16_t endGlyph;
    uint16_t startCoverageIndex;
} FontOtCoverageRange;

typedef struct FontOtCoverage {
    uint16_t format;
    uint16_t glyphCount; /* format 1: array length; format 2: covered glyph count */
    uint16_t rangeCount; /* format 2 */
    uint16_t *glyphs;     /* format 1; owned */
    FontOtCoverageRange *ranges; /* format 2; owned */
} FontOtCoverage;

void FontOtCoverageClear(FontOtCoverage *c);

/** Parse Coverage at absolute @p offset within @p table. */
Result FontOtCoverageParse(FontSpan table,
                                          size_t offset,
                                          FontOtCoverage *out);

/**
 * Coverage index for @p glyphId, or RoboNotFound if not covered.
 * @p outIndex may be NULL.
 */
Result FontOtCoverageIndex(const FontOtCoverage *c,
                                          uint16_t glyphId,
                                          uint16_t *outIndex);

/* ---- ClassDef ---- */

typedef struct FontOtClassRange {
    uint16_t startGlyph;
    uint16_t endGlyph;
    uint16_t classValue;
} FontOtClassRange;

typedef struct FontOtClassDef {
    uint16_t format;
    uint16_t startGlyph; /* format 1 */
    uint16_t glyphCount; /* format 1 */
    uint16_t *classValues; /* format 1; owned */
    uint16_t rangeCount; /* format 2 */
    FontOtClassRange *ranges; /* format 2; owned */
} FontOtClassDef;

void FontOtClassDefClear(FontOtClassDef *c);

Result FontOtClassDefParse(FontSpan table,
                                           size_t offset,
                                           FontOtClassDef *out);

/** Class for glyph (0 if unlisted). */
Result FontOtClassDefGet(const FontOtClassDef *c,
                                         uint16_t glyphId,
                                         uint16_t *outClass);

/* ---- Device / VariationIndex (structural) ---- */

typedef struct FontOtDevice {
    uint16_t startSize;
    uint16_t endSize;
    uint16_t deltaFormat; /* 1..3 delta, 0x8000 = variation index */
    uint16_t deltaValueCount;
    uint16_t *deltaValues; /* owned; packed units as uint16 words */
} FontOtDevice;

void FontOtDeviceClear(FontOtDevice *d);

Result FontOtDeviceParse(FontSpan table,
                                        size_t offset,
                                        FontOtDevice *out);

/* ---- Script / Feature / Lookup lists ---- */

typedef struct FontOtLangSys {
    uint16_t lookupOrder; /* always 0 / reserved */
    uint16_t requiredFeatureIndex; /* 0xFFFF = none */
    uint16_t featureIndexCount;
    uint16_t *featureIndices; /* owned */
} FontOtLangSys;

typedef struct FontOtLangSysRecord {
    FontTag langTag;
    size_t langSysOffset; /* absolute in table */
    FontOtLangSys langSys;
} FontOtLangSysRecord;

typedef struct FontOtScript {
    FontTag scriptTag;
    size_t scriptOffset;
    uint8_t hasDefaultLangSys;
    FontOtLangSys defaultLangSys;
    uint16_t langSysCount;
    FontOtLangSysRecord *langSysRecords; /* owned */
} FontOtScript;

typedef struct FontOtScriptList {
    uint16_t scriptCount;
    FontOtScript *scripts; /* owned */
} FontOtScriptList;

void FontOtScriptListClear(FontOtScriptList *sl);

Result FontOtScriptListParse(FontSpan table,
                                             size_t offset,
                                             FontOtScriptList *out);

typedef struct FontOtFeature {
    FontTag featureTag;
    size_t featureOffset;
    uint16_t featureParamsOffset; /* 0 = none; relative to Feature */
    uint16_t lookupIndexCount;
    uint16_t *lookupIndices; /* owned */
} FontOtFeature;

typedef struct FontOtFeatureList {
    uint16_t featureCount;
    FontOtFeature *features; /* owned */
} FontOtFeatureList;

void FontOtFeatureListClear(FontOtFeatureList *fl);

Result FontOtFeatureListParse(FontSpan table,
                                              size_t offset,
                                              FontOtFeatureList *out);

#define RoboFontOtLookupFlagRtl 0x0001u
#define RoboFontOtLookupFlagIgnoreBase 0x0002u
#define RoboFontOtLookupFlagIgnoreLigatures 0x0004u
#define RoboFontOtLookupFlagIgnoreMarks 0x0008u
#define RoboFontOtLookupFlagUseMarkFilteringSet 0x0010u
#define RoboFontOtLookupFlagMarkAttachmentTypeMask 0xFF00u

typedef struct FontOtLookupSubtable {
    size_t offset; /* absolute in parent GSUB/GPOS table */
    uint32_t lengthHint; /* 0 if unknown; remaining to table end if not set */
} FontOtLookupSubtable;

typedef struct FontOtLookup {
    uint16_t lookupType;
    uint16_t lookupFlag;
    uint16_t markFilteringSet; /* valid if USE_MARK_FILTERING_SET */
    uint16_t subtableCount;
    FontOtLookupSubtable *subtables; /* owned */
    size_t lookupOffset;
} FontOtLookup;

typedef struct FontOtLookupList {
    uint16_t lookupCount;
    FontOtLookup *lookups; /* owned */
} FontOtLookupList;

void FontOtLookupListClear(FontOtLookupList *ll);

Result FontOtLookupListParse(FontSpan table,
                                             size_t offset,
                                             FontOtLookupList *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_OT_COMMON_H */
