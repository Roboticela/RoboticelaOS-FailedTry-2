/*
 * RoboGraphics screenshot / framebuffer export (Goal 6.14).
 *
 * Pipeline:
 *   source surface|framebuffer → region resolve → owned pixel snapshot
 *     → (optional) Phase 6.13 encode → memory / sink
 *
 * This is a graphics-surface capture API — not native window, QEMU, or
 * desktop screenshot support (Phase 7+).
 *
 * Synchronization: capture is synchronous and not safe concurrent with
 * mutation of the source. Callers must not write the source while capture
 * runs (same contract as Goal 6.4 surfaces).
 *
 * See graphics/docs/Screenshot.md.
 */

#ifndef ROBO_GRAPHICS_SCREENSHOT_H
#define ROBO_GRAPHICS_SCREENSHOT_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsFramebuffer.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Which buffer to capture.
 *
 * SURFACE / FRAMEBUFFER: caller-supplied storage (current CPU content).
 * FRONT_BUFFER / BACK_BUFFER: reserved for future present/swap chains —
 * currently always ROBO_NOT_SUPPORTED (no double-buffer abstraction yet).
 */
typedef enum robo_gfx_capture_source {
    ROBO_GFX_CAPTURE_SURFACE = 1,
    ROBO_GFX_CAPTURE_FRAMEBUFFER = 2,
    ROBO_GFX_CAPTURE_FRONT_BUFFER = 3,
    ROBO_GFX_CAPTURE_BACK_BUFFER = 4
} robo_gfx_capture_source_t;

/**
 * Region resolution policy.
 *
 * STRICT: region must be non-empty and fully enclosed by source bounds.
 * CLIP:   region is intersected with source bounds; empty after clip fails.
 *
 * When use_full_extent is set, the region field is ignored.
 */
typedef enum robo_gfx_capture_region_policy {
    ROBO_GFX_CAPTURE_REGION_STRICT = 1,
    ROBO_GFX_CAPTURE_REGION_CLIP = 2
} robo_gfx_capture_region_policy_t;

typedef struct robo_gfx_capture_options {
    robo_gfx_capture_region_policy_t region_policy;
    /** 1 → capture entire source; ignore @p region. */
    int use_full_extent;
    robo_gfx_rect_t region;
} robo_gfx_capture_options_t;

typedef struct robo_gfx_screenshot robo_gfx_screenshot_t;

void robo_gfx_capture_options_init(robo_gfx_capture_options_t *opts);

/**
 * Capture an immutable owned copy of @p surface pixels.
 * Does not modify @p surface.
 */
robo_result_t robo_gfx_screenshot_capture_surface(
    const robo_gfx_surface_t *surface,
    const robo_gfx_capture_options_t *opts,
    robo_gfx_screenshot_t **out);

/**
 * Capture an immutable owned copy of @p framebuffer pixels.
 * Does not modify @p framebuffer.
 */
robo_result_t robo_gfx_screenshot_capture_framebuffer(
    const robo_gfx_framebuffer_t *framebuffer,
    const robo_gfx_capture_options_t *opts,
    robo_gfx_screenshot_t **out);

/**
 * Generic capture by source kind.
 * SURFACE requires @p surface; FRAMEBUFFER requires @p framebuffer.
 * FRONT/BACK → ROBO_NOT_SUPPORTED until present/swap exists.
 */
robo_result_t robo_gfx_screenshot_capture(
    robo_gfx_capture_source_t source,
    const robo_gfx_surface_t *surface,
    const robo_gfx_framebuffer_t *framebuffer,
    const robo_gfx_capture_options_t *opts,
    robo_gfx_screenshot_t **out);

void robo_gfx_screenshot_retain(robo_gfx_screenshot_t *shot);
void robo_gfx_screenshot_release(robo_gfx_screenshot_t *shot);

uint32_t robo_gfx_screenshot_width(const robo_gfx_screenshot_t *shot);
uint32_t robo_gfx_screenshot_height(const robo_gfx_screenshot_t *shot);
robo_gfx_pixel_format_t robo_gfx_screenshot_format(
    const robo_gfx_screenshot_t *shot);

/**
 * Borrowed snapshot surface (retained by the screenshot).
 * Independent of the original source after capture returns.
 */
robo_gfx_surface_t *robo_gfx_screenshot_surface(robo_gfx_screenshot_t *shot);

/** Wrap snapshot as a raster image (retains the snapshot surface). */
robo_result_t robo_gfx_screenshot_to_image(robo_gfx_screenshot_t *shot,
                                           robo_gfx_image_t **out);

/**
 * Encode via Phase 6.13. Alpha/opaque policy comes from @p encode_opts.
 * Does not mutate the screenshot.
 */
robo_result_t robo_gfx_screenshot_export(
    const robo_gfx_screenshot_t *shot,
    const robo_gfx_encode_options_t *encode_opts,
    robo_gfx_encoded_buffer_t *out);

robo_result_t robo_gfx_screenshot_export_to_sink(
    const robo_gfx_screenshot_t *shot,
    const robo_gfx_encode_options_t *encode_opts,
    robo_gfx_encode_sink_fn sink,
    void *user);

/**
 * One-shot: capture surface → encode → memory buffer.
 * Temporary snapshot is released before return.
 */
robo_result_t robo_gfx_surface_export_screenshot(
    const robo_gfx_surface_t *surface,
    const robo_gfx_capture_options_t *capture_opts,
    const robo_gfx_encode_options_t *encode_opts,
    robo_gfx_encoded_buffer_t *out);

robo_result_t robo_gfx_framebuffer_export_screenshot(
    const robo_gfx_framebuffer_t *framebuffer,
    const robo_gfx_capture_options_t *capture_opts,
    const robo_gfx_encode_options_t *encode_opts,
    robo_gfx_encoded_buffer_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_SCREENSHOT_H */
