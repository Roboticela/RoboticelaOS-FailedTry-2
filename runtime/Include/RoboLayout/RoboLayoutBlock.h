/*
 * RoboLayout block layout (Phase 10.3).
 *
 * The first phase that computes a coordinate. Everything before it decided
 * *which* boxes there are, *what* they are and *which* formatting context
 * coordinates them; this one answers where each one goes and how big it is, in
 * the one direction the block algorithm owns: down.
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
 *   1. it fills its containing block's content width;
 *   2. its block-level children are stacked from its content origin, each below
 *      the last;
 *   3. its height is the declared one if this build can resolve it, and the
 *      stacked extent of its children otherwise;
 *   4. its result is written in its containing block's coordinate space, which is
 *      what `robo_layout_result_t.position` has meant since 10.1.
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
 *   Margins, padding and borders are **10.9, 10.10 and 10.11**. So a box's
 *   content box is its border box and its content origin is its top-left corner.
 *   The values are readable in the borrowed style and simply not applied, which
 *   is why `margin: 10px 20px` in the pipeline document changes no number here.
 *
 *   Inline layout is **10.4**. A box that participates in an inline context is
 *   not placed by this pass, and neither is anything inside it -- an inline box
 *   has no geometry yet, so a descendant placed inside it would be placed inside
 *   nothing. Those boxes are counted (`inline_level_skipped`, `unreached`) rather
 *   than silently omitted, and the recursion descends only through boxes this
 *   pass placed. That is also why the pass creates no **anonymous** box: an
 *   anonymous block box exists to give an inline run a block-level home, and a
 *   line box is 10.4's.
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
    /** Boxes block layout deliberately did not place because they are laid out
     *  along a line instead: their own display is inline-level, or the context
     *  they participate in is not a block-direction one. 10.4 places these. */
    size_t inline_level_skipped;
    /** Boxes that received no geometry because nothing that contains them was
     *  placed -- a block-level box inside an inline box, which needs the
     *  anonymous box an inline run is wrapped in and which 10.4 builds. Counted
     *  rather than dropped, because "no geometry" and "not in the document" must
     *  not look the same. */
    size_t unreached;
    /** Heights taken from a declared `height` this build could resolve: a
     *  computed length in `px`. */
    size_t heights_explicit;
    /** Heights obtained by stacking children -- the `auto` path, and the path a
     *  declared-but-unresolvable height takes. */
    size_t heights_from_content;
    /** Containers with no block-level child at all, so the stacked extent is
     *  zero and the height is `0` unless one was declared. The single most
     *  surprising fact about this phase, and therefore one worth counting. */
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
 */
robo_result_t robo_layout_block_layout(robo_layout_tree_t *tree,
                                       robo_layout_size_t viewport,
                                       robo_layout_block_stats_t *out_stats,
                                       robo_layout_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_BLOCK_H */
