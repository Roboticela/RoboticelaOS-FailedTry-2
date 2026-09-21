/*
 * RoboGraphics fundamental IDs and limits (Goal 6.1).
 *
 * Coordinate / color / format details live in sibling headers.
 * See graphics/docs/coordinates.md.
 */

#ifndef ROBO_GRAPHICS_TYPES_H
#define ROBO_GRAPHICS_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque identity for a surface (stable while the surface object lives). */
typedef uint64_t GfxSurfaceId;

/** Opaque identity for a framebuffer (stable while the framebuffer lives). */
typedef uint64_t GfxFramebufferId;

/** Opaque identity for an image object. */
typedef uint64_t GfxImageId;

/** Monotonic frame identifier within a process (0 = unset). */
typedef uint64_t GfxFrameId;

/** Backend identifier (not a heap object). */
typedef uint32_t GfxBackendId;

#define RoboGfxSurfaceIdNone ((GfxSurfaceId)0u)
#define RoboGfxFramebufferIdNone ((GfxFramebufferId)0u)
#define RoboGfxImageIdNone ((GfxImageId)0u)
#define RoboGfxFrameIdNone ((GfxFrameId)0u)

/**
 * Soft architectural ceiling for CPU-owned surfaces in Goal 6.1.
 * Later goals may raise via capability query; never exceed hard max.
 */
#define RoboGfxMaxSurfaceDim 16384u
#define RoboGfxMaxSurfaceDimHard 65536u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_TYPES_H */
