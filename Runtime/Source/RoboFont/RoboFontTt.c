#include "RoboFont/RoboFontTt.h"

#include "RoboFont/RoboFontClassification.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct FontTt {
    uint32_t refs;
    FontSfnt *sfnt;
    FontHead head;
    FontHhea hhea;
    FontMaxp maxp;
    FontPost post;
    FontOs2 os2;
    uint8_t hasOs2;
    uint8_t hasPost;
    FontNameTable name;
    uint8_t hasName;
    FontSpan hmtx;
    FontSpan glyf;
    uint32_t *loca; /* numGlyphs + 1 byte offsets */
    uint16_t numGlyphs;
    uint16_t numberOfHMetrics;
};

static Result TableSpan(const FontSfnt *sfnt,
                                FontTag tag,
                                FontSpan *out)
{
    return FontSfntTableBytes(sfnt, tag, out);
}

static Result ParseLoca(FontSpan span,
                                uint16_t numGlyphs,
                                int16_t indexFormat,
                                size_t glyfSize,
                                uint32_t **outOffs)
{
    uint32_t need = (uint32_t)numGlyphs + 1u;
    uint32_t *offs;
    uint32_t i;
    FontReader r;

    if (!outOffs) {
        return RoboInvalidArgument;
    }
    *outOffs = NULL;
    if (numGlyphs > RoboFontTtMaxGlyphs) {
        return RoboParseError;
    }

    offs = (uint32_t *)RoboCalloc(need, sizeof(uint32_t));
    if (!offs) {
        return RoboOutOfMemory;
    }
    FontReaderInitSpan(&r, span);

    if (indexFormat == 0) {
        if (span.size < (size_t)need * 2u) {
            RoboFree(offs);
            return RoboParseError;
        }
        for (i = 0u; i < need; i++) {
            uint16_t v;
            if (FontReadU16be(&r, &v) != RoboOk) {
                RoboFree(offs);
                return RoboParseError;
            }
            offs[i] = (uint32_t)v * 2u;
        }
    } else if (indexFormat == 1) {
        if (span.size < (size_t)need * 4u) {
            RoboFree(offs);
            return RoboParseError;
        }
        for (i = 0u; i < need; i++) {
            if (FontReadU32be(&r, &offs[i]) != RoboOk) {
                RoboFree(offs);
                return RoboParseError;
            }
        }
    } else {
        RoboFree(offs);
        return RoboParseError;
    }

    for (i = 1u; i < need; i++) {
        if (offs[i] < offs[i - 1u]) {
            RoboFree(offs);
            return RoboParseError;
        }
    }
    if (offs[numGlyphs] > glyfSize) {
        RoboFree(offs);
        return RoboParseError;
    }
    *outOffs = offs;
    return RoboOk;
}

static size_t ExpectedHmtxSize(uint16_t numGlyphs, uint16_t nHMetrics)
{
    if (nHMetrics > numGlyphs) {
        return 0u;
    }
    return (size_t)nHMetrics * 4u + (size_t)(numGlyphs - nHMetrics) * 2u;
}

static Result VerifyHeadChecksum(const FontSfnt *sfnt,
                                          FontSpan headSpan)
{
    FontSfntTableRecord rec;
    uint8_t *tmp;
    uint32_t calc;
    Result res;

    res = FontSfntFindTable(sfnt, RoboFontTagHead, &rec);
    if (res != RoboOk) {
        return res;
    }
    if (headSpan.size < 12u) {
        return RoboParseError;
    }
    tmp = (uint8_t *)Malloc(headSpan.size);
    if (!tmp) {
        return RoboOutOfMemory;
    }
    memcpy(tmp, headSpan.data, headSpan.size);
    /* Zero checkSumAdjustment at bytes 8..11 for verification. */
    tmp[8] = tmp[9] = tmp[10] = tmp[11] = 0u;
    calc = FontSfntChecksum(tmp, headSpan.size);
    RoboFree(tmp);
    if (calc != rec.checksum) {
        return RoboParseError;
    }
    return RoboOk;
}

Result FontTtParse(FontSfnt *sfnt,
                                 uint32_t flags,
                                 FontTt **out)
{
    FontTt *tt;
    FontSpan headS;
    FontSpan hheaS;
    FontSpan maxpS;
    FontSpan hmtxS;
    FontSpan locaS;
    FontSpan glyfS;
    FontSpan postS;
    FontSpan os2S;
    FontSpan nameS;
    Result res;
    size_t hmtxNeed;
    int hasGlyf;

    if (!out || !sfnt) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    hasGlyf = FontSfntHasTable(sfnt, RoboFontTagGlyf);
    if ((flags & RoboFontTtFlagsRequireGlyf) != 0u && !hasGlyf) {
        return RoboNotSupported;
    }

    if (TableSpan(sfnt, RoboFontTagHead, &headS) != RoboOk ||
        TableSpan(sfnt, RoboFontTagHhea, &hheaS) != RoboOk ||
        TableSpan(sfnt, RoboFontTagMaxp, &maxpS) != RoboOk ||
        TableSpan(sfnt, RoboFontTagHmtx, &hmtxS) != RoboOk) {
        return RoboNotFound;
    }

    tt = (FontTt *)RoboCalloc(1, sizeof(*tt));
    if (!tt) {
        return RoboOutOfMemory;
    }

    res = FontHeadParse(headS, &tt->head);
    if (res != RoboOk) {
        goto fail;
    }
    if ((flags & RoboFontTtFlagsVerifyHeadChecksum) != 0u) {
        res = VerifyHeadChecksum(sfnt, headS);
        if (res != RoboOk) {
            goto fail;
        }
    }
    res = FontHheaParse(hheaS, &tt->hhea);
    if (res != RoboOk) {
        goto fail;
    }
    res = FontMaxpParse(maxpS, &tt->maxp);
    if (res != RoboOk) {
        goto fail;
    }

    tt->numGlyphs = tt->maxp.numGlyphs;
    tt->numberOfHMetrics = tt->hhea.numberOfHMetrics;

    if (tt->numGlyphs > RoboFontTtMaxGlyphs) {
        res = RoboParseError;
        goto fail;
    }
    if (tt->numGlyphs == 0u) {
        if (tt->numberOfHMetrics != 0u) {
            res = RoboParseError;
            goto fail;
        }
    } else if (tt->numberOfHMetrics == 0u ||
               tt->numberOfHMetrics > tt->numGlyphs) {
        res = RoboParseError;
        goto fail;
    }

    hmtxNeed = ExpectedHmtxSize(tt->numGlyphs, tt->numberOfHMetrics);
    if (hmtxS.size < hmtxNeed) {
        res = RoboParseError;
        goto fail;
    }
    tt->hmtx = hmtxS;

    if (hasGlyf) {
        if (TableSpan(sfnt, RoboFontTagLoca, &locaS) != RoboOk ||
            TableSpan(sfnt, RoboFontTagGlyf, &glyfS) != RoboOk) {
            res = RoboNotFound;
            goto fail;
        }
        /* TrueType outlines require maxp v1.0 */
        if (tt->maxp.version != 0x00010000u) {
            res = RoboParseError;
            goto fail;
        }
        res = ParseLoca(locaS, tt->numGlyphs, tt->head.indexToLocFormat,
                         glyfS.size, &tt->loca);
        if (res != RoboOk) {
            goto fail;
        }
        tt->glyf = glyfS;
    } else if ((flags & RoboFontTtFlagsRequireGlyf) != 0u) {
        res = RoboNotSupported;
        goto fail;
    }

    if (TableSpan(sfnt, RoboFontTagPost, &postS) == RoboOk) {
        res = FontPostParse(postS, &tt->post);
        if (res != RoboOk) {
            goto fail;
        }
        tt->hasPost = 1u;
    }
    if (TableSpan(sfnt, RoboFontTagOs2, &os2S) == RoboOk) {
        res = FontOs2Parse(os2S, &tt->os2);
        if (res != RoboOk) {
            goto fail;
        }
        tt->hasOs2 = 1u;
    }
    if (TableSpan(sfnt, RoboFontTagName, &nameS) == RoboOk) {
        res = FontNameParse(nameS, &tt->name);
        if (res != RoboOk) {
            goto fail;
        }
        tt->hasName = 1u;
    }

    tt->refs = 1u;
    FontSfntRetain(sfnt);
    tt->sfnt = sfnt;
    *out = tt;
    return RoboOk;

fail:
    if (tt) {
        RoboFree(tt->loca);
        FontNameTableClear(&tt->name);
        RoboFree(tt);
    }
    return res;
}

void FontTtRetain(FontTt *tt)
{
    if (!tt) {
        return;
    }
    tt->refs++;
}

void FontTtRelease(FontTt *tt)
{
    if (!tt) {
        return;
    }
    if (tt->refs == 0u) {
        return;
    }
    tt->refs--;
    if (tt->refs != 0u) {
        return;
    }
    FontSfntRelease(tt->sfnt);
    tt->sfnt = NULL;
    RoboFree(tt->loca);
    tt->loca = NULL;
    FontNameTableClear(&tt->name);
    RoboFree(tt);
}

const FontSfnt *FontTtSfnt(const FontTt *tt)
{
    return tt ? tt->sfnt : NULL;
}

const FontHead *FontTtHead(const FontTt *tt)
{
    return tt ? &tt->head : NULL;
}

const FontHhea *FontTtHhea(const FontTt *tt)
{
    return tt ? &tt->hhea : NULL;
}

const FontMaxp *FontTtMaxp(const FontTt *tt)
{
    return tt ? &tt->maxp : NULL;
}

const FontPost *FontTtPost(const FontTt *tt)
{
    return (tt && tt->hasPost) ? &tt->post : NULL;
}

const FontOs2 *FontTtOs2(const FontTt *tt)
{
    return (tt && tt->hasOs2) ? &tt->os2 : NULL;
}

const FontNameTable *FontTtName(const FontTt *tt)
{
    return (tt && tt->hasName) ? &tt->name : NULL;
}

uint16_t FontTtNumGlyphs(const FontTt *tt)
{
    return tt ? tt->numGlyphs : 0u;
}

static Result CheckGid(const FontTt *tt, GlyphId gid)
{
    if (!tt) {
        return RoboInvalidArgument;
    }
    /* Glyph index 0 (.notdef) is valid; RoboGlyphIdNone coincides with 0 for
     * "unmapped" at the cmap layer — TT APIs treat 0 as a real glyph index. */
    if (gid >= tt->numGlyphs) {
        return RoboNotFound;
    }
    return RoboOk;
}

Result FontTtAdvanceWidth(const FontTt *tt,
                                         GlyphId glyphId,
                                         uint16_t *outAw)
{
    FontReader r;
    uint16_t idx;
    uint16_t aw;
    int16_t lsb;
    Result res = CheckGid(tt, glyphId);
    if (res != RoboOk) {
        return res;
    }
    if (!outAw) {
        return RoboInvalidArgument;
    }
    idx = (glyphId < tt->numberOfHMetrics) ? (uint16_t)glyphId
                                               : (uint16_t)(tt->numberOfHMetrics - 1u);
    FontReaderInitSpan(&r, tt->hmtx);
    if (FontReaderSeek(&r, (size_t)idx * 4u) != RoboOk ||
        FontReadU16be(&r, &aw) != RoboOk ||
        FontReadI16be(&r, &lsb) != RoboOk) {
        return RoboParseError;
    }
    (void)lsb;
    *outAw = aw;
    return RoboOk;
}

Result FontTtLeftSideBearing(const FontTt *tt,
                                             GlyphId glyphId,
                                             int16_t *outLsb)
{
    FontReader r;
    int16_t lsb;
    Result res = CheckGid(tt, glyphId);
    if (res != RoboOk) {
        return res;
    }
    if (!outLsb) {
        return RoboInvalidArgument;
    }
    FontReaderInitSpan(&r, tt->hmtx);
    if (glyphId < tt->numberOfHMetrics) {
        if (FontReaderSeek(&r, (size_t)glyphId * 4u + 2u) != RoboOk ||
            FontReadI16be(&r, &lsb) != RoboOk) {
            return RoboParseError;
        }
    } else {
        size_t off =
            (size_t)tt->numberOfHMetrics * 4u +
            (size_t)(glyphId - tt->numberOfHMetrics) * 2u;
        if (FontReaderSeek(&r, off) != RoboOk ||
            FontReadI16be(&r, &lsb) != RoboOk) {
            return RoboParseError;
        }
    }
    *outLsb = lsb;
    return RoboOk;
}

Result FontTtGlyphOffset(const FontTt *tt,
                                        GlyphId glyphId,
                                        uint32_t *outOffset)
{
    Result res = CheckGid(tt, glyphId);
    if (res != RoboOk) {
        return res;
    }
    if (!outOffset || !tt->loca) {
        return tt->loca ? RoboInvalidArgument : RoboNotSupported;
    }
    *outOffset = tt->loca[glyphId];
    return RoboOk;
}

Result FontTtGlyphLength(const FontTt *tt,
                                        GlyphId glyphId,
                                        uint32_t *outLength)
{
    Result res = CheckGid(tt, glyphId);
    if (res != RoboOk) {
        return res;
    }
    if (!outLength || !tt->loca) {
        return tt->loca ? RoboInvalidArgument : RoboNotSupported;
    }
    *outLength = tt->loca[glyphId + 1u] - tt->loca[glyphId];
    return RoboOk;
}

Result FontTtParseGlyph(const FontTt *tt,
                                       GlyphId glyphId,
                                       FontGlyfGlyph *out)
{
    uint32_t off;
    uint32_t len;
    Result res;

    if (!tt || !out) {
        return RoboInvalidArgument;
    }
    if (!tt->loca) {
        return RoboNotSupported;
    }
    res = FontTtGlyphOffset(tt, glyphId, &off);
    if (res != RoboOk) {
        return res;
    }
    res = FontTtGlyphLength(tt, glyphId, &len);
    if (res != RoboOk) {
        return res;
    }
    return FontTtParseGlyphAt(tt->glyf, off, len, tt->numGlyphs, out);
}

static Result ValidateCompositeRec(const FontTt *tt,
                                            GlyphId gid,
                                            uint32_t depth,
                                            uint8_t *visiting)
{
    FontGlyfGlyph g;
    Result res;
    uint16_t i;

    if (depth > RoboFontTtMaxCompositeDepth) {
        return RoboParseError;
    }
    if (gid >= tt->numGlyphs) {
        return RoboParseError;
    }
    if (visiting[gid]) {
        return RoboParseError; /* cycle */
    }

    memset(&g, 0, sizeof(g));
    res = FontTtParseGlyph(tt, gid, &g);
    if (res != RoboOk) {
        return res;
    }
    if (g.kind != RoboFontGlyfComposite) {
        FontGlyfGlyphClear(&g);
        return RoboOk;
    }

    visiting[gid] = 1u;
    for (i = 0u; i < g.componentCount; i++) {
        res = ValidateCompositeRec(tt, g.components[i].glyphIndex, depth + 1u, visiting);
        if (res != RoboOk) {
            visiting[gid] = 0u;
            FontGlyfGlyphClear(&g);
            return res;
        }
    }
    visiting[gid] = 0u;
    FontGlyfGlyphClear(&g);
    return RoboOk;
}

Result FontTtValidateCompositeClosure(const FontTt *tt,
                                                      GlyphId glyphId)
{
    uint8_t *visiting;
    Result res;
    if (!tt || !tt->loca) {
        return RoboInvalidArgument;
    }
    if (CheckGid(tt, glyphId) != RoboOk) {
        return RoboNotFound;
    }
    visiting = (uint8_t *)RoboCalloc(tt->numGlyphs, 1u);
    if (!visiting) {
        return RoboOutOfMemory;
    }
    res = ValidateCompositeRec(tt, glyphId, 0u, visiting);
    RoboFree(visiting);
    return res;
}

Result FontTtMetrics(const FontTt *tt, FontMetrics *out)
{
    FontBbox bb;
    FontUnit ascent;
    FontUnit descent;
    FontUnit lineGap;
    if (!tt || !out) {
        return RoboInvalidArgument;
    }
    ascent = tt->hhea.ascender;
    /* hhea descender is typically negative; store magnitude. */
    descent = tt->hhea.descender < 0 ? (FontUnit)(-tt->hhea.descender)
                                     : (FontUnit)tt->hhea.descender;
    lineGap = tt->hhea.lineGap < 0 ? 0 : (FontUnit)tt->hhea.lineGap;
    if (tt->hasOs2 && (tt->os2.fsSelection & 0x80u) != 0u) {
        /* USE_TYPO_METRICS */
        ascent = tt->os2.sTypoAscender;
        descent = tt->os2.sTypoDescender < 0
                      ? (FontUnit)(-tt->os2.sTypoDescender)
                      : (FontUnit)tt->os2.sTypoDescender;
        lineGap = tt->os2.sTypoLineGap < 0
                       ? 0
                       : (FontUnit)tt->os2.sTypoLineGap;
    }
    bb.xMin = tt->head.xMin;
    bb.yMin = tt->head.yMin;
    bb.xMax = tt->head.xMax;
    bb.yMax = tt->head.yMax;
    return FontMetricsInit(out, tt->head.unitsPerEm, ascent, descent, lineGap,
                                  &bb);
}

Result FontTtStyleDesc(const FontTt *tt,
                                      FontStyleDesc *out)
{
    FontClassificationInfo *info = NULL;
    const FontClassification *cls;
    Result res;

    if (!tt || !out) {
        return RoboInvalidArgument;
    }
    res = FontClassificationInfoCreate(tt, NULL, NULL, &info);
    if (res != RoboOk) {
        return res;
    }
    cls = FontClassificationInfoGet(info);
    FontClassificationToStyleDesc(cls, out);
    FontClassificationInfoRelease(info);
    return RoboOk;
}

Result FontTtFuzzParse(const uint8_t *data, size_t size)
{
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontTt *tt = NULL;
    Result res;

    if (!data || size == 0u) {
        return RoboInvalidArgument;
    }
    res = FontSourceCreateMemoryBorrow(data, size, &src);
    if (res != RoboOk) {
        return res;
    }
    res = FontSfntParse(src, 0u, RoboFontSfntFlagsNone, &sfnt);
    if (res == RoboOk) {
        res = FontTtParse(sfnt, RoboFontTtFlagsNone, &tt);
    }
    if (tt) {
        FontTtRelease(tt);
    }
    if (sfnt) {
        FontSfntRelease(sfnt);
    }
    FontSourceRelease(src);
    return res;
}
