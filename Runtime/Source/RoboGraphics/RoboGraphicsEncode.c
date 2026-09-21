/*
 * RoboGraphics Goal 6.13 — surface → encode → memory / sink.
 *
 * PNG: signature + IHDR + IDAT (zlib store blocks) + IEND.
 * BMP: BITMAPINFOHEADER 24-bpp BGR, or BITMAPV4HEADER BI_BITFIELDS BGRA.
 */

#include "RoboGraphics/RoboGraphicsEncode.h"

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsComposite.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboMem/RoboMem.h"

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
} EncodeBuf;

static void BufInit(EncodeBuf *b)
{
    b->data = NULL;
    b->size = 0u;
    b->cap = 0u;
}

static void BufFree(EncodeBuf *b)
{
    if (b->data) {
        RoboFree(b->data);
    }
    BufInit(b);
}

static Result BufReserve(EncodeBuf *b, size_t need)
{
    size_t ncap;
    uint8_t *p;

    if (need <= b->cap) {
        return RoboOk;
    }
    ncap = b->cap ? b->cap : 256u;
    while (ncap < need) {
        if (ncap > (SIZE_MAX / 2u)) {
            return RoboOutOfMemory;
        }
        ncap *= 2u;
    }
    p = (uint8_t *)Realloc(b->data, ncap);
    if (!p) {
        return RoboOutOfMemory;
    }
    b->data = p;
    b->cap = ncap;
    return RoboOk;
}

static Result BufAppend(EncodeBuf *b, const void *src, size_t n)
{
    Result res;

    if (n == 0u) {
        return RoboOk;
    }
    if (b->size > SIZE_MAX - n) {
        return RoboInvalidArgument;
    }
    res = BufReserve(b, b->size + n);
    if (res != RoboOk) {
        return res;
    }
    memcpy(b->data + b->size, src, n);
    b->size += n;
    return RoboOk;
}

static Result BufAppendU8(EncodeBuf *b, uint8_t v)
{
    return BufAppend(b, &v, 1u);
}

static Result BufAppendU16le(EncodeBuf *b, uint16_t v)
{
    uint8_t p[2];
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    return BufAppend(b, p, 2u);
}

static Result BufAppendU32le(EncodeBuf *b, uint32_t v)
{
    uint8_t p[4];
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
    return BufAppend(b, p, 4u);
}

static Result BufAppendU32be(EncodeBuf *b, uint32_t v)
{
    uint8_t p[4];
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
    return BufAppend(b, p, 4u);
}

static uint32_t PngCrc32(const uint8_t *data, size_t len)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    int k;

    for (i = 0u; i < len; i++) {
        c ^= data[i];
        for (k = 0; k < 8; k++) {
            if (c & 1u) {
                c = 0xEDB88320u ^ (c >> 1);
            } else {
                c >>= 1;
            }
        }
    }
    return c ^ 0xFFFFFFFFu;
}

static uint32_t adler32(const uint8_t *data, size_t len)
{
    uint32_t s1 = 1u;
    uint32_t s2 = 0u;
    size_t i;

    for (i = 0u; i < len; i++) {
        s1 = (s1 + data[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    return (s2 << 16) | s1;
}

static int SourceStoresAlpha(GfxPixelFormat fmt)
{
    GfxPixelFormatInfo info;
    if (GetGfxPixelFormatInfo(fmt, &info) != RoboOk) {
        return 0;
    }
    return info.alphaStored ? 1 : 0;
}

static int OptsValid(const GfxEncodeOptions *opts,
                      GfxPixelFormat srcFmt)
{
    if (!opts) {
        return 0;
    }
    if (opts->container != RoboGfxEncodePng &&
        opts->container != RoboGfxEncodeBmp) {
        return 0;
    }
    if (opts->layout != RoboGfxEncodeLayoutRgba8 &&
        opts->layout != RoboGfxEncodeLayoutRgb8) {
        return 0;
    }
    if (opts->layout == RoboGfxEncodeLayoutRgb8 &&
        SourceStoresAlpha(srcFmt) && !opts->backgroundSet) {
        return 0;
    }
    return 1;
}

static GfxColorRgba8 SamplePixel(const GfxSurface *surface,
                                           uint32_t x,
                                           uint32_t y,
                                           const GfxEncodeOptions *opts)
{
    GfxColorRgba8 c = GfxRgba8(0, 0, 0, 0);
    if (GfxSurfaceGetPixel(surface, x, y, &c) != RoboOk) {
        return GfxRgba8(0, 0, 0, 0);
    }
    if (opts->layout == RoboGfxEncodeLayoutRgb8) {
        if (SourceStoresAlpha(GfxSurfaceFormat(surface))) {
            c = GfxCompositeSrcOver(c, opts->background);
        }
        c.a = 255u;
    }
    return c;
}

static Result MulSize(size_t a, size_t b, size_t *out)
{
    if (a != 0u && b > SIZE_MAX / a) {
        return RoboInvalidArgument;
    }
    *out = a * b;
    return RoboOk;
}

static Result AddSize(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b) {
        return RoboInvalidArgument;
    }
    *out = a + b;
    return RoboOk;
}

static Result RegionToU32(GfxRect region,
                                   uint32_t *ox,
                                   uint32_t *oy,
                                   uint32_t *w,
                                   uint32_t *h)
{
    if (GfxRectIsEmpty(region) || region.width < 0 ||
        region.height < 0) {
        return RoboInvalidArgument;
    }
    if (region.x < 0 || region.y < 0) {
        return RoboInvalidArgument;
    }
    *ox = (uint32_t)region.x;
    *oy = (uint32_t)region.y;
    *w = (uint32_t)region.width;
    *h = (uint32_t)region.height;
    return RoboOk;
}

static int RectInsideSurface(GfxRect rect,
                               const GfxSurface *surface)
{
    GfxRect bounds;
    int64_t rr;
    int64_t rb;

    if (GfxSurfacePixelBounds(surface, &bounds) != RoboOk) {
        return 0;
    }
    if (GfxRectIsEmpty(rect) || rect.width < 0 || rect.height < 0) {
        return 0;
    }
    if (rect.x < bounds.x || rect.y < bounds.y) {
        return 0;
    }
    rr = (int64_t)rect.x + (int64_t)rect.width;
    rb = (int64_t)rect.y + (int64_t)rect.height;
    if (rr > (int64_t)bounds.x + (int64_t)bounds.width) {
        return 0;
    }
    if (rb > (int64_t)bounds.y + (int64_t)bounds.height) {
        return 0;
    }
    return 1;
}

static Result PngAppendChunk(EncodeBuf *out,
                                      const char type[4],
                                      const uint8_t *payload,
                                      size_t payloadLen)
{
    uint8_t typeBytes[4];
    EncodeBuf crcSrc;
    uint32_t crc;
    Result res;

    typeBytes[0] = (uint8_t)type[0];
    typeBytes[1] = (uint8_t)type[1];
    typeBytes[2] = (uint8_t)type[2];
    typeBytes[3] = (uint8_t)type[3];

    res = BufAppendU32be(out, (uint32_t)payloadLen);
    if (res != RoboOk) {
        return res;
    }
    res = BufAppend(out, typeBytes, 4u);
    if (res != RoboOk) {
        return res;
    }
    if (payloadLen > 0u) {
        res = BufAppend(out, payload, payloadLen);
        if (res != RoboOk) {
            return res;
        }
    }

    BufInit(&crcSrc);
    res = BufAppend(&crcSrc, typeBytes, 4u);
    if (res == RoboOk && payloadLen > 0u) {
        res = BufAppend(&crcSrc, payload, payloadLen);
    }
    if (res != RoboOk) {
        BufFree(&crcSrc);
        return res;
    }
    crc = PngCrc32(crcSrc.data, crcSrc.size);
    BufFree(&crcSrc);
    return BufAppendU32be(out, crc);
}

static Result ZlibStore(EncodeBuf *out,
                                const uint8_t *raw,
                                size_t rawLen)
{
    const size_t maxBlock = 65535u;
    size_t off = 0u;
    Result res;
    uint32_t ad;

    /* CMF/FLG: CM=8, CINFO=7, FLEVEL=0 → 0x78 0x01 */
    res = BufAppendU8(out, 0x78u);
    if (res != RoboOk) {
        return res;
    }
    res = BufAppendU8(out, 0x01u);
    if (res != RoboOk) {
        return res;
    }

    if (rawLen == 0u) {
        res = BufAppendU8(out, 0x01u); /* final empty store */
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU16le(out, 0u);
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU16le(out, 0xFFFFu);
        if (res != RoboOk) {
            return res;
        }
    } else {
        while (off < rawLen) {
            size_t chunk = rawLen - off;
            int last;
            if (chunk > maxBlock) {
                chunk = maxBlock;
            }
            last = (off + chunk >= rawLen) ? 1 : 0;
            res = BufAppendU8(out, last ? 0x01u : 0x00u);
            if (res != RoboOk) {
                return res;
            }
            res = BufAppendU16le(out, (uint16_t)chunk);
            if (res != RoboOk) {
                return res;
            }
            res = BufAppendU16le(out, (uint16_t)(0xFFFFu ^ (uint16_t)chunk));
            if (res != RoboOk) {
                return res;
            }
            res = BufAppend(out, raw + off, chunk);
            if (res != RoboOk) {
                return res;
            }
            off += chunk;
        }
    }

    ad = adler32(raw, rawLen);
    return BufAppendU32be(out, ad);
}

static Result BuildFilteredRgbaRgb(
    const GfxSurface *surface,
    uint32_t ox,
    uint32_t oy,
    uint32_t w,
    uint32_t h,
    const GfxEncodeOptions *opts,
    EncodeBuf *raw)
{
    uint32_t y;
    uint32_t x;
    uint8_t bpp = (opts->layout == RoboGfxEncodeLayoutRgba8) ? 4u : 3u;
    size_t rowPayload;
    size_t filteredRow;
    size_t total;
    Result res;

    res = MulSize((size_t)w, (size_t)bpp, &rowPayload);
    if (res != RoboOk) {
        return res;
    }
    res = AddSize(rowPayload, 1u, &filteredRow);
    if (res != RoboOk) {
        return res;
    }
    res = MulSize(filteredRow, (size_t)h, &total);
    if (res != RoboOk) {
        return res;
    }

    res = BufReserve(raw, total);
    if (res != RoboOk) {
        return res;
    }

    for (y = 0u; y < h; y++) {
        res = BufAppendU8(raw, 0u); /* filter None */
        if (res != RoboOk) {
            return res;
        }
        for (x = 0u; x < w; x++) {
            GfxColorRgba8 c =
                SamplePixel(surface, ox + x, oy + y, opts);
            uint8_t px[4];
            px[0] = c.r;
            px[1] = c.g;
            px[2] = c.b;
            if (bpp == 4u) {
                px[3] = c.a;
                res = BufAppend(raw, px, 4u);
            } else {
                res = BufAppend(raw, px, 3u);
            }
            if (res != RoboOk) {
                return res;
            }
        }
    }
    return RoboOk;
}

static Result EncodePng(const GfxSurface *surface,
                                uint32_t ox,
                                uint32_t oy,
                                uint32_t w,
                                uint32_t h,
                                const GfxEncodeOptions *opts,
                                EncodeBuf *out)
{
    static const uint8_t sig[8] = {
        0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    uint8_t ihdr[13];
    EncodeBuf raw;
    EncodeBuf zlib;
    Result res;
    uint8_t colorType;

    if (w == 0u || h == 0u) {
        return RoboInvalidArgument;
    }

    colorType = (opts->layout == RoboGfxEncodeLayoutRgba8) ? 6u : 2u;

    ihdr[0] = (uint8_t)((w >> 24) & 0xFFu);
    ihdr[1] = (uint8_t)((w >> 16) & 0xFFu);
    ihdr[2] = (uint8_t)((w >> 8) & 0xFFu);
    ihdr[3] = (uint8_t)(w & 0xFFu);
    ihdr[4] = (uint8_t)((h >> 24) & 0xFFu);
    ihdr[5] = (uint8_t)((h >> 16) & 0xFFu);
    ihdr[6] = (uint8_t)((h >> 8) & 0xFFu);
    ihdr[7] = (uint8_t)(h & 0xFFu);
    ihdr[8] = 8u; /* bit depth */
    ihdr[9] = colorType;
    ihdr[10] = 0u;
    ihdr[11] = 0u;
    ihdr[12] = 0u;

    res = BufAppend(out, sig, 8u);
    if (res != RoboOk) {
        return res;
    }
    res = PngAppendChunk(out, "IHDR", ihdr, 13u);
    if (res != RoboOk) {
        return res;
    }

    BufInit(&raw);
    BufInit(&zlib);
    res = BuildFilteredRgbaRgb(surface, ox, oy, w, h, opts, &raw);
    if (res == RoboOk) {
        res = ZlibStore(&zlib, raw.data, raw.size);
    }
    BufFree(&raw);
    if (res == RoboOk) {
        res = PngAppendChunk(out, "IDAT", zlib.data, zlib.size);
    }
    BufFree(&zlib);
    if (res != RoboOk) {
        return res;
    }
    return PngAppendChunk(out, "IEND", NULL, 0u);
}

static size_t BmpRowStride24(uint32_t w)
{
    /* 3*w padded to multiple of 4 */
    size_t row = (size_t)w * 3u;
    return (row + 3u) & ~(size_t)3u;
}

static size_t BmpRowStride32(uint32_t w)
{
    return (size_t)w * 4u; /* already multiple of 4 */
}

static Result EncodeBmp(const GfxSurface *surface,
                                uint32_t ox,
                                uint32_t oy,
                                uint32_t w,
                                uint32_t h,
                                const GfxEncodeOptions *opts,
                                EncodeBuf *out)
{
    int withAlpha = (opts->layout == RoboGfxEncodeLayoutRgba8) ? 1 : 0;
    uint32_t dibSize = withAlpha ? 108u : 40u;
    uint32_t bpp = withAlpha ? 32u : 24u;
    size_t rowStride;
    size_t pixelBytes;
    size_t fileSize;
    uint32_t offBits;
    uint32_t y;
    uint32_t x;
    Result res;

    if (w == 0u || h == 0u) {
        return RoboInvalidArgument;
    }

    if (withAlpha) {
        rowStride = BmpRowStride32(w);
    } else {
        rowStride = BmpRowStride24(w);
    }
    res = MulSize(rowStride, (size_t)h, &pixelBytes);
    if (res != RoboOk) {
        return res;
    }
    offBits = 14u + dibSize;
    res = AddSize((size_t)offBits, pixelBytes, &fileSize);
    if (res != RoboOk) {
        return res;
    }
    if (fileSize > 0xFFFFFFFFu) {
        return RoboInvalidArgument;
    }

    /* BITMAPFILEHEADER */
    res = BufAppendU8(out, 'B');
    if (res != RoboOk) {
        return res;
    }
    res = BufAppendU8(out, 'M');
    if (res != RoboOk) {
        return res;
    }
    res = BufAppendU32le(out, (uint32_t)fileSize);
    if (res != RoboOk) {
        return res;
    }
    res = BufAppendU16le(out, 0u);
    if (res != RoboOk) {
        return res;
    }
    res = BufAppendU16le(out, 0u);
    if (res != RoboOk) {
        return res;
    }
    res = BufAppendU32le(out, offBits);
    if (res != RoboOk) {
        return res;
    }

    /* DIB header */
    res = BufAppendU32le(out, dibSize);
    if (res != RoboOk) {
        return res;
    }
    res = BufAppendU32le(out, w);
    if (res != RoboOk) {
        return res;
    }
    res = BufAppendU32le(out, h); /* bottom-up */
    if (res != RoboOk) {
        return res;
    }
    res = BufAppendU16le(out, 1u); /* planes */
    if (res != RoboOk) {
        return res;
    }
    res = BufAppendU16le(out, (uint16_t)bpp);
    if (res != RoboOk) {
        return res;
    }
    if (withAlpha) {
        /* BI_BITFIELDS */
        res = BufAppendU32le(out, 3u);
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, (uint32_t)pixelBytes);
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, 2835u); /* ~72 DPI */
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, 2835u);
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, 0u);
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, 0u);
        if (res != RoboOk) {
            return res;
        }
        /* BGRA masks */
        res = BufAppendU32le(out, 0x00FF0000u); /* R */
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, 0x0000FF00u); /* G */
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, 0x000000FFu); /* B */
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, 0xFF000000u); /* A */
        if (res != RoboOk) {
            return res;
        }
        /* CSType = LCS_WINDOWS_COLOR_SPACE 'Win ' */
        res = BufAppendU32le(out, 0x57696E20u);
        if (res != RoboOk) {
            return res;
        }
        /* endpoints + gamma (36 + 12 = 48 bytes zeros) */
        {
            uint8_t zeros[48];
            memset(zeros, 0, sizeof(zeros));
            res = BufAppend(out, zeros, sizeof(zeros));
            if (res != RoboOk) {
                return res;
            }
        }
    } else {
        res = BufAppendU32le(out, 0u); /* BI_RGB */
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, (uint32_t)pixelBytes);
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, 2835u);
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, 2835u);
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, 0u);
        if (res != RoboOk) {
            return res;
        }
        res = BufAppendU32le(out, 0u);
        if (res != RoboOk) {
            return res;
        }
    }

    /* Pixel data bottom-up */
    for (y = 0u; y < h; y++) {
        uint32_t srcY = oy + (h - 1u - y);
        size_t written = 0u;
        for (x = 0u; x < w; x++) {
            GfxColorRgba8 c =
                SamplePixel(surface, ox + x, srcY, opts);
            if (withAlpha) {
                uint8_t px[4];
                px[0] = c.b;
                px[1] = c.g;
                px[2] = c.r;
                px[3] = c.a;
                res = BufAppend(out, px, 4u);
                written += 4u;
            } else {
                uint8_t px[3];
                px[0] = c.b;
                px[1] = c.g;
                px[2] = c.r;
                res = BufAppend(out, px, 3u);
                written += 3u;
            }
            if (res != RoboOk) {
                return res;
            }
        }
        while (written < rowStride) {
            res = BufAppendU8(out, 0u);
            if (res != RoboOk) {
                return res;
            }
            written++;
        }
    }
    return RoboOk;
}

static Result EncodeRegion(const GfxSurface *surface,
                                   GfxRect region,
                                   const GfxEncodeOptions *opts,
                                   GfxEncodedBuffer *out)
{
    uint32_t ox;
    uint32_t oy;
    uint32_t w;
    uint32_t h;
    EncodeBuf buf;
    Result res;

    if (!surface || !opts || !out) {
        return RoboInvalidArgument;
    }
    out->data = NULL;
    out->size = 0u;

    if (!GfxPixelFormatIsImplemented(GfxSurfaceFormat(surface))) {
        return RoboNotSupported;
    }
    if (!OptsValid(opts, GfxSurfaceFormat(surface))) {
        return RoboInvalidArgument;
    }
    if (!RectInsideSurface(region, surface)) {
        return RoboInvalidArgument;
    }
    res = RegionToU32(region, &ox, &oy, &w, &h);
    if (res != RoboOk) {
        return res;
    }

    BufInit(&buf);
    if (opts->container == RoboGfxEncodePng) {
        res = EncodePng(surface, ox, oy, w, h, opts, &buf);
    } else {
        res = EncodeBmp(surface, ox, oy, w, h, opts, &buf);
    }
    if (res != RoboOk) {
        BufFree(&buf);
        return res;
    }
    out->data = buf.data;
    out->size = buf.size;
    return RoboOk;
}

void GfxEncodeOptionsInit(GfxEncodeOptions *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->container = RoboGfxEncodePng;
    opts->layout = RoboGfxEncodeLayoutRgba8;
    opts->backgroundSet = 0;
    opts->background = GfxRgb8(0, 0, 0);
}

void GfxEncodedBufferRelease(GfxEncodedBuffer *buf)
{
    if (!buf) {
        return;
    }
    if (buf->data) {
        RoboFree(buf->data);
    }
    buf->data = NULL;
    buf->size = 0u;
}

Result GfxSurfaceEncode(const GfxSurface *surface,
                                      const GfxEncodeOptions *opts,
                                      GfxEncodedBuffer *out)
{
    GfxRect bounds;
    Result res;

    if (!surface || !opts || !out) {
        return RoboInvalidArgument;
    }
    res = GfxSurfacePixelBounds(surface, &bounds);
    if (res != RoboOk) {
        return res;
    }
    return EncodeRegion(surface, bounds, opts, out);
}

Result GfxSurfaceEncodeRect(const GfxSurface *surface,
                                           GfxRect region,
                                           const GfxEncodeOptions *opts,
                                           GfxEncodedBuffer *out)
{
    return EncodeRegion(surface, region, opts, out);
}

Result GfxImageEncode(GfxImage *image,
                                    const GfxEncodeOptions *opts,
                                    GfxEncodedBuffer *out)
{
    GfxSurface *surf;

    if (!image || !opts || !out) {
        return RoboInvalidArgument;
    }
    if (MakeGfxImageKind(image) != RoboGfxImageKindRaster) {
        return RoboInvalidArgument;
    }
    surf = GfxImageSurface(image);
    if (!surf) {
        return RoboInvalidArgument;
    }
    return GfxSurfaceEncode(surf, opts, out);
}

Result GfxSurfaceEncodeToSink(
    const GfxSurface *surface,
    GfxRect region,
    const GfxEncodeOptions *opts,
    GfxEncodeSinkFn sink,
    void *user)
{
    GfxEncodedBuffer buf;
    Result res;

    if (!sink) {
        return RoboInvalidArgument;
    }
    res = EncodeRegion(surface, region, opts, &buf);
    if (res != RoboOk) {
        return res;
    }
    res = sink(buf.data, buf.size, user);
    GfxEncodedBufferRelease(&buf);
    return res;
}
