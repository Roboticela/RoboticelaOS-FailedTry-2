/*
 * RoboGraphics Goal 6.13 — image output (PNG / BMP encode).
 * Test-only inflate for store zlib; runtime encoder stays independent.
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

typedef struct {
    uint8_t *data;
    size_t size;
    size_t cap;
} MemSink;

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

static uint32_t ReadU32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t ReadU16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t ReadU32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t TestCrc32(const uint8_t *data, size_t len)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;
    for (i = 0u; i < len; i++) {
        c ^= data[i];
        for (k = 0; k < 8; k++) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
    }
    return c ^ 0xFFFFFFFFu;
}

/* Inflate store-only zlib produced by RoboGraphicsEncode. */
static int InflateStoreZlib(const uint8_t *z,
                              size_t zlen,
                              uint8_t *out,
                              size_t outCap,
                              size_t *outLen)
{
    size_t off;
    size_t o = 0u;

    if (zlen < 2u || z[0] != 0x78u || z[1] != 0x01u) {
        return 0;
    }
    off = 2u;
    for (;;) {
        uint8_t hdr;
        uint16_t len;
        uint16_t nlen;
        int last;
        if (off + 5u > zlen) {
            return 0;
        }
        hdr = z[off++];
        last = (hdr & 1u) ? 1 : 0;
        if ((hdr & 0x06u) != 0u) {
            return 0; /* not store */
        }
        len = ReadU16le(z + off);
        nlen = ReadU16le(z + off + 2u);
        off += 4u;
        if ((uint16_t)(len ^ 0xFFFFu) != nlen) {
            return 0;
        }
        if (off + len > zlen) {
            return 0;
        }
        if (o + len > outCap) {
            return 0;
        }
        memcpy(out + o, z + off, len);
        o += len;
        off += len;
        if (last) {
            break;
        }
    }
    if (off + 4u > zlen) {
        return 0;
    }
    *outLen = o;
    return 1;
}

static int PngDecodePixels(const uint8_t *png,
                             size_t pngLen,
                             uint32_t *outW,
                             uint32_t *outH,
                             int *outRgba,
                             uint8_t *pixels,
                             size_t pixelsCap,
                             size_t *pixelsLen)
{
    size_t off;
    uint32_t w = 0u;
    uint32_t h = 0u;
    uint8_t colorType = 0u;
    uint8_t idat[65536];
    size_t idatLen = 0u;
    uint8_t raw[65536];
    size_t rawLen = 0u;
    uint8_t bpp;
    size_t expect;
    uint32_t y;
    size_t o = 0u;

    static const uint8_t sig[8] = {
        0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};

    if (pngLen < 8u || memcmp(png, sig, 8u) != 0) {
        return 0;
    }
    off = 8u;
    while (off + 12u <= pngLen) {
        uint32_t clen = ReadU32be(png + off);
        const uint8_t *type = png + off + 4u;
        const uint8_t *data = png + off + 8u;
        uint32_t crcFile;
        uint32_t crcCalc;
        if (off + 12u + clen > pngLen) {
            return 0;
        }
        crcFile = ReadU32be(png + off + 8u + clen);
        crcCalc = TestCrc32(type, 4u + clen);
        if (crcFile != crcCalc) {
            return 0;
        }
        if (memcmp(type, "IHDR", 4u) == 0) {
            if (clen != 13u) {
                return 0;
            }
            w = ReadU32be(data);
            h = ReadU32be(data + 4u);
            if (data[8] != 8u || data[10] != 0u || data[11] != 0u ||
                data[12] != 0u) {
                return 0;
            }
            colorType = data[9];
            if (colorType != 2u && colorType != 6u) {
                return 0;
            }
        } else if (memcmp(type, "IDAT", 4u) == 0) {
            if (idatLen + clen > sizeof(idat)) {
                return 0;
            }
            memcpy(idat + idatLen, data, clen);
            idatLen += clen;
        } else if (memcmp(type, "IEND", 4u) == 0) {
            break;
        }
        off += 12u + clen;
    }

    if (w == 0u || h == 0u || idatLen == 0u) {
        return 0;
    }
    if (!InflateStoreZlib(idat, idatLen, raw, sizeof(raw), &rawLen)) {
        return 0;
    }
    bpp = (colorType == 6u) ? 4u : 3u;
    expect = (size_t)h * (1u + (size_t)w * bpp);
    if (rawLen != expect) {
        return 0;
    }
    if ((size_t)w * (size_t)h * bpp > pixelsCap) {
        return 0;
    }
    for (y = 0u; y < h; y++) {
        size_t row = (size_t)y * (1u + (size_t)w * bpp);
        if (raw[row] != 0u) {
            return 0;
        }
        memcpy(pixels + o, raw + row + 1u, (size_t)w * bpp);
        o += (size_t)w * bpp;
    }
    *outW = w;
    *outH = h;
    *outRgba = (colorType == 6u) ? 1 : 0;
    *pixelsLen = o;
    return 1;
}

static void ExpectRgba(const uint8_t *p, uint8_t r, uint8_t g, uint8_t b,
                        uint8_t a)
{
    RoboCheckEqInt(p[0], r);
    RoboCheckEqInt(p[1], g);
    RoboCheckEqInt(p[2], b);
    RoboCheckEqInt(p[3], a);
}

static void ExpectRgb(const uint8_t *p, uint8_t r, uint8_t g, uint8_t b)
{
    RoboCheckEqInt(p[0], r);
    RoboCheckEqInt(p[1], g);
    RoboCheckEqInt(p[2], b);
}

static void TestEncodePng1x1Rgba(void)
{
    GfxSurface *s = NULL;
    GfxEncodeOptions opts;
    GfxEncodedBuffer a;
    GfxEncodedBuffer b;
    uint8_t pixels[16];
    size_t plen = 0u;
    uint32_t w = 0u;
    uint32_t h = 0u;
    int rgba = 0;

    RoboCheck(MakeSurface(1, 1, RoboGfxFormatRgba8, &s) == RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 0, 0,
                                          GfxRgba8(10, 20, 30, 40)) ==
               RoboOk);

    GfxEncodeOptionsInit(&opts);
    opts.container = RoboGfxEncodePng;
    opts.layout = RoboGfxEncodeLayoutRgba8;

    RoboCheck(GfxSurfaceEncode(s, &opts, &a) == RoboOk);
    RoboCheck(GfxSurfaceEncode(s, &opts, &b) == RoboOk);
    RoboCheck(a.size == b.size);
    RoboCheck(a.size >= 8u);
    RoboCheck(memcmp(a.data, b.data, a.size) == 0);
    RoboCheck(PngDecodePixels(a.data, a.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    RoboCheckEqInt(w, 1);
    RoboCheckEqInt(h, 1);
    RoboCheckEqInt(rgba, 1);
    ExpectRgba(pixels, 10, 20, 30, 40);

    GfxEncodedBufferRelease(&a);
    GfxEncodedBufferRelease(&b);
    GfxSurfaceRelease(s);
}

static void TestEncodePngRgbAndAlphaPolicy(void)
{
    GfxSurface *s = NULL;
    GfxEncodeOptions opts;
    GfxEncodedBuffer buf;
    uint8_t pixels[64];
    size_t plen = 0u;
    uint32_t w = 0u;
    uint32_t h = 0u;
    int rgba = 0;

    RoboCheck(MakeSurface(2, 1, RoboGfxFormatRgba8, &s) == RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 0, 0,
                                          GfxRgba8(255, 0, 0, 0)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 1, 0,
                                          GfxRgba8(0, 0, 255, 128)) ==
               RoboOk);

    GfxEncodeOptionsInit(&opts);
    opts.container = RoboGfxEncodePng;
    opts.layout = RoboGfxEncodeLayoutRgb8;
    /* Missing background with alpha source */
    RoboCheck(GfxSurfaceEncode(s, &opts, &buf) ==
               RoboInvalidArgument);

    opts.backgroundSet = 1;
    opts.background = GfxRgb8(0, 255, 0);
    RoboCheck(GfxSurfaceEncode(s, &opts, &buf) == RoboOk);
    RoboCheck(PngDecodePixels(buf.data, buf.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    RoboCheckEqInt(w, 2);
    RoboCheckEqInt(h, 1);
    RoboCheckEqInt(rgba, 0);
    /* Fully transparent → background green */
    ExpectRgb(pixels, 0, 255, 0);
    /* Partial blue over green: src-over */
    {
        GfxColorRgba8 expected = GfxCompositeSrcOver(
            GfxRgba8(0, 0, 255, 128), GfxRgb8(0, 255, 0));
        ExpectRgb(pixels + 3, expected.r, expected.g, expected.b);
    }

    GfxEncodedBufferRelease(&buf);
    GfxSurfaceRelease(s);
}

static void TestEncodePngChannels(void)
{
    GfxSurface *s = NULL;
    GfxEncodeOptions opts;
    GfxEncodedBuffer buf;
    uint8_t pixels[256];
    size_t plen = 0u;
    uint32_t w = 0u;
    uint32_t h = 0u;
    int rgba = 0;

    RoboCheck(MakeSurface(4, 2, RoboGfxFormatRgba8, &s) == RoboOk);
    RoboCheck(GfxSurfaceClear(s, GfxRgba8(0, 0, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 0, 0,
                                          GfxRgba8(0, 0, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 1, 0,
                                          GfxRgba8(255, 255, 255, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 2, 0,
                                          GfxRgba8(255, 0, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 3, 0,
                                          GfxRgba8(0, 255, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 0, 1,
                                          GfxRgba8(0, 0, 255, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 1, 1,
                                          GfxRgba8(255, 255, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 2, 1,
                                          GfxRgba8(0, 255, 255, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 3, 1,
                                          GfxRgba8(255, 0, 255, 128)) ==
               RoboOk);

    GfxEncodeOptionsInit(&opts);
    RoboCheck(GfxSurfaceEncode(s, &opts, &buf) == RoboOk);
    RoboCheck(PngDecodePixels(buf.data, buf.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    RoboCheckEqInt(w, 4);
    RoboCheckEqInt(h, 2);
    ExpectRgba(pixels + 0, 0, 0, 0, 255);
    ExpectRgba(pixels + 4, 255, 255, 255, 255);
    ExpectRgba(pixels + 8, 255, 0, 0, 255);
    ExpectRgba(pixels + 12, 0, 255, 0, 255);
    ExpectRgba(pixels + 16, 0, 0, 255, 255);
    ExpectRgba(pixels + 20, 255, 255, 0, 255);
    ExpectRgba(pixels + 24, 0, 255, 255, 255);
    ExpectRgba(pixels + 28, 255, 0, 255, 128);

    GfxEncodedBufferRelease(&buf);
    GfxSurfaceRelease(s);
}

static void TestEncodeRegion(void)
{
    GfxSurface *s = NULL;
    GfxEncodeOptions opts;
    GfxEncodedBuffer buf;
    GfxRect region;
    uint8_t pixels[64];
    size_t plen = 0u;
    uint32_t w = 0u;
    uint32_t h = 0u;
    int rgba = 0;

    RoboCheck(MakeSurface(4, 4, RoboGfxFormatRgba8, &s) == RoboOk);
    RoboCheck(GfxSurfaceClear(s, GfxRgba8(1, 2, 3, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 1, 1,
                                          GfxRgba8(9, 8, 7, 6)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 2, 2,
                                          GfxRgba8(5, 4, 3, 2)) ==
               RoboOk);

    GfxEncodeOptionsInit(&opts);
    region = MakeGfxRect(1, 1, 2, 2);
    RoboCheck(GfxSurfaceEncodeRect(s, region, &opts, &buf) == RoboOk);
    RoboCheck(PngDecodePixels(buf.data, buf.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    RoboCheckEqInt(w, 2);
    RoboCheckEqInt(h, 2);
    ExpectRgba(pixels + 0, 9, 8, 7, 6);
    ExpectRgba(pixels + 4, 1, 2, 3, 255);
    ExpectRgba(pixels + 8, 1, 2, 3, 255);
    ExpectRgba(pixels + 12, 5, 4, 3, 2);
    GfxEncodedBufferRelease(&buf);

    /* Edge strip */
    region = MakeGfxRect(0, 3, 4, 1);
    RoboCheck(GfxSurfaceEncodeRect(s, region, &opts, &buf) == RoboOk);
    RoboCheck(PngDecodePixels(buf.data, buf.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    RoboCheckEqInt(w, 4);
    RoboCheckEqInt(h, 1);
    GfxEncodedBufferRelease(&buf);

    /* Invalid / empty */
    region = MakeGfxRect(0, 0, 0, 1);
    RoboCheck(GfxSurfaceEncodeRect(s, region, &opts, &buf) ==
               RoboInvalidArgument);
    region = MakeGfxRect(3, 3, 2, 2);
    RoboCheck(GfxSurfaceEncodeRect(s, region, &opts, &buf) ==
               RoboInvalidArgument);
    region = MakeGfxRect(-1, 0, 1, 1);
    RoboCheck(GfxSurfaceEncodeRect(s, region, &opts, &buf) ==
               RoboInvalidArgument);

    GfxSurfaceRelease(s);
}

static void TestEncodeBmp24And32(void)
{
    GfxSurface *s = NULL;
    GfxEncodeOptions opts;
    GfxEncodedBuffer buf;
    uint32_t off;
    uint32_t dib;
    uint16_t bpp;
    uint32_t w;
    uint32_t h;
    const uint8_t *pix;

    RoboCheck(MakeSurface(2, 2, RoboGfxFormatRgba8, &s) == RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 0, 0,
                                          GfxRgba8(255, 0, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 1, 0,
                                          GfxRgba8(0, 255, 0, 255)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 0, 1,
                                          GfxRgba8(0, 0, 255, 128)) ==
               RoboOk);
    RoboCheck(GfxSurfaceSetPixel(s, 1, 1,
                                          GfxRgba8(255, 255, 255, 0)) ==
               RoboOk);

    GfxEncodeOptionsInit(&opts);
    opts.container = RoboGfxEncodeBmp;
    opts.layout = RoboGfxEncodeLayoutRgb8;
    opts.backgroundSet = 1;
    opts.background = GfxRgb8(0, 0, 0);

    RoboCheck(GfxSurfaceEncode(s, &opts, &buf) == RoboOk);
    RoboCheck(buf.size >= 54u);
    RoboCheck(buf.data[0] == 'B' && buf.data[1] == 'M');
    off = ReadU32le(buf.data + 10);
    dib = ReadU32le(buf.data + 14);
    w = ReadU32le(buf.data + 18);
    h = ReadU32le(buf.data + 22);
    bpp = ReadU16le(buf.data + 28);
    RoboCheckEqInt(dib, 40);
    RoboCheckEqInt(w, 2);
    RoboCheckEqInt(h, 2);
    RoboCheckEqInt(bpp, 24);
    /* Bottom row in file is top of image? bottom-up: first row is y=h-1 */
    pix = buf.data + off;
    /* First stored row = surface y=1: blue (0,0,255,128) over black → ... */
    {
        GfxColorRgba8 c = GfxCompositeSrcOver(
            GfxRgba8(0, 0, 255, 128), GfxRgb8(0, 0, 0));
        RoboCheckEqInt(pix[0], c.b);
        RoboCheckEqInt(pix[1], c.g);
        RoboCheckEqInt(pix[2], c.r);
    }
    /* Padding: 2*3=6 → pad 2 to 8 */
    RoboCheckEqInt(pix[6], 0);
    RoboCheckEqInt(pix[7], 0);
    GfxEncodedBufferRelease(&buf);

    opts.layout = RoboGfxEncodeLayoutRgba8;
    opts.backgroundSet = 0;
    RoboCheck(GfxSurfaceEncode(s, &opts, &buf) == RoboOk);
    dib = ReadU32le(buf.data + 14);
    bpp = ReadU16le(buf.data + 28);
    off = ReadU32le(buf.data + 10);
    RoboCheckEqInt(dib, 108);
    RoboCheckEqInt(bpp, 32);
    pix = buf.data + off;
    /* First row = y=1: (0,0,255,128) as BGRA */
    RoboCheckEqInt(pix[0], 255);
    RoboCheckEqInt(pix[1], 0);
    RoboCheckEqInt(pix[2], 0);
    RoboCheckEqInt(pix[3], 128);

    GfxEncodedBufferRelease(&buf);
    GfxSurfaceRelease(s);
}

static Result SinkAppend(const void *bytes, size_t length, void *user)
{
    MemSink *m = (MemSink *)user;
    uint8_t *p;
    if (m->size > SIZE_MAX - length) {
        return RoboInvalidArgument;
    }
    p = (uint8_t *)Realloc(m->data, m->size + length);
    if (!p) {
        return RoboOutOfMemory;
    }
    m->data = p;
    memcpy(m->data + m->size, bytes, length);
    m->size += length;
    return RoboOk;
}

static void TestEncodeSinkAndImage(void)
{
    GfxSurface *s = NULL;
    GfxImage *img = NULL;
    GfxEncodeOptions opts;
    GfxEncodedBuffer buf;
    GfxRect bounds;
    MemSink sink;

    RoboCheck(MakeSurface(3, 2, RoboGfxFormatRgb8, &s) == RoboOk);
    RoboCheck(GfxSurfaceClear(s, GfxRgb8(11, 22, 33)) == RoboOk);

    GfxEncodeOptionsInit(&opts);
    opts.layout = RoboGfxEncodeLayoutRgb8;

    memset(&sink, 0, sizeof(sink));
    RoboCheck(GfxSurfacePixelBounds(s, &bounds) == RoboOk);
    RoboCheck(GfxSurfaceEncodeToSink(s, bounds, &opts, SinkAppend,
                                               &sink) == RoboOk);
    RoboCheck(GfxSurfaceEncode(s, &opts, &buf) == RoboOk);
    RoboCheck(sink.size == buf.size);
    RoboCheck(memcmp(sink.data, buf.data, buf.size) == 0);
    RoboFree(sink.data);
    GfxEncodedBufferRelease(&buf);

    RoboCheck(GfxImageCreateFromSurface(s, &img) == RoboOk);
    RoboCheck(GfxImageEncode(img, &opts, &buf) == RoboOk);
    RoboCheck(buf.size > 0u);
    GfxEncodedBufferRelease(&buf);
    GfxImageRelease(img);
    GfxSurfaceRelease(s);
}

static void TestEncodeFormatsAndSafety(void)
{
    GfxSurface *bgra = NULL;
    GfxSurface *gray = NULL;
    GfxEncodeOptions opts;
    GfxEncodedBuffer buf;
    uint8_t pixels[32];
    size_t plen = 0u;
    uint32_t w = 0u;
    uint32_t h = 0u;
    int rgba = 0;

    RoboCheck(MakeSurface(1, 1, RoboGfxFormatBgra8, &bgra) == RoboOk);
    RoboCheck(GfxSurfaceSetPixel(bgra, 0, 0,
                                          GfxRgba8(1, 2, 3, 4)) ==
               RoboOk);
    GfxEncodeOptionsInit(&opts);
    RoboCheck(GfxSurfaceEncode(bgra, &opts, &buf) == RoboOk);
    RoboCheck(PngDecodePixels(buf.data, buf.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    ExpectRgba(pixels, 1, 2, 3, 4);
    GfxEncodedBufferRelease(&buf);
    GfxSurfaceRelease(bgra);

    RoboCheck(MakeSurface(1, 1, RoboGfxFormatGray8, &gray) == RoboOk);
    RoboCheck(GfxSurfaceSetPixel(gray, 0, 0,
                                          GfxRgba8(100, 100, 100, 255)) ==
               RoboOk);
    opts.layout = RoboGfxEncodeLayoutRgb8;
    RoboCheck(GfxSurfaceEncode(gray, &opts, &buf) == RoboOk);
    RoboCheck(PngDecodePixels(buf.data, buf.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    RoboCheckEqInt(rgba, 0);
    ExpectRgb(pixels, 100, 100, 100);
    GfxEncodedBufferRelease(&buf);

    RoboCheck(GfxSurfaceEncode(NULL, &opts, &buf) ==
               RoboInvalidArgument);
    RoboCheck(GfxSurfaceEncode(gray, NULL, &buf) ==
               RoboInvalidArgument);
    GfxSurfaceRelease(gray);
}

static void TestEncodeIendCrc(void)
{
    GfxSurface *s = NULL;
    GfxEncodeOptions opts;
    GfxEncodedBuffer buf;
    size_t off;

    RoboCheck(MakeSurface(1, 1, RoboGfxFormatRgba8, &s) == RoboOk);
    GfxEncodeOptionsInit(&opts);
    RoboCheck(GfxSurfaceEncode(s, &opts, &buf) == RoboOk);
    /* Last chunk IEND CRC is fixed AE426082 */
    RoboCheck(buf.size >= 12u);
    off = buf.size - 8u;
    RoboCheck(memcmp(buf.data + off, "IEND", 4u) == 0);
    RoboCheckEqInt(buf.data[buf.size - 4], 0xAE);
    RoboCheckEqInt(buf.data[buf.size - 3], 0x42);
    RoboCheckEqInt(buf.data[buf.size - 2], 0x60);
    RoboCheckEqInt(buf.data[buf.size - 1], 0x82);
    GfxEncodedBufferRelease(&buf);
    GfxSurfaceRelease(s);
}

static void TestEncodeLargerAndStride(void)
{
    GfxSurface *s = NULL;
    GfxSurfaceDesc desc;
    GfxEncodeOptions opts;
    GfxEncodedBuffer a;
    GfxEncodedBuffer b;
    uint32_t x;
    uint32_t y;

    GfxSurfaceDescInit(&desc);
    desc.width = 17;
    desc.height = 13;
    desc.format = RoboGfxFormatRgba8;
    desc.strideBytes = 17u * 4u + 8u; /* padded stride */
    desc.access = RoboGfxSurfaceAccessReadWrite;
    RoboCheck(GfxSurfaceCreate(&desc, &s) == RoboOk);
    for (y = 0u; y < 13u; y++) {
        for (x = 0u; x < 17u; x++) {
            RoboCheck(GfxSurfaceSetPixel(
                           s, x, y,
                           GfxRgba8((uint8_t)x, (uint8_t)y, 50,
                                          200)) == RoboOk);
        }
    }
    GfxEncodeOptionsInit(&opts);
    RoboCheck(GfxSurfaceEncode(s, &opts, &a) == RoboOk);
    RoboCheck(GfxSurfaceEncode(s, &opts, &b) == RoboOk);
    RoboCheck(a.size == b.size);
    RoboCheck(memcmp(a.data, b.data, a.size) == 0);
    {
        uint8_t pixels[17 * 13 * 4];
        size_t plen = 0u;
        uint32_t w = 0u;
        uint32_t h = 0u;
        int rgba = 0;
        RoboCheck(PngDecodePixels(a.data, a.size, &w, &h, &rgba, pixels,
                                     sizeof(pixels), &plen));
        RoboCheckEqInt(w, 17);
        RoboCheckEqInt(h, 13);
        ExpectRgba(pixels + (12 * 17 + 16) * 4, 16, 12, 50, 200);
    }
    GfxEncodedBufferRelease(&a);
    GfxEncodedBufferRelease(&b);
    GfxSurfaceRelease(s);
}

void TestRegisterGraphicsEncode(void)
{
    TestRegister("graphics.encode.png_1x1", TestEncodePng1x1Rgba);
    TestRegister("graphics.encode.png_rgb_alpha_policy",
                       TestEncodePngRgbAndAlphaPolicy);
    TestRegister("graphics.encode.png_channels", TestEncodePngChannels);
    TestRegister("graphics.encode.region", TestEncodeRegion);
    TestRegister("graphics.encode.bmp", TestEncodeBmp24And32);
    TestRegister("graphics.encode.sink_image",
                       TestEncodeSinkAndImage);
    TestRegister("graphics.encode.formats_safety",
                       TestEncodeFormatsAndSafety);
    TestRegister("graphics.encode.iend_crc", TestEncodeIendCrc);
    TestRegister("graphics.encode.stride_large",
                       TestEncodeLargerAndStride);
}
