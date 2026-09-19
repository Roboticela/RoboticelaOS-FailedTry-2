/*
 * RoboLayout line boxes (Phase 10.6).
 *
 * 10.4 placed the participants of an inline formatting context along a **preliminary
 * line** and said so in its own header: a cursor, a top, a running height and a
 * counter, with the line's identity, alignment, baseline and font-derived height
 * named as 10.6's. This is that phase, and what it adds is a *record*: a line box is
 * now a thing the tree owns, with an id, a finalized rectangle, and the list of
 * objects it placed.
 *
 * Construction state and the record are different things, on purpose
 *
 * The one structural decision here is that the line being built is **not** a line
 * box. The open line is four numbers and a member cursor on the C stack, and it
 * becomes a line box only when it is *finalized* -- at a wrap, or at the end of its
 * context -- at which point an immutable record is appended to the tree's line
 * arena. There is no door that mutates a line box, because there is no code that
 * would need one: the pass holds the open line directly and writes the finished
 * record once. The requirement "keep finalized results separate from temporary
 * construction state" is therefore not a rule this module follows but the shape of
 * the types: `robo_layout_line_box_t` has no member that describes a line still
 * being built, and a caller cannot reach the construction record at all.
 *
 * A line box is a **conclusion of a pass**, exactly as a formatting context is
 * (RoboLayoutFormattingContext.h), so it lives in the same place for the same
 * reason: an arena on the tree, id-addressed, released wholesale by the next pass
 * and by anything that changes the box set or the classification. Nothing public
 * creates or mutates one; the read side below is the whole public surface.
 *
 * The members, and what is deliberately not one
 *
 * A line's members are the objects the line *placed*: the ones whose advance width
 * the line's cursor advanced by. In this build that is a text run, an atomic inline
 * (`inline-block`), and a transparent inline box with no contents -- precisely the
 * three things `layout_inline_place` is called for. A transparent inline box *with*
 * contents is **not** a member of any one line, and that is not an omission: its
 * rectangle is the **union** of what its contents reached (10.4), and a union can
 * span several lines. Recording it as a member of one of them would either state a
 * rectangle the box does not have or double-count the width its children already
 * contributed. So it stays what it is -- a participant of the *context*
 * (`robo_layout_inline_stats_t.participants_placed` counts it) -- and
 * `robo_layout_object_line` answers NOT_FOUND for it, which the door's own note
 * states rather than leaving a caller to discover.
 *
 * Two consequences of that rule, both asserted by the suite:
 *
 *   the members of the lines of one context are exactly the objects that advanced
 *   its cursor, so `robo_layout_line_box_t.members` over all lines, summed, is the
 *   number of placements -- and not the number of participants;
 * *  `line.size.width` is the **sum of its members' widths**, which equals the
 *  cursor the line ended at. That is the current build's rule and not CSS's final
 *  word -- fragments and justification are later phases' -- and the header says so
 *  where the field is declared rather than in a comment somewhere else.
 *
 *  `line.size.height` **was** "the tallest member" through 10.6 and is
 *  `ascent + descent` since 10.7 (RoboLayoutBaseline.h). The two agree for a line
 *  whose members share a font -- which is why the change moved no number in the
 *  suites -- and differ exactly where a baseline model has something to say: a line
 *  mixing `12px` and `24px` text is as tall as the two of them need to sit on one
 *  floor, not as tall as the taller one alone.
 *
 * Geometry, and the space it is in
 *
 * A line's origin and size are in its **owner's content-box space** -- the owner
 * being the box that established the context -- because that is the space the line
 * lives in, exactly as a box's result is in its containing block's. For a direct
 * child of the owner, a participant's result and the line's rectangle are therefore
 * in the same space and can be compared without arithmetic. A line is *not* rebased
 * into a transparent inline box's space the way a participant is (10.4's rebase),
 * because a line belongs to the context and not to a box inside it.
 *
 * An empty inline formatting context has **no** line boxes. That is CSS's rule and
 * this phase keeps it: a line exists because something was placed on it, and a
 * container with nothing to place produces nothing to describe. A line whose single
 * member is an unmeasured text run has width 0 and height 0 and *does* exist -- the
 * distinction between "no line" and "a line with nothing measurable on it" is
 * exactly the distinction 10.5 drew between a source that answers nothing and a
 * source that answered zero.
 *
 * What is deliberately missing
 *
 * Baselines, `line-height` and font-derived line extents are 10.7's. Fragmentation
 * -- splitting one participant across two lines -- is not implemented, so a text run
 * too wide for its line *overflows* rather than breaking, and a line's members are
 * whole objects. Unicode line-breaking, whitespace collapsing, the bidi algorithm,
 * alignment, and painting are absent, and none of them is named here as if it were
 * present.
 *
 * Why there is no new diagnostic domain
 *
 * A domain is where a failure is *reported* (RoboLayoutTypes.h), and every failure
 * this phase can produce is raised by the inline pass that builds lines -- an arena
 * that could not grow is the only one it has, and it is reported in
 * ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT. A LINE_BOX domain with no failure of its own
 * would be a name with nothing behind it, which is the same argument 10.2 used to
 * refuse a FLEX context kind and 9.12 used to refuse `display: flex`.
 */

#ifndef ROBO_LAYOUT_LINE_BOX_H
#define ROBO_LAYOUT_LINE_BOX_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutBaseline.h"
#include "RoboLayout/RoboLayoutFormattingContext.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Identity                                                            */
/* ------------------------------------------------------------------ */

/**
 * Tree-scoped identity of one line box. Never a pointer, never reused.
 *
 * Dense and 1-based within a tree, the same discipline layout object ids and
 * formatting-context ids follow: `id == index + 1` makes walking the lines a loop
 * rather than a lookup, and 0 is the absence.
 *
 * Unlike contexts, lines are *ordered*: index 0 is the first line the pass
 * finalized, and for the lines of one context the order is the order the lines were
 * laid out top to bottom. That is a property worth having and not merely a
 * consequence of storage: a reader asking "what is the third line of this context"
 * gets the third line of that context, and the suite asserts it.
 */
typedef uint32_t robo_layout_line_id_t;

#define ROBO_LAYOUT_LINE_ID_NONE ((robo_layout_line_id_t)0u)

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

/**
 * Whether a line box is still being built or has been finalized.
 *
 * Two values, and in this build every line a caller can read is FINAL. OPEN exists
 * because the vocabulary is the honest description of the *lifecycle* the phase
 * implements -- a line is opened when the first participant is placed on it and
 * finalized at a wrap or at the end of its context -- and because a later phase that
 * keeps an open line across a call boundary (incremental layout, 10.14) would
 * otherwise have to add the distinction to a released API. An OPEN line is never
 * stored in the arena: the construction record is on the pass's stack, which is why
 * `robo_layout_tree_line_at` cannot hand one back.
 */
typedef enum robo_layout_line_state {
    /** Being built: it has a rectangle so far, and more may be placed on it. */
    ROBO_LAYOUT_LINE_OPEN = 0,
    /** Finalized: its geometry and membership are settled and never change again. */
    ROBO_LAYOUT_LINE_FINAL = 1
} robo_layout_line_state_t;

/** Number of defined line states. */
#define ROBO_LAYOUT_LINE_STATE_COUNT 2u

/** Non-zero when @p state is a defined enumerator. */
int robo_layout_line_state_is_known(robo_layout_line_state_t state);

/** Human-readable state name ("open", "final"); never NULL, "unknown" for an
 *  undefined enumerator. */
const char *robo_layout_line_state_name(robo_layout_line_state_t state);

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

/**
 * One finalized line box.
 *
 * A record by value and owned by nobody: it holds no allocation -- the membership
 * lives in the tree's member arena -- so reading one out copies it and the tree is
 * unaffected. The tree owns the array it lives in (RoboLayoutTree.h).
 *
 * Everything in it is already in the integer pixel space the results use, because
 * that is the space the pass works in: `origin` is the line's x and y in its owner's
 * content-box space, and `size` is the occupied width and the line's height.
 */
typedef struct robo_layout_line_box {
    /** Tree-scoped identity; never NONE for a live line. */
    robo_layout_line_id_t id;
    /** Always FINAL for a line read out of the tree (see the state vocabulary). */
    robo_layout_line_state_t state;

    /** The formatting context this line belongs to. */
    robo_layout_fc_id_t context;
    /**
     * The box whose *contents* this line holds -- the context's owner.
     *
     * Carried beside `context` rather than derived from it so that a reader with a
     * line and no context arena still knows whose line it is, and so that the two
     * facts cannot be read from each other's absence.
     */
    robo_layout_node_id_t owner;
    /**
     * 0-based position of this line within its context, in layout order.
     *
     * The first line of a context is 0, the next 1, and so on -- so a reader can
     * detect a gap or a duplicate without sorting anything.
     */
    size_t index;

    /** Top-left of the line, in the owner's content-box space. */
    robo_layout_point_t origin;
    /**
     * Occupied width and line height, in px.
     *
     * `width` is the sum of the members' widths, which is the cursor the line
     * finished at; `height` is the *tallest* member on the line -- the current
     * build's rule, chosen because it is the one number this phase can derive
     * without a font's own line metrics. A real `line-height` and a baseline-derived
     * extent are 10.7's, and a line whose only member is unmeasured is 0 x 0 rather
     * than absent (the header's note on empty contexts).
     */
    robo_layout_size_t size;
    /**
     * Where the baseline sits in this line, and the extent it was derived from
     * (Phase 10.7).
     *
     * `size.height` **is** `baseline.ascent + baseline.descent` by construction, so
     * the three numbers cannot disagree: the line's height is not a fourth quantity
     * that happens to match, it is the extent the baseline model spans. A caller
     * comparing them is checking the pass's arithmetic, and the suite does exactly
     * that over every line.
     *
     * The metrics are stated **relative to the baseline**, which is why they carry no
     * coordinate: where the baseline *is* in space is `origin.y + ascent`, and the two
     * doors below state that conversion in the two spaces a reader might be in
     * (`baseline_offset` in the line's own, `baseline_y` in its owner's).
     *
     * `kind` is never NONE for a line read out of the tree, and that is a fact about
     * what gets stored rather than a promise: a line exists only because something was
     * placed on it, and every object this build places has a baseline (a text run's
     * font metrics, or the BOX fallback -- RoboLayoutBaseline.h).
     */
    robo_layout_baseline_metrics_t baseline;
    /**
     * The width the line had to fit into: its owner's content width.
     *
     * Recorded because "why did this line wrap?" is otherwise unanswerable from the
     * line alone -- the answer is a comparison against a number the line does not
     * otherwise carry -- and because the comparison is what a later phase's wrapping
     * diagnostics and overflow reporting will need.
     */
    int32_t available_width;
    /**
     * Objects placed on this line, in layout order.
     *
     * The *placements*, not the participants of the context: a transparent inline
     * box with contents encloses members without being one (the header argues why),
     * so this is smaller than `robo_layout_inline_stats_t.participants_placed` for a
     * document that nests inline boxes. Read them with
     * `robo_layout_line_member`.
     */
    size_t members;
} robo_layout_line_box_t;

/** Clear @p line to "no line": NONE identity, open, zero geometry and no members.
 *  Safe on NULL. */
void robo_layout_line_box_init(robo_layout_line_box_t *line);

/** Field-by-field equality, including identity. Safe on NULL. */
int robo_layout_line_box_equal(const robo_layout_line_box_t *a,
                               const robo_layout_line_box_t *b);

/** The line's rectangle, formed from @p origin and @p size.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL argument. */
robo_result_t robo_layout_line_box_rect(const robo_layout_line_box_t *line,
                                       robo_layout_rect_t *out_rect);

/**
 * The line's baseline metrics, by value.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments. @p out is cleared to
 *         NONE first, so a caller that ignores the code has an unusable baseline
 *         rather than a leftover one.
 */
robo_result_t robo_layout_line_box_baseline(const robo_layout_line_box_t *line,
                                           robo_layout_baseline_metrics_t *out);

/**
 * The baseline's distance from the **top of the line**, in px.
 *
 * The line-relative coordinate, which is what a caller holding a line and no other
 * context wants: a renderer drawing downward from the line's top edge adds this and
 * has the floor.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND when the
 *         line carries no usable baseline -- which is a line row a caller built by
 *         hand rather than one read out of a tree, because the pass never stores one.
 */
robo_result_t robo_layout_line_box_baseline_offset(const robo_layout_line_box_t *line,
                                                   int32_t *out_offset);

/**
 * The baseline's **y in the space the line's own origin is in** -- its owner's
 * content-box space.
 *
 * `origin.y + ascent`, and the same conversion as the door above stated one space
 * out. Both exist because the two are different questions that a single accessor
 * would have let a caller conflate: one is "how far down the line is the floor?" and
 * the other is "where is the floor on the page?".
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND when the
 *         line carries no usable baseline.
 */
robo_result_t robo_layout_line_box_baseline_y(const robo_layout_line_box_t *line,
                                             int32_t *out_y);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_LINE_BOX_H */
