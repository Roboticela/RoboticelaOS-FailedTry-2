/*
 * RoboGraphics Goal 6.12 — text / glyph bridge.
 * Synthetic RoboFont fixtures only — no Chromium / system fonts.
 */

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

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

typedef struct {
    uint32_t tag;
    const uint8_t *data;
    uint32_t length;
} SynthTable;

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

static size_t BuildSfnt(uint8_t *out, size_t outCap, uint32_t version,
                         const SynthTable *tables, uint32_t n)
{
    size_t dirEnd = 12u + (size_t)n * 16u;
    size_t cursor;
    size_t need = dirEnd;
    uint32_t i;
    uint8_t *p;
    for (i = 0u; i < n; i++) {
        need += ((size_t)tables[i].length + 3u) & ~(size_t)3u;
    }
    if (need > outCap) {
        return 0u;
    }
    memset(out, 0, need);
    p = out;
    PutU32(&p, version);
    WriteSearchFields(out, (uint16_t)n);
    cursor = dirEnd;
    for (i = 0u; i < n; i++) {
        uint32_t pad = (uint32_t)(((size_t)tables[i].length + 3u) & ~(size_t)3u);
        uint8_t *rec = out + 12u + (size_t)i * 16u;
        p = rec;
        PutU32(&p, tables[i].tag);
        PutU32(&p, 0u);
        PutU32(&p, (uint32_t)cursor);
        PutU32(&p, tables[i].length);
        memcpy(out + cursor, tables[i].data, tables[i].length);
        cursor += pad;
    }
    return need;
}

static size_t BuildHead(uint8_t *b, uint16_t upem)
{
    uint8_t *p = b;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutU32(&p, 0x00010000u);
    PutU32(&p, 0);
    PutU32(&p, 0x5F0F3CF5u);
    PutU16(&p, 0);
    PutU16(&p, upem);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutU32(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 100);
    PutI16(&p, 100);
    PutU16(&p, 0u);
    PutU16(&p, 8u);
    PutI16(&p, 2);
    PutI16(&p, 1);
    PutI16(&p, 0);
    return (size_t)(p - b);
}

static size_t BuildHhea(uint8_t *b, uint16_t nH)
{
    uint8_t *p = b;
    int i;
    PutU16(&p, 1);
    PutU16(&p, 0);
    PutI16(&p, 800);
    PutI16(&p, -200);
    PutI16(&p, 0);
    PutU16(&p, 1000);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 100);
    PutI16(&p, 1);
    PutI16(&p, 0);
    PutI16(&p, 0);
    for (i = 0; i < 4; i++) {
        PutI16(&p, 0);
    }
    PutI16(&p, 0);
    PutU16(&p, nH);
    return (size_t)(p - b);
}

static size_t BuildMaxp(uint8_t *b, uint16_t ng)
{
    uint8_t *p = b;
    PutU32(&p, 0x00010000u);
    PutU16(&p, ng);
    PutU16(&p, 8);
    PutU16(&p, 2);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 2);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 0);
    PutU16(&p, 4);
    PutU16(&p, 4);
    return (size_t)(p - b);
}

static size_t BuildHmtx(uint8_t *b, uint16_t n)
{
    uint8_t *p = b;
    uint16_t i;
    for (i = 0u; i < n; i++) {
        PutU16(&p, 200);
        PutI16(&p, 10);
    }
    return (size_t)(p - b);
}

static size_t BuildSquareGlyf(uint8_t *b)
{
    uint8_t *p = b;
    PutI16(&p, 1);
    PutI16(&p, 0);
    PutI16(&p, 0);
    PutI16(&p, 100);
    PutI16(&p, 100);
    PutU16(&p, 3);
    PutU16(&p, 0);
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                          RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagYShort |
                          RoboFontGlyfFlagXSameOrPositive |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve | RoboFontGlyfFlagXShort |
                          RoboFontGlyfFlagYSameOrPositive));
    PutU8(&p, 100);
    PutU8(&p, 100);
    PutU8(&p, 100);
    return (size_t)(p - b);
}

static Result MakeSquareFace(FontFace **outFace,
                                      FontSource **outSrc)
{
    uint8_t head[64], hhea[64], maxp[64], hmtx[16], loca[16], glyf[64];
    uint8_t font[384];
    SynthTable tabs[6];
    size_t gn = BuildSquareGlyf(glyf);
    uint8_t *lp = loca;
    size_t n;

    PutU32(&lp, 0);
    PutU32(&lp, (uint32_t)gn);
    PutU32(&lp, (uint32_t)gn);

    tabs[0].tag = RoboFontTagHead;
    tabs[0].data = head;
    tabs[0].length = (uint32_t)BuildHead(head, 1000);
    tabs[1].tag = RoboFontTagHhea;
    tabs[1].data = hhea;
    tabs[1].length = (uint32_t)BuildHhea(hhea, 2);
    tabs[2].tag = RoboFontTagMaxp;
    tabs[2].data = maxp;
    tabs[2].length = (uint32_t)BuildMaxp(maxp, 2);
    tabs[3].tag = RoboFontTagHmtx;
    tabs[3].data = hmtx;
    tabs[3].length = (uint32_t)BuildHmtx(hmtx, 2);
    tabs[4].tag = RoboFontTagLoca;
    tabs[4].data = loca;
    tabs[4].length = 12u;
    tabs[5].tag = RoboFontTagGlyf;
    tabs[5].data = glyf;
    tabs[5].length = (uint32_t)gn;

    n = BuildSfnt(font, sizeof(font), RoboFontSfntVersionTrue, tabs, 6);
    if (n == 0u) {
        return RoboOutOfMemory;
    }
    if (FontSourceCreateMemoryCopy(font, n, outSrc) != RoboOk) {
        return RoboOutOfMemory;
    }
    return FontFaceCreate(*outSrc, 0, outFace);
}

static Result MakeSurface(uint32_t w,
                                  uint32_t h,
                                  GfxSurface **out)
{
    GfxSurfaceDesc desc;
    GfxSurfaceDescInit(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    return GfxSurfaceCreate(&desc, out);
}

static void TestTextCoords(void)
{
    GfxPointF d;
    float scale = 0.0f;
    GfxPoint snap;

    RoboCheckEqInt(GfxFontScale(0u, 10.0f, &scale),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxFontScale(1000u, 0.0f, &scale),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxFontScale(1000u, 10.0f, &scale), RoboOk);
    RoboCheck(scale > 0.009f && scale < 0.011f);

    /* font (0,100) at origin (50,80), size 10, upem 1000 → (50, 79) */
    RoboCheckEqInt(
        GfxFontPointToDevice(0, 100, 1000u, 10.0f,
                                     MakeGfxPointF(50.0f, 80.0f), &d),
        RoboOk);
    RoboCheck(d.x > 49.9f && d.x < 50.1f);
    RoboCheck(d.y > 78.9f && d.y < 79.1f);

    /* below baseline */
    RoboCheckEqInt(
        GfxFontPointToDevice(0, -50, 1000u, 10.0f,
                                     MakeGfxPointF(0.0f, 0.0f), &d),
        RoboOk);
    RoboCheck(d.y > 0.49f && d.y < 0.51f);

    snap = GfxGlyphSnapPoint(MakeGfxPointF(1.5f, -1.5f));
    RoboCheckEqInt(snap.x, 2);
    RoboCheckEqInt(snap.y, -2);
}

static void TestTextMetrics(void)
{
    FontSource *src = NULL;
    FontFace *face = NULL;
    GfxGlyphMetricsDevice m;

    RoboCheckEqInt(MakeSquareFace(&face, &src), RoboOk);
    RoboCheckEqInt(GetGfxGlyphMetricsDevice(NULL, 0, 10.0f, &m),
                      RoboInvalidArgument);
    RoboCheckEqInt(GetGfxGlyphMetricsDevice(face, 0, 0.0f, &m),
                      RoboInvalidArgument);
    RoboCheckEqInt(GetGfxGlyphMetricsDevice(face, 0, 10.0f, &m), RoboOk);
    /* advance 200 FUnits @ 10px / 1000 = 2px; lsb 10 → 0.1 */
    RoboCheck(m.advanceWidth > 1.9f && m.advanceWidth < 2.1f);
    RoboCheck(m.leftSideBearing > 0.09f && m.leftSideBearing < 0.11f);
    RoboCheck(m.hasBounds);
    RoboCheck(m.bounds.width > 0.9f && m.bounds.width < 1.1f);

    RoboCheckEqInt(GetGfxGlyphMetricsDevice(face, 99u, 10.0f, &m),
                      RoboInvalidArgument);

    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestTextOutlinePath(void)
{
    FontSource *src = NULL;
    FontFace *face = NULL;
    GfxPath *path = NULL;
    GfxPathCommand cmd;
    size_t n;
    int sawMove = 0;
    int sawLine = 0;
    int sawClose = 0;
    size_t i;

    RoboCheckEqInt(MakeSquareFace(&face, &src), RoboOk);
    RoboCheckEqInt(GfxPathCreate(&path), RoboOk);
    RoboCheckEqInt(
        GfxGlyphOutlineToPath(face, 0, 10.0f,
                                       MakeGfxPointF(20.0f, 40.0f), path),
        RoboOk);
    n = GfxPathCommandCount(path);
    RoboCheck(n >= 4u);
    for (i = 0u; i < n; i++) {
        RoboCheckEqInt(GfxPathCommandAt(path, i, &cmd), RoboOk);
        if (cmd.verb == RoboGfxPathMoveTo) {
            sawMove = 1;
        }
        if (cmd.verb == RoboGfxPathLineTo) {
            sawLine = 1;
        }
        if (cmd.verb == RoboGfxPathClose) {
            sawClose = 1;
        }
    }
    RoboCheck(sawMove);
    RoboCheck(sawLine);
    RoboCheck(sawClose);

    /* empty glyph */
    RoboCheckEqInt(
        GfxGlyphOutlineToPath(face, 1, 10.0f,
                                       MakeGfxPointF(0.0f, 0.0f), path),
        RoboOk);
    RoboCheck(GfxPathIsEmpty(path));

    RoboCheckEqInt(
        GfxGlyphOutlineToPath(NULL, 0, 10.0f,
                                       MakeGfxPointF(0.0f, 0.0f), path),
        RoboInvalidArgument);

    GfxPathRelease(path);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestTextDrawGlyph(void)
{
    FontSource *src = NULL;
    FontFace *face = NULL;
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxGlyphDraw draw;
    GfxColorRgba8 c;
    uint32_t marked = 0u;
    uint32_t y;
    uint32_t x;

    RoboCheckEqInt(MakeSquareFace(&face, &src), RoboOk);
    RoboCheckEqInt(MakeSurface(64, 64, &s), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(s, &r), RoboOk);
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 0)), RoboOk);

    GfxGlyphDrawInit(&draw);
    draw.face = face;
    draw.glyphId = 0;
    draw.sizePx = 100.0f; /* 100 FUnit square → 10 device units */
    draw.origin = MakeGfxPointF(10.0f, 50.0f);
    draw.color = GfxRgb8(255, 0, 0);

    RoboCheckEqInt(GfxRasterDrawGlyph(r, &draw), RoboOk);
    for (y = 0u; y < 64u; y++) {
        for (x = 0u; x < 64u; x++) {
            RoboCheckEqInt(GfxSurfaceGetPixel(s, x, y, &c), RoboOk);
            if (c.r == 255u && c.g == 0u && c.b == 0u) {
                marked++;
            }
        }
    }
    RoboCheck(marked > 0u);

    /* off-screen → no-op */
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(0, 0, 0)), RoboOk);
    draw.origin = MakeGfxPointF(5000.0f, 5000.0f);
    RoboCheckEqInt(GfxRasterDrawGlyph(r, &draw), RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 0u, 0u, &c), RoboOk);
    RoboCheckEqInt(c.r, 0);

    /* clipped */
    RoboCheckEqInt(GfxRasterSetClip(r, MakeGfxRect(0, 0, 5, 5)),
                      RoboOk);
    draw.origin = MakeGfxPointF(10.0f, 50.0f);
    RoboCheckEqInt(GfxRasterDrawGlyph(r, &draw), RoboOk);

    /* empty glyph */
    RoboCheckEqInt(GfxRasterResetClip(r), RoboOk);
    draw.glyphId = 1;
    RoboCheckEqInt(GfxRasterDrawGlyph(r, &draw), RoboOk);

    /*
     * Phase 12.1: `ALPHA` is implemented -- the outline becomes a grayscale coverage mask
     * rather than a one-pixel stroke. The square glyph is 10x10 device units, entirely on
     * the surface, so the mask's interior is fully covered and a pixel at its centre is the
     * glyph's own colour.
     */
    RoboCheckEqInt(GfxRasterResetClip(r), RoboOk);
    RoboCheckEqInt(GfxRasterClear(r, GfxRgb8(255, 255, 255)), RoboOk);
    draw.glyphId = 0;
    draw.origin = MakeGfxPointF(10.0f, 50.0f);
    draw.color = GfxRgb8(0, 0, 0);
    draw.mode = RoboGfxGlyphModeAlpha;
    RoboCheckEqInt(GfxRasterDrawGlyph(r, &draw), RoboOk);
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 14u, 45u, &c), RoboOk);
    RoboCheckEqInt((int)c.r, 0);
    /* One pixel outside the ink's right edge is untouched by it. */
    RoboCheckEqInt(GfxSurfaceGetPixel(s, 21u, 45u, &c), RoboOk);
    RoboCheckEqInt((int)c.r, 255);
    /* An empty glyph in the same mode draws nothing and succeeds. */
    draw.glyphId = 1;
    RoboCheckEqInt(GfxRasterDrawGlyph(r, &draw), RoboOk);

    /* The modes this build does not implement still refuse rather than silently stroking. */
    draw.glyphId = 0;
    draw.mode = RoboGfxGlyphModeMask;
    RoboCheckEqInt(GfxRasterDrawGlyph(r, &draw), RoboNotSupported);
    draw.mode = RoboGfxGlyphModeColor;
    RoboCheckEqInt(GfxRasterDrawGlyph(r, &draw), RoboNotSupported);

    RoboCheckEqInt(GfxRasterDrawGlyph(NULL, &draw),
                      RoboInvalidArgument);
    draw.face = NULL;
    draw.mode = RoboGfxGlyphModeOutlineStroke;
    RoboCheckEqInt(GfxRasterDrawGlyph(r, &draw),
                      RoboInvalidArgument);

    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestTextLayoutMetrics(void)
{
    FontSource *src = NULL;
    FontFace *face = NULL;
    FontMetrics layout;

    RoboCheckEqInt(MakeSquareFace(&face, &src), RoboOk);
    RoboCheckEqInt(FontFaceParseMetrics(face), RoboOk);
    RoboCheckEqInt(FontFaceLayoutMetrics(face, &layout), RoboOk);
    RoboCheck(layout.valid);
    RoboCheckEqInt((int)layout.unitsPerEm, 1000);
    RoboCheck(layout.ascent > 0);
    RoboCheck(layout.descent >= 0);

    FontFaceRelease(face);
    FontSourceRelease(src);
}

void TestRegisterGraphicsText(void)
{
    TestRegister("graphics.text.coords", TestTextCoords);
    TestRegister("graphics.text.metrics", TestTextMetrics);
    TestRegister("graphics.text.outline", TestTextOutlinePath);
    TestRegister("graphics.text.draw", TestTextDrawGlyph);
    TestRegister("graphics.text.layout", TestTextLayoutMetrics);
}
