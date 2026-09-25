/*
 * RoboPaint context and the deterministic traversal (Phase 11.1–11.2) — see
 * RoboPaintContext.h.
 *
 * This file is the pass: the walk, the accumulation and the stats. Everything it
 * concludes comes from RoboLayout's own read doors -- there is no width, height, margin,
 * padding, border, position or overflow arithmetic here beyond the two things layout
 * cannot know, the page-space accumulation and the per-axis clip intersection, and both
 * of those live in RoboPaintGeometry.c so that this file stays a traversal.
 *
 * The walk is deliberately the dumbest correct thing: depth-first, document order, no
 * stack of its own beyond the call stack, and one operation per fact the box states. The
 * three properties the phase claims are consequences of that shape rather than checks
 * bolted onto it:
 *
 *   **determinism** -- the order is the tree's (`firstChild` then `NextSibling`) and
 *   every number is a bounded record's, so two passes over one tree produce byte-identical
 *   lists (`PaintListEqual`);
 *
 *   **purity** -- every door on the left-hand side takes the tree by `const` pointer, so
 *   "the painter does not write layout" is the signature;
 *
 *   **no re-derivation** -- the boxes come from `PaintBoxesOf`, the colours from
 *   the box's own style and its *used* border record, and the clip from the overflow the
 *   layout pass measured. Paint decides only *whether* something draws, which is the
 *   question layout does not ask.
 */

#include "RoboPaint/RoboPaintContext.h"

#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSValue.h"

#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLImage.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLTag.h"

#include "RoboLayout/RoboLayoutBaseline.h"

#include "RoboPaint/RoboPaintGeometry.h"
#include "RoboPaint/RoboPaintImage.h"
#include "RoboPaint/RoboPaintOpacity.h"
#include "RoboPaint/RoboPaintOperation.h"
#include "RoboPaint/RoboPaintRun.h"
#include "RoboPaint/RoboPaintText.h"

/* The three element-level style reads this file defines are shared with 11.10's hit-test
 * walk (`RoboPaintInternal.h` argues why they are privately linked rather than installed),
 * so the header is included here to check the definitions against their declarations. */
#include "RoboPaintInternal.h"

#include "RoboMem/RoboMem.h"
#include "RoboText/RoboText.h"
#include "RoboUnicode/RoboUnicode.h"

#include <string.h>

struct PaintContext {
    PaintStats stats;
    PaintPassOutcome outcome;
    /* Borrowed and const as read back: the pass read the tree, it did not write it. */
    const LayoutTree *tree;
    void *userData;
    /**
     * The face text is **shaped** with as it is recorded (12.16), borrowed, or NULL.
     *
     * The one thing this pass needs that the tree cannot tell it: a run carries characters and a style, and
     * turning those into glyphs takes a face. NULL is a legal configuration and the pre-12.16 behaviour --
     * runs record their characters and no glyphs, and a drawing pass reports `noFont` -- so a caller that
     * wants 11.6's list back can have it by not installing one.
     */
    FontFace *textFace;
};

/* ------------------------------------------------------------------ */
/* The pass outcome                                                    */
/* ------------------------------------------------------------------ */

int PaintPassOutcomeIsKnown(PaintPassOutcome outcome)
{
    return outcome >= RoboPaintPassNone &&
           outcome < (PaintPassOutcome)RoboPaintPassOutcomeCount;
}

const char *PaintPassOutcomeName(PaintPassOutcome outcome)
{
    switch (outcome) {
    case RoboPaintPassNone:
        return "none";
    case RoboPaintPassPainted:
        return "painted";
    case RoboPaintPassFailed:
        return "failed";
    case RoboPaintPassStale:
        return "stale";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Reading a colour out of a computed style                            */
/* ------------------------------------------------------------------ */

/*
 * The one place this module reads a property RoboLayout deliberately ignored, and it is
 * the *paint-visible* half of 10.14's classification rather than a boundary violation:
 * `background-color` and the border colours are the properties 10.14 says layout does
 * not read and paint does. The read is by **name** through
 * `CssComputedStyleComputedByName`, which is the same door
 * `RoboLayoutBorder.c` uses for the same reason: no property id has to have been
 * resolved for the property to be found.
 */

/** The `COLOR` value of @p property in @p style, or a `NONE` colour. */
CssColor PaintPropertyColor(const CssComputedStyle *style,
                                          const char *property)
{
    const CssComputedValue *value;

    if (!style || !property) {
        return CssColorNone();
    }
    value = CssComputedStyleComputedByName(style, CssSliceFromText(property));
    if (value && value->kind == RoboCssComputedValueColor) {
        return CssComputedValueColor(value);
    }
    /* Absent, or present as something that is not a colour. Both answer "no colour was
     * declared", which is the statement a caller of this helper can act on. */
    return CssColorNone();
}

/** The element's own computed `color`, or a `NONE` colour. */
CssColor PaintElementColor(const CssComputedStyle *style)
{
    return PaintPropertyColor(style, "color");
}

/* ------------------------------------------------------------------ */
/* Reading the opacity out of a computed style (11.9)                  */
/* ------------------------------------------------------------------ */

/*
 * The second property this module reads that RoboLayout deliberately ignores, read through
 * the same door and for the same reason `background-color` is: `opacity` is paint-visible
 * and layout-invisible, so it is 10.14's `STYLE` bit without a layout bit and this layer is
 * where it is consumed.
 *
 * Three rules are worth stating here, because each one is a trap:
 *
 *   **the kind is checked, not the magnitude**   the value arrives as
 *   `RoboCssComputedValueOpacity` and only that kind is read. A `200px` width is a
 *   *number* too, so a reader that took the real out of whatever it found would read a
 *   width as an opacity of `200` -- `RoboCSSComputedValue.h` gives opacity its own kind for
 *   exactly this reason, and this is the reader that would have made it necessary.
 *
 *   **absent and present-and-one are different answers, and both mean opaque**   no
 *   declaration and `opacity: 1` reach the same drawing, and
 *   `CssComputedValueOpacity` is the door that answers *one* for a value that is not
 *   an opacity rather than zero -- because zero would make every element of every document
 *   that never mentions `opacity` invisible.
 *
 *   **it does not inherit, and the run is where that matters**   this function is called for
 *   **boxes only**. A text run's style is its parent element's, borrowed (`RoboLayoutText.h`),
 *   and that element has already opened an opacity scope around it -- so a run that read its
 *   own style's opacity would apply the same declaration a second time and square it.
 */

/** The opacity @p style declares, or the fully opaque record when it declares none. */
PaintOpacity PaintElementOpacity(const CssComputedStyle *style)
{
    const CssComputedValue *value;

    if (!style) {
        return PaintOpacityOpaque();
    }
    value = CssComputedStyleComputedByName(style, CssSliceFromText("opacity"));
    if (!value || value->kind != RoboCssComputedValueOpacity) {
        return PaintOpacityOpaque();
    }
    return PaintOpacityFromValue(CssComputedValueOpacity(value));
}

/**
 * @p color with `currentColor` resolved against @p current, and whether it draws.
 *
 * The resolution rule is `CssColorResolveCurrent`'s and this wrapper exists for
 * the two decisions beside it:
 *
 *   a `NONE` colour asked to draw becomes `currentColor` **only** when @p defaultForNone
 *   is set. That is the difference between the background (`background-color`'s initial
 *   value is `transparent`, so an undeclared background draws nothing) and a border
 *   (`border-*-color`'s initial value is `currentColor`, so an undeclared colour draws in
 *   the element's text colour). Encoding it as a flag keeps one code path for the
 *   resolution and states the two initial values where they differ;
 *
 *   a colour that could not be resolved is **still a colour** the operation may carry
 *   (`currentColor` is legal in a record), so the caller learns "contextual" and draws
 *   anyway. That is what `stats.contextualColors` counts.
 *
 * @param outColor    receives the colour the operation will carry.
 * @param outDraws    receives 1 when the colour puts a pixel down.
 * @param outContextual receives 1 when the colour stayed `currentColor`.
 */
static void PaintResolveColor(CssColor color,
                                CssColor current,
                                int defaultForNone,
                                CssColor *outColor,
                                int *outDraws,
                                int *outContextual)
{
    CssColor resolved;

    if (outColor) {
        *outColor = CssColorNone();
    }
    if (outDraws) {
        *outDraws = 0;
    }
    if (outContextual) {
        *outContextual = 0;
    }

    if (color.kind == RoboCssColorNone) {
        if (!defaultForNone) {
            /* An undeclared background: nothing to draw, and nothing contextual either --
             * `transparent` is a colour the author effectively asked for. */
            return;
        }
        /* An undeclared border colour is `currentColor` by CSS's initial value. */
        color = CssColorCurrent();
    }
    if (color.kind == RoboCssColorInvalid) {
        return;
    }

    if (CssColorResolveCurrent(color, current, &resolved)) {
        if (outColor) {
            *outColor = resolved;
        }
        if (outDraws) {
            /* A fully transparent colour occupies no pixel, which is a statement about
             * the document rather than an omission -- so it is counted as "does not
             * draw" and no operation is emitted for it. */
            *outDraws = resolved.alpha != 0u;
        }
        return;
    }

    /*
     * It stayed contextual: there was no `color` to substitute. The operation still
     * carries it -- a `currentColor` record is valid (RoboPaintOperation.h) -- and the
     * stat says how many did, because a document where *every* colour is contextual is a
     * document whose styles were not computed.
     */
    if (outColor) {
        *outColor = color;
    }
    if (outDraws) {
        *outDraws = 1;
    }
    if (outContextual) {
        *outContextual = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Building one operation                                              */
/* ------------------------------------------------------------------ */

/** The four border kinds, indexed by `CssBoxSide`. */
static const PaintOpKind paintBorderKinds[RoboCssBoxSideCount] = {
    RoboPaintOpBorderTop,
    RoboPaintOpBorderRight,
    RoboPaintOpBorderBottom,
    RoboPaintOpBorderLeft
};

/*
 * Emission is one helper per *payload shape* rather than one with a kind switch, because
 * the kinds of record carry different fields and filling the wrong ones is the mistake
 * worth making impossible: a structural record fills a colour that says "nothing", a
 * colour drawing fills a colour a rasteriser can use, a run fills a **span** into the
 * pool it belongs to (11.6) and an image fills a **retained resource** (11.7) -- and
 * none of the four can be spelled by accident from another's helper.
 */

/**
 * Append a **structural** record: a `BEGIN_BOX`/`CONTENT`/`END_BOX`, or one of the two
 * **scope** kinds the same shape carries (`PUSH_CLIP`/`POP_CLIP` from 11.8, and
 * `PUSH_OPACITY`/`POP_OPACITY` from 11.9).
 *
 * The **opacity in force** is a parameter beside the clip rather than a field filled later,
 * and the rule it is filled by is the one rule that makes every record in the list read the
 * same way: a record carries the clip **and** the opacity in force at its own position in the
 * sequence. For a scope pair that means both halves carry the value the pair establishes --
 * which is what makes a pair self-identifying, and what a builder checks the pop against.
 */
static Result PaintEmitStructural(PaintList *list,
                                      PaintOpKind kind,
                                      LayoutNodeId node,
                                      size_t depth,
                                      const PaintBoxes *boxes,
                                      LayoutPoint origin,
                                      const PaintClip *clip,
                                      const PaintOpacity *opacity,
                                      PaintDiag *diag)
{
    PaintOperation operation;

    PaintOperationInit(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = depth;
    operation.boxes = *boxes;
    operation.origin = origin;
    operation.clip = *clip;
    operation.opacity = *opacity;
    operation.color = CssColorNone();
    return PaintListAppend(list, &operation, diag);
}

/**
 * Append a drawing operation: a background, or one border side.
 *
 * It takes **both** of the fields whose meaning varies by kind, and takes them as
 * parameters rather than reading them: a background passes the colour the box declared
 * and `NONE` for the style, a border passes both of its own. That is the shape the
 * record's validity rule enforces from the other side, so a caller cannot half-fill a
 * drawing record by accident -- the two facts a border has and a background does not
 * arrive together.
 */
static Result PaintEmitDrawing(PaintList *list,
                                       PaintOpKind kind,
                                       LayoutNodeId node,
                                       size_t depth,
                                       const PaintBoxes *boxes,
                                       LayoutPoint origin,
                                       const PaintClip *clip,
                                       const PaintOpacity *opacity,
                                       CssColor color,
                                       CssBorderStyle style,
                                       PaintDiag *diag)
{
    PaintOperation operation;

    PaintOperationInit(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = depth;
    operation.boxes = *boxes;
    operation.origin = origin;
    operation.clip = *clip;
    operation.opacity = *opacity;
    operation.color = color;
    operation.style = style;
    return PaintListAppend(list, &operation, diag);
}

/* ------------------------------------------------------------------ */
/* The decorations of one placed box                                   */
/* ------------------------------------------------------------------ */

/**
 * Append the background and border operations a placed box's own declarations ask for.
 *
 * Four rules, and each one is "emit only when it draws" applied to a different property:
 *
 *   a background is emitted when `background-color` resolved to a colour that puts a
 *   pixel down. An undeclared background is `transparent` and emits nothing -- which is
 *   why a plain `<div>` has no background operation rather than an invisible one;
 *
 *   a border side is emitted when its **used** width is above zero *and* its colour
 *   draws. The width comes from layout's own used record, so a `border: 5px none` side
 *   -- a declared five that costs no room -- is already a zero here and needs no second
 *   decision;
 *
 *   an undeclared border colour is `currentColor`, because that is CSS's initial value
 *   for the longhand and the reason `RoboLayoutBorder.h` reports `NONE` rather than
 *   inventing `transparent`;
 *
 *   `currentColor` that nothing could resolve is still emitted, and counted.
 *
 * The border record is asked for once and its four used sides read through the layer's
 * own width door, so "which field of the used record is the width" is asked where
 * RoboLayout answers it rather than three times here.
 */
static Result PaintBoxDecorations(PaintContext *context,
                                          const LayoutTree *tree,
                                          LayoutNodeId id,
                                          size_t depth,
                                          const PaintBoxes *boxes,
                                          LayoutPoint origin,
                                          const PaintClip *clip,
                                          const PaintOpacity *opacity,
                                          PaintList *list,
                                          PaintDiag *diag)
{
    const CssComputedStyle *computed = LayoutObjectStyle(tree, id);
    CssColor elementColor;
    CssColor color;
    CssBorderStyle style;
    int draws;
    int contextual;
    LayoutBorderEdges borders;
    size_t side;

    if (!computed) {
        /* No declaration could be read from this box at all. A fact about the build --
         * the element had no computed style -- and not a failure, so the box contributes
         * a marker pair and no decoration. */
        context->stats.withoutStyle++;
        return RoboOk;
    }
    elementColor = PaintElementColor(computed);

    /* The background: `background-color`, which is `transparent` until an author says
     * otherwise. Painted over the **border box**, CSS's initial `background-clip`. */
    color = PaintPropertyColor(computed, "background-color");
    PaintResolveColor(color, elementColor, 0, &color, &draws, &contextual);
    if (contextual) {
        context->stats.contextualColors++;
    }
    if (draws) {
        Result result = PaintEmitDrawing(list, RoboPaintOpBackground, id,
                                                  depth, boxes, origin, clip, opacity, color,
                                                  RoboCssBorderStyleNone, diag);

        if (result != RoboOk) {
            return result;
        }
        context->stats.backgrounds++;
    }

    if (LayoutObjectBorders(tree, id, &borders, NULL) != RoboOk) {
        /* Declined (a text run or a transparent inline box never reaches here -- both are
         * refused by `PaintBoxesOf` first) or absent. Either way there is no used
         * border to draw, which is an answer and not a failure. */
        return RoboOk;
    }

    for (side = 0u; side < (size_t)RoboCssBoxSideCount; side++) {
        GfxPixelX width = 0;

        if (LayoutBorderEdgesWidth(&borders, (CssBoxSide)side, &width) !=
            RoboOk) {
            continue;
        }
        if (width <= 0) {
            /* The used width is the whole of "does this side draw": a zero-width border,
             * a `none` style and an undeclared side are one number here, which is the
             * conclusion RoboLayout already reached. */
            continue;
        }

        /*
         * The colour **and** the style come off the same used side and in one switch, so
         * they cannot be read from different sides -- the mistake a second switch would
         * make possible. 10.11 put both on the used record for exactly this moment: the
         * style is not a property this layer reads from a computed style but a fact the
         * layer below already resolved, and a paint pass that read only the colour would
         * be dropping it silently.
         */
        switch (side) {
        case RoboCssBoxSideTop:
            color = borders.top.color;
            style = borders.top.style;
            break;
        case RoboCssBoxSideRight:
            color = borders.right.color;
            style = borders.right.style;
            break;
        case RoboCssBoxSideBottom:
            color = borders.bottom.color;
            style = borders.bottom.style;
            break;
        default:
            color = borders.left.color;
            style = borders.left.style;
            break;
        }

        PaintResolveColor(color, elementColor, 1, &color, &draws, &contextual);
        if (contextual) {
            context->stats.contextualColors++;
        }
        if (!draws) {
            continue;
        }
        {
            Result result = PaintEmitDrawing(list, paintBorderKinds[side], id,
                                                      depth, boxes, origin, clip, opacity, color,
                                                      style, diag);

            if (result != RoboOk) {
                return result;
            }
        }
        context->stats.borderSides++;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* One text run (11.6)                                                 */
/* ------------------------------------------------------------------ */

/*
 * Three facts this module had none of before this phase, and each of them is a *read*:
 *
 *   what characters the run holds -- the DOM's bytes, decoded through RoboUnicode into
 *   the code points a range counts in;
 *   how wide the placement is -- layout's own result for the object, which for a text
 *   object is the line fragment;
 *   where the baseline sits -- layout's own baseline record, read as an offset from the
 *   top of that placement.
 *
 * Nothing here measures anything. A run's width came from the font through 10.5's seam and
 * was consumed by the line; the painter that computed a width would be a second measurer
 * with a different answer, and the painted text would not line up with the box that was
 * sized for it.
 */

/*
 * Shaping, at the place the run is read (12.16).
 *
 * The pass now does one thing it did not do before: it turns a run's characters into **glyphs**, once, with
 * the face the caller installed. The three decisions worth stating:
 *
 *   **the style is the measurer's own record**, copied rather than re-derived -- the same four numbers
 *   `RoboLayoutTextFont.c` measured with, read here through `LayoutTextStyleResolve` and carried on the
 *   operation. So the glyphs the document is drawn with are the glyphs it was measured with, and the advance
 *   the pen accumulates at paint time is the advance the line was broken against.
 *
 *   **a failure of the text layer is not a failure of the document.** A face that will not cover the text,
 *   a buffer that will not decode: RoboText answers both with a result (`.notdef` and counted misses, or
 *   `U+FFFD`), so the only thing this can fail on is memory. A run with **no** face is not even attempted:
 *   the characters are stored and the glyph span stays empty, which is the `noFont` state a drawing pass
 *   names rather than a hole in the list.
 *
 *   **the base direction is the run's own.** `LayoutTextStyle.direction` is the direction the *measurer*
 *   resolved for this text, so declaring it here is a copy of one fact rather than a second opinion about
 *   the text -- and it matters for exactly the text that has none of its own, a label of digits or dashes,
 *   which would otherwise classify `Unknown` rather than the direction of the surface it sits on.
 *
 * Nothing here is retained: the buffer, the shape result and the scratch glyph array are all released before
 * this returns, and what the list keeps is `PaintGlyph` values -- eight numbers and a flag word, no pointers
 * (`RoboPaintText.h`).
 */

/** The RoboText font inputs @p style names, for @p face. */
/**
 * The run's font setting, in the painter's own vocabulary (Phase 12.24).
 *
 * The five numbers a shaping call needs, read from the record the measurer resolved: the size, the weight,
 * the slope and the paragraph's own base direction. The *clamp* that narrows a `font-weight` CSS allows into
 * the nine-step scale `RoboText` names moved into `RoboPaintRun.c` with the rest of the shaping seam, so there
 * is one place that decides what an unusual weight is drawn at rather than one per painter.
 */
static void PaintRunStyleFromLayout(PaintRunStyle *run,
                                    const LayoutTextStyle *style)
{
    PaintRunStyleInit(run);
    run->sizePx = (float)style->fontSizePx;
    run->weight = style->fontWeight;
    run->italic = style->italic ? 1 : 0;
    run->rtl = style->direction == RoboLayoutTextDirectionRtl ? 1 : 0;
}

/** Shape @p span's characters through @p context's face into @p list's glyph pool.
 *
 * @param outGlyphs receives the span naming the glyphs, empty when nothing was shaped.
 * @param outToken  receives the face token, `None` exactly when @p outGlyphs is empty.
 *
 * @return RoboOk; RoboOutOfMemory; RoboInvalidArgument when the face's font inputs are not a setting this
 *         build can name (a size CSS allows but RoboText refuses).
 */
static Result PaintShapeRun(PaintContext *context,
                                      PaintList *list,
                                      FontFace *face,
                                      const uint32_t *codePoints,
                                      PaintTextSpan span,
                                      const LayoutTextStyle *style,
                                      PaintGlyphSpan *outGlyphs,
                                      PaintFontToken *outToken,
                                      PaintDiag *diag)
{
    PaintRunStyle runStyle;
    PaintGlyph *glyphs = NULL;
    PaintGlyphSpan glyphSpan;
    size_t glyphCount = 0u;
    Result status;

    (void)context;
    *outGlyphs = PaintGlyphSpanAt(0u);
    *outToken = PaintFontTokenNone;
    if (!face) {
        /* The characters are the document's and are already stored; the glyphs are what a missing face
         * costs, and a drawing pass counts the run `noFont` rather than drawing boxes it would have to
         * invent a face to find. */
        return RoboOk;
    }

    /*
     * The scalars the pool already holds, through the **shared shaped-run seam** (Phase 12.24): the shaping
     * call, the glyph-run walk and the conversion into `PaintGlyph` records now live once, in
     * `RoboPaintRun.c`, because the shell needed the same three things and the alternative was a second copy
     * of them -- which is what the shell had before this phase. This file keeps the two facts that are the
     * paint *list's*: the span the glyphs land in, and the face token the list's own font table issues.
     */
    PaintRunStyleFromLayout(&runStyle, style);
    status = PaintRunShapeScalars(face, codePoints + (size_t)span.first, (size_t)span.count,
                                    &runStyle, &glyphs, &glyphCount, NULL, diag);
    if (status != RoboOk) {
        return status;
    }
    if (glyphCount == 0u) {
        /* A run of text the text layer produced no glyph for at all. Not a failure and not a face either:
         * the record keeps its characters and the empty "was not shaped" state, so a drawing pass says so
         * rather than drawing nothing where a box was measured. */
        return RoboOk;
    }

    status = PaintListAddGlyphs(list, glyphs, glyphCount, &glyphSpan, diag);
    RoboFree(glyphs);
    if (status != RoboOk) {
        return status;
    }
    /*
     * The face, registered **after** the glyphs landed and idempotently: a document with two hundred runs of
     * one face has one entry, because `PaintFontTableAdd` answers with the token a face already has. The
     * order matters -- a failure to store glyphs must not leave a face registered for a run that has none --
     * and the table is the **list's**, which is what makes the token meaningful to the list the record lands
     * in and to the display list the builder copies it into.
     */
    status = PaintFontTableAdd(PaintListFontTable(list), face, outToken, diag);
    if (status != RoboOk) {
        return status;
    }
    *outGlyphs = glyphSpan;
    return RoboOk;
}

/** The code points of @p handle's @p range, appended to @p list's pool.
 *
 * @return RoboOk and the span; RoboNotFound when the node does not resolve or its bytes
 *         do not decode; RoboInvalidArgument for a range outside the text;
 *         RoboOutOfMemory.
 */
static Result PaintReadText(PaintList *list,
                                    const HtmlNodeHandle *handle,
                                    LayoutTextRange range,
                                    PaintTextSpan *outSpan,
                                    PaintDiag *diag)
{
    HtmlNodeView view;
    uint32_t *codePoints;
    size_t start = 0u;
    size_t length = 0u;
    size_t offset;
    size_t count = 0u;
    Result result;

    *outSpan = PaintTextSpanAt(0u);
    if (HtmlNodeGetView(handle, &view, NULL) != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboNotFound,
                               "a text run's node did not resolve");
    }
    if (view.kind != RoboHtmlNodeKindText) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboNotFound,
                               "a text object names a node that holds no text");
    }
    /*
     * The range's byte window, through the bridge that owns the range type -- so "both
     * endpoints land on code-point boundaries and inside the text" is established once, by
     * the code that knows the convention, rather than by a second copy of the arithmetic
     * here. The **window** is what this uses; the range's code-point endpoints were used by
     * the measurer and are not needed to walk forward from a known boundary.
     */
    if (LayoutTextRangeBytes(handle, range, &start, &length, NULL) != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboInvalidArgument,
                               "a text run's range is not inside its node");
    }
    if (length == 0u) {
        /* An empty run is a legal text node and draws nothing; the caller counts it as a
         * run that produced no command rather than as a failure. */
        return RoboOk;
    }
    /*
     * One scratch buffer sized by the window's **byte** length, which is an upper bound on
     * its code points -- UTF-8 is never shorter than one byte per code point, so a window of
     * `n` bytes holds at most `n` characters and the decode cannot overflow. The alternative
     * -- decoding into the pool one chunk at a time -- would give a run several spans and
     * break the one thing a span promises (a run is one contiguous `[first, first + count)`).
     */
    codePoints = (uint32_t *)RoboCalloc(length, sizeof(*codePoints));
    if (!codePoints) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboOutOfMemory,
                               "could not hold a text run's characters");
    }
    offset = start;
    while (offset < start + length && count < length) {
        Uchar32 codePoint = 0;
        size_t next = offset;

        if (UnicodeUtf8Decode(view.text, view.textLength, offset,
                                    RoboUnicodeReplace, &codePoint, &next) !=
                RoboUnicodeOk ||
            next <= offset) {
            RoboFree(codePoints);
            return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboNotFound,
                                   "a text run's bytes could not be decoded");
        }
        codePoints[count] = (uint32_t)codePoint;
        count++;
        offset = next;
    }
    result = PaintListAddText(list, codePoints, count, outSpan, diag);
    RoboFree(codePoints);
    if (result != RoboOk) {
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, result,
                               "a text run's characters could not be stored");
    }
    return RoboOk;
}

/**
 * Emit the `TEXT` operation for text object @p id, or report that there is none to emit.
 *
 * @param outEmitted non-NULL; set to 1 when an operation was appended, 0 when the run
 *                    draws nothing -- which is a **statement about the document** rather
 *                    than a failure, so the caller counts it and the pass continues. The
 *                    reasons are the drawing rule's (`RoboPaintTypes.h`): no colour to
 *                    draw in, no characters, nothing placed it, or a range that could not
 *                    be read.
 *
 * @return RoboOk; a failure only when the list could not grow.
 */
static Result PaintEmitText(PaintContext *context,
                                    const LayoutTree *tree,
                                    LayoutNodeId id,
                                    size_t depth,
                                    LayoutPoint origin,
                                    const PaintClip *clip,
                                    const PaintOpacity *opacity,
                                    PaintList *list,
                                    int *outEmitted,
                                    PaintDiag *diag)
{
    const CssComputedStyle *computed;
    LayoutTextRun run;
    LayoutBaselineMetrics baseline;
    PaintBoxes local;
    PaintBoxes boxes;
    PaintOperation operation;
    PaintTextRun text;
    HtmlNodeHandle handle;
    CssColor color;
    CssColor elementColor;
    LayoutTextStyle font;
    int draws = 0;
    int contextual = 0;
    Result result;

    *outEmitted = 0;

    /* The placement, through the **same door a box's three rectangles come from**: it
     * declines a text run's inset records and says so with `insetsMeasured`, so a run's
     * border, padding and content box are one rectangle and no second derivation of "a run
     * has no box layers" exists in this module. */
    if (PaintBoxesOf(tree, id, &local, NULL) != RoboOk) {
        return RoboOk;
    }
    if (LayoutObjectTextRun(tree, id, &run, NULL) != RoboOk) {
        return RoboOk;
    }
    if (LayoutObjectDomNode(tree, id, &handle) != RoboOk) {
        return RoboOk;
    }

    /*
     * The colour, read exactly where a box's is read: the run's style **is its parent
     * element's**, borrowed, because every property a run needs is inherited and RoboCSS
     * already distributed them (`RoboLayoutText.h` argues it). `currentColor` resolves
     * against the same style, which is the element's own `color` -- and an element with no
     * usable `color` draws no text, counted rather than guessed at.
     */
    computed = LayoutObjectStyle(tree, id);
    elementColor = PaintElementColor(computed);
    color = PaintPropertyColor(computed, "color");
    /* `color`'s initial value is not "nothing": an undeclared text colour is the
     * element's resolved colour, so the `defaultForNone` flag is set here exactly as it
     * is for a border, and for the same reason. */
    PaintResolveColor(color, elementColor, 1, &color, &draws, &contextual);
    if (contextual) {
        context->stats.contextualColors++;
    }
    if (!draws) {
        return RoboOk;
    }

    /*
     * The font inputs, **through the layer that resolved them for measurement** rather than
     * re-read from the computed style here: `LayoutTextStyleResolve` is the one
     * place a computed `font-size` becomes a pixel size with CSS's initial values applied,
     * and a second reader would be a second answer to "what size is this text". The record
     * then travels with the run, so the size it was measured at is the size it is drawn at
     * by construction.
     */
    if (LayoutTextStyleResolve(computed, &font) != RoboOk) {
        return RoboOk;
    }

    if (LayoutObjectBaseline(tree, id, &baseline, NULL) != RoboOk) {
        return RoboOk;
    }

    PaintTextRunInit(&text);
    if (PaintReadText(list, &handle, run.range, &text.span, diag) != RoboOk ||
        PaintTextSpanIsEmpty(text.span)) {
        /* A run whose characters could not be read draws nothing. The failure is *this
         * run's*, not the document's: the rest of it is painted, and `textRuns` against
         * `texts` says one run produced no command. */
        return RoboOk;
    }
    /*
     * The baseline, through the offset door rather than off `ascent`: the two are equal for
     * every value this build produces, and the door is the one that **says so** -- a painter
     * that read `ascent` would be relying on a convention `RoboLayoutBaseline.h` states as a
     * convention, in a module that is not allowed to know it.
     */
    {
        int32_t offset = 0;

        if (LayoutBaselineOffset(&baseline, &offset) != RoboOk) {
            return RoboOk;
        }
        text.baselineOffset = offset;
    }
    text.font = font;
    /*
     * And the run is **shaped here** (12.16), which is the phase's whole change of order: the glyphs exist
     * as the document is recorded rather than as each frame is drawn, so a display list replay walks numbers
     * and no `cmap` is consulted anywhere below this file. The characters are read back out of the pool the
     * line above just stored them in -- borrowed, not copied, for the duration of the call.
     *
     * A run this cannot shape is a run whose **glyphs** are missing, never one whose characters are: the
     * failure is this build running out of memory, and it is reported rather than counted because the rest of
     * the document was already recorded against a list that is still consistent.
     */
    {
        const uint32_t *codePoints = PaintTextPoolCodePoints(PaintListTextPool(list));
        PaintGlyphSpan glyphSpan;
        PaintFontToken fontToken;
        /*
         * The face, **from the run** when it carries one (Phase 12.17) and from the pass's own argument
         * otherwise.
         *
         * The measurer selected it from the run's font request and recorded it on the run, so a mixed-font
         * document draws each run in the face it was measured with -- one selection, threaded, rather than
         * a second resolution here that could disagree. A run with no face (a document built with a single
         * face and no provider) falls back to the pass's face, which keeps the pre-12.17 configuration
         * working unchanged, and a run with neither records its characters and no glyphs (`noFont`).
         */
        FontFace *face = run.face ? run.face : context->textFace;

        result = PaintShapeRun(context, list, face, codePoints, text.span, &font, &glyphSpan,
                                 &fontToken, diag);
        if (result != RoboOk) {
            return result;
        }
        text.glyphs = glyphSpan;
        text.fontToken = fontToken;
    }

    (void)PaintBoxesTranslate(&local, origin.x, origin.y, &boxes, NULL);

    PaintOperationInit(&operation);
    operation.kind = RoboPaintOpText;
    operation.node = id;
    operation.depth = depth;
    operation.boxes = boxes;
    operation.origin = origin;
    operation.clip = *clip;
    /*
     * The opacity **in force**, passed in rather than read from `computed` -- and this is the
     * one place in this file where a style is deliberately *not* consulted. A run borrows the
     * style of the element that owns the text node, and that element opened its own opacity
     * scope around this run before the run was reached. Reading the declaration again here
     * would apply it twice, so a `0.5` element's text would draw at `0.25` while its
     * background drew at `0.5`. The value the traversal is carrying is the answer; the style
     * is the question that was already asked one level up.
     */
    operation.opacity = *opacity;
    operation.color = color;
    operation.text = text;
    result = PaintListAppend(list, &operation, diag);
    if (result != RoboOk) {
        return result;
    }
    *outEmitted = 1;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* One image (11.7)                                                    */
/* ------------------------------------------------------------------ */

/*
 * An image is the first drawing kind whose appearance this module does **not** resolve, and
 * that is the whole of why it needs its own site rather than one more branch in the decoration
 * pass: a background's colour comes from the box's computed style and a border's from its used
 * record, where an image's pixels come from a **resource** the document attached to the element.
 * So the reads are different, and the two reads this site makes are both RoboHTML's own doors --
 * `LayoutObjectDomNode` for the element a layout object was built for (the same door
 * 11.6's text branch uses for its characters) and `HtmlElementImageGet` for the resource
 * attached to it.
 *
 * Nothing here fetches, decodes or sizes anything. A `src` is a fact about the document and was
 * resolved before the paint pass ran; the destination rectangle is the box's **content box**,
 * which is layout's answer to "where does this element's content go" and the same answer for a
 * replaced element as for any other. Intrinsic sizing -- an `<img>` that is as large as its
 * resource without a CSS declaration -- is RoboLayout's work and is not implemented for replaced
 * elements in this build, which is why a document must size its images (`paint/docs/Images.md`
 * states the limitation in full).
 */

/**
 * Emit the `IMAGE` operation for the box of image element @p id, if that is what the box is.
 *
 * @param outIsImage  non-NULL; set to 1 when the box's element **is** an `<img>`, which is the
 *                      fact the caller counts. A box that is not an image element is not a
 *                      skipped image -- it is simply not this site's business -- and the two are
 *                      kept apart here rather than inferred from a count afterwards.
 * @param outEmitted   non-NULL; set to 1 when an operation was appended, 0 when there is no
 *                      image to draw -- a **statement about the document** rather than a failure,
 *                      so the caller counts it and the pass continues. The reasons are the
 *                      missing-image policy's: no `src`, nothing resolved, a decode that failed
 *                      or was refused, a resource without a pixel surface, or dimensions this
 *                      build will not carry into a record (`RoboPaintImage.h`).
 *
 * @return RoboOk; a failure only when the list could not grow.
 */
static Result PaintEmitImage(const LayoutTree *tree,
                                     LayoutNodeId id,
                                     size_t depth,
                                     const PaintBoxes *boxes,
                                     LayoutPoint origin,
                                     const PaintClip *clip,
                                     const PaintOpacity *opacity,
                                     PaintList *list,
                                     int *outIsImage,
                                     int *outEmitted,
                                     PaintDiag *diag)
{
    HtmlNodeHandle handle;
    HtmlImage *resource = NULL;
    GfxImage *pixels;
    PaintOperation operation;
    PaintImage payload;
    Result result;

    *outIsImage = 0;
    *outEmitted = 0;

    if (LayoutObjectDomNode(tree, id, &handle) != RoboOk) {
        /* A box with no element behind it -- a generated one -- is not an image element, and
         * that is an answer rather than a failure. */
        return RoboOk;
    }
    /*
     * The element-level question first, and through the element model rather than through a
     * string comparison: `HtmlElementIsTag` is the door the demo's own adapter uses to
     * ask the same thing, and it is what makes "this box is an `<img>`" one predicate instead of
     * a tag lookup repeated wherever a layer cares about images.
     */
    if (!HtmlElementIsTag(&handle, RoboHtmlTagImg)) {
        return RoboOk;
    }
    *outIsImage = 1;

    /*
     * The resource, borrowed. A getter that answers anything but RoboOk, and a NULL resource,
     * are one answer here: there is nothing attached to this element to draw. A `src` that names
     * nothing, a `src` no resolver has run for, a fetch that failed and bytes that would not
     * decode all arrive as "no resource" or as a resource whose state is not DECODED, and all of
     * them are counted rather than drawn -- the missing-image policy the phase states.
     */
    if (HtmlElementImageGet(&handle, &resource, NULL) != RoboOk || !resource) {
        return RoboOk;
    }
    if (MakeHtmlImageState(resource) != RoboHtmlImageStateDecoded) {
        return RoboOk;
    }
    pixels = HtmlImageGraphics(resource);
    /*
     * The payload, and it is the *record* layer's constructor that decides whether these pixels
     * are drawable at all: a resource with no surface, or with a zero or absurd dimension, is
     * refused there rather than here, so "which combinations are a drawing" has one home
     * (`PaintImageFromResource`). The diagnostic is deliberately not taken -- the refusal
     * is a counted skip, and a `diag` filled for a legitimate outcome would make a caller's
     * error path fire on an ordinary document.
     */
    if (PaintImageFromResource(pixels, &payload, NULL) != RoboOk) {
        return RoboOk;
    }

    PaintOperationInit(&operation);
    operation.kind = RoboPaintOpImage;
    operation.node = id;
    operation.depth = depth;
    /*
     * The three rectangles, in page space, and the **content box** among them is the image's
     * destination rectangle -- resolved one stage along, when the display builder turns this
     * record into a `draw-image` command. It is carried rather than stored a second time because
     * the record's own shape already says where content goes, and a fourth rectangle would be a
     * second answer to a question layout has already answered.
     */
    operation.boxes = *boxes;
    operation.origin = origin;
    operation.clip = *clip;
    /* The box's own opacity, passed in rather than read from its style -- the arrangement the
     * text site states in full, and for the same reason: the box opened its own scope, and
     * reading the declaration again here would apply it twice. */
    operation.opacity = *opacity;
    /* No colour and no style: an image record's appearance is its resource's pixels, so the
     * colour field stays `none`, which is what `PaintOperationIsValid` requires of it. */
    operation.image = payload;
    result = PaintListAppend(list, &operation, diag);
    if (result != RoboOk) {
        return result;
    }
    *outEmitted = 1;
    /*
     * The pass's own counters are deliberately **not** touched here: they belong to the caller,
     * beside the one place that knows whether the box it walked was an image element at all. A
     * site that counted its own trades one reader's mistake for a wrong total.
     */
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The walk                                                            */
/* ------------------------------------------------------------------ */

/** The boundary @p box's overflow puts in force for its contents, or @p clip unchanged.
 *
 * The clip edge is the **padding box**: CSS clips a box's overflowing contents at its
 * padding edge, and `LayoutOverflowEdgesClipRect` reads the same box one layer
 * down. Nothing here decides whether to clip -- the box's own used modes say that, and a
 * boundary that hides neither axis leaves the clip in force untouched, which is the case
 * every box in an ordinary document produces.
 */
static Result PaintChildClip(PaintContext *context,
                                     const LayoutTree *tree,
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
    }    context->stats.clipped++;
    return PaintClipIntersect(clip, &boxes->padding, overflow.horizontal,
                                    overflow.vertical, out, diag);
}

/**
 * The opacity @p box's own declaration puts in force for **itself and everything under it**.
 *
 * The product of the strength already in force and this box's own, which is the whole of the
 * arithmetic: opacity composes along a path, so a `0.5` box inside a `0.5` box is a `0.25`
 * subtree. Nothing here decides *whether* to open a scope -- the caller compares the result
 * against what was in force, exactly as the clip's caller does, so both state kinds open a
 * scope under one rule: **only when the value in force actually changes**.
 *
 * `opacityDeclared` is counted here, beside the read that finds it, and it counts a fact
 * about the *document* -- boxes whose declaration is not fully opaque -- in exactly the way
 * `clipped` counts boxes whose used modes hide an axis. Both are counted only for a **placed**
 * box, because both are reached on the path this pass paints; an unplaced box is counted in
 * `unplaced` and nothing below it is asked about.
 *
 * Two cases that are not failures and are worth naming, since this is where a reader will look
 * for them: a box with **no computed style** declares nothing and is opaque (the same answer
 * `withoutStyle` gives one layer down), and a declaration that arrived **out of range** was
 * already clamped where it was computed (`RoboCSSOpacity.h`), so what reaches the product is a
 * record this build can draw with.
 *
 * The one deviation from CSS this inherits is `RoboPaintOpacity.h`'s and not this call's: the
 * value is applied per drawing rather than to a flattened subtree, because the group form
 * needs an offscreen layer this build has no compositor for.
 */
static PaintOpacity PaintChildOpacity(PaintContext *context,
                                               const LayoutTree *tree,
                                               LayoutNodeId id,
                                               const PaintOpacity *opacity)
{
    PaintOpacity declared = PaintElementOpacity(LayoutObjectStyle(tree, id));

    if (!PaintOpacityIsOpaque(&declared)) {
        context->stats.opacityDeclared++;
    }
    return PaintOpacityMultiply(opacity, &declared);
}

/**
 * Paint @p id and, under it, everything it contains.
 *
 * @param placed  non-zero when @p origin is a real containing-block content origin --
 *                that is, when every ancestor was a placement. A box reached under a box
 *                nothing placed has no origin to be painted at, so it is counted and its
 *                subtree is walked for the counts and emitted nowhere.
 */
static Result PaintWalk(PaintContext *context,
                                const LayoutTree *tree,
                                LayoutNodeId id,
                                size_t depth,
                                int placedAncestor,
                                LayoutPoint origin,
                                const PaintClip *clip,
                                const PaintOpacity *opacity,
                                PaintList *list,
                                PaintDiag *diag)
{
    LayoutObjectKind kind = MakeLayoutObjectKind(tree, id);
    LayoutNodeId child;
    int placed = 0;
    PaintBoxes local;
    PaintBoxes boxes;
    LayoutPoint childOrigin = origin;
    PaintClip childClip = *clip;
    PaintOpacity childOpacity = *opacity;
    int clipScope = 0;
    int opacityScope = 0;

    if (!LayoutObjectKindIsKnown(kind) || kind == RoboLayoutObjectNone) {
        /* A child link that names nothing. Reported rather than skipped: a pass that
         * painted the rest of the document and said nothing would hand back a list
         * nobody asked for (RoboPaintContext.h). */
        return RoboPaintDiag(diag, RoboPaintDomainTraversal, RoboError,
                               "a child link names no object in this tree");
    }

    context->stats.visited++;
    if (depth > context->stats.depthMax) {
        context->stats.depthMax = (uint32_t)depth;
    }

    if (kind == RoboLayoutObjectText) {
        /*
         * A text run (11.6): a leaf, so there is nothing below it to walk, and one
         * **drawing** operation rather than a structural triple -- because a run is not a
         * box and the `BEGIN_BOX`/`CONTENT`/`END_BOX` skeleton is a statement about boxes.
         *
         * `placedAncestor` is honoured here exactly as it is for a box, and for the same
         * reason: a run's paint coordinates are its parent's content origin plus what
         * layout wrote, so a run inside a box nothing placed has no coordinates to be
         * emitted at. The two counters split the runs the traversal reached into the ones
         * that produced a command and the ones that draw nothing -- an identity by
         * construction (`RoboPaintContext.h`), so "this document has text and none of it
         * was painted" is a number a reader sees rather than a silence.
         */
        int emitted = 0;

        context->stats.textRuns++;
        if (placedAncestor &&
            PaintEmitText(context, tree, id, depth, origin, clip, opacity, list, &emitted,
                            diag) != RoboOk) {
            return diag ? diag->code : RoboError;
        }
        if (emitted) {
            context->stats.texts++;
        } else {
            context->stats.textSkipped++;
        }
        return RoboOk;
    }

    /* The one number a later paint-invalidation pass owes, counted where it can be:
     * a box whose only staleness is paint-visible. Read through layout's own door, so
     * "does this box need layout?" is answered by the model that maintains the bits. */
    {
        uint32_t state = LayoutTreeInvalidationState(tree, id);

        if ((state & RoboLayoutInvalidationStyle) != 0u &&
            (state & RoboLayoutInvalidationLayoutBits) == 0u) {
            context->stats.styleOnly++;
        }
    }

    if (placedAncestor &&
        PaintBoxesOf(tree, id, &local, NULL) == RoboOk) {
        placed = 1;
    }
    if (!placed) {
        /* Nothing placed this box, so there is no rectangle and no content origin for
         * anything inside it. Counted, walked for the counts, and emitted nowhere. */
        context->stats.unplaced++;
        child = RoboLayoutNodeIdNone;
        if (LayoutObjectFirstChild(tree, id, &child) != RoboOk) {
            return RoboOk;
        }
        while (child != RoboLayoutNodeIdNone) {
            Result result = PaintWalk(context, tree, child, depth + 1u, 0, origin,
                                              clip, opacity, list, diag);

            if (result != RoboOk) {
                return result;
            }
            if (LayoutObjectNextSibling(tree, child, &child) != RoboOk) {
                break;
            }
        }
        return RoboOk;
    }

    /* Into one page space: the accumulated origin added to what layout wrote. */
    (void)PaintBoxesTranslate(&local, origin.x, origin.y, &boxes, NULL);

    /*
     * The box's **own** opacacity, and the whole of what this site decides: the strength in
     * force for the box and everything under it is the outer strength times the box's
     * declaration, and the scope below opens only when that product differs from what was
     * already in force.
     */
    childOpacity = PaintChildOpacity(context, tree, id, opacity);
    opacityScope = !PaintOpacityEqual(&childOpacity, opacity);

    {
        Result result = PaintEmitStructural(list, RoboPaintOpBeginBox, id,
                                                     depth, &boxes, origin, clip, opacity,
                                                     diag);

        if (result != RoboOk) {
            return result;
        }
    }
    context->stats.boxes++;

    /*
     * The **opacity scope** (11.9), opened immediately after the box's own marker and before
     * anything the box draws.
     *
     * Its placement is the one structural departure from the clip scope, and it is not a style
     * choice: **a clip belongs to a box's contents and an opacity belongs to the box itself.**
     * CSS paints an element's own `background-color` and borders at the element's opacity --
     * a `0.5` red box is a half-transparent red box -- so the scope must already be open when
     * the first decoration is emitted. The clip cannot be, because the same CSS paints that
     * box's own background under its *parent's* boundary; that is why the one pair opens at the
     * box's content boundary and the other at its marker.
     *
     * The two pairs therefore nest differently around the same box, and the rule a reader can
     * use on both is a single rule: **every record carries the state in force at its own
     * position in the sequence.** `BEGIN_BOX` stands before the push, so it carries the
     * enclosing strength; the decorations, the content boundary, the subtree and the pop all
     * stand inside it and carry the box's; and `END_BOX` stands after the pop, so it carries
     * the enclosing strength again. That is the same rule that makes `POP_CLIP` and `END_BOX`
     * disagree about the clip, and the same rule that makes a pair self-identifying -- both
     * halves of a scope carry the value the pair establishes.
     */
    if (opacityScope) {
        Result result = PaintEmitStructural(list, RoboPaintOpPushOpacity, id,
                                                     depth, &boxes, origin, clip,
                                                     &childOpacity, diag);

        if (result != RoboOk) {
            return result;
        }
        context->stats.opacityScopes++;
    }

    /*
     * The decorations carry the box's own depth, so that a decoration and its marker can
     * never disagree about how deep the box is, and they carry the **box's own** opacity
     * rather than the enclosing one -- see the scope above.
     */
    {
        Result result = PaintBoxDecorations(context, tree, id, depth, &boxes,
                                                     origin, clip, &childOpacity, list, diag);

        if (result != RoboOk) {
            return result;
        }
    }

    /*
     * The box's **content image** (11.7), emitted inside the box's own group -- after its
     * decorations and before its content boundary.
     *
     * The placement is CSS's paint order for a **replaced element**, and it is the one thing
     * that distinguishes this record from a text run: an `<img>` has no object of its own, so
     * what its resource draws *is* the element's content, painted after the element's own
     * background and border and before anything the element contains. A `TEXT` record is emitted
     * a level *below* its box for the opposite reason -- a run **is** an object, and the box it
     * belongs to is its parent -- so the two drawing kinds that carry no colour reach the stream
     * at two different depths, and both because the tree says so.
     *
     * The counters are split here rather than inside the site because "the box's element is an
     * image" and "the image drew" are different facts: `imageElements` counts the first,
     * `images` and `imageSkipped` split it. A document with no `<img>` at all therefore leaves
     * all three at zero, which is how a reader tells it apart from one whose every image failed.
     */
    {
        int isImage = 0;
        int emitted = 0;

        if (PaintEmitImage(tree, id, depth, &boxes, origin, clip, &childOpacity,
                             list, &isImage, &emitted, diag) != RoboOk) {
            return diag ? diag->code : RoboError;
        }
        if (isImage) {
            context->stats.imageElements++;
            if (emitted) {
                context->stats.images++;
            } else {
                context->stats.imageSkipped++;
            }
        }
    }

    /*
     * The **content boundary**, emitted for every placed box whether or not it declared
     * a background, drew a border or has children -- because what it states is a fact
     * about the sequence. It is the explicit end of this box's own records and the start
     * of its subtree's: a consumer reads a box's own painting as `[BEGIN_BOX .. CONTENT]`
     * and its contents as `(CONTENT .. END_BOX)` without having to infer the boundary
     * from the depth of the next child's marker, and a childless box has a boundary just
     * as a parent does. It is a structural record, so it draws nothing and carries no
     * colour (RoboPaintTypes.h).
     */
    {
        Result result = PaintEmitStructural(list, RoboPaintOpContent, id, depth,
                                                     &boxes, origin, clip, &childOpacity,
                                                     diag);

        if (result != RoboOk) {
            return result;
        }
    }

    if (PaintChildClip(context, tree, id, &boxes, clip, &childClip, diag) != RoboOk) {
        return diag ? diag->code : RoboError;
    }
    /*
     * The **clip scope** (11.8), and the one condition that opens one: the boundary in force
     * for this box's contents must actually **differ** from the boundary the box itself was
     * entered under.
     *
     * The two are equal in the overwhelmingly common cases, and both are worth stating because
     * they are different documents: a box whose `overflow` is `visible` does not clip
     * (`PaintChildClip` returns the clip unchanged), and a box whose `overflow` is `hidden`
     * inside an ancestor that already bounded the same region does not narrow anything either.
     * Neither establishes a scope, and the count of the scopes this traversal *declines* to
     * push is exactly `stats.clipped` minus `stats.clipScopes` (`RoboPaintContext.h`).
     *
     * It is emitted **after the box's content boundary** and left open until after the box's
     * last child, which is the arrangement the record's own comment argues: the boundary clips
     * the contents, and the box's own background and border are painted under the boundary it
     * was entered under rather than its own.
     */
    clipScope = !PaintClipEqual(&childClip, clip);
    if (clipScope) {
        Result result = PaintEmitStructural(list, RoboPaintOpPushClip, id,
                                                     depth, &boxes, origin, &childClip,
                                                     &childOpacity, diag);

        if (result != RoboOk) {
            return result;
        }
        context->stats.clipScopes++;
    }
    /* The containing block's content **origin**, in paint coordinates: the child's
     * layout geometry is relative to this point, which is what makes `local = boxes -
     * origin` the space layout actually wrote (RoboPaintOperation.h). */
    childOrigin = MakeGfxPoint(boxes.content.x, boxes.content.y);

    child = RoboLayoutNodeIdNone;
    if (LayoutObjectFirstChild(tree, id, &child) == RoboOk) {
        while (child != RoboLayoutNodeIdNone) {
            Result result = PaintWalk(context, tree, child, depth + 1u, placed,
                                              childOrigin, &childClip, &childOpacity, list,
                                              diag);

            if (result != RoboOk) {
                return result;
            }
            if (LayoutObjectNextSibling(tree, child, &child) != RoboOk) {
                break;
            }
        }
    }

    /*
     * The scope closes with the box it belongs to, and the pop carries the **same clip the
     * push carried** -- not the boundary being restored. That is what makes the pair
     * self-identifying: a builder checks the pop against the innermost open scope by comparing
     * the two records, and a consumer replaying the list restores the enclosing boundary by
     * popping its own stack rather than by reading a second value out of the payload.
     *
     * It is emitted before the box's `END_BOX`, so the box marker pair stays contiguous around
     * everything the box owns -- its own records and its subtree, scope included.
     */
    if (clipScope) {
        Result result = PaintEmitStructural(list, RoboPaintOpPopClip, id,
                                                     depth, &boxes, origin, &childClip,
                                                     &childOpacity, diag);

        if (result != RoboOk) {
            return result;
        }
    }

    /*
     * The opacity scope closes with the box it belongs to, immediately before the box's own
     * `END_BOX` -- so the marker pair `[BEGIN_BOX .. END_BOX]` brackets the scope and its
     * contents exactly as it brackets the clip scope inside it.
     *
     * Both halves of the pair carry **the same record the push carried**: the pop's value is
     * what the scope established, not what it restores. That is what makes a pair checkable
     * from a stream -- a builder compares the pop against the innermost open scope and a
     * consumer restores the enclosing strength by popping its own stack -- and it is the rule
     * the clip pair already states one field along. A debug dump resolves that the pop's value
     * matches the push's, both are the scope's strength, and the enclosing strength is
     * whatever the sibling before it used.
     */
    if (opacityScope) {
        Result result = PaintEmitStructural(list, RoboPaintOpPopOpacity, id,
                                                     depth, &boxes, origin, clip,
                                                     &childOpacity, diag);

        if (result != RoboOk) {
            return result;
        }
    }

    {
        Result result = PaintEmitStructural(list, RoboPaintOpEndBox, id,
                                                     depth, &boxes, origin, clip, opacity,
                                                     diag);

        if (result != RoboOk) {
            return result;
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The context                                                         */
/* ------------------------------------------------------------------ */

Result PaintContextCreate(PaintContext **outContext,
                                        PaintDiag *diag)
{
    PaintContext *context;

    if (!outContext) {
        return RoboPaintDiag(diag, RoboPaintDomainContext, RoboInvalidArgument,
                               "out_context is NULL");
    }
    *outContext = NULL;
    context = (PaintContext *)RoboCalloc(1u, sizeof(*context));
    if (!context) {
        return RoboPaintDiag(diag, RoboPaintDomainContext, RoboOutOfMemory,
                               "could not allocate the paint context");
    }
    /* calloc already gives the empty state -- zeroed counters, outcome NONE, no tree.
     * The explicit assignment is here so that the definition of "empty" lives in one
     * place rather than in the allocator's behaviour. */
    context->outcome = RoboPaintPassNone;
    context->tree = NULL;
    context->userData = NULL;
    context->textFace = NULL;
    *outContext = context;
    return RoboOk;
}

void PaintContextDestroy(PaintContext *context)
{
    /* The tree, the list and the caller's user data are references, so there is nothing
     * to release but the struct itself -- which is what makes the destroy safe in any
     * order relative to the things it was used with. */
    RoboFree(context);
}

void PaintContextReset(PaintContext *context)
{
    if (!context) {
        return;
    }
    memset(&context->stats, 0, sizeof(context->stats));
    context->outcome = RoboPaintPassNone;
    context->tree = NULL;
    /* userData is deliberately left alone: it belongs to the caller, and a reset that
     * dropped it would break a caller that installs it once and resets per pass -- the
     * normal way to use this door, and RoboLayout's context makes the same call. */
}

void PaintContextSetUserData(PaintContext *context, void *userData)
{
    if (context) {
        context->userData = userData;
    }
}

void *PaintContextUserData(const PaintContext *context)
{
    return context ? context->userData : NULL;
}

void PaintContextSetTextFace(PaintContext *context, FontFace *face)
{
    if (!context) {
        return;
    }
    context->textFace = face;
    /*
     * The two tables the shape below reads, parsed **once, here** -- the rule the drawing pass already states
     * for the face it draws with (`RoboPaintDecoration.c`): a `FontFace` is created with neither table, and a
     * glyph lookup or a metrics read against one that has not been parsed answers `RoboNotSupported` /
     * `Unavailable` rather than parsing it. Asking here means the first run of the document does not pay for it
     * and the answer does not depend on which run happens to be first.
     *
     * Both are **idempotent** and neither is fatal: a face whose tables will not parse shapes every glyph as
     * `.notdef`, which is the answer a face with no glyph for the text already gets, and the run counts its
     * misses. Deleting the face is the caller's business and this door keeps no reference to the result.
     */
    if (face) {
        (void)FontFaceParseCmap(face);
        (void)FontFaceParseMetrics(face);
    }
}

FontFace *PaintContextTextFace(const PaintContext *context)
{
    return context ? context->textFace : NULL;
}

Result PaintContextRun(PaintContext *context,
                                    const LayoutTree *tree,
                                    PaintList *list,
                                    PaintDiag *diag)
{
    size_t completed;
    LayoutNodeId root;
    Result result;

    if (!context || !tree || !list) {
        if (context) {
            context->outcome = RoboPaintPassFailed;
        }
        return RoboPaintDiag(diag, RoboPaintDomainContext, RoboInvalidArgument,
                               "context, tree and list are all required");
    }

    /*
     * Per-pass fields are reset; `passes` is cumulative and is not -- the same split
     * RoboLayout's context uses, and for the same reason: a caller reading stats after a
     * second pass wants that pass's numbers, and a caller asking how many passes have run
     * wants the total. Zeroing the record and then incrementing makes `passes`
     * permanently 1, which reads as "this context ran once" for a context that ran five
     * times.
     */
    completed = context->stats.passes;
    memset(&context->stats, 0, sizeof(context->stats));
    context->stats.passes = completed;
    context->stats.objects = LayoutTreeObjectCount(tree);
    context->tree = tree;

    /*
     * The list is cleared first, so a caller may reuse one list across passes and never
     * sees a previous document's operations. Clearing before the staleness check rather
     * than after is deliberate: a refused pass hands back an **empty** list rather than
     * the previous pass's, which is the only answer a caller can act on.
     */
    PaintListClear(list);

    /*
     * A tree whose geometry is not final is refused rather than painted
     * (RoboPaintContext.h): a list built from half-computed geometry is a wrong list, and
     * a caller that wanted a preview can run a layout pass first.
     */
    if (LayoutTreeNeedsLayout(tree)) {
        /*
         * A **fourth outcome, not a fifth error**: the walk did not run, the list is
         * empty, and `RoboOk` is the honest result code because nothing went wrong that
         * a caller cannot fix. A caller acts on the outcome; one that reads only the code
         * sees success with an empty list, which is exactly what a refused pass is.
         */
        context->outcome = RoboPaintPassStale;
        context->stats.passes++;
        return RoboOk;
    }

    root = LayoutTreeRoot(tree);
    if (root == RoboLayoutNodeIdNone) {
        /*
         * No root box: a document whose root element generates no box, or one that builds
         * no boxes at all. A legitimate pass that paints nothing -- `objects` is already
         * 0 and every counter is already right -- rather than an error.
         */
        context->outcome = RoboPaintPassPainted;
        context->stats.passes++;
        return RoboOk;
    }

    /*
     * The root is entered under the two states a document starts in: a boundary that hides
     * neither axis, and full strength. Neither is a *scope* -- both are what an empty stack
     * answers -- so they are stated here as the initial values rather than pushed as commands,
     * which is exactly why a document that declares no opacity anywhere produces a list with
     * no opacity command in it.
     *
     * The strength comes from the opacity module's own door rather than from a written `1.0`,
     * for the reason the clip's modes come from the overflow vocabulary: "fully opaque" is a
     * fact about CSS that the layer owning the property states, and a build that changed the
     * property's initial value would change one number rather than one number and a copy here.
     */
    {
        PaintOpacity opaque = PaintOpacityOpaque();

        result = PaintWalk(context, tree, root, 0u, 1, MakeGfxPoint(0, 0),
                            &(PaintClip){ { RoboCssOverflowVisible, 0, 0 },
                                                  { RoboCssOverflowVisible, 0, 0 } },
                            &opaque,
                            list, diag);
    }
    if (result != RoboOk) {
        context->outcome = RoboPaintPassFailed;
        return result;
    }

    context->stats.ops = PaintListCount(list);
    context->outcome = RoboPaintPassPainted;
    context->stats.passes++;
    return RoboOk;
}

void PaintContextStats(const PaintContext *context, PaintStats *out)
{
    if (!out) {
        return;
    }
    if (!context) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = context->stats;
}

PaintPassOutcome PaintContextOutcome(const PaintContext *context)
{
    return context ? context->outcome : RoboPaintPassNone;
}

const LayoutTree *PaintContextTree(const PaintContext *context)
{
    return context ? context->tree : NULL;
}
