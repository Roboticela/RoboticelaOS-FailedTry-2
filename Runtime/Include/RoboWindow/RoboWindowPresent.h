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

/** Bit in presentFormatMask for @p format (1u << format enum value). */
#define RoboWindowPresentFmtBit(fmt) (1u << (unsigned)(fmt))

#define RoboWindowPresentFmtRgba8 \
    RoboWindowPresentFmtBit(RoboGfxFormatRgba8)
#define RoboWindowPresentFmtBgra8 \
    RoboWindowPresentFmtBit(RoboGfxFormatBgra8)
#define RoboWindowPresentFmtRgb8 \
    RoboWindowPresentFmtBit(RoboGfxFormatRgb8)
#define RoboWindowPresentFmtGray8 \
    RoboWindowPresentFmtBit(RoboGfxFormatGray8)
#define RoboWindowPresentFmtAlpha8 \
    RoboWindowPresentFmtBit(RoboGfxFormatAlpha8)

/** Default CPU presentable set for mock / early backends. */
#define RoboWindowPresentFmtCpuDefault                              \
    (RoboWindowPresentFmtRgba8 | RoboWindowPresentFmtBgra8 |      \
     RoboWindowPresentFmtRgb8)

/**
 * Borrow @p surface as the default present source for @p id.
 * NULL clears the association. Does not retain — caller keeps the surface
 * alive until present completes / association cleared.
 */
Result WindowSetPresentSurface(WindowId id,
                                              GfxSurface *surface);

/** Current borrowed present surface (NULL if unset / invalid id). */
GfxSurface *WindowGetPresentSurface(WindowId id);

/**
 * Synchronously present @p surface, or the associated surface when @p surface
 * is NULL.
 *
 * Validates window, thread, surface, format, stride, and caps, then invokes
 * the backend. Success means the backend accepted the frame — not that a
 * host OS window painted pixels (Goal 7.4 has no host display).
 */
Result WindowPresent(WindowId id,
                                  GfxSurface *surface);

/**
 * Present a framebuffer by wrapping it in a temporary borrowed surface for
 * the duration of the call (no ownership transfer of @p fb).
 */
Result WindowPresentFramebuffer(WindowId id,
                                              GfxFramebuffer *fb);

/**
 * Validate a surface against presentation rules and optional @p caps.
 * @p caps NULL → format/size rules only (no backend mask).
 *
 * Errors: RoboInvalidArgument (null/zero/stride), RoboNotSupported
 * (unimplemented or backend-unsupported format / oversize).
 */
Result WindowPresentValidateSurface(
    const GfxSurface *surface,
    const WindowBackendCaps *caps);

/**
 * Validate a framebuffer for presentation (same rules as surface backing).
 */
Result WindowPresentValidateFramebuffer(
    const GfxFramebuffer *fb,
    const WindowBackendCaps *caps);

/** Non-zero if active backend lists @p format in presentFormatMask. */
int WindowPresentFormatSupported(GfxPixelFormat format);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_PRESENT_H */
