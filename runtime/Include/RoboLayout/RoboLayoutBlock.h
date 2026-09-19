/*
 * RoboLayout block layout (Phase 10.3), and the pass the inline direction runs in
 * (Phase 10.4).
 *
 * The first phase that computes a coordinate. Everything before it decided
 * *which* boxes there are, *what* they are and *which* formatting context
 * coordinates them; this one answers where each one goes and how big it is, in
 * the one direction the block algorithm owns: down.
 *
 * 10.4 added the other direction to the same door, and the reason is not
 * convenience: an inline formatting context's height is the block extent of the box
 * that owns it, so a build that laid the lines out *after* the blocks would have to
 * re-stack every ancestor to use them. The door therefore reports two records -- the
 * block direction here, the inline direction in RoboLayoutInline.h -- and there is
 * no `robo_layout_inline_layout` beside it, because a second entry point could only
 * duplicate this walk or run it in secret.
 *
 * The containing block is the parent, and the context is not
 *
 * This is the distinction the phase turns on, and getting it backwards is the
 * mistake that would cost 10.9 and 10.12 a rewrite:
 *
 *   a box's **containing block** is its **parent box**. Children are placed
 *   inside the box that contains them, and this does not depend on which
 *   formatting context anybody participates in.
 *
 *   a box's **formatting context** decides the *direction* its contents are
 *   stacked in, and whether this pass is the one that stacks them at all.
 *
 * 10.2 called a block container with a block-level child "transparent", and the
 * word invites the wrong reading: the box is transparent to *formatting context*,
 * so its children are coordinated by an ancestor's context -- but it is still the
 * box they are laid out *inside*, with its own content edges. A build that read
 * "transparent" as "not in the chain" would place a grandchild at the
 * grandparent's origin and report a document it had not laid out. So the two
 * questions are asked separately here, and `layout.block.context_gate`
 * asserts they agree:
 *
 *   the walk descends the **parent link**, always;
 *   the placement gate is the **formatting context**, via 10.2's own
 *   `robo_layout_fc_kind_is_block_container`.
 *
 * What a block box does
 *
 *   1. its margins are resolved against its containing block -- `auto` included,
 *      which is what distributes the leftover space beside a declared width;
 *   2. its padding is resolved from its own declarations, which needs nobody's
 *      permission and no containing block;
 *   3. it fills what its margins **and its padding** left of the containing block's
 *      content width, because CSS's constraint equation has a term for each -- so a
 *      box with padding and no declared width is as wide as its containing block
 *      rather than wider by its insets;
 *   4. its block-level children are stacked from its content origin, each below
 *      the last, inside a content box that is its own declared size and *not* its
 *      border box;
 *   5. its height is the declared one if this build can resolve it, and the
 *      stacked extent of its children otherwise -- either way the **content**
 *      height, which the padding then grows;
 *   6. its result is written in its containing block's coordinate space, which is
 *      what `robo_layout_result_t.position` has meant since 10.1 -- since 10.9 that
 *      position is its **margin** origin, and since 10.10 its `size` is the **border**
 *      box, so the content origin is that position plus `padding-left`/`padding-top`
 *      and the content box is its `size` less the padding.
 *
 * What this phase deliberately does not do, and where each one belongs
 *
 *   `width: 200px` is **ignored**. A block box fills its containing block, which
 *   is CSS's `width: auto`, and resolving a declared width -- including
 *   percentages, `min-width`, `max-width` and `box-sizing` -- is 10.8. A caller
 *   that reads a width from this pass is reading the *containing block's*, and
 *   the header says so rather than leaving it to be discovered.
 *
 *   A declared `height` **is** honoured, and only when this build can resolve it:
 *   a computed LENGTH in `px`. Anything else -- `auto`, a percentage, a relative
 *   unit -- is treated as `auto` rather than guessed at, because "I cannot
 *   resolve this yet" and "the content decides" produce the same geometry and
 *   only one of them is a claim this phase can defend. Reading it is not a
 *   sizing algorithm: there is one property, one kind check and one unit check.
 *
 *   Borders are **10.11**. So a box's border box is its content box plus padding and
 *   nothing else -- and the two are no longer one number. A margin never changes a box's
 *   `size`; a padding does, which is the difference in one line: since 10.9 a box is
 *   placed at its **margin origin** and its parent stacks by its **margin box**, and since
 *   10.10 its `size` is the **border** box while its own declarations are the **content**
 *   box, under this build's `box-sizing: content-box`. There is **no margin collapsing**,
 *   so adjacent vertical margins add (`layout/docs/Margins.md`), and **no padding on a
 *   transparent inline box** (`layout/docs/Padding.md`).
 *
 *   Inline layout is **10.4**, and it runs *inside* this pass rather than beside
 *   it. A box that establishes an inline formatting context has its contents placed
 *   along a line by `robo_layout_inline_walk_contents` (RoboLayoutInline.h), and the
 *   extent that comes back *is* the content extent the height rule above uses. The
 *   two directions cannot be separate passes -- an inline context's height is part
 *   of the block stack -- which is why this door reports two records rather than
 *   one and why there is no second entry point.
 *
 *   An inline-level box that participates in a *block-direction* context is still
 *   not placed, and is counted as `inline_level_skipped`: CSS gives the inline run
 *   it belongs to an **anonymous** block box, and this build makes none -- such a
 *   box has no element and therefore no computed style (RoboLayoutObject.h), so
 *   inventing one here would be inventing an unstyled box that nothing can measure.
 *   The recursion does not descend into one either, because a box inside an unplaced
 *   box has no origin to be placed relative to.
 *
 *   Positioning, overflow, floats and fragmentation are **10.12, 10.13 and
 *   later**. In particular the result's two overflow flags stay 0 even when a
 *   declared height is smaller than the content it holds: that a box overflows is
 *   a fact 10.13 owns, and setting a flag here would be answering it.
 *
 * Determinism
 *
 * The pass is a pure function of the tree, the viewport and the borrowed styles:
 * boxes are visited in creation order (document order) and children in sibling
 * order, so two runs over one tree produce equal results box for box, and a run
 * over two trees built from one document produces equal geometry. Nothing is
 * cached across calls and nothing is read from a previous pass, which is why
 * `robo_layout_context_run` clearing every result before it computes is a
 * statement of the contract rather than a defensive habit.
 */

#ifndef ROBO_LAYOUT_BLOCK_H
#define ROBO_LAYOUT_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutInline.h"
#include "RoboLayout/RoboLayoutTree.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Who is in the block flow                                            */
/* ------------------------------------------------------------------ */

/*
 * There is deliberately no "is this display block-level?" here, and its absence
 * is a decision rather than an omission.
 *
 * 9.12 already answers it -- `robo_css_display_is_block_level`, and its two
 * siblings `is_inline_level` and `is_inline_block` -- off the vocabulary's own
 * outer/inner table. A second predicate in this module would be a second place
 * for one answer to live, which is the failure this runtime refuses everywhere
 * else; and it would be the *worse* copy, because the day `flex` adds a row with
 * an outer of `block`, 9.12's table is where that fact lands and a copy here
 * would have to be found and edited.
 *
 * So this pass reads the display *through* 9.12's accessors, exactly as 10.2's
 * classifier reads the outer/inner pair through them, and holds no CSS semantics
 * of its own. The one thing the pass adds is the *second* question -- which boxes
 * this pass owns -- and that answer comes from 10.2's formatting contexts, not
 * from a display value:
 *
 *   robo_css_display_is_block_level(box.display)          is it a block in a flow?
 *   robo_layout_fc_kind_is_block_container(kind of          is the flow it is in
 *                                          box.context)     one this pass stacks?
 *
 * Both must be true for the pass to place a box, and `layout.block.context_gate`
 * asserts that the second is never true while the first is false -- an invariant
 * 10.2's rules make impossible, checked rather than assumed.
 */

/* ------------------------------------------------------------------ */
/* What one pass saw                                                   */
/* ------------------------------------------------------------------ */

/**
 * Counters for one block-layout pass. A record by value, owned by nobody.
 *
 * Counters rather than one total, because the interesting fact about a document
 * is the *mix*: a tree of nested transparent containers and a tree of filled
 * leaves can place the same number of boxes and have completely different
 * shapes, and a suite that could not tell them apart would not be testing the
 * algorithm.
 */
typedef struct robo_layout_block_stats {
    /** Containers whose contents were stacked in the block direction: boxes with
     *  at least one block-level child in a block-direction context. */
    size_t containers;
    /** Block-level boxes that received geometry. */
    size_t blocks_placed;
    /** Inline-level **boxes and text runs** in a **block-direction** context: there
     *  is no line to put them on, and the anonymous block box CSS would wrap them in
     *  is a later goal. A text run is counted here for the same reason a box is -- it
     *  is inline-level, so a text node between two blocks has no line to be on — and
     *  it is counted *instead of* being measured, because a measurement with nowhere
     *  to go is not a result. An inline-level box inside an *inline* context is
     *  deliberately **not** counted here -- 10.4 places it, and
     *  `robo_layout_inline_stats_t` counts it. */
    size_t inline_level_skipped;
    /** Objects that received no geometry and that neither direction declined for a
     *  stated reason -- a block-level box inside an inline box, which needs the
     *  anonymous box CSS wraps an inline run in and which no phase in this build
     *  makes. Counted rather than dropped, because "no geometry" and "not in the
     *  document" must not look the same. The pass computes it as a difference, so
     *  it is the field that proves the accounting closes:
     *  `blocks_placed + inline_level_skipped + participants_placed + unreached`
     *  is the tree's object count -- boxes *and* text runs, which since 10.5 is the
     *  count the identity is stated over. */
    size_t unreached;
    /** Widths taken from a declared `width` this build could resolve: a computed
     *  length in `px` (10.8). The counterpart of `heights_explicit`, and the number
     *  a suite can use to tell a document whose boxes obey their authors from one
     *  whose every box filled its containing block -- two documents that can place
     *  the same boxes at the same positions. */
    size_t widths_declared;
    /** Widths filled from the containing block: the `auto` path, and the path a
     *  declared-but-unresolvable width takes -- a percentage, which this build has no
     *  containing dimension to resolve against. */
    size_t widths_filled;
    /** Heights taken from a declared `height` this build could resolve: a
     *  computed length in `px`. */
    size_t heights_explicit;
    /** Heights obtained by stacking children -- the `auto` path, and the path a
     *  declared-but-unresolvable height takes. */
    size_t heights_from_content;
    /** **Sides** whose declared margin reached geometry (10.9), zero and negative
     *  included. Summed over sides rather than over boxes because that is the unit a
     *  stylesheet is written in: four longhands, one declaration. Counted from the
     *  *resolution* rather than from the style, so an `auto` that distributed is not
     *  one of these -- it is `margins_auto_used` below. */
    size_t margins_applied;
    /** Sides whose `auto` was given a value by **distribution** (10.9): the horizontal
     *  margins of a block-level box whose declared width left space over. Zero for a
     *  document where nobody asked to be centred, and zero for every vertical `auto`
     *  even where one was written, because normal flow does not distribute
     *  vertically. */
    size_t margins_auto_used;
    /** Sides declared with a margin this build cannot use -- a percentage, a relative
     *  unit (10.9). Their used value is zero, which is why they are counted: a
     *  document whose spacing depends on `margin: 5%` lays out flush here, and the
     *  count is what says so. */
    size_t margins_unresolved;
    /** **Sides** whose declared padding reached geometry (10.10), zero included.
     *  Summed over sides for the reason the margin counters are, and counted from the
     *  *resolution* rather than from the style: a side nobody declared is `ABSENT` and is
     *  counted nowhere, which is the distinction 10.9 had to add a kind for and this phase
     *  inherited rather than re-learned. */
    size_t paddings_applied;
    /** Sides declared with a padding this build cannot use -- a percentage, a relative
     *  unit, a negative length, or the `auto` the grammar does not allow (10.10). Their
     *  used value is zero, which is why they are counted: a document whose insets depend
     *  on `padding: 5%` lays out flush here, and the count is what says so. */
    size_t paddings_unresolved;
    /** Containers with no block-level child at all, so the extent this pass
     *  stacked in the block direction is zero and the height is `0` unless one was
     *  declared -- or, from 10.4, is the extent the *lines* reached. A box that
     *  establishes an inline formatting context is one of these, because 10.2's
     *  rules make it have no block-level child by construction. The single most
     *  surprising fact about 10.3, and therefore one worth counting. */
    size_t empty_containers;
    /** Deepest box placed. */
    uint32_t depth_max;
} robo_layout_block_stats_t;

/** Clear @p stats to zero. Safe on NULL. */
void robo_layout_block_stats_init(robo_layout_block_stats_t *stats);

/** Field-by-field equality. Safe on NULL. */
int robo_layout_block_stats_equal(const robo_layout_block_stats_t *a,
                                  const robo_layout_block_stats_t *b);

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

/**
 * Lay out @p tree's block direction into @p viewport.
 *
 * @p viewport is the **initial containing block**: the space the root box is laid
 * out into. It is an argument rather than a query because RoboLayout owns no
 * window and no display (README.md), and because a pass that asked a window how
 * big it was could not be tested without one. `robo_layout_context_run` passes
 * the viewport it recorded; a caller may also call this directly.
 *
 * @p tree must have been classified (RoboLayoutFormattingContext.h) --
 * `robo_layout_tree_is_classified` -- because the placement gate is the
 * formatting context: without one there is no way to ask which boxes this pass
 * owns, and guessing "every block-level box" would be the second answer to a
 * question 10.2 already answered. Running before classification is reported
 * rather than repaired.
 *
 * The pass **replaces** geometry rather than refining it: every box it reaches is
 * written from scratch, so calling it twice over one tree produces the same
 * results rather than stacking a second pass on the first.
 *
 * Every box whose geometry this pass computes has its result moved to COMPUTED;
 * every box it does not reach is left UNRESOLVED. The two states are the per-box
 * answer, and they are the authority -- `geometry` in the capability struct says
 * the build computes coordinates at all, not that a particular box has one.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL tree or a negative viewport;
 *         ROBO_NOT_FOUND when the tree has no root box (an empty document is a
 *         legitimate pass, and this is how the caller learns nothing was placed);
 *         ROBO_ERROR when the tree is not classified. A failure reports through
 *         @p diag and leaves whatever it had already written -- a partial pass is
 *         visible in the per-box states rather than dressed up as a complete one.
 *
 * @p out_inline_stats receives what the **inline** direction did, and may be NULL
 * for a caller that wants only the block numbers. The two records are not two
 * passes: an inline formatting context's height *is* the block extent of the box
 * that owns it, so the lines are laid out while this recursion descends and the
 * numbers are written to both records as they happen. `boxes_computed` in
 * `robo_layout_stats_t` is the two placement counts added together, which is the
 * whole of what a caller has to know about the coupling.
 *
 * @p text_metrics is **who measures the text this pass lays out** (10.5), and it is
 * an argument for exactly the reason @p viewport is one: it is an input to a pass and
 * not a property of a tree, so a caller that has a font supplies one and a caller
 * that is testing cursor arithmetic -- or laying out a document with no text --
 * supplies NULL and gets the same geometry 10.4 produced. A NULL source is not a
 * failure: every run measures to zero, `robo_layout_text_stats_t.unmeasured` counts
 * them, and a caller can always tell "nobody measured" from "measured to nothing"
 * (`measured` with `width_max` 0) and from "there was no text" (`runs_placed` 0).
 *
 * @p out_text_stats receives what the **text** direction did, and may be NULL like
 * the other two. Three records rather than one because they answer three different
 * questions -- where the boxes went, where the participants on the lines went, and
 * how much of the text anything could measure -- and the third is the one a reader
 * needs precisely when the first two look plausible.
 */
robo_result_t robo_layout_block_layout(robo_layout_tree_t *tree,
                                       robo_layout_size_t viewport,
                                       const robo_layout_text_metrics_t *text_metrics,
                                       robo_layout_block_stats_t *out_stats,
                                       robo_layout_inline_stats_t *out_inline_stats,
                                       robo_layout_text_stats_t *out_text_stats,
                                       robo_layout_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_BLOCK_H */
