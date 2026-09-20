/*
 * RoboPaint background painting (Phase 11.4) — see RoboPaintBackground.h.
 *
 * The pass: walk the display list once, fill each `draw-background` command into a
 * RoboGraphics raster, count everything else. The raster is the graphics module's own
 * immediate-mode CPU context, so the clip and the source-over blend are its, and the two
 * things this file owns are the walk and the per-command clip derived from the boundary the
 * paint pass carried.
 *
 * The clip is applied with the raster's own `reset_clip`/`clip`/`set_clip` rather than by
 * intersecting rectangles here, and the reason is that the raster's clip is *already* the
 * one place a draw is bounded: reset to the surface bounds, replace the axis the boundary
 * hides, and let `set_clip` intersect -- so "the boundary bounds the fill" is the graphics
 * module's arithmetic, not a second copy of it.
 *
 * Nothing here reads a tree, a computed style or the box model: a display command carries the
 * one rectangle to fill and the boundary in force, and the only conversion left is the colour
 * (`RoboPaintColor.h`).
 */

#include "RoboPaint/RoboPaintBackground.h"

#include "RoboGraphics/RoboGraphicsRaster.h"
#include "RoboGraphics/RoboGraphicsColor.h"

#include "RoboPaint/RoboPaintColor.h"

#include "RoboPaintInternal.h"

#include <string.h>

void robo_paint_background_stats_init(robo_paint_background_stats_t *stats)
{
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

/**
 * Reset @p raster to the surface bounds and, when @p clip bounds an axis, bound that axis.
 *
 * Reset-then-replace rather than intersect, because the raster's own clip is the one place a
 * fill is bounded: the axis the boundary leaves open keeps the surface's own interval, and the
 * axis it hides takes the boundary's. @p out_applied is set only when a boundary was applied,
 * so "drawn under a clip" stays a fact about the command rather than about the surface.
 */
static robo_result_t background_apply_clip(robo_gfx_raster_t *raster,
                                          const robo_paint_clip_t *clip,
                                          int *out_applied,
                                          robo_paint_diag_t *diag)
{
    robo_gfx_rect_t full;
    robo_gfx_rect_t wanted;

    *out_applied = 0;

    /* Every command starts from the surface bounds, so a boundary from the previous command
     * cannot leak onto this one. */
    if (robo_gfx_raster_reset_clip(raster) != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                               "the raster could not reset its clip to the surface bounds");
    }
    if (!robo_paint_clip_is_active(clip)) {
        return ROBO_OK;
    }
    if (robo_gfx_raster_clip(raster, &full) != ROBO_OK) {
        /* The surface's drawable area is not representable as int32 pixel bounds -- which the
         * raster's own creation already refused -- so this is a graphics-layer failure rather
         * than a document one. */
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                               "the surface bounds are not representable");
    }

    wanted = full;
    if (robo_paint_clip_axis_is_active(&clip->horizontal)) {
        /* The interval is half-open and validated non-negative by the clip record, and the
         * extent saturates rather than wrapping for the reason every accumulation in this
         * module does. */
        wanted.x = clip->horizontal.start;
        wanted.width = robo_paint_extent((int64_t)clip->horizontal.end -
                                         (int64_t)clip->horizontal.start);
    }
    if (robo_paint_clip_axis_is_active(&clip->vertical)) {
        wanted.y = clip->vertical.start;
        wanted.height = robo_paint_extent((int64_t)clip->vertical.end -
                                          (int64_t)clip->vertical.start);
    }
    if (robo_gfx_raster_set_clip(raster, wanted) != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                               "the boundary could not be applied to the raster clip");
    }
    *out_applied = 1;
    return ROBO_OK;
}

robo_result_t robo_paint_background_paint(const robo_paint_display_list_t *list,
                                          robo_gfx_surface_t *target,
                                          robo_paint_background_stats_t *out_stats,
                                          robo_paint_diag_t *diag)
{
    robo_paint_background_stats_t stats;
    robo_gfx_raster_t *raster = NULL;
    robo_result_t status;
    size_t index;
    size_t count;

    if (!list || !target) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_INVALID_ARGUMENT,
                               "the display list and the target surface are required");
    }

    robo_paint_background_stats_init(&stats);

    /*
     * The raster is the graphics module's, so the clip and the blend are its rules. Its
     * creation enforces that the target is write-capable and of an implemented format, which
     * is why a read-only surface refuses here rather than at the first fill.
     *
     * Its two refusals arrive as **different codes** -- a target that cannot take a write is
     * `ROBO_INVALID_ARGUMENT` to the graphics module and an unimplemented format is
     * `ROBO_NOT_SUPPORTED` -- and both are the same answer one level up: there is nothing to
     * paint into. This door says so once, with the code this header names, which is why the
     * mapping is here rather than two branches that would each have to be read separately.
     * `ROBO_OUT_OF_MEMORY` is **not** folded in: that is this build failing rather than the
     * caller's surface being wrong, and a caller retrying after freeing something deserves to
     * be told which it was.
     */
    status = robo_gfx_raster_create(target, &raster);
    if (status == ROBO_OUT_OF_MEMORY) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_OUT_OF_MEMORY,
                               "a raster could not be allocated for the target surface");
    }
    if (status != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_NOT_SUPPORTED,
                               "the target surface cannot be rasterised (it is read-only or "
                               "of a format this build does not implement)");
    }

    count = robo_paint_display_list_count(list);
    for (index = 0u; index < count; index++) {
        robo_paint_display_command_t command;
        robo_gfx_color_rgba8_t color;
        int clipped = 0;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            /* Unreachable: the index came from the list's own count. */
            robo_gfx_raster_destroy(raster);
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                                   "the display list changed under the pass");
        }
        stats.commands++;

        if (command.kind != ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND) {
            /* Structural records draw nothing and a border band is a later goal's -- skipped
             * and counted, so "the backgrounds are painted and the borders are not" is a
             * number a reader sees. */
            stats.skipped++;
            continue;
        }
        stats.backgrounds++;

        /* The colour, before the geometry, because "there was nothing to draw" and "there was
         * nowhere to draw it" are different facts and this is the one a document controls. */
        if (command.color.kind == ROBO_CSS_COLOR_CURRENT) {
            stats.contextual++;
            continue;
        }
        if (!robo_paint_color_is_drawable(command.color)) {
            if (command.color.kind == ROBO_CSS_COLOR_RGBA) {
                stats.transparent++;
            } else {
                /* A kind a valid display command cannot carry. Counted rather than drawn, and
                 * not an error: the list's own validity rule is the door that keeps these
                 * out, and a pass that failed on one would blame the document. */
                stats.skipped++;
            }
            continue;
        }
        if (command.rect.width <= 0 || command.rect.height <= 0) {
            stats.empty++;
            continue;
        }
        if (robo_paint_color_to_rgba8(command.color, &color) != ROBO_OK) {
            /* Unreachable after `is_drawable`, reported rather than guessed at. */
            stats.skipped++;
            continue;
        }

        status = background_apply_clip(raster, &command.clip, &clipped, diag);
        if (status != ROBO_OK) {
            robo_gfx_raster_destroy(raster);
            return status;
        }
        if (clipped) {
            stats.clipped++;
        }

        status = robo_gfx_raster_fill_rect(raster, command.rect, color);
        if (status != ROBO_OK) {
            robo_gfx_raster_destroy(raster);
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                                   "the background rectangle could not be filled");
        }
        stats.filled++;
    }

    robo_gfx_raster_destroy(raster);

    if (out_stats) {
        *out_stats = stats;
    }
    return ROBO_OK;
}
