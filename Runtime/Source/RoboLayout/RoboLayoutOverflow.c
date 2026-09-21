/*
 * RoboLayout overflow (Phase 10.13) — see RoboLayoutOverflow.h.
 *
 * Two readers, one policy and one accumulation rule, and the file is short for the
 * reason every phase's is: the box model did the hard arithmetic already. What is here
 * is
 *
 *   the **read**       a computed style's three overflow properties, classified into
 *                      two axes of three kinds each;
 *   the **resolve**    the kinds into two used modes and two counters;
 *   the **include**    one rectangle folded into a record, as four per-side maxima;
 *   the **region**     the record turned back into a rectangle, clipped per axis.
 *
 * One rule this file adds to the module, and it is the phase in a sentence:
 *
 *   an extent is how far contents reached *past* the padding box, never a coordinate
 *   and never negative
 *
 * Everything else is 10.10's and 10.11's arithmetic in a second frame: the four maxima
 * are performed in 64 bits and saturated, exactly as the two size doors are, because
 * the terms are each bounded by the seam's maximum and a sum of two of them is not. No
 * allocation, no state between calls and no cache: every function below is a function of
 * the record it was handed, which is what makes re-layout a matter of calling the pass
 * again.
 */

#include "RoboLayout/RoboLayoutOverflow.h"

#include "RoboLayoutInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

int LayoutOverflowKindIsKnown(LayoutOverflowKind kind)
{
    return kind == RoboLayoutOverflowAbsent ||
           kind == RoboLayoutOverflowResolved ||
           kind == RoboLayoutOverflowUnresolved;
}

const char *LayoutOverflowKindName(LayoutOverflowKind kind)
{
    switch (kind) {
    case RoboLayoutOverflowAbsent:
        return "absent";
    case RoboLayoutOverflowResolved:
        return "resolved";
    case RoboLayoutOverflowUnresolved:
        return "unresolved";
    default:
        break;
    }
    return "unknown";
}

int LayoutOverflowAxisIsKnown(LayoutOverflowAxisId axis)
{
    return axis == RoboLayoutOverflowAxisHorizontal ||
           axis == RoboLayoutOverflowAxisVertical;
}

const char *LayoutOverflowAxisName(LayoutOverflowAxisId axis)
{
    switch (axis) {
    case RoboLayoutOverflowAxisHorizontal:
        return "horizontal";
    case RoboLayoutOverflowAxisVertical:
        return "vertical";
    default:
        break;
    }
    return "unknown";
}

LayoutOverflowAxis LayoutOverflowAxisAbsent(void)
{
    LayoutOverflowAxis axis;

    axis.kind = RoboLayoutOverflowAbsent;
    axis.mode = RoboCssOverflowVisible;
    return axis;
}

LayoutOverflowAxis LayoutOverflowAxisResolved(CssOverflow mode)
{
    LayoutOverflowAxis axis;

    axis.kind = RoboLayoutOverflowResolved;
    /* A mode nothing defined is stored as `visible` rather than refused, because the
     * caller is this module's own reader and a hand-built record is caught by
     * LayoutOverflowAxisIsValid -- the door that exists for exactly that. */
    axis.mode = CssOverflowIsKnown(mode) ? mode : RoboCssOverflowVisible;
    return axis;
}

LayoutOverflowAxis LayoutOverflowAxisUnresolved(void)
{
    LayoutOverflowAxis axis;

    axis.kind = RoboLayoutOverflowUnresolved;
    /* And `visible` again, and here it is a *decision* rather than a default: a
     * declaration this build cannot use is used as the property's initial value, which
     * is 10.8's rule for a dimension nothing can resolve. */
    axis.mode = RoboCssOverflowVisible;
    return axis;
}

int LayoutOverflowAxisIsAbsent(const LayoutOverflowAxis *axis)
{
    return axis && axis->kind == RoboLayoutOverflowAbsent;
}

int LayoutOverflowAxisIsResolved(const LayoutOverflowAxis *axis)
{
    return axis && axis->kind == RoboLayoutOverflowResolved;
}

int LayoutOverflowAxisIsUnresolved(const LayoutOverflowAxis *axis)
{
    return axis && axis->kind == RoboLayoutOverflowUnresolved;
}

int LayoutOverflowAxisIsValid(const LayoutOverflowAxis *axis)
{
    if (!axis || !LayoutOverflowKindIsKnown(axis->kind)) {
        return 0;
    }
    /*
     * The mode is checked in every kind, not only in `RESOLVED`, and that is deliberate:
     * the cleared state of both other kinds is `visible`, so a record whose mode is
     * something else has been written by hand and is not one this module's reader can
     * produce. Checking it once here is what makes "an unusable declaration is used as
     * `visible`" an invariant of the type rather than a rule every consumer remembers.
     */
    return CssOverflowIsKnown(axis->mode);
}

Result LayoutOverflowAxisMode(const LayoutOverflowAxis *axis,
                                             CssOverflow *out)
{
    if (!axis || !out) {
        return RoboInvalidArgument;
    }
    if (axis->kind != RoboLayoutOverflowResolved) {
        return RoboNotFound;
    }
    *out = axis->mode;
    return RoboOk;
}

int LayoutOverflowAxisEqual(const LayoutOverflowAxis *a,
                                    const LayoutOverflowAxis *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    /* The mode only where it means something: two undeclared axes are equal whatever
     * their stale `mode` fields hold, which is the same rule 10.10's margin vocabulary
     * and 10.11's width vocabulary use. */
    if (a->kind != RoboLayoutOverflowResolved) {
        return 1;
    }
    return a->mode == b->mode;
}

/* ------------------------------------------------------------------ */
/* The declared overflow                                               */
/* ------------------------------------------------------------------ */

void LayoutOverflowInit(LayoutOverflow *overflow)
{
    if (!overflow) {
        return;
    }
    overflow->horizontal = LayoutOverflowAxisAbsent();
    overflow->vertical = LayoutOverflowAxisAbsent();
}

/**
 * The axis record @p style declares for the longhand @p longhand, falling back to the
 * shorthand @p shorthand.
 *
 * Three answers from two reads, and the fallback is the phase's one reading rule: the
 * longhand first, and the shorthand only for an axis the longhand said *nothing* about.
 * "Said nothing" is the `ABSENT` kind and not "resolved to visible", which is why the
 * kinds exist: `overflow-x: visible; overflow: hidden` is a box that clips vertically
 * and not horizontally, and a build that fell back on a resolved `visible` would clip
 * both.
 *
 * A computed value that is not overflow-shaped -- a `var()` reference this build carries,
 * or a declaration CSS refused and recorded as INVALID -- is `UNRESOLVED`, which is the
 * same answer 10.8 gives a dimension it cannot resolve and is counted for the same
 * reason.
 */
static LayoutOverflowAxis LayoutOverflowAxisOfProperty(
    const CssComputedStyle *style,
    const char *longhand,
    const char *shorthand)
{
    const CssComputedValue *value;
    CssOverflow mode;

    if (!style || !longhand) {
        return LayoutOverflowAxisAbsent();
    }

    value = CssComputedStyleComputedByName(style,
                                                     CssSliceFromText(longhand));
    if (!value || value->kind == RoboCssComputedValueNone) {
        /*
         * The longhand said nothing -- so the shorthand speaks for this axis, which is
         * what `overflow: hidden` means and the whole of this build's shorthand handling:
         * no expansion exists, so the one declaration that *is* the two axes is read as
         * both of them. A shorthand that is also absent is the initial state.
         */
        if (!shorthand) {
            return LayoutOverflowAxisAbsent();
        }
        value = CssComputedStyleComputedByName(style,
                                                        CssSliceFromText(shorthand));
        if (!value || value->kind == RoboCssComputedValueNone) {
            return LayoutOverflowAxisAbsent();
        }
    }

    if (value->kind == RoboCssComputedValueOverflow) {
        mode = CssComputedValueOverflow(value);
        if (CssOverflowIsKnown(mode)) {
            return LayoutOverflowAxisResolved(mode);
        }
    }
    /* Declared, and not a value this build can use. Its used mode is `visible`, and it is
     * counted rather than dropped so that a document whose clipping depends on a
     * `var(--o)` is visible as one. */
    return LayoutOverflowAxisUnresolved();
}

LayoutOverflow LayoutOverflowOf(const CssComputedStyle *style)
{
    LayoutOverflow overflow;

    overflow.horizontal = LayoutOverflowAxisOfProperty(style, "overflow-x", "overflow");
    overflow.vertical = LayoutOverflowAxisOfProperty(style, "overflow-y", "overflow");
    return overflow;
}

Result LayoutOverflowAxisOf(const LayoutOverflow *overflow,
                                           LayoutOverflowAxisId axis,
                                           LayoutOverflowAxis *out)
{
    if (!overflow || !out) {
        return RoboInvalidArgument;
    }
    switch (axis) {
    case RoboLayoutOverflowAxisHorizontal:
        *out = overflow->horizontal;
        return RoboOk;
    case RoboLayoutOverflowAxisVertical:
        *out = overflow->vertical;
        return RoboOk;
    default:
        break;
    }
    return RoboNotFound;
}

int LayoutOverflowEqual(const LayoutOverflow *a,
                               const LayoutOverflow *b)
{
    if (!a || !b) {
        return a == b;
    }
    return LayoutOverflowAxisEqual(&a->horizontal, &b->horizontal) &&
           LayoutOverflowAxisEqual(&a->vertical, &b->vertical);
}

int LayoutOverflowIsValid(const LayoutOverflow *overflow)
{
    if (!overflow) {
        return 0;
    }
    return LayoutOverflowAxisIsValid(&overflow->horizontal) &&
           LayoutOverflowAxisIsValid(&overflow->vertical);
}

/* ------------------------------------------------------------------ */
/* The used overflow                                                   */
/* ------------------------------------------------------------------ */

void LayoutOverflowEdgesInit(LayoutOverflowEdges *edges)
{
    if (!edges) {
        return;
    }
    /* `visible` rather than a "none" enumerator, because the cleared state of this record
     * is a real box: `overflow: visible` with nothing sticking out is the most common box
     * in every document, and a record whose zero meant "no answer" would make the initial
     * state unrepresentable. */
    edges->horizontal = RoboCssOverflowVisible;
    edges->vertical = RoboCssOverflowVisible;
    edges->left = 0;
    edges->top = 0;
    edges->right = 0;
    edges->bottom = 0;
    edges->axesResolved = 0u;
    edges->axesUnresolved = 0u;
}

int LayoutOverflowEdgesIsValid(const LayoutOverflowEdges *edges)
{
    if (!edges) {
        return 0;
    }
    if (!CssOverflowIsKnown(edges->horizontal) ||
        !CssOverflowIsKnown(edges->vertical)) {
        return 0;
    }
    /* Non-negative, and the check is the vocabulary's whole arithmetic invariant: an
     * extent says how far contents reached *past* an edge, so a negative one would be a
     * measurement of contents that stayed inside. */
    if (edges->left < 0 || edges->top < 0 || edges->right < 0 || edges->bottom < 0) {
        return 0;
    }
    if (edges->axesResolved + edges->axesUnresolved >
        (size_t)RoboLayoutOverflowAxisCount) {
        return 0;
    }
    return 1;
}

int LayoutOverflowEdgesEqual(const LayoutOverflowEdges *a,
                                     const LayoutOverflowEdges *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->horizontal == b->horizontal && a->vertical == b->vertical &&
           a->left == b->left && a->top == b->top && a->right == b->right &&
           a->bottom == b->bottom && a->axesResolved == b->axesResolved &&
           a->axesUnresolved == b->axesUnresolved;
}

CssOverflow LayoutOverflowEdgesMode(
    const LayoutOverflowEdges *edges,
    LayoutOverflowAxisId axis)
{
    if (!edges) {
        return RoboCssOverflowVisible;
    }
    switch (axis) {
    case RoboLayoutOverflowAxisVertical:
        return edges->vertical;
    case RoboLayoutOverflowAxisHorizontal:
    default:
        /* An unknown axis answers `visible`, for the reason an unknown overflow value
         * answers "does not clip": a record nothing can read must not throw contents
         * away. */
        return edges->horizontal;
    }
}

int LayoutOverflowEdgesClips(const LayoutOverflowEdges *edges,
                                     LayoutOverflowAxisId axis)
{
    return CssOverflowClips(LayoutOverflowEdgesMode(edges, axis));
}

int LayoutOverflowEdgesClipsAny(const LayoutOverflowEdges *edges)
{
    return LayoutOverflowEdgesClips(edges, RoboLayoutOverflowAxisHorizontal) ||
           LayoutOverflowEdgesClips(edges, RoboLayoutOverflowAxisVertical);
}

Result LayoutOverflowEdgesExtent(const LayoutOverflowEdges *edges,
                                                CssBoxSide side,
                                                int32_t *out)
{
    if (!edges || !out) {
        return RoboInvalidArgument;
    }
    switch (side) {
    case RoboCssBoxSideTop:
        *out = edges->top;
        return RoboOk;
    case RoboCssBoxSideRight:
        *out = edges->right;
        return RoboOk;
    case RoboCssBoxSideBottom:
        *out = edges->bottom;
        return RoboOk;
    case RoboCssBoxSideLeft:
        *out = edges->left;
        return RoboOk;
    default:
        break;
    }
    return RoboInvalidArgument;
}

int LayoutOverflowEdgesHasOverflowAxis(
    const LayoutOverflowEdges *edges,
    LayoutOverflowAxisId axis)
{
    if (!edges) {
        return 0;
    }
    switch (axis) {
    case RoboLayoutOverflowAxisVertical:
        return edges->top > 0 || edges->bottom > 0;
    case RoboLayoutOverflowAxisHorizontal:
        return edges->left > 0 || edges->right > 0;
    default:
        break;
    }
    return 0;
}

int LayoutOverflowEdgesHasOverflow(const LayoutOverflowEdges *edges)
{
    return LayoutOverflowEdgesHasOverflowAxis(
               edges, RoboLayoutOverflowAxisHorizontal) ||
           LayoutOverflowEdgesHasOverflowAxis(
               edges, RoboLayoutOverflowAxisVertical);
}

/** Clamp @p value into the geometry's range, from a 64-bit sum. */
static int32_t LayoutOverflowClamp(int64_t value)
{
    if (value > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        return (int32_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    if (value < 0) {
        return 0;
    }
    return (int32_t)value;
}

/** Clamp @p value into the geometry's coordinate range, sign kept. */
static int32_t LayoutOverflowClampSigned(int64_t value)
{
    if (value > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        return (int32_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    if (value < -(int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        return (int32_t)(-(int64_t)LAYOUT_STYLE_LENGTH_MAX);
    }
    return (int32_t)value;
}

/**
 * The one refusal both rectangle-taking doors share, so they refuse identically -- and
 * the two *different* answers in it are the point: a record that was not handed at all is
 * a caller error (INVALID_ARGUMENT, which is what every other door in this module
 * answers for a NULL argument), while a record that was handed and is not one is a
 * *record* error (ERROR). Folding the second into the first would tell a caller that
 * passed a hand-built struct to go and look at its arguments.
 */
static Result LayoutOverflowRequireEdges(
    const LayoutOverflowEdges *edges,
    LayoutDiag *diag)
{
    if (!edges) {
        return RoboLayoutDiag(diag, RoboLayoutDomainOverflow, RoboInvalidArgument,
                                "deriving a rectangle needs an overflow record");
    }
    if (!LayoutOverflowEdgesIsValid(edges)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainOverflow, RoboError,
                                "an overflow record no rectangle can be derived from");
    }
    return RoboOk;
}

Result LayoutOverflowEdgesInclude(LayoutOverflowEdges *edges,
                                                LayoutRect paddingBox,
                                                LayoutRect region,
                                                LayoutDiag *diag)
{
    int64_t candidate;
    Result status;

    if (!edges) {
        return RoboLayoutDiag(diag, RoboLayoutDomainOverflow, RoboInvalidArgument,
                                "folding a region in needs a record");
    }
    if (paddingBox.width < 0 || paddingBox.height < 0 || region.width < 0 ||
        region.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainOverflow, RoboInvalidArgument,
                                "a rectangle with a negative size has no region");
    }
    status = LayoutOverflowRequireEdges(edges, diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * Four per-side maxima, each "how far past this edge did the region reach", computed in
     * 64 bits and floored at zero. The floor is what makes the operation idempotent for a
     * region that was already inside -- and it is a *maximum* with the existing extent
     * rather than an assignment, because a box's overflow is the outermost thing any of its
     * contents reached and not the last one measured.
     *
     * The right/bottom terms are computed from the far edges rather than from widths, so a
     * region is placed by its own coordinates: `region.x + region.width - (paddingBox.x +
     * paddingBox.width)` is the same number whichever side the caller measured from, and
     * the two coordinate spaces are required to be one.
     */
    candidate = (int64_t)paddingBox.x - (int64_t)region.x;
    if (candidate > (int64_t)edges->left) {
        edges->left = LayoutOverflowClamp(candidate);
    }
    candidate = (int64_t)paddingBox.y - (int64_t)region.y;
    if (candidate > (int64_t)edges->top) {
        edges->top = LayoutOverflowClamp(candidate);
    }
    candidate = ((int64_t)region.x + (int64_t)region.width) -
                ((int64_t)paddingBox.x + (int64_t)paddingBox.width);
    if (candidate > (int64_t)edges->right) {
        edges->right = LayoutOverflowClamp(candidate);
    }
    candidate = ((int64_t)region.y + (int64_t)region.height) -
                ((int64_t)paddingBox.y + (int64_t)paddingBox.height);
    if (candidate > (int64_t)edges->bottom) {
        edges->bottom = LayoutOverflowClamp(candidate);
    }
    return RoboOk;
}

Result LayoutOverflowEdgesRegion(const LayoutOverflowEdges *edges,
                                               LayoutRect paddingBox,
                                               LayoutRect *out,
                                               LayoutDiag *diag)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    Result status;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainOverflow, RoboInvalidArgument,
                                "deriving a region needs an out");
    }
    if (paddingBox.width < 0 || paddingBox.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainOverflow, RoboInvalidArgument,
                                "a padding box with a negative size has no region");
    }
    status = LayoutOverflowRequireEdges(edges, diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * Left and right are taken per axis rather than per side, because `overflow: hidden`
     * is a statement about an *axis*: a box that clips horizontally keeps whatever its
     * contents did vertically, and the pair (left, right) therefore clips together while
     * (top, bottom) does not. This is the one place the two axes are answered separately,
     * and it is the reason the record has two modes and not one.
     */
    if (LayoutOverflowEdgesClips(edges, RoboLayoutOverflowAxisHorizontal)) {
        left = (int64_t)paddingBox.x;
        right = (int64_t)paddingBox.x + (int64_t)paddingBox.width;
    } else {
        left = (int64_t)paddingBox.x - (int64_t)edges->left;
        right = (int64_t)paddingBox.x + (int64_t)paddingBox.width + (int64_t)edges->right;
    }
    if (LayoutOverflowEdgesClips(edges, RoboLayoutOverflowAxisVertical)) {
        top = (int64_t)paddingBox.y;
        bottom = (int64_t)paddingBox.y + (int64_t)paddingBox.height;
    } else {
        top = (int64_t)paddingBox.y - (int64_t)edges->top;
        bottom = (int64_t)paddingBox.y + (int64_t)paddingBox.height + (int64_t)edges->bottom;
    }

    out->x = (GfxPixelX)LayoutOverflowClampSigned(left);
    out->y = (GfxPixelY)LayoutOverflowClampSigned(top);
    out->width = LayoutOverflowClamp(right - left);
    out->height = LayoutOverflowClamp(bottom - top);
    return RoboOk;
}

Result LayoutOverflowEdgesClipRect(
    const LayoutOverflowEdges *edges,
    LayoutRect paddingBox,
    LayoutRect *out,
    LayoutDiag *diag)
{
    Result status;

    if (!out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainOverflow, RoboInvalidArgument,
                                "deriving a clip rectangle needs an out");
    }
    if (paddingBox.width < 0 || paddingBox.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainOverflow, RoboInvalidArgument,
                                "a padding box with a negative size has no clip rectangle");
    }
    status = LayoutOverflowRequireEdges(edges, diag);
    if (status != RoboOk) {
        return status;
    }
    /*
     * The clip rectangle is the padding box and nothing else -- CSS's own boundary for
     * `overflow: hidden` -- so this door neither shrinks nor grows it, and the interesting
     * part of it is the *refusal* below: a box with no boundary has no clip rectangle, and
     * answering the padding box anyway would tell a paint pass to clip a box that paints its
     * contents outside itself. That is the failure this phase exists to prevent, arriving
     * from the other side.
     */
    if (!LayoutOverflowEdgesClipsAny(edges)) {
        return RoboNotFound;
    }
    *out = paddingBox;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The one policy                                                      */
/* ------------------------------------------------------------------ */

Result LayoutOverflowResolve(const LayoutOverflow *declared,
                                           LayoutOverflowEdges *out,
                                           LayoutDiag *diag)
{
    LayoutOverflowEdges edges;
    const LayoutOverflowAxis *axes[RoboLayoutOverflowAxisCount];
    CssOverflow *modes[RoboLayoutOverflowAxisCount];
    size_t index;

    if (!declared || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainOverflow, RoboInvalidArgument,
                                "resolving overflow needs a record and an out");
    }

    LayoutOverflowEdgesInit(&edges);
    axes[RoboLayoutOverflowAxisHorizontal] = &declared->horizontal;
    axes[RoboLayoutOverflowAxisVertical] = &declared->vertical;
    modes[RoboLayoutOverflowAxisHorizontal] = &edges.horizontal;
    modes[RoboLayoutOverflowAxisVertical] = &edges.vertical;

    for (index = 0u; index < (size_t)RoboLayoutOverflowAxisCount; index++) {
        const LayoutOverflowAxis *axis = axes[index];

        switch (axis->kind) {
        case RoboLayoutOverflowAbsent:
            /* Nobody declared this axis: CSS's initial value is `visible`, and it is counted
             * nowhere -- the distinction the kinds exist for. */
            *modes[index] = RoboCssOverflowVisible;
            break;
        case RoboLayoutOverflowResolved:
            edges.axesResolved++;
            *modes[index] = CssOverflowIsKnown(axis->mode) ? axis->mode
                                                                  : RoboCssOverflowVisible;
            break;
        case RoboLayoutOverflowUnresolved:
        default:
            /* Declared, and not a value this build can use. Used as `visible`, counted so
             * that a document whose clipping depends on a `var()` is visible as one. */
            edges.axesUnresolved++;
            *modes[index] = RoboCssOverflowVisible;
            break;
        }
    }

    *out = edges;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Measuring one box                                                   */
/* ------------------------------------------------------------------ */

LayoutRect LayoutOverflowObjectRegion(const LayoutObject *object,
                                                     LayoutPoint paddingOrigin)
{
    LayoutOverflowEdges zeroed;
    LayoutRect region;
    const LayoutOverflowEdges *edges;
    int64_t width;
    int64_t height;

    LayoutOverflowEdgesInit(&zeroed);
    region.x = paddingOrigin.x;
    region.y = paddingOrigin.y;
    region.width = 0;
    region.height = 0;

    if (!object || !LayoutResultIsComputed(&object->result)) {
        return region;
    }

    /*
     * The object's own padding box, in the space the caller asked the answer in: its
     * border box is what a result carries, and the borders it was sized with are what turn
     * one into the other. A text run and a transparent inline box carry none (their records
     * are cleared), so for them this is the placement rectangle itself -- which is exactly
     * what their contribution should be.
     */
    width = (int64_t)object->result.size.width - (int64_t)object->borders.left.width -
            (int64_t)object->borders.right.width;
    height = (int64_t)object->result.size.height - (int64_t)object->borders.top.width -
             (int64_t)object->borders.bottom.width;
    region.width = LayoutOverflowClamp(width);
    region.height = LayoutOverflowClamp(height);

    /*
     * And its own overflow, which is a *cleared* record for an object nothing measured --
     * zero extents and two `visible` axes -- so the fold below is the identity for a leaf.
     * The zeroed local exists because the record on a never-measured object is only
     * guaranteed to be cleared, not to be this vocabulary's idea of it.
     */
    edges = LayoutOverflowEdgesIsValid(&object->overflow)
                ? &object->overflow
                : &zeroed;
    (void)LayoutOverflowEdgesRegion(edges, region, &region, NULL);
    return region;
}

Result LayoutOverflowMeasure(const LayoutTree *tree,
                                           const LayoutObject *object,
                                           const LayoutBorderEdges *borders,
                                           const LayoutPaddingEdges *padding,
                                           LayoutSize borderBox,
                                           LayoutOverflowEdges *out,
                                           LayoutDiag *diag)
{
    LayoutOverflow declared;
    LayoutOverflowEdges edges;
    LayoutRect paddingBox;
    LayoutNodeId child;
    int64_t paddingWidth;
    int64_t paddingHeight;
    Result status;

    if (!tree || !object || !borders || !padding || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainOverflow, RoboInvalidArgument,
                                "measuring overflow needs the tree, the box and an out");
    }
    if (borderBox.width < 0 || borderBox.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainOverflow, RoboInvalidArgument,
                                "a border box is never negative");
    }

    /* The declarations first, then the measurement: the modes are what the *record* says
     * about the box, and the extents are what the contents turned out to be. */
    declared = LayoutOverflowOf(object->style);
    status = LayoutOverflowResolve(&declared, &edges, diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * The padding box, in the box's *own* space with its corner at the origin -- so the
     * extents this function computes are measured against the box itself and no coordinate
     * of any ancestor is involved. It is the border box less the borders, floored at zero
     * for the reason every subtraction in this module is: a box whose borders are wider than
     * the box itself has no padding box, and "no room" is zero rather than negative.
     */
    paddingWidth = (int64_t)borderBox.width - (int64_t)borders->left.width -
                    (int64_t)borders->right.width;
    paddingHeight = (int64_t)borderBox.height - (int64_t)borders->top.width -
                     (int64_t)borders->bottom.width;
    paddingBox.x = 0;
    paddingBox.y = 0;
    paddingBox.width = LayoutOverflowClamp(paddingWidth);
    paddingBox.height = LayoutOverflowClamp(paddingHeight);

    /*
     * Then every placed child, once. One loop for both directions and for all three kinds of
     * child, and that uniformity is the phase's recursion rather than a shortcut:
     *
     *   a **block-level** child's result is its border box in this box's *content* space,
     *   a **text run**'s result is the run's rectangle in the same space,
     *   an **atomic inline**'s result is the box the line placed, also in that space,
     *   and a **transparent inline box**'s result is the union of what its own contents
     *   reached -- so each one's region already contains everything below it, at every
     *   depth, and nothing here walks a grandchild.
     *
     * The child's padding box is placed by adding the insets that are *outside* it -- this
     * box's padding (which is where the content origin is, relative to the padding box this
     * measurement is against) and the child's own border -- so the two frames meet in one
     * expression and a child that sticks out to the left contributes a left extent that is
     * measured from the parent's edge and not from its own.
     *
     * A child with no computed result is skipped rather than counted: it was declined for a
     * reason the *other* counters record (`inlineLevelSkipped`, `unreached`), and an
     * unplaced box has no region to contribute to anybody.
     */
    child = object->firstChild;
    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(tree, child);

        if (!childObject) {
            return RoboLayoutDiag(diag, RoboLayoutDomainOverflow, RoboError,
                                    "a box's child link names no box in this tree");
        }
        if (LayoutResultIsComputed(&childObject->result)) {
            LayoutPoint childPaddingOrigin;

            childPaddingOrigin.x = (GfxPixelX)(
                (int64_t)padding->left + (int64_t)childObject->result.position.x +
                (int64_t)childObject->borders.left.width);
            childPaddingOrigin.y = (GfxPixelY)(
                (int64_t)padding->top + (int64_t)childObject->result.position.y +
                (int64_t)childObject->borders.top.width);
            status = LayoutOverflowEdgesInclude(
                &edges, paddingBox,
                LayoutOverflowObjectRegion(childObject, childPaddingOrigin), diag);
            if (status != RoboOk) {
                return status;
            }
        }
        child = childObject->NextSibling;
    }

    *out = edges;
    return RoboOk;
}
