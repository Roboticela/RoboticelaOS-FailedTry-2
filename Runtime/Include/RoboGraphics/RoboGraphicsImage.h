/*
 * RoboGraphics image abstraction (Goal 6.1).
 *
 * Images are format-agnostic containers. Encoding of raster pixels is Goal
 * 6.13 (RoboGraphicsEncode.h). Decoding (PNG/JPEG/…) remains later.
 * Goal 6.1: raster images backed by a surface, or metadata-only placeholders.
 */

#ifndef ROBO_GRAPHICS_IMAGE_H
#define ROBO_GRAPHICS_IMAGE_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboGraphics/RoboGraphicsTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GfxImageKind {
    RoboGfxImageKindRaster = 1,
    RoboGfxImageKindExternal = 2, /* metadata / future GPU / foreign buffer */
    RoboGfxImageKindEmpty = 3
} GfxImageKind;

typedef struct GfxImageDesc {
    uint32_t width;
    uint32_t height;
    GfxPixelFormat format;
    GfxImageKind kind;
} GfxImageDesc;

typedef struct GfxImage GfxImage;

void GfxImageDescInit(GfxImageDesc *desc);

/** Create an empty metadata image (no pixels). */
Result GfxImageCreateEmpty(const GfxImageDesc *desc,
                                          GfxImage **out);

/** Create a raster image that retains @p surface. */
Result GfxImageCreateFromSurface(GfxSurface *surface,
                                                 GfxImage **out);

void GfxImageRetain(GfxImage *image);
void GfxImageRelease(GfxImage *image);

GfxImageId GetGfxImageId(const GfxImage *image);
GfxImageKind MakeGfxImageKind(const GfxImage *image);
uint32_t GfxImageWidth(const GfxImage *image);
uint32_t GfxImageHeight(const GfxImage *image);
GfxPixelFormat GfxImageFormat(const GfxImage *image);

/**
 * Borrowed surface pointer for raster images (retained by the image).
 * NULL for empty/external without surface.
 */
GfxSurface *GfxImageSurface(GfxImage *image);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_IMAGE_H */
