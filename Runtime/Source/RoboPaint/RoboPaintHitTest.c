/*
 * RoboPaint hit test (Phase 11.10) — see RoboPaintHitTest.h.
 *
 * This file is the **inverse walk**. The paint traversal turns a laid-out tree into a sequence
 * of drawings in paint order; this one takes a point on the drawing plane and walks the same
 * tree the other way round to find the node that would be drawn there.
 *
 * Everything it concludes comes from a door some other layer already owns, and that is the
 * property worth stating first because it is what makes the two walks agree rather than merely
 * resemble each other:
 *
 *   the rectangles      `PaintBoxesOf`, the same door the painter's markers and
 *                       decorations come from, plus `PaintBoxesTranslate` for the page
 *                       accumulation -- so "where is this box" has one answer in this module;
 *   the boundary        `LayoutObjectOverflow` for the modes and
 *                       `PaintClipIntersect` for the accumulation, exactly as
 *                       `PaintChildClip` reads them;
 *   the strength        `PaintElementOpacity` and `PaintOpacityMultiply`, which
 *                       are shared with the painter through `RoboPaintInternal.h` rather than
 *                       copied, so a change to what an element's `opacity` means cannot reach
 *                       one walk and not the other;
 *   the region          `LayoutOverflowEdgesRegion`, the same door a parent folds a
 *                       child's reach in through, so "how far can this subtree reach" is
 *                       RoboLayout's conclusion and not a second one computed here.
 *
 * The one thing this file decides for itself is the **order**, and the order is the reverse of
 * the painter's:
 *
 *   a box's children are examined before the box itself, because the painter draws the box's
 *   own decorations first and its subtree over them;
 *   the children are examined last-to-first, because the painter draws them first-to-last;
 *   and the box's contents are examined under the boundary its own `overflow` puts in force
 *   while the box itself is examined under the boundary it was entered with, because that is
 *   where 11.8's clip scope stands in the sequence.
 *
 * Get any of those backwards and the answer is the node *underneath* the one the user is
 * pointing at, which is the classic hit-test bug -- which is why the suite states each of them
 * as its own case rather than relying on one overlapping fixture to catch all three.
 *
 * **No arithmetic of its own beyond the subtraction of an origin**, and no second model of
 * anything: this file is a walk, a record and a trace.
 */

#include "RoboPaint/RoboPaintHitTest.h"

#include "RoboHTML/RoboHTMLNode.h"

#include "RoboLayout/RoboLayoutOverflow.h"

#include "RoboPaint/RoboPaintOperation.h"

#include "RoboPaintInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The outcome vocabulary                                              */
/* ------------------------------------------------------------------ */

static const char *const hitOutcomeNames[RoboPaintHitTestOutcomeCount] = {
    "none",
    "node",
    "miss",
    "stale"
};

int PaintHitTestOutcomeIsKnown(PaintHitTestOutcome outcome)
{
    return (int)outcome >= 0 && (size_t)outcome < (size_t)RoboPaintHitTestOutcomeCount;
}

const char *PaintHitTestOutcomeName(PaintHitTestOutcome outcome)
{
    if (!PaintHitTestOutcomeIsKnown(outcome)) {
        return "unknown";
    }
    return hitOutcomeNames[(size_t)outcome];
}

/* ------------------------------------------------------------------ */
/* The answer                                                          */
/* ------------------------------------------------------------------ */

void PaintHitTestResultInit(PaintHitTestResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
    result->outcome = RoboPaintHitNone;
    result->node = RoboLayoutNodeIdNone;
    HtmlNodeHandleClear(&result->domNode);
    result->kind = RoboLayoutObjectNone;
    /* The two states a document starts in rather than zeroed cells: a cleared result must be
     * *readable* -- "no boundary is in force" and "full strength" are the answers that draw
     * everything -- and a zeroed clip record would claim every coordinate is outside a
     * boundary of `[0, 0)`, which is the opposite of what a cleared record means. */
    PaintBoxesInit(&result->boxes);
    PaintClipInit(&result->clip);
    PaintOpacityInit(&result->opacity);
}

int PaintHitTestResultIsValid(const PaintHitTestResult *result)
{
    if (!result) {
        return 0;
    }
    if (!PaintHitTestOutcomeIsKnown(result->outcome)) {
        return 0;
    }
    if ((result->flags & ~(uint32_t)RoboPaintHitFlagAll) != 0u) {
        return 0;
    }
    if (!PaintBoxesIsValid(&result->boxes) ||
        !PaintClipIsValid(&result->clip) ||
        !PaintOpacityIsValid(&result->opacity)) {
        return 0;
    }
    if (result->outcome == RoboPaintHitOnNode) {
        /*
         * A hit has to be able to answer "which node": an id every other door in this module
         * accepts, a kind, and a DOM handle. All three, because a caller dispatching on the id
         * and a caller dispatching on the handle must not be able to disagree about whether
         * anything was hit, and a kind of `NONE` would say the answer is an object of no sort.
         */
        if (result->node == RoboLayoutNodeIdNone) {
            return 0;
        }
        if (result->kind != RoboLayoutObjectBox &&
            result->kind != RoboLayoutObjectText) {
            return 0;
        }
        if (!HtmlNodeHandleIsWellformed(&result->domNode)) {
            return 0;
        }
        return 1;
    }
    /*
     * Everything else is the *absence* of an answer, and the record says so rather than
     * carrying a leftover: no node, no kind, no handle, and no flag describing a hit that did
     * not happen. `flags == 0` is the strict half of that, and it is deliberate -- a `MISS`
     * whose `CONTENT` bit was set would be a record a caller could read as "you are inside the
     * content box of nothing".
     */
    if (result->node != RoboLayoutNodeIdNone ||
        result->kind != RoboLayoutObjectNone ||
        HtmlNodeHandleIsWellformed(&result->domNode) ||
        result->flags != 0u) {
        return 0;
    }
    return 1;
}

int PaintHitTestResultIsHit(const PaintHitTestResult *result)
{
    return result && result->outcome == RoboPaintHitOnNode;
}

int PaintHitTestResultHasFlag(const PaintHitTestResult *result, uint32_t flag)
{
    if (!result) {
        return 0;
    }
    return (result->flags & flag) != 0u;
}

PaintHitTestOutcome PaintHitTestResultOutcome(
    const PaintHitTestResult *result)
{
    return result ? result->outcome : RoboPaintHitNone;
}

LayoutNodeId PaintHitTestResultNode(const PaintHitTestResult *result)
{
    return result ? result->node : RoboLayoutNodeIdNone;
}

LayoutObjectKind PaintHitTestResultKind(
    const PaintHitTestResult *result)
{
    return result ? result->kind : RoboLayoutObjectNone;
}

size_t PaintHitTestResultDepth(const PaintHitTestResult *result)
{
    return result ? result->depth : 0u;
}

void PaintHitTestResultPoint(const PaintHitTestResult *result,
                                     LayoutPoint *out)
{
    if (out) {
        *out = result ? result->point : MakeGfxPoint(0, 0);
    }
}

void PaintHitTestResultLocal(const PaintHitTestResult *result,
                                     LayoutPoint *out)
{
    if (out) {
        *out = result ? result->local : MakeGfxPoint(0, 0);
    }
}

void PaintHitTestResultBoxes(const PaintHitTestResult *result,
                                     PaintBoxes *out)
{
    if (!out) {
        return;
    }
    if (!result) {
        PaintBoxesInit(out);
        return;
    }
    *out = result->boxes;
}

void PaintHitTestResultClip(const PaintHitTestResult *result,
                                    PaintClip *out)
{
    if (!out) {
        return;
    }
    if (!result) {
        PaintClipInit(out);
        return;
    }
    *out = result->clip;
}

void PaintHitTestResultOpacity(const PaintHitTestResult *result,
                                       PaintOpacity *out)
{
    if (!out) {
        return;
    }
    if (!result) {
        PaintOpacityInit(out);
        return;
    }
    *out = result->opacity;
}

HtmlNodeHandle PaintHitTestResultDomNode(
    const PaintHitTestResult *result)
{
    HtmlNodeHandle handle;

    HtmlNodeHandleClear(&handle);
    if (result) {
        handle = result->domNode;
    }
    return handle;
}

int PaintHitTestResultEqual(const PaintHitTestResult *a,
                                    const PaintHitTestResult *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->outcome != b->outcome || a->node != b->node || a->kind != b->kind ||
        a->depth != b->depth || a->flags != b->flags) {
        return 0;
    }
    if (a->point.x != b->point.x || a->point.y != b->point.y ||
        a->local.x != b->local.x || a->local.y != b->local.y ||
        a->origin.x != b->origin.x || a->origin.y != b->origin.y) {
        return 0;
    }
    if (!PaintBoxesEqual(&a->boxes, &b->boxes) ||
        !PaintClipEqual(&a->clip, &b->clip) ||
        !PaintOpacityEqual(&a->opacity, &b->opacity)) {
        return 0;
    }
    /* The DOM handle is compared by its three tokens rather than by `memcmp`, because it is a
     * value with a well-formedness rule and two cleared handles are the same handle. */
    if (HtmlNodeHandleIsWellformed(&a->domNode) !=
        HtmlNodeHandleIsWellformed(&b->domNode)) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* What the query saw                                                  */
/* ------------------------------------------------------------------ */

void PaintHitTestStatsInit(PaintHitTestStats *stats)
{
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

/* ------------------------------------------------------------------ */
/* The geometry of a subtree                                           */
/* ------------------------------------------------------------------ */

/**
 * The smallest rectangle containing both, tolerating an empty operand.
 *
 * An empty rectangle is not a point at the origin, it is *nothing* -- `RoboGraphicsGeometry.h`
 * states that a non-positive extent is empty rather than invalid -- so it must not drag the
 * union towards `(0, 0)`. A zero-height box is the ordinary case this protects: its border box
 * contributes its width and no height, and a naive min/max over the corners would make the bound
 * span from the document's origin to the box instead.
 *
 * Both extents are taken in 64 bits and saturated, the rule `RoboPaintInternal.h` states for
 * every accumulation in this module: an operand is bounded and a difference of two is not.
 */
static LayoutRect HitRectUnion(LayoutRect a, LayoutRect b)
{
    LayoutRect out;
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;

    if (GfxRectIsEmpty(a)) {
        return b;
    }
    if (GfxRectIsEmpty(b)) {
        return a;
    }
    left = (int64_t)a.x < (int64_t)b.x ? (int64_t)a.x : (int64_t)b.x;
    top = (int64_t)a.y < (int64_t)b.y ? (int64_t)a.y : (int64_t)b.y;
    right = (int64_t)a.x + a.width > (int64_t)b.x + b.width ? (int64_t)a.x + a.width
                                                           : (int64_t)b.x + b.width;
    bottom = (int64_t)a.y + a.height > (int64_t)b.y + b.height ? (int64_t)a.y + a.height
                                                               : (int64_t)b.y + b.height;
    out.x = PaintClampI64(left);
    out.y = PaintClampI64(top);
    out.width = PaintExtent(right - left);
    out.height = PaintExtent(bottom - top);
    return out;
}

/*
 * The bound, and it is the difference between a query whose cost follows the point and one
 * whose cost follows the document.
 *
 * **What the bound is**: the box's border box, grown to include the region its contents reached
 * (10.13). That is exactly the rectangle every node beneath the box lives inside, which is why
 * it can be tested before anything is descended into.
 *
 * **Why the border box is where it starts, rather than the padding box the region is measured
 * from**: the region a parent folds in is measured from a child's padding box, but a box's own
 * *border* is outside that and is part of the box, so a bound of the region alone would exclude
 * the box's own border band -- and a hit test that could not find a border is a hit test that
 * fails on the one rectangle the painter definitely draws.
 *
 * **Why a refusal is not a gap**: `LayoutObjectOverflow` declines a text run and a
 * transparent inline box with contents, and for both the placement rectangle *is* the union of
 * what they enclose -- a run is a leaf, and a transparent inline box's rectangle is the union of
 * its contents by construction (`RoboLayoutObject.h`). So for those the border box is already
 * the whole bound, and declining is the correct answer rather than a hole in it. The two cases
 * are the same two `insetsMeasured` names on `PaintBoxes`, which is what makes the
 * bound and the painter's record agree about which placements have layers.
 */
static Result HitSubtreeBound(const LayoutTree *tree,
                                       LayoutNodeId id,
                                       const PaintBoxes *boxes,
                                       LayoutRect *out,
                                       PaintDiag *diag)
{
    LayoutOverflowEdges overflow;
    LayoutRect region;

    *out = boxes->border;
    if (LayoutObjectOverflow(tree, id, &overflow, NULL) != RoboOk) {
        return RoboOk;
    }
    if (!LayoutOverflowEdgesHasOverflow(&overflow)) {
        /*
         * Nothing reached past the padding box, and the region of a box with no extents *is* the
         * padding box -- which the border box already contains. So the early answer is the right
         * one and not merely the cheap one, and this is the case every box of an ordinary
         * document takes.
         */
        return RoboOk;
    }
    if (LayoutOverflowEdgesRegion(&overflow, boxes->padding, &region, NULL) != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainHitTest, RoboError,
                               "a box's overflow region could not be derived from its record");
    }
    *out = HitRectUnion(*out, region);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The two states a box passes down                                    */
/* ------------------------------------------------------------------ */

/** The strength @p box's own declaration puts in force for **itself and everything under it**.
 *
 * The painter's arithmetic, read from the same two doors: a box's own `opacity` multiplied into
 * the strength already in force. A box with **no computed style** declares nothing and is opaque,
 * which `PaintElementOpacity` answers rather than this call having to. */
static PaintOpacity HitChildOpacity(const LayoutTree *tree,
                                              LayoutNodeId id,
                                              const PaintOpacity *opacity)
{
    PaintOpacity declared =
        PaintElementOpacity(LayoutObjectStyle(tree, id));

    return PaintOpacityMultiply(opacity, &declared);
}

/** The boundary @p box's `overflow` puts in force for its contents, or @p clip unchanged.
 *
 * `PaintChildClip`'s rule stated once more rather than shared, because the two differ in one
 * thing that matters: the painter counts the box as `clipped` while it is there, and a hit test
 * has no document-level counters of that sort. Everything else -- the overflow door, the
 * "does either axis hide" question and the padding box as the clip edge -- is the same, and the
 * clip edge being the **padding box** is why the box's own border band stays hittable under a
 * boundary its contents are subject to. */
static Result HitChildClip(const LayoutTree *tree,
                                    LayoutNodeId id,
                                    const PaintBoxes *boxes,
                                    const PaintClip *clip,
                                    PaintClip *out,
                                    PaintDiag *diag)
{
    LayoutOverflowEdges overflow;

    *out = *clip;
    if (LayoutObjectOverflow(tree, id, &overflow, NULL) != RoboOk) {
        return RoboOk;
    }
    if (!LayoutOverflowEdgesClipsAny(&overflow)) {
        return RoboOk;
    }
    return PaintClipIntersect(clip, &boxes->padding, overflow.horizontal,
                                    overflow.vertical, out, diag);
}

/* ------------------------------------------------------------------ */
/* The trace                                                           */
/* ------------------------------------------------------------------ */

/*
 * Reasons a line names, in the order the walk reaches them, and each one is a decision the walk
 * makes rather than a status it later reports:
 *
 *   `unplaced`  nothing placed the node, so there is no rectangle and no coordinate to test in.
 *               Its subtree is not walked, because there is no origin for anything inside it
 *               (the same reason the painter emits nothing below such a box).
 *   `region`    the node is placed, and the point is outside the rectangle every node beneath it
 *               lives inside. The subtree is skipped.
 *   `clip`      the point is outside the boundary the node is drawn under, so neither it nor
 *               anything beneath it can be seen there.
 *   `enter`     the node was examined and the walk went on: a box descends into its children, a
 *               text run is simply tested. `kind=` on the same line says which.
 *   `hit`       the node is the answer. Written as its own line after the node's examination, so
 *               that the trace stays in traversal order and the answer is still readable.
 */
#define RoboPaintHitReasonUnplaced "unplaced"
#define RoboPaintHitReasonRegion "region"
#define RoboPaintHitReasonClip "clip"
#define RoboPaintHitReasonEnter "enter"
#define RoboPaintHitReasonHit "hit"

/** The kind column's width: `text` is the longer of the two. */
#define RoboPaintHitKindColumn 4u
/** The reason column's width: `unplaced` is the longest. */
#define RoboPaintHitReasonColumn 8u

/** Append @p text padded with spaces to @p width. */
static void HitWriteColumn(PaintWriter *trace, const char *text, size_t width)
{
    size_t written = 0u;

    while (text[written] != '\0') {
        PaintWriterChar(trace, text[written]);
        written++;
    }
    while (written < width) {
        PaintWriterChar(trace, ' ');
        written++;
    }
}

/** The `box`/`text` name of @p kind; never NULL. */
static const char *HitKindName(LayoutObjectKind kind)
{
    switch (kind) {
    case RoboLayoutObjectBox:
        return "box";
    case RoboLayoutObjectText:
        return "text";
    default:
        break;
    }
    return "none";
}

/** One examined object's line: `node=.. depth=.. kind.. outcome=.. border=.. bound=..`. */
static void HitTraceNode(PaintWriter *trace,
                          LayoutNodeId id,
                          size_t depth,
                          LayoutObjectKind kind,
                          const char *reason,
                          LayoutRect border,
                          LayoutRect bound)
{
    PaintWriterString(trace, "node=");
    PaintWriterU64(trace, (uint64_t)id);
    PaintWriterString(trace, " depth=");
    PaintWriterU64(trace, (uint64_t)depth);
    PaintWriterChar(trace, ' ');
    HitWriteColumn(trace, HitKindName(kind), RoboPaintHitKindColumn);
    PaintWriterString(trace, " outcome=");
    HitWriteColumn(trace, reason, RoboPaintHitReasonColumn);
    PaintWriterString(trace, " border=");
    PaintWriterRect(trace, border);
    PaintWriterString(trace, " bound=");
    PaintWriterRect(trace, bound);
    PaintWriterChar(trace, '\n');
}

/** The answer's own line, written after the node that produced it was examined. */
static void HitTraceHit(PaintWriter *trace,
                         LayoutNodeId id,
                         size_t depth,
                         LayoutPoint point)
{
    PaintWriterString(trace, RoboPaintHitReasonHit);
    PaintWriterString(trace, " node=");
    PaintWriterU64(trace, (uint64_t)id);
    PaintWriterString(trace, " depth=");
    PaintWriterU64(trace, (uint64_t)depth);
    PaintWriterString(trace, " point=");
    PaintWriterI64(trace, (int64_t)point.x);
    PaintWriterChar(trace, ',');
    PaintWriterI64(trace, (int64_t)point.y);
    PaintWriterChar(trace, '\n');
}

/* ------------------------------------------------------------------ */
/* Recording an answer                                                 */
/* ------------------------------------------------------------------ */

/**
 * Record @p id as the answer about @p point.
 *
 * The four facts that have to be copied in -- and each of them comes from the call that already
 * had to work it out rather than being re-derived here:
 *
 *   **the identity**: the layout object's id, and the DOM node it was built from. Both travel
 *   because the layers above consume different ones, and asking for the handle at the point of
 *   use would mean a walk into the tree at every use (RoboPaintHitTest.h);
 *
 *   **the two spaces**: the point as the query gave it, and the same point in the node's own
 *   containing-block space. The subtraction is saturating, so a node at the coordinate range's
 *   edge produces a *readable* local point rather than a wrapped one;
 *
 *   **the geometry**: the same three rectangles the painter read, in paint coordinates;
 *
 *   **the two states in force**: the boundary and the strength. @p opacity here is the strength
 *   **on this node's own drawing** -- the enclosing strength for a text run, whose owning element
 *   already opened its scope, and the box's own declaration multiplied in for a box, whose
 *   decorations are painted at its own strength. That is the painter's rule read at the same two
 *   places it applies it;
 *
 * plus the three flags, two of which are questions that are *derivable* from what is above and
 * one of which is not: `TEXT` is the kind, `CONTENT` is `boxes.content` containing the point, and
 * `TRANSPARENT` is the strength in force having come to zero -- which no rectangle can answer and
 * which is why it is a flag rather than something a caller computes. The derivable pair is
 * asserted against its derivation in the suite rather than trusted here.
 */
static void HitRecord(PaintHitTestResult *result,
                       const LayoutTree *tree,
                       LayoutNodeId id,
                       size_t depth,
                       LayoutObjectKind kind,
                       LayoutPoint point,
                       LayoutPoint origin,
                       const PaintBoxes *boxes,
                       const PaintClip *clip,
                       const PaintOpacity *opacity)
{
    HtmlNodeHandle handle;

    HtmlNodeHandleClear(&handle);
    (void)LayoutObjectDomNode(tree, id, &handle);

    result->outcome = RoboPaintHitOnNode;
    result->node = id;
    result->domNode = handle;
    result->kind = kind;
    result->depth = depth;
    result->point = point;
    result->local = MakeGfxPoint(PaintSubSaturated(point.x, origin.x),
                                   PaintSubSaturated(point.y, origin.y));
    result->origin = origin;
    result->boxes = *boxes;
    result->clip = *clip;
    result->opacity = *opacity;
    result->flags = 0u;
    if (kind == RoboLayoutObjectText) {
        result->flags |= RoboPaintHitFlagText;
    }
    if (GfxRectContainsPoint(boxes->content, point)) {
        result->flags |= RoboPaintHitFlagContent;
    }
    if (PaintOpacityIsTransparent(opacity)) {
        result->flags |= RoboPaintHitFlagTransparent;
    }
}

/* ------------------------------------------------------------------ */
/* The walk                                                            */
/* ------------------------------------------------------------------ */

/*
 * The walk, and its shape is the chapter this file is about. Read it as four questions asked in
 * order, and the order is the answer to "which node does the user see":
 *
 *   1. **is the point anywhere near this node?** -- the subtree bound. Cheapest and most
 *      decisive: a point outside the rectangle a node's whole subtree lives in cannot be on
 *      anything in it, so the subtree costs one comparison. This is also what keeps a query
 *      proportional to the boxes the point is near rather than to the document, and what makes
 *      the region 10.13 measured worth having a consumer at last.
 *
 *   2. **is the point inside the boundary the node is drawn under?** -- the clip in force. A
 *      `hidden` axis is a boundary the drawing does not cross, so a point beyond one is not on
 *      this node *or* anything beneath it.
 *
 *   3. **what is on top of this node?** -- its children, last first, under the boundary and the
 *      strength this box's own declarations put in force for its contents. Descendants are drawn
 *      over their ancestor's own decorations, so they are asked first; and the *last* child is
 *      drawn over the earlier ones, so it is asked first as well. The point in `childOrigin` is
 *      the one coordinate the painter computes here too: the child's geometry is relative to
 *      this box's content origin.
 *
 *   4. **is the node itself there?** -- only after its subtree has declined to answer, and under
 *      the boundary the node was *entered* with rather than the one its contents are under. That
 *      asymmetry is what keeps a clipped ancestor's own border hittable.
 *
 * The recursion is a value walk, not a stack: the origin, the boundary and the strength are the
 * call's parameters, because "what is in force here" is what this call concluded on the way down
 * and a shared mutable stack would be a second mechanism for one fact -- the same argument
 * `RoboPaintClip.h` makes for the painter.
 *
 * @param placedAncestor  non-zero when @p origin is a real containing-block content origin --
 *                         that is, when every ancestor was a placement. A node reached under one
 *                         that was not has no coordinates to be tested in, and the painter
 *                         treats it the same way.
 * @param trace            optional; when given, every examined object appends one line. The
 *                         public query passes NULL, so this costs nothing outside the dump.
 */
static Result HitWalk(const LayoutTree *tree,
                              LayoutNodeId id,
                              size_t depth,
                              int placedAncestor,
                              LayoutPoint origin,
                              const PaintClip *clip,
                              const PaintOpacity *opacity,
                              LayoutPoint point,
                              PaintHitTestResult *result,
                              PaintHitTestStats *stats,
                              PaintWriter *trace,
                              PaintDiag *diag)
{
    LayoutObjectKind kind = MakeLayoutObjectKind(tree, id);
    LayoutNodeId child;
    PaintBoxes local;
    PaintBoxes boxes;
    LayoutRect bound;
    LayoutPoint childOrigin;
    PaintClip childClip;
    PaintOpacity childOpacity;

    if (!LayoutObjectKindIsKnown(kind) || kind == RoboLayoutObjectNone) {
        /* A child link that names nothing: reported rather than skipped, because a query that
         * answered "no node" for a document it could not walk is a query nobody can act on. */
        return RoboPaintDiag(diag, RoboPaintDomainHitTest, RoboError,
                               "a child link names no object in this tree");
    }

    stats->objects++;
    if (depth > stats->depthMax) {
        stats->depthMax = (uint32_t)depth;
    }

    /* The placement, through the door the painter's markers and runs come from -- so a node this
     * walk tests is a node the painter draws, and a node it declines is one the painter declines.
     * A **text run** and a **transparent inline box with contents** are placements whose inset
     * records decline and whose three rectangles are therefore one rectangle: `insetsMeasured`
     * on the record says which, and nothing here has to know. */
    if (!placedAncestor || PaintBoxesOf(tree, id, &local, NULL) != RoboOk) {
        /*
         * Nothing placed this object. Counted and **not descended into**: every node below it
         * would be reached with no origin to be tested in, so the walk would examine rectangles
         * that have no position on the page -- which is the same reason the painter emits nothing
         * below such a box. `unplaced` is therefore one object rather than a subtree, and
         * `PaintHitTestStats` says so where a reader will look for the difference
         * between it and the tree's object count.
         */
        stats->unplaced++;
        if (trace) {
            /* No placement means no rectangle, and the line says so with two empty ones rather
             * than with a rectangle derived from a result that does not exist. */
            LayoutRect nothing = MakeGfxRect(0, 0, 0, 0);

            HitTraceNode(trace, id, depth, kind, RoboPaintHitReasonUnplaced, nothing,
                           nothing);
        }
        return RoboOk;
    }

    if (kind == RoboLayoutObjectText) {
        stats->texts++;
    } else {
        stats->boxes++;
    }

    /* Into one page space: the accumulated origin added to what layout wrote. */
    (void)PaintBoxesTranslate(&local, origin.x, origin.y, &boxes, NULL);

    if (kind == RoboLayoutObjectText) {
        /*
         * A run is a leaf: no children, no box layers, no boundary of its own -- so the walk
         * tests exactly one rectangle and returns. Its bound is its own placement, which is also
         * its answer to "how far can this subtree reach".
         */
        if (trace) {
            HitTraceNode(trace, id, depth, kind, RoboPaintHitReasonEnter, boxes.border,
                           boxes.border);
        }
        if (!PaintClipContains(clip, point)) {
            stats->rejectedClip++;
            return RoboOk;
        }
        if (!GfxRectContainsPoint(boxes.border, point)) {
            return RoboOk;
        }
        HitRecord(result, tree, id, depth, kind, point, origin, &boxes, clip, opacity);
        stats->hits++;
        if (trace) {
            HitTraceHit(trace, id, depth, point);
        }
        return RoboOk;
    }

    /*
     * The subtree bound, and the one early-out that makes a query follow the point rather than
     * the document. It is asked **before** the boundary, because it is a question about geometry
     * -- which node could possibly be here -- where the boundary is a question about drawing, and
     * a reader of the trace wants the geometric reason first.
     */
    if (HitSubtreeBound(tree, id, &boxes, &bound, diag) != RoboOk) {
        return diag ? diag->code : RoboError;
    }
    if (!GfxRectContainsPoint(bound, point)) {
        stats->rejectedRegion++;
        if (trace) {
            HitTraceNode(trace, id, depth, kind, RoboPaintHitReasonRegion, boxes.border,
                           bound);
        }
        return RoboOk;
    }

    /* The box's own painting is bounded by the boundary it was **entered** with, not by the one
     * its contents get -- 11.8's arrangement, and the reason a `overflow: hidden` box's own
     * border band is still hittable. */
    if (!PaintClipContains(clip, point)) {
        stats->rejectedClip++;
        if (trace) {
            HitTraceNode(trace, id, depth, kind, RoboPaintHitReasonClip, boxes.border, bound);
        }
        return RoboOk;
    }

    if (trace) {
        HitTraceNode(trace, id, depth, kind, RoboPaintHitReasonEnter, boxes.border, bound);
    }

    childOpacity = HitChildOpacity(tree, id, opacity);
    if (HitChildClip(tree, id, &boxes, clip, &childClip, diag) != RoboOk) {
        return diag ? diag->code : RoboError;
    }

    /*
     * The children, **last first**, because the painter draws them first-to-last and the node on
     * top is the one that answers. Each is entered with this box's content origin and with the
     * states this box's own declarations put in force for its contents -- which is where the clip
     * and the box's own region deliberately part company.
     *
     * A child outside the contents' boundary is not skipped here: it is entered and answers
     * `clip` itself, so the reason reaches the trace and the counter with the node it belongs to
     * rather than being folded into the parent's line.
     */
    childOrigin = MakeGfxPoint(boxes.content.x, boxes.content.y);
    child = RoboLayoutNodeIdNone;
    if (LayoutObjectLastChild(tree, id, &child) == RoboOk) {
        while (child != RoboLayoutNodeIdNone) {
            Result status = HitWalk(tree, child, depth + 1u, 1, childOrigin, &childClip,
                                            &childOpacity, point, result, stats, trace, diag);

            if (status != RoboOk) {
                return status;
            }
            if (result->outcome == RoboPaintHitOnNode) {
                return RoboOk;
            }
            if (LayoutObjectPreviousSibling(tree, child, &child) != RoboOk) {
                break;
            }
        }
    }

    /*
     * And only now the box itself: its own region is its border box, under the boundary it was
     * entered with, and at the strength its own `opacity` declaration produced -- which is what
     * the painter paints its decorations at.
     *
     * A box that draws nothing is still an answer, and that is CSS's rule rather than a
     * convenience: a transparent `div` receives a point over its whole border box, because
     * painting nothing is not the same as being absent. The alternative -- testing only boxes
     * with a visible background or border -- would need `pointer-events` machinery this build
     * does not have and would be wrong about a document that has none.
     */
    if (!GfxRectContainsPoint(boxes.border, point)) {
        return RoboOk;
    }
    HitRecord(result, tree, id, depth, kind, point, origin, &boxes, clip, &childOpacity);
    stats->hits++;
    if (trace) {
        HitTraceHit(trace, id, depth, point);
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The query                                                           */
/* ------------------------------------------------------------------ */

/**
 * The walk's prologue: the states a document starts in, the empty-tree case and the refusal.
 *
 * Split from the trace door below because the two share all of it and differ only in the sink --
 * and a second copy of the refusal is the one place a stale tree could be answered two ways.
 */
static Result HitRun(const LayoutTree *tree,
                             LayoutPoint point,
                             PaintHitTestResult *out,
                             PaintHitTestStats *outStats,
                             PaintWriter *trace,
                             PaintDiag *diag)
{
    PaintHitTestStats stats;
    LayoutNodeId root;
    PaintClip clip;
    PaintOpacity opacity;
    Result status;

    PaintHitTestStatsInit(&stats);
    PaintHitTestResultInit(out);
    out->point = point;

    /*
     * A tree whose geometry is not final is **refused** rather than answered, and the refusal is
     * an outcome rather than an error for the reason the paint pass gives: the cure is to run a
     * layout pass, and a caller that could not tell "your tree is stale" from "your tree is
     * broken" would go looking for a bug in the document.
     *
     * It is a refusal rather than a best-effort walk because the two mistakes are not symmetric.
     * A hit test against half-computed geometry is not a less accurate answer, it is a wrong one:
     * it names whichever node happens to hold a stale rectangle, and a caller about to dispatch a
     * click on that answer would be delivering it to a node the user is not pointing at.
     */
    if (LayoutTreeNeedsLayout(tree)) {
        out->outcome = RoboPaintHitStale;
        if (trace) {
            /* One line naming the refusal: nothing was examined, so there is no node to name and
             * the reason column is what a reader needs. */
            LayoutRect nothing = MakeGfxRect(0, 0, 0, 0);

            HitTraceNode(trace, RoboLayoutNodeIdNone, 0u, RoboLayoutObjectNone, "stale",
                           nothing, nothing);
        }
        if (outStats) {
            *outStats = stats;
        }
        return RoboOk;
    }

    root = LayoutTreeRoot(tree);
    if (root == RoboLayoutNodeIdNone) {
        /* No root box: a document that builds no boxes, or one whose root generates none. A miss
         * with nothing examined, which is what the counters already say rather than a special
         * case a caller has to test for. */
        out->outcome = RoboPaintHitMiss;
        if (outStats) {
            *outStats = stats;
        }
        return RoboOk;
    }

    /*
     * The root is entered under the two states a document starts in: a boundary that hides
     * neither axis, and full strength. Neither is a scope -- both are what an empty stack
     * answers -- so they are stated here as the initial values rather than pushed, exactly as the
     * paint pass states them, and the strength comes from the opacity module's own door rather
     * than from a written `1.0` for the same reason it does there.
     */
    clip = PaintClipNone();
    opacity = PaintOpacityOpaque();
    status = HitWalk(tree, root, 0u, 1, MakeGfxPoint(0, 0), &clip, &opacity, point, out, &stats,
                      trace, diag);
    if (status != RoboOk) {
        /* A broken tree: the answer is not "no node", it is "the question could not be asked", so
         * the result is returned to its cleared state rather than left half-filled. */
        PaintHitTestResultInit(out);
        out->point = point;
        if (outStats) {
            *outStats = stats;
        }
        return status;
    }
    if (out->outcome == RoboPaintHitNone) {
        out->outcome = RoboPaintHitMiss;
    }
    if (outStats) {
        *outStats = stats;
    }
    return RoboOk;
}

Result PaintHitTest(const LayoutTree *tree,
                                 LayoutPoint point,
                                 PaintHitTestResult *out,
                                 PaintHitTestStats *outStats,
                                 PaintDiag *diag)
{
    if (!tree || !out) {
        return RoboPaintDiag(diag, RoboPaintDomainHitTest, RoboInvalidArgument,
                               "a tree and a result are both required");
    }
    return HitRun(tree, point, out, outStats, NULL, diag);
}

/* ------------------------------------------------------------------ */
/* The dump                                                            */
/* ------------------------------------------------------------------ */

Result PaintHitTestWrite(const LayoutTree *tree,
                                       LayoutPoint point,
                                       char *buffer,
                                       size_t capacity,
                                       size_t *outLength,
                                       PaintDiag *diag)
{
    PaintHitTestResult answer;
    PaintHitTestResult traced;
    PaintWriter head;
    PaintWriter trace;
    Result status;
    size_t headLength;

    if (!tree || !buffer || !outLength) {
        return RoboPaintDiag(diag, RoboPaintDomainHitTest, RoboInvalidArgument,
                               "a tree, a buffer and a length are all required");
    }

    /*
     * The answer's own line goes **first**, because it is what a reader came for, and the trace
     * follows in the order the walk examined the objects -- so a wrong answer is read by finding
     * the line that should have been the `hit` one and seeing which reason passed it over.
     *
     * That ordering is why the walk runs **twice**, and it is not a shortcut avoided: the header
     * names the outcome, so it cannot be written until the answer is known, and the trace has to
     * follow it. Running the walk twice is sound because the walk is a **pure function of the
     * tree and the point** -- the same property `PaintHitTest` itself relies on when a
     * caller runs it once per input event -- so the second pass produces exactly the result the
     * first one did, which is the fact `paint.hit-test.dump` pins by dumping one tree twice.
     */
    PaintHitTestResultInit(&answer);
    status = HitRun(tree, point, &answer, NULL, NULL, diag);
    if (status != RoboOk) {
        /* A broken tree: the refusal is reported rather than turned into a trace of a walk that
         * did not happen. */
        *outLength = 0u;
        buffer[0] = '\0';
        return status;
    }

    PaintWriterInit(&head, buffer, capacity);
    PaintWriterString(&head, "hit-test point=");
    PaintWriterI64(&head, (int64_t)point.x);
    PaintWriterChar(&head, ',');
    PaintWriterI64(&head, (int64_t)point.y);
    PaintWriterString(&head, " outcome=");
    PaintWriterString(&head, PaintHitTestOutcomeName(answer.outcome));
    if (answer.outcome == RoboPaintHitOnNode) {
        PaintWriterString(&head, " node=");
        PaintWriterU64(&head, (uint64_t)answer.node);
        PaintWriterString(&head, " depth=");
        PaintWriterU64(&head, (uint64_t)answer.depth);
    }
    PaintWriterChar(&head, '\n');
    headLength = head.length;

    /*
     * The trace, into whatever room is left. When the header alone did not fit there is no room
     * at all, so the second walk writes into a counting writer -- NULL buffer, zero capacity --
     * purely so that `outLength` can report what the caller needs, which is the contract every
     * `_write` door in this tree uses: a caller sizes a buffer from the refusal rather than from
     * an estimate.
     */
    PaintHitTestResultInit(&traced);
    if (headLength < capacity) {
        PaintWriterInit(&trace, buffer + headLength, capacity - headLength);
    } else {
        PaintWriterInit(&trace, NULL, 0u);
    }
    status = HitRun(tree, point, &traced, NULL, &trace, diag);
    if (status != RoboOk) {
        *outLength = 0u;
        buffer[0] = '\0';
        return status;
    }

    *outLength = headLength + trace.length;
    if (!PaintWriterFits(&head) || !PaintWriterFits(&trace)) {
        return RoboNotSupported;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Version note                                                        */
/* ------------------------------------------------------------------ */

/*
 * Both the query and the dump are compilable against a **stale tree**: the refusal is a value,
 * and `HitRun` is the single place it is decided -- so the query's two entry points cannot
 * disagree about whether a tree was walkable, which is the failure a second copy of the check
 * would eventually produce. That is why the dump does not run a walk of its own and why the
 * trace is a sink rather than a second traversal: there is one order, stated once.
 */
