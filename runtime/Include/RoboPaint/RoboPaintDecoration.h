/*
 * RoboPaint decoration painting (Phase 11.4–11.5).
 *
 * The place in this module where a rectangle and a colour reach a **surface**, and one
 * door rather than two because of what "two doors" would mean. The paint pipeline stays
 * exactly the one the previous phases built, and this is its last arrow filled in:
 *
 *   HTML / DOM → RoboCSS → RoboLayout → RoboPaint → records → display list
 *       → **drawing commands** → RoboGraphics raster fill → the caller's surface
 *
 * **A box's two decorations are its background and its border, and they are one pass
 * because they are one order.** The list interleaves them the way the traversal emits
 * them -- a box's background, then its four border sides, then its children, whose own
 * backgrounds and borders come after -- and CSS's paint order is exactly that: a
 * descendant's background is painted *after* its ancestor's border, so a child that
 * overflows its parent (a relative offset, a spill) draws over the parent's border rather
 * than under it. Passing over the list twice -- once for backgrounds, once for borders --
 * would paint every background in the document before every border in it, which is a
 * different and visibly wrong picture. So the walk is one walk, and its order is the
 * list's.
 *
 * The two kinds are not treated alike in what they *mean*, though, and the difference is
 * worth stating because it is the phase boundary between 11.4 and 11.5:
 *
 *   a **background** is one rectangle filled with one colour, and that is the whole of it;
 *
 *   a **border** is one rectangle -- the band a side occupies, already resolved from the
 *   box model -- filled with one colour *and shaped by a style*. CSS has ten styles, of
 *   which two (`none`, `hidden`) draw nothing at all and therefore produce no command,
 *   one (`solid`) is exactly its band, and seven (`dotted`, `dashed`, `double`, `groove`,
 *   `ridge`, `inset`, `outset`) are patterns *inside* the band. This build fills the band
 *   for all eight that draw, so every border is present, correctly placed and in its own
 *   colour, and the seven patterns are not drawn yet -- a simplification that is a
 *   **number a caller reads** rather than a silence (`stats.unshaped`), because a border
 *   drawn as a solid band when the author asked for dots is exactly the kind of quiet
 *   wrongness this module counts instead of shipping.
 *
 * **The list is the input, not the records**, because the display list is the artifact
 * whose geometry is resolved -- a command carries the one rectangle to fill, the boundary
 * in force and, for a band, its style, so the painter never reads the box model, a
 * computed style or the tree. The only conversion left is the colour, and that is
 * `RoboPaintColor.h`'s.
 *
 * **Clipping is read, not implemented.** A command carries the boundary in force from the
 * paint pass (Phase 11.1) as two axes. When an axis is bounded it is applied to the
 * RoboGraphics raster's clip, and when it is not the axis is left at the surface's own
 * bounds -- so an `overflow: hidden` ancestor bounds a descendant's fill, which is the
 * correct CSS behaviour for a box's own painting, while no new clipping architecture is
 * invented.
 *
 * **Ownership is the caller's.** The surface belongs to whoever made it; this door retains
 * nothing and destroys nothing. The list is read through `const` and never mutated.
 */
#ifndef ROBO_PAINT_DECORATION_H
#define ROBO_PAINT_DECORATION_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintDisplayList.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What one decoration pass did.
 *
 * A record by value, owned by nobody, and the interesting facts are the *pairs*:
 * `commands` against `skipped` is how much of the list was drawable at all; `draws`
 * against `backgrounds` and `borders` is what the document was made of; `filled` against
 * `draws` is how many of them actually put a colour down. `transparent` and `contextual`
 * are the two reasons a drawing command draws nothing, and keeping them apart is what
 * tells "the author asked for nothing" from "the colour could not be resolved"; `empty` is
 * the third reason and `unshaped` the fourth kind of fact; `clipped` is how many were
 * drawn under a boundary the command carried.
 *
 * Three identities hold by construction and every one of them is asserted, so no counter
 * is a remainder nobody checked:
 *
 *   commands = draws + skipped
 *   draws    = backgrounds + borders
 *   draws    = filled + contextual + transparent + empty
 */
typedef struct robo_paint_decoration_stats {
    /** Display commands visited. */
    size_t commands;
    /** Commands that draw something: `draw-background` plus `draw-border`. */
    size_t draws;
    /** `draw-background` commands seen. */
    size_t backgrounds;
    /** `draw-border` commands seen: one per border side whose used width is above zero. */
    size_t borders;
    /** Drawing commands that put a colour on the surface. */
    size_t filled;
    /**
     * Commands this pass does not attempt to draw: the three structural kinds, which draw
     * nothing by definition, and a drawing command whose colour is neither a resolved one nor
     * `currentColor` -- a command the record layer's own validity rule refuses, counted here
     * rather than drawn so that `commands = draws + skipped` stays exact even for a hand-built
     * list.
     */
    size_t skipped;
    /** Drawings whose colour stayed `currentColor` and so could not be drawn here. */
    size_t contextual;
    /** Drawings whose colour is fully transparent (nothing to draw). */
    size_t transparent;
    /** Drawings with a zero-area rectangle (nothing to draw). */
    size_t empty;
    /**
     * Borders filled whose style this build does not shape yet: `dotted`, `dashed`,
     * `double`, `groove`, `ridge`, `inset` and `outset`. They are **drawn** -- the band,
     * in the side's colour, which is CSS's `solid` shape -- so this is not a count of
     * omissions but of simplifications, and it is the number that keeps "this build paints
     * borders" from being read as "this build paints every border the way it looks".
     */
    size_t unshaped;
    /** Drawings filled under a boundary carried by the command's clip. */
    size_t clipped;
} robo_paint_decoration_stats_t;

/** Clear @p stats to all zeroes. Safe on NULL. */
void robo_paint_decoration_stats_init(robo_paint_decoration_stats_t *stats);

/**
 * Fill every drawing command of @p list into @p target, in the list's order.
 *
 * Walks @p list once, and for each command:
 *
 *   a **drawing** command (`draw-background`, `draw-border`): resolve its colour
 *   (`robo_paint_color_to_rgba8`), apply the command's clip, and fill its rectangle with
 *   `robo_gfx_raster_fill_rect`. A `currentColor` is counted (`contextual`) and a fully
 *   transparent one is counted (`transparent`), and a zero-area rectangle is counted
 *   (`empty`) -- each is a statement about the document rather than a failure, which is
 *   why the pass still succeeds;
 *
 *   a **structural** command (`begin-node`, `content`, `end-node`): **skipped** and
 *   counted. They draw nothing, by definition, so `skipped` is a statement about the list
 *   rather than a gap in this pass -- and a drawing command carrying a colour the record
 *   layer refuses (`none`, `invalid`) joins them there rather than being drawn.
 *
 * @p target must be a write-capable surface of an implemented CPU format; the painter
 * creates a RoboGraphics **raster** over it (so the clip and the source-over blend are the
 * graphics module's, not a second compositor's) and destroys it before returning.
 * @p target is not retained and not released.
 *
 * @p out_stats may be NULL.
 *
 * An **empty list** is a legitimate pass that fills nothing, and so is a list of structural
 * commands alone. The pass is **deterministic**: the order is the list's, and every number
 * it writes is a command's own.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p list or @p target;
 *         ROBO_NOT_SUPPORTED when @p target cannot be rasterised (a read-only or
 *         unsupported format); ROBO_OUT_OF_MEMORY when the raster cannot be allocated;
 *         ROBO_ERROR with a diagnostic when a fill or a clip application fails.
 */
robo_result_t robo_paint_decoration_paint(const robo_paint_display_list_t *list,
                                          robo_gfx_surface_t *target,
                                          robo_paint_decoration_stats_t *out_stats,
                                          robo_paint_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_DECORATION_H */
