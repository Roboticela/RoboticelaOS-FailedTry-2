/*
 * RoboPaint geometry (Phase 11.1) — see RoboPaintGeometry.h.
 *
 * The three boxes and the boundary, and the whole of this module's arithmetic lives
 * here: two derivations of nested rectangles, one translation, and one per-axis
 * intersection. Everything else in the module either reads a record or records one.
 */

#include "RoboPaint/RoboPaintGeometry.h"

#include "RoboPaintInternal.h"

/* ------------------------------------------------------------------ */
/* The two inset records, as arrays                                        */
/* ------------------------------------------------------------------ */

/*
 * Both layers are read through RoboLayout's own width doors rather than by reaching for
 * `edges->top.width`, and the reason is the one `RoboLayoutBorder.h` gives for the door
 * existing: "which field of the used record is the width" is asked once here instead of
 * at every site that does arithmetic with it. Eight arithmetic sites would be eight
 * chances to read a style by mistake.
 */

/** The four sides, in the order the arrays below are indexed. */
static const CssBoxSide paintSides[RoboCssBoxSideCount] = {
    RoboCssBoxSideTop,
    RoboCssBoxSideRight,
    RoboCssBoxSideBottom,
    RoboCssBoxSideLeft
};

Result PaintBorderWidths(const LayoutBorderEdges *borders,
                                       int32_t outWidths[RoboCssBoxSideCount],
                                       PaintDiag *diag)
{
    size_t index;

    if (!borders || !outWidths) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboInvalidArgument,
                               "borders and the width array are required");
    }
    if (!LayoutBorderEdgesIsValid(borders)) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboError,
                               "the border record is not one this build produced");
    }
    for (index = 0u; index < (size_t)RoboCssBoxSideCount; index++) {
        GfxPixelX width = 0;

        if (LayoutBorderEdgesWidth(borders, paintSides[index], &width) != RoboOk) {
            return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboError,
                                   "a border side's used width could not be read");
        }
        outWidths[index] = (int32_t)width;
    }
    return RoboOk;
}

Result PaintPaddingWidths(const LayoutPaddingEdges *padding,
                                        int32_t outWidths[RoboCssBoxSideCount],
                                        PaintDiag *diag)
{
    if (!padding || !outWidths) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboInvalidArgument,
                               "padding and the width array are required");
    }
    if (!LayoutPaddingEdgesIsValid(padding)) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboError,
                               "the padding record is not one this build produced");
    }
    /*
     * The padding layer has no per-side reader: its four fields are the four sides in
     * the same `top, right, bottom, left` order that `CssBoxSide` uses, so the
     * array is filled in that order directly -- and the order is asserted once, by the
     * suite, against a record it built from a real stylesheet.
     */
    outWidths[RoboCssBoxSideTop] = (int32_t)padding->top;
    outWidths[RoboCssBoxSideRight] = (int32_t)padding->right;
    outWidths[RoboCssBoxSideBottom] = (int32_t)padding->bottom;
    outWidths[RoboCssBoxSideLeft] = (int32_t)padding->left;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The three boxes                                                     */
/* ------------------------------------------------------------------ */

void PaintBoxesInit(PaintBoxes *boxes)
{
    if (!boxes) {
        return;
    }
    boxes->border = GfxRectEmpty();
    boxes->padding = GfxRectEmpty();
    boxes->content = GfxRectEmpty();
    boxes->insetsMeasured = 0;
}

int PaintBoxesIsValid(const PaintBoxes *boxes)
{
    if (!boxes) {
        return 0;
    }
    if (!GfxRectIsValid(boxes->border) || !GfxRectIsValid(boxes->padding) ||
        !GfxRectIsValid(boxes->content)) {
        return 0;
    }
    if (!GfxRectEncloses(boxes->border, boxes->padding) ||
        !GfxRectEncloses(boxes->padding, boxes->content)) {
        return 0;
    }
    /*
     * The flag and the geometry have to agree: a box whose insets were not measured has
     * one rectangle wearing three names, and a record that claimed otherwise would be
     * telling a consumer it may draw a border band that is empty because nobody computed
     * one.
     */
    if (!boxes->insetsMeasured &&
        !(GfxRectEqual(boxes->border, boxes->padding) &&
          GfxRectEqual(boxes->padding, boxes->content))) {
        return 0;
    }
    return 1;
}

int PaintBoxesEqual(const PaintBoxes *a, const PaintBoxes *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->insetsMeasured == b->insetsMeasured &&
           GfxRectEqual(a->border, b->border) &&
           GfxRectEqual(a->padding, b->padding) &&
           GfxRectEqual(a->content, b->content);
}

/**
 * The layer inside @p outer, inset by @p left/@p top/@p right/@p bottom -- and
 * **clamped so that it is always inside @p outer**.
 *
 * The clamp is the difference between an arithmetic that is right for an ordinary box and
 * one that is right for every box, and the case that needs it is ordinary enough: a
 * `width: 10px` box with a `border: 20px solid` has insets that the box cannot hold, and
 * the unclamped answer puts the padding box's *origin* outside the border box. That is not
 * a box -- it is a rectangle whose layers cross -- and this module's own validity rule
 * (`PaintBoxesIsValid`) would then refuse every operation derived from it, which
 * would fail a pass on a document that is merely extreme.
 *
 * So a layer the insets squeeze out is an **empty layer at the outer box's own edge**, and
 * the three rectangles nest by construction rather than by luck. That is also the readable
 * answer: a border wider than its box covers the box, and there is no room left inside it
 * for anything.
 *
 * Every sum is taken in 64 bits and saturated, for the reason `RoboPaintInternal.h`
 * states: an accumulation of two bounded coordinates is not bounded.
 */
static void PaintInsetRect(const LayoutRect *outer,
                             int32_t left,
                             int32_t top,
                             int32_t right,
                             int32_t bottom,
                             LayoutRect *out)
{
    int32_t outerRight = PaintAddSaturated(outer->x, outer->width);
    int32_t outerBottom = PaintAddSaturated(outer->y, outer->height);
    int32_t x = PaintAddSaturated(outer->x, left);
    int32_t y = PaintAddSaturated(outer->y, top);
    int32_t farX = PaintSubSaturated(outerRight, right);
    int32_t farY = PaintSubSaturated(outerBottom, bottom);

    /* The near edge is pulled back inside the outer box, then the far edge is pushed out
     * to at least the near one -- which is what keeps the extent non-negative without a
     * separate floor. */
    if (x < outer->x) {
        x = outer->x;
    }
    if (x > outerRight) {
        x = outerRight;
    }
    if (y < outer->y) {
        y = outer->y;
    }
    if (y > outerBottom) {
        y = outerBottom;
    }
    if (farX > outerRight) {
        farX = outerRight;
    }
    if (farX < x) {
        farX = x;
    }
    if (farY > outerBottom) {
        farY = outerBottom;
    }
    if (farY < y) {
        farY = y;
    }

    out->x = x;
    out->y = y;
    out->width = PaintExtent((int64_t)farX - (int64_t)x);
    out->height = PaintExtent((int64_t)farY - (int64_t)y);
}

Result PaintBoxesFromLayers(const LayoutRect *borderBox,
                                           const LayoutBorderEdges *borders,
                                           const LayoutPaddingEdges *padding,
                                           PaintBoxes *out,
                                           PaintDiag *diag)
{
    int32_t borderWidths[RoboCssBoxSideCount];
    int32_t paddings[RoboCssBoxSideCount];

    if (!borderBox || !borders || !padding || !out) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboInvalidArgument,
                               "the border box and both inset records are required");
    }
    if (!GfxRectIsValid(*borderBox)) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboInvalidArgument,
                               "a border box with a negative size is not a box");
    }
    if (PaintBorderWidths(borders, borderWidths, diag) != RoboOk ||
        PaintPaddingWidths(padding, paddings, diag) != RoboOk) {
        return diag ? diag->code : RoboError;
    }

    out->border = *borderBox;

    /*
     * Two applications of the same clamp, and the order matters: the padding box is the
     * border box inset by the border widths, and the content box is *that* inset by the
     * paddings -- which is what makes the three compose in the order CSS's box model does
     * rather than each being computed from the border box independently.
     */
    PaintInsetRect(borderBox, borderWidths[RoboCssBoxSideLeft],
                     borderWidths[RoboCssBoxSideTop],
                     borderWidths[RoboCssBoxSideRight],
                     borderWidths[RoboCssBoxSideBottom], &out->padding);
    PaintInsetRect(&out->padding, paddings[RoboCssBoxSideLeft],
                     paddings[RoboCssBoxSideTop], paddings[RoboCssBoxSideRight],
                     paddings[RoboCssBoxSideBottom], &out->content);

    out->insetsMeasured = 1;
    return RoboOk;
}

Result PaintBoxesOf(const LayoutTree *tree,
                                  LayoutNodeId id,
                                  PaintBoxes *out,
                                  PaintDiag *diag)
{
    LayoutResult result;
    LayoutBorderEdges borders;
    LayoutPaddingEdges padding;
    LayoutRect borderBox;
    Result borderStatus;
    Result paddingStatus;

    if (!tree || !out || id == RoboLayoutNodeIdNone) {
        /* A `NONE` id is an *argument* error and a missing box is a *miss*: the same
         * division every read door in this tree draws, and the reason the two are not
         * folded into one code. */
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboInvalidArgument,
                               "a tree, an id and an output are required");
    }
    PaintBoxesInit(out);

    if (LayoutObjectResult(tree, id, &result) != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboNotFound,
                               "the id names no box in this tree");
    }
    /*
     * An object nothing placed is **not paint**: there is no rectangle, and answering
     * three zeroed ones would put a box at the page origin with no size, which is a
     * wrong answer a consumer could not tell from a real empty box. RoboLayout keeps the
     * per-box authority in the result *state* for exactly this question, so this door
     * asks it rather than inferring from a zero.
     */
    if (!LayoutResultIsComputed(&result)) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboNotFound,
                               "nothing placed this object, so it has no geometry to paint");
    }
    if (LayoutResultRect(&result, &borderBox, NULL) != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboError,
                               "the result's geometry is not a rectangle");
    }

    borderStatus = LayoutObjectBorders(tree, id, &borders, NULL);
    paddingStatus = LayoutObjectPadding(tree, id, &padding, NULL);
    if (borderStatus == RoboOk && paddingStatus == RoboOk) {
        return PaintBoxesFromLayers(&borderBox, &borders, &padding, out, diag);
    }
    /*
     * Both doors decline the same two things -- a **text run** and a **transparent inline
     * box with contents** -- and declining them is a statement rather than a failure: a
     * run is not a box, and a transparent inline box's rectangle is the union of what it
     * enclosed, which this build does not inflate by a declared padding and border
     * (`RoboLayoutPadding.h` argues it). So the box has one rectangle wearing three
     * names, and the flag says so.
     *
     * A failure that is not a decline is a different answer: a record the layout pass
     * never wrote, or one that is not a record at all, means the tree is not the state
     * this door was promised, and it is reported rather than flattened into "no insets".
     */
    if ((borderStatus != RoboOk && borderStatus != RoboNotFound) ||
        (paddingStatus != RoboOk && paddingStatus != RoboNotFound)) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboError,
                               "the box's inset records could not be read");
    }
    out->border = borderBox;
    out->padding = borderBox;
    out->content = borderBox;
    out->insetsMeasured = 0;
    return RoboOk;
}

Result PaintBoxesTranslate(const PaintBoxes *boxes,
                                         int32_t dx,
                                         int32_t dy,
                                         PaintBoxes *out,
                                         PaintDiag *diag)
{
    if (!boxes || !out) {
        return RoboPaintDiag(diag, RoboPaintDomainGeometry, RoboInvalidArgument,
                               "the boxes and an output are required");
    }
    *out = *boxes;
    out->border.x = PaintAddSaturated(boxes->border.x, dx);
    out->border.y = PaintAddSaturated(boxes->border.y, dy);
    out->padding.x = PaintAddSaturated(boxes->padding.x, dx);
    out->padding.y = PaintAddSaturated(boxes->padding.y, dy);
    out->content.x = PaintAddSaturated(boxes->content.x, dx);
    out->content.y = PaintAddSaturated(boxes->content.y, dy);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The clip in force                                                   */
/* ------------------------------------------------------------------ */

void PaintClipInit(PaintClip *clip)
{
    if (!clip) {
        return;
    }
    clip->horizontal.mode = RoboCssOverflowVisible;
    clip->horizontal.start = 0;
    clip->horizontal.end = 0;
    clip->vertical.mode = RoboCssOverflowVisible;
    clip->vertical.start = 0;
    clip->vertical.end = 0;
}

PaintClip PaintClipNone(void)
{
    PaintClip clip;

    PaintClipInit(&clip);
    return clip;
}

int PaintClipAxisIsActive(const PaintClipAxis *axis)
{
    if (!axis) {
        return 0;
    }
    /* "has a boundary" and "this mode clips" are the same question, and RoboCSS already
     * answers it -- so the identity is borrowed rather than re-decided here, and a mode
     * added to that vocabulary later that also clips counts as active without a change
     * in this file. */
    return CssOverflowClips(axis->mode);
}

int PaintClipIsActive(const PaintClip *clip)
{
    if (!clip) {
        return 0;
    }
    return PaintClipAxisIsActive(&clip->horizontal) ||
           PaintClipAxisIsActive(&clip->vertical);
}

int PaintClipIsValid(const PaintClip *clip)
{
    if (!clip) {
        return 0;
    }
    if (!CssOverflowIsKnown(clip->horizontal.mode) ||
        !CssOverflowIsKnown(clip->vertical.mode)) {
        return 0;
    }
    if (PaintClipAxisIsActive(&clip->horizontal) &&
        clip->horizontal.end < clip->horizontal.start) {
        return 0;
    }
    if (PaintClipAxisIsActive(&clip->vertical) &&
        clip->vertical.end < clip->vertical.start) {
        return 0;
    }
    return 1;
}

int PaintClipEqual(const PaintClip *a, const PaintClip *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->horizontal.mode == b->horizontal.mode &&
           a->horizontal.start == b->horizontal.start &&
           a->horizontal.end == b->horizontal.end &&
           a->vertical.mode == b->vertical.mode &&
           a->vertical.start == b->vertical.start && a->vertical.end == b->vertical.end;
}

int PaintClipAxisIsEmpty(const PaintClipAxis *axis)
{
    return PaintClipAxisIsActive(axis) && axis->end <= axis->start;
}

int PaintClipIsEmpty(const PaintClip *clip)
{
    if (!clip) {
        return 0;
    }
    /* Either axis is enough: an interval with no width bounds nothing, whatever the other
     * axis says -- which is why this is an `or` and not an `and`. */
    return PaintClipAxisIsEmpty(&clip->horizontal) ||
           PaintClipAxisIsEmpty(&clip->vertical);
}

int PaintClipAxisContains(const PaintClipAxis *axis, int32_t coordinate)
{
    if (!axis || !PaintClipAxisIsActive(axis)) {
        /* No record, or an axis that bounds nothing: every coordinate is inside it. That is
         * the direction this must fail in -- answering 0 would exclude every point of every
         * document whose `overflow` is `visible`. */
        return 1;
    }
    /* Half-open, the convention the interval is stated in: `start` is inside and `end` is
     * not, which is what makes two abutting intervals tile without overlapping. An *empty*
     * interval contains nothing at all, because `end <= start` leaves no coordinate that is
     * both at or above the start and below the end. */
    return coordinate >= axis->start && coordinate < axis->end;
}

int PaintClipContains(const PaintClip *clip, LayoutPoint point)
{
    if (!clip) {
        return 1;
    }
    return PaintClipAxisContains(&clip->horizontal, point.x) &&
           PaintClipAxisContains(&clip->vertical, point.y);
}

/**
 * One axis of the intersection: @p existing bounded by @p start/@p end when @p mode
 * hides, carried through unchanged when it does not.
 *
 * A helper rather than two copies because the two axes differ only in which numbers they
 * are given, and a transposed pair of copies is the classic way this arithmetic goes
 * wrong.
 *
 * **A disjoint result is canonicalized to a zero-length interval**, and that is a decision
 * rather than a tidy-up. The smaller of two `max`es and the larger of two `min`es can come out
 * backwards -- a box an ancestor's boundary entirely excluded produces exactly that -- and a
 * record whose interval runs backwards is one `PaintClipIsValid` refuses. Left alone,
 * the cell of the record model that means "nothing can be drawn here" would be the one cell that
 * is not a record, and every consumer would have to choose between trusting the type and
 * trusting the arithmetic. Canonicalized, an excluded box is an **empty interval at the
 * boundary that excluded it** (`end == start`), which is valid, is what
 * `PaintClipAxisIsEmpty` answers, and survives the whole pipeline -- a push, a pop, a
 * stack, a display command and a projection to a rectangle of zero width -- without a caller
 * ever having to ask whether it is holding a legal record.
 *
 * The placement of the empty interval is the **upper start**: the larger of the two starts, the
 * later of the two edges either interval began at. That is a function of the two intervals and of
 * nothing else, which is what makes it deterministic for equal inputs -- no notion of which side
 * "did the excluding" enters the arithmetic, and a `[200, 210]` box under a `[0, 40]` boundary
 * lands on `[200, 200]` just as a `[0, 40]` box under a `[200, 210]` boundary would.
 */
static void PaintClipIntersectAxis(PaintClipAxis *out,
                                      const PaintClipAxis *existing,
                                      CssOverflow mode,
                                      int32_t start,
                                      int32_t end)
{
    if (!CssOverflowClips(mode)) {
        /* A `visible` axis does not clip, and it does not *unclip* either: whatever an
         * ancestor put in force stays in force, which is why this is a copy and not a
         * clear. */
        *out = *existing;
        return;
    }
    if (PaintClipAxisIsActive(existing)) {
        out->mode = mode;
        out->start = PaintMax(existing->start, start);
        out->end = PaintMin(existing->end, end);
    } else {
        out->mode = mode;
        out->start = start;
        out->end = end;
    }
    if (out->end < out->start) {
        out->end = out->start;
    }
}

Result PaintClipIntersect(const PaintClip *clip,
                                       const LayoutRect *boundary,
                                       CssOverflow horizontal,
                                       CssOverflow vertical,
                                       PaintClip *out,
                                       PaintDiag *diag)
{
    int32_t right;
    int32_t bottom;

    if (!clip || !boundary || !out) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboInvalidArgument,
                               "the clip, the boundary and an output are required");
    }
    if (!CssOverflowIsKnown(horizontal) || !CssOverflowIsKnown(vertical)) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboInvalidArgument,
                               "an overflow mode this build does not have");
    }
    if (!GfxRectIsValid(*boundary)) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboInvalidArgument,
                               "a boundary with a negative size is not a rectangle");
    }
    if (!PaintClipIsValid(clip)) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboError,
                               "the clip in force is not a record this build produced");
    }

    /* The far edges in 64 bits: `x + width` is the sum of two bounded numbers. */
    right = PaintAddSaturated(boundary->x, boundary->width);
    bottom = PaintAddSaturated(boundary->y, boundary->height);

    PaintClipIntersectAxis(&out->horizontal, &clip->horizontal, horizontal,
                              boundary->x, right);
    PaintClipIntersectAxis(&out->vertical, &clip->vertical, vertical,
                              boundary->y, bottom);
    return RoboOk;
}

Result PaintClipIntersectClip(const PaintClip *outer,
                                            const PaintClip *inner,
                                            PaintClip *out,
                                            PaintDiag *diag)
{
    if (!outer || !inner || !out) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboInvalidArgument,
                               "both clips and an output are required");
    }
    if (!PaintClipIsValid(outer) || !PaintClipIsValid(inner)) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboError,
                               "a clip in the intersection is not a record this build "
                               "produced");
    }
    /*
     * The same per-axis helper the boundary intersection uses, in the same order, for the
     * reason it exists: the two axes differ only in which numbers they are given, and a
     * transposed pair of copies is the classic way this arithmetic goes wrong. An axis
     * `inner` does not bound takes the `visible` branch inside the helper and is copied from
     * `outer` -- so "a scope that bounds only one axis does not free the other" is the
     * helper's own rule rather than a second statement of it here.
     */
    PaintClipIntersectAxis(&out->horizontal, &outer->horizontal, inner->horizontal.mode,
                              inner->horizontal.start, inner->horizontal.end);
    PaintClipIntersectAxis(&out->vertical, &outer->vertical, inner->vertical.mode,
                              inner->vertical.start, inner->vertical.end);
    return RoboOk;
}

Result PaintClipRect(const PaintClip *clip, LayoutRect *out)
{
    if (!clip || !out) {
        return RoboInvalidArgument;
    }
    if (!PaintClipAxisIsActive(&clip->horizontal) ||
        !PaintClipAxisIsActive(&clip->vertical)) {
        return RoboNotFound;
    }
    out->x = clip->horizontal.start;
    out->y = clip->vertical.start;
    out->width = PaintExtent((int64_t)clip->horizontal.end - (int64_t)clip->horizontal.start);
    out->height = PaintExtent((int64_t)clip->vertical.end - (int64_t)clip->vertical.start);
    return RoboOk;
}
