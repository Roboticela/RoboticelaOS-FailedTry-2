/*
 * RoboGraphics glyph coverage rasterization suite (Phase 12.1) — `graphics.glyph.*`.
 *
 * The claims this release makes, each pinned where it can be observed rather than argued:
 *
 *   coverage is fractional      a pixel the ink half-covers carries half the coverage, and the
 *                                composite of it is a *value* the pipeline's own arithmetic
 *                                predicts — not a thresholded edge;
 *
 *   the winding is real          a glyph with a counter (`o`'s hole) is rasterized as a ring: the
 *                                hole's pixels carry no coverage at all, which only a non-zero
 *                                winding rule produces;
 *
 *   curves are antialiased       the same claim on a curved outline, where the mask must hold
 *                                many distinct intermediate values and be symmetric about its
 *                                own centre;
 *
 *   coverage ≠ alpha             the same mask composited with an opaque colour and with a
 *                                half-transparent one differs by exactly the colour's own alpha,
 *                                because the two are separate factors meeting once;
 *
 *   gamma is a documented model  the three models are pinned at their endpoints, checked
 *                                monotone, and the two polarity models are checked against the
 *                                linear-light mixture they exist to reproduce;
 *
 *   bounds are safe              negative origins, clipping, overhanging ink and a glyph whose
 *                                coordinates are astronomically large all resolve without writing
 *                                a pixel outside the target;
 *
 *   the output is deterministic  the same face, glyph, size and origin produce byte-identical
 *                                masks and byte-identical pixels, twice;
 *
 *   the text bridge reaches it   `RoboGraphicsText.h`'s ALPHA mode and the mask door this suite
 *                                calls directly produce the same bytes, so the mode the paint
 *                                stack selects *is* the rasterizer this suite measures.
 *
 * The face is synthetic and built here — a long `loca`, six tables, four glyphs (empty, square,
 * curved blob, square with a counter) — stated as numbers so every expectation is arithmetic. No
 * system font, no random sampling, and no test framework beyond the runtime's own.
 */

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsGlyph.h"
#include "RoboGraphics/RoboGraphicsText.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* A synthetic face, built here                                        */
/* ------------------------------------------------------------------ */

/*
 * units per em   1000   so `sizePx` 100 makes one font unit 0.1 px
 * glyph 0        empty  `.notdef`
 * glyph 1        square (0,0)-(100,100)
 * glyph 2        curved blob: four on-curve extremes joined by four quadratic arcs
 * glyph 3        ring: the square above, with a (25,25)-(75,75) counter wound the other way
 * advances       500 for the empty glyph, 600 for the other three
 *
 * Every shape is a round number so a test's expectation can be written down rather than computed,
 * and the ring exists only so that a rasterizer which dropped the winding direction would fail a
 * case rather than a picture.
 */
#define GlyphUpem 1000u
#define GlyphAdvance 600u
#define GlyphNotdefAdvance 500u
#define GlyphNumGlyphs 4u
#define GlyphSquareUnits 100

#define GlyphIdNotdef 0u
#define GlyphIdSquare 1u
#define GlyphIdBlob 2u
#define GlyphIdRing 3u

typedef struct SynthTable {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} SynthTable;

typedef struct SynthPoint {
    int16_t x;
    int16_t y;
    uint8_t onCurve;
} SynthPoint;

static void PutU8(uint8_t **p, uint8_t v) { *(*p)++ = v; }

static void PutU16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}

static void PutI16(uint8_t **p, int16_t v) { PutU16(p, (uint16_t)v); }

static void PutU32(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 24);
    *(*p)++ = (uint8_t)(v >> 16);
    *(*p)++ = (uint8_t)(v >> 8);
    *(*p)++ = (uint8_t)v;
}

static void WriteSearchFields(uint8_t *hdr, uint16_t numTables)
{
    uint16_t pow2 = 1u;
    uint16_t entry = 0u;
    uint8_t *p;

    while ((uint16_t)(pow2 * 2u) <= numTables) {
        pow2 = (uint16_t)(pow2 * 2u);
        entry++;
    }
    p = hdr + 4;
    PutU16(&p, numTables);
    PutU16(&p, (uint16_t)(pow2 * 16u));
    PutU16(&p, entry);
    PutU16(&p, (uint16_t)(numTables * 16u - pow2 * 16u));
}

static size_t BuildSfnt(uint8_t *out,
                        size_t outCap,
                        uint32_t version,
                        const SynthTable *tables,
                        uint32_t count)
{
    size_t dirEnd = 12u + (size_t)count * 16u;
    size_t cursor = dirEnd;
    size_t need = dirEnd;
    uint32_t i;

    for (i = 0u; i < count; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > outCap) {
        return 0u;
    }
    memset(out, 0, need);
    {
        uint8_t *p = out;

        PutU32(&p, version);
    }
    WriteSearchFields(out, (uint16_t)count);
    for (i = 0u; i < count; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *p = out + 12u + (size_t)i * 16u;

        PutU32(&p, tables[i].tag);
        PutU32(&p, 0u);
        PutU32(&p, (uint32_t)cursor);
        PutU32(&p, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return need;
}

static size_t BuildHead(uint8_t *b, uint16_t upem, int16_t xMax, int16_t yMax)
{
    uint8_t *p = b;

    PutU16(&p, 1);              /* majorVersion */
    PutU16(&p, 0);              /* minorVersion */
    PutU32(&p, 0x00010000u);    /* fontRevision */
    PutU32(&p, 0u);             /* checkSumAdjustment */
    PutU32(&p, RoboFontHeadMagic);
    PutU16(&p, 0u);             /* flags */
    PutU16(&p, upem);           /* unitsPerEm */
    PutU32(&p, 0u);             /* created (high) */
    PutU32(&p, 0u);             /* created (low) */
    PutU32(&p, 0u);             /* modified (high) */
    PutU32(&p, 0u);             /* modified (low) */
    PutI16(&p, 0);              /* xMin */
    PutI16(&p, 0);              /* yMin */
    PutI16(&p, xMax);
    PutI16(&p, yMax);
    PutU16(&p, 0u);             /* macStyle */
    PutU16(&p, 8u);             /* lowestRecPPEM */
    PutI16(&p, 2);              /* fontDirectionHint */
    PutI16(&p, 1);              /* indexToLocFormat: long */
    PutI16(&p, 0);              /* glyphDataFormat */
    return (size_t)(p - b);
}

static size_t BuildHhea(uint8_t *b, uint16_t numberOfHMetrics)
{
    uint8_t *p = b;
    int i;

    PutU16(&p, 1);
    PutU16(&p, 0);
    PutI16(&p, 800);   /* ascender */
    PutI16(&p, -200);  /* descender */
    PutI16(&p, 90);    /* lineGap */
    PutU16(&p, 900);   /* advanceWidthMax */
    PutI16(&p, -10);   /* minLeftSideBearing */
    PutI16(&p, -20);   /* minRightSideBearing */
    PutI16(&p, 880);   /* xMaxExtent */
    PutI16(&p, 1);     /* caretSlopeRise */
    PutI16(&p, 0);     /* caretSlopeRun */
    PutI16(&p, 0);
    for (i = 0; i < 4; i++) {
        PutI16(&p, 0); /* reserved */
    }
    PutI16(&p, 0);     /* metricDataFormat */
    PutU16(&p, numberOfHMetrics);
    return (size_t)(p - b);
}

static size_t BuildMaxp(uint8_t *b, uint16_t numGlyphs, uint16_t maxPoints, uint16_t maxContours)
{
    uint8_t *p = b;
    int i;

    PutU32(&p, 0x00010000u);
    PutU16(&p, numGlyphs);
    PutU16(&p, maxPoints);
    PutU16(&p, maxContours);
    for (i = 0; i < 12; i++) {
        PutU16(&p, 0);
    }
    return (size_t)(p - b);
}

static size_t BuildHmtx(uint8_t *b, const uint16_t *advances, const int16_t *bearings, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;

    for (i = 0u; i < n; i++) {
        PutU16(&p, advances[i]);
        PutI16(&p, bearings[i]);
    }
    return (size_t)(p - b);
}

/**
 * One simple `glyf` glyph: every point's coordinates written as signed 16-bit deltas.
 *
 * The encoding is the deliberate one: no `X_SHORT`/`Y_SHORT`/`SAME` bits, so a flag byte is only
 * ever `ON_CURVE` or zero and the whole glyph is a function a reader can check by hand. The
 * interior points therefore cost more bytes than a real font's would, which is the trade this
 * fixture is happy to make.
 */
static size_t BuildGlyfSimple(uint8_t *out,
                              const SynthPoint *points,
                              const uint16_t *ends,
                              int contourCount,
                              int16_t xMax,
                              int16_t yMax)
{
    uint8_t *p = out;
    int16_t n = (int16_t)(ends[contourCount - 1] + 1);
    int16_t prev;
    int i;

    PutI16(&p, (int16_t)contourCount);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, xMax);
    PutI16(&p, yMax);
    for (i = 0; i < contourCount; i++) {
        PutU16(&p, ends[i]);
    }
    PutU16(&p, 0u); /* instructionLength */
    for (i = 0; i < (int)n; i++) {
        PutU8(&p, points[i].onCurve ? (uint8_t)RoboFontGlyfFlagOnCurve : (uint8_t)0u);
    }
    prev = 0;
    for (i = 0; i < (int)n; i++) {
        PutI16(&p, (int16_t)(points[i].x - prev));
        prev = points[i].x;
    }
    prev = 0;
    for (i = 0; i < (int)n; i++) {
        PutI16(&p, (int16_t)(points[i].y - prev));
        prev = points[i].y;
    }
    return (size_t)(p - out);
}

/** Pad a glyph to a 4-byte boundary, the way a real `loca` expects its offsets to be. */
static uint32_t Pad4(size_t value)
{
    return (uint32_t)((value + 3u) & ~(size_t)3u);
}

static Result MakeGlyphFace(FontSource **outSource, FontFace **outFace)
{
    uint8_t head[64];
    uint8_t hhea[64];
    uint8_t maxp[64];
    uint8_t hmtx[64];
    uint8_t loca[64];
    uint8_t glyf[512];
    uint8_t font[1024];
    uint16_t advances[GlyphNumGlyphs];
    int16_t bearings[GlyphNumGlyphs];
    uint32_t offsets[GlyphNumGlyphs + 1u];
    SynthTable tables[6];
    SynthPoint square[4];
    SynthPoint blob[8];
    SynthPoint ring[8];
    uint16_t squareEnds[1];
    uint16_t blobEnds[1];
    uint16_t ringEnds[2];
    size_t glyfLength = 0u;
    uint8_t *locaP = loca;
    size_t fontLength;
    uint32_t i;
    Result res;

    if (!outSource || !outFace) {
        return RoboInvalidArgument;
    }
    *outSource = NULL;
    *outFace = NULL;

    /* glyph 1 — the square, counter-clockwise in font design space. */
    square[0].x = 0;   square[0].y = 0;   square[0].onCurve = 1u;
    square[1].x = 100; square[1].y = 0;   square[1].onCurve = 1u;
    square[2].x = 100; square[2].y = 100; square[2].onCurve = 1u;
    square[3].x = 0;   square[3].y = 100; square[3].onCurve = 1u;
    squareEnds[0] = 3u;

    /* glyph 2 — four on-curve extremes joined by four quadratic arcs through the corners. */
    blob[0].x = 0;   blob[0].y = 50;  blob[0].onCurve = 1u;
    blob[1].x = 0;   blob[1].y = 0;   blob[1].onCurve = 0u;
    blob[2].x = 50;  blob[2].y = 0;   blob[2].onCurve = 1u;
    blob[3].x = 100; blob[3].y = 0;   blob[3].onCurve = 0u;
    blob[4].x = 100; blob[4].y = 50;  blob[4].onCurve = 1u;
    blob[5].x = 100; blob[5].y = 100; blob[5].onCurve = 0u;
    blob[6].x = 50;  blob[6].y = 100; blob[6].onCurve = 1u;
    blob[7].x = 0;   blob[7].y = 100; blob[7].onCurve = 0u;
    blobEnds[0] = 7u;

    /* glyph 3 — the square, and a counter wound the other way so the winding cancels inside. */
    ring[0].x = 0;   ring[0].y = 0;   ring[0].onCurve = 1u;
    ring[1].x = 100; ring[1].y = 0;   ring[1].onCurve = 1u;
    ring[2].x = 100; ring[2].y = 100; ring[2].onCurve = 1u;
    ring[3].x = 0;   ring[3].y = 100; ring[3].onCurve = 1u;
    ring[4].x = 25;  ring[4].y = 25;  ring[4].onCurve = 1u;
    ring[5].x = 25;  ring[5].y = 75;  ring[5].onCurve = 1u;
    ring[6].x = 75;  ring[6].y = 75;  ring[6].onCurve = 1u;
    ring[7].x = 75;  ring[7].y = 25;  ring[7].onCurve = 1u;
    ringEnds[0] = 3u;
    ringEnds[1] = 7u;

    memset(glyf, 0, sizeof(glyf));
    memset(loca, 0, sizeof(loca));
    memset(head, 0, sizeof(head));
    memset(hhea, 0, sizeof(hhea));
    memset(maxp, 0, sizeof(maxp));
    memset(hmtx, 0, sizeof(hmtx));
    memset(font, 0, sizeof(font));

    offsets[0] = 0u;
    offsets[1] = 0u; /* glyph 0 is empty: two equal offsets, which is how a font spells "no outline" */
    glyfLength = 0u;
    glyfLength += BuildGlyfSimple(glyf + glyfLength, square, squareEnds, 1, 100, 100);
    glyfLength = Pad4(glyfLength);
    offsets[2] = (uint32_t)glyfLength;
    glyfLength += BuildGlyfSimple(glyf + glyfLength, blob, blobEnds, 1, 100, 100);
    glyfLength = Pad4(glyfLength);
    offsets[3] = (uint32_t)glyfLength;
    glyfLength += BuildGlyfSimple(glyf + glyfLength, ring, ringEnds, 2, 100, 100);
    glyfLength = Pad4(glyfLength);
    offsets[4] = (uint32_t)glyfLength;

    for (i = 0u; i <= GlyphNumGlyphs; i++) {
        PutU32(&locaP, offsets[i]);
    }

    advances[0] = (uint16_t)GlyphNotdefAdvance;
    bearings[0] = 0;
    for (i = 1u; i < GlyphNumGlyphs; i++) {
        advances[i] = (uint16_t)GlyphAdvance;
        bearings[i] = 0;
    }

    tables[0].tag = RoboFontTagHead;
    tables[0].data = head;
    tables[0].length = (uint32_t)BuildHead(head, (uint16_t)GlyphUpem, 100, 100);
    tables[1].tag = RoboFontTagHhea;
    tables[1].data = hhea;
    tables[1].length = (uint32_t)BuildHhea(hhea, (uint16_t)GlyphNumGlyphs);
    tables[2].tag = RoboFontTagMaxp;
    tables[2].data = maxp;
    tables[2].length = (uint32_t)BuildMaxp(maxp, (uint16_t)GlyphNumGlyphs, 8u, 2u);
    tables[3].tag = RoboFontTagHmtx;
    tables[3].data = hmtx;
    tables[3].length = (uint32_t)BuildHmtx(hmtx, advances, bearings, (uint16_t)GlyphNumGlyphs);
    tables[4].tag = RoboFontTagLoca;
    tables[4].data = loca;
    tables[4].length = (uint32_t)((GlyphNumGlyphs + 1u) * 4u);
    tables[5].tag = RoboFontTagGlyf;
    tables[5].data = glyf;
    tables[5].length = (uint32_t)glyfLength;

    fontLength = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tables, 6u);
    if (fontLength == 0u) {
        return RoboOutOfMemory;
    }
    res = FontSourceCreateMemoryCopy(font, fontLength, outSource);
    if (res != RoboOk) {
        return res;
    }
    res = FontFaceCreate(*outSource, 0, outFace);
    if (res != RoboOk) {
        FontSourceRelease(*outSource);
        *outSource = NULL;
        return res;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static FontOutline *OutlineOf(FontFace *face, GlyphId glyphId)
{
    FontOutline *outline = NULL;

    if (FontFaceParseOutlines(face) != RoboOk) {
        return NULL;
    }
    if (FontFaceGlyphOutline(face, glyphId, &outline) != RoboOk) {
        return NULL;
    }
    return outline;
}

static Result MakeSurface(uint32_t width, uint32_t height, GfxSurface **out)
{
    GfxSurfaceDesc desc;

    GfxSurfaceDescInit(&desc);
    desc.width = width;
    desc.height = height;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    return GfxSurfaceCreate(&desc, out);
}

static void FillSurface(GfxSurface *surface, GfxColorRgba8 color)
{
    RoboCheckEqInt(GfxSurfaceClearRgba8(surface, color), RoboOk);
}

static GfxColorRgba8 PixelAt(GfxSurface *surface, uint32_t x, uint32_t y)
{
    GfxColorRgba8 color;

    memset(&color, 0, sizeof(color));
    RoboCheckEqInt(GfxSurfaceGetPixel(surface, x, y, &color), RoboOk);
    return color;
}

static int MaskEquals(const GfxGlyphMask *a, const GfxGlyphMask *b)
{
    size_t bytes;

    if (GfxGlyphMaskWidth(a) != GfxGlyphMaskWidth(b) ||
        GfxGlyphMaskHeight(a) != GfxGlyphMaskHeight(b) ||
        GfxGlyphMaskX(a) != GfxGlyphMaskX(b) || GfxGlyphMaskY(a) != GfxGlyphMaskY(b)) {
        return 0;
    }
    bytes = (size_t)GfxGlyphMaskWidth(a) * (size_t)GfxGlyphMaskHeight(a);
    if (bytes == 0u) {
        return 1;
    }
    return memcmp(GfxGlyphMaskCoverage(a), GfxGlyphMaskCoverage(b), bytes) == 0;
}

static int SurfaceEquals(GfxSurface *a, GfxSurface *b)
{
    uint32_t y;

    if (GfxSurfaceWidth(a) != GfxSurfaceWidth(b) || GfxSurfaceHeight(a) != GfxSurfaceHeight(b)) {
        return 0;
    }
    for (y = 0u; y < GfxSurfaceHeight(a); y++) {
        uint32_t x;

        for (x = 0u; x < GfxSurfaceWidth(a); x++) {
            GfxColorRgba8 ca = PixelAt(a, x, y);
            GfxColorRgba8 cb = PixelAt(b, x, y);

            if (ca.r != cb.r || ca.g != cb.g || ca.b != cb.b || ca.a != cb.a) {
                return 0;
            }
        }
    }
    return 1;
}

/** Distinct coverage values in a mask — the count that says "this edge is antialiased". */
static uint32_t DistinctCoverage(const GfxGlyphMask *mask)
{
    uint8_t seen[256];
    size_t total = (size_t)GfxGlyphMaskWidth(mask) * (size_t)GfxGlyphMaskHeight(mask);
    const uint8_t *coverage = GfxGlyphMaskCoverage(mask);
    size_t index;
    uint32_t distinct = 0u;

    memset(seen, 0, sizeof(seen));
    for (index = 0u; index < total; index++) {
        seen[coverage[index]] = 1u;
    }
    for (index = 0u; index < 256u; index++) {
        if (seen[index] != 0u) {
            distinct++;
        }
    }
    return distinct;
}

static uint32_t PartialCoveragePixels(const GfxGlyphMask *mask)
{
    size_t total = (size_t)GfxGlyphMaskWidth(mask) * (size_t)GfxGlyphMaskHeight(mask);
    const uint8_t *coverage = GfxGlyphMaskCoverage(mask);
    size_t index;
    uint32_t count = 0u;

    for (index = 0u; index < total; index++) {
        if (coverage[index] > 0u && coverage[index] < 255u) {
            count++;
        }
    }
    return count;
}

/** Pixels that carry any ink at all. */
static uint32_t InkedPixels(GfxSurface *surface, GfxColorRgba8 paper)
{
    uint32_t y;
    uint32_t count = 0u;

    for (y = 0u; y < GfxSurfaceHeight(surface); y++) {
        uint32_t x;

        for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
            GfxColorRgba8 color = PixelAt(surface, x, y);

            if (color.r != paper.r || color.g != paper.g || color.b != paper.b) {
                count++;
            }
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Coverage                                                            */
/* ------------------------------------------------------------------ */

/** A square at an integer origin lands on pixel boundaries: every touched pixel is full. */
static void TestCoverageFullAndZero(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *mask = NULL;
    GfxGlyphRasterStats stats;
    uint32_t x;
    uint32_t y;

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdSquare);
    RoboCheck(outline != NULL);

    memset(&stats, 0, sizeof(stats));
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(5.0f, 20.0f), NULL, NULL, &mask,
                                                &stats),
                   RoboOk);
    /* 100 font units at 100px / 1000upem = 10 device pixels. */
    RoboCheckEqInt((int)GfxGlyphMaskX(mask), 5);
    RoboCheckEqInt((int)GfxGlyphMaskY(mask), 10);
    RoboCheckEqInt((int)GfxGlyphMaskWidth(mask), 10);
    RoboCheckEqInt((int)GfxGlyphMaskHeight(mask), 10);
    RoboCheckEqInt((int)stats.pixels, 100);
    RoboCheckEqInt((int)stats.covered, 100);
    RoboCheckEqInt((int)stats.full, 100);
    /* The square's two horizontal edges are dropped — they can never cross a scanline — so the
     * four sides are two edges. */
    RoboCheckEqInt((int)stats.segments, 2);
    RoboCheckEqInt((int)stats.samples, 64);

    for (y = 0u; y < 10u; y++) {
        for (x = 0u; x < 10u; x++) {
            RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, x, y), 255);
        }
    }
    /* The mask is the ink's own bounding box and nothing more: a read one column or one row past
     * it is out of range and answers zero rather than touching the next allocation. */
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 10u, 5u), 0);
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 0u, 10u), 0);
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 100u, 100u), 0);

    GfxGlyphMaskRelease(mask);
    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/** Half a pixel of ink is half the coverage, and the arithmetic is the pipeline's own. */
static void TestCoveragePartial(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *mask = NULL;
    GfxGlyphRasterStats stats;

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdSquare);
    RoboCheck(outline != NULL);

    /* Origin x = 5.5 puts the square's left edge halfway through pixel column 5. */
    memset(&stats, 0, sizeof(stats));
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(5.5f, 20.0f), NULL, NULL, &mask,
                                                &stats),
                   RoboOk);
    RoboCheckEqInt((int)GfxGlyphMaskX(mask), 5);
    RoboCheckEqInt((int)GfxGlyphMaskWidth(mask), 11);
    /* 32 of 64 samples = (32*255 + 32) / 64 = 128. */
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 0u, 5u), 128);
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 1u, 5u), 255);
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 10u, 5u), 128);
    RoboCheckEqInt((int)stats.full, 90);
    RoboCheckEqInt((int)stats.covered, 110);
    RoboCheck(PartialCoveragePixels(mask) == 20u);

    GfxGlyphMaskRelease(mask);
    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/**
 * The curved glyph is where antialiasing has to be visible: many distinct coverage values, a
 * continuum of partial pixels, and a mask symmetric about its own centre.
 */
static void TestCoverageCurve(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *mask = NULL;
    GfxGlyphRasterStats stats;
    uint32_t x;
    uint32_t y;

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdBlob);
    RoboCheck(outline != NULL);

    memset(&stats, 0, sizeof(stats));
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 200.0f,
                                                MakeGfxPointF(0.0f, 40.0f), NULL, NULL, &mask,
                                                &stats),
                   RoboOk);
    RoboCheckEqInt((int)GfxGlyphMaskWidth(mask), 20);
    RoboCheckEqInt((int)GfxGlyphMaskHeight(mask), 20);

    {
        uint32_t distinct = DistinctCoverage(mask);

        /* A binary rasterizer would report two (0 and 255). Anything below five would mean the
         * quantization, not the coverage, is what a reader is seeing. */
        RoboCheck(distinct >= 8u);
    }
    RoboCheck(PartialCoveragePixels(mask) > 20u);
    RoboCheck(stats.full > 100u);

    /* The blob is symmetric about (50, 50) in design space, so pixel (i, j) and its mirror
     * (19 - i, 19 - j) must carry the same coverage. A rasterizer that rounded one side of a curve
     * and not the other would fail here. */
    for (y = 0u; y < 20u; y++) {
        for (x = 0u; x < 20u; x++) {
            RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, x, y),
                           (int)GfxGlyphMaskCoverageAt(mask, 19u - x, 19u - y));
        }
    }

    /* The arcs cut the corners, so the extreme corner pixel holds no ink at all. */
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 0u, 0u), 0);

    GfxGlyphMaskRelease(mask);
    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/** A counter is a hole because the winding cancels, and nothing else makes it one. */
static void TestCoverageWinding(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *mask = NULL;

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdRing);
    RoboCheck(outline != NULL);

    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 200.0f,
                                                MakeGfxPointF(10.0f, 40.0f), NULL, NULL, &mask,
                                                NULL),
                   RoboOk);
    /* The 20x20 px outer square less the 10x10 px counter at +5,+5 within it. */
    RoboCheckEqInt((int)GfxGlyphMaskWidth(mask), 20);
    RoboCheckEqInt((int)GfxGlyphMaskHeight(mask), 20);
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 2u, 2u), 255);   /* the ring body */
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 10u, 10u), 0);   /* the hole */
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 10u, 2u), 255);  /* above the counter */
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 2u, 10u), 255);  /* left of the counter */

    GfxGlyphMaskRelease(mask);
    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

static void TestCoverageEmptyAndSmallAndLarge(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *mask = NULL;
    GfxGlyphRasterStats stats;

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);

    /* The empty glyph is a successful empty mask, not an error. */
    outline = OutlineOf(face, GlyphIdNotdef);
    RoboCheck(outline != NULL);
    memset(&stats, 0, sizeof(stats));
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(0.0f, 0.0f), NULL, NULL, &mask,
                                                &stats),
                   RoboOk);
    RoboCheck(GfxGlyphMaskIsEmpty(mask));
    RoboCheckEqInt((int)stats.pixels, 0);
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 0u, 0u), 0);
    GfxGlyphMaskRelease(mask);
    FontOutlineRelease(outline);

    /* Small: the same square at 4px is a 4x4 mask, and the supersampling still resolves it. */
    outline = OutlineOf(face, GlyphIdSquare);
    RoboCheck(outline != NULL);
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 40.0f,
                                                MakeGfxPointF(0.0f, 0.0f), NULL, NULL, &mask, NULL),
                   RoboOk);
    RoboCheckEqInt((int)GfxGlyphMaskWidth(mask), 4);
    RoboCheckEqInt((int)GfxGlyphMaskHeight(mask), 4);
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 0u, 0u), 255);
    GfxGlyphMaskRelease(mask);

    /* Large: 2000 units at 200px / 1000upem = 400 px on a side, 160000 pixels of ink. */
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 2000.0f,
                                                MakeGfxPointF(0.0f, 0.0f), NULL, NULL, &mask,
                                                &stats),
                   RoboOk);
    RoboCheckEqInt((int)GfxGlyphMaskWidth(mask), 200);
    RoboCheckEqInt((int)GfxGlyphMaskHeight(mask), 200);
    RoboCheckEqInt((int)stats.full, 40000);
    GfxGlyphMaskRelease(mask);
    FontOutlineRelease(outline);

    FontFaceRelease(face);
    FontSourceRelease(source);
}

/** A fractional origin is honored: the same glyph at x and x + 0.5 is a different mask. */
static void TestCoverageSubpixelPlacement(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *whole = NULL;
    GfxGlyphMask *half = NULL;

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdBlob);
    RoboCheck(outline != NULL);

    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(4.0f, 20.0f), NULL, NULL, &whole,
                                                NULL),
                   RoboOk);
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(4.5f, 20.0f), NULL, NULL, &half,
                                                NULL),
                   RoboOk);
    RoboCheck(!MaskEquals(whole, half));
    /* A rounded-to-integer rasterizer would have produced the same mask twice. */
    RoboCheck(PartialCoveragePixels(half) > 0u);

    GfxGlyphMaskRelease(whole);
    GfxGlyphMaskRelease(half);
    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/* ------------------------------------------------------------------ */
/* Bounds                                                             */
/* ------------------------------------------------------------------ */

static void TestBoundsClip(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *mask = NULL;
    GfxGlyphRasterStats stats;
    GfxRect clip = MakeGfxRect(8, 12, 4, 4);

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdSquare);
    RoboCheck(outline != NULL);

    memset(&stats, 0, sizeof(stats));
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(5.0f, 20.0f), &clip, NULL, &mask,
                                                &stats),
                   RoboOk);
    RoboCheckEqInt((int)GfxGlyphMaskX(mask), 8);
    RoboCheckEqInt((int)GfxGlyphMaskY(mask), 12);
    RoboCheckEqInt((int)GfxGlyphMaskWidth(mask), 4);
    RoboCheckEqInt((int)GfxGlyphMaskHeight(mask), 4);
    RoboCheckEqInt((int)stats.pixels, 16);
    RoboCheckEqInt((int)stats.clipped, 1);
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 0u, 0u), 255);

    /* A clip that misses the ink entirely is an empty mask and a success. */
    GfxGlyphMaskRelease(mask);
    clip = MakeGfxRect(200, 200, 8, 8);
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(5.0f, 20.0f), &clip, NULL, &mask,
                                                &stats),
                   RoboOk);
    RoboCheck(GfxGlyphMaskIsEmpty(mask));
    GfxGlyphMaskRelease(mask);

    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/** A negative origin is legal geometry: the ink is clipped at the surface, not refused. */
static void TestBoundsNegativeOrigin(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *mask = NULL;
    GfxSurface *surface = NULL;
    GfxRaster *raster = NULL;
    GfxRect clip = MakeGfxRect(0, 0, 16, 16);

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdSquare);
    RoboCheck(outline != NULL);
    RoboCheckEqInt(MakeSurface(16u, 16u, &surface), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(surface, &raster), RoboOk);
    FillSurface(surface, GfxRgb8(255, 255, 255));

    /* The square is 10x10 at origin (-4, 6): only its right/bottom corner is on the surface. */
    RoboCheckEqInt(GfxRasterSetClip(raster, clip), RoboOk);
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(-4.0f, 6.0f), &clip, NULL, &mask,
                                                NULL),
                   RoboOk);
    RoboCheckEqInt((int)GfxGlyphMaskX(mask), 0);
    RoboCheckEqInt((int)GfxGlyphMaskWidth(mask), 6);
    RoboCheckEqInt((int)GfxGlyphMaskHeight(mask), 6);
    RoboCheckEqInt((int)GfxRasterDrawGlyphMask(raster, mask, GfxRgb8(0, 0, 0),
                                               RoboGfxCoverageGammaLinear),
                   RoboOk);

    {
        GfxColorRgba8 inside = PixelAt(surface, 0u, 0u);
        GfxColorRgba8 outside = PixelAt(surface, 10u, 10u);

        RoboCheckEqInt((int)inside.r, 0);
        RoboCheckEqInt((int)outside.r, 255);
    }

    GfxGlyphMaskRelease(mask);
    GfxRasterDestroy(raster);
    GfxSurfaceRelease(surface);
    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/**
 * Coordinates no picture could hold.
 *
 * A 1e9 px font size makes the outline's device coordinates astronomically large; the quantizer
 * saturates them, the mask is bounded by the clip, and the whole call stays a no-op outside it —
 * which is the property the phase's "must never crash, must never write outside" asks for.
 */
static void TestBoundsExtremeCoordinates(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *mask = NULL;
    GfxRect clip = MakeGfxRect(0, 0, 32, 32);

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdSquare);
    RoboCheck(outline != NULL);

    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 1.0e9f,
                                                MakeGfxPointF(4.0f, 24.0f), &clip, NULL, &mask,
                                                NULL),
                   RoboOk);
    /* The saturated square starts at x = 4 (its origin) and the clip's right edge bounds it, so
     * the mask is the part of it the clip can hold -- 28 of the clip's 32 columns, 24 of its
     * rows. */
    RoboCheckEqInt((int)GfxGlyphMaskX(mask), 4);
    RoboCheckEqInt((int)GfxGlyphMaskWidth(mask), 28);
    RoboCheckEqInt((int)GfxGlyphMaskHeight(mask), 24);
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(mask, 16u, 16u), 255);
    GfxGlyphMaskRelease(mask);

    /* A negative size is a caller's mistake rather than a glyph that draws nothing. */
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, -1.0f,
                                                MakeGfxPointF(0.0f, 0.0f), &clip, NULL, &mask,
                                                NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, 0u, 10.0f, MakeGfxPointF(0.0f, 0.0f),
                                                &clip, NULL, &mask, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(NULL, (uint16_t)GlyphUpem, 10.0f,
                                                MakeGfxPointF(0.0f, 0.0f), &clip, NULL, &mask,
                                                NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 10.0f,
                                                MakeGfxPointF(0.0f, 0.0f), &clip, NULL, NULL, NULL),
                   RoboInvalidArgument);

    /* Unclipped, a glyph that large is refused rather than truncated. */
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 1.0e9f,
                                                MakeGfxPointF(0.0f, 0.0f), NULL, NULL, &mask, NULL),
                   RoboNotSupported);
    RoboCheck(mask == NULL);

    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/* ------------------------------------------------------------------ */
/* Gamma                                                              */
/* ------------------------------------------------------------------ */

static void TestGammaEndpointsAndMonotone(void)
{
    int model;

    for (model = 0; model <= 2; model++) {
        GfxCoverageGamma gamma = (GfxCoverageGamma)model;
        int value;

        RoboCheckEqInt((int)GfxGammaCoverageToAlpha(0u, gamma), 0);
        RoboCheckEqInt((int)GfxGammaCoverageToAlpha(255u, gamma), 255);

        for (value = 1; value <= 255; value++) {
            uint8_t previous = GfxGammaCoverageToAlpha((uint8_t)(value - 1), gamma);
            uint8_t current = GfxGammaCoverageToAlpha((uint8_t)value, gamma);

            RoboCheck(current >= previous);
        }
    }

    /* An unknown model is the linear one, not an out-of-bounds read. */
    RoboCheckEqInt((int)GfxGammaCoverageToAlpha(128u, (GfxCoverageGamma)99), 128);
    RoboCheckEqInt((int)GfxGammaCoverageToAlpha(128u, (GfxCoverageGamma)-3), 128);
}

static void TestGammaKnownValues(void)
{
    /* Linear writes coverage straight through. */
    RoboCheckEqInt((int)GfxGammaCoverageToAlpha(128u, RoboGfxCoverageGammaLinear), 128);
    RoboCheckEqInt((int)GfxGammaCoverageToAlpha(64u, RoboGfxCoverageGammaLinear), 64);

    /* LightOnDark is Esrgb(coverage) = round(255 · (128/255)^(1/2.4)) = 188. */
    RoboCheckEqInt((int)GfxGammaCoverageToAlpha(128u, RoboGfxCoverageGammaLightOnDark), 188);
    RoboCheckEqInt((int)GfxGammaCoverageToAlpha(64u, RoboGfxCoverageGammaLightOnDark), 137);

    /* DarkOnLight is the mirror image, 1 − Esrgb(1 − coverage): 255 − Esrgb(127) = 68, and
     * 255 − Esrgb(64) = 118. */
    RoboCheckEqInt((int)GfxGammaCoverageToAlpha(128u, RoboGfxCoverageGammaDarkOnLight), 68);
    RoboCheckEqInt((int)GfxGammaCoverageToAlpha(191u, RoboGfxCoverageGammaDarkOnLight), 118);

    /* Both polarity models sit on the side of coverage their derivation says they do. */
    {
        int value;

        for (value = 1; value < 255; value++) {
            RoboCheck(GfxGammaCoverageToAlpha((uint8_t)value, RoboGfxCoverageGammaLightOnDark) >=
                      (uint8_t)value);
            RoboCheck(GfxGammaCoverageToAlpha((uint8_t)value, RoboGfxCoverageGammaDarkOnLight) <=
                      (uint8_t)value);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Alpha                                                              */
/* ------------------------------------------------------------------ */

static void TestAlphaOpaqueAndPartial(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *mask = NULL;
    GfxSurface *surface = NULL;
    GfxRaster *raster = NULL;

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdSquare);
    RoboCheck(outline != NULL);
    RoboCheckEqInt(MakeSurface(32u, 32u, &surface), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(surface, &raster), RoboOk);

    /* Fully covered pixels get the glyph's colour exactly, and a transparent colour is a no-op. */
    FillSurface(surface, GfxRgb8(255, 255, 255));
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(5.0f, 20.0f), NULL, NULL, &mask,
                                                NULL),
                   RoboOk);
    RoboCheckEqInt(GfxRasterDrawGlyphMask(raster, mask, GfxRgba8(255, 0, 0, 255),
                                          RoboGfxCoverageGammaLinear),
                   RoboOk);
    {
        GfxColorRgba8 inside = PixelAt(surface, 10u, 15u);

        RoboCheckEqInt((int)inside.r, 255);
        RoboCheckEqInt((int)inside.g, 0);
        RoboCheckEqInt((int)inside.b, 0);
        RoboCheckEqInt((int)inside.a, 255);
    }
    RoboCheckEqInt(GfxRasterDrawGlyphMask(raster, mask, GfxRgba8(0, 0, 255, 0),
                                          RoboGfxCoverageGammaLinear),
                   RoboOk);
    {
        GfxColorRgba8 untouched = PixelAt(surface, 10u, 15u);

        /* A fully transparent colour leaves the destination alone -- the red is still red. */
        RoboCheckEqInt((int)untouched.r, 255);
        RoboCheckEqInt((int)untouched.g, 0);
        RoboCheckEqInt((int)untouched.b, 0);
    }
    GfxGlyphMaskRelease(mask);

    /* Half-covered pixel, opaque black on white, no gamma: (1 - 128/255) rounds to 127. */
    FillSurface(surface, GfxRgb8(255, 255, 255));
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(5.5f, 20.0f), NULL, NULL, &mask,
                                                NULL),
                   RoboOk);
    RoboCheckEqInt(GfxRasterDrawGlyphMask(raster, mask, GfxRgb8(0, 0, 0),
                                          RoboGfxCoverageGammaLinear),
                   RoboOk);
    {
        GfxColorRgba8 edge = PixelAt(surface, 5u, 14u);
        GfxColorRgba8 core = PixelAt(surface, 8u, 14u);

        RoboCheckEqInt((int)edge.r, 127);
        RoboCheckEqInt((int)edge.a, 255);
        RoboCheckEqInt((int)core.r, 0);
    }

    /*
     * And the gamma models are what makes that edge *correct*: dark ink on a light page at half
     * coverage is physically about 0.5^(1/2.2) · 255 = 185, which is the answer DarkOnLight
     * reaches (187) and the linear model misses (127). Light ink on a dark page is the mirror:
     * LightOnDark puts 188 where the linear model would put 128.
     */
    FillSurface(surface, GfxRgb8(255, 255, 255));
    RoboCheckEqInt(GfxRasterDrawGlyphMask(raster, mask, GfxRgb8(0, 0, 0),
                                          RoboGfxCoverageGammaDarkOnLight),
                   RoboOk);
    RoboCheckEqInt((int)PixelAt(surface, 5u, 14u).r, 187);
    RoboCheckEqInt((int)PixelAt(surface, 8u, 14u).r, 0);

    FillSurface(surface, GfxRgb8(0, 0, 0));
    RoboCheckEqInt(GfxRasterDrawGlyphMask(raster, mask, GfxRgb8(255, 255, 255),
                                          RoboGfxCoverageGammaLightOnDark),
                   RoboOk);
    RoboCheckEqInt((int)PixelAt(surface, 5u, 14u).r, 188);

    GfxGlyphMaskRelease(mask);
    GfxRasterDestroy(raster);
    GfxSurfaceRelease(surface);
    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/**
 * Coverage is not colour alpha.
 *
 * The same mask drawn twice — once with an opaque colour, once with a half-transparent one — must
 * differ by exactly the colour's own alpha, and both must equal the module's own blend door applied
 * to the same operands. Folding one into the other would fail one of the three.
 */
static void TestAlphaIsSeparateFromCoverage(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *mask = NULL;
    GfxSurface *surface = NULL;
    GfxRaster *raster = NULL;
    GfxColorRgba8 paper = GfxRgb8(255, 255, 255);
    GfxColorRgba8 opaque = GfxRgba8(20, 40, 60, 255);
    GfxColorRgba8 translucent = GfxRgba8(20, 40, 60, 128);

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdSquare);
    RoboCheck(outline != NULL);
    RoboCheckEqInt(MakeSurface(24u, 24u, &surface), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(surface, &raster), RoboOk);
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(5.5f, 20.0f), NULL, NULL, &mask,
                                                NULL),
                   RoboOk);

    /* Opacity 255, opaque colour: the glyph's own channels, unchanged. */
    FillSurface(surface, paper);
    RoboCheckEqInt(GfxRasterDrawGlyphMask(raster, mask, opaque, RoboGfxCoverageGammaLinear),
                   RoboOk);
    {
        GfxColorRgba8 full = PixelAt(surface, 8u, 14u);

        RoboCheckEqInt((int)full.r, 20);
        RoboCheckEqInt((int)full.g, 40);
        RoboCheckEqInt((int)full.b, 60);
    }

    /* The same pixel with the colour's own alpha: the compositor's answer for the same operands,
     * computed through the module's own door rather than re-derived here. */
    FillSurface(surface, paper);
    RoboCheckEqInt(GfxRasterDrawGlyphMask(raster, mask, translucent, RoboGfxCoverageGammaLinear),
                   RoboOk);
    {
        GfxColorRgba8 got = PixelAt(surface, 8u, 14u);
        GfxColorRgba8 want = GfxCompositeSrcOverOpacity(translucent, paper, 255u);

        RoboCheckEqInt((int)got.r, (int)want.r);
        RoboCheckEqInt((int)got.g, (int)want.g);
        RoboCheckEqInt((int)got.b, (int)want.b);
        RoboCheck(got.r != 20u); /* it really did blend */
    }

    /* And raster opacity multiplies in as a third factor, through the raster's own door. */
    FillSurface(surface, paper);
    GfxRasterSetOpacity(raster, 128u);
    RoboCheckEqInt(GfxRasterDrawGlyphMask(raster, mask, opaque, RoboGfxCoverageGammaLinear),
                   RoboOk);
    {
        GfxColorRgba8 got = PixelAt(surface, 8u, 14u);
        GfxColorRgba8 want = GfxCompositeSrcOverOpacity(opaque, paper, 128u);

        RoboCheckEqInt((int)got.r, (int)want.r);
        RoboCheckEqInt((int)got.g, (int)want.g);
        RoboCheckEqInt((int)got.b, (int)want.b);
    }
    GfxRasterSetOpacity(raster, 0u);
    {
        GfxColorRgba8 before = PixelAt(surface, 8u, 14u);

        RoboCheckEqInt(GfxRasterDrawGlyphMask(raster, mask, opaque, RoboGfxCoverageGammaLinear),
                       RoboOk);
        RoboCheckEqInt((int)PixelAt(surface, 8u, 14u).r, (int)before.r);
    }
    GfxRasterSetOpacity(raster, 255u);

    GfxGlyphMaskRelease(mask);
    GfxRasterDestroy(raster);
    GfxSurfaceRelease(surface);
    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/** The raster's clip bounds the drawing, and the mask's own rectangle never escapes it. */
static void TestAlphaClipAndSafety(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *mask = NULL;
    GfxSurface *surface = NULL;
    GfxRaster *raster = NULL;

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdSquare);
    RoboCheck(outline != NULL);
    RoboCheckEqInt(MakeSurface(32u, 32u, &surface), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(surface, &raster), RoboOk);

    /* A mask built without a clip, overhanging the surface in every direction. */
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(-3.0f, 4.0f), NULL, NULL, &mask, NULL),
                   RoboOk);
    RoboCheckEqInt((int)GfxGlyphMaskX(mask), -3);
    FillSurface(surface, GfxRgb8(255, 255, 255));
    RoboCheckEqInt(GfxRasterSetClip(raster, MakeGfxRect(0, 0, 6, 6)), RoboOk);
    RoboCheckEqInt(GfxRasterDrawGlyphMask(raster, mask, GfxRgb8(0, 0, 0),
                                          RoboGfxCoverageGammaLinear),
                   RoboOk);
    /* Inside both the mask and the clip the ink landed; where either ends, nothing did. The ink
     * itself reaches device y = 4 exclusively, so (4, 4) is the first row past it. */
    RoboCheckEqInt((int)PixelAt(surface, 0u, 0u).r, 0);
    RoboCheckEqInt((int)PixelAt(surface, 4u, 2u).r, 0);
    RoboCheckEqInt((int)PixelAt(surface, 4u, 4u).r, 255);
    /* This pixel carries ink in the mask (device 6, 0) and lies outside the raster's clip, so it
     * is the one that distinguishes "clipped" from "not drawn". */
    RoboCheckEqInt((int)PixelAt(surface, 6u, 0u).r, 255);
    RoboCheckEqInt((int)PixelAt(surface, 6u, 6u).r, 255);
    RoboCheckEqInt((int)PixelAt(surface, 7u, 25u).r, 255);

    /* An empty mask and a NULL mask are no-ops; a NULL raster is a caller's mistake. */
    RoboCheckEqInt(GfxRasterDrawGlyphMask(raster, NULL, GfxRgb8(0, 0, 0),
                                          RoboGfxCoverageGammaLinear),
                   RoboInvalidArgument);
    RoboCheckEqInt(GfxRasterDrawGlyphMask(NULL, mask, GfxRgb8(0, 0, 0),
                                          RoboGfxCoverageGammaLinear),
                   RoboInvalidArgument);
    GfxGlyphMaskRelease(mask);
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 100.0f,
                                                MakeGfxPointF(0.0f, 0.0f), &(GfxRect){200, 200, 4, 4},
                                                NULL, &mask, NULL),
                   RoboOk);
    RoboCheck(GfxGlyphMaskIsEmpty(mask));
    RoboCheckEqInt(GfxRasterDrawGlyphMask(raster, mask, GfxRgb8(0, 0, 0),
                                          RoboGfxCoverageGammaLinear),
                   RoboOk);
    GfxGlyphMaskRelease(mask);

    GfxRasterDestroy(raster);
    GfxSurfaceRelease(surface);
    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/* ------------------------------------------------------------------ */
/* Determinism and lifetime                                           */
/* ------------------------------------------------------------------ */

static void TestDeterminism(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *first = NULL;
    GfxGlyphMask *second = NULL;
    GfxSurface *surfaceA = NULL;
    GfxSurface *surfaceB = NULL;
    GfxRaster *rasterA = NULL;
    GfxRaster *rasterB = NULL;
    int value;

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdBlob);
    RoboCheck(outline != NULL);

    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 137.0f,
                                                MakeGfxPointF(3.25f, 21.75f), NULL, NULL, &first,
                                                NULL),
                   RoboOk);
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 137.0f,
                                                MakeGfxPointF(3.25f, 21.75f), NULL, NULL, &second,
                                                NULL),
                   RoboOk);
    RoboCheck(MaskEquals(first, second));

    RoboCheckEqInt(MakeSurface(40u, 40u, &surfaceA), RoboOk);
    RoboCheckEqInt(MakeSurface(40u, 40u, &surfaceB), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(surfaceA, &rasterA), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(surfaceB, &rasterB), RoboOk);
    FillSurface(surfaceA, GfxRgb8(250, 250, 245));
    FillSurface(surfaceB, GfxRgb8(250, 250, 245));
    RoboCheckEqInt(GfxRasterDrawGlyphMask(rasterA, first, GfxRgb8(0, 0, 0),
                                          RoboGfxCoverageGammaDarkOnLight),
                   RoboOk);
    RoboCheckEqInt(GfxRasterDrawGlyphMask(rasterB, second, GfxRgb8(0, 0, 0),
                                          RoboGfxCoverageGammaDarkOnLight),
                   RoboOk);
    RoboCheck(SurfaceEquals(surfaceA, surfaceB));

    /* The conversion is a table, so it is deterministic by construction — pinned anyway. */
    for (value = 0; value < 256; value++) {
        RoboCheckEqInt((int)GfxGammaCoverageToAlpha((uint8_t)value, RoboGfxCoverageGammaLightOnDark),
                       (int)GfxGammaCoverageToAlpha((uint8_t)value, RoboGfxCoverageGammaLightOnDark));
    }

    GfxRasterDestroy(rasterA);
    GfxRasterDestroy(rasterB);
    GfxSurfaceRelease(surfaceA);
    GfxSurfaceRelease(surfaceB);
    GfxGlyphMaskRelease(first);
    GfxGlyphMaskRelease(second);
    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/** Two masks are independent values: releasing one never changes the other's bytes. */
static void TestMaskLifetimeIsolation(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *kept = NULL;
    GfxGlyphMask *temp = NULL;
    uint8_t copy[64];

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    outline = OutlineOf(face, GlyphIdSquare);
    RoboCheck(outline != NULL);

    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 40.0f,
                                                MakeGfxPointF(1.0f, 5.0f), NULL, NULL, &kept,
                                                NULL),
                   RoboOk);
    RoboCheckEqInt(GfxGlyphMaskWidth(kept), 4u);
    memcpy(copy, GfxGlyphMaskCoverage(kept), 16u);

    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, 40.0f,
                                                MakeGfxPointF(1.5f, 5.0f), NULL, NULL, &temp,
                                                NULL),
                   RoboOk);
    GfxGlyphMaskRetain(temp);
    GfxGlyphMaskRelease(temp); /* one reference left: the mask is still alive */
    RoboCheck(!GfxGlyphMaskIsEmpty(temp));
    GfxGlyphMaskRelease(temp);

    RoboCheck(memcmp(copy, GfxGlyphMaskCoverage(kept), 16u) == 0);
    GfxGlyphMaskRelease(kept);
    GfxGlyphMaskRelease(NULL);

    /* Out-of-range reads answer zero rather than reading outside the mask. */
    RoboCheckEqInt((int)GfxGlyphMaskCoverageAt(NULL, 0u, 0u), 0);

    FontOutlineRelease(outline);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/* ------------------------------------------------------------------ */
/* The text bridge and the integration chain                          */
/* ------------------------------------------------------------------ */

/**
 * The mode the paint stack selects is this rasterizer.
 *
 * `GfxGlyphDraw.mode = ALPHA` through `GfxRasterDrawGlyph` and the mask door called by hand must put
 * the same bytes on the same surface — which is what makes the previous test's measurements
 * statements about UI text rather than about a fixture.
 */
static void TestIntegrationModeAndMaskAgree(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    FontOutline *outline;
    GfxGlyphMask *mask = NULL;
    GfxSurface *byMode = NULL;
    GfxSurface *byMask = NULL;
    GfxRaster *modeRaster = NULL;
    GfxRaster *maskRaster = NULL;
    GfxGlyphDraw draw;
    GfxGlyphRasterParams params;

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    RoboCheckEqInt(MakeSurface(64u, 64u, &byMode), RoboOk);
    RoboCheckEqInt(MakeSurface(64u, 64u, &byMask), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(byMode, &modeRaster), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(byMask, &maskRaster), RoboOk);
    FillSurface(byMode, GfxRgb8(255, 255, 255));
    FillSurface(byMask, GfxRgb8(255, 255, 255));

    GfxGlyphDrawInit(&draw);
    draw.face = face;
    draw.glyphId = GlyphIdBlob;
    draw.sizePx = 180.0f;
    draw.origin = MakeGfxPointF(6.5f, 44.0f);
    draw.color = GfxRgb8(10, 20, 30);
    draw.mode = RoboGfxGlyphModeAlpha;
    RoboCheckEqInt(GfxRasterDrawGlyph(modeRaster, &draw), RoboOk);

    GfxGlyphRasterParamsInit(&params);
    RoboCheckEqInt(params.gamma, RoboGfxCoverageGammaLinear);
    RoboCheckEqInt((int)params.supersample, (int)RoboGfxGlyphSamplesPerPixel);
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(NULL, (uint16_t)GlyphUpem, 1.0f,
                                                MakeGfxPointF(0.0f, 0.0f), NULL, NULL, &mask, NULL),
                   RoboInvalidArgument);

    outline = OutlineOf(face, GlyphIdBlob);
    RoboCheck(outline != NULL);
    RoboCheckEqInt(GfxGlyphMaskRasterizeOutline(outline, (uint16_t)GlyphUpem, draw.sizePx,
                                                draw.origin,
                                                &(GfxRect){0, 0, 64, 64}, &params, &mask, NULL),
                   RoboOk);
    RoboCheckEqInt(GfxRasterDrawGlyphMask(maskRaster, mask, draw.color, params.gamma), RoboOk);
    RoboCheck(SurfaceEquals(byMode, byMask));
    RoboCheck(InkedPixels(byMode, GfxRgb8(255, 255, 255)) > 100u);

    /* The explicit door takes the same parameters and refuses a supersample it cannot sample. */
    params.supersample = 3u;
    RoboCheckEqInt(GfxRasterDrawGlyphCoverage(modeRaster, &draw, &params), RoboInvalidArgument);
    params.supersample = 4u;
    RoboCheckEqInt(GfxRasterDrawGlyphCoverage(modeRaster, &draw, &params), RoboOk);
    params.supersample = 0u;
    RoboCheckEqInt(GfxRasterDrawGlyphCoverage(modeRaster, &draw, &params), RoboOk);
    RoboCheckEqInt(GfxRasterDrawGlyphCoverage(NULL, &draw, NULL), RoboInvalidArgument);
    draw.face = NULL;
    RoboCheckEqInt(GfxRasterDrawGlyphCoverage(modeRaster, &draw, NULL), RoboInvalidArgument);
    draw.face = face;
    RoboCheckEqInt(GfxRasterDrawGlyphCoverage(modeRaster, NULL, NULL), RoboInvalidArgument);

    /* An unknown gamma is the documented default rather than a darker-than-expected pixel. */
    params.gamma = (GfxCoverageGamma)77;
    params.supersample = 0u;
    RoboCheckEqInt(GfxRasterDrawGlyphCoverage(modeRaster, &draw, &params), RoboOk);

    GfxGlyphMaskRelease(mask);
    FontOutlineRelease(outline);
    GfxRasterDestroy(modeRaster);
    GfxRasterDestroy(maskRaster);
    GfxSurfaceRelease(byMode);
    GfxSurfaceRelease(byMask);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/**
 * The visible improvement, as a number.
 *
 * The outline-stroke mode traces the glyph's contour with one-pixel lines, so the pixels it marks
 * are a thin subset of the ink's footprint. The coverage mode fills that footprint. Comparing the
 * two on the same surface is the phase's whole claim, reduced to something a test can assert: the
 * coverage path puts ink on far more pixels, and it puts *partial* ink on edges the stroke mode
 * cannot represent at all.
 */
static void TestIntegrationStrokeVersusCoverage(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    GfxSurface *stroked = NULL;
    GfxSurface *covered = NULL;
    GfxRaster *strokeRaster = NULL;
    GfxRaster *coverRaster = NULL;
    GfxGlyphDraw draw;
    uint32_t strokedPixels;
    uint32_t coveredPixels;

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    RoboCheckEqInt(MakeSurface(80u, 80u, &stroked), RoboOk);
    RoboCheckEqInt(MakeSurface(80u, 80u, &covered), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(stroked, &strokeRaster), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(covered, &coverRaster), RoboOk);
    FillSurface(stroked, GfxRgb8(255, 255, 255));
    FillSurface(covered, GfxRgb8(255, 255, 255));

    GfxGlyphDrawInit(&draw);
    draw.face = face;
    draw.glyphId = GlyphIdBlob;
    draw.sizePx = 300.0f; /* 30 px of ink: large enough that the two modes differ unmistakably */
    draw.origin = MakeGfxPointF(10.0f, 60.0f);
    draw.color = GfxRgb8(0, 0, 0);

    RoboCheckEqInt(GfxRasterDrawGlyph(strokeRaster, &draw), RoboOk);
    draw.mode = RoboGfxGlyphModeAlpha;
    RoboCheckEqInt(GfxRasterDrawGlyph(coverRaster, &draw), RoboOk);

    strokedPixels = InkedPixels(stroked, GfxRgb8(255, 255, 255));
    coveredPixels = InkedPixels(covered, GfxRgb8(255, 255, 255));
    RoboCheck(strokedPixels > 0u);
    RoboCheck(coveredPixels > strokedPixels * 2u);

    /* Unimplemented modes are still refused rather than silently approximated. */
    draw.mode = RoboGfxGlyphModeMask;
    RoboCheckEqInt(GfxRasterDrawGlyph(coverRaster, &draw), RoboNotSupported);
    draw.mode = RoboGfxGlyphModeColor;
    RoboCheckEqInt(GfxRasterDrawGlyph(coverRaster, &draw), RoboNotSupported);
    draw.mode = RoboGfxGlyphModeAlpha;
    RoboCheckEqInt(GfxRasterDrawGlyph(NULL, &draw), RoboInvalidArgument);
    draw.face = NULL;
    RoboCheckEqInt(GfxRasterDrawGlyph(coverRaster, &draw), RoboInvalidArgument);

    GfxRasterDestroy(strokeRaster);
    GfxRasterDestroy(coverRaster);
    GfxSurfaceRelease(stroked);
    GfxSurfaceRelease(covered);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/** Every glyph the phase must not fall over on, drawn at a size that makes each one real. */
static void TestIntegrationRobustGlyphs(void)
{
    FontSource *source = NULL;
    FontFace *face = NULL;
    GfxSurface *surface = NULL;
    GfxRaster *raster = NULL;
    GfxGlyphDraw draw;
    uint32_t glyphId;

    RoboCheckEqInt(MakeGlyphFace(&source, &face), RoboOk);
    RoboCheckEqInt(MakeSurface(48u, 48u, &surface), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(surface, &raster), RoboOk);
    FillSurface(surface, GfxRgb8(255, 255, 255));

    GfxGlyphDrawInit(&draw);
    draw.face = face;
    draw.sizePx = 120.0f;
    draw.origin = MakeGfxPointF(8.0f, 40.0f);
    draw.color = GfxRgb8(0, 0, 0);
    draw.mode = RoboGfxGlyphModeAlpha;

    for (glyphId = 0u; glyphId < GlyphNumGlyphs; glyphId++) {
        draw.glyphId = glyphId;
        RoboCheckEqInt(GfxRasterDrawGlyph(raster, &draw), RoboOk);
    }

    /* A glyph id the face does not have is reported, not drawn as whatever came next. */
    draw.glyphId = 99u;
    RoboCheck(GfxRasterDrawGlyph(raster, &draw) != RoboOk);

    GfxRasterDestroy(raster);
    GfxSurfaceRelease(surface);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

void TestRegisterGraphicsGlyph(void)
{
    TestRegister("graphics.glyph.coverage_full_and_zero", TestCoverageFullAndZero);
    TestRegister("graphics.glyph.coverage_partial", TestCoveragePartial);
    TestRegister("graphics.glyph.coverage_curve", TestCoverageCurve);
    TestRegister("graphics.glyph.coverage_winding", TestCoverageWinding);
    TestRegister("graphics.glyph.coverage_empty_small_large", TestCoverageEmptyAndSmallAndLarge);
    TestRegister("graphics.glyph.subpixel_placement", TestCoverageSubpixelPlacement);
    TestRegister("graphics.glyph.bounds_clip", TestBoundsClip);
    TestRegister("graphics.glyph.bounds_negative_origin", TestBoundsNegativeOrigin);
    TestRegister("graphics.glyph.bounds_extreme_coordinates", TestBoundsExtremeCoordinates);
    TestRegister("graphics.glyph.gamma_endpoints_monotone", TestGammaEndpointsAndMonotone);
    TestRegister("graphics.glyph.gamma_known_values", TestGammaKnownValues);
    TestRegister("graphics.glyph.alpha_opaque_and_partial", TestAlphaOpaqueAndPartial);
    TestRegister("graphics.glyph.alpha_separate_from_coverage", TestAlphaIsSeparateFromCoverage);
    TestRegister("graphics.glyph.alpha_clip_and_safety", TestAlphaClipAndSafety);
    TestRegister("graphics.glyph.determinism", TestDeterminism);
    TestRegister("graphics.glyph.mask_lifetime_isolation", TestMaskLifetimeIsolation);
    TestRegister("graphics.glyph.integration_mode_and_mask", TestIntegrationModeAndMaskAgree);
    TestRegister("graphics.glyph.integration_stroke_vs_coverage",
                 TestIntegrationStrokeVersusCoverage);
    TestRegister("graphics.glyph.integration_robust_glyphs", TestIntegrationRobustGlyphs);
}
