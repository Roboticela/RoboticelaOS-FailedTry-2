/*
 * RoboGraphics image decoding (Phase 8.12 — the read half of Goal 6.13).
 *
 * Goal 6.13 wrote images; nothing read them. This header adds the decoder,
 * and it lives in RoboGraphics rather than in RoboHTML because pixels are
 * RoboGraphics' business: an HTML document may own an <img>, but the thing
 * an <img> holds is a GfxImage, and nothing about PNG belongs to the
 * document model.
 *
 *   bytes in an unknown container
 *        → GfxDecodeProbe       (what is this?)
 *        → GfxImageDecodeInfo  (how big, how encoded — no pixels)
 *        → GfxImageDecode       (pixels, as a retained RGBA8 image)
 *
 * What is implemented
 *   PNG, at bit depth 8, in colour types 0 (grayscale), 2 (RGB), 3 (palette,
 *   with optional tRNS) and 4 / 6 (with alpha), carried in a single or
 *   multiple IDAT chunks. DEFLATE is decoded in full: stored, fixed-Huffman
 *   and dynamic-Huffman blocks, which is what a real PNG from a real encoder
 *   needs — the Goal 6.13 encoder only ever writes store blocks, so a decoder
 *   that handled only those would round-trip our own output and fail on
 *   everything else.
 *
 *   All five PNG row filters (None, Sub, Up, Average, Paeth) are applied.
 *
 * What is deliberately not
 *   Adam7 interlacing and sub-byte bit depths (1/2/4) are refused with
 *   RoboNotSupported rather than approximated. Both are safe refusals: the
 *   caller learns the image cannot be decoded here, which is a different and
 *   better answer than a silently wrong picture. JPEG/WebP/AVIF/GIF are not
 *   sniffed at all and report RoboGfxDecodeContainerUnknown.
 *
 * Cost is bounded by metadata, not by trust
 *   Every dimension that arrives from the file is treated as hostile: width
 *   and height are checked against GfxDecodeLimits *and* against
 *   integer overflow before a single byte is allocated, the expected raw
 *   scanline size is computed with explicit overflow checks, and the decoder
 *   requires the DEFLATE stream to produce exactly that many bytes. A 16-byte
 *   file claiming to be 100000x100000 therefore fails in the header parse
 *   with no allocation at all, which is the property the Phase 8.12 security
 *   tests pin.
 *
 * Threading: no shared mutable state; decode is a pure function of the input
 * bytes plus the limits. The returned image follows the normal
 * GfxImageRetain / GfxImageRelease lifetime.
 *
 * See graphics/docs/ImageDecode.md.
 */

#ifndef ROBO_GRAPHICS_DECODE_H
#define ROBO_GRAPHICS_DECODE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsImage.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Container a byte run was recognised as. */
typedef enum GfxDecodeContainer {
    /** Not a format this decoder knows (includes empty and tiny inputs). */
    RoboGfxDecodeContainerUnknown = 0,
    RoboGfxDecodeContainerPng = 1
} GfxDecodeContainer;

/** Human-readable container name; never NULL. */
const char *GfxDecodeContainerName(GfxDecodeContainer c);

/** 1 if @p c is a defined container enumerator. */
int GfxDecodeContainerIsKnown(GfxDecodeContainer c);

/**
 * Ceilings a decode may not exceed. All fields are caller-set policy, not
 * format limits: a zeroed struct means "use the defaults".
 *
 * The defaults are dimension 4096 and 64 MiB of decoded RGBA8, which is one
 * order of magnitude more than any document in this runtime needs and small
 * enough that a hostile header cannot exhaust a machine by claiming a size.
 */
typedef struct GfxDecodeLimits {
    /** 0 → RoboGfxDecodeDefaultMaxDimension. */
    uint32_t maxDimension;
    /** 0 → RoboGfxDecodeDefaultMaxDecodedBytes. */
    size_t maxDecodedBytes;
} GfxDecodeLimits;

#define RoboGfxDecodeDefaultMaxDimension 4096u
/** 64 MiB — 4096x4096 RGBA8 with headroom. */
#define RoboGfxDecodeDefaultMaxDecodedBytes ((size_t)64u * 1024u * 1024u)

/** Fill @p limits with the defaults. NULL is a no-op. */
void GfxDecodeLimitsInit(GfxDecodeLimits *limits);

/**
 * Header facts, readable without decoding pixels.
 *
 * @c decodedBytes is the RGBA8 size @c GfxImageDecode would allocate
 * for this image, so a caller can budget before committing to it.
 */
typedef struct GfxDecodeInfo {
    GfxDecodeContainer container;
    uint32_t width;
    uint32_t height;
    /** Bits per sample as written in the file (8 for everything supported). */
    uint8_t bitDepth;
    /** Container-native colour type: PNG IHDR colour type. */
    uint8_t colorType;
    /** Samples per pixel implied by the colour type. */
    uint8_t channels;
    /** Non-zero when the image is carried in interleaved passes. */
    int interlaced;
    size_t decodedBytes;
} GfxDecodeInfo;

/**
 * Identify the container from the leading bytes only.
 * Never fails: an unrecognised or too-short run is UNKNOWN. Never allocates.
 */
GfxDecodeContainer GfxDecodeProbe(const void *bytes,
                                                  size_t length);

/**
 * Parse and validate the header without decoding pixels.
 *
 * @return RoboOk and fills @p out;
 *         RoboInvalidArgument for NULL @p bytes/@p out, or zero @p length;
 *         RoboNotSupported when the container is unknown, or is known but
 *         uses an encoding this decoder does not implement (interlacing,
 *         sub-byte bit depth, an unknown critical chunk);
 *         RoboError when the bytes claim a supported encoding but are
 *         malformed, corrupt or truncated;
 *         RoboOutOfMemory only if reading the header needs to grow the
 *         IDAT buffer past @p limits.
 */
Result GfxImageDecodeInfo(const void *bytes,
                                         size_t length,
                                         const GfxDecodeLimits *limits,
                                         GfxDecodeInfo *out);

/**
 * Decode @p bytes into a newly created RGBA8 raster image.
 *
 * The result is always RGBA8 regardless of how the file stored its channels,
 * because that is the one format every consumer downstream agrees on:
 * grayscale replicates to RGB with alpha 255, RGB gains alpha 255, a palette
 * index resolves through PLTE with alpha from tRNS when it is present.
 *
 * @return RoboOk and a retained image in @p out (release with
 *         GfxImageRelease); otherwise a failure with @p out set to
 *         NULL and nothing allocated. The error codes are those of
 *         GfxImageDecodeInfo, plus RoboError for a scanline stream
 *         that does not decompress to exactly the expected size.
 */
Result GfxImageDecode(const void *bytes,
                                    size_t length,
                                    const GfxDecodeLimits *limits,
                                    GfxImage **out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_DECODE_H */
