/*
 * RoboFontColor — OpenType color fonts (Goal 5.19).
 */

#include "RoboFont/RoboFontColor.h"

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define INFO_COOKIE 0x434C5231u /* 'CLR1' */
#define NODE_NONE 0xFFFFFFFFu

typedef struct FontColorColrV0Base {
    uint16_t glyphId;
    uint16_t firstLayer;
    uint16_t numLayers;
} FontColorColrV0Base;

typedef struct FontColorColrV1Base {
    uint16_t glyphId;
    uint32_t paintOffset;
} FontColorColrV1Base;

typedef struct FontColorSbixStrike {
    uint16_t ppemX;
    uint16_t ppemY;
    size_t strikeOffset;
} FontColorSbixStrike;

typedef struct FontColorCblcStrike {
    uint32_t indexSubtableArrayOffset;
    uint32_t indexTablesSize;
    uint32_t numIndexSubtables;
    uint16_t startGlyph;
    uint16_t endGlyph;
    uint16_t ppemX;
    uint16_t ppemY;
    uint8_t bitDepth;
    uint8_t flags;
} FontColorCblcStrike;

typedef struct FontColorPaintCache {
    GlyphId glyphId;
    uint32_t rootIndex;
    uint32_t nodeCount;
    uint8_t building;
    uint8_t hasVariation;
    FontColorPaint *nodes;
} FontColorPaintCache;

struct FontColorInfo {
    uint32_t cookie;
    uint32_t refs;
    uint32_t capabilities;
    uint16_t colrVersion;
    uint32_t numGlyphs;

    FontSfnt *sfnt;
    const FontVariationCoords *coords;

    FontSpan cpalTable;
    uint16_t cpalVersion;
    uint16_t numPaletteEntries;
    uint16_t paletteCount;
    uint16_t numColorRecords;
    size_t colorRecordsOffset;
    FontColorPalette *palettes;
    FontColorRgba *colors;

    FontSpan colrTable;
    uint16_t colrNumLayers;
    size_t colrLayerListOffset;
    uint32_t colrV1LayerCount;
    size_t colrItemVarStoreOffset;

    FontColorColrV0Base *colrV0Bases;
    uint32_t colrV0BaseCount;
    FontColorGlyphLayer *colrV0Layers;
    uint32_t colrV0LayerCount;

    FontColorColrV1Base *colrV1Bases;
    uint32_t colrV1BaseCount;

    FontSpan sbixTable;
    uint16_t sbixStrikeCount;
    FontColorSbixStrike *sbixStrikes;

    FontSpan cblcTable;
    FontSpan cbdtTable;
    uint16_t cblcStrikeCount;
    FontColorCblcStrike *cblcStrikes;

    FontSpan svgTable;
    FontColorGlyphSvg *svgDocs;
    uint32_t svgDocCount;

    FontColorPaintCache paintCache[256u];
    uint32_t paintCacheCount;
};

typedef struct FontColorPaintBuilder {
    FontColorInfo *info;
    FontSpan colr;
    FontColorPaint *nodes;
    uint32_t nodeCount;
    uint32_t visitedOffsets[RoboFontColorMaxPaintNodes];
    uint32_t visitedCount;
    GlyphId colrGlyphStack[RoboFontColorMaxPaintDepth];
    uint32_t colrGlyphDepth;
    uint8_t depth;
    uint8_t hasVariation;
    Result error;
} FontColorPaintBuilder;

static uint8_t F2dot14AlphaHi(int16_t alpha)
{
    return (uint8_t)(((uint16_t)alpha >> 8) & 0xFFu);
}

static uint8_t F2dot14AlphaToU8(int16_t alpha)
{
    int32_t v = (int32_t)alpha;
    if (v <= 0) {
        return 0u;
    }
    if (v >= 16384) {
        return 255u;
    }
    return (uint8_t)((v * 255 + 8192) / 16384);
}

static Result ReadOffset24(FontSpan table, size_t at, size_t *outAbs)
{
    uint32_t off;

    if (!outAbs) {
        return RoboInvalidArgument;
    }
    if (FontCheckedRange(table.size, at, 3u) != RoboOk) {
        return RoboParseError;
    }
    off = ((uint32_t)table.data[at] << 16) | ((uint32_t)table.data[at + 1u] << 8) |
          (uint32_t)table.data[at + 2u];
    return FontOtResolveOffset32(table, 0u, off, outAbs);
}

static int SvgHasExternalRefHint(const uint8_t *data, size_t size)
{
    size_t i;
    static const char kHttp[] = "http://";
    static const char kXlink[] = "xlink:href";
    static const char kHref[] = "href=";

    if (!data || size == 0u) {
        return 0;
    }
    for (i = 0u; i + 7u <= size; i++) {
        if (memcmp(data + i, kHttp, 7u) == 0) {
            return 1;
        }
    }
    for (i = 0u; i + 10u <= size; i++) {
        if (memcmp(data + i, kXlink, 10u) == 0) {
            return 1;
        }
    }
    for (i = 0u; i + 5u <= size; i++) {
        if (memcmp(data + i, kHref, 5u) == 0) {
            return 1;
        }
    }
    return 0;
}

static int32_t CmpU16(uint16_t a, uint16_t b)
{
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

static const FontColorColrV0Base *ColrV0BaseFind(
    const FontColorInfo *info,
    GlyphId gid)
{
    uint32_t lo = 0u;
    uint32_t hi;
    if (!info || gid > 0xFFFFu || info->colrV0BaseCount == 0u) {
        return NULL;
    }
    hi = info->colrV0BaseCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const FontColorColrV0Base *b = &info->colrV0Bases[mid];
        int32_t c = CmpU16(b->glyphId, (uint16_t)gid);
        if (c == 0) {
            return b;
        }
        if (c < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

static const FontColorColrV1Base *ColrV1BaseFind(
    const FontColorInfo *info,
    GlyphId gid)
{
    uint32_t lo = 0u;
    uint32_t hi;
    if (!info || gid > 0xFFFFu || info->colrV1BaseCount == 0u) {
        return NULL;
    }
    hi = info->colrV1BaseCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const FontColorColrV1Base *b = &info->colrV1Bases[mid];
        int32_t c = CmpU16(b->glyphId, (uint16_t)gid);
        if (c == 0) {
            return b;
        }
        if (c < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

static FontColorPaintCache *PaintCacheFind(
    FontColorInfo *info,
    GlyphId gid)
{
    uint32_t i;
    if (!info) {
        return NULL;
    }
    for (i = 0u; i < info->paintCacheCount; i++) {
        if (info->paintCache[i].glyphId == gid) {
            return &info->paintCache[i];
        }
    }
    return NULL;
}

static FontColorPaintCache *PaintCacheAlloc(
    FontColorInfo *info,
    GlyphId gid)
{
    FontColorPaintCache *e;
    if (!info || info->paintCacheCount >= 256u) {
        return NULL;
    }
    e = &info->paintCache[info->paintCacheCount++];
    memset(e, 0, sizeof(*e));
    e->glyphId = gid;
    e->rootIndex = NODE_NONE;
    return e;
}

static void ClearCpalState(FontColorInfo *info)
{
    if (!info) {
        return;
    }
    RoboFree(info->palettes);
    RoboFree(info->colors);
    info->palettes = NULL;
    info->colors = NULL;
    info->paletteCount = 0u;
    info->numPaletteEntries = 0u;
    info->numColorRecords = 0u;
    info->colorRecordsOffset = 0u;
    info->cpalVersion = 0u;
    memset(&info->cpalTable, 0, sizeof(info->cpalTable));
}

static void ClearColrState(FontColorInfo *info)
{
    if (!info) {
        return;
    }
    RoboFree(info->colrV0Bases);
    RoboFree(info->colrV0Layers);
    RoboFree(info->colrV1Bases);
    info->colrV0Bases = NULL;
    info->colrV0Layers = NULL;
    info->colrV1Bases = NULL;
    info->colrV0BaseCount = 0u;
    info->colrV0LayerCount = 0u;
    info->colrV1BaseCount = 0u;
    info->colrNumLayers = 0u;
    info->colrVersion = 0u;
    info->colrLayerListOffset = 0u;
    info->colrV1LayerCount = 0u;
    info->colrItemVarStoreOffset = 0u;
    memset(&info->colrTable, 0, sizeof(info->colrTable));
}

static void InfoFreeOwned(FontColorInfo *info)
{
    uint32_t i;
    if (!info) {
        return;
    }
    RoboFree(info->palettes);
    RoboFree(info->colors);
    RoboFree(info->colrV0Bases);
    RoboFree(info->colrV0Layers);
    RoboFree(info->colrV1Bases);
    RoboFree(info->sbixStrikes);
    RoboFree(info->cblcStrikes);
    RoboFree(info->svgDocs);
    for (i = 0u; i < info->paintCacheCount; i++) {
        RoboFree(info->paintCache[i].nodes);
    }
    if (info->coords) {
        FontVariationCoordsRelease((FontVariationCoords *)info->coords);
    }
    if (info->sfnt) {
        FontSfntRelease(info->sfnt);
    }
    memset(info, 0, sizeof(*info));
}

static Result ParseCpal(FontColorInfo *info, FontSpan span)
{
    FontReader r;
    FontCpal cpal;
    uint16_t i;
    size_t paletteIndicesOff = 12u;
    Result res;

    if (!info) {
        return RoboInvalidArgument;
    }
    res = FontCpalParse(span, &cpal);
    if (res != RoboOk) {
        return res;
    }
    if (cpal.numPalette > RoboFontColorMaxPalettes ||
        cpal.numColorRecords > RoboFontColorMaxPaletteEntries) {
        return RoboParseError;
    }
    if (FontCheckedRange(span.size, paletteIndicesOff,
                                (size_t)cpal.numPalette * 2u) != RoboOk) {
        return RoboParseError;
    }

    info->cpalTable = span;
    info->cpalVersion = cpal.version;
    info->numPaletteEntries = cpal.numPalettesEntries;
    info->paletteCount = cpal.numPalette;
    info->numColorRecords = cpal.numColorRecords;
    info->colorRecordsOffset = cpal.colorRecordsArrayOffset;

    if (cpal.numPalette > 0u) {
        info->palettes = (FontColorPalette *)RoboCalloc(
            cpal.numPalette, sizeof(FontColorPalette));
        if (!info->palettes) {
            return RoboOutOfMemory;
        }
    }
    if (cpal.numColorRecords > 0u) {
        info->colors = (FontColorRgba *)RoboCalloc(cpal.numColorRecords,
                                                             sizeof(FontColorRgba));
        if (!info->colors) {
            return RoboOutOfMemory;
        }
    }

    FontReaderInitSpan(&r, span);
    for (i = 0u; i < cpal.numColorRecords; i++) {
        size_t off = cpal.colorRecordsArrayOffset + (size_t)i * 4u;
        FontReader cr;
        uint8_t b;
        uint8_t g;
        uint8_t red;
        uint8_t a;
        if (FontCheckedRange(span.size, off, 4u) != RoboOk) {
            return RoboParseError;
        }
        FontReaderInitSpan(&cr, span);
        if (FontReaderSeek(&cr, off) != RoboOk ||
            FontReadU8(&cr, &b) != RoboOk || FontReadU8(&cr, &g) != RoboOk ||
            FontReadU8(&cr, &red) != RoboOk || FontReadU8(&cr, &a) != RoboOk) {
            return RoboParseError;
        }
        info->colors[i].b = b;
        info->colors[i].g = g;
        info->colors[i].r = red;
        info->colors[i].a = a;
    }

    for (i = 0u; i < cpal.numPalette; i++) {
        uint16_t first;
        uint16_t entryCount;
        size_t poff = paletteIndicesOff + (size_t)i * 2u;
        FontReader pr;
        FontReaderInitSpan(&pr, span);
        if (FontReaderSeek(&pr, poff) != RoboOk ||
            FontReadU16be(&pr, &first) != RoboOk) {
            return RoboParseError;
        }
        if (i + 1u < cpal.numPalette) {
            uint16_t next;
            FontReader nr;
            FontReaderInitSpan(&nr, span);
            if (FontReaderSeek(&nr, paletteIndicesOff + (size_t)(i + 1u) * 2u) !=
                    RoboOk ||
                FontReadU16be(&nr, &next) != RoboOk) {
                return RoboParseError;
            }
            entryCount = (uint16_t)(next - first);
        } else {
            entryCount = (uint16_t)(cpal.numColorRecords - first);
        }
        if (first > cpal.numColorRecords || (uint32_t)first + entryCount > cpal.numColorRecords) {
            return RoboParseError;
        }
        info->palettes[i].index = i;
        info->palettes[i].entryCount = entryCount;
        info->palettes[i].firstColorIndex = first;
        info->palettes[i].flags = 0u;
        info->palettes[i].nameId = 0xFFFFu;
        if (cpal.version >= 1u) {
            size_t metaBase = paletteIndicesOff + (size_t)cpal.numPalette * 2u;
            uint32_t typesOff = 0u;
            uint32_t labelsOff = 0u;
            if (metaBase + 12u <= span.size) {
                FontReader mr;
                FontReaderInitSpan(&mr, span);
                if (FontReaderSeek(&mr, metaBase) == RoboOk &&
                    FontReadU32be(&mr, &typesOff) == RoboOk &&
                    FontReadU32be(&mr, &labelsOff) == RoboOk) {
                    size_t tabs;
                    if (typesOff != 0u &&
                        FontOtResolveOffset32(span, 0u, typesOff, &tabs) == RoboOk &&
                        FontCheckedRange(span.size, tabs + (size_t)i * 2u, 2u) == RoboOk) {
                        FontReader tr;
                        FontReaderInitSpan(&tr, span);
                        if (FontReaderSeek(&tr, tabs + (size_t)i * 2u) == RoboOk) {
                            (void)FontReadU16be(&tr, &info->palettes[i].flags);
                        }
                    }
                    if (labelsOff != 0u) {
                        size_t labs;
                        if (FontOtResolveOffset32(span, 0u, labelsOff, &labs) == RoboOk &&
                            FontCheckedRange(span.size, labs + (size_t)i * 2u, 2u) == RoboOk) {
                            FontReader lr;
                            FontReaderInitSpan(&lr, span);
                            if (FontReaderSeek(&lr, labs + (size_t)i * 2u) == RoboOk) {
                                (void)FontReadU16be(&lr, &info->palettes[i].nameId);
                            }
                        }
                    }
                }
            }
        }
    }

    info->capabilities |= RoboFontColorCapCpal;
    return RoboOk;
}

static Result ParseColrV0(FontColorInfo *info, FontSpan span)
{
    FontColr colr;
    Result res;
    uint32_t i;

    res = FontColrParse(span, &colr);
    if (res != RoboOk || colr.version != 0u) {
        return res != RoboOk ? res : RoboNotSupported;
    }
    if (colr.numBaseGlyphRecords > RoboFontColorMaxLayers ||
        colr.numLayerRecords > RoboFontColorMaxLayers) {
        return RoboParseError;
    }

    info->colrTable = span;
    info->colrVersion = colr.version;
    info->colrNumLayers = colr.numLayerRecords;

    if (colr.numBaseGlyphRecords > 0u) {
        info->colrV0Bases = (FontColorColrV0Base *)RoboCalloc(
            colr.numBaseGlyphRecords, sizeof(FontColorColrV0Base));
        if (!info->colrV0Bases) {
            return RoboOutOfMemory;
        }
        info->colrV0BaseCount = colr.numBaseGlyphRecords;
    }
    if (colr.numLayerRecords > 0u) {
        info->colrV0Layers = (FontColorGlyphLayer *)RoboCalloc(
            colr.numLayerRecords, sizeof(FontColorGlyphLayer));
        if (!info->colrV0Layers) {
            return RoboOutOfMemory;
        }
        info->colrV0LayerCount = colr.numLayerRecords;
    }

    for (i = 0u; i < colr.numBaseGlyphRecords; i++) {
        size_t off = colr.baseGlyphRecordsOffset + (size_t)i * 6u;
        FontReader br;
        uint16_t gid;
        uint16_t first;
        uint16_t count;
        FontReaderInitSpan(&br, span);
        if (FontReaderSeek(&br, off) != RoboOk ||
            FontReadU16be(&br, &gid) != RoboOk ||
            FontReadU16be(&br, &first) != RoboOk ||
            FontReadU16be(&br, &count) != RoboOk) {
            return RoboParseError;
        }
        if ((uint32_t)first + count > colr.numLayerRecords) {
            return RoboParseError;
        }
        info->colrV0Bases[i].glyphId = gid;
        info->colrV0Bases[i].firstLayer = first;
        info->colrV0Bases[i].numLayers = count;
    }

    for (i = 0u; i < colr.numLayerRecords; i++) {
        size_t off = colr.layerRecordsOffset + (size_t)i * 4u;
        FontReader lr;
        uint16_t gid;
        uint16_t pal;
        FontReaderInitSpan(&lr, span);
        if (FontReaderSeek(&lr, off) != RoboOk ||
            FontReadU16be(&lr, &gid) != RoboOk ||
            FontReadU16be(&lr, &pal) != RoboOk) {
            return RoboParseError;
        }
        info->colrV0Layers[i].glyphId = gid;
        info->colrV0Layers[i].paletteIndex = pal;
    }

    info->capabilities |= RoboFontColorCapColr | RoboFontColorCapColrV0;
    return RoboOk;
}

static Result ParseColrV1(FontColorInfo *info, FontSpan span)
{
    FontReader r;
    uint16_t version;
    uint16_t numBase;
    uint32_t baseOff;
    uint32_t layerOff;
    uint16_t numLayer;
    uint32_t basePaintOff;
    uint32_t layerListOff;
    uint32_t clipListOff;
    uint32_t varMapOff;
    uint32_t varStoreOff;
    size_t abs;
    uint32_t i;

    if (!info || span.size < 34u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &version) != RoboOk ||
        FontReadU16be(&r, &numBase) != RoboOk ||
        FontReadU32be(&r, &baseOff) != RoboOk ||
        FontReadU32be(&r, &layerOff) != RoboOk ||
        FontReadU16be(&r, &numLayer) != RoboOk ||
        FontReadU32be(&r, &basePaintOff) != RoboOk ||
        FontReadU32be(&r, &layerListOff) != RoboOk ||
        FontReadU32be(&r, &clipListOff) != RoboOk ||
        FontReadU32be(&r, &varMapOff) != RoboOk ||
        FontReadU32be(&r, &varStoreOff) != RoboOk) {
        return RoboParseError;
    }
    if (version != 1u) {
        return RoboNotSupported;
    }

    info->colrTable = span;
    info->colrVersion = version;
    info->colrNumLayers = numLayer;
    if (varStoreOff != 0u) {
        info->capabilities |= RoboFontColorCapVariation;
        if (FontOtResolveOffset32(span, 0u, varStoreOff, &abs) == RoboOk) {
            info->colrItemVarStoreOffset = abs;
        }
    }

    if (layerListOff != 0u &&
        FontOtResolveOffset32(span, 0u, layerListOff, &abs) == RoboOk &&
        abs + 4u <= span.size) {
        FontReader lr;
        info->colrLayerListOffset = abs;
        FontReaderInitSpan(&lr, span);
        if (FontReaderSeek(&lr, abs) == RoboOk &&
            FontReadU32be(&lr, &info->colrV1LayerCount) == RoboOk) {
            if (info->colrV1LayerCount > RoboFontColorMaxLayers) {
                return RoboParseError;
            }
        }
    }

    if (basePaintOff != 0u &&
        FontOtResolveOffset32(span, 0u, basePaintOff, &abs) == RoboOk) {
        if (FontCheckedRange(span.size, abs, (size_t)numBase * 5u) != RoboOk) {
            return RoboParseError;
        }
        if (numBase > 0u) {
            info->colrV1Bases = (FontColorColrV1Base *)RoboCalloc(
                numBase, sizeof(FontColorColrV1Base));
            if (!info->colrV1Bases) {
                return RoboOutOfMemory;
            }
            info->colrV1BaseCount = numBase;
        }
        for (i = 0u; i < numBase; i++) {
            size_t rec = abs + (size_t)i * 5u;
            FontReader br;
            uint16_t gid;
            size_t paintAbs;
            FontReaderInitSpan(&br, span);
            if (FontReaderSeek(&br, rec) != RoboOk ||
                FontReadU16be(&br, &gid) != RoboOk ||
                ReadOffset24(span, rec + 2u, &paintAbs) != RoboOk) {
                return RoboParseError;
            }
            info->colrV1Bases[i].glyphId = gid;
            info->colrV1Bases[i].paintOffset = (uint32_t)paintAbs;
        }
    }

    (void)baseOff;
    (void)layerOff;
    (void)clipListOff;
    (void)varMapOff;
    info->capabilities |= RoboFontColorCapColr | RoboFontColorCapColrV1;
    return RoboOk;
}

static Result ParseSbix(FontColorInfo *info, FontSpan span)
{
    FontReader r;
    uint16_t version;
    uint16_t flags;
    uint32_t numStrikes;
    uint32_t i;

    (void)flags;
    if (!info || info->numGlyphs == 0u) {
        return RoboParseError;
    }
    if (span.size < 8u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &version) != RoboOk ||
        FontReadU16be(&r, &flags) != RoboOk ||
        FontReadU32be(&r, &numStrikes) != RoboOk) {
        return RoboParseError;
    }
    if (version != 1u || numStrikes == 0u || numStrikes > RoboFontColorMaxStrikes) {
        return RoboParseError;
    }
    if (FontCheckedRange(span.size, 8u, (size_t)numStrikes * 4u) != RoboOk) {
        return RoboParseError;
    }

    info->sbixTable = span;
    info->sbixStrikes = (FontColorSbixStrike *)RoboCalloc(
        numStrikes, sizeof(FontColorSbixStrike));
    if (!info->sbixStrikes) {
        return RoboOutOfMemory;
    }
    info->sbixStrikeCount = (uint16_t)numStrikes;

    for (i = 0u; i < numStrikes; i++) {
        uint32_t strikeOff;
        size_t soff = 8u + (size_t)i * 4u;
        FontReader sr;
        uint16_t ppemX;
        uint16_t ppemY;
        uint32_t dataOff;
        size_t glyphArrayBytes;
        if (FontCheckedRange(span.size, soff, 4u) != RoboOk) {
            return RoboParseError;
        }
        FontReaderInitSpan(&sr, span);
        if (FontReaderSeek(&sr, soff) != RoboOk ||
            FontReadU32be(&sr, &strikeOff) != RoboOk) {
            return RoboParseError;
        }
        if (strikeOff == 0u || strikeOff + 8u > span.size) {
            return RoboParseError;
        }
        FontReaderInitSpan(&sr, span);
        if (FontReaderSeek(&sr, strikeOff) != RoboOk ||
            FontReadU16be(&sr, &ppemX) != RoboOk ||
            FontReadU16be(&sr, &ppemY) != RoboOk ||
            FontReadU32be(&sr, &dataOff) != RoboOk) {
            return RoboParseError;
        }
        if (dataOff == 0u || dataOff + 4u > span.size) {
            return RoboParseError;
        }
        glyphArrayBytes = ((size_t)info->numGlyphs + 1u) * 4u;
        if (FontCheckedRange(span.size, dataOff, glyphArrayBytes) != RoboOk) {
            return RoboParseError;
        }
        info->sbixStrikes[i].ppemX = ppemX;
        info->sbixStrikes[i].ppemY = ppemY ? ppemY : ppemX;
        info->sbixStrikes[i].strikeOffset = strikeOff;
    }

    info->capabilities |= RoboFontColorCapSbix;
    return RoboOk;
}

static Result ParseCblc(FontColorInfo *info,
                                  FontSpan cblc,
                                  FontSpan cbdt)
{
    FontReader r;
    uint16_t major;
    uint16_t minor;
    uint32_t numSizes;
    uint32_t i;

    if (!info || cblc.size < 8u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, cblc);
    if (FontReadU16be(&r, &major) != RoboOk ||
        FontReadU16be(&r, &minor) != RoboOk ||
        FontReadU32be(&r, &numSizes) != RoboOk) {
        return RoboParseError;
    }
    if (major != 2u && major != 3u) {
        return RoboNotSupported;
    }
    (void)minor;
    if (numSizes == 0u || numSizes > RoboFontColorMaxStrikes) {
        return RoboParseError;
    }
    if (FontCheckedRange(cblc.size, 8u, (size_t)numSizes * 48u) != RoboOk) {
        return RoboParseError;
    }

    info->cblcTable = cblc;
    info->cbdtTable = cbdt;
    info->cblcStrikes = (FontColorCblcStrike *)RoboCalloc(
        numSizes, sizeof(FontColorCblcStrike));
    if (!info->cblcStrikes) {
        return RoboOutOfMemory;
    }
    info->cblcStrikeCount = (uint16_t)numSizes;

    for (i = 0u; i < numSizes; i++) {
        size_t off = 8u + (size_t)i * 48u;
        FontReader sr;
        uint32_t indexArrayOff;
        uint32_t indexTablesSize;
        uint32_t numSubtables;
        uint32_t colorRef;
        uint16_t startGlyph;
        uint16_t endGlyph;
        uint8_t ppemX;
        uint8_t ppemY;
        uint8_t bitDepth;
        uint8_t flags;
        (void)colorRef;
        FontReaderInitSpan(&sr, cblc);
        if (FontReaderSeek(&sr, off) != RoboOk ||
            FontReadU32be(&sr, &indexArrayOff) != RoboOk ||
            FontReadU32be(&sr, &indexTablesSize) != RoboOk ||
            FontReadU32be(&sr, &numSubtables) != RoboOk ||
            FontReadU32be(&sr, &colorRef) != RoboOk) {
            return RoboParseError;
        }
        if (FontReaderSeek(&sr, off + 32u) != RoboOk ||
            FontReadU16be(&sr, &startGlyph) != RoboOk ||
            FontReadU16be(&sr, &endGlyph) != RoboOk ||
            FontReadU8(&sr, &ppemX) != RoboOk ||
            FontReadU8(&sr, &ppemY) != RoboOk ||
            FontReadU8(&sr, &bitDepth) != RoboOk ||
            FontReadU8(&sr, &flags) != RoboOk) {
            return RoboParseError;
        }
        if (indexArrayOff == 0u) {
            return RoboParseError;
        }
        {
            size_t indexAbs;
            if (FontOtResolveOffset32(cblc, 0u, indexArrayOff, &indexAbs) != RoboOk) {
                return RoboParseError;
            }
            info->cblcStrikes[i].indexSubtableArrayOffset = (uint32_t)indexAbs;
        }
        info->cblcStrikes[i].indexTablesSize = indexTablesSize;
        info->cblcStrikes[i].numIndexSubtables = numSubtables;
        info->cblcStrikes[i].startGlyph = startGlyph;
        info->cblcStrikes[i].endGlyph = endGlyph;
        info->cblcStrikes[i].ppemX = ppemX;
        info->cblcStrikes[i].ppemY = ppemY ? ppemY : ppemX;
        info->cblcStrikes[i].bitDepth = bitDepth;
        info->cblcStrikes[i].flags = flags;
    }

    info->capabilities |= RoboFontColorCapCblc;
    if (cbdt.data && cbdt.size > 0u) {
        info->capabilities |= RoboFontColorCapCbdt;
    }
    return RoboOk;
}

static Result ParseSvg(FontColorInfo *info, FontSpan span)
{
    FontReader r;
    uint16_t version;
    uint32_t docListOff;
    size_t abs;
    uint16_t numEntries;
    uint32_t i;

    if (!info || span.size < 6u) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &version) != RoboOk ||
        FontReadU32be(&r, &docListOff) != RoboOk) {
        return RoboParseError;
    }
    if (version != 0u) {
        return RoboNotSupported;
    }
    if (docListOff == 0u ||
        FontOtResolveOffset32(span, 0u, docListOff, &abs) != RoboOk ||
        abs + 2u > span.size) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, span);
    if (FontReaderSeek(&r, abs) != RoboOk ||
        FontReadU16be(&r, &numEntries) != RoboOk) {
        return RoboParseError;
    }
    if (numEntries == 0u) {
        return RoboParseError;
    }
    if (numEntries > RoboFontColorMaxSvgDocs) {
        numEntries = (uint16_t)RoboFontColorMaxSvgDocs;
    }
    if (FontCheckedRange(span.size, abs + 2u, (size_t)numEntries * 12u) != RoboOk) {
        return RoboParseError;
    }

    info->svgTable = span;
    info->svgDocs = (FontColorGlyphSvg *)RoboCalloc(
        numEntries, sizeof(FontColorGlyphSvg));
    if (!info->svgDocs) {
        return RoboOutOfMemory;
    }
    info->svgDocCount = numEntries;

    for (i = 0u; i < numEntries; i++) {
        size_t rec = abs + 2u + (size_t)i * 12u;
        FontReader dr;
        uint16_t startGid;
        uint16_t endGid;
        uint32_t payloadOff;
        uint32_t payloadLenU32;
        size_t payloadAbs;
        size_t payloadLen;
        FontReaderInitSpan(&dr, span);
        if (FontReaderSeek(&dr, rec) != RoboOk ||
            FontReadU16be(&dr, &startGid) != RoboOk ||
            FontReadU16be(&dr, &endGid) != RoboOk ||
            FontReadU32be(&dr, &payloadOff) != RoboOk ||
            FontReadU32be(&dr, &payloadLenU32) != RoboOk) {
            return RoboParseError;
        }
        if (endGid < startGid) {
            return RoboParseError;
        }
        if (payloadOff == 0u || payloadLenU32 == 0u ||
            FontOtResolveOffset32(span, abs, payloadOff, &payloadAbs) != RoboOk) {
            return RoboParseError;
        }
        payloadLen = (size_t)payloadLenU32;
        if (payloadLen > RoboFontColorMaxSvgPayload) {
            return RoboParseError;
        }
        if (FontCheckedRange(span.size, payloadAbs, payloadLen) != RoboOk) {
            return RoboParseError;
        }
        info->svgDocs[i].startGlyphId = startGid;
        info->svgDocs[i].endGlyphId = endGid;
        info->svgDocs[i].document.data = span.data + payloadAbs;
        info->svgDocs[i].document.size = payloadLen;
        info->svgDocs[i].hasExternalRefHint =
            (uint8_t)SvgHasExternalRefHint(span.data + payloadAbs, payloadLen);
    }

    info->capabilities |= RoboFontColorCapSvg;
    return RoboOk;
}

static int PaintOffsetSeen(FontColorPaintBuilder *b, uint32_t off)
{
    uint32_t i;
    for (i = 0u; i < b->visitedCount; i++) {
        if (b->visitedOffsets[i] == off) {
            return 1;
        }
    }
    return 0;
}

static void PaintOffsetMark(FontColorPaintBuilder *b, uint32_t off)
{
    if (b->visitedCount < RoboFontColorMaxPaintNodes) {
        b->visitedOffsets[b->visitedCount++] = off;
    }
}

static int ColrGlyphVisiting(FontColorPaintBuilder *b, GlyphId gid)
{
    uint32_t i;
    for (i = 0u; i < b->colrGlyphDepth; i++) {
        if (b->colrGlyphStack[i] == gid) {
            return 1;
        }
    }
    return 0;
}

static void ColrGlyphPush(FontColorPaintBuilder *b, GlyphId gid)
{
    if (b->colrGlyphDepth < RoboFontColorMaxPaintDepth) {
        b->colrGlyphStack[b->colrGlyphDepth++] = gid;
    }
}

static void ColrGlyphPop(FontColorPaintBuilder *b)
{
    if (b->colrGlyphDepth > 0u) {
        b->colrGlyphDepth--;
    }
}

static uint32_t PaintAppend(FontColorPaintBuilder *b, FontColorPaint *n)
{
    if (b->error != RoboOk || b->nodeCount >= RoboFontColorMaxPaintNodes) {
        b->error = RoboNotSupported;
        return NODE_NONE;
    }
    b->nodes[b->nodeCount] = *n;
    return b->nodeCount++;
}

static Result PaintValidateColorline(FontSpan colr, size_t at)
{
    size_t abs;
    uint16_t extendMode;
    FontReader r;
    if (ReadOffset24(colr, at, &abs) != RoboOk || abs + 2u > colr.size) {
        return RoboParseError;
    }
    FontReaderInitSpan(&r, colr);
    if (FontReaderSeek(&r, abs) != RoboOk ||
        FontReadU16be(&r, &extendMode) != RoboOk) {
        return RoboParseError;
    }
    (void)extendMode;
    return RoboOk;
}

static Result PaintExpandAt(FontColorPaintBuilder *b,
                                     uint32_t paintOff,
                                     uint32_t *outFirstIndex);

static Result PaintExpandColrLayers(FontColorPaintBuilder *b,
                                              size_t paintAbs,
                                              uint8_t depth,
                                              uint32_t *outFirstIndex)
{
    FontReader r;
    uint8_t numLayers;
    uint32_t firstLayer = 0u;
    uint32_t i;
    FontColorPaint head;
    uint32_t headIndex;
    uint32_t prev = NODE_NONE;
    uint32_t firstChild = NODE_NONE;

    if (b->depth >= RoboFontColorMaxPaintDepth) {
        b->error = RoboNotSupported;
        return RoboNotSupported;
    }
    FontReaderInitSpan(&r, b->colr);
    if (FontReaderSeek(&r, paintAbs) != RoboOk ||
        FontReadU8(&r, &head.format) != RoboOk ||
        FontReadU8(&r, &numLayers) != RoboOk) {
        b->error = RoboParseError;
        return RoboParseError;
    }
    {
        size_t firstLayerAbs = paintAbs + 2u;
        uint32_t layerIndex = 0u;
        if (FontCheckedRange(b->colr.size, firstLayerAbs, 3u) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        layerIndex = ((uint32_t)b->colr.data[firstLayerAbs] << 16) |
                      ((uint32_t)b->colr.data[firstLayerAbs + 1u] << 8) |
                      (uint32_t)b->colr.data[firstLayerAbs + 2u];
        firstLayer = layerIndex;
    }
    (void)head.format;
    if (b->info->colrLayerListOffset == 0u ||
        firstLayer + numLayers > b->info->colrV1LayerCount) {
        b->error = RoboParseError;
        return RoboParseError;
    }

    memset(&head, 0, sizeof(head));
    head.op = RoboFontColorPaintColrLayers;
    head.format = 1u;
    head.depth = depth;
    head.childIndex = NODE_NONE;
    head.siblingIndex = NODE_NONE;
    headIndex = PaintAppend(b, &head);
    if (headIndex == NODE_NONE) {
        return b->error;
    }
    firstChild = headIndex;

    for (i = 0u; i < numLayers; i++) {
        size_t layerRec =
            b->info->colrLayerListOffset + 4u + (size_t)(firstLayer + i) * 3u;
        size_t childAbs;
        uint32_t childIndex;
        if (ReadOffset24(b->colr, layerRec, &childAbs) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        b->depth++;
        if (PaintExpandAt(b, (uint32_t)childAbs, &childIndex) != RoboOk) {
            b->depth--;
            return b->error;
        }
        b->depth--;
        if (childIndex != NODE_NONE) {
            if (prev != NODE_NONE) {
                b->nodes[prev].siblingIndex = childIndex;
            } else {
                b->nodes[headIndex].childIndex = childIndex;
            }
            prev = childIndex;
        }
    }

    if (outFirstIndex) {
        *outFirstIndex = firstChild;
    }
    return RoboOk;
}

static Result PaintExpandAt(FontColorPaintBuilder *b,
                                     uint32_t paintOff,
                                     uint32_t *outFirstIndex)
{
    FontColorPaint node;
    FontReader r;
    uint8_t format;
    size_t paintAbs = paintOff;
    uint32_t nodeIndex;
    uint32_t childIndex = NODE_NONE;
    uint32_t siblingIndex = NODE_NONE;
    size_t childAbs = 0u;
    Result res;

    if (b->error != RoboOk) {
        return b->error;
    }
    if (paintAbs >= b->colr.size) {
        b->error = RoboParseError;
        return RoboParseError;
    }
    if (PaintOffsetSeen(b, (uint32_t)paintAbs)) {
        b->error = RoboParseError;
        return RoboParseError;
    }
    PaintOffsetMark(b, (uint32_t)paintAbs);

    FontReaderInitSpan(&r, b->colr);
    if (FontReaderSeek(&r, paintAbs) != RoboOk ||
        FontReadU8(&r, &format) != RoboOk) {
        b->error = RoboParseError;
        return RoboParseError;
    }

    memset(&node, 0, sizeof(node));
    node.format = format;
    node.depth = b->depth;
    node.childIndex = NODE_NONE;
    node.siblingIndex = NODE_NONE;

    switch (format) {
    case 1u:
        return PaintExpandColrLayers(b, paintAbs, b->depth, outFirstIndex);

    case 2u:
    case 3u:
        node.op = RoboFontColorPaintSolid;
        if (FontReadU16be(&r, &node.paletteIndex) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        {
            int16_t alpha;
            if (FontReadI16be(&r, &alpha) != RoboOk) {
                b->error = RoboParseError;
                return RoboParseError;
            }
            node.alphaF2dot14Hi = F2dot14AlphaHi(alpha);
            node.alphaU8 = F2dot14AlphaToU8(alpha);
        }
        if (format == 3u) {
            node.hasVariation = 1u;
            b->hasVariation = 1u;
        }
        break;

    case 4u:
    case 6u:
    case 8u:
        node.op = (format == 4u)   ? RoboFontColorPaintLinearGradient
                  : (format == 6u) ? RoboFontColorPaintRadialGradient
                                   : RoboFontColorPaintSweepGradient;
        if (PaintValidateColorline(b->colr, paintAbs + 1u) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        break;
    case 5u:
    case 7u:
    case 9u:
        node.op = (format == 5u)   ? RoboFontColorPaintLinearGradient
                  : (format == 7u) ? RoboFontColorPaintRadialGradient
                                   : RoboFontColorPaintSweepGradient;
        node.hasVariation = 1u;
        b->hasVariation = 1u;
        if (PaintValidateColorline(b->colr, paintAbs + 1u) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        break;

    case 10u: {
        size_t childAbs;
        uint16_t gid16;
        node.op = RoboFontColorPaintGlyph;
        if (ReadOffset24(b->colr, paintAbs + 1u, &childAbs) != RoboOk ||
            FontReadU16be(&r, &gid16) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        node.glyphId = gid16;
        b->depth++;
        res = PaintExpandAt(b, (uint32_t)childAbs, &childIndex);
        b->depth--;
        if (res != RoboOk) {
            return res;
        }
        node.childIndex = childIndex;
        break;
    }

    case 11u: {
        const FontColorColrV1Base *base;
        uint16_t gid16;
        node.op = RoboFontColorPaintColrGlyph;
        if (FontReadU16be(&r, &gid16) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        node.glyphId = gid16;
        if (ColrGlyphVisiting(b, node.glyphId) ||
            b->depth >= RoboFontColorMaxPaintDepth) {
            node.unsupported = 1u;
            node.op = RoboFontColorPaintUnsupported;
            break;
        }
        base = ColrV1BaseFind(b->info, node.glyphId);
        if (!base) {
            node.unsupported = 1u;
            node.op = RoboFontColorPaintUnsupported;
            break;
        }
        ColrGlyphPush(b, node.glyphId);
        b->depth++;
        res = PaintExpandAt(b, base->paintOffset, &childIndex);
        b->depth--;
        ColrGlyphPop(b);
        if (res != RoboOk) {
            return res;
        }
        node.childIndex = childIndex;
        break;
    }

    case 12u:
    case 13u: {
        size_t childAbs;
        node.op = RoboFontColorPaintTransform;
        if (ReadOffset24(b->colr, paintAbs + 1u, &childAbs) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        if (format == 13u) {
            node.hasVariation = 1u;
            b->hasVariation = 1u;
        }
        b->depth++;
        res = PaintExpandAt(b, (uint32_t)childAbs, &childIndex);
        b->depth--;
        if (res != RoboOk) {
            return res;
        }
        node.childIndex = childIndex;
        break;
    }

    case 14u:
    case 15u:
        node.op = RoboFontColorPaintTranslate;
        if (format == 15u) {
            node.hasVariation = 1u;
            b->hasVariation = 1u;
        }
        if (ReadOffset24(b->colr, paintAbs + 1u, &childAbs) != RoboOk ||
            FontReadI16be(&r, &node.tx) != RoboOk ||
            FontReadI16be(&r, &node.ty) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        b->depth++;
        res = PaintExpandAt(b, (uint32_t)childAbs, &childIndex);
        b->depth--;
        if (res != RoboOk) {
            return res;
        }
        node.childIndex = childIndex;
        break;

    case 16u:
    case 17u:
        node.op = RoboFontColorPaintScale;
        if (format == 17u) {
            node.hasVariation = 1u;
            b->hasVariation = 1u;
        }
        if (ReadOffset24(b->colr, paintAbs + 1u, &childAbs) != RoboOk ||
            FontReadI16be(&r, &node.xx) != RoboOk ||
            FontReadI16be(&r, &node.yy) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        b->depth++;
        res = PaintExpandAt(b, (uint32_t)childAbs, &childIndex);
        b->depth--;
        if (res != RoboOk) {
            return res;
        }
        node.childIndex = childIndex;
        break;

    case 24u:
    case 25u:
        node.op = RoboFontColorPaintRotate;
        if (format == 25u) {
            node.hasVariation = 1u;
            b->hasVariation = 1u;
        }
        if (ReadOffset24(b->colr, paintAbs + 1u, &childAbs) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        b->depth++;
        res = PaintExpandAt(b, (uint32_t)childAbs, &childIndex);
        b->depth--;
        if (res != RoboOk) {
            return res;
        }
        node.childIndex = childIndex;
        break;

    case 28u:
    case 29u:
        node.op = RoboFontColorPaintSkew;
        if (format == 29u) {
            node.hasVariation = 1u;
            b->hasVariation = 1u;
        }
        if (ReadOffset24(b->colr, paintAbs + 1u, &childAbs) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        b->depth++;
        res = PaintExpandAt(b, (uint32_t)childAbs, &childIndex);
        b->depth--;
        if (res != RoboOk) {
            return res;
        }
        node.childIndex = childIndex;
        break;

    case 32u: {
        size_t backdropAbs;
        size_t sourceAbs;
        node.op = RoboFontColorPaintComposite;
        if (ReadOffset24(b->colr, paintAbs + 1u, &backdropAbs) != RoboOk ||
            ReadOffset24(b->colr, paintAbs + 4u, &sourceAbs) != RoboOk ||
            FontReadU8(&r, &node.compositeMode) != RoboOk) {
            b->error = RoboParseError;
            return RoboParseError;
        }
        b->depth++;
        res = PaintExpandAt(b, (uint32_t)backdropAbs, &childIndex);
        if (res == RoboOk) {
            res = PaintExpandAt(b, (uint32_t)sourceAbs, &siblingIndex);
        }
        b->depth--;
        if (res != RoboOk) {
            return res;
        }
        node.childIndex = childIndex;
        node.siblingIndex = siblingIndex;
        break;
    }

    default:
        node.op = RoboFontColorPaintUnsupported;
        node.unsupported = 1u;
        break;
    }

    nodeIndex = PaintAppend(b, &node);
    if (nodeIndex == NODE_NONE) {
        return b->error;
    }
    if (outFirstIndex) {
        *outFirstIndex = nodeIndex;
    }
    return RoboOk;
}

static Result PaintBuildForGlyph(FontColorInfo *info,
                                           GlyphId gid,
                                           FontColorPaintCache **outCache)
{
    const FontColorColrV1Base *base;
    FontColorPaintBuilder builder;
    FontColorPaintCache *cache;
    uint32_t root = NODE_NONE;

    if (!info || !outCache) {
        return RoboInvalidArgument;
    }
    *outCache = NULL;
    base = ColrV1BaseFind(info, gid);
    if (!base || info->colrTable.size == 0u) {
        return RoboNotFound;
    }

    cache = PaintCacheFind(info, gid);
    if (cache) {
        if (cache->building) {
            return RoboBusy;
        }
        if (cache->nodes) {
            *outCache = cache;
            return RoboOk;
        }
    } else {
        cache = PaintCacheAlloc(info, gid);
        if (!cache) {
            return RoboOutOfMemory;
        }
    }

    cache->building = 1u;
    memset(&builder, 0, sizeof(builder));
    builder.info = info;
    builder.colr = info->colrTable;
    builder.nodes = (FontColorPaint *)RoboCalloc(
        RoboFontColorMaxPaintNodes, sizeof(FontColorPaint));
    if (!builder.nodes) {
        cache->building = 0u;
        return RoboOutOfMemory;
    }
    builder.error = RoboOk;
    if (PaintExpandAt(&builder, base->paintOffset, &root) != RoboOk || root == NODE_NONE) {
        RoboFree(builder.nodes);
        cache->building = 0u;
        return builder.error != RoboOk ? builder.error : RoboNotSupported;
    }

    cache->nodes = (FontColorPaint *)Realloc(
        builder.nodes, (size_t)builder.nodeCount * sizeof(FontColorPaint));
    if (!cache->nodes) {
        RoboFree(builder.nodes);
        cache->building = 0u;
        return RoboOutOfMemory;
    }
    cache->rootIndex = root;
    cache->nodeCount = builder.nodeCount;
    cache->hasVariation = builder.hasVariation;
    cache->building = 0u;
    *outCache = cache;
    return RoboOk;
}

static Result ColrV1LoadV0Layers(FontColorInfo *info, FontSpan span)
{
    FontReader r;
    uint16_t version;
    uint16_t numBase;
    uint32_t baseOff;
    uint32_t layerOff;
    uint16_t numLayer;
    size_t abs;
    uint32_t i;

    if (!info || span.size < 14u) {
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
    (void)version;
    if (numBase > RoboFontColorMaxLayers || numLayer > RoboFontColorMaxLayers) {
        return RoboParseError;
    }
    if (numBase > 0u && !info->colrV0Bases) {
        info->colrV0Bases = (FontColorColrV0Base *)RoboCalloc(
            numBase, sizeof(FontColorColrV0Base));
        if (!info->colrV0Bases) {
            return RoboOutOfMemory;
        }
        info->colrV0BaseCount = numBase;
    }
    if (numLayer > 0u && !info->colrV0Layers) {
        info->colrV0Layers = (FontColorGlyphLayer *)RoboCalloc(
            numLayer, sizeof(FontColorGlyphLayer));
        if (!info->colrV0Layers) {
            return RoboOutOfMemory;
        }
        info->colrV0LayerCount = numLayer;
        info->colrNumLayers = numLayer;
    }
    if (baseOff != 0u && numBase > 0u &&
        FontOtResolveOffset32(span, 0u, baseOff, &abs) == RoboOk) {
        for (i = 0u; i < numBase; i++) {
            size_t off = abs + (size_t)i * 6u;
            FontReader br;
            uint16_t gid;
            uint16_t first;
            uint16_t count;
            FontReaderInitSpan(&br, span);
            if (FontReaderSeek(&br, off) != RoboOk ||
                FontReadU16be(&br, &gid) != RoboOk ||
                FontReadU16be(&br, &first) != RoboOk ||
                FontReadU16be(&br, &count) != RoboOk) {
                return RoboParseError;
            }
            if ((uint32_t)first + count > numLayer) {
                return RoboParseError;
            }
            info->colrV0Bases[i].glyphId = gid;
            info->colrV0Bases[i].firstLayer = first;
            info->colrV0Bases[i].numLayers = count;
        }
    }
    if (layerOff != 0u && numLayer > 0u &&
        FontOtResolveOffset32(span, 0u, layerOff, &abs) == RoboOk) {
        for (i = 0u; i < numLayer; i++) {
            size_t off = abs + (size_t)i * 4u;
            FontReader lr;
            uint16_t gid;
            uint16_t pal;
            FontReaderInitSpan(&lr, span);
            if (FontReaderSeek(&lr, off) != RoboOk ||
                FontReadU16be(&lr, &gid) != RoboOk ||
                FontReadU16be(&lr, &pal) != RoboOk) {
                return RoboParseError;
            }
            info->colrV0Layers[i].glyphId = gid;
            info->colrV0Layers[i].paletteIndex = pal;
        }
    }
    return RoboOk;
}

static int GlyphHasV1Paint(const FontColorInfo *info, GlyphId gid)
{
    return ColrV1BaseFind(info, gid) != NULL;
}

static const FontColorGlyphSvg *SvgDocForGlyph(
    const FontColorInfo *info,
    GlyphId gid)
{
    uint32_t i;
    if (!info) {
        return NULL;
    }
    for (i = 0u; i < info->svgDocCount; i++) {
        if (gid >= info->svgDocs[i].startGlyphId &&
            gid <= info->svgDocs[i].endGlyphId) {
            return &info->svgDocs[i];
        }
    }
    return NULL;
}

static Result SbixGlyphImage(const FontColorInfo *info,
                                      GlyphId gid,
                                      uint16_t preferPpem,
                                      FontColorGlyphImage *out)
{
    uint16_t best = 0u;
    uint32_t bestDiff = 0xFFFFFFFFu;
    uint16_t i;

    if (!info || !out || gid >= info->numGlyphs || info->sbixStrikeCount == 0u) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < info->sbixStrikeCount; i++) {
        uint32_t diff;
        uint16_t px = info->sbixStrikes[i].ppemX;
        diff = px > preferPpem ? (uint32_t)(px - preferPpem) : (uint32_t)(preferPpem - px);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = i;
        }
    }
    {
        const FontColorSbixStrike *st = &info->sbixStrikes[best];
        FontReader sr;
        uint32_t dataOff;
        size_t glyphArrayOff;
        uint32_t off0;
        uint32_t off1;
        size_t imgOff;
        size_t imgLen;
        uint32_t graphicType = 0u;
        FontSpan span = info->sbixTable;

        FontReaderInitSpan(&sr, span);
        if (FontReaderSeek(&sr, st->strikeOffset + 4u) != RoboOk ||
            FontReadU32be(&sr, &dataOff) != RoboOk) {
            return RoboParseError;
        }
        glyphArrayOff = dataOff;
        if (FontCheckedRange(span.size, glyphArrayOff + (size_t)gid * 4u, 8u) !=
            RoboOk) {
            return RoboNotFound;
        }
        FontReaderInitSpan(&sr, span);
        if (FontReaderSeek(&sr, glyphArrayOff + (size_t)gid * 4u) != RoboOk ||
            FontReadU32be(&sr, &off0) != RoboOk ||
            FontReadU32be(&sr, &off1) != RoboOk) {
            return RoboParseError;
        }
        if (off0 == 0u && off1 == 0u) {
            return RoboNotFound;
        }
        imgOff = glyphArrayOff + (size_t)off0;
        imgLen = (size_t)off1 - (size_t)off0;
        if (imgLen < 4u ||
            FontCheckedRange(span.size, imgOff, imgLen) != RoboOk) {
            return RoboParseError;
        }
        graphicType = ((uint32_t)span.data[imgOff] << 24) |
                       ((uint32_t)span.data[imgOff + 1u] << 16) |
                       ((uint32_t)span.data[imgOff + 2u] << 8) |
                       (uint32_t)span.data[imgOff + 3u];
        memset(out, 0, sizeof(*out));
        out->ppemX = st->ppemX;
        out->ppemY = st->ppemY;
        out->graphicType = graphicType;
        out->data.data = span.data + imgOff + 4u;
        out->data.size = imgLen - 4u;
        return RoboOk;
    }
}

static Result CblcGlyphImage(const FontColorInfo *info,
                                      GlyphId gid,
                                      uint16_t preferPpem,
                                      FontColorGlyphImage *out)
{
    uint16_t strikeI;
    uint16_t best = 0u;
    uint32_t bestDiff = 0xFFFFFFFFu;

    if (!info || !out || gid > 0xFFFFu || info->cblcStrikeCount == 0u ||
        info->cbdtTable.size == 0u) {
        return RoboInvalidArgument;
    }

    for (strikeI = 0u; strikeI < info->cblcStrikeCount; strikeI++) {
        const FontColorCblcStrike *st = &info->cblcStrikes[strikeI];
        uint32_t diff;
        uint16_t px = st->ppemX;
        if ((uint16_t)gid < st->startGlyph || (uint16_t)gid > st->endGlyph) {
            continue;
        }
        diff = px > preferPpem ? (uint32_t)(px - preferPpem) : (uint32_t)(preferPpem - px);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = strikeI;
        }
    }
    if (bestDiff == 0xFFFFFFFFu) {
        return RoboNotFound;
    }

    {
        const FontColorCblcStrike *st = &info->cblcStrikes[best];
        FontSpan cblc = info->cblcTable;
        FontSpan cbdt = info->cbdtTable;
        size_t arrayOff = st->indexSubtableArrayOffset;
        uint32_t subI;
        for (subI = 0u; subI < st->numIndexSubtables; subI++) {
            size_t entryOff = arrayOff + (size_t)subI * 8u;
            FontReader er;
            uint32_t first;
            uint32_t last;
            uint32_t addOff;
            size_t subAbs;
            uint16_t indexFormat;
            uint16_t imageFormat;
            uint32_t imageDataOff;
            size_t offsetArrayOff;
            uint32_t relOff;
            size_t imgAbs;
            size_t imgLen;
            if (FontCheckedRange(cblc.size, entryOff, 8u) != RoboOk) {
                continue;
            }
            FontReaderInitSpan(&er, cblc);
            if (FontReaderSeek(&er, entryOff) != RoboOk ||
                FontReadU32be(&er, &first) != RoboOk ||
                FontReadU32be(&er, &last) != RoboOk ||
                FontReadU32be(&er, &addOff) != RoboOk) {
                continue;
            }
            if (gid < first || gid > last) {
                continue;
            }
            if (FontOtResolveOffset32(cblc, arrayOff, addOff, &subAbs) != RoboOk) {
                continue;
            }
            FontReaderInitSpan(&er, cblc);
            if (FontReaderSeek(&er, subAbs) != RoboOk ||
                FontReadU16be(&er, &indexFormat) != RoboOk ||
                FontReadU16be(&er, &imageFormat) != RoboOk) {
                continue;
            }
            if (indexFormat != 1u) {
                return RoboNotSupported;
            }
            if (FontReadU32be(&er, &imageDataOff) != RoboOk) {
                continue;
            }
            offsetArrayOff = FontReaderPos(&er);
            if (FontCheckedRange(cblc.size, offsetArrayOff + (size_t)(gid - first) * 4u,
                                        4u) != RoboOk) {
                continue;
            }
            FontReaderInitSpan(&er, cblc);
            if (FontReaderSeek(&er, offsetArrayOff + (size_t)(gid - first) * 4u) !=
                    RoboOk ||
                FontReadU32be(&er, &relOff) != RoboOk) {
                continue;
            }
            imgAbs = (size_t)imageDataOff + (size_t)relOff;
            if (imgAbs >= cbdt.size) {
                continue;
            }
            imgLen = cbdt.size - imgAbs;
            memset(out, 0, sizeof(*out));
            out->ppemX = st->ppemX;
            out->ppemY = st->ppemY;
            out->format = (uint8_t)imageFormat;
            out->data.data = cbdt.data + imgAbs;
            out->data.size = imgLen;
            return RoboOk;
        }
    }
    return RoboNotFound;
}

Result FontColorInfoCreate(FontSfnt *sfnt,
                                          const FontVariationCoords *coordsOpt,
                                          FontColorInfo **out)
{
    FontColorInfo *info;
    FontSpan span;
    int hasColr;
    int hasCpal;
    int hasSbix;
    int hasCbdt;
    int hasCblc;
    int hasSvg;
    uint32_t startCaps;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (!sfnt) {
        return RoboInvalidArgument;
    }

    hasColr = FontSfntHasTable(sfnt, RoboFontTagColr);
    hasCpal = FontSfntHasTable(sfnt, RoboFontTagCpal);
    hasSbix = FontSfntHasTable(sfnt, RoboFontTagSbix);
    hasCbdt = FontSfntHasTable(sfnt, RoboFontTagCbdt);
    hasCblc = FontSfntHasTable(sfnt, RoboFontTagCblc);
    hasSvg = FontSfntHasTable(sfnt, RoboFontTagSvg);
    if (!hasColr && !hasCpal && !hasSbix && !hasCbdt && !hasCblc && !hasSvg) {
        return RoboNotFound;
    }

    info = (FontColorInfo *)RoboCalloc(1, sizeof(FontColorInfo));
    if (!info) {
        return RoboOutOfMemory;
    }
    info->cookie = INFO_COOKIE;
    info->refs = 1u;
    FontSfntRetain(sfnt);
    info->sfnt = sfnt;
    if (coordsOpt) {
        FontVariationCoordsRetain((FontVariationCoords *)coordsOpt);
        info->coords = coordsOpt;
    }
    (void)FontSfntNumGlyphs(sfnt, &info->numGlyphs);

    if (hasCpal &&
        FontSfntTableBytes(sfnt, RoboFontTagCpal, &span) == RoboOk) {
        startCaps = info->capabilities;
        if (ParseCpal(info, span) != RoboOk) {
            ClearCpalState(info);
            info->capabilities = startCaps;
        }
    }

    if (hasColr && FontSfntTableBytes(sfnt, RoboFontTagColr, &span) == RoboOk) {
        FontReader r;
        uint16_t colrVersion = 0u;
        startCaps = info->capabilities;
        if (span.size >= 2u) {
            FontReaderInitSpan(&r, span);
            (void)FontReadU16be(&r, &colrVersion);
        }
        if (colrVersion == 0u) {
            if (ParseColrV0(info, span) != RoboOk) {
                ClearColrState(info);
                info->capabilities = startCaps;
            }
        } else if (colrVersion == 1u) {
            if (ParseColrV1(info, span) != RoboOk) {
                ClearColrState(info);
                info->capabilities = startCaps;
            } else {
                (void)ColrV1LoadV0Layers(info, span);
            }
        } else {
            /* COLR version > 1: skip COLR caps; other tables may still parse. */
        }
    }

    if (hasSbix && info->numGlyphs > 0u &&
        FontSfntTableBytes(sfnt, RoboFontTagSbix, &span) == RoboOk) {
        startCaps = info->capabilities;
        if (ParseSbix(info, span) != RoboOk) {
            info->capabilities = startCaps;
        }
    }

    if (hasCblc && FontSfntTableBytes(sfnt, RoboFontTagCblc, &span) == RoboOk) {
        FontSpan cbdtSpan = {0, 0};
        startCaps = info->capabilities;
        if (hasCbdt) {
            (void)FontSfntTableBytes(sfnt, RoboFontTagCbdt, &cbdtSpan);
        }
        if (ParseCblc(info, span, cbdtSpan) != RoboOk) {
            info->capabilities = startCaps;
        }
    }

    if (hasSvg && FontSfntTableBytes(sfnt, RoboFontTagSvg, &span) == RoboOk) {
        startCaps = info->capabilities;
        if (ParseSvg(info, span) != RoboOk) {
            info->capabilities = startCaps;
        }
    }

    if (info->capabilities == RoboFontColorCapNone) {
        InfoFreeOwned(info);
        RoboFree(info);
        return RoboNotFound;
    }

    *out = info;
    return RoboOk;
}

Result FontColorInfoCreateFromFace(
    FontFace *face,
    const FontVariationCoords *coordsOpt,
    FontColorInfo **out)
{
    Result res;
    const FontSfnt *sfnt;

    if (!face || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    res = FontFaceParseSfnt(face);
    if (res == RoboNotSupported || res == RoboParseError || res == RoboNotFound) {
        return RoboNotFound;
    }
    if (res != RoboOk) {
        return res;
    }
    sfnt = FontFaceSfnt(face);
    if (!sfnt) {
        return RoboParseError;
    }
    return FontColorInfoCreate((FontSfnt *)sfnt, coordsOpt, out);
}

void FontColorInfoRetain(FontColorInfo *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return;
    }
    info->refs++;
}

void FontColorInfoRelease(FontColorInfo *info)
{
    if (!info || info->cookie != INFO_COOKIE) {
        return;
    }
    if (info->refs == 0u) {
        return;
    }
    info->refs--;
    if (info->refs != 0u) {
        return;
    }
    InfoFreeOwned(info);
    info->cookie = 0u;
    RoboFree(info);
}

uint32_t FontColorInfoCapabilities(const FontColorInfo *info)
{
    return info && info->cookie == INFO_COOKIE ? info->capabilities : 0u;
}

int FontColorInfoIsColorCapable(const FontColorInfo *info)
{
    return info && info->cookie == INFO_COOKIE &&
           (info->capabilities & ~RoboFontColorCapCpal) != 0u;
}

uint16_t FontColorInfoColrVersion(const FontColorInfo *info)
{
    return info && info->cookie == INFO_COOKIE ? info->colrVersion : 0u;
}

void FontColorPaletteSelectionInit(FontColorPaletteSelection *sel)
{
    if (!sel) {
        return;
    }
    memset(sel, 0, sizeof(*sel));
}

uint16_t FontColorInfoPaletteCount(const FontColorInfo *info)
{
    return info && info->cookie == INFO_COOKIE ? info->paletteCount : 0u;
}

Result FontColorInfoPalette(const FontColorInfo *info,
                                           uint16_t paletteIndex,
                                           FontColorPalette *out)
{
    if (!info || info->cookie != INFO_COOKIE || !out) {
        return RoboInvalidArgument;
    }
    if (paletteIndex >= info->paletteCount || !info->palettes) {
        return RoboNotFound;
    }
    *out = info->palettes[paletteIndex];
    return RoboOk;
}

Result FontColorInfoPaletteEntry(const FontColorInfo *info,
                                                 uint16_t paletteIndex,
                                                 uint16_t entryIndex,
                                                 FontColorRgba *out)
{
    FontColorPalette pal;
    Result res;
    uint16_t colorIndex;

    if (!out) {
        return RoboInvalidArgument;
    }
    res = FontColorInfoPalette(info, paletteIndex, &pal);
    if (res != RoboOk) {
        return res;
    }
    if (entryIndex >= pal.entryCount) {
        return RoboNotFound;
    }
    colorIndex = (uint16_t)(pal.firstColorIndex + entryIndex);
    if (colorIndex >= info->numColorRecords || !info->colors) {
        return RoboParseError;
    }
    *out = info->colors[colorIndex];
    return RoboOk;
}

Result FontColorResolvePaletteColor(
    const FontColorInfo *info,
    const FontColorPaletteSelection *sel,
    uint16_t paletteIndex,
    FontColorRgba *out)
{
    FontColorPalette pal;
    Result res;
    uint16_t selected;
    uint16_t i;

    if (!info || info->cookie != INFO_COOKIE || !sel || !out) {
        return RoboInvalidArgument;
    }
    if (paletteIndex == RoboFontColorForegroundPalette) {
        return RoboNotFound;
    }
    selected = sel->paletteIndex;
    if (selected >= info->paletteCount) {
        return RoboNotFound;
    }
    if (sel->hasOverrides && sel->overrideIndices && sel->overrideColors) {
        for (i = 0u; i < sel->overrideCount; i++) {
            if (sel->overrideIndices[i] == paletteIndex) {
                *out = sel->overrideColors[i];
                return RoboOk;
            }
        }
    }
    res = FontColorInfoPalette(info, selected, &pal);
    if (res != RoboOk) {
        return res;
    }
    if (paletteIndex >= pal.entryCount) {
        return RoboNotFound;
    }
    return FontColorInfoPaletteEntry(info, selected, paletteIndex, out);
}

Result FontColorInfoGlyph(const FontColorInfo *info,
                                         GlyphId glyphId,
                                         FontColorGlyph *out)
{
    const FontColorColrV0Base *v0;
    const FontColorGlyphSvg *svg;

    if (!info || info->cookie != INFO_COOKIE || !out) {
        return RoboInvalidArgument;
    }
    if (info->numGlyphs > 0u && glyphId >= info->numGlyphs) {
        return RoboNotFound;
    }
    memset(out, 0, sizeof(*out));
    out->glyphId = glyphId;
    out->paintRoot = NODE_NONE;

    if (GlyphHasV1Paint(info, glyphId)) {
        out->format = RoboFontColorGlyphPaint;
        return RoboOk;
    }
    v0 = ColrV0BaseFind(info, glyphId);
    if (v0) {
        out->format = RoboFontColorGlyphLayers;
        out->layerCount = v0->numLayers;
        out->layers = &info->colrV0Layers[v0->firstLayer];
        return RoboOk;
    }
    if ((info->capabilities & RoboFontColorCapSbix) != 0u) {
        FontColorGlyphImage img;
        if (SbixGlyphImage(info, glyphId, 0u, &img) == RoboOk) {
            out->format = RoboFontColorGlyphBitmap;
            out->hasImage = 1u;
            return RoboOk;
        }
    }
    if ((info->capabilities & RoboFontColorCapCblc) != 0u &&
        (info->capabilities & RoboFontColorCapCbdt) != 0u) {
        FontColorGlyphImage img;
        if (CblcGlyphImage(info, glyphId, 0u, &img) == RoboOk) {
            out->format = RoboFontColorGlyphBitmap;
            out->hasImage = 1u;
            return RoboOk;
        }
    }
    svg = SvgDocForGlyph(info, glyphId);
    if (svg) {
        out->format = RoboFontColorGlyphSvg;
        out->hasSvg = 1u;
        return RoboOk;
    }
    out->format = RoboFontColorGlyphMonochrome;
    return RoboOk;
}

Result FontColorInfoGlyphLayers(const FontColorInfo *info,
                                                GlyphId glyphId,
                                                const FontColorGlyphLayer **outLayers,
                                                uint32_t *outCount)
{
    const FontColorColrV0Base *v0;

    if (!info || info->cookie != INFO_COOKIE) {
        return RoboInvalidArgument;
    }
    if (info->numGlyphs > 0u && glyphId >= info->numGlyphs) {
        return RoboNotFound;
    }
    v0 = ColrV0BaseFind(info, glyphId);
    if (!v0) {
        return RoboNotFound;
    }
    if (outLayers) {
        *outLayers = &info->colrV0Layers[v0->firstLayer];
    }
    if (outCount) {
        *outCount = v0->numLayers;
    }
    return RoboOk;
}

Result FontColorInfoGlyphPaint(const FontColorInfo *info,
                                               GlyphId glyphId,
                                               const FontColorPaint **outNodes,
                                               uint32_t *outNodeCount,
                                               uint32_t *outRootIndex)
{
    FontColorInfo *mut = (FontColorInfo *)info;
    FontColorPaintCache *cache;
    Result res;

    if (!info || info->cookie != INFO_COOKIE) {
        return RoboInvalidArgument;
    }
    if (info->numGlyphs > 0u && glyphId >= info->numGlyphs) {
        return RoboNotFound;
    }
    if (!ColrV1BaseFind(info, glyphId)) {
        return RoboNotFound;
    }
    res = PaintBuildForGlyph(mut, glyphId, &cache);
    if (res != RoboOk) {
        return res;
    }
    if (outNodes) {
        *outNodes = cache->nodes;
    }
    if (outNodeCount) {
        *outNodeCount = cache->nodeCount;
    }
    if (outRootIndex) {
        *outRootIndex = cache->rootIndex;
    }
    return RoboOk;
}

Result FontColorInfoGlyphImage(const FontColorInfo *info,
                                               GlyphId glyphId,
                                               uint16_t preferPpem,
                                               FontColorGlyphImage *out)
{
    Result res;

    if (!info || info->cookie != INFO_COOKIE || !out) {
        return RoboInvalidArgument;
    }
    if (info->numGlyphs > 0u && glyphId >= info->numGlyphs) {
        return RoboNotFound;
    }
    if ((info->capabilities & RoboFontColorCapSbix) != 0u) {
        res = SbixGlyphImage(info, glyphId, preferPpem, out);
        if (res == RoboOk) {
            return RoboOk;
        }
        if (res != RoboNotFound && res != RoboInvalidArgument) {
            return res;
        }
    }
    if ((info->capabilities & RoboFontColorCapCblc) != 0u &&
        (info->capabilities & RoboFontColorCapCbdt) != 0u) {
        return CblcGlyphImage(info, glyphId, preferPpem, out);
    }
    return RoboNotFound;
}

Result FontColorInfoGlyphSvg(const FontColorInfo *info,
                                             GlyphId glyphId,
                                             FontColorGlyphSvg *out)
{
    const FontColorGlyphSvg *doc;

    if (!info || info->cookie != INFO_COOKIE || !out) {
        return RoboInvalidArgument;
    }
    if (info->numGlyphs > 0u && glyphId >= info->numGlyphs) {
        return RoboNotFound;
    }
    doc = SvgDocForGlyph(info, glyphId);
    if (!doc) {
        return RoboNotFound;
    }
    *out = *doc;
    return RoboOk;
}

const FontVariationCoords *FontColorInfoVariationCoords(
    const FontColorInfo *info)
{
    return info && info->cookie == INFO_COOKIE ? info->coords : NULL;
}

uint16_t FontColorInfoSbixStrikeCount(const FontColorInfo *info)
{
    return info && info->cookie == INFO_COOKIE ? info->sbixStrikeCount : 0u;
}

uint16_t FontColorInfoCblcStrikeCount(const FontColorInfo *info)
{
    return info && info->cookie == INFO_COOKIE ? info->cblcStrikeCount : 0u;
}

uint32_t FontColorInfoSvgDocumentCount(const FontColorInfo *info)
{
    return info && info->cookie == INFO_COOKIE ? info->svgDocCount : 0u;
}

Result FontColorFuzzParse(const uint8_t *data, size_t size)
{
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontColorInfo *info = NULL;
    FontColorGlyph glyph;
    FontColorGlyphImage img;
    FontColorGlyphSvg svg;
    FontColorPaint const *nodes = NULL;
    uint32_t nodeCount = 0u;
    uint32_t root = 0u;
    GlyphId gid;

    if (!data && size != 0u) {
        return RoboInvalidArgument;
    }
    if (size == 0u) {
        return RoboOk;
    }
    if (size > 256u * 1024u) {
        size = 256u * 1024u;
    }
    if (FontSourceCreateMemoryCopy(data, size, &src) != RoboOk) {
        return RoboOk;
    }
    if (FontSfntParse(src, 0u, 0u, &sfnt) != RoboOk) {
        FontSourceRelease(src);
        return RoboOk;
    }
    if (FontColorInfoCreate(sfnt, NULL, &info) == RoboOk && info) {
        gid = (GlyphId)(data[0] | ((uint32_t)data[(size > 1u ? 1u : 0u)] << 8));
        (void)FontColorInfoCapabilities(info);
        (void)FontColorInfoPaletteCount(info);
        (void)FontColorInfoGlyph(info, gid, &glyph);
        (void)FontColorInfoGlyphLayers(info, gid, NULL, NULL);
        (void)FontColorInfoGlyphPaint(info, gid, &nodes, &nodeCount, &root);
        (void)FontColorInfoGlyphImage(info, gid, data[0], &img);
        (void)FontColorInfoGlyphSvg(info, gid, &svg);
        FontColorInfoRelease(info);
    }
    FontSfntRelease(sfnt);
    FontSourceRelease(src);
    return RoboOk;
}
