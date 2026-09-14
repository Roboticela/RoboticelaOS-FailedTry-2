#include "RoboGraphics/RoboGraphicsFrame.h"

#include <string.h>

static uint64_t g_frame_id_seq = 1u;

void robo_gfx_frame_init(robo_gfx_frame_t *frame)
{
    if (!frame) {
        return;
    }
    memset(frame, 0, sizeof(*frame));
}

robo_result_t robo_gfx_frame_begin(robo_gfx_frame_t *frame,
                                   robo_gfx_surface_t *surface)
{
    if (!frame || !surface) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (frame->id == ROBO_GFX_FRAME_ID_NONE) {
        frame->id = g_frame_id_seq++;
        if (frame->id == ROBO_GFX_FRAME_ID_NONE) {
            frame->id = g_frame_id_seq++;
        }
    }
    frame->target = surface;
    frame->width = robo_gfx_surface_width(surface);
    frame->height = robo_gfx_surface_height(surface);
    frame->format = robo_gfx_surface_format(surface);
    return ROBO_OK;
}

robo_result_t robo_gfx_frame_end(robo_gfx_frame_t *frame)
{
    if (!frame || !frame->target) {
        return ROBO_INVALID_ARGUMENT;
    }
    return ROBO_OK;
}

void robo_gfx_frame_reset(robo_gfx_frame_t *frame)
{
    if (!frame) {
        return;
    }
    frame->target = NULL;
    frame->width = 0u;
    frame->height = 0u;
    frame->format = ROBO_GFX_FORMAT_UNKNOWN;
    frame->timestamp_ns = 0u;
    /* keep id for correlation across reset if desired */
}
