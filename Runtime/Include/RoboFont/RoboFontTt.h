/*
 * RoboFont TrueType semantic tables (Goal 5.3).
 *
 * Parses core TT tables on top of the SFNT container (Goal 5.2):
 *   head, hhea, maxp, hmtx, loca, glyf, name, OS/2, post
 *
 * Does NOT parse cmap (5.6), CFF (5.5), GSUB/GPOS (later), or rasterize.
 *
 * Ownership: FontTt retains the SFNT (and thus the source).
 * Glyph structural parse results own temporary arrays cleared by the caller.
 */

#ifndef ROBO_FONT_TT_H
#define ROBO_FONT_TT_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboFontHeadMagic ((uint32_t)0x5F0F3CF5u)
#define RoboFontTtMaxGlyphs 65535u
#define RoboFontTtMaxCompositeDepth 64u
#define RoboFontTtMaxComponentsPerGlyph 256u
#define RoboFontTtMaxNameRecords 4096u
#define RoboFontTtMaxNameString 4096u

/* ---- head ---- */

typedef struct FontHead {
    uint16_t majorVersion;
    uint16_t minorVersion;
    uint32_t fontRevision; /* Fixed 16.16 as raw uint32 */
    uint32_t checksumAdjustment;
    uint32_t magicNumber;
    uint16_t flags;
    uint16_t unitsPerEm;
    int64_t created;  /* LONGDATETIME */
    int64_t modified;
    int16_t xMin;
    int16_t yMin;
    int16_t xMax;
    int16_t yMax;
    uint16_t macStyle;
    uint16_t lowestRecPpem;
    int16_t fontDirectionHint;
    int16_t indexToLocFormat; /* 0 short, 1 long */
    int16_t glyphDataFormat;
} FontHead;

Result FontHeadParse(FontSpan span, FontHead *out);

/* ---- hhea ---- */

typedef struct FontHhea {
    uint16_t majorVersion;
    uint16_t minorVersion;
    int16_t ascender;
    int16_t descender;
    int16_t lineGap;
    uint16_t advanceWidthMax;
    int16_t minLeftSideBearing;
    int16_t minRightSideBearing;
    int16_t xMaxExtent;
    int16_t caretSlopeRise;
    int16_t caretSlopeRun;
    int16_t caretOffset;
    int16_t metricDataFormat;
    uint16_t numberOfHMetrics;
} FontHhea;

Result FontHheaParse(FontSpan span, FontHhea *out);

/* ---- maxp ---- */

typedef struct FontMaxp {
    uint32_t version; /* Fixed: 0x00005000 or 0x00010000 */
    uint16_t numGlyphs;
    /* v1.0 fields (zeroed for v0.5) */
    uint16_t maxPoints;
    uint16_t maxContours;
    uint16_t maxCompositePoints;
    uint16_t maxCompositeContours;
    uint16_t maxZones;
    uint16_t maxTwilightPoints;
    uint16_t maxStorage;
    uint16_t maxFunctionDefs;
    uint16_t maxInstructionDefs;
    uint16_t maxStackElements;
    uint16_t maxSizeOfInstructions;
    uint16_t maxComponentElements;
    uint16_t maxComponentDepth;
} FontMaxp;

Result FontMaxpParse(FontSpan span, FontMaxp *out);

/* ---- post ---- */

typedef struct FontPost {
    uint32_t version; /* Fixed */
    int32_t italicAngle; /* Fixed 16.16 */
    int16_t underlinePosition;
    int16_t underlineThickness;
    uint32_t isFixedPitch;
    uint32_t minMemType42;
    uint32_t maxMemType42;
    uint32_t minMemType1;
    uint32_t maxMemType1;
} FontPost;

Result FontPostParse(FontSpan span, FontPost *out);

/* ---- OS/2 (version-aware) ---- */

typedef struct FontOs2 {
    uint16_t version;
    int16_t xAvgCharWidth;
    uint16_t usWeightClass;
    uint16_t usWidthClass;
    uint16_t fsType;
    int16_t ySubscriptXSize;
    int16_t ySubscriptYSize;
    int16_t ySubscriptXOffset;
    int16_t ySubscriptYOffset;
    int16_t ySuperscriptXSize;
    int16_t ySuperscriptYSize;
    int16_t ySuperscriptXOffset;
    int16_t ySuperscriptYOffset;
    int16_t yStrikeoutSize;
    int16_t yStrikeoutPosition;
    int16_t sFamilyClass;
    uint8_t panose[10];
    uint32_t ulUnicodeRange1;
    uint32_t ulUnicodeRange2;
    uint32_t ulUnicodeRange3;
    uint32_t ulUnicodeRange4;
    uint8_t achVendId[4];
    uint16_t fsSelection;
    uint16_t usFirstCharIndex;
    uint16_t usLastCharIndex;
    int16_t sTypoAscender;
    int16_t sTypoDescender;
    int16_t sTypoLineGap;
    uint16_t usWinAscent;
    uint16_t usWinDescent;
    /* version >= 1 */
    uint32_t ulCodePageRange1;
    uint32_t ulCodePageRange2;
    uint8_t hasCodePageRanges;
    /* version >= 2 */
    int16_t sxHeight;
    int16_t sCapHeight;
    uint16_t usDefaultChar;
    uint16_t usBreakChar;
    uint16_t usMaxContext;
    uint8_t hasV2Metrics;
    /* version >= 5 */
    uint16_t usLowerOpticalPointSize;
    uint16_t usUpperOpticalPointSize;
    uint8_t hasV5Optical;
} FontOs2;

Result FontOs2Parse(FontSpan span, FontOs2 *out);

/* ---- name ---- */

#define RoboFontNameIdFamily 1u
#define RoboFontNameIdSubfamily 2u
#define RoboFontNameIdUnique 3u
#define RoboFontNameIdFull 4u
#define RoboFontNameIdVersion 5u
#define RoboFontNameIdPostscript 6u
#define RoboFontNameIdTypoFamily 16u
#define RoboFontNameIdTypoSubfamily 17u

typedef struct FontNameRecord {
    uint16_t platformId;
    uint16_t encodingId;
    uint16_t languageId;
    uint16_t nameId;
    uint16_t length;
    uint16_t offset; /* from string storage start */
} FontNameRecord;

typedef struct FontNameTable {
    uint16_t format;
    uint16_t count;
    uint16_t stringOffset;
    FontNameRecord *records; /* count entries; owned when from tt bundle */
    FontSpan storage;         /* string bytes view */
} FontNameTable;

/**
 * Parse name table header + records into @p out.
 * If @p out->records is NULL, allocates; caller must FontNameTableClear
 * unless owned by FontTt.
 */
Result FontNameParse(FontSpan span, FontNameTable *out);
void FontNameTableClear(FontNameTable *t);

/**
 * Decode a name record to UTF-8 into @p buf.
 * Prefers Windows Unicode (platform 3, encoding 1/10), then Mac Roman ASCII.
 * RoboNotFound if no decodable record for @p nameId.
 */
Result FontNameGetUtf8(const FontNameTable *t,
                                      uint16_t nameId,
                                      char *buf,
                                      size_t bufCap,
                                      size_t *outLen);

/* ---- hmtx / loca (query via tt object) ---- */

/* ---- glyf ---- */

#define RoboFontGlyfFlagOnCurve 0x01u
#define RoboFontGlyfFlagXShort 0x02u
#define RoboFontGlyfFlagYShort 0x04u
#define RoboFontGlyfFlagRepeat 0x08u
#define RoboFontGlyfFlagXSameOrPositive 0x10u
#define RoboFontGlyfFlagYSameOrPositive 0x20u

#define RoboFontGlyfCompArgsWords 0x0001u
#define RoboFontGlyfCompArgsXy 0x0002u
#define RoboFontGlyfCompRoundXy 0x0004u
#define RoboFontGlyfCompHaveScale 0x0008u
#define RoboFontGlyfCompMore 0x0020u
#define RoboFontGlyfCompHaveXyScale 0x0040u
#define RoboFontGlyfCompHave2X2 0x0080u
#define RoboFontGlyfCompHaveInstructions 0x0100u
#define RoboFontGlyfCompUseMyMetrics 0x0200u
#define RoboFontGlyfCompOverlap 0x0400u

typedef enum FontGlyfKind {
    RoboFontGlyfEmpty = 0,
    RoboFontGlyfSimple = 1,
    RoboFontGlyfComposite = 2
} FontGlyfKind;

typedef struct FontGlyfHeader {
    int16_t numberOfContours;
    int16_t xMin;
    int16_t yMin;
    int16_t xMax;
    int16_t yMax;
} FontGlyfHeader;

typedef struct FontGlyfComponent {
    uint16_t flags;
    uint16_t glyphIndex;
    int32_t arg1; /* FWord or point index depending on flags */
    int32_t arg2;
    /* Transform: Fixed 16.16 stored as int16 × 2^-14 (F2DOT14) × 4 or scales */
    int16_t xscale; /* F2DOT14; 1.0 = 0x4000 when have scale */
    int16_t scale01;
    int16_t scale10;
    int16_t yscale;
    uint8_t hasTransform;
} FontGlyfComponent;

typedef struct FontGlyfGlyph {
    FontGlyfKind kind;
    FontGlyfHeader header;
    uint32_t offset; /* in glyf table */
    uint32_t length;
    /* simple */
    uint16_t contourCount;
    uint16_t pointCount;
    uint16_t instructionLength;
    uint16_t *endPts;     /* owned; contourCount */
    uint8_t *flags;        /* owned; pointCount expanded */
    int16_t *xCoords;     /* owned; absolute coords */
    int16_t *yCoords;
    FontSpan instructions; /* view into glyf */
    /* composite */
    uint16_t componentCount;
    FontGlyfComponent *components; /* owned */
    uint16_t compositeInstructionLength;
    FontSpan compositeInstructions;
} FontGlyfGlyph;

void FontGlyfGlyphClear(FontGlyfGlyph *g);

/* ---- bundled TrueType parse ---- */

typedef enum FontTtFlags {
    RoboFontTtFlagsNone = 0,
    /** Require glyf+loca (TrueType outlines). CFF-only fonts → NOT_SUPPORTED. */
    RoboFontTtFlagsRequireGlyf = 1,
    /** Verify head directory checksum with checksumAdjustment zeroed. */
    RoboFontTtFlagsVerifyHeadChecksum = 2
} FontTtFlags;

typedef struct FontTt FontTt;

/**
 * Parse core TrueType tables from an already-parsed SFNT.
 * Retains @p sfnt. Cross-validates hmtx/loca/glyf relationships.
 */
Result FontTtParse(FontSfnt *sfnt,
                                 uint32_t flags,
                                 FontTt **out);

void FontTtRetain(FontTt *tt);
void FontTtRelease(FontTt *tt);

const FontSfnt *FontTtSfnt(const FontTt *tt);
const FontHead *FontTtHead(const FontTt *tt);
const FontHhea *FontTtHhea(const FontTt *tt);
const FontMaxp *FontTtMaxp(const FontTt *tt);
const FontPost *FontTtPost(const FontTt *tt);
const FontOs2 *FontTtOs2(const FontTt *tt); /* NULL if absent */
const FontNameTable *FontTtName(const FontTt *tt); /* NULL if absent */

uint16_t FontTtNumGlyphs(const FontTt *tt);

Result FontTtAdvanceWidth(const FontTt *tt,
                                         GlyphId glyphId,
                                         uint16_t *outAw);
Result FontTtLeftSideBearing(const FontTt *tt,
                                             GlyphId glyphId,
                                             int16_t *outLsb);

Result FontTtGlyphOffset(const FontTt *tt,
                                        GlyphId glyphId,
                                        uint32_t *outOffset);
Result FontTtGlyphLength(const FontTt *tt,
                                        GlyphId glyphId,
                                        uint32_t *outLength);

/** Parse glyph structure (simple or composite). Clears @p out first. */
Result FontTtParseGlyph(const FontTt *tt,
                                       GlyphId glyphId,
                                       FontGlyfGlyph *out);

/** Parse one glyph from a glyf span (for unit tests / tooling). */
Result FontTtParseGlyphAt(FontSpan glyf,
                                          uint32_t offset,
                                          uint32_t length,
                                          uint16_t numGlyphs,
                                          FontGlyfGlyph *out);

/**
 * Walk composite components with recursion depth limit (validates only).
 * RoboParseError on excessive depth / bad refs.
 */
Result FontTtValidateCompositeClosure(const FontTt *tt,
                                                      GlyphId glyphId);

/** Fill RoboFont metrics from head + hhea (+ optional OS/2 typo metrics). */
Result FontTtMetrics(const FontTt *tt, FontMetrics *out);

/** Derive styleDesc from OS/2 / head.macStyle when available. */
Result FontTtStyleDesc(const FontTt *tt,
                                      FontStyleDesc *out);

/** Fuzz-ready: SFNT + TT parse of borrowed bytes, then release. */
Result FontTtFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_TT_H */
