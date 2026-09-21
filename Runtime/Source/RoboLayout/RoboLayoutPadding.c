/*
 * RoboLayout padding (Phase 10.10) — the CSS seam, the one policy, and the two
 * derived numbers.
 *
 * The mirror image of RoboLayoutMargin.c, and shorter in the two places the grammar
 * makes it shorter: the classification's signedness parameter is *off* here, so a
 * negative declaration cannot arrive as a usable length; and there is one policy
 * instead of two, because padding has no `auto` for two policies to disagree about.
 * What is left is the arithmetic this phase is about -- what a border box holds and
 * what a content box asks for -- and it is written once, in two functions, rather than
 * once per pass.
 *
 * Nothing here reads a rectangle and nothing here writes one: a padding is a number
 * about space, and the passes are what turn it into coordinates. That is why this file
 * has no `LayoutWalk` in it.
 */

#include "RoboLayout/RoboLayoutPadding.h"

#include <string.h>

#include "RoboLayoutInternal.h"

/* ------------------------------------------------------------------ */
/* One side                                                            */
/* ------------------------------------------------------------------ */

int LayoutPaddingKindIsKnown(LayoutPaddingKind kind)
{
    return kind == RoboLayoutPaddingAbsent || kind == RoboLayoutPaddingResolved ||
           kind == RoboLayoutPaddingUnresolved;
}

const char *LayoutPaddingKindName(LayoutPaddingKind kind)
{
    switch (kind) {
    case RoboLayoutPaddingAbsent:
        return "absent";
    case RoboLayoutPaddingResolved:
        return "resolved";
    case RoboLayoutPaddingUnresolved:
        return "unresolved";
    default:
        break;
    }
    return "unknown";
}

LayoutPadding LayoutPaddingZero(void)
{
    return LayoutPaddingResolved((GfxPixelX)0);
}

LayoutPadding LayoutPaddingAbsent(void)
{
    LayoutPadding padding;

    padding.kind = RoboLayoutPaddingAbsent;
    padding.length = 0;
    return padding;
}

LayoutPadding LayoutPaddingResolved(GfxPixelX length)
{
    LayoutPadding padding;

    padding.kind = RoboLayoutPaddingResolved;
    padding.length = length;
    return padding;
}

LayoutPadding LayoutPaddingUnresolved(void)
{
    LayoutPadding padding;

    /* A cleared length on the two kinds that carry none, so that two records built by
     * two callers compare equal -- `LayoutPaddingEqual` compares every field,
     * and a record whose meaningless field held a stack byte would make copying a
     * padding a way to change it. */
    padding.kind = RoboLayoutPaddingUnresolved;
    padding.length = 0;
    return padding;
}

int LayoutPaddingIsResolved(const LayoutPadding *padding)
{
    return padding && padding->kind == RoboLayoutPaddingResolved;
}

int LayoutPaddingIsAbsent(const LayoutPadding *padding)
{
    return padding && padding->kind == RoboLayoutPaddingAbsent;
}

int LayoutPaddingIsUnresolved(const LayoutPadding *padding)
{
    return padding && padding->kind == RoboLayoutPaddingUnresolved;
}

int LayoutPaddingIsValid(const LayoutPadding *padding)
{
    if (!padding) {
        return 0;
    }
    if (!LayoutPaddingKindIsKnown(padding->kind)) {
        return 0;
    }
    /*
     * The length is checked only where it means something, which is the asymmetry every
     * predicate in this module has and for the same reason: an `ABSENT` or `UNRESOLVED`
     * side carries a field nothing reads, and refusing a record because of a byte nobody
     * will look at would make those kinds unbuildable by a caller that cleared its struct
     * first.
     *
     * And where it does mean something, **two** bounds are checked where the margin
     * predicate checks one magnitude: a padding is a distance inward, so a negative one
     * is not a small padding, it is a declaration with no meaning. That is the whole of
     * the difference between this vocabulary and 10.9's, and it is checked here as well
     * as at the seam so that a hand-built record cannot smuggle one into a size.
     */
    if (padding->kind == RoboLayoutPaddingResolved) {
        double value = (double)padding->length;

        return value >= 0.0 && value <= LAYOUT_STYLE_LENGTH_MAX;
    }
    return 1;
}

Result LayoutPaddingLength(const LayoutPadding *padding,
                                        GfxPixelX *out)
{
    if (!padding || !out) {
        return RoboInvalidArgument;
    }
    if (padding->kind != RoboLayoutPaddingResolved) {
        /* A miss rather than the zero the record happens to hold, for the reason 10.8's
         * dimension door and 10.9's margin door both give: `padding: 0` is an inset an
         * author asked not to have, and it is a different answer from "there is no
         * number here". */
        return RoboNotFound;
    }
    *out = padding->length;
    return RoboOk;
}

int LayoutPaddingEqual(const LayoutPadding *a,
                              const LayoutPadding *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    if (a->kind != RoboLayoutPaddingResolved) {
        return 1;
    }
    return a->length == b->length;
}

/* ------------------------------------------------------------------ */
/* The four sides                                                      */
/* ------------------------------------------------------------------ */

/** The longhand each side is read from, indexed by `CssBoxSide`. */
static const char *const layoutPaddingProperty[RoboCssBoxSideCount] = {
    "padding-top",
    "padding-right",
    "padding-bottom",
    "padding-left"
};

void LayoutPaddingsInit(LayoutPaddings *paddings)
{
    if (!paddings) {
        return;
    }
    paddings->top = LayoutPaddingAbsent();
    paddings->right = LayoutPaddingAbsent();
    paddings->bottom = LayoutPaddingAbsent();
    paddings->left = LayoutPaddingAbsent();
}

/**
 * Map one classification answer onto a padding kind.
 *
 * The seam's third *shape*: 10.8's classification with signedness **off**, and its four
 * answers onto three kinds. The fold is the `AUTO` one, and it is deliberate:
 *
 *   `LAYOUT_STYLE_VALUE_AUTO` means a `KEYWORD` spelling `auto`, which for a padding is
 *   not a value the grammar has -- 9.11 rejects `padding: auto` outright, so a style
 *   carrying one has done something the CSS grammar forbids. Mapping it to `UNRESOLVED`
 *   says exactly that ("declared, and not a value this build can use") and counts it,
 *   which is more than a fourth kind would do: `AUTO` would give a grammar violation a
 *   vocabulary of its own and invite a later goal to give it a meaning.
 *
 * A negative length cannot reach here as `PX`: the classification was asked with
 * `allowNegative = 0`, so it answers `UNUSABLE` and is counted as a declaration this
 * build cannot use. That is the one parameter's worth of difference from the margin
 * reader, and it is why the two readers can share everything else.
 */
static LayoutPadding LayoutPaddingForKind(LayoutStyleValueKind kind,
                                                     double number)
{
    switch (kind) {
    case LAYOUT_STYLE_VALUE_PX:
        /* Rounded through 10.8's door, which 10.9 made genuinely half-away-from-zero. A
         * padding is never negative by the time it gets here, so the direction of the
         * rounding is the only thing that matters: `padding: 10.5px` is 11, which is what
         * a browser does with it too. */
        return LayoutPaddingResolved(LayoutPxRound(number));
    case LAYOUT_STYLE_VALUE_UNUSABLE:
    case LAYOUT_STYLE_VALUE_AUTO:
    default:
        return LayoutPaddingUnresolved();
    case LAYOUT_STYLE_VALUE_ABSENT:
        return LayoutPaddingAbsent();
    }
}

/** Read one side of @p style: the classification, then the mapping above. */
static LayoutPadding LayoutPaddingOfProperty(const CssComputedStyle *style,
                                                       const char *property)
{
    LayoutStyleValueKind kind = LAYOUT_STYLE_VALUE_ABSENT;
    double number = 0.0;

    /* A NULL style cannot happen here (the caller has one by construction) but the
     * classification refuses it, and a refusal is an absence rather than a length. */
    if (!LayoutStyleClassify(style, property, 0, &kind, &number)) {
        return LayoutPaddingAbsent();
    }
    return LayoutPaddingForKind(kind, number);
}

LayoutPaddings LayoutPaddingsOf(const CssComputedStyle *style)
{
    LayoutPaddings paddings;

    paddings.top = LayoutPaddingOfProperty(style, layoutPaddingProperty[0]);
    paddings.right = LayoutPaddingOfProperty(style, layoutPaddingProperty[1]);
    paddings.bottom = LayoutPaddingOfProperty(style, layoutPaddingProperty[2]);
    paddings.left = LayoutPaddingOfProperty(style, layoutPaddingProperty[3]);
    return paddings;
}

Result LayoutPaddingsSide(const LayoutPaddings *paddings,
                                       CssBoxSide side,
                                       LayoutPadding *out)
{
    if (!paddings || !out) {
        return RoboInvalidArgument;
    }
    if (!CssBoxSideIsKnown(side)) {
        return RoboNotFound;
    }
    switch (side) {
    case RoboCssBoxSideTop:
        *out = paddings->top;
        return RoboOk;
    case RoboCssBoxSideRight:
        *out = paddings->right;
        return RoboOk;
    case RoboCssBoxSideBottom:
        *out = paddings->bottom;
        return RoboOk;
    case RoboCssBoxSideLeft:
    default:
        *out = paddings->left;
        return RoboOk;
    }
}

int LayoutPaddingsEqual(const LayoutPaddings *a,
                               const LayoutPaddings *b)
{
    if (!a || !b) {
        return a == b;
    }
    return LayoutPaddingEqual(&a->top, &b->top) &&
           LayoutPaddingEqual(&a->right, &b->right) &&
           LayoutPaddingEqual(&a->bottom, &b->bottom) &&
           LayoutPaddingEqual(&a->left, &b->left);
}

int LayoutPaddingsIsValid(const LayoutPaddings *paddings)
{
    if (!paddings) {
        return 0;
    }
    return LayoutPaddingIsValid(&paddings->top) &&
           LayoutPaddingIsValid(&paddings->right) &&
           LayoutPaddingIsValid(&paddings->bottom) &&
           LayoutPaddingIsValid(&paddings->left);
}

/* ------------------------------------------------------------------ */
/* The used values                                                     */
/* ------------------------------------------------------------------ */

void LayoutPaddingEdgesInit(LayoutPaddingEdges *edges)
{
    if (!edges) {
        return;
    }
    memset(edges, 0, sizeof(*edges));
}

int LayoutPaddingEdgesIsValid(const LayoutPaddingEdges *edges)
{
    if (!edges) {
        return 0;
    }
    /*
     * The four numbers first, and every one of them at or above zero: a size derived from
     * a negative inset would be a rectangle smaller than its own contents, which is a
     * shape no renderer can draw and therefore a thing to catch here rather than in the
     * pass that multiplies it.
     */
    if (edges->top < 0 || edges->right < 0 || edges->bottom < 0 || edges->left < 0) {
        return 0;
    }
    /*
     * Then the counters, which have to be a partition of at most four sides -- a real
     * check rather than a formality for the reason 10.9's version gives: a record claiming
     * five resolved sides is a record some later arithmetic built wrong, and the moment to
     * catch it is before a caller reads a size out of it.
     */
    if (edges->resolved > RoboCssBoxSideCount ||
        edges->unresolved > RoboCssBoxSideCount) {
        return 0;
    }
    return edges->resolved + edges->unresolved <= RoboCssBoxSideCount;
}

int LayoutPaddingEdgesEqual(const LayoutPaddingEdges *a,
                                    const LayoutPaddingEdges *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->top == b->top && a->right == b->right && a->bottom == b->bottom &&
           a->left == b->left && a->resolved == b->resolved &&
           a->unresolved == b->unresolved;
}

/** The one place a padding record is refused, so both doors refuse it identically. */
static Result LayoutPaddingEdgesRequire(const LayoutPaddingEdges *edges,
                                                 LayoutDiag *diag)
{
    if (!LayoutPaddingEdgesIsValid(edges)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainPadding, RoboError,
                                "a padding record no size can be derived from");
    }
    return RoboOk;
}

Result LayoutPaddingEdgesContentWidth(const LayoutPaddingEdges *edges,
                                                      int32_t borderWidth,
                                                      int32_t *out,
                                                      LayoutDiag *diag)
{
    int64_t available;
    Result status;

    if (!edges || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainPadding, RoboInvalidArgument,
                                "reading a content width needs a record and an out");
    }
    status = LayoutPaddingEdgesRequire(edges, diag);
    if (status != RoboOk) {
        return status;
    }

    available = (int64_t)borderWidth - (int64_t)edges->left - (int64_t)edges->right;
    /*
     * Floored, and floored *here* rather than at each term, which is the same shape
     * 10.9's `LayoutMarginsFinish` has and for the same reason: one clamp, in one place,
     * on the one number a size arithmetic can act on. A border box narrower than its own
     * padding has no room for contents, and "no room" is zero rather than a negative width
     * -- a negative available width would make every `auto` dimension in the subtree
     * depend on which of two impossible numbers a subtraction produced.
     */
    if (available < 0) {
        available = 0;
    }
    if (available > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        available = (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    *out = (int32_t)available;
    return RoboOk;
}

Result LayoutPaddingEdgesPaddingBox(const LayoutPaddingEdges *edges,
                                                   LayoutSize content,
                                                   LayoutSize *out,
                                                   LayoutDiag *diag)
{
    int64_t width;
    int64_t height;
    Result status;

    if (!edges || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainPadding, RoboInvalidArgument,
                                "deriving a border box needs a record and an out");
    }
    if (content.width < 0 || content.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainPadding, RoboInvalidArgument,
                                "a content box is never negative");
    }
    status = LayoutPaddingEdgesRequire(edges, diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * In 64 bits, and clamped at the geometry's maximum rather than refused. The two terms
     * are each bounded by the seam's maximum and their sum is not -- a `width: 2147483647px`
     * box with a `padding: 10px` is a document an author can write, and the honest answer is
     * a size that saturates rather than one that wraps into a negative rectangle. The clamp
     * is on the *sum*, so a box whose content already saturates keeps the padding it cannot
     * express rather than losing it to a subtraction.
     */
    width = (int64_t)content.width + (int64_t)edges->left + (int64_t)edges->right;
    height = (int64_t)content.height + (int64_t)edges->top + (int64_t)edges->bottom;

    if (width > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        width = (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    if (height > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        height = (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }

    out->width = (GfxPixelX)width;
    out->height = (GfxPixelY)height;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The one policy                                                      */
/* ------------------------------------------------------------------ */

Result LayoutPaddingResolve(const LayoutPaddings *paddings,
                                          LayoutPaddingEdges *out,
                                          LayoutDiag *diag)
{
    const LayoutPadding *sides[RoboCssBoxSideCount];
    GfxPixelX *used[RoboCssBoxSideCount];
    LayoutPaddingEdges edges;
    size_t index;

    if (!paddings || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainPadding, RoboInvalidArgument,
                                "resolving padding needs a record and an out");
    }

    LayoutPaddingEdgesInit(&edges);
    sides[RoboCssBoxSideTop] = &paddings->top;
    sides[RoboCssBoxSideRight] = &paddings->right;
    sides[RoboCssBoxSideBottom] = &paddings->bottom;
    sides[RoboCssBoxSideLeft] = &paddings->left;
    used[RoboCssBoxSideTop] = &edges.top;
    used[RoboCssBoxSideRight] = &edges.right;
    used[RoboCssBoxSideBottom] = &edges.bottom;
    used[RoboCssBoxSideLeft] = &edges.left;

    for (index = 0u; index < RoboCssBoxSideCount; index++) {
        *used[index] = 0;
        switch (sides[index]->kind) {
        case RoboLayoutPaddingResolved:
            *used[index] = sides[index]->length;
            edges.resolved++;
            break;
        case RoboLayoutPaddingAbsent:
            /* Zero, and **counted nowhere**: the initial value is what the geometry uses,
             * and the counters are about declarations. This is the branch 10.9 had to add
             * a kind for, and it is the only place the distinction survives. */
            break;
        case RoboLayoutPaddingUnresolved:
        default:
            /* Zero, and *counted*: a document must not fail because of a declaration this
             * build cannot use, and the author's `5%` must not be silently a confident
             * zero nobody can see. */
            edges.unresolved++;
            break;
        }
    }

    /*
     * The record is validated before it is handed out rather than after it is used, so the
     * one arithmetic that can go wrong -- a negative side, which only a caller-supplied
     * record could carry -- is caught here and not in whichever pass happened to multiply
     * first.
     */
    if (!LayoutPaddingEdgesIsValid(&edges)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainPadding, RoboError,
                                "a padding resolution produced a record no size can hold");
    }
    *out = edges;
    return RoboOk;
}
