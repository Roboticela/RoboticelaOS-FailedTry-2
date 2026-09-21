#include "RoboFont/RoboFontOt.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

Result FontBaseParse(FontSpan span, FontBase *out)
{
    FontReader r;
    uint16_t major;
    uint16_t minor;
    uint16_t horiz;
    uint16_t vert;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 8u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &major) != RoboOk ||
        FontReadU16be(&r, &minor) != RoboOk ||
        FontReadU16be(&r, &horiz) != RoboOk ||
        FontReadU16be(&r, &vert) != RoboOk) {
        return RoboParseError;
    }
    if (major != 1u || minor > 1u) {
        return RoboNotSupported;
    }
    out->majorVersion = major;
    out->minorVersion = minor;
    out->table = span;
    if (horiz != 0u) {
        size_t abs;
        if (FontOtResolveOffset16(span, 0u, horiz, &abs) != RoboOk ||
            abs + 4u > span.size) {
            return RoboParseError;
        }
        out->horizAxisOffset = abs;
        out->hasHoriz = 1u;
    }
    if (vert != 0u) {
        size_t abs;
        if (FontOtResolveOffset16(span, 0u, vert, &abs) != RoboOk ||
            abs + 4u > span.size) {
            return RoboParseError;
        }
        out->vertAxisOffset = abs;
        out->hasVert = 1u;
    }
    if (minor >= 1u) {
        uint32_t itemVar;
        if (span.size < 12u || FontReadU32be(&r, &itemVar) != RoboOk) {
            return RoboParseError;
        }
        if (itemVar != 0u) {
            size_t abs;
            if (FontOtResolveOffset32(span, 0u, itemVar, &abs) != RoboOk) {
                return RoboParseError;
            }
            out->itemVarStoreOffset = abs;
        }
    }
    return RoboOk;
}

Result FontJstfParse(FontSpan span, FontJstf *out)
{
    FontReader r;
    uint16_t major;
    uint16_t minor;
    uint16_t count;
    uint16_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 6u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &major) != RoboOk ||
        FontReadU16be(&r, &minor) != RoboOk ||
        FontReadU16be(&r, &count) != RoboOk) {
        return RoboParseError;
    }
    if (major != 1u || minor != 0u) {
        return RoboNotSupported;
    }
    if (count > RoboFontOtMaxScripts) {
        return RoboParseError;
    }
    if (FontCheckedRange(span.size, 6u, (size_t)count * 6u) != RoboOk) {
        return RoboParseError;
    }
    out->majorVersion = major;
    out->minorVersion = minor;
    out->scriptCount = count;
    out->table = span;
    for (i = 0u; i < count; i++) {
        uint32_t tag;
        uint16_t soff;
        size_t abs;
        (void)tag;
        if (FontReadU32be(&r, &tag) != RoboOk ||
            FontReadU16be(&r, &soff) != RoboOk) {
            return RoboParseError;
        }
        if (FontOtResolveOffset16(span, 0u, soff, &abs) != RoboOk ||
            abs + 2u > span.size) {
            return RoboParseError;
        }
    }
    return RoboOk;
}

Result FontMathParse(FontSpan span, FontMath *out)
{
    FontReader r;
    uint32_t version;
    uint16_t cOff;
    uint16_t gOff;
    uint16_t vOff;
    size_t abs;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 10u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU32be(&r, &version) != RoboOk ||
        FontReadU16be(&r, &cOff) != RoboOk ||
        FontReadU16be(&r, &gOff) != RoboOk ||
        FontReadU16be(&r, &vOff) != RoboOk) {
        return RoboParseError;
    }
    if (version != 0x00010000u) {
        return RoboNotSupported;
    }
    out->version = version;
    out->table = span;
    if (FontOtResolveOffset16(span, 0u, cOff, &abs) != RoboOk) {
        return RoboParseError;
    }
    out->mathConstantsOffset = abs;
    if (FontOtResolveOffset16(span, 0u, gOff, &abs) != RoboOk) {
        return RoboParseError;
    }
    out->mathGlyphInfoOffset = abs;
    if (FontOtResolveOffset16(span, 0u, vOff, &abs) != RoboOk) {
        return RoboParseError;
    }
    out->mathVariantsOffset = abs;
    return RoboOk;
}

void FontKernClear(FontKern *k)
{
    if (!k) {
        return;
    }
    RoboFree(k->subtables);
    memset(k, 0, sizeof(*k));
}

Result FontKernParse(FontSpan span, FontKern *out)
{
    FontReader r;
    uint16_t version;
    uint16_t nTables;
    uint16_t i;
    size_t cursor;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontKernClear(out);
    if (span.size < 4u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &version) != RoboOk ||
        FontReadU16be(&r, &nTables) != RoboOk) {
        return RoboParseError;
    }
    /* Apple format uses uint32 version; Windows uses uint16. Support Windows v0. */
    if (version != 0u) {
        return RoboNotSupported;
    }
    if (nTables > 256u) {
        return RoboParseError;
    }
    out->version = version;
    out->nTables = nTables;
    out->table = span;
    if (nTables == 0u) {
        return RoboOk;
    }
    out->subtables =
        (FontKernSubtable *)RoboCalloc(nTables, sizeof(FontKernSubtable));
    if (!out->subtables) {
        return RoboOutOfMemory;
    }
    cursor = 4u;
    for (i = 0u; i < nTables; i++) {
        uint16_t sv;
        uint16_t len;
        uint16_t cov;
        if (cursor + 6u > span.size) {
            FontKernClear(out);
            return RoboParseError;
        }
        if (FontReaderSeek(&r, cursor) != RoboOk ||
            FontReadU16be(&r, &sv) != RoboOk ||
            FontReadU16be(&r, &len) != RoboOk ||
            FontReadU16be(&r, &cov) != RoboOk) {
            FontKernClear(out);
            return RoboParseError;
        }
        if (len < 6u || cursor + len > span.size) {
            FontKernClear(out);
            return RoboParseError;
        }
        out->subtables[i].version = sv;
        out->subtables[i].length = len;
        out->subtables[i].coverage = cov;
        out->subtables[i].format = (uint16_t)(cov >> 8);
        out->subtables[i].offset = cursor;
        cursor += len;
    }
    return RoboOk;
}

Result FontCpalParse(FontSpan span, FontCpal *out)
{
    FontReader r;
    uint16_t version;
    uint16_t numEntries;
    uint16_t numPalettes;
    uint16_t numColors;
    uint32_t colorOff;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 10u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &version) != RoboOk ||
        FontReadU16be(&r, &numEntries) != RoboOk ||
        FontReadU16be(&r, &numPalettes) != RoboOk ||
        FontReadU16be(&r, &numColors) != RoboOk ||
        FontReadU32be(&r, &colorOff) != RoboOk) {
        return RoboParseError;
    }
    if (version > 1u) {
        return RoboNotSupported;
    }
    if (numPalettes > 1024u || numColors > 65535u || numEntries > 65535u) {
        return RoboParseError;
    }
    if (FontCheckedRange(span.size, 12u, (size_t)numPalettes * 2u) != RoboOk) {
        return RoboParseError;
    }
    {
        size_t abs;
        if (FontOtResolveOffset32(span, 0u, colorOff, &abs) != RoboOk) {
            return RoboParseError;
        }
        if (FontCheckedRange(span.size, abs, (size_t)numColors * 4u) != RoboOk) {
            return RoboParseError;
        }
        out->colorRecordsArrayOffset = abs;
    }
    out->version = version;
    out->numPalettesEntries = numEntries;
    out->numPalette = numPalettes;
    out->numColorRecords = numColors;
    out->table = span;
    return RoboOk;
}

Result FontColrParse(FontSpan span, FontColr *out)
{
    FontReader r;
    uint16_t version;
    uint16_t numBase;
    uint32_t baseOff;
    uint32_t layerOff;
    uint16_t numLayer;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    if (span.size < 14u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &version) != RoboOk ||
        FontReadU16be(&r, &numBase) != RoboOk ||
        FontReadU32be(&r, &baseOff) != RoboOk ||
        FontReadU32be(&r, &layerOff) != RoboOk ||
        FontReadU16be(&r, &numLayer) != RoboOk) {
        return RoboParseError;
    }
    if (version > 1u) {
        return RoboNotSupported;
    }
    if (numBase > 65535u || numLayer > 65535u) {
        return RoboParseError;
    }
    {
        size_t abs;
        if (FontOtResolveOffset32(span, 0u, baseOff, &abs) != RoboOk) {
            return RoboParseError;
        }
        /* BaseGlyphRecord: glyphID + firstLayerIndex + numLayers = 6 bytes */
        if (FontCheckedRange(span.size, abs, (size_t)numBase * 6u) != RoboOk) {
            return RoboParseError;
        }
        out->baseGlyphRecordsOffset = abs;
        if (FontOtResolveOffset32(span, 0u, layerOff, &abs) != RoboOk) {
            return RoboParseError;
        }
        /* LayerRecord: glyphID + paletteIndex = 4 bytes */
        if (FontCheckedRange(span.size, abs, (size_t)numLayer * 4u) != RoboOk) {
            return RoboParseError;
        }
        out->layerRecordsOffset = abs;
    }
    out->version = version;
    out->numBaseGlyphRecords = numBase;
    out->numLayerRecords = numLayer;
    out->table = span;
    return RoboOk;
}

void FontStatClear(FontStat *s)
{
    if (!s) {
        return;
    }
    RoboFree(s->axes);
    memset(s, 0, sizeof(*s));
}

Result FontStatParse(FontSpan span, FontStat *out)
{
    FontReader r;
    uint16_t major;
    uint16_t minor;
    uint16_t axisSize;
    uint16_t axisCount;
    uint32_t axesOff;
    uint16_t valueCount;
    uint32_t valuesOff;
    uint16_t i;

    if (!out) {
        return RoboInvalidArgument;
    }
    FontStatClear(out);
    if (span.size < 20u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &major) != RoboOk ||
        FontReadU16be(&r, &minor) != RoboOk ||
        FontReadU16be(&r, &axisSize) != RoboOk ||
        FontReadU16be(&r, &axisCount) != RoboOk ||
        FontReadU32be(&r, &axesOff) != RoboOk ||
        FontReadU16be(&r, &valueCount) != RoboOk ||
        FontReadU32be(&r, &valuesOff) != RoboOk ||
        FontReadU16be(&r, &out->elidedFallbackNameId) != RoboOk) {
        return RoboParseError;
    }
    if (major != 1u || minor > 2u) {
        return RoboNotSupported;
    }
    if (axisSize < 8u || axisCount > 64u || valueCount > 4096u) {
        return RoboParseError;
    }
    {
        size_t abs;
        if (FontOtResolveOffset32(span, 0u, axesOff, &abs) != RoboOk) {
            return RoboParseError;
        }
        if (FontCheckedRange(span.size, abs, (size_t)axisCount * axisSize) !=
            RoboOk) {
            return RoboParseError;
        }
        out->designAxesOffset = abs;
        if (valuesOff != 0u) {
            if (FontOtResolveOffset32(span, 0u, valuesOff, &abs) != RoboOk) {
                return RoboParseError;
            }
            out->offsetToAxisValues = abs;
        }
    }
    out->majorVersion = major;
    out->minorVersion = minor;
    out->designAxisSize = axisSize;
    out->designAxisCount = axisCount;
    out->axisValueCount = valueCount;
    out->table = span;

    if (axisCount == 0u) {
        return RoboOk;
    }
    out->axes =
        (FontStatAxis *)RoboCalloc(axisCount, sizeof(FontStatAxis));
    if (!out->axes) {
        return RoboOutOfMemory;
    }
    for (i = 0u; i < axisCount; i++) {
        size_t aoff = out->designAxesOffset + (size_t)i * axisSize;
        FontReader ar;
        uint32_t tag;
        uint16_t nameId;
        uint16_t ordering;
        FontReaderInitSpan(&ar, span);
        if (FontReaderSeek(&ar, aoff) != RoboOk ||
            FontReadU32be(&ar, &tag) != RoboOk ||
            FontReadU16be(&ar, &nameId) != RoboOk ||
            FontReadU16be(&ar, &ordering) != RoboOk) {
            FontStatClear(out);
            return RoboParseError;
        }
        out->axes[i].tag = tag;
        out->axes[i].nameId = nameId;
        out->axes[i].ordering = ordering;
    }
    return RoboOk;
}
