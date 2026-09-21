/*
 * RoboRender version pin — Phase 11.12 rasterization.
 *
 * RoboRender is the module that turns a **display list into pixels**, and it is the first release
 * in this tree that ends in a buffer rather than in a description of one. Everything below it
 * answers questions -- where is every box, what is the boundary in force, what should be drawn in
 * what order, which regions went stale -- and every one of those answers is *data*. This release
 * spends them: `robo_render_rasterize` walks a display list and leaves a target whose bytes are the
 * picture, and `robo_render_rasterize_regions` does the same for the part of it a paint
 * invalidation set says is stale.
 *
 * ## What this module owns, and what it deliberately does not
 *
 * The boundary with the module below it is the phase's first decision, and it is stated in three
 * places (`RoboPaint.h`, `RoboPaintVersion.h`, `paint/docs/Decoration.md`) because getting it
 * wrong in either direction produces a worse build:
 *
 *   **RoboPaint owns what the list *means**: the command vocabulary, the two scope stacks, the
 *   boundary and the strength each drawing is executed under, and the buckets that say why a
 *   drawing did not land. A second consumer that re-walked the list would be a second answer to
 *   "what does this command do", and the day the two disagreed the rasteriser would be the one a
 *   person believed.
 *
 *   **RoboRender owns what a *frame* is**: an owned pixel target, a run over the whole vocabulary,
 *   an execution record, byte-determinism, and region-limited execution. Those are not properties
 *   of a list, which is why they are not in the module that produces one.
 *
 * So 11.12 adds **one door below this module** (`robo_paint_decoration_paint_bounded`, the same
 * walk with the raster's clip narrowed by a rectangle) and everything else here. The one thing it
 * does *not* do is duplicate the walk, and the reason is the reason 11.11 refused to duplicate the
 * invalidation questions: two implementations of one rule agree until a document makes them
 * disagree, and there is no test that can tell you which one is right.
 *
 * ## The four decisions worth stating before the code
 *
 * **One format, byte-addressable, stated.** A target is `ROBO_GFX_FORMAT_RGBA8` and nothing else
 * (`ROBO_RENDER_TARGET_FORMAT`). Not because a second format is hard -- RoboGraphics implements
 * five -- but because *determinism is a claim about bytes*, and a claim about bytes is only worth
 * making about a layout a caller can predict. A caller that wants a different layout encodes one
 * (`RoboGraphicsEncode.h`), and the day a renderer's target must be the window's format, that is a
 * decision with a surface behind it rather than a default.
 *
 * **The bound is a *clip*, not a *filter*.** A region run executes **every** command of the list
 * and narrows the raster's own clip to the region, so nothing outside it is written and nothing
 * inside it is missed. Skipping the commands whose rectangle misses the region looks like the same
 * picture and is not: a text run's rectangle is its **advance** box, so a glyph's ink can land
 * inside a region its rectangle never reaches. Bounding with the clip needs no per-kind proof of
 * that kind, and it is what makes a region run equal to a full frame *inside* the region -- which
 * this release's `render.scene.region_matches_full` asserts pixel by pixel rather than argues.
 *
 * **One run per set, over the set's own bounds.** A set can name overlapping regions, and drawing
 * the list once per region would composite a semi-transparent drawing **twice** where two regions
 * overlap -- a wrong pixel that no count would catch. So the run executes once, over the union of
 * the set's regions: 11.11's `set_bounds` is documented as *sufficient* (it can only be too large),
 * and a repaint that is one pass over a superset of the damage is right for the same reason a
 * repaint of the *whole* frame is right, at a fraction of the pixels. The tighter answer -- merge
 * the regions into disjoint rectangles and run once per rectangle -- needs the region tree 11.11
 * deliberately did not build, and it is named as the optimization it is rather than implied.
 *
 * **Determinism is a statement about bytes, and this module can be asked for the bytes.** Two doors
 * (`robo_render_target_checksum`, `robo_render_target_equal`) exist so that "the same list into the
 * same target twice produced the same picture" is a thing a caller -- and a suite -- can *check*
 * rather than believe. Nothing in the pipeline reads a clock, an address, an allocation order or a
 * hash iteration order, and the execution record is a struct of counts for the same reason: a
 * record that could not be compared would be a record nobody could pin.
 *
 * ## What this release does not claim
 *
 * It does not present anything: a target is a buffer this module owns, and putting one on a screen
 * belongs to the window, display and framebuffer path (`RoboWindow`, `RoboDisplay`, RoboGraphics'
 * own framebuffer), which is Phase 11.13's -- `robo_render_target_surface` is the *door* for that
 * work rather than a piece of it. It does not implement tiles, damage tracking, a frame scheduler,
 * VSync, a compositor, compositor layers, GPU rasterisation, worker threads, scrolling, transforms,
 * filters, blend modes, masks, `border-radius` clipping, CSS gradients, animations or transitions:
 * `RoboRender.h`'s capability table names each of those as a **0**, and
 * `render/docs/Rasterization.md` §"What is absent" says what each would need. It does not patch a
 * display list -- the list is rebuilt and re-executed; the set says how little of the *pixels* may
 * be re-executed, not how little of the *list*. And it does not read HTML, CSS, a layout tree or a
 * computed style: a display list is its input contract, and the boundary scan in `Tools/Build.ps1`
 * enforces that no higher-layer name appears in this module's sources.
 *
 * ## The coordinate statement
 *
 * **Paint coordinates are device pixels in this build**, and the mapping is the identity. The
 * origin is the top-left of the page, `x` grows right, `y` grows down, a rectangle is half-open
 * (`x` inclusive, `x + width` exclusive) exactly as RoboGraphics' own pixel rectangles are, and the
 * target's pixel `(0, 0)` is paint coordinate `(0, 0)`. There is **no rounding left to perform**
 * here and that is worth stating rather than leaving to be discovered: every rectangle that reaches
 * this module has already been resolved to integers by RoboLayout and RoboPaint, and the one
 * continuous quantity in the pipeline -- a text run's pen, accumulated in `float` as advances are
 * added -- is snapped by RoboGraphics' own `robo_gfx_glyph_snap_point` policy one layer down. A
 * device-pixel-ratio or a zoom would break the identity, which is why both are named as absent
 * rather than assumed to be 1 forever.
 */
#ifndef ROBO_RENDER_VERSION_H
#define ROBO_RENDER_VERSION_H

#define ROBO_RENDER_VERSION_MAJOR 0
#define ROBO_RENDER_VERSION_MINOR 1
#define ROBO_RENDER_VERSION_PATCH 0
#define ROBO_RENDER_VERSION_STRING "0.1.0"

/** 1 once the Phase 11 surface is frozen (not yet — see the header comment). */
#define ROBO_RENDER_API_FROZEN 0

/**
 * 1 since this module's first release: **a display list executed into pixels.**
 *
 * The flip this constant names is the one `RoboPaintVersion.h` wrote down for `raster` and left for
 * a later phase -- "a build in which a whole display list reaches a surface as a frame" -- and it is
 * read by two doors that must not disagree (`robo_render_get_capabilities()`'s `rasterization`
 * field and `robo_render_rasterization_is_implemented()`), plus the conformance suite, which
 * asserts all three together.
 *
 * What it claims is **narrow**, and the width is what a caller must read rather than the flag: the
 * commands a display list can carry all reach the target -- a background, a border band, a text
 * run's glyphs, an image, in the order the list states, bounded by its clip scopes and at the
 * strengths its opacity scopes establish -- with an execution record a caller can read and
 * byte-identical output for identical input. What it does **not** claim is anything the *target's*
 * consumers own: presentation to a window or a framebuffer (11.13), a compositor, layers, tiles,
 * damage tracking, a frame scheduler, VSync, a GPU, transforms, filters or masks. Every one of those
 * is a 0 in `robo_render_capabilities_t` beside this flag, which is where a caller deciding what to
 * build next should look -- and the same discipline `ROBO_PAINT_DRAWING_IMPLEMENTED` follows one
 * module down.
 */
#define ROBO_RENDER_RASTERIZATION_IMPLEMENTED 1

#endif /* ROBO_RENDER_VERSION_H */
