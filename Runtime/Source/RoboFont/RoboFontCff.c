/*
 * CFF / CFF2 font bundle parse (Goal 5.5).
 */

#include "RoboFont/RoboFontCff.h"
#include "RoboFont/RoboFontSource.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

static void CffFontClearContents(FontCffFont *f)
{
    uint32_t i;
    if (!f) {
        return;
    }
    FontCffIndexClear(&f->nameIndex);
    FontCffIndexClear(&f->topDictIndex);
    FontCffIndexClear(&f->stringIndex);
    FontCffIndexClear(&f->globalSubrs);
    FontCffIndexClear(&f->charstrings);
    FontCffIndexClear(&f->fdarrayIndex);
    FontCffIndexClear(&f->localSubrs);
    FontCffTopDictClear(&f->top);
    FontCffPrivateDictClear(&f->priv);
    FontCffCharsetClear(&f->charset);
    FontCffEncodingClear(&f->encoding);
    FontCffFdselectClear(&f->fdselect);
    if (f->fontDicts) {
        for (i = 0u; i < f->fdCount; i++) {
            FontCffFontDictClear(&f->fontDicts[i]);
        }
        RoboFree(f->fontDicts);
    }
    if (f->fdPrivates) {
        for (i = 0u; i < f->fdCount; i++) {
            FontCffPrivateDictClear(&f->fdPrivates[i]);
        }
        RoboFree(f->fdPrivates);
    }
    if (f->fdLocalSubrs) {
        for (i = 0u; i < f->fdCount; i++) {
            FontCffIndexClear(&f->fdLocalSubrs[i]);
        }
        RoboFree(f->fdLocalSubrs);
    }
    FontCffVstoreClear(&f->vstore);
    if (f->sfnt) {
        FontSfntRelease(f->sfnt);
        f->sfnt = NULL;
    }
}

void FontCffRetain(FontCffFont *font)
{
    if (font) {
        font->refs++;
    }
}

void FontCffRelease(FontCffFont *font)
{
    if (!font) {
        return;
    }
    if (font->refs > 1) {
        font->refs--;
        return;
    }
    CffFontClearContents(font);
    RoboFree(font);
}

FontOutlineTech FontCffOutlineTech(const FontCffFont *font)
{
    return font ? font->tech : RoboFontOutlineUnknown;
}

uint32_t FontCffGlyphCount(const FontCffFont *font)
{
    return font ? font->glyphCount : 0u;
}

Result FontCffCharstringBytes(const FontCffFont *font,
                                             uint32_t gid,
                                             FontSpan *out)
{
    if (!font || !out) {
        return RoboInvalidArgument;
    }
    return FontCffIndexGet(&font->charstrings, gid, out);
}

static Result ParsePrivateAt(FontSpan table,
                                      int32_t privSize,
                                      int32_t privOffset,
                                      int isCff2,
                                      FontCffPrivateDict *outPriv,
                                      FontCffIndex *outLocal)
{
    FontSpan privSpan;
    FontCffDict raw;
    size_t abs;
    Result res;

    if (privSize < 0 || privOffset < 0) {
        return RoboOk;
    }
    /* Private offset is relative to the start of the CFF/CFF2 table. */
    abs = (size_t)privOffset;
    if (FontSpanSlice(table, abs, (size_t)privSize, &privSpan) != RoboOk) {
        return RoboParseError;
    }
    memset(&raw, 0, sizeof(raw));
    res = FontCffDictParse(privSpan, &raw);
    if (res != RoboOk) {
        return res;
    }
    res = FontCffPrivateDictInterpret(&raw, outPriv);
    FontCffDictClear(&raw);
    if (res != RoboOk) {
        return res;
    }
    if (outPriv->subrs >= 0 && outLocal) {
        size_t subrOff = abs + (size_t)outPriv->subrs;
        size_t end = 0u;
        if (isCff2) {
            res = FontCff2IndexParse(table, subrOff, outLocal, &end);
        } else {
            res = FontCffIndexParse(table, subrOff, outLocal, &end);
        }
        if (res != RoboOk) {
            return res;
        }
    }
    return RoboOk;
}

static Result ParseCff1(FontSpan table,
                                uint32_t flags,
                                FontCffFont *f)
{
    FontReader r;
    size_t cursor;
    size_t end;
    FontSpan topBytes;
    FontCffDict topRaw;
    Result res;

    FontReaderInitSpan(&r, table);
    if (FontReadU8(&r, &f->major) != RoboOk ||
        FontReadU8(&r, &f->minor) != RoboOk ||
        FontReadU8(&r, &f->hdrSize) != RoboOk ||
        FontReadU8(&r, &f->offSize) != RoboOk) {
        return RoboParseError;
    }
    if (f->major != 1u) {
        return RoboNotSupported;
    }
    if (f->hdrSize < 4u || f->offSize < 1u || f->offSize > 4u) {
        return RoboParseError;
    }
    if ((size_t)f->hdrSize > table.size) {
        return RoboParseError;
    }
    cursor = f->hdrSize;

    res = FontCffIndexParse(table, cursor, &f->nameIndex, &end);
    if (res != RoboOk) {
        return res;
    }
    cursor = end;

    res = FontCffIndexParse(table, cursor, &f->topDictIndex, &end);
    if (res != RoboOk) {
        return res;
    }
    cursor = end;

    res = FontCffIndexParse(table, cursor, &f->stringIndex, &end);
    if (res != RoboOk) {
        return res;
    }
    cursor = end;

    res = FontCffIndexParse(table, cursor, &f->globalSubrs, &end);
    if (res != RoboOk) {
        return res;
    }

    if (f->topDictIndex.count < 1u) {
        return RoboParseError;
    }
    res = FontCffIndexGet(&f->topDictIndex, 0u, &topBytes);
    if (res != RoboOk) {
        return res;
    }
    memset(&topRaw, 0, sizeof(topRaw));
    res = FontCffDictParse(topBytes, &topRaw);
    if (res != RoboOk) {
        return res;
    }
    res = FontCffTopDictInterpret(&topRaw, &f->top);
    FontCffDictClear(&topRaw);
    if (res != RoboOk) {
        return res;
    }

    if (f->top.charstringType != 2) {
        return RoboNotSupported;
    }
    if (f->top.charstrings < 0) {
        if (flags & RoboFontCffFlagsRequireCharstrings) {
            return RoboParseError;
        }
    } else {
        res = FontCffIndexParse(table, (size_t)f->top.charstrings,
                                        &f->charstrings, &end);
        if (res != RoboOk) {
            return res;
        }
        f->glyphCount = f->charstrings.count;
        if (f->glyphCount > RoboFontCffMaxGlyphs) {
            return RoboParseError;
        }
    }

    if ((flags & RoboFontCffFlagsParsePrivate) && !f->top.isCid &&
        f->top.privateOffset >= 0 && f->top.privateSize >= 0) {
        res = ParsePrivateAt(table, f->top.privateSize, f->top.privateOffset, 0,
                               &f->priv, &f->localSubrs);
        if (res != RoboOk) {
            return res;
        }
    }

    if ((flags & RoboFontCffFlagsParseCid) && f->top.isCid) {
        uint32_t i;
        if (f->top.fdarray < 0 || f->top.fdselect < 0) {
            return RoboParseError;
        }
        res = FontCffIndexParse(table, (size_t)f->top.fdarray,
                                        &f->fdarrayIndex, &end);
        if (res != RoboOk) {
            return res;
        }
        f->fdCount = f->fdarrayIndex.count;
        if (f->fdCount == 0u || f->fdCount > RoboFontCffMaxFdarray) {
            return RoboParseError;
        }
        f->fontDicts = (FontCffFontDict *)RoboCalloc(
            f->fdCount, sizeof(FontCffFontDict));
        f->fdPrivates = (FontCffPrivateDict *)RoboCalloc(
            f->fdCount, sizeof(FontCffPrivateDict));
        f->fdLocalSubrs =
            (FontCffIndex *)RoboCalloc(f->fdCount, sizeof(FontCffIndex));
        if (!f->fontDicts || !f->fdPrivates || !f->fdLocalSubrs) {
            return RoboOutOfMemory;
        }
        for (i = 0u; i < f->fdCount; i++) {
            FontSpan fdBytes;
            FontCffDict raw;
            res = FontCffIndexGet(&f->fdarrayIndex, i, &fdBytes);
            if (res != RoboOk) {
                return res;
            }
            memset(&raw, 0, sizeof(raw));
            res = FontCffDictParse(fdBytes, &raw);
            if (res != RoboOk) {
                return res;
            }
            res = FontCffFontDictInterpret(&raw, &f->fontDicts[i]);
            FontCffDictClear(&raw);
            if (res != RoboOk) {
                return res;
            }
            if (flags & RoboFontCffFlagsParsePrivate) {
                res = ParsePrivateAt(table, f->fontDicts[i].privateSize,
                                       f->fontDicts[i].privateOffset, 0,
                                       &f->fdPrivates[i], &f->fdLocalSubrs[i]);
                if (res != RoboOk) {
                    return res;
                }
            }
        }
        res = FontCffFdselectParse(table, (size_t)f->top.fdselect,
                                           f->glyphCount, f->fdCount, &f->fdselect);
        if (res != RoboOk) {
            return res;
        }
    }

    if ((flags & RoboFontCffFlagsParseCharset) && f->glyphCount > 0u &&
        f->top.charset >= 0) {
        res = FontCffCharsetParse(table, (size_t)f->top.charset, f->glyphCount,
                                          f->top.isCid, &f->charset);
        if (res != RoboOk) {
            return res;
        }
    }

    if ((flags & RoboFontCffFlagsParseEncoding) && !f->top.isCid &&
        f->top.encoding >= 0) {
        res = FontCffEncodingParse(table, (size_t)f->top.encoding, f->glyphCount,
                                           &f->encoding);
        if (res != RoboOk) {
            return res;
        }
    }

    return RoboOk;
}

static Result ParseCff2(FontSpan table,
                                uint32_t flags,
                                FontCffFont *f)
{
    FontReader r;
    size_t cursor;
    size_t end;
    FontSpan topBytes;
    FontCffDict topRaw;
    Result res;
    uint32_t i;

    FontReaderInitSpan(&r, table);
    if (FontReadU8(&r, &f->major) != RoboOk ||
        FontReadU8(&r, &f->minor) != RoboOk ||
        FontReadU8(&r, &f->hdrSize) != RoboOk ||
        FontReadU16be(&r, &f->topDictLength) != RoboOk) {
        return RoboParseError;
    }
    if (f->major != 2u) {
        return RoboNotSupported;
    }
    if (f->hdrSize < 5u) {
        return RoboParseError;
    }
    cursor = f->hdrSize;
    if (cursor + (size_t)f->topDictLength > table.size) {
        return RoboParseError;
    }
    if (FontSpanSlice(table, cursor, f->topDictLength, &topBytes) != RoboOk) {
        return RoboParseError;
    }
    cursor += f->topDictLength;

    memset(&topRaw, 0, sizeof(topRaw));
    res = FontCffDictParse(topBytes, &topRaw);
    if (res != RoboOk) {
        return res;
    }
    res = FontCffTopDictInterpret(&topRaw, &f->top);
    FontCffDictClear(&topRaw);
    if (res != RoboOk) {
        return res;
    }
    /* CFF2: single-byte ops 15/16 are vsindex/blend, not charset/encoding. */
    f->top.charset = -1;
    f->top.encoding = -1;

    /* Global Subr INDEX follows Top DICT */
    res = FontCff2IndexParse(table, cursor, &f->globalSubrs, &end);
    if (res != RoboOk) {
        return res;
    }

    if (f->top.charstrings < 0) {
        if (flags & RoboFontCffFlagsRequireCharstrings) {
            return RoboParseError;
        }
    } else {
        res = FontCff2IndexParse(table, (size_t)f->top.charstrings,
                                         &f->charstrings, &end);
        if (res != RoboOk) {
            return res;
        }
        f->glyphCount = f->charstrings.count;
    }

    if ((flags & RoboFontCffFlagsParseVstore) && f->top.vstore >= 0) {
        res = FontCffVstoreParse(table, (size_t)f->top.vstore, &f->vstore);
        if (res != RoboOk) {
            return res;
        }
        f->hasVstore = 1;
    }

    if (f->top.fdarray < 0) {
        return RoboParseError; /* CFF2 requires FDArray */
    }
    res = FontCff2IndexParse(table, (size_t)f->top.fdarray, &f->fdarrayIndex,
                                     &end);
    if (res != RoboOk) {
        return res;
    }
    f->fdCount = f->fdarrayIndex.count;
    if (f->fdCount == 0u || f->fdCount > RoboFontCffMaxFdarray) {
        return RoboParseError;
    }
    f->fontDicts = (FontCffFontDict *)RoboCalloc(
        f->fdCount, sizeof(FontCffFontDict));
    f->fdPrivates = (FontCffPrivateDict *)RoboCalloc(
        f->fdCount, sizeof(FontCffPrivateDict));
    f->fdLocalSubrs =
        (FontCffIndex *)RoboCalloc(f->fdCount, sizeof(FontCffIndex));
    if (!f->fontDicts || !f->fdPrivates || !f->fdLocalSubrs) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < f->fdCount; i++) {
        FontSpan fdBytes;
        FontCffDict raw;
        res = FontCffIndexGet(&f->fdarrayIndex, i, &fdBytes);
        if (res != RoboOk) {
            return res;
        }
        memset(&raw, 0, sizeof(raw));
        res = FontCffDictParse(fdBytes, &raw);
        if (res != RoboOk) {
            return res;
        }
        res = FontCffFontDictInterpret(&raw, &f->fontDicts[i]);
        FontCffDictClear(&raw);
        if (res != RoboOk) {
            return res;
        }
        if (flags & RoboFontCffFlagsParsePrivate) {
            res = ParsePrivateAt(table, f->fontDicts[i].privateSize,
                                   f->fontDicts[i].privateOffset, 1,
                                   &f->fdPrivates[i], &f->fdLocalSubrs[i]);
            if (res != RoboOk) {
                return res;
            }
        }
    }

    if (f->top.fdselect >= 0 && f->glyphCount > 0u) {
        res = FontCffFdselectParse(table, (size_t)f->top.fdselect, f->glyphCount,
                                           f->fdCount, &f->fdselect);
        if (res != RoboOk) {
            return res;
        }
    }

    return RoboOk;
}

Result FontCffParse(FontSfnt *sfnt,
                                  uint32_t flags,
                                  FontCffFont **out)
{
    FontOutlineTech tech;
    FontSpan table;
    FontCffFont *f;
    Result res;

    if (!sfnt || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    tech = FontOtDetectOutline(sfnt);
    if (tech != RoboFontOutlineCff && tech != RoboFontOutlineCff2) {
        return RoboNotSupported;
    }

    f = (FontCffFont *)RoboCalloc(1u, sizeof(*f));
    if (!f) {
        return RoboOutOfMemory;
    }
    f->refs = 1;
    f->tech = tech;
    FontSfntRetain(sfnt);
    f->sfnt = sfnt;

    if (tech == RoboFontOutlineCff2) {
        if (FontSfntTableBytes(sfnt, RoboFontTagCff2, &table) != RoboOk) {
            FontCffRelease(f);
            return RoboParseError;
        }
        res = ParseCff2(table, flags, f);
    } else {
        if (FontSfntTableBytes(sfnt, RoboFontTagCff, &table) != RoboOk) {
            FontCffRelease(f);
            return RoboParseError;
        }
        res = ParseCff1(table, flags, f);
    }

    if (res != RoboOk) {
        FontCffRelease(f);
        return res;
    }
    *out = f;
    return RoboOk;
}

Result FontCffParseGlyph(const FontCffFont *font,
                                        uint32_t gid,
                                        FontCffCharstring *out)
{
    FontSpan cs;
    FontCffSubrCtx ctx;
    const FontCffPrivateDict *priv;
    const FontCffIndex *local;
    double defW = 0.0;
    double nomW = 0.0;
    int hasNom = 0;

    if (!font || !out) {
        return RoboInvalidArgument;
    }
    if (FontCffCharstringBytes(font, gid, &cs) != RoboOk) {
        return RoboParseError;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.globalSubrs = &font->globalSubrs;
    ctx.globalBias = FontCffSubrBias(font->globalSubrs.count);

    priv = &font->priv;
    local = &font->localSubrs;
    if (font->fdCount > 0u && font->fdselect.fds) {
        uint8_t fd = 0u;
        if (FontCffFdselectGet(&font->fdselect, gid, &fd) == RoboOk &&
            fd < font->fdCount) {
            priv = &font->fdPrivates[fd];
            local = &font->fdLocalSubrs[fd];
        }
    }
    ctx.localSubrs = local;
    ctx.localBias = FontCffSubrBias(local->count);
    if (priv->hasDefaultWidth) {
        defW = priv->defaultWidthX;
    }
    if (priv->hasNominalWidth) {
        nomW = priv->nominalWidthX;
        hasNom = 1;
    }

    {
        uint32_t flags = RoboFontCffCsFlagsNone;
        if (font->major == 2u) {
            flags |= RoboFontCffCsFlagsAllowNoEndchar;
        }
        return FontCffCharstringParseEx(cs, &ctx, defW, nomW, hasNom,
                                                 flags, out);
    }
}

Result FontCffFuzzParse(const uint8_t *data, size_t size)
{
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontCffFont *cff = NULL;
    Result res;

    if (!data && size > 0u) {
        return RoboInvalidArgument;
    }
    res = FontSourceCreateMemoryCopy(data, size, &src);
    if (res != RoboOk) {
        return res;
    }
    res = FontSfntParse(src, 0u, 0u, &sfnt);
    if (res != RoboOk) {
        FontSourceRelease(src);
        return res;
    }
    res = FontCffParse(sfnt, RoboFontCffFlagsParseAll, &cff);
    if (cff) {
        FontCffRelease(cff);
    }
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
    return res;
}
