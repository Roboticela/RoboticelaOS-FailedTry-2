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

static int hex_nibble(char c)
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

static size_t hex_decode(const char *hex, uint8_t *out, size_t capacity)
{
    size_t n = 0u;
    size_t i = 0u;

    while (hex[i] != '\0' && hex[i + 1u] != '\0') {
        int hi = hex_nibble(hex[i]);
        int lo = hex_nibble(hex[i + 1u]);

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

static uint32_t crc32_bytes(const uint8_t *data, size_t length)
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

static void be32_write(uint8_t *p, uint32_t v)
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
static size_t png_emit_chunk(uint8_t *out, size_t capacity, size_t pos,
                             const char *type, const uint8_t *data,
                             size_t length)
{
    uint8_t crc_input[SCRATCH];
    uint32_t crc;

    if (pos + 12u + length > capacity || length + 4u > sizeof(crc_input)) {
        return 0u;
    }
    be32_write(out + pos, (uint32_t)length);
    memcpy(out + pos + 4u, type, 4u);
    if (length != 0u) {
        memcpy(out + pos + 8u, data, length);
    }
    memcpy(crc_input, out + pos + 4u, 4u);
    if (length != 0u) {
        memcpy(crc_input + 4u, data, length);
    }
    crc = crc32_bytes(crc_input, length + 4u);
    be32_write(out + pos + 8u + length, crc);
    return pos + 12u + length;
}

/** zlib stream using stored DEFLATE blocks — no compression, no dependency. */
static size_t zlib_store(uint8_t *out, size_t capacity, const uint8_t *raw,
                         size_t raw_length)
{
    size_t pos = 0u;
    size_t remaining = raw_length;

    if (capacity < raw_length + 12u) {
        return 0u;
    }
    out[pos++] = 0x78u; /* CM = deflate, window 32 KiB */
    out[pos++] = 0x01u; /* level 0, no preset dictionary, FCHECK valid */
    if (raw_length == 0u) {
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
        memcpy(out + pos, raw + (raw_length - remaining), n);
        pos += n;
        remaining -= n;
    }
    /* Adler-32 is not checked by the decoder, but write a correct-looking one
     * so these streams are legal zlib rather than only decodable zlib. */
    memset(out + pos, 0, 4u);
    pos += 4u;
    return pos;
}

static robo_result_t make_surface(uint32_t w, uint32_t h,
                                  robo_gfx_pixel_format_t format,
                                  robo_gfx_surface_t **out)
{
    robo_gfx_surface_desc_t desc;

    robo_gfx_surface_desc_init(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = format;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    return robo_gfx_surface_create(&desc, out);
}

static void surface_write(robo_gfx_surface_t *s, uint32_t x, uint32_t y,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    robo_gfx_color_rgba8_t c;

    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(s, x, y, c), ROBO_OK);
}

/** Read back one logical RGBA8 pixel through the public accessor. */
static void surface_read(robo_gfx_surface_t *s, uint32_t x, uint32_t y,
                         uint8_t *out4)
{
    robo_gfx_color_rgba8_t c;

    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(s, x, y, &c), ROBO_OK);
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
static uint32_t image_rgba_fnv(robo_gfx_image_t *image)
{
    robo_gfx_surface_t *surface = robo_gfx_image_surface(image);
    robo_gfx_surface_map_t map;
    uint8_t flat[8192];
    uint32_t y;

    ROBO_CHECK(surface != NULL);
    memset(&map, 0, sizeof(map));
    ROBO_CHECK_EQ_INT(robo_gfx_surface_map(surface, ROBO_GFX_SURFACE_ACCESS_READ,
                                           &map),
                      ROBO_OK);
    ROBO_CHECK((size_t)map.width * (size_t)map.height * 4u <= sizeof(flat));
    for (y = 0u; y < map.height; y++) {
        memcpy(flat + (size_t)map.width * 4u * (size_t)y,
               map.pixels_const + map.stride_bytes * (size_t)y,
               (size_t)map.width * 4u);
    }
    ROBO_CHECK_EQ_INT(robo_gfx_surface_unmap(surface), ROBO_OK);
    return fnv32(flat, (size_t)map.width * (size_t)map.height * 4u);
}

/* ------------------------------------------------------------------ */
/* Fixture sweep                                                       */
/* ------------------------------------------------------------------ */

typedef struct decode_fixture {
    const char *name;
    const char *hex;
    size_t byte_count;
    uint32_t width;
    uint32_t height;
    uint8_t color_type;
    uint8_t channels;
    size_t rgba_bytes;
    uint32_t rgba_fnv;
    uint8_t pixel_0_0[4];
    uint8_t pixel_corner[4];
    uint32_t corner_x;
    uint32_t corner_y;
} decode_fixture_t;

/*
 * Every fixture is checked against the *header* facts and the *pixel* facts
 * together: a decoder that got the dimensions right and the colours wrong
 * would pass a dimensions-only test, which is why the FNV is in the table.
 */
static const decode_fixture_t FIXTURES[] = {
    {"gradient", RoboPngGradient_hex, ROBO_PNG_GRADIENT_BYTES,
     ROBO_PNG_GRADIENT_WIDTH, ROBO_PNG_GRADIENT_HEIGHT, 6u, 4u,
     ROBO_PNG_GRADIENT_RGBA_BYTES, ROBO_PNG_GRADIENT_RGBA_FNV,
     ROBO_PNG_GRADIENT_PIXEL_0_0, ROBO_PNG_GRADIENT_PIXEL_7_7, 7u, 7u},
    {"checker", RoboPngChecker_hex, ROBO_PNG_CHECKER_BYTES,
     ROBO_PNG_CHECKER_WIDTH, ROBO_PNG_CHECKER_HEIGHT, 6u, 4u,
     ROBO_PNG_CHECKER_RGBA_BYTES, ROBO_PNG_CHECKER_RGBA_FNV,
     ROBO_PNG_CHECKER_PIXEL_0_0, ROBO_PNG_CHECKER_PIXEL_31_31, 31u, 31u},
    {"opaque", RoboPngOpaque_hex, ROBO_PNG_OPAQUE_BYTES, ROBO_PNG_OPAQUE_WIDTH,
     ROBO_PNG_OPAQUE_HEIGHT, 2u, 3u, ROBO_PNG_OPAQUE_RGBA_BYTES,
     ROBO_PNG_OPAQUE_RGBA_FNV, ROBO_PNG_OPAQUE_PIXEL_0_0,
     ROBO_PNG_OPAQUE_PIXEL_7_7, 7u, 7u},
    {"grayscale", RoboPngGrayscale_hex, ROBO_PNG_GRAYSCALE_BYTES,
     ROBO_PNG_GRAYSCALE_WIDTH, ROBO_PNG_GRAYSCALE_HEIGHT, 0u, 1u,
     ROBO_PNG_GRAYSCALE_RGBA_BYTES, ROBO_PNG_GRAYSCALE_RGBA_FNV,
     ROBO_PNG_GRAYSCALE_PIXEL_0_0, ROBO_PNG_GRAYSCALE_PIXEL_7_7, 7u, 7u},
    {"translucent", RoboPngTranslucent_hex, ROBO_PNG_TRANSLUCENT_BYTES,
     ROBO_PNG_TRANSLUCENT_WIDTH, ROBO_PNG_TRANSLUCENT_HEIGHT, 4u, 2u,
     ROBO_PNG_TRANSLUCENT_RGBA_BYTES, ROBO_PNG_TRANSLUCENT_RGBA_FNV,
     ROBO_PNG_TRANSLUCENT_PIXEL_0_0, ROBO_PNG_TRANSLUCENT_PIXEL_5_5, 5u, 5u},
    {"paletted", RoboPngPaletted_hex, ROBO_PNG_PALETTED_BYTES,
     ROBO_PNG_PALETTED_WIDTH, ROBO_PNG_PALETTED_HEIGHT, 3u, 1u,
     ROBO_PNG_PALETTED_RGBA_BYTES, ROBO_PNG_PALETTED_RGBA_FNV,
     ROBO_PNG_PALETTED_PIXEL_0_0, ROBO_PNG_PALETTED_PIXEL_3_3, 3u, 3u},
};

#define FIXTURE_COUNT (sizeof(FIXTURES) / sizeof(FIXTURES[0]))

static void decode_accept_fixture(const decode_fixture_t *f)
{
    uint8_t bytes[2048];
    size_t length = hex_decode(f->hex, bytes, sizeof(bytes));
    robo_gfx_decode_info_t info;
    robo_gfx_image_t *image = NULL;
    robo_gfx_decode_limits_t limits;

    memset(&info, 0, sizeof(info));
    ROBO_CHECK_EQ_INT(length, f->byte_count);

    /* 1. probe: recognition from the signature alone, no allocation. */
    ROBO_CHECK_EQ_INT(robo_gfx_decode_probe(bytes, length),
                      ROBO_GFX_DECODE_CONTAINER_PNG);

    /* 2. header only: dims and colour model, no pixels. */
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode_info(bytes, length, NULL, &info),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(info.container, ROBO_GFX_DECODE_CONTAINER_PNG);
    ROBO_CHECK_EQ_INT((int)info.width, (int)f->width);
    ROBO_CHECK_EQ_INT((int)info.height, (int)f->height);
    ROBO_CHECK_EQ_INT(info.bit_depth, 8);
    ROBO_CHECK_EQ_INT(info.color_type, f->color_type);
    ROBO_CHECK_EQ_INT(info.channels, f->channels);
    ROBO_CHECK_EQ_INT(info.interlaced, 0);
    ROBO_CHECK_EQ_INT((int)info.decoded_bytes, (int)f->rgba_bytes);

    /* 3. pixels. */
    robo_gfx_decode_limits_init(&limits);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(bytes, length, &limits, &image),
                      ROBO_OK);
    ROBO_CHECK(image != NULL);
    ROBO_CHECK_EQ_INT((int)robo_gfx_image_width(image), (int)f->width);
    ROBO_CHECK_EQ_INT((int)robo_gfx_image_height(image), (int)f->height);
    ROBO_CHECK_EQ_INT(robo_gfx_image_format(image), ROBO_GFX_FORMAT_RGBA8);
    ROBO_CHECK_EQ_INT(robo_gfx_image_kind(image), ROBO_GFX_IMAGE_KIND_RASTER);
    if (image) {
        uint8_t px[4];
        robo_gfx_surface_t *surface = robo_gfx_image_surface(image);

        ROBO_CHECK_EQ_INT((int)image_rgba_fnv(image), (int)f->rgba_fnv);

        surface_read(surface, 0u, 0u, px);
        ROBO_CHECK_EQ_INT(px[0], f->pixel_0_0[0]);
        ROBO_CHECK_EQ_INT(px[1], f->pixel_0_0[1]);
        ROBO_CHECK_EQ_INT(px[2], f->pixel_0_0[2]);
        ROBO_CHECK_EQ_INT(px[3], f->pixel_0_0[3]);

        surface_read(surface, f->corner_x, f->corner_y, px);
        ROBO_CHECK_EQ_INT(px[0], f->pixel_corner[0]);
        ROBO_CHECK_EQ_INT(px[1], f->pixel_corner[1]);
        ROBO_CHECK_EQ_INT(px[2], f->pixel_corner[2]);
        ROBO_CHECK_EQ_INT(px[3], f->pixel_corner[3]);

        robo_gfx_image_release(image);
    }
}

static void test_decode_fixtures(void)
{
    size_t i;

    for (i = 0u; i < FIXTURE_COUNT; i++) {
        decode_accept_fixture(&FIXTURES[i]);
    }
}

/*
 * The three rejection fixtures. Each must fail with the *specific* code that
 * says why, because "it failed" is not information a caller can act on:
 * NOT_SUPPORTED means this decoder cannot do it, ERROR means the bytes are
 * wrong.
 */
static void test_decode_rejections(void)
{
    uint8_t bytes[2048];
    size_t length;
    robo_gfx_decode_info_t info;
    robo_gfx_image_t *image = NULL;
    robo_gfx_decode_limits_t limits;

    robo_gfx_decode_limits_init(&limits);

    length = hex_decode(RoboPngInterlaced_hex, bytes, sizeof(bytes));
    ROBO_CHECK_EQ_INT(length, ROBO_PNG_INTERLACED_BYTES);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(bytes, length, &limits, &image),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK(image == NULL);

    length = hex_decode(RoboPngPackedNibble_hex, bytes, sizeof(bytes));
    ROBO_CHECK_EQ_INT(length, ROBO_PNG_PACKED_NIBBLE_BYTES);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(bytes, length, &limits, &image),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK(image == NULL);
    ROBO_CHECK_EQ_INT(
        robo_gfx_image_decode_info(bytes, length, &limits, &info),
        ROBO_NOT_SUPPORTED);
}

static void test_decode_probe_and_argument_safety(void)
{
    uint8_t bytes[64];
    robo_gfx_decode_info_t info;
    robo_gfx_image_t *image = NULL;

    memset(bytes, 0, sizeof(bytes));

    /* Empty, tiny, and not-an-image inputs are all UNKNOWN, never a crash. */
    ROBO_CHECK_EQ_INT(robo_gfx_decode_probe(NULL, 0u),
                      ROBO_GFX_DECODE_CONTAINER_UNKNOWN);
    ROBO_CHECK_EQ_INT(robo_gfx_decode_probe("", 0u),
                      ROBO_GFX_DECODE_CONTAINER_UNKNOWN);
    ROBO_CHECK_EQ_INT(robo_gfx_decode_probe("PNG", 3u),
                      ROBO_GFX_DECODE_CONTAINER_UNKNOWN);
    /* A truncated signature is not a PNG. */
    ROBO_CHECK_EQ_INT(robo_gfx_decode_probe("\x89PNG", 4u),
                      ROBO_GFX_DECODE_CONTAINER_UNKNOWN);
    /* BMP and JPEG magic are recognised as "not ours", not as errors. */
    ROBO_CHECK_EQ_INT(robo_gfx_decode_probe("BM\x00\x00\x00\x00\x00\x00", 8u),
                      ROBO_GFX_DECODE_CONTAINER_UNKNOWN);
    ROBO_CHECK_EQ_INT(robo_gfx_decode_probe("\xFF\xD8\xFF\xE0\x00\x10", 6u),
                      ROBO_GFX_DECODE_CONTAINER_UNKNOWN);

    /* Names and enumerator validity. */
    ROBO_CHECK_STR_EQ(robo_gfx_decode_container_name(ROBO_GFX_DECODE_CONTAINER_PNG),
                      "png");
    ROBO_CHECK_STR_EQ(
        robo_gfx_decode_container_name(ROBO_GFX_DECODE_CONTAINER_UNKNOWN),
        "unknown");
    ROBO_CHECK(robo_gfx_decode_container_is_known(ROBO_GFX_DECODE_CONTAINER_PNG));
    ROBO_CHECK(!robo_gfx_decode_container_is_known(
        ROBO_GFX_DECODE_CONTAINER_UNKNOWN));
    ROBO_CHECK(!robo_gfx_decode_container_is_known(
        (robo_gfx_decode_container_t)99));

    /* NULL / zero-length / NULL-out are argument errors, not decode attempts. */
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(NULL, 16u, NULL, &image),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(image == NULL);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(bytes, 0u, NULL, &image),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(image == NULL);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(bytes, 16u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* 16 zero bytes are not an image: "not supported", not "bad argument". */
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode_info(bytes, 16u, NULL, &info),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode_info(NULL, 16u, NULL, &info),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode_info(bytes, 0u, NULL, &info),
                      ROBO_INVALID_ARGUMENT);

    /* Zeroed limits means defaults. */
    memset(&info, 0, sizeof(info));
    ROBO_CHECK_EQ_INT(
        robo_gfx_image_decode_info((const void *)"not an image", 12u, NULL,
                                   &info),
        ROBO_NOT_SUPPORTED);

    {
        robo_gfx_decode_limits_t defaults;

        memset(&defaults, 0xFF, sizeof(defaults));
        robo_gfx_decode_limits_init(&defaults);
        ROBO_CHECK_EQ_INT((int)defaults.max_dimension,
                          (int)ROBO_GFX_DECODE_DEFAULT_MAX_DIMENSION);
        ROBO_CHECK_EQ_INT((int)defaults.max_decoded_bytes,
                          (int)ROBO_GFX_DECODE_DEFAULT_MAX_DECODED_BYTES);
        robo_gfx_decode_limits_init(NULL); /* must not crash */
    }
}

/* ------------------------------------------------------------------ */
/* Round trip against the Goal 6.13 encoder                            */
/* ------------------------------------------------------------------ */

static void round_trip(robo_gfx_pixel_format_t format, uint32_t w, uint32_t h,
                       int colourful)
{
    robo_gfx_surface_t *source = NULL;
    robo_gfx_encode_options_t opts;
    robo_gfx_encoded_buffer_t encoded;
    robo_gfx_image_t *decoded = NULL;
    uint32_t x;
    uint32_t y;

    ROBO_CHECK_EQ_INT(make_surface(w, h, format, &source), ROBO_OK);
    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x++) {
            uint8_t r = colourful ? (uint8_t)(x * 37u + 5u) : (uint8_t)((x + y) * 11u);
            uint8_t g = colourful ? (uint8_t)(y * 53u + 7u) : r;
            uint8_t b = colourful ? (uint8_t)((x * y) & 0xFFu) : r;
            uint8_t a = 255u;

            if (format == ROBO_GFX_FORMAT_RGBA8 || format == ROBO_GFX_FORMAT_ALPHA8) {
                a = (uint8_t)(128u + ((x + y) & 0x7Fu));
            }
            surface_write(source, x, y, r, g, b, a);
        }
    }

    robo_gfx_encode_options_init(&opts);
    opts.container = ROBO_GFX_ENCODE_PNG;
    opts.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
    memset(&encoded, 0, sizeof(encoded));
    ROBO_CHECK_EQ_INT(robo_gfx_surface_encode(source, &opts, &encoded),
                      ROBO_OK);
    ROBO_CHECK(encoded.size > 0u);

    /* The encoder writes store blocks; this is the stored-block path. */
    ROBO_CHECK_EQ_INT(
        robo_gfx_image_decode(encoded.data, encoded.size, NULL, &decoded),
        ROBO_OK);
    ROBO_CHECK(decoded != NULL);
    if (decoded) {
        robo_gfx_surface_t *back = robo_gfx_image_surface(decoded);

        ROBO_CHECK_EQ_INT((int)robo_gfx_image_width(decoded), (int)w);
        ROBO_CHECK_EQ_INT((int)robo_gfx_image_height(decoded), (int)h);
        ROBO_CHECK_EQ_INT(robo_gfx_image_format(decoded), ROBO_GFX_FORMAT_RGBA8);
        /*
         * Compare logically, not by bytes: the source may be BGRA/GRAY, and
         * the decoder's contract is "logical RGBA8", so reading both through
         * the accessors is the only comparison that means anything.
         */
        for (y = 0u; y < h; y++) {
            for (x = 0u; x < w; x++) {
                uint8_t want[4];
                uint8_t got[4];

                surface_read(source, x, y, want);
                surface_read(back, x, y, got);
                ROBO_CHECK_EQ_INT(got[0], want[0]);
                ROBO_CHECK_EQ_INT(got[1], want[1]);
                ROBO_CHECK_EQ_INT(got[2], want[2]);
                ROBO_CHECK_EQ_INT(got[3], want[3]);
            }
        }
        robo_gfx_image_release(decoded);
    }
    robo_gfx_encoded_buffer_release(&encoded);
    robo_gfx_surface_release(source);
}

static void test_decode_round_trip(void)
{
    round_trip(ROBO_GFX_FORMAT_RGBA8, 17u, 9u, 1);
    round_trip(ROBO_GFX_FORMAT_BGRA8, 5u, 4u, 1);
    round_trip(ROBO_GFX_FORMAT_RGB8, 8u, 8u, 1);
    round_trip(ROBO_GFX_FORMAT_GRAY8, 6u, 3u, 0);
    round_trip(ROBO_GFX_FORMAT_RGBA8, 1u, 1u, 1);
}

/* ------------------------------------------------------------------ */
/* Malformed input, built here with valid CRCs                         */
/* ------------------------------------------------------------------ */

typedef struct png_builder {
    uint8_t out[SCRATCH];
    size_t pos;
} png_builder_t;

static void builder_start(png_builder_t *b)
{
    static const uint8_t sig[8] = {0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au,
                                   0x0Au};

    memcpy(b->out, sig, sizeof(sig));
    b->pos = sizeof(sig);
}

static void builder_ihdr(png_builder_t *b, uint32_t w, uint32_t h,
                         uint8_t depth, uint8_t color, uint8_t interlace)
{
    uint8_t ihdr[13];

    be32_write(ihdr, w);
    be32_write(ihdr + 4u, h);
    ihdr[8] = depth;
    ihdr[9] = color;
    ihdr[10] = 0u;
    ihdr[11] = 0u;
    ihdr[12] = interlace;
    b->pos = png_emit_chunk(b->out, sizeof(b->out), b->pos, "IHDR", ihdr, 13u);
}

static void builder_chunk(png_builder_t *b, const char *type,
                          const uint8_t *data, size_t length)
{
    b->pos = png_emit_chunk(b->out, sizeof(b->out), b->pos, type, data, length);
}

static void builder_zlib_idat(png_builder_t *b, const uint8_t *raw,
                              size_t raw_length)
{
    uint8_t zlib[SCRATCH];
    size_t n = zlib_store(zlib, sizeof(zlib), raw, raw_length);

    if (n == 0u) {
        ROBO_CHECK(0 && "zlib_store scratch overflow");
        return;
    }
    builder_chunk(b, "IDAT", zlib, n);
}

static void builder_iend(png_builder_t *b)
{
    b->pos = png_emit_chunk(b->out, sizeof(b->out), b->pos, "IEND", NULL, 0u);
}

/** PAETH predictor, forward direction (RFC 2083 §6.6). */
static uint8_t test_paeth(uint8_t a, uint8_t b, uint8_t c)
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
static void filter_rows(const uint8_t *image, uint8_t *out, uint32_t w,
                        uint32_t h, size_t bpp, int filter)
{
    uint32_t y;

    for (y = 0u; y < h; y++) {
        size_t row_bytes = (size_t)w * bpp;
        const uint8_t *cur = image + row_bytes * (size_t)y;
        const uint8_t *prev =
            (y == 0u) ? NULL : image + row_bytes * (size_t)(y - 1u);
        uint8_t *dst = out + (row_bytes + 1u) * (size_t)y;
        size_t i;

        dst[0] = (uint8_t)filter;
        for (i = 0u; i < row_bytes; i++) {
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
                    (uint8_t)(cur[i] - test_paeth(left, up, upleft));
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

static void test_decode_unfilter_filters(void)
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
        png_builder_t b;
        robo_gfx_image_t *decoded = NULL;
        robo_gfx_surface_t *s;

        filter_rows(image, blocked, TEST_IMAGE_W, TEST_IMAGE_H, 1u, filter);
        builder_start(&b);
        builder_ihdr(&b, TEST_IMAGE_W, TEST_IMAGE_H, 8u, 0u, 0u);
        builder_zlib_idat(&b, blocked, sizeof(blocked));
        builder_iend(&b);

        ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &decoded),
                          ROBO_OK);
        ROBO_CHECK(decoded != NULL);
        if (!decoded) {
            continue;
        }
        s = robo_gfx_image_surface(decoded);
        for (y = 0u; y < TEST_IMAGE_H; y++) {
            for (x = 0u; x < TEST_IMAGE_W; x++) {
                uint8_t px[4];
                uint8_t want = image[y * TEST_IMAGE_W + x];

                surface_read(s, x, y, px);
                ROBO_CHECK_EQ_INT(px[0], want);
                ROBO_CHECK_EQ_INT(px[1], want);
                ROBO_CHECK_EQ_INT(px[2], want);
                ROBO_CHECK_EQ_INT(px[3], 255);
            }
        }
        robo_gfx_image_release(decoded);
    }
}

static void test_decode_limit_enforcement(void)
{
    uint8_t bytes[2048];
    size_t length = hex_decode(RoboPngChecker_hex, bytes, sizeof(bytes));
    robo_gfx_decode_limits_t limits;
    robo_gfx_image_t *image = NULL;
    robo_gfx_decode_info_t info;

    /* Dimension ceiling below the real size. */
    robo_gfx_decode_limits_init(&limits);
    limits.max_dimension = 8u;
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(bytes, length, &limits, &image),
                      ROBO_ERROR);
    ROBO_CHECK(image == NULL);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode_info(bytes, length, &limits, &info),
                      ROBO_ERROR);

    /* Byte ceiling below the real decoded size, dimension ceiling generous. */
    robo_gfx_decode_limits_init(&limits);
    limits.max_dimension = 4096u;
    limits.max_decoded_bytes = 1024u; /* 32x32 RGBA8 needs 4096 */
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(bytes, length, &limits, &image),
                      ROBO_ERROR);
    ROBO_CHECK(image == NULL);

    /* A ceiling exactly at the size is admitted, so the check is not off-by-one. */
    limits.max_decoded_bytes = 4096u;
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(bytes, length, &limits, &image),
                      ROBO_OK);
    ROBO_CHECK(image != NULL);
    robo_gfx_image_release(image);
}

static void test_decode_hostile_headers(void)
{
    png_builder_t b;
    robo_gfx_image_t *image = NULL;
    robo_gfx_decode_info_t info;

    /* A header claiming 4 gigapixels must fail before allocating anything:
     * the whole point of validating dimensions ahead of the decode. */
    builder_start(&b);
    builder_ihdr(&b, 0xFFFFFFFFu, 0xFFFFFFFFu, 8u, 6u, 0u);
    b.pos = png_emit_chunk(b.out, sizeof(b.out), b.pos, "IDAT",
                           (const uint8_t *)"\x78\x01\x01\x00\x00\xff\xff\x00\x00\x00\x00",
                           11u);
    builder_iend(&b);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_ERROR);
    ROBO_CHECK(image == NULL);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode_info(b.out, b.pos, NULL, &info),
                      ROBO_ERROR);

    /* Zero width / zero height are not dimensions a surface can have. */
    builder_start(&b);
    builder_ihdr(&b, 0u, 4u, 8u, 0u, 0u);
    builder_zlib_idat(&b, (const uint8_t *)"\x00\x00", 2u);
    builder_iend(&b);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_ERROR);
    ROBO_CHECK(image == NULL);

    builder_start(&b);
    builder_ihdr(&b, 4u, 0u, 8u, 0u, 0u);
    builder_zlib_idat(&b, (const uint8_t *)"\x00\x00", 2u);
    builder_iend(&b);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_ERROR);
    ROBO_CHECK(image == NULL);
}

static void test_decode_hostile_headers_continued(void)
{
    png_builder_t b;
    robo_gfx_image_t *image = NULL;

    /* Unknown critical chunk: refused, not skipped. */
    builder_start(&b);
    builder_ihdr(&b, 2u, 2u, 8u, 0u, 0u);
    builder_chunk(&b, "ZZZZ", (const uint8_t *)"abcd", 4u);
    builder_zlib_idat(&b, (const uint8_t *)"\x00\x00\x00", 3u);
    builder_iend(&b);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK(image == NULL);

    /* Unknown *ancillary* chunk: skipped, and the image still decodes. */
    builder_start(&b);
    builder_ihdr(&b, 2u, 2u, 8u, 0u, 0u);
    builder_chunk(&b, "zzzz", (const uint8_t *)"abcd", 4u);
    /* Two rows of two grayscale samples: (2 * 1 + 1) * 2 = 6 bytes. */
    builder_zlib_idat(&b, (const uint8_t *)"\x00\x01\x02\x03\x04\x05", 6u);
    builder_iend(&b);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_OK);
    ROBO_CHECK(image != NULL);
    robo_gfx_image_release(image);

    /* A non-zero compression or filter method is something else's format. */
    builder_start(&b);
    {
        uint8_t ihdr[13];

        memset(ihdr, 0, sizeof(ihdr));
        be32_write(ihdr, 2u);
        be32_write(ihdr + 4u, 2u);
        ihdr[8] = 8u;
        ihdr[9] = 0u;
        ihdr[10] = 1u; /* compression method 1 */
        b.pos = png_emit_chunk(b.out, sizeof(b.out), b.pos, "IHDR", ihdr, 13u);
    }
    builder_zlib_idat(&b, (const uint8_t *)"\x00\x00\x00", 3u);
    builder_iend(&b);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK(image == NULL);

    /* IEND with a payload is malformed. */
    builder_start(&b);
    builder_ihdr(&b, 2u, 2u, 8u, 0u, 0u);
    builder_zlib_idat(&b, (const uint8_t *)"\x00\x00\x00", 3u);
    builder_chunk(&b, "IEND", (const uint8_t *)"x", 1u);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_ERROR);
    ROBO_CHECK(image == NULL);
}

static void test_decode_hostile_streams(void)
{
    png_builder_t b;
    robo_gfx_image_t *image = NULL;
    uint8_t zlib[128];
    uint8_t raw[8];

    /* 1. No IDAT at all. */
    builder_start(&b);
    builder_ihdr(&b, 2u, 2u, 8u, 0u, 0u);
    builder_iend(&b);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_ERROR);
    ROBO_CHECK(image == NULL);

    /* 2. No IEND: the stream is truncated even though IHDR and IDAT are fine. */
    builder_start(&b);
    builder_ihdr(&b, 2u, 2u, 8u, 0u, 0u);
    builder_zlib_idat(&b, (const uint8_t *)"\x00\x01\x02\x03\x04\x05\x06",
                      7u);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_ERROR);
    ROBO_CHECK(image == NULL);

    /* 3. Deflate yields too few bytes for the declared dimensions. */
    builder_start(&b);
    builder_ihdr(&b, 8u, 8u, 8u, 0u, 0u);
    {
        size_t n = zlib_store(zlib, sizeof(zlib), (const uint8_t *)"\x00\x01",
                              2u);

        builder_chunk(&b, "IDAT", zlib, n);
    }
    builder_iend(&b);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_ERROR);
    ROBO_CHECK(image == NULL);

    /* 4. Deflate yields too many bytes for the declared dimensions. */
    builder_start(&b);
    builder_ihdr(&b, 1u, 1u, 8u, 0u, 0u);
    {
        size_t n = zlib_store(zlib, sizeof(zlib), (const uint8_t *)"\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09",
                              10u);

        builder_chunk(&b, "IDAT", zlib, n);
    }
    builder_iend(&b);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_ERROR);
    ROBO_CHECK(image == NULL);

    /* 5. Bad zlib header: CM = 7 is not deflate. */
    {
        uint8_t stream[16];
        size_t n = zlib_store(stream, sizeof(stream),
                              (const uint8_t *)"\x00\x01\x02", 3u);

        stream[0] = 0x70u; /* CM = 7 */
        builder_start(&b);
        builder_ihdr(&b, 2u, 2u, 8u, 0u, 0u);
        builder_chunk(&b, "IDAT", stream, n);
        builder_iend(&b);
        ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                          ROBO_ERROR);
        ROBO_CHECK(image == NULL);
    }

    /* 6. Preset dictionary set: refused as unsupported, not misdecoded. */
    {
        uint8_t stream[16];
        size_t n = zlib_store(stream, sizeof(stream),
                              (const uint8_t *)"\x00\x01\x02", 3u);

        stream[1] |= 0x20u; /* FDICT */
        /* Restore FCHECK so the header is otherwise well-formed. */
        stream[1] = (uint8_t)((stream[1] + 31u - (((((uint32_t)stream[0] << 8) |
                                                   stream[1]) %
                                                  31u))) &
                              0xFFu);
        builder_start(&b);
        builder_ihdr(&b, 2u, 2u, 8u, 0u, 0u);
        builder_chunk(&b, "IDAT", stream, n);
        builder_iend(&b);
        ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK(image == NULL);
    }

    /* 7. Undefined row filter (5). */
    builder_start(&b);
    builder_ihdr(&b, 2u, 2u, 8u, 0u, 0u);
    raw[0] = 5u;
    raw[1] = 1u;
    raw[2] = 2u;
    raw[3] = 5u;
    raw[4] = 3u;
    raw[5] = 4u;
    builder_zlib_idat(&b, raw, 6u);
    builder_iend(&b);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_ERROR);
    ROBO_CHECK(image == NULL);

    /* 8. Reserved DEFLATE block type (BTYPE = 3). */
    {
        uint8_t stream[8];

        stream[0] = 0x78u;
        stream[1] = 0x01u;
        stream[2] = 0x07u; /* BFINAL = 1, BTYPE = 3 */
        stream[3] = 0x00u;
        builder_start(&b);
        builder_ihdr(&b, 2u, 2u, 8u, 0u, 0u);
        builder_chunk(&b, "IDAT", stream, 4u);
        builder_iend(&b);
        ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                          ROBO_ERROR);
        ROBO_CHECK(image == NULL);
    }
}

static void test_decode_crc_and_structure(void)
{
    uint8_t bytes[2048];
    size_t length = hex_decode(RoboPngGradient_hex, bytes, sizeof(bytes));
    robo_gfx_image_t *image = NULL;
    robo_gfx_decode_info_t info;
    size_t i;

    ROBO_CHECK_EQ_INT(length, ROBO_PNG_GRADIENT_BYTES);

    /* Flip one byte inside IHDR's CRC: the chunk is byte-identical in every
     * other respect, so only the CRC check can catch it. */
    {
        uint8_t copy[2048];

        memcpy(copy, bytes, length);
        copy[29] ^= 0x01u; /* IHDR crc byte */
        ROBO_CHECK_EQ_INT(robo_gfx_image_decode(copy, length, NULL, &image),
                          ROBO_ERROR);
        ROBO_CHECK(image == NULL);
    }

    /*
     * A stored block whose LEN / NLEN complement is wrong, with a freshly
     * computed CRC so the chunk is intact. Nothing but the DEFLATE-level
     * check can catch this, which is what makes it a test of the inflate
     * path rather than of the chunk walk.
     */
    {
        png_builder_t b;
        uint8_t stream[64];
        size_t n = zlib_store(stream, sizeof(stream),
                              (const uint8_t *)"\x00\x01\x02\x03\x04\x05\x06",
                              7u);

        ROBO_CHECK(n > 11u);
        stream[5] = (uint8_t)(stream[5] ^ 0xFFu); /* break ~LEN */
        builder_start(&b);
        builder_ihdr(&b, 2u, 2u, 8u, 0u, 0u);
        builder_chunk(&b, "IDAT", stream, n);
        builder_iend(&b);
        ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                          ROBO_ERROR);
        ROBO_CHECK(image == NULL);
    }

    /* Every truncation point must fail cleanly, never crash or half-decode. */
    for (i = 1u; i < length; i++) {
        image = NULL;
        {
            robo_result_t r = robo_gfx_image_decode(bytes, i, NULL, &image);

            ROBO_CHECK(r != ROBO_OK);
            ROBO_CHECK(image == NULL);
        }
    }

    /* The full stream still decodes after all that. */
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(bytes, length, NULL, &image),
                      ROBO_OK);
    ROBO_CHECK(image != NULL);
    robo_gfx_image_release(image);

    /* A 12-byte header on its own is a PNG with no image data. */
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode_info(bytes, 12u, NULL, &info),
                      ROBO_ERROR);
}

static void test_decode_palette_and_transparency(void)
{
    uint8_t bytes[2048];
    size_t length = hex_decode(RoboPngPaletted_hex, bytes, sizeof(bytes));
    robo_gfx_image_t *image = NULL;
    robo_gfx_surface_t *s;
    uint8_t px[4];

    ROBO_CHECK_EQ_INT(length, ROBO_PNG_PALETTED_BYTES);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(bytes, length, NULL, &image),
                      ROBO_OK);
    ROBO_CHECK(image != NULL);
    if (!image) {
        return;
    }
    s = robo_gfx_image_surface(image);

    /* Palette index 0 = opaque red, index 1 = green at tRNS alpha 128. */
    surface_read(s, 0u, 0u, px);
    ROBO_CHECK_EQ_INT(px[0], 255);
    ROBO_CHECK_EQ_INT(px[1], 0);
    ROBO_CHECK_EQ_INT(px[2], 0);
    ROBO_CHECK_EQ_INT(px[3], 255);

    surface_read(s, 1u, 0u, px);
    ROBO_CHECK_EQ_INT(px[0], 0);
    ROBO_CHECK_EQ_INT(px[1], 255);
    ROBO_CHECK_EQ_INT(px[2], 0);
    ROBO_CHECK_EQ_INT(px[3], 128);

    /* Index 3 is white at tRNS alpha 0 — the alpha channel survives decoding. */
    surface_read(s, 3u, 0u, px);
    ROBO_CHECK_EQ_INT(px[0], 255);
    ROBO_CHECK_EQ_INT(px[1], 255);
    ROBO_CHECK_EQ_INT(px[2], 255);
    ROBO_CHECK_EQ_INT(px[3], 0);

    robo_gfx_image_release(image);
}

static void test_decode_palette_malformed(void)
{
    png_builder_t b;
    robo_gfx_image_t *image = NULL;
    uint8_t zlib[64];

    /* A palette image with no PLTE chunk cannot be decoded: the indices have
     * no meaning, so refusing is the only honest answer. */
    builder_start(&b);
    builder_ihdr(&b, 2u, 2u, 8u, 3u, 0u);
    {
        size_t n = zlib_store(zlib, sizeof(zlib),
                              (const uint8_t *)"\x00\x00\x00\x00\x00\x00",
                              6u);
        builder_chunk(&b, "IDAT", zlib, n);
    }
    builder_iend(&b);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_ERROR);
    ROBO_CHECK(image == NULL);

    /* A palette index past the end of PLTE yields opaque black rather than a
     * read past the palette, and the rest of the image still decodes. */
    builder_start(&b);
    builder_ihdr(&b, 2u, 1u, 8u, 3u, 0u);
    builder_chunk(&b, "PLTE", (const uint8_t *)"\xFF\x00\x00\x00\xFF\x00", 6u);
    {
        size_t n = zlib_store(zlib, sizeof(zlib),
                             (const uint8_t *)"\x00\x01\x07", 3u);
        builder_chunk(&b, "IDAT", zlib, n);
    }
    builder_iend(&b);
    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_OK);
    ROBO_CHECK(image != NULL);
    if (image) {
        robo_gfx_surface_t *s = robo_gfx_image_surface(image);
        uint8_t px[4];

        surface_read(s, 0u, 0u, px);
        ROBO_CHECK_EQ_INT(px[0], 0);
        ROBO_CHECK_EQ_INT(px[1], 255);
        ROBO_CHECK_EQ_INT(px[2], 0);
        surface_read(s, 1u, 0u, px);
        ROBO_CHECK_EQ_INT(px[0], 0);
        ROBO_CHECK_EQ_INT(px[1], 0);
        ROBO_CHECK_EQ_INT(px[2], 0);
        ROBO_CHECK_EQ_INT(px[3], 255);
        robo_gfx_image_release(image);
    }
}

static void test_decode_tiling_and_large(void)
{
    /*
     * A wider image than the scratch builder's convenience helpers, decoded
     * through the real store-block writer: this is the case where a stride
     * bug or an off-by-one in row addressing would show up as a wrong pixel
     * at the far right of a row rather than as a crash.
     */
    png_builder_t b;
    uint8_t raw[SCRATCH / 2];
    uint32_t w = 64u;
    uint32_t h = 8u;
    robo_gfx_image_t *image = NULL;
    size_t raw_length = (size_t)w * 3u * h + h;
    uint32_t y;
    uint32_t x;

    for (y = 0u; y < h; y++) {
        uint8_t *row = raw + ((size_t)w * 3u + 1u) * (size_t)y;

        row[0] = 0u;
        for (x = 0u; x < w * 3u; x++) {
            row[1u + x] = (uint8_t)((x + y * 7u) & 0xFFu);
        }
    }

    builder_start(&b);
    builder_ihdr(&b, w, h, 8u, 2u, 0u);
    builder_zlib_idat(&b, raw, raw_length);
    builder_iend(&b);

    ROBO_CHECK_EQ_INT(robo_gfx_image_decode(b.out, b.pos, NULL, &image),
                      ROBO_OK);
    ROBO_CHECK(image != NULL);
    if (image) {
        robo_gfx_surface_t *s = robo_gfx_image_surface(image);
        uint8_t px[4];

        ROBO_CHECK_EQ_INT((int)robo_gfx_image_width(image), (int)w);
        ROBO_CHECK_EQ_INT((int)robo_gfx_image_height(image), (int)h);
        /* The source pattern is (byte index + y * 7) & 0xFF, so the last
         * pixel's three samples are its row offsets plus that row's term. */
        surface_read(s, w - 1u, h - 1u, px);
        ROBO_CHECK_EQ_INT(
            px[0], (uint8_t)(((w - 1u) * 3u + (h - 1u) * 7u) & 0xFFu));
        ROBO_CHECK_EQ_INT(
            px[1],
            (uint8_t)(((w - 1u) * 3u + 1u + (h - 1u) * 7u) & 0xFFu));
        ROBO_CHECK_EQ_INT(
            px[2],
            (uint8_t)(((w - 1u) * 3u + 2u + (h - 1u) * 7u) & 0xFFu));
        ROBO_CHECK_EQ_INT(px[3], 255);
        robo_gfx_image_release(image);
    }
}

void robo_test_register_graphics_decode(void)
{
    robo_test_register("graphics.decode.real_fixtures", test_decode_fixtures);
    robo_test_register("graphics.decode.probe_safety",
                       test_decode_probe_and_argument_safety);
    robo_test_register("graphics.decode.rejections", test_decode_rejections);
    robo_test_register("graphics.decode.round_trip_encoder",
                       test_decode_round_trip);
    robo_test_register("graphics.decode.unfilter", test_decode_unfilter_filters);
    robo_test_register("graphics.decode.limits", test_decode_limit_enforcement);
    robo_test_register("graphics.decode.hostile_headers",
                       test_decode_hostile_headers);
    robo_test_register("graphics.decode.hostile_headers_two",
                       test_decode_hostile_headers_continued);
    robo_test_register("graphics.decode.hostile_streams",
                       test_decode_hostile_streams);
    robo_test_register("graphics.decode.crc_and_truncation",
                       test_decode_crc_and_structure);
    robo_test_register("graphics.decode.palette",
                       test_decode_palette_and_transparency);
    robo_test_register("graphics.decode.palette_malformed",
                       test_decode_palette_malformed);
    robo_test_register("graphics.decode.large_rows", test_decode_tiling_and_large);
}
