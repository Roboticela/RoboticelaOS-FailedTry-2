#include "RoboFont/RoboFontOt.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct FontOt {
    uint32_t refs;
    FontSfnt *sfnt;
    FontOutlineTech outline;
    FontCmap cmap;
    uint8_t hasCmap;
    FontGdef gdef;
    uint8_t hasGdef;
    FontOtLayout gsub;
    uint8_t hasGsub;
    FontOtLayout gpos;
    uint8_t hasGpos;
    FontBase base;
    uint8_t hasBase;
    FontJstf jstf;
    uint8_t hasJstf;
    FontMath math;
    uint8_t hasMath;
    FontKern kern;
    uint8_t hasKern;
    FontColr colr;
    uint8_t hasColr;
    FontCpal cpal;
    uint8_t hasCpal;
    FontStat stat;
    uint8_t hasStat;
};

FontOutlineTech FontOtDetectOutline(const FontSfnt *sfnt)
{
    int hasGlyf;
    int hasLoca;
    int hasCff;
    int hasCff2;
    if (!sfnt) {
        return RoboFontOutlineUnknown;
    }
    hasGlyf = FontSfntHasTable(sfnt, RoboFontTagGlyf);
    hasLoca = FontSfntHasTable(sfnt, RoboFontTagLoca);
    hasCff = FontSfntHasTable(sfnt, RoboFontTagCff);
    hasCff2 = FontSfntHasTable(sfnt, RoboFontTagCff2);
    if (hasCff2) {
        return RoboFontOutlineCff2;
    }
    if (hasCff) {
        return RoboFontOutlineCff;
    }
    if (hasGlyf && hasLoca) {
        return RoboFontOutlineTruetype;
    }
    if (hasGlyf || hasLoca) {
        return RoboFontOutlineUnsupported;
    }
    return RoboFontOutlineUnknown;
}

FontOtTableFamily MakeFontOtTableFamily(FontTag tag)
{
    switch (tag) {
    case RoboFontTagHead:
    case RoboFontTagName:
    case RoboFontTagOs2:
    case RoboFontTagPost:
    case RoboFontTagMaxp:
        return RoboFontOtFamilyCore;
    case RoboFontTagHhea:
    case RoboFontTagHmtx:
        return RoboFontOtFamilyMetric;
    case RoboFontTagCmap:
        return RoboFontOtFamilyCmap;
    case RoboFontTagGsub:
    case RoboFontTagGpos:
    case RoboFontTagGdef:
    case RoboFontTagBase:
    case RoboFontTagJstf:
        return RoboFontOtFamilyLayout;
    case RoboFontTagFvar:
    case RoboFontTagAvar:
    case RoboFontTagGvar:
    case RoboFontTagHvar:
    case RoboFontTagMvar:
    case RoboFontTagStat:
        return RoboFontOtFamilyVariation;
    case RoboFontTagColr:
    case RoboFontTagCpal:
    case RoboFontTagSbix:
    case RoboFontTagCbdt:
    case RoboFontTagCblc:
    case RoboFontTagSvg:
        return RoboFontOtFamilyColor;
    case RoboFontTagMath:
        return RoboFontOtFamilyMath;
    case RoboFontTagKern:
        return RoboFontOtFamilyLegacy;
    case RoboFontTagGlyf:
    case RoboFontTagLoca:
    case RoboFontTagCff:
    case RoboFontTagCff2:
        return RoboFontOtFamilyOutline;
    default:
        return RoboFontOtFamilyUnknown;
    }
}

static Result TakeTable(FontSfnt *sfnt,
                                FontTag tag,
                                FontSpan *out)
{
    return FontSfntTableBytes(sfnt, tag, out);
}

Result FontOtParse(FontSfnt *sfnt,
                                 uint32_t flags,
                                 FontOt **out)
{
    FontOt *ot;
    FontSpan span;
    Result res;

    if (!out || !sfnt) {
        return RoboInvalidArgument;
    }
    *out = NULL;

    ot = (FontOt *)RoboCalloc(1, sizeof(*ot));
    if (!ot) {
        return RoboOutOfMemory;
    }
    ot->outline = FontOtDetectOutline(sfnt);

    if ((flags & RoboFontOtFlagsParseCmap) != 0u &&
        TakeTable(sfnt, RoboFontTagCmap, &span) == RoboOk) {
        res = FontCmapParse(span, &ot->cmap);
        if (res != RoboOk) {
            goto fail;
        }
        ot->hasCmap = 1u;
    }

    if ((flags & RoboFontOtFlagsParseLayout) != 0u) {
        if (TakeTable(sfnt, RoboFontTagGdef, &span) == RoboOk) {
            res = FontGdefParse(span, &ot->gdef);
            if (res != RoboOk) {
                goto fail;
            }
            ot->hasGdef = 1u;
        }
        if (TakeTable(sfnt, RoboFontTagGsub, &span) == RoboOk) {
            res = FontGsubParse(span, &ot->gsub);
            if (res != RoboOk) {
                goto fail;
            }
            ot->hasGsub = 1u;
        }
        if (TakeTable(sfnt, RoboFontTagGpos, &span) == RoboOk) {
            res = FontGposParse(span, &ot->gpos);
            if (res != RoboOk) {
                goto fail;
            }
            ot->hasGpos = 1u;
        }
    }

    if ((flags & RoboFontOtFlagsParseExtra) != 0u) {
        if (TakeTable(sfnt, RoboFontTagBase, &span) == RoboOk) {
            res = FontBaseParse(span, &ot->base);
            if (res != RoboOk) {
                goto fail;
            }
            ot->hasBase = 1u;
        }
        if (TakeTable(sfnt, RoboFontTagJstf, &span) == RoboOk) {
            res = FontJstfParse(span, &ot->jstf);
            if (res != RoboOk) {
                goto fail;
            }
            ot->hasJstf = 1u;
        }
        if (TakeTable(sfnt, RoboFontTagMath, &span) == RoboOk) {
            res = FontMathParse(span, &ot->math);
            if (res != RoboOk) {
                goto fail;
            }
            ot->hasMath = 1u;
        }
        if (TakeTable(sfnt, RoboFontTagKern, &span) == RoboOk) {
            res = FontKernParse(span, &ot->kern);
            if (res != RoboOk) {
                goto fail;
            }
            ot->hasKern = 1u;
        }
        if (TakeTable(sfnt, RoboFontTagCpal, &span) == RoboOk) {
            res = FontCpalParse(span, &ot->cpal);
            if (res != RoboOk) {
                goto fail;
            }
            ot->hasCpal = 1u;
        }
        if (TakeTable(sfnt, RoboFontTagColr, &span) == RoboOk) {
            res = FontColrParse(span, &ot->colr);
            if (res != RoboOk) {
                goto fail;
            }
            ot->hasColr = 1u;
        }
        if (TakeTable(sfnt, RoboFontTagStat, &span) == RoboOk) {
            res = FontStatParse(span, &ot->stat);
            if (res != RoboOk) {
                goto fail;
            }
            ot->hasStat = 1u;
        }
    }

    ot->refs = 1u;
    FontSfntRetain(sfnt);
    ot->sfnt = sfnt;
    *out = ot;
    return RoboOk;

fail:
    FontCmapClear(&ot->cmap);
    FontGdefClear(&ot->gdef);
    FontOtLayoutClear(&ot->gsub);
    FontOtLayoutClear(&ot->gpos);
    FontKernClear(&ot->kern);
    FontStatClear(&ot->stat);
    RoboFree(ot);
    return res;
}

void FontOtRetain(FontOt *ot)
{
    if (!ot) {
        return;
    }
    ot->refs++;
}

void FontOtRelease(FontOt *ot)
{
    if (!ot) {
        return;
    }
    if (ot->refs == 0u) {
        return;
    }
    ot->refs--;
    if (ot->refs != 0u) {
        return;
    }
    FontCmapClear(&ot->cmap);
    FontGdefClear(&ot->gdef);
    FontOtLayoutClear(&ot->gsub);
    FontOtLayoutClear(&ot->gpos);
    FontKernClear(&ot->kern);
    FontStatClear(&ot->stat);
    FontSfntRelease(ot->sfnt);
    ot->sfnt = NULL;
    RoboFree(ot);
}

const FontSfnt *FontOtSfnt(const FontOt *ot)
{
    return ot ? ot->sfnt : NULL;
}

FontOutlineTech FontOtOutline(const FontOt *ot)
{
    return ot ? ot->outline : RoboFontOutlineUnknown;
}

const FontCmap *FontOtCmap(const FontOt *ot)
{
    return (ot && ot->hasCmap) ? &ot->cmap : NULL;
}

const FontGdef *FontOtGdef(const FontOt *ot)
{
    return (ot && ot->hasGdef) ? &ot->gdef : NULL;
}

const FontOtLayout *FontOtGsub(const FontOt *ot)
{
    return (ot && ot->hasGsub) ? &ot->gsub : NULL;
}

const FontOtLayout *FontOtGpos(const FontOt *ot)
{
    return (ot && ot->hasGpos) ? &ot->gpos : NULL;
}

const FontBase *FontOtBase(const FontOt *ot)
{
    return (ot && ot->hasBase) ? &ot->base : NULL;
}

const FontJstf *FontOtJstf(const FontOt *ot)
{
    return (ot && ot->hasJstf) ? &ot->jstf : NULL;
}

const FontMath *FontOtMath(const FontOt *ot)
{
    return (ot && ot->hasMath) ? &ot->math : NULL;
}

const FontKern *FontOtKern(const FontOt *ot)
{
    return (ot && ot->hasKern) ? &ot->kern : NULL;
}

const FontColr *FontOtColr(const FontOt *ot)
{
    return (ot && ot->hasColr) ? &ot->colr : NULL;
}

const FontCpal *FontOtCpal(const FontOt *ot)
{
    return (ot && ot->hasCpal) ? &ot->cpal : NULL;
}

const FontStat *FontOtStat(const FontOt *ot)
{
    return (ot && ot->hasStat) ? &ot->stat : NULL;
}

Result FontOtFuzzParse(const uint8_t *data, size_t size)
{
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontOt *ot = NULL;
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
        res = FontOtParse(sfnt, RoboFontOtFlagsParseAll, &ot);
    }
    if (ot) {
        FontOtRelease(ot);
    }
    if (sfnt) {
        FontSfntRelease(sfnt);
    }
    FontSourceRelease(src);
    return res;
}
