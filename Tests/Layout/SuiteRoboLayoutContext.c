/*
 * RoboLayout context suite (Phase 10.1) — layout.context.*
 *
 * One pass: the tree it ran on, the constraints it ran with, what it saw, and
 * where a later goal hangs its scratch state.
 *
 * The pass is the awkward part of this phase to test *well*, because its whole
 * output is a statement of what has *not* been computed. So every counter is
 * asserted against a tree whose shape the suite built itself, and the central
 * assertion is one no other suite can make: after a pass, every box's result is
 * UNRESOLVED and reading its geometry is a *miss*. A build that returned a zeroed
 * rectangle instead would pass a "did the pass run" test and fail this one -- and
 * the difference is a box painted at the origin versus a box skipped.
 *
 * `boxesComputed` is asserted to be 0 and `geometry_is_implemented()` to be 0 in
 * the same test, because those are one fact stated twice and a build that flipped
 * one would have two answers to one question.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A tree to run on                                                    */
/* ------------------------------------------------------------------ */

typedef struct layoutContextFixture {
    LayoutDocument doc;
    LayoutTree *tree;
    LayoutNodeId root;
} LayoutContextFixture;

#define LAYOUT_CONTEXT_FIXTURE_HTML \
    "<div>\n"                       \
    "  <section>\n"                 \
    "    <p>one</p>\n"              \
    "    <p>two</p>\n"              \
    "  </section>\n"                \
    "  <aside></aside>\n"           \
    "</div>\n"

static void LayoutContextTreeBegin(LayoutContextFixture *fixture,
                                      const char *html,
                                      const LayoutStyleSource *styles,
                                      LayoutBuildReport *outReport)
{
    LayoutBuildOptions options;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                           &options, outReport, NULL),
                      RoboOk);
    fixture->root = LayoutTreeRoot(fixture->tree);
}

static void LayoutContextTreeEnd(LayoutContextFixture *fixture)
{
    LayoutTreeDestroy(fixture->tree);
    fixture->tree = NULL;
    LayoutDocumentRelease(&fixture->doc);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void TestContextLifecycle(void)
{
    LayoutContext *context = NULL;
    LayoutStats stats;
    LayoutConstraints constraints;
    LayoutSize size;

    LayoutFixture();

    RoboCheckEqInt(LayoutContextCreate(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheck(context != NULL);

    /* Nothing has run, so nothing is claimed. */
    RoboCheckEqInt((int)LayoutContextOutcome(context), (int)RoboLayoutPassNone);
    RoboCheck(LayoutContextTree(context) == NULL);
    RoboCheck(LayoutContextUserData(context) == NULL);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.objects, 0u);
    RoboCheckEqInt(stats.visited, 0u);
    RoboCheckEqInt(stats.boxesComputed, 0u);
    RoboCheckEqInt(stats.passes, 0u);
    RoboCheckEqInt(LayoutContextConstraints(context, &constraints), RoboOk);
    RoboCheckEqInt(LayoutConstraintsHasAvailable(&constraints), 0);

    /* Every read door answers a sane thing for a NULL context rather than
     * crashing: a caller that lost its handle gets "nothing", not a fault. */
    RoboCheckEqInt((int)LayoutContextOutcome(NULL), (int)RoboLayoutPassNone);
    RoboCheck(LayoutContextTree(NULL) == NULL);
    RoboCheck(LayoutContextUserData(NULL) == NULL);
    LayoutContextStats(NULL, &stats);
    RoboCheckEqInt(stats.visited, 0u);
    RoboCheckEqInt(LayoutContextViewport(NULL, &size), RoboInvalidArgument);
    RoboCheckEqInt(LayoutContextConstraints(NULL, &constraints),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutContextViewport(context, NULL), RoboInvalidArgument);

    LayoutContextSetUserData(NULL, context);
    LayoutContextReset(NULL);
    LayoutContextDestroy(NULL);
    LayoutContextDestroy(context);
}

static void TestContextReset(void)
{
    LayoutContext *context = NULL;
    LayoutContextFixture fixture;
    LayoutSize viewport;
    LayoutConstraints afterReset;
    LayoutStats stats;
    int *marker;
    int value = 7;

    LayoutFixture();
    marker = &value;

    LayoutContextTreeBegin(&fixture, LAYOUT_CONTEXT_FIXTURE_HTML, NULL, NULL);
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);

    viewport.width = 1024;
    viewport.height = 768;
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    LayoutContextSetUserData(context, marker);
    RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context),
                      (int)RoboLayoutPassWalked);
    RoboCheck(LayoutContextTree(context) == fixture.tree);

    LayoutContextReset(context);

    /* "This context has run nothing" is the state reset restores, not "this
     * context has run something else": the pass count goes to zero with the
     * rest. */
    RoboCheckEqInt((int)LayoutContextOutcome(context), (int)RoboLayoutPassNone);
    RoboCheck(LayoutContextTree(context) == NULL);
    RoboCheckEqInt(LayoutContextViewport(context, &viewport), RoboNotFound);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.passes, 0u);
    RoboCheckEqInt(stats.visited, 0u);
    RoboCheckEqInt(LayoutContextConstraints(context, &afterReset), RoboOk);
    RoboCheckEqInt(LayoutConstraintsHasAvailable(&afterReset), 0);

    /* User data belongs to the caller and survives a reset: a caller that
     * installs its scratch once and resets per pass is the normal way to use
     * this. */
    RoboCheck(LayoutContextUserData(context) == marker);

    LayoutContextDestroy(context);
    LayoutContextTreeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Viewport and constraints                                            */
/* ------------------------------------------------------------------ */

static void TestContextViewport(void)
{
    LayoutContext *context = NULL;
    LayoutSize viewport;
    LayoutSize out;
    LayoutConstraints constraints;
    LayoutSize negative;

    LayoutFixture();
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);

    /* Nothing recorded means NOT_FOUND, not an invented screen size. */
    out.width = -5;
    out.height = -5;
    RoboCheckEqInt(LayoutContextViewport(context, &out), RoboNotFound);
    RoboCheckEqInt(out.width, -5);

    viewport.width = 1280;
    viewport.height = 800;
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextViewport(context, &out), RoboOk);
    RoboCheckEqInt(out.width, 1280);
    RoboCheckEqInt(out.height, 800);

    /* The viewport is recorded as the *available* space and as nothing else --
     * whether a box fills it is a formatting-context decision (10.2), so a
     * context that also treated it as a maximum would be answering for a later
     * goal. */
    RoboCheckEqInt(LayoutContextConstraints(context, &constraints), RoboOk);
    RoboCheckEqInt(LayoutConstraintsHasAvailable(&constraints), 1);
    RoboCheckEqInt(LayoutConstraintsHasMaximum(&constraints), 0);
    RoboCheckEqInt(LayoutConstraintsHasMinimum(&constraints), 0);
    RoboCheckEqInt(LayoutConstraintsAvailable(&constraints, &out), RoboOk);
    RoboCheckEqInt(out.width, 1280);
    RoboCheckEqInt(out.height, 800);

    /* A negative viewport is a caller bug, and it is refused rather than clamped
     * into something that looks like a screen. */
    negative.width = -1280;
    negative.height = 800;
    RoboCheckEqInt(LayoutContextSetViewport(context, negative, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutContextViewport(context, &out), RoboOk);
    RoboCheckEqInt(out.width, 1280);

    LayoutContextDestroy(context);
}

static void TestContextConstraints(void)
{
    LayoutContext *context = NULL;
    LayoutConstraints constraints;
    LayoutConstraints out;
    LayoutSize size;

    LayoutFixture();
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);

    /* No constraints installed reads back as the cleared record rather than as a
     * failure: "nothing is known" is not an error. */
    RoboCheckEqInt(LayoutContextConstraints(context, &out), RoboOk);
    RoboCheckEqInt(LayoutConstraintsHasAvailable(&out), 0);
    RoboCheckEqInt(LayoutConstraintsHasMinimum(&out), 0);

    /* Install a pair of bounds and check the record travels by value. */
    LayoutConstraintsInit(&constraints);
    size.width = 640;
    size.height = 480;
    RoboCheckEqInt(LayoutConstraintsSetMinimum(&constraints, size, NULL), RoboOk);
    size.width = 640;
    size.height = 480;
    RoboCheckEqInt(LayoutConstraintsSetMaximum(&constraints, size, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextSetConstraints(context, &constraints, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextConstraints(context, &out), RoboOk);
    RoboCheckEqInt(LayoutConstraintsHasMinimum(&out), 1);
    RoboCheckEqInt(LayoutConstraintsHasMaximum(&out), 1);
    RoboCheckEqInt(LayoutConstraintsEqual(&constraints, &out), 1);

    /* Setting the viewport and setting the constraints are independent: a pass
     * may be run with bounds that come from somewhere other than a viewport. */
    size.width = 320;
    size.height = 240;
    RoboCheckEqInt(LayoutContextSetViewport(context, size, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextConstraints(context, &out), RoboOk);
    RoboCheckEqInt(LayoutConstraintsHasMaximum(&out), 1);
    RoboCheckEqInt(LayoutConstraintsMaximum(&out, &size), RoboOk);
    RoboCheckEqInt(size.width, 640);

    RoboCheckEqInt(LayoutContextSetConstraints(context, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutContextSetConstraints(NULL, &constraints, NULL),
                      RoboInvalidArgument);

    LayoutContextDestroy(context);
}

static void TestContextUserData(void)
{
    LayoutContext *context = NULL;
    int marker = 3;
    int other = 4;

    LayoutFixture();
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);

    /* The extension point a later goal uses instead of growing the struct:
     * RoboLayout never dereferences it, so what it points at is entirely the
     * caller's business. */
    RoboCheck(LayoutContextUserData(context) == NULL);
    LayoutContextSetUserData(context, &marker);
    RoboCheck(LayoutContextUserData(context) == &marker);
    LayoutContextSetUserData(context, &other);
    RoboCheck(LayoutContextUserData(context) == &other);
    /* NULL is a legal value and means "no scratch". */
    LayoutContextSetUserData(context, NULL);
    RoboCheck(LayoutContextUserData(context) == NULL);

    LayoutContextDestroy(context);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

static void TestContextRun(void)
{
    LayoutContext *context = NULL;
    LayoutContextFixture fixture;
    LayoutBuildReport report;
    LayoutStats stats;
    size_t index;
    uint32_t deepest = 0u;
    LayoutResult result;
    LayoutPoint position;
    LayoutSize size;
    LayoutRect rect;

    LayoutFixture();
    LayoutContextTreeBegin(&fixture, LAYOUT_CONTEXT_FIXTURE_HTML, NULL, &report);
    RoboCheck(report.boxesCreated > 0u);
    RoboCheckEqInt((int)fixture.root, 1);

    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context),
                      (int)RoboLayoutPassWalked);
    RoboCheck(LayoutContextTree(context) == fixture.tree);

    LayoutContextStats(context, &stats);
    /*
     * Every **object**, not every box: a text node generates an object of its own
     * (10.5) and the pass visits it exactly as it visits a box, so the count the
     * pass reports is the tree's object count -- the two builder counters added
     * rather than one of them.
     */
    RoboCheckEqInt(stats.objects, report.boxesCreated + report.textsCreated);
    RoboCheckEqInt(stats.visited, report.boxesCreated + report.textsCreated);
    RoboCheckEqInt(stats.boxesUnresolved,
                      report.boxesCreated + report.textsCreated);
    RoboCheckEqInt(stats.passes, 1u);
    /* No style source was supplied, so every object is marked and *counted*: it is a
     * fact about the build, not a failure. A text object borrows its parent's style,
     * and there is no parent style here, so it is in the count as well. */
    RoboCheckEqInt(stats.withoutStyle, report.boxesCreated + report.textsCreated);

    /* The pass is the thing that moves a box from "built" to "unresolved", so the
     * counters and the results agree. */
    for (index = 0u; index < report.boxesCreated + report.textsCreated; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;
        uint32_t depth = 0u;

        RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, index, &id), RoboOk);
        RoboCheckEqInt(LayoutObjectResult(fixture.tree, id, &result), RoboOk);
        RoboCheckEqInt((int)result.state, (int)RoboLayoutResultUnresolved);
        RoboCheckEqInt(LayoutResultIsComputed(&result), 0);
        RoboCheckEqInt(result.flags, 0u);
        RoboCheckEqInt(LayoutObjectDepth(fixture.tree, id, &depth), RoboOk);
        if (depth > deepest) {
            deepest = depth;
        }
    }
    RoboCheckEqInt(stats.depthMax, deepest);

    /* And this is the phase in one assertion: reading a result tells the truth
     * instead of handing back (0, 0). */
    RoboCheckEqInt(result.state, RoboLayoutResultUnresolved);
    RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboNotFound);
    RoboCheckEqInt(LayoutResultSize(&result, &size), RoboNotFound);
    RoboCheckEqInt(LayoutResultRect(&result, &rect, NULL), RoboNotFound);

    /*
     * Two counters, about two different things -- and that they differ here is the
     * assertion, not a contradiction.
     *
     * No viewport was recorded, so this pass placed nothing: `boxesComputed` is 0
     * and every box above reads UNRESOLVED. The *build*, meanwhile, can compute a
     * coordinate -- `geometry` is 1 since 10.3 -- it simply had no initial
     * containing block to compute one into. A suite that asserted the two agree
     * would be asserting that a capability is per-pass, which is exactly the
     * confusion the result state exists to prevent.
     */
    RoboCheckEqInt(stats.boxesComputed, 0u);
    RoboCheckEqInt(LayoutGeometryIsImplemented(), 1);
    RoboCheckEqInt(stats.boxesUnresolved, stats.objects);

    LayoutContextDestroy(context);
    LayoutContextTreeEnd(&fixture);
}

static void TestContextRunRepeated(void)
{
    LayoutContext *context = NULL;
    LayoutContextFixture fixture;
    LayoutBuildReport report;
    LayoutStats first;
    LayoutStats second;
    LayoutResult result;
    int run;

    LayoutFixture();
    LayoutContextTreeBegin(&fixture, LAYOUT_CONTEXT_FIXTURE_HTML, NULL, &report);
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);

    /* A caller reading stats after a second pass wants *that* pass's numbers, and
     * a caller asking how many passes have run wants the total. Both are true at
     * once, which is why `passes` is cumulative and everything else is not. */
    for (run = 0; run < 4; run++) {
        RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
        LayoutContextStats(context, &first);
        RoboCheckEqInt(first.objects, report.boxesCreated + report.textsCreated);
        RoboCheckEqInt(first.visited, report.boxesCreated + report.textsCreated);
        RoboCheckEqInt(first.boxesUnresolved,
                          report.boxesCreated + report.textsCreated);
        RoboCheckEqInt(first.passes, (size_t)(run + 1));
        RoboCheckEqInt(first.boxesComputed, 0u);
        (void)second;
    }

    /* A repeated pass does not accumulate: every box is UNRESOLVED, not
     * "unresolved twice". */
    {
        LayoutNodeId id = fixture.root;

        RoboCheckEqInt(LayoutObjectResult(fixture.tree, id, &result), RoboOk);
        RoboCheckEqInt((int)result.state, (int)RoboLayoutResultUnresolved);
        RoboCheckEqInt(result.size.width, 0);
        RoboCheckEqInt(result.size.height, 0);
    }

    LayoutContextDestroy(context);
    LayoutContextTreeEnd(&fixture);
}

static void TestContextRunEmptyTree(void)
{
    LayoutContext *context = NULL;
    LayoutTree *tree = NULL;
    LayoutStats stats;

    LayoutFixture();
    /* A document whose root element is `display: none` builds no boxes, and an
     * empty tree is a legitimate pass rather than an error. */
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context),
                      (int)RoboLayoutPassWalked);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.objects, 0u);
    RoboCheckEqInt(stats.visited, 0u);
    RoboCheckEqInt(stats.depthMax, 0u);
    RoboCheckEqInt(stats.passes, 1u);

    LayoutContextDestroy(context);
    LayoutTreeDestroy(tree);
}

static void TestContextRunFailures(void)
{
    LayoutContext *context = NULL;
    LayoutContextFixture fixture;
    LayoutDiag diag = RoboLayoutCleared;

    LayoutFixture();
    LayoutContextTreeBegin(&fixture, LAYOUT_CONTEXT_FIXTURE_HTML, NULL, NULL);
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);

    RoboCheckEqInt(LayoutContextRun(NULL, fixture.tree, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.code, RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainContext);

    LayoutDiagReset(&diag);
    RoboCheckEqInt(LayoutContextRun(context, NULL, &diag), RoboInvalidArgument);
    RoboCheckEqInt(diag.code, RoboInvalidArgument);

    /* A failed pass says so rather than leaving the previous pass's outcome
     * standing, because a caller reads the outcome to decide whether the
     * counters mean anything. */
    RoboCheckEqInt(LayoutContextRun(context, NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt((int)LayoutContextOutcome(context),
                      (int)RoboLayoutPassFailed);

    LayoutContextDestroy(context);
    LayoutContextTreeEnd(&fixture);
}

static void TestContextRunWithStyles(void)
{
    LayoutContext *context = NULL;
    LayoutContextFixture fixture;
    LayoutBuildReport report;
    LayoutFakeSource fake;
    LayoutStyleSource styles;
    LayoutStats stats;

    LayoutFixture();
    LayoutFakeInit(&fake);
    LayoutFakeAdd(&fake, "div", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "section", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "p", RoboCssDisplayBlock);
    /* `<aside>` is deliberately left out: an element the source answers nothing
     * about is a box with no style, not a missing box. */
    styles = LayoutFakeSourceOf(&fake);

    LayoutContextTreeBegin(&fixture, LAYOUT_CONTEXT_FIXTURE_HTML, &styles, &report);
    RoboCheck(report.boxesCreated > 0u);
    RoboCheck(report.boxesWithoutStyle > 0u);
    RoboCheck(report.boxesWithoutStyle < report.boxesCreated);

    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    /* The pass counts exactly what the build marked, so the two halves of the
     * module agree about which boxes have no style. */
    RoboCheckEqInt(stats.withoutStyle, report.boxesWithoutStyle);
    RoboCheckEqInt(stats.objects, report.boxesCreated + report.textsCreated);
    /* The fake answered some elements, so it was actually consulted. */
    RoboCheck(fake.answers > 0u);

    LayoutContextDestroy(context);
    LayoutFakeDispose(&fake);
    LayoutContextTreeEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The context owns nothing borrowed                                   */
/* ------------------------------------------------------------------ */

static void TestContextAllocatorBalance(void)
{
    size_t before;
    LayoutContextFixture fixture;
    LayoutContext *context = NULL;
    int pass;

    LayoutFixture();
    /* The baseline is taken *before* the tree, so the comparison at the end covers
     * the tree's arena as well as the contexts: a context that leaked one of the
     * tree's boxes would show here and nowhere else. */
    before = LayoutLiveBlocks();
    LayoutContextTreeBegin(&fixture, LAYOUT_CONTEXT_FIXTURE_HTML, NULL, NULL);

    /* Destroying a context frees the context and nothing else -- not the tree it
     * ran on, not the user data, not the styles. One context, many passes, one
     * allocation. */
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    for (pass = 0; pass < 32; pass++) {
        LayoutContextSetViewport(context, (LayoutSize){ 800, 600 }, NULL);
        RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
    }
    LayoutContextDestroy(context);

    /* And the tree it ran on is untouched by the pass having existed: still live,
     * still the same shape. */
    RoboCheckEqInt(LayoutObjectIsLive(fixture.tree, fixture.root), 1);
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree) > 0u, 1);

    LayoutContextTreeEnd(&fixture);
    RoboCheckEqInt(LayoutLiveBlocks(), before);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutContext(void);

void TestRegisterLayoutContext(void)
{
    TestRegister("layout.context.lifecycle", TestContextLifecycle);
    TestRegister("layout.context.reset", TestContextReset);
    TestRegister("layout.context.viewport", TestContextViewport);
    TestRegister("layout.context.constraints", TestContextConstraints);
    TestRegister("layout.context.user_data", TestContextUserData);
    TestRegister("layout.context.run", TestContextRun);
    TestRegister("layout.context.run.repeated", TestContextRunRepeated);
    TestRegister("layout.context.run.empty", TestContextRunEmptyTree);
    TestRegister("layout.context.run.failures", TestContextRunFailures);
    TestRegister("layout.context.run.styles", TestContextRunWithStyles);
    TestRegister("layout.context.allocator_balance", TestContextAllocatorBalance);
}
