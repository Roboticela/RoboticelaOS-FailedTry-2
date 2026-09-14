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
typedef uint64_t robo_gfx_surface_id_t;

/** Opaque identity for a framebuffer (stable while the framebuffer lives). */
typedef uint64_t robo_gfx_framebuffer_id_t;

/** Opaque identity for an image object. */
typedef uint64_t robo_gfx_image_id_t;

/** Monotonic frame identifier within a process (0 = unset). */
typedef uint64_t robo_gfx_frame_id_t;

/** Backend identifier (not a heap object). */
typedef uint32_t robo_gfx_backend_id_t;

#define ROBO_GFX_SURFACE_ID_NONE ((robo_gfx_surface_id_t)0u)
#define ROBO_GFX_FRAMEBUFFER_ID_NONE ((robo_gfx_framebuffer_id_t)0u)
#define ROBO_GFX_IMAGE_ID_NONE ((robo_gfx_image_id_t)0u)
#define ROBO_GFX_FRAME_ID_NONE ((robo_gfx_frame_id_t)0u)

/**
 * Soft architectural ceiling for CPU-owned surfaces in Goal 6.1.
 * Later goals may raise via capability query; never exceed hard max.
 */
#define ROBO_GFX_MAX_SURFACE_DIM 16384u
#define ROBO_GFX_MAX_SURFACE_DIM_HARD 65536u

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_TYPES_H */
