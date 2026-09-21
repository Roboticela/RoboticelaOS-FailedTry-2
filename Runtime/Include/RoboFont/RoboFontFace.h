/*
 * RoboFont face — underlying font resource identity (Goals 5.1–5.14).
 *
 * Optional TT / cmap / coverage / metrics / outlines / metadata / classification
 * via parse_* APIs. Ownership: face retains source and optional backends.
 */

#ifndef ROBO_FONT_FACE_H
#define ROBO_FONT_FACE_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontClassification.h"
#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontCoverage.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboFont/RoboFontName.h"
#include "RoboFont/RoboFontOutline.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FontFormat {
    RoboFontFormatUnknown = 0,
    RoboFontFormatSfnt = 1,
    RoboFontFormatWoff = 2,
    RoboFontFormatWoff2 = 3,
    RoboFontFormatUnsupported = 4
} FontFormat;

typedef struct FontFace FontFace;
typedef struct FontTt FontTt;
typedef struct FontSfnt FontSfnt;
typedef struct FontEmojiInfo FontEmojiInfo;
typedef struct FontVariationModel FontVariationModel;
typedef struct FontColorInfo FontColorInfo;
typedef struct FontOutlineRequest FontOutlineRequest;

Result FontFaceCreate(FontSource *source,
                                    uint32_t faceIndex,
                                    FontFace **out);

void FontFaceRetain(FontFace *face);
void FontFaceRelease(FontFace *face);

FontFaceId GetFontFaceId(const FontFace *face);
FontFormat FontFaceFormat(const FontFace *face);
uint32_t FontFaceIndex(const FontFace *face);

const FontSource *FontFaceSource(const FontFace *face);

Result FontFaceStyle(const FontFace *face,
                                   FontStyleDesc *out);

Result FontFaceGlyphCount(const FontFace *face,
                                         uint32_t *outCount);

Result FontFaceParseTruetype(FontFace *face, uint32_t ttFlags);
const FontTt *FontFaceTruetype(const FontFace *face);

Result FontFaceParseCmap(FontFace *face);
const FontCmapMap *FontFaceCmap(const FontFace *face);
Result FontFaceLookupGlyph(const FontFace *face,
                                          uint32_t codePoint,
                                          FontCmapLookupResult *out);

/** Lazy cmap-backed coverage (Goal 5.14). Idempotent; immutable after build. */
Result FontFaceParseCoverage(FontFace *face);
const FontCoverage *FontFaceCoverage(const FontFace *face);

/** Ensure SFNT container is parsed (shared by metrics/emoji/…). */
Result FontFaceParseSfnt(FontFace *face);
const FontSfnt *FontFaceSfnt(const FontFace *face);

/** Lazy emoji capability analysis (Goal 5.17). Idempotent; immutable after build. */
Result FontFaceParseEmoji(FontFace *face);
const FontEmojiInfo *FontFaceEmoji(const FontFace *face);

/** Lazy variation model (Goal 5.18). Idempotent; RoboNotFound if no fvar. */
Result FontFaceParseVariation(FontFace *face);
const FontVariationModel *FontFaceVariation(const FontFace *face);

/** Lazy color-font info (Goal 5.19). Idempotent; RoboNotFound if no color tables. */
Result FontFaceParseColor(FontFace *face);
const FontColorInfo *FontFaceColor(const FontFace *face);

Result FontFaceParseMetrics(FontFace *face);
const FontMetricsInfo *FontFaceMetricsInfo(const FontFace *face);
Result FontFaceLayoutMetrics(const FontFace *face,
                                            FontMetrics *out);
Result FontFaceGlyphMetrics(const FontFace *face,
                                           GlyphId glyphId,
                                           FontGlyphMetrics *out);

Result FontFaceParseOutlines(FontFace *face);
Result FontFaceGlyphOutline(FontFace *face,
                                           GlyphId glyphId,
                                           FontOutline **out);

/**
 * Variation-aware outline request (Goal 5.18 foundation).
 * Accepts coordinates but returns the static default outline; interpolation
 * is not executed yet (FontOutlineVariationApplied → 0).
 */
Result FontFaceGlyphOutlineAt(FontFace *face,
                                              const FontOutlineRequest *req,
                                              FontOutline **out);

Result FontFaceParseMetadata(FontFace *face);
const FontNameInfo *FontFaceNameInfo(const FontFace *face);
const FontMetadata *FontFaceMetadata(const FontFace *face);

Result FontFaceParseClassification(FontFace *face);
const FontClassification *FontFaceClassification(
    const FontFace *face);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_FACE_H */
