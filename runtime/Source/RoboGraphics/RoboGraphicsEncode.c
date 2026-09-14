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
} encode_buf_t;

static void buf_init(encode_buf_t *b)
{
    b->data = NULL;
    b->size = 0u;
    b->cap = 0u;
}

static void buf_free(encode_buf_t *b)
{
    if (b->data) {
        robo_free(b->data);
    }
    buf_init(b);
}

static robo_result_t buf_reserve(encode_buf_t *b, size_t need)
{
    size_t ncap;
    uint8_t *p;

    if (need <= b->cap) {
        return ROBO_OK;
    }
    ncap = b->cap ? b->cap : 256u;
    while (ncap < need) {
        if (ncap > (SIZE_MAX / 2u)) {
            return ROBO_OUT_OF_MEMORY;
        }
        ncap *= 2u;
    }
    p = (uint8_t *)robo_realloc(b->data, ncap);
    if (!p) {
        return ROBO_OUT_OF_MEMORY;
    }
    b->data = p;
    b->cap = ncap;
    return ROBO_OK;
}

static robo_result_t buf_append(encode_buf_t *b, const void *src, size_t n)
{
    robo_result_t res;

    if (n == 0u) {
        return ROBO_OK;
    }
    if (b->size > SIZE_MAX - n) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = buf_reserve(b, b->size + n);
    if (res != ROBO_OK) {
        return res;
    }
    memcpy(b->data + b->size, src, n);
    b->size += n;
    return ROBO_OK;
}

static robo_result_t buf_append_u8(encode_buf_t *b, uint8_t v)
{
    return buf_append(b, &v, 1u);
}

static robo_result_t buf_append_u16le(encode_buf_t *b, uint16_t v)
{
    uint8_t p[2];
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    return buf_append(b, p, 2u);
}

static robo_result_t buf_append_u32le(encode_buf_t *b, uint32_t v)
{
    uint8_t p[4];
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
    return buf_append(b, p, 4u);
}

static robo_result_t buf_append_u32be(encode_buf_t *b, uint32_t v)
{
    uint8_t p[4];
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
    return buf_append(b, p, 4u);
}

static uint32_t png_crc32(const uint8_t *data, size_t len)
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

static int source_stores_alpha(robo_gfx_pixel_format_t fmt)
{
    robo_gfx_pixel_format_info_t info;
    if (robo_gfx_pixel_format_info(fmt, &info) != ROBO_OK) {
        return 0;
    }
    return info.alpha_stored ? 1 : 0;
}

static int opts_valid(const robo_gfx_encode_options_t *opts,
                      robo_gfx_pixel_format_t src_fmt)
{
    if (!opts) {
        return 0;
    }
    if (opts->container != ROBO_GFX_ENCODE_PNG &&
        opts->container != ROBO_GFX_ENCODE_BMP) {
        return 0;
    }
    if (opts->layout != ROBO_GFX_ENCODE_LAYOUT_RGBA8 &&
        opts->layout != ROBO_GFX_ENCODE_LAYOUT_RGB8) {
        return 0;
    }
    if (opts->layout == ROBO_GFX_ENCODE_LAYOUT_RGB8 &&
        source_stores_alpha(src_fmt) && !opts->background_set) {
        return 0;
    }
    return 1;
}

static robo_gfx_color_rgba8_t sample_pixel(const robo_gfx_surface_t *surface,
                                           uint32_t x,
                                           uint32_t y,
                                           const robo_gfx_encode_options_t *opts)
{
    robo_gfx_color_rgba8_t c = robo_gfx_rgba8(0, 0, 0, 0);
    if (robo_gfx_surface_get_pixel(surface, x, y, &c) != ROBO_OK) {
        return robo_gfx_rgba8(0, 0, 0, 0);
    }
    if (opts->layout == ROBO_GFX_ENCODE_LAYOUT_RGB8) {
        if (source_stores_alpha(robo_gfx_surface_format(surface))) {
            c = robo_gfx_composite_src_over(c, opts->background);
        }
        c.a = 255u;
    }
    return c;
}

static robo_result_t mul_size(size_t a, size_t b, size_t *out)
{
    if (a != 0u && b > SIZE_MAX / a) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = a * b;
    return ROBO_OK;
}

static robo_result_t add_size(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = a + b;
    return ROBO_OK;
}

static robo_result_t region_to_u32(robo_gfx_rect_t region,
                                   uint32_t *ox,
                                   uint32_t *oy,
                                   uint32_t *w,
                                   uint32_t *h)
{
    if (robo_gfx_rect_is_empty(region) || region.width < 0 ||
        region.height < 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (region.x < 0 || region.y < 0) {
        return ROBO_INVALID_ARGUMENT;
    }
    *ox = (uint32_t)region.x;
    *oy = (uint32_t)region.y;
    *w = (uint32_t)region.width;
    *h = (uint32_t)region.height;
    return ROBO_OK;
}

static int rect_inside_surface(robo_gfx_rect_t rect,
                               const robo_gfx_surface_t *surface)
{
    robo_gfx_rect_t bounds;
    int64_t rr;
    int64_t rb;

    if (robo_gfx_surface_pixel_bounds(surface, &bounds) != ROBO_OK) {
        return 0;
    }
    if (robo_gfx_rect_is_empty(rect) || rect.width < 0 || rect.height < 0) {
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

static robo_result_t png_append_chunk(encode_buf_t *out,
                                      const char type[4],
                                      const uint8_t *payload,
                                      size_t payload_len)
{
    uint8_t type_bytes[4];
    encode_buf_t crc_src;
    uint32_t crc;
    robo_result_t res;

    type_bytes[0] = (uint8_t)type[0];
    type_bytes[1] = (uint8_t)type[1];
    type_bytes[2] = (uint8_t)type[2];
    type_bytes[3] = (uint8_t)type[3];

    res = buf_append_u32be(out, (uint32_t)payload_len);
    if (res != ROBO_OK) {
        return res;
    }
    res = buf_append(out, type_bytes, 4u);
    if (res != ROBO_OK) {
        return res;
    }
    if (payload_len > 0u) {
        res = buf_append(out, payload, payload_len);
        if (res != ROBO_OK) {
            return res;
        }
    }

    buf_init(&crc_src);
    res = buf_append(&crc_src, type_bytes, 4u);
    if (res == ROBO_OK && payload_len > 0u) {
        res = buf_append(&crc_src, payload, payload_len);
    }
    if (res != ROBO_OK) {
        buf_free(&crc_src);
        return res;
    }
    crc = png_crc32(crc_src.data, crc_src.size);
    buf_free(&crc_src);
    return buf_append_u32be(out, crc);
}

static robo_result_t zlib_store(encode_buf_t *out,
                                const uint8_t *raw,
                                size_t raw_len)
{
    const size_t max_block = 65535u;
    size_t off = 0u;
    robo_result_t res;
    uint32_t ad;

    /* CMF/FLG: CM=8, CINFO=7, FLEVEL=0 → 0x78 0x01 */
    res = buf_append_u8(out, 0x78u);
    if (res != ROBO_OK) {
        return res;
    }
    res = buf_append_u8(out, 0x01u);
    if (res != ROBO_OK) {
        return res;
    }

    if (raw_len == 0u) {
        res = buf_append_u8(out, 0x01u); /* final empty store */
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u16le(out, 0u);
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u16le(out, 0xFFFFu);
        if (res != ROBO_OK) {
            return res;
        }
    } else {
        while (off < raw_len) {
            size_t chunk = raw_len - off;
            int last;
            if (chunk > max_block) {
                chunk = max_block;
            }
            last = (off + chunk >= raw_len) ? 1 : 0;
            res = buf_append_u8(out, last ? 0x01u : 0x00u);
            if (res != ROBO_OK) {
                return res;
            }
            res = buf_append_u16le(out, (uint16_t)chunk);
            if (res != ROBO_OK) {
                return res;
            }
            res = buf_append_u16le(out, (uint16_t)(0xFFFFu ^ (uint16_t)chunk));
            if (res != ROBO_OK) {
                return res;
            }
            res = buf_append(out, raw + off, chunk);
            if (res != ROBO_OK) {
                return res;
            }
            off += chunk;
        }
    }

    ad = adler32(raw, raw_len);
    return buf_append_u32be(out, ad);
}

static robo_result_t build_filtered_rgba_rgb(
    const robo_gfx_surface_t *surface,
    uint32_t ox,
    uint32_t oy,
    uint32_t w,
    uint32_t h,
    const robo_gfx_encode_options_t *opts,
    encode_buf_t *raw)
{
    uint32_t y;
    uint32_t x;
    uint8_t bpp = (opts->layout == ROBO_GFX_ENCODE_LAYOUT_RGBA8) ? 4u : 3u;
    size_t row_payload;
    size_t filtered_row;
    size_t total;
    robo_result_t res;

    res = mul_size((size_t)w, (size_t)bpp, &row_payload);
    if (res != ROBO_OK) {
        return res;
    }
    res = add_size(row_payload, 1u, &filtered_row);
    if (res != ROBO_OK) {
        return res;
    }
    res = mul_size(filtered_row, (size_t)h, &total);
    if (res != ROBO_OK) {
        return res;
    }

    res = buf_reserve(raw, total);
    if (res != ROBO_OK) {
        return res;
    }

    for (y = 0u; y < h; y++) {
        res = buf_append_u8(raw, 0u); /* filter None */
        if (res != ROBO_OK) {
            return res;
        }
        for (x = 0u; x < w; x++) {
            robo_gfx_color_rgba8_t c =
                sample_pixel(surface, ox + x, oy + y, opts);
            uint8_t px[4];
            px[0] = c.r;
            px[1] = c.g;
            px[2] = c.b;
            if (bpp == 4u) {
                px[3] = c.a;
                res = buf_append(raw, px, 4u);
            } else {
                res = buf_append(raw, px, 3u);
            }
            if (res != ROBO_OK) {
                return res;
            }
        }
    }
    return ROBO_OK;
}

static robo_result_t encode_png(const robo_gfx_surface_t *surface,
                                uint32_t ox,
                                uint32_t oy,
                                uint32_t w,
                                uint32_t h,
                                const robo_gfx_encode_options_t *opts,
                                encode_buf_t *out)
{
    static const uint8_t sig[8] = {
        0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    uint8_t ihdr[13];
    encode_buf_t raw;
    encode_buf_t zlib;
    robo_result_t res;
    uint8_t color_type;

    if (w == 0u || h == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }

    color_type = (opts->layout == ROBO_GFX_ENCODE_LAYOUT_RGBA8) ? 6u : 2u;

    ihdr[0] = (uint8_t)((w >> 24) & 0xFFu);
    ihdr[1] = (uint8_t)((w >> 16) & 0xFFu);
    ihdr[2] = (uint8_t)((w >> 8) & 0xFFu);
    ihdr[3] = (uint8_t)(w & 0xFFu);
    ihdr[4] = (uint8_t)((h >> 24) & 0xFFu);
    ihdr[5] = (uint8_t)((h >> 16) & 0xFFu);
    ihdr[6] = (uint8_t)((h >> 8) & 0xFFu);
    ihdr[7] = (uint8_t)(h & 0xFFu);
    ihdr[8] = 8u; /* bit depth */
    ihdr[9] = color_type;
    ihdr[10] = 0u;
    ihdr[11] = 0u;
    ihdr[12] = 0u;

    res = buf_append(out, sig, 8u);
    if (res != ROBO_OK) {
        return res;
    }
    res = png_append_chunk(out, "IHDR", ihdr, 13u);
    if (res != ROBO_OK) {
        return res;
    }

    buf_init(&raw);
    buf_init(&zlib);
    res = build_filtered_rgba_rgb(surface, ox, oy, w, h, opts, &raw);
    if (res == ROBO_OK) {
        res = zlib_store(&zlib, raw.data, raw.size);
    }
    buf_free(&raw);
    if (res == ROBO_OK) {
        res = png_append_chunk(out, "IDAT", zlib.data, zlib.size);
    }
    buf_free(&zlib);
    if (res != ROBO_OK) {
        return res;
    }
    return png_append_chunk(out, "IEND", NULL, 0u);
}

static size_t bmp_row_stride_24(uint32_t w)
{
    /* 3*w padded to multiple of 4 */
    size_t row = (size_t)w * 3u;
    return (row + 3u) & ~(size_t)3u;
}

static size_t bmp_row_stride_32(uint32_t w)
{
    return (size_t)w * 4u; /* already multiple of 4 */
}

static robo_result_t encode_bmp(const robo_gfx_surface_t *surface,
                                uint32_t ox,
                                uint32_t oy,
                                uint32_t w,
                                uint32_t h,
                                const robo_gfx_encode_options_t *opts,
                                encode_buf_t *out)
{
    int with_alpha = (opts->layout == ROBO_GFX_ENCODE_LAYOUT_RGBA8) ? 1 : 0;
    uint32_t dib_size = with_alpha ? 108u : 40u;
    uint32_t bpp = with_alpha ? 32u : 24u;
    size_t row_stride;
    size_t pixel_bytes;
    size_t file_size;
    uint32_t off_bits;
    uint32_t y;
    uint32_t x;
    robo_result_t res;

    if (w == 0u || h == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }

    if (with_alpha) {
        row_stride = bmp_row_stride_32(w);
    } else {
        row_stride = bmp_row_stride_24(w);
    }
    res = mul_size(row_stride, (size_t)h, &pixel_bytes);
    if (res != ROBO_OK) {
        return res;
    }
    off_bits = 14u + dib_size;
    res = add_size((size_t)off_bits, pixel_bytes, &file_size);
    if (res != ROBO_OK) {
        return res;
    }
    if (file_size > 0xFFFFFFFFu) {
        return ROBO_INVALID_ARGUMENT;
    }

    /* BITMAPFILEHEADER */
    res = buf_append_u8(out, 'B');
    if (res != ROBO_OK) {
        return res;
    }
    res = buf_append_u8(out, 'M');
    if (res != ROBO_OK) {
        return res;
    }
    res = buf_append_u32le(out, (uint32_t)file_size);
    if (res != ROBO_OK) {
        return res;
    }
    res = buf_append_u16le(out, 0u);
    if (res != ROBO_OK) {
        return res;
    }
    res = buf_append_u16le(out, 0u);
    if (res != ROBO_OK) {
        return res;
    }
    res = buf_append_u32le(out, off_bits);
    if (res != ROBO_OK) {
        return res;
    }

    /* DIB header */
    res = buf_append_u32le(out, dib_size);
    if (res != ROBO_OK) {
        return res;
    }
    res = buf_append_u32le(out, w);
    if (res != ROBO_OK) {
        return res;
    }
    res = buf_append_u32le(out, h); /* bottom-up */
    if (res != ROBO_OK) {
        return res;
    }
    res = buf_append_u16le(out, 1u); /* planes */
    if (res != ROBO_OK) {
        return res;
    }
    res = buf_append_u16le(out, (uint16_t)bpp);
    if (res != ROBO_OK) {
        return res;
    }
    if (with_alpha) {
        /* BI_BITFIELDS */
        res = buf_append_u32le(out, 3u);
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, (uint32_t)pixel_bytes);
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, 2835u); /* ~72 DPI */
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, 2835u);
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, 0u);
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, 0u);
        if (res != ROBO_OK) {
            return res;
        }
        /* BGRA masks */
        res = buf_append_u32le(out, 0x00FF0000u); /* R */
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, 0x0000FF00u); /* G */
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, 0x000000FFu); /* B */
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, 0xFF000000u); /* A */
        if (res != ROBO_OK) {
            return res;
        }
        /* CSType = LCS_WINDOWS_COLOR_SPACE 'Win ' */
        res = buf_append_u32le(out, 0x57696E20u);
        if (res != ROBO_OK) {
            return res;
        }
        /* endpoints + gamma (36 + 12 = 48 bytes zeros) */
        {
            uint8_t zeros[48];
            memset(zeros, 0, sizeof(zeros));
            res = buf_append(out, zeros, sizeof(zeros));
            if (res != ROBO_OK) {
                return res;
            }
        }
    } else {
        res = buf_append_u32le(out, 0u); /* BI_RGB */
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, (uint32_t)pixel_bytes);
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, 2835u);
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, 2835u);
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, 0u);
        if (res != ROBO_OK) {
            return res;
        }
        res = buf_append_u32le(out, 0u);
        if (res != ROBO_OK) {
            return res;
        }
    }

    /* Pixel data bottom-up */
    for (y = 0u; y < h; y++) {
        uint32_t src_y = oy + (h - 1u - y);
        size_t written = 0u;
        for (x = 0u; x < w; x++) {
            robo_gfx_color_rgba8_t c =
                sample_pixel(surface, ox + x, src_y, opts);
            if (with_alpha) {
                uint8_t px[4];
                px[0] = c.b;
                px[1] = c.g;
                px[2] = c.r;
                px[3] = c.a;
                res = buf_append(out, px, 4u);
                written += 4u;
            } else {
                uint8_t px[3];
                px[0] = c.b;
                px[1] = c.g;
                px[2] = c.r;
                res = buf_append(out, px, 3u);
                written += 3u;
            }
            if (res != ROBO_OK) {
                return res;
            }
        }
        while (written < row_stride) {
            res = buf_append_u8(out, 0u);
            if (res != ROBO_OK) {
                return res;
            }
            written++;
        }
    }
    return ROBO_OK;
}

static robo_result_t encode_region(const robo_gfx_surface_t *surface,
                                   robo_gfx_rect_t region,
                                   const robo_gfx_encode_options_t *opts,
                                   robo_gfx_encoded_buffer_t *out)
{
    uint32_t ox;
    uint32_t oy;
    uint32_t w;
    uint32_t h;
    encode_buf_t buf;
    robo_result_t res;

    if (!surface || !opts || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    out->data = NULL;
    out->size = 0u;

    if (!robo_gfx_pixel_format_is_implemented(robo_gfx_surface_format(surface))) {
        return ROBO_NOT_SUPPORTED;
    }
    if (!opts_valid(opts, robo_gfx_surface_format(surface))) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!rect_inside_surface(region, surface)) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = region_to_u32(region, &ox, &oy, &w, &h);
    if (res != ROBO_OK) {
        return res;
    }

    buf_init(&buf);
    if (opts->container == ROBO_GFX_ENCODE_PNG) {
        res = encode_png(surface, ox, oy, w, h, opts, &buf);
    } else {
        res = encode_bmp(surface, ox, oy, w, h, opts, &buf);
    }
    if (res != ROBO_OK) {
        buf_free(&buf);
        return res;
    }
    out->data = buf.data;
    out->size = buf.size;
    return ROBO_OK;
}

void robo_gfx_encode_options_init(robo_gfx_encode_options_t *opts)
{
    if (!opts) {
        return;
    }
    memset(opts, 0, sizeof(*opts));
    opts->container = ROBO_GFX_ENCODE_PNG;
    opts->layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;
    opts->background_set = 0;
    opts->background = robo_gfx_rgb8(0, 0, 0);
}

void robo_gfx_encoded_buffer_release(robo_gfx_encoded_buffer_t *buf)
{
    if (!buf) {
        return;
    }
    if (buf->data) {
        robo_free(buf->data);
    }
    buf->data = NULL;
    buf->size = 0u;
}

robo_result_t robo_gfx_surface_encode(const robo_gfx_surface_t *surface,
                                      const robo_gfx_encode_options_t *opts,
                                      robo_gfx_encoded_buffer_t *out)
{
    robo_gfx_rect_t bounds;
    robo_result_t res;

    if (!surface || !opts || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_gfx_surface_pixel_bounds(surface, &bounds);
    if (res != ROBO_OK) {
        return res;
    }
    return encode_region(surface, bounds, opts, out);
}

robo_result_t robo_gfx_surface_encode_rect(const robo_gfx_surface_t *surface,
                                           robo_gfx_rect_t region,
                                           const robo_gfx_encode_options_t *opts,
                                           robo_gfx_encoded_buffer_t *out)
{
    return encode_region(surface, region, opts, out);
}

robo_result_t robo_gfx_image_encode(robo_gfx_image_t *image,
                                    const robo_gfx_encode_options_t *opts,
                                    robo_gfx_encoded_buffer_t *out)
{
    robo_gfx_surface_t *surf;

    if (!image || !opts || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (robo_gfx_image_kind(image) != ROBO_GFX_IMAGE_KIND_RASTER) {
        return ROBO_INVALID_ARGUMENT;
    }
    surf = robo_gfx_image_surface(image);
    if (!surf) {
        return ROBO_INVALID_ARGUMENT;
    }
    return robo_gfx_surface_encode(surf, opts, out);
}

robo_result_t robo_gfx_surface_encode_to_sink(
    const robo_gfx_surface_t *surface,
    robo_gfx_rect_t region,
    const robo_gfx_encode_options_t *opts,
    robo_gfx_encode_sink_fn sink,
    void *user)
{
    robo_gfx_encoded_buffer_t buf;
    robo_result_t res;

    if (!sink) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = encode_region(surface, region, opts, &buf);
    if (res != ROBO_OK) {
        return res;
    }
    res = sink(buf.data, buf.size, user);
    robo_gfx_encoded_buffer_release(&buf);
    return res;
}
