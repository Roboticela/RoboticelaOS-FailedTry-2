/*
 * RoboFont SFNT container parser (Goal 5.2).
 *
 * Parses SFNT header + table directory only. Does not interpret table bodies
 * (cmap, head, glyf, CFF, GSUB, … — later goals).
 *
 * Ownership:
 *   Font Source must remain alive while RoboSfntFont (and any table views) live.
 *   The SFNT object retains the source.
 *
 * Security: every access is bounds-checked. Overlapping table regions and
 * duplicate tags are rejected. Pathological table counts are capped.
 */

#ifndef ROBO_FONT_SFNT_H
#define ROBO_FONT_SFNT_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontSource.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Four-character tag as big-endian uint32 (e.g. 'head'). */
typedef uint32_t FontTag;

#define RoboFontTag(a, b, c, d)                                              \
    ((FontTag)((((uint32_t)(uint8_t)(a)) << 24) |                      \
                       (((uint32_t)(uint8_t)(b)) << 16) |                      \
                       (((uint32_t)(uint8_t)(c)) << 8) |                       \
                       ((uint32_t)(uint8_t)(d))))

#define RoboFontTagHead RoboFontTag('h', 'e', 'a', 'd')
#define RoboFontTagHhea RoboFontTag('h', 'h', 'e', 'a')
#define RoboFontTagHmtx RoboFontTag('h', 'm', 't', 'x')
#define RoboFontTagVhea RoboFontTag('v', 'h', 'e', 'a')
#define RoboFontTagVmtx RoboFontTag('v', 'm', 't', 'x')
#define RoboFontTagMaxp RoboFontTag('m', 'a', 'x', 'p')
#define RoboFontTagName RoboFontTag('n', 'a', 'm', 'e')
#define RoboFontTagCmap RoboFontTag('c', 'm', 'a', 'p')
#define RoboFontTagGlyf RoboFontTag('g', 'l', 'y', 'f')
#define RoboFontTagLoca RoboFontTag('l', 'o', 'c', 'a')
#define RoboFontTagOs2 RoboFontTag('O', 'S', '/', '2')
#define RoboFontTagPost RoboFontTag('p', 'o', 's', 't')
#define RoboFontTagCff RoboFontTag('C', 'F', 'F', ' ')
#define RoboFontTagCff2 RoboFontTag('C', 'F', 'F', '2')
#define RoboFontTagGsub RoboFontTag('G', 'S', 'U', 'B')
#define RoboFontTagGpos RoboFontTag('G', 'P', 'O', 'S')
#define RoboFontTagGdef RoboFontTag('G', 'D', 'E', 'F')
#define RoboFontTagBase RoboFontTag('B', 'A', 'S', 'E')
#define RoboFontTagJstf RoboFontTag('J', 'S', 'T', 'F')
#define RoboFontTagMath RoboFontTag('M', 'A', 'T', 'H')
#define RoboFontTagKern RoboFontTag('k', 'e', 'r', 'n')
#define RoboFontTagColr RoboFontTag('C', 'O', 'L', 'R')
#define RoboFontTagCpal RoboFontTag('C', 'P', 'A', 'L')
#define RoboFontTagStat RoboFontTag('S', 'T', 'A', 'T')
#define RoboFontTagVorg RoboFontTag('V', 'O', 'R', 'G')
#define RoboFontTagHvar RoboFontTag('H', 'V', 'A', 'R')
#define RoboFontTagFvar RoboFontTag('f', 'v', 'a', 'r')
#define RoboFontTagAvar RoboFontTag('a', 'v', 'a', 'r')
#define RoboFontTagGvar RoboFontTag('g', 'v', 'a', 'r')
#define RoboFontTagMvar RoboFontTag('M', 'V', 'A', 'R')
#define RoboFontTagSbix RoboFontTag('s', 'b', 'i', 'x')
#define RoboFontTagCbdt RoboFontTag('C', 'B', 'D', 'T')
#define RoboFontTagCblc RoboFontTag('C', 'B', 'L', 'C')
#define RoboFontTagSvg RoboFontTag('S', 'V', 'G', ' ')

/** Recognized SFNT scaler types / versions. */
#define RoboFontSfntVersionTrue ((uint32_t)0x00010000u)
#define RoboFontSfntVersionTrueTag RoboFontTag('t', 'r', 'u', 'e')
#define RoboFontSfntVersionTyp1 RoboFontTag('t', 'y', 'p', '1')
#define RoboFontSfntVersionOtto RoboFontTag('O', 'T', 'T', 'O')
#define RoboFontSfntVersionTtcf RoboFontTag('t', 't', 'c', 'f')

/** Defensive limits (reject pathological inputs). */
#define RoboFontSfntMaxTables 4095u
#define RoboFontSfntMaxSourceSize (64u * 1024u * 1024u)

typedef struct FontSfntHeader {
    uint32_t sfntVersion;
    uint16_t numTables;
    uint16_t searchRange;
    uint16_t entrySelector;
    uint16_t rangeShift;
} FontSfntHeader;

typedef struct FontSfntTableRecord {
    FontTag tag;
    uint32_t checksum;
    uint32_t offset; /* absolute offset in the source blob */
    uint32_t length;
} FontSfntTableRecord;

typedef struct FontSfnt FontSfnt;

typedef enum FontSfntFlags {
    RoboFontSfntFlagsNone = 0,
    /** Compare each table's directory checksum to the computed padded sum. */
    RoboFontSfntFlagsVerifyChecksums = 1
} FontSfntFlags;

/**
 * Parse an SFNT (or TTC member) from @p source.
 * @p faceIndex selects the face in a TrueType Collection (0 for single fonts).
 * Retains @p source. On failure *out is NULL.
 */
Result FontSfntParse(FontSource *source,
                                   uint32_t faceIndex,
                                   uint32_t flags,
                                   FontSfnt **out);

void FontSfntRetain(FontSfnt *sfnt);
void FontSfntRelease(FontSfnt *sfnt);

const FontSource *FontSfntSource(const FontSfnt *sfnt);
const FontSfntHeader *GetFontSfntHeader(const FontSfnt *sfnt);

uint32_t FontSfntTableCount(const FontSfnt *sfnt);

/** 1 if a table with @p tag exists. */
int FontSfntHasTable(const FontSfnt *sfnt, FontTag tag);

/**
 * Find table metadata. RoboNotFound if absent.
 * @p out may be NULL to only probe existence (prefer has_table).
 */
Result FontSfntFindTable(const FontSfnt *sfnt,
                                        FontTag tag,
                                        FontSfntTableRecord *out);

/**
 * Bounded view of table bytes inside the source. View is valid while @p sfnt
 * (and thus its source) remains alive.
 */
Result FontSfntTableBytes(const FontSfnt *sfnt,
                                         FontTag tag,
                                         FontSpan *out);

/** Enumerate table @p index in [0, tableCount). */
Result FontSfntTableAt(const FontSfnt *sfnt,
                                      uint32_t index,
                                      FontSfntTableRecord *out);

/**
 * Compute SFNT table checksum: sum of big-endian uint32 words over data,
 * with trailing bytes padded to 0 to a multiple of 4.
 */
uint32_t FontSfntChecksum(const uint8_t *data, size_t length);

/**
 * Verify directory checksum for one table (or all if tag==0 with VERIFY).
 * Does NOT apply head.checksumAdjustment (later with head table parsing).
 */
Result FontSfntVerifyTableChecksum(const FontSfnt *sfnt,
                                                   FontTag tag);

/** 1 if searchRange/entrySelector/rangeShift match OpenType formulas. */
int FontSfntSearchFieldsConsistent(const FontSfntHeader *h);

/**
 * Format tag into 4 chars + NUL (bufCap >= 5). Non-printable → '.'.
 */
Result FontTagFormat(FontTag tag, char *buf, size_t bufCap);

/**
 * Fuzz-ready entry: parse borrowed @p data[0..size), then release.
 * Returns RoboOk only if the container parsed cleanly.
 * Never crashes on malformed input.
 */
Result FontSfntFuzzParse(const uint8_t *data, size_t size);

/** True if @p version is an accepted single-font SFNT scaler type. */
int FontSfntVersionIsSupported(uint32_t version);

/**
 * Probe how many faces a blob contains without fully parsing tables.
 * Single SFNT → 1. TTC/OTC (`ttcf`) → numFonts (capped).
 * Unsupported / too short → RoboParseError or RoboNotSupported.
 */
Result FontSfntProbeFaceCount(const uint8_t *data,
                                              size_t size,
                                              uint32_t *outCount);

Result FontSfntProbeFaceCountSource(const FontSource *source,
                                                     uint32_t *outCount);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_SFNT_H */
