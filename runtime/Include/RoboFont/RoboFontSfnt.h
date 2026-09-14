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
typedef uint32_t robo_font_tag_t;

#define ROBO_FONT_TAG(a, b, c, d)                                              \
    ((robo_font_tag_t)((((uint32_t)(uint8_t)(a)) << 24) |                      \
                       (((uint32_t)(uint8_t)(b)) << 16) |                      \
                       (((uint32_t)(uint8_t)(c)) << 8) |                       \
                       ((uint32_t)(uint8_t)(d))))

#define ROBO_FONT_TAG_HEAD ROBO_FONT_TAG('h', 'e', 'a', 'd')
#define ROBO_FONT_TAG_HHEA ROBO_FONT_TAG('h', 'h', 'e', 'a')
#define ROBO_FONT_TAG_HMTX ROBO_FONT_TAG('h', 'm', 't', 'x')
#define ROBO_FONT_TAG_VHEA ROBO_FONT_TAG('v', 'h', 'e', 'a')
#define ROBO_FONT_TAG_VMTX ROBO_FONT_TAG('v', 'm', 't', 'x')
#define ROBO_FONT_TAG_MAXP ROBO_FONT_TAG('m', 'a', 'x', 'p')
#define ROBO_FONT_TAG_NAME ROBO_FONT_TAG('n', 'a', 'm', 'e')
#define ROBO_FONT_TAG_CMAP ROBO_FONT_TAG('c', 'm', 'a', 'p')
#define ROBO_FONT_TAG_GLYF ROBO_FONT_TAG('g', 'l', 'y', 'f')
#define ROBO_FONT_TAG_LOCA ROBO_FONT_TAG('l', 'o', 'c', 'a')
#define ROBO_FONT_TAG_OS2 ROBO_FONT_TAG('O', 'S', '/', '2')
#define ROBO_FONT_TAG_POST ROBO_FONT_TAG('p', 'o', 's', 't')
#define ROBO_FONT_TAG_CFF ROBO_FONT_TAG('C', 'F', 'F', ' ')
#define ROBO_FONT_TAG_CFF2 ROBO_FONT_TAG('C', 'F', 'F', '2')
#define ROBO_FONT_TAG_GSUB ROBO_FONT_TAG('G', 'S', 'U', 'B')
#define ROBO_FONT_TAG_GPOS ROBO_FONT_TAG('G', 'P', 'O', 'S')
#define ROBO_FONT_TAG_GDEF ROBO_FONT_TAG('G', 'D', 'E', 'F')
#define ROBO_FONT_TAG_BASE ROBO_FONT_TAG('B', 'A', 'S', 'E')
#define ROBO_FONT_TAG_JSTF ROBO_FONT_TAG('J', 'S', 'T', 'F')
#define ROBO_FONT_TAG_MATH ROBO_FONT_TAG('M', 'A', 'T', 'H')
#define ROBO_FONT_TAG_KERN ROBO_FONT_TAG('k', 'e', 'r', 'n')
#define ROBO_FONT_TAG_COLR ROBO_FONT_TAG('C', 'O', 'L', 'R')
#define ROBO_FONT_TAG_CPAL ROBO_FONT_TAG('C', 'P', 'A', 'L')
#define ROBO_FONT_TAG_STAT ROBO_FONT_TAG('S', 'T', 'A', 'T')
#define ROBO_FONT_TAG_VORG ROBO_FONT_TAG('V', 'O', 'R', 'G')
#define ROBO_FONT_TAG_HVAR ROBO_FONT_TAG('H', 'V', 'A', 'R')
#define ROBO_FONT_TAG_FVAR ROBO_FONT_TAG('f', 'v', 'a', 'r')
#define ROBO_FONT_TAG_AVAR ROBO_FONT_TAG('a', 'v', 'a', 'r')
#define ROBO_FONT_TAG_GVAR ROBO_FONT_TAG('g', 'v', 'a', 'r')
#define ROBO_FONT_TAG_MVAR ROBO_FONT_TAG('M', 'V', 'A', 'R')
#define ROBO_FONT_TAG_SBIX ROBO_FONT_TAG('s', 'b', 'i', 'x')
#define ROBO_FONT_TAG_CBDT ROBO_FONT_TAG('C', 'B', 'D', 'T')
#define ROBO_FONT_TAG_CBLC ROBO_FONT_TAG('C', 'B', 'L', 'C')
#define ROBO_FONT_TAG_SVG ROBO_FONT_TAG('S', 'V', 'G', ' ')

/** Recognized SFNT scaler types / versions. */
#define ROBO_FONT_SFNT_VERSION_TRUE ((uint32_t)0x00010000u)
#define ROBO_FONT_SFNT_VERSION_TRUE_TAG ROBO_FONT_TAG('t', 'r', 'u', 'e')
#define ROBO_FONT_SFNT_VERSION_TYP1 ROBO_FONT_TAG('t', 'y', 'p', '1')
#define ROBO_FONT_SFNT_VERSION_OTTO ROBO_FONT_TAG('O', 'T', 'T', 'O')
#define ROBO_FONT_SFNT_VERSION_TTCF ROBO_FONT_TAG('t', 't', 'c', 'f')

/** Defensive limits (reject pathological inputs). */
#define ROBO_FONT_SFNT_MAX_TABLES 4095u
#define ROBO_FONT_SFNT_MAX_SOURCE_SIZE (64u * 1024u * 1024u)

typedef struct robo_font_sfnt_header {
    uint32_t sfnt_version;
    uint16_t num_tables;
    uint16_t search_range;
    uint16_t entry_selector;
    uint16_t range_shift;
} robo_font_sfnt_header_t;

typedef struct robo_font_sfnt_table_record {
    robo_font_tag_t tag;
    uint32_t checksum;
    uint32_t offset; /* absolute offset in the source blob */
    uint32_t length;
} robo_font_sfnt_table_record_t;

typedef struct robo_font_sfnt robo_font_sfnt_t;

typedef enum robo_font_sfnt_flags {
    ROBO_FONT_SFNT_FLAGS_NONE = 0,
    /** Compare each table's directory checksum to the computed padded sum. */
    ROBO_FONT_SFNT_FLAGS_VERIFY_CHECKSUMS = 1
} robo_font_sfnt_flags_t;

/**
 * Parse an SFNT (or TTC member) from @p source.
 * @p face_index selects the face in a TrueType Collection (0 for single fonts).
 * Retains @p source. On failure *out is NULL.
 */
robo_result_t robo_font_sfnt_parse(robo_font_source_t *source,
                                   uint32_t face_index,
                                   uint32_t flags,
                                   robo_font_sfnt_t **out);

void robo_font_sfnt_retain(robo_font_sfnt_t *sfnt);
void robo_font_sfnt_release(robo_font_sfnt_t *sfnt);

const robo_font_source_t *robo_font_sfnt_source(const robo_font_sfnt_t *sfnt);
const robo_font_sfnt_header_t *robo_font_sfnt_header(const robo_font_sfnt_t *sfnt);

uint32_t robo_font_sfnt_table_count(const robo_font_sfnt_t *sfnt);

/** 1 if a table with @p tag exists. */
int robo_font_sfnt_has_table(const robo_font_sfnt_t *sfnt, robo_font_tag_t tag);

/**
 * Find table metadata. ROBO_NOT_FOUND if absent.
 * @p out may be NULL to only probe existence (prefer has_table).
 */
robo_result_t robo_font_sfnt_find_table(const robo_font_sfnt_t *sfnt,
                                        robo_font_tag_t tag,
                                        robo_font_sfnt_table_record_t *out);

/**
 * Bounded view of table bytes inside the source. View is valid while @p sfnt
 * (and thus its source) remains alive.
 */
robo_result_t robo_font_sfnt_table_bytes(const robo_font_sfnt_t *sfnt,
                                         robo_font_tag_t tag,
                                         robo_font_span_t *out);

/** Enumerate table @p index in [0, table_count). */
robo_result_t robo_font_sfnt_table_at(const robo_font_sfnt_t *sfnt,
                                      uint32_t index,
                                      robo_font_sfnt_table_record_t *out);

/**
 * Compute SFNT table checksum: sum of big-endian uint32 words over data,
 * with trailing bytes padded to 0 to a multiple of 4.
 */
uint32_t robo_font_sfnt_checksum(const uint8_t *data, size_t length);

/**
 * Verify directory checksum for one table (or all if tag==0 with VERIFY).
 * Does NOT apply head.checksumAdjustment (later with head table parsing).
 */
robo_result_t robo_font_sfnt_verify_table_checksum(const robo_font_sfnt_t *sfnt,
                                                   robo_font_tag_t tag);

/** 1 if searchRange/entrySelector/rangeShift match OpenType formulas. */
int robo_font_sfnt_search_fields_consistent(const robo_font_sfnt_header_t *h);

/**
 * Format tag into 4 chars + NUL (buf_cap >= 5). Non-printable → '.'.
 */
robo_result_t robo_font_tag_format(robo_font_tag_t tag, char *buf, size_t buf_cap);

/**
 * Fuzz-ready entry: parse borrowed @p data[0..size), then release.
 * Returns ROBO_OK only if the container parsed cleanly.
 * Never crashes on malformed input.
 */
robo_result_t robo_font_sfnt_fuzz_parse(const uint8_t *data, size_t size);

/** True if @p version is an accepted single-font SFNT scaler type. */
int robo_font_sfnt_version_is_supported(uint32_t version);

/**
 * Probe how many faces a blob contains without fully parsing tables.
 * Single SFNT → 1. TTC/OTC (`ttcf`) → numFonts (capped).
 * Unsupported / too short → ROBO_PARSE_ERROR or ROBO_NOT_SUPPORTED.
 */
robo_result_t robo_font_sfnt_probe_face_count(const uint8_t *data,
                                              size_t size,
                                              uint32_t *out_count);

robo_result_t robo_font_sfnt_probe_face_count_source(const robo_font_source_t *source,
                                                     uint32_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_SFNT_H */
