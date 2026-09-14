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
} mem_sink_t;

static robo_result_t make_surface(uint32_t w,
                                  uint32_t h,
                                  robo_gfx_pixel_format_t fmt,
                                  robo_gfx_surface_t **out)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_desc_init(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = fmt;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    return robo_gfx_surface_create(&desc, out);
}

static uint32_t read_u32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t read_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t test_crc32(const uint8_t *data, size_t len)
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
static int inflate_store_zlib(const uint8_t *z,
                              size_t zlen,
                              uint8_t *out,
                              size_t out_cap,
                              size_t *out_len)
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
        len = read_u16le(z + off);
        nlen = read_u16le(z + off + 2u);
        off += 4u;
        if ((uint16_t)(len ^ 0xFFFFu) != nlen) {
            return 0;
        }
        if (off + len > zlen) {
            return 0;
        }
        if (o + len > out_cap) {
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
    *out_len = o;
    return 1;
}

static int png_decode_pixels(const uint8_t *png,
                             size_t png_len,
                             uint32_t *out_w,
                             uint32_t *out_h,
                             int *out_rgba,
                             uint8_t *pixels,
                             size_t pixels_cap,
                             size_t *pixels_len)
{
    size_t off;
    uint32_t w = 0u;
    uint32_t h = 0u;
    uint8_t color_type = 0u;
    uint8_t idat[65536];
    size_t idat_len = 0u;
    uint8_t raw[65536];
    size_t raw_len = 0u;
    uint8_t bpp;
    size_t expect;
    uint32_t y;
    size_t o = 0u;

    static const uint8_t sig[8] = {
        0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};

    if (png_len < 8u || memcmp(png, sig, 8u) != 0) {
        return 0;
    }
    off = 8u;
    while (off + 12u <= png_len) {
        uint32_t clen = read_u32be(png + off);
        const uint8_t *type = png + off + 4u;
        const uint8_t *data = png + off + 8u;
        uint32_t crc_file;
        uint32_t crc_calc;
        if (off + 12u + clen > png_len) {
            return 0;
        }
        crc_file = read_u32be(png + off + 8u + clen);
        crc_calc = test_crc32(type, 4u + clen);
        if (crc_file != crc_calc) {
            return 0;
        }
        if (memcmp(type, "IHDR", 4u) == 0) {
            if (clen != 13u) {
                return 0;
            }
            w = read_u32be(data);
            h = read_u32be(data + 4u);
            if (data[8] != 8u || data[10] != 0u || data[11] != 0u ||
                data[12] != 0u) {
                return 0;
            }
            color_type = data[9];
            if (color_type != 2u && color_type != 6u) {
                return 0;
            }
        } else if (memcmp(type, "IDAT", 4u) == 0) {
            if (idat_len + clen > sizeof(idat)) {
                return 0;
            }
            memcpy(idat + idat_len, data, clen);
            idat_len += clen;
        } else if (memcmp(type, "IEND", 4u) == 0) {
            break;
        }
        off += 12u + clen;
    }

    if (w == 0u || h == 0u || idat_len == 0u) {
        return 0;
    }
    if (!inflate_store_zlib(idat, idat_len, raw, sizeof(raw), &raw_len)) {
        return 0;
    }
    bpp = (color_type == 6u) ? 4u : 3u;
    expect = (size_t)h * (1u + (size_t)w * bpp);
    if (raw_len != expect) {
        return 0;
    }
    if ((size_t)w * (size_t)h * bpp > pixels_cap) {
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
    *out_w = w;
    *out_h = h;
    *out_rgba = (color_type == 6u) ? 1 : 0;
    *pixels_len = o;
    return 1;
}

static void expect_rgba(const uint8_t *p, uint8_t r, uint8_t g, uint8_t b,
                        uint8_t a)
{
    ROBO_CHECK_EQ_INT(p[0], r);
    ROBO_CHECK_EQ_INT(p[1], g);
    ROBO_CHECK_EQ_INT(p[2], b);
    ROBO_CHECK_EQ_INT(p[3], a);
}

static void expect_rgb(const uint8_t *p, uint8_t r, uint8_t g, uint8_t b)
{
    ROBO_CHECK_EQ_INT(p[0], r);
    ROBO_CHECK_EQ_INT(p[1], g);
    ROBO_CHECK_EQ_INT(p[2], b);
}

static void test_encode_png_1x1_rgba(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_encode_options_t opts;
    robo_gfx_encoded_buffer_t a;
    robo_gfx_encoded_buffer_t b;
    uint8_t pixels[16];
    size_t plen = 0u;
    uint32_t w = 0u;
    uint32_t h = 0u;
    int rgba = 0;

    ROBO_CHECK(make_surface(1, 1, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 0, 0,
                                          robo_gfx_rgba8(10, 20, 30, 40)) ==
               ROBO_OK);

    robo_gfx_encode_options_init(&opts);
    opts.container = ROBO_GFX_ENCODE_PNG;
    opts.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;

    ROBO_CHECK(robo_gfx_surface_encode(s, &opts, &a) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_encode(s, &opts, &b) == ROBO_OK);
    ROBO_CHECK(a.size == b.size);
    ROBO_CHECK(a.size >= 8u);
    ROBO_CHECK(memcmp(a.data, b.data, a.size) == 0);
    ROBO_CHECK(png_decode_pixels(a.data, a.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    ROBO_CHECK_EQ_INT(w, 1);
    ROBO_CHECK_EQ_INT(h, 1);
    ROBO_CHECK_EQ_INT(rgba, 1);
    expect_rgba(pixels, 10, 20, 30, 40);

    robo_gfx_encoded_buffer_release(&a);
    robo_gfx_encoded_buffer_release(&b);
    robo_gfx_surface_release(s);
}

static void test_encode_png_rgb_and_alpha_policy(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_encode_options_t opts;
    robo_gfx_encoded_buffer_t buf;
    uint8_t pixels[64];
    size_t plen = 0u;
    uint32_t w = 0u;
    uint32_t h = 0u;
    int rgba = 0;

    ROBO_CHECK(make_surface(2, 1, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 0, 0,
                                          robo_gfx_rgba8(255, 0, 0, 0)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 1, 0,
                                          robo_gfx_rgba8(0, 0, 255, 128)) ==
               ROBO_OK);

    robo_gfx_encode_options_init(&opts);
    opts.container = ROBO_GFX_ENCODE_PNG;
    opts.layout = ROBO_GFX_ENCODE_LAYOUT_RGB8;
    /* Missing background with alpha source */
    ROBO_CHECK(robo_gfx_surface_encode(s, &opts, &buf) ==
               ROBO_INVALID_ARGUMENT);

    opts.background_set = 1;
    opts.background = robo_gfx_rgb8(0, 255, 0);
    ROBO_CHECK(robo_gfx_surface_encode(s, &opts, &buf) == ROBO_OK);
    ROBO_CHECK(png_decode_pixels(buf.data, buf.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    ROBO_CHECK_EQ_INT(w, 2);
    ROBO_CHECK_EQ_INT(h, 1);
    ROBO_CHECK_EQ_INT(rgba, 0);
    /* Fully transparent → background green */
    expect_rgb(pixels, 0, 255, 0);
    /* Partial blue over green: src-over */
    {
        robo_gfx_color_rgba8_t expected = robo_gfx_composite_src_over(
            robo_gfx_rgba8(0, 0, 255, 128), robo_gfx_rgb8(0, 255, 0));
        expect_rgb(pixels + 3, expected.r, expected.g, expected.b);
    }

    robo_gfx_encoded_buffer_release(&buf);
    robo_gfx_surface_release(s);
}

static void test_encode_png_channels(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_encode_options_t opts;
    robo_gfx_encoded_buffer_t buf;
    uint8_t pixels[256];
    size_t plen = 0u;
    uint32_t w = 0u;
    uint32_t h = 0u;
    int rgba = 0;

    ROBO_CHECK(make_surface(4, 2, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_clear(s, robo_gfx_rgba8(0, 0, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 0, 0,
                                          robo_gfx_rgba8(0, 0, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 1, 0,
                                          robo_gfx_rgba8(255, 255, 255, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 2, 0,
                                          robo_gfx_rgba8(255, 0, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 3, 0,
                                          robo_gfx_rgba8(0, 255, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 0, 1,
                                          robo_gfx_rgba8(0, 0, 255, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 1, 1,
                                          robo_gfx_rgba8(255, 255, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 2, 1,
                                          robo_gfx_rgba8(0, 255, 255, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 3, 1,
                                          robo_gfx_rgba8(255, 0, 255, 128)) ==
               ROBO_OK);

    robo_gfx_encode_options_init(&opts);
    ROBO_CHECK(robo_gfx_surface_encode(s, &opts, &buf) == ROBO_OK);
    ROBO_CHECK(png_decode_pixels(buf.data, buf.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    ROBO_CHECK_EQ_INT(w, 4);
    ROBO_CHECK_EQ_INT(h, 2);
    expect_rgba(pixels + 0, 0, 0, 0, 255);
    expect_rgba(pixels + 4, 255, 255, 255, 255);
    expect_rgba(pixels + 8, 255, 0, 0, 255);
    expect_rgba(pixels + 12, 0, 255, 0, 255);
    expect_rgba(pixels + 16, 0, 0, 255, 255);
    expect_rgba(pixels + 20, 255, 255, 0, 255);
    expect_rgba(pixels + 24, 0, 255, 255, 255);
    expect_rgba(pixels + 28, 255, 0, 255, 128);

    robo_gfx_encoded_buffer_release(&buf);
    robo_gfx_surface_release(s);
}

static void test_encode_region(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_encode_options_t opts;
    robo_gfx_encoded_buffer_t buf;
    robo_gfx_rect_t region;
    uint8_t pixels[64];
    size_t plen = 0u;
    uint32_t w = 0u;
    uint32_t h = 0u;
    int rgba = 0;

    ROBO_CHECK(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_clear(s, robo_gfx_rgba8(1, 2, 3, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 1, 1,
                                          robo_gfx_rgba8(9, 8, 7, 6)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 2, 2,
                                          robo_gfx_rgba8(5, 4, 3, 2)) ==
               ROBO_OK);

    robo_gfx_encode_options_init(&opts);
    region = robo_gfx_rect(1, 1, 2, 2);
    ROBO_CHECK(robo_gfx_surface_encode_rect(s, region, &opts, &buf) == ROBO_OK);
    ROBO_CHECK(png_decode_pixels(buf.data, buf.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    ROBO_CHECK_EQ_INT(w, 2);
    ROBO_CHECK_EQ_INT(h, 2);
    expect_rgba(pixels + 0, 9, 8, 7, 6);
    expect_rgba(pixels + 4, 1, 2, 3, 255);
    expect_rgba(pixels + 8, 1, 2, 3, 255);
    expect_rgba(pixels + 12, 5, 4, 3, 2);
    robo_gfx_encoded_buffer_release(&buf);

    /* Edge strip */
    region = robo_gfx_rect(0, 3, 4, 1);
    ROBO_CHECK(robo_gfx_surface_encode_rect(s, region, &opts, &buf) == ROBO_OK);
    ROBO_CHECK(png_decode_pixels(buf.data, buf.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    ROBO_CHECK_EQ_INT(w, 4);
    ROBO_CHECK_EQ_INT(h, 1);
    robo_gfx_encoded_buffer_release(&buf);

    /* Invalid / empty */
    region = robo_gfx_rect(0, 0, 0, 1);
    ROBO_CHECK(robo_gfx_surface_encode_rect(s, region, &opts, &buf) ==
               ROBO_INVALID_ARGUMENT);
    region = robo_gfx_rect(3, 3, 2, 2);
    ROBO_CHECK(robo_gfx_surface_encode_rect(s, region, &opts, &buf) ==
               ROBO_INVALID_ARGUMENT);
    region = robo_gfx_rect(-1, 0, 1, 1);
    ROBO_CHECK(robo_gfx_surface_encode_rect(s, region, &opts, &buf) ==
               ROBO_INVALID_ARGUMENT);

    robo_gfx_surface_release(s);
}

static void test_encode_bmp_24_and_32(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_encode_options_t opts;
    robo_gfx_encoded_buffer_t buf;
    uint32_t off;
    uint32_t dib;
    uint16_t bpp;
    uint32_t w;
    uint32_t h;
    const uint8_t *pix;

    ROBO_CHECK(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 0, 0,
                                          robo_gfx_rgba8(255, 0, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 1, 0,
                                          robo_gfx_rgba8(0, 255, 0, 255)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 0, 1,
                                          robo_gfx_rgba8(0, 0, 255, 128)) ==
               ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(s, 1, 1,
                                          robo_gfx_rgba8(255, 255, 255, 0)) ==
               ROBO_OK);

    robo_gfx_encode_options_init(&opts);
    opts.container = ROBO_GFX_ENCODE_BMP;
    opts.layout = ROBO_GFX_ENCODE_LAYOUT_RGB8;
    opts.background_set = 1;
    opts.background = robo_gfx_rgb8(0, 0, 0);

    ROBO_CHECK(robo_gfx_surface_encode(s, &opts, &buf) == ROBO_OK);
    ROBO_CHECK(buf.size >= 54u);
    ROBO_CHECK(buf.data[0] == 'B' && buf.data[1] == 'M');
    off = read_u32le(buf.data + 10);
    dib = read_u32le(buf.data + 14);
    w = read_u32le(buf.data + 18);
    h = read_u32le(buf.data + 22);
    bpp = read_u16le(buf.data + 28);
    ROBO_CHECK_EQ_INT(dib, 40);
    ROBO_CHECK_EQ_INT(w, 2);
    ROBO_CHECK_EQ_INT(h, 2);
    ROBO_CHECK_EQ_INT(bpp, 24);
    /* Bottom row in file is top of image? bottom-up: first row is y=h-1 */
    pix = buf.data + off;
    /* First stored row = surface y=1: blue (0,0,255,128) over black → ... */
    {
        robo_gfx_color_rgba8_t c = robo_gfx_composite_src_over(
            robo_gfx_rgba8(0, 0, 255, 128), robo_gfx_rgb8(0, 0, 0));
        ROBO_CHECK_EQ_INT(pix[0], c.b);
        ROBO_CHECK_EQ_INT(pix[1], c.g);
        ROBO_CHECK_EQ_INT(pix[2], c.r);
    }
    /* Padding: 2*3=6 → pad 2 to 8 */
    ROBO_CHECK_EQ_INT(pix[6], 0);
    ROBO_CHECK_EQ_INT(pix[7], 0);
    robo_gfx_encoded_buffer_release(&buf);

    opts.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
    opts.background_set = 0;
    ROBO_CHECK(robo_gfx_surface_encode(s, &opts, &buf) == ROBO_OK);
    dib = read_u32le(buf.data + 14);
    bpp = read_u16le(buf.data + 28);
    off = read_u32le(buf.data + 10);
    ROBO_CHECK_EQ_INT(dib, 108);
    ROBO_CHECK_EQ_INT(bpp, 32);
    pix = buf.data + off;
    /* First row = y=1: (0,0,255,128) as BGRA */
    ROBO_CHECK_EQ_INT(pix[0], 255);
    ROBO_CHECK_EQ_INT(pix[1], 0);
    ROBO_CHECK_EQ_INT(pix[2], 0);
    ROBO_CHECK_EQ_INT(pix[3], 128);

    robo_gfx_encoded_buffer_release(&buf);
    robo_gfx_surface_release(s);
}

static robo_result_t sink_append(const void *bytes, size_t length, void *user)
{
    mem_sink_t *m = (mem_sink_t *)user;
    uint8_t *p;
    if (m->size > SIZE_MAX - length) {
        return ROBO_INVALID_ARGUMENT;
    }
    p = (uint8_t *)robo_realloc(m->data, m->size + length);
    if (!p) {
        return ROBO_OUT_OF_MEMORY;
    }
    m->data = p;
    memcpy(m->data + m->size, bytes, length);
    m->size += length;
    return ROBO_OK;
}

static void test_encode_sink_and_image(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_image_t *img = NULL;
    robo_gfx_encode_options_t opts;
    robo_gfx_encoded_buffer_t buf;
    robo_gfx_rect_t bounds;
    mem_sink_t sink;

    ROBO_CHECK(make_surface(3, 2, ROBO_GFX_FORMAT_RGB8, &s) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_clear(s, robo_gfx_rgb8(11, 22, 33)) == ROBO_OK);

    robo_gfx_encode_options_init(&opts);
    opts.layout = ROBO_GFX_ENCODE_LAYOUT_RGB8;

    memset(&sink, 0, sizeof(sink));
    ROBO_CHECK(robo_gfx_surface_pixel_bounds(s, &bounds) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_encode_to_sink(s, bounds, &opts, sink_append,
                                               &sink) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_encode(s, &opts, &buf) == ROBO_OK);
    ROBO_CHECK(sink.size == buf.size);
    ROBO_CHECK(memcmp(sink.data, buf.data, buf.size) == 0);
    robo_free(sink.data);
    robo_gfx_encoded_buffer_release(&buf);

    ROBO_CHECK(robo_gfx_image_create_from_surface(s, &img) == ROBO_OK);
    ROBO_CHECK(robo_gfx_image_encode(img, &opts, &buf) == ROBO_OK);
    ROBO_CHECK(buf.size > 0u);
    robo_gfx_encoded_buffer_release(&buf);
    robo_gfx_image_release(img);
    robo_gfx_surface_release(s);
}

static void test_encode_formats_and_safety(void)
{
    robo_gfx_surface_t *bgra = NULL;
    robo_gfx_surface_t *gray = NULL;
    robo_gfx_encode_options_t opts;
    robo_gfx_encoded_buffer_t buf;
    uint8_t pixels[32];
    size_t plen = 0u;
    uint32_t w = 0u;
    uint32_t h = 0u;
    int rgba = 0;

    ROBO_CHECK(make_surface(1, 1, ROBO_GFX_FORMAT_BGRA8, &bgra) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(bgra, 0, 0,
                                          robo_gfx_rgba8(1, 2, 3, 4)) ==
               ROBO_OK);
    robo_gfx_encode_options_init(&opts);
    ROBO_CHECK(robo_gfx_surface_encode(bgra, &opts, &buf) == ROBO_OK);
    ROBO_CHECK(png_decode_pixels(buf.data, buf.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    expect_rgba(pixels, 1, 2, 3, 4);
    robo_gfx_encoded_buffer_release(&buf);
    robo_gfx_surface_release(bgra);

    ROBO_CHECK(make_surface(1, 1, ROBO_GFX_FORMAT_GRAY8, &gray) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_set_pixel(gray, 0, 0,
                                          robo_gfx_rgba8(100, 100, 100, 255)) ==
               ROBO_OK);
    opts.layout = ROBO_GFX_ENCODE_LAYOUT_RGB8;
    ROBO_CHECK(robo_gfx_surface_encode(gray, &opts, &buf) == ROBO_OK);
    ROBO_CHECK(png_decode_pixels(buf.data, buf.size, &w, &h, &rgba, pixels,
                                 sizeof(pixels), &plen));
    ROBO_CHECK_EQ_INT(rgba, 0);
    expect_rgb(pixels, 100, 100, 100);
    robo_gfx_encoded_buffer_release(&buf);

    ROBO_CHECK(robo_gfx_surface_encode(NULL, &opts, &buf) ==
               ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(robo_gfx_surface_encode(gray, NULL, &buf) ==
               ROBO_INVALID_ARGUMENT);
    robo_gfx_surface_release(gray);
}

static void test_encode_iend_crc(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_encode_options_t opts;
    robo_gfx_encoded_buffer_t buf;
    size_t off;

    ROBO_CHECK(make_surface(1, 1, ROBO_GFX_FORMAT_RGBA8, &s) == ROBO_OK);
    robo_gfx_encode_options_init(&opts);
    ROBO_CHECK(robo_gfx_surface_encode(s, &opts, &buf) == ROBO_OK);
    /* Last chunk IEND CRC is fixed AE426082 */
    ROBO_CHECK(buf.size >= 12u);
    off = buf.size - 8u;
    ROBO_CHECK(memcmp(buf.data + off, "IEND", 4u) == 0);
    ROBO_CHECK_EQ_INT(buf.data[buf.size - 4], 0xAE);
    ROBO_CHECK_EQ_INT(buf.data[buf.size - 3], 0x42);
    ROBO_CHECK_EQ_INT(buf.data[buf.size - 2], 0x60);
    ROBO_CHECK_EQ_INT(buf.data[buf.size - 1], 0x82);
    robo_gfx_encoded_buffer_release(&buf);
    robo_gfx_surface_release(s);
}

static void test_encode_larger_and_stride(void)
{
    robo_gfx_surface_t *s = NULL;
    robo_gfx_surface_desc_t desc;
    robo_gfx_encode_options_t opts;
    robo_gfx_encoded_buffer_t a;
    robo_gfx_encoded_buffer_t b;
    uint32_t x;
    uint32_t y;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 17;
    desc.height = 13;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.stride_bytes = 17u * 4u + 8u; /* padded stride */
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK(robo_gfx_surface_create(&desc, &s) == ROBO_OK);
    for (y = 0u; y < 13u; y++) {
        for (x = 0u; x < 17u; x++) {
            ROBO_CHECK(robo_gfx_surface_set_pixel(
                           s, x, y,
                           robo_gfx_rgba8((uint8_t)x, (uint8_t)y, 50,
                                          200)) == ROBO_OK);
        }
    }
    robo_gfx_encode_options_init(&opts);
    ROBO_CHECK(robo_gfx_surface_encode(s, &opts, &a) == ROBO_OK);
    ROBO_CHECK(robo_gfx_surface_encode(s, &opts, &b) == ROBO_OK);
    ROBO_CHECK(a.size == b.size);
    ROBO_CHECK(memcmp(a.data, b.data, a.size) == 0);
    {
        uint8_t pixels[17 * 13 * 4];
        size_t plen = 0u;
        uint32_t w = 0u;
        uint32_t h = 0u;
        int rgba = 0;
        ROBO_CHECK(png_decode_pixels(a.data, a.size, &w, &h, &rgba, pixels,
                                     sizeof(pixels), &plen));
        ROBO_CHECK_EQ_INT(w, 17);
        ROBO_CHECK_EQ_INT(h, 13);
        expect_rgba(pixels + (12 * 17 + 16) * 4, 16, 12, 50, 200);
    }
    robo_gfx_encoded_buffer_release(&a);
    robo_gfx_encoded_buffer_release(&b);
    robo_gfx_surface_release(s);
}

void robo_test_register_graphics_encode(void)
{
    robo_test_register("graphics.encode.png_1x1", test_encode_png_1x1_rgba);
    robo_test_register("graphics.encode.png_rgb_alpha_policy",
                       test_encode_png_rgb_and_alpha_policy);
    robo_test_register("graphics.encode.png_channels", test_encode_png_channels);
    robo_test_register("graphics.encode.region", test_encode_region);
    robo_test_register("graphics.encode.bmp", test_encode_bmp_24_and_32);
    robo_test_register("graphics.encode.sink_image",
                       test_encode_sink_and_image);
    robo_test_register("graphics.encode.formats_safety",
                       test_encode_formats_and_safety);
    robo_test_register("graphics.encode.iend_crc", test_encode_iend_crc);
    robo_test_register("graphics.encode.stride_large",
                       test_encode_larger_and_stride);
}
