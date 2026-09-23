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
/* Declared in `RoboFontOt.h`; repeated here (legal in C11) so this header need not pull in the OpenType
 * layout vocabulary to name the two tables a face caches. */
typedef struct FontOtLayout FontOtLayout;
typedef struct FontGdef FontGdef;

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

/**
 * Lazy OpenType layout parse — GSUB and GDEF, cached on the face (Goal 5.4 door, Phase 12.10).
 *
 * Idempotent and immutable once built, like every other lazy parse here: the *parsed* layout tables are
 * shared by every shaping call, which is what keeps a text run from re-parsing a font's GSUB table, and
 * what makes a lookup application read-only. A face with no GSUB (or no GDEF) parses to NULL for that
 * table rather than failing: a font without substitution is a font, not an error.
 *
 * @return RoboOk; RoboBusy when another thread is building it; RoboNotFound when the container carries no
 *         GSUB/GPOS tables at all (a `glyf`-only face with no layout); the parse error otherwise.
 */
Result FontFaceParseOtLayout(FontFace *face);

/** The cached GSUB layout, or NULL when the face has none or it has not been parsed. */
const FontOtLayout *FontFaceGsub(const FontFace *face);

/**
 * The face's GPOS table, parsed once and cached, or NULL when the face carries none (Goal 5.4).
 *
 * The positioning half of @ref FontFaceGsub, and the same parse: one `FontOtParse` reads the whole layout
 * group (GSUB, GPOS, GDEF) under `RoboFontOtFlagsParseLayout`, so asking for either table costs the same
 * walk and a face that carries only GPOS is as usable as one that carries only GSUB.
 */
const FontOtLayout *FontFaceGpos(const FontFace *face);

/** The cached GDEF, or NULL when the face has none or it has not been parsed. */
const FontGdef *FontFaceGdef(const FontFace *face);

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
