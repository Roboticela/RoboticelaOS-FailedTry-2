#ifndef ROBO_GFX_H
#define ROBO_GFX_H

#include <stdint.h>
#include "roboticela/bootinfo.h"

#define ROBO_SURF_OWNED 1u

typedef struct RoboDisplayInfo {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t pixfmt;
    void *framebuffer;
} RoboDisplayInfo;

typedef struct RoboSurface {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixfmt;
    uint32_t flags;
    uint32_t *pixels;
} RoboSurface;

int robo_gfx_init(const RoboBootInfo *info);
int robo_display_get_info(RoboDisplayInfo *out);
RoboSurface *robo_surface_create(uint32_t w, uint32_t h);
RoboSurface *robo_surface_wrap(uint32_t *pixels, uint32_t w, uint32_t h,
                               uint32_t stride, uint32_t pixfmt);
void robo_surface_destroy(RoboSurface *s);
void robo_surface_clear(RoboSurface *s, uint32_t argb);
void robo_surface_pixel(RoboSurface *s, int x, int y, uint32_t argb);
void robo_surface_line(RoboSurface *s, int x0, int y0, int x1, int y1,
                       uint32_t argb);
void robo_surface_rect(RoboSurface *s, int x, int y, int w, int h,
                       uint32_t argb);
void robo_surface_round_rect(RoboSurface *s, int x, int y, int w, int h,
                             int r, uint32_t argb);
void robo_surface_bitmap(RoboSurface *s, int x, int y, uint32_t w, uint32_t h,
                         const uint32_t *src, uint32_t src_stride);
void robo_surface_blend(RoboSurface *s, int x, int y, uint32_t argb);
void robo_text_draw(RoboSurface *s, int x, int y, const char *str,
                    uint32_t argb);
int robo_surface_present(const RoboSurface *s);
int robo_gfx_selftest(void);
int robo_gfx_ready(void);

#endif
