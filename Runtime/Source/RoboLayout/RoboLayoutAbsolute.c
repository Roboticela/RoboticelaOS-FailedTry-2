/*
 * RoboLayout out-of-flow placement (Phase 14.0) — see `RoboLayoutAbsolute.h` for the
 * contract.
 *
 * The file is three readers and one policy, in the order a pass meets them:
 *
 *   **the sides**     a computed style's `left`/`right` (or `top`/`bottom`) become two
 *                     four-kind declarations. It goes through 10.12's own reader
 *                     (`LayoutPositionOf`) rather than reading the properties again, so
 *                     this module and the relative-displacement phase cannot disagree
 *                     about what a style declared;
 *
 *   **the axis**      one function over one axis, and it is where the phase's three rules
 *                     live: the near side wins, the far side positions when the near side
 *                     says nothing, and two present insets size the axis between them when
 *                     the declared dimension does not;
 *
 *   **the placement** the two axes plus the containing block, assembled into the one record
 *                     a pass applies; and the two doors that need the box's **own size** to
 *                     finish the job (`...PlacementOrigin`, `...ContentHeight`), which is the
 *                     whole reason this module is a second call rather than an offset.
 *
 * Why the axis policy is one function and not four
 *
 * The width and the height of an out-of-flow box are answered by the same three rules, so
 * writing them twice would be writing the same rule twice -- and the two places it would
 * drift are the two that matter: whether an over-constrained axis is counted, and whether a
 * far-anchored axis reports an extent (it must not: the extent is the box's own, and the
 * `-1` is what says so).
 *
 * No allocations, no state between calls, no cache: every function below is a function of
 * the records it was handed, exactly as 10.12's are, because a re-layout is this module
 * being asked again.
 */

#include "RoboLayout/RoboLayoutAbsolute.h"

#include "RoboLayoutInternal.h"

/* ------------------------------------------------------------------ */
/* The sides                                                           */
/* ------------------------------------------------------------------ */

LayoutAbsoluteSide LayoutAbsoluteSideAbsent(void)
{
    LayoutAbsoluteSide side;

    side.present = 0;
    side.autoSide = 0;
    side.declared = 0;
    side.length = 0;
    return side;
}

static LayoutAbsoluteSide LayoutAbsoluteSideOfInset(const LayoutInset *inset)
{
    LayoutAbsoluteSide side = LayoutAbsoluteSideAbsent();

    if (!inset) {
        return side;
    }
    /*
     * One conversion and one place it happens: 10.12 classified the declaration into four
     * kinds (`LayoutInsetKind`) and this module asks the same question of it. A kind this
     * build does not know is *not* silently treated as absent -- it is `declared` with no
     * usable length, which is the counted state, because a record from a caller that built
     * one by hand is a fact about the record rather than about the document.
     */
    if (!LayoutInsetKindIsKnown(inset->kind)) {
        side.declared = 1;
        return side;
    }
    side.declared = 1;
    switch (inset->kind) {
    case RoboLayoutInsetResolved:
        side.present = 1;
        side.length = inset->length;
        break;
    case RoboLayoutInsetAuto:
        side.autoSide = 1;
        break;
    case RoboLayoutInsetAbsent:
        side.declared = 0;
        break;
    case RoboLayoutInsetUnresolved:
    default:
        break;
    }
    return side;
}

void LayoutAbsoluteEdgesInit(LayoutAbsoluteEdges *edges)
{
    if (!edges) {
        return;
    }
    edges->near = LayoutAbsoluteSideAbsent();
    edges->far = LayoutAbsoluteSideAbsent();
}

int LayoutAbsoluteSideEqual(const LayoutAbsoluteSide *a,
                                     const LayoutAbsoluteSide *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->present != b->present || a->autoSide != b->autoSide ||
        a->declared != b->declared) {
        return 0;
    }
    /* The length is compared only where it means something, and a side that carries no
     * length is equal to one that carries any -- which is what keeps a cleared record and
     * an `auto` side from comparing unequal over a field neither of them reads. */
    if (a->present && b->present) {
        return a->length == b->length;
    }
    return 1;
}

int LayoutAbsoluteEdgesEqual(const LayoutAbsoluteEdges *a,
                                      const LayoutAbsoluteEdges *b)
{
    if (!a || !b) {
        return a == b;
    }
    return LayoutAbsoluteSideEqual(&a->near, &b->near) &&
           LayoutAbsoluteSideEqual(&a->far, &b->far);
}

int LayoutAbsoluteEdgesIsValid(const LayoutAbsoluteEdges *edges)
{
    if (!edges) {
        return 0;
    }
    /* A side is valid when its three flags do not contradict each other: a usable length
     * and the keyword cannot both be declared, and a length this build honours must be a
     * length it can hold. */
    if (edges->near.present && edges->near.autoSide) {
        return 0;
    }
    if (edges->far.present && edges->far.autoSide) {
        return 0;
    }
    if (edges->near.present && !edges->near.declared) {
        return 0;
    }
    if (edges->far.present && !edges->far.declared) {
        return 0;
    }
    if (edges->near.autoSide && !edges->near.declared) {
        return 0;
    }
    if (edges->far.autoSide && !edges->far.declared) {
        return 0;
    }
    if (edges->near.present && !LayoutInsetIsValid(&(LayoutInset){
                                       RoboLayoutInsetResolved, edges->near.length})) {
        return 0;
    }
    if (edges->far.present && !LayoutInsetIsValid(&(LayoutInset){
                                      RoboLayoutInsetResolved, edges->far.length})) {
        return 0;
    }
    return 1;
}

LayoutAbsoluteEdges LayoutAbsoluteEdgesForAxis(const LayoutPosition *position,
                                                        LayoutAbsoluteAxis axis)
{
    LayoutAbsoluteEdges edges;

    LayoutAbsoluteEdgesInit(&edges);
    if (!position || !LayoutAbsoluteAxisIsKnown(axis)) {
        return edges;
    }
    if (axis == RoboLayoutAbsoluteAxisHorizontal) {
        LayoutInset left;
        LayoutInset right;

        left = LayoutInsetAbsent();
        right = LayoutInsetAbsent();
        if (LayoutInsetsSide(&position->insets, RoboCssBoxSideLeft, &left) != RoboOk) {
            left = LayoutInsetAbsent();
        }
        if (LayoutInsetsSide(&position->insets, RoboCssBoxSideRight, &right) != RoboOk) {
            right = LayoutInsetAbsent();
        }
        edges.near = LayoutAbsoluteSideOfInset(&left);
        edges.far = LayoutAbsoluteSideOfInset(&right);
        return edges;
    }
    {
        LayoutInset top;
        LayoutInset bottom;

        top = LayoutInsetAbsent();
        bottom = LayoutInsetAbsent();
        if (LayoutInsetsSide(&position->insets, RoboCssBoxSideTop, &top) != RoboOk) {
            top = LayoutInsetAbsent();
        }
        if (LayoutInsetsSide(&position->insets, RoboCssBoxSideBottom, &bottom) != RoboOk) {
            bottom = LayoutInsetAbsent();
        }
        edges.near = LayoutAbsoluteSideOfInset(&top);
        edges.far = LayoutAbsoluteSideOfInset(&bottom);
    }
    return edges;
}

LayoutAbsoluteEdges LayoutAbsoluteEdgesOf(const CssComputedStyle *style,
                                                   LayoutAbsoluteAxis axis)
{
    LayoutPosition declared;

    /* One read path: `LayoutPositionOf` is 10.12's own reader over a computed style, and a
     * second reader here would be a second answer to "what did this document declare". */
    LayoutPositionInit(&declared);
    if (!style) {
        return LayoutAbsoluteEdgesForAxis(&declared, axis);
    }
    declared = LayoutPositionOf(style);
    return LayoutAbsoluteEdgesForAxis(&declared, axis);
}

/* ------------------------------------------------------------------ */
/* The containing block                                                */
/* ------------------------------------------------------------------ */

void LayoutAbsoluteContainingBlockInit(LayoutAbsoluteContainingBlock *block)
{
    if (!block) {
        return;
    }
    block->origin.x = 0;
    block->origin.y = 0;
    block->size.width = 0;
    block->size.height = 0;
    block->initial = 0;
}

int LayoutAbsoluteContainingBlockIsValid(
    const LayoutAbsoluteContainingBlock *block)
{
    if (!block) {
        return 0;
    }
    if (block->size.width < 0 || block->size.height < 0) {
        return 0;
    }
    if (block->size.width > LAYOUT_STYLE_LENGTH_MAX ||
        block->size.height > LAYOUT_STYLE_LENGTH_MAX) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* The axis policy                                                     */
/* ------------------------------------------------------------------ */

int LayoutAbsoluteAxisIsKnown(LayoutAbsoluteAxis axis)
{
    return axis == RoboLayoutAbsoluteAxisHorizontal ||
           axis == RoboLayoutAbsoluteAxisVertical;
}

const char *LayoutAbsoluteAxisName(LayoutAbsoluteAxis axis)
{
    switch (axis) {
    case RoboLayoutAbsoluteAxisHorizontal:
        return "horizontal";
    case RoboLayoutAbsoluteAxisVertical:
        return "vertical";
    default:
        break;
    }
    return "unknown";
}

void LayoutAbsoluteAxisUseInit(LayoutAbsoluteAxisUse *use)
{
    if (!use) {
        return;
    }
    LayoutAbsoluteEdgesInit(&use->edges);
    use->sizedByInsets = 0;
    use->marginBoxExtent = -1;
    use->anchorFar = 0;
    use->declaredOverConstrained = 0;
    use->sidesUsed = 0;
    use->sidesAuto = 0;
    use->sidesUnresolved = 0;
}

int LayoutAbsoluteAxisUseIsValid(const LayoutAbsoluteAxisUse *use)
{
    size_t accounted = 0;

    if (!use) {
        return 0;
    }
    if (!LayoutAbsoluteEdgesIsValid(&use->edges)) {
        return 0;
    }
    if (use->sizedByInsets) {
        /* A stretched axis has an extent, and an extent is never negative. */
        if (use->marginBoxExtent < 0) {
            return 0;
        }
    } else if (use->marginBoxExtent != -1) {
        /* The `-1` is the value that means "the box's own size decides"; anything else
         * with the flag clear is a record that disagrees with itself. */
        return 0;
    }
    if (use->sidesUsed + use->sidesAuto + use->sidesUnresolved > 2u) {
        return 0;
    }
    /* The three counters partition the two sides, so the remainder is the absent ones --
     * the identity 10.9's, 10.10's, 10.11's and 10.12's records each close. */
    accounted = use->sidesUsed + use->sidesAuto + use->sidesUnresolved;
    if (accounted > 2u) {
        return 0;
    }
    return 1;
}

int LayoutAbsoluteAxisUseEqual(const LayoutAbsoluteAxisUse *a,
                                        const LayoutAbsoluteAxisUse *b)
{
    if (!a || !b) {
        return a == b;
    }
    return LayoutAbsoluteEdgesEqual(&a->edges, &b->edges) &&
           a->sizedByInsets == b->sizedByInsets &&
           a->marginBoxExtent == b->marginBoxExtent &&
           a->anchorFar == b->anchorFar &&
           a->declaredOverConstrained == b->declaredOverConstrained &&
           a->sidesUsed == b->sidesUsed && a->sidesAuto == b->sidesAuto &&
           a->sidesUnresolved == b->sidesUnresolved;
}

static void LayoutAbsoluteCountSides(const LayoutAbsoluteEdges *edges,
                                     LayoutAbsoluteAxisUse *use)
{
    const LayoutAbsoluteSide *sides[2];
    size_t index = 0u;

    sides[0] = &edges->near;
    sides[1] = &edges->far;
    for (index = 0u; index < 2u; index++) {
        const LayoutAbsoluteSide *side = sides[index];

        if (!side->declared) {
            continue;
        }
        if (side->present) {
            use->sidesUsed++;
        } else if (side->autoSide) {
            use->sidesAuto++;
        } else {
            use->sidesUnresolved++;
        }
    }
}

Result LayoutAbsoluteResolveAxis(const LayoutPosition *position,
                                            LayoutAbsoluteAxis axis,
                                            const LayoutDimension *declared,
                                            int32_t containingBlockExtent,
                                            LayoutAbsoluteAxisUse *out,
                                            LayoutDiag *diag)
{
    int definite = 0;

    if (!position || !out || !LayoutAbsoluteAxisIsKnown(axis)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "out-of-flow resolution needs a position, an axis and an out");
    }
    if (!LayoutPositionIsValid(position)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "the position record is not one this build may read");
    }
    if (containingBlockExtent < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "a containing block extent is never negative");
    }
    if (declared && !LayoutDimensionIsValid(declared)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "the declared dimension is not a record this build may read");
    }

    LayoutAbsoluteAxisUseInit(out);
    out->edges = LayoutAbsoluteEdgesForAxis(position, axis);
    LayoutAbsoluteCountSides(&out->edges, out);

    /* A NULL record is CSS's initial value rather than an omission: the box declared
     * nothing, and nothing is `auto` -- which is not definite. */
    definite = declared && LayoutDimensionIsDefinite(declared);

    if (out->edges.near.present) {
        /*
         * The near side positions this axis, whatever the far side says. The far side is
         * then either *ignored* (the usual case: the box's size decides where it ends) or
         * **over-constrained** when the box also declared a definite size -- CSS's
         * left-to-right answer, and this build's, stated in the header.
         */
        if (out->edges.far.present && definite) {
            out->declaredOverConstrained = 1;
        }
    } else if (out->edges.far.present) {
        /*
         * Nothing on the near side (`absent` or `auto`), so the far side positions the box.
         * The used *extent* is the box's own and is deliberately not computed here: that is
         * the whole reason `LayoutAbsolutePlacementOrigin` exists.
         */
        out->anchorFar = 1;
    }

    if (out->edges.near.present && out->edges.far.present && !definite) {
        /*
         * Two present insets and no definite declared dimension: the insets size the axis
         * between them. This is `left: 0; right: 0` on a `width: auto` box -- the shape the
         * desktop shell's wallpaper, menu bar and dock are all written in -- and a build
         * that read it as "fill the containing block" agrees with it only when both insets
         * are zero.
         */
        out->sizedByInsets = 1;
        out->marginBoxExtent = LayoutExtentClamp((int64_t)containingBlockExtent -
                                                 (int64_t)out->edges.near.length -
                                                 (int64_t)out->edges.far.length);
        if (out->marginBoxExtent < 0) {
            /* A box's extent is never negative: insets larger than the containing block
             * leave no room rather than an impossible size, which is the floor 10.10 gives
             * a padding box narrower than its own padding. */
            out->marginBoxExtent = 0;
        }
    }

    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The placement                                                       */
/* ------------------------------------------------------------------ */

int LayoutAbsolutePlacementIsValid(const LayoutAbsolutePlacement *placement)
{
    if (!placement) {
        return 0;
    }
    if (!LayoutAbsoluteContainingBlockIsValid(&placement->containingBlock)) {
        return 0;
    }
    if (!LayoutAbsoluteEdgesIsValid(&placement->horizontal) ||
        !LayoutAbsoluteEdgesIsValid(&placement->vertical)) {
        return 0;
    }
    if (!CssPositioningIsKnown(placement->mode)) {
        return 0;
    }
    if (placement->availableWidth < 0) {
        return 0;
    }
    if (placement->sizedByInsetsY) {
        if (placement->marginBoxHeight < 0) {
            return 0;
        }
    } else if (placement->marginBoxHeight != -1) {
        return 0;
    }
    return 1;
}

int LayoutAbsolutePlacementEqual(const LayoutAbsolutePlacement *a,
                                         const LayoutAbsolutePlacement *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->containingBlock.origin.x == b->containingBlock.origin.x &&
           a->containingBlock.origin.y == b->containingBlock.origin.y &&
           a->containingBlock.size.width == b->containingBlock.size.width &&
           a->containingBlock.size.height == b->containingBlock.size.height &&
           a->containingBlock.initial == b->containingBlock.initial &&
           LayoutAbsoluteEdgesEqual(&a->horizontal, &b->horizontal) &&
           LayoutAbsoluteEdgesEqual(&a->vertical, &b->vertical) &&
           a->anchorFarX == b->anchorFarX && a->anchorFarY == b->anchorFarY &&
           a->sizedByInsetsX == b->sizedByInsetsX &&
           a->sizedByInsetsY == b->sizedByInsetsY &&
           a->marginBoxHeight == b->marginBoxHeight &&
           a->overConstrainedX == b->overConstrainedX &&
           a->overConstrainedY == b->overConstrainedY && a->mode == b->mode &&
           a->availableWidth == b->availableWidth;
}

Result LayoutAbsolutePlacementBuild(const LayoutPosition *position,
                                              const LayoutAbsoluteContainingBlock *containingBlock,
                                              const LayoutDimension *declaredWidth,
                                              const LayoutDimension *declaredHeight,
                                              LayoutAbsolutePlacement *out,
                                              struct LayoutAbsoluteCounters *counters,
                                              LayoutDiag *diag)
{
    LayoutAbsoluteAxisUse horizontal;
    LayoutAbsoluteAxisUse vertical;
    int64_t available = 0;
    Result status;

    if (!position || !containingBlock || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "building a placement needs a position, a containing block and an out");
    }
    if (!LayoutAbsoluteContainingBlockIsValid(containingBlock)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "the containing block is not a rectangle this build may read");
    }
    if (!LayoutAbsoluteIsOutOfFlow(position)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "only an out-of-flow box is placed against a containing block");
    }

    status = LayoutAbsoluteResolveAxis(position, RoboLayoutAbsoluteAxisHorizontal,
                                                 declaredWidth, containingBlock->size.width,
                                                 &horizontal, diag);
    if (status != RoboOk) {
        return status;
    }
    status = LayoutAbsoluteResolveAxis(position, RoboLayoutAbsoluteAxisVertical,
                                                 declaredHeight, containingBlock->size.height,
                                                 &vertical, diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * The width the contents are laid out into, and it is the *same* number an in-flow box
     * gets: the containing block less whatever the insets claim. The margins, the border and
     * the padding are subtracted by the pass -- which is why a `width: auto` box that fills
     * this ends up exactly as wide as the containing block less its two insets, CSS's
     * stretch, with no second rule here to keep in step with the first.
     */
    available = (int64_t)containingBlock->size.width;
    if (horizontal.edges.near.present) {
        available -= (int64_t)horizontal.edges.near.length;
    }
    if (horizontal.edges.far.present) {
        available -= (int64_t)horizontal.edges.far.length;
    }
    if (available < 0) {
        /* As above: insets larger than the containing block leave no room rather than a
         * negative width, and a width nothing can be laid out into is zero. */
        available = 0;
    }

    out->containingBlock = *containingBlock;
    out->horizontal = horizontal.edges;
    out->vertical = vertical.edges;
    out->anchorFarX = horizontal.anchorFar;
    out->anchorFarY = vertical.anchorFar;
    out->sizedByInsetsX = horizontal.sizedByInsets;
    out->sizedByInsetsY = vertical.sizedByInsets;
    out->marginBoxHeight = vertical.marginBoxExtent;
    out->overConstrainedX = horizontal.declaredOverConstrained;
    out->overConstrainedY = vertical.declaredOverConstrained;
    out->mode = position->mode;
    out->availableWidth = LayoutExtentClamp(available);

    if (counters) {
        counters->sidesUsed += horizontal.sidesUsed + vertical.sidesUsed;
        counters->sidesAuto += horizontal.sidesAuto + vertical.sidesAuto;
        counters->sidesUnresolved +=
            horizontal.sidesUnresolved + vertical.sidesUnresolved;
        counters->axesFromInsets +=
            (horizontal.sizedByInsets ? 1u : 0u) + (vertical.sizedByInsets ? 1u : 0u);
        counters->axesAnchoredFar +=
            (horizontal.anchorFar ? 1u : 0u) + (vertical.anchorFar ? 1u : 0u);
    }
    return RoboOk;
}

Result LayoutAbsolutePlacementOrigin(const LayoutAbsolutePlacement *placement,
                                                  LayoutSize marginBox,
                                                  LayoutPoint *out,
                                                  LayoutDiag *diag)
{
    int64_t x = 0;
    int64_t y = 0;

    if (!placement || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "a placement origin needs a placement and an out");
    }
    if (!LayoutAbsolutePlacementIsValid(placement)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "the placement is not a record this build may read");
    }
    if (marginBox.width < 0 || marginBox.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "a margin box is never negative");
    }

    /*
     * The near axis is a displacement from the containing block's origin and the far axis is
     * a *distance back* from its far edge, which is why the two branches subtract different
     * things: `right: 8px` means "my right edge is 8px in from the containing block's right
     * edge", and the box's own width is what turns that into a coordinate.
     *
     * Both terms are bounded and the sum is not (10.15's rule), so the arithmetic is 64-bit
     * and the answer is clamped rather than wrapped.
     */
    x = (int64_t)placement->containingBlock.origin.x;
    if (placement->anchorFarX) {
        x += (int64_t)placement->containingBlock.size.width -
             (int64_t)placement->horizontal.far.length - (int64_t)marginBox.width;
    } else if (placement->horizontal.near.present) {
        x += (int64_t)placement->horizontal.near.length;
    }

    y = (int64_t)placement->containingBlock.origin.y;
    if (placement->anchorFarY) {
        y += (int64_t)placement->containingBlock.size.height -
             (int64_t)placement->vertical.far.length - (int64_t)marginBox.height;
    } else if (placement->vertical.near.present) {
        y += (int64_t)placement->vertical.near.length;
    }

    out->x = LayoutExtentClamp(x);
    out->y = LayoutExtentClamp(y);
    return RoboOk;
}

Result LayoutAbsoluteContentHeight(const LayoutAbsolutePlacement *placement,
                                              int32_t marginsAndInsets,
                                              int32_t *out,
                                              LayoutDiag *diag)
{
    int64_t height = 0;

    if (!placement || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "a stretched height needs a placement and an out");
    }
    if (!LayoutAbsolutePlacementIsValid(placement)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "the placement is not a record this build may read");
    }
    if (!placement->sizedByInsetsY) {
        /* Asked of an axis whose insets did not size it: there is no number to convert, and
         * answering one would hide a caller bug behind a plausible height. */
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboNotFound,
                                "this placement's height is not sized by its insets");
    }
    if (marginsAndInsets < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainAbsolute, RoboInvalidArgument,
                                "the insets a content height is derived from are never negative");
    }

    /*
     * The margin box less every inset the pass has already resolved, floored at zero: the
     * box's `top`/`bottom` size its **margin** box (that is the basis CSS's constraint
     * equation is written on), and the content box is what is left after the two margins, the
     * two borders and the two paddings -- the same subtraction the pass makes for an in-flow
     * box, in the one place it is not already being made.
     */
    height = (int64_t)placement->marginBoxHeight - (int64_t)marginsAndInsets;
    if (height < 0) {
        height = 0;
    }
    *out = LayoutExtentClamp(height);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The counters                                                        */
/* ------------------------------------------------------------------ */

void LayoutAbsoluteCountersInit(LayoutAbsoluteCounters *counters)
{
    if (!counters) {
        return;
    }
    counters->boxesOutOfFlow = 0u;
    counters->sidesUsed = 0u;
    counters->sidesAuto = 0u;
    counters->sidesUnresolved = 0u;
    counters->axesFromInsets = 0u;
    counters->axesAnchoredFar = 0u;
    counters->viewportAnchored = 0u;
}

int LayoutAbsoluteCountersEqual(const LayoutAbsoluteCounters *a,
                                         const LayoutAbsoluteCounters *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->boxesOutOfFlow == b->boxesOutOfFlow &&
           a->sidesUsed == b->sidesUsed && a->sidesAuto == b->sidesAuto &&
           a->sidesUnresolved == b->sidesUnresolved &&
           a->axesFromInsets == b->axesFromInsets &&
           a->axesAnchoredFar == b->axesAnchoredFar &&
           a->viewportAnchored == b->viewportAnchored;
}

/* ------------------------------------------------------------------ */
/* The two classification questions                                    */
/* ------------------------------------------------------------------ */

int LayoutAbsoluteIsOutOfFlow(const LayoutPosition *position)
{
    if (!position || !LayoutPositionIsValid(position)) {
        return 0;
    }
    /* 9.13's own fact rather than a second rule, and the NULL case above is CSS's initial
     * value: a box that declared no `position` is `static`, and a static box is in flow. */
    return CssPositioningIsOutOfFlow(position->mode);
}

int LayoutAbsoluteIsViewportAnchored(const LayoutPosition *position)
{
    if (!position || !LayoutPositionIsValid(position)) {
        return 0;
    }
    return position->mode == RoboCssPositioningFixed;
}
