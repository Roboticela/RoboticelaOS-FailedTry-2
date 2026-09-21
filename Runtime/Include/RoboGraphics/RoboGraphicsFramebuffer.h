/*
 * RoboGraphics framebuffer (Goal 6.3).
 *
 * A framebuffer is a rectangular, CPU-addressable 2D pixel store with an
 * explicit stride. It is the foundational storage abstraction for later
 * surface / display / QEMU presentation work.
 *
 * Goal 6.3 implements owned (RoboMem) storage and a simple borrowed view.
 * GPU / native / QEMU backends arrive later behind the same ownership model.
 *
 * Pixel-format metadata comes only from Goal 6.2 APIs — do not hardcode bpp.
 *
 * Threading: not concurrently mutable unless the caller synchronizes.
 * Retain/release is not atomic.
 *
 * See graphics/docs/Framebuffer.md.
 */

#ifndef ROBO_GRAPHICS_FRAMEBUFFER_H
#define ROBO_GRAPHICS_FRAMEBUFFER_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GfxFramebufferOwnership {
    RoboGfxFramebufferOwned = 1,
    RoboGfxFramebufferBorrowed = 2
} GfxFramebufferOwnership;

typedef struct GfxFramebufferDesc {
    uint32_t width;
    uint32_t height;
    GfxPixelFormat format;
    /** 0 → tightly packed minimum row bytes from pixel-format helpers. */
    size_t strideBytes;
} GfxFramebufferDesc;

typedef struct GfxFramebuffer GfxFramebuffer;

void GfxFramebufferDescInit(GfxFramebufferDesc *desc);

/**
 * Allocate an owned framebuffer (RoboMem), zero-filled.
 * width/height must be non-zero and within RoboGfxMaxSurfaceDim.
 * format must be implemented (Goal 6.2).
 * Overflow / invalid → RoboInvalidArgument; OOM → RoboOutOfMemory.
 */
Result GfxFramebufferCreate(const GfxFramebufferDesc *desc,
                                          GfxFramebuffer **out);

/**
 * Borrow external storage. Caller keeps @p pixels alive until last release.
 * strideBytes must be >= minimum row bytes (0 → tightly packed).
 */
Result GfxFramebufferCreateBorrowed(
    const GfxFramebufferDesc *desc,
    void *pixels,
    GfxFramebuffer **out);

void GfxFramebufferRetain(GfxFramebuffer *fb);
void GfxFramebufferRelease(GfxFramebuffer *fb);

GfxFramebufferId GetGfxFramebufferId(const GfxFramebuffer *fb);
GfxFramebufferOwnership MakeGfxFramebufferOwnership(
    const GfxFramebuffer *fb);

uint32_t GfxFramebufferWidth(const GfxFramebuffer *fb);
uint32_t GfxFramebufferHeight(const GfxFramebuffer *fb);
GfxPixelFormat GfxFramebufferFormat(const GfxFramebuffer *fb);
uint8_t GfxFramebufferBytesPerPixel(const GfxFramebuffer *fb);
size_t GfxFramebufferStride(const GfxFramebuffer *fb);
size_t GfxFramebufferByteSize(const GfxFramebuffer *fb);

uint8_t *GfxFramebufferPixels(GfxFramebuffer *fb);
const uint8_t *GfxFramebufferPixelsConst(const GfxFramebuffer *fb);

/**
 * Pointer to the start of row @p y.
 * Out of range → RoboInvalidArgument and *outRow = NULL.
 */
Result GfxFramebufferRow(GfxFramebuffer *fb,
                                       uint32_t y,
                                       uint8_t **outRow);
Result GfxFramebufferRowConst(const GfxFramebuffer *fb,
                                             uint32_t y,
                                             const uint8_t **outRow);

/**
 * Byte offset of pixel (x,y) from storage base.
 * Bounds-checked against width/height; overflow-safe via pixel-format helpers.
 */
Result GfxFramebufferPixelOffset(const GfxFramebuffer *fb,
                                                uint32_t x,
                                                uint32_t y,
                                                size_t *outOffset);

/**
 * Pointer to the first byte of pixel (x,y).
 */
Result GfxFramebufferPixel(GfxFramebuffer *fb,
                                         uint32_t x,
                                         uint32_t y,
                                         uint8_t **outPixel);
Result GfxFramebufferPixelConst(const GfxFramebuffer *fb,
                                               uint32_t x,
                                               uint32_t y,
                                               const uint8_t **outPixel);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_FRAMEBUFFER_H */
