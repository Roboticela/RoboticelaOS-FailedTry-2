/*
 * QEMU / UEFI GOP framebuffer model (Goal 7.12).
 *
 * Matches BootInfo / SYS_DISPLAY fields: pitch in bytes, bpp typically 32,
 * pixfmt ROBO_PIX_BGRX (0) or ROBO_PIX_RGBX (1).
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
#define ROBO_QEMU_PIX_BGRX 0u
#define ROBO_QEMU_PIX_RGBX 1u

typedef struct robo_qemu_fb_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch_bytes; /* bytes per scanline */
    uint32_t bpp;         /* bits per pixel; 32 expected */
    uint32_t pixfmt;      /* ROBO_QEMU_PIX_* */
    void *pixels;         /* NULL = metadata-only (syscall present path) */
    size_t byte_size;     /* validated total mapping size when pixels set */
    int ready;
    int headless; /* claimed without a physical FB */
} robo_qemu_fb_info_t;

void robo_qemu_fb_info_init(robo_qemu_fb_info_t *info);

/**
 * Validate width/height/pitch/bpp/size. Rejects overflow and impossible
 * geometries. pixels may be NULL (metadata-only).
 */
robo_result_t robo_qemu_fb_info_validate(const robo_qemu_fb_info_t *info);

/** Preferred RoboGraphics format for @p pixfmt; UNKNOWN if unsupported. */
robo_gfx_pixel_format_t robo_qemu_fb_gfx_format(uint32_t pixfmt);

/** Map RoboGraphics format → QEMU pixfmt; fails if unsupported. */
robo_result_t robo_qemu_fb_pixfmt_from_gfx(robo_gfx_pixel_format_t format,
                                           uint32_t *out_pixfmt);

/**
 * Synchronous CPU blit of @p surface into @p info->pixels at dest.
 * Requires validated info with non-NULL pixels. Converts RGBA8↔BGRA8 when
 * surface format differs from FB pixfmt; RGB8 packed into X=0xFF.
 */
robo_result_t robo_qemu_fb_present_surface(const robo_qemu_fb_info_t *info,
                                           const robo_gfx_surface_t *surface,
                                           int32_t dest_x,
                                           int32_t dest_y);

/**
 * Read one packed 32-bit pixel from FB (host sim / tests).
 * Out of range → ROBO_INVALID_ARGUMENT.
 */
robo_result_t robo_qemu_fb_get_pixel(const robo_qemu_fb_info_t *info,
                                     uint32_t x,
                                     uint32_t y,
                                     uint32_t *out_px);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_QEMU_FRAMEBUFFER_H */
