/*
 * RoboGraphics Phase 8.12 — PNG decoding.
 *
 * Structure, in the order the bytes arrive:
 *
 *   probe    signature only, never allocates
 *   header   chunk walk; validates every CRC; fills dimension, colour model
 *            and palette facts; concatenates IDAT
 *   inflate  zlib → DEFLATE, all three block kinds, into an exactly sized
 *            scanline buffer
 *   unfilter the five PNG row filters
 *   pack     samples → RGBA8 into a surface, then an image that retains it
 *   to RGBA8
 *
 * Two properties hold throughout:
 *
 *   1. Nothing is allocated from a number the file supplied until that
 *      number has been checked against the limits *and* against overflow.
 *      `png_expected_raw_size` is the single place the scanline size is
 *      computed, and it refuses rather than wrapping.
 *   2. The DEFLATE stream must produce exactly the expected number of bytes.
 *      Short is truncation, long is malformation; neither is guessed at.
 */

#include "RoboGraphics/RoboGraphicsDecode.h"

#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

/* ------------------------------------------------------------------ */
/* Container identity                                                  */
/* ------------------------------------------------------------------ */

static const uint8_t PNG_SIGNATURE[8] = {0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au,
                                         0x1Au, 0x0Au};

#define PNG_COLOR_GRAY 0u
#define PNG_COLOR_RGB 2u
#define PNG_COLOR_PALETTE 3u
#define PNG_COLOR_GRAY_ALPHA 4u
#define PNG_COLOR_RGBA 6u

const char *robo_gfx_decode_container_name(robo_gfx_decode_container_t c)
{
    switch (c) {
    case ROBO_GFX_DECODE_CONTAINER_PNG:
        return "png";
    case ROBO_GFX_DECODE_CONTAINER_UNKNOWN:
    default:
        return "unknown";
    }
}

int robo_gfx_decode_container_is_known(robo_gfx_decode_container_t c)
{
    return c == ROBO_GFX_DECODE_CONTAINER_PNG;
}

void robo_gfx_decode_limits_init(robo_gfx_decode_limits_t *limits)
{
    if (!limits) {
        return;
    }
    limits->max_dimension = ROBO_GFX_DECODE_DEFAULT_MAX_DIMENSION;
    limits->max_decoded_bytes = ROBO_GFX_DECODE_DEFAULT_MAX_DECODED_BYTES;
}

robo_gfx_decode_container_t robo_gfx_decode_probe(const void *bytes,
                                                  size_t length)
{
    if (!bytes || length < sizeof(PNG_SIGNATURE)) {
        return ROBO_GFX_DECODE_CONTAINER_UNKNOWN;
    }
    if (memcmp(bytes, PNG_SIGNATURE, sizeof(PNG_SIGNATURE)) == 0) {
        return ROBO_GFX_DECODE_CONTAINER_PNG;
    }
    return ROBO_GFX_DECODE_CONTAINER_UNKNOWN;
}

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t read_be16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
}

/*
 * Bitwise CRC-32 (IEEE 802.3, the PNG polynomial). No table: a chunk stream
 * is short and a 256-entry table is 1 KiB of rodata this decoder does not
 * need to own, since CRC cost is linear in image bytes and dominated by
 * DEFLATE anyway.
 */
static uint32_t png_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    int b;

    for (i = 0u; i < length; i++) {
        crc ^= (uint32_t)data[i];
        for (b = 0; b < 8; b++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* Header state                                                        */
/* ------------------------------------------------------------------ */

typedef struct png_header {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;
    uint8_t color_type;
    uint8_t interlace;
    uint8_t samples_per_pixel;

    uint8_t palette[256 * 3];
    uint32_t palette_entries;

    /* tRNS, normalised to "is every sample of this value full transparent". */
    int trns_present;
    uint8_t trns_gray;
    uint8_t trns_rgb[3];
    uint8_t trns_palette[256];
    uint32_t trns_palette_entries;

    uint8_t *idat;
    size_t idat_size;
    size_t idat_capacity;
} png_header_t;

static uint8_t png_channels_for_color_type(uint8_t color_type)
{
    switch (color_type) {
    case PNG_COLOR_GRAY:
        return 1u;
    case PNG_COLOR_RGB:
        return 3u;
    case PNG_COLOR_PALETTE:
        return 1u;
    case PNG_COLOR_GRAY_ALPHA:
        return 2u;
    case PNG_COLOR_RGBA:
        return 4u;
    default:
        return 0u;
    }
}

/*
 * One sample is one byte, which is why bit depth 8 is the only depth this
 * decoder accepts: it makes bytes-per-pixel equal samples-per-pixel and
 * removes every shift-and-mask from the unpack path.
 */
static size_t png_bytes_per_pixel(const png_header_t *h)
{
    return (size_t)h->samples_per_pixel;
}

/**
 * Scanline bytes: every row is one filter-type byte plus width * bpp.
 * Overflow is checked here and only here, so no later arithmetic can wrap.
 */
static robo_result_t png_expected_raw_size(const png_header_t *h,
                                           size_t *out)
{
    size_t row;
    size_t total;
    size_t bpp = png_bytes_per_pixel(h);

    if (h->width == 0u || h->height == 0u) {
        return ROBO_ERROR;
    }
    if (bpp == 0u) {
        return ROBO_NOT_SUPPORTED;
    }
    if ((size_t)h->width > (SIZE_MAX - 1u) / bpp) {
        return ROBO_ERROR;
    }
    row = (size_t)h->width * bpp + 1u;
    if ((size_t)h->height > SIZE_MAX / row) {
        return ROBO_ERROR;
    }
    total = row * (size_t)h->height;
    *out = total;
    return ROBO_OK;
}

static robo_result_t png_reserve_idat(png_header_t *h, size_t extra, size_t cap)
{
    size_t need;
    size_t next;
    uint8_t *grown;

    if (extra > SIZE_MAX - h->idat_size) {
        return ROBO_ERROR;
    }
    need = h->idat_size + extra;
    if (need <= h->idat_capacity) {
        return ROBO_OK;
    }
    if (cap != 0u && need > cap) {
        /* The IDAT stream alone already exceeds the decoded budget. */
        return ROBO_ERROR;
    }
    next = h->idat_capacity ? h->idat_capacity : 1024u;
    while (next < need) {
        if (next > SIZE_MAX / 2u) {
            next = need;
            break;
        }
        next *= 2u;
    }
    grown = (uint8_t *)robo_realloc(h->idat, next);
    if (!grown) {
        return ROBO_OUT_OF_MEMORY;
    }
    h->idat = grown;
    h->idat_capacity = next;
    return ROBO_OK;
}

static void png_header_free(png_header_t *h)
{
    if (h->idat) {
        robo_free(h->idat);
    }
    h->idat = NULL;
    h->idat_size = 0u;
    h->idat_capacity = 0u;
}

/* ------------------------------------------------------------------ */
/* Chunk walk                                                          */
/* ------------------------------------------------------------------ */

#define PNG_CHUNK_TYPE(a, b, c, d)                                           \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) |   \
     (uint32_t)(d))

#define PNG_TYPE_IHDR PNG_CHUNK_TYPE('I', 'H', 'D', 'R')
#define PNG_TYPE_PLTE PNG_CHUNK_TYPE('P', 'L', 'T', 'E')
#define PNG_TYPE_TRNS PNG_CHUNK_TYPE('t', 'R', 'N', 'S')
#define PNG_TYPE_IDAT PNG_CHUNK_TYPE('I', 'D', 'A', 'T')
#define PNG_TYPE_IEND PNG_CHUNK_TYPE('I', 'E', 'N', 'D')

static int png_type_is_critical(uint32_t type)
{
    /* Ancillary chunks have bit 5 of the first type byte set (lowercase). */
    return (type & 0x20000000u) == 0u;
}

static robo_result_t png_parse_ihdr(png_header_t *h, const uint8_t *data,
                                    size_t length)
{
    uint8_t color_type;
    uint8_t bit_depth;
    uint8_t channels;

    if (length != 13u) {
        return ROBO_ERROR;
    }
    h->width = read_be32(data);
    h->height = read_be32(data + 4u);
    bit_depth = data[8];
    color_type = data[9];

    /* compression method 0 and filter method 0 are the only defined ones. */
    if (data[10] != 0u || data[11] != 0u) {
        return ROBO_NOT_SUPPORTED;
    }
    h->interlace = data[12];
    if (h->interlace > 1u) {
        return ROBO_ERROR;
    }
    if (h->interlace == 1u) {
        /* Adam7 is refused rather than approximated; see the header docs. */
        return ROBO_NOT_SUPPORTED;
    }
    if (h->width == 0u || h->height == 0u) {
        return ROBO_ERROR;
    }
    if (bit_depth != 8u) {
        return ROBO_NOT_SUPPORTED;
    }
    channels = png_channels_for_color_type(color_type);
    if (channels == 0u) {
        return ROBO_NOT_SUPPORTED;
    }

    h->bit_depth = bit_depth;
    h->color_type = color_type;
    h->samples_per_pixel = channels;
    return ROBO_OK;
}

static robo_result_t png_parse_plte(png_header_t *h, const uint8_t *data,
                                    size_t length)
{
    uint32_t entries;

    if (h->color_type != PNG_COLOR_PALETTE) {
        return ROBO_ERROR; /* PLTE is only meaningful for colour type 3 */
    }
    if (length == 0u || (length % 3u) != 0u || length > sizeof(h->palette)) {
        return ROBO_ERROR;
    }
    entries = (uint32_t)(length / 3u);
    memcpy(h->palette, data, length);
    h->palette_entries = entries;
    return ROBO_OK;
}

static robo_result_t png_parse_trns(png_header_t *h, const uint8_t *data,
                                    size_t length)
{
    switch (h->color_type) {
    case PNG_COLOR_PALETTE:
        if (h->palette_entries == 0u || length > h->palette_entries) {
            return ROBO_ERROR;
        }
        if (length == 0u) {
            return ROBO_ERROR;
        }
        memcpy(h->trns_palette, data, length);
        h->trns_palette_entries = (uint32_t)length;
        h->trns_present = 1;
        return ROBO_OK;
    case PNG_COLOR_GRAY:
        if (length != 2u) {
            return ROBO_ERROR;
        }
        /* Bit depth 8: a transparency sample above 255 can never match. */
        if (read_be16(data) > 0xFFu) {
            return ROBO_OK;
        }
        h->trns_gray = (uint8_t)read_be16(data);
        h->trns_present = 1;
        return ROBO_OK;
    case PNG_COLOR_RGB:
        if (length != 6u) {
            return ROBO_ERROR;
        }
        if (read_be16(data) > 0xFFu || read_be16(data + 2u) > 0xFFu ||
            read_be16(data + 4u) > 0xFFu) {
            return ROBO_OK;
        }
        h->trns_rgb[0] = (uint8_t)read_be16(data);
        h->trns_rgb[1] = (uint8_t)read_be16(data + 2u);
        h->trns_rgb[2] = (uint8_t)read_be16(data + 4u);
        h->trns_present = 1;
        return ROBO_OK;
    default:
        /* tRNS is not defined for colour types 4 and 6. */
        return ROBO_ERROR;
    }
}

static robo_result_t png_walk_chunks(png_header_t *h, const uint8_t *bytes,
                                     size_t length, size_t idat_cap)
{
    size_t pos = sizeof(PNG_SIGNATURE);
    int seen_ihdr = 0;
    int seen_iend = 0;

    while (pos + 12u <= length) {
        uint32_t chunk_length = read_be32(bytes + pos);
        uint32_t type = read_be32(bytes + pos + 4u);
        const uint8_t *data = bytes + pos + 8u;
        uint32_t stored_crc;
        uint32_t computed_crc;
        robo_result_t res;

        /* A length with bit 31 set is not a length this decoder will trust. */
        if (chunk_length > 0x7FFFFFFFu) {
            return ROBO_ERROR;
        }
        if ((size_t)chunk_length > length - pos - 12u) {
            return ROBO_ERROR; /* truncated chunk */
        }
        stored_crc = read_be32(data + chunk_length);
        computed_crc = png_crc32(bytes + pos + 4u, (size_t)chunk_length + 4u);
        if (stored_crc != computed_crc) {
            return ROBO_ERROR; /* corrupt chunk */
        }

        if (type == PNG_TYPE_IHDR) {
            if (seen_ihdr || pos != sizeof(PNG_SIGNATURE)) {
                return ROBO_ERROR; /* IHDR must be the first chunk, once */
            }
            res = png_parse_ihdr(h, data, chunk_length);
            if (res != ROBO_OK) {
                return res;
            }
            seen_ihdr = 1;
        } else if (type == PNG_TYPE_PLTE) {
            if (!seen_ihdr) {
                return ROBO_ERROR;
            }
            res = png_parse_plte(h, data, chunk_length);
            if (res != ROBO_OK) {
                return res;
            }
        } else if (type == PNG_TYPE_TRNS) {
            if (!seen_ihdr) {
                return ROBO_ERROR;
            }
            res = png_parse_trns(h, data, chunk_length);
            if (res != ROBO_OK) {
                return res;
            }
        } else if (type == PNG_TYPE_IDAT) {
            if (!seen_ihdr) {
                return ROBO_ERROR;
            }
            res = png_reserve_idat(h, chunk_length, idat_cap);
            if (res != ROBO_OK) {
                return res;
            }
            if (chunk_length != 0u) {
                memcpy(h->idat + h->idat_size, data, chunk_length);
                h->idat_size += chunk_length;
            }
        } else if (type == PNG_TYPE_IEND) {
            if (chunk_length != 0u) {
                return ROBO_ERROR;
            }
            seen_iend = 1;
            pos += 12u + (size_t)chunk_length;
            break;
        } else if (png_type_is_critical(type)) {
            /*
             * An unknown critical chunk means the file requires something
             * this decoder does not implement. Refusing is the honest answer:
             * skipping it could produce a picture that is quietly wrong.
             */
            return ROBO_NOT_SUPPORTED;
        } else {
            /* Ancillary chunk: skipped by design. */
        }

        pos += 12u + (size_t)chunk_length;
    }

    if (!seen_ihdr) {
        return ROBO_ERROR;
    }
    if (!seen_iend) {
        return ROBO_ERROR; /* truncated stream: no IEND */
    }
    if (h->idat_size == 0u) {
        return ROBO_ERROR; /* no image data at all */
    }
    if (h->color_type == PNG_COLOR_PALETTE && h->palette_entries == 0u) {
        return ROBO_ERROR; /* a palette image without PLTE cannot be decoded */
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* DEFLATE                                                             */
/* ------------------------------------------------------------------ */

typedef struct bit_reader {
    const uint8_t *src;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    uint32_t bitcount;
    int failed;
} bit_reader_t;

/** Read @p need bits LSB-first; returns -1 and latches failure when short. */
static int bits_read(bit_reader_t *r, int need)
{
    uint32_t value;

    while (r->bitcount < (uint32_t)need) {
        if (r->pos >= r->size) {
            r->failed = 1;
            return -1;
        }
        r->bitbuf |= (uint32_t)r->src[r->pos++] << r->bitcount;
        r->bitcount += 8u;
    }
    value = r->bitbuf & ((1u << (uint32_t)need) - 1u);
    r->bitbuf >>= (uint32_t)need;
    r->bitcount -= (uint32_t)need;
    return (int)value;
}

#define HUFF_MAX_BITS 15
#define HUFF_MAX_SYMBOLS 288

typedef struct huffman {
    uint16_t count[HUFF_MAX_BITS + 1];
    uint16_t symbol[HUFF_MAX_SYMBOLS];
} huffman_t;

/** Canonical code assignment (RFC 1951 §3.2.2). */
static void huffman_build(huffman_t *h, const uint8_t *lengths, size_t count)
{
    uint16_t offsets[HUFF_MAX_BITS + 1];
    size_t i;
    int len;

    memset(h->count, 0, sizeof(h->count));
    for (i = 0u; i < count; i++) {
        h->count[lengths[i]]++;
    }
    h->count[0] = 0u;
    offsets[0] = 0u;
    offsets[1] = 0u;
    for (len = 1; len < HUFF_MAX_BITS; len++) {
        offsets[len + 1] = (uint16_t)(offsets[len] + h->count[len]);
    }
    for (i = 0u; i < count; i++) {
        if (lengths[i] != 0u) {
            h->symbol[offsets[lengths[i]]++] = (uint16_t)i;
        }
    }
}

/** 0 when the code set is over-subscribed (unusable); 1 otherwise. */
static int huffman_usable(const huffman_t *h)
{
    int left = 1;
    int len;

    for (len = 1; len <= HUFF_MAX_BITS; len++) {
        left <<= 1;
        left -= (int)h->count[len];
        if (left < 0) {
            return 0;
        }
    }
    return 1;
}

/** Decode one symbol; -1 on a failed stream or an invalid code. */
static int huffman_decode(bit_reader_t *r, const huffman_t *h)
{
    int code = 0;
    int first = 0;
    int index = 0;
    int len;

    for (len = 1; len <= HUFF_MAX_BITS; len++) {
        int bit = bits_read(r, 1);

        if (bit < 0) {
            return -1;
        }
        code |= bit;
        if (code - first < (int)h->count[len]) {
            return (int)h->symbol[index + (code - first)];
        }
        index += (int)h->count[len];
        first = (first + (int)h->count[len]) << 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t DEFLATE_LENGTH_BASE[29] = {
    3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 13u, 15u, 17u, 19u, 23u, 27u,
    31u, 35u, 43u, 51u, 59u, 67u, 83u, 99u, 115u, 131u, 163u, 195u, 227u,
    258u};
static const uint8_t DEFLATE_LENGTH_EXTRA[29] = {0u, 0u, 0u, 0u, 0u, 0u, 0u,
                                                 0u, 1u, 1u, 1u, 1u, 2u, 2u,
                                                 2u, 2u, 3u, 3u, 3u, 3u, 4u,
                                                 4u, 4u, 4u, 5u, 5u, 5u, 5u,
                                                 0u};
static const uint16_t DEFLATE_DISTANCE_BASE[30] = {
    1u, 2u, 3u, 4u, 5u, 7u, 9u, 13u, 17u, 25u, 33u, 49u, 65u, 97u, 129u,
    193u, 257u, 385u, 513u, 769u, 1025u, 1537u, 2049u, 3073u, 4097u, 6145u,
    8193u, 12289u, 16385u, 24577u};
static const uint8_t DEFLATE_DISTANCE_EXTRA[30] = {
    0u, 0u, 0u, 0u, 1u, 1u, 2u, 2u, 3u, 3u, 4u, 4u, 5u, 5u, 6u,
    6u, 7u, 7u, 8u, 8u, 9u, 9u, 10u, 10u, 11u, 11u, 12u, 12u, 13u, 13u};

static void deflate_fixed_tables(huffman_t *literal, huffman_t *distance)
{
    uint8_t lengths[HUFF_MAX_SYMBOLS];
    int i;

    for (i = 0; i < 144; i++) {
        lengths[i] = 8u;
    }
    for (i = 144; i < 256; i++) {
        lengths[i] = 9u;
    }
    for (i = 256; i < 280; i++) {
        lengths[i] = 7u;
    }
    for (i = 280; i < 288; i++) {
        lengths[i] = 8u;
    }
    huffman_build(literal, lengths, 288u);

    for (i = 0; i < 30; i++) {
        lengths[i] = 5u;
    }
    huffman_build(distance, lengths, 30u);
}

/** Copy one length/distance pair; overlapping runs are byte-at-a-time. */
static int deflate_copy_match(uint8_t *dst, size_t capacity, size_t *out_pos,
                              unsigned length, unsigned distance)
{
    unsigned i;

    if ((size_t)distance > *out_pos) {
        return 0;
    }
    for (i = 0u; i < length; i++) {
        if (*out_pos >= capacity) {
            return 0;
        }
        dst[*out_pos] = dst[*out_pos - distance];
        (*out_pos)++;
    }
    return 1;
}

/**
 * The block body. The code tables are the caller's, built either by the
 * fixed tables or from the dynamic header — which is the only difference
 * between the two Huffman block kinds, so they share one loop.
 */
static int deflate_codes(bit_reader_t *r, const huffman_t *literal,
                         const huffman_t *distance, uint8_t *dst,
                         size_t capacity, size_t *out_pos)
{
    for (;;) {
        int symbol = huffman_decode(r, literal);

        if (symbol < 0) {
            return 0;
        }
        if (symbol < 256) {
            if (*out_pos >= capacity) {
                return 0;
            }
            dst[(*out_pos)++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 256) {
            return 1;
        }
        symbol -= 257;
        if (symbol >= 29) {
            return 0;
        }
        {
            int extra = bits_read(r, DEFLATE_LENGTH_EXTRA[symbol]);
            int dsymbol;

            if (extra < 0) {
                return 0;
            }
            {
                unsigned length =
                    (unsigned)DEFLATE_LENGTH_BASE[symbol] + (unsigned)extra;

                dsymbol = huffman_decode(r, distance);
                if (dsymbol < 0 || dsymbol >= 30) {
                    return 0;
                }
                extra = bits_read(r, DEFLATE_DISTANCE_EXTRA[dsymbol]);
                if (extra < 0) {
                    return 0;
                }
                if (!deflate_copy_match(
                        dst, capacity, out_pos, length,
                        (unsigned)DEFLATE_DISTANCE_BASE[dsymbol] +
                            (unsigned)extra)) {
                    return 0;
                }
            }
        }
    }
}

static int deflate_dynamic(bit_reader_t *r, uint8_t *dst, size_t capacity,
                           size_t *out_pos)
{
    static const uint8_t code_length_order[19] = {16u, 17u, 18u, 0u, 8u,  7u,
                                                  9u,  6u,  10u, 5u, 11u, 4u,
                                                  12u, 3u,  13u, 2u, 14u, 1u,
                                                  15u};
    uint8_t lengths[320];
    huffman_t code_length;
    huffman_t literal;
    huffman_t distance;
    int hlit;
    int hdist;
    int hclen;
    int i;
    int index;
    int total;

    hlit = bits_read(r, 5);
    hdist = bits_read(r, 5);
    hclen = bits_read(r, 4);
    if (hlit < 0 || hdist < 0 || hclen < 0) {
        return 0;
    }
    hlit += 257;
    hdist += 1;
    hclen += 4;
    total = hlit + hdist;

    memset(lengths, 0, sizeof(lengths));
    for (i = 0; i < hclen; i++) {
        int value = bits_read(r, 3);

        if (value < 0) {
            return 0;
        }
        lengths[code_length_order[i]] = (uint8_t)value;
    }
    huffman_build(&code_length, lengths, 19u);
    if (!huffman_usable(&code_length)) {
        return 0;
    }

    memset(lengths, 0, sizeof(lengths));
    index = 0;
    while (index < total) {
        int symbol = huffman_decode(r, &code_length);
        int repeat;
        int value;

        if (symbol < 0) {
            return 0;
        }
        if (symbol < 16) {
            lengths[index++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 16) {
            int extra = bits_read(r, 2);

            if (extra < 0 || index == 0) {
                return 0;
            }
            repeat = 3 + extra;
            value = lengths[index - 1];
        } else if (symbol == 17) {
            int extra = bits_read(r, 3);

            if (extra < 0) {
                return 0;
            }
            repeat = 3 + extra;
            value = 0;
        } else {
            int extra = bits_read(r, 7);

            if (extra < 0) {
                return 0;
            }
            repeat = 11 + extra;
            value = 0;
        }
        if (repeat > total - index) {
            return 0;
        }
        while (repeat-- > 0) {
            lengths[index++] = (uint8_t)value;
        }
    }

    /* RFC 1951: the literal tree must have a code for end-of-block (256). */
    if (lengths[256] == 0u) {
        return 0;
    }
    huffman_build(&literal, lengths, (size_t)hlit);
    if (!huffman_usable(&literal)) {
        return 0;
    }
    huffman_build(&distance, lengths + hlit, (size_t)hdist);
    if (!huffman_usable(&distance)) {
        return 0;
    }
    return deflate_codes(r, &literal, &distance, dst, capacity, out_pos);
}

/**
 * Inflate a zlib-wrapped DEFLATE stream into exactly @p capacity bytes.
 *
 * Exactly: a stream that produces fewer is truncated and one that wants to
 * produce more is malformed. Both are refused, so the caller never has to
 * reason about a partially filled scanline buffer.
 */
static robo_result_t inflate_exact(const uint8_t *src, size_t size,
                                   uint8_t *dst, size_t capacity)
{
    bit_reader_t reader;
    size_t produced = 0u;

    if (size < 2u) {
        return ROBO_ERROR;
    }
    {
        uint8_t cmf = src[0];
        uint8_t flg = src[1];

        /* CM = 8 (deflate), CINFO <= 7 (window <= 32 KiB), FDICT clear. */
        if ((cmf & 0x0Fu) != 8u || ((cmf >> 4) & 0x0Fu) > 7u) {
            return ROBO_ERROR;
        }
        if ((((uint32_t)cmf << 8) | (uint32_t)flg) % 31u != 0u) {
            return ROBO_ERROR;
        }
        if ((flg & 0x20u) != 0u) {
            return ROBO_NOT_SUPPORTED; /* preset dictionary */
        }
    }

    reader.src = src;
    reader.size = size;
    reader.pos = 2u;
    reader.bitbuf = 0u;
    reader.bitcount = 0u;
    reader.failed = 0;

    for (;;) {
        int final = bits_read(&reader, 1);
        int type;

        if (final < 0) {
            return ROBO_ERROR;
        }
        type = bits_read(&reader, 2);
        if (type < 0) {
            return ROBO_ERROR;
        }
        if (type == 0) {
            /* Stored: discard the partial byte, then LEN / ~LEN / raw bytes. */
            int low;
            int high;

            reader.bitbuf = 0u;
            reader.bitcount = 0u;
            low = bits_read(&reader, 16);
            high = bits_read(&reader, 16);
            if (low < 0 || high < 0) {
                return ROBO_ERROR;
            }
            if (((unsigned)low ^ 0xFFFFu) != (unsigned)high || low == 0) {
                return ROBO_ERROR;
            }
            if (reader.pos + (size_t)low > reader.size) {
                return ROBO_ERROR;
            }
            if ((size_t)low > capacity - produced) {
                return ROBO_ERROR; /* more image data than the header implies */
            }
            memcpy(dst + produced, reader.src + reader.pos, (size_t)low);
            reader.pos += (size_t)low;
            produced += (size_t)low;
        } else if (type == 1 || type == 2) {
            huffman_t literal;
            huffman_t distance;
            int ok;

            if (type == 1) {
                deflate_fixed_tables(&literal, &distance);
                ok = deflate_codes(&reader, &literal, &distance, dst, capacity,
                                   &produced);
            } else {
                ok = deflate_dynamic(&reader, dst, capacity, &produced);
            }
            if (!ok) {
                return ROBO_ERROR;
            }
        } else {
            return ROBO_ERROR; /* BTYPE 3 is reserved */
        }
        if (final != 0) {
            break;
        }
    }

    if (produced != capacity) {
        return ROBO_ERROR;
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Unfiltering                                                         */
/* ------------------------------------------------------------------ */

static uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c)
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
 * Apply the five row filters in place, turning filtered scanlines into raw
 * samples. @p raw holds the whole image, rows back to back, each preceded by
 * its filter byte.
 */
static robo_result_t png_unfilter(uint8_t *raw, uint32_t height,
                                  uint32_t width, size_t bpp)
{
    size_t row_bytes = (size_t)width * bpp;
    size_t stride = row_bytes + 1u;
    uint32_t y;

    for (y = 0u; y < height; y++) {
        uint8_t *row = raw + stride * (size_t)y;
        uint8_t filter = row[0];
        uint8_t *cur = row + 1u;
        const uint8_t *prev = (y == 0u) ? NULL : (row - stride + 1u);
        size_t i;

        for (i = 0u; i < row_bytes; i++) {
            uint8_t left = (i >= bpp) ? cur[i - bpp] : 0u;
            uint8_t up = prev ? prev[i] : 0u;
            uint8_t upleft = (prev && i >= bpp) ? prev[i - bpp] : 0u;

            switch (filter) {
            case 0u: /* None */
                break;
            case 1u: /* Sub */
                cur[i] = (uint8_t)(cur[i] + left);
                break;
            case 2u: /* Up */
                cur[i] = (uint8_t)(cur[i] + up);
                break;
            case 3u: /* Average */
                cur[i] = (uint8_t)(cur[i] +
                                   (uint8_t)(((unsigned)left + (unsigned)up) /
                                             2u));
                break;
            case 4u: /* Paeth */
                cur[i] = (uint8_t)(cur[i] +
                                   paeth_predictor(left, up, upleft));
                break;
            default:
                return ROBO_ERROR; /* filter type 5+ is not defined */
            }
        }
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Samples → RGBA8                                                     */
/* ------------------------------------------------------------------ */

/** Write one source sample group into @p dst as RGBA8. */
static void png_pixel_to_rgba(const png_header_t *h, const uint8_t *sample,
                              uint8_t *dst)
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a = 255u;

    switch (h->color_type) {
    case PNG_COLOR_GRAY:
        r = g = b = sample[0];
        if (h->trns_present && sample[0] == h->trns_gray) {
            a = 0u;
        }
        break;
    case PNG_COLOR_RGB:
        r = sample[0];
        g = sample[1];
        b = sample[2];
        if (h->trns_present && r == h->trns_rgb[0] && g == h->trns_rgb[1] &&
            b == h->trns_rgb[2]) {
            a = 0u;
        }
        break;
    case PNG_COLOR_GRAY_ALPHA:
        r = g = b = sample[0];
        a = sample[1];
        break;
    case PNG_COLOR_RGBA:
        r = sample[0];
        g = sample[1];
        b = sample[2];
        a = sample[3];
        break;
    case PNG_COLOR_PALETTE: {
        uint32_t index = (uint32_t)sample[0];

        if (index >= h->palette_entries) {
            /* Out-of-range index: emit an opaque black rather than reading
             * past the palette. Reachable only from a malformed file. */
            r = g = b = 0u;
            break;
        }
        r = h->palette[index * 3u];
        g = h->palette[index * 3u + 1u];
        b = h->palette[index * 3u + 2u];
        if (h->trns_present && index < h->trns_palette_entries) {
            a = h->trns_palette[index];
        }
        break;
    }
    default:
        r = g = b = 0u;
        break;
    }
    dst[0] = r;
    dst[1] = g;
    dst[2] = b;
    dst[3] = a;
}

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */

static void decode_limits_resolve(const robo_gfx_decode_limits_t *limits,
                                 uint32_t *max_dimension,
                                 size_t *max_decoded_bytes)
{
    if (limits && limits->max_dimension != 0u) {
        *max_dimension = limits->max_dimension;
    } else {
        *max_dimension = ROBO_GFX_DECODE_DEFAULT_MAX_DIMENSION;
    }
    if (limits && limits->max_decoded_bytes != 0u) {
        *max_decoded_bytes = limits->max_decoded_bytes;
    } else {
        *max_decoded_bytes = ROBO_GFX_DECODE_DEFAULT_MAX_DECODED_BYTES;
    }
}

/**
 * The whole decoder. @p want_pixels selects whether the expensive half runs;
 * the header half — including the IDAT concatenation — is identical either
 * way, so the two public entry points cannot disagree about what a file is.
 */
static robo_result_t png_decode(const uint8_t *bytes, size_t length,
                                const robo_gfx_decode_limits_t *limits,
                                robo_gfx_decode_info_t *info_out,
                                robo_gfx_image_t **image_out)
{
    png_header_t header;
    uint32_t max_dimension;
    size_t max_decoded_bytes;
    size_t raw_size = 0u;
    robo_result_t res;

    memset(&header, 0, sizeof(header));
    decode_limits_resolve(limits, &max_dimension, &max_decoded_bytes);

    /* For the caller's information only: this bound is about how much IDAT
     * we are willing to buffer, so it is generous relative to the decoded
     * budget. It stops a file that is *mostly* IDAT from being buffered in
     * full before the dimensions are known to be sane. */
    res = png_walk_chunks(&header, bytes, length,
                          max_decoded_bytes > (SIZE_MAX / 4u)
                              ? SIZE_MAX
                              : max_decoded_bytes * 4u);
    if (res != ROBO_OK) {
        png_header_free(&header);
        return res;
    }

    if (header.width > max_dimension || header.height > max_dimension) {
        png_header_free(&header);
        return ROBO_ERROR;
    }
    res = png_expected_raw_size(&header, &raw_size);
    if (res != ROBO_OK) {
        png_header_free(&header);
        return res;
    }
    /* RGBA8 is four bytes per pixel; the check above already proved
     * width * height cannot overflow a size_t. */
    if ((size_t)header.width * (size_t)header.height > max_decoded_bytes / 4u) {
        png_header_free(&header);
        return ROBO_ERROR;
    }
    if (info_out) {
        info_out->container = ROBO_GFX_DECODE_CONTAINER_PNG;
        info_out->width = header.width;
        info_out->height = header.height;
        info_out->bit_depth = header.bit_depth;
        info_out->color_type = header.color_type;
        info_out->channels = header.samples_per_pixel;
        info_out->interlaced = 0;
        info_out->decoded_bytes =
            (size_t)header.width * (size_t)header.height * 4u;
    }
    if (!image_out) {
        png_header_free(&header);
        return ROBO_OK;
    }

    {
        uint8_t *raw = (uint8_t *)robo_malloc(raw_size);
        robo_gfx_surface_desc_t desc;
        robo_gfx_surface_t *surface = NULL;
        robo_gfx_surface_map_t map;
        size_t bpp = png_bytes_per_pixel(&header);
        size_t row_bytes = (size_t)header.width * bpp;
        size_t stride = row_bytes + 1u;
        uint32_t y;
        uint32_t x;

        if (!raw) {
            png_header_free(&header);
            return ROBO_OUT_OF_MEMORY;
        }

        res = inflate_exact(header.idat, header.idat_size, raw, raw_size);
        if (res != ROBO_OK) {
            robo_free(raw);
            png_header_free(&header);
            return res;
        }
        res = png_unfilter(raw, header.height, header.width, bpp);
        if (res != ROBO_OK) {
            robo_free(raw);
            png_header_free(&header);
            return res;
        }

        robo_gfx_surface_desc_init(&desc);
        desc.width = header.width;
        desc.height = header.height;
        desc.format = ROBO_GFX_FORMAT_RGBA8;
        res = robo_gfx_surface_create(&desc, &surface);
        if (res != ROBO_OK) {
            robo_free(raw);
            png_header_free(&header);
            return res;
        }
        /* Map rather than take the convenience pointer, so the byte layout is
         * whatever the surface actually says its stride is — a future surface
         * with padding rows decodes correctly without touching this loop. */
        memset(&map, 0, sizeof(map));
        res = robo_gfx_surface_map(surface, ROBO_GFX_SURFACE_ACCESS_WRITE, &map);
        if (res != ROBO_OK || !map.pixels) {
            robo_gfx_surface_release(surface);
            robo_free(raw);
            png_header_free(&header);
            return (res == ROBO_OK) ? ROBO_ERROR : res;
        }

        for (y = 0u; y < header.height; y++) {
            const uint8_t *row = raw + stride * (size_t)y + 1u;
            uint8_t *dst_row = map.pixels + map.stride_bytes * (size_t)y;

            for (x = 0u; x < header.width; x++) {
                png_pixel_to_rgba(&header, row + (size_t)x * bpp,
                                  dst_row + (size_t)x * 4u);
            }
        }
        robo_gfx_surface_unmap(surface);
        robo_free(raw);

        res = robo_gfx_image_create_from_surface(surface, image_out);
        robo_gfx_surface_release(surface); /* the image retained its own */
        png_header_free(&header);
        return res;
    }
}

robo_result_t robo_gfx_image_decode_info(const void *bytes, size_t length,
                                         const robo_gfx_decode_limits_t *limits,
                                         robo_gfx_decode_info_t *out)
{
    robo_gfx_decode_container_t container;

    if (!bytes || !out || length == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    container = robo_gfx_decode_probe(bytes, length);
    if (container == ROBO_GFX_DECODE_CONTAINER_UNKNOWN) {
        return ROBO_NOT_SUPPORTED;
    }
    return png_decode((const uint8_t *)bytes, length, limits, out, NULL);
}

robo_result_t robo_gfx_image_decode(const void *bytes, size_t length,
                                    const robo_gfx_decode_limits_t *limits,
                                    robo_gfx_image_t **out)
{
    robo_gfx_decode_container_t container;

    if (!bytes || !out || length == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    container = robo_gfx_decode_probe(bytes, length);
    if (container == ROBO_GFX_DECODE_CONTAINER_UNKNOWN) {
        return ROBO_NOT_SUPPORTED;
    }
    return png_decode((const uint8_t *)bytes, length, limits, NULL, out);
}
