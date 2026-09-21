/*
 * RoboFont CFF / CFF2 (Goal 5.5).
 *
 * Parses and validates Compact Font Format tables inside SFNT/OTTO fonts.
 * Does NOT produce final glyph outlines (Goal 5.8), evaluate variable-font
 * instances (5.18), map Unicode via cmap (5.6), or rasterize/hint.
 *
 * Outline technology (TrueType / CFF / CFF2) remains classified by SFNT tags;
 * this module parses the CFF/CFF2 table bodies.
 */

#ifndef ROBO_FONT_CFF_H
#define ROBO_FONT_CFF_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSfnt.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Resource limits ---- */

#define RoboFontCffMaxIndexCount 65536u
#define RoboFontCffMaxIndexObject (1u << 20) /* 1 MiB per object */
#define RoboFontCffMaxDictOps 512u
#define RoboFontCffMaxDictOperands 48u
#define RoboFontCffMaxCharstringLen (1u << 16)
#define RoboFontCffMaxOperandStack 48u
#define RoboFontCffMaxSubrDepth 16u
#define RoboFontCffMaxSubrCalls 1024u
#define RoboFontCffMaxFdarray 256u
#define RoboFontCffMaxVarRegions 4096u
#define RoboFontCffMaxCsOps 8192u
#define RoboFontCffMaxGlyphs 65535u
#define RoboFontCffMaxStringIndex 65000u

/* ---- Variable-size offset ---- */

Result FontCffReadOffset(FontReader *r,
                                        uint8_t offSize,
                                        uint32_t *out);

/* ---- INDEX ---- */

typedef struct FontCffIndex {
    uint32_t count;
    uint8_t offSize;
    /** Absolute offsets into @p table for each object (count entries). */
    uint32_t *dataOffsets; /* length count; relative to table base */
    uint32_t *dataLengths; /* length count */
    FontSpan table; /* owning table view (borrowed) */
    size_t dataBase;       /* absolute offset of first data byte */
} FontCffIndex;

void FontCffIndexClear(FontCffIndex *idx);

/**
 * Parse a CFF1 INDEX (Card16 count) starting at @p offset within @p table.
 * On success, @p outEnd is set to the first byte past the INDEX.
 */
Result FontCffIndexParse(FontSpan table,
                                        size_t offset,
                                        FontCffIndex *out,
                                        size_t *outEnd);

/** CFF2 INDEX uses Card32 count. */
Result FontCff2IndexParse(FontSpan table,
                                         size_t offset,
                                         FontCffIndex *out,
                                         size_t *outEnd);

uint32_t FontCffIndexCount(const FontCffIndex *idx);

Result FontCffIndexGet(const FontCffIndex *idx,
                                      uint32_t i,
                                      FontSpan *out);

Result FontCffIndexGetOffset(const FontCffIndex *idx,
                                             uint32_t i,
                                             uint32_t *outAbs);

Result FontCffIndexGetLength(const FontCffIndex *idx,
                                             uint32_t i,
                                             uint32_t *outLen);

/** Standard subroutine bias for @p nSubrs. */
uint32_t FontCffSubrBias(uint32_t nSubrs);

/* ---- DICT ---- */

typedef enum FontCffDictOp {
    RoboCffOpVersion = 0,
    RoboCffOpNotice = 1,
    RoboCffOpFullname = 2,
    RoboCffOpFamilyname = 3,
    RoboCffOpWeight = 4,
    RoboCffOpFontbbox = 5,
    RoboCffOpBluevalues = 6,
    RoboCffOpOtherblues = 7,
    RoboCffOpFamilyblues = 8,
    RoboCffOpFamilyotherblues = 9,
    RoboCffOpStdhw = 10,
    RoboCffOpStdvw = 11,
    RoboCffOpEscape = 12,
    RoboCffOpUniqueid = 13,
    RoboCffOpXuid = 14,
    RoboCffOpCharset = 15,
    RoboCffOpEncoding = 16,
    RoboCffOpCharstrings = 17,
    RoboCffOpPrivate = 18, /* size, offset */
    RoboCffOpSubrs = 19,
    RoboCffOpDefaultwidthx = 20,
    RoboCffOpNominalwidthx = 21,
    /* escape (12) operators — encoded as 1200 + second byte */
    RoboCffOpCopyright = 1200,
    RoboCffOpIsfixedpitch = 1201,
    RoboCffOpItalicangle = 1202,
    RoboCffOpUnderlineposition = 1203,
    RoboCffOpUnderlinethickness = 1204,
    RoboCffOpPainttype = 1205,
    RoboCffOpCharstringtype = 1206,
    RoboCffOpFontmatrix = 1207,
    RoboCffOpStrokewidth = 1208,
    RoboCffOpBluescale = 1209,
    RoboCffOpBlueshift = 1210,
    RoboCffOpBluefuzz = 1211,
    RoboCffOpStemsnaph = 1212,
    RoboCffOpStemsnapv = 1213,
    RoboCffOpForcebold = 1214,
    RoboCffOpLanguagegroup = 1217,
    RoboCffOpExpansionfactor = 1218,
    RoboCffOpInitialrandomseed = 1219,
    RoboCffOpSyntheticbase = 1220,
    RoboCffOpPostscript = 1221,
    RoboCffOpBasefontname = 1222,
    RoboCffOpBasefontblend = 1223,
    RoboCffOpRos = 1230,
    RoboCffOpCidfontversion = 1231,
    RoboCffOpCidfontrevision = 1232,
    RoboCffOpCidfonttype = 1233,
    RoboCffOpCidcount = 1234,
    RoboCffOpUidbase = 1235,
    RoboCffOpFdarray = 1236,
    RoboCffOpFdselect = 1237,
    RoboCffOpFontname = 1238,
    /* CFF2 escape ops (12,xx). In CFF2 Top DICT, single-byte 15/16 are
     * vsindex/blend (same codes as CFF1 charset/encoding — context differs). */
    RoboCffOpVstore = 1224,
    RoboCffOpMaxstack = 1225
} FontCffDictOp;

typedef struct FontCffNumber {
    int32_t i;     /* integer (or truncated real) */
    double real;   /* real when isReal */
    int isReal;
} FontCffNumber;

typedef struct FontCffDictEntry {
    uint16_t op; /* FontCffDictOp or raw */
    uint8_t argc;
    FontCffNumber args[RoboFontCffMaxDictOperands];
} FontCffDictEntry;

typedef struct FontCffDict {
    uint32_t count;
    FontCffDictEntry *entries;
} FontCffDict;

void FontCffDictClear(FontCffDict *d);

Result FontCffDictParse(FontSpan dictBytes,
                                       FontCffDict *out);

/** Find first entry with operator @p op; RoboNotFound if absent. */
Result FontCffDictFind(const FontCffDict *d,
                                      uint16_t op,
                                      const FontCffDictEntry **out);

/* ---- Structured Top / Private DICT summaries ---- */

typedef struct FontCffTopDict {
    FontCffDict raw;
    int32_t charset;      /* offset or predefined 0/1/2; -1 unset */
    int32_t encoding;     /* offset or predefined 0/1; -1 unset */
    int32_t charstrings;  /* offset; -1 unset */
    int32_t privateSize;
    int32_t privateOffset;
    int32_t fdarray;
    int32_t fdselect;
    int32_t vstore; /* CFF2 */
    int32_t rosRegistry;
    int32_t rosOrdering;
    int32_t rosSupplement;
    int isCid;
    int32_t cidCount;
    double fontMatrix[6];
    int hasFontMatrix;
    int32_t fontBbox[4];
    int hasFontBbox;
    int32_t charstringType; /* default 2 */
} FontCffTopDict;

typedef struct FontCffPrivateDict {
    FontCffDict raw;
    int32_t subrs; /* offset relative to Private DICT start; -1 unset */
    double defaultWidthX;
    double nominalWidthX;
    int hasDefaultWidth;
    int hasNominalWidth;
} FontCffPrivateDict;

void FontCffTopDictClear(FontCffTopDict *t);
void FontCffPrivateDictClear(FontCffPrivateDict *p);

Result FontCffTopDictInterpret(const FontCffDict *raw,
                                               FontCffTopDict *out);

Result FontCffPrivateDictInterpret(const FontCffDict *raw,
                                                   FontCffPrivateDict *out);

/* ---- Charset / Encoding / FDSelect ---- */

typedef struct FontCffCharset {
    uint8_t format;
    uint32_t glyphCount; /* including .notdef */
    uint16_t *sids;       /* glyphCount SIDs; sids[0]=0 for .notdef */
} FontCffCharset;

void FontCffCharsetClear(FontCffCharset *c);

Result FontCffCharsetParse(FontSpan table,
                                          size_t offset,
                                          uint32_t nGlyphs,
                                          int isCid,
                                          FontCffCharset *out);

Result FontCffCharsetSid(const FontCffCharset *c,
                                        uint32_t gid,
                                        uint16_t *outSid);

typedef struct FontCffEncoding {
    uint8_t format; /* 0/1 or 0x80|format with supplement */
    uint32_t nCodes;
    uint8_t *codes; /* code for glyphs 1..n (format 0) or expanded */
    uint32_t nSupplements;
    uint8_t *suppCodes;
    uint16_t *suppSids;
    int isPredefined; /* 0 StandardEncoding, 1 ExpertEncoding */
} FontCffEncoding;

void FontCffEncodingClear(FontCffEncoding *e);

Result FontCffEncodingParse(FontSpan table,
                                           size_t offset,
                                           uint32_t nGlyphs,
                                           FontCffEncoding *out);

typedef struct FontCffFdselect {
    uint8_t format;
    uint32_t nGlyphs;
    uint8_t *fds; /* per-glyph FD index when format 0, or expanded from ranges */
    uint32_t nRanges;
} FontCffFdselect;

void FontCffFdselectClear(FontCffFdselect *f);

Result FontCffFdselectParse(FontSpan table,
                                           size_t offset,
                                           uint32_t nGlyphs,
                                           uint32_t fdCount,
                                           FontCffFdselect *out);

Result FontCffFdselectGet(const FontCffFdselect *f,
                                         uint32_t gid,
                                         uint8_t *outFd);

/* ---- Type 2 CharString IR ---- */

typedef enum FontCffCsOp {
    RoboCffCsHstem = 1,
    RoboCffCsVstem = 3,
    RoboCffCsVmoveto = 4,
    RoboCffCsRlineto = 5,
    RoboCffCsHlineto = 6,
    RoboCffCsVlineto = 7,
    RoboCffCsRrcurveto = 8,
    RoboCffCsCallsubr = 10,
    RoboCffCsReturn = 11,
    RoboCffCsEscape = 12,
    RoboCffCsEndchar = 14,
    RoboCffCsHstemhm = 18,
    RoboCffCsHintmask = 19,
    RoboCffCsCntrmask = 20,
    RoboCffCsRmoveto = 21,
    RoboCffCsHmoveto = 22,
    RoboCffCsVstemhm = 23,
    RoboCffCsRcurveline = 24,
    RoboCffCsRlinecurve = 25,
    RoboCffCsVvcurveto = 26,
    RoboCffCsHhcurveto = 27,
    RoboCffCsCallgsubr = 29,
    RoboCffCsVhcurveto = 30,
    RoboCffCsHvcurveto = 31,
    /* escape */
    RoboCffCsAnd = 1203,
    RoboCffCsOr = 1204,
    RoboCffCsNot = 1205,
    RoboCffCsAbs = 1209,
    RoboCffCsAdd = 1210,
    RoboCffCsSub = 1211,
    RoboCffCsDiv = 1212,
    RoboCffCsNeg = 1214,
    RoboCffCsEq = 1215,
    RoboCffCsDrop = 1218,
    RoboCffCsPut = 1220,
    RoboCffCsGet = 1221,
    RoboCffCsIfelse = 1222,
    RoboCffCsRandom = 1223,
    RoboCffCsMul = 1224,
    RoboCffCsSqrt = 1226,
    RoboCffCsDup = 1227,
    RoboCffCsExch = 1228,
    RoboCffCsIndex = 1229,
    RoboCffCsRoll = 1230,
    RoboCffCsHflex = 1234,
    RoboCffCsFlex = 1235,
    RoboCffCsHflex1 = 1236,
    RoboCffCsFlex1 = 1237
} FontCffCsOp;

typedef struct FontCffCsOpRec {
    uint16_t op;
    uint8_t argc;
    int32_t args[RoboFontCffMaxOperandStack];
    /** For hintmask/cntrmask: mask byte count and bytes follow in mask[]. */
    uint8_t maskBytes;
    uint8_t mask[32];
} FontCffCsOpRec;

typedef struct FontCffCharstring {
    uint32_t opCount;
    FontCffCsOpRec *ops;
    int32_t width; /* optional width; INT32_MIN if absent */
    int hasWidth;
    uint32_t stemHintCount;
} FontCffCharstring;

void FontCffCharstringClear(FontCffCharstring *cs);

typedef struct FontCffSubrCtx {
    const FontCffIndex *globalSubrs;
    const FontCffIndex *localSubrs;
    uint32_t globalBias;
    uint32_t localBias;
} FontCffSubrCtx;

/**
 * Validate and decode a Type 2 CharString into an IR.
 * Executes arithmetic/stack ops; records path/hint ops; resolves subrs.
 */
Result FontCffCharstringParse(FontSpan csBytes,
                                             const FontCffSubrCtx *subrs,
                                             double defaultWidth,
                                             double nominalWidth,
                                             int hasNominalWidth,
                                             FontCffCharstring *out);

/** Like parse, with flags (e.g. CFF2 may omit endchar). */
#define RoboFontCffCsFlagsNone 0u
#define RoboFontCffCsFlagsAllowNoEndchar (1u << 0)

Result FontCffCharstringParseEx(FontSpan csBytes,
                                                const FontCffSubrCtx *subrs,
                                                double defaultWidth,
                                                double nominalWidth,
                                                int hasNominalWidth,
                                                uint32_t flags,
                                                FontCffCharstring *out);


/* ---- CFF2 Item Variation Store (structural) ---- */

typedef struct FontCffAxisCoord {
    int16_t startCoord;
    int16_t peakCoord;
    int16_t endCoord;
} FontCffAxisCoord;

typedef struct FontCffVarRegion {
    uint16_t axisCount;
    FontCffAxisCoord *axes;
} FontCffVarRegion;

typedef struct FontCffVarData {
    uint16_t itemCount;
    uint16_t shortDeltaCount;
    uint16_t regionIndexCount;
    uint16_t *regionIndices;
    /* delta sets omitted as raw span for later evaluation */
    FontSpan deltaSets;
} FontCffVarData;

typedef struct FontCffVstore {
    uint16_t format;
    uint16_t axisCount;
    uint16_t regionCount;
    FontCffVarRegion *regions;
    uint16_t dataCount;
    FontCffVarData *data;
} FontCffVstore;

void FontCffVstoreClear(FontCffVstore *v);

Result FontCffVstoreParse(FontSpan table,
                                         size_t offset,
                                         FontCffVstore *out);

/* ---- Font DICT (CID / CFF2) ---- */

typedef struct FontCffFontDict {
    FontCffDict raw;
    int32_t privateSize;
    int32_t privateOffset;
    int32_t fontNameSid;
} FontCffFontDict;

void FontCffFontDictClear(FontCffFontDict *f);

Result FontCffFontDictInterpret(const FontCffDict *raw,
                                                FontCffFontDict *out);

/* ---- Parsed CFF / CFF2 font ---- */

typedef struct FontCffFont {
    int refs;
    FontSfnt *sfnt;
    FontOutlineTech tech; /* CFF or CFF2 */

    uint8_t major;
    uint8_t minor;
    uint8_t hdrSize;
    uint8_t offSize;       /* CFF1 */
    uint16_t topDictLength; /* CFF2 */

    FontCffIndex nameIndex;     /* CFF1 only */
    FontCffIndex topDictIndex; /* CFF1: INDEX; CFF2: single blob via length */
    FontCffIndex stringIndex;   /* CFF1 only */
    FontCffIndex globalSubrs;
    FontCffIndex charstrings;
    FontCffIndex fdarrayIndex;

    FontCffTopDict top;
    FontCffPrivateDict priv; /* first/sole Private for non-CID */
    FontCffIndex localSubrs;

    FontCffCharset charset;
    FontCffEncoding encoding;
    FontCffFdselect fdselect;

    uint32_t fdCount;
    FontCffFontDict *fontDicts;
    FontCffPrivateDict *fdPrivates;
    FontCffIndex *fdLocalSubrs;

    FontCffVstore vstore;
    int hasVstore;

    uint32_t glyphCount;
} FontCffFont;

#define RoboFontCffFlagsNone 0u
#define RoboFontCffFlagsRequireCharstrings (1u << 0)
#define RoboFontCffFlagsParseCharset (1u << 1)
#define RoboFontCffFlagsParseEncoding (1u << 2)
#define RoboFontCffFlagsParsePrivate (1u << 3)
#define RoboFontCffFlagsParseCid (1u << 4)
#define RoboFontCffFlagsParseVstore (1u << 5)
#define RoboFontCffFlagsParseAll                                          \
    (RoboFontCffFlagsRequireCharstrings | RoboFontCffFlagsParseCharset | \
     RoboFontCffFlagsParseEncoding | RoboFontCffFlagsParsePrivate |  \
     RoboFontCffFlagsParseCid | RoboFontCffFlagsParseVstore)

/**
 * Parse CFF or CFF2 from @p sfnt based on outline detection.
 * Retains SFNT. Caller releases via FontCffRelease.
 */
Result FontCffParse(FontSfnt *sfnt,
                                  uint32_t flags,
                                  FontCffFont **out);

void FontCffRetain(FontCffFont *font);
void FontCffRelease(FontCffFont *font);

FontOutlineTech FontCffOutlineTech(const FontCffFont *font);
uint32_t FontCffGlyphCount(const FontCffFont *font);

Result FontCffCharstringBytes(const FontCffFont *font,
                                             uint32_t gid,
                                             FontSpan *out);

Result FontCffParseGlyph(const FontCffFont *font,
                                        uint32_t gid,
                                        FontCffCharstring *out);

/** Fuzz / security entry: parse raw SFNT bytes. */
Result FontCffFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_CFF_H */
