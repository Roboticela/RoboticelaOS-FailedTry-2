/*
 * RoboHTML Phase 8.10 document lifecycle suite.
 *
 * The lifecycle is a state machine on the document, so this suite tests it in
 * that shape: the graph itself (which edges exist, which are refused), then
 * the transitions each entry point actually drives, then what stays true
 * across them.
 *
 * The organising idea is that COMPLETE is a statement about *input* — no
 * further input can arrive — while INTERACTIVE is a statement about the
 * *tree* — parsing finished and the DOM is live. Almost every case below is a
 * check that those two are not confused:
 *
 *   a one-shot build over a complete source   → COMPLETE
 *   a build truncated at the node ceiling     → INTERACTIVE
 *   a source detached mid-load                → INTERACTIVE
 *   an empty source                           → COMPLETE
 *   a reset that abandoned a load             → COMPLETE
 *
 * Two invariants are asserted alongside the transitions, because a lifecycle
 * that changed the tree would be worse than no lifecycle at all: after every
 * transition the whole-tree audit (HtmlDocumentValidateTree) is clean,
 * and the root handle is the same root. A state machine that quietly retired
 * nodes while reporting a new state would pass every state assertion above and
 * still be broken.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLLifecycle.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void LcFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void LcFixtureEnd(void)
{
    HtmlShutdown();
}

/** A fresh document; brings the module up on demand (declaration order). */
static HtmlDocumentId LcDoc(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    LcFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

/** Current lifecycle, through the accessor every caller uses. */
static HtmlDocumentLifecycle LcState(HtmlDocumentId id)
{
    HtmlDocumentLifecycle life =
        RoboHtmlDocumentLifecycleUninitialized;

    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
    return life;
}

/** A one-shot source over @p text. */
static void LcSource(HtmlSource *src, const char *text)
{
    HtmlSourceInit(src);
    RoboCheckEqInt(HtmlSourceString(src, text, NULL), RoboOk);
}

/** Attach a non-empty source directly: CREATED/INTERACTIVE/COMPLETE → LOADING. */
static void LcAttach(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    LcSource(&src, text);
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, NULL),
                      RoboOk);
}

/** One-shot build; the source is complete, so this is a whole load. */
static void LcBuild(HtmlDocumentId id,
                     const char *text,
                     HtmlTreeResult *out)
{
    HtmlSource src;

    LcSource(&src, text);
    RoboCheckEqInt(HtmlBuildTree(id, &src, out, NULL), RoboOk);
}

static HtmlNodeHandle LcRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

/** Create an element attached under @p parent (legal before finalization). */
static HtmlNodeHandle LcElement(HtmlDocumentId id,
                                          const HtmlNodeHandle *parent,
                                          const char *name)
{
    HtmlElementCreateInfo info;
    HtmlNodeHandle out;

    HtmlElementCreateInfoInit(&info);
    info.parent = *parent;
    info.origin = RoboHtmlNodeOriginTest;
    info.namespaceId = RoboHtmlNamespaceHtml;
    info.localName = name;

    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(HtmlElementCreate(id, &info, &out, NULL), RoboOk);
    return out;
}

/** Create a text node attached under @p parent. */
static HtmlNodeHandle LcText(HtmlDocumentId id,
                                       const HtmlNodeHandle *parent,
                                       const char *content)
{
    HtmlTextCreateInfo info;
    HtmlNodeHandle out;

    HtmlTextCreateInfoInit(&info);
    info.parent = *parent;
    info.origin = RoboHtmlNodeOriginTest;
    info.content = content;
    info.length = content ? strlen(content) : 0u;

    HtmlNodeHandleClear(&out);
    RoboCheckEqInt(HtmlTextCreate(id, &info, &out, NULL), RoboOk);
    return out;
}

static size_t LcChildCount(const HtmlNodeHandle *node)
{
    size_t count = 0u;

    RoboCheckEqInt(HtmlNodeChildCount(node, &count, NULL), RoboOk);
    return count;
}

/**
 * The whole-tree audit, run after every transition in this suite. A lifecycle
 * change must never be what breaks the tree.
 */
static void LcExpectClean(HtmlDocumentId id)
{
    HtmlTreeReport report;

    RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                      RoboOk);
    RoboCheckMsg(report.ok, "tree invariants broken by a lifecycle change");
    RoboCheckEqInt((int)report.orphans, 0);
    RoboCheckEqInt((int)report.badLinks, 0);
    RoboCheckEqInt((int)report.cycles, 0);
    RoboCheckEqInt((int)report.badParents, 0);
    RoboCheckEqInt((int)report.badCounts, 0);
    RoboCheckEqInt((int)report.siblingMismatches, 0);
}

/** A traversal visitor that counts what it reaches. */
typedef struct lcCount {
    size_t nodes;
    size_t elements;
    size_t texts;
} LcCount;

static Result LcCountVisit(const HtmlNodeView *view,
                                    size_t depth,
                                    void *user)
{
    LcCount *count = (LcCount *)user;

    (void)depth;
    if (!view || !count) {
        return RoboInvalidArgument;
    }
    count->nodes++;
    if (view->kind == RoboHtmlNodeKindElement) {
        count->elements++;
    } else if (view->kind == RoboHtmlNodeKindText) {
        count->texts++;
    }
    return RoboOk;
}

/** Walk @p node's descendants, counting. Returns the walk's own result. */
static Result LcWalk(const HtmlNodeHandle *node,
                             LcCount *count)
{
    memset(count, 0, sizeof(*count));
    return HtmlNodeWalkDescendants(node, RoboHtmlNodeFilterAll,
                                           LcCountVisit, count, NULL);
}

/** Handle of the first descendant element named @p name. */
static HtmlNodeHandle LcFind(const HtmlNodeHandle *start,
                                       const char *name)
{
    HtmlNodeHandle found;

    HtmlNodeHandleClear(&found);
    if (HtmlNodeFindElement(start, name, &found, NULL) != RoboOk) {
        HtmlNodeHandleClear(&found);
    }
    return found;
}

/* ------------------------------------------------------------------ */
/* 1. Initial state and the happy path                                 */
/* ------------------------------------------------------------------ */

static void TestLifecycleInitialState(void)
{
    HtmlDocumentId id;
    HtmlDocumentMetadata meta;

    id = LcDoc();

    /* A created document is CREATED: nothing attached, nothing parsed. */
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleCreated);
    RoboCheck(!HtmlDocumentIsLoading(id));
    RoboCheck(!HtmlDocumentIsInteractive(id));
    RoboCheck(!HtmlDocumentIsComplete(id));
    RoboCheck(!HtmlDocumentIsSettled(id));
    RoboCheck(HtmlDocumentIsEmpty(id));

    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.lifecycle, RoboHtmlDocumentLifecycleCreated);
    RoboCheckEqInt(meta.previousLifecycle,
                      RoboHtmlDocumentLifecycleUninitialized);
    /* Construction is open, so the tree is being built, not settled. */
    RoboCheckEqInt(meta.finalized, 0);
    RoboCheckEqInt(meta.rootOk, 1);
    RoboCheckEqInt(meta.nodeCount, 1u);
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleValidPath(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle rootBefore;
    HtmlNodeHandle rootAfter;

    id = LcDoc();
    rootBefore = LcRoot(id);

    /* CREATED → LOADING: input is attached. */
    LcAttach(id, "<p>x</p>");
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleLoading);
    RoboCheck(HtmlDocumentIsLoading(id));
    RoboCheck(!HtmlDocumentIsSettled(id));
    LcExpectClean(id);

    /* LOADING → INTERACTIVE: parsing finished, the DOM is live. */
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleInteractive);
    RoboCheck(HtmlDocumentIsInteractive(id));
    RoboCheck(HtmlDocumentIsSettled(id));
    RoboCheck(!HtmlDocumentIsComplete(id));
    LcExpectClean(id);

    /* INTERACTIVE → COMPLETE: no further input can arrive. */
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    RoboCheck(HtmlDocumentIsComplete(id));
    RoboCheck(HtmlDocumentIsSettled(id));
    /* Exactly one state, not two: complete is not also interactive. */
    RoboCheck(!HtmlDocumentIsInteractive(id));

    /* The root survived every step: a lifecycle change is not a rebuild. */
    rootAfter = LcRoot(id);
    RoboCheck(HtmlNodeHandleEqual(&rootBefore, &rootAfter));
    LcExpectClean(id);

    /* COMPLETE → DESTROYED, one step at a time. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheck(!HtmlDocumentIsLive(id));
    LcFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 2. Which entry point settles where                                  */
/* ------------------------------------------------------------------ */

static void TestLifecycleOneShotBuildCompletes(void)
{
    HtmlDocumentId id;
    HtmlTreeResult result;

    id = LcDoc();

    /*
     * A one-shot build consumes a complete source, so the document is done:
     * the build drives LOADING → INTERACTIVE → COMPLETE and the caller never
     * has to say so.
     */
    LcBuild(id, "<html><body><p>Hello</p></body></html>", &result);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    RoboCheck(HtmlDocumentIsComplete(id));
    RoboCheckEqInt(HtmlDocumentIsFinalized(id), 1);
    RoboCheck(!HtmlDocumentIsEmpty(id));
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleEmptySourceSettlesComplete(void)
{
    HtmlDocumentId id;
    HtmlSource src;

    id = LcDoc();

    /*
     * Nothing to parse means nothing can be outstanding: an empty source
     * still passes through INTERACTIVE, then settles. The document stays
     * empty and stays owned.
     */
    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceEmpty(&src, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, NULL),
                      RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    RoboCheck(HtmlDocumentIsEmpty(id));
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleDetachSettlesInteractive(void)
{
    HtmlDocumentId id;

    id = LcDoc();

    /*
     * Detaching abandons the input rather than finishing it, so the document
     * is interactive and explicitly *not* complete.
     */
    LcAttach(id, "<p>abandoned</p>");
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleLoading);
    RoboCheckEqInt(HtmlDocumentDetachSource(id, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleInteractive);
    RoboCheck(HtmlDocumentIsInteractive(id));
    RoboCheck(!HtmlDocumentIsComplete(id));
    RoboCheckStrEq(HtmlDocumentSourceName(id), "");
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleTruncatedBuildStaysInteractive(void)
{
    enum { ELEMENTS = 4300 };
    HtmlDocumentId id;
    HtmlSource src;
    HtmlTreeResult result;
    char *html;
    size_t i;
    size_t at = 0u;

    id = LcDoc();

    /*
     * The node ceiling (RoboHtmlMaxNodesPerDocument) is 4096, so this
     * input cannot be consumed in full. The build therefore abandons the
     * source rather than finishing it, and the document stops at INTERACTIVE:
     * claiming COMPLETE would be saying "no further input can arrive" about
     * input that was never read.
     */
    html = (char *)Malloc(64u + (size_t)ELEMENTS * 32u);
    RoboCheck(html != NULL);
    if (!html) {
        (void)HtmlDocumentDestroy(id, NULL);
        LcFixtureEnd();
        return;
    }
    for (i = 0u; i < (size_t)ELEMENTS; i++) {
        static const char PIECE[] = "<span>node</span>";

        memcpy(html + at, PIECE, sizeof(PIECE) - 1u);
        at += sizeof(PIECE) - 1u;
    }
    html[at] = '\0';

    HtmlSourceInit(&src);
    RoboCheckEqInt(
        HtmlSourceMemory(&src, (const uint8_t *)html, at, NULL),
        RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, &result, NULL), RoboOk);
    RoboCheckMsg(result.truncated, "expected the node ceiling to truncate");
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleInteractive);
    RoboCheck(!HtmlDocumentIsComplete(id));
    RoboCheck(HtmlDocumentIsSettled(id));
    RoboCheckEqInt(HtmlDocumentIsFinalized(id), 1);
    LcExpectClean(id);

    RoboFree(html);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleReplacementSourceReloads(void)
{
    HtmlDocumentId id;
    HtmlTreeResult result;

    id = LcDoc();
    LcBuild(id, "<html><body><p>one</p></body></html>", &result);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);

    /*
     * A settled document accepts a replacement source: COMPLETE → LOADING.
     * Settled is not sealed.
     */
    LcAttach(id, "<p>two</p>");
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleLoading);

    /* A second one-shot build reloads and settles again. */
    LcBuild(id, "<html><body><p>three</p></body></html>", &result);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleParserLeaseSettlesInteractive(void)
{
    HtmlDocumentId id;
    HtmlParser *parser = NULL;
    HtmlSource src;

    id = LcDoc();

    RoboCheckEqInt(HtmlParserCreate(id, &parser, NULL), RoboOk);
    LcSource(&src, "<div>streamed</div>");
    RoboCheckEqInt(HtmlParserAttachSource(parser, &src, NULL),
                      RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleLoading);

    /*
     * A parser detaching has not finished the load — the tree is whatever was
     * parsed so far — so the document is interactive.
     */
    RoboCheckEqInt(HtmlParserDetach(parser, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleInteractive);
    HtmlParserDestroy(parser);
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 3. Refused transitions leave the document exactly as it was         */
/* ------------------------------------------------------------------ */

static void TestLifecycleCompleteRequiresInteractive(void)
{
    HtmlDocumentId id;

    id = LcDoc();

    /* From CREATED, completion is refused: nothing has been parsed. */
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleCreated);

    /* From LOADING, also refused: parsing has not finished. */
    LcAttach(id, "<p>x</p>");
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleLoading);

    /* The intermediate state is the only way through. */
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleRepeatedTransitions(void)
{
    HtmlDocumentId id;

    id = LcDoc();

    /* Interactive is only reachable from LOADING. */
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleCreated);

    LcAttach(id, "<p>x</p>");
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL), RoboOk);
    /* ... and only once. */
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleInteractive);

    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL), RoboOk);
    /* Completion is a one-way edge, and there is no way back to interactive. */
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);

    /*
     * Failed attempts really changed nothing: the recorded previous state is
     * still the transition that succeeded.
     */
    {
        HtmlDocumentMetadata meta;

        RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                          RoboOk);
        RoboCheckEqInt(meta.previousLifecycle,
                          RoboHtmlDocumentLifecycleInteractive);
    }
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleInvalidTransitionsAreInert(void)
{
    HtmlDocumentId id;
    HtmlSource src;

    id = LcDoc();

    /* A failed document is sticky and cannot be talked back into loading. */
    LcAttach(id, "<p>x</p>");
    RoboCheckEqInt(
        HtmlDocumentMarkFailed(id, RoboHtmlDomainSource, "test",
                                       NULL),
        RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleFailed);
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleFailed);
    /* A failed document refuses new input rather than silently discarding. */
    LcSource(&src, "<p>y</p>");
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, NULL),
                      RoboError);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleFailed);
    /* And a second failure is a no-op, not an error about the first. */
    RoboCheckEqInt(
        HtmlDocumentMarkFailed(id, RoboHtmlDomainParser, "again",
                                       NULL),
        RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleFailed);
    /* Failure is not teardown: the tree is still there to inspect. */
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 4. Observability                                                    */
/* ------------------------------------------------------------------ */

static void TestLifecycleObservability(void)
{
    HtmlDocumentId id;
    HtmlDocumentId dead;
    HtmlDocumentLifecycle life;

    id = LcDoc();

    /* Exactly one predicate is true at a time, in every reachable state. */
    RoboCheckEqInt(HtmlDocumentIsLoading(id), 0);
    RoboCheckEqInt(HtmlDocumentIsInteractive(id), 0);
    RoboCheckEqInt(HtmlDocumentIsComplete(id), 0);

    LcAttach(id, "<p>x</p>");
    RoboCheckEqInt(HtmlDocumentIsLoading(id), 1);
    RoboCheckEqInt(HtmlDocumentIsInteractive(id), 0);
    RoboCheckEqInt(HtmlDocumentIsComplete(id), 0);

    (void)HtmlDocumentMarkInteractive(id, NULL);
    RoboCheckEqInt(HtmlDocumentIsLoading(id), 0);
    RoboCheckEqInt(HtmlDocumentIsInteractive(id), 1);
    RoboCheckEqInt(HtmlDocumentIsComplete(id), 0);

    (void)HtmlDocumentMarkComplete(id, NULL);
    RoboCheckEqInt(HtmlDocumentIsLoading(id), 0);
    RoboCheckEqInt(HtmlDocumentIsInteractive(id), 0);
    RoboCheckEqInt(HtmlDocumentIsComplete(id), 1);

    /* A retired id answers "no" to every state question... */
    dead = id;
    RoboCheckEqInt(HtmlDocumentDestroy(dead, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentIsLoading(dead), 0);
    RoboCheckEqInt(HtmlDocumentIsInteractive(dead), 0);
    RoboCheckEqInt(HtmlDocumentIsComplete(dead), 0);
    RoboCheckEqInt(HtmlDocumentIsSettled(dead), 0);
    /* ... and get_lifecycle is how a caller tells that apart from a state the
     * document is simply not in. */
    RoboCheckEqInt(HtmlDocumentGetLifecycle(dead, &life),
                      RoboNotFound);

    /* An id that was never issued behaves identically: retired ids are not
     * probeable, so this cannot become an oracle for recycled ids. */
    RoboCheckEqInt(
        HtmlDocumentIsComplete((HtmlDocumentId)(dead + 1000u)),
        0);
    LcFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 5. Lifecycle × mutation                                             */
/* ------------------------------------------------------------------ */

static void TestLifecycleMutationInEveryState(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle first;
    HtmlNodeHandle second;
    char buf[64];
    size_t length = 0u;

    id = LcDoc();
    root = LcRoot(id);

    /* CREATED: construction is open, and the tree is still mutable. */
    first = LcElement(id, &root, "div");
    second = LcElement(id, &root, "span");
    RoboCheckEqInt(LcChildCount(&root), 2u);
    LcExpectClean(id);

    /* LOADING: input is attached, and mutation keeps working. */
    LcAttach(id, "<p>ignored</p>");
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleLoading);
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &second, NULL),
                      RoboOk);
    RoboCheckEqInt(LcChildCount(&root), 1u);
    LcExpectClean(id);

    /* INTERACTIVE: parsing is over, the DOM is live. */
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL), RoboOk);
    (void)LcText(id, &first, "Hello");
    RoboCheckEqInt(LcChildCount(&first), 1u);
    LcExpectClean(id);

    /* COMPLETE: completion describes input, not permission. */
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    RoboCheckEqInt(HtmlNodeAppendChild(&first, &second, NULL),
                      RoboOk);
    RoboCheckEqInt(LcChildCount(&first), 2u);
    RoboCheckEqInt(
        HtmlNodeSubtreeText(&first, buf, sizeof(buf), &length, NULL),
        RoboOk);
    RoboCheck(strstr(buf, "Hello") != NULL);
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleMutationAfterDestruction(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle child;
    HtmlNodeHandle made;
    size_t removed = 0u;

    id = LcDoc();
    root = LcRoot(id);
    (void)LcElement(id, &root, "p");
    LcBuild(id, "<html><body><b>live</b></body></html>", NULL);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    child = LcFind(&root, "body");
    RoboCheck(HtmlNodeHandleIsWellformed(&child));

    /*
     * Destroy retires the document *and* every handle into it, so a mutation
     * attempted afterwards is a not-found lookup rather than a write into freed
     * state. The old handles are stale, not dangling.
     */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleIsStale(&root));
    RoboCheck(HtmlNodeHandleIsStale(&child));
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &child, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlNodeRemoveChildren(&root, &removed, NULL),
                      RoboNotFound);
    RoboCheckEqInt(removed, 0u);
    /* The document root accessor refuses too, rather than handing back a
     * zeroed handle as if it were a node. */
    made = HtmlDocumentRoot(id);
    RoboCheck(!HtmlNodeHandleIsWellformed(&made));

    /* Repeated destruction is safe: the second call reports not-found rather
     * than double-freeing. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboNotFound);
    LcFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 6. Lifecycle × traversal                                            */
/* ------------------------------------------------------------------ */

static void TestLifecycleTraversalInEveryState(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle child;
    LcCount count;

    id = LcDoc();
    root = LcRoot(id);

    /* CREATED: the root resolves, and an empty walk is legal, not an error. */
    RoboCheckEqInt(LcWalk(&root, &count), RoboOk);
    RoboCheckEqInt(count.nodes, 0u);

    /* LOADING: same, and still no crash on an unfinished tree. */
    LcAttach(id, "<p>x</p>");
    RoboCheckEqInt(LcWalk(&root, &count), RoboOk);
    RoboCheckEqInt(count.nodes, 0u);

    /* Add real children so the walk has something to reach. */
    child = LcElement(id, &root, "div");
    (void)LcElement(id, &child, "span");
    (void)LcText(id, &child, "text");

    /* INTERACTIVE: the walk reaches the whole subtree. */
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL), RoboOk);
    RoboCheckEqInt(LcWalk(&root, &count), RoboOk);
    RoboCheckEqInt(count.nodes, 3u);
    RoboCheckEqInt(count.elements, 2u);
    RoboCheckEqInt(count.texts, 1u);

    /* COMPLETE: exactly the same result. Completion does not change the tree. */
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL), RoboOk);
    RoboCheckEqInt(LcWalk(&root, &count), RoboOk);
    RoboCheckEqInt(count.nodes, 3u);
    RoboCheckEqInt(count.elements, 2u);
    RoboCheckEqInt(count.texts, 1u);
    LcExpectClean(id);

    /* After destruction the start handle is stale, so the walk refuses rather
     * than walking freed nodes. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(LcWalk(&root, &count), RoboNotFound);
    RoboCheckEqInt(count.nodes, 0u);
    LcFixtureEnd();
}

static void TestLifecycleTraversalAcrossTransitions(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle p;
    HtmlNodeHandle span;
    LcCount count;

    id = LcDoc();
    LcBuild(id, "<html><body><p>Hello</p></body></html>", NULL);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);

    root = LcRoot(id);
    body = LcFind(&root, "body");
    p = LcFind(&root, "p");
    RoboCheck(HtmlNodeHandleIsWellformed(&body));
    RoboCheck(HtmlNodeHandleIsWellformed(&p));
    RoboCheckEqInt(LcWalk(&body, &count), RoboOk);
    RoboCheck(count.nodes > 0u);

    /*
     * The lifecycle itself is not a structural change, so a handle and a walk
     * taken before a transition are still valid after one on the same tree.
     */
    LcAttach(id, "<p>unused</p>");
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleLoading);
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL), RoboOk);
    RoboCheck(!HtmlNodeHandleIsStale(&body));
    RoboCheck(!HtmlNodeHandleIsStale(&p));
    RoboCheckEqInt(LcWalk(&body, &count), RoboOk);
    RoboCheck(count.nodes > 0u);

    /*
     * A *reload* is a structural change: the previous tree is retired, and a
     * handle into it reports stale rather than resolving to whatever now
     * occupies that slot.
     */
    LcBuild(id, "<html><body><span>x</span></body></html>", NULL);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    RoboCheck(HtmlNodeHandleIsStale(&body));
    RoboCheck(HtmlNodeHandleIsStale(&p));

    root = LcRoot(id);
    span = LcFind(&root, "span");
    RoboCheck(HtmlNodeHandleIsWellformed(&span));
    RoboCheckEqInt(LcWalk(&span, &count), RoboOk);
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 7. Parser integration and malformed input                           */
/* ------------------------------------------------------------------ */

static void TestLifecycleParserIntegration(void)
{
    HtmlDocumentId id;
    HtmlTreeBuilder *builder = NULL;
    HtmlSource src;
    HtmlParserState state = RoboHtmlParserStateIdle;
    int i;

    id = LcDoc();

    /* Before any source the document is CREATED, not loading. */
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleCreated);

    RoboCheckEqInt(HtmlTreeBuilderCreate(id, &builder, NULL),
                      RoboOk);
    LcSource(&src, "<h1>Title</h1>");
    RoboCheckEqInt(HtmlTreeBuilderBegin(builder, &src, NULL),
                      RoboOk);
    /* Building is loading. */
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleLoading);
    RoboCheck(!HtmlDocumentIsSettled(id));

    for (i = 0; i < 64; i++) {
        RoboCheckEqInt(HtmlTreeBuilderNext(builder, NULL), RoboOk);
        RoboCheckEqInt(HtmlTreeBuilderGetState(builder, &state),
                          RoboOk);
        if (state == RoboHtmlParserStateComplete) {
            break;
        }
    }
    RoboCheckEqInt(state, RoboHtmlParserStateComplete);

    /*
     * The parser drove the transition, not the test: a finished build over a
     * complete source settles the document.
     */
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    RoboCheckEqInt(HtmlDocumentIsFinalized(id), 1);
    LcExpectClean(id);

    HtmlTreeBuilderDestroy(builder);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleMalformedHtml(void)
{
    static const char *const cases[] = {
        "",
        "<div><p>unclosed<span></div></b>text<<<",
        "<!doctype html><html><body><table><tr><td>cell",
        "&amp&amp;",
        "<a href=\"<script>\">link</a>",
        "<p>&#x1F600;</p>",
        "text only, no markup at all",
        "   \n\t  ",
        "<p><p><p><p><p>deep</p>",
    };
    HtmlDocumentId id;
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        HtmlTreeResult result;

        id = LcDoc();
        LcBuild(id, cases[i], &result);

        /*
         * Malformed input is recovered, never fatal: the document reaches a
         * defined settled state and its tree passes the audit, so no parse
         * outcome can produce an impossible lifecycle.
         */
        RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
        RoboCheck(HtmlDocumentIsSettled(id));
        RoboCheckEqInt(HtmlDocumentIsFinalized(id), 1);
        LcExpectClean(id);

        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }
    LcFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 8. Ownership, root integrity, reset                                 */
/* ------------------------------------------------------------------ */

static void TestLifecycleRootAndOwnershipIntegrity(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle rootBefore;
    HtmlNodeHandle rootAfter;
    HtmlNodeHandle body;
    HtmlNodeHandle p;
    HtmlNodeHandle prev;
    HtmlNodeHandle next;
    size_t countBefore;
    size_t countAfter;

    id = LcDoc();
    LcBuild(id, "<html><body><p>one</p><p>two</p></body></html>", NULL);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);

    rootBefore = LcRoot(id);
    countBefore = HtmlDocumentNodeCount(id);
    RoboCheck(HtmlDocumentOwnsNode(id, &rootBefore));

    /* A source attach alone moves the document to LOADING, then the caller
     * settles it again: the whole cycle, without touching the tree. */
    LcAttach(id, "<html><body><p>replacement</p></body></html>");
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleLoading);
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL), RoboOk);

    /* The root is still the same node, still owned, still the root. */
    rootAfter = LcRoot(id);
    RoboCheck(HtmlNodeHandleEqual(&rootBefore, &rootAfter));
    RoboCheck(HtmlDocumentOwnsNode(id, &rootAfter));
    countAfter = HtmlDocumentNodeCount(id);
    RoboCheckEqInt(countAfter, countBefore);
    LcExpectClean(id);

    /* Parent and sibling links read correctly after all those transitions. */
    body = LcFind(&rootAfter, "body");
    RoboCheck(HtmlNodeHandleIsWellformed(&body));
    p = LcFind(&body, "p");
    RoboCheck(HtmlNodeHandleIsWellformed(&p));
    RoboCheckEqInt(HtmlNodeParent(&p, &prev, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&prev, &body));
    /* The second <p> follows, and the last one has no successor. */
    RoboCheckEqInt(HtmlNodeNextSibling(&p, &next, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleIsWellformed(&next));
    RoboCheckEqInt(HtmlNodeNextSibling(&next, &prev, NULL),
                      RoboNotFound);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleResetSettlesComplete(void)
{
    HtmlDocumentId id;
    HtmlDocumentMetadata meta;

    id = LcDoc();

    /* Reset on a document that never loaded keeps CREATED: it still has no
     * source and never had one. */
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleCreated);

    /* Reset on a loading document abandons the load into the settled resting
     * state — the same state an empty source produces. */
    LcAttach(id, "<p>x</p>");
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleLoading);
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    RoboCheck(HtmlDocumentIsEmpty(id));
    LcExpectClean(id);

    /*
     * Reset on an *interactive* document — a load that was abandoned part way,
     * which is what a streamed document sits in — settles it the same way, and
     * succeeds. Re-requesting INTERACTIVE first would be an edge the machine
     * does not have, and failing here would leave the document cleared but
     * still marked interactive: a half-applied reset.
     */
    LcAttach(id, "<p>interrupted</p>");
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleInteractive);
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    RoboCheck(HtmlDocumentIsEmpty(id));
    LcExpectClean(id);

    /* Reset on a settled document leaves the state where it was. */
    LcBuild(id, "<p>y</p>", NULL);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.lifecycle, RoboHtmlDocumentLifecycleComplete);
    RoboCheckEqInt(meta.isEmpty, 1);
    RoboCheckEqInt(meta.nodeCount, 1u);
    LcExpectClean(id);

    /* And a reset document still accepts a new source. */
    LcBuild(id, "<p>z</p>", NULL);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    RoboCheck(!HtmlDocumentIsEmpty(id));

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleHandBuiltDocumentStaysCreated(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle child;

    id = LcDoc();
    root = LcRoot(id);
    child = LcElement(id, &root, "div");
    RoboCheck(HtmlNodeHandleIsWellformed(&child));

    /*
     * A document built through the element API and never given a source has no
     * load to settle, so it stays CREATED — and CREATED already permits
     * everything the DOM needs (content, mutation, traversal). There is
     * deliberately no CREATED → INTERACTIVE edge: every state above CREATED is
     * a statement about a load, and inventing a second route to INTERACTIVE
     * would blur what "created" means.
     */
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleCreated);
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleCreated);
    LcExpectClean(id);

    /* Attaching a source is what opens the way out of CREATED. */
    LcAttach(id, "<p>now loaded</p>");
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleTeardownSafety(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle deep;
    char name[32];
    char html[512];
    size_t at = 0u;
    int i;

    /*
     * Case 1: teardown from a construction site — a deep hand-built tree that
     * never settled, so teardown has to walk depth the parser did not make.
     */
    id = LcDoc();
    root = LcRoot(id);
    deep = root;
    for (i = 0; i < 12; i++) {
        sprintf(name, "d%d", i);
        deep = LcElement(id, &deep, name);
    }
    RoboCheck(HtmlNodeHandleIsWellformed(&deep));
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleIsStale(&root));
    RoboCheck(HtmlNodeHandleIsStale(&deep));
    RoboCheck(!HtmlDocumentIsLive(id));
    /* Destroying again is deterministic, never a double free. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboNotFound);

    /*
     * Case 2: teardown from COMPLETE — a deeply nested parsed tree, so
     * teardown runs after the document has settled and finalized.
     */
    for (i = 0; i < 20; i++) {
        memcpy(html + at, "<div>", 5u);
        at += 5u;
    }
    memcpy(html + at, "deep", 4u);
    at += 4u;
    for (i = 0; i < 20; i++) {
        memcpy(html + at, "</div>", 6u);
        at += 6u;
    }
    html[at] = '\0';

    id = LcDoc();
    LcBuild(id, html, NULL);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);
    root = LcRoot(id);
    deep = LcFind(&root, "div");
    RoboCheck(HtmlNodeHandleIsWellformed(&deep));
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleIsStale(&root));
    RoboCheck(HtmlNodeHandleIsStale(&deep));
    RoboCheck(!HtmlDocumentIsLive(id));
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboNotFound);
    LcFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 9. Debug output                                                     */
/* ------------------------------------------------------------------ */

static void TestLifecycleDumpShowsState(void)
{
    HtmlDocumentId id;
    char dump[4096];
    size_t length = 0u;

    id = LcDoc();

    /* The dump reports the document's own state, not a fixed string. */
    RoboCheckEqInt(
        HtmlTreeDumpToBuffer(id, dump, sizeof(dump), &length, NULL),
        RoboOk);
    RoboCheckMsg(strstr(dump, "Document lifecycle=created") != NULL, dump);

    LcAttach(id, "<p>x</p>");
    RoboCheckEqInt(
        HtmlTreeDumpToBuffer(id, dump, sizeof(dump), &length, NULL),
        RoboOk);
    RoboCheckMsg(strstr(dump, "Document lifecycle=loading") != NULL, dump);

    (void)HtmlDocumentMarkInteractive(id, NULL);
    RoboCheckEqInt(
        HtmlTreeDumpToBuffer(id, dump, sizeof(dump), &length, NULL),
        RoboOk);
    RoboCheckMsg(strstr(dump, "Document lifecycle=interactive") != NULL,
                   dump);

    (void)HtmlDocumentMarkComplete(id, NULL);
    RoboCheckEqInt(
        HtmlTreeDumpToBuffer(id, dump, sizeof(dump), &length, NULL),
        RoboOk);
    RoboCheckMsg(strstr(dump, "Document lifecycle=complete") != NULL, dump);

    /* And a parsed tree puts its elements under that same line. */
    LcBuild(id, "<html><body><p>Hello</p></body></html>", NULL);
    RoboCheckEqInt(
        HtmlTreeDumpToBuffer(id, dump, sizeof(dump), &length, NULL),
        RoboOk);
    RoboCheck(strstr(dump, "Document lifecycle=complete") != NULL);
    RoboCheck(strstr(dump, "p") != NULL);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 10. Preservation and diagnostics across transitions                 */
/* ------------------------------------------------------------------ */

static void TestLifecyclePreservesUnicodeAndAttributes(void)
{
    static const char *const DOC =
        "<html lang=\"ar\"><body>"
        "<p title=\"\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7\">"
        "\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c</p>"
        "<p data-name=\"\xe0\xa4\xb9\xe0\xa4\xbf\xe0\xa4\xa8\xe0\xa5\x8d"
        "\xe0\xa4\xa6\xe0\xa5\x80\">\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"
        "</p>"
        "<p aria-label=\"\xf0\x9f\x98\x80\">\xf0\x9f\x9a\x80</p>"
        "</body></html>";
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle p;
    HtmlAttribute attr;
    char text[256];
    size_t textLength = 0u;

    id = LcDoc();
    LcBuild(id, DOC, NULL);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);

    root = LcRoot(id);
    body = LcFind(&root, "body");
    RoboCheck(HtmlNodeHandleIsWellformed(&body));
    p = LcFind(&body, "p");
    RoboCheck(HtmlNodeHandleIsWellformed(&p));

    /*
     * Attributes and text are read *after* the lifecycle settled, so this is
     * also a check that completion did not disturb the payload arena.
     */
    HtmlAttributeInit(&attr);
    RoboCheckEqInt(
        HtmlElementFindAttribute(&p, "title", &attr, NULL), RoboOk);
    RoboCheckEqInt(attr.valueLength, 10u);
    RoboCheck(strcmp(attr.value, "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7") ==
               0);

    RoboCheckEqInt(
        HtmlNodeSubtreeText(&body, text, sizeof(text), &textLength,
                                    NULL),
        RoboOk);
    RoboCheck(strstr(text, "\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c") != NULL);
    RoboCheck(strstr(text, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e") != NULL);
    RoboCheck(strstr(text, "\xf0\x9f\x9a\x80") != NULL);

    /* The same values survive another trip through the whole lifecycle. */
    LcAttach(id, DOC);
    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, NULL), RoboOk);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleComplete);

    root = LcRoot(id);
    body = LcFind(&root, "body");
    p = LcFind(&body, "p");
    HtmlAttributeInit(&attr);
    RoboCheckEqInt(
        HtmlElementFindAttribute(&p, "title", &attr, NULL), RoboOk);
    RoboCheck(strcmp(attr.value, "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7") ==
               0);
    LcExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleDiagDomain(void)
{
    HtmlDocumentId id;
    HtmlDiag diag;
    HtmlSource src;

    id = LcDoc();

    /* A refused transition reports itself in the lifecycle domain, so a caller
     * can tell "your state machine says no" from "bad argument". */
    RoboCheckEqInt(HtmlDocumentMarkComplete(id, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboHtmlDomainLifecycle);

    RoboCheckEqInt(HtmlDocumentMarkInteractive(id, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboHtmlDomainLifecycle);

    /* An unknown document is a document-domain not-found: different question,
     * different answer. */
    RoboCheckEqInt(HtmlDocumentMarkComplete(
                          (HtmlDocumentId)9999u, &diag),
                      RoboNotFound);
    RoboCheckEqInt(diag.domain, RoboHtmlDomainDocument);

    /* Attaching while loading is busy, not invalid: the input is real. */
    LcAttach(id, "<p>x</p>");
    LcSource(&src, "<p>y</p>");
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, &diag),
                      RoboBusy);
    RoboCheckEqInt(LcState(id), RoboHtmlDocumentLifecycleLoading);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

static void TestLifecycleIsNotAStructuralChange(void)
{
    HtmlDocumentId id;
    uint32_t before = 0u;
    uint32_t after = 0u;

    id = LcDoc();
    LcBuild(id, "<html><body><p>x</p></body></html>", NULL);

    RoboCheckEqInt(HtmlNodeTreeVersion(id, &before, NULL), RoboOk);
    RoboCheck(before != 0u);

    /*
     * Lifecycle transitions are not structural changes: a document that
     * settles must not invalidate an in-flight traversal, because not one node
     * moved. The tree version is what says so.
     */
    LcAttach(id, "<p>again</p>");
    (void)HtmlDocumentMarkInteractive(id, NULL);
    (void)HtmlDocumentMarkComplete(id, NULL);
    RoboCheckEqInt(HtmlNodeTreeVersion(id, &after, NULL), RoboOk);
    RoboCheckEqInt((int)(after - before), 0);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    LcFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlLifecycle(void)
{
    TestRegister("html.lifecycle.initial_state",
                       TestLifecycleInitialState);
    TestRegister("html.lifecycle.valid_path",
                       TestLifecycleValidPath);
    TestRegister("html.lifecycle.one_shot_build_completes",
                       TestLifecycleOneShotBuildCompletes);
    TestRegister("html.lifecycle.empty_source_settles_complete",
                       TestLifecycleEmptySourceSettlesComplete);
    TestRegister("html.lifecycle.detach_settles_interactive",
                       TestLifecycleDetachSettlesInteractive);
    TestRegister("html.lifecycle.truncated_build_stays_interactive",
                       TestLifecycleTruncatedBuildStaysInteractive);
    TestRegister("html.lifecycle.replacement_source_reloads",
                       TestLifecycleReplacementSourceReloads);
    TestRegister("html.lifecycle.parser_lease_settles_interactive",
                       TestLifecycleParserLeaseSettlesInteractive);
    TestRegister("html.lifecycle.complete_requires_interactive",
                       TestLifecycleCompleteRequiresInteractive);
    TestRegister("html.lifecycle.repeated_transitions",
                       TestLifecycleRepeatedTransitions);
    TestRegister("html.lifecycle.invalid_transitions_are_inert",
                       TestLifecycleInvalidTransitionsAreInert);
    TestRegister("html.lifecycle.observability",
                       TestLifecycleObservability);
    TestRegister("html.lifecycle.mutation_in_every_state",
                       TestLifecycleMutationInEveryState);
    TestRegister("html.lifecycle.mutation_after_destruction",
                       TestLifecycleMutationAfterDestruction);
    TestRegister("html.lifecycle.traversal_in_every_state",
                       TestLifecycleTraversalInEveryState);
    TestRegister("html.lifecycle.traversal_across_transitions",
                       TestLifecycleTraversalAcrossTransitions);
    TestRegister("html.lifecycle.parser_integration",
                       TestLifecycleParserIntegration);
    TestRegister("html.lifecycle.malformed_html",
                       TestLifecycleMalformedHtml);
    TestRegister("html.lifecycle.root_and_ownership_integrity",
                       TestLifecycleRootAndOwnershipIntegrity);
    TestRegister("html.lifecycle.reset_settles_complete",
                       TestLifecycleResetSettlesComplete);
    TestRegister("html.lifecycle.hand_built_document_stays_created",
                       TestLifecycleHandBuiltDocumentStaysCreated);
    TestRegister("html.lifecycle.teardown_safety",
                       TestLifecycleTeardownSafety);
    TestRegister("html.lifecycle.dump_shows_state",
                       TestLifecycleDumpShowsState);
    TestRegister("html.lifecycle.preserves_unicode_and_attributes",
                       TestLifecyclePreservesUnicodeAndAttributes);
    TestRegister("html.lifecycle.diag_domain",
                       TestLifecycleDiagDomain);
    TestRegister("html.lifecycle.is_not_a_structural_change",
                       TestLifecycleIsNotAStructuralChange);
}
