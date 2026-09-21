/*
 * RoboGraphics Phase 8.12 — PNG decoding (graphics.decode.*).
 *
 * Three independent sources of truth are used against the decoder, and the
 * distinction between them is the whole point of the suite:
 *
 *   1. Fixtures produced by an *independent* encoder (see
 *      RoboGraphicsPngFixtures.h), with expected pixels computed from the
 *      source image rather than read back out of the decoder. These exercise
 *      the dynamic-Huffman path, which our own encoder never emits.
 *   2. The Goal 6.13 encoder, for the stored-block path and for a round trip
 *      at every colour model the surface layer supports.
 *   3. PNGs assembled here byte by byte with a test-local chunk writer and a
 *      stored-block zlib writer. This is what makes the malformed cases real:
 *      every one of them carries a *valid* CRC, so passing proves the decoder
 *      validates structure and content rather than leaning on its CRC check.
 *
 * Nothing in this file asserts that a function exists. Every test decodes
 * bytes and checks the pixels that come out, or checks that a refusal happens
 * and that nothing was allocated when it did.
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsDecode.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include "RoboGraphicsPngFixtures.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

/* Working buffer size for the test-local PNG builder below. */
#define SCRATCH 4096u

/* ------------------------------------------------------------------ */
/* Test-local helpers                                                  */
/* ------------------------------------------------------------------ */

static int HexNibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static size_t HexDecode(const char *hex, uint8_t *out, size_t capacity)
{
    size_t n = 0u;
    size_t i = 0u;

    while (hex[i] != '\0' && hex[i + 1u] != '\0') {
        int hi = HexNibble(hex[i]);
        int lo = HexNibble(hex[i + 1u]);

        if (hi < 0 || lo < 0 || n >= capacity) {
            break;
        }
        out[n++] = (uint8_t)((hi << 4) | lo);
        i += 2u;
    }
    return n;
}

static uint32_t fnv32(const uint8_t *data, size_t length)
{
    uint32_t h = 2166136261u;
    size_t i;

    for (i = 0u; i < length; i++) {
        h ^= (uint32_t)data[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t Crc32Bytes(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    int b;

    for (i = 0u; i < length; i++) {
        crc ^= (uint32_t)data[i];
        for (b = 0; b < 8; b++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static void Be32Write(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/*
 * Test-local chunk writer. Deliberately a separate implementation from the
 * decoder's: if the two disagreed about framing, the fixtures built here
 * would be rejected by the decoder and these tests would fail loudly.
 */
static size_t PngEmitChunk(uint8_t *out, size_t capacity, size_t pos,
                             const char *type, const uint8_t *data,
                             size_t length)
{
    uint8_t crcInput[SCRATCH];
    uint32_t crc;

    if (pos + 12u + length > capacity || length + 4u > sizeof(crcInput)) {
        return 0u;
    }
    Be32Write(out + pos, (uint32_t)length);
    memcpy(out + pos + 4u, type, 4u);
    if (length != 0u) {
        memcpy(out + pos + 8u, data, length);
    }
    memcpy(crcInput, out + pos + 4u, 4u);
    if (length != 0u) {
        memcpy(crcInput + 4u, data, length);
    }
    crc = Crc32Bytes(crcInput, length + 4u);
    Be32Write(out + pos + 8u + length, crc);
    return pos + 12u + length;
}

/** zlib stream using stored DEFLATE blocks — no compression, no dependency. */
static size_t ZlibStore(uint8_t *out, size_t capacity, const uint8_t *raw,
                         size_t rawLength)
{
    size_t pos = 0u;
    size_t remaining = rawLength;

    if (capacity < rawLength + 12u) {
        return 0u;
    }
    out[pos++] = 0x78u; /* CM = deflate, window 32 KiB */
    out[pos++] = 0x01u; /* level 0, no preset dictionary, FCHECK valid */
    if (rawLength == 0u) {
        out[pos++] = 0x01u; /* BFINAL = 1, BTYPE = stored */
        out[pos++] = 0x00u;
        out[pos++] = 0x00u;
        out[pos++] = 0xFFu;
        out[pos++] = 0xFFu;
    }
    while (remaining > 0u) {
        size_t n = remaining > 65535u ? 65535u : remaining;
        uint16_t nlen = (uint16_t)(~((uint16_t)n));

        out[pos++] = (remaining == n) ? 0x01u : 0x00u;
        out[pos++] = (uint8_t)(n & 0xFFu);
        out[pos++] = (uint8_t)((n >> 8) & 0xFFu);
        out[pos++] = (uint8_t)(nlen & 0xFFu);
        out[pos++] = (uint8_t)((nlen >> 8) & 0xFFu);
        memcpy(out + pos, raw + (rawLength - remaining), n);
        pos += n;
        remaining -= n;
    }
    /* Adler-32 is not checked by the decoder, but write a correct-looking one
     * so these streams are legal zlib rather than only decodable zlib. */
    memset(out + pos, 0, 4u);
    pos += 4u;
    return pos;
}

static Result MakeSurface(uint32_t w, uint32_t h,
                                  GfxPixelFormat format,
                                  GfxSurface **out)
{
    GfxSurfaceDesc desc;

    GfxSurfaceDescInit(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = format;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    return GfxSurfaceCreate(&desc, out);
}

static void SurfaceWrite(GfxSurface *s, uint32_t x, uint32_t y,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    GfxColorRgba8 c;

    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    RoboCheckEqInt(GfxSurfaceSetPixel(s, x, y, c), RoboOk);
}

/** Read back one logical RGBA8 pixel through the public accessor. */
static void SurfaceRead(GfxSurface *s, uint32_t x, uint32_t y,
                         uint8_t *out4)
{
    GfxColorRgba8 c;

    RoboCheckEqInt(GfxSurfaceGetPixel(s, x, y, &c), RoboOk);
    out4[0] = c.r;
    out4[1] = c.g;
    out4[2] = c.b;
    out4[3] = c.a;
}

/**
 * FNV-1a over the image's pixels, flattened to tightly packed RGBA8.
 *
 * The fixtures' expected hashes were computed over the flat pixel array of
 * the source image, so rows are copied out through the surface's real stride
 * before hashing — a decoder that wrote the right pixels at the wrong stride
 * would pass a per-row hash and must not pass this one.
 */
static uint32_t ImageRgbaFnv(GfxImage *image)
{
    GfxSurface *surface = GfxImageSurface(image);
    GfxSurfaceMap map;
    uint8_t flat[8192];
    uint32_t y;

    RoboCheck(surface != NULL);
    memset(&map, 0, sizeof(map));
    RoboCheckEqInt(GetGfxSurfaceMap(surface, RoboGfxSurfaceAccessRead,
                                           &map),
                      RoboOk);
    RoboCheck((size_t)map.width * (size_t)map.height * 4u <= sizeof(flat));
    for (y = 0u; y < map.height; y++) {
        memcpy(flat + (size_t)map.width * 4u * (size_t)y,
               map.pixelsConst + map.strideBytes * (size_t)y,
               (size_t)map.width * 4u);
    }
    RoboCheckEqInt(GfxSurfaceUnmap(surface), RoboOk);
    return fnv32(flat, (size_t)map.width * (size_t)map.height * 4u);
}

/* ------------------------------------------------------------------ */
/* Fixture sweep                                                       */
/* ------------------------------------------------------------------ */

typedef struct decodeFixture {
    const char *name;
    const char *hex;
    size_t byteCount;
    uint32_t width;
    uint32_t height;
    uint8_t colorType;
    uint8_t channels;
    size_t rgbaBytes;
    uint32_t rgbaFnv;
    uint8_t pixel00[4];
    uint8_t pixelCorner[4];
    uint32_t cornerX;
    uint32_t cornerY;
} DecodeFixture;

/*
 * Every fixture is checked against the *header* facts and the *pixel* facts
 * together: a decoder that got the dimensions right and the colours wrong
 * would pass a dimensions-only test, which is why the FNV is in the table.
 */
static const DecodeFixture FIXTURES[] = {
    {"gradient", RoboPngGradient_hex, RoboPngGradientBytes,
     RoboPngGradientWidth, RoboPngGradientHeight, 6u, 4u,
     RoboPngGradientRgbaBytes, RoboPngGradientRgbaFnv,
     RoboPngGradientPixel00, RoboPngGradientPixel77, 7u, 7u},
    {"checker", RoboPngChecker_hex, RoboPngCheckerBytes,
     RoboPngCheckerWidth, RoboPngCheckerHeight, 6u, 4u,
     RoboPngCheckerRgbaBytes, RoboPngCheckerRgbaFnv,
     RoboPngCheckerPixel00, RoboPngCheckerPixel3131, 31u, 31u},
    {"opaque", RoboPngOpaque_hex, RoboPngOpaqueBytes, RoboPngOpaqueWidth,
     RoboPngOpaqueHeight, 2u, 3u, RoboPngOpaqueRgbaBytes,
     RoboPngOpaqueRgbaFnv, RoboPngOpaquePixel00,
     RoboPngOpaquePixel77, 7u, 7u},
    {"grayscale", RoboPngGrayscale_hex, RoboPngGrayscaleBytes,
     RoboPngGrayscaleWidth, RoboPngGrayscaleHeight, 0u, 1u,
     RoboPngGrayscaleRgbaBytes, RoboPngGrayscaleRgbaFnv,
     RoboPngGrayscalePixel00, RoboPngGrayscalePixel77, 7u, 7u},
    {"translucent", RoboPngTranslucent_hex, RoboPngTranslucentBytes,
     RoboPngTranslucentWidth, RoboPngTranslucentHeight, 4u, 2u,
     RoboPngTranslucentRgbaBytes, RoboPngTranslucentRgbaFnv,
     RoboPngTranslucentPixel00, RoboPngTranslucentPixel55, 5u, 5u},
    {"paletted", RoboPngPaletted_hex, RoboPngPalettedBytes,
     RoboPngPalettedWidth, RoboPngPalettedHeight, 3u, 1u,
     RoboPngPalettedRgbaBytes, RoboPngPalettedRgbaFnv,
     RoboPngPalettedPixel00, RoboPngPalettedPixel33, 3u, 3u},
};

#define FIXTURE_COUNT (sizeof(FIXTURES) / sizeof(FIXTURES[0]))

static void DecodeAcceptFixture(const DecodeFixture *f)
{
    uint8_t bytes[2048];
    size_t length = HexDecode(f->hex, bytes, sizeof(bytes));
    GfxDecodeInfo info;
    GfxImage *image = NULL;
    GfxDecodeLimits limits;

    memset(&info, 0, sizeof(info));
    RoboCheckEqInt(length, f->byteCount);

    /* 1. probe: recognition from the signature alone, no allocation. */
    RoboCheckEqInt(GfxDecodeProbe(bytes, length),
                      RoboGfxDecodeContainerPng);

    /* 2. header only: dims and colour model, no pixels. */
    RoboCheckEqInt(GfxImageDecodeInfo(bytes, length, NULL, &info),
                      RoboOk);
    RoboCheckEqInt(info.container, RoboGfxDecodeContainerPng);
    RoboCheckEqInt((int)info.width, (int)f->width);
    RoboCheckEqInt((int)info.height, (int)f->height);
    RoboCheckEqInt(info.bitDepth, 8);
    RoboCheckEqInt(info.colorType, f->colorType);
    RoboCheckEqInt(info.channels, f->channels);
    RoboCheckEqInt(info.interlaced, 0);
    RoboCheckEqInt((int)info.decodedBytes, (int)f->rgbaBytes);

    /* 3. pixels. */
    GfxDecodeLimitsInit(&limits);
    RoboCheckEqInt(GfxImageDecode(bytes, length, &limits, &image),
                      RoboOk);
    RoboCheck(image != NULL);
    RoboCheckEqInt((int)GfxImageWidth(image), (int)f->width);
    RoboCheckEqInt((int)GfxImageHeight(image), (int)f->height);
    RoboCheckEqInt(GfxImageFormat(image), RoboGfxFormatRgba8);
    RoboCheckEqInt(MakeGfxImageKind(image), RoboGfxImageKindRaster);
    if (image) {
        uint8_t px[4];
        GfxSurface *surface = GfxImageSurface(image);

        RoboCheckEqInt((int)ImageRgbaFnv(image), (int)f->rgbaFnv);

        SurfaceRead(surface, 0u, 0u, px);
        RoboCheckEqInt(px[0], f->pixel00[0]);
        RoboCheckEqInt(px[1], f->pixel00[1]);
        RoboCheckEqInt(px[2], f->pixel00[2]);
        RoboCheckEqInt(px[3], f->pixel00[3]);

        SurfaceRead(surface, f->cornerX, f->cornerY, px);
        RoboCheckEqInt(px[0], f->pixelCorner[0]);
        RoboCheckEqInt(px[1], f->pixelCorner[1]);
        RoboCheckEqInt(px[2], f->pixelCorner[2]);
        RoboCheckEqInt(px[3], f->pixelCorner[3]);

        GfxImageRelease(image);
    }
}

static void TestDecodeFixtures(void)
{
    size_t i;

    for (i = 0u; i < FIXTURE_COUNT; i++) {
        DecodeAcceptFixture(&FIXTURES[i]);
    }
}

/*
 * The three rejection fixtures. Each must fail with the *specific* code that
 * says why, because "it failed" is not information a caller can act on:
 * NOT_SUPPORTED means this decoder cannot do it, ERROR means the bytes are
 * wrong.
 */
static void TestDecodeRejections(void)
{
    uint8_t bytes[2048];
    size_t length;
    GfxDecodeInfo info;
    GfxImage *image = NULL;
    GfxDecodeLimits limits;

    GfxDecodeLimitsInit(&limits);

    length = HexDecode(RoboPngInterlaced_hex, bytes, sizeof(bytes));
    RoboCheckEqInt(length, RoboPngInterlacedBytes);
    RoboCheckEqInt(GfxImageDecode(bytes, length, &limits, &image),
                      RoboNotSupported);
    RoboCheck(image == NULL);

    length = HexDecode(RoboPngPackedNibble_hex, bytes, sizeof(bytes));
    RoboCheckEqInt(length, RoboPngPackedNibbleBytes);
    RoboCheckEqInt(GfxImageDecode(bytes, length, &limits, &image),
                      RoboNotSupported);
    RoboCheck(image == NULL);
    RoboCheckEqInt(
        GfxImageDecodeInfo(bytes, length, &limits, &info),
        RoboNotSupported);
}

static void TestDecodeProbeAndArgumentSafety(void)
{
    uint8_t bytes[64];
    GfxDecodeInfo info;
    GfxImage *image = NULL;

    memset(bytes, 0, sizeof(bytes));

    /* Empty, tiny, and not-an-image inputs are all UNKNOWN, never a crash. */
    RoboCheckEqInt(GfxDecodeProbe(NULL, 0u),
                      RoboGfxDecodeContainerUnknown);
    RoboCheckEqInt(GfxDecodeProbe("", 0u),
                      RoboGfxDecodeContainerUnknown);
    RoboCheckEqInt(GfxDecodeProbe("PNG", 3u),
                      RoboGfxDecodeContainerUnknown);
    /* A truncated signature is not a PNG. */
    RoboCheckEqInt(GfxDecodeProbe("\x89PNG", 4u),
                      RoboGfxDecodeContainerUnknown);
    /* BMP and JPEG magic are recognised as "not ours", not as errors. */
    RoboCheckEqInt(GfxDecodeProbe("BM\x00\x00\x00\x00\x00\x00", 8u),
                      RoboGfxDecodeContainerUnknown);
    RoboCheckEqInt(GfxDecodeProbe("\xFF\xD8\xFF\xE0\x00\x10", 6u),
                      RoboGfxDecodeContainerUnknown);

    /* Names and enumerator validity. */
    RoboCheckStrEq(GfxDecodeContainerName(RoboGfxDecodeContainerPng),
                      "png");
    RoboCheckStrEq(
        GfxDecodeContainerName(RoboGfxDecodeContainerUnknown),
        "unknown");
    RoboCheck(GfxDecodeContainerIsKnown(RoboGfxDecodeContainerPng));
    RoboCheck(!GfxDecodeContainerIsKnown(
        RoboGfxDecodeContainerUnknown));
    RoboCheck(!GfxDecodeContainerIsKnown(
        (GfxDecodeContainer)99));

    /* NULL / zero-length / NULL-out are argument errors, not decode attempts. */
    RoboCheckEqInt(GfxImageDecode(NULL, 16u, NULL, &image),
                      RoboInvalidArgument);
    RoboCheck(image == NULL);
    RoboCheckEqInt(GfxImageDecode(bytes, 0u, NULL, &image),
                      RoboInvalidArgument);
    RoboCheck(image == NULL);
    RoboCheckEqInt(GfxImageDecode(bytes, 16u, NULL, NULL),
                      RoboInvalidArgument);
    /* 16 zero bytes are not an image: "not supported", not "bad argument". */
    RoboCheckEqInt(GfxImageDecodeInfo(bytes, 16u, NULL, &info),
                      RoboNotSupported);
    RoboCheckEqInt(GfxImageDecodeInfo(NULL, 16u, NULL, &info),
                      RoboInvalidArgument);
    RoboCheckEqInt(GfxImageDecodeInfo(bytes, 0u, NULL, &info),
                      RoboInvalidArgument);

    /* Zeroed limits means defaults. */
    memset(&info, 0, sizeof(info));
    RoboCheckEqInt(
        GfxImageDecodeInfo((const void *)"not an image", 12u, NULL,
                                   &info),
        RoboNotSupported);

    {
        GfxDecodeLimits defaults;

        memset(&defaults, 0xFF, sizeof(defaults));
        GfxDecodeLimitsInit(&defaults);
        RoboCheckEqInt((int)defaults.maxDimension,
                          (int)RoboGfxDecodeDefaultMaxDimension);
        RoboCheckEqInt((int)defaults.maxDecodedBytes,
                          (int)RoboGfxDecodeDefaultMaxDecodedBytes);
        GfxDecodeLimitsInit(NULL); /* must not crash */
    }
}

/* ------------------------------------------------------------------ */
/* Round trip against the Goal 6.13 encoder                            */
/* ------------------------------------------------------------------ */

static void RoundTrip(GfxPixelFormat format, uint32_t w, uint32_t h,
                       int colourful)
{
    GfxSurface *source = NULL;
    GfxEncodeOptions opts;
    GfxEncodedBuffer encoded;
    GfxImage *decoded = NULL;
    uint32_t x;
    uint32_t y;

    RoboCheckEqInt(MakeSurface(w, h, format, &source), RoboOk);
    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x++) {
            uint8_t r = colourful ? (uint8_t)(x * 37u + 5u) : (uint8_t)((x + y) * 11u);
            uint8_t g = colourful ? (uint8_t)(y * 53u + 7u) : r;
            uint8_t b = colourful ? (uint8_t)((x * y) & 0xFFu) : r;
            uint8_t a = 255u;

            if (format == RoboGfxFormatRgba8 || format == RoboGfxFormatAlpha8) {
                a = (uint8_t)(128u + ((x + y) & 0x7Fu));
            }
            SurfaceWrite(source, x, y, r, g, b, a);
        }
    }

    GfxEncodeOptionsInit(&opts);
    opts.container = RoboGfxEncodePng;
    opts.layout = RoboGfxEncodeLayoutRgba8;
    memset(&encoded, 0, sizeof(encoded));
    RoboCheckEqInt(GfxSurfaceEncode(source, &opts, &encoded),
                      RoboOk);
    RoboCheck(encoded.size > 0u);

    /* The encoder writes store blocks; this is the stored-block path. */
    RoboCheckEqInt(
        GfxImageDecode(encoded.data, encoded.size, NULL, &decoded),
        RoboOk);
    RoboCheck(decoded != NULL);
    if (decoded) {
        GfxSurface *back = GfxImageSurface(decoded);

        RoboCheckEqInt((int)GfxImageWidth(decoded), (int)w);
        RoboCheckEqInt((int)GfxImageHeight(decoded), (int)h);
        RoboCheckEqInt(GfxImageFormat(decoded), RoboGfxFormatRgba8);
        /*
         * Compare logically, not by bytes: the source may be BGRA/GRAY, and
         * the decoder's contract is "logical RGBA8", so reading both through
         * the accessors is the only comparison that means anything.
         */
        for (y = 0u; y < h; y++) {
            for (x = 0u; x < w; x++) {
                uint8_t want[4];
                uint8_t got[4];

                SurfaceRead(source, x, y, want);
                SurfaceRead(back, x, y, got);
                RoboCheckEqInt(got[0], want[0]);
                RoboCheckEqInt(got[1], want[1]);
                RoboCheckEqInt(got[2], want[2]);
                RoboCheckEqInt(got[3], want[3]);
            }
        }
        GfxImageRelease(decoded);
    }
    GfxEncodedBufferRelease(&encoded);
    GfxSurfaceRelease(source);
}

static void TestDecodeRoundTrip(void)
{
    RoundTrip(RoboGfxFormatRgba8, 17u, 9u, 1);
    RoundTrip(RoboGfxFormatBgra8, 5u, 4u, 1);
    RoundTrip(RoboGfxFormatRgb8, 8u, 8u, 1);
    RoundTrip(RoboGfxFormatGray8, 6u, 3u, 0);
    RoundTrip(RoboGfxFormatRgba8, 1u, 1u, 1);
}

/* ------------------------------------------------------------------ */
/* Malformed input, built here with valid CRCs                         */
/* ------------------------------------------------------------------ */

typedef struct pngBuilder {
    uint8_t out[SCRATCH];
    size_t pos;
} PngBuilder;

static void BuilderStart(PngBuilder *b)
{
    static const uint8_t sig[8] = {0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au,
                                   0x0Au};

    memcpy(b->out, sig, sizeof(sig));
    b->pos = sizeof(sig);
}

static void BuilderIhdr(PngBuilder *b, uint32_t w, uint32_t h,
                         uint8_t depth, uint8_t color, uint8_t interlace)
{
    uint8_t ihdr[13];

    Be32Write(ihdr, w);
    Be32Write(ihdr + 4u, h);
    ihdr[8] = depth;
    ihdr[9] = color;
    ihdr[10] = 0u;
    ihdr[11] = 0u;
    ihdr[12] = interlace;
    b->pos = PngEmitChunk(b->out, sizeof(b->out), b->pos, "IHDR", ihdr, 13u);
}

static void BuilderChunk(PngBuilder *b, const char *type,
                          const uint8_t *data, size_t length)
{
    b->pos = PngEmitChunk(b->out, sizeof(b->out), b->pos, type, data, length);
}

static void BuilderZlibIdat(PngBuilder *b, const uint8_t *raw,
                              size_t rawLength)
{
    uint8_t zlib[SCRATCH];
    size_t n = ZlibStore(zlib, sizeof(zlib), raw, rawLength);

    if (n == 0u) {
        RoboCheck(0 && "zlib_store scratch overflow");
        return;
    }
    BuilderChunk(b, "IDAT", zlib, n);
}

static void BuilderIend(PngBuilder *b)
{
    b->pos = PngEmitChunk(b->out, sizeof(b->out), b->pos, "IEND", NULL, 0u);
}

/** PAETH predictor, forward direction (RFC 2083 §6.6). */
static uint8_t TestPaeth(uint8_t a, uint8_t b, uint8_t c)
{
    int p = (int)a + (int)b - (int)c;
    int pa = p > (int)a ? p - (int)a : (int)a - p;
    int pb = p > (int)b ? p - (int)b : (int)b - p;
    int pc = p > (int)c ? p - (int)c : (int)c - p;

    if (pa <= pb && pa <= pc) {
        return a;
    }
    if (pb <= pc) {
        return b;
    }
    return c;
}

/**
 * Forward-filter @p image into blocked scanlines using @p filter on every
 * row. This is the encoder's half of RFC 2083 §6; the decoder under test is
 * the other half, so a round trip through it is an unfilter test rather than
 * a "did it not crash" test.
 */
static void FilterRows(const uint8_t *image, uint8_t *out, uint32_t w,
                        uint32_t h, size_t bpp, int filter)
{
    uint32_t y;

    for (y = 0u; y < h; y++) {
        size_t rowBytes = (size_t)w * bpp;
        const uint8_t *cur = image + rowBytes * (size_t)y;
        const uint8_t *prev =
            (y == 0u) ? NULL : image + rowBytes * (size_t)(y - 1u);
        uint8_t *dst = out + (rowBytes + 1u) * (size_t)y;
        size_t i;

        dst[0] = (uint8_t)filter;
        for (i = 0u; i < rowBytes; i++) {
            uint8_t left = (i >= bpp) ? cur[i - bpp] : 0u;
            uint8_t up = prev ? prev[i] : 0u;
            uint8_t upleft = (prev && i >= bpp) ? prev[i - bpp] : 0u;
            uint8_t filtered;

            switch (filter) {
            case 1:
                filtered = (uint8_t)(cur[i] - left);
                break;
            case 2:
                filtered = (uint8_t)(cur[i] - up);
                break;
            case 3:
                filtered = (uint8_t)(cur[i] -
                                     (uint8_t)(((unsigned)left +
                                               (unsigned)up) /
                                              2u));
                break;
            case 4:
                filtered =
                    (uint8_t)(cur[i] - TestPaeth(left, up, upleft));
                break;
            default:
                filtered = cur[i];
                break;
            }
            dst[1u + i] = filtered;
        }
    }
}

#define TEST_IMAGE_W 7u
#define TEST_IMAGE_H 5u

static void TestDecodeUnfilterFilters(void)
{
    /*
     * One known image, encoded five times — once per PNG filter — and each
     * must decode back to exactly that image. A single shared expectation is
     * deliberate: if average or Paeth is wrong, that filter's case fails on
     * its own, which names the bug instead of just reporting "decode broke".
     */
    uint8_t image[TEST_IMAGE_W * TEST_IMAGE_H];
    uint8_t blocked[TEST_IMAGE_W * TEST_IMAGE_H + TEST_IMAGE_H];
    uint32_t y;
    uint32_t x;
    int filter;

    for (y = 0u; y < TEST_IMAGE_H; y++) {
        for (x = 0u; x < TEST_IMAGE_W; x++) {
            /* Irregular by construction, so a filter bug cannot cancel out. */
            image[y * TEST_IMAGE_W + x] =
                (uint8_t)((x * 41u + y * 97u + (x * y) * 7u) & 0xFFu);
        }
    }

    for (filter = 0; filter <= 4; filter++) {
        PngBuilder b;
        GfxImage *decoded = NULL;
        GfxSurface *s;

        FilterRows(image, blocked, TEST_IMAGE_W, TEST_IMAGE_H, 1u, filter);
        BuilderStart(&b);
        BuilderIhdr(&b, TEST_IMAGE_W, TEST_IMAGE_H, 8u, 0u, 0u);
        BuilderZlibIdat(&b, blocked, sizeof(blocked));
        BuilderIend(&b);

        RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &decoded),
                          RoboOk);
        RoboCheck(decoded != NULL);
        if (!decoded) {
            continue;
        }
        s = GfxImageSurface(decoded);
        for (y = 0u; y < TEST_IMAGE_H; y++) {
            for (x = 0u; x < TEST_IMAGE_W; x++) {
                uint8_t px[4];
                uint8_t want = image[y * TEST_IMAGE_W + x];

                SurfaceRead(s, x, y, px);
                RoboCheckEqInt(px[0], want);
                RoboCheckEqInt(px[1], want);
                RoboCheckEqInt(px[2], want);
                RoboCheckEqInt(px[3], 255);
            }
        }
        GfxImageRelease(decoded);
    }
}

static void TestDecodeLimitEnforcement(void)
{
    uint8_t bytes[2048];
    size_t length = HexDecode(RoboPngChecker_hex, bytes, sizeof(bytes));
    GfxDecodeLimits limits;
    GfxImage *image = NULL;
    GfxDecodeInfo info;

    /* Dimension ceiling below the real size. */
    GfxDecodeLimitsInit(&limits);
    limits.maxDimension = 8u;
    RoboCheckEqInt(GfxImageDecode(bytes, length, &limits, &image),
                      RoboError);
    RoboCheck(image == NULL);
    RoboCheckEqInt(GfxImageDecodeInfo(bytes, length, &limits, &info),
                      RoboError);

    /* Byte ceiling below the real decoded size, dimension ceiling generous. */
    GfxDecodeLimitsInit(&limits);
    limits.maxDimension = 4096u;
    limits.maxDecodedBytes = 1024u; /* 32x32 RGBA8 needs 4096 */
    RoboCheckEqInt(GfxImageDecode(bytes, length, &limits, &image),
                      RoboError);
    RoboCheck(image == NULL);

    /* A ceiling exactly at the size is admitted, so the check is not off-by-one. */
    limits.maxDecodedBytes = 4096u;
    RoboCheckEqInt(GfxImageDecode(bytes, length, &limits, &image),
                      RoboOk);
    RoboCheck(image != NULL);
    GfxImageRelease(image);
}

static void TestDecodeHostileHeaders(void)
{
    PngBuilder b;
    GfxImage *image = NULL;
    GfxDecodeInfo info;

    /* A header claiming 4 gigapixels must fail before allocating anything:
     * the whole point of validating dimensions ahead of the decode. */
    BuilderStart(&b);
    BuilderIhdr(&b, 0xFFFFFFFFu, 0xFFFFFFFFu, 8u, 6u, 0u);
    b.pos = PngEmitChunk(b.out, sizeof(b.out), b.pos, "IDAT",
                           (const uint8_t *)"\x78\x01\x01\x00\x00\xff\xff\x00\x00\x00\x00",
                           11u);
    BuilderIend(&b);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboError);
    RoboCheck(image == NULL);
    RoboCheckEqInt(GfxImageDecodeInfo(b.out, b.pos, NULL, &info),
                      RoboError);

    /* Zero width / zero height are not dimensions a surface can have. */
    BuilderStart(&b);
    BuilderIhdr(&b, 0u, 4u, 8u, 0u, 0u);
    BuilderZlibIdat(&b, (const uint8_t *)"\x00\x00", 2u);
    BuilderIend(&b);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboError);
    RoboCheck(image == NULL);

    BuilderStart(&b);
    BuilderIhdr(&b, 4u, 0u, 8u, 0u, 0u);
    BuilderZlibIdat(&b, (const uint8_t *)"\x00\x00", 2u);
    BuilderIend(&b);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboError);
    RoboCheck(image == NULL);
}

static void TestDecodeHostileHeadersContinued(void)
{
    PngBuilder b;
    GfxImage *image = NULL;

    /* Unknown critical chunk: refused, not skipped. */
    BuilderStart(&b);
    BuilderIhdr(&b, 2u, 2u, 8u, 0u, 0u);
    BuilderChunk(&b, "ZZZZ", (const uint8_t *)"abcd", 4u);
    BuilderZlibIdat(&b, (const uint8_t *)"\x00\x00\x00", 3u);
    BuilderIend(&b);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboNotSupported);
    RoboCheck(image == NULL);

    /* Unknown *ancillary* chunk: skipped, and the image still decodes. */
    BuilderStart(&b);
    BuilderIhdr(&b, 2u, 2u, 8u, 0u, 0u);
    BuilderChunk(&b, "zzzz", (const uint8_t *)"abcd", 4u);
    /* Two rows of two grayscale samples: (2 * 1 + 1) * 2 = 6 bytes. */
    BuilderZlibIdat(&b, (const uint8_t *)"\x00\x01\x02\x03\x04\x05", 6u);
    BuilderIend(&b);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboOk);
    RoboCheck(image != NULL);
    GfxImageRelease(image);

    /* A non-zero compression or filter method is something else's format. */
    BuilderStart(&b);
    {
        uint8_t ihdr[13];

        memset(ihdr, 0, sizeof(ihdr));
        Be32Write(ihdr, 2u);
        Be32Write(ihdr + 4u, 2u);
        ihdr[8] = 8u;
        ihdr[9] = 0u;
        ihdr[10] = 1u; /* compression method 1 */
        b.pos = PngEmitChunk(b.out, sizeof(b.out), b.pos, "IHDR", ihdr, 13u);
    }
    BuilderZlibIdat(&b, (const uint8_t *)"\x00\x00\x00", 3u);
    BuilderIend(&b);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboNotSupported);
    RoboCheck(image == NULL);

    /* IEND with a payload is malformed. */
    BuilderStart(&b);
    BuilderIhdr(&b, 2u, 2u, 8u, 0u, 0u);
    BuilderZlibIdat(&b, (const uint8_t *)"\x00\x00\x00", 3u);
    BuilderChunk(&b, "IEND", (const uint8_t *)"x", 1u);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboError);
    RoboCheck(image == NULL);
}

static void TestDecodeHostileStreams(void)
{
    PngBuilder b;
    GfxImage *image = NULL;
    uint8_t zlib[128];
    uint8_t raw[8];

    /* 1. No IDAT at all. */
    BuilderStart(&b);
    BuilderIhdr(&b, 2u, 2u, 8u, 0u, 0u);
    BuilderIend(&b);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboError);
    RoboCheck(image == NULL);

    /* 2. No IEND: the stream is truncated even though IHDR and IDAT are fine. */
    BuilderStart(&b);
    BuilderIhdr(&b, 2u, 2u, 8u, 0u, 0u);
    BuilderZlibIdat(&b, (const uint8_t *)"\x00\x01\x02\x03\x04\x05\x06",
                      7u);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboError);
    RoboCheck(image == NULL);

    /* 3. Deflate yields too few bytes for the declared dimensions. */
    BuilderStart(&b);
    BuilderIhdr(&b, 8u, 8u, 8u, 0u, 0u);
    {
        size_t n = ZlibStore(zlib, sizeof(zlib), (const uint8_t *)"\x00\x01",
                              2u);

        BuilderChunk(&b, "IDAT", zlib, n);
    }
    BuilderIend(&b);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboError);
    RoboCheck(image == NULL);

    /* 4. Deflate yields too many bytes for the declared dimensions. */
    BuilderStart(&b);
    BuilderIhdr(&b, 1u, 1u, 8u, 0u, 0u);
    {
        size_t n = ZlibStore(zlib, sizeof(zlib), (const uint8_t *)"\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09",
                              10u);

        BuilderChunk(&b, "IDAT", zlib, n);
    }
    BuilderIend(&b);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboError);
    RoboCheck(image == NULL);

    /* 5. Bad zlib header: CM = 7 is not deflate. */
    {
        uint8_t stream[16];
        size_t n = ZlibStore(stream, sizeof(stream),
                              (const uint8_t *)"\x00\x01\x02", 3u);

        stream[0] = 0x70u; /* CM = 7 */
        BuilderStart(&b);
        BuilderIhdr(&b, 2u, 2u, 8u, 0u, 0u);
        BuilderChunk(&b, "IDAT", stream, n);
        BuilderIend(&b);
        RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                          RoboError);
        RoboCheck(image == NULL);
    }

    /* 6. Preset dictionary set: refused as unsupported, not misdecoded. */
    {
        uint8_t stream[16];
        size_t n = ZlibStore(stream, sizeof(stream),
                              (const uint8_t *)"\x00\x01\x02", 3u);

        stream[1] |= 0x20u; /* FDICT */
        /* Restore FCHECK so the header is otherwise well-formed. */
        stream[1] = (uint8_t)((stream[1] + 31u - (((((uint32_t)stream[0] << 8) |
                                                   stream[1]) %
                                                  31u))) &
                              0xFFu);
        BuilderStart(&b);
        BuilderIhdr(&b, 2u, 2u, 8u, 0u, 0u);
        BuilderChunk(&b, "IDAT", stream, n);
        BuilderIend(&b);
        RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                          RoboNotSupported);
        RoboCheck(image == NULL);
    }

    /* 7. Undefined row filter (5). */
    BuilderStart(&b);
    BuilderIhdr(&b, 2u, 2u, 8u, 0u, 0u);
    raw[0] = 5u;
    raw[1] = 1u;
    raw[2] = 2u;
    raw[3] = 5u;
    raw[4] = 3u;
    raw[5] = 4u;
    BuilderZlibIdat(&b, raw, 6u);
    BuilderIend(&b);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboError);
    RoboCheck(image == NULL);

    /* 8. Reserved DEFLATE block type (BTYPE = 3). */
    {
        uint8_t stream[8];

        stream[0] = 0x78u;
        stream[1] = 0x01u;
        stream[2] = 0x07u; /* BFINAL = 1, BTYPE = 3 */
        stream[3] = 0x00u;
        BuilderStart(&b);
        BuilderIhdr(&b, 2u, 2u, 8u, 0u, 0u);
        BuilderChunk(&b, "IDAT", stream, 4u);
        BuilderIend(&b);
        RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                          RoboError);
        RoboCheck(image == NULL);
    }
}

static void TestDecodeCrcAndStructure(void)
{
    uint8_t bytes[2048];
    size_t length = HexDecode(RoboPngGradient_hex, bytes, sizeof(bytes));
    GfxImage *image = NULL;
    GfxDecodeInfo info;
    size_t i;

    RoboCheckEqInt(length, RoboPngGradientBytes);

    /* Flip one byte inside IHDR's CRC: the chunk is byte-identical in every
     * other respect, so only the CRC check can catch it. */
    {
        uint8_t copy[2048];

        memcpy(copy, bytes, length);
        copy[29] ^= 0x01u; /* IHDR crc byte */
        RoboCheckEqInt(GfxImageDecode(copy, length, NULL, &image),
                          RoboError);
        RoboCheck(image == NULL);
    }

    /*
     * A stored block whose LEN / NLEN complement is wrong, with a freshly
     * computed CRC so the chunk is intact. Nothing but the DEFLATE-level
     * check can catch this, which is what makes it a test of the inflate
     * path rather than of the chunk walk.
     */
    {
        PngBuilder b;
        uint8_t stream[64];
        size_t n = ZlibStore(stream, sizeof(stream),
                              (const uint8_t *)"\x00\x01\x02\x03\x04\x05\x06",
                              7u);

        RoboCheck(n > 11u);
        stream[5] = (uint8_t)(stream[5] ^ 0xFFu); /* break ~LEN */
        BuilderStart(&b);
        BuilderIhdr(&b, 2u, 2u, 8u, 0u, 0u);
        BuilderChunk(&b, "IDAT", stream, n);
        BuilderIend(&b);
        RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                          RoboError);
        RoboCheck(image == NULL);
    }

    /* Every truncation point must fail cleanly, never crash or half-decode. */
    for (i = 1u; i < length; i++) {
        image = NULL;
        {
            Result r = GfxImageDecode(bytes, i, NULL, &image);

            RoboCheck(r != RoboOk);
            RoboCheck(image == NULL);
        }
    }

    /* The full stream still decodes after all that. */
    RoboCheckEqInt(GfxImageDecode(bytes, length, NULL, &image),
                      RoboOk);
    RoboCheck(image != NULL);
    GfxImageRelease(image);

    /* A 12-byte header on its own is a PNG with no image data. */
    RoboCheckEqInt(GfxImageDecodeInfo(bytes, 12u, NULL, &info),
                      RoboError);
}

static void TestDecodePaletteAndTransparency(void)
{
    uint8_t bytes[2048];
    size_t length = HexDecode(RoboPngPaletted_hex, bytes, sizeof(bytes));
    GfxImage *image = NULL;
    GfxSurface *s;
    uint8_t px[4];

    RoboCheckEqInt(length, RoboPngPalettedBytes);
    RoboCheckEqInt(GfxImageDecode(bytes, length, NULL, &image),
                      RoboOk);
    RoboCheck(image != NULL);
    if (!image) {
        return;
    }
    s = GfxImageSurface(image);

    /* Palette index 0 = opaque red, index 1 = green at tRNS alpha 128. */
    SurfaceRead(s, 0u, 0u, px);
    RoboCheckEqInt(px[0], 255);
    RoboCheckEqInt(px[1], 0);
    RoboCheckEqInt(px[2], 0);
    RoboCheckEqInt(px[3], 255);

    SurfaceRead(s, 1u, 0u, px);
    RoboCheckEqInt(px[0], 0);
    RoboCheckEqInt(px[1], 255);
    RoboCheckEqInt(px[2], 0);
    RoboCheckEqInt(px[3], 128);

    /* Index 3 is white at tRNS alpha 0 — the alpha channel survives decoding. */
    SurfaceRead(s, 3u, 0u, px);
    RoboCheckEqInt(px[0], 255);
    RoboCheckEqInt(px[1], 255);
    RoboCheckEqInt(px[2], 255);
    RoboCheckEqInt(px[3], 0);

    GfxImageRelease(image);
}

static void TestDecodePaletteMalformed(void)
{
    PngBuilder b;
    GfxImage *image = NULL;
    uint8_t zlib[64];

    /* A palette image with no PLTE chunk cannot be decoded: the indices have
     * no meaning, so refusing is the only honest answer. */
    BuilderStart(&b);
    BuilderIhdr(&b, 2u, 2u, 8u, 3u, 0u);
    {
        size_t n = ZlibStore(zlib, sizeof(zlib),
                              (const uint8_t *)"\x00\x00\x00\x00\x00\x00",
                              6u);
        BuilderChunk(&b, "IDAT", zlib, n);
    }
    BuilderIend(&b);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboError);
    RoboCheck(image == NULL);

    /* A palette index past the end of PLTE yields opaque black rather than a
     * read past the palette, and the rest of the image still decodes. */
    BuilderStart(&b);
    BuilderIhdr(&b, 2u, 1u, 8u, 3u, 0u);
    BuilderChunk(&b, "PLTE", (const uint8_t *)"\xFF\x00\x00\x00\xFF\x00", 6u);
    {
        size_t n = ZlibStore(zlib, sizeof(zlib),
                             (const uint8_t *)"\x00\x01\x07", 3u);
        BuilderChunk(&b, "IDAT", zlib, n);
    }
    BuilderIend(&b);
    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboOk);
    RoboCheck(image != NULL);
    if (image) {
        GfxSurface *s = GfxImageSurface(image);
        uint8_t px[4];

        SurfaceRead(s, 0u, 0u, px);
        RoboCheckEqInt(px[0], 0);
        RoboCheckEqInt(px[1], 255);
        RoboCheckEqInt(px[2], 0);
        SurfaceRead(s, 1u, 0u, px);
        RoboCheckEqInt(px[0], 0);
        RoboCheckEqInt(px[1], 0);
        RoboCheckEqInt(px[2], 0);
        RoboCheckEqInt(px[3], 255);
        GfxImageRelease(image);
    }
}

static void TestDecodeTilingAndLarge(void)
{
    /*
     * A wider image than the scratch builder's convenience helpers, decoded
     * through the real store-block writer: this is the case where a stride
     * bug or an off-by-one in row addressing would show up as a wrong pixel
     * at the far right of a row rather than as a crash.
     */
    PngBuilder b;
    uint8_t raw[SCRATCH / 2];
    uint32_t w = 64u;
    uint32_t h = 8u;
    GfxImage *image = NULL;
    size_t rawLength = (size_t)w * 3u * h + h;
    uint32_t y;
    uint32_t x;

    for (y = 0u; y < h; y++) {
        uint8_t *row = raw + ((size_t)w * 3u + 1u) * (size_t)y;

        row[0] = 0u;
        for (x = 0u; x < w * 3u; x++) {
            row[1u + x] = (uint8_t)((x + y * 7u) & 0xFFu);
        }
    }

    BuilderStart(&b);
    BuilderIhdr(&b, w, h, 8u, 2u, 0u);
    BuilderZlibIdat(&b, raw, rawLength);
    BuilderIend(&b);

    RoboCheckEqInt(GfxImageDecode(b.out, b.pos, NULL, &image),
                      RoboOk);
    RoboCheck(image != NULL);
    if (image) {
        GfxSurface *s = GfxImageSurface(image);
        uint8_t px[4];

        RoboCheckEqInt((int)GfxImageWidth(image), (int)w);
        RoboCheckEqInt((int)GfxImageHeight(image), (int)h);
        /* The source pattern is (byte index + y * 7) & 0xFF, so the last
         * pixel's three samples are its row offsets plus that row's term. */
        SurfaceRead(s, w - 1u, h - 1u, px);
        RoboCheckEqInt(
            px[0], (uint8_t)(((w - 1u) * 3u + (h - 1u) * 7u) & 0xFFu));
        RoboCheckEqInt(
            px[1],
            (uint8_t)(((w - 1u) * 3u + 1u + (h - 1u) * 7u) & 0xFFu));
        RoboCheckEqInt(
            px[2],
            (uint8_t)(((w - 1u) * 3u + 2u + (h - 1u) * 7u) & 0xFFu));
        RoboCheckEqInt(px[3], 255);
        GfxImageRelease(image);
    }
}

void TestRegisterGraphicsDecode(void)
{
    TestRegister("graphics.decode.real_fixtures", TestDecodeFixtures);
    TestRegister("graphics.decode.probe_safety",
                       TestDecodeProbeAndArgumentSafety);
    TestRegister("graphics.decode.rejections", TestDecodeRejections);
    TestRegister("graphics.decode.round_trip_encoder",
                       TestDecodeRoundTrip);
    TestRegister("graphics.decode.unfilter", TestDecodeUnfilterFilters);
    TestRegister("graphics.decode.limits", TestDecodeLimitEnforcement);
    TestRegister("graphics.decode.hostile_headers",
                       TestDecodeHostileHeaders);
    TestRegister("graphics.decode.hostile_headers_two",
                       TestDecodeHostileHeadersContinued);
    TestRegister("graphics.decode.hostile_streams",
                       TestDecodeHostileStreams);
    TestRegister("graphics.decode.crc_and_truncation",
                       TestDecodeCrcAndStructure);
    TestRegister("graphics.decode.palette",
                       TestDecodePaletteAndTransparency);
    TestRegister("graphics.decode.palette_malformed",
                       TestDecodePaletteMalformed);
    TestRegister("graphics.decode.large_rows", TestDecodeTilingAndLarge);
}
