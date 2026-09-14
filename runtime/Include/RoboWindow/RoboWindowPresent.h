/*
 * RoboWindow ↔ RoboGraphics presentation (Goal 7.4).
 *
 * Flow:
 *   RoboGraphics surface / framebuffer
 *           ↓ validate (format, stride, dims, lifetime)
 *   RoboWindow present API
 *           ↓ synchronous call
 *   native presentation backend (mock / future host / QEMU)
 *
 * Ownership: present reads the source only for the duration of the call.
 * Backends must not retain caller surfaces after return unless documented
 * (test mock may copy pixels for verification — production backends must not
 * pretend an OS window was shown).
 *
 * No VSync, GPU, or host window output in Goal 7.4.
 *
 * Chromium compositor/surfaces are technical reference only — not a dependency.
 */

#ifndef ROBO_WINDOW_PRESENT_H
#define ROBO_WINDOW_PRESENT_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsFramebuffer.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboWindow/RoboWindowCapabilities.h"
#include "RoboWindow/RoboWindowTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bit in present_format_mask for @p format (1u << format enum value). */
#define ROBO_WINDOW_PRESENT_FMT_BIT(fmt) (1u << (unsigned)(fmt))

#define ROBO_WINDOW_PRESENT_FMT_RGBA8 \
    ROBO_WINDOW_PRESENT_FMT_BIT(ROBO_GFX_FORMAT_RGBA8)
#define ROBO_WINDOW_PRESENT_FMT_BGRA8 \
    ROBO_WINDOW_PRESENT_FMT_BIT(ROBO_GFX_FORMAT_BGRA8)
#define ROBO_WINDOW_PRESENT_FMT_RGB8 \
    ROBO_WINDOW_PRESENT_FMT_BIT(ROBO_GFX_FORMAT_RGB8)
#define ROBO_WINDOW_PRESENT_FMT_GRAY8 \
    ROBO_WINDOW_PRESENT_FMT_BIT(ROBO_GFX_FORMAT_GRAY8)
#define ROBO_WINDOW_PRESENT_FMT_ALPHA8 \
    ROBO_WINDOW_PRESENT_FMT_BIT(ROBO_GFX_FORMAT_ALPHA8)

/** Default CPU presentable set for mock / early backends. */
#define ROBO_WINDOW_PRESENT_FMT_CPU_DEFAULT                              \
    (ROBO_WINDOW_PRESENT_FMT_RGBA8 | ROBO_WINDOW_PRESENT_FMT_BGRA8 |      \
     ROBO_WINDOW_PRESENT_FMT_RGB8)

/**
 * Borrow @p surface as the default present source for @p id.
 * NULL clears the association. Does not retain — caller keeps the surface
 * alive until present completes / association cleared.
 */
robo_result_t robo_window_set_present_surface(robo_window_id_t id,
                                              robo_gfx_surface_t *surface);

/** Current borrowed present surface (NULL if unset / invalid id). */
robo_gfx_surface_t *robo_window_get_present_surface(robo_window_id_t id);

/**
 * Synchronously present @p surface, or the associated surface when @p surface
 * is NULL.
 *
 * Validates window, thread, surface, format, stride, and caps, then invokes
 * the backend. Success means the backend accepted the frame — not that a
 * host OS window painted pixels (Goal 7.4 has no host display).
 */
robo_result_t robo_window_present(robo_window_id_t id,
                                  robo_gfx_surface_t *surface);

/**
 * Present a framebuffer by wrapping it in a temporary borrowed surface for
 * the duration of the call (no ownership transfer of @p fb).
 */
robo_result_t robo_window_present_framebuffer(robo_window_id_t id,
                                              robo_gfx_framebuffer_t *fb);

/**
 * Validate a surface against presentation rules and optional @p caps.
 * @p caps NULL → format/size rules only (no backend mask).
 *
 * Errors: ROBO_INVALID_ARGUMENT (null/zero/stride), ROBO_NOT_SUPPORTED
 * (unimplemented or backend-unsupported format / oversize).
 */
robo_result_t robo_window_present_validate_surface(
    const robo_gfx_surface_t *surface,
    const robo_window_backend_caps_t *caps);

/**
 * Validate a framebuffer for presentation (same rules as surface backing).
 */
robo_result_t robo_window_present_validate_framebuffer(
    const robo_gfx_framebuffer_t *fb,
    const robo_window_backend_caps_t *caps);

/** Non-zero if active backend lists @p format in present_format_mask. */
int robo_window_present_format_supported(robo_gfx_pixel_format_t format);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_PRESENT_H */
