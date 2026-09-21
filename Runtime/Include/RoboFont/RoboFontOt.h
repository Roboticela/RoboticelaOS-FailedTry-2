/*
 * RoboFont OpenType semantic tables (Goal 5.4).
 *
 * Parses and represents OpenType structures for later shaping/color/selection.
 * Does NOT execute GSUB/GPOS, shape text, or fully map cmap (5.6).
 * CFF/CFF2 table bodies are Goal 5.5 (`RoboFontCff.h`).
 *
 * Ownership: FontOt retains the SFNT (and thus the source).
 */

#ifndef ROBO_FONT_OT_H
#define ROBO_FONT_OT_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontOtCommon.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Outline technology ---- */

typedef enum FontOutlineTech {
    RoboFontOutlineUnknown = 0,
    RoboFontOutlineTruetype = 1, /* glyf + loca */
    RoboFontOutlineCff = 2,      /* 'CFF ' table present */
    RoboFontOutlineCff2 = 3,     /* 'CFF2' table present */
    RoboFontOutlineUnsupported = 4
} FontOutlineTech;

FontOutlineTech FontOtDetectOutline(const FontSfnt *sfnt);

/* ---- Table categories / registry helpers ---- */

typedef enum FontOtTableFamily {
    RoboFontOtFamilyUnknown = 0,
    RoboFontOtFamilyCore = 1,
    RoboFontOtFamilyMetric = 2,
    RoboFontOtFamilyCmap = 3,
    RoboFontOtFamilyLayout = 4,
    RoboFontOtFamilyVariation = 5,
    RoboFontOtFamilyColor = 6,
    RoboFontOtFamilyMath = 7,
    RoboFontOtFamilyLegacy = 8,
    RoboFontOtFamilyOutline = 9
} FontOtTableFamily;

FontOtTableFamily MakeFontOtTableFamily(FontTag tag);

/* ---- cmap structural (Goal 5.4; full mapping → 5.6) ---- */

typedef struct FontCmapEncoding {
    uint16_t platformId;
    uint16_t encodingId;
    uint32_t subtableOffset; /* from cmap table start */
    uint16_t format;          /* 0,2,4,6,8,10,12,13,14,… */
    uint32_t length;          /* subtable length field when known */
    uint32_t language;        /* format-dependent */
} FontCmapEncoding;

typedef struct FontCmap {
    uint16_t version;
    uint16_t numTables;
    FontCmapEncoding *encodings; /* owned; numTables */
    FontSpan table;               /* view */
} FontCmap;

void FontCmapClear(FontCmap *c);

/** Structural parse: encoding records + per-subtable format/length validation. */
Result FontCmapParse(FontSpan span, FontCmap *out);

int FontCmapFormatIsRecognized(uint16_t format);

/* ---- GDEF / GSUB / GPOS ---- */

typedef struct FontGdef {
    uint16_t majorVersion;
    uint16_t minorVersion;
    uint8_t hasGlyphClassDef;
    uint8_t hasAttachList;
    uint8_t hasLigCaretList;
    uint8_t hasMarkAttachClassDef;
    uint8_t hasMarkGlyphSets;
    uint8_t hasItemVarStore;
    FontOtClassDef glyphClassDef;
    FontOtClassDef markAttachClassDef;
    size_t attachListOffset;
    size_t ligCaretListOffset;
    size_t markGlyphSetsOffset;
    size_t itemVarStoreOffset;
    FontSpan table;
} FontGdef;

void FontGdefClear(FontGdef *g);
Result FontGdefParse(FontSpan span, FontGdef *out);

typedef enum FontOtLayoutKind {
    RoboFontOtLayoutGsub = 1,
    RoboFontOtLayoutGpos = 2
} FontOtLayoutKind;

typedef struct FontOtLayout {
    FontOtLayoutKind kind;
    uint16_t majorVersion;
    uint16_t minorVersion;
    FontOtScriptList scripts;
    FontOtFeatureList features;
    FontOtLookupList lookups;
    size_t featureVariationsOffset; /* 0 if absent */
    FontSpan table;
} FontOtLayout;

void FontOtLayoutClear(FontOtLayout *L);
Result FontGsubParse(FontSpan span, FontOtLayout *out);
Result FontGposParse(FontSpan span, FontOtLayout *out);

/* GSUB lookup types 1–8; GPOS 1–9 (extension = 7 GSUB / 9 GPOS). */
int FontGsubLookupTypeIsKnown(uint16_t type);
int FontGposLookupTypeIsKnown(uint16_t type);

/* ---- BASE / JSTF / MATH / kern / COLR / CPAL / STAT (structural) ---- */

typedef struct FontBase {
    uint16_t majorVersion;
    uint16_t minorVersion;
    uint8_t hasHoriz;
    uint8_t hasVert;
    size_t horizAxisOffset;
    size_t vertAxisOffset;
    size_t itemVarStoreOffset;
    FontSpan table;
} FontBase;

Result FontBaseParse(FontSpan span, FontBase *out);

typedef struct FontJstf {
    uint16_t majorVersion;
    uint16_t minorVersion;
    uint16_t scriptCount;
    FontSpan table;
} FontJstf;

Result FontJstfParse(FontSpan span, FontJstf *out);

typedef struct FontMath {
    uint32_t version; /* Fixed */
    size_t mathConstantsOffset;
    size_t mathGlyphInfoOffset;
    size_t mathVariantsOffset;
    FontSpan table;
} FontMath;

Result FontMathParse(FontSpan span, FontMath *out);

typedef struct FontKernSubtable {
    uint16_t version;
    uint16_t length;
    uint16_t coverage;
    uint16_t format; /* derived from coverage bits */
    size_t offset;   /* absolute in kern table */
} FontKernSubtable;

typedef struct FontKern {
    uint16_t version;
    uint16_t nTables;
    FontKernSubtable *subtables; /* owned */
    FontSpan table;
} FontKern;

void FontKernClear(FontKern *k);
Result FontKernParse(FontSpan span, FontKern *out);

typedef struct FontCpal {
    uint16_t version;
    uint16_t numPalettesEntries;
    uint16_t numPalette;
    uint16_t numColorRecords;
    size_t colorRecordsArrayOffset;
    FontSpan table;
} FontCpal;

Result FontCpalParse(FontSpan span, FontCpal *out);

typedef struct FontColr {
    uint16_t version;
    uint16_t numBaseGlyphRecords;
    size_t baseGlyphRecordsOffset;
    size_t layerRecordsOffset;
    uint16_t numLayerRecords;
    FontSpan table;
} FontColr;

Result FontColrParse(FontSpan span, FontColr *out);

typedef struct FontStatAxis {
    FontTag tag;
    uint16_t nameId;
    uint16_t ordering;
} FontStatAxis;

typedef struct FontStat {
    uint16_t majorVersion;
    uint16_t minorVersion;
    uint16_t designAxisSize;
    uint16_t designAxisCount;
    size_t designAxesOffset;
    uint16_t axisValueCount;
    size_t offsetToAxisValues;
    uint16_t elidedFallbackNameId;
    FontStatAxis *axes; /* owned; designAxisCount */
    FontSpan table;
} FontStat;

void FontStatClear(FontStat *s);
Result FontStatParse(FontSpan span, FontStat *out);

/* ---- Bundled OpenType parse ---- */

typedef enum FontOtFlags {
    RoboFontOtFlagsNone = 0,
    /** Parse layout tables when present (GDEF/GSUB/GPOS). */
    RoboFontOtFlagsParseLayout = 1,
    /** Parse BASE/JSTF/MATH/kern/COLR/CPAL/STAT when present. */
    RoboFontOtFlagsParseExtra = 2,
    /** Parse cmap structurally when present. */
    RoboFontOtFlagsParseCmap = 4,
    RoboFontOtFlagsParseAll =
        (RoboFontOtFlagsParseLayout | RoboFontOtFlagsParseExtra |
         RoboFontOtFlagsParseCmap)
} FontOtFlags;

typedef struct FontOt FontOt;

Result FontOtParse(FontSfnt *sfnt,
                                 uint32_t flags,
                                 FontOt **out);

void FontOtRetain(FontOt *ot);
void FontOtRelease(FontOt *ot);

const FontSfnt *FontOtSfnt(const FontOt *ot);
FontOutlineTech FontOtOutline(const FontOt *ot);

const FontCmap *FontOtCmap(const FontOt *ot);
const FontGdef *FontOtGdef(const FontOt *ot);
const FontOtLayout *FontOtGsub(const FontOt *ot);
const FontOtLayout *FontOtGpos(const FontOt *ot);
const FontBase *FontOtBase(const FontOt *ot);
const FontJstf *FontOtJstf(const FontOt *ot);
const FontMath *FontOtMath(const FontOt *ot);
const FontKern *FontOtKern(const FontOt *ot);
const FontColr *FontOtColr(const FontOt *ot);
const FontCpal *FontOtCpal(const FontOt *ot);
const FontStat *FontOtStat(const FontOt *ot);

/** Fuzz-ready entry. */
Result FontOtFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_OT_H */
