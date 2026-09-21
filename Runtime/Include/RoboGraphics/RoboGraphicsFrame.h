/*
 * RoboGraphics frame metadata (Goal 6.1).
 *
 * Separates "finished raster result" from presentation. VSync / scheduling
 * belong to compositor / window goals — not here.
 */

#ifndef ROBO_GRAPHICS_FRAME_H
#define ROBO_GRAPHICS_FRAME_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboGraphics/RoboGraphicsTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GfxFrame {
    GfxFrameId id;
    GfxSurface *target; /* borrowed; not retained by value struct */
    uint32_t width;
    uint32_t height;
    GfxPixelFormat format;
    uint64_t timestampNs; /* 0 = unset */
} GfxFrame;

void GfxFrameInit(GfxFrame *frame);

/**
 * Bind frame metadata to @p surface (does not retain).
 * Assigns a process-local monotonic id when id was NONE.
 */
Result GfxFrameBegin(GfxFrame *frame,
                                   GfxSurface *surface);

/** Mark frame finished for presentation/test consumers. */
Result GfxFrameEnd(GfxFrame *frame);

/** Clear target pointer; does not release the surface. */
void GfxFrameReset(GfxFrame *frame);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_FRAME_H */
