/*
 * RoboLayout intrinsic sizing (Phase 19.1).
 *
 * The measurement layer RoboLayoutIntrinsic.h describes: min-content is the
 * widest unbreakable piece, max-content is the whole content unwrapped, and
 * fit-content is the clamp between them. Nothing here writes a result, changes a
 * coordinate or lays anything out -- it walks the tree 10.1 built, asks the 10.5
 * metrics seam how wide a run is, and combines the answers.
 *
 * The word split is the one non-obvious piece. A `min-content` text width is the
 * width of the widest word, and "word" here means a maximal run of code points
 * that are not CSS's collapsible whitespace -- found by decoding the node's own
 * bytes through RoboUnicode (so a multi-byte character is one code point, not
 * three bytes) and recording where the whitespace fell. Each segment is then
 * measured with its own code-point range through the same seam the whole node
 * uses, which is what keeps an intrinsic width a *shaped* measurement rather than
 * a byte count.
 */

#include "RoboLayout/RoboLayoutIntrinsic.h"

#include "RoboLayout/RoboLayoutObject.h"
#include "RoboLayout/RoboLayoutTree.h"

#include "RoboCSS/RoboCSSDisplay.h"

#include "RoboUnicode/RoboUnicode.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

int LayoutIntrinsicKindIsKnown(LayoutIntrinsicKind kind)
{
    return kind == RoboLayoutIntrinsicNone ||
           kind == RoboLayoutIntrinsicMinContent ||
           kind == RoboLayoutIntrinsicMaxContent ||
           kind == RoboLayoutIntrinsicFitContent;
}

const char *LayoutIntrinsicKindName(LayoutIntrinsicKind kind)
{
    switch (kind) {
    case RoboLayoutIntrinsicNone:
        return "none";
    case RoboLayoutIntrinsicMinContent:
        return "min-content";
    case RoboLayoutIntrinsicMaxContent:
        return "max-content";
    case RoboLayoutIntrinsicFitContent:
        return "fit-content";
    default:
        return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

void LayoutIntrinsicSizeInit(LayoutIntrinsicSize *size)
{
    if (!size) {
        return;
    }
    memset(size, 0, sizeof(*size));
}

void LayoutIntrinsicSizeClear(LayoutIntrinsicSize *size)
{
    LayoutIntrinsicSizeInit(size);
}

LayoutIntrinsicSize LayoutIntrinsicSizeZero(void)
{
    LayoutIntrinsicSize size;

    LayoutIntrinsicSizeInit(&size);
    return size;
}

LayoutIntrinsicSize LayoutIntrinsicSizeMake(GfxPixelX minContent,
                                            GfxPixelX maxContent,
                                            GfxPixelY minContentHeight,
                                            GfxPixelY maxContentHeight)
{
    LayoutIntrinsicSize size;

    size.minContent = minContent;
    size.maxContent = maxContent;
    size.minContentHeight = minContentHeight;
    size.maxContentHeight = maxContentHeight;
    return size;
}

int LayoutIntrinsicSizeIsValid(const LayoutIntrinsicSize *size)
{
    if (!size) {
        return 0;
    }
    if (size->minContent < 0 || size->maxContent < 0 ||
        size->minContentHeight < 0 || size->maxContentHeight < 0) {
        return 0;
    }
    return size->minContent <= size->maxContent;
}

int LayoutIntrinsicSizeEqual(const LayoutIntrinsicSize *a,
                             const LayoutIntrinsicSize *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->minContent == b->minContent && a->maxContent == b->maxContent &&
           a->minContentHeight == b->minContentHeight &&
           a->maxContentHeight == b->maxContentHeight;
}

GfxPixelX LayoutIntrinsicSizeWidth(const LayoutIntrinsicSize *size,
                                   LayoutIntrinsicKind kind)
{
    if (!size) {
        return 0;
    }
    if (kind == RoboLayoutIntrinsicMinContent) {
        return size->minContent;
    }
    /* `NONE`, `MAX_CONTENT` and `FIT_CONTENT` all answer the unwrapped width;
     * fit-content's clamp needs `available` and is `LayoutIntrinsicResolveFitContent`. */
    return size->maxContent;
}

GfxPixelX LayoutIntrinsicResolveFitContent(const LayoutIntrinsicSize *size,
                                           GfxPixelX available)
{
    if (!size) {
        return available;
    }
    if (available < size->minContent) {
        return size->minContent;
    }
    if (available > size->maxContent) {
        return size->maxContent;
    }
    return available;
}

/* ------------------------------------------------------------------ */
/* Measuring a text node                                               */
/* ------------------------------------------------------------------ */

/** Non-zero when @p codePoint is one of CSS's collapsible whitespace characters. */
static int IntrinsicIsCollapsibleWhitespace(Uchar32 codePoint)
{
    return codePoint == 0x20u || codePoint == 0x09u || codePoint == 0x0Au ||
           codePoint == 0x0Cu || codePoint == 0x0Du;
}

/** A code-point span, built without a door that would imply the wrong unit. */
static LayoutTextRange IntrinsicRange(uint32_t start, uint32_t end)
{
    LayoutTextRange range;

    range.start = start;
    range.end = end;
    return range;
}

/**
 * Measure one code-point span of @p handle, filling @p outRun.
 *
 * A thin wrapper over `LayoutTextMeasure` so the whole-node and per-word paths build
 * their request the same way, and so the resolved style is carried onto the request
 * exactly once.
 */
static Result IntrinsicMeasureRange(const HtmlNodeHandle *handle,
                                    LayoutTextRange range,
                                    const LayoutTextStyle *style,
                                    const CssComputedStyle *computed,
                                    const LayoutTextMetrics *metrics,
                                    LayoutTextRun *outRun,
                                    LayoutDiag *diag)
{
    LayoutTextRequest request;

    memset(&request, 0, sizeof(request));
    request.textNode = *handle;
    request.range = range;
    request.style = style;
    request.computed = computed;
    return LayoutTextMeasure(metrics, &request, outRun, diag);
}

Result LayoutIntrinsicMeasureTextNode(const HtmlNodeHandle *textNode,
                                      const CssComputedStyle *ownerStyle,
                                      const LayoutTextMetrics *metrics,
                                      LayoutIntrinsicSize *outSize,
                                      LayoutDiag *diag)
{
    HtmlNodeView view;
    LayoutTextStyle style;
    LayoutTextRun run;
    uint32_t codePoints = 0u;
    GfxPixelX minContent = 0;
    GfxPixelY minHeight = 0;
    GfxPixelY maxHeight = 0;
    size_t offset = 0u;
    uint32_t index = 0u;
    uint32_t wordStart = 0u;
    int inWord = 0;
    Result measured;

    if (!textNode || !outSize) {
        return RoboLayoutDiag(diag, RoboLayoutDomainIntrinsic, RoboInvalidArgument,
                              "a text node and an output size are required");
    }
    LayoutIntrinsicSizeInit(outSize);

    measured = LayoutTextStyleResolve(ownerStyle, &style);
    if (measured != RoboOk) {
        return measured;
    }

    /* The whole node: max-content, measured once, unwrapped. */
    measured = LayoutTextNodeCodePoints(textNode, &codePoints, diag);
    if (measured != RoboOk) {
        return measured;
    }
    measured = IntrinsicMeasureRange(textNode, LayoutTextRangeFull(codePoints), &style,
                                     ownerStyle, metrics, &run, diag);
    if (measured != RoboOk) {
        return measured;
    }
    outSize->maxContent = run.width;
    maxHeight = (GfxPixelY)run.height;

    measured = HtmlNodeGetView(textNode, &view, NULL);
    if (measured != RoboOk) {
        /* A handle that resolved for the code-point count must resolve here too; if
         * it does not, the max-content answer stands and min-content is zero. */
        outSize->minContent = 0;
        outSize->minContentHeight = maxHeight;
        outSize->maxContentHeight = maxHeight;
        return RoboOk;
    }

    /*
     * Walk the node's bytes once. `offset`/`index` are the byte and code-point
     * positions of the code point about to be read; a whitespace code point closes
     * the word that was open and a non-whitespace one opens one. The `next <= offset`
     * guard is here because a decode that stopped advancing would turn this loop into
     * a hang, and a hang is the one failure a range check cannot report -- the same
     * guard RoboLayoutText.c keeps for the same reason.
     */
    while (offset < view.textLength) {
        size_t next = offset;
        Uchar32 codePoint = 0;
        UnicodeStatus decoded = UnicodeUtf8Decode(view.text, view.textLength, offset,
                                                  RoboUnicodeReplace, &codePoint, &next);

        if (decoded != RoboUnicodeOk || next <= offset) {
            break;
        }
        if (IntrinsicIsCollapsibleWhitespace(codePoint)) {
            if (inWord) {
                measured = IntrinsicMeasureRange(textNode, IntrinsicRange(wordStart, index),
                                                 &style, ownerStyle, metrics, &run, diag);
                if (measured == RoboOk) {
                    if (run.width > minContent) {
                        minContent = run.width;
                    }
                    if ((GfxPixelY)run.height > minHeight) {
                        minHeight = (GfxPixelY)run.height;
                    }
                }
                inWord = 0;
            }
        } else if (!inWord) {
            wordStart = index;
            inWord = 1;
        }
        offset = next;
        index++;
    }
    if (inWord) {
        measured = IntrinsicMeasureRange(textNode, IntrinsicRange(wordStart, index), &style,
                                         ownerStyle, metrics, &run, diag);
        if (measured == RoboOk) {
            if (run.width > minContent) {
                minContent = run.width;
            }
            if ((GfxPixelY)run.height > minHeight) {
                minHeight = (GfxPixelY)run.height;
            }
        }
    }

    outSize->minContent = minContent;
    outSize->minContentHeight = minHeight;
    outSize->maxContentHeight = maxHeight;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Measuring a subtree                                                 */
/* ------------------------------------------------------------------ */

/**
 * Combine a child's intrinsic size into a box's running answer.
 *
 * `runningLine` is the width of the line the max-content layout is currently
 * forming: inline-level children add to it, and a block-level child closes it.
 */
static void IntrinsicCombineChild(LayoutIntrinsicSize *out,
                                  const LayoutIntrinsicSize *child,
                                  int inlineLevel,
                                  GfxPixelX *runningLine)
{
    /* min-content is the widest unbreakable piece anywhere in the subtree: a
     * maximum over every child, wherever the child sits. */
    if (child->minContent > out->minContent) {
        out->minContent = child->minContent;
    }
    if (child->minContentHeight > out->minContentHeight) {
        out->minContentHeight = child->minContentHeight;
    }

    if (inlineLevel) {
        *runningLine += child->maxContent;
        if (*runningLine > out->maxContent) {
            out->maxContent = *runningLine;
        }
    } else {
        /* A block child starts its own line: it is not summed with its siblings,
         * and the line it was accumulating on ends here. */
        if (child->maxContent > out->maxContent) {
            out->maxContent = child->maxContent;
        }
        *runningLine = 0;
    }
    if (child->maxContentHeight > out->maxContentHeight) {
        out->maxContentHeight = child->maxContentHeight;
    }
}

static Result IntrinsicMeasureObjectRecursive(const LayoutTree *tree,
                                              LayoutNodeId id,
                                              const LayoutTextMetrics *metrics,
                                              LayoutIntrinsicSize *outSize,
                                              LayoutDiag *diag)
{
    LayoutObject object;
    GfxPixelX runningLine = 0;
    LayoutNodeId child;
    Result measured;

    LayoutIntrinsicSizeInit(outSize);

    measured = LayoutObjectGet(tree, id, &object, diag);
    if (measured != RoboOk) {
        return measured;
    }

    if (object.kind == RoboLayoutObjectText) {
        return LayoutIntrinsicMeasureTextNode(&object.domNode, object.style, metrics,
                                              outSize, diag);
    }

    child = object.firstChild;
    while (child != RoboLayoutNodeIdNone) {
        LayoutIntrinsicSize childSize;
        LayoutNodeId nextChild = RoboLayoutNodeIdNone;
        LayoutObject childObject;
        int inlineLevel;

        measured = IntrinsicMeasureObjectRecursive(tree, child, metrics, &childSize, diag);
        if (measured != RoboOk) {
            return measured;
        }

        if (LayoutObjectGet(tree, child, &childObject, NULL) == RoboOk) {
            nextChild = childObject.NextSibling;
            if (childObject.kind == RoboLayoutObjectText) {
                /* A text object is inline content by construction (10.5). */
                inlineLevel = 1;
            } else {
                inlineLevel = CssDisplayIsInlineLevel(childObject.display) ||
                              CssDisplayIsInlineBlock(childObject.display);
            }
        } else {
            inlineLevel = 1;
        }

        IntrinsicCombineChild(outSize, &childSize, inlineLevel, &runningLine);
        child = nextChild;
    }

    if (!LayoutIntrinsicSizeIsValid(outSize)) {
        /* The combination rule cannot break `min <= max`, but a measurement that
         * answered a negative width could; refuse rather than carry an impossible
         * size downstream. */
        return RoboLayoutDiag(diag, RoboLayoutDomainIntrinsic, RoboError,
                              "an intrinsic measurement produced an invalid size");
    }
    return RoboOk;
}

Result LayoutIntrinsicMeasureObject(const LayoutTree *tree,
                                    LayoutNodeId id,
                                    const LayoutTextMetrics *metrics,
                                    LayoutIntrinsicSize *outSize,
                                    LayoutDiag *diag)
{
    if (!tree || !outSize) {
        return RoboLayoutDiag(diag, RoboLayoutDomainIntrinsic, RoboInvalidArgument,
                              "a tree and an output size are required");
    }
    /* A NONE id is a caller bug -- the same rule `LayoutObjectGet` states -- while an id
     * that names no *live* box is a miss, and the two are told apart because a caller
     * acts on them differently. */
    if (id == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainIntrinsic, RoboInvalidArgument,
                              "the object id is NONE");
    }
    if (!LayoutObjectIsLive(tree, id)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainIntrinsic, RoboNotFound,
                              "the object id is not live in this tree");
    }
    return IntrinsicMeasureObjectRecursive(tree, id, metrics, outSize, diag);
}

Result LayoutIntrinsicMeasureTree(const LayoutTree *tree,
                                  const LayoutTextMetrics *metrics,
                                  LayoutIntrinsicSize *outSize,
                                  LayoutDiag *diag)
{
    LayoutNodeId root;

    if (!tree || !outSize) {
        return RoboLayoutDiag(diag, RoboLayoutDomainIntrinsic, RoboInvalidArgument,
                              "a tree and an output size are required");
    }
    root = LayoutTreeRoot(tree);
    if (root == RoboLayoutNodeIdNone) {
        return RoboLayoutDiag(diag, RoboLayoutDomainIntrinsic, RoboNotFound,
                              "the tree has no root box to measure");
    }
    return LayoutIntrinsicMeasureObject(tree, root, metrics, outSize, diag);
}
