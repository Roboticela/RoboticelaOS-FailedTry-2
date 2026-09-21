/*
 * RoboLayout block layout (Phase 10.3) — see RoboLayoutBlock.h.
 *
 * One recursion, three questions per box, and the header argues each of them:
 * what is my containing block, how wide am I, how tall am I. The file is short
 * because the phase is narrow, and it is written as a recursion rather than a
 * loop over the flat arena because the two facts a box needs are its parent's
 * *origin* and its parent's *content width* -- neither of which is on the box --
 * so a flat walk would have to recompute them or keep a stack, which is what the
 * call stack is.
 *
 * Everything here is a pure function of the tree, the viewport and the borrowed
 * styles: nothing is cached between calls, nothing reads a previous pass, and the
 * traversal order is the tree's own -- creation order for boxes, sibling order for
 * children. That is what makes the pass deterministic (the header's last section)
 * and it is why there is no accumulator struct threaded through the recursion
 * beyond the stats record, which is a count and not an input.
 *
 * The one place this file could quietly go wrong is the ordering of the writes.
 * A box's height is not known until its children are laid out, so its own geometry
 * is written *after* theirs -- and a reader that expected parents-before-children
 * would be reading a box whose result is still UNRESOLVED while its children's are
 * COMPUTED. That is stated in the header rather than papered over by writing a
 * provisional geometry first: a provisional `(0, 0)` is exactly the lie 10.1 built
 * the result *state* to avoid, and writing one would make `UNRESOLVED` mean "not
 * yet" instead of "not known".
 *
 * 10.8 changed what 10.3 *passed* here, and the change is one line with a long
 * shadow. Where this recursion used to hand every child its containing block's width,
 * it now hands it the box's **resolved** width (`RoboLayoutSizing.h`), because a box
 * that declares `width: 300px` is its children's containing block and 300 is what
 * they are laid out into. For an `auto` width the two are the same number -- which is
 * why every 10.3 expectation about an undeclared width still holds -- and the
 * recursion's *shape* is untouched: the width resolves first because it depends on
 * nothing, the children go in at it, and the height follows because it depends on
 * them. That ordering is what the two dimension resolvers exist for.
 */

#include "RoboLayout/RoboLayoutBlock.h"

#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSDisplay.h"

#include "RoboLayout/RoboLayoutSizing.h"

#include "RoboLayout/RoboLayoutFormattingContext.h"
#include "RoboLayout/RoboLayoutObject.h"
#include "RoboLayout/RoboLayoutResult.h"

#include "RoboLayoutInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* What a box's declarations ask for                                   */
/* ------------------------------------------------------------------ */

/**
 * The px length @p style names for @p property, when it names a usable one.
 *
 * Written on top of `LayoutSizingReadDimension` rather than beside it, and
 * that is the phase: the four checks that used to live here -- an entry that exists,
 * a kind that is a length, a unit that is `px`, a number that is finite, 
 * non-negative and inside the geometry's range -- are now one classification in
 * RoboLayoutSizing.c. What is left here is the *bound* reading's shape: "a bound, or
 * none", where None covers `auto` and every value this build cannot resolve.
 */
static int LayoutSizingBoundPx(const CssComputedStyle *style,
                                  const char *property,
                                  GfxPixelX *out)
{
    LayoutDimension dimension = LayoutDimensionAuto();
    GfxPixelX length = 0;

    if (!LayoutSizingReadDimension(style, property, &dimension)) {
        return 0;
    }
    if (LayoutDimensionLength(&dimension, &length) != RoboOk) {
        return 0;
    }
    *out = length;
    return 1;
}

/**
 * Fill @p sizing from @p object: what its containing block offers, what its
 * declarations ask for, and what its bounds allow.
 *
 * Defined here and called from both directions (RoboLayoutInternal.h), because the
 * question -- "what does *this* box's style ask for, into this containing block?" --
 * is the same one for a block box and for an atomic inline, and two implementations
 * of it would answer differently the day either gained a property read.
 *
 * Three reads, and the two dimension ones are deliberately unguarded. A box with no
 * computed style has made no declarations, and the record `init` wrote already says
 * `AUTO`; `LayoutSizingReadDimension` fills only when it can answer, so the
 * unguarded form is the *contract* rather than a shortcut.
 *
 * Only the *width* is offered, and that asymmetry is CSS's: a block box's height is
 * its contents', never its containing block's, so there is no available height to
 * record. `hasAvailableHeight` exists for the goal that gives `height: 50%`
 * something to resolve against, and leaving it clear is how this build says "nobody
 * has told me, and I am not going to assume zero".
 *
 * The bounds are read through the same seam as the dimensions, and RoboCSS registers
 * no `min-width`, so for every document this build can parse there is no entry and no
 * bound recorded. That is not a stub: the clamping path is live and tested through
 * the sizing record directly, and the day the property table knows those names a
 * style that declares them reaches it here with no other change. A bound is recorded
 * only when **both** of its dimensions name a usable length, because 10.1's
 * constraints record holds a bound as one size and a half-known pair would silently
 * bound the other dimension to whatever the unknown half defaulted to -- a
 * `max-width` alone turning every height into zero.
 */
void LayoutObjectSizing(LayoutSizing *sizing,
                               const LayoutObject *object,
                               int32_t availableWidth)
{
    GfxPixelX minimumWidth = 0;
    GfxPixelX minimumHeight = 0;
    GfxPixelX maximumWidth = 0;
    GfxPixelX maximumHeight = 0;
    LayoutSize bound;

    LayoutSizingInit(sizing);
    (void)LayoutSizingSetAvailableWidth(sizing, availableWidth, NULL);

    (void)LayoutSizingReadDimension(object->style, "width", &sizing->width);
    (void)LayoutSizingReadDimension(object->style, "height", &sizing->height);

    if (LayoutSizingBoundPx(object->style, "min-width", &minimumWidth) &&
        LayoutSizingBoundPx(object->style, "min-height", &minimumHeight)) {
        bound.width = minimumWidth;
        bound.height = minimumHeight;
        (void)LayoutSizingSetMinimum(sizing, bound, NULL);
    }
    if (LayoutSizingBoundPx(object->style, "max-width", &maximumWidth) &&
        LayoutSizingBoundPx(object->style, "max-height", &maximumHeight)) {
        bound.width = maximumWidth;
        bound.height = maximumHeight;
        (void)LayoutSizingSetMaximum(sizing, bound, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* The recursion                                                       */
/* ------------------------------------------------------------------ */

static Result LayoutBlockBox(LayoutWalk *walk,
                                      LayoutNodeId id,
                                      LayoutPoint origin,
                                      int32_t availableWidth,
                                      GfxPixelY *outHeight);

/**
 * Whether this pass places @p object, and the reason when it does not.
 *
 * The three-way answer is the whole placement rule, and the two rejections are
 * *different facts* that a single boolean would merge: "10.4 lays this out along a
 * line" is a normal document, and "this is block-level but the context it
 * participates in is not a block-direction one" is a contradiction between 10.2's
 * classification and CSS that cannot happen.
 */
typedef enum layoutBlockVerdict {
    /** Place it: block-level, in a block-direction context. */
    LAYOUT_BLOCK_PLACE = 0,
    /** Inline-level, or in an inline context. 10.4 owns it. */
    LAYOUT_BLOCK_INLINE_LEVEL = 1,
    /** Block-level but not in a block-direction context. Impossible; reported. */
    LAYOUT_BLOCK_CONTRADICTION = 2
} LayoutBlockVerdict;

static LayoutBlockVerdict LayoutBlockVerdictFor(const LayoutTree *tree,
                                                      const LayoutObject *object)
{
    LayoutFormattingContext context;
    int inBlockFlow = 0;

    if (!CssDisplayIsBlockLevel(object->display)) {
        return LAYOUT_BLOCK_INLINE_LEVEL;
    }
    /*
     * The gate 10.2 exists to provide. Asked of the context the box *participates*
     * in, not the one it owns: a box establishes a context for its contents, and
     * its own layout happens in somebody else's.
     */
    if (LayoutTreeContext(tree, object->context, &context, NULL) == RoboOk) {
        inBlockFlow = LayoutFcKindIsBlockContainer(context.kind);
    }
    return inBlockFlow ? LAYOUT_BLOCK_PLACE : LAYOUT_BLOCK_CONTRADICTION;
}

/**
 * Stack @p id's block-level children from its content origin and report the
 * extent they reached.
 *
 * The cursor is local, so it is the *content* extent and not a coordinate: the
 * caller has already decided where this box's content origin is, and adding it
 * here would put the origin in two places.
 *
 * Children are visited in sibling order, which the builder made document order.
 * A child that is not placed is counted and **not descended into**: an inline box
 * has no geometry in this build, so a descendant placed inside it would be placed
 * inside nothing. Those descendants are counted as unreached by the caller's
 * difference rather than walked to be counted, which keeps the pass linear in the
 * boxes it places rather than in the boxes that exist.
 */
static Result LayoutBlockChildren(LayoutWalk *walk,
                                           const LayoutObject *object,
                                           int32_t contentWidth,
                                           GfxPixelY *outExtent)
{
    LayoutNodeId child = object->firstChild;
    /*
     * 64 bits, and clamped only on the way out (Phase 10.15). Two children each near
     * the geometry's maximum sum past any `int32`, and a cursor that wrapped would be a
     * *negative* extent -- which the sizing layer refuses, so the pass would fail rather
     * than saturate. The bound and the rule are the ones the inset layers already use
     * (`LayoutExtentClamp`, RoboLayoutInternal.h).
     */
    int64_t cursor = 0;
    int placedAny = 0;

    /*
     * The inline direction first, because it is a different *question* about the
     * same box and not a different kind of child: when the box established an
     * INLINE formatting context, its contents are participants of a line and 10.4
     * owns every one of them. The distinction is 10.2's, read here through the
     * context the box owns -- which is the same door 10.2's classifier wrote, so
     * there is no second rule about which boxes have lines in them.
     *
     * A box that establishes an inline context has no block-level child, so the
     * block direction has nothing to stack and the `emptyContainers` count below is
     * exactly what the loop would have produced: the extent this call hands back is
     * the lines' instead of a cursor's, and the box's own height rule (declared, or
     * the content's extent) is unchanged.
     */
    if (object->contextOwned != RoboLayoutFcIdNone) {
        LayoutFormattingContext owned;

        if (LayoutTreeContext(walk->tree, object->contextOwned, &owned, NULL) ==
                RoboOk &&
            owned.kind == RoboLayoutFcInline) {
            walk->block->emptyContainers++;
            return LayoutInlineWalkContents(walk, object->id, contentWidth,
                                                    outExtent);
        }
    }

    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *childObject = LayoutObjectSlotConst(walk->tree,
                                                                                child);

        if (!childObject) {
            return RoboLayoutDiag(walk->diag, RoboLayoutDomainBlockLayout, RoboError,
                                    "a box's child link names no box in this tree");
        }

        switch (LayoutBlockVerdictFor(walk->tree, childObject)) {
        case LAYOUT_BLOCK_PLACE: {
            LayoutPoint childOrigin;
            GfxPixelY childHeight = 0;

            childOrigin.x = 0;
            childOrigin.y = LayoutExtentClamp(cursor);
            if (LayoutBlockBox(walk, child, childOrigin, contentWidth, &childHeight) !=
                RoboOk) {
                return walk->diag ? walk->diag->code : RoboError;
            }
            cursor += (int64_t)childHeight;
            placedAny = 1;
            break;
        }
        case LAYOUT_BLOCK_INLINE_LEVEL:
            /*
             * Inline-level, in a **block-direction** context — so there is no line
             * to put it on and this is the mixed-content case the header describes:
             * CSS wraps the inline run in an anonymous block box, this build makes
             * none, and the honest answer is to count it and leave it UNRESOLVED.
             * The inline pass never sees one of these, because it walks the contents
             * of inline contexts and this box is not in one.
             *
             * A **text run** takes this branch too, and that is the same case rather
             * than a second one: text is inline-level by construction
             * (RoboLayoutObject.h), so a text node between two blocks is a run with no
             * line to be on, and it waits for the same anonymous box. Counting it
             * here — rather than measuring it and then having nowhere to put the
             * measurement — is what keeps `unreached` a statement about *structure*
             * and not a bucket of things nobody got round to.
             */
            walk->block->inlineLevelSkipped++;
            break;
        case LAYOUT_BLOCK_CONTRADICTION:
        default:
            return RoboLayoutDiag(
                walk->diag, RoboLayoutDomainBlockLayout, RoboError,
                "a block-level box participates in a context that is not a block "
                "formatting context");
        }

        child = childObject->NextSibling;
    }

    if (placedAny) {
        walk->block->containers++;
    } else {
        walk->block->emptyContainers++;
    }
    *outExtent = LayoutExtentClamp(cursor);
    return RoboOk;
}

/**
 * Lay out @p owner's contents in the block direction — the door the inline pass
 * calls for an atomic inline's contents (RoboLayoutInternal.h).
 *
 * A box is looked up and the child loop is the one above: an `inline-block`'s
 * contents are an ordinary block formatting context (10.2's rule for `flow-root`),
 * so there is no second walk and no second set of counters.
 */
Result LayoutBlockWalkChildren(LayoutWalk *walk,
                                              LayoutNodeId owner,
                                              int32_t contentWidth,
                                              GfxPixelY *outExtent)
{
    const LayoutObject *object;

    if (!walk || !walk->tree || !walk->block || !outExtent) {
        return RoboLayoutDiag(walk ? walk->diag : NULL, RoboLayoutDomainBlockLayout,
                                RoboInvalidArgument, "the pass needs a walk and an out");
    }
    object = LayoutObjectSlotConst(walk->tree, owner);
    if (!object) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainBlockLayout, RoboError,
                                "the box that owns a block context is not in the tree");
    }
    return LayoutBlockChildren(walk, object, contentWidth, outExtent);
}

/**
 * Place one block-level box at @p origin, lay its contents out, and report its
 * height.
 *
 * @p origin is in the box's *containing block's* space, which is the parent's
 * content origin -- so a child's position is `(0, cursor)` and the parent's
 * absolute position is already accounted for one level up. That is the same
 * convention `LayoutResult.position` documents, and it is why no
 * coordinate in this file is ever a document-space one.
 *
 * @p availableWidth is the containing block's *content* width, which today is
 * the same number as its border-box width because padding and borders are not
 * applied (10.10, 10.11). It is passed as `int32_t` rather than as a size because
 * a box has no height available to it in this phase: a block box's height is
 * decided by its contents, never by its containing block.
 */
static Result LayoutBlockBox(LayoutWalk *walk,
                                      LayoutNodeId id,
                                      LayoutPoint origin,
                                      int32_t availableWidth,
                                      GfxPixelY *outHeight)
{
    LayoutObject *object = LayoutObjectSlot(walk->tree, id);
    LayoutSizing sizing;
    LayoutMarginEdges edges;
    LayoutBorders declaredBorder;
    LayoutBorderEdges borders;
    LayoutPaddings declaredPadding;
    LayoutPaddingEdges padding;
    LayoutPosition declaredPosition;
    LayoutPositionOffset positionOffset;
    LayoutSize content;
    LayoutSize contentSize;
    LayoutSize paddingBox;
    LayoutSize size;
    LayoutPoint marginOrigin;
    LayoutPoint finalOrigin;
    GfxPixelY contentExtent = 0;
    Result status;

    if (!object) {
        return RoboLayoutDiag(walk->diag, RoboLayoutDomainBlockLayout, RoboError,
                                "a box vanished under the pass");
    }

    LayoutObjectSizing(&sizing, object, availableWidth);

    /*
     * The margins next (10.9), and *before* the width for a reason that is forced
     * rather than tidy: an `auto` width fills whatever the margins leave, so the two
     * horizontal margins have to be settled before there is a number to fill. The
     * declared dimension is what the margin arithmetic reads to decide whether there
     * is anything to distribute -- `auto` margins have nothing to take beside an
     * `auto` width -- which is why the sizing record is built first and *not* resolved
     * yet.
     *
     * The containing block a margin resolves against is the one this box's width
     * resolves against: 10.3's rule that a box's containing block is its parent, with
     * the viewport standing in for the root. One number, two uses, and the second use
     * is the one whose answer the first has to wait for.
     */
    {
        LayoutMargins declared = LayoutMarginsOf(object->style);

        status = LayoutMarginsResolveBlock(&declared, availableWidth, &sizing.width,
                                                   &edges, walk->diag);
        if (status != RoboOk) {
            return status;
        }
    }

    /*
     * Then the box's own padding (10.10), and here it is *not* an ordering decision the
     * way the margins were: a padding neither distributes nor resolves against the
     * containing block, so it is read from the style and used, and the one door it has is
     * the one policy. What it does change is the number the width is resolved against, and
     * that is the next step rather than this one.
     */
    /*
     * Then the box's two insets (10.10, 10.11), and neither is an ordering decision the
     * way the margins were: a border and a padding distribute nothing and resolve against
     * nothing, so both are read from the style and used, and each has one door rather than
     * two policies. What they change is the number the width is resolved against, which is
     * the next step rather than this one.
     *
     * The border resolves first only because it is the outer of the two -- the two calls are
     * independent, and a reader following the arithmetic down the box wants the order the
     * box is built in.
     */
    declaredBorder = LayoutBordersOf(object->style);
    status = LayoutBorderResolve(&declaredBorder, &borders, walk->diag);
    if (status != RoboOk) {
        return status;
    }
    declaredPadding = LayoutPaddingsOf(object->style);
    status = LayoutPaddingResolve(&declaredPadding, &padding, walk->diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * What the box's own box is resolved against, and the phase's first consequence:
     * the containing block less the margins (10.9), less the **border** (10.11) and less
     * the padding (10.10), because CSS's constraint equation has a term for each. So a box
     * with `border: 5px solid; padding: 10px` and no declared width is exactly as wide as
     * its containing block rather than 30px wider, and both insets come out of the
     * contents' room instead of out of the parent.
     *
     * One subtraction per layer, in the order the box is built, and each subtraction belongs
     * to the layer that owns the property: the border layer turns the containing block into
     * the **padding box** width and the padding layer turns that into the **content** width.
     * It happens *here* rather than inside the sizing layer, because the sizing layer does
     * not know either property exists -- "how much room is there" is the caller's question
     * and the answer is a number.
     */
    {
        int32_t paddingBoxAvailable = 0;
        int32_t contentAvailable = 0;

        status = LayoutBorderEdgesPaddingBoxWidth(&borders,
                                                           edges.availableContentWidth,
                                                           &paddingBoxAvailable, walk->diag);
        if (status != RoboOk) {
            return status;
        }
        status = LayoutPaddingEdgesContentWidth(&padding,
                                                        paddingBoxAvailable,
                                                        &contentAvailable, walk->diag);
        if (status != RoboOk) {
            return status;
        }
        status = LayoutSizingSetAvailableWidth(&sizing, contentAvailable,
                                                       walk->diag);
        if (status != RoboOk) {
            return status;
        }
    }

    /*
     * The width next (10.8), because the children are laid out *into* this box's
     * content box and so the number they get is the resolved one rather than the
     * containing block's. That is the difference the phase adds, and it is one line:
     * for an `auto` width the two are the same number, because `auto` fills the
     * containing block, and every 10.3 expectation about a box that declares no width
     * is therefore untouched.
     *
     * And what the number *is* is 10.10's question, whose answer is one word:
     * `content-box`. This build is CSS's initial `box-sizing` value, so a declared
     * `width` is the **content** box and the resolved width is exactly what this box's
     * children are laid out into -- unchanged from 10.8, and unchanged *because* of the
     * model rather than in spite of it. The border box a result carries is that number
     * plus the padding, derived below. A build that had read the declaration as a border
     * box would subtract here and give the children less than their author asked for.
     */
    status = LayoutSizingResolveWidth(&sizing, walk->diag);
    if (status != RoboOk) {
        return status;
    }

    /*
     * The children next, because an `auto` height *is* their extent -- and because if
     * this box declares a height, the children still have to be laid out inside it.
     */
    status = LayoutBlockChildren(walk, object, (int32_t)sizing.resolved.width,
                                   &contentExtent);
    if (status != RoboOk) {
        return status;
    }

    /*
     * Then the height, from what the contents turned out to be. The extent is
     * reported at the *resolved* width as well as the stacked height, because a
     * contents extent is what would hold the children and a later goal that grows a
     * child beyond its parent needs both numbers to say so.
     */
    content.width = sizing.resolved.width;
    content.height = contentExtent;
    status = LayoutSizingSetContent(&sizing, content, walk->diag);
    if (status != RoboOk) {
        return status;
    }
    status = LayoutSizingResolveHeight(&sizing, walk->diag);
    if (status != RoboOk) {
        return status;
    }
    /*
     * And the resolved pair is the box's **content** box, which is what the sizing layer
     * knows about; the box a reader sees is that plus the padding and the border. That split
     * is the phase in two lines -- three since 10.11 -- and it is why the numbers are
     * computed here rather than inside the layer: the layer answers "how big did this box's
     * declarations ask it to be", and "how big is the box" is the answer of the two layers
     * that own the properties the layer has never heard of.
     *
     * One addition per layer, in the order the box is built, and it is the exact mirror of
     * the subtraction above: the padding layer turns the content box into the **padding box**
     * and the border layer turns that into the **border box**, which is what a result's
     * `size` carries. A build that composed them the other way round would still get the
     * same number here -- addition commutes -- which is why the *downward* direction is
     * where the order is load-bearing and why the suite drives both.
     */
    status = LayoutSizingResolved(&sizing, &contentSize);
    if (status != RoboOk) {
        return status;
    }
    status = LayoutPaddingEdgesPaddingBox(&padding, contentSize, &paddingBox,
                                                    walk->diag);
    if (status != RoboOk) {
        return status;
    }
    status = LayoutBorderEdgesBorderBox(&borders, paddingBox, &size, walk->diag);
    if (status != RoboOk) {
        return status;
    }

    /* Counted from the *source* rather than from the declaration, so the two pairs of
     * counters mean the same thing on every path: a width is declared or it filled
     * what it was given, and a height is declared or it came from the contents. A
     * declaration this build could not resolve lands in the second bucket, which is
     * what 10.3's counters said and what the `UNRESOLVED` kind makes checkable. */
    if (sizing.widthSource == RoboLayoutDimensionSourceDeclared) {
        walk->block->widthsDeclared++;
    } else {
        walk->block->widthsFilled++;
    }
    if (sizing.heightSource == RoboLayoutDimensionSourceDeclared) {
        walk->block->heightsExplicit++;
    } else {
        walk->block->heightsFromContent++;
    }

    /*
     * The margin counters, summed over sides rather than boxes for the reason the
     * record's own counters are: a document's margins arrive four at a time and a
     * count of *sides* is what can be checked against a stylesheet by hand. They are
     * counted here rather than from the declarations because the *resolution* is what
     * they describe -- an `auto` that distributed is not a declaration this build
     * found, it is one it used.
     */
    walk->block->marginsApplied += edges.resolved;
    walk->block->marginsAutoUsed += edges.autosUsed;
    walk->block->marginsUnresolved += edges.unresolved;
    /* And the padding's two, summed over sides for the same reason: a document's padding
     * arrives four at a time and `padding: 5px` is four declarations one layer down. */
    walk->block->paddingsApplied += padding.resolved;
    walk->block->paddingsUnresolved += padding.unresolved;
    /* And the border's three, which are three rather than two because a border's *used*
     * width is not its declared one: a document can declare four `border-width: 5px` and
     * draw nothing, and only a counter that asks the second question can say so. */
    walk->block->bordersApplied += borders.widthsResolved;
    walk->block->bordersUnresolved += borders.widthsUnresolved;
    walk->block->bordersDrawn += borders.bordersDrawn;
    /*
     * And the box's own geometry is written at its **margin origin**, which is the
     * whole of what a margin does to a box: `origin` is where its containing block's
     * content box starts, and the box starts `left` and `top` further in.
     *
     * The `size` written here is the **border** box -- the content box the sizing layer
     * resolved plus the padding (10.10) and the border (10.11) -- which is what
     * `RoboLayoutResult.h` has said this field means since 10.1 and what makes it a number
     * a renderer can draw without re-deriving anything. A margin still never changes it,
     * which is why the two numbers the box's own box is placed by (`marginOrigin`, and the
     * margins' contribution to the parent's stacking) are not added into it anywhere.
     *
     * The **content origin** is this origin plus the border and the padding, and it is
     * deliberately not written anywhere: a child's position is already relative to its
     * containing block's *content* origin (`RoboLayoutResult.h`), so the children below
     * need no shift and a reader obtains the content origin from @ref
     * LayoutObjectBorders and @ref LayoutObjectPadding rather than from a
     * second copy of the same fact. Two terms since 10.11, and the two records compose in
     * one order: the border first, then the padding.
     */
    /* A margin origin is an accumulation too (Phase 10.15): the caller's origin plus a
     * bounded margin, and the sum of two bounded numbers is not bounded. */
    marginOrigin.x = LayoutExtentClamp((int64_t)origin.x + (int64_t)edges.left);
    marginOrigin.y = LayoutExtentClamp((int64_t)origin.y + (int64_t)edges.top);

    /*
     * The position next (10.12), and it is the only phase in this module that changes
     * *where* a box is without changing anything about the box. Two steps rather than one
     * because the two coordinate spaces are the phase: `marginOrigin` is where flow put
     * the box -- the number every sibling's placement was computed from -- and
     * `finalOrigin` is where it is drawn, which is `marginOrigin` displaced by the
     * resolved offset.
     *
     * A `static` box resolves to a zero offset and the two are the same number, which is
     * why every 10.3–10.11 expectation about a position still holds: the three inset
     * phases added their numbers *inside* `size`, and this one adds a number *outside* it
     * and leaves the size alone. The `*outHeight` below is computed from the flow
     * geometry for the same reason, so the following sibling starts where it always would
     * have whatever this box did with its offset.
     */
    declaredPosition = LayoutPositionOf(object->style);
    status = LayoutPositionResolve(&declaredPosition, &positionOffset, walk->diag);
    if (status != RoboOk) {
        return status;
    }
    status = LayoutPositionApply(marginOrigin, &positionOffset, &finalOrigin,
                                        walk->diag);
    if (status != RoboOk) {
        return status;
    }
    status = LayoutResultSetGeometry(&object->result, finalOrigin, size,
                                             walk->diag);
    if (status != RoboOk) {
        return status;
    }
    /* And the position's counters (10.12), which are the first counters in this record
     * that are about *where* rather than about *how big* -- and the first to count a
     * fact about the box rather than about a declaration (`boxesShifted`).
     * `offsetsDeclined` is the one that would otherwise be invisible: a `static` box
     * with four insets and a `relative` box with four autos both resolve to a zero
     * displacement, and only this counter tells the first document from the second.
     * Counted *after* the resolve above so the record always describes this box rather
     * than whatever a previous box left on the stack -- the ordering bug a determinism
     * case caught as pointer-sized garbage in the counters. */
    walk->block->offsetsApplied += positionOffset.insetsResolved;
    walk->block->offsetsDeclined += positionOffset.insetsDeclined;
    walk->block->offsetsUnresolved += positionOffset.insetsUnresolved;
    if (positionOffset.dx != 0 || positionOffset.dy != 0) {
        walk->block->boxesShifted++;
    }

    /*
     * The used margins are kept on the box (RoboLayoutObject.h), because the record a
     * later phase wants is a *margin box* and the result cannot carry one: the result
     * holds the border box by definition, and adding the four numbers back is exactly
     * the arithmetic 10.9 exists to stop a caller from re-deriving.
     */
    object->margins = edges;
    /*
     * And the used padding, kept on the box for the reason the margins are and one more:
     * it is the **only** way to recover the content box. A result carries the border box,
     * so `padding-left`/`padding-top` are what turn one into the other, and re-reading them
     * from the style would mean re-deciding which declarations this build could use.
     */
    object->padding = padding;
    /*
     * And the used border, kept on the box for the padding's reason and paint's: it is the
     * **outermost** of the two insets that belong to the box, so it is the second term of a
     * content origin beside `padding`, and it carries the style and colour of each side as
     * well as the width -- which is every fact a pass that draws an edge needs, decided once
     * here rather than re-decided from a style later.
     */
    object->borders = borders;
    /*
     * And the displacement it was placed with (10.12), which is what makes the box's
     * **flow** position recoverable: the result above carries the final position, so a
     * caller that wants where flow put the box subtracts this record. Kept on the box for
     * the reason the three insets are and one more -- a displacement is not derivable from
     * a size or an inset, so without this field the flow position would exist nowhere.
     */
    object->positionOffset = positionOffset;

    /*
     * And the overflow (10.13), which is the *last* thing measured about a box and has to
     * be: it is a statement about the contents, so it cannot be made until every child has
     * a rectangle, and every child has one only after the recursion above returned. The
     * measurement reads the children, not the box, which is why nothing earlier in this
     * function could have produced it.
     *
     * Note what it is measured against: the **padding box**, which is `size` less the
     * borders, and the four insets are relative to that -- a border is outside the boundary
     * CSS clips to and a padding is inside it. `padding` is also what locates this box's
     * *content* origin relative to that boundary, which is why the record is needed here
     * rather than only the widths.
     */
    {
        LayoutOverflowEdges overflow;

        status = LayoutOverflowMeasure(walk->tree, object, &borders, &padding, size,
                                              &overflow, walk->diag);
        if (status != RoboOk) {
            return status;
        }
        object->overflow = overflow;
        /*
         * The three counters, and the two that count a *box* are the interesting pair: a
         * document can declare `overflow: hidden` on every box and have nothing stick out
         * of any of them, so "how many boxes clip" and "how many boxes overflow" are two
         * different facts and neither implies the other. The third is about declarations
         * this build could not use -- a `var()` reference, an identifier from CSS's other
         * overflow values -- whose used mode is `visible` and which would otherwise be
         * invisible in the record.
         */
        walk->block->overflowClipped +=
            LayoutOverflowEdgesClipsAny(&overflow) ? 1u : 0u;
        walk->block->overflowing +=
            LayoutOverflowEdgesHasOverflow(&overflow) ? 1u : 0u;
        walk->block->overflowUnresolved += overflow.axesUnresolved;

        /*
         * And the result's two flags, which are the same fact in one bit per axis: a
         * consumer that only needs to know *whether* a box's contents do not fit reads a
         * bit, and a consumer that needs to know how far reads the record above. The flags
         * are set here rather than derived later because this is the one moment both halves
         * of the answer are in hand -- and `set_flags` replaces the whole word, which is
         * safe because this pass is the only writer of these two bits.
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
            status = LayoutResultSetFlags(&object->result, flags, walk->diag);
            if (status != RoboOk) {
                return status;
            }
        }
    }

    walk->block->blocksPlaced++;
    if (object->depth > walk->block->depthMax) {
        walk->block->depthMax = object->depth;
    }

    /*
     * And the extent the parent stacks by is the **margin box**, which is the second
     * thing a margin does: `cursor` in `LayoutBlockChildren` is a bottom edge, and the
     * next box starts below this one's bottom *margin*. Reported as one number rather
     * than as a size because that is all a containing block needs from a child in this
     * direction -- the parent never looks at where its child's content box ended, only
     * at where the next one may start.
     *
     * And 10.12 does not touch it, which is the phase in one line: a displacement moves
     * the box and never its flow contribution, so `size.height` and the two margins here
     * are the *flow* geometry and a box shifted 100px down still leaves the hole flow gave
     * it. A build that added the offset here would look identical in every single-box test
     * and move every following sibling, which is why the suite asserts the pair.
     *
     * `size.height` is the **border** box since 10.10 and stays it in 10.11, so this one
     * expression spans all **four** of a box's boxes: the padding and the border are already
     * inside `size` (that is what the two derived doors added), the margins are the two terms
     * around it, and the content box is inside both and not needed. Which is worth stating,
     * because it is the cleanest consequence either phase has -- a border changes this number
     * without changing its formula, and a build that had added the border here *again* would
     * double it. The formula is the same one 10.9 wrote when `size` was the content box, which
     * is the point: every inset phase since has added its number inside `size` rather than
     * beside it.
     *
     * No collapsing (the header says so): two adjacent margins add rather than
     * overlap, so a box with `margin-bottom: 20px` above one with `margin-top: 30px`
     * leaves 50px between them here where CSS would leave 30. That is the one place
     * this phase knowingly differs from the specification, it is stated in
     * `layout/docs/Margins.md` §6, and `layout.margins.block_no_collapse` asserts the
     * sum so that the goal that implements collapsing has a case to change.
     */
    /*
     * The margin box, and the third accumulation: `edges.top + size.height +
     * edges.bottom` is three bounded numbers, so it is summed in 64 bits and clamped
     * like the cursor it feeds -- a build that let this wrap would hand the parent a
     * negative child height, which is the same failure one level up.
     */
    *outHeight = LayoutExtentClamp((int64_t)edges.top + (int64_t)size.height +
                                      (int64_t)edges.bottom);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

void LayoutBlockStatsInit(LayoutBlockStats *stats)
{
    if (!stats) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

int LayoutBlockStatsEqual(const LayoutBlockStats *a,
                                  const LayoutBlockStats *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->containers == b->containers && a->blocksPlaced == b->blocksPlaced &&
           a->inlineLevelSkipped == b->inlineLevelSkipped &&
           a->unreached == b->unreached &&
           a->widthsDeclared == b->widthsDeclared &&
           a->widthsFilled == b->widthsFilled &&
           a->heightsExplicit == b->heightsExplicit &&
           a->heightsFromContent == b->heightsFromContent &&
           a->marginsApplied == b->marginsApplied &&
           a->marginsAutoUsed == b->marginsAutoUsed &&
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
           a->emptyContainers == b->emptyContainers &&
           a->depthMax == b->depthMax;
}

Result LayoutBlockLayout(LayoutTree *tree,
                                       LayoutSize viewport,
                                       const LayoutTextMetrics *textMetrics,
                                       LayoutBlockStats *outStats,
                                       LayoutInlineStats *outInlineStats,
                                       LayoutTextStats *outTextStats,
                                       LayoutDiag *diag)
{
    LayoutWalk walk;
    LayoutInlineStats scratch;
    LayoutTextStats textScratch;
    LayoutNodeId root;
    LayoutPoint origin;
    GfxPixelY height = 0;

    if (!tree) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBlockLayout, RoboInvalidArgument,
                                "tree is NULL");
    }
    if (viewport.width < 0 || viewport.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBlockLayout, RoboInvalidArgument,
                                "viewport is negative");
    }

    /*
     * Both records are cleared together and at the same point, which is the rule
     * 10.3's header states for the block one: an *argument* refusal above happens
     * before a caller's records are touched, and everything from here on -- including
     * the refusal below -- leaves them describing *this* call rather than a previous
     * one. A caller that asked only for the block numbers passes NULL and the inline
     * record is a local that nothing reads.
     */
    LayoutBlockStatsInit(outStats);
    LayoutInlineStatsInit(outInlineStats);
    LayoutInlineStatsInit(&scratch);
    LayoutTextStatsInit(outTextStats);
    LayoutTextStatsInit(&textScratch);

    /*
     * And the line boxes this pass is about to produce replace whatever the last one
     * left (Phase 10.6). The reset is here -- at the top of the pass, with the records
     * -- rather than inside the inline pass, because it is the same rule those records
     * follow: everything from this point on describes *this* call. A repeated pass over
     * an unchanged tree therefore produces the same lines, and a pass over a tree whose
     * viewport changed produces a different set rather than two sets appended.
     *
     * It is also why the *classification* does not have to be re-run for lines to be
     * correct: a caller may classify once and lay out many times, and each pass still
     * starts from no lines.
     */
    LayoutTreeLinesReset(tree);

    /*
     * The classification first, because it is the placement gate. Reported rather
     * than repaired: a caller that asked for block layout without asking for
     * classification has skipped the phase that decides which boxes this pass
     * owns, and inventing an answer would be inventing a second classifier.
     */
    if (!LayoutTreeIsClassified(tree)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainBlockLayout, RoboError,
                                "block layout needs a classified tree");
    }

    root = LayoutTreeRoot(tree);
    if (root == RoboLayoutNodeIdNone) {
        /* A document whose root element generates no box. There is nothing to
         * place, and that is an answer about the document rather than a failure --
         * but it does mean this call placed nothing, and the caller is told so
         * instead of being handed an empty stats record that looks the same as a
         * pass over a viewport-free tree. */
        return RoboLayoutDiag(diag, RoboLayoutDomainBlockLayout, RoboNotFound,
                                "the tree has no root box");
    }

    walk.tree = tree;
    walk.block = outStats;
    walk.inlineFlow = outInlineStats ? outInlineStats : &scratch;
    /*
     * The text record has a scratch for the same reason the inline one does: the pass
     * fills three records and a caller may want any subset of them, so "not asked
     * for" must still be a place to write -- never a NULL the inline pass would have
     * to test at every text run. The metrics, by contrast, are passed through as they
     * came: NULL is a *meaning* here ("nothing can measure"), not an omission.
     */
    walk.text = outTextStats ? outTextStats : &textScratch;
    walk.textMetrics = textMetrics;
    walk.diag = diag;

    /*
     * The root's containing block is the initial containing block, which is the
     * viewport. Every other box's containing block is its parent -- reached by
     * the recursion below and never by asking a formatting context.
     */
    origin.x = 0;
    origin.y = 0;
    {
        Result status = LayoutBlockBox(&walk, root, origin,
                                               (int32_t)viewport.width, &height);

        if (status != RoboOk) {
            return status;
        }
    }

    /*
     * The boxes this pass did not reach, counted here rather than during the
     * recursion: a box inside an unplaced box is never visited, so counting it
     * would mean walking it, and the arena already knows how many boxes there
     * are. `unreached` is therefore a *difference* and its definition is
     * "boxes with no geometry that neither pass declined for a stated reason" --
     * a block-level box inside an inline box, which needs the anonymous box this
     * build does not make.
     *
     * Both records are subtracted, because they are both answers to "why has this
     * object no geometry?": 10.3's `inlineLevelSkipped` is an inline-level box -- or
     * a text run, which is inline-level by construction -- in a block-direction
     * context, and 10.4's `blockLevelDeclined` is its mirror. An object the inline
     * pass *placed* is subtracted too, so the identity below closes over every
     * direction and every kind of object at once:
     *
     *   blocksPlaced + inlineLevelSkipped + participantsPlaced + unreached
     *       == objects
     *
     * and the suite asserts it, because a build that dropped a box would have four
     * right-looking counts and a document that did not add up.
     */
    {
        size_t boxes = LayoutTreeObjectCount(tree);
        size_t explained = outStats->inlineLevelSkipped;

        explained += walk.inlineFlow->participantsPlaced;
        if (boxes > outStats->blocksPlaced + explained) {
            outStats->unreached = boxes - outStats->blocksPlaced - explained;
        }
    }

    return RoboOk;
}
