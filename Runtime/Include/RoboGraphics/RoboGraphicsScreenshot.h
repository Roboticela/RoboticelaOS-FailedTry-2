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
 * currently always RoboNotSupported (no double-buffer abstraction yet).
 */
typedef enum GfxCaptureSource {
    RoboGfxCaptureSurface = 1,
    RoboGfxCaptureFramebuffer = 2,
    RoboGfxCaptureFrontBuffer = 3,
    RoboGfxCaptureBackBuffer = 4
} GfxCaptureSource;

/**
 * Region resolution policy.
 *
 * STRICT: region must be non-empty and fully enclosed by source bounds.
 * CLIP:   region is intersected with source bounds; empty after clip fails.
 *
 * When useFullExtent is set, the region field is ignored.
 */
typedef enum GfxCaptureRegionPolicy {
    RoboGfxCaptureRegionStrict = 1,
    RoboGfxCaptureRegionClip = 2
} GfxCaptureRegionPolicy;

typedef struct GfxCaptureOptions {
    GfxCaptureRegionPolicy regionPolicy;
    /** 1 → capture entire source; ignore @p region. */
    int useFullExtent;
    GfxRect region;
} GfxCaptureOptions;

typedef struct GfxScreenshot GfxScreenshot;

void GfxCaptureOptionsInit(GfxCaptureOptions *opts);

/**
 * Capture an immutable owned copy of @p surface pixels.
 * Does not modify @p surface.
 */
Result GfxScreenshotCaptureSurface(
    const GfxSurface *surface,
    const GfxCaptureOptions *opts,
    GfxScreenshot **out);

/**
 * Capture an immutable owned copy of @p framebuffer pixels.
 * Does not modify @p framebuffer.
 */
Result GfxScreenshotCaptureFramebuffer(
    const GfxFramebuffer *framebuffer,
    const GfxCaptureOptions *opts,
    GfxScreenshot **out);

/**
 * Generic capture by source kind.
 * SURFACE requires @p surface; FRAMEBUFFER requires @p framebuffer.
 * FRONT/BACK → RoboNotSupported until present/swap exists.
 */
Result GfxScreenshotCapture(
    GfxCaptureSource source,
    const GfxSurface *surface,
    const GfxFramebuffer *framebuffer,
    const GfxCaptureOptions *opts,
    GfxScreenshot **out);

void GfxScreenshotRetain(GfxScreenshot *shot);
void GfxScreenshotRelease(GfxScreenshot *shot);

uint32_t GfxScreenshotWidth(const GfxScreenshot *shot);
uint32_t GfxScreenshotHeight(const GfxScreenshot *shot);
GfxPixelFormat GfxScreenshotFormat(
    const GfxScreenshot *shot);

/**
 * Borrowed snapshot surface (retained by the screenshot).
 * Independent of the original source after capture returns.
 */
GfxSurface *GfxScreenshotSurface(GfxScreenshot *shot);

/** Wrap snapshot as a raster image (retains the snapshot surface). */
Result GfxScreenshotToImage(GfxScreenshot *shot,
                                           GfxImage **out);

/**
 * Encode via Phase 6.13. Alpha/opaque policy comes from @p encodeOpts.
 * Does not mutate the screenshot.
 */
Result GfxScreenshotExport(
    const GfxScreenshot *shot,
    const GfxEncodeOptions *encodeOpts,
    GfxEncodedBuffer *out);

Result GfxScreenshotExportToSink(
    const GfxScreenshot *shot,
    const GfxEncodeOptions *encodeOpts,
    GfxEncodeSinkFn sink,
    void *user);

/**
 * One-shot: capture surface → encode → memory buffer.
 * Temporary snapshot is released before return.
 */
Result GfxSurfaceExportScreenshot(
    const GfxSurface *surface,
    const GfxCaptureOptions *captureOpts,
    const GfxEncodeOptions *encodeOpts,
    GfxEncodedBuffer *out);

Result GfxFramebufferExportScreenshot(
    const GfxFramebuffer *framebuffer,
    const GfxCaptureOptions *captureOpts,
    const GfxEncodeOptions *encodeOpts,
    GfxEncodedBuffer *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_SCREENSHOT_H */
