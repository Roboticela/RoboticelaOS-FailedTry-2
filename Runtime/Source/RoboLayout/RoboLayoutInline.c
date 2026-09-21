/*
 * RoboLayout inline layout (Phase 10.4) — see RoboLayoutInline.h.
 *
 * One walk over an inline formatting context's participants, and three ideas in
 * it. The header argues all three; this file is where they have to survive
 * contact with a tree:
 *
 *   a participant is placed at the *cursor*, and the cursor advances by its
 *   width. A participant that does not fit after the cursor starts a new line,
 *   because the cursor is the only thing that carries from one to the next;
 *
 *   a **transparent** inline box (display `inline`, establishing nothing) is not
 *   a unit: its children are participants of the same context, so they are placed
 *   by the same loop and the box's own rectangle is the *union* of what they
 *   reached. That is what the scope chain below is for — a handful of integers on
 *   the C stack, nested and unwound by the recursion itself;
 *
 *   an **atomic** inline (display `inline-block`) *is* a unit, so its width has to
 *   be known before it is placed — which is what makes it the one kind the wrap
 *   rule can act on — and its contents are laid out by the block pass, in the
 *   box's own space, through the door 10.3 exposes for exactly this call.
 *
 * Nothing here allocates, caches or reads a previous pass: the flow state lives on
 * the stack of `LayoutInlineWalkContents` and the scope chain is the call
 * stack's own shape. That is why a caller can run the same tree twice and get the
 * same numbers back, and why there is no per-context scratch to release.
 *
 * The one place this file could quietly go wrong is the *ordering* of the writes,
 * and it is the mirror of the one 10.3 names: a transparent inline box's geometry
 * cannot be written until its last participant has been placed, because until then
 * its own rectangle is still growing. So a parent is written after its children —
 * the same order a block container's height forces — and the third step below
 * (`rebase`) exists because of what that costs: a child was written in the *line's*
 * space, and its containing block is the box that turned out to surround it.
 */

#include "RoboLayout/RoboLayoutInline.h"

#include "RoboCSS/RoboCSSDisplay.h"

#include "RoboLayout/RoboLayoutFormattingContext.h"
#include "RoboLayout/RoboLayoutObject.h"
#include "RoboLayout/RoboLayoutResult.h"

#include "RoboLayoutInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The preliminary line                                                */
/* ------------------------------------------------------------------ */

/**
 * The line the cursor is on.
 *
 * Deliberately four numbers and not a box. A line *box* has identity, alignment,
 * a baseline and a height that comes from fonts, and it is Phase 10.6's — so what
 * runs here is a cursor, the top of the line it is on, the tallest participant
 * placed on it so far, and how many lines the context has started. The header
 * calls it preliminary in the same words, and `LayoutInlineStats` reports
 * the *count* and nothing about any individual line.
 *
 * `top` and `height` are in the owner's content-box space, so `top + height` at the
 * end of a context is the extent the block pass needs for the owner's height.
 */
typedef struct layoutInlineLine {
    /** x the next participant starts at. */
    int32_t cursor;
    /** y of the current line, in the owner's content-box space. */
    GfxPixelY top;
    /**
     * The baseline model, accumulated as members arrive (Phase 10.7).
     *
     * `ascent` is the **maximum** ascent of the members placed so far and `descent`
     * the maximum descent, which is CSS's rule for a line: the line has to be tall
     * enough for the tallest thing above its floor and the deepest thing below it.
     * The line's height is their sum, which is what makes "the tallest member" (10.6)
     * the same number whenever every member shares a font and a different one exactly
     * where a baseline model has something to say.
     *
     * They are accumulated here rather than read off the finalized members at the end
     * because a member's ascent is known at placement and the line's maximum must be
     * monotonic for the placement rule to be statable at all.
     */
    int32_t ascent;
    int32_t descent;
    /**
     * Which member's font determination the line's baseline is anchored on.
     *
     * The kind of **the first member to reach the line's final ascent** -- the one
     * whose top edge is the line's top edge, and therefore whose baseline *is* the
     * line's. A later member that merely ties does not take it over, so the answer is
     * "the first, among the tallest", which is a fact about the document's order and
     * not about the order a walk happened to visit -- and for a line whose members
     * share one font it is simply that font's kind.
     *
     * Set by the first member unconditionally, so a line of unmeasured text reports
     * TEXT with a zero ascent rather than NONE: the baseline is anchored on text, and
     * its extent is zero because nothing measured it.
     */
    LayoutBaselineKind baselineKind;
    /**
     * Sum of the placed members' widths -- the occupied width of the line.
     *
     * Accumulated rather than taken from the cursor, and the two agree by
     * construction (every placement advances the cursor by exactly the width it adds
     * here), which is what makes the equality a fact about the algorithm rather than
     * a coincidence. It is a *separate* accumulator because the definition
     * `line.width = sum of member widths` is the one Phase 10.6 states and the one
     * `report.line_geometry` asserts; a build that later places a member without
     * advancing the cursor (a zero-advance box, a mark) would then differ from the
     * cursor and the difference would be visible rather than absorbed.
     */
    int32_t width;
    /** 0-based ordinal of this line within its context (RoboLayoutLineBox.h). */
    size_t index;
    /** Members placed on this line, and the ends of their chain. */
    size_t members;
    LayoutLineMemberId firstMember;
    LayoutLineMemberId lastMember;
} LayoutInlineLine;

/**
 * One context's flow state, for the length of one call.
 *
 * The scope chain 10.4 kept here is gone, and its absence is this phase's one
 * structural change: a transparent box's rectangle is the union of what its contents
 * reached, and since 10.7 a participant's position is not final until its line is
 * finalized -- possibly long after the box that enclosed it closed. So the union is a
 * post-order walk over the finished context (`LayoutInlineUnion`) rather than an
 * accumulator on the recursion's stack, and there is nothing left for a scope to hold.
 * RoboLayoutInline.h §5 argues the two walks and why the rebase keeps the shape it has.
 */
typedef struct layoutInlineFlow {
    /** The owner's content width: the line's width, and the fallback a
     *  width-less atomic inline fills (see the header). */
    int32_t availableWidth;
    /** The open line -- construction state, on the caller's stack, not a record. */
    LayoutInlineLine line;
    /** The context these lines belong to, and the box that established it. */
    LayoutFcId context;
    LayoutNodeId owner;
    /**
     * Lines finalized by this context so far, and the id of the last one.
     *
     * The count is what the stats record reports and what gives the *next* line its
     * `index`; the id is what the block extent is read back from, so that the extent
     * comes from the finalized record rather than from the open line that is about to
     * be discarded with the stack frame.
     */
    size_t lines;
    size_t membersTotal;
    LayoutLineId lastLine;
} LayoutInlineFlow;

/* ------------------------------------------------------------------ */
/* Placing                                                             */
/* ------------------------------------------------------------------ */

/** Count a participant: the one place the three per-box facts are recorded. */
static void LayoutInlineCount(LayoutWalk *walk,
                                LayoutInlineFlow *flow,
                                const LayoutObject *object)
{
    (void)flow;
    walk->inlineFlow->participantsPlaced++;
    if (object->depth > walk->inlineFlow->depthMax) {
        walk->inlineFlow->depthMax = object->depth;
    }
}

/**
 * Finalize the open line: append it to the tree as a line box, and reset it.
 *
 * The one moment construction state becomes a record, and it is a *no-op* for a line
 * nothing was placed on. That is CSS's rule and not an optimisation: an empty inline
 * formatting context has no line boxes, so a container with nothing to place
 * produces nothing to describe, and a build that emitted a zero-height line for it
 * would make "no content" and "one empty line" the same document
 * (RoboLayoutLineBox.h).
 *
 * Called at a wrap and at the end of the context -- the only two places a line can
 * end in this build, because nothing fragments a line. It is also where the *extent*
 * a wrap advances by comes from, which is why it is called *before* the advance: the
 * line's height is settled here and the next line starts below it.
 */
static Result LayoutInlineFinalizeLine(LayoutWalk *walk,
                                                 LayoutInlineFlow *flow)
{
    LayoutPoint origin;
    LayoutSize size;
    LayoutBaselineMetrics baseline;
    LayoutLineId id = RoboLayoutLineIdNone;
    Result status;

    if (flow->line.members == 0u) {
        return RoboOk;
    }

    /*
     * The line's baseline, and the whole of 10.7's arithmetic in three assignments:
     * the ascent is the members' maximum ascent, the descent their maximum descent,
     * and the height is their sum -- so the line is exactly as tall as the tallest
     * thing above its floor plus the deepest thing below it, and *no member is ever
     * outside its line's rectangle* (a member's own height is its own ascent plus its
     * own descent, and both terms are bounded by the maxima).
     */
    baseline.kind = flow->line.baselineKind;
    baseline.ascent = flow->line.ascent;
    baseline.descent = flow->line.descent;

    origin.x = 0;
    origin.y = flow->line.top;
    size.width = flow->line.width;
    size.height = LayoutBaselineMetricsExtent(&baseline);
    status = LayoutLineAdd(walk->tree, flow->context, flow->owner, flow->line.index,
                                  origin, size, baseline, flow->availableWidth,
                                  flow->line.firstMember, flow->line.members, &id, walk->diag);
    if (status != RoboOk) {
        return status;
    }

    walk->inlineFlow->linesFromTextBaseline +=
        baseline.kind == RoboLayoutBaselineText ? 1u : 0u;
    walk->inlineFlow->linesFromFallbackBaseline +=
        baseline.kind == RoboLayoutBaselineBox ? 1u : 0u;

    /*
     * The back-link and the vertical placement, in one walk. Two writes per member
     * and they are the same walk because they are the same event: this is the moment
     * the line's baseline is known, and a member is on the line precisely when its
     * baseline *is* the line's.
     *
     * Why the y is written here rather than at placement, which is the one thing this
     * phase changes about 10.4's structure: a member's y is
     * `origin.y + (line_ascent - member_ascent)`, and the line's ascent is a maximum
     * over members that is not final until the last of them has been placed. Placing
     * the *first* member at its final y is therefore impossible -- a taller one that
     * arrives later would have to move it, and that is exactly what this walk does.
     * The alternative, placing every member at the line's top and never correcting it,
     * is 10.6: it is right whenever the members share a font and wrong by the
     * difference of their ascents whenever they do not.
     *
     * The subtraction never goes negative and the assertion says so, because a member
     * whose ascent exceeded the line's would mean this walk disagrees with the
     * accumulation in `LayoutInlinePlace` about which maximum it is using -- a bug
     * whose symptom, if it were allowed through, would be a participant drawn *above*
     * its own line.
     */
    {
        LayoutLineMemberId member = flow->line.firstMember;

        while (member != LAYOUT_LINE_MEMBER_ID_NONE) {
            const LayoutLineMember *record =
                LayoutLineMemberSlotConst(walk->tree, member);
            LayoutObject *placed;

            if (!record) {
                break;
            }
            placed = LayoutObjectSlot(walk->tree, record->node);
            if (placed) {
                if (baseline.ascent < placed->baseline.ascent) {
                    /*
                     * Unreachable, and a *reported* failure rather than an assertion
                     * because there is no assert machinery in this module and inventing
                     * one for a single impossible branch would be a second answer to a
                     * question the diagnostic record already answers. The condition is
                     * an invariant between two functions in this file (the accumulation
                     * and the maximum it is read back as), so a trip here is a bug, and
                     * the two ways to be wrong about it -- clamp, or report -- differ in
                     * whether the next reader learns.
                     */
                    return RoboLayoutDiag(
                        walk->diag, RoboLayoutDomainInlineLayout, RoboError,
                        "a line's ascent is smaller than one of its members'");
                }
                placed->line = id;
                /*
                 * And the vertical displacement (10.12), added *after* the baseline
                 * correction because it is a displacement from where the line put the
                 * member and not a second opinion about where the baseline is. A `top: 4px`
                 * on an atomic inline lifts nothing and drops the box 4px; its line's floor
                 * is unchanged, which is why this is the only place a member's `y` is
                 * touched after placement.
                 */
                placed->result.position.y =
                    origin.y + (baseline.ascent - placed->baseline.ascent) +
                    placed->positionOffset.dy;
            }
            member = record->next;
        }
    }

    flow->lines++;
    flow->lastLine = id;
    flow->membersTotal += flow->line.members;

    /* The next line begins where this one ends, with nothing on it. `index` is the
     * only field carried forward, because the *ordinal* counts lines in the context
     * and the rest describe one line. */
    memset(&flow->line, 0, sizeof(flow->line));
    flow->line.index = flow->lines;
    flow->line.top = (GfxPixelY)0;
    return RoboOk;
}

/**
 * Start a new line below the one just finished.
 *
 * The advance is the *finalized* line's height, read back out of the record rather
 * than from the open line, so that a line's contribution to the block stack is the
 * geometry the tree holds and not a number that only existed on the stack. The top
 * therefore accumulates the heights of the lines above -- which is the whole of this
 * phase's vertical arithmetic, and 10.7's baselines will replace it without moving
 * the shape.
 */
static Result LayoutInlineNewLine(LayoutWalk *walk,
                                            LayoutInlineFlow *flow,
                                            LayoutInlineStats *stats)
{
    GfxPixelY top = flow->line.top;
    Result status = LayoutInlineFinalizeLine(walk, flow);

    if (status != RoboOk) {
        return status;
    }
    if (flow->lastLine != RoboLayoutLineIdNone) {
        const LayoutLineSlot *slot = LayoutLineSlotConst(walk->tree,
                                                                    flow->lastLine);

        if (slot) {
            top = (GfxPixelY)(slot->box.origin.y + slot->box.size.height);
        }
    }

    /* `finalize` already advanced `index` to the new line's ordinal -- it counts the
     * lines the context has produced -- so all this call has to do is put the new top
     * in place. The rest of the record is zeroed by the same memset. */
    flow->line.top = top;
    stats->wraps++;
    return RoboOk;
}

/**
 * Whether @p width would not fit after the cursor.
 *
 * The `cursor > 0` guard is the whole of CSS's rule about a box wider than its
 * containing block: an object that does not fit on an *empty* line is placed
 * anyway and overflows, rather than starting lines forever. Overflow is 10.13's
 * fact and this pass sets no flag for it — a box that runs past its line is a
 * fact the result's two flag bits deliberately do not carry yet.
 */
static int LayoutInlineWouldOverflow(const LayoutInlineFlow *flow, int32_t width)
{
    return flow->line.cursor > 0 && (flow->line.cursor + width) > flow->availableWidth;
}

/**
 * Write a participant's geometry, its baseline, and advance the line.
 *
 * The one door a participant's result is written through, so the four things that
 * must happen together — the geometry, the baseline it was placed with, the line's
 * baseline arithmetic, and the cursor — cannot drift apart in one path and not
 * another.
 *
 * `y` is **provisional** (10.7): every member is written at its line's top here and
 * moved to `line_top + (line_ascent - its own ascent)` at finalization, because the
 * line's ascent is not known until the last member is placed. The parameter is kept
 * rather than dropped so that the provisional value is the line's own top on every
 * path, which is what makes the correction the only thing that ever moves a member.
 */
static Result LayoutInlinePlace(LayoutWalk *walk,
                                         LayoutInlineFlow *flow,
                                         LayoutObject *object,
                                         int32_t x,
                                         GfxPixelY y,
                                         int32_t width,
                                         int32_t height,
                                         LayoutBaselineMetrics baseline,
                                         int32_t marginLeft,
                                         int32_t marginRight)
{
    LayoutPoint position;
    LayoutSize size;
    Result status;
    int firstOnLine = flow->line.members == 0u;
    int32_t advance = marginLeft + width + marginRight;

    /*
     * The horizontal margins arrive here rather than being folded into `width` by the
     * caller, and the distinction is the phase in one place: the box is placed
     * `marginLeft` in from the cursor and its **size stays its own** -- the two
     * numbers are not added together anywhere, even though the cursor moves past both.
     */
    /*
     * The horizontal displacement (10.12) is applied to the *position* and never to the
     * *advance* below, which is the phase in two lines: a relatively positioned atomic
     * inline is drawn `dx` further along its line while the cursor moves by its margin box
     * exactly as it always would. The offset is read off the object because the block pass
     * writes it the same way for a block box -- one record, two passes.
     *
     * The vertical half is applied at finalization instead, because a member's `y` is not
     * final until its line's baseline is (10.7), and adding it here would be overwritten by
     * the correction walk there.
     */
    position.x = x + marginLeft + object->positionOffset.dx;
    position.y = y;
    size.width = width;
    size.height = height;
    status = LayoutResultSetGeometry(&object->result, position, size, walk->diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * The baseline the object was placed with, kept on the object beside its line
     * (RoboLayoutObject.h). Written here rather than at finalization because this is
     * where the measurement is in hand -- and because the correction walk needs it
     * *before* the line's own maximum is applied to it.
     */
    object->baseline = baseline;

    /*
     * And the line's own accumulating maximum, with the two rules 10.7 states:
     *
     *   the first member sets the line's ascent and kind unconditionally, so a line
     *   of unmeasured text reports a TEXT baseline of zero extent rather than no
     *   baseline at all -- the baseline is anchored on text and has no extent because
     *   nothing measured it, which is a different statement from "no baseline";
     *
     *   a later member takes the ascent over only by *strictly* exceeding it, so the
     *   kind belongs to the first member among the tallest. That makes the answer a
     *   fact about document order rather than about the order a walk visited, and for
     *   a line whose members share a font it is simply that font's kind.
     *
     * The descent takes no kind: it is a maximum like the ascent, but the *floor* is
     * what a baseline is and only the ascent can determine where it goes.
     */
    if (firstOnLine || baseline.ascent > flow->line.ascent) {
        flow->line.ascent = baseline.ascent;
        flow->line.baselineKind = baseline.kind;
    }
    if (baseline.descent > flow->line.descent) {
        flow->line.descent = baseline.descent;
    }

    /*
     * And the line's own bookkeeping, in the one place a placement happens. The
     * member is appended *before* the cursor moves, so a placement that could not be
     * recorded is a failure rather than a line whose width counted an object its
     * membership did not: the two facts are the same fact, and the reason the append
     * is not optional is exactly that a reader asking "how wide is this line?" and a
     * reader asking "what is on it?" must not be able to disagree.
     */
    status = LayoutLineMembersAppend(walk->tree, object->id, &flow->line.firstMember,
                                             &flow->line.lastMember, &flow->line.members,
                                             walk->diag);
    if (status != RoboOk) {
        return status;
    }
    /*
     * `object->line` is deliberately **not** written here. The line this
     * participant is going on has not been finalized yet, so it has no id to write --
     * and it cannot be guessed: laying out an atomic inline recurses into the block
     * pass, which finalizes lines of its own *between* this line's first placement and
     * this line's finalization, so "the next id" is not this line's id. The back-link
     * is filled in at finalization, in one walk of the chain, which is the only moment
     * the id exists and the members are known together
     * (`LayoutInlineFinalizeLine`).
     */

    /*
     * And the cursor moves by the **advance**: left margin, the box, right margin. So
     * a line's accounted width is the sum of what its members *occupy* rather than the
     * sum of their content boxes, which is the same statement 10.6 made and the reason
     * the definition survives this phase: for a text run and for a marginless box the
     * two are equal, and where they differ the margin box is the honest one -- a
     * `<span>`-shaped box with a `margin-right` does push the next thing along.
     */
    flow->line.cursor = x + advance;
    flow->line.width += advance;
    LayoutInlineCount(walk, flow, object);
    return RoboOk;
}

/**
 * Place a participant whose advance width is known before it is placed: an atomic
 * inline, or a non-atomic one with no contents at all.
 *
 * Both cases end at the same cursor arithmetic, and they differ in where the
 * numbers come from — which is the header's central distinction, in code:
 *
 *   an **atomic** inline is sized by its declarations, through the sizing layer both
 *   directions share (10.8, `LayoutObjectSizing`): a computed length in `px`
 *   is usable and nothing else is. A width it cannot resolve takes the containing
 *   block's content width, which is CSS's `width: auto` and what 10.3 does for a
 *   block box; a height it cannot resolve comes from what it contained, laid out by
 *   the block pass through 10.3's own door. The *width* resolves first and is what
 *   those contents are laid out into, which is 10.8's one structural change to this
 *   path and the reason `LayoutSizingResolveWidth` exists apart from the pair.
 *
 *   a **non-atomic** inline with no contents is placed with no extent at all, and
 *   the header argues why that zero is the honest answer rather than a placeholder:
 *   nothing in this build measures text, and 10.5 is the goal that will.
 */
static Result LayoutInlinePlaceLeaf(LayoutWalk *walk,
                                              LayoutInlineFlow *flow,
                                              LayoutNodeId id)
{
    LayoutObject *object = LayoutObjectSlot(walk->tree, id);
    LayoutBaselineMetrics baseline;
    int atomic;
    int32_t width = 0;
    int32_t height = 0;
    int32_t marginLeft = 0;
    int32_t marginRight = 0;
    int32_t marginBottom = 0;

    if (!object) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainInlineLayout, RoboError,
                                "a box vanished under the inline pass");
    }

    atomic = object->contextOwned != RoboLayoutFcIdNone;

    if (atomic) {
        LayoutSizing sizing;
        LayoutSize content;
        LayoutSize contentSize;
        LayoutSize paddingBox;
        LayoutSize borderBox;
        LayoutMargins declared = LayoutMarginsOf(object->style);
        LayoutMarginEdges edges;
        LayoutBorders declaredBorder;
        LayoutBorderEdges borders;
        LayoutPaddings declaredPadding;
        LayoutPaddingEdges padding;
        LayoutPosition declaredPosition;
        LayoutPositionOffset positionOffset;
        int32_t paddingBoxAvailable = 0;
        int32_t contentAvailable = 0;
        Result sizingStatus;

        /*
         * The margins first (10.9), for the reason the block pass resolves them first
         * and one more: an atomic inline's horizontal margins move the cursor it is
         * placed at *and* take space out of what its own `auto` width may fill. The
         * containing block here is the inline context's content box, which is what
         * 10.4's fallback has always filled.
         *
         * The **inline** policy, not the block one, and `auto` therefore does not
         * distribute: an inline-level box has no width constraint to be under- or
         * over-, so CSS gives every `auto` margin a used value of 0 here. Reaching for
         * the block door would centre an `inline-block` inside its line, which is not
         * a thing CSS does.
         */
        sizingStatus = LayoutMarginsResolveInline(&declared, flow->availableWidth,
                                                          &edges, walk->diag);
        if (sizingStatus != RoboOk) {
            return sizingStatus;
        }
        marginLeft = edges.left;
        marginRight = edges.right;
        marginBottom = edges.bottom;

        /*
         * And the border and the padding (10.11, 10.10), which for this box are the same
         * doors the block pass calls: neither has an `auto` and neither resolves against
         * anything, so an atomic inline and a block box read the same declarations the same
         * way. Both are on the *inside* of the box, so they take room from the participant's
         * own contents rather than from the line -- which is exactly why they still change
         * the *advance*: a wider box advances the cursor further.
         */
        declaredBorder = LayoutBordersOf(object->style);
        sizingStatus = LayoutBorderResolve(&declaredBorder, &borders, walk->diag);
        if (sizingStatus != RoboOk) {
            return sizingStatus;
        }
        declaredPadding = LayoutPaddingsOf(object->style);
        sizingStatus = LayoutPaddingResolve(&declaredPadding, &padding, walk->diag);
        if (sizingStatus != RoboOk) {
            return sizingStatus;
        }
        /* One subtraction per layer, in the order the box is built: the line's content box
         * less the margins, less the border, less the padding. */
        sizingStatus = LayoutBorderEdgesPaddingBoxWidth(&borders,
                                                                 edges.availableContentWidth,
                                                                 &paddingBoxAvailable,
                                                                 walk->diag);
        if (sizingStatus != RoboOk) {
            return sizingStatus;
        }
        sizingStatus = LayoutPaddingEdgesContentWidth(&padding,
                                                              paddingBoxAvailable,
                                                              &contentAvailable, walk->diag);
        if (sizingStatus != RoboOk) {
            return sizingStatus;
        }

        /*
         * The same sizing layer the block pass uses (10.8), and it is the same
         * *helper* too -- "what this box's declarations ask for, into this containing
         * block" is one question and two implementations of it would be two answers -- and
         * since 10.10 the same *input*, too: the inline context's content box less the
         * margins, the border **and the padding**, because CSS's constraint equation has
         * every term however the box participates.
         */
        LayoutObjectSizing(&sizing, object, contentAvailable);

        /*
         * The width, then the contents, then the height -- the two-phase shape the
         * header explains and the reason `LayoutSizingResolve` is not the only
         * door: an `auto` height is the block extent of this box's own contents, and
         * those contents can only be laid out once the width is settled. Resolving
         * both at once would need the contents' extent before it exists.
         */
        sizingStatus = LayoutSizingResolveWidth(&sizing, walk->diag);
        if (sizingStatus != RoboOk) {
            return sizingStatus;
        }
        width = (int32_t)sizing.resolved.width;

        if (sizing.height.kind == RoboLayoutDimensionDefinite) {
            /*
             * Declared, so the contents are not walked -- 10.4's rule, kept: a
             * declared height means this box's height is not the contents' extent, and
             * looking inside one to find something the author already stated would be
             * work whose result nothing reads. The width above is still resolved and
             * still the box's own, so a declared width reaches an `inline-block`'s
             * contents here exactly as it reaches a block's (10.12 does the rest).
             */
            walk->inlineFlow->heightsDeclared++;
        } else {
            GfxPixelY extent = 0;

            if (object->childCount > 0u) {
                Result status = LayoutBlockWalkChildren(walk, id, width, &extent);

                if (status != RoboOk) {
                    return status;
                }
            }
            content.width = sizing.resolved.width;
            content.height = extent;
            sizingStatus = LayoutSizingSetContent(&sizing, content, walk->diag);
            if (sizingStatus != RoboOk) {
                return sizingStatus;
            }
            walk->inlineFlow->heightsFromContent++;
        }

        sizingStatus = LayoutSizingResolveHeight(&sizing, walk->diag);
        if (sizingStatus != RoboOk) {
            return sizingStatus;
        }

        /*
         * And then the box a reader sees, which is where this path and the block pass
         * agree in two calls: the sizing layer's pair is this participant's **content** box
         * (10.10's `content-box` model -- a declared `width: 40px` on an `inline-block` is
         * 40 inside it), the padding layer turns that into the **padding box**, and the
         * border layer turns that into the **border** box. The two locals are then what the
         * placement below writes and what the line advances by, so both insets reach the
         * cursor and the result without either of them knowing the properties exist.
         */
        sizingStatus = LayoutSizingResolved(&sizing, &contentSize);
        if (sizingStatus != RoboOk) {
            return sizingStatus;
        }
        sizingStatus = LayoutPaddingEdgesPaddingBox(&padding, contentSize,
                                                           &paddingBox, walk->diag);
        if (sizingStatus != RoboOk) {
            return sizingStatus;
        }
        sizingStatus = LayoutBorderEdgesBorderBox(&borders, paddingBox,
                                                          &borderBox, walk->diag);
        if (sizingStatus != RoboOk) {
            return sizingStatus;
        }
        width = (int32_t)borderBox.width;
        height = (int32_t)borderBox.height;

        /* Counted from the sources, so the two pairs mean the same thing as the block
         * pass's: a width obeyed its author or filled what it was given, and a height
         * was declared or came from the contents. */
        if (sizing.widthSource == RoboLayoutDimensionSourceDeclared) {
            walk->inlineFlow->widthsDeclared++;
        } else {
            walk->inlineFlow->widthsFilled++;
        }
        walk->inlineFlow->atomicPlaced++;

        /* Counted from the *resolution* like the block pass's, so the two records mean
         * the same thing about the same declarations. `autosUsed` has no counterpart
         * here and needs none: it is zero by rule on this policy, which is what makes
         * the inline record's silence about `auto` a fact rather than a gap. */
        walk->inlineFlow->marginsApplied += edges.resolved;
        walk->inlineFlow->marginsUnresolved += edges.unresolved;
        walk->inlineFlow->paddingsApplied += padding.resolved;
        walk->inlineFlow->paddingsUnresolved += padding.unresolved;
        walk->inlineFlow->bordersApplied += borders.widthsResolved;
        walk->inlineFlow->bordersUnresolved += borders.widthsUnresolved;
        walk->inlineFlow->bordersDrawn += borders.bordersDrawn;
        /*
         * And the position (10.12), which is the one declaration on an atomic inline that
         * changes none of the numbers above it: not its size, not its advance along the
         * line, not its baseline. It is read, resolved to a displacement, and written on
         * the box -- and the two existing placement points read it back, `LayoutInlinePlace`
         * for `dx` and `LayoutInlineFinalizeLine` for `dy`, so the offset applies to a
         * participant the same way the block pass applies it to a block box.
         *
         * Counted here, in the atomic branch, because an atomic inline is the only inline
         * participant that carries one: a **transparent** inline box's rectangle is the
         * union of its contents and a **text run** is not a box at all. Both decline, and
         * the doors say so rather than answering a zero (`RoboLayoutPosition.h`).
         */
        declaredPosition = LayoutPositionOf(object->style);
        sizingStatus = LayoutPositionResolve(&declaredPosition, &positionOffset,
                                                     walk->diag);
        if (sizingStatus != RoboOk) {
            return sizingStatus;
        }
        walk->inlineFlow->offsetsApplied += positionOffset.insetsResolved;
        walk->inlineFlow->offsetsDeclined += positionOffset.insetsDeclined;
        walk->inlineFlow->offsetsUnresolved += positionOffset.insetsUnresolved;
        if (positionOffset.dx != 0 || positionOffset.dy != 0) {
            walk->inlineFlow->boxesShifted++;
        }

        /* And the used margins, padding and border are kept on the box, exactly as the block
         * pass keeps them: the record a later phase wants is the participant's margin box, and
         * re-deriving one from the declaration would re-decide the `auto` question -- while the
         * border and padding records are the only way to recover this participant's content box
         * from the border box its result carries, one inset at a time. */
        object->margins = edges;
        object->padding = padding;
        object->borders = borders;
        object->positionOffset = positionOffset;
        /*
         * And the overflow (10.13), through the same door the block pass uses and at
         * the same point in the box's construction: after every inset is known and after
         * the contents have been laid out, because the measurement reads the *children*
         * and this is the moment they all have rectangles.
         *
         * An atomic inline is the only inline participant with a record of its own -- a
         * transparent box's rectangle is a union and a text run is not a box -- and the
         * two decline at `LayoutObjectOverflow`. What is measured here is the same
         * four extents against the same padding box, so an `inline-block` that spills its
         * contents reports them exactly as a block box does; the *parent* then folds this
         * participant's region in when it measures itself, which is how a spilling
         * `inline-block` reaches the block box that contains its line.
         */
        {
            LayoutOverflowEdges overflow;

            sizingStatus = LayoutOverflowMeasure(walk->tree, object, &borders,
                                                         &padding, borderBox, &overflow,
                                                         walk->diag);
            if (sizingStatus != RoboOk) {
                return sizingStatus;
            }
            object->overflow = overflow;
            walk->inlineFlow->overflowClipped +=
                LayoutOverflowEdgesClipsAny(&overflow) ? 1u : 0u;
            walk->inlineFlow->overflowing +=
                LayoutOverflowEdgesHasOverflow(&overflow) ? 1u : 0u;
            walk->inlineFlow->overflowUnresolved += overflow.axesUnresolved;
            /*
             * And the flags, on the participant's own result rather than on its line's:
             * a box that spills is a box that spills, and the two bits travel with the
             * box a consumer is asking about. The line, the owner and the ancestors each
             * carry their own, which is what makes "which box did this?" answerable
             * without walking children.
             */
            {
                uint32_t flags = 0u;

                if (LayoutOverflowEdgesHasOverflowAxis(
                        &overflow, RoboLayoutOverflowAxisHorizontal)) {
                    flags |= RoboLayoutResultFlagOverflowX;
                }
                if (LayoutOverflowEdgesHasOverflowAxis(
                        &overflow, RoboLayoutOverflowAxisVertical)) {
                    flags |= RoboLayoutResultFlagOverflowY;
                }
                /*
                 * Set through the result's own door and *before* placement, which is the
                 * one place in this pass where the order matters: the participant's
                 * geometry is written later, by `LayoutResultSetGeometry`, and that
                 * door does not touch the flag word -- so the bits set here survive it. A
                 * build that reset them at placement would report no overflow for every
                 * atomic inline and would look correct in every document whose boxes fit.
                 */
                sizingStatus = LayoutResultSetFlags(&object->result, flags,
                                                             walk->diag);
                if (sizingStatus != RoboOk) {
                    return sizingStatus;
                }
            }
        }
    } else {
        walk->inlineFlow->nonAtomicPlaced++;
    }

    /*
     * And its baseline: the **bottom-edge fallback** (RoboLayoutBaseline.h), which is
     * the whole of this build's rule for everything that is not a text run. An atomic
     * inline whose height came from a declaration or from what it contained therefore
     * sits on the line by its feet, and an empty inline box -- which is a leaf here,
     * because a box with no children has no contents to walk -- sits on the baseline
     * with no extent at all.
     *
     * 10.9 adds the bottom **margin** to the extent, and that is CSS's rule rather than
     * a convenience: the baseline of an `inline-block` with no in-flow line box of its
     * own is its **bottom margin edge**, so an `inline-block` with `margin-bottom: 6px`
     * sits 6px above the floor and makes its line 6px taller. Passing `height` alone
     * would put the box's border edge on the floor and quietly ignore a declaration the
     * author wrote. A non-atomic leaf passes 0/0 whatever it declared, because CSS
     * applies no margins to a non-replaced inline box -- which is also why only the
     * atomic path writes `object->margins`.
     */
    if (LayoutBaselineMetricsFromExtent(height + marginBottom, &baseline) != RoboOk) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainInlineLayout, RoboError,
                                "a participant's baseline could not be derived");
    }

    if (LayoutInlineWouldOverflow(flow, marginLeft + width + marginRight)) {
        Result wrapped = LayoutInlineNewLine(walk, flow, walk->inlineFlow);

        if (wrapped != RoboOk) {
            return wrapped;
        }
    }
    return LayoutInlinePlace(walk, flow, object, flow->line.cursor, flow->line.top, width,
                               height, baseline, marginLeft, marginRight);
}

static Result LayoutInlineObject(LayoutWalk *walk,
                                          LayoutInlineFlow *flow,
                                          LayoutNodeId id);

/**
 * Place a **transparent** inline box and everything inside it, then write the box
 * itself as the union of what its contents reached.
 *
 * The recursion is into the *same* flow, which is the whole difference between this
 * and the atomic path: a `span`'s children are participants of the same formatting
 * context, at the same cursor, on the same lines.
 *
 * A box that placed nothing — an empty `span`, or one whose only contents were
 * blocks this phase declines — is written as a zero-size box at the cursor, which
 * is what "it is here and it is empty" looks like in geometry.
 */
static Result LayoutInlinePlaceTransparent(LayoutWalk *walk,
                                                     LayoutInlineFlow *flow,
                                                     LayoutObject *object)
{
    LayoutPoint position;
    LayoutSize size;
    LayoutNodeId child = object->firstChild;
    Result status;
    int32_t cursorAtOpen = flow->line.cursor;

    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree,
                                                                                child);

        if (!childObject) {
            return RoboLayoutDiag(walk->diag, RoboLayoutDomainInlineLayout, RoboError,
                                    "a box's child link names no box in this tree");
        }
        status = LayoutInlineObject(walk, flow, child);
        if (status != RoboOk) {
            return status;
        }
        child = childObject->NextSibling;
    }

    /*
     * A **provisional** rectangle: zero size at the cursor, in the owner's space.
     *
     * This is what 10.4 wrote at the end of a scope, and 10.7 is the phase that had to
     * stop doing the union here. The reason is the baseline: a box's rectangle is the
     * union of what its contents *reached*, and since this phase a participant's
     * position is not final until its line is finalized -- which may happen long after
     * the box that enclosed it closed, at a wrap further down the document, or at the
     * end of the context. A union computed here would therefore be a rectangle built
     * from positions that are about to move, and the amount it was wrong by would
     * depend on whether a later sibling happened to be taller.
     *
     * So the union moved to `LayoutInlineUnion`, a post-order walk run once the
     * context's last line is finalized and every position in it is final. What stays
     * here is the answer for a box that placed *nothing* -- an empty `span`, or one
     * whose only contents were blocks this phase declines: "it is here and it is
     * empty", at the cursor, which is a fact only this moment knows (the union walk
     * reasons from contents, and there are none to reason from).
     *
     * Written directly rather than through `LayoutInlinePlace`, and both halves of
     * that are deliberate: the cursor has already been advanced by the children
     * (advancing it again would count this box's contents twice), and this box is a
     * participant of the *context* rather than a member of any line, so there is no
     * line arithmetic to do.
     */
    position.x = cursorAtOpen;
    position.y = flow->line.top;
    size.width = 0;
    size.height = 0;

    status = LayoutResultSetGeometry(&object->result, position, size, walk->diag);
    if (status != RoboOk) {
        return status;
    }
    walk->inlineFlow->nonAtomicPlaced++;
    LayoutInlineCount(walk, flow, object);
    return RoboOk;
}

/**
 * Place a **text run** (10.5): a text node's content, measured and put on the line.
 *
 * This is the phase's arrival. Before it, a `<span>` holding only text was zero-wide
 * because nothing in the build measured text (10.4's own header said so); now the
 * text node generates an object of its own, that object is a participant of this
 * context, and its advance width comes from a measurement.
 *
 * Four decisions, each of which the text header argues and this call site has to
 * honour:
 *
 *   the run is measured **as a unit**, because splitting a text node into the pieces
 *   a line can actually hold is line breaking and line breaking is 10.6's. So a
 *   measured run *is* a unit whose width is known before it is placed -- which is
 *   what makes it, like an atomic inline, something the wrap rule can act on. A run
 *   wider than the line overflows rather than breaking, and the header says so;
 *
 *   the run is **kept on the object** rather than discarded after placement, so the
 *   measurement's consequences -- width, code points, missing glyphs, direction --
 *   are readable from the tree afterwards (`LayoutObjectTextRun`);
 *
 *   the **style** handed to the measurement is the object's own borrowed style,
 *   which for a text object is its parent element's (RoboLayoutObject.h): every
 *   property a run needs is inherited, so the element's computed style already holds
 *   the answer and a text-node style would be the same values stored twice;
 *
 *   and a **NULL measurer** measures to zero and is *counted as unmeasured* rather
 *   than treated as a failure, which is what keeps every 10.4 expectation about a
 *   document 10.4 could not measure true in this build (RoboLayoutText.h).
 */
static Result LayoutInlinePlaceText(LayoutWalk *walk,
                                              LayoutInlineFlow *flow,
                                              LayoutObject *object)
{
    LayoutTextRun run;
    LayoutBaselineMetrics baseline;
    int usable = LayoutTextMetricsIsUsable(walk->textMetrics);
    Result status;

    status = LayoutTextMeasureNode(&object->domNode, object->style,
                                          walk->textMetrics, &run, walk->diag);
    if (status != RoboOk) {
        return status;
    }
    object->textRun = run;

    /*
     * Counted before the placement below, because these facts are about the
     * *measurement* and the placement is what happens to it. A run that fails to be
     * placed is still a run something answered for, and a reader comparing
     * `runsPlaced` against `measured` learns which of the two stopped.
     */
    walk->text->runsPlaced++;
    if (usable) {
        walk->text->measured++;
    } else {
        walk->text->unmeasured++;
    }
    if (LayoutTextRangeIsEmpty(run.range)) {
        walk->text->empty++;
    }
    walk->text->codePoints += (size_t)run.codePoints;
    walk->text->glyphsMissing += (size_t)run.glyphsMissing;
    if (run.direction == RoboLayoutTextDirectionRtl) {
        walk->text->rtl++;
    }
    if (run.width > walk->text->widthMax) {
        walk->text->widthMax = run.width;
    }
    if (run.height > walk->text->heightMax) {
        walk->text->heightMax = run.height;
    }
    if (object->depth > walk->text->depthMax) {
        walk->text->depthMax = object->depth;
    }

    /*
     * And its baseline, from the font's own line metrics: ascent above, descent below,
     * and the run's line gap split across the two as half-leading so that the metrics'
     * extent equals the run's `height` to the pixel (RoboLayoutBaseline.h). Doing it
     * here rather than inside the measurement seam is what keeps the seam's job one
     * thing -- "how wide is this run?" -- and the *line* geometry this module's.
     */
    if (LayoutBaselineMetricsFromRun(&run, &baseline) != RoboOk) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainInlineLayout, RoboError,
                                "a text run's baseline could not be derived");
    }

    if (LayoutInlineWouldOverflow(flow, run.width)) {
        Result wrapped = LayoutInlineNewLine(walk, flow, walk->inlineFlow);

        if (wrapped != RoboOk) {
            return wrapped;
        }
    }
    /*
     * A text run's margins are 0/0, and that is a fact about text rather than a
     * shortcut: a text node is not a box, CSS has no margin property that applies to
     * one, and this object's borrowed style is its *parent's* -- so reading margins
     * here would read the element's and apply them to the element's characters.
     */
    return LayoutInlinePlace(walk, flow, object, flow->line.cursor, flow->line.top, run.width,
                               run.height, baseline, 0, 0);
}

/**
 * Place one participant of the current inline formatting context.
 *
 * Four answers, and the third is the reason this function is not a loop:
 *
 *   a **text run** is measured and placed (above);
 *
 *   a **block-level** box is declined and counted. CSS would have wrapped the
 *   inline run in an anonymous block box before this point and this build makes
 *   none (RoboLayoutInline.h), so placing it here would stack a block in the middle
 *   of a line;
 *
 *   a **transparent** inline box with contents opens a scope and recurses;
 *
 *   anything else — an atomic inline, or an empty non-atomic one — is a leaf.
 */
static Result LayoutInlineObject(LayoutWalk *walk,
                                          LayoutInlineFlow *flow,
                                          LayoutNodeId id)
{
    LayoutObject *object = LayoutObjectSlot(walk->tree, id);

    if (!object) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainInlineLayout, RoboError,
                                "a box vanished under the inline pass");
    }

    /*
     * Text first, before anything about display: a text object *is* inline-level by
     * derivation (RoboLayoutObject.h) and the check below would pass, so the order
     * is not what makes it correct -- but a caller reading this function should meet
     * the phase's new participant before the old rules, and the two paths share no
     * arithmetic.
     */
    if (object->kind == RoboLayoutObjectText) {
        return LayoutInlinePlaceText(walk, flow, object);
    }

    if (!CssDisplayIsInlineLevel(object->display)) {
        walk->inlineFlow->blockLevelDeclined++;
        return RoboOk;
    }

    if (object->childCount > 0u && object->contextOwned == RoboLayoutFcIdNone) {
        return LayoutInlinePlaceTransparent(walk, flow, object);
    }
    return LayoutInlinePlaceLeaf(walk, flow, id);
}

/* ------------------------------------------------------------------ */
/* The union a transparent box is                                          */
/* ------------------------------------------------------------------ */

/**
 * Whether @p object's rectangle is a union of contents rather than a placed size.
 *
 * The same three facts `LayoutInlineObject` dispatches on, stated once so that the
 * placement pass and the union walk cannot disagree about which boxes have contents:
 * inline-level (so it is a participant rather than something the block pass owns),
 * established no context of its own (so it is transparent rather than atomic -- an
 * `inline-block`'s contents are laid out *inside* it and it is sized before it is
 * placed), and has children at all.
 */
static int LayoutInlineHasContents(const LayoutObject *object)
{
    return CssDisplayIsInlineLevel(object->display) &&
           object->contextOwned == RoboLayoutFcIdNone && object->childCount > 0u;
}

/**
 * Write every transparent box's rectangle as the union of what its contents reached.
 *
 * **Post-order**, and both halves of that are load-bearing. Bottom-up, because an
 * enclosing union is made of *final* rectangles: computing a parent from its
 * children's provisional ones would union a set of locations that are about to move,
 * and the parent would be wrong by however much the deepest member shifted. In the
 * owner's space, because nothing has been rebased yet -- a position is absolute while
 * the context runs and becomes relative to its containing block at the end
 * (`LayoutInlineRebase`, which this walk feeds).
 *
 * It unions **direct children** rather than descending to the leaves, and that is an
 * equality rather than a shortcut: a child's own rectangle already encloses what it
 * reached, so the union over children *is* the union over contents. An atomic inline's
 * children are not visited at all -- `has_contents` excludes it -- because its
 * rectangle is a size it was given and its contents were laid out inside it by the
 * block pass in its own space.
 *
 * A box that placed nothing keeps the provisional rectangle the placement pass wrote
 * for it (zero size at the cursor), which is why this walk does not have to know where
 * the cursor was: it only ever *replaces* an answer, never invents one.
 */
static void LayoutInlineUnion(LayoutWalk *walk, LayoutNodeId id)
{
    LayoutObject *object = LayoutObjectSlot(walk->tree, id);
    LayoutNodeId child;
    int have = 0;
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;

    if (!object || !LayoutResultIsComputed(&object->result)) {
        return;
    }
    if (!LayoutInlineHasContents(object)) {
        return;
    }

    /* Bottom-up: settle everything inside this box first. */
    child = object->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree,
                                                                                child);

        if (!childObject) {
            break;
        }
        LayoutInlineUnion(walk, child);
        child = childObject->NextSibling;
    }

    /* Then this box's own rectangle, from children whose rectangles are now final. */
    child = object->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree,
                                                                                child);
        int32_t x;
        int32_t y;
        int32_t childRight;
        int32_t childBottom;

        if (!childObject) {
            break;
        }
        /* A declined child -- a block-level box in an inline context -- is UNRESOLVED
         * and has no rectangle to union. It is counted by the placement pass rather
         * than by this walk (RoboLayoutInline.h §8). */
        if (LayoutResultIsComputed(&childObject->result)) {
            x = childObject->result.position.x;
            y = childObject->result.position.y;
            childRight = x + childObject->result.size.width;
            childBottom = y + childObject->result.size.height;
            if (!have) {
                left = x;
                top = y;
                right = childRight;
                bottom = childBottom;
                have = 1;
            } else {
                if (x < left) {
                    left = x;
                }
                if (y < top) {
                    top = y;
                }
                if (childRight > right) {
                    right = childRight;
                }
                if (childBottom > bottom) {
                    bottom = childBottom;
                }
            }
        }
        child = childObject->NextSibling;
    }

    if (have) {
        object->result.position.x = left;
        object->result.position.y = top;
        object->result.size.width = right - left;
        object->result.size.height = bottom - top;
    }
}

/* ------------------------------------------------------------------ */
/* Rebasing nested participants                                        */
/* ------------------------------------------------------------------ */

/**
 * Move a participant's position from the line's space into its parent's.
 *
 * The third step, and the price of writing unions after contents. Every
 * participant is placed in the *owner's* content-box space while the context runs,
 * because that is the space a line lives in; but a box's containing block is its
 * parent (RoboLayoutBlock.h), and for a participant inside a transparent inline
 * box that parent is the inline box — not the context owner. So when the context is
 * complete, each participant that has a parent *within* the context has the
 * parent's own origin subtracted from its position.
 *
 * It is a second walk rather than a running adjustment because an inline box's
 * origin is a union: a later sibling that wraps can move the box's left edge, and a
 * child written before that happened would be left holding a stale offset. Two
 * passes over one context is the honest cost of a bounding box.
 *
 * The recursion stops at an atomic inline: its contents were laid out by the block
 * pass in the box's own space, so they are already stated relative to it. And it
 * stops at anything without geometry — a box this phase declined is UNRESOLVED, and
 * there is nothing to move.
 */
static void LayoutInlineRebase(LayoutWalk *walk,
                                 LayoutNodeId id,
                                 int32_t parentX,
                                 int32_t parentY)
{
    LayoutObject *object = LayoutObjectSlot(walk->tree, id);
    LayoutNodeId child;
    int32_t absoluteX;
    int32_t absoluteY;

    if (!object || !LayoutResultIsComputed(&object->result)) {
        return;
    }

    absoluteX = object->result.position.x;
    absoluteY = object->result.position.y;
    object->result.position.x = absoluteX - parentX;
    object->result.position.y = absoluteY - parentY;

    if (!CssDisplayIsInlineLevel(object->display)) {
        return;
    }
    if (object->contextOwned != RoboLayoutFcIdNone) {
        return;
    }

    child = object->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree,
                                                                                child);

        if (!childObject) {
            break;
        }
        LayoutInlineRebase(walk, child, absoluteX, absoluteY);
        child = childObject->NextSibling;
    }
}

/* ------------------------------------------------------------------ */
/* The context                                                         */
/* ------------------------------------------------------------------ */

void LayoutInlineStatsInit(LayoutInlineStats *stats)
{
    if (!stats) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

int LayoutInlineStatsEqual(const LayoutInlineStats *a,
                                   const LayoutInlineStats *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->contexts == b->contexts && a->emptyContexts == b->emptyContexts &&
           a->participantsPlaced == b->participantsPlaced &&
           a->atomicPlaced == b->atomicPlaced &&
           a->nonAtomicPlaced == b->nonAtomicPlaced &&
           a->blockLevelDeclined == b->blockLevelDeclined && a->lines == b->lines &&
           a->linesFromTextBaseline == b->linesFromTextBaseline &&
           a->linesFromFallbackBaseline == b->linesFromFallbackBaseline &&
           a->lineMembers == b->lineMembers &&
           a->wraps == b->wraps && a->widthsDeclared == b->widthsDeclared &&
           a->widthsFilled == b->widthsFilled &&
           a->heightsDeclared == b->heightsDeclared &&
           a->heightsFromContent == b->heightsFromContent &&
           a->marginsApplied == b->marginsApplied &&
           a->marginsUnresolved == b->marginsUnresolved &&
           a->paddingsApplied == b->paddingsApplied &&
           a->paddingsUnresolved == b->paddingsUnresolved &&
           a->bordersApplied == b->bordersApplied &&
           a->bordersUnresolved == b->bordersUnresolved &&
           a->bordersDrawn == b->bordersDrawn &&
           a->offsetsApplied == b->offsetsApplied &&
           a->offsetsDeclined == b->offsetsDeclined &&
           a->offsetsUnresolved == b->offsetsUnresolved &&
           a->boxesShifted == b->boxesShifted &&
           a->overflowClipped == b->overflowClipped &&
           a->overflowing == b->overflowing &&
           a->overflowUnresolved == b->overflowUnresolved &&
           a->depthMax == b->depthMax;
}

Result LayoutInlineWalkContents(LayoutWalk *walk,
                                               LayoutNodeId owner,
                                               int32_t contentWidth,
                                               GfxPixelY *outExtent)
{
    const LayoutObject *object;
    LayoutInlineFlow flow;
    LayoutNodeId child;
    size_t placedBefore;
    Result status;

    if (!walk || !walk->tree || !walk->inlineFlow || !outExtent) {
        return RoboLayoutDiag(walk ? walk->diag : NULL, RoboLayoutDomainInlineLayout,
                                RoboInvalidArgument, "the inline pass needs a walk and an out");
    }

    object = LayoutObjectSlotConst(walk->tree, owner);
    if (!object) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainInlineLayout, RoboError,
                                "the box that owns an inline context is not in the tree");
    }
    if (contentWidth < 0) {
        contentWidth = 0;
    }

    memset(&flow, 0, sizeof(flow));
    flow.availableWidth = contentWidth;
    /*
     * The two facts every line of this context records about where it came from: the
     * context it belongs to and the box whose contents it holds. The context id is
     * read off the owner -- the classification put it there -- rather than passed in,
     * because the caller of this door is the block pass and it is asking about
     * *contents*, not about context identity. A NONE id is left as it is: a box the
     * classification gave no context can still have its contents walked (the block
     * pass does it for a box that establishes one), and a line with no context is
     * refused by the arena rather than stored with a lie.
     */
    flow.context = object->contextOwned;
    flow.owner = owner;
    flow.line.index = 0u;
    flow.line.firstMember = LAYOUT_LINE_MEMBER_ID_NONE;
    flow.line.lastMember = LAYOUT_LINE_MEMBER_ID_NONE;
    flow.lastLine = RoboLayoutLineIdNone;

    placedBefore = walk->inlineFlow->participantsPlaced;
    walk->inlineFlow->contexts++;

    /*
     * The participants, in sibling order — which the builder made document order,
     * and which is what makes the line's contents ordered the way they are written.
     * The walk descends through transparent inline boxes to the leaves that actually
     * occupy the line, so a `span`'s children are reached here and not by a pass of
     * their own.
     */
    child = object->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree,
                                                                                child);

        if (!childObject) {
            return RoboLayoutDiag(walk->diag, RoboLayoutDomainInlineLayout, RoboError,
                                    "a box's child link names no box in this tree");
        }
        status = LayoutInlineObject(walk, &flow, child);
        if (status != RoboOk) {
            return status;
        }
        child = childObject->NextSibling;
    }

    /*
     * The last line is finalized here, and it is the *only* place a context ends a
     * line: every other line ended at a wrap. Nothing fragments a line in this build,
     * so a context's last line is simply the one still open when its contents run out
     * -- and a context that placed nothing finalizes nothing, because `finalize` is a
     * no-op for an empty line.
     */
    status = LayoutInlineFinalizeLine(walk, &flow);
    if (status != RoboOk) {
        return status;
    }

    walk->inlineFlow->lines += flow.lines;
    walk->inlineFlow->lineMembers += flow.membersTotal;
    if (walk->inlineFlow->participantsPlaced == placedBefore) {
        walk->inlineFlow->emptyContexts++;
    }

    /*
     * Then the two walks that can only happen now, in this order because the second
     * depends on the first.
     *
     * The **union** first: every transparent box's rectangle is the union of what its
     * contents reached, and the contents' positions became final in the finalization
     * above. Doing it here rather than at each box's close is what 10.7 changed and
     * RoboLayoutInline.c's flow note argues; what has *not* changed is the rule -- a
     * box whose contents all landed on a later line is still the bounding box of what
     * they reached, and a box that placed nothing is still zero-size at the cursor.
     */
    child = object->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree,
                                                                                child);

        if (!childObject) {
            break;
        }
        LayoutInlineUnion(walk, child);
        child = childObject->NextSibling;
    }

    /*
     * And then the rebase, over the same participants. The owner's own children are
     * placed relative to the owner, so they are rebased by nothing; a participant
     * further in is rebased by the transparent box it turned out to be inside.
     */
    child = object->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree,
                                                                                child);

        if (!childObject) {
            break;
        }
        LayoutInlineRebase(walk, child, 0, 0);
        child = childObject->NextSibling;
    }

    /*
     * The extent the block pass needs, and since 10.6 it is read from the **last
     * finalized line** rather than from the open line that is about to die with this
     * stack frame: the block extent of an inline formatting context *is* the bottom
     * edge of its last line, and stating it as such is what makes the record the
     * authority rather than a parallel copy of it. Zero when no line was produced,
     * which is the answer for a container with nothing on a line and is what keeps
     * `<p></p>` the height 10.3 gave it.
     */
    *outExtent = 0;
    if (flow.lastLine != RoboLayoutLineIdNone) {
        const LayoutLineSlot *last = LayoutLineSlotConst(walk->tree,
                                                                    flow.lastLine);

        if (last) {
            *outExtent = (GfxPixelY)(last->box.origin.y + last->box.size.height);
        }
    }
    return RoboOk;
}
