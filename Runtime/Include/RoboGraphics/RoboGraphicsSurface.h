/*
 * RoboGraphics surface (Goals 6.1 / 6.4).
 *
 * Surface = general graphics resource (paint / raster / present target).
 * Framebuffer = concrete CPU pixel store (Goal 6.3).
 *
 * CPU-owned and borrowed surfaces are backed by a framebuffer. Future GPU /
 * display / shared surfaces may use other storage without changing this API.
 *
 * Prefer map / unmap for CPU access (explicit lifetime). pixels() / row()
 * remain available as CPU-only conveniences when the surface is not mapped
 * through a foreign backend (always true for Goal 6.4 CPU surfaces).
 *
 * Threading: not concurrently mutable unless the caller synchronizes.
 * Retain/release is not atomic. Map/unmap is not thread-safe.
 *
 * See graphics/docs/Surface.md.
 */

#ifndef ROBO_GRAPHICS_SURFACE_H
#define ROBO_GRAPHICS_SURFACE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsFramebuffer.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GfxSurfaceOwnership {
    RoboGfxSurfaceOwned = 1,
    RoboGfxSurfaceBorrowed = 2
    /* Future: SHARED, EXTERNAL, GPU — not implemented in Goal 6.4 */
} GfxSurfaceOwnership;

typedef enum GfxSurfaceAccess {
    RoboGfxSurfaceAccessRead = 1,
    RoboGfxSurfaceAccessWrite = 2,
    RoboGfxSurfaceAccessReadWrite = 3
} GfxSurfaceAccess;

typedef struct GfxSurfaceDesc {
    uint32_t width;
    uint32_t height;
    GfxPixelFormat format;
    size_t strideBytes; /* 0 → tightly packed min stride */
    GfxSurfaceAccess access;
} GfxSurfaceDesc;

/**
 * CPU mapping view. Valid only between successful map and matching unmap.
 * Do not retain @p pixels / @p pixelsConst after unmap or surface release.
 */
typedef struct GfxSurfaceMap {
    uint8_t *pixels;             /* NULL when access is read-only */
    const uint8_t *pixelsConst; /* always set when map succeeds */
    uint32_t width;
    uint32_t height;
    size_t strideBytes;
    size_t byteSize;
    GfxPixelFormat format;
    GfxSurfaceAccess access;
} GfxSurfaceMap;

typedef struct GfxSurface GfxSurface;

void GfxSurfaceDescInit(GfxSurfaceDesc *desc);

/**
 * Allocate an owned CPU surface (framebuffer-backed, zero-filled).
 * Invalid dims/format → RoboInvalidArgument.
 * Unsupported CPU format → RoboNotSupported.
 * OOM → RoboOutOfMemory.
 */
Result GfxSurfaceCreate(const GfxSurfaceDesc *desc,
                                      GfxSurface **out);

/**
 * Borrow external pixels via a borrowed framebuffer.
 * @p pixels must remain valid until last surface release.
 */
Result GfxSurfaceCreateBorrowed(const GfxSurfaceDesc *desc,
                                               void *pixels,
                                               GfxSurface **out);

void GfxSurfaceRetain(GfxSurface *surface);
void GfxSurfaceRelease(GfxSurface *surface);

GfxSurfaceId GetGfxSurfaceId(const GfxSurface *surface);
GfxSurfaceOwnership MakeGfxSurfaceOwnership(
    const GfxSurface *surface);
uint32_t GfxSurfaceWidth(const GfxSurface *surface);
uint32_t GfxSurfaceHeight(const GfxSurface *surface);
GfxSize GfxSurfaceSize(const GfxSurface *surface);
GfxPixelFormat GfxSurfaceFormat(const GfxSurface *surface);
uint8_t GfxSurfaceBytesPerPixel(const GfxSurface *surface);
size_t GfxSurfaceStride(const GfxSurface *surface);
size_t GfxSurfaceByteSize(const GfxSurface *surface);
GfxSurfaceAccess MakeGfxSurfaceAccess(const GfxSurface *surface);

/** 1 if currently mapped for CPU access. */
int GfxSurfaceIsMapped(const GfxSurface *surface);

/**
 * Begin a CPU mapping. One active map at a time.
 * @p access must be allowed by the surface's declared access mode.
 * Already mapped → RoboBusy.
 */
Result GetGfxSurfaceMap(GfxSurface *surface,
                                   GfxSurfaceAccess access,
                                   GfxSurfaceMap *out);

/**
 * End the active CPU mapping. Not mapped → RoboInvalidArgument.
 * After unmap, pointers from the last map must not be used.
 */
Result GfxSurfaceUnmap(GfxSurface *surface);

/**
 * CPU convenience pointers (no map required for Goal 6.4 CPU surfaces).
 * Prefer map/unmap when preparing code for future non-CPU backends.
 * Mutable pixels() requires write-capable surface access; otherwise NULL.
 */
uint8_t *GfxSurfacePixels(GfxSurface *surface);
const uint8_t *GfxSurfacePixelsConst(const GfxSurface *surface);

/**
 * Pointer to row @p y. NULL if out of range (legacy convenience).
 * Prefer GfxSurfaceRowChecked for error-aware callers.
 */
uint8_t *GfxSurfaceRow(GfxSurface *surface, uint32_t y);

Result GfxSurfaceRowChecked(GfxSurface *surface,
                                           uint32_t y,
                                           uint8_t **outRow);
Result GfxSurfaceRowConst(const GfxSurface *surface,
                                         uint32_t y,
                                         const uint8_t **outRow);

Result GfxSurfacePixelOffset(const GfxSurface *surface,
                                            uint32_t x,
                                            uint32_t y,
                                            size_t *outOffset);
Result GfxSurfacePixel(GfxSurface *surface,
                                     uint32_t x,
                                     uint32_t y,
                                     uint8_t **outPixel);
Result GfxSurfacePixelConst(const GfxSurface *surface,
                                           uint32_t x,
                                           uint32_t y,
                                           const uint8_t **outPixel);

/**
 * Fill every pixel using Goal 6.2 pack conversion from @p color.
 * Requires write-capable access. Unsupported format → RoboNotSupported.
 */
Result GfxSurfaceClearRgba8(GfxSurface *surface,
                                           GfxColorRgba8 color);

/**
 * Borrowed view of the underlying CPU framebuffer (retained by the surface).
 * NULL if the surface has no framebuffer backing (future GPU-only paths).
 * Do not release the returned pointer — owned by the surface.
 */
GfxFramebuffer *GfxSurfaceFramebuffer(GfxSurface *surface);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_SURFACE_H */
