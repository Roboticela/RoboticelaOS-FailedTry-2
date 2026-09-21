#include "RoboFont/RoboFontClassification.h"

#include "RoboFont/RoboFontSfnt.h"
#include "RoboMem/RoboMem.h"

#include <stdint.h>
#include <string.h>

struct FontClassificationInfo {
    uint32_t refs;
    FontClassification cls;
};

static int32_t FixedToMillidegrees(int32_t fixed1616)
{
    return (int32_t)(((int64_t)fixed1616 * 1000) / 65536);
}

static void RawInitEmpty(FontClassRaw *raw)
{
    memset(raw, 0, sizeof(*raw));
}

static void FillRawFromTt(const FontTt *tt, FontClassRaw *raw)
{
    const FontOs2 *os2;
    const FontPost *post;
    const FontHead *head;

    RawInitEmpty(raw);
    if (!tt) {
        return;
    }
    head = FontTtHead(tt);
    if (head) {
        raw->hasHead = 1u;
        raw->macStyle = head->macStyle;
    }
    os2 = FontTtOs2(tt);
    if (os2) {
        raw->hasOs2 = 1u;
        raw->os2Version = os2->version;
        raw->usWeightClass = os2->usWeightClass;
        raw->usWidthClass = os2->usWidthClass;
        raw->fsSelection = os2->fsSelection;
    }
    post = FontTtPost(tt);
    if (post) {
        raw->hasPost = 1u;
        raw->italicAngleFixed = post->italicAngle;
    }
}

static void FillRawStat(const FontStat *stat, FontClassRaw *raw)
{
    uint16_t i;
    if (!stat || !stat->axes) {
        return;
    }
    raw->hasStat = 1u;
    raw->statAxisCount = stat->designAxisCount;
    for (i = 0u; i < stat->designAxisCount; i++) {
        FontTag tag = stat->axes[i].tag;
        if (tag == RoboFontTag('w', 'g', 'h', 't')) {
            raw->statHasWght = 1u;
        } else if (tag == RoboFontTag('w', 'd', 't', 'h')) {
            raw->statHasWdth = 1u;
        } else if (tag == RoboFontTag('i', 't', 'a', 'l')) {
            raw->statHasItal = 1u;
        } else if (tag == RoboFontTag('s', 'l', 'n', 't')) {
            raw->statHasSlnt = 1u;
        } else if (tag == RoboFontTag('o', 'p', 's', 'z')) {
            raw->statHasOpsz = 1u;
        }
    }
}

static void FillRawMeta(const FontMetadata *meta, FontClassRaw *raw)
{
    if (!meta) {
        return;
    }
    raw->hasNameMeta = 1u;
    raw->nameHasLegacyFamily =
        (meta->familyName.avail == RoboFontNamePresent) ? 1u : 0u;
    raw->nameHasLegacySubfamily =
        (meta->subfamilyName.avail == RoboFontNamePresent) ? 1u : 0u;
    raw->nameHasTypoFamily =
        (meta->typographicFamilyName.avail == RoboFontNamePresent) ? 1u : 0u;
    raw->nameHasTypoSubfamily =
        (meta->typographicSubfamilyName.avail == RoboFontNamePresent) ? 1u : 0u;
    raw->nameHasWwsFamily =
        (meta->wwsFamilyName.avail == RoboFontNamePresent) ? 1u : 0u;
    raw->nameHasWwsSubfamily =
        (meta->wwsSubfamilyName.avail == RoboFontNamePresent) ? 1u : 0u;
}

Result FontClassify(const FontClassRaw *raw,
                                 FontClassification *out)
{
    uint16_t fs;
    uint8_t oblique;
    uint8_t italic;
    uint8_t boldBit;
    uint8_t regular;

    if (!raw || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->raw = *raw;
    FontStyleDescInit(&out->styleDesc);

    fs = raw->hasOs2 ? raw->fsSelection : 0u;
    out->wwsFlag = (raw->hasOs2 && (fs & RoboFontFsWws) != 0u) ? 1u : 0u;

    /* ---- Weight ---- */
    if (raw->hasOs2 && FontWeightIsValid((FontWeight)raw->usWeightClass)) {
        out->weight = (FontWeight)raw->usWeightClass;
        out->weightSource = RoboFontClassSrcOs2;
        out->weightAvail = RoboFontClassPresent;
        if ((fs & RoboFontFsBold) != 0u && out->weight < 600u) {
            out->conflictBoldBitVsWeight = 1u;
        }
    } else if (raw->hasOs2 && (fs & RoboFontFsBold) != 0u) {
        out->weight = RoboFontWeightBold;
        out->weightSource = RoboFontClassSrcFsSelection;
        out->weightAvail = RoboFontClassFallback;
        if (raw->usWeightClass != 0u &&
            !FontWeightIsValid((FontWeight)raw->usWeightClass)) {
            /* raw invalid preserved; normalized from bold bit */
            (void)0;
        }
    } else if (raw->hasHead && (raw->macStyle & RoboFontMacBold) != 0u) {
        out->weight = RoboFontWeightBold;
        out->weightSource = RoboFontClassSrcHead;
        out->weightAvail = RoboFontClassFallback;
    } else {
        out->weight = RoboFontWeightNormal;
        out->weightSource = RoboFontClassSrcDefault;
        out->weightAvail = RoboFontClassDefault;
        if (raw->hasOs2 && raw->usWeightClass != 0u &&
            !FontWeightIsValid((FontWeight)raw->usWeightClass)) {
            out->weightAvail = RoboFontClassInvalid;
            out->weightSource = RoboFontClassSrcOs2;
            /* still expose safe default weight 400 for consumers */
            out->weight = RoboFontWeightNormal;
        }
    }
    out->weightCategory = MakeFontWeightCategory(out->weight);

    /* ---- Stretch ---- */
    if (raw->hasOs2 && FontWidthClassIsValid(raw->usWidthClass)) {
        out->widthClass = (FontWidthClass)raw->usWidthClass;
        out->stretch = FontStretchFromWidthClass(raw->usWidthClass);
        out->stretchSource = RoboFontClassSrcOs2;
        out->stretchAvail = RoboFontClassPresent;
    } else {
        out->widthClass = RoboFontWidthNormal;
        out->stretch = RoboFontStretchNormal;
        out->stretchSource = RoboFontClassSrcDefault;
        out->stretchAvail = RoboFontClassDefault;
        if (raw->hasOs2 && raw->usWidthClass != 0u &&
            !FontWidthClassIsValid(raw->usWidthClass)) {
            out->stretchAvail = RoboFontClassInvalid;
            out->stretchSource = RoboFontClassSrcOs2;
            out->widthClass = RoboFontWidthUnknown;
        }
    }

    /* ---- Style ---- */
    oblique = (raw->hasOs2 && (fs & RoboFontFsOblique) != 0u) ? 1u : 0u;
    italic = (raw->hasOs2 && (fs & RoboFontFsItalic) != 0u) ? 1u : 0u;
    boldBit = (raw->hasOs2 && (fs & RoboFontFsBold) != 0u) ? 1u : 0u;
    regular = (raw->hasOs2 && (fs & RoboFontFsRegular) != 0u) ? 1u : 0u;
    (void)boldBit;

    if (oblique && italic) {
        out->bothItalicAndOblique = 1u;
    }
    if (regular && (oblique || italic)) {
        out->conflictRegularAndSlope = 1u;
    }

    if (oblique) {
        out->style = RoboFontStyleOblique;
        out->styleSource = RoboFontClassSrcOs2;
        out->styleAvail = RoboFontClassPresent;
    } else if (italic) {
        out->style = RoboFontStyleItalic;
        out->styleSource = RoboFontClassSrcOs2;
        out->styleAvail = RoboFontClassPresent;
    } else if (raw->hasHead && (raw->macStyle & RoboFontMacItalic) != 0u) {
        out->style = RoboFontStyleItalic;
        out->styleSource = RoboFontClassSrcHead;
        out->styleAvail = RoboFontClassFallback;
    } else if (raw->hasPost && raw->italicAngleFixed != 0) {
        out->style = RoboFontStyleOblique;
        out->styleSource = RoboFontClassSrcPost;
        out->styleAvail = RoboFontClassFallback;
    } else {
        out->style = RoboFontStyleNormal;
        out->styleSource = raw->hasOs2 ? RoboFontClassSrcOs2
                                         : RoboFontClassSrcDefault;
        out->styleAvail = raw->hasOs2 ? RoboFontClassPresent
                                        : RoboFontClassDefault;
    }

    if (raw->hasPost) {
        out->hasItalicAngle = 1u;
        out->italicAngleFixed = raw->italicAngleFixed;
        out->italicAngleMillidegrees = FixedToMillidegrees(raw->italicAngleFixed);
    }

    out->styleDesc.weight = out->weight;
    out->styleDesc.stretch = out->stretch;
    out->styleDesc.style =
        (out->style == RoboFontStyleUnknown) ? RoboFontStyleNormal : out->style;
    return RoboOk;
}

void FontClassificationToStyleDesc(const FontClassification *cls,
                                            FontStyleDesc *out)
{
    if (!out) {
        return;
    }
    FontStyleDescInit(out);
    if (!cls) {
        return;
    }
    *out = cls->styleDesc;
}

static Result InfoFromRaw(const FontClassRaw *raw,
                                   FontClassificationInfo **out)
{
    FontClassificationInfo *info;
    Result res;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    info = (FontClassificationInfo *)RoboCalloc(1, sizeof(*info));
    if (!info) {
        return RoboOutOfMemory;
    }
    info->refs = 1u;
    res = FontClassify(raw, &info->cls);
    if (res != RoboOk) {
        RoboFree(info);
        return res;
    }
    *out = info;
    return RoboOk;
}

Result FontClassificationInfoCreate(
    const FontTt *tt,
    const FontStat *statOpt,
    const FontMetadata *metaOpt,
    FontClassificationInfo **out)
{
    FontClassRaw raw;

    if (!tt || !out) {
        return RoboInvalidArgument;
    }
    FillRawFromTt(tt, &raw);
    FillRawStat(statOpt, &raw);
    FillRawMeta(metaOpt, &raw);
    return InfoFromRaw(&raw, out);
}

Result FontClassificationInfoFromSfnt(
    FontSfnt *sfnt,
    const FontMetadata *metaOpt,
    FontClassificationInfo **out)
{
    FontTt *tt = NULL;
    FontOt *ot = NULL;
    const FontStat *stat = NULL;
    Result res;

    if (!sfnt || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    res = FontTtParse(sfnt, RoboFontTtFlagsNone, &tt);
    if (res != RoboOk) {
        return res;
    }
    if (FontOtParse(sfnt, RoboFontOtFlagsParseExtra, &ot) == RoboOk && ot) {
        stat = FontOtStat(ot);
    }
    res = FontClassificationInfoCreate(tt, stat, metaOpt, out);
    if (ot) {
        FontOtRelease(ot);
    }
    FontTtRelease(tt);
    return res;
}

void FontClassificationInfoRetain(FontClassificationInfo *info)
{
    if (!info) {
        return;
    }
    info->refs++;
}

void FontClassificationInfoRelease(FontClassificationInfo *info)
{
    if (!info) {
        return;
    }
    if (info->refs == 0u) {
        return;
    }
    info->refs--;
    if (info->refs != 0u) {
        return;
    }
    RoboFree(info);
}

const FontClassification *FontClassificationInfoGet(
    const FontClassificationInfo *info)
{
    return info ? &info->cls : NULL;
}

Result FontClassificationFuzzParse(const uint8_t *data, size_t size)
{
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontClassificationInfo *info = NULL;
    FontClassRaw raw;
    FontClassification cls;
    Result res;

    if (!data) {
        return RoboInvalidArgument;
    }
    /* Always attempt pure-field classify on first bytes as OS/2-ish bag. */
    RawInitEmpty(&raw);
    if (size >= 8u) {
        raw.hasOs2 = 1u;
        raw.usWeightClass = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
        raw.usWidthClass = (uint16_t)(((uint16_t)data[2] << 8) | data[3]);
        raw.fsSelection = (uint16_t)(((uint16_t)data[4] << 8) | data[5]);
        raw.os2Version = (uint16_t)(((uint16_t)data[6] << 8) | data[7]);
    }
    if (size >= 12u) {
        raw.hasHead = 1u;
        raw.macStyle = (uint16_t)(((uint16_t)data[8] << 8) | data[9]);
        raw.hasPost = 1u;
        raw.italicAngleFixed =
            (int32_t)(((uint32_t)data[10] << 24) | ((uint32_t)data[11] << 16));
    }
    (void)FontClassify(&raw, &cls);

    if (size == 0u) {
        return RoboOk;
    }
    res = FontSourceCreateMemoryBorrow(data, size, &src);
    if (res != RoboOk) {
        return RoboOk;
    }
    if (FontSfntParse(src, 0u, RoboFontSfntFlagsNone, &sfnt) == RoboOk &&
        sfnt) {
        (void)FontClassificationInfoFromSfnt(sfnt, NULL, &info);
        if (info) {
            FontClassificationInfoRelease(info);
        }
        FontSfntRelease(sfnt);
    }
    FontSourceRelease(src);
    return RoboOk;
}
