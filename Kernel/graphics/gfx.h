#ifndef ROBO_GFX_H
#define ROBO_GFX_H

#include <stdint.h>
#include "roboticela/bootinfo.h"

#define RoboSurfOwned 1u

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

int GfxInit(const RoboBootInfo *info);
int DisplayGetInfo(RoboDisplayInfo *out);
RoboSurface *SurfaceCreate(uint32_t w, uint32_t h);
RoboSurface *SurfaceWrap(uint32_t *pixels, uint32_t w, uint32_t h,
                               uint32_t stride, uint32_t pixfmt);
void SurfaceDestroy(RoboSurface *s);
void SurfaceClear(RoboSurface *s, uint32_t argb);
void SurfacePixel(RoboSurface *s, int x, int y, uint32_t argb);
void SurfaceLine(RoboSurface *s, int x0, int y0, int x1, int y1,
                       uint32_t argb);
void SurfaceRect(RoboSurface *s, int x, int y, int w, int h,
                       uint32_t argb);
void SurfaceRoundRect(RoboSurface *s, int x, int y, int w, int h,
                             int r, uint32_t argb);
void SurfaceBitmap(RoboSurface *s, int x, int y, uint32_t w, uint32_t h,
                         const uint32_t *src, uint32_t srcStride);
void SurfaceBlend(RoboSurface *s, int x, int y, uint32_t argb);
void TextDraw(RoboSurface *s, int x, int y, const char *str,
                    uint32_t argb);
int SurfacePresent(const RoboSurface *s);
int GfxSelftest(void);
int GfxReady(void);

#endif
