/*
 * RoboPaint background painting (Phase 11.4).
 *
 * The first place in this module where a rectangle and a colour reach a **surface**. The
 * pipeline stays exactly the one the previous phases built, and this is its last arrow
 * filled in:
 *
 *   HTML / DOM → RoboCSS → RoboLayout → RoboPaint → records → display list
 *       → **draw-background** → RoboGraphics raster fill → the caller's surface
 *
 * It is deliberately a **background-only** painter, and the fence is worth stating because
 * the temptation to rasterise "the whole list" here is exactly the thing to avoid: the
 * display list's other kinds are *structural* (`begin-node`, `content`, `end-node`, which
 * draw nothing) and `draw-border` (whose band rectangle is already resolved, but whose
 * rasterisation belongs to a later goal). So this door walks a display list, fills the
 * `draw-background` commands, **skips** everything else, and counts what it skipped -- which
 * makes "the backgrounds are painted and the borders are not" a number a reader sees rather
 * than a silence. `robo_paint_get_capabilities().raster` stays **0** for that reason: a
 * general rasteriser that consumes *every* command is a later goal's, and this one consumes
 * one kind.
 *
 * **The list is the input, not the records**, because the display list is the artifact whose
 * geometry is resolved -- a command carries the one rectangle to fill, so the painter never
 * reads the box model, a computed style or the tree. The only conversion left is the colour,
 * and that is `RoboPaintColor.h`'s.
 *
 * **Clipping is read, not implemented.** A command carries the boundary in force from the
 * paint pass (Phase 11.1) as two axes. When an axis is bounded it is applied to the RoboGraphics
 * raster's clip, and when it is not the axis is left at the surface's own bounds -- so an
 * `overflow: hidden` ancestor bounds a descendant's background, which is the correct CSS
 * behaviour for a box's own background paint, while no new clipping architecture is invented.
 *
 * **Ownership is the caller's.** The surface belongs to whoever made it; this door retains
 * nothing and destroys nothing. The list is read through `const` and never mutated.
 */
#ifndef ROBO_PAINT_BACKGROUND_H
#define ROBO_PAINT_BACKGROUND_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintDisplayList.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What one background pass did.
 *
 * A record by value, owned by nobody, and the interesting facts are the *pairs*:
 * `backgrounds` against `commands` is how much of the list was drawable at all; `filled`
 * against `backgrounds` is how many actually put a colour down; `transparent` and
 * `contextual` are the two reasons a background command draws nothing, and keeping them
 * apart is what tells "the author asked for nothing" from "the colour could not be resolved";
 * `clipped` is how many were drawn under a boundary from an ancestor's overflow.
 */
typedef struct robo_paint_background_stats {
    /** Display commands visited. */
    size_t commands;
    /** `draw-background` commands seen. */
    size_t backgrounds;
    /** Backgrounds that put a colour on the surface. */
    size_t filled;
    /** Commands that are not backgrounds: structural records and border bands. */
    size_t skipped;
    /** Backgrounds whose colour stayed `currentColor` and so could not be drawn here. */
    size_t contextual;
    /** Backgrounds whose colour is fully transparent (nothing to draw). */
    size_t transparent;
    /** Backgrounds with a zero-area rectangle (nothing to draw). */
    size_t empty;
    /** Backgrounds drawn under a boundary carried by the command's clip. */
    size_t clipped;
} robo_paint_background_stats_t;

/** Clear @p stats to all zeroes. Safe on NULL. */
void robo_paint_background_stats_init(robo_paint_background_stats_t *stats);

/**
 * Fill every `draw-background` command of @p list into @p target.
 *
 * Walks @p list in order, and for each command:
 *
 *   a `draw-background`: resolve its colour (`robo_paint_color_to_rgba8`), apply the command's
 *   clip, and fill its rectangle with `robo_gfx_raster_fill_rect`. A colour that is not a
 *   resolved one is **counted and skipped** (`contextual`/`transparent`), and a zero-area
 *   rectangle is counted (`empty`) -- each is a statement about the document rather than a
 *   failure, which is why the pass still succeeds;
 *
 *   anything else (`begin-node`, `content`, `end-node`, `draw-border`): **skipped** and
 *   counted. The structural commands draw nothing; a border band is a later goal's.
 *
 * @p target must be a write-capable surface of an implemented CPU format; the painter creates
 * a RoboGraphics **raster** over it (so the clip and the source-over blend are the graphics
 * module's, not a second compositor's) and destroys it before returning. @p target is not
 * retained and not released.
 *
 * @p out_stats may be NULL.
 *
 * An **empty list** is a legitimate pass that fills nothing, and so is a list of structural
 * commands alone. The pass is **deterministic**: the order is the list's, and every number it
 * writes is a command's own.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p list or @p target;
 *         ROBO_NOT_SUPPORTED when @p target cannot be rasterised (a read-only or unsupported
 *         format); ROBO_ERROR with a diagnostic when a fill or a clip application fails.
 */
robo_result_t robo_paint_background_paint(const robo_paint_display_list_t *list,
                                          robo_gfx_surface_t *target,
                                          robo_paint_background_stats_t *out_stats,
                                          robo_paint_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_BACKGROUND_H */
