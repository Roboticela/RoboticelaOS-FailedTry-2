#include "RoboFont/RoboFontFace.h"

#include "RoboFont/RoboFontCff.h"
#include "RoboFont/RoboFontClassification.h"
#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontCoverage.h"
#include "RoboFont/RoboFontColor.h"
#include "RoboFont/RoboFontEmoji.h"
#include "RoboFont/RoboFontName.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboFont/RoboFontVariation.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct FontFace {
    uint32_t refs;
    FontFaceId id;
    FontSource *source;
    uint32_t faceIndex;
    FontFormat format;
    FontStyleDesc style;
    FontSfnt *sfnt;
    FontTt *tt;
    FontCmapMap *cmap;
    FontCoverage *coverage;
    int coverageBuilding; /* concurrent lazy-init: RoboBusy while set */
    FontEmojiInfo *emoji;
    int emojiBuilding;
    FontVariationModel *variation;
    int variationBuilding;
    int variationAbsent; /* 1 if parse found no fvar */
    FontColorInfo *color;
    int colorBuilding;
    int colorAbsent; /* 1 if parse found no color tables */
    FontMetricsInfo *metrics;
    FontCffFont *cff;
    FontOutlineCache *outlineCache;
    FontNameInfo *nameInfo;
    FontClassificationInfo *classification;
};

static uint64_t gNextFaceId = 1u;

static FontFormat DetectFormat(const uint8_t *b, size_t n)
{
    if (!b || n < 4u) {
        return RoboFontFormatUnknown;
    }
    if ((b[0] == 0x00u && b[1] == 0x01u && b[2] == 0x00u && b[3] == 0x00u) ||
        (b[0] == 't' && b[1] == 'r' && b[2] == 'u' && b[3] == 'e') ||
        (b[0] == 'O' && b[1] == 'T' && b[2] == 'T' && b[3] == 'O') ||
        (b[0] == 't' && b[1] == 't' && b[2] == 'c' && b[3] == 'f')) {
        return RoboFontFormatSfnt;
    }
    if (n >= 4u && b[0] == 'w' && b[1] == 'O' && b[2] == 'F' && b[3] == 'F') {
        return RoboFontFormatWoff;
    }
    if (n >= 4u && b[0] == 'w' && b[1] == 'O' && b[2] == 'F' && b[3] == '2') {
        return RoboFontFormatWoff2;
    }
    return RoboFontFormatUnknown;
}

Result FontFaceCreate(FontSource *source,
                                    uint32_t faceIndex,
                                    FontFace **out)
{
    FontFace *f;
    const uint8_t *bytes;
    size_t size;

    if (!out || !source) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    bytes = FontSourceBytes(source);
    size = FontSourceSize(source);
    if (!bytes || size == 0u) {
        return RoboInvalidArgument;
    }

    f = (FontFace *)RoboCalloc(1, sizeof(*f));
    if (!f) {
        return RoboOutOfMemory;
    }
    f->refs = 1u;
    f->id = gNextFaceId++;
    if (f->id == 0u) {
        f->id = gNextFaceId++;
    }
    FontSourceRetain(source);
    f->source = source;
    f->faceIndex = faceIndex;
    f->format = DetectFormat(bytes, size);
    FontStyleDescInit(&f->style);
    *out = f;
    return RoboOk;
}

void FontFaceRetain(FontFace *face)
{
    if (!face) {
        return;
    }
    face->refs++;
}

void FontFaceRelease(FontFace *face)
{
    if (!face) {
        return;
    }
    if (face->refs == 0u) {
        return;
    }
    face->refs--;
    if (face->refs != 0u) {
        return;
    }
    if (face->metrics) {
        FontMetricsInfoRelease(face->metrics);
        face->metrics = NULL;
    }
    if (face->classification) {
        FontClassificationInfoRelease(face->classification);
        face->classification = NULL;
    }
    if (face->nameInfo) {
        FontNameInfoRelease(face->nameInfo);
        face->nameInfo = NULL;
    }
    if (face->outlineCache) {
        FontOutlineCacheRelease(face->outlineCache);
        face->outlineCache = NULL;
    }
    if (face->cff) {
        FontCffRelease(face->cff);
        face->cff = NULL;
    }
    if (face->coverage) {
        FontCoverageRelease(face->coverage);
        face->coverage = NULL;
    }
    if (face->emoji) {
        FontEmojiInfoRelease(face->emoji);
        face->emoji = NULL;
    }
    if (face->variation) {
        FontVariationModelRelease(face->variation);
        face->variation = NULL;
    }
    if (face->color) {
        FontColorInfoRelease(face->color);
        face->color = NULL;
    }
    if (face->cmap) {
        FontCmapMapRelease(face->cmap);
        face->cmap = NULL;
    }
    if (face->tt) {
        FontTtRelease(face->tt);
        face->tt = NULL;
    }
    if (face->sfnt) {
        FontSfntRelease(face->sfnt);
        face->sfnt = NULL;
    }
    FontSourceRelease(face->source);
    face->source = NULL;
    RoboFree(face);
}

FontFaceId GetFontFaceId(const FontFace *face)
{
    return face ? face->id : 0u;
}

FontFormat FontFaceFormat(const FontFace *face)
{
    return face ? face->format : RoboFontFormatUnknown;
}

uint32_t FontFaceIndex(const FontFace *face)
{
    return face ? face->faceIndex : 0u;
}

const FontSource *FontFaceSource(const FontFace *face)
{
    return face ? face->source : NULL;
}

Result FontFaceStyle(const FontFace *face,
                                   FontStyleDesc *out)
{
    if (!face || !out) {
        return RoboInvalidArgument;
    }
    *out = face->style;
    return RoboOk;
}

Result FontFaceGlyphCount(const FontFace *face,
                                         uint32_t *outCount)
{
    if (!face || !outCount) {
        return RoboInvalidArgument;
    }
    *outCount = 0u;
    if (face->tt) {
        *outCount = FontTtNumGlyphs(face->tt);
        return RoboOk;
    }
    if (face->metrics) {
        const FontMetricsGlobal *g = FontMetricsInfoGlobal(face->metrics);
        if (g) {
            *outCount = g->numGlyphs;
            return RoboOk;
        }
    }
    if (face->cmap) {
        *outCount = FontCmapMapNumGlyphs(face->cmap);
        return RoboOk;
    }
    if (face->sfnt) {
        return FontSfntNumGlyphs(face->sfnt, outCount);
    }
    return RoboNotSupported;
}

static Result FaceEnsureSfnt(FontFace *face)
{
    FontSfnt *sfnt = NULL;
    Result res;
    if (face->sfnt) {
        return RoboOk;
    }
    if (face->format != RoboFontFormatSfnt) {
        return RoboNotSupported;
    }
    res = FontSfntParse(face->source, face->faceIndex, RoboFontSfntFlagsNone,
                               &sfnt);
    if (res != RoboOk) {
        return res;
    }
    face->sfnt = sfnt;
    return RoboOk;
}

Result FontFaceParseTruetype(FontFace *face, uint32_t ttFlags)
{
    FontTt *tt = NULL;
    Result res;

    if (!face) {
        return RoboInvalidArgument;
    }
    if (face->tt) {
        return RoboOk;
    }
    res = FaceEnsureSfnt(face);
    if (res != RoboOk) {
        return res;
    }
    res = FontTtParse(face->sfnt, ttFlags, &tt);
    if (res != RoboOk) {
        return res;
    }

    face->tt = tt;
    (void)FontTtStyleDesc(tt, &face->style);
    /* Classification is built lazily via parse_classification. */
    return RoboOk;
}

Result FontFaceParseClassification(FontFace *face)
{
    FontOt *ot = NULL;
    const FontStat *stat = NULL;
    const FontMetadata *meta = NULL;
    FontClassificationInfo *info = NULL;
    const FontClassification *cls;
    Result res;

    if (!face) {
        return RoboInvalidArgument;
    }
    if (face->classification) {
        return RoboOk;
    }
    res = FontFaceParseTruetype(face, RoboFontTtFlagsNone);
    if (res != RoboOk) {
        return res;
    }
    if (FontOtParse(face->sfnt, RoboFontOtFlagsParseExtra, &ot) == RoboOk &&
        ot) {
        stat = FontOtStat(ot);
    }
    if (!face->nameInfo) {
        (void)FontFaceParseMetadata(face);
    }
    meta = FontFaceMetadata(face);
    res = FontClassificationInfoCreate(face->tt, stat, meta, &info);
    if (ot) {
        FontOtRelease(ot);
    }
    if (res != RoboOk) {
        return res;
    }
    cls = FontClassificationInfoGet(info);
    if (cls) {
        face->style = cls->styleDesc;
    }
    face->classification = info;
    return RoboOk;
}

const FontClassification *FontFaceClassification(
    const FontFace *face)
{
    return face && face->classification
               ? FontClassificationInfoGet(face->classification)
               : NULL;
}

const FontTt *FontFaceTruetype(const FontFace *face)
{
    return face ? face->tt : NULL;
}

Result FontFaceParseCmap(FontFace *face)
{
    uint32_t numGlyphs = 0u;
    FontCmapMap *map = NULL;
    Result res;

    if (!face) {
        return RoboInvalidArgument;
    }
    if (face->cmap) {
        return RoboOk;
    }
    res = FaceEnsureSfnt(face);
    if (res != RoboOk) {
        return res;
    }
    if (face->tt) {
        numGlyphs = FontTtNumGlyphs(face->tt);
    } else {
        res = FontSfntNumGlyphs(face->sfnt, &numGlyphs);
        if (res != RoboOk) {
            return res;
        }
    }
    res = FontCmapMapCreate(face->sfnt, numGlyphs, &map);
    if (res != RoboOk) {
        return res;
    }
    face->cmap = map;
    return RoboOk;
}

const FontCmapMap *FontFaceCmap(const FontFace *face)
{
    return face ? face->cmap : NULL;
}

Result FontFaceParseCoverage(FontFace *face)
{
    FontCoverage *cov = NULL;
    Result res;

    if (!face) {
        return RoboInvalidArgument;
    }
    if (face->coverage) {
        return RoboOk;
    }
    if (face->coverageBuilding) {
        return RoboBusy;
    }
    face->coverageBuilding = 1;
    res = FontFaceParseCmap(face);
    if (res != RoboOk) {
        face->coverageBuilding = 0;
        return res;
    }
    res = FontCoverageCreateFromCmap(face->cmap, NULL, &cov);
    if (res != RoboOk) {
        face->coverageBuilding = 0;
        return res;
    }
    face->coverage = cov;
    face->coverageBuilding = 0;
    return RoboOk;
}

const FontCoverage *FontFaceCoverage(const FontFace *face)
{
    return face ? face->coverage : NULL;
}

Result FontFaceParseSfnt(FontFace *face)
{
    if (!face) {
        return RoboInvalidArgument;
    }
    return FaceEnsureSfnt(face);
}

const FontSfnt *FontFaceSfnt(const FontFace *face)
{
    return face ? face->sfnt : NULL;
}

Result FontFaceParseEmoji(FontFace *face)
{
    FontEmojiInfo *info = NULL;
    Result res;

    if (!face) {
        return RoboInvalidArgument;
    }
    if (face->emoji) {
        return RoboOk;
    }
    if (face->emojiBuilding) {
        return RoboBusy;
    }
    face->emojiBuilding = 1;
    res = FontEmojiInfoCreateFromFace(face, NULL, &info);
    if (res != RoboOk) {
        face->emojiBuilding = 0;
        return res;
    }
    face->emoji = info;
    face->emojiBuilding = 0;
    return RoboOk;
}

const FontEmojiInfo *FontFaceEmoji(const FontFace *face)
{
    return face ? face->emoji : NULL;
}

Result FontFaceParseVariation(FontFace *face)
{
    FontVariationModel *model = NULL;
    Result res;

    if (!face) {
        return RoboInvalidArgument;
    }
    if (face->variation) {
        return RoboOk;
    }
    if (face->variationAbsent) {
        return RoboNotFound;
    }
    if (face->variationBuilding) {
        return RoboBusy;
    }
    face->variationBuilding = 1;
    res = FontVariationModelCreateFromFace(face, &model);
    if (res == RoboNotFound) {
        face->variationAbsent = 1;
        face->variationBuilding = 0;
        return RoboNotFound;
    }
    if (res != RoboOk) {
        face->variationBuilding = 0;
        return res;
    }
    face->variation = model;
    face->variationBuilding = 0;
    return RoboOk;
}

const FontVariationModel *FontFaceVariation(const FontFace *face)
{
    return face ? face->variation : NULL;
}

Result FontFaceParseColor(FontFace *face)
{
    FontColorInfo *info = NULL;
    Result res;

    if (!face) {
        return RoboInvalidArgument;
    }
    if (face->color) {
        return RoboOk;
    }
    if (face->colorAbsent) {
        return RoboNotFound;
    }
    if (face->colorBuilding) {
        return RoboBusy;
    }
    face->colorBuilding = 1;
    res = FontColorInfoCreateFromFace(face, NULL, &info);
    if (res == RoboNotFound) {
        face->colorAbsent = 1;
        face->colorBuilding = 0;
        return RoboNotFound;
    }
    if (res != RoboOk) {
        face->colorBuilding = 0;
        return res;
    }
    face->color = info;
    face->colorBuilding = 0;
    return RoboOk;
}

const FontColorInfo *FontFaceColor(const FontFace *face)
{
    return face ? face->color : NULL;
}

Result FontFaceLookupGlyph(const FontFace *face,
                                          uint32_t codePoint,
                                          FontCmapLookupResult *out)
{
    if (!face || !out) {
        return RoboInvalidArgument;
    }
    if (!face->cmap) {
        return RoboNotSupported;
    }
    return FontCmapLookup(face->cmap, codePoint, out);
}

Result FontFaceParseMetrics(FontFace *face)
{
    FontMetricsInfo *info = NULL;
    Result res;

    if (!face) {
        return RoboInvalidArgument;
    }
    if (face->metrics) {
        return RoboOk;
    }
    res = FaceEnsureSfnt(face);
    if (res != RoboOk) {
        return res;
    }
    /* Ensure TT parse so metrics and face->tt stay coherent. */
    if (!face->tt) {
        res = FontFaceParseTruetype(face, RoboFontTtFlagsNone);
        if (res != RoboOk) {
            return res;
        }
    }
    res = FontMetricsInfoCreateFromTt(face->tt, &info);
    if (res != RoboOk) {
        return res;
    }
    face->metrics = info;
    return RoboOk;
}

const FontMetricsInfo *FontFaceMetricsInfo(const FontFace *face)
{
    return face ? face->metrics : NULL;
}

Result FontFaceLayoutMetrics(const FontFace *face,
                                            FontMetrics *out)
{
    if (!face || !out) {
        return RoboInvalidArgument;
    }
    if (!face->metrics) {
        return RoboNotSupported;
    }
    return FontMetricsInfoLayout(face->metrics, out);
}

Result FontFaceGlyphMetrics(const FontFace *face,
                                           GlyphId glyphId,
                                           FontGlyphMetrics *out)
{
    if (!face || !out) {
        return RoboInvalidArgument;
    }
    if (!face->metrics) {
        return RoboNotSupported;
    }
    return FontMetricsInfoGlyph(face->metrics, glyphId, out);
}

Result FontFaceParseOutlines(FontFace *face)
{
    FontOutlineTech tech;
    Result res;

    if (!face) {
        return RoboInvalidArgument;
    }
    if (face->outlineCache) {
        return RoboOk;
    }
    res = FaceEnsureSfnt(face);
    if (res != RoboOk) {
        return res;
    }
    tech = FontOtDetectOutline(face->sfnt);
    if (tech == RoboFontOutlineTruetype) {
        if (!face->tt) {
            res = FontFaceParseTruetype(face, RoboFontTtFlagsNone);
            if (res != RoboOk) {
                return res;
            }
        }
    } else if (tech == RoboFontOutlineCff || tech == RoboFontOutlineCff2) {
        if (!face->cff) {
            res = FontCffParse(face->sfnt, RoboFontCffFlagsParseAll, &face->cff);
            if (res != RoboOk) {
                return res;
            }
        }
    } else {
        return RoboNotSupported;
    }
    res = FontOutlineCacheCreate(&face->outlineCache);
    return res;
}

Result FontFaceGlyphOutline(FontFace *face,
                                           GlyphId glyphId,
                                           FontOutline **out)
{
    if (!face || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (!face->outlineCache || !face->sfnt) {
        return RoboNotSupported;
    }
    return FontOutlineCacheGet(face->outlineCache, face->sfnt, face->tt,
                                       face->cff, glyphId, out);
}

Result FontFaceGlyphOutlineAt(FontFace *face,
                                              const FontOutlineRequest *req,
                                              FontOutline **out)
{
    if (!face || !req || !out) {
        return RoboInvalidArgument;
    }
    /* Goal 5.18: accept coords but return static default outline. */
    (void)req->coords;
    return FontFaceGlyphOutline(face, req->glyphId, out);
}

Result FontFaceParseMetadata(FontFace *face)
{
    Result res;
    if (!face) {
        return RoboInvalidArgument;
    }
    if (face->nameInfo) {
        return RoboOk;
    }
    res = FaceEnsureSfnt(face);
    if (res != RoboOk) {
        return res;
    }
    /* Prefer TT name table when available (already validated). */
    if (!face->tt) {
        (void)FontFaceParseTruetype(face, RoboFontTtFlagsNone);
    }
    return FontNameInfoCreate(face->sfnt, face->tt, NULL, &face->nameInfo);
}

const FontNameInfo *FontFaceNameInfo(const FontFace *face)
{
    return face ? face->nameInfo : NULL;
}

const FontMetadata *FontFaceMetadata(const FontFace *face)
{
    return face && face->nameInfo ? FontNameInfoMetadata(face->nameInfo) : NULL;
}
