/*
 * RoboHTML Phase 8.4 document-model suite.
 *
 * Covers the document as the authoritative context for the HTML tree:
 * identity and metadata, root ownership, node ownership and staleness,
 * cross-document mistakes, source/encoding association, construction
 * closure and reuse, whole-tree invariant auditing, failure state,
 * teardown, and the end-to-end tokenizer → tree builder → document path.
 *
 * The tests assert *behaviour*, so every one of them drives real APIs and
 * inspects real state; nothing here merely proves a header compiles.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void DmFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void DmFixtureEnd(void)
{
    HtmlShutdown();
}

static HtmlDocumentId DmDoc(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

/** Build @p text into @p id; result optional. */
static void DmBuild(HtmlDocumentId id,
                     const char *text,
                     HtmlTreeResult *out)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, out, NULL), RoboOk);
}

/** Handle of the first descendant element named @p name (invalid when none). */
static HtmlNodeHandle DmFind(HtmlDocumentId id,
                                       const char *name)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);
    HtmlNodeHandle found;

    HtmlNodeHandleClear(&found);
    if (HtmlNodeFindElement(&root, name, &found, NULL) != RoboOk) {
        HtmlNodeHandleClear(&found);
    }
    return found;
}

/** Non-zero when the first descendant element named @p name is live. */
static int DmHas(HtmlDocumentId id, const char *name)
{
    HtmlNodeHandle h = DmFind(id, name);

    return HtmlNodeHandleIsWellformed(&h);
}

/*
 * A document whose shape is exactly known, used wherever counts matter.
 * The input carries no whitespace between tags, so the only text nodes are
 * the two element texts — which keeps the expected counts checkable by hand.
 */
#define DM_COMPACT_DOC                                                       \
    "<!DOCTYPE html><html><head><title>T</title></head>"                     \
    "<body><p>a</p><!--c--></body></html>"

/** Trees are expected to be consistent unless a test says otherwise. */
static void DmCheckTreeClean(HtmlDocumentId id)
{
    HtmlTreeReport rep;

    RoboCheckEqInt(HtmlDocumentValidateTree(id, &rep, NULL),
                      RoboOk);
    RoboCheckEqInt(rep.rootOk, 1);
    RoboCheckEqInt(rep.orphans, 0u);
    RoboCheckEqInt(rep.cycles, 0u);
    RoboCheckEqInt(rep.badLinks, 0u);
    RoboCheckEqInt(rep.badParents, 0u);
    RoboCheckEqInt(rep.badCounts, 0u);
    RoboCheckEqInt(rep.siblingMismatches, 0u);
    RoboCheckEqInt(rep.ok, 1);
    RoboCheckEqInt(rep.nodes, rep.registered);
}

/* ------------------------------------------------------------------ */
/* 1. Metadata (items 1-5, 16, 27, 28)                                 */
/* ------------------------------------------------------------------ */

static void TestDmMetadataFresh(void)
{
    HtmlDocumentId id;
    HtmlDocumentMetadata meta;
    size_t kinds[RoboHtmlNodeKindCount];

    DmFixtureBegin();
    id = DmDoc();

    RoboCheckEqInt(
        GetHtmlDocumentMetadata(id, &meta, kinds, NULL), RoboOk);
    RoboCheckEqInt(meta.id, id);
    RoboCheckEqInt(meta.lifecycle, RoboHtmlDocumentLifecycleCreated);
    RoboCheckEqInt(meta.finalized, 0);
    RoboCheckEqInt(meta.isEmpty, 1);
    RoboCheckEqInt(meta.rootOk, 1);
    RoboCheckEqInt(meta.nodeCount, 1u); /* the document node itself */
    RoboCheckEqInt(meta.elementCount, 0u);
    RoboCheckEqInt(meta.textCount, 0u);
    RoboCheckEqInt(meta.commentCount, 0u);
    RoboCheckEqInt(meta.doctypeCount, 0u);
    RoboCheckEqInt(meta.sourceKind, RoboHtmlSourceKindNone);
    RoboCheckEqInt(meta.sourceLength, 0u);
    /* Unset labels are empty strings, never NULL, so callers need no guard. */
    RoboCheck(meta.sourceName != NULL);
    RoboCheckStrEq(meta.sourceName, "");
    RoboCheck(meta.failMessage == NULL);
    RoboCheckEqInt(meta.failDomain, RoboHtmlDomainNone);
    RoboCheckEqInt(meta.encoding.declared, RoboHtmlEncodingUnknown);
    RoboCheckEqInt(meta.encoding.status, RoboHtmlEncodingStatusOk);

    /* Per-kind counts, indexed by node kind. */
    RoboCheckEqInt(kinds[RoboHtmlNodeKindDocument], 1u);
    RoboCheckEqInt(kinds[RoboHtmlNodeKindElement], 0u);
    RoboCheckEqInt(kinds[RoboHtmlNodeKindText], 0u);
    RoboCheckEqInt(kinds[RoboHtmlNodeKindComment], 0u);
    RoboCheckEqInt(kinds[RoboHtmlNodeKindDocumentType], 0u);
    RoboCheckEqInt(kinds[RoboHtmlNodeKindFragment], 0u);

    DmCheckTreeClean(id);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmMetadataBuilt(void)
{
    HtmlDocumentId id;
    HtmlDocumentMetadata meta;
    size_t kinds[RoboHtmlNodeKindCount];
    HtmlTreeReport rep;

    DmFixtureBegin();
    id = DmDoc();
    DmBuild(id, DM_COMPACT_DOC, NULL);

    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, kinds, NULL),
                      RoboOk);
    /* A one-shot build consumes a complete source, so the document is done. */
    RoboCheckEqInt(meta.lifecycle, RoboHtmlDocumentLifecycleComplete);
    /*
     * The build drove CREATED → LOADING → INTERACTIVE → COMPLETE, so the
     * step immediately before the current state is INTERACTIVE — which is
     * what "previous" means here, and what makes the two-step settle
     * observable rather than inferred.
     */
    RoboCheckEqInt(meta.previousLifecycle,
                      RoboHtmlDocumentLifecycleInteractive);
    RoboCheckEqInt(meta.finalized, 1);
    RoboCheckEqInt(meta.isEmpty, 0);
    RoboCheckEqInt(meta.rootOk, 1);

    /* document + doctype + html head title body p + 2 text + 1 comment */
    RoboCheckEqInt(meta.nodeCount, 10u);
    RoboCheckEqInt(meta.elementCount, 5u);  /* html head title body p */
    RoboCheckEqInt(meta.textCount, 2u);     /* "T", "a" */
    RoboCheckEqInt(meta.commentCount, 1u);
    RoboCheckEqInt(meta.doctypeCount, 1u);
    RoboCheckEqInt(kinds[RoboHtmlNodeKindElement], 5u);
    RoboCheckEqInt(kinds[RoboHtmlNodeKindText], 2u);
    RoboCheckEqInt(kinds[RoboHtmlNodeKindComment], 1u);
    RoboCheckEqInt(kinds[RoboHtmlNodeKindDocumentType], 1u);
    RoboCheckEqInt(kinds[RoboHtmlNodeKindDocument], 1u);

    /* The metadata counts and the registry-derived count must agree. */
    RoboCheckEqInt(meta.nodeCount, HtmlDocumentNodeCount(id));

    /*
     * Depth is node depth: root 0, doctype/html 1, head/body 2,
     * title/p/comment 3, and the two text nodes at 4.
     */
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &rep, NULL),
                      RoboOk);
    RoboCheckEqInt(rep.maxDepth, 4u);
    DmCheckTreeClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmMetadataMisuse(void)
{
    HtmlDocumentId id;
    HtmlDocumentMetadata meta;
    HtmlDiag diag;

    DmFixtureBegin();
    id = DmDoc();

    RoboCheckEqInt(GetHtmlDocumentMetadata(id, NULL, NULL, NULL),
                      RoboInvalidArgument);

    /* An unknown id is not found, and the snapshot is left zeroed. */
    HtmlDiagReset(&diag);
    memset(&meta, 0xAB, sizeof(meta));
    RoboCheckEqInt(
        GetHtmlDocumentMetadata(0xDEADu, &meta, NULL, &diag),
        RoboNotFound);
    RoboCheckEqInt(diag.domain, RoboHtmlDomainDocument);
    RoboCheckEqInt(meta.nodeCount, 0u);
    RoboCheckEqInt(meta.rootOk, 0);

    /* outKindCount is optional; passing NULL is not an error. */
    RoboCheckEqInt(
        GetHtmlDocumentMetadata(id, &meta, NULL, NULL), RoboOk);

    /* A destroyed document reports NOT_FOUND, not stale data. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(
        GetHtmlDocumentMetadata(id, &meta, NULL, NULL), RoboNotFound);

    DmFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 2. Root (items 7-9, 23)                                             */
/* ------------------------------------------------------------------ */

static void TestDmRoot(void)
{
    HtmlDocumentId a;
    HtmlDocumentId b;
    HtmlNodeHandle rootA;
    HtmlNodeHandle rootB;
    HtmlNodeView view;
    HtmlNodeKind kind = RoboHtmlNodeKindFragment;

    DmFixtureBegin();
    a = DmDoc();
    b = DmDoc();

    rootA = HtmlDocumentRoot(a);
    rootB = HtmlDocumentRoot(b);
    RoboCheck(HtmlNodeHandleIsWellformed(&rootA));
    RoboCheck(HtmlNodeHandleIsWellformed(&rootB));

    /* The root is a document node with no parent. */
    RoboCheckEqInt(HtmlNodeHandleResolve(&rootA, &kind), RoboOk);
    RoboCheckEqInt(kind, RoboHtmlNodeKindDocument);
    RoboCheckEqInt(HtmlNodeGetView(&rootA, &view, NULL), RoboOk);
    RoboCheck(!HtmlNodeHandleIsWellformed(&view.parent));
    RoboCheckEqInt(view.kind, RoboHtmlNodeKindDocument);

    /* Identity is the document id, so two roots are never equal. */
    RoboCheck(!HtmlNodeHandleEqual(&rootA, &rootB));
    RoboCheckEqInt(HtmlNodeHandleDocument(&rootA), a);
    RoboCheckEqInt(HtmlNodeHandleDocument(&rootB), b);

    /* Ownership: a root belongs to its own document and to no other. */
    RoboCheck(HtmlDocumentOwnsNode(a, &rootA));
    RoboCheck(!HtmlDocumentOwnsNode(b, &rootA));
    RoboCheck(!HtmlDocumentOwnsNode(a, &rootB));
    RoboCheck(HtmlDocumentOwnsNode(b, &rootB));

    /* Retrieval is stable, and an unknown document has no root at all. */
    {
        HtmlNodeHandle again = HtmlDocumentRoot(a);
        HtmlNodeHandle none = HtmlDocumentRoot(0xDEADu);

        RoboCheck(HtmlNodeHandleEqual(&rootA, &again));
        RoboCheck(!HtmlNodeHandleIsWellformed(&none));
    }

    RoboCheckEqInt(HtmlDocumentDestroy(a, NULL), RoboOk);
    /* A retired root is stale, and the document no longer hands one out. */
    RoboCheck(HtmlNodeHandleIsStale(&rootA));
    RoboCheck(!HtmlDocumentOwnsNode(a, &rootA));
    {
        HtmlNodeHandle gone = HtmlDocumentRoot(a);

        RoboCheck(!HtmlNodeHandleIsWellformed(&gone));
    }
    RoboCheckEqInt(HtmlDocumentDestroy(b, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmRootSurvivesRebuild(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle rootBefore;
    HtmlNodeHandle rootAfter;
    HtmlNodeHandle pBefore;

    DmFixtureBegin();
    id = DmDoc();
    DmBuild(id, "<p>one</p>", NULL);

    rootBefore = HtmlDocumentRoot(id);
    pBefore = DmFind(id, "p");
    RoboCheck(HtmlNodeHandleIsWellformed(&pBefore));

    /* Re-parsing replaces the content but not the document's own root. */
    DmBuild(id, "<div>two</div>", NULL);
    rootAfter = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleEqual(&rootBefore, &rootAfter));
    RoboCheck(HtmlDocumentOwnsNode(id, &rootBefore));
    RoboCheck(!HtmlNodeHandleIsStale(&rootBefore));

    /* Every other node from the first tree is retired. */
    RoboCheck(HtmlNodeHandleIsStale(&pBefore));
    RoboCheckEqInt(HtmlNodeHandleResolve(&pBefore, NULL),
                      RoboNotFound);
    RoboCheck(!HtmlDocumentOwnsNode(id, &pBefore));

    /* And the rebuilt tree is itself consistent. */
    RoboCheck(DmHas(id, "div"));
    DmCheckTreeClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 3. Identity, ownership and staleness (items 10-14)                   */
/* ------------------------------------------------------------------ */

static void TestDmNodeIdentityAndLookup(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle ById;
    HtmlNodeHandle p;
    HtmlDiag diag;

    DmFixtureBegin();
    id = DmDoc();
    DmBuild(id, "<p>x</p>", NULL);

    /* Root id → handle round-trips to the identical identity. */
    root = HtmlDocumentRoot(id);
    RoboCheckEqInt(
        HtmlDocumentNodeById(id, root.nodeId, &ById, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&root, &ById));

    p = DmFind(id, "p");
    RoboCheck(HtmlNodeHandleIsWellformed(&p));
    RoboCheckEqInt(
        HtmlDocumentNodeById(id, p.nodeId, &ById, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&p, &ById));
    RoboCheck(HtmlDocumentOwnsNode(id, &p));

    /* Ids are dense: each live node occupies id == slot + 1. */
    {
        size_t count = HtmlDocumentNodeCount(id);
        size_t i;
        int dense = 1;

        for (i = 1u; i <= count; i++) {
            HtmlNodeHandle h;

            if (HtmlDocumentNodeById(id, i, &h, NULL) != RoboOk ||
                h.nodeId != i) {
                dense = 0;
            }
        }
        RoboCheck(dense);
        /* And one past the end is genuinely absent. */
        RoboCheckEqInt(HtmlDocumentNodeById(
                              id, (HtmlNodeId)count + 1u, &ById, NULL),
                          RoboNotFound);
    }

    /* Argument and lookup failures are reported, and never leave a handle. */
    RoboCheckEqInt(HtmlDocumentNodeById(id, root.nodeId, NULL,
                                                    NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(
        HtmlDocumentNodeById(id, RoboHtmlNodeIdNone, &ById, &diag),
        RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboHtmlDomainTree);
    RoboCheck(!HtmlNodeHandleIsWellformed(&ById));

    memset(&ById, 0xAB, sizeof(ById));
    RoboCheckEqInt(HtmlDocumentNodeById(id, 9999u, &ById, NULL),
                      RoboNotFound);
    RoboCheck(!HtmlNodeHandleIsWellformed(&ById));

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmStaleHandles(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle h;
    HtmlNodeHandle forged;
    HtmlNodeKind kind = RoboHtmlNodeKindFragment;

    DmFixtureBegin();
    id = DmDoc();
    DmBuild(id, "<h1>H</h1>", NULL);
    h = DmFind(id, "h1");
    RoboCheck(!HtmlNodeHandleIsStale(&h));

    /*
     * A forged generation on a live id must be stale. Staleness is a
     * generation question, not only an id question, so resolve and
     * is_stale have to agree here.
     */
    forged = h;
    forged.generation = h.generation + 1000u;
    RoboCheck(HtmlNodeHandleIsStale(&forged));
    RoboCheckEqInt(HtmlNodeHandleResolve(&forged, &kind),
                      RoboNotFound);
    RoboCheck(!HtmlDocumentOwnsNode(id, &forged));

    /* A handle whose document is unknown is stale, not merely unresolvable. */
    forged = h;
    forged.documentId = 0xDEADu;
    RoboCheck(HtmlNodeHandleIsStale(&forged));

    /* Retired by a rebuild: is_stale, resolve and ownership all agree. */
    DmBuild(id, "<p>P</p>", NULL);
    RoboCheck(HtmlNodeHandleIsStale(&h));
    RoboCheckEqInt(HtmlNodeHandleResolve(&h, &kind), RoboNotFound);
    RoboCheck(!HtmlDocumentOwnsNode(id, &h));

    /* An ill-formed handle is an argument error, never "stale". */
    {
        HtmlNodeHandle zero;

        HtmlNodeHandleClear(&zero);
        RoboCheck(!HtmlNodeHandleIsStale(&zero));
        RoboCheck(!HtmlDocumentOwnsNode(id, &zero));
        RoboCheck(!HtmlNodeHandleIsStale(NULL));
        RoboCheck(!HtmlDocumentOwnsNode(id, NULL));
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmCrossDocument(void)
{
    HtmlDocumentId a;
    HtmlDocumentId b;
    HtmlNodeHandle pa;
    HtmlNodeHandle pb;

    DmFixtureBegin();
    a = DmDoc();
    b = DmDoc();
    DmBuild(a, "<p>a</p>", NULL);
    DmBuild(b, "<p>b</p>", NULL);

    pa = DmFind(a, "p");
    pb = DmFind(b, "p");
    RoboCheck(HtmlNodeHandleIsWellformed(&pa));
    RoboCheck(HtmlNodeHandleIsWellformed(&pb));

    /* Same shape, same local ids — but ownership never crosses documents. */
    RoboCheck(HtmlDocumentOwnsNode(a, &pa));
    RoboCheck(HtmlDocumentOwnsNode(b, &pb));
    RoboCheck(!HtmlDocumentOwnsNode(b, &pa));
    RoboCheck(!HtmlDocumentOwnsNode(a, &pb));
    RoboCheckEqInt(HtmlNodeHandleDocument(&pa), a);
    RoboCheckEqInt(HtmlNodeHandleDocument(&pb), b);

    /* Node ids may coincide while identities stay distinct. */
    if (pa.nodeId == pb.nodeId) {
        RoboCheck(pa.generation != pb.generation ||
                   pa.documentId != pb.documentId);
    }

    /* A node from a keeps resolving in a even as b is rebuilt. */
    DmBuild(b, "<div>other</div>", NULL);
    RoboCheck(HtmlDocumentOwnsNode(a, &pa));
    RoboCheck(!HtmlNodeHandleIsStale(&pa));
    RoboCheck(HtmlNodeHandleIsStale(&pb));

    RoboCheckEqInt(HtmlDocumentDestroy(a, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentDestroy(b, NULL), RoboOk);
    DmFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 4. Invariant auditing (items 14, 24)                                */
/* ------------------------------------------------------------------ */

static void TestDmInvariantsClean(void)
{
    HtmlDocumentId id;
    HtmlTreeReport rep;

    DmFixtureBegin();
    id = DmDoc();

    /* An empty document is consistent: one node, the root, nothing else. */
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &rep, NULL),
                      RoboOk);
    RoboCheckEqInt(rep.ok, 1);
    RoboCheckEqInt(rep.registered, 1u);
    RoboCheckEqInt(rep.nodes, 1u);
    RoboCheckEqInt(rep.maxDepth, 0u);

    DmBuild(id, DM_COMPACT_DOC, NULL);
    DmCheckTreeClean(id);
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &rep, NULL),
                      RoboOk);
    RoboCheckEqInt(rep.nodes, 10u);
    RoboCheckEqInt(rep.maxDepth, 4u); /* the two text nodes sit deepest */

    RoboCheckEqInt(HtmlDocumentValidateTree(id, NULL, NULL),
                      RoboInvalidArgument);
    {
        HtmlDiag diag;

        HtmlDiagReset(&diag);
        RoboCheckEqInt(
            HtmlDocumentValidateTree(0xDEADu, &rep, &diag),
            RoboNotFound);
        /* The report is zeroed even when the audit could not run. */
        RoboCheckEqInt(rep.ok, 0);
        RoboCheckEqInt(rep.registered, 0u);
    }

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmInvariantsOrphanDetected(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle stray;
    HtmlTreeReport rep;

    DmFixtureBegin();
    id = DmDoc();

    /*
     * Registering a node without linking it produces a node the tree cannot
     * reach. That is reachable through the public construction API, which is
     * exactly why the audit must notice it: an unreachable live node is a
     * leak that teardown would still reclaim but rendering would never see.
     *
     * Phase 8.9 refined the verdict rather than the counting. A node with no
     * parent is now a *legal* state — remove_child detaches a node and keeps
     * it re-insertable — so the audit reports such a node as @c detached
     * instead of calling it corruption, and @c orphans is reserved for the
     * case that is always wrong: a node claiming a parent while nothing
     * reachable lists it. The node is still reported, which is the point of
     * this test; what changed is which counter says so.
     */
    RoboCheckEqInt(HtmlDocumentRegisterNode(
                          id, RoboHtmlNodeKindElement,
                          RoboHtmlNodeOriginTest, &stray, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &rep, NULL),
                      RoboOk);
    RoboCheckEqInt(rep.registered, 2u);
    RoboCheckEqInt(rep.nodes, 1u);   /* only the root is reachable */
    RoboCheckEqInt(rep.detached, 1u);
    RoboCheckEqInt(rep.orphans, 0u);
    RoboCheckEqInt(rep.rootOk, 1);
    RoboCheckEqInt(rep.ok, 1);

    /* Clearing the tree removes both the stray node and the report. */
    DmBuild(id, "<p>x</p>", NULL);
    DmCheckTreeClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmInvariantsDeepChain(void)
{
    HtmlDocumentId id;
    HtmlTreeReport rep;
    char html[4096];
    size_t used = 0u;
    int i;

    DmFixtureBegin();
    id = DmDoc();
    for (i = 0; i < 300; i++) {
        html[used++] = '<';
        html[used++] = 'd';
        html[used++] = 'i';
        html[used++] = 'v';
        html[used++] = '>';
    }
    html[used++] = 'x';
    html[used] = '\0';
    DmBuild(id, html, NULL);

    RoboCheckEqInt(HtmlDocumentValidateTree(id, &rep, NULL),
                      RoboOk);
    RoboCheckEqInt(rep.ok, 1);
    /* root + html head body + 300 divs + 1 text */
    RoboCheckEqInt(rep.registered, 305u);
    RoboCheckEqInt(rep.nodes, 305u);
    /* html 1, body 2, 300 nested divs → 302, and the text inside the last. */
    RoboCheckEqInt(rep.maxDepth, 303u);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 5. Source and encoding association (items 15-17)                    */
/* ------------------------------------------------------------------ */

static void TestDmSourceAssociation(void)
{
    HtmlDocumentId id;
    HtmlDocumentMetadata meta;
    HtmlSource src;

    DmFixtureBegin();
    id = DmDoc();

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, "<p>hello</p>", NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlSourceSetName(&src, "Applications/Demo.html",
                                               NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, NULL),
                      RoboOk);

    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.sourceKind, RoboHtmlSourceKindString);
    RoboCheckEqInt(meta.sourceLength, strlen("<p>hello</p>"));
    RoboCheckStrEq(meta.sourceName, "Applications/Demo.html");
    RoboCheckEqInt(meta.lifecycle, RoboHtmlDocumentLifecycleLoading);

    /* Detaching clears the association rather than leaving it dangling. */
    RoboCheckEqInt(HtmlDocumentDetachSource(id, NULL), RoboOk);
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.sourceKind, RoboHtmlSourceKindNone);
    RoboCheckEqInt(meta.sourceLength, 0u);
    RoboCheckStrEq(meta.sourceName, "");
    /* Detaching abandons the input, so the document is interactive. */
    RoboCheckEqInt(meta.lifecycle, RoboHtmlDocumentLifecycleInteractive);

    /* Detach with nothing attached is a no-op, not an error. */
    RoboCheckEqInt(HtmlDocumentDetachSource(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentAttachSource(id, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlDocumentAttachSource(0xDEADu, &src, NULL),
                      RoboNotFound);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmSourceBytesAreNotBorrowedByTheTree(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle p;
    HtmlSource src;
    char buffer[64];
    char text[32];
    size_t length = 0u;

    DmFixtureBegin();
    id = DmDoc();

    strcpy(buffer, "<p>keep me</p>");
    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, buffer, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);

    /*
     * The tree must own what it renders. Scribbling over the source buffer
     * after the build proves the document copied everything it needs rather
     * than pointing back into caller memory.
     */
    memset(buffer, 'Z', sizeof(buffer));

    p = DmFind(id, "p");
    RoboCheck(HtmlNodeHandleIsWellformed(&p));
    RoboCheckEqInt(HtmlNodeSubtreeText(&p, text, sizeof(text),
                                                  &length, NULL),
                      RoboOk);
    RoboCheckStrEq(text, "keep me");
    RoboCheckEqInt(length, strlen("keep me"));

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmEncodingAssociation(void)
{
    HtmlDocumentId id;
    HtmlDocumentMetadata meta;

    DmFixtureBegin();
    id = DmDoc();

    /* A declared label reaches the document's encoding state. */
    RoboCheckEqInt(
        HtmlDocumentInputEncodingDeclared(id, "UTF-8", NULL), RoboOk);
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.encoding.declared, RoboHtmlEncodingUtf8);
    RoboCheckEqInt(meta.encoding.actual, RoboHtmlEncodingUtf8);
    RoboCheckEqInt(meta.encoding.status, RoboHtmlEncodingStatusOk);

    /* An unsupported-but-valid label is recorded, not fatal. */
    RoboCheckEqInt(
        HtmlDocumentInputEncodingDeclared(id, "Shift_JIS", NULL),
        RoboOk);
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.encoding.declared, RoboHtmlEncodingShiftJis);

    /* Encoding state is part of the document, so it survives a build. */
    DmBuild(id, "<p>x</p>", NULL);
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.encoding.declared, RoboHtmlEncodingShiftJis);

    /* Reset drops it along with the tree. */
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.encoding.declared, RoboHtmlEncodingUnknown);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 6. Finalization and reuse (items 18, 30)                            */
/* ------------------------------------------------------------------ */

static void TestDmFinalize(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle extra;
    HtmlDiag diag;
    HtmlDocumentMetadata meta;

    DmFixtureBegin();
    id = DmDoc();

    /* A fresh document accepts construction. */
    RoboCheck(HtmlDocumentAllowsConstruction(id));
    RoboCheck(!HtmlDocumentIsFinalized(id));

    DmBuild(id, "<p>x</p>", NULL);

    /* A completed build closes construction. */
    RoboCheck(HtmlDocumentIsFinalized(id));
    RoboCheck(!HtmlDocumentAllowsConstruction(id));
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.finalized, 1);

    HtmlDiagReset(&diag);
    RoboCheckEqInt(HtmlDocumentRegisterNode(
                          id, RoboHtmlNodeKindElement,
                          RoboHtmlNodeOriginTest, &extra, &diag),
                      RoboError);
    RoboCheckEqInt(diag.domain, RoboHtmlDomainTree);

    /* Finalization is idempotent. */
    RoboCheckEqInt(HtmlDocumentFinalize(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentFinalize(id, NULL), RoboOk);

    /* Reading is unaffected: the tree is still fully usable. */
    DmCheckTreeClean(id);
    {
        HtmlNodeHandle root = HtmlDocumentRoot(id);
        size_t count = 0u;

        RoboCheck(HtmlDocumentOwnsNode(id, &root));
        RoboCheckEqInt(HtmlNodeChildCount(&root, &count, NULL),
                          RoboOk);
        RoboCheck(count > 0u);
    }

    /* A replacement source reopens the document for parsing. */
    {
        HtmlSource src;

        HtmlSourceInit(&src);
        RoboCheckEqInt(HtmlSourceString(&src, "<p>y</p>", NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlDocumentAttachSource(id, &src, NULL),
                          RoboOk);
        RoboCheck(!HtmlDocumentIsFinalized(id));
        RoboCheck(HtmlDocumentAllowsConstruction(id));
    }
    /* And a build closes it again. */
    DmBuild(id, "<p>z</p>", NULL);
    RoboCheck(HtmlDocumentIsFinalized(id));

    /* Reset reopens it once more. */
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheck(!HtmlDocumentIsFinalized(id));
    RoboCheck(HtmlDocumentAllowsConstruction(id));

    /* Finalizing an unknown document is NOT_FOUND, not a silent success. */
    RoboCheckEqInt(HtmlDocumentFinalize(0xDEADu, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlDocumentIsFinalized(0xDEADu), 0);
    RoboCheckEqInt(HtmlDocumentAllowsConstruction(0xDEADu), 0);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmReset(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle rootBefore;
    HtmlNodeHandle p;
    HtmlNodeHandle rootAfter;
    HtmlDocumentMetadata meta;

    DmFixtureBegin();
    id = DmDoc();

    /* Reset on a document that never loaded anything keeps it CREATED. */
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id,
                                                       &meta.lifecycle),
                      RoboOk);
    RoboCheckEqInt(meta.lifecycle, RoboHtmlDocumentLifecycleCreated);
    RoboCheck(HtmlDocumentIsEmpty(id));

    DmBuild(id, "<p>one</p>", NULL);
    rootBefore = HtmlDocumentRoot(id);
    p = DmFind(id, "p");
    RoboCheck(HtmlNodeHandleIsWellformed(&p));

    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    /*
     * A document that had loaded settles in COMPLETE with an empty tree:
     * that is exactly the state an empty source produces, and no input can
     * arrive.
     */
    RoboCheckEqInt(meta.lifecycle, RoboHtmlDocumentLifecycleComplete);
    RoboCheck(HtmlDocumentIsSettled(id));
    RoboCheckEqInt(meta.isEmpty, 1);
    RoboCheckEqInt(meta.nodeCount, 1u);
    RoboCheckEqInt(meta.sourceLength, 0u);
    RoboCheckStrEq(meta.sourceName, "");
    RoboCheckEqInt(meta.finalized, 0);
    RoboCheck(HtmlDocumentAllowsConstruction(id));

    /* Identity survives: the root is the same node, content is not. */
    rootAfter = HtmlDocumentRoot(id);
    RoboCheck(HtmlNodeHandleEqual(&rootBefore, &rootAfter));
    RoboCheck(HtmlDocumentOwnsNode(id, &rootAfter));
    RoboCheck(HtmlNodeHandleIsStale(&p));

    DmCheckTreeClean(id);

    /* The reset document is fully usable for a new parse. */
    DmBuild(id, "<div>two</div>", NULL);
    RoboCheck(DmHas(id, "div"));
    DmCheckTreeClean(id);

    RoboCheckEqInt(HtmlDocumentReset(0xDEADu, NULL), RoboNotFound);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmResetRefusedWhileBuilding(void)
{
    HtmlDocumentId id;
    HtmlTreeBuilder *builder = NULL;
    HtmlDiag diag;

    DmFixtureBegin();
    id = DmDoc();

    /* A live builder holds the document lease and its own tree state. */
    RoboCheckEqInt(HtmlTreeBuilderCreate(id, &builder, NULL),
                      RoboOk);
    RoboCheck(builder != NULL);

    HtmlDiagReset(&diag);
    RoboCheckEqInt(HtmlDocumentReset(id, &diag), RoboBusy);
    RoboCheckEqInt(diag.domain, RoboHtmlDomainDocument);

    HtmlTreeBuilderDestroy(builder);
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 7. Failure and teardown (items 19-22, 30)                           */
/* ------------------------------------------------------------------ */

static void TestDmFailedState(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle node;
    HtmlDocumentMetadata meta;
    HtmlDiag diag;

    DmFixtureBegin();
    id = DmDoc();

    RoboCheckEqInt(HtmlDocumentMarkFailed(
                          id, RoboHtmlDomainSource, "source unavailable",
                          NULL),
                      RoboOk);
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.lifecycle, RoboHtmlDocumentLifecycleFailed);
    RoboCheckEqInt(meta.failDomain, RoboHtmlDomainSource);
    RoboCheckStrEq(meta.failMessage, "source unavailable");

    /* The failure record is sticky: a later failure does not overwrite it. */
    RoboCheckEqInt(HtmlDocumentMarkFailed(
                          id, RoboHtmlDomainEncoding, "second", NULL),
                      RoboOk);
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.failDomain, RoboHtmlDomainSource);
    RoboCheckStrEq(meta.failMessage, "source unavailable");

    /* A failed document accepts no construction, no finalization, no reset. */
    RoboCheck(!HtmlDocumentAllowsConstruction(id));
    RoboCheckEqInt(HtmlDocumentFinalize(id, NULL), RoboError);
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboError);
    HtmlDiagReset(&diag);
    RoboCheckEqInt(HtmlDocumentRegisterNode(
                          id, RoboHtmlNodeKindElement,
                          RoboHtmlNodeOriginTest, &node, &diag),
                      RoboError);
    RoboCheckEqInt(diag.domain, RoboHtmlDomainNode);

    /* The root is still readable, and teardown still works. */
    {
        HtmlNodeHandle root = HtmlDocumentRoot(id);

        RoboCheck(HtmlNodeHandleIsWellformed(&root));
    }
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheck(!HtmlDocumentIsLive(id));
    DmFixtureEnd();
}

static void TestDmTeardown(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlTreeReport rep;
    HtmlDocumentMetadata meta;

    DmFixtureBegin();
    id = DmDoc();
    DmBuild(id, DM_COMPACT_DOC, NULL);
    root = HtmlDocumentRoot(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlLiveDocumentCount(), 0u);

    /* Everything that named the document now reports it as gone. */
    RoboCheck(!HtmlDocumentIsLive(id));
    RoboCheck(HtmlNodeHandleIsStale(&root));
    RoboCheck(!HtmlDocumentOwnsNode(id, &root));
    RoboCheckEqInt(HtmlDocumentNodeCount(id), 0u);
    RoboCheck(!HtmlDocumentIsEmpty(id));
    RoboCheck(!HtmlDocumentIsFinalized(id));
    RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &meta.lifecycle),
                      RoboNotFound);
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &rep, NULL),
                      RoboNotFound);

    /* Repeated teardown is reported, never undefined. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboNotFound);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboNotFound);

    DmFixtureEnd();
}

static void TestDmLargeTreeCleanup(void)
{
    HtmlDocumentId id;
    HtmlDocumentMetadata meta;
    HtmlTreeReport rep;
    char html[32768];
    size_t used = 0u;
    int i;

    DmFixtureBegin();
    id = DmDoc();
    for (i = 0; i < 1500 && used + 6u < sizeof(html); i++) {
        html[used++] = '<';
        html[used++] = 'd';
        html[used++] = 'i';
        html[used++] = 'v';
        html[used++] = '>';
    }
    html[used++] = 'x';
    html[used] = '\0';
    DmBuild(id, html, NULL);

    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.elementCount, 1503u); /* html head body + 1500 */
    RoboCheckEqInt(meta.nodeCount, HtmlDocumentNodeCount(id));
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &rep, NULL),
                      RoboOk);
    RoboCheckEqInt(rep.ok, 1);
    RoboCheckEqInt(rep.registered, 1505u);

    /* A deep tree tears down in one step and leaves nothing behind. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlLiveDocumentCount(), 0u);

    DmFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* 8. Pipeline integration (items 25, 26, 29)                          */
/* ------------------------------------------------------------------ */

static void TestDmPipelineIntegration(void)
{
    HtmlDocumentId id;
    HtmlTreeBuilder *builder = NULL;
    HtmlSource src;
    HtmlNodeHandle root;

    DmFixtureBegin();
    id = DmDoc();

    /*
     * Drive the builder step by step, as a caller with an incremental
     * source would: the document must be consistent at each stage, and
     * construction must stay open until the build finishes.
     */
    HtmlSourceInit(&src);
    RoboCheckEqInt(
        HtmlSourceString(&src, DM_COMPACT_DOC, NULL), RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderCreate(id, &builder, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderBegin(builder, &src, NULL),
                      RoboOk);
    RoboCheck(HtmlDocumentAllowsConstruction(id));
    RoboCheck(!HtmlDocumentIsFinalized(id));

    {
        int guard = 0;

        while (guard++ < 200) {
            RoboCheckEqInt(HtmlTreeBuilderNext(builder, NULL),
                              RoboOk);
            if (HtmlTreeBuilderDiagnosticCount(builder) > 0u) {
                break;
            }
            {
                HtmlParserState state = RoboHtmlParserStateIdle;

                RoboCheckEqInt(
                    HtmlTreeBuilderGetState(builder, &state), RoboOk);
                if (state == RoboHtmlParserStateComplete) {
                    break;
                }
            }
        }
    }

    /* The builder consumed EOF, which finishes the build. */
    RoboCheckEqInt(HtmlTreeBuilderFinish(builder, NULL), RoboOk);
    RoboCheck(HtmlDocumentIsFinalized(id));

    root = HtmlDocumentRoot(id);
    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    RoboCheck(HtmlDocumentOwnsNode(id, &root));

    /* Destroying the builder leaves the document's tree intact and owned. */
    HtmlTreeBuilderDestroy(builder);
    DmCheckTreeClean(id);
    RoboCheck(DmHas(id, "title"));
    RoboCheck(DmHas(id, "p"));

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmDemoDocument(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle body;
    HtmlNodeHandle child;
    char buf[128];

    DmFixtureBegin();
    id = DmDoc();

    /* The visual checkpoint's document, through the document model. */
    DmBuild(id,
             "<!DOCTYPE html>\n"
             "<html>\n"
             "<head>\n"
             "<title>RoboticelaOS Runtime</title>\n"
             "</head>\n"
             "<body>\n"
             "<h1>RoboticelaOS Runtime</h1>\n"
             "<p>Independent HTML rendering is working.</p>\n"
             "<p>RoboGraphics + RoboWindow + RoboHTML</p>\n"
             "<button>Demo Button</button>\n"
             "</body>\n"
             "</html>\n",
             NULL);

    DmCheckTreeClean(id);
    RoboCheck(HtmlDocumentIsFinalized(id));

    body = DmFind(id, "body");
    RoboCheck(HtmlNodeHandleIsWellformed(&body));
    {
        size_t count = 0u;

        RoboCheckEqInt(HtmlNodeChildCount(&body, &count, NULL),
                          RoboOk);
        RoboCheck(count > 0u);
        /* The demo renderer walks the real tree: the first child resolves. */
        RoboCheckEqInt(HtmlNodeChildAt(&body, 0u, &child, NULL),
                          RoboOk);
        RoboCheck(HtmlDocumentOwnsNode(id, &child));
    }

    RoboCheckEqInt(
        HtmlNodeFindChildByName(&body, "h1", &child, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeSubtreeText(&child, buf, sizeof(buf),
                                                  NULL, NULL),
                      RoboOk);
    RoboCheckStrEq(buf, "RoboticelaOS Runtime");

    RoboCheckEqInt(
        HtmlNodeFindChildByName(&body, "button", &child, NULL),
        RoboOk);
    RoboCheckEqInt(HtmlNodeSubtreeText(&child, buf, sizeof(buf),
                                                  NULL, NULL),
                      RoboOk);
    RoboCheckStrEq(buf, "Demo Button");

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    DmFixtureEnd();
}

static void TestDmUnicodeAndMalformed(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlDocumentMetadata meta;
    char buf[128];
    size_t length = 0u;

    DmFixtureBegin();

    /* Unicode content survives anchoring into a document-owned tree. */
    id = DmDoc();
    DmBuild(id,
             "<p>R\xc3\xa9sum\xc3\xa9 \xe2\x80\x94 "
             "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x98\x80</p>",
             NULL);
    DmCheckTreeClean(id);
    root = HtmlDocumentRoot(id);
    RoboCheckEqInt(HtmlNodeSubtreeText(&root, buf, sizeof(buf),
                                                  &length, NULL),
                      RoboOk);
    RoboCheck(HtmlDocumentOwnsNode(id, &root));
    RoboCheck(strstr(buf, "R\xc3\xa9sum\xc3\xa9") != NULL);
    RoboCheck(strstr(buf, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e") != NULL);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);

    /*
     * Malformed markup is recovered, and the result is still a document the
     * model can vouch for: reachable, owned, and consistent.
     */
    id = DmDoc();
    DmBuild(id, "<div><p>unclosed<span></div></b>text<<<", NULL);
    DmCheckTreeClean(id);
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    /* A one-shot build consumes a complete source, so the document is done. */
    RoboCheckEqInt(meta.lifecycle, RoboHtmlDocumentLifecycleComplete);
    RoboCheck(meta.nodeCount > 1u);
    RoboCheck(DmHas(id, "div"));
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);

    /*
     * "Empty" has two distinct meanings and the model keeps them apart.
     * Attaching an empty source leaves the document empty; *parsing* empty
     * input produces the implied html/head/body structure, as a browser
     * does. Either way the result is a consistent, owned tree.
     */
    id = DmDoc();
    RoboCheck(HtmlDocumentIsEmpty(id));
    {
        HtmlSource empty;

        HtmlSourceInit(&empty);
        RoboCheckEqInt(HtmlSourceString(&empty, "", NULL), RoboOk);
        RoboCheckEqInt(HtmlDocumentAttachSource(id, &empty, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlDocumentGetLifecycle(
                              id, &meta.lifecycle),
                          RoboOk);
        RoboCheckEqInt(meta.lifecycle,
                          RoboHtmlDocumentLifecycleComplete);
        RoboCheck(HtmlDocumentIsEmpty(id));
    }

    DmBuild(id, "", NULL);
    DmCheckTreeClean(id);
    RoboCheckEqInt(GetHtmlDocumentMetadata(id, &meta, NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(meta.nodeCount, 4u); /* document + html head body */
    RoboCheckEqInt(meta.elementCount, 3u);
    RoboCheckEqInt(meta.textCount, 0u);
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);

    DmFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlDocumentModel(void)
{
    TestRegister("html.docmodel.metadata_fresh", TestDmMetadataFresh);
    TestRegister("html.docmodel.metadata_built", TestDmMetadataBuilt);
    TestRegister("html.docmodel.metadata_misuse", TestDmMetadataMisuse);
    TestRegister("html.docmodel.root", TestDmRoot);
    TestRegister("html.docmodel.root_stable",
                       TestDmRootSurvivesRebuild);
    TestRegister("html.docmodel.node_identity",
                       TestDmNodeIdentityAndLookup);
    TestRegister("html.docmodel.stale_handles", TestDmStaleHandles);
    TestRegister("html.docmodel.cross_document", TestDmCrossDocument);
    TestRegister("html.docmodel.invariants_clean",
                       TestDmInvariantsClean);
    TestRegister("html.docmodel.invariants_orphan",
                       TestDmInvariantsOrphanDetected);
    TestRegister("html.docmodel.invariants_deep",
                       TestDmInvariantsDeepChain);
    TestRegister("html.docmodel.source", TestDmSourceAssociation);
    TestRegister("html.docmodel.source_ownership",
                       TestDmSourceBytesAreNotBorrowedByTheTree);
    TestRegister("html.docmodel.encoding", TestDmEncodingAssociation);
    TestRegister("html.docmodel.finalize", TestDmFinalize);
    TestRegister("html.docmodel.reset", TestDmReset);
    TestRegister("html.docmodel.reset_busy",
                       TestDmResetRefusedWhileBuilding);
    TestRegister("html.docmodel.failed", TestDmFailedState);
    TestRegister("html.docmodel.teardown", TestDmTeardown);
    TestRegister("html.docmodel.large_cleanup",
                       TestDmLargeTreeCleanup);
    TestRegister("html.docmodel.pipeline", TestDmPipelineIntegration);
    TestRegister("html.docmodel.demo_document", TestDmDemoDocument);
    TestRegister("html.docmodel.unicode_malformed",
                       TestDmUnicodeAndMalformed);
}
