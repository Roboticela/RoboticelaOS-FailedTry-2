/*
 * RoboPaint decoration and text painting (Phase 11.4–11.6).
 *
 * The place in this module where a rectangle and a colour reach a **surface**, and one
 * door rather than two because of what "two doors" would mean. The paint pipeline stays
 * exactly the one the previous phases built, and this is its last arrow filled in:
 *
 *   HTML / DOM → RoboCSS → RoboLayout → RoboPaint → records → display list
 *       → **drawing commands** → RoboGraphics raster fill → the caller's surface
 *
 * **A document's drawings are one pass because they are one order.** The list interleaves them the way the traversal emits
 * them -- a box's background, then its four border sides, then its children, whose own
 * backgrounds and borders come after -- and CSS's paint order is exactly that: a
 * descendant's background is painted *after* its ancestor's border, so a child that
 * overflows its parent (a relative offset, a spill) draws over the parent's border rather
 * than under it. Passing over the list twice -- once for backgrounds, once for borders --
 * would paint every background in the document before every border in it, which is a
 * different and visibly wrong picture. So the walk is one walk, and its order is the
 * list's.
 *
 * The **text** half arrives in 11.6 and does not change that argument; it *is* the argument.
 * A text run is a drawing that can overlap what surrounds it -- a run wider than its box, a
 * run moved by a relative offset, a following sibling's background -- so a text pass separate
 * from the decoration pass would get the same order wrong in the same way, and the suite's
 * `child_over_parent_border` case has a text counterpart that would fail for two passes and
 * nothing else. A third difference comes with it: text needs a **font**, and the font is the
 * caller's exactly as the surface is -- see the `face` parameter below.
 *
 * The three kinds are not treated alike in what they *mean*, though, and the difference is
 * worth stating because it is the phase boundary between 11.4, 11.5 and 11.6:
 *
 *   a **background** is one rectangle filled with one colour, and that is the whole of it;
 *
 *   a **text run** is characters drawn along a baseline, and it is the first drawing here
 *   whose payload is not a number at all: the characters live in the display list's own
 *   pool and the command carries a span that names them, its baseline as an offset from the
 *   rectangle's top, and the font inputs the run was measured with (`RoboPaintText.h`
 *   argues the representation). This build draws them through RoboGraphics' own glyph
 *   bridge, which strokes outlines -- so text appears as 1-pixel outline strokes rather than
 *   as filled or antialiased glyphs, and `text_painting` claims a run's glyphs reach a
 *   surface rather than claiming they look the way a text renderer's output looks. Filled
 *   and coverage glyph modes are reserved in `RoboGraphicsText.h` and refused there;
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
 *
 * **The pass can be bounded (11.12).** A second door takes a rectangle and executes the same
 * list inside it: every drawing is still walked, but the **raster's own clip** is intersected
 * with that rectangle as well as with the command's own boundary, so a partial repaint is the
 * same pass over the same list with a smaller window rather than a filtered list or a
 * re-derived one (`RoboRenderRasterizer.h` is the frame that drives it, and
 * `render/docs/Rasterization.md` argues why the bound is a clip rather than a skip). The
 * unbounded door is the same walk with no rectangle, and the two are the same code.
 */
#ifndef ROBO_PAINT_DECORATION_H
#define ROBO_PAINT_DECORATION_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintDisplayList.h"
#include "RoboPaint/RoboPaintText.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What one decoration pass did.
 *
 * A record by value, owned by nobody, and the interesting facts are the *pairs*:
 * `commands` against `skipped` is how much of the list was drawable at all; `draws`
 * against `backgrounds`, `borders` and `texts` is what the document was made of; `filled`
 * against `draws` is how many of them actually put a colour down. `transparent` and
 * `contextual` are the two reasons a drawing command draws nothing, and keeping them apart
 * is what tells "the author asked for nothing" from "the colour could not be resolved";
 * `empty` is the third reason and `no_font` the fourth -- the reason that is about the
 * call rather than the document; `unshaped`, `glyphs`, `glyphs_missing` and `rtl` are the
 * four numbers that say *how* the drawings that did happen were drawn; `clipped` is how many
 * were drawn under a boundary the command carried.
 *
 * Three identities hold by construction and every one of them is asserted, so no counter
 * is a remainder nobody checked:
 *
 *   commands = draws + skipped
 *   draws    = backgrounds + borders + texts + images
 *   draws    = filled + contextual + transparent + empty + no_font + no_resource
 *
 * with `images` a subset of `filled` rather than a term of the second identity: an image that
 * reached the surface is a drawing that put pixels down, exactly as a fill is, and counting it
 * in both is what lets a reader ask either "how many of this document's drawings were images?"
 * or "how many drawings reached the surface?" without one answer being derived from the other.
 */
typedef struct robo_paint_decoration_stats {
    /** Display commands visited. */
    size_t commands;
    /** Commands that draw something: `draw-background`, `draw-border` and `draw-text`. */
    size_t draws;
    /** `draw-background` commands seen. */
    size_t backgrounds;
    /** `draw-border` commands seen: one per border side whose used width is above zero. */
    size_t borders;
    /**
     * `draw-text` commands seen: one per run the record list carried (11.6).
     *
     * The third drawing kind, and the reason it is a counter of its own rather than folded
     * into `draws` is that a reader who sees a document's text and borders in one number
     * cannot tell which of them reached the surface -- and after 11.6 those are different
     * claims, with different fences (`border_painting` and `text_painting`).
     */
    size_t texts;
    /**
     * `draw-image` commands seen: one per `IMAGE` record the traversal emitted (11.7).
     *
     * The fourth drawing kind, and the first whose drawing is **not a colour**: an image command
     * carries a resource and two rectangles, so a reader who saw it folded into `backgrounds`
     * would be told a resource's pixels were a resolved colour. Its own fence in
     * `robo_paint_capabilities_t` (`image_painting`) is what it is paired with, the same way
     * `text_painting` is paired with `texts`.
     */
    size_t images;
    /**
     * Drawings that put pixels on the surface -- a fill, a border band, a run's glyphs, or an
     * image.
     *
     * Four kinds and one number, because the question it answers is about the *surface* rather
     * than about the document: `draws` counts what the list asked for and `filled` counts what
     * landed. `images` is a subset of it, so a reader that wants "how many were images" reads
     * that field and a reader that wants "how many drawings happened" reads this one.
     */
    size_t filled;
    /**
     * Commands this pass does not attempt to draw: the structural kinds -- the three node
     * markers and, since 11.8, the two clip kinds, which draw nothing by definition -- and a
     * drawing command whose colour is neither a resolved one nor `currentColor`: a command the
     * record layer's own validity rule refuses, counted here rather than drawn so that
     * `commands = draws + skipped` stays exact even for a hand-built list.
     */
    size_t skipped;
    /** Drawings whose colour stayed `currentColor` and so could not be drawn here. */
    size_t contextual;
    /** Drawings whose colour is fully transparent (nothing to draw). */
    size_t transparent;
    /** Drawings with a zero-area rectangle (nothing to draw). */
    size_t empty;
    /**
     * Text commands this pass **could not attempt** because the caller supplied no font.
     *
     * The fifth term of the drawing partition, and the one that is a fact about the *call*
     * rather than about the document: a run is a list of characters plus the font inputs it
     * was measured with, and turning characters into glyphs needs a face -- which this door
     * takes as an argument for exactly the reason it takes the surface as one. A caller that
     * paints a textless document passes NULL and nothing changes; a caller that passes NULL
     * for a document with text is told so by this number rather than by a silently blank
     * frame.
     */
    size_t no_font;
    /**
     * Image commands whose payload this pass **cannot draw** (11.7).
     *
     * The sixth term of the drawing partition, and the one a document reaches without anything
     * being wrong with the call: an image command whose payload is cleared or not a payload this
     * build may draw -- no resource, no surface behind it, a zero dimension, or a source rectangle
     * outside the resource. The record and command layers' own validity rules refuse those, so a
     * list this build produced never contains one; the term exists because a hand-built list can,
     * and because leaving it out would make `draws` a remainder that silently swallowed the case
     * instead of naming it. It counts the same fact the traversal counts one layer up as
     * `image_skipped`, and the two differ only for a hand-built list.
     */
    size_t no_resource;
    /**
     * Glyph draws attempted: one per code point of every text command that was drawn.
     *
     * Counted against `texts` so "the text was painted" can be read as a statement about
     * characters rather than about commands -- a run of forty characters and a run of one
     * are one command each, and a reader comparing this number with a document's own length
     * is entitled to have it add up.
     */
    size_t glyphs;
    /**
     * Code points drawn as `.notdef` because the face has no glyph for them -- and, counted
     * the same way, code points whose glyph metrics would not parse.
     *
     * The **same rule and the same number** as the layout measurer's `glyphs_missing`
     * (`RoboLayoutTextFont.h`): an unmapped code point is measured and drawn at glyph 0's
     * advance, which is what every engine does with `.notdef`, and this counter is the only
     * thing that says the width a box was sized for is a width for text the font cannot
     * draw. A painter that skipped such characters would leave a gap where layout expected
     * an advance, and one that counted them as fine would hide the miss; counted here, the
     * two layers agree.
     */
    size_t glyphs_missing;
    /**
     * Text commands whose run is **right-to-left**, drawn here in logical order.
     *
     * This build has no bidi algorithm, so an RTL run's characters are drawn left to right
     * from its origin -- a run whose glyphs are in the right order in the wrong direction is
     * not a rendering of the document. Naming the count is the same choice the seven
     * unshaped border styles got: the fact that a simplification happened is a number a
     * reader sees rather than a silence. The direction itself travels on the record, so the
     * pass that implements bidi reads it from there rather than re-resolving a style.
     */
    size_t rtl;
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
    /**
     * Clip **scopes** the pass entered: one per `push-clip` command (11.8).
     *
     * A structural command, so it is also one of `skipped` -- and this is the count that says
     * how many of those skips were a boundary rather than a node marker, which is the fact a
     * reader comparing `clipped` with the document needs. `clipped` counts *drawings* bounded
     * by a boundary; this counts *scopes* opened, and a scope with no drawing inside it (a
     * childless `overflow: hidden` box) is a scope and not a clipped drawing -- which is why
     * the two are not the same number even though both are about clipping.
     */
    size_t clip_scopes;
    /**
     * Deepest clip scope the pass stood inside: the high-water mark of its own stack.
     *
     * Counted on the pass rather than read off the list because it is a property of the
     * *walk*: the traversal's scopes are properly nested, so this is also the depth of the
     * deepest box whose ancestors all bounded their contents. A later rasteriser sizing a clip
     * stack, or a compositor sizing a save/restore array, reads it from here instead of walking
     * the list to find out.
     */
    size_t clip_depth_max;
    /**
     * Opacity **scopes** the pass entered: one per `push-opacity` command (11.9).
     *
     * The `clip_scopes` of a second state, and a structural command too -- so it is also one of
     * `skipped`, and it is the number that says how many of those skips were a strength rather
     * than a node marker.
     */
    size_t opacity_scopes;
    /**
     * Deepest opacity scope the pass stood inside: the high-water mark of its own stack.
     *
     * `clip_depth_max`'s sibling, and separate from it because the two are not the same depth:
     * an opacity pair opens at a box's marker and a clip pair at its content boundary, so a
     * document that nests one without the other has two different numbers. A later rasteriser
     * that grows a strength stack, or a compositor that needs one, asks this instead of walking
     * the list.
     */
    size_t opacity_depth_max;
    /**
     * Drawings put on the surface at a strength **below full** (11.9).
     *
     * The count of drawings the phase actually changed, and it is deliberately not called
     * "drawings inside an opacity scope": a `0.5` box whose fill is already fully transparent
     * is inside a scope and is still drawn at full strength -- it is drawn at all only because
     * the pass reached it, and its own alpha decides what lands. This number counts the ones
     * where the raster's opacity was set to something other than `255`, which is the operation
     * that would be worth removing if the phase were ever optimised.
     */
    size_t faded;
    /**
     * Drawings the pass **executed** whose own rectangle does not reach the bound (11.12).
     *
     * Zero for every unbounded pass -- there is no bound to miss -- and for a bounded one it is
     * the number that tells a reader which part of the list was still relevant to the
     * rectangle, which is the question a caller comparing "repaint everything" with "repaint
     * this region" is asking. It is a diagnosis of *work* and deliberately **not** a claim
     * about pixels: the raster's clip is what decides what lands, so a drawing counted here put
     * nothing down -- except, possibly, a text run, whose ink can overhang the advance box its
     * rectangle is (`RoboRenderRasterizer.h` states why text is never skipped for that reason).
     *
     * It is a subset of `draws` rather than a fourth bucket, because the drawing *was*
     * executed: the clip excluded it, which is a different fact from a command the pass could
     * not attempt. So `commands = draws + skipped` still holds exactly, bounded or not.
     */
    size_t bounded_out;
} robo_paint_decoration_stats_t;

/** Clear @p stats to all zeroes. Safe on NULL. */
void robo_paint_decoration_stats_init(robo_paint_decoration_stats_t *stats);

/**
 * Fill every drawing command of @p list into @p target, in the list's order.
 *
 * Walks @p list once, and for each command:
 *
 *   a **fill** drawing command (`draw-background`, `draw-border`): resolve its colour
 *   (`robo_paint_color_to_rgba8`), apply the command's clip, and fill its rectangle with
 *   `robo_gfx_raster_fill_rect`. A `currentColor` is counted (`contextual`) and a fully
 *   transparent one is counted (`transparent`), and a zero-area rectangle is counted
 *   (`empty`) -- each is a statement about the document rather than a failure, which is
 *   why the pass still succeeds;
 *
 *   a **`draw-image`** command (11.7): apply the same clip and the same strength, then hand its
 *   resource, its source rectangle and its destination to `robo_gfx_raster_draw_image`. It is held
 *   to the geometry rule -- a zero-area destination is counted (`empty`) -- and to a rule of its
 *   own instead of the colour's, because its appearance is a resource rather than a colour: a
 *   payload this build cannot draw is counted (`no_resource`). Both are statements about the
 *   document and neither fails the pass;
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
 * A **`draw-text`** command (11.6) resolves its code points to glyph ids through @p face's
 * own cmap and draws each one with `robo_gfx_raster_draw_glyph`, advancing a pen by the
 * glyph's device advance. The pen is accumulated in `float` and the drawn origin is snapped
 * by the graphics module's own policy, which is the same rounding the bridge's scaled metrics
 * use; a code point @p face has no glyph for is drawn at glyph 0 and counted
 * (`glyphs_missing`), exactly as the layout measurer counted the same miss one layer down.
 *
 * @p face is **the caller's resource, exactly as @p target is**: this door retains nothing,
 * resolves no `font-family`, falls nothing back and caches no glyph. It may be NULL, and then
 * every text command is counted (`no_font`) and skipped -- a document with no text paints
 * identically either way, so a caller that does not draw text never needs a font. Which face
 * a document's text should use is a selection question with owners elsewhere (RoboFont's
 * Goals 5.11/5.12/5.15 and CSS's `font-family`), and it is the same argument
 * `RoboLayoutTextFont.h` makes for its measurer: the face is handed in, borrowed for the pass,
 * and never discovered here.
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
 *         ROBO_ERROR with a diagnostic when a fill, an image draw, a clip application or a
 *         glyph draw fails.
 */
robo_result_t robo_paint_decoration_paint(const robo_paint_display_list_t *list,
                                          robo_gfx_surface_t *target,
                                          robo_font_face_t *face,
                                          robo_paint_decoration_stats_t *out_stats,
                                          robo_paint_diag_t *diag);

/**
 * The same pass, executed inside @p bound (11.12).
 *
 * One walk of @p list, the same order, the same clips, the same strengths and the same
 * drawings as the unbounded door above -- with **one** difference: every drawing is clipped
 * to @p bound as well as to its own boundary, so nothing outside the rectangle is touched.
 * A NULL @p bound is the unbounded pass exactly (`robo_paint_decoration_paint` is this door
 * with NULL), and a bound that misses the target entirely is a successful pass that draws
 * nothing.
 *
 * **The bound is a bound, not a filter**, and that is the phase's decision rather than an
 * implementation choice. Skipping a drawing whose rectangle misses the bound looks like the
 * same picture and is not: a text run's rectangle is its **advance** box and a glyph's ink can
 * overhang it, so a run can put pixels inside a bound its rectangle does not reach. Bounding
 * with the clip instead needs no per-kind proof of that kind -- every write of every kind is
 * already clipped per pixel by the graphics module's own raster -- and it is what makes
 * region-limited execution equal to a full frame *inside* the region rather than merely
 * plausibly close to it.
 *
 * `bounded_out` counts the drawings whose rectangle missed @p bound, which is a statement
 * about work rather than about pixels (see its field comment).
 *
 * @p bound's coordinates are **paint coordinates**, the same space a command's rectangle and
 * a clip axis are in, and it is clamped to the target's own pixel bounds -- so a region that
 * runs off the surface is a smaller drawing area rather than an error. A negative extent is
 * *accepted as empty* (nothing draws), for the reason every rectangle rule in this module
 * accepts an empty one: "there is nowhere to draw" is a document's shape rather than a
 * caller mistake.
 *
 * @return as the unbounded door: ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p list or
 *         @p target; ROBO_NOT_SUPPORTED when @p target cannot be rasterised;
 *         ROBO_OUT_OF_MEMORY; ROBO_ERROR with a diagnostic when a drawing fails.
 */
robo_result_t robo_paint_decoration_paint_bounded(const robo_paint_display_list_t *list,
                                                  robo_gfx_surface_t *target,
                                                  robo_font_face_t *face,
                                                  const robo_layout_rect_t *bound,
                                                  robo_paint_decoration_stats_t *out_stats,
                                                  robo_paint_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_DECORATION_H */
