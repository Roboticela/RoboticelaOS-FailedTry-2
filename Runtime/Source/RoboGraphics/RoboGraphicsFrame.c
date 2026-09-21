#include "RoboGraphics/RoboGraphicsFrame.h"

#include <string.h>

static uint64_t gFrameIdSeq = 1u;

void GfxFrameInit(GfxFrame *frame)
{
    if (!frame) {
        return;
    }
    memset(frame, 0, sizeof(*frame));
}

Result GfxFrameBegin(GfxFrame *frame,
                                   GfxSurface *surface)
{
    if (!frame || !surface) {
        return RoboInvalidArgument;
    }
    if (frame->id == RoboGfxFrameIdNone) {
        frame->id = gFrameIdSeq++;
        if (frame->id == RoboGfxFrameIdNone) {
            frame->id = gFrameIdSeq++;
        }
    }
    frame->target = surface;
    frame->width = GfxSurfaceWidth(surface);
    frame->height = GfxSurfaceHeight(surface);
    frame->format = GfxSurfaceFormat(surface);
    return RoboOk;
}

Result GfxFrameEnd(GfxFrame *frame)
{
    if (!frame || !frame->target) {
        return RoboInvalidArgument;
    }
    return RoboOk;
}

void GfxFrameReset(GfxFrame *frame)
{
    if (!frame) {
        return;
    }
    frame->target = NULL;
    frame->width = 0u;
    frame->height = 0u;
    frame->format = RoboGfxFormatUnknown;
    frame->timestampNs = 0u;
    /* keep id for correlation across reset if desired */
}
