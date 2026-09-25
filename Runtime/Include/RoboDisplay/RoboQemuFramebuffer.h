/*
 * QEMU / UEFI GOP framebuffer model (Goal 7.12).
 *
 * Matches BootInfo / SYS_DISPLAY fields: pitch in bytes, bpp typically 32,
 * pixfmt RoboPixBgrx (0) or RoboPixRgbx (1).
 *
 * Direct-memory present is used by the host simulator. Guest presentation
 * normally goes through SYS_DISPLAY; this helper still validates metadata
 * and can blit when a writable mapping is available.
 */

#ifndef ROBO_QEMU_FRAMEBUFFER_H
#define ROBO_QEMU_FRAMEBUFFER_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Same values as roboticela/bootinfo.h — duplicated to avoid OS-header deps. */
#define RoboQemuPixBgrx 0u
#define RoboQemuPixRgbx 1u

typedef struct QemuFbInfo {
    uint32_t width;
    uint32_t height;
    uint32_t pitchBytes; /* bytes per scanline */
    uint32_t bpp;         /* bits per pixel; 32 expected */
    uint32_t pixfmt;      /* ROBO_QEMU_PIX_* */
    void *pixels;         /* NULL = metadata-only (syscall present path) */
    size_t byteSize;     /* validated total mapping size when pixels set */
    int ready;
    int headless; /* claimed without a physical FB */
} QemuFbInfo;

void QemuFbInfoInit(QemuFbInfo *info);

/**
 * Validate width/height/pitch/bpp/size. Rejects overflow and impossible
 * geometries. pixels may be NULL (metadata-only).
 */
Result QemuFbInfoValidate(const QemuFbInfo *info);

/** Preferred RoboGraphics format for @p pixfmt; UNKNOWN if unsupported. */
GfxPixelFormat QemuFbGfxFormat(uint32_t pixfmt);

/** Map RoboGraphics format → QEMU pixfmt; fails if unsupported. */
Result QemuFbPixfmtFromGfx(GfxPixelFormat format,
                                           uint32_t *outPixfmt);

/**
 * Synchronous CPU blit of @p surface into @p info->pixels at dest.
 * Requires validated info with non-NULL pixels. Converts RGBA8↔BGRA8 when
 * surface format differs from FB pixfmt; RGB8 packed into X=0xFF.
 */
Result QemuFbPresentSurface(const QemuFbInfo *info,
                                           const GfxSurface *surface,
                                           int32_t destX,
                                           int32_t destY);

/**
 * Blit only @p srcRect of @p surface into the FB at the same origin.
 * Empty rect → RoboOk. Used for software-cursor damage presents.
 */
Result QemuFbPresentSurfaceRegion(const QemuFbInfo *info,
                                                 const GfxSurface *surface,
                                                 GfxRect srcRect);

/**
 * Read one packed 32-bit pixel from FB (host sim / tests).
 * Out of range → RoboInvalidArgument.
 */
Result QemuFbGetPixel(const QemuFbInfo *info,
                                     uint32_t x,
                                     uint32_t y,
                                     uint32_t *outPx);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_QEMU_FRAMEBUFFER_H */
