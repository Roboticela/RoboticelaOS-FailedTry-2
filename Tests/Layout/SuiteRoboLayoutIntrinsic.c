/*
 * RoboLayout intrinsic sizing suite (Phase 19.1) -- layout.intrinsic.*
 *
 * Three things are under test and they are told apart deliberately:
 *
 *   the **vocabulary** -- the four intrinsic kinds and their predicates and names,
 *   which is pure and needs no record;
 *
 *   the **record** -- `LayoutIntrinsicSize`, its validity, equality and the
 *   fit-content clamp, which is where the phase's arithmetic is written once;
 *
 *   the **measurement** -- a text node's min-content (its widest word) and
 *   max-content (the whole node), a box's combination of its children, and the
 *   `LayoutSizing` plumbing an intrinsic width resolves through.
 *
 * Measurement goes through a **scripted measurer**, the same shape 10.5's suite
 * uses: a per-code-point answer, so a case can state its expected numbers as
 * arithmetic on the text rather than as pixels read off one font. That is not a
 * shortcut -- the phase's claim is that an intrinsic width is a *shaped
 * measurement* (the seam answered), and the only way to prove the seam was reached
 * with the right spans is to make the answer depend on the span.
 *
 * The degradations are asserted as their own case: a build with no metrics source
 * measures every run as zero, exactly as 10.5 documents, rather than inventing a
 * width from a byte count.
 */

#include "RoboLayoutTestSupport.h"

#include "RoboLayout/RoboLayoutIntrinsic.h"
#include "RoboLayout/RoboLayoutSizing.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A scripted measurer                                                 */
/* ------------------------------------------------------------------ */

typedef struct layoutIntrinsicScript {
    int32_t perCodePoint;
    int32_t height;
    size_t calls;
    int fail;
} LayoutIntrinsicScript;

static Result LayoutIntrinsicScriptMeasure(void *userdata,
                                           const LayoutTextRequest *request,
                                           LayoutTextRun *outRun)
{
    LayoutIntrinsicScript *script = (LayoutIntrinsicScript *)userdata;

    if (!script || !request || !outRun || !request->style) {
        return RoboInvalidArgument;
    }
    script->calls++;
    if (script->fail) {
        return RoboError;
    }
    outRun->source = request->textNode;
    outRun->range = request->range;
    outRun->direction = request->style->direction;
    outRun->codePoints = LayoutTextRangeLength(request->range);
    outRun->width = (int32_t)outRun->codePoints * script->perCodePoint;
    outRun->height = script->height;
    outRun->ascent = script->height;
    outRun->descent = 0;
    return RoboOk;
}

static LayoutTextMetrics LayoutIntrinsicScriptMetrics(LayoutIntrinsicScript *script)
{
    LayoutTextMetrics metrics;

    LayoutTextMetricsInit(&metrics);
    metrics.userdata = script;
    metrics.measure = LayoutIntrinsicScriptMeasure;
    return metrics;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

typedef struct layoutIntrinsicFixture {
    LayoutDocument doc;
    LayoutFakeSource styles;
    LayoutStyleSource source;
    LayoutTree *tree;
} LayoutIntrinsicFixture;

static void LayoutIntrinsicBegin(LayoutIntrinsicFixture *fixture, const char *html)
{
    LayoutBuildOptions options;

    memset(fixture, 0, sizeof(*fixture));
    LayoutFakeInit(&fixture->styles);
    LayoutDocumentBuild(&fixture->doc, html);
    LayoutFakeAdd(&fixture->styles, "html", RoboCssDisplayBlock);
    LayoutFakeAdd(&fixture->styles, "body", RoboCssDisplayBlock);
    LayoutFakeAdd(&fixture->styles, "div", RoboCssDisplayBlock);
    LayoutFakeAdd(&fixture->styles, "p", RoboCssDisplayBlock);
    LayoutFakeAdd(&fixture->styles, "span", RoboCssDisplayInline);
    fixture->source = LayoutFakeSourceOf(&fixture->styles);
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->source;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                               &options, NULL, NULL),
                   RoboOk);
}

static void LayoutIntrinsicEnd(LayoutIntrinsicFixture *fixture)
{
    LayoutTreeDestroy(fixture->tree);
    fixture->tree = NULL;
    LayoutFakeDispose(&fixture->styles);
    LayoutDocumentRelease(&fixture->doc);
}

static LayoutNodeId LayoutIntrinsicBoxOf(LayoutIntrinsicFixture *fixture,
                                         const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    return LayoutObjectIdForDomNode(fixture->tree, &element);
}

/** The first text node under the first element named @p tag. */
static HtmlNodeHandle LayoutIntrinsicTextChildOf(const HtmlNodeHandle *element)
{
    HtmlNodeView view;
    HtmlNodeHandle child;

    HtmlNodeHandleClear(&child);
    if (!element || !HtmlNodeHandleIsWellformed(element)) {
        return child;
    }
    if (HtmlNodeGetView(element, &view, NULL) != RoboOk) {
        return child;
    }
    child = view.firstChild;
    while (HtmlNodeHandleIsWellformed(&child)) {
        HtmlNodeView childView;

        if (HtmlNodeGetView(&child, &childView, NULL) != RoboOk) {
            break;
        }
        if (childView.kind == RoboHtmlNodeKindText) {
            return child;
        }
        child = childView.NextSibling;
    }
    HtmlNodeHandleClear(&child);
    return child;
}

static HtmlNodeHandle LayoutIntrinsicTextNodeOf(LayoutIntrinsicFixture *fixture,
                                                const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);

    return LayoutIntrinsicTextChildOf(&element);
}

/** The whole text node's intrinsic size, through the object the builder made. */
static Result LayoutIntrinsicMeasureTextOf(LayoutIntrinsicFixture *fixture,
                                           const char *tag,
                                           const LayoutTextMetrics *metrics,
                                           LayoutIntrinsicSize *outSize)
{
    HtmlNodeHandle node = LayoutIntrinsicTextNodeOf(fixture, tag);

    if (!HtmlNodeHandleIsWellformed(&node)) {
        return RoboNotFound;
    }
    return LayoutIntrinsicMeasureTextNode(&node, NULL, metrics, outSize, NULL);
}

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void TestIntrinsicKindVocabulary(void)
{
    LayoutFixture();

    RoboCheckEqInt((int)RoboLayoutIntrinsicKindCount, 4);
    RoboCheckEqInt((int)RoboLayoutIntrinsicNone, 0);
    RoboCheckEqInt((int)RoboLayoutIntrinsicMinContent, 1);
    RoboCheckEqInt((int)RoboLayoutIntrinsicMaxContent, 2);
    RoboCheckEqInt((int)RoboLayoutIntrinsicFitContent, 3);

    RoboCheckEqInt(LayoutIntrinsicKindIsKnown(RoboLayoutIntrinsicNone), 1);
    RoboCheckEqInt(LayoutIntrinsicKindIsKnown(RoboLayoutIntrinsicMinContent), 1);
    RoboCheckEqInt(LayoutIntrinsicKindIsKnown(RoboLayoutIntrinsicMaxContent), 1);
    RoboCheckEqInt(LayoutIntrinsicKindIsKnown(RoboLayoutIntrinsicFitContent), 1);
    RoboCheckEqInt(LayoutIntrinsicKindIsKnown((LayoutIntrinsicKind)4), 0);
    RoboCheckEqInt(LayoutIntrinsicKindIsKnown((LayoutIntrinsicKind)77), 0);

    RoboCheckStrEq(LayoutIntrinsicKindName(RoboLayoutIntrinsicNone), "none");
    RoboCheckStrEq(LayoutIntrinsicKindName(RoboLayoutIntrinsicMinContent),
                   "min-content");
    RoboCheckStrEq(LayoutIntrinsicKindName(RoboLayoutIntrinsicMaxContent),
                   "max-content");
    RoboCheckStrEq(LayoutIntrinsicKindName(RoboLayoutIntrinsicFitContent),
                   "fit-content");

    /* Distinct, non-NULL, and an unknown kind is not a NULL the caller has to
     * guard: a name a diagnostic would print is a name for every value. */
    RoboCheck(LayoutIntrinsicKindName((LayoutIntrinsicKind)77) != NULL);
    RoboCheckStrEq(LayoutIntrinsicKindName((LayoutIntrinsicKind)77), "unknown");
    RoboCheck(strcmp(LayoutIntrinsicKindName(RoboLayoutIntrinsicMinContent),
                     LayoutIntrinsicKindName(RoboLayoutIntrinsicMaxContent)) != 0);
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

static void TestIntrinsicSizeRecord(void)
{
    LayoutIntrinsicSize size;
    LayoutIntrinsicSize other;

    LayoutFixture();

    LayoutIntrinsicSizeInit(&size);
    RoboCheckEqInt(size.minContent, 0);
    RoboCheckEqInt(size.maxContent, 0);
    RoboCheckEqInt(size.minContentHeight, 0);
    RoboCheckEqInt(size.maxContentHeight, 0);
    /* A zero size is *valid*: content that asks for nothing is an answer. */
    RoboCheckEqInt(LayoutIntrinsicSizeIsValid(&size), 1);

    LayoutIntrinsicSizeClear(&size);
    RoboCheckEqInt(size.maxContent, 0);

    other = LayoutIntrinsicSizeZero();
    RoboCheckEqInt(LayoutIntrinsicSizeEqual(&size, &other), 1);
    RoboCheckEqInt(LayoutIntrinsicSizeEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutIntrinsicSizeEqual(&size, NULL), 0);
    RoboCheckEqInt(LayoutIntrinsicSizeEqual(NULL, &size), 0);
    RoboCheckEqInt(LayoutIntrinsicSizeIsValid(NULL), 0);

    size = LayoutIntrinsicSizeMake(10, 30, 5, 7);
    RoboCheckEqInt(size.minContent, 10);
    RoboCheckEqInt(size.maxContent, 30);
    RoboCheckEqInt(size.minContentHeight, 5);
    RoboCheckEqInt(size.maxContentHeight, 7);
    RoboCheckEqInt(LayoutIntrinsicSizeIsValid(&size), 1);
    RoboCheckEqInt(LayoutIntrinsicSizeEqual(&size, &other), 0);

    /* min > max is impossible by construction and refused by the predicate. */
    other = LayoutIntrinsicSizeMake(40, 30, 5, 5);
    RoboCheckEqInt(LayoutIntrinsicSizeIsValid(&other), 0);
    /* A negative extent is never a size. */
    other = LayoutIntrinsicSizeMake(-1, 30, 0, 0);
    RoboCheckEqInt(LayoutIntrinsicSizeIsValid(&other), 0);
    other = LayoutIntrinsicSizeMake(10, 30, -2, 0);
    RoboCheckEqInt(LayoutIntrinsicSizeIsValid(&other), 0);

    /* The width a kind names out of the record. */
    size = LayoutIntrinsicSizeMake(10, 30, 5, 7);
    RoboCheckEqInt(LayoutIntrinsicSizeWidth(&size, RoboLayoutIntrinsicMinContent),
                   10);
    RoboCheckEqInt(LayoutIntrinsicSizeWidth(&size, RoboLayoutIntrinsicMaxContent),
                   30);
    /* `none` and `fit-content` both answer the unwrapped width; fit-content's clamp
     * needs an available width and is the door below. */
    RoboCheckEqInt(LayoutIntrinsicSizeWidth(&size, RoboLayoutIntrinsicNone), 30);
    RoboCheckEqInt(LayoutIntrinsicSizeWidth(&size, RoboLayoutIntrinsicFitContent),
                   30);
    RoboCheckEqInt(LayoutIntrinsicSizeWidth(NULL, RoboLayoutIntrinsicMinContent), 0);
}

static void TestIntrinsicFitContent(void)
{
    LayoutIntrinsicSize size;

    LayoutFixture();
    size = LayoutIntrinsicSizeMake(10, 30, 0, 0);

    /* clamp(available, min-content, max-content), held at both ends and inside. */
    RoboCheckEqInt(LayoutIntrinsicResolveFitContent(&size, 5), 10);
    RoboCheckEqInt(LayoutIntrinsicResolveFitContent(&size, 10), 10);
    RoboCheckEqInt(LayoutIntrinsicResolveFitContent(&size, 20), 20);
    RoboCheckEqInt(LayoutIntrinsicResolveFitContent(&size, 30), 30);
    RoboCheckEqInt(LayoutIntrinsicResolveFitContent(&size, 100), 30);
    /* A NULL record answers the available width rather than failing. */
    RoboCheckEqInt(LayoutIntrinsicResolveFitContent(NULL, 42), 42);
}

/* ------------------------------------------------------------------ */
/* Measuring a text node                                               */
/* ------------------------------------------------------------------ */

static void TestIntrinsicTextMinMax(void)
{
    LayoutIntrinsicFixture fixture;
    LayoutIntrinsicScript script;
    LayoutTextMetrics metrics;
    LayoutIntrinsicSize size;

    LayoutFixture();
    LayoutIntrinsicBegin(&fixture, "<html><body><p>hello world</p></body></html>");
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutIntrinsicScriptMetrics(&script);

    RoboCheckEqInt(LayoutIntrinsicMeasureTextOf(&fixture, "p", &metrics, &size),
                   RoboOk);
    /* Max-content is the whole node: 11 code points at 10px. */
    RoboCheckEqInt(size.maxContent, 110);
    /* Min-content is the widest *word*: "hello" and "world" are five each. */
    RoboCheckEqInt(size.minContent, 50);
    RoboCheckEqInt(size.maxContentHeight, 20);
    RoboCheckEqInt(size.minContentHeight, 20);
    RoboCheckEqInt(LayoutIntrinsicSizeIsValid(&size), 1);
    /* The whole node was measured once and each word once: two words, so three
     * calls -- which is what makes "the seam was reached with the right spans"
     * assertable rather than assumed. */
    RoboCheckEqInt(script.calls, 3u);

    LayoutIntrinsicEnd(&fixture);
}

static void TestIntrinsicTextLongWord(void)
{
    LayoutIntrinsicFixture fixture;
    LayoutIntrinsicScript script;
    LayoutTextMetrics metrics;
    LayoutIntrinsicSize size;

    LayoutFixture();
    /* One word with no break opportunity: min-content equals max-content, which is
     * the whole reason a long word overflows a narrow box. */
    LayoutIntrinsicBegin(&fixture, "<html><body><p>abcdefgh</p></body></html>");
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutIntrinsicScriptMetrics(&script);

    RoboCheckEqInt(LayoutIntrinsicMeasureTextOf(&fixture, "p", &metrics, &size),
                   RoboOk);
    RoboCheckEqInt(size.maxContent, 80);
    RoboCheckEqInt(size.minContent, 80);
    /* The whole node, then its single word: two measurements of the same span. */
    RoboCheckEqInt(script.calls, 2u);

    LayoutIntrinsicEnd(&fixture);
}

static void TestIntrinsicTextWhitespace(void)
{
    LayoutIntrinsicFixture fixture;
    LayoutIntrinsicScript script;
    LayoutTextMetrics metrics;
    LayoutIntrinsicSize size;

    LayoutFixture();
    LayoutIntrinsicBegin(&fixture, "<html><body><p>   </p></body></html>");
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutIntrinsicScriptMetrics(&script);

    /* The layout tree drops a whitespace-only text node, so the case drives the
     * measurement directly -- which is exactly the entry point a caller with a
     * text node of its own would use. */
    RoboCheckEqInt(LayoutIntrinsicMeasureTextOf(&fixture, "p", &metrics, &size),
                   RoboOk);
    RoboCheckEqInt(size.maxContent, 30);
    /* No word, so the narrowest the content can be is zero. */
    RoboCheckEqInt(size.minContent, 0);
    RoboCheckEqInt(LayoutIntrinsicSizeIsValid(&size), 1);
    /* Only the whole node was measured: there was no word to measure. */
    RoboCheckEqInt(script.calls, 1u);

    LayoutIntrinsicEnd(&fixture);
}

static void TestIntrinsicTextMultilingual(void)
{
    LayoutIntrinsicFixture fixture;
    LayoutIntrinsicScript script;
    LayoutTextMetrics metrics;
    LayoutIntrinsicSize size;
    HtmlNodeHandle node;
    uint32_t codePoints = 0u;

    LayoutFixture();
    /* "h\xC3\xA9llo w\xC3\xB6rld": eleven code points, thirteen bytes. A build that
     * counted bytes would answer 130/130 and this case would fail. */
    LayoutIntrinsicBegin(&fixture,
                         "<html><body><p>h\xC3\xA9llo w\xC3\xB6rld</p></body></html>");
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutIntrinsicScriptMetrics(&script);

    node = LayoutIntrinsicTextNodeOf(&fixture, "p");
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&node), 1);
    RoboCheckEqInt(LayoutTextNodeCodePoints(&node, &codePoints, NULL), RoboOk);
    RoboCheckEqInt(codePoints, 11u);

    RoboCheckEqInt(LayoutIntrinsicMeasureTextOf(&fixture, "p", &metrics, &size),
                   RoboOk);
    RoboCheckEqInt(size.maxContent, 110);
    RoboCheckEqInt(size.minContent, 50);

    LayoutIntrinsicEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Combining a subtree                                                 */
/* ------------------------------------------------------------------ */

static void TestIntrinsicInlineSum(void)
{
    LayoutIntrinsicFixture fixture;
    LayoutIntrinsicScript script;
    LayoutTextMetrics metrics;
    LayoutIntrinsicSize size;
    LayoutNodeId div;

    LayoutFixture();
    LayoutIntrinsicBegin(&fixture,
                         "<html><body><div><span>ab</span><span>cd</span></div></body></html>");
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutIntrinsicScriptMetrics(&script);

    div = LayoutIntrinsicBoxOf(&fixture, "div");
    RoboCheckEqInt(LayoutIntrinsicMeasureObject(fixture.tree, div, &metrics, &size,
                                                NULL),
                   RoboOk);
    /* Two inline children sum on one line: max-content is 20 + 20. */
    RoboCheckEqInt(size.maxContent, 40);
    /* min-content is the widest unbreakable piece anywhere, not the sum. */
    RoboCheckEqInt(size.minContent, 20);
    RoboCheckEqInt(size.maxContentHeight, 20);
    RoboCheckEqInt(size.minContentHeight, 20);
    RoboCheckEqInt(LayoutIntrinsicSizeIsValid(&size), 1);

    LayoutIntrinsicEnd(&fixture);
}

static void TestIntrinsicBlockReset(void)
{
    LayoutIntrinsicFixture fixture;
    LayoutIntrinsicScript script;
    LayoutTextMetrics metrics;
    LayoutIntrinsicSize size;
    LayoutNodeId div;

    LayoutFixture();
    LayoutIntrinsicBegin(&fixture,
                         "<html><body><div><p>ab</p><p>cdef</p></div></body></html>");
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutIntrinsicScriptMetrics(&script);

    div = LayoutIntrinsicBoxOf(&fixture, "div");
    RoboCheckEqInt(LayoutIntrinsicMeasureObject(fixture.tree, div, &metrics, &size,
                                                NULL),
                   RoboOk);
    /* Block children do not sum: each starts its own line, so max-content is the
     * widest of them (40, not 60). */
    RoboCheckEqInt(size.maxContent, 40);
    RoboCheckEqInt(size.minContent, 40);
    RoboCheckEqInt(LayoutIntrinsicSizeIsValid(&size), 1);

    LayoutIntrinsicEnd(&fixture);
}

static void TestIntrinsicTree(void)
{
    LayoutIntrinsicFixture fixture;
    LayoutIntrinsicScript script;
    LayoutTextMetrics metrics;
    LayoutIntrinsicSize size;

    LayoutFixture();
    LayoutIntrinsicBegin(&fixture,
                         "<html><body><div><span>ab</span><span>cd</span></div></body></html>");
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutIntrinsicScriptMetrics(&script);

    RoboCheckEqInt(LayoutIntrinsicMeasureTree(fixture.tree, &metrics, &size, NULL),
                   RoboOk);
    /* The root's answer is the document's: the `div` measured through two block
     * ancestors (`head` is empty), so the running line is entirely the `div`'s. */
    RoboCheckEqInt(size.maxContent, 40);
    RoboCheckEqInt(size.minContent, 20);

    LayoutIntrinsicEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Degradation and refusals                                            */
/* ------------------------------------------------------------------ */

static void TestIntrinsicUnmeasured(void)
{
    LayoutIntrinsicFixture fixture;
    LayoutIntrinsicSize size;
    LayoutNodeId div;

    LayoutFixture();
    LayoutIntrinsicBegin(&fixture,
                         "<html><body><div><span>ab</span><span>cd</span></div></body></html>");

    /* No metrics source: every run measures as zero, which is 10.5's documented
     * degradation rather than an invented width. */
    RoboCheckEqInt(LayoutIntrinsicMeasureTextOf(&fixture, "div", NULL, &size),
                   RoboNotFound); /* div is not a text node */
    div = LayoutIntrinsicBoxOf(&fixture, "div");
    RoboCheckEqInt(LayoutIntrinsicMeasureObject(fixture.tree, div, NULL, &size,
                                                NULL),
                   RoboOk);
    RoboCheckEqInt(size.maxContent, 0);
    RoboCheckEqInt(size.minContent, 0);
    RoboCheckEqInt(LayoutIntrinsicSizeIsValid(&size), 1);

    LayoutIntrinsicEnd(&fixture);
}

static void TestIntrinsicRefusals(void)
{
    LayoutIntrinsicFixture fixture;
    LayoutIntrinsicScript script;
    LayoutTextMetrics metrics;
    LayoutIntrinsicSize size;
    LayoutTree *empty = NULL;
    HtmlNodeHandle node;

    LayoutFixture();
    LayoutIntrinsicBegin(&fixture, "<html><body><p>ab</p></body></html>");
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutIntrinsicScriptMetrics(&script);
    node = LayoutIntrinsicTextNodeOf(&fixture, "p");
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&node), 1);

    RoboCheckEqInt(LayoutIntrinsicMeasureTextNode(NULL, NULL, &metrics, &size, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(LayoutIntrinsicMeasureTextNode(&node, NULL, &metrics, NULL, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(LayoutIntrinsicMeasureObject(NULL, LayoutIntrinsicBoxOf(&fixture, "p"),
                                                &metrics, &size, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(LayoutIntrinsicMeasureObject(fixture.tree, RoboLayoutNodeIdNone,
                                                &metrics, &size, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(LayoutIntrinsicMeasureObject(fixture.tree, (LayoutNodeId)9999u,
                                                &metrics, &size, NULL),
                   RoboNotFound);
    RoboCheckEqInt(LayoutIntrinsicMeasureTree(NULL, &metrics, &size, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(LayoutIntrinsicMeasureTree(fixture.tree, &metrics, NULL, NULL),
                   RoboInvalidArgument);

    /* A tree with no live root has no intrinsic size to report -- which is a
     * different fact from a root that asks for zero. */
    RoboCheckEqInt(LayoutTreeCreate(&empty, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeRoot(empty), RoboLayoutNodeIdNone);
    RoboCheckEqInt(LayoutIntrinsicMeasureTree(empty, &metrics, &size, NULL),
                   RoboNotFound);
    LayoutTreeDestroy(empty);

    /* A measurer that fails propagates rather than being read as a zero width. */
    script.fail = 1;
    RoboCheckEqInt(LayoutIntrinsicMeasureTextNode(&node, NULL, &metrics, &size, NULL),
                   RoboError);

    LayoutIntrinsicEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The sizing plumbing                                                 */
/* ------------------------------------------------------------------ */

static void TestIntrinsicDimension(void)
{
    LayoutDimension dim;
    LayoutDimension other;

    LayoutFixture();

    dim = LayoutDimensionIntrinsic(RoboLayoutIntrinsicMaxContent);
    RoboCheckEqInt(LayoutDimensionIsIntrinsic(&dim), 1);
    RoboCheckEqInt(LayoutDimensionIsAuto(&dim), 0);
    RoboCheckEqInt(LayoutDimensionIsDefinite(&dim), 0);
    RoboCheckEqInt(LayoutDimensionIsUnresolved(&dim), 0);
    RoboCheckEqInt(LayoutDimensionIsValid(&dim), 1);
    RoboCheckEqInt((int)LayoutDimensionIntrinsicKindOf(&dim),
                   (int)RoboLayoutIntrinsicMaxContent);

    /* An intrinsic dimension must name *which* intrinsic width; `NONE` names none
     * and is refused by the validity predicate. */
    other = LayoutDimensionIntrinsic(RoboLayoutIntrinsicNone);
    RoboCheckEqInt(LayoutDimensionIsValid(&other), 0);

    /* The kind is part of equality: `min-content` and `max-content` are different
     * requests even when they resolve to the same number. */
    other = LayoutDimensionIntrinsic(RoboLayoutIntrinsicMinContent);
    RoboCheckEqInt(LayoutDimensionEqual(&dim, &other), 0);
    other = LayoutDimensionIntrinsic(RoboLayoutIntrinsicMaxContent);
    RoboCheckEqInt(LayoutDimensionEqual(&dim, &other), 1);

    /* A dimension of another kind answers `NONE` and is not intrinsic. */
    other = LayoutDimensionAuto();
    RoboCheckEqInt(LayoutDimensionIsIntrinsic(&other), 0);
    RoboCheckEqInt((int)LayoutDimensionIntrinsicKindOf(&other),
                   (int)RoboLayoutIntrinsicNone);
    RoboCheckEqInt((int)LayoutDimensionIntrinsicKindOf(NULL),
                   (int)RoboLayoutIntrinsicNone);
    RoboCheckEqInt(LayoutDimensionIsIntrinsic(NULL), 0);
}

static void TestIntrinsicSizingRecord(void)
{
    LayoutSizing sizing;
    LayoutIntrinsicSize size;
    LayoutIntrinsicSize got;
    LayoutIntrinsicSize invalid;

    LayoutFixture();

    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingHasIntrinsic(&sizing), 0);
    RoboCheckEqInt(LayoutSizingIntrinsic(&sizing, &got), RoboNotFound);
    RoboCheckEqInt(LayoutSizingHasIntrinsic(NULL), 0);
    RoboCheckEqInt(LayoutSizingIntrinsic(NULL, &got), RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingIntrinsic(&sizing, NULL), RoboInvalidArgument);

    size = LayoutIntrinsicSizeMake(40, 120, 20, 20);
    RoboCheckEqInt(LayoutSizingSetIntrinsic(&sizing, size, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingHasIntrinsic(&sizing), 1);
    RoboCheckEqInt(LayoutSizingIntrinsic(&sizing, &got), RoboOk);
    RoboCheckEqInt(LayoutIntrinsicSizeEqual(&got, &size), 1);

    /* An invalid record is refused rather than stored, the same rule the bounds
     * doors follow. */
    invalid = LayoutIntrinsicSizeMake(200, 100, 0, 0);
    RoboCheckEqInt(LayoutSizingSetIntrinsic(&sizing, invalid, NULL),
                   RoboInvalidArgument);
    RoboCheckEqInt(LayoutSizingSetIntrinsic(NULL, invalid, NULL),
                   RoboInvalidArgument);
}

static void TestIntrinsicSizingResolve(void)
{
    LayoutSizing sizing;
    LayoutIntrinsicSize size;

    LayoutFixture();
    size = LayoutIntrinsicSizeMake(40, 120, 20, 20);

    /* max-content resolves from the measured size, source INTRINSIC. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetIntrinsic(&sizing, size, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingSetWidth(&sizing,
                                        LayoutDimensionIntrinsic(
                                            RoboLayoutIntrinsicMaxContent),
                                        NULL),
                   RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 120);
    RoboCheckEqInt((int)sizing.widthSource,
                   (int)RoboLayoutDimensionSourceIntrinsic);

    /* min-content is the widest word. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetIntrinsic(&sizing, size, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingSetWidth(&sizing,
                                        LayoutDimensionIntrinsic(
                                            RoboLayoutIntrinsicMinContent),
                                        NULL),
                   RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 40);
    RoboCheckEqInt((int)sizing.widthSource,
                   (int)RoboLayoutDimensionSourceIntrinsic);

    /* fit-content clamps against the available width, at both ends and inside. */
    {
        const GfxPixelX available[3] = {60, 10, 200};
        const GfxPixelX expect[3] = {60, 40, 120};
        size_t index;

        for (index = 0u; index < 3u; index++) {
            LayoutSizingInit(&sizing);
            RoboCheckEqInt(LayoutSizingSetIntrinsic(&sizing, size, NULL), RoboOk);
            RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, available[index],
                                                         NULL),
                           RoboOk);
            RoboCheckEqInt(LayoutSizingSetWidth(&sizing,
                                                LayoutDimensionIntrinsic(
                                                    RoboLayoutIntrinsicFitContent),
                                                NULL),
                           RoboOk);
            RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboOk);
            RoboCheckEqInt(sizing.resolved.width, expect[index]);
            RoboCheckEqInt((int)sizing.widthSource,
                           (int)RoboLayoutDimensionSourceIntrinsic);
        }
    }

    /* fit-content with no available width clamps against max-content -- the answer
     * with the most evidence behind it. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetIntrinsic(&sizing, size, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingSetWidth(&sizing,
                                        LayoutDimensionIntrinsic(
                                            RoboLayoutIntrinsicFitContent),
                                        NULL),
                   RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 120);
    RoboCheckEqInt((int)sizing.widthSource,
                   (int)RoboLayoutDimensionSourceIntrinsic);

    /* An intrinsic width with nothing measured degrades to the containing block,
     * exactly as AUTO does -- and to a failure when there is no available width. */
    LayoutSizingInit(&sizing);
    RoboCheckEqInt(LayoutSizingSetWidth(&sizing,
                                        LayoutDimensionIntrinsic(
                                            RoboLayoutIntrinsicMaxContent),
                                        NULL),
                   RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboNotFound);
    RoboCheckEqInt(LayoutSizingSetAvailableWidth(&sizing, 100, NULL), RoboOk);
    RoboCheckEqInt(LayoutSizingResolveWidth(&sizing, NULL), RoboOk);
    RoboCheckEqInt(sizing.resolved.width, 100);
    RoboCheckEqInt((int)sizing.widthSource,
                   (int)RoboLayoutDimensionSourceAvailable);
}

/* ------------------------------------------------------------------ */
/* Allocator balance                                                   */
/* ------------------------------------------------------------------ */

static void TestIntrinsicAllocatorBalance(void)
{
    LayoutIntrinsicScript script;
    LayoutTextMetrics metrics;
    LayoutIntrinsicSize size;
    size_t before;
    size_t index;

    LayoutFixture();
    before = LayoutLiveBlocks();
    memset(&script, 0, sizeof(script));
    script.perCodePoint = 10;
    script.height = 20;
    metrics = LayoutIntrinsicScriptMetrics(&script);

    for (index = 0u; index < 8u; index++) {
        LayoutIntrinsicFixture fixture;

        LayoutIntrinsicBegin(&fixture,
                             "<html><body><div><span>ab</span><span>cd</span></div></body></html>");
        RoboCheckEqInt(LayoutIntrinsicMeasureTree(fixture.tree, &metrics, &size,
                                                  NULL),
                       RoboOk);
        LayoutIntrinsicEnd(&fixture);
        RoboCheckEqInt(LayoutLiveBlocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterLayoutIntrinsic(void);

void TestRegisterLayoutIntrinsic(void)
{
    TestRegister("layout.intrinsic.kind", TestIntrinsicKindVocabulary);
    TestRegister("layout.intrinsic.record", TestIntrinsicSizeRecord);
    TestRegister("layout.intrinsic.fit_content", TestIntrinsicFitContent);
    TestRegister("layout.intrinsic.text_min_max", TestIntrinsicTextMinMax);
    TestRegister("layout.intrinsic.text_long_word", TestIntrinsicTextLongWord);
    TestRegister("layout.intrinsic.text_whitespace", TestIntrinsicTextWhitespace);
    TestRegister("layout.intrinsic.text_multilingual",
                 TestIntrinsicTextMultilingual);
    TestRegister("layout.intrinsic.inline_sum", TestIntrinsicInlineSum);
    TestRegister("layout.intrinsic.block_reset", TestIntrinsicBlockReset);
    TestRegister("layout.intrinsic.tree", TestIntrinsicTree);
    TestRegister("layout.intrinsic.unmeasured", TestIntrinsicUnmeasured);
    TestRegister("layout.intrinsic.refusals", TestIntrinsicRefusals);
    TestRegister("layout.intrinsic.dimension", TestIntrinsicDimension);
    TestRegister("layout.intrinsic.sizing_record", TestIntrinsicSizingRecord);
    TestRegister("layout.intrinsic.sizing_resolve", TestIntrinsicSizingResolve);
    TestRegister("layout.intrinsic.allocator_balance",
                 TestIntrinsicAllocatorBalance);
}
