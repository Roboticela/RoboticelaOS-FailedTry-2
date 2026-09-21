/*
 * RoboFontCoverage — cmap-backed Unicode coverage (Goal 5.14).
 */

#include "RoboFont/RoboFontCoverage.h"

#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontOutline.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct FontCoverage {
    uint32_t refs;
    FontCmapMap *cmap; /* retained */
    FontCoverageRange *ranges;
    uint32_t rangeCount;
    uint32_t rangeCap;
    UnicodeScript *scripts;
    uint32_t scriptCount;
    uint8_t *scriptSeen; /* RoboUScriptCount */
    FontCoverageStats stats;
};

typedef struct {
    FontCoverage *cov;
    const FontCoverageOptions *opt;
    uint32_t visited;
    uint32_t openFirst;
    uint32_t openLast;
    int open;
} CoverageBuild;

void FontCoverageOptionsInit(FontCoverageOptions *opt)
{
    if (!opt) {
        return;
    }
    memset(opt, 0, sizeof(*opt));
    opt->analyzeScripts = 1;
    opt->analyzeCategories = 1;
    opt->analyzeEmoji = 1;
    opt->maxRanges = RoboFontCoverageMaxRanges;
    opt->maxVisit = RoboFontCoverageMaxVisit;
}

static uint32_t OptMaxRanges(const FontCoverageOptions *opt)
{
    uint32_t m = opt && opt->maxRanges ? opt->maxRanges : RoboFontCoverageMaxRanges;
    return m > RoboFontCoverageMaxRanges ? RoboFontCoverageMaxRanges : m;
}

static uint32_t OptMaxVisit(const FontCoverageOptions *opt)
{
    uint32_t m = opt && opt->maxVisit ? opt->maxVisit : RoboFontCoverageMaxVisit;
    return m > RoboFontCoverageMaxVisit ? RoboFontCoverageMaxVisit : m;
}

static void CoverageClear(FontCoverage *cov)
{
    if (!cov) {
        return;
    }
    if (cov->cmap) {
        FontCmapMapRelease(cov->cmap);
        cov->cmap = NULL;
    }
    RoboFree(cov->ranges);
    RoboFree(cov->scripts);
    RoboFree(cov->scriptSeen);
    memset(cov, 0, sizeof(*cov));
}

static Result FlushOpenRange(CoverageBuild *b)
{
    FontCoverage *cov;
    uint32_t maxR;
    if (!b || !b->open) {
        return RoboOk;
    }
    cov = b->cov;
    maxR = OptMaxRanges(b->opt);
    if (cov->rangeCount >= maxR) {
        cov->stats.truncated = 1;
        b->open = 0;
        return RoboOk;
    }
    if (cov->rangeCount >= cov->rangeCap) {
        uint32_t ncap = cov->rangeCap ? cov->rangeCap * 2u : 16u;
        FontCoverageRange *nr;
        if (ncap > maxR) {
            ncap = maxR;
        }
        if (ncap <= cov->rangeCap) {
            cov->stats.truncated = 1;
            b->open = 0;
            return RoboOk;
        }
        nr = (FontCoverageRange *)Realloc(
            cov->ranges, (size_t)ncap * sizeof(*nr));
        if (!nr) {
            return RoboOutOfMemory;
        }
        cov->ranges = nr;
        cov->rangeCap = ncap;
    }
    cov->ranges[cov->rangeCount].first = b->openFirst;
    cov->ranges[cov->rangeCount].last = b->openLast;
    cov->rangeCount++;
    b->open = 0;
    return RoboOk;
}

static Result AddScript(FontCoverage *cov, UnicodeScript sc)
{
    if (sc == RoboUScriptCommon) {
        cov->stats.hasCommon = 1;
        return RoboOk;
    }
    if (sc == RoboUScriptInherited) {
        cov->stats.hasInherited = 1;
        return RoboOk;
    }
    if ((unsigned)sc >= (unsigned)RoboUScriptCount) {
        return RoboOk;
    }
    if (!cov->scriptSeen) {
        return RoboOk;
    }
    if (cov->scriptSeen[sc]) {
        return RoboOk;
    }
    if (cov->scriptCount >= RoboFontCoverageMaxScripts) {
        cov->stats.truncated = 1;
        return RoboOk;
    }
    cov->scriptSeen[sc] = 1;
    cov->scripts[cov->scriptCount++] = sc;
    return RoboOk;
}

static Result AnalyzeCp(CoverageBuild *b, uint32_t cp)
{
    FontCoverage *cov = b->cov;
    const FontCoverageOptions *opt = b->opt;
    UnicodeScript scx[16];
    size_t n;
    size_t i;

    if (opt && opt->analyzeCategories) {
        UnicodeGeneralCategory gc = UnicodeCategory((Uchar32)cp);
        if ((unsigned)gc < 32u) {
            cov->stats.categoryBits |= (1u << (unsigned)gc);
        }
    }
    if (opt && opt->analyzeEmoji && UnicodeIsEmoji((Uchar32)cp)) {
        cov->stats.emojiScalars++;
    }
    if (!opt || !opt->analyzeScripts) {
        return RoboOk;
    }
    n = UnicodeScriptExtensions((Uchar32)cp, scx,
                                       sizeof(scx) / sizeof(scx[0]));
    if (n == 0u) {
        return AddScript(cov, MakeUnicodeScript((Uchar32)cp));
    }
    for (i = 0u; i < n && i < sizeof(scx) / sizeof(scx[0]); i++) {
        Result res = AddScript(cov, scx[i]);
        if (res != RoboOk) {
            return res;
        }
    }
    return RoboOk;
}

static Result OnMapped(uint32_t cp, GlyphId gid, void *user)
{
    CoverageBuild *b = (CoverageBuild *)user;
    FontCoverage *cov = b->cov;
    uint32_t maxV = OptMaxVisit(b->opt);
    uint32_t numGlyphs;
    Result res;

    if (b->visited >= maxV) {
        cov->stats.truncated = 1;
        return RoboNotSupported; /* abort foreach early */
    }
    b->visited++;
    cov->stats.mappedScalars++;

    numGlyphs = FontCmapMapNumGlyphs(cov->cmap);
    if (gid >= numGlyphs) {
        cov->stats.invalidMappings++;
        return RoboOk;
    }
    if (gid == 0u) {
        cov->stats.notdefMappings++;
        return RoboOk;
    }

    cov->stats.usableMappings++;
    if (cov->stats.minCodePoint > cp) {
        cov->stats.minCodePoint = cp;
    }
    if (cov->stats.maxCodePoint < cp) {
        cov->stats.maxCodePoint = cp;
    }

    res = AnalyzeCp(b, cp);
    if (res != RoboOk) {
        return res;
    }

    if (!b->open) {
        b->open = 1;
        b->openFirst = cp;
        b->openLast = cp;
        return RoboOk;
    }
    if (cp == b->openLast + 1u) {
        b->openLast = cp;
        return RoboOk;
    }
    res = FlushOpenRange(b);
    if (res != RoboOk) {
        return res;
    }
    b->open = 1;
    b->openFirst = cp;
    b->openLast = cp;
    return RoboOk;
}

Result FontCoverageCreateFromCmap(
    const FontCmapMap *map,
    const FontCoverageOptions *opt,
    FontCoverage **out)
{
    FontCoverage *cov;
    FontCoverageOptions local;
    CoverageBuild build;
    Result res;

    if (!out || !map) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    if (!opt) {
        FontCoverageOptionsInit(&local);
        opt = &local;
    }

    cov = (FontCoverage *)RoboCalloc(1u, sizeof(*cov));
    if (!cov) {
        return RoboOutOfMemory;
    }
    cov->refs = 1u;
    cov->stats.minCodePoint = 0xFFFFFFFFu;
    cov->stats.maxCodePoint = 0u;
    cov->stats.hasUvs = FontCmapMapHasUvs(map);

    cov->scriptSeen = (uint8_t *)RoboCalloc((size_t)RoboUScriptCount, 1u);
    cov->scripts = (UnicodeScript *)RoboCalloc(RoboFontCoverageMaxScripts,
                                                        sizeof(UnicodeScript));
    if (!cov->scriptSeen || !cov->scripts) {
        CoverageClear(cov);
        RoboFree(cov);
        return RoboOutOfMemory;
    }

    FontCmapMapRetain((FontCmapMap *)map);
    cov->cmap = (FontCmapMap *)map;

    memset(&build, 0, sizeof(build));
    build.cov = cov;
    build.opt = opt;
    res = FontCmapForeachMapped(map, OnMapped, &build);
    if (res == RoboNotSupported && cov->stats.truncated) {
        res = RoboOk; /* visit limit — keep partial coverage */
    }
    if (res == RoboOk) {
        res = FlushOpenRange(&build);
    }
    if (res != RoboOk) {
        CoverageClear(cov);
        RoboFree(cov);
        return res;
    }

    if (cov->stats.usableMappings == 0u) {
        cov->stats.minCodePoint = 0u;
    }
    cov->stats.rangeCount = cov->rangeCount;
    cov->stats.scriptCount = cov->scriptCount;
    *out = cov;
    return RoboOk;
}

Result FontCoverageCreateFromFace(
    FontFace *face,
    const FontCoverageOptions *opt,
    FontCoverage **out)
{
    const FontCoverage *existing;
    Result res;

    if (!out || !face) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    (void)opt; /* face-attached coverage always uses default analysis options */

    existing = FontFaceCoverage(face);
    if (existing) {
        FontCoverageRetain((FontCoverage *)existing);
        *out = (FontCoverage *)existing;
        return RoboOk;
    }
    res = FontFaceParseCoverage(face);
    if (res != RoboOk) {
        return res;
    }
    existing = FontFaceCoverage(face);
    if (!existing) {
        return RoboNotFound;
    }
    FontCoverageRetain((FontCoverage *)existing);
    *out = (FontCoverage *)existing;
    return RoboOk;
}

void FontCoverageRetain(FontCoverage *cov)
{
    if (cov) {
        cov->refs++;
    }
}

void FontCoverageRelease(FontCoverage *cov)
{
    if (!cov) {
        return;
    }
    if (cov->refs > 1u) {
        cov->refs--;
        return;
    }
    CoverageClear(cov);
    RoboFree(cov);
}

const FontCmapMap *FontCoverageCmap(const FontCoverage *cov)
{
    return cov ? cov->cmap : NULL;
}

const FontCoverageStats *GetFontCoverageStats(
    const FontCoverage *cov)
{
    return cov ? &cov->stats : NULL;
}

uint32_t FontCoverageRangeCount(const FontCoverage *cov)
{
    return cov ? cov->rangeCount : 0u;
}

const FontCoverageRange *GetFontCoverageRange(
    const FontCoverage *cov, uint32_t index)
{
    if (!cov || index >= cov->rangeCount) {
        return NULL;
    }
    return &cov->ranges[index];
}

uint32_t FontCoverageScriptCount(const FontCoverage *cov)
{
    return cov ? cov->scriptCount : 0u;
}

UnicodeScript FontCoverageScript(const FontCoverage *cov,
                                                uint32_t index)
{
    if (!cov || index >= cov->scriptCount) {
        return RoboUScriptUnknown;
    }
    return cov->scripts[index];
}

int FontCoverageHasScript(const FontCoverage *cov,
                                  UnicodeScript script)
{
    uint32_t i;
    if (!cov) {
        return 0;
    }
    if (script == RoboUScriptCommon) {
        return cov->stats.hasCommon;
    }
    if (script == RoboUScriptInherited) {
        return cov->stats.hasInherited;
    }
    for (i = 0u; i < cov->scriptCount; i++) {
        if (cov->scripts[i] == script) {
            return 1;
        }
    }
    return 0;
}

static int RangeContains(const FontCoverage *cov, uint32_t cp)
{
    uint32_t lo = 0u;
    uint32_t hi;
    if (!cov || cov->rangeCount == 0u) {
        return 0;
    }
    hi = cov->rangeCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (cp > cov->ranges[mid].last) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo >= cov->rangeCount) {
        return 0;
    }
    return cp >= cov->ranges[lo].first && cp <= cov->ranges[lo].last;
}

int FontCoverageContains(const FontCoverage *cov, uint32_t codePoint)
{
    if (!cov || !FontCmapIsUnicodeScalar(codePoint)) {
        return 0;
    }
    return RangeContains(cov, codePoint);
}

Result FontCoverageQuery(const FontCoverage *cov,
                                       uint32_t codePoint,
                                       FontCoverageQueryResult *out)
{
    FontCmapLookupResult lr;
    Result res;

    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->outlineHint = RoboFontCoverageOutlineUnknown;
    if (!cov || !cov->cmap) {
        out->status = RoboFontCoverageNoCmap;
        return RoboOk;
    }
    if (!FontCmapIsUnicodeScalar(codePoint)) {
        out->status = RoboFontCoverageInvalidCodePoint;
        return RoboOk;
    }
    res = FontCmapLookup(cov->cmap, codePoint, &lr);
    if (res != RoboOk) {
        return res;
    }
    if (lr.status == RoboFontCmapInvalidCodePoint) {
        out->status = RoboFontCoverageInvalidCodePoint;
        return RoboOk;
    }
    if (lr.status == RoboFontCmapInvalidFont) {
        out->status = RoboFontCoverageInvalidMapping;
        return RoboOk;
    }
    if (lr.status == RoboFontCmapUnmapped) {
        out->status = RoboFontCoverageUnsupported;
        return RoboOk;
    }
    /* MAPPED */
    out->glyphId = lr.glyphId;
    if (lr.glyphId == 0u) {
        out->status = RoboFontCoverageMappedNotdef;
    } else if (lr.glyphId >= FontCmapMapNumGlyphs(cov->cmap)) {
        out->status = RoboFontCoverageInvalidMapping;
    } else {
        out->status = RoboFontCoverageMappedUsable;
    }
    return RoboOk;
}

Result FontCoverageGlyph(const FontCoverage *cov,
                                       uint32_t codePoint,
                                       GlyphId *outGid)
{
    FontCoverageQueryResult q;
    Result res;

    if (!outGid) {
        return RoboInvalidArgument;
    }
    *outGid = 0u;
    res = FontCoverageQuery(cov, codePoint, &q);
    if (res != RoboOk) {
        return res;
    }
    if (q.status != RoboFontCoverageMappedUsable) {
        return RoboNotFound;
    }
    *outGid = q.glyphId;
    return RoboOk;
}

Result FontCoverageQueryUvs(const FontCoverage *cov,
                                           uint32_t baseCodePoint,
                                           uint32_t variationSelector,
                                           FontCmapUvsResult *out)
{
    if (!cov || !cov->cmap) {
        if (out) {
            memset(out, 0, sizeof(*out));
            out->status = RoboFontCmapUvsNone;
            out->baseCodePoint = baseCodePoint;
            out->variationSelector = variationSelector;
        }
        return cov ? RoboOk : RoboInvalidArgument;
    }
    return FontCmapLookupUvs(cov->cmap, baseCodePoint, variationSelector,
                                     out);
}

Result FontCoverageProbeOutline(FontFace *face,
                                               GlyphId glyphId,
                                               FontCoverageOutlineHint *out)
{
    FontOutline *ol = NULL;
    Result res;
    FontOutlineStatus st;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = RoboFontCoverageOutlineUnknown;
    if (!face) {
        return RoboInvalidArgument;
    }
    res = FontFaceGlyphOutline(face, glyphId, &ol);
    if (res != RoboOk) {
        return res;
    }
    if (!ol) {
        return RoboOk;
    }
    st = MakeFontOutlineStatus(ol);
    if (st == RoboFontOutlineStatusEmpty) {
        *out = RoboFontCoverageOutlineEmpty;
    } else if (st == RoboFontOutlineStatusOk) {
        *out = RoboFontCoverageOutlineNonempty;
    }
    FontOutlineRelease(ol);
    return RoboOk;
}

uint64_t FontCoverageApproxBytes(const FontCoverage *cov)
{
    if (!cov) {
        return 0u;
    }
    return (uint64_t)sizeof(*cov) +
           (uint64_t)cov->rangeCap * (uint64_t)sizeof(FontCoverageRange) +
           (uint64_t)RoboFontCoverageMaxScripts * (uint64_t)sizeof(UnicodeScript) +
           (uint64_t)RoboUScriptCount;
}

Result FontCoverageFuzzParse(const uint8_t *data, size_t size)
{
    FontCmapMap *map = NULL;
    FontCoverage *cov = NULL;
    FontCoverageOptions opt;
    Result res;
    FontSpan span;

    if (!data && size > 0u) {
        return RoboInvalidArgument;
    }
    (void)FontCmapFuzzParse(data, size);
    span.data = data;
    span.size = size;
    res = FontCmapMapCreateFromSpan(span, 256u, &map);
    if (res != RoboOk || !map) {
        return res == RoboOutOfMemory ? res : RoboOk;
    }
    FontCoverageOptionsInit(&opt);
    opt.maxVisit = 4096u;
    opt.maxRanges = 256u;
    res = FontCoverageCreateFromCmap(map, &opt, &cov);
    if (cov) {
        FontCoverageQueryResult q;
        (void)FontCoverageQuery(cov, 0x41u, &q);
        (void)FontCoverageContains(cov, 0x41u);
        FontCoverageRelease(cov);
    }
    FontCmapMapRelease(map);
    return res == RoboOutOfMemory ? res : RoboOk;
}
