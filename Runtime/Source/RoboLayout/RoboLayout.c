/*
 * RoboLayout module lifecycle, version and capabilities (Phase 10.1-10.2).
 *
 * The module owns *nothing*: every tree, context, bridge and record belongs to
 * the caller that created it. That is why init cannot fail for an allocation
 * reason, why shutdown releases no state, and why the whole file is a flag, a
 * version and a table of things this build does not do yet.
 */

#include "RoboLayout/RoboLayout.h"

#include "RoboLayout/RoboLayoutVersion.h"

#include <string.h>

static int gLayoutInitialized;

void LayoutCapabilitiesInit(LayoutCapabilities *capabilities)
{
    if (!capabilities) {
        return;
    }
    memset(capabilities, 0, sizeof(*capabilities));
}

Result LayoutGetCapabilities(LayoutCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));

    /* 10.1, and every one of these is asserted by
     * `layout.conformance.capabilities` -- by name, not by trusting this
     * file. A flag is a promise about the build, and an untested promise is one
     * that stops being true without anybody noticing. */
    out->objectModel = 1;
    out->constraints = 1;
    out->resultModel = 1;
    out->context = 1;
    out->domBuild = 1;
    out->computedStyleLink = 1;

    /* 10.2: the pass classifies every box into a formatting context, and that
     * classification is a finished feature rather than a flag waiting to be
     * flipped -- which is why it is 1 here while `geometry` was still 0. */
    out->formattingContexts = 1;

    /*
     * 10.3 and 10.4, and the four flags that have to be read together.
     *
     * `geometry` is 1 because this build really does compute coordinates: the pass
     * writes positions and sizes, and a caller that ran one with a viewport has boxes
     * whose results are COMPUTED. It is a statement about the *build*, not about any
     * particular box -- a box neither direction placed is still UNRESOLVED, and its
     * result state is the per-box authority that says so.
     *
     * `blockLayout` and `inlineLayout` are the two directions, each read from its
     * own constant so that the field and the macro cannot disagree; 10.3 shipped the
     * first at 1 and the second at 0, and 10.4 is the phase where the second's code
     * arrived and the constant with it (`LayoutInlineWalkContents`).
     *
     * `blockAndInlineLayout` is their conjunction rather than a third constant,
     * and it is deliberately still an expression instead of a literal 1: it names the
     * wider question -- "is layout done?" -- and 10.3's version of this file argued
     * that a phase which implemented one direction and flipped *this* one would have
     * advertised a feature it does not have. Writing it as the conjunction keeps that
     * property for the next direction that is added, because the day one of the two is
     * switched off this field goes back to 0 by itself.
     *
     * `painting` stays 0: nothing here draws.
     */
    out->geometry = RoboLayoutGeometryImplemented;
    out->blockLayout = RoboLayoutBlockLayoutImplemented;
    out->inlineLayout = RoboLayoutInlineLayoutImplemented;
    out->blockAndInlineLayout = (RoboLayoutBlockLayoutImplemented &&
                                    RoboLayoutInlineLayoutImplemented);

    /*
     * 10.5, and read from its own constant for the same reason the two directions are:
     * the field and the macro cannot disagree. What it reports is that this build can
     * *measure* -- a text node becomes a layout object and a run's width can come from
     * a font -- and not that any particular pass measured anything: a pass with no
     * source installed is a legitimate configuration, counts every run as *unmeasured*,
     * and lays the document out exactly as 10.4 did. That distinction is why the
     * per-pass answer lives in `LayoutTextStats` and this flag is about the
     * build.
     */
    out->textLayout = RoboLayoutTextLayoutImplemented;

    /*
     * 10.6, read from its own constant: the pass finalizes line boxes into the tree,
     * so a caller can ask what is on a line, how wide it is and where it sits. Not a
     * promise about any document -- an empty inline context has no lines by CSS's own
     * rule -- and not a promise about what a line *knows*: a baseline and a
     * font-derived extent are 10.7's and the field's own comment says so.
     */
    out->lineBoxes = RoboLayoutLineBoxesImplemented;
    out->baselines = RoboLayoutBaselinesImplemented;

    /*
     * 10.8, read from its own constant like the six above it. What it reports is that
     * the sizing layer exists and that a declared px `width`/`height` reach the
     * geometry -- **not** that sizing is finished: a percentage is read, recorded as
     * `UNRESOLVED`, and filled from the containing block, and intrinsic sizing does
     * not exist. The field's own comment in RoboLayout.h states that boundary where a
     * caller reads it rather than only here.
     */
    out->sizing = RoboLayoutSizingImplemented;

    /*
     * And 10.9's, same shape: the margin layer exists, a declaration reaches a used
     * value, and a box is placed at its margin origin with its parent stacking by its
     * margin box. **Not** "margins are finished": there is no collapsing, a percentage
     * is used as zero rather than resolved, and a non-replaced inline box has no
     * margins at all, which is why `LayoutObjectMargins` refuses one.
     */
    out->margins = RoboLayoutMarginsImplemented;
    out->padding = RoboLayoutPaddingImplemented;

    /*
     * And 10.11's, the last of the box model's four layers and the only one whose *used*
     * value is not its declared one: a `border-style: none` gives every side a used width of
     * zero however `border-width` was written, which is why the record carries a counter for
     * a width that resolved and a second for a border that actually draws. **Not** "borders
     * are finished": `border-radius` does not exist, `hidden`'s collapsing meaning belongs
     * to tables this build has none of, and a transparent inline box takes no border.
     */
    out->border = RoboLayoutBorderImplemented;
    /*
     * And 10.12's, the first capability in this struct that is about *where* a box is rather
     * than how big it is or what is around it. A `relative` box is displaced and nothing else
     * changes: its size, its baseline and the room flow reserved for it are all untouched.
     * **Not** "positioning is finished": `absolute`, `fixed` and `sticky` are values 9.13
     * already carries and behaviour this build does not have, no stacking context or paint
     * order moves, and a transparent inline box and a text run carry no offset.
     */
    out->positioning = RoboLayoutPositioningImplemented;
    /*
     * And 14.0's, which is the behaviour 10.12 deferred: a box taken out of flow and placed
     * against a containing block. **Not** "positioning is finished": nothing is reordered, so
     * `z-index`, stacking contexts and paint order are still absent, `sticky` is still in flow,
     * and there is no scrollport to constrain against.
     */
    out->outOfFlow = RoboLayoutOutOfFlowImplemented;
    /*
     * And 10.13's, the first capability in this struct that is about what happens when a
     * box's contents do *not* fit. A record per measured box -- two used modes and four
     * extents beyond the padding box -- plus a region and a clip rectangle a paint pass can
     * consume, with every placed child's region folded into its parent's measurement. **Not**
     * "overflow is finished": nothing scrolls, no scrollport or scrollbar exists,
     * `scroll`/`auto`/`clip` are values this build has not added, CSS's `visible`/`auto` axis
     * promotion does not apply (it exists for scrolling), and no pixel is touched here.
     */
    out->overflow = RoboLayoutOverflowImplemented;
    /*
     * And 10.14's, the first capability in this struct that is about a **conclusion's
     * shelf life** rather than about a box. A state and a reason per box, a classifier over
     * property names, three propagation policies, a dirty root, and one decision -- skip the
     * pass when nothing needs one. **Not** "invalidation is finished": there is no partial
     * layout (the dirty root is reported and the pass still recomputes from the tree root),
     * no dependency graph, no multithreading, and no paint invalidation beyond the STYLE bit
     * a paint phase would consume.
     */
    out->invalidation = RoboLayoutInvalidationImplemented;
    /*
     * And 12.17's, the first capability in this struct that is about a **document's own typography**
     * rather than about a box. A computed style's five font properties become one value record, a
     * `LayoutFontProvider` answers its family entries in author order, and the answering face is
     * recorded on the run so that "how wide is it?" and "which face drew it?" are one answer. **Not**
     * "fonts are finished": there is no font database in this module (the provider is a callback),
     * no coverage walk, no fallback chain, no per-scalar splitting, no synthetic weight or slope, and
     * no `@font-face`. A build that installs no provider measures with the single attached face.
     */
    out->fontRequest = RoboLayoutFontRequestImplemented;
    /*
     * And 19.1's, the first capability about a box's **contents** rather than the box's own geometry:
     * the widest unbreakable piece (`min-content`), the whole content unwrapped (`max-content`), and
     * the clamp between them (`fit-content`), each measured from shaped RoboText metrics and each
     * reachable from a declared `width` keyword through the sizing resolver. **Not** "sizing is
     * finished": no replaced-element intrinsic dimensions, no intrinsic height as a function of width
     * (that is wrapping), and no flex/grid/table intrinsic sizing (19.3/19.4/19.6).
     */
    out->intrinsicSizing = RoboLayoutIntrinsicSizingImplemented;
    /*
     * And `percentageResolution`, the field 19.2 added. **Not** "percentages are finished": a
     * percentage resolves against the containing block's own dimension on a dimension, a margin,
     * a padding and an inset, and degrades to `UNRESOLVED` where there is no base -- but no grid
     * track or flex basis percentage, no percentage min/max bound, no out-of-flow inset
     * percentage, no `calc()` and no viewport unit.
     */
    out->percentageResolution = RoboLayoutPercentageResolutionImplemented;
    /*
     * And `flexLayout`, the field 19.3 added. **Not** "flexbox is finished": a flex
     * container lays its items out along a main axis with grow/shrink, wrapping and
     * the three alignment properties -- but no flex root, no baseline alignment, no
     * automatic minimum size, no `gap`, no shorthand expansion and no vertical
     * writing mode.
     */
    out->flexLayout = RoboLayoutFlexLayoutImplemented;
    /*
     * And `gridLayout`, the field 19.4 added. **Not** "grid is finished": a grid
     * container sizes two axes of tracks and places its items into them -- but no
     * `justify-items`/`justify-self`, no named lines, no `grid-template-areas`, no
     * `subgrid`, no `auto-fill` repeat, no shorthand expansion, no masonry and no
     * vertical writing mode.
     */
    out->gridLayout = RoboLayoutGridLayoutImplemented;
    out->painting = 0;
    return RoboOk;
}

const char *LayoutVersionString(void)
{
    return RoboLayoutVersionString;
}

Result LayoutGetVersion(I32 *outMajor,
                                      I32 *outMinor,
                                      I32 *outPatch)
{
    if (!outMajor || !outMinor || !outPatch) {
        return RoboInvalidArgument;
    }
    *outMajor = RoboLayoutVersionMajor;
    *outMinor = RoboLayoutVersionMinor;
    *outPatch = RoboLayoutVersionPatch;
    return RoboOk;
}

int LayoutGeometryIsImplemented(void)
{
    /*
     * One macro, used by both doors, so the capability field and the function
     * cannot disagree -- which is exactly the drift a caller would have no way to
     * see.
     */
    return RoboLayoutGeometryImplemented;
}

Result LayoutInit(void)
{
    if (gLayoutInitialized) {
        return RoboAlreadyExists;
    }
    gLayoutInitialized = 1;
    return RoboOk;
}

void LayoutShutdown(void)
{
    /* Nothing to release: the module owns no state. Stated rather than left as an
     * empty function body, because "there is nothing here" is the fact a reader
     * needs. */
    gLayoutInitialized = 0;
}

int LayoutIsInitialized(void)
{
    return gLayoutInitialized;
}
