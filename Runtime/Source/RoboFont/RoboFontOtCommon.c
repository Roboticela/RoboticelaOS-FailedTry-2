#include "RoboFont/RoboFontOtCommon.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

Result FontOtResolveOffset16(FontSpan table,
                                            size_t base,
                                            uint16_t offset,
                                            size_t *outAbs)
{
    size_t abs;
    if (!outAbs) {
        return RoboInvalidArgument;
    }
    if (base > table.size) {
        return RoboParseError;
    }
    abs = base + (size_t)offset;
    if (abs < base || abs > table.size) {
        return RoboParseError;
    }
    *outAbs = abs;
    return RoboOk;
}

Result FontOtResolveOffset32(FontSpan table,
                                            size_t base,
                                            uint32_t offset,
                                            size_t *outAbs)
{
    size_t abs;
    if (!outAbs) {
        return RoboInvalidArgument;
    }
    if (base > table.size) {
        return RoboParseError;
    }
    abs = base + (size_t)offset;
    if (abs < base || abs > table.size) {
        return RoboParseError;
    }
    *outAbs = abs;
    return RoboOk;
}

void FontOtCoverageClear(FontOtCoverage *c)
{
    if (!c) {
        return;
    }
    RoboFree(c->glyphs);
    RoboFree(c->ranges);
    memset(c, 0, sizeof(*c));
}

Result FontOtCoverageParse(FontSpan table,
                                          size_t offset,
                                          FontOtCoverage *out)
{
    FontReader r;
    uint16_t format;
    uint16_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontOtCoverageClear(out);
    if (offset + 4u > table.size) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU16be(&r, &format) != RoboOk) {
        return RoboParseError;
    }
    out->format = format;

    if (format == 1u) {
        uint16_t count;
        if (FontReadU16be(&r, &count) != RoboOk) {
            return RoboParseError;
        }
        if (count > RoboFontOtMaxCoverageGlyphs) {
            return RoboParseError;
        }
        if (FontCheckedRange(table.size, FontReaderPos(&r),
                                    (size_t)count * 2u) != RoboOk) {
            return RoboParseError;
        }
        out->glyphCount = count;
        if (count == 0u) {
            return RoboOk;
        }
        out->glyphs = (uint16_t *)RoboCalloc(count, sizeof(uint16_t));
        if (!out->glyphs) {
            return RoboOutOfMemory;
        }
        for (i = 0u; i < count; i++) {
            if (FontReadU16be(&r, &out->glyphs[i]) != RoboOk) {
                FontOtCoverageClear(out);
                return RoboParseError;
            }
            if (i > 0u && out->glyphs[i] <= out->glyphs[i - 1u]) {
                FontOtCoverageClear(out);
                return RoboParseError;
            }
        }
        return RoboOk;
    }

    if (format == 2u) {
        uint16_t rangeCount;
        uint32_t covered = 0u;
        if (FontReadU16be(&r, &rangeCount) != RoboOk) {
            return RoboParseError;
        }
        if (rangeCount > RoboFontOtMaxClassRanges) {
            return RoboParseError;
        }
        if (FontCheckedRange(table.size, FontReaderPos(&r),
                                    (size_t)rangeCount * 6u) != RoboOk) {
            return RoboParseError;
        }
        out->rangeCount = rangeCount;
        if (rangeCount == 0u) {
            return RoboOk;
        }
        out->ranges = (FontOtCoverageRange *)RoboCalloc(
            rangeCount, sizeof(FontOtCoverageRange));
        if (!out->ranges) {
            return RoboOutOfMemory;
        }
        for (i = 0u; i < rangeCount; i++) {
            uint16_t startG;
            uint16_t endG;
            uint16_t startCi;
            if (FontReadU16be(&r, &startG) != RoboOk ||
                FontReadU16be(&r, &endG) != RoboOk ||
                FontReadU16be(&r, &startCi) != RoboOk) {
                FontOtCoverageClear(out);
                return RoboParseError;
            }
            if (startG > endG) {
                FontOtCoverageClear(out);
                return RoboParseError;
            }
            if (i > 0u && startG <= out->ranges[i - 1u].endGlyph) {
                FontOtCoverageClear(out);
                return RoboParseError;
            }
            out->ranges[i].startGlyph = startG;
            out->ranges[i].endGlyph = endG;
            out->ranges[i].startCoverageIndex = startCi;
            covered += (uint32_t)(endG - startG) + 1u;
            if (covered > RoboFontOtMaxCoverageGlyphs) {
                FontOtCoverageClear(out);
                return RoboParseError;
            }
        }
        out->glyphCount = (uint16_t)covered;
        return RoboOk;
    }

    /* Coverage formats are only 1 and 2; anything else is malformed. */
    return RoboParseError;
}

Result FontOtCoverageIndex(const FontOtCoverage *c,
                                          uint16_t glyphId,
                                          uint16_t *outIndex)
{
    if (!c) {
        return RoboInvalidArgument;
    }
    if (c->format == 1u) {
        uint16_t lo = 0u;
        uint16_t hi = c->glyphCount;
        while (lo < hi) {
            uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
            if (c->glyphs[mid] < glyphId) {
                lo = (uint16_t)(mid + 1u);
            } else if (c->glyphs[mid] > glyphId) {
                hi = mid;
            } else {
                if (outIndex) {
                    *outIndex = mid;
                }
                return RoboOk;
            }
        }
        return RoboNotFound;
    }
    if (c->format == 2u) {
        uint16_t i;
        for (i = 0u; i < c->rangeCount; i++) {
            const FontOtCoverageRange *rg = &c->ranges[i];
            if (glyphId < rg->startGlyph) {
                return RoboNotFound;
            }
            if (glyphId <= rg->endGlyph) {
                if (outIndex) {
                    *outIndex =
                        (uint16_t)(rg->startCoverageIndex + (glyphId - rg->startGlyph));
                }
                return RoboOk;
            }
        }
        return RoboNotFound;
    }
    return RoboNotSupported;
}

void FontOtClassDefClear(FontOtClassDef *c)
{
    if (!c) {
        return;
    }
    RoboFree(c->classValues);
    RoboFree(c->ranges);
    memset(c, 0, sizeof(*c));
}

Result FontOtClassDefParse(FontSpan table,
                                           size_t offset,
                                           FontOtClassDef *out)
{
    FontReader r;
    uint16_t format;
    uint16_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontOtClassDefClear(out);
    if (offset + 2u > table.size) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU16be(&r, &format) != RoboOk) {
        return RoboParseError;
    }
    out->format = format;

    if (format == 1u) {
        uint16_t start;
        uint16_t count;
        if (FontReadU16be(&r, &start) != RoboOk ||
            FontReadU16be(&r, &count) != RoboOk) {
            return RoboParseError;
        }
        if (count > RoboFontOtMaxCoverageGlyphs) {
            return RoboParseError;
        }
        if (FontCheckedRange(table.size, FontReaderPos(&r),
                                    (size_t)count * 2u) != RoboOk) {
            return RoboParseError;
        }
        out->startGlyph = start;
        out->glyphCount = count;
        if (count == 0u) {
            return RoboOk;
        }
        out->classValues = (uint16_t *)RoboCalloc(count, sizeof(uint16_t));
        if (!out->classValues) {
            return RoboOutOfMemory;
        }
        for (i = 0u; i < count; i++) {
            if (FontReadU16be(&r, &out->classValues[i]) != RoboOk) {
                FontOtClassDefClear(out);
                return RoboParseError;
            }
        }
        return RoboOk;
    }

    if (format == 2u) {
        uint16_t rangeCount;
        if (FontReadU16be(&r, &rangeCount) != RoboOk) {
            return RoboParseError;
        }
        if (rangeCount > RoboFontOtMaxClassRanges) {
            return RoboParseError;
        }
        if (FontCheckedRange(table.size, FontReaderPos(&r),
                                    (size_t)rangeCount * 6u) != RoboOk) {
            return RoboParseError;
        }
        out->rangeCount = rangeCount;
        if (rangeCount == 0u) {
            return RoboOk;
        }
        out->ranges = (FontOtClassRange *)RoboCalloc(
            rangeCount, sizeof(FontOtClassRange));
        if (!out->ranges) {
            return RoboOutOfMemory;
        }
        for (i = 0u; i < rangeCount; i++) {
            if (FontReadU16be(&r, &out->ranges[i].startGlyph) != RoboOk ||
                FontReadU16be(&r, &out->ranges[i].endGlyph) != RoboOk ||
                FontReadU16be(&r, &out->ranges[i].classValue) != RoboOk) {
                FontOtClassDefClear(out);
                return RoboParseError;
            }
            if (out->ranges[i].startGlyph > out->ranges[i].endGlyph) {
                FontOtClassDefClear(out);
                return RoboParseError;
            }
            if (i > 0u &&
                out->ranges[i].startGlyph <= out->ranges[i - 1u].endGlyph) {
                FontOtClassDefClear(out);
                return RoboParseError;
            }
        }
        return RoboOk;
    }

    /* ClassDef formats are only 1 and 2; anything else is malformed. */
    return RoboParseError;
}

Result FontOtClassDefGet(const FontOtClassDef *c,
                                         uint16_t glyphId,
                                         uint16_t *outClass)
{
    if (!c || !outClass) {
        return RoboInvalidArgument;
    }
    *outClass = 0u;
    if (c->format == 1u) {
        if (glyphId < c->startGlyph) {
            return RoboOk;
        }
        {
            uint32_t idx = (uint32_t)glyphId - (uint32_t)c->startGlyph;
            if (idx >= c->glyphCount) {
                return RoboOk;
            }
            *outClass = c->classValues[idx];
        }
        return RoboOk;
    }
    if (c->format == 2u) {
        uint16_t i;
        for (i = 0u; i < c->rangeCount; i++) {
            if (glyphId < c->ranges[i].startGlyph) {
                break;
            }
            if (glyphId <= c->ranges[i].endGlyph) {
                *outClass = c->ranges[i].classValue;
                break;
            }
        }
        return RoboOk;
    }
    return RoboNotSupported;
}

void FontOtDeviceClear(FontOtDevice *d)
{
    if (!d) {
        return;
    }
    RoboFree(d->deltaValues);
    memset(d, 0, sizeof(*d));
}

Result FontOtDeviceParse(FontSpan table,
                                        size_t offset,
                                        FontOtDevice *out)
{
    FontReader r;
    uint16_t nSizes;
    uint16_t words;
    uint16_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontOtDeviceClear(out);
    if (offset + 6u > table.size) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU16be(&r, &out->startSize) != RoboOk ||
        FontReadU16be(&r, &out->endSize) != RoboOk ||
        FontReadU16be(&r, &out->deltaFormat) != RoboOk) {
        return RoboParseError;
    }
    if (out->deltaFormat == 0x8000u) {
        /* VariationIndex: two more uint16 (outer/inner) — already read format. */
        if (FontReaderPos(&r) + 4u > table.size) {
            return RoboParseError;
        }
        out->deltaValueCount = 2u;
        out->deltaValues = (uint16_t *)RoboCalloc(2u, sizeof(uint16_t));
        if (!out->deltaValues) {
            return RoboOutOfMemory;
        }
        if (FontReadU16be(&r, &out->deltaValues[0]) != RoboOk ||
            FontReadU16be(&r, &out->deltaValues[1]) != RoboOk) {
            FontOtDeviceClear(out);
            return RoboParseError;
        }
        return RoboOk;
    }
    if (out->deltaFormat < 1u || out->deltaFormat > 3u) {
        return RoboNotSupported;
    }
    if (out->startSize > out->endSize) {
        return RoboParseError;
    }
    nSizes = (uint16_t)(out->endSize - out->startSize + 1u);
    /* Packed: format 1 → 8/uint16, 2 → 4/uint16, 3 → 2/uint16 */
    {
        uint16_t perWord = (uint16_t)(1u << (4u - out->deltaFormat));
        words = (uint16_t)((nSizes + perWord - 1u) / perWord);
    }
    if (words > 4096u) {
        return RoboParseError;
    }
    if (FontCheckedRange(table.size, FontReaderPos(&r),
                                (size_t)words * 2u) != RoboOk) {
        return RoboParseError;
    }
    out->deltaValueCount = words;
    out->deltaValues = (uint16_t *)RoboCalloc(words == 0u ? 1u : words, sizeof(uint16_t));
    if (!out->deltaValues) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < words; i++) {
        if (FontReadU16be(&r, &out->deltaValues[i]) != RoboOk) {
            FontOtDeviceClear(out);
            return RoboParseError;
        }
    }
    return RoboOk;
}

static void LangSysClear(FontOtLangSys *ls)
{
    if (!ls) {
        return;
    }
    RoboFree(ls->featureIndices);
    memset(ls, 0, sizeof(*ls));
}

static Result LangSysParse(FontSpan table,
                                    size_t offset,
                                    FontOtLangSys *out)
{
    FontReader r;
    uint16_t i;
    uint16_t count;

    memset(out, 0, sizeof(*out));
    if (offset + 6u > table.size) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU16be(&r, &out->lookupOrder) != RoboOk ||
        FontReadU16be(&r, &out->requiredFeatureIndex) != RoboOk ||
        FontReadU16be(&r, &count) != RoboOk) {
        return RoboParseError;
    }
    if (count > RoboFontOtMaxFeatureLookups) {
        return RoboParseError;
    }
    out->featureIndexCount = count;
    if (FontCheckedRange(table.size, FontReaderPos(&r),
                                (size_t)count * 2u) != RoboOk) {
        return RoboParseError;
    }
    if (count == 0u) {
        return RoboOk;
    }
    out->featureIndices = (uint16_t *)RoboCalloc(count, sizeof(uint16_t));
    if (!out->featureIndices) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < count; i++) {
        if (FontReadU16be(&r, &out->featureIndices[i]) != RoboOk) {
            LangSysClear(out);
            return RoboParseError;
        }
    }
    return RoboOk;
}

void FontOtScriptListClear(FontOtScriptList *sl)
{
    uint16_t i;
    uint16_t j;
    if (!sl) {
        return;
    }
    if (sl->scripts) {
        for (i = 0u; i < sl->scriptCount; i++) {
            LangSysClear(&sl->scripts[i].defaultLangSys);
            if (sl->scripts[i].langSysRecords) {
                for (j = 0u; j < sl->scripts[i].langSysCount; j++) {
                    LangSysClear(&sl->scripts[i].langSysRecords[j].langSys);
                }
                RoboFree(sl->scripts[i].langSysRecords);
            }
        }
        RoboFree(sl->scripts);
    }
    memset(sl, 0, sizeof(*sl));
}

Result FontOtScriptListParse(FontSpan table,
                                             size_t offset,
                                             FontOtScriptList *out)
{
    FontReader r;
    uint16_t count;
    uint16_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontOtScriptListClear(out);
    if (offset + 2u > table.size) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU16be(&r, &count) != RoboOk) {
        return RoboParseError;
    }
    if (count > RoboFontOtMaxScripts) {
        return RoboParseError;
    }
    if (FontCheckedRange(table.size, FontReaderPos(&r),
                                (size_t)count * 6u) != RoboOk) {
        return RoboParseError;
    }
    out->scriptCount = count;
    if (count == 0u) {
        return RoboOk;
    }
    out->scripts = (FontOtScript *)RoboCalloc(count, sizeof(FontOtScript));
    if (!out->scripts) {
        return RoboOutOfMemory;
    }

    for (i = 0u; i < count; i++) {
        uint32_t tag;
        uint16_t scriptOff;
        size_t scriptAbs;
        uint16_t defaultOff;
        uint16_t langCount;
        uint16_t j;
        FontReader sr;

        if (FontReadU32be(&r, &tag) != RoboOk ||
            FontReadU16be(&r, &scriptOff) != RoboOk) {
            FontOtScriptListClear(out);
            return RoboParseError;
        }
        if (FontOtResolveOffset16(table, offset, scriptOff, &scriptAbs) !=
            RoboOk) {
            FontOtScriptListClear(out);
            return RoboParseError;
        }
        out->scripts[i].scriptTag = tag;
        out->scripts[i].scriptOffset = scriptAbs;

        if (scriptAbs + 4u > table.size) {
            FontOtScriptListClear(out);
            return RoboParseError;
        }
        FontReaderInitSpan(&sr, table);
        if (FontReaderSeek(&sr, scriptAbs) != RoboOk ||
            FontReadU16be(&sr, &defaultOff) != RoboOk ||
            FontReadU16be(&sr, &langCount) != RoboOk) {
            FontOtScriptListClear(out);
            return RoboParseError;
        }
        if (langCount > RoboFontOtMaxLangsys) {
            FontOtScriptListClear(out);
            return RoboParseError;
        }
        if (defaultOff != 0u) {
            size_t dabs;
            if (FontOtResolveOffset16(table, scriptAbs, defaultOff, &dabs) !=
                    RoboOk ||
                LangSysParse(table, dabs, &out->scripts[i].defaultLangSys) != RoboOk) {
                FontOtScriptListClear(out);
                return RoboParseError;
            }
            out->scripts[i].hasDefaultLangSys = 1u;
        }
        out->scripts[i].langSysCount = langCount;
        if (FontCheckedRange(table.size, FontReaderPos(&sr),
                                    (size_t)langCount * 6u) != RoboOk) {
            FontOtScriptListClear(out);
            return RoboParseError;
        }
        if (langCount == 0u) {
            continue;
        }
        out->scripts[i].langSysRecords = (FontOtLangSysRecord *)RoboCalloc(
            langCount, sizeof(FontOtLangSysRecord));
        if (!out->scripts[i].langSysRecords) {
            FontOtScriptListClear(out);
            return RoboOutOfMemory;
        }
        for (j = 0u; j < langCount; j++) {
            uint32_t ltag;
            uint16_t loff;
            size_t labs;
            if (FontReadU32be(&sr, &ltag) != RoboOk ||
                FontReadU16be(&sr, &loff) != RoboOk) {
                FontOtScriptListClear(out);
                return RoboParseError;
            }
            if (FontOtResolveOffset16(table, scriptAbs, loff, &labs) != RoboOk ||
                LangSysParse(table, labs,
                               &out->scripts[i].langSysRecords[j].langSys) != RoboOk) {
                FontOtScriptListClear(out);
                return RoboParseError;
            }
            out->scripts[i].langSysRecords[j].langTag = ltag;
            out->scripts[i].langSysRecords[j].langSysOffset = labs;
        }
    }
    return RoboOk;
}

void FontOtFeatureListClear(FontOtFeatureList *fl)
{
    uint16_t i;
    if (!fl) {
        return;
    }
    if (fl->features) {
        for (i = 0u; i < fl->featureCount; i++) {
            RoboFree(fl->features[i].lookupIndices);
        }
        RoboFree(fl->features);
    }
    memset(fl, 0, sizeof(*fl));
}

Result FontOtFeatureListParse(FontSpan table,
                                              size_t offset,
                                              FontOtFeatureList *out)
{
    FontReader r;
    uint16_t count;
    uint16_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontOtFeatureListClear(out);
    if (offset + 2u > table.size) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU16be(&r, &count) != RoboOk) {
        return RoboParseError;
    }
    if (count > RoboFontOtMaxFeatures) {
        return RoboParseError;
    }
    if (FontCheckedRange(table.size, FontReaderPos(&r),
                                (size_t)count * 6u) != RoboOk) {
        return RoboParseError;
    }
    out->featureCount = count;
    if (count == 0u) {
        return RoboOk;
    }
    out->features =
        (FontOtFeature *)RoboCalloc(count, sizeof(FontOtFeature));
    if (!out->features) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < count; i++) {
        uint32_t tag;
        uint16_t foff;
        size_t fabs;
        FontReader fr;
        uint16_t params;
        uint16_t lcount;
        uint16_t j;

        if (FontReadU32be(&r, &tag) != RoboOk ||
            FontReadU16be(&r, &foff) != RoboOk) {
            FontOtFeatureListClear(out);
            return RoboParseError;
        }
        if (FontOtResolveOffset16(table, offset, foff, &fabs) != RoboOk) {
            FontOtFeatureListClear(out);
            return RoboParseError;
        }
        out->features[i].featureTag = tag;
        out->features[i].featureOffset = fabs;
        if (fabs + 4u > table.size) {
            FontOtFeatureListClear(out);
            return RoboParseError;
        }
        FontReaderInitSpan(&fr, table);
        if (FontReaderSeek(&fr, fabs) != RoboOk ||
            FontReadU16be(&fr, &params) != RoboOk ||
            FontReadU16be(&fr, &lcount) != RoboOk) {
            FontOtFeatureListClear(out);
            return RoboParseError;
        }
        if (lcount > RoboFontOtMaxFeatureLookups) {
            FontOtFeatureListClear(out);
            return RoboParseError;
        }
        out->features[i].featureParamsOffset = params;
        out->features[i].lookupIndexCount = lcount;
        if (FontCheckedRange(table.size, FontReaderPos(&fr),
                                    (size_t)lcount * 2u) != RoboOk) {
            FontOtFeatureListClear(out);
            return RoboParseError;
        }
        if (lcount == 0u) {
            continue;
        }
        out->features[i].lookupIndices =
            (uint16_t *)RoboCalloc(lcount, sizeof(uint16_t));
        if (!out->features[i].lookupIndices) {
            FontOtFeatureListClear(out);
            return RoboOutOfMemory;
        }
        for (j = 0u; j < lcount; j++) {
            if (FontReadU16be(&fr, &out->features[i].lookupIndices[j]) != RoboOk) {
                FontOtFeatureListClear(out);
                return RoboParseError;
            }
        }
    }
    return RoboOk;
}

void FontOtLookupListClear(FontOtLookupList *ll)
{
    uint16_t i;
    if (!ll) {
        return;
    }
    if (ll->lookups) {
        for (i = 0u; i < ll->lookupCount; i++) {
            RoboFree(ll->lookups[i].subtables);
        }
        RoboFree(ll->lookups);
    }
    memset(ll, 0, sizeof(*ll));
}

Result FontOtLookupListParse(FontSpan table,
                                             size_t offset,
                                             FontOtLookupList *out)
{
    FontReader r;
    uint16_t count;
    uint16_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontOtLookupListClear(out);
    if (offset + 2u > table.size) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, table);
    if (FontReaderSeek(&r, offset) != RoboOk ||
        FontReadU16be(&r, &count) != RoboOk) {
        return RoboParseError;
    }
    if (count > RoboFontOtMaxLookups) {
        return RoboParseError;
    }
    if (FontCheckedRange(table.size, FontReaderPos(&r),
                                (size_t)count * 2u) != RoboOk) {
        return RoboParseError;
    }
    out->lookupCount = count;
    if (count == 0u) {
        return RoboOk;
    }
    out->lookups =
        (FontOtLookup *)RoboCalloc(count, sizeof(FontOtLookup));
    if (!out->lookups) {
        return RoboOutOfMemory;
    }

    for (i = 0u; i < count; i++) {
        uint16_t loff;
        size_t labs;
        FontReader lr;
        uint16_t type;
        uint16_t flags;
        uint16_t scount;
        uint16_t j;

        if (FontReadU16be(&r, &loff) != RoboOk) {
            FontOtLookupListClear(out);
            return RoboParseError;
        }
        if (FontOtResolveOffset16(table, offset, loff, &labs) != RoboOk) {
            FontOtLookupListClear(out);
            return RoboParseError;
        }
        out->lookups[i].lookupOffset = labs;
        if (labs + 6u > table.size) {
            FontOtLookupListClear(out);
            return RoboParseError;
        }
        FontReaderInitSpan(&lr, table);
        if (FontReaderSeek(&lr, labs) != RoboOk ||
            FontReadU16be(&lr, &type) != RoboOk ||
            FontReadU16be(&lr, &flags) != RoboOk ||
            FontReadU16be(&lr, &scount) != RoboOk) {
            FontOtLookupListClear(out);
            return RoboParseError;
        }
        if (scount == 0u || scount > RoboFontOtMaxSubtables) {
            FontOtLookupListClear(out);
            return RoboParseError;
        }
        out->lookups[i].lookupType = type;
        out->lookups[i].lookupFlag = flags;
        out->lookups[i].subtableCount = scount;
        if (FontCheckedRange(table.size, FontReaderPos(&lr),
                                    (size_t)scount * 2u) != RoboOk) {
            FontOtLookupListClear(out);
            return RoboParseError;
        }
        out->lookups[i].subtables = (FontOtLookupSubtable *)RoboCalloc(
            scount, sizeof(FontOtLookupSubtable));
        if (!out->lookups[i].subtables) {
            FontOtLookupListClear(out);
            return RoboOutOfMemory;
        }
        for (j = 0u; j < scount; j++) {
            uint16_t soff;
            size_t sabs;
            if (FontReadU16be(&lr, &soff) != RoboOk) {
                FontOtLookupListClear(out);
                return RoboParseError;
            }
            if (FontOtResolveOffset16(table, labs, soff, &sabs) != RoboOk) {
                FontOtLookupListClear(out);
                return RoboParseError;
            }
            out->lookups[i].subtables[j].offset = sabs;
            out->lookups[i].subtables[j].lengthHint =
                (uint32_t)(table.size - sabs);
        }
        if ((flags & RoboFontOtLookupFlagUseMarkFilteringSet) != 0u) {
            if (FontReadU16be(&lr, &out->lookups[i].markFilteringSet) != RoboOk) {
                FontOtLookupListClear(out);
                return RoboParseError;
            }
        }
    }
    return RoboOk;
}
