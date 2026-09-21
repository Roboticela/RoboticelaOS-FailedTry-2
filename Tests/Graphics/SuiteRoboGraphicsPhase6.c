/*
 * RoboGraphics Phase 6 final validation / API freeze (Goal 6.15).
 *
 * Integration, visual fixtures, determinism, safety, and smoke performance.
 * Does not add new graphics features. Synthetic RoboFont fixtures only.
 */

#include "RoboFont/RoboFont.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

/* ---- Synthetic square-glyph font (same pattern as graphics.text) ---- */

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
    PutU8(&p, (uint8_t)(RoboFontGlyfFlagOnCurve |
                          RoboFontGlyfFlagXSameOrPositive |
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
                                  GfxPixelFormat fmt,
                                  GfxSurface **out)
{
    GfxSurfaceDesc desc;
    GfxSurfaceDescInit(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = fmt;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    return GfxSurfaceCreate(&desc, out);
}

static void ExpectRgba(GfxColorRgba8 c,
                        uint8_t r,
                        uint8_t g,
                        uint8_t b,
                        uint8_t a)
{
    RoboCheckEqInt(c.r, r);
    RoboCheckEqInt(c.g, g);
    RoboCheckEqInt(c.b, b);
    RoboCheckEqInt(c.a, a);
}

static void PaintFixtureScene(GfxSurface *s)
{
    GfxRaster *r = NULL;
    RoboCheck(GfxSurfaceClear(s, GfxRgba8(0, 0, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxRasterCreate(s, &r) == RoboOk);
    RoboCheck(GfxRasterFillRect(r, MakeGfxRect(1, 1, 2, 2),
                                         GfxRgba8(255, 0, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxRasterDrawLine(r, MakeGfxPoint(0, 0),
                                         MakeGfxPoint(3, 0),
                                         GfxRgba8(0, 255, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxRasterDrawPixel(r, MakeGfxPoint(3, 3),
                                          GfxRgba8(0, 0, 255, 255)) ==
               RoboOk);
    GfxRasterDestroy(r);
}

static void TestP6Freeze(void)
{
    GfxCapabilities caps;

    RoboCheckEqInt(RoboGraphicsApiFrozen, 1);
    /* Additive modules only, decode then the scaled image draw; the frozen API surface is
       unchanged. */
    RoboCheckStrEq(GraphicsVersionString(), "0.17.0");
    RoboCheckEqInt(GraphicsInit(), RoboOk);
    RoboCheckEqInt(GfxQueryCapabilities(&caps), RoboOk);
    RoboCheck(caps.cpuRasterAvailable);
    RoboCheck(!caps.gpuBackendAvailable);
    RoboCheck(!caps.nativeWindowAvailable);
    RoboCheck(!caps.framebufferPresentAvailable);
    GraphicsShutdown();
}

static void TestP6VisualFixture(void)
{
    GfxSurface *s = NULL;
    GfxColorRgba8 c;
    static const uint8_t expect[4][4][4] = {
        {{0, 255, 0, 255},
         {0, 255, 0, 255},
         {0, 255, 0, 255},
         {0, 255, 0, 255}},
        {{0, 0, 0, 255},
         {255, 0, 0, 255},
         {255, 0, 0, 255},
         {0, 0, 0, 255}},
        {{0, 0, 0, 255},
         {255, 0, 0, 255},
         {255, 0, 0, 255},
         {0, 0, 0, 255}},
        {{0, 0, 0, 255},
         {0, 0, 0, 255},
         {0, 0, 0, 255},
         {0, 0, 255, 255}},
    };
    uint32_t x;
    uint32_t y;

    RoboCheck(MakeSurface(4, 4, RoboGfxFormatRgba8, &s) == RoboOk);
    PaintFixtureScene(s);
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            RoboCheck(GfxSurfaceGetPixel(s, x, y, &c) == RoboOk);
            ExpectRgba(c, expect[y][x][0], expect[y][x][1], expect[y][x][2],
                        expect[y][x][3]);
        }
    }
    GfxSurfaceRelease(s);
}

static void TestP6RasterShotEncode(void)
{
    GfxSurface *s = NULL;
    GfxScreenshot *shot = NULL;
    GfxCaptureOptions caps;
    GfxEncodeOptions enc;
    GfxEncodedBuffer a;
    GfxEncodedBuffer b;
    GfxColorRgba8 c;

    RoboCheck(MakeSurface(4, 4, RoboGfxFormatRgba8, &s) == RoboOk);
    PaintFixtureScene(s);

    GfxCaptureOptionsInit(&caps);
    RoboCheck(GfxScreenshotCaptureSurface(s, &caps, &shot) == RoboOk);
    RoboCheck(GfxSurfaceGetPixel(GfxScreenshotSurface(shot), 1,
                                          1, &c) == RoboOk);
    ExpectRgba(c, 255, 0, 0, 255);

    RoboCheck(GfxSurfaceClear(s, GfxRgba8(1, 1, 1, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceGetPixel(GfxScreenshotSurface(shot), 1,
                                          1, &c) == RoboOk);
    ExpectRgba(c, 255, 0, 0, 255);

    GfxEncodeOptionsInit(&enc);
    RoboCheck(GfxScreenshotExport(shot, &enc, &a) == RoboOk);
    RoboCheck(GfxScreenshotExport(shot, &enc, &b) == RoboOk);
    RoboCheck(a.size == b.size);
    RoboCheck(memcmp(a.data, b.data, a.size) == 0);
    RoboCheck(a.size >= 8u);
    RoboCheck(a.data[0] == 0x89u && a.data[1] == 'P');

    GfxEncodedBufferRelease(&a);
    GfxEncodedBufferRelease(&b);
    GfxScreenshotRelease(shot);
    GfxSurfaceRelease(s);
}

static void TestP6PathTransform(void)
{
    GfxPath *path = NULL;
    GfxAffine xf;
    GfxRect bounds;
    GfxPathIter it;
    GfxPathCommand cmd;
    int sawLine = 0;

    RoboCheck(GfxPathCreate(&path) == RoboOk);
    RoboCheck(GfxPathMoveTo(path, MakeGfxPoint(0, 0)) == RoboOk);
    RoboCheck(GfxPathLineTo(path, MakeGfxPoint(10, 0)) == RoboOk);
    RoboCheck(GfxPathLineTo(path, MakeGfxPoint(10, 10)) == RoboOk);
    RoboCheck(GfxPathClose(path) == RoboOk);

    GfxAffineTranslate(&xf, 5.0f, 7.0f);
    RoboCheck(GfxPathTransform(path, &xf) == RoboOk);
    RoboCheck(GfxPathBounds(path, &bounds) == RoboOk);
    RoboCheckEqInt(bounds.x, 5);
    RoboCheckEqInt(bounds.y, 7);
    RoboCheckEqInt(bounds.width, 11);
    RoboCheckEqInt(bounds.height, 11);

    GfxPathIterBegin(path, &it);
    while (GfxPathIterNext(&it, &cmd)) {
        if (cmd.verb == RoboGfxPathLineTo) {
            sawLine = 1;
            RoboCheck(cmd.point.x >= 5);
        }
    }
    RoboCheck(sawLine);

    GfxPathRelease(path);
}

static void TestP6TransformRaster(void)
{
    GfxSurface *s = NULL;
    GfxAffine xf;
    GfxPointF pf;
    GfxPoint pi;
    GfxColorRgba8 c;

    RoboCheck(MakeSurface(16, 16, RoboGfxFormatRgba8, &s) == RoboOk);
    RoboCheck(GfxSurfaceClear(s, GfxRgba8(0, 0, 0, 255)) ==
               RoboOk);

    GfxAffineScale(&xf, 2.0f, 2.0f);
    pf = GfxAffineMapPointF(&xf, MakeGfxPointF(3.0f, 4.0f));
    pi = GfxPointFFloor(pf);
    RoboCheckEqInt(pi.x, 6);
    RoboCheckEqInt(pi.y, 8);
    RoboCheck(GfxSurfaceSetPixel(s, (uint32_t)pi.x, (uint32_t)pi.y,
                                          GfxRgba8(200, 100, 50, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceGetPixel(s, 6, 8, &c) == RoboOk);
    ExpectRgba(c, 200, 100, 50, 255);

    GfxSurfaceRelease(s);
}

static void TestP6FontTextBridge(void)
{
    FontSource *src = NULL;
    FontFace *face = NULL;
    GfxPath *path = NULL;
    GfxPointF d;
    GfxGlyphMetricsDevice m;
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxGlyphDraw draw;

    RoboCheck(GfxFontPointToDevice(
                   0, 100, 1000u, 10.0f, MakeGfxPointF(50.0f, 80.0f),
                   &d) == RoboOk);
    RoboCheck(d.x > 49.9f && d.x < 50.1f);
    RoboCheck(d.y > 78.9f && d.y < 79.1f);

    RoboCheck(MakeSquareFace(&face, &src) == RoboOk);
    RoboCheck(GetGfxGlyphMetricsDevice(face, 0, 10.0f, &m) == RoboOk);
    RoboCheck(m.advanceWidth > 1.9f && m.advanceWidth < 2.1f);

    RoboCheck(GfxPathCreate(&path) == RoboOk);
    RoboCheck(GfxGlyphOutlineToPath(face, 0u, 10.0f,
                                              MakeGfxPointF(0.0f, 20.0f),
                                              path) == RoboOk);
    RoboCheck(!GfxPathIsEmpty(path));

    RoboCheck(MakeSurface(32, 32, RoboGfxFormatRgba8, &s) == RoboOk);
    RoboCheck(GfxRasterCreate(s, &r) == RoboOk);
    GfxGlyphDrawInit(&draw);
    draw.face = face;
    draw.glyphId = 0u;
    draw.sizePx = 10.0f;
    draw.origin = MakeGfxPointF(4.0f, 20.0f);
    draw.color = GfxRgba8(255, 255, 255, 255);
    draw.mode = RoboGfxGlyphModeOutlineStroke;
    RoboCheck(GfxRasterDrawGlyph(r, &draw) == RoboOk);

    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);
    GfxPathRelease(path);
    FontFaceRelease(face);
    FontSourceRelease(src);
}

static void TestP6AlphaOverlap(void)
{
    GfxSurface *s = NULL;
    GfxColorRgba8 c;
    GfxColorRgba8 expected;

    RoboCheck(MakeSurface(2, 2, RoboGfxFormatRgba8, &s) == RoboOk);
    RoboCheck(GfxSurfaceClear(s, GfxRgba8(0, 255, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceCompositePixel(
                   s, 0, 0, GfxRgba8(255, 0, 0, 128), 255) == RoboOk);
    expected = GfxCompositeSrcOver(GfxRgba8(255, 0, 0, 128),
                                           GfxRgba8(0, 255, 0, 255));
    RoboCheck(GfxSurfaceGetPixel(s, 0, 0, &c) == RoboOk);
    ExpectRgba(c, expected.r, expected.g, expected.b, expected.a);
    GfxSurfaceRelease(s);
}

static void TestP6Determinism(void)
{
    GfxSurface *a = NULL;
    GfxSurface *b = NULL;
    GfxEncodeOptions enc;
    GfxEncodedBuffer ea;
    GfxEncodedBuffer eb;
    size_t bytes;
    const uint8_t *pa;
    const uint8_t *pb;

    RoboCheck(MakeSurface(8, 8, RoboGfxFormatRgba8, &a) == RoboOk);
    RoboCheck(MakeSurface(8, 8, RoboGfxFormatRgba8, &b) == RoboOk);
    PaintFixtureScene(a);
    PaintFixtureScene(b);

    pa = GfxSurfacePixelsConst(a);
    pb = GfxSurfacePixelsConst(b);
    bytes = GfxSurfaceByteSize(a);
    RoboCheck(pa && pb);
    RoboCheck(memcmp(pa, pb, bytes) == 0);

    GfxEncodeOptionsInit(&enc);
    RoboCheck(GfxSurfaceEncode(a, &enc, &ea) == RoboOk);
    RoboCheck(GfxSurfaceEncode(b, &enc, &eb) == RoboOk);
    RoboCheck(ea.size == eb.size);
    RoboCheck(memcmp(ea.data, eb.data, ea.size) == 0);

    GfxEncodedBufferRelease(&ea);
    GfxEncodedBufferRelease(&eb);
    GfxSurfaceRelease(a);
    GfxSurfaceRelease(b);
}

static void TestP6Safety(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxCaptureOptions caps;
    GfxScreenshot *shot = NULL;
    GfxEncodeOptions enc;
    GfxEncodedBuffer buf;

    RoboCheck(GfxRasterCreate(NULL, &r) == RoboInvalidArgument);
    RoboCheck(MakeSurface(2, 2, RoboGfxFormatRgba8, &s) == RoboOk);
    RoboCheck(GfxRasterCreate(s, &r) == RoboOk);
    RoboCheck(GfxRasterFillRect(r, MakeGfxRect(0, 0, -1, 1),
                                         GfxRgb8(1, 2, 3)) ==
               RoboInvalidArgument);
    GfxRasterDestroy(r);

    GfxCaptureOptionsInit(&caps);
    caps.useFullExtent = 0;
    caps.regionPolicy = RoboGfxCaptureRegionStrict;
    caps.region = MakeGfxRect(0, 0, 0, 0);
    RoboCheck(GfxScreenshotCaptureSurface(s, &caps, &shot) ==
               RoboInvalidArgument);

    GfxEncodeOptionsInit(&enc);
    RoboCheck(GfxSurfaceEncode(NULL, &enc, &buf) ==
               RoboInvalidArgument);

    GfxSurfaceRelease(s);
}

static void TestP6PerfSmoke(void)
{
    GfxSurface *s = NULL;
    GfxRaster *r = NULL;
    GfxEncodeOptions enc;
    GfxEncodedBuffer buf;
    clock_t t0;
    clock_t t1;
    int i;

    RoboCheck(MakeSurface(64, 64, RoboGfxFormatRgba8, &s) == RoboOk);
    RoboCheck(GfxRasterCreate(s, &r) == RoboOk);

    t0 = clock();
    for (i = 0; i < 200; i++) {
        RoboCheck(GfxRasterFillRect(
                       r, MakeGfxRect(0, 0, 64, 64),
                       GfxRgba8((uint8_t)i, 40, 80, 255)) == RoboOk);
        RoboCheck(GfxRasterDrawLine(
                       r, MakeGfxPoint(0, 0), MakeGfxPoint(63, 63),
                       GfxRgba8(255, 255, 255, 255)) == RoboOk);
        RoboCheck(GfxSurfaceCompositePixel(
                       s, 32, 32, GfxRgba8(0, 0, 255, 128), 255) ==
                   RoboOk);
    }
    GfxEncodeOptionsInit(&enc);
    RoboCheck(GfxSurfaceEncode(s, &enc, &buf) == RoboOk);
    t1 = clock();
    RoboCheck(t1 >= t0);
    RoboCheck((t1 - t0) < 30 * CLOCKS_PER_SEC);

    GfxEncodedBufferRelease(&buf);
    GfxRasterDestroy(r);
    GfxSurfaceRelease(s);
}

static void TestP6ChromiumIndependence(void)
{
    GfxSurface *s = NULL;
    GfxCapabilities caps;

    RoboCheck(GfxQueryCapabilities(&caps) == RoboOk);
    RoboCheck(!caps.gpuBackendAvailable);
    RoboCheck(!caps.nativeWindowAvailable);
    RoboCheck(MakeSurface(1, 1, RoboGfxFormatRgba8, &s) == RoboOk);
    RoboCheck(GfxPresentNull(s) == RoboOk);
    GfxSurfaceRelease(s);
}

void TestRegisterGraphicsPhase6(void)
{
    TestRegister("graphics.p6.freeze", TestP6Freeze);
    TestRegister("graphics.p6.visual_fixture", TestP6VisualFixture);
    TestRegister("graphics.p6.raster_shot_encode",
                       TestP6RasterShotEncode);
    TestRegister("graphics.p6.path_transform", TestP6PathTransform);
    TestRegister("graphics.p6.transform_raster",
                       TestP6TransformRaster);
    TestRegister("graphics.p6.font_text_bridge",
                       TestP6FontTextBridge);
    TestRegister("graphics.p6.alpha_overlap", TestP6AlphaOverlap);
    TestRegister("graphics.p6.determinism", TestP6Determinism);
    TestRegister("graphics.p6.safety", TestP6Safety);
    TestRegister("graphics.p6.perf_smoke", TestP6PerfSmoke);
    TestRegister("graphics.p6.chromium", TestP6ChromiumIndependence);
}
