/*
 * RoboLayout margins (Phase 10.9) — the CSS seam, and the two policies.
 *
 * Two things live here and they are deliberately unequal in size: a *classification*
 * (what a style declared, per side) and a *policy* (what a block box and an
 * inline-level participant do with it). The classification is four calls into 10.8's
 * seam with signedness opened; the policy is the arithmetic this phase is about, and
 * it is written once per policy rather than once per caller.
 *
 * Nothing here reads a rectangle and nothing here writes one: a margin is a number
 * about space, and the passes are what turn it into coordinates. That is why this
 * file has no `LayoutWalk` in it.
 */

#include "RoboLayout/RoboLayoutMargin.h"

#include <string.h>

#include "RoboLayoutInternal.h"

/* ------------------------------------------------------------------ */
/* One side                                                            */
/* ------------------------------------------------------------------ */

int LayoutMarginKindIsKnown(LayoutMarginKind kind)
{
    return kind == RoboLayoutMarginAbsent || kind == RoboLayoutMarginAuto ||
           kind == RoboLayoutMarginResolved || kind == RoboLayoutMarginUnresolved;
}

const char *LayoutMarginKindName(LayoutMarginKind kind)
{
    switch (kind) {
    case RoboLayoutMarginAbsent:
        return "absent";
    case RoboLayoutMarginAuto:
        return "auto";
    case RoboLayoutMarginResolved:
        return "resolved";
    case RoboLayoutMarginUnresolved:
        return "unresolved";
    default:
        break;
    }
    return "unknown";
}

LayoutMargin LayoutMarginZero(void)
{
    return LayoutMarginResolved((GfxPixelX)0);
}

LayoutMargin LayoutMarginAbsent(void)
{
    LayoutMargin margin;

    margin.kind = RoboLayoutMarginAbsent;
    margin.length = 0;
    return margin;
}

LayoutMargin LayoutMarginResolved(GfxPixelX length)
{
    LayoutMargin margin;

    margin.kind = RoboLayoutMarginResolved;
    margin.length = length;
    return margin;
}

LayoutMargin LayoutMarginAuto(void)
{
    LayoutMargin margin;

    /*
     * A cleared length on the two kinds that carry none, so that two `auto`s built
     * by two callers compare equal -- `LayoutMarginEqual` compares every field,
     * and a record whose meaningless field held a stack byte would make copying a
     * margin a way to change it.
     */
    margin.kind = RoboLayoutMarginAuto;
    margin.length = 0;
    return margin;
}

LayoutMargin LayoutMarginUnresolved(void)
{
    LayoutMargin margin;

    margin.kind = RoboLayoutMarginUnresolved;
    margin.length = 0;
    return margin;
}

int LayoutMarginIsAuto(const LayoutMargin *margin)
{
    return margin && margin->kind == RoboLayoutMarginAuto;
}

int LayoutMarginIsResolved(const LayoutMargin *margin)
{
    return margin && margin->kind == RoboLayoutMarginResolved;
}

int LayoutMarginIsAbsent(const LayoutMargin *margin)
{
    return margin && margin->kind == RoboLayoutMarginAbsent;
}

int LayoutMarginIsUnresolved(const LayoutMargin *margin)
{
    return margin && margin->kind == RoboLayoutMarginUnresolved;
}

int LayoutMarginIsValid(const LayoutMargin *margin)
{
    if (!margin) {
        return 0;
    }
    if (!LayoutMarginKindIsKnown(margin->kind)) {
        return 0;
    }
    /*
     * The length is checked only where it means something, which is the same
     * asymmetry 10.8's dimension predicate has and for the same reason: an `auto`
     * carries a field that nothing reads, and refusing a record because of a byte
     * nobody will look at would make the two kinds unbuildable by a caller that
     * cleared its struct first.
     */
    if (margin->kind == RoboLayoutMarginResolved) {
        double magnitude = (double)margin->length;

        if (magnitude < 0.0) {
            magnitude = -magnitude;
        }
        return magnitude <= LAYOUT_STYLE_LENGTH_MAX;
    }
    return 1;
}

Result LayoutMarginLength(const LayoutMargin *margin,
                                       GfxPixelX *out)
{
    if (!margin || !out) {
        return RoboInvalidArgument;
    }
    if (margin->kind != RoboLayoutMarginResolved) {
        /* A miss rather than the zero the record happens to hold, for the reason
         * 10.8's dimension door gives: `margin: 0` is a gap an author asked not to
         * have, and it is a different answer from "there is no number here". */
        return RoboNotFound;
    }
    *out = margin->length;
    return RoboOk;
}

int LayoutMarginEqual(const LayoutMargin *a, const LayoutMargin *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return 0;
    }
    if (a->kind != RoboLayoutMarginResolved) {
        return 1;
    }
    return a->length == b->length;
}

/* ------------------------------------------------------------------ */
/* The four sides                                                      */
/* ------------------------------------------------------------------ */

/** The longhand each side is read from, indexed by `CssBoxSide`. */
static const char *const layoutMarginProperty[RoboCssBoxSideCount] = {
    "margin-top",
    "margin-right",
    "margin-bottom",
    "margin-left"
};

void LayoutMarginsInit(LayoutMargins *margins)
{
    if (!margins) {
        return;
    }
    margins->top = LayoutMarginAbsent();
    margins->right = LayoutMarginAbsent();
    margins->bottom = LayoutMarginAbsent();
    margins->left = LayoutMarginAbsent();
}

/**
 * Map one classification answer onto a margin kind.
 *
 * The seam's second *shape*: 10.8's classification with signedness opened, and its four
 * answers mapped one-for-one onto the margin vocabulary -- which since 10.9 is a
 * *bijection* rather than a fold. It was not always: the first cut of this reader mapped
 * the classification's `ABSENT` onto a resolved zero, because CSS's initial value for a
 * margin is a number rather than the keyword. The counters forced the split, and the
 * reason is worth keeping beside the code: a reader that cannot tell "nobody declared
 * this" from "this was declared `0`" cannot answer *how many margins the document had*,
 * and `stats.block.marginsApplied` is exactly that question asked once per placed box.
 */
static LayoutMargin LayoutMarginForKind(LayoutStyleValueKind kind,
                                                   double number)
{
    switch (kind) {
    case LAYOUT_STYLE_VALUE_PX:
        /* Rounded through 10.8's door, which 10.9 made genuinely half-away-from-zero
         * so that `margin: -10.5px` is -11 rather than -10: a margin is the first
         * signed length this module rounds, and the door's own doc has promised
         * "half away from zero" since 10.8. */
        return LayoutMarginResolved(LayoutPxRound(number));
    case LAYOUT_STYLE_VALUE_AUTO:
        return LayoutMarginAuto();
    case LAYOUT_STYLE_VALUE_UNUSABLE:
        return LayoutMarginUnresolved();
    case LAYOUT_STYLE_VALUE_ABSENT:
    default:
        return LayoutMarginAbsent();
    }
}

/** Read one side of @p style: the classification, then the mapping above. */
static LayoutMargin LayoutMarginOfProperty(const CssComputedStyle *style,
                                                     const char *property)
{
    LayoutStyleValueKind kind = LAYOUT_STYLE_VALUE_ABSENT;
    double number = 0.0;

    /* A NULL style cannot happen here (the caller has one by construction) but the
     * classification refuses it, and a refusal is an absence rather than a length. */
    if (!LayoutStyleClassify(style, property, 1, &kind, &number)) {
        return LayoutMarginAbsent();
    }
    return LayoutMarginForKind(kind, number);
}

LayoutMargins LayoutMarginsOf(const CssComputedStyle *style)
{
    LayoutMargins margins;

    margins.top = LayoutMarginOfProperty(style, layoutMarginProperty[0]);
    margins.right = LayoutMarginOfProperty(style, layoutMarginProperty[1]);
    margins.bottom = LayoutMarginOfProperty(style, layoutMarginProperty[2]);
    margins.left = LayoutMarginOfProperty(style, layoutMarginProperty[3]);
    return margins;
}

Result LayoutMarginsSide(const LayoutMargins *margins,
                                      CssBoxSide side,
                                      LayoutMargin *out)
{
    if (!margins || !out) {
        return RoboInvalidArgument;
    }
    if (!CssBoxSideIsKnown(side)) {
        return RoboNotFound;
    }
    switch (side) {
    case RoboCssBoxSideTop:
        *out = margins->top;
        return RoboOk;
    case RoboCssBoxSideRight:
        *out = margins->right;
        return RoboOk;
    case RoboCssBoxSideBottom:
        *out = margins->bottom;
        return RoboOk;
    case RoboCssBoxSideLeft:
    default:
        *out = margins->left;
        return RoboOk;
    }
}

int LayoutMarginsEqual(const LayoutMargins *a,
                              const LayoutMargins *b)
{
    if (!a || !b) {
        return a == b;
    }
    return LayoutMarginEqual(&a->top, &b->top) &&
           LayoutMarginEqual(&a->right, &b->right) &&
           LayoutMarginEqual(&a->bottom, &b->bottom) &&
           LayoutMarginEqual(&a->left, &b->left);
}

int LayoutMarginsIsValid(const LayoutMargins *margins)
{
    if (!margins) {
        return 0;
    }
    return LayoutMarginIsValid(&margins->top) &&
           LayoutMarginIsValid(&margins->right) &&
           LayoutMarginIsValid(&margins->bottom) &&
           LayoutMarginIsValid(&margins->left);
}

/* ------------------------------------------------------------------ */
/* The used values                                                     */
/* ------------------------------------------------------------------ */

void LayoutMarginEdgesInit(LayoutMarginEdges *edges,
                                   int32_t containingWidth)
{
    if (!edges) {
        return;
    }
    memset(edges, 0, sizeof(*edges));
    edges->availableContentWidth =
        containingWidth > 0 ? containingWidth : (int32_t)0;
}

int LayoutMarginEdgesIsValid(const LayoutMarginEdges *edges)
{
    if (!edges) {
        return 0;
    }
    if (edges->availableContentWidth < 0) {
        return 0;
    }
    /*
     * The counters have to be a partition of at most four sides, and that is a real
     * check rather than a formality: `resolved` counts sides whose *declaration*
     * reached geometry, so a record claiming five of them is a record some later
     * arithmetic built wrong, and the moment to catch it is before a caller reads a
     * coordinate out of it.
     */
    if (edges->resolved > RoboCssBoxSideCount ||
        edges->autosUsed > RoboCssBoxSideCount ||
        edges->unresolved > RoboCssBoxSideCount) {
        return 0;
    }
    return edges->resolved + edges->autosUsed + edges->unresolved <=
           RoboCssBoxSideCount;
}

int LayoutMarginEdgesEqual(const LayoutMarginEdges *a,
                                  const LayoutMarginEdges *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->top == b->top && a->right == b->right && a->bottom == b->bottom &&
           a->left == b->left &&
           a->availableContentWidth == b->availableContentWidth &&
           a->resolved == b->resolved && a->autosUsed == b->autosUsed &&
           a->unresolved == b->unresolved;
}

Result LayoutMarginEdgesOuterWidth(const LayoutMarginEdges *edges,
                                                  int32_t contentWidth,
                                                  int32_t *out)
{
    if (!edges || !out) {
        return RoboInvalidArgument;
    }
    *out = edges->left + contentWidth + edges->right;
    return RoboOk;
}

/**
 * Use every side that is not a horizontal `auto`: the shared half of both policies.
 *
 * Fills the four used values and the three counters, leaving `left` and `right` at
 * **zero** where the declaration was `auto` -- so a policy that means to distribute
 * finds the non-auto arithmetic already done, and a policy that does not finds the
 * used value it wants sitting there. That is the whole of the sharing: the two
 * policies differ only in what happens to a horizontal `auto` next.
 *
 * `auto` on a vertical side is used as zero *here*, in the shared half, because it is
 * zero under both policies and for two different reasons -- normal flow does not
 * distribute vertically, and an inline participant has no containing block to
 * distribute against.
 */
static void LayoutMarginsUseNonAuto(const LayoutMargins *margins,
                                        LayoutMarginEdges *out)
{
    const LayoutMargin *sides[RoboCssBoxSideCount];
    GfxPixelX *used[RoboCssBoxSideCount];
    size_t index;

    sides[RoboCssBoxSideTop] = &margins->top;
    sides[RoboCssBoxSideRight] = &margins->right;
    sides[RoboCssBoxSideBottom] = &margins->bottom;
    sides[RoboCssBoxSideLeft] = &margins->left;
    used[RoboCssBoxSideTop] = &out->top;
    used[RoboCssBoxSideRight] = &out->right;
    used[RoboCssBoxSideBottom] = &out->bottom;
    used[RoboCssBoxSideLeft] = &out->left;

    for (index = 0u; index < RoboCssBoxSideCount; index++) {
        *used[index] = 0;
        switch (sides[index]->kind) {
        case RoboLayoutMarginResolved:
            *used[index] = sides[index]->length;
            out->resolved++;
            break;
        case RoboLayoutMarginAbsent:
            /* Zero, and **counted nowhere**: the initial value is what the geometry
             * uses, and the counters are about declarations. The distinction survives
             * the switch -- which is the only reason the kind exists. */
            break;
        case RoboLayoutMarginUnresolved:
            /* Used as zero and *counted*, which is 10.8's rule for a declaration it
             * cannot resolve: a document must not fail because of one, and an
             * author's `5%` must not be silently a confident zero nobody can see. */
            out->unresolved++;
            break;
        case RoboLayoutMarginAuto:
        default:
            /* Left at zero for the policy that follows to decide about. */
            break;
        }
    }
}

/** Take the two horizontal margins out of the containing block, once, at the end. */
static void LayoutMarginsFinish(LayoutMarginEdges *edges, int32_t containingWidth)
{
    int64_t available = (int64_t)containingWidth - (int64_t)edges->left -
                        (int64_t)edges->right;

    /*
     * Floored, and floored *here* rather than at each term, because a negative margin
     * is a legal declaration that must come out negative in the edges themselves --
     * the box then overhangs its containing block, which is what CSS does -- while the
     * width left for its contents must never go below zero. One clamp, in one place,
     * on the one number a size arithmetic can act on.
     */
    if (available < 0) {
        available = 0;
    }
    if (available > (int64_t)LAYOUT_STYLE_LENGTH_MAX) {
        available = (int64_t)LAYOUT_STYLE_LENGTH_MAX;
    }
    edges->availableContentWidth = (int32_t)available;
}

Result LayoutMarginsResolveBlock(const LayoutMargins *margins,
                                               int32_t containingWidth,
                                               const LayoutDimension *width,
                                               LayoutMarginEdges *out,
                                               LayoutDiag *diag)
{
    LayoutMarginEdges edges;
    int leftAuto;
    int rightAuto;

    if (!margins || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainMargins, RoboInvalidArgument,
                                "resolving margins needs a record and an out");
    }

    LayoutMarginEdgesInit(&edges, containingWidth);
    LayoutMarginsUseNonAuto(margins, &edges);

    leftAuto = LayoutMarginIsAuto(&margins->left);
    rightAuto = LayoutMarginIsAuto(&margins->right);

    /*
     * The distribution, and the one condition that decides whether there is anything
     * to distribute: the box's width has to be **settled** before the space beside it
     * can be. An `auto` width takes all the space there is, so `auto` margins beside
     * it have nothing left to take -- CSS says their used value is 0 -- and a width
     * this build cannot resolve is treated the same way, because the *fill* rule for
     * it is the same rule.
     */
    if ((leftAuto || rightAuto) && width &&
        LayoutDimensionIsDefinite(width)) {
        int64_t leftover = (int64_t)containingWidth - (int64_t)width->length -
                           (int64_t)edges.left - (int64_t)edges.right;

        if (leftover > 0) {
            if (leftAuto && rightAuto) {
                /*
                 * Split, with an odd pixel going **right**, so a centred box in an odd
                 * leftover sits one pixel left of centre. CSS 2.1 says the two used
                 * values are equal, which integers cannot always do; a build has to
                 * pick, the choice is invisible in every case that does not look for
                 * it, and `layout.margins.block_auto_odd` is the case that looks.
                 */
                int64_t half = leftover / 2;

                edges.left = (GfxPixelX)half;
                edges.right = (GfxPixelX)(leftover - half);
                edges.autosUsed += 2u;
            } else if (leftAuto) {
                edges.left = (GfxPixelX)leftover;
                edges.autosUsed++;
            } else {
                edges.right = (GfxPixelX)leftover;
                edges.autosUsed++;
            }
        }
        /*
         * And when there is no leftover -- an over-constrained box, where the margins
         * and the width already exceed the containing block -- both autos stay at the
         * zero the shared half left them at and the box overflows to the right. CSS
         * resolves this by adjusting `margin-right` and calling the layout
         * over-constrained; this build does not adjust a margin its author did not
         * leave `auto`, because the number it would write is not a number the style
         * asked for and the overflow is 10.13's to report.
         */
    }

    LayoutMarginsFinish(&edges, containingWidth);
    if (!LayoutMarginEdgesIsValid(&edges)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainMargins, RoboError,
                                "a margin resolution produced a record no result can "
                                "hold");
    }
    *out = edges;
    return RoboOk;
}

Result LayoutMarginsResolveInline(const LayoutMargins *margins,
                                                int32_t containingWidth,
                                                LayoutMarginEdges *out,
                                                LayoutDiag *diag)
{
    LayoutMarginEdges edges;

    if (!margins || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainMargins, RoboInvalidArgument,
                                "resolving margins needs a record and an out");
    }

    LayoutMarginEdgesInit(&edges, containingWidth);
    LayoutMarginsUseNonAuto(margins, &edges);

    /*
     * Every `auto` is already zero, and that is the policy rather than an omission:
     * `auto` margins distribute against a containing block in normal flow, and an
     * inline-level participant has no width constraint to be under- or over-. This
     * door exists so a caller cannot accidentally get the block rule by reaching for
     * the only function there is -- and `autosUsed` stays 0 here, so the difference
     * between the two policies is visible in the record rather than only in the
     * numbers.
     */
    LayoutMarginsFinish(&edges, containingWidth);
    *out = edges;
    return RoboOk;
}
