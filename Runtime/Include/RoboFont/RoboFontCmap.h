/*
 * RoboFont character-to-glyph mapping (Goal 5.6).
 *
 * Builds an immutable lookup map from the SFNT `cmap` table.
 * Does NOT implement font fallback, shaping, or text segmentation.
 *
 * Structural encoding-record discovery may also use Goal 5.4
 * `FontCmapParse`; this module adds full format bodies + lookup.
 */

#ifndef ROBO_FONT_CMAP_H
#define ROBO_FONT_CMAP_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboFontCmapMaxSegments 8192u
#define RoboFontCmapMaxGroups 65536u
#define RoboFontCmapMaxGlyphArray (1u << 20)
#define RoboFontCmapMaxUvsSelectors 256u
#define RoboFontCmapMaxUvsRanges 8192u
#define RoboFontCmapMaxUvsMappings 65536u

/* ---- Lookup result ---- */

typedef enum FontCmapLookupStatus {
    RoboFontCmapMapped = 0,
    RoboFontCmapUnmapped = 1,
    RoboFontCmapInvalidCodePoint = 2,
    RoboFontCmapInvalidFont = 3
} FontCmapLookupStatus;

typedef struct FontCmapLookupResult {
    FontCmapLookupStatus status;
    GlyphId glyphId; /* valid only when status == MAPPED */
} FontCmapLookupResult;

typedef enum FontCmapUvsStatus {
    RoboFontCmapUvsNone = 0,           /* no format-14 / no matching VS */
    RoboFontCmapUvsDefault = 1,        /* use default cmap for base */
    RoboFontCmapUvsNonDefault = 2,    /* explicit glyph for (base, VS) */
    RoboFontCmapUvsUnsupported = 3,    /* VS present but no UVS for base */
    RoboFontCmapUvsInvalidInput = 4
} FontCmapUvsStatus;

typedef struct FontCmapUvsResult {
    FontCmapUvsStatus status;
    uint32_t baseCodePoint;
    uint32_t variationSelector;
    GlyphId glyphId; /* NON_DEFAULT only */
} FontCmapUvsResult;

/* ---- Immutable map ---- */

typedef struct FontCmapMap FontCmapMap;

/**
 * Build a cmap map from an SFNT font.
 * @p numGlyphs is maxp.numGlyphs (GID validation). 0 is rejected.
 * Retains @p sfnt.
 */
Result FontCmapMapCreate(FontSfnt *sfnt,
                                        uint32_t numGlyphs,
                                        FontCmapMap **out);

/** Build from a raw cmap table span (tests / fuzz). Does not retain SFNT. */
Result FontCmapMapCreateFromSpan(FontSpan cmapTable,
                                                  uint32_t numGlyphs,
                                                  FontCmapMap **out);

void FontCmapMapRetain(FontCmapMap *map);
void FontCmapMapRelease(FontCmapMap *map);

uint32_t FontCmapMapNumGlyphs(const FontCmapMap *map);
uint16_t FontCmapMapSelectedFormat(const FontCmapMap *map);
uint16_t FontCmapMapSelectedPlatform(const FontCmapMap *map);
uint16_t FontCmapMapSelectedEncoding(const FontCmapMap *map);
int FontCmapMapHasUvs(const FontCmapMap *map);

/** True if @p cp is a Unicode scalar value (not surrogate, ≤ U+10FFFF). */
int FontCmapIsUnicodeScalar(uint32_t codePoint);

/**
 * Lookup Unicode code point → glyph ID.
 * Thread-safe for concurrent readers after map creation.
 */
Result FontCmapLookup(const FontCmapMap *map,
                                    uint32_t codePoint,
                                    FontCmapLookupResult *out);

/**
 * Convenience: returns glyph ID when mapped; otherwise 0 and sets @p outStatus
 * if non-NULL. Prefer FontCmapLookup when status matters.
 */
GlyphId FontCmapLookupGlyph(const FontCmapMap *map,
                                            uint32_t codePoint,
                                            FontCmapLookupStatus *outStatus);

/** Format-14 UVS foundation. */
Result FontCmapLookupUvs(const FontCmapMap *map,
                                        uint32_t baseCodePoint,
                                        uint32_t variationSelector,
                                        FontCmapUvsResult *out);

/**
 * Visit every Unicode scalar the primary cmap maps (status == MAPPED),
 * including explicit mappings to glyph 0 (`.notdef`).
 * Visits are in ascending code-point order.
 * @p fn may return non-OK to abort early (that result is propagated).
 *
 * Used by Goal 5.14 coverage construction — does not re-parse cmap formats.
 */
typedef Result (*FontCmapMappedFn)(uint32_t codePoint,
                                                  GlyphId glyphId,
                                                  void *user);

Result FontCmapForeachMapped(const FontCmapMap *map,
                                            FontCmapMappedFn fn,
                                            void *user);

/**
 * Read maxp.numGlyphs from SFNT without a full TrueType parse.
 * Supports maxp versions 0.5 and 1.0.
 */
Result FontSfntNumGlyphs(const FontSfnt *sfnt,
                                        uint32_t *outCount);

/** Fuzz entry: parse arbitrary SFNT or raw cmap-ish bytes safely. */
Result FontCmapFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_CMAP_H */
