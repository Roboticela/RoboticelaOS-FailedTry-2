/*
 * RoboLayout intrinsic sizing (Phase 19.1).
 *
 * A box's *intrinsic* size is the size its **contents** ask for, before any
 * containing block has a say: how wide the widest unbreakable piece is
 * (`min-content`) and how wide the whole content would be on one line
 * (`max-content`). CSS writes those three spellings -- `min-content`,
 * `max-content`, `fit-content` -- as `width` and `height` values, and every
 * formatting context that has to shrink a box to fit (a float, an inline-block,
 * an absolutely positioned box with no width) needs the two numbers rather than a
 * declared length.
 *
 * This is a **measurement layer**, not a layout pass. It walks the layout tree
 * whose boxes 10.1 built, asks the RoboText metrics seam how wide a run is, and
 * combines the answers; it writes no result, changes no coordinate, and is
 * callable before anything has been placed. That is deliberate: an intrinsic size
 * is an *input* to layout, and a layer that computed one by laying the document
 * out would be answering the question with its own answer.
 *
 * Where the numbers come from, and what is deliberately not claimed
 *
 * `min-content` of a text run is the width of its **widest word** -- the words are
 * the unbreakable pieces, found by decoding the node's own bytes and splitting on
 * CSS's collapsible whitespace. `max-content` is the whole run measured once,
 * unwrapped. Both go through the one seam 10.5 built (`LayoutTextMetrics`), so an
 * intrinsic width is shaped text metrics and never a character count -- and a
 * build with no metrics source measures every run as zero, exactly as 10.5's
 * degradation, rather than inventing a width.
 *
 * `fit-content` is *not* a third measurement: it is `clamp(available, min-content,
 * max-content)`, and `LayoutIntrinsicResolveFitContent` is that arithmetic in one
 * place so no caller restates it.
 *
 * What is deliberately absent, and each a named later goal:
 *
 *   - **Intrinsic height as a function of width.** A box's height at a
 *     max-content width is one line; at a min-content width it is as many lines as
 *     the text wraps into, and *that* count is line breaking's. This layer reports
 *     the content's line extent at the width it measured, and says so, rather than
 *     pretending to wrap.
 *   - **Replaced elements.** An `<img>`'s intrinsic dimensions are its file's, and
 *     the image-loading seam that would answer them is 17.5's; this layer has an
 *     aspect-ratio-free record and no way to ask. A replaced element therefore
 *     contributes zero here until that seam is threaded through, which is stated
 *     rather than guessed.
 *   - **Flex/grid/table intrinsic sizing.** Those are 19.3, 19.4 and 19.6: each
 *     consumes these two numbers and adds its own track/flex arithmetic, and none
 *     of them belongs beside the measurement.
 *
 * The dependency direction is unchanged: RoboLayout consumes RoboText metrics
 * through `LayoutTextMetrics` and never parses a font. This header includes no
 * RoboFont type and no RoboText type.
 */

#ifndef ROBO_LAYOUT_INTRINSIC_H
#define ROBO_LAYOUT_INTRINSIC_H

#include <stdint.h>

#include "RoboBase/RoboError.h"

#include "RoboCSS/RoboCSSComputed.h"

#include "RoboHTML/RoboHTMLNode.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutText.h"
#include "RoboLayout/RoboLayoutTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The layout tree is only ever borrowed here, exactly as RoboLayoutObject.h borrows
 * it -- so the tag is forward-declared rather than pulling the whole tree header into
 * every translation unit that measures an intrinsic size. */
typedef struct LayoutTree LayoutTree;

/**
 * The three CSS intrinsic-width spellings, and "none of them".
 *
 * Carried on a resolved dimension so the *request* survives to the resolver: a
 * `width: max-content` and a `width: 300px` are different statements even when
 * they happen to resolve to the same number, and the difference is what a
 * diagnostic and a later invalidation rule read.
 */
typedef enum LayoutIntrinsicKind {
    /** Not an intrinsic request. */
    RoboLayoutIntrinsicNone = 0,
    /** The widest unbreakable piece of the content. */
    RoboLayoutIntrinsicMinContent = 1,
    /** The whole content, unwrapped. */
    RoboLayoutIntrinsicMaxContent = 2,
    /** `clamp(min-content, available, max-content)`. */
    RoboLayoutIntrinsicFitContent = 3
} LayoutIntrinsicKind;

/** Number of defined intrinsic kinds. */
#define RoboLayoutIntrinsicKindCount 4u

/** Non-zero when @p kind is one of the four above. */
int LayoutIntrinsicKindIsKnown(LayoutIntrinsicKind kind);

/** Human-readable name ("none", "min-content", "max-content", "fit-content");
 *  never NULL; "unknown" when undefined. */
const char *LayoutIntrinsicKindName(LayoutIntrinsicKind kind);

/**
 * The two widths (and the content's line extent) that describe a subtree's
 * intrinsic size.
 *
 * A record by value, owned by nobody. `minContent` is always <= `maxContent` in a
 * record this module produced; `IsValid` is the predicate, and the combination
 * rules are written so it cannot be violated by construction.
 *
 * The heights are the content's extent at the widths measured, and for a text run
 * that is the run's own line extent -- there is no wrapping here, so
 * `minContentHeight` equals `maxContentHeight` for a run and the two are kept
 * apart only because a box's combination of children can make them differ (a
 * taller child on one line than another).
 */
typedef struct LayoutIntrinsicSize {
    /** Width of the widest unbreakable piece. */
    GfxPixelX minContent;
    /** Width of the whole content on one line. */
    GfxPixelX maxContent;
    /** Content line extent for the min-content width. */
    GfxPixelY minContentHeight;
    /** Content line extent for the max-content width. */
    GfxPixelY maxContentHeight;
} LayoutIntrinsicSize;

/** Clear @p size to a zero box: every field zero, which is a *valid* size and the
 *  answer for content that asks for nothing. Safe on NULL. */
void LayoutIntrinsicSizeInit(LayoutIntrinsicSize *size);

/** Same as init. Safe on NULL. */
void LayoutIntrinsicSizeClear(LayoutIntrinsicSize *size);

/** The zero size, as a value. A maker rather than a setter, because there is
 *  nothing to set. */
LayoutIntrinsicSize LayoutIntrinsicSizeZero(void);

/** A size from its four numbers. Does **not** validate. */
LayoutIntrinsicSize LayoutIntrinsicSizeMake(GfxPixelX minContent,
                                            GfxPixelX maxContent,
                                            GfxPixelY minContentHeight,
                                            GfxPixelY maxContentHeight);

/**
 * Non-zero when @p size is internally consistent: both widths non-negative and
 * `minContent <= maxContent`, both heights non-negative.
 */
int LayoutIntrinsicSizeIsValid(const LayoutIntrinsicSize *size);

/** Field-by-field equality. Safe on NULL. */
int LayoutIntrinsicSizeEqual(const LayoutIntrinsicSize *a,
                             const LayoutIntrinsicSize *b);

/** The width @p kind names, out of @p size, ignoring @p available. `NONE` and
 *  `fit-content` answer the max-content width (fit-content needs @p available --
 *  see below). Safe on NULL; a NULL @p size answers 0. */
GfxPixelX LayoutIntrinsicSizeWidth(const LayoutIntrinsicSize *size,
                                   LayoutIntrinsicKind kind);

/**
 * `fit-content(available)`: `clamp(available, min-content, max-content)`.
 *
 * The one place the clamp is written. A NULL @p size answers @p available.
 */
GfxPixelX LayoutIntrinsicResolveFitContent(const LayoutIntrinsicSize *size,
                                           GfxPixelX available);

/* ------------------------------------------------------------------ */
/* Measuring                                                           */
/* ------------------------------------------------------------------ */

/**
 * The intrinsic size of a text node, from shaped RoboText metrics.
 *
 * `maxContent` is the whole node measured once. `minContent` is the widest
 * *word*: the node's bytes are decoded through RoboUnicode, split on CSS's
 * collapsible whitespace, and each maximal non-whitespace segment is measured
 * through @p metrics with its own code-point range. Content that is all
 * whitespace (or empty) has no word and answers `minContent` of zero while still
 * answering a max-content width -- the same distinction `LayoutTextIsCollapsibleWhitespace`
 * draws for the builder.
 *
 * Measurements go through `LayoutTextMeasure`, so a NULL or unusable @p metrics
 * measures every segment as zero with the range echoed -- the degradation 10.5
 * documents -- rather than failing. @p ownerStyle may be NULL, which yields CSS's
 * initial font inputs exactly as `LayoutTextMeasureNode` does.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or an ill-formed
 *         handle; RoboNotFound for a handle that does not resolve; whatever a
 *         failing measurer returned when it failed.
 */
Result LayoutIntrinsicMeasureTextNode(const HtmlNodeHandle *textNode,
                                      const CssComputedStyle *ownerStyle,
                                      const LayoutTextMetrics *metrics,
                                      LayoutIntrinsicSize *outSize,
                                      LayoutDiag *diag);

/**
 * The intrinsic size of one layout object and, for a box, its whole subtree.
 *
 * A text object measures its node (above). A box combines its children, and the
 * rule is the one that makes min-content and max-content mean what CSS says:
 *
 *   minContent        the **maximum** of the children's min-content widths --
 *                     the widest unbreakable piece anywhere in the subtree, which
 *                     is the narrowest the box can be without breaking a piece.
 *   maxContent        the widest line the content would form on one line: a
 *                     **running sum** of consecutive inline-level children, and
 *                     the maximum over that sum and every block-level child's own
 *                     max-content width. A block child starts a new line, so it is
 *                     not summed with its siblings.
 *   heights           the line extent at each width: the maximum child extent
 *                     beside the min-content width, and the maximum over the
 *                     max-content lines for the other.
 *
 * A box with no children -- or one whose children are all empty -- answers the
 * zero size, which is a valid intrinsic size rather than a failure.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a NONE id;
 *         RoboNotFound for an id that names no live box in @p tree; whatever a
 *         child's measurement returned when it failed.
 */
Result LayoutIntrinsicMeasureObject(const struct LayoutTree *tree,
                                    LayoutNodeId id,
                                    const LayoutTextMetrics *metrics,
                                    LayoutIntrinsicSize *outSize,
                                    LayoutDiag *diag);

/**
 * The intrinsic size of a whole tree, measured from its root box.
 *
 * `RoboNotFound` for a tree with no live root -- an empty document has no
 * intrinsic size to report, which is different from a root that asks for zero.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound for a
 *         tree with no root.
 */
Result LayoutIntrinsicMeasureTree(const struct LayoutTree *tree,
                                  const LayoutTextMetrics *metrics,
                                  LayoutIntrinsicSize *outSize,
                                  LayoutDiag *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_INTRINSIC_H */
