/*
 * Shared RoboPaint test fixtures (Phase 11.1).
 *
 * Every suite in paint/tests includes this. It is not a framework -- the runtime has
 * exactly one of those (`Tests/Framework/RoboTest.h`) and this phase adds none, on
 * purpose. It holds the four things a paint suite would otherwise copy:
 *
 *   bring-up    RoboBase, RoboHTML, RoboCSS, RoboLayout and RoboPaint, in that order,
 *               because that is the order DEPENDENCIES.md puts them in;
 *
 *   a pipeline  a real document built by the real tree builder, styled by a real
 *               cascade, bridged to a real layout tree and run through a real layout
 *               pass -- and then *painted*. Nothing in a paint suite fabricates a
 *               layout result, and that is the point: a hand-built record would prove
 *               the painter reads a struct, where these cases prove it reads what
 *               RoboLayout concluded;
 *
 *   a measurer  the scripted text measurer the layout suites use, so that a document
 *               with text produces a *measured* run and the traversal's text branch is
 *               exercised rather than skipped for want of a font;
 *
 *   lookups     one expression for "the box of the element with this class", plus the
 *               operation walks a paint assertion needs. The class lookup is a real
 *               DOM search (`HtmlNodeFindFirst`) rather than an index, so a
 *               fixture that gains a wrapper does not silently assert the wrong box.
 *
 * The layout fixture is shared with layout/tests on purpose: `RoboLayoutTestSupport.h`
 * already owns document building, the CSS-side style context and the fake style source,
 * and a second copy here would be a second thing to keep in step with the subsystem this
 * module merely consumes.
 *
 * Every helper asserts rather than returning a status, which is the convention the
 * layout suites set: a suite that had to check each step of its own scaffolding would
 * drown its subject in harness.
 */

#ifndef ROBO_PAINT_TEST_SUPPORT_H
#define ROBO_PAINT_TEST_SUPPORT_H

#include "RoboPaint/RoboPaint.h"

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Bring-up                                                           */
/* ------------------------------------------------------------------ */

/** Bring every layer a paint test needs up (once) and leave them up. */
static inline void PaintFixture(void)
{
    LayoutFixture();
    if (!PaintIsInitialized()) {
        RoboCheckEqInt(PaintInit(), RoboOk);
    }
}

/** Live allocator blocks: the number a suite asserts returns to its baseline. */
static inline size_t PaintLiveBlocks(void)
{
    return MemDebugLiveCount();
}

/* ------------------------------------------------------------------ */
/* A scripted measurer                                                 */
/* ------------------------------------------------------------------ */

/**
 * One scripted answer: this many px per code point, and this line extent.
 *
 * The shape `SuiteRoboLayoutLineBox.c` and the layout integration suite both use, and
 * deliberately not shared with either: a shared measurer would be a third thing to keep
 * in step, and a paint suite varies it not at all -- it exists here so that a text run is
 * *measured* and the traversal's "counted and not painted" branch is exercised.
 */
typedef struct paintScript {
    int32_t perCodePoint;
    int32_t height;
    size_t calls;
} PaintScript;

static inline Result PaintMeasure(void *userdata,
                                   const LayoutTextRequest *request,
                                   LayoutTextRun *outRun)
{
    PaintScript *script = (PaintScript *)userdata;

    if (!script || !request || !outRun || !request->style) {
        return RoboInvalidArgument;
    }
    script->calls++;
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

static inline LayoutTextMetrics PaintMetricsOf(PaintScript *script)
{
    LayoutTextMetrics metrics;

    LayoutTextMetricsInit(&metrics);
    metrics.userdata = script;
    metrics.measure = PaintMeasure;
    return metrics;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/** Everything one paint document needs: DOM, bridge, styles, tree, measurer. */
typedef struct paintFixtureDoc {
    LayoutDocument doc;
    LayoutCssFixture css;
    LayoutDom *dom;
    LayoutTree *tree;
    LayoutStyleSource source;
    PaintScript script;
} PaintFixtureDoc;

static inline void PaintBegin(PaintFixtureDoc *fixture, const char *html, const char *css)
{
    LayoutBuildOptions options;

    memset(fixture, 0, sizeof(*fixture));
    PaintFixture();
    LayoutDocumentBuild(&fixture->doc, html);
    LayoutCssBegin(&fixture->css);
    LayoutCssSheet(&fixture->css, css);
    RoboCheckEqInt(LayoutDomCreate(fixture->css.context, fixture->doc.id,
                                             &fixture->dom, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutDomRefresh(fixture->dom, NULL), RoboOk);
    RoboCheckEqInt(LayoutDomStyleSource(fixture->dom, &fixture->source, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->source;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
    fixture->script.perCodePoint = 8;
    fixture->script.height = 12;
}

static inline void PaintEnd(PaintFixtureDoc *fixture)
{
    /* The tree borrows the bridge's styles, so it goes first. */
    LayoutTreeDestroy(fixture->tree);
    fixture->tree = NULL;
    LayoutDomDestroy(fixture->dom);
    fixture->dom = NULL;
    LayoutCssEnd(&fixture->css);
    LayoutDocumentRelease(&fixture->doc);
}

/**
 * One full layout pass, with the context destroyed afterwards.
 *
 * The diagnostic is captured and reported rather than passed as NULL: a pass that
 * refuses a document says why, and a suite that discarded the message would turn "the
 * pass failed" into an evening of bisecting a fixture.
 */
static inline void PaintLayout(PaintFixtureDoc *fixture, int32_t width, int32_t height)
{
    LayoutContext *context = NULL;
    LayoutTextMetrics metrics;
    LayoutSize viewport;
    LayoutDiag diag;
    Result status;

    viewport.width = width;
    viewport.height = height;
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    metrics = PaintMetricsOf(&fixture->script);
    RoboCheckEqInt(LayoutContextSetTextMetrics(context, &metrics), RoboOk);
    LayoutDiagReset(&diag);
    status = LayoutContextRun(context, fixture->tree, &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the layout pass refused the document: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
    LayoutContextDestroy(context);
}

/* ------------------------------------------------------------------ */
/* The rule the fixture is here to prove                               */
/* ------------------------------------------------------------------ */

/**
 * A document that has been **laid out**: the tree every paint case paints.
 *
 * `PaintBegin` + `PaintLayout` is the two-line preamble of every case that is about
 * painting rather than about the pipeline, and the pair exists so that a case can say
 * "given a laid-out document" in one line. The viewport is 800x600 -- wide enough that
 * nothing in these fixtures wraps unless it is meant to.
 */
static inline void PaintBeginLaidOut(PaintFixtureDoc *fixture,
                                        const char *html,
                                        const char *css)
{
    PaintBegin(fixture, html, css);
    PaintLayout(fixture, 800, 600);
}

/* ------------------------------------------------------------------ */
/* Lookups                                                             */
/* ------------------------------------------------------------------ */

/** Does @p view carry class @p user (an exact single-class match)? */
static inline int PaintMatchClass(const HtmlNodeView *view, void *user)
{
    const char *wanted = (const char *)user;
    HtmlAttribute attribute;

    if (!view || !wanted || view->kind != RoboHtmlNodeKindElement) {
        return 0;
    }
    if (HtmlElementFindAttribute(&view->handle, "class", &attribute, NULL) != RoboOk) {
        return 0;
    }
    return attribute.value && strcmp(attribute.value, wanted) == 0;
}

/** The element with class @p name in @p fixture, or an unformed handle. */
static inline HtmlNodeHandle PaintFind(PaintFixtureDoc *fixture,
                                                 const char *name)
{
    HtmlNodeHandle root = HtmlDocumentRoot(fixture->doc.id);
    HtmlNodeHandle found;

    HtmlNodeHandleClear(&found);
    if (!HtmlNodeHandleIsWellformed(&root)) {
        return found;
    }
    if (HtmlNodeFindFirst(&root, RoboHtmlNodeFilterElements, PaintMatchClass,
                                  (void *)name, &found, NULL) != RoboOk) {
        HtmlNodeHandleClear(&found);
    }
    return found;
}

/** The box built for the element with class @p name, asserted to exist. */
static inline LayoutNodeId PaintBox(PaintFixtureDoc *fixture, const char *name)
{
    HtmlNodeHandle element = PaintFind(fixture, name);
    LayoutNodeId id;

    RoboCheckEqInt((int)HtmlNodeHandleIsWellformed(&element), 1);
    id = LayoutObjectIdForDomNode(fixture->tree, &element);
    RoboCheck(id != RoboLayoutNodeIdNone);
    return id;
}

/**
 * The box built for the first element named @p tag, asserted to exist.
 *
 * The fixtures that have exactly one element of a kind use this -- `html`, `body` -- and
 * the ones with several use `PaintBox`: a tag lookup with two `<div>`s would silently
 * assert whichever the parser reached first, which is the kind of harness bug that makes
 * a suite pass for the wrong reason.
 */
static inline LayoutNodeId PaintBoxByTag(PaintFixtureDoc *fixture, const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);
    LayoutNodeId id;

    RoboCheckEqInt((int)HtmlNodeHandleIsWellformed(&element), 1);
    id = LayoutObjectIdForDomNode(fixture->tree, &element);
    RoboCheck(id != RoboLayoutNodeIdNone);
    return id;
}

/* ------------------------------------------------------------------ */
/* Running a paint pass                                                */
/* ------------------------------------------------------------------ */

/** What one paint pass left behind: the list, and what the pass said it saw. */
typedef struct paintResult {
    PaintList *list;
    PaintStats stats;
    PaintPassOutcome outcome;
} PaintResult;

/** Create an empty list and a pass record. */
static inline void PaintResultInit(PaintResult *result)
{
    result->list = NULL;
    memset(&result->stats, 0, sizeof(result->stats));
    result->outcome = RoboPaintPassNone;
    RoboCheckEqInt(PaintListCreate(&result->list, NULL), RoboOk);
}

static inline void PaintResultDispose(PaintResult *result)
{
    PaintListDestroy(result->list);
    result->list = NULL;
}

/**
 * Paint @p fixture's laid-out tree, reporting the outcome rather than asserting it.
 *
 * The outcome is deliberately *not* checked here: three of the cases below are about a
 * pass that refuses (a stale tree, an empty tree, a document that draws nothing), and a
 * helper that asserted PAINTED would make those unexpressible. The diagnostic is
 * reported when the pass *fails*, because a failure is always a bug in the case.
 */
static inline void PaintRun(PaintFixtureDoc *fixture,
                      PaintContext *context,
                      PaintList *list)
{
    PaintDiag diag;
    Result status;

    PaintDiagReset(&diag);
    status = PaintContextRun(context, fixture->tree, list, &diag);
    if (status != RoboOk) {
        TestFail(__FILE__, __LINE__, "the paint pass refused the document: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
}

/** The whole pipeline in one call: a fresh context, a fresh list, one pass. */
static inline void PaintOnce(PaintFixtureDoc *fixture, PaintResult *out)
{
    PaintContext *context = NULL;

    PaintResultInit(out);
    RoboCheckEqInt(PaintContextCreate(&context, NULL), RoboOk);
    PaintRun(fixture, context, out->list);
    RoboCheckEqInt((int)PaintContextOutcome(context),
                      (int)RoboPaintPassPainted);
    PaintContextStats(context, &out->stats);
    out->outcome = PaintContextOutcome(context);
    PaintContextDestroy(context);
}

/* ------------------------------------------------------------------ */
/* Operations                                                          */
/* ------------------------------------------------------------------ */

/**
 * The @p nth operation of @p list whose kind is @p kind.
 *
 * The walk a suite would otherwise write five times, and the reason it is `nth` rather
 * than `first`: "the second border side draws" is a real question and "the first" is a
 * special case of it.
 *
 * @return 1 when the operation was found and @p out filled; 0 otherwise.
 */
static inline int PaintNthOfKind(const PaintResult *result,
                             PaintOpKind kind,
                             size_t nth,
                             PaintOperation *out)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            return 0;
        }
        if (operation.kind != kind) {
            continue;
        }
        if (seen == nth) {
            if (out) {
                *out = operation;
            }
            return 1;
        }
        seen++;
    }
    return 0;
}

/** How many operations of @p kind the pass produced. */
static inline size_t PaintCountOf(const PaintResult *result, PaintOpKind kind)
{
    return PaintListCountOfKind(result->list, kind);
}

/** The first operation of @p kind, asserted to exist. */
static inline PaintOperation PaintRequire(const PaintResult *result,
                                            PaintOpKind kind)
{
    PaintOperation operation;

    PaintOperationInit(&operation);
    RoboCheckEqInt(PaintNthOfKind(result, kind, 0u, &operation), 1);
    return operation;
}

/** The index of the first operation of @p kind, or `(size_t)-1`. */
static inline size_t PaintIndexOfKind(const PaintResult *result, PaintOpKind kind)
{
    size_t index;

    for (index = 0u; index < PaintListCount(result->list); index++) {
        PaintOperation operation;

        if (PaintListAt(result->list, index, &operation) != RoboOk) {
            break;
        }
        if (operation.kind == kind) {
            return index;
        }
    }
    return (size_t)-1;
}

/* ------------------------------------------------------------------ */
/* Assertions                                                          */
/* ------------------------------------------------------------------ */

static inline void PaintRectEq(LayoutRect actual,
                                 LayoutRect expected,
                                 const char *what)
{
    if (actual.x != expected.x || actual.y != expected.y ||
        actual.width != expected.width || actual.height != expected.height) {
        TestFail(__FILE__, __LINE__,
                       "%s: expected %d,%d %dx%d, got %d,%d %dx%d", what, expected.x,
                       expected.y, expected.width, expected.height, actual.x, actual.y,
                       actual.width, actual.height);
    }
}

static inline void PaintRectIs(int32_t x, int32_t y, int32_t w, int32_t h,
                                 LayoutRect actual, const char *what)
{
    LayoutRect expected;

    expected.x = x;
    expected.y = y;
    expected.width = w;
    expected.height = h;
    PaintRectEq(actual, expected, what);
}

/** Assert two colours are the same record. */
static inline void PaintColorEq(CssColor actual,
                                  CssColor expected,
                                  const char *what)
{
    if (!CssColorEquals(actual, expected)) {
        char actualText[RoboCssColorTextMax + 1u];
        char expectedText[RoboCssColorTextMax + 1u];
        size_t length = 0u;

        (void)CssColorWrite(actual, actualText, sizeof(actualText), &length, NULL);
        (void)CssColorWrite(expected, expectedText, sizeof(expectedText), &length,
                                   NULL);
        TestFail(__FILE__, __LINE__, "%s: expected %s, got %s", what, expectedText,
                       actualText);
    }
}

/**
 * Assert that @p operation's boxes, turned back into layout coordinates, are exactly
 * what RoboLayout wrote for that box.
 *
 * This is the phase's central claim pinned as one call: the operation carries both
 * coordinate spaces, and the two must agree with the tree. A consumer that had only the
 * list and re-walked the tree to recover the other space is the "recalculate in paint"
 * this module exists to prevent, and this assertion is what makes carrying both safe
 * rather than a second opinion.
 */
static inline void PaintLocalBoxesMatchLayout(PaintFixtureDoc *fixture,
                                           PaintOperation operation)
{
    PaintBoxes local;
    PaintBoxes fromLayout;

    memset(&local, 0, sizeof(local));
    memset(&fromLayout, 0, sizeof(fromLayout));
    RoboCheckEqInt(PaintOperationLocalBoxes(&operation, &local, NULL), RoboOk);
    RoboCheckEqInt(PaintBoxesOf(fixture->tree, operation.node, &fromLayout, NULL),
                      RoboOk);
    if (!PaintBoxesEqual(&local, &fromLayout)) {
        TestFail(__FILE__, __LINE__,
                       "operation %d's local boxes are not what layout wrote for node %d",
                       (int)operation.kind, (int)operation.node);
    }
}

#endif /* ROBO_PAINT_TEST_SUPPORT_H */
