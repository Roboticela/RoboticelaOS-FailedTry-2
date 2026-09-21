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
 *      `PngExpectedRawSize` is the single place the scanline size is
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

const char *GfxDecodeContainerName(GfxDecodeContainer c)
{
    switch (c) {
    case RoboGfxDecodeContainerPng:
        return "png";
    case RoboGfxDecodeContainerUnknown:
    default:
        return "unknown";
    }
}

int GfxDecodeContainerIsKnown(GfxDecodeContainer c)
{
    return c == RoboGfxDecodeContainerPng;
}

void GfxDecodeLimitsInit(GfxDecodeLimits *limits)
{
    if (!limits) {
        return;
    }
    limits->maxDimension = RoboGfxDecodeDefaultMaxDimension;
    limits->maxDecodedBytes = RoboGfxDecodeDefaultMaxDecodedBytes;
}

GfxDecodeContainer GfxDecodeProbe(const void *bytes,
                                                  size_t length)
{
    if (!bytes || length < sizeof(PNG_SIGNATURE)) {
        return RoboGfxDecodeContainerUnknown;
    }
    if (memcmp(bytes, PNG_SIGNATURE, sizeof(PNG_SIGNATURE)) == 0) {
        return RoboGfxDecodeContainerPng;
    }
    return RoboGfxDecodeContainerUnknown;
}

static uint32_t ReadBe32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint32_t ReadBe16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
}

/*
 * Bitwise CRC-32 (IEEE 802.3, the PNG polynomial). No table: a chunk stream
 * is short and a 256-entry table is 1 KiB of rodata this decoder does not
 * need to own, since CRC cost is linear in image bytes and dominated by
 * DEFLATE anyway.
 */
static uint32_t PngCrc32(const uint8_t *data, size_t length)
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

typedef struct pngHeader {
    uint32_t width;
    uint32_t height;
    uint8_t bitDepth;
    uint8_t colorType;
    uint8_t interlace;
    uint8_t samplesPerPixel;

    uint8_t palette[256 * 3];
    uint32_t paletteEntries;

    /* tRNS, normalised to "is every sample of this value full transparent". */
    int trnsPresent;
    uint8_t trnsGray;
    uint8_t trnsRgb[3];
    uint8_t trnsPalette[256];
    uint32_t trnsPaletteEntries;

    uint8_t *idat;
    size_t idatSize;
    size_t idatCapacity;
} PngHeader;

static uint8_t PngChannelsForColorType(uint8_t colorType)
{
    switch (colorType) {
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
static size_t PngBytesPerPixel(const PngHeader *h)
{
    return (size_t)h->samplesPerPixel;
}

/**
 * Scanline bytes: every row is one filter-type byte plus width * bpp.
 * Overflow is checked here and only here, so no later arithmetic can wrap.
 */
static Result PngExpectedRawSize(const PngHeader *h,
                                           size_t *out)
{
    size_t row;
    size_t total;
    size_t bpp = PngBytesPerPixel(h);

    if (h->width == 0u || h->height == 0u) {
        return RoboError;
    }
    if (bpp == 0u) {
        return RoboNotSupported;
    }
    if ((size_t)h->width > (SIZE_MAX - 1u) / bpp) {
        return RoboError;
    }
    row = (size_t)h->width * bpp + 1u;
    if ((size_t)h->height > SIZE_MAX / row) {
        return RoboError;
    }
    total = row * (size_t)h->height;
    *out = total;
    return RoboOk;
}

static Result PngReserveIdat(PngHeader *h, size_t extra, size_t cap)
{
    size_t need;
    size_t next;
    uint8_t *grown;

    if (extra > SIZE_MAX - h->idatSize) {
        return RoboError;
    }
    need = h->idatSize + extra;
    if (need <= h->idatCapacity) {
        return RoboOk;
    }
    if (cap != 0u && need > cap) {
        /* The IDAT stream alone already exceeds the decoded budget. */
        return RoboError;
    }
    next = h->idatCapacity ? h->idatCapacity : 1024u;
    while (next < need) {
        if (next > SIZE_MAX / 2u) {
            next = need;
            break;
        }
        next *= 2u;
    }
    grown = (uint8_t *)Realloc(h->idat, next);
    if (!grown) {
        return RoboOutOfMemory;
    }
    h->idat = grown;
    h->idatCapacity = next;
    return RoboOk;
}

static void PngHeaderFree(PngHeader *h)
{
    if (h->idat) {
        RoboFree(h->idat);
    }
    h->idat = NULL;
    h->idatSize = 0u;
    h->idatCapacity = 0u;
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

static int PngTypeIsCritical(uint32_t type)
{
    /* Ancillary chunks have bit 5 of the first type byte set (lowercase). */
    return (type & 0x20000000u) == 0u;
}

static Result PngParseIhdr(PngHeader *h, const uint8_t *data,
                                    size_t length)
{
    uint8_t colorType;
    uint8_t bitDepth;
    uint8_t channels;

    if (length != 13u) {
        return RoboError;
    }
    h->width = ReadBe32(data);
    h->height = ReadBe32(data + 4u);
    bitDepth = data[8];
    colorType = data[9];

    /* compression method 0 and filter method 0 are the only defined ones. */
    if (data[10] != 0u || data[11] != 0u) {
        return RoboNotSupported;
    }
    h->interlace = data[12];
    if (h->interlace > 1u) {
        return RoboError;
    }
    if (h->interlace == 1u) {
        /* Adam7 is refused rather than approximated; see the header docs. */
        return RoboNotSupported;
    }
    if (h->width == 0u || h->height == 0u) {
        return RoboError;
    }
    if (bitDepth != 8u) {
        return RoboNotSupported;
    }
    channels = PngChannelsForColorType(colorType);
    if (channels == 0u) {
        return RoboNotSupported;
    }

    h->bitDepth = bitDepth;
    h->colorType = colorType;
    h->samplesPerPixel = channels;
    return RoboOk;
}

static Result PngParsePlte(PngHeader *h, const uint8_t *data,
                                    size_t length)
{
    uint32_t entries;

    if (h->colorType != PNG_COLOR_PALETTE) {
        return RoboError; /* PLTE is only meaningful for colour type 3 */
    }
    if (length == 0u || (length % 3u) != 0u || length > sizeof(h->palette)) {
        return RoboError;
    }
    entries = (uint32_t)(length / 3u);
    memcpy(h->palette, data, length);
    h->paletteEntries = entries;
    return RoboOk;
}

static Result PngParseTrns(PngHeader *h, const uint8_t *data,
                                    size_t length)
{
    switch (h->colorType) {
    case PNG_COLOR_PALETTE:
        if (h->paletteEntries == 0u || length > h->paletteEntries) {
            return RoboError;
        }
        if (length == 0u) {
            return RoboError;
        }
        memcpy(h->trnsPalette, data, length);
        h->trnsPaletteEntries = (uint32_t)length;
        h->trnsPresent = 1;
        return RoboOk;
    case PNG_COLOR_GRAY:
        if (length != 2u) {
            return RoboError;
        }
        /* Bit depth 8: a transparency sample above 255 can never match. */
        if (ReadBe16(data) > 0xFFu) {
            return RoboOk;
        }
        h->trnsGray = (uint8_t)ReadBe16(data);
        h->trnsPresent = 1;
        return RoboOk;
    case PNG_COLOR_RGB:
        if (length != 6u) {
            return RoboError;
        }
        if (ReadBe16(data) > 0xFFu || ReadBe16(data + 2u) > 0xFFu ||
            ReadBe16(data + 4u) > 0xFFu) {
            return RoboOk;
        }
        h->trnsRgb[0] = (uint8_t)ReadBe16(data);
        h->trnsRgb[1] = (uint8_t)ReadBe16(data + 2u);
        h->trnsRgb[2] = (uint8_t)ReadBe16(data + 4u);
        h->trnsPresent = 1;
        return RoboOk;
    default:
        /* tRNS is not defined for colour types 4 and 6. */
        return RoboError;
    }
}

static Result PngWalkChunks(PngHeader *h, const uint8_t *bytes,
                                     size_t length, size_t idatCap)
{
    size_t pos = sizeof(PNG_SIGNATURE);
    int seenIhdr = 0;
    int seenIend = 0;

    while (pos + 12u <= length) {
        uint32_t chunkLength = ReadBe32(bytes + pos);
        uint32_t type = ReadBe32(bytes + pos + 4u);
        const uint8_t *data = bytes + pos + 8u;
        uint32_t storedCrc;
        uint32_t computedCrc;
        Result res;

        /* A length with bit 31 set is not a length this decoder will trust. */
        if (chunkLength > 0x7FFFFFFFu) {
            return RoboError;
        }
        if ((size_t)chunkLength > length - pos - 12u) {
            return RoboError; /* truncated chunk */
        }
        storedCrc = ReadBe32(data + chunkLength);
        computedCrc = PngCrc32(bytes + pos + 4u, (size_t)chunkLength + 4u);
        if (storedCrc != computedCrc) {
            return RoboError; /* corrupt chunk */
        }

        if (type == PNG_TYPE_IHDR) {
            if (seenIhdr || pos != sizeof(PNG_SIGNATURE)) {
                return RoboError; /* IHDR must be the first chunk, once */
            }
            res = PngParseIhdr(h, data, chunkLength);
            if (res != RoboOk) {
                return res;
            }
            seenIhdr = 1;
        } else if (type == PNG_TYPE_PLTE) {
            if (!seenIhdr) {
                return RoboError;
            }
            res = PngParsePlte(h, data, chunkLength);
            if (res != RoboOk) {
                return res;
            }
        } else if (type == PNG_TYPE_TRNS) {
            if (!seenIhdr) {
                return RoboError;
            }
            res = PngParseTrns(h, data, chunkLength);
            if (res != RoboOk) {
                return res;
            }
        } else if (type == PNG_TYPE_IDAT) {
            if (!seenIhdr) {
                return RoboError;
            }
            res = PngReserveIdat(h, chunkLength, idatCap);
            if (res != RoboOk) {
                return res;
            }
            if (chunkLength != 0u) {
                memcpy(h->idat + h->idatSize, data, chunkLength);
                h->idatSize += chunkLength;
            }
        } else if (type == PNG_TYPE_IEND) {
            if (chunkLength != 0u) {
                return RoboError;
            }
            seenIend = 1;
            pos += 12u + (size_t)chunkLength;
            break;
        } else if (PngTypeIsCritical(type)) {
            /*
             * An unknown critical chunk means the file requires something
             * this decoder does not implement. Refusing is the honest answer:
             * skipping it could produce a picture that is quietly wrong.
             */
            return RoboNotSupported;
        } else {
            /* Ancillary chunk: skipped by design. */
        }

        pos += 12u + (size_t)chunkLength;
    }

    if (!seenIhdr) {
        return RoboError;
    }
    if (!seenIend) {
        return RoboError; /* truncated stream: no IEND */
    }
    if (h->idatSize == 0u) {
        return RoboError; /* no image data at all */
    }
    if (h->colorType == PNG_COLOR_PALETTE && h->paletteEntries == 0u) {
        return RoboError; /* a palette image without PLTE cannot be decoded */
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* DEFLATE                                                             */
/* ------------------------------------------------------------------ */

typedef struct bitReader {
    const uint8_t *src;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    uint32_t bitcount;
    int failed;
} BitReader;

/** Read @p need bits LSB-first; returns -1 and latches failure when short. */
static int BitsRead(BitReader *r, int need)
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
} Huffman;

/** Canonical code assignment (RFC 1951 §3.2.2). */
static void HuffmanBuild(Huffman *h, const uint8_t *lengths, size_t count)
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
static int HuffmanUsable(const Huffman *h)
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
static int HuffmanDecode(BitReader *r, const Huffman *h)
{
    int code = 0;
    int first = 0;
    int index = 0;
    int len;

    for (len = 1; len <= HUFF_MAX_BITS; len++) {
        int bit = BitsRead(r, 1);

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

static void DeflateFixedTables(Huffman *literal, Huffman *distance)
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
    HuffmanBuild(literal, lengths, 288u);

    for (i = 0; i < 30; i++) {
        lengths[i] = 5u;
    }
    HuffmanBuild(distance, lengths, 30u);
}

/** Copy one length/distance pair; overlapping runs are byte-at-a-time. */
static int DeflateCopyMatch(uint8_t *dst, size_t capacity, size_t *outPos,
                              unsigned length, unsigned distance)
{
    unsigned i;

    if ((size_t)distance > *outPos) {
        return 0;
    }
    for (i = 0u; i < length; i++) {
        if (*outPos >= capacity) {
            return 0;
        }
        dst[*outPos] = dst[*outPos - distance];
        (*outPos)++;
    }
    return 1;
}

/**
 * The block body. The code tables are the caller's, built either by the
 * fixed tables or from the dynamic header — which is the only difference
 * between the two Huffman block kinds, so they share one loop.
 */
static int DeflateCodes(BitReader *r, const Huffman *literal,
                         const Huffman *distance, uint8_t *dst,
                         size_t capacity, size_t *outPos)
{
    for (;;) {
        int symbol = HuffmanDecode(r, literal);

        if (symbol < 0) {
            return 0;
        }
        if (symbol < 256) {
            if (*outPos >= capacity) {
                return 0;
            }
            dst[(*outPos)++] = (uint8_t)symbol;
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
            int extra = BitsRead(r, DEFLATE_LENGTH_EXTRA[symbol]);
            int dsymbol;

            if (extra < 0) {
                return 0;
            }
            {
                unsigned length =
                    (unsigned)DEFLATE_LENGTH_BASE[symbol] + (unsigned)extra;

                dsymbol = HuffmanDecode(r, distance);
                if (dsymbol < 0 || dsymbol >= 30) {
                    return 0;
                }
                extra = BitsRead(r, DEFLATE_DISTANCE_EXTRA[dsymbol]);
                if (extra < 0) {
                    return 0;
                }
                if (!DeflateCopyMatch(
                        dst, capacity, outPos, length,
                        (unsigned)DEFLATE_DISTANCE_BASE[dsymbol] +
                            (unsigned)extra)) {
                    return 0;
                }
            }
        }
    }
}

static int DeflateDynamic(BitReader *r, uint8_t *dst, size_t capacity,
                           size_t *outPos)
{
    static const uint8_t codeLengthOrder[19] = {16u, 17u, 18u, 0u, 8u,  7u,
                                                  9u,  6u,  10u, 5u, 11u, 4u,
                                                  12u, 3u,  13u, 2u, 14u, 1u,
                                                  15u};
    uint8_t lengths[320];
    Huffman codeLength;
    Huffman literal;
    Huffman distance;
    int hlit;
    int hdist;
    int hclen;
    int i;
    int index;
    int total;

    hlit = BitsRead(r, 5);
    hdist = BitsRead(r, 5);
    hclen = BitsRead(r, 4);
    if (hlit < 0 || hdist < 0 || hclen < 0) {
        return 0;
    }
    hlit += 257;
    hdist += 1;
    hclen += 4;
    total = hlit + hdist;

    memset(lengths, 0, sizeof(lengths));
    for (i = 0; i < hclen; i++) {
        int value = BitsRead(r, 3);

        if (value < 0) {
            return 0;
        }
        lengths[codeLengthOrder[i]] = (uint8_t)value;
    }
    HuffmanBuild(&codeLength, lengths, 19u);
    if (!HuffmanUsable(&codeLength)) {
        return 0;
    }

    memset(lengths, 0, sizeof(lengths));
    index = 0;
    while (index < total) {
        int symbol = HuffmanDecode(r, &codeLength);
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
            int extra = BitsRead(r, 2);

            if (extra < 0 || index == 0) {
                return 0;
            }
            repeat = 3 + extra;
            value = lengths[index - 1];
        } else if (symbol == 17) {
            int extra = BitsRead(r, 3);

            if (extra < 0) {
                return 0;
            }
            repeat = 3 + extra;
            value = 0;
        } else {
            int extra = BitsRead(r, 7);

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
    HuffmanBuild(&literal, lengths, (size_t)hlit);
    if (!HuffmanUsable(&literal)) {
        return 0;
    }
    HuffmanBuild(&distance, lengths + hlit, (size_t)hdist);
    if (!HuffmanUsable(&distance)) {
        return 0;
    }
    return DeflateCodes(r, &literal, &distance, dst, capacity, outPos);
}

/**
 * Inflate a zlib-wrapped DEFLATE stream into exactly @p capacity bytes.
 *
 * Exactly: a stream that produces fewer is truncated and one that wants to
 * produce more is malformed. Both are refused, so the caller never has to
 * reason about a partially filled scanline buffer.
 */
static Result InflateExact(const uint8_t *src, size_t size,
                                   uint8_t *dst, size_t capacity)
{
    BitReader reader;
    size_t produced = 0u;

    if (size < 2u) {
        return RoboError;
    }
    {
        uint8_t cmf = src[0];
        uint8_t flg = src[1];

        /* CM = 8 (deflate), CINFO <= 7 (window <= 32 KiB), FDICT clear. */
        if ((cmf & 0x0Fu) != 8u || ((cmf >> 4) & 0x0Fu) > 7u) {
            return RoboError;
        }
        if ((((uint32_t)cmf << 8) | (uint32_t)flg) % 31u != 0u) {
            return RoboError;
        }
        if ((flg & 0x20u) != 0u) {
            return RoboNotSupported; /* preset dictionary */
        }
    }

    reader.src = src;
    reader.size = size;
    reader.pos = 2u;
    reader.bitbuf = 0u;
    reader.bitcount = 0u;
    reader.failed = 0;

    for (;;) {
        int final = BitsRead(&reader, 1);
        int type;

        if (final < 0) {
            return RoboError;
        }
        type = BitsRead(&reader, 2);
        if (type < 0) {
            return RoboError;
        }
        if (type == 0) {
            /* Stored: discard the partial byte, then LEN / ~LEN / raw bytes. */
            int low;
            int high;

            reader.bitbuf = 0u;
            reader.bitcount = 0u;
            low = BitsRead(&reader, 16);
            high = BitsRead(&reader, 16);
            if (low < 0 || high < 0) {
                return RoboError;
            }
            if (((unsigned)low ^ 0xFFFFu) != (unsigned)high || low == 0) {
                return RoboError;
            }
            if (reader.pos + (size_t)low > reader.size) {
                return RoboError;
            }
            if ((size_t)low > capacity - produced) {
                return RoboError; /* more image data than the header implies */
            }
            memcpy(dst + produced, reader.src + reader.pos, (size_t)low);
            reader.pos += (size_t)low;
            produced += (size_t)low;
        } else if (type == 1 || type == 2) {
            Huffman literal;
            Huffman distance;
            int ok;

            if (type == 1) {
                DeflateFixedTables(&literal, &distance);
                ok = DeflateCodes(&reader, &literal, &distance, dst, capacity,
                                   &produced);
            } else {
                ok = DeflateDynamic(&reader, dst, capacity, &produced);
            }
            if (!ok) {
                return RoboError;
            }
        } else {
            return RoboError; /* BTYPE 3 is reserved */
        }
        if (final != 0) {
            break;
        }
    }

    if (produced != capacity) {
        return RoboError;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Unfiltering                                                         */
/* ------------------------------------------------------------------ */

static uint8_t PaethPredictor(uint8_t a, uint8_t b, uint8_t c)
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
static Result PngUnfilter(uint8_t *raw, uint32_t height,
                                  uint32_t width, size_t bpp)
{
    size_t rowBytes = (size_t)width * bpp;
    size_t stride = rowBytes + 1u;
    uint32_t y;

    for (y = 0u; y < height; y++) {
        uint8_t *row = raw + stride * (size_t)y;
        uint8_t filter = row[0];
        uint8_t *cur = row + 1u;
        const uint8_t *prev = (y == 0u) ? NULL : (row - stride + 1u);
        size_t i;

        for (i = 0u; i < rowBytes; i++) {
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
                                   PaethPredictor(left, up, upleft));
                break;
            default:
                return RoboError; /* filter type 5+ is not defined */
            }
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Samples → RGBA8                                                     */
/* ------------------------------------------------------------------ */

/** Write one source sample group into @p dst as RGBA8. */
static void PngPixelToRgba(const PngHeader *h, const uint8_t *sample,
                              uint8_t *dst)
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a = 255u;

    switch (h->colorType) {
    case PNG_COLOR_GRAY:
        r = g = b = sample[0];
        if (h->trnsPresent && sample[0] == h->trnsGray) {
            a = 0u;
        }
        break;
    case PNG_COLOR_RGB:
        r = sample[0];
        g = sample[1];
        b = sample[2];
        if (h->trnsPresent && r == h->trnsRgb[0] && g == h->trnsRgb[1] &&
            b == h->trnsRgb[2]) {
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

        if (index >= h->paletteEntries) {
            /* Out-of-range index: emit an opaque black rather than reading
             * past the palette. Reachable only from a malformed file. */
            r = g = b = 0u;
            break;
        }
        r = h->palette[index * 3u];
        g = h->palette[index * 3u + 1u];
        b = h->palette[index * 3u + 2u];
        if (h->trnsPresent && index < h->trnsPaletteEntries) {
            a = h->trnsPalette[index];
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

static void DecodeLimitsResolve(const GfxDecodeLimits *limits,
                                 uint32_t *maxDimension,
                                 size_t *maxDecodedBytes)
{
    if (limits && limits->maxDimension != 0u) {
        *maxDimension = limits->maxDimension;
    } else {
        *maxDimension = RoboGfxDecodeDefaultMaxDimension;
    }
    if (limits && limits->maxDecodedBytes != 0u) {
        *maxDecodedBytes = limits->maxDecodedBytes;
    } else {
        *maxDecodedBytes = RoboGfxDecodeDefaultMaxDecodedBytes;
    }
}

/**
 * The whole decoder. @p want_pixels selects whether the expensive half runs;
 * the header half — including the IDAT concatenation — is identical either
 * way, so the two public entry points cannot disagree about what a file is.
 */
static Result PngDecode(const uint8_t *bytes, size_t length,
                                const GfxDecodeLimits *limits,
                                GfxDecodeInfo *infoOut,
                                GfxImage **imageOut)
{
    PngHeader header;
    uint32_t maxDimension;
    size_t maxDecodedBytes;
    size_t rawSize = 0u;
    Result res;

    memset(&header, 0, sizeof(header));
    DecodeLimitsResolve(limits, &maxDimension, &maxDecodedBytes);

    /* For the caller's information only: this bound is about how much IDAT
     * we are willing to buffer, so it is generous relative to the decoded
     * budget. It stops a file that is *mostly* IDAT from being buffered in
     * full before the dimensions are known to be sane. */
    res = PngWalkChunks(&header, bytes, length,
                          maxDecodedBytes > (SIZE_MAX / 4u)
                              ? SIZE_MAX
                              : maxDecodedBytes * 4u);
    if (res != RoboOk) {
        PngHeaderFree(&header);
        return res;
    }

    if (header.width > maxDimension || header.height > maxDimension) {
        PngHeaderFree(&header);
        return RoboError;
    }
    res = PngExpectedRawSize(&header, &rawSize);
    if (res != RoboOk) {
        PngHeaderFree(&header);
        return res;
    }
    /* RGBA8 is four bytes per pixel; the check above already proved
     * width * height cannot overflow a size_t. */
    if ((size_t)header.width * (size_t)header.height > maxDecodedBytes / 4u) {
        PngHeaderFree(&header);
        return RoboError;
    }
    if (infoOut) {
        infoOut->container = RoboGfxDecodeContainerPng;
        infoOut->width = header.width;
        infoOut->height = header.height;
        infoOut->bitDepth = header.bitDepth;
        infoOut->colorType = header.colorType;
        infoOut->channels = header.samplesPerPixel;
        infoOut->interlaced = 0;
        infoOut->decodedBytes =
            (size_t)header.width * (size_t)header.height * 4u;
    }
    if (!imageOut) {
        PngHeaderFree(&header);
        return RoboOk;
    }

    {
        uint8_t *raw = (uint8_t *)Malloc(rawSize);
        GfxSurfaceDesc desc;
        GfxSurface *surface = NULL;
        GfxSurfaceMap map;
        size_t bpp = PngBytesPerPixel(&header);
        size_t rowBytes = (size_t)header.width * bpp;
        size_t stride = rowBytes + 1u;
        uint32_t y;
        uint32_t x;

        if (!raw) {
            PngHeaderFree(&header);
            return RoboOutOfMemory;
        }

        res = InflateExact(header.idat, header.idatSize, raw, rawSize);
        if (res != RoboOk) {
            RoboFree(raw);
            PngHeaderFree(&header);
            return res;
        }
        res = PngUnfilter(raw, header.height, header.width, bpp);
        if (res != RoboOk) {
            RoboFree(raw);
            PngHeaderFree(&header);
            return res;
        }

        GfxSurfaceDescInit(&desc);
        desc.width = header.width;
        desc.height = header.height;
        desc.format = RoboGfxFormatRgba8;
        res = GfxSurfaceCreate(&desc, &surface);
        if (res != RoboOk) {
            RoboFree(raw);
            PngHeaderFree(&header);
            return res;
        }
        /* Map rather than take the convenience pointer, so the byte layout is
         * whatever the surface actually says its stride is — a future surface
         * with padding rows decodes correctly without touching this loop. */
        memset(&map, 0, sizeof(map));
        res = GetGfxSurfaceMap(surface, RoboGfxSurfaceAccessWrite, &map);
        if (res != RoboOk || !map.pixels) {
            GfxSurfaceRelease(surface);
            RoboFree(raw);
            PngHeaderFree(&header);
            return (res == RoboOk) ? RoboError : res;
        }

        for (y = 0u; y < header.height; y++) {
            const uint8_t *row = raw + stride * (size_t)y + 1u;
            uint8_t *dstRow = map.pixels + map.strideBytes * (size_t)y;

            for (x = 0u; x < header.width; x++) {
                PngPixelToRgba(&header, row + (size_t)x * bpp,
                                  dstRow + (size_t)x * 4u);
            }
        }
        GfxSurfaceUnmap(surface);
        RoboFree(raw);

        res = GfxImageCreateFromSurface(surface, imageOut);
        GfxSurfaceRelease(surface); /* the image retained its own */
        PngHeaderFree(&header);
        return res;
    }
}

Result GfxImageDecodeInfo(const void *bytes, size_t length,
                                         const GfxDecodeLimits *limits,
                                         GfxDecodeInfo *out)
{
    GfxDecodeContainer container;

    if (!bytes || !out || length == 0u) {
        return RoboInvalidArgument;
    }
    container = GfxDecodeProbe(bytes, length);
    if (container == RoboGfxDecodeContainerUnknown) {
        return RoboNotSupported;
    }
    return PngDecode((const uint8_t *)bytes, length, limits, out, NULL);
}

Result GfxImageDecode(const void *bytes, size_t length,
                                    const GfxDecodeLimits *limits,
                                    GfxImage **out)
{
    GfxDecodeContainer container;

    if (!bytes || !out || length == 0u) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    container = GfxDecodeProbe(bytes, length);
    if (container == RoboGfxDecodeContainerUnknown) {
        return RoboNotSupported;
    }
    return PngDecode((const uint8_t *)bytes, length, limits, NULL, out);
}
