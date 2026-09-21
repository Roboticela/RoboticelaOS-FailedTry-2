#include "RoboFont/RoboFontMetrics.h"

#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct FontMetricsInfo {
    uint32_t refs;
    FontTt *tt; /* retained */
    FontMetricsGlobal global;
    FontVhea vhea;
    uint8_t hasVhea;
    FontSpan vmtx;
    uint16_t numberOfVMetrics;
};

void FontMetricsClear(FontMetrics *m)
{
    if (!m) {
        return;
    }
    memset(m, 0, sizeof(*m));
}

Result FontMetricsInit(FontMetrics *m,
                                     uint16_t unitsPerEm,
                                     FontUnit ascent,
                                     FontUnit descent,
                                     FontUnit lineGap,
                                     const FontBbox *bbox)
{
    if (!m || unitsPerEm == 0u || descent < 0) {
        return RoboInvalidArgument;
    }
    memset(m, 0, sizeof(*m));
    m->unitsPerEm = unitsPerEm;
    m->ascent = ascent;
    m->descent = descent;
    m->lineGap = lineGap;
    if (bbox) {
        m->bbox = *bbox;
        m->hasBbox = 1u;
    }
    m->valid = 1u;
    return RoboOk;
}

FontUnit FontMetricsLineHeight(const FontMetrics *m)
{
    if (!m || !m->valid) {
        return 0;
    }
    return m->ascent + m->descent + m->lineGap;
}

Result FontUnitsToPixels(FontUnit units,
                                        uint16_t unitsPerEm,
                                        FontPixelSize pixelSize,
                                        float *outPixels)
{
    if (!outPixels || unitsPerEm == 0u || !(pixelSize > 0.0f)) {
        return RoboInvalidArgument;
    }
    *outPixels = ((float)units * pixelSize) / (float)unitsPerEm;
    return RoboOk;
}

Result FontPixelsToUnits(float pixels,
                                        uint16_t unitsPerEm,
                                        FontPixelSize pixelSize,
                                        FontUnit *outUnits)
{
    double v;
    if (!outUnits || unitsPerEm == 0u || !(pixelSize > 0.0f)) {
        return RoboInvalidArgument;
    }
    v = ((double)pixels * (double)unitsPerEm) / (double)pixelSize;
    if (v >= 0.0) {
        *outUnits = (FontUnit)(v + 0.5);
    } else {
        *outUnits = (FontUnit)(v - 0.5);
    }
    return RoboOk;
}

FontPixelSize FontPointsToPixels(FontPointSize points)
{
    return points * (96.0f / 72.0f);
}

int FontUnitsPerEmIsValid(uint16_t unitsPerEm)
{
    return unitsPerEm >= RoboFontUpemMin && unitsPerEm <= RoboFontUpemMax;
}

static double DFloor(double v)
{
    double i = (double)(long long)v;
    if (v >= 0.0 || i == v) {
        return i;
    }
    return i - 1.0;
}

static double DCeil(double v)
{
    double i = (double)(long long)v;
    if (v <= 0.0 || i == v) {
        return i;
    }
    return i + 1.0;
}

static float ApplyRound(double v, FontRoundMode mode)
{
    switch (mode) {
    case RoboFontRoundFloor:
        return (float)DFloor(v);
    case RoboFontRoundCeil:
        return (float)DCeil(v);
    case RoboFontRoundTowardZero:
        return (float)(long long)v;
    case RoboFontRoundNearest:
    default:
        if (v >= 0.0) {
            return (float)DFloor(v + 0.5);
        }
        return (float)DCeil(v - 0.5);
    }
}

Result FontScaleFontUnits(FontUnit value,
                                         uint16_t unitsPerEm,
                                         FontPixelSize fontSize,
                                         FontRoundMode roundMode,
                                         float *outScaled)
{
    double scaled;
    if (!outScaled || !FontUnitsPerEmIsValid(unitsPerEm) ||
        !(fontSize > 0.0f)) {
        return RoboInvalidArgument;
    }
    /* Multiply before divide using double to avoid int overflow. */
    scaled = ((double)value * (double)fontSize) / (double)unitsPerEm;
    *outScaled = ApplyRound(scaled, roundMode);
    return RoboOk;
}

/* ---- vhea ---- */

Result FontVheaParse(FontSpan span, FontVhea *out)
{
    FontReader r;
    int16_t reserved[4];
    int i;
    if (!out || span.size < 36u) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    FontReaderInitSpan(&r, span);
    if (FontReadU16be(&r, &out->majorVersion) != RoboOk ||
        FontReadU16be(&r, &out->minorVersion) != RoboOk ||
        FontReadI16be(&r, &out->ascent) != RoboOk ||
        FontReadI16be(&r, &out->descent) != RoboOk ||
        FontReadI16be(&r, &out->lineGap) != RoboOk ||
        FontReadI16be(&r, &out->advanceHeightMax) != RoboOk ||
        FontReadI16be(&r, &out->minTopSideBearing) != RoboOk ||
        FontReadI16be(&r, &out->minBottomSideBearing) != RoboOk ||
        FontReadI16be(&r, &out->yMaxExtent) != RoboOk ||
        FontReadI16be(&r, &out->caretSlopeRise) != RoboOk ||
        FontReadI16be(&r, &out->caretSlopeRun) != RoboOk ||
        FontReadI16be(&r, &out->caretOffset) != RoboOk) {
        return RoboParseError;
    }
    for (i = 0; i < 4; i++) {
        if (FontReadI16be(&r, &reserved[i]) != RoboOk) {
            return RoboParseError;
        }
    }
    if (FontReadI16be(&r, &out->metricDataFormat) != RoboOk ||
        FontReadU16be(&r, &out->numberOfVMetrics) != RoboOk) {
        return RoboParseError;
    }
    if (out->majorVersion != 1u || out->metricDataFormat != 0) {
        return RoboParseError;
    }
    if (out->numberOfVMetrics == 0u) {
        return RoboParseError;
    }
    return RoboOk;
}

static size_t ExpectedVmtxSize(uint16_t numGlyphs, uint16_t nVMetrics)
{
    if (nVMetrics == 0u || nVMetrics > numGlyphs) {
        return 0u;
    }
    return (size_t)nVMetrics * 4u + (size_t)(numGlyphs - nVMetrics) * 2u;
}

static FontUnit MagDescender(int16_t d)
{
    return d < 0 ? (FontUnit)(-d) : (FontUnit)d;
}

static void SetOptUnit(FontOptionalUnit *o,
                         FontUnit v,
                         FontMetricAvail a)
{
    o->value = v;
    o->avail = a;
}

static void SetOptU16(FontOptionalU16 *o,
                        uint16_t v,
                        FontMetricAvail a)
{
    o->value = v;
    o->avail = a;
}

static void FillGlobalFromTt(FontTt *tt, FontMetricsGlobal *g)
{
    const FontHead *head = FontTtHead(tt);
    const FontHhea *hhea = FontTtHhea(tt);
    const FontOs2 *os2 = FontTtOs2(tt);
    const FontPost *post = FontTtPost(tt);
    FontUnit ascent;
    FontUnit descent;
    FontUnit gap;
    FontBbox bb;
    int useTypo = 0;

    memset(g, 0, sizeof(*g));
    g->numGlyphs = FontTtNumGlyphs(tt);
    g->numberOfHMetrics = hhea->numberOfHMetrics;

    if (FontUnitsPerEmIsValid(head->unitsPerEm)) {
        g->unitsPerEm = head->unitsPerEm;
        g->upemAvail = RoboFontMetricPresent;
    } else {
        g->unitsPerEm = 0u;
        g->upemAvail = RoboFontMetricMalformed;
    }

    g->hheaAscender = hhea->ascender;
    g->hheaDescenderMag = MagDescender(hhea->descender);
    g->hheaLineGap = hhea->lineGap < 0 ? 0 : (FontUnit)hhea->lineGap;
    g->hasHheaLine = 1u;

    g->maxAdvanceWidth = hhea->advanceWidthMax;
    g->minLeftSideBearing = hhea->minLeftSideBearing;
    g->minRightSideBearing = hhea->minRightSideBearing;
    g->maxXExtent = hhea->xMaxExtent;
    g->hasHheaExtrema = 1u;

    /* Caret: OT permits (1,0) when both rise and run are 0. */
    if (hhea->caretSlopeRise == 0 && hhea->caretSlopeRun == 0) {
        g->caretSlopeRise = 1;
        g->caretSlopeRun = 0;
        g->caretOffset = 0;
        g->caretAvail = RoboFontMetricFallback;
    } else {
        g->caretSlopeRise = hhea->caretSlopeRise;
        g->caretSlopeRun = hhea->caretSlopeRun;
        g->caretOffset = hhea->caretOffset;
        g->caretAvail = RoboFontMetricPresent;
    }

    if (os2) {
        g->typoAscender = os2->sTypoAscender;
        g->typoDescenderMag = MagDescender(os2->sTypoDescender);
        g->typoLineGap =
            os2->sTypoLineGap < 0 ? 0 : (FontUnit)os2->sTypoLineGap;
        g->hasTypoLine = 1u;
        g->winAscent = os2->usWinAscent;
        g->winDescent = os2->usWinDescent;
        g->hasWinClip = 1u;
        useTypo = (os2->fsSelection & RoboFontOs2UseTypoMetrics) != 0u;

        SetOptUnit(&g->averageCharWidth, os2->xAvgCharWidth,
                     RoboFontMetricPresent);
        SetOptU16(&g->usWeightClass, os2->usWeightClass,
                    RoboFontMetricPresent);
        SetOptU16(&g->usWidthClass, os2->usWidthClass,
                    RoboFontMetricPresent);
        SetOptUnit(&g->strikeoutThickness, os2->yStrikeoutSize,
                     RoboFontMetricPresent);
        SetOptUnit(&g->strikeoutPosition, os2->yStrikeoutPosition,
                     RoboFontMetricPresent);
        if (os2->hasV2Metrics) {
            SetOptUnit(&g->xHeight, os2->sxHeight, RoboFontMetricPresent);
            SetOptUnit(&g->capHeight, os2->sCapHeight, RoboFontMetricPresent);
        } else {
            SetOptUnit(&g->xHeight, 0, RoboFontMetricUnavailable);
            SetOptUnit(&g->capHeight, 0, RoboFontMetricUnavailable);
        }
    } else {
        SetOptUnit(&g->averageCharWidth, 0, RoboFontMetricUnavailable);
        SetOptU16(&g->usWeightClass, 0, RoboFontMetricUnavailable);
        SetOptU16(&g->usWidthClass, 0, RoboFontMetricUnavailable);
        SetOptUnit(&g->strikeoutThickness, 0, RoboFontMetricUnavailable);
        SetOptUnit(&g->strikeoutPosition, 0, RoboFontMetricUnavailable);
        SetOptUnit(&g->xHeight, 0, RoboFontMetricUnavailable);
        SetOptUnit(&g->capHeight, 0, RoboFontMetricUnavailable);
    }

    if (post) {
        SetOptUnit(&g->underlinePosition, post->underlinePosition,
                     RoboFontMetricPresent);
        SetOptUnit(&g->underlineThickness, post->underlineThickness,
                     RoboFontMetricPresent);
    } else {
        /* Spec-permitted soft defaults; clearly marked FALLBACK. */
        SetOptUnit(&g->underlinePosition, (FontUnit)(-(int)g->unitsPerEm / 5),
                     RoboFontMetricFallback);
        SetOptUnit(&g->underlineThickness, (FontUnit)(g->unitsPerEm / 14),
                     RoboFontMetricFallback);
    }

    if (head->xMin <= head->xMax && head->yMin <= head->yMax) {
        g->fontBbox.xMin = head->xMin;
        g->fontBbox.yMin = head->yMin;
        g->fontBbox.xMax = head->xMax;
        g->fontBbox.yMax = head->yMax;
        g->fontBboxAvail = RoboFontMetricPresent;
    } else {
        g->fontBboxAvail = RoboFontMetricMalformed;
    }

    if (useTypo && g->hasTypoLine) {
        ascent = g->typoAscender;
        descent = g->typoDescenderMag;
        gap = g->typoLineGap;
        g->layoutSource = RoboFontLineFromOs2Typo;
    } else {
        ascent = g->hheaAscender;
        descent = g->hheaDescenderMag;
        gap = g->hheaLineGap;
        g->layoutSource = RoboFontLineFromHhea;
    }

    bb = g->fontBbox;
    if (g->upemAvail == RoboFontMetricPresent) {
        (void)FontMetricsInit(
            &g->layout, g->unitsPerEm, ascent, descent, gap,
            g->fontBboxAvail == RoboFontMetricPresent ? &bb : NULL);
    }
}

static Result TryAttachVertical(FontMetricsInfo *info)
{
    const FontSfnt *sfnt;
    FontSpan vheaS;
    FontSpan vmtxS;
    size_t need;
    Result res;

    sfnt = FontTtSfnt(info->tt);
    if (!sfnt) {
        return RoboOk;
    }
    if (FontSfntTableBytes(sfnt, RoboFontTagVhea, &vheaS) != RoboOk ||
        FontSfntTableBytes(sfnt, RoboFontTagVmtx, &vmtxS) != RoboOk) {
        return RoboOk; /* optional */
    }
    res = FontVheaParse(vheaS, &info->vhea);
    if (res != RoboOk) {
        return RoboOk; /* leave hasVertical=0 on malformed optional tables */
    }
    if (info->vhea.numberOfVMetrics > info->global.numGlyphs) {
        return RoboOk;
    }
    need = ExpectedVmtxSize(info->global.numGlyphs, info->vhea.numberOfVMetrics);
    if (need == 0u || vmtxS.size < need) {
        return RoboOk;
    }
    info->hasVhea = 1u;
    info->vmtx = vmtxS;
    info->numberOfVMetrics = info->vhea.numberOfVMetrics;
    info->global.hasVertical = 1u;
    return RoboOk;
}

Result FontMetricsInfoCreateFromTt(FontTt *tt,
                                                    FontMetricsInfo **out)
{
    FontMetricsInfo *info;
    if (!tt || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    info = (FontMetricsInfo *)RoboCalloc(1u, sizeof(*info));
    if (!info) {
        return RoboOutOfMemory;
    }
    info->refs = 1u;
    FontTtRetain(tt);
    info->tt = tt;
    FillGlobalFromTt(tt, &info->global);
    if (info->global.upemAvail != RoboFontMetricPresent || !info->global.layout.valid) {
        FontMetricsInfoRelease(info);
        return RoboParseError;
    }
    (void)TryAttachVertical(info);
    *out = info;
    return RoboOk;
}

Result FontMetricsInfoCreate(FontSfnt *sfnt,
                                            FontMetricsInfo **out)
{
    FontTt *tt = NULL;
    Result res;
    if (!sfnt || !out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    /* CFF/CFF2 fonts share SFNT metrics tables; do not require glyf. */
    res = FontTtParse(sfnt, RoboFontTtFlagsNone, &tt);
    if (res != RoboOk) {
        return res;
    }
    res = FontMetricsInfoCreateFromTt(tt, out);
    FontTtRelease(tt);
    return res;
}

void FontMetricsInfoRetain(FontMetricsInfo *info)
{
    if (!info) {
        return;
    }
    info->refs++;
}

void FontMetricsInfoRelease(FontMetricsInfo *info)
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
    if (info->tt) {
        FontTtRelease(info->tt);
        info->tt = NULL;
    }
    RoboFree(info);
}

const FontMetricsGlobal *FontMetricsInfoGlobal(
    const FontMetricsInfo *info)
{
    return info ? &info->global : NULL;
}

Result FontMetricsInfoLayout(const FontMetricsInfo *info,
                                            FontMetrics *out)
{
    if (!info || !out) {
        return RoboInvalidArgument;
    }
    *out = info->global.layout;
    return RoboOk;
}

const FontVhea *FontMetricsInfoVhea(
    const FontMetricsInfo *info)
{
    if (!info || !info->hasVhea) {
        return NULL;
    }
    return &info->vhea;
}

static Result FillGlyphBbox(const FontTt *tt,
                                     GlyphId gid,
                                     FontGlyphMetrics *gm)
{
    FontGlyfGlyph g;
    Result res;
    uint32_t len = 0u;

    gm->hasBbox = 0u;
    gm->hasRsb = 0u;
    if (FontTtGlyphLength(tt, gid, &len) != RoboOk) {
        /* No glyf/loca (CFF path) — advances still valid. */
        return RoboOk;
    }
    if (len == 0u) {
        /* Empty glyph: RSB = advance - LSB (zero width). */
        gm->rightSideBearing =
            (int16_t)((int32_t)gm->advanceWidth - (int32_t)gm->leftSideBearing);
        gm->hasRsb = 1u;
        memset(&gm->bbox, 0, sizeof(gm->bbox));
        gm->hasBbox = 1u;
        return RoboOk;
    }
    memset(&g, 0, sizeof(g));
    res = FontTtParseGlyph(tt, gid, &g);
    if (res != RoboOk) {
        FontGlyfGlyphClear(&g);
        return res;
    }
    gm->bbox.xMin = g.header.xMin;
    gm->bbox.yMin = g.header.yMin;
    gm->bbox.xMax = g.header.xMax;
    gm->bbox.yMax = g.header.yMax;
    gm->hasBbox = 1u;
    {
        int32_t width = (int32_t)g.header.xMax - (int32_t)g.header.xMin;
        int32_t rsb =
            (int32_t)gm->advanceWidth - (int32_t)gm->leftSideBearing - width;
        if (rsb < -32768 || rsb > 32767) {
            gm->hasRsb = 0u;
        } else {
            gm->rightSideBearing = (int16_t)rsb;
            gm->hasRsb = 1u;
        }
    }
    FontGlyfGlyphClear(&g);
    return RoboOk;
}

Result FontMetricsInfoGlyph(const FontMetricsInfo *info,
                                           GlyphId glyphId,
                                           FontGlyphMetrics *out)
{
    uint16_t aw = 0u;
    int16_t lsb = 0;
    Result res;

    if (!info || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->glyphId = glyphId;
    if (glyphId >= info->global.numGlyphs) {
        out->status = RoboFontGlyphMetricsInvalidId;
        return RoboOk;
    }
    res = FontTtAdvanceWidth(info->tt, glyphId, &aw);
    if (res != RoboOk) {
        out->status = RoboFontGlyphMetricsMalformed;
        return RoboOk;
    }
    res = FontTtLeftSideBearing(info->tt, glyphId, &lsb);
    if (res != RoboOk) {
        out->status = RoboFontGlyphMetricsMalformed;
        return RoboOk;
    }
    out->advanceWidth = aw;
    out->leftSideBearing = lsb;
    res = FillGlyphBbox(info->tt, glyphId, out);
    if (res == RoboParseError) {
        out->status = RoboFontGlyphMetricsMalformed;
        return RoboOk;
    }
    if (res != RoboOk) {
        return res;
    }
    out->status = RoboFontGlyphMetricsOk;
    return RoboOk;
}

Result FontMetricsInfoGlyphVertical(
    const FontMetricsInfo *info,
    GlyphId glyphId,
    FontVerticalGlyphMetrics *out)
{
    FontReader r;
    uint16_t idx;
    uint16_t ah;
    int16_t tsb;

    if (!info || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->glyphId = glyphId;
    if (!info->hasVhea) {
        out->status = RoboFontGlyphMetricsUnavailable;
        return RoboOk;
    }
    if (glyphId >= info->global.numGlyphs) {
        out->status = RoboFontGlyphMetricsInvalidId;
        return RoboOk;
    }
    idx = (glyphId < info->numberOfVMetrics)
              ? (uint16_t)glyphId
              : (uint16_t)(info->numberOfVMetrics - 1u);
    FontReaderInitSpan(&r, info->vmtx);
    if (FontReaderSeek(&r, (size_t)idx * 4u) != RoboOk ||
        FontReadU16be(&r, &ah) != RoboOk ||
        FontReadI16be(&r, &tsb) != RoboOk) {
        out->status = RoboFontGlyphMetricsMalformed;
        return RoboOk;
    }
    if (glyphId >= info->numberOfVMetrics) {
        size_t off = (size_t)info->numberOfVMetrics * 4u +
                     (size_t)(glyphId - info->numberOfVMetrics) * 2u;
        if (FontReaderSeek(&r, off) != RoboOk ||
            FontReadI16be(&r, &tsb) != RoboOk) {
            out->status = RoboFontGlyphMetricsMalformed;
            return RoboOk;
        }
    }
    out->advanceHeight = ah;
    out->topSideBearing = tsb;
    out->status = RoboFontGlyphMetricsOk;
    return RoboOk;
}

Result FontMetricsFuzzParse(const uint8_t *data, size_t size)
{
    FontSource *src = NULL;
    FontSfnt *sfnt = NULL;
    FontMetricsInfo *info = NULL;
    Result res;

    if (!data || size == 0u) {
        return RoboOk;
    }
    if (FontSourceCreateMemoryBorrow(data, size, &src) != RoboOk) {
        return RoboOk;
    }
    res = FontSfntParse(src, 0u, RoboFontSfntFlagsNone, &sfnt);
    if (res == RoboOk) {
        (void)FontMetricsInfoCreate(sfnt, &info);
        if (info) {
            FontGlyphMetrics gm;
            FontVerticalGlyphMetrics vm;
            (void)FontMetricsInfoGlyph(info, 0u, &gm);
            (void)FontMetricsInfoGlyphVertical(info, 0u, &vm);
            FontMetricsInfoRelease(info);
        }
        FontSfntRelease(sfnt);
    }
    /* Also fuzz standalone vhea bytes. */
    if (size >= 36u) {
        FontVhea vhea;
        FontSpan sp;
        sp.data = data;
        sp.size = size > 64u ? 64u : size;
        (void)FontVheaParse(sp, &vhea);
    }
    FontSourceRelease(src);
    return RoboOk;
}
