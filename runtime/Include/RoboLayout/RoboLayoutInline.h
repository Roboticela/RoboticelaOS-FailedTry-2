/*
 * RoboLayout inline layout (Phase 10.4).
 *
 * 10.3 answered "where does this box go?" for boxes stacked one below another.
 * This is the other direction, and the answer it computes is a *line*: the boxes
 * that participate in an inline formatting context are placed one after another
 * along it, and when the next one does not fit, a new line begins below.
 *
 * Who is on the line
 *
 * Not "every inline-level box" — that is 9.12's vocabulary, and it is not the
 * question. The question is the one 10.2 exists to answer: *which context does
 * this box participate in?* A box is placed along a line exactly when the context
 * it participates in is an INLINE one, and 10.2 establishes that context for a
 * block container with no block-level child — `display: block` holding nothing but
 * text, or nothing but inline boxes. That is why this phase needed no anonymous
 * box to get started: `<div><span></span><span></span></div>` already has an inline
 * formatting context, and it is the div's own.
 *
 * The one distinction the phase turns on: atomic and non-atomic
 *
 * An inline-level box comes in two kinds, and they are not degrees of the same
 * thing:
 *
 *   an **atomic** inline establishes a formatting context of its own, so it is
 *   opaque to the line: it occupies a horizontal run, and what is inside it is
 *   laid out *inside* it. In this build that is `inline-block`, and 10.2 says why
 *   in one line — its inner display is `flow-root`. It is placed *as a unit*, so
 *   the line has to know its width before it is placed, which is what makes it the
 *   one thing here the wrapping rule can act on.
 *
 *   a **non-atomic** inline establishes nothing, so it is **transparent** to the
 *   line: its children are participants of the *same* context and are placed on
 *   the *same* line, as if they were the container's own children. In this build
 *   that is `inline`. Its own rectangle is then not a size it has but a **union**
 *   of what its contents reached.
 *
 * and 10.5 adds the participant that is neither: a **text run**. A text node is not
 * a box, but it is a participant -- measured, placed at the cursor, and advancing it
 * by its own advance width -- and that is what makes a `span` holding only text stop
 * being zero-wide.
 *
 * Getting this backwards is the expensive mistake, exactly as 10.2's
 * participating/establishing pair was: a build that treated a `span` as opaque
 * would place it with no width (it has none to measure) and drop everything inside
 * it, and a build that treated an `inline-block` as transparent would let its
 * contents stack in the outer line. So the rule is stated once, in terms of the
 * two facts the architecture already carries — the box's outer display, and
 * whether it established a context — and the suite asserts it as a table:
 *
 *   atomic      robo_css_display_is_inline_level(display)  &&  (the box
 *                                                               established a
 *                                                               context)
 *   non-atomic  robo_css_display_is_inline_level(display)  &&  (it established
 *                                                               none)
 *
 * A **block-level** box participating in an inline context is the third case, and
 * the phase declines it rather than placing it badly: CSS wraps the inline run in
 * an *anonymous* block box first, and that box does not exist in this build — it
 * has no element and therefore no computed style (RoboLayoutObject.h), so
 * inventing one here would be inventing an unstyled box nothing can measure.
 * Those boxes are counted (`block_level_declined`) and left UNRESOLVED, which is
 * the same treatment 10.3 gave them and the same reason.
 *
 * Where a box's width comes from, and why one of them is zero
 *
 *   an **atomic** inline is sized by the declarations that apply to it. `width`
 *   and `height` apply to an atomic inline in CSS, so both are read through the
 *   same rule 10.3 reads `height` with: a computed LENGTH in `px` is usable, and
 *   anything else — `auto`, a percentage, `em` — is not. A width this build cannot
 *   resolve falls back to the containing block's content width **less its horizontal
 *   margins** since 10.9 and less its horizontal **padding** since 10.10, which is what
 *   10.3 does for a block box and is CSS's
 *   `width: auto`; shrink-to-fit is intrinsic sizing and is 10.8's. A height it
 *   cannot resolve comes from what the box contained. Its declared `width` is its
 *   **content** box, under 10.10's `content-box` model, so the padding is outside the
 *   number the author wrote and inside the number the line advances by.
 *
 *   a **non-atomic** inline is sized by *nothing* it declares, and that is CSS
 *   rather than a shortcut: `width` and `height` do not apply to a non-replaced
 *   inline box, whose extent is the sum of its inline content. So its rectangle is
 *   the union of what its participants reached in the line's coordinate space —
 *   which is a real computation, not a placeholder — and it is **zero** when it
 *   contains no box at all. **10.9 extends the same rule to its margins**, and 10.10 to
 *   its **padding**: no margin and no padding applies to a non-replaced inline box in
 *   this build, so its used values are nothing even when the author declared some, and
 *   its union is unchanged. CSS does grow such a box's inline advance by its horizontal
 *   padding and its line box by its vertical padding; this build does neither, and the
 *   reason is that a transparent inline box's rectangle is *derived* from its contents
 *   rather than placed, so padding on it is a rule about fragmenting an inline box across
 *   lines — the anonymous-box work this module has not done. It is stated in
 *   `RoboLayoutPadding.h` and asserted as a value by the suite.
 *
 * and since 10.9 a box's **margins** are part of where it goes and not part of how
 * big it is. An atomic participant is placed `margin-left` in from the cursor and the
 * cursor then advances by its **margin box** — `left + width + right` — so a wrapped
 * line accounts for what each participant *occupies* rather than for its content box;
 * and its baseline is its **bottom margin edge**, which is what CSS specifies for an
 * `inline-block` with no in-flow line box of its own. **10.10 puts the padding inside
 * that box**: the `width` this arithmetic advances by is the participant's **border** box
 * — its content box plus its padding — so the same two terms now span three boxes, which
 * is why the padding reaches the cursor, the result and the baseline's extent without any
 * of them being told about the property. Every `auto` margin is zero here
 * whether the width is settled or not, because distribution is a block-level
 * normal-flow rule: an inline participant has no width constraint to be under- or
 * over-, and `robo_layout_margins_resolve_inline` is where that is stated
 * (RoboLayoutMargin.h).
 *
 * That zero was the honest answer *in 10.4*, and the phase said so loudly rather
 * than dressing it up: a `<span>` holding only text had no extent because nothing in
 * that build measured text, and `layout.inline.textless` asserted the zero *and* the
 * reason. **10.5 is the goal that replaced it**, and it did so without changing this
 * rule: a text run is now a participant, so the union is the union of *its* extents.
 * The rule itself -- a non-atomic inline's rectangle is what its contents reached,
 * and nothing it declares -- is unchanged, which is exactly why the zero was worth
 * stating instead of papering over.
 *
 * Wrapping, and what a "line" is here
 *
 * The rule is one sentence: when an object whose advance width is known *before*
 * it is placed does not fit after the line's cursor, the cursor returns to the
 * line start and the line moves down by the height of the tallest thing on it.
 * That covers leaves and atomic inlines — the two cases whose width is knowable in
 * advance. A non-atomic inline with contents is *not* pre-measured and is not
 * wrapped as a unit: its contents are placed along the line one at a time and
 * wrap individually, which is what CSS does, and its own rectangle is the union of
 * the fragments it produced.
 *
 * What the state that makes this work is *not*: it is not a line box. A line box
 * is a box — it has an element-less identity, an alignment, a baseline and a
 * height that comes from fonts — and it is 10.6's. What runs below is a
 * **preliminary line**: a cursor, a top, a running height, and a counter. It is
 * stated as such because 10.6 will replace it, and because a caller that read a
 * number out of it and called it a line box would be reading a promise this phase
 * does not make. `robo_layout_inline_stats_t` reports the line *count* and nothing
 * about any individual line.
 *
 * Coordinate space
 *
 * A participant's position is in the inline formatting context owner's
 * **content-box space** — the same space every result is in (RoboLayoutResult.h),
 * because for a direct child of the owner the owner *is* its containing block. A
 * participant nested inside a non-atomic inline, whose containing block is that
 * inline box, has its position rebased to the inline box's own origin, so the
 * convention survives nesting exactly as it survives it for blocks. The rebase is
 * a second walk over the context's participants and it is a *pass* rather than a
 * placement, because an inline box's origin is the union of its contents, which is
 * not known until the last of them has been placed.
 *
 * Determinism, and the accounting
 *
 * The pass is a pure function of the tree, the viewport and the borrowed styles,
 * and a box the inline pass declines is left UNRESOLVED rather than given a zero
 * rectangle — the same statement 10.3 makes, and the reason the result *state*
 * rather than the numbers is the per-box authority.
 *
 * Refusals are counted, never dropped: a box that participates in an inline
 * context is placed, or it is a block-level box this phase declines, or it is
 * inside a box that was not placed at all — and the three cases are told apart by
 * `robo_layout_inline_stats_t` and `robo_layout_block_stats_t` together. A build
 * that silently skipped a box would produce a document whose parts did not add up,
 * and nothing downstream could see it.
 *
 * Why there is no `robo_layout_inline_layout` beside `robo_layout_block_layout`
 *
 * Because the two directions are one pass. An inline formatting context's height
 * *is* the block extent of the box that owns it, so block layout cannot finish
 * before the lines exist and the lines cannot be laid out before the block pass
 * knows where their owner is. The door is `robo_layout_block_layout`
 * (RoboLayoutBlock.h) — unchanged in what it is for, now laying out both directions
 * and reporting both records — and a second entry point could only duplicate that
 * walk or run it in secret.
 */

#ifndef ROBO_LAYOUT_INLINE_H
#define ROBO_LAYOUT_INLINE_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutGeometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* What the inline direction saw                                        */
/* ------------------------------------------------------------------ */

/**
 * Counters for one pass's inline work. A record by value, owned by nobody.
 *
 * Counters rather than one total, and the reason is 10.3's: a document's shape is
 * the *mix*. A tree of transparent inline boxes and a tree of atomic ones can place
 * the same number of participants and have completely different geometry, and a
 * suite that could not tell them apart would not be testing the algorithm.
 *
 * Every field is filled by the same pass that fills `robo_layout_block_stats_t`,
 * because it is the same pass; the two records are reported side by side
 * (`robo_layout_stats_t`) and neither is derived from the other.
 */
typedef struct robo_layout_inline_stats {
    /** Inline formatting contexts whose contents were laid out: boxes that
     *  established one, counting those whose contents turned out to be empty. */
    size_t contexts;
    /** Of those, the ones with no participant at all — a `<p>` whose only content
     *  is text, or a truly empty one. The most common shape in a small document,
     *  and therefore worth counting rather than leaving to a reader to subtract. */
    size_t empty_contexts;
    /**
     * Everything given geometry along a line: inline-level boxes **and text runs**.
     *
     * This plus a block pass's `blocks_placed` is every object in a laid-out document
     * that has a result, and since 10.5 the count includes text objects -- which is
     * why the three counters below are about *boxes* specifically. A text run is not a
     * box, so counting it as one would make the atomic/non-atomic pair describe a set
     * of things no formatting context has a rule about.
     */
    size_t participants_placed;
    /** Of those, the **boxes** opaque to the line: inline-level and establishing a
     *  formatting context of their own, so placed as a unit. `inline-block`
     *  today. Text runs are not counted here -- they are not boxes. */
    size_t atomic_placed;
    /** Of those, the **boxes** transparent to the line: inline-level and establishing
     *  nothing, so their children are participants of the same context and their
     *  own rectangle is the union of what those children reached. Text runs are not
     *  counted here either. */
    size_t non_atomic_placed;
    /** Block-level boxes found inside an inline formatting context, which this
     *  phase declines rather than places: CSS gives them an *anonymous* block box
     *  to live in and this build makes none. Counted rather than dropped, because
     *  "not placed yet" and "not in the document" must not look the same. */
    size_t block_level_declined;
    /**
     * **Line boxes** the contexts finalized (10.6).
     *
     * Until 10.6 this counted preliminary lines, and the counters are the same
     * number: a line is finalized exactly when something was placed on it, so the
     * count is the number of non-empty lines and an empty context contributes none.
     * What changed is what a reader can do with it -- the lines themselves are now
     * records in the tree (`robo_layout_tree_line_count`), so this is a check against
     * the arena rather than the only way to learn that anything was on a line.
     */
    size_t lines;
    /**
     * Lines whose baseline came from **font line metrics** (10.7).
     *
     * Equals the number of lines whose baseline-defining member was a measured text
     * run -- "baseline-defining" being the first member to reach the line's final
     * ascent (RoboLayoutInline.c). A line of unmeasured text counts here, with a zero
     * extent: its floor is anchored on text and there is nothing measured above it,
     * which is a different statement from a line with no floor.
     */
    size_t lines_from_text_baseline;
    /**
     * Lines whose baseline came from the **bottom-edge fallback** (10.7).
     *
     * The kind of a line whose tallest member is not a text run: an atomic inline, or
     * an empty inline box. The two counters partition `lines` -- every stored line has
     * a baseline and this build produces exactly two kinds -- so
     * `lines_from_text_baseline + lines_from_fallback_baseline == lines` is an
     * identity a caller can check rather than a coincidence, and the suite does.
     */
    size_t lines_from_fallback_baseline;
    /**
     * Objects placed **on** those lines, summed.
     *
     * Not the same number as `participants_placed`, and the difference is 10.6's
     * central distinction: a **transparent inline box with contents** is a participant
     * of the context and a member of no single line, because its rectangle is the
     * union of what it encloses (RoboLayoutLineBox.h). So this count is the placements
     * -- the objects whose advance the cursor advanced by -- and the suite asserts it
     * against the arena's own member counts rather than against the participant count.
     *
     * Equal to `participants_placed` for a document that nests no inline box, which is
     * the shape most documents have and the reason the distinction is easy to miss.
     */
    size_t line_members;
    /** Participants that began a new line because they did not fit after the
     *  cursor. Zero for a document that wrapped nowhere, which is the number a
     *  wide viewport should produce. */
    size_t wraps;
    /** Atomic participants whose advance width came from a declared `width` this
     *  build could resolve: a computed length in `px`. */
    size_t widths_declared;
    /** Atomic participants with no resoluble declared width, which took the
     *  containing block's content width — 10.3's rule for a block box, and
     *  CSS's `width: auto` until 10.8 gives them shrink-to-fit. */
    size_t widths_filled;
    /** Heights taken from a declared `height` this build could resolve. */
    size_t heights_declared;
    /** Heights obtained from the box's own contents — the `auto` path, and the
     *  path a declared-but-unresolvable height takes. */
    size_t heights_from_content;
    /** **Sides** of an atomic participant whose declared margin reached its advance
     *  (10.9), zero and negative included. Every `auto` is absent from this count *by
     *  rule* rather than by omission: distribution is a block-level normal-flow rule,
     *  so an inline participant's `auto` margin has a used value of 0 and was given
     *  nothing (`RoboLayoutMargin.h`). A transparent inline box and a text run
     *  contribute nothing here, because no margin applies to either. */
    size_t margins_applied;
    /** Sides declared on an atomic participant with a margin this build cannot use —
     *  a percentage, a relative unit (10.9). Used as zero, and counted so that a
     *  document spaced with `margin: 10%` is visible as one. */
    size_t margins_unresolved;
    /** **Sides** of an atomic participant whose declared padding reached its advance
     *  along the line and its border box (10.10), zero included. Summed over sides for
     *  the reason the margin counters are, and counted from the *resolution* so that an
     *  `ABSENT` side is counted nowhere. */
    size_t paddings_applied;
    /** Sides declared on an atomic participant with a padding this build cannot use -- a
     *  percentage, a relative unit, a negative length, or `auto` (10.10). Their used value
     *  is zero, which is why they are counted. */
    size_t paddings_unresolved;
    /** **Sides** of an atomic participant whose declared `border-*-width` reached its
     *  advance along the line and its border box (10.11), zero included. Summed over sides
     *  for the reason the counters above it are, and counted from the resolution so that an
     *  `ABSENT` side is counted nowhere. A transparent inline box and a text run contribute
     *  nothing here, because no border applies to either. */
    size_t borders_applied;
    /** Sides declared on an atomic participant with a border width this build cannot use
     *  -- a relative unit, a `calc()`, a negative length, or `auto` (10.11). Used as zero,
     *  and counted so that a document whose borders depend on one is visible as such. */
    size_t borders_unresolved;
    /** Sides of an atomic participant whose **used** width is above zero (10.11): a border
     *  that takes room along the line and that a paint pass will draw. Distinct from
     *  `borders_applied` for the reason the block record's is: `border-width: 5px` with
     *  `border-style: none` resolves and draws nothing. */
    size_t borders_drawn;
    /** **Sides** of an atomic participant whose declared inset reached this pass on a
     *  `relative` box (10.12), zero and negative included. Summed over sides for the
     *  reason every counter above it is, and it is a count of *atomic participants* only:
     *  a transparent inline box's rectangle is a union and a text run is not a box, so
     *  neither carries a displacement. */
    size_t offsets_applied;
    /** Sides whose inset this phase **did not act on** (10.12): the box's mode was not
     *  `relative`, so CSS applies no offset (`static`) or this build does not move it
     *  (`absolute`, `fixed`, `sticky`). Counted so that a document with insets on a
     *  non-relative participant is visible as one. */
    size_t offsets_declined;
    /** Sides declared on an atomic participant with an inset this build cannot use -- a
     *  percentage, a relative unit (10.12). Used as zero, and counted so that a document
     *  displaced with `left: 10%` is visible as one. */
    size_t offsets_unresolved;
    /** Atomic participants given a **non-zero** displacement (10.12). Counted for the
     *  reason the block record counts it: four declarations can resolve and move nothing,
     *  so "how many boxes did this move?" is a question only a box count answers. */
    size_t boxes_shifted;
    /** Deepest participant placed. */
    uint32_t depth_max;
} robo_layout_inline_stats_t;

/** Clear @p stats to zero. Safe on NULL. */
void robo_layout_inline_stats_init(robo_layout_inline_stats_t *stats);

/** Field-by-field equality. Safe on NULL. */
int robo_layout_inline_stats_equal(const robo_layout_inline_stats_t *a,
                                   const robo_layout_inline_stats_t *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_INLINE_H */
