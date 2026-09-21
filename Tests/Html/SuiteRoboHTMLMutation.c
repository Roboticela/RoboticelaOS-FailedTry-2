/*
 * RoboHTML Phase 8.9 DOM mutation suite.
 *
 * What it covers, in the order the task spec lists it: the four structural
 * operations and their positional cases, bulk child removal, parent/child/
 * sibling/first/last integrity after every one of them, ordering and child
 * count, duplicate ownership, self-insertion and ancestor cycles,
 * cross-document refusal, detached-node behaviour, mutation across the
 * construction/finalization boundary, parser-created trees, Unicode and
 * attribute preservation, the mutation hook, the capability flag, the
 * traversal-invalidation rule, and teardown.
 *
 * The fixture builds trees through the element and text models under a fresh
 * document rather than through the parser, because a mutation test wants a
 * shape it stated itself — except for the two tests that deliberately go
 * through the real parser to prove a finalized tree is mutable.
 *
 * Every structural assertion is doubled: the specific one (which child is
 * first, how many children) and a whole-tree audit
 * (HtmlDocumentValidateTree). The specific check says what the
 * operation was supposed to do; the audit is what catches a repair that
 * fixed the link the test looked at and left another one wrong — a stale
 * lastChild, a childCount that drifted, a prevSibling that disagrees with
 * the traversal order, or a node that fell out of the tree entirely.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLMutation.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTraversal.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void MuFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void MuFixtureEnd(void)
{
    HtmlShutdown();
}

static HtmlDocumentId MuDoc(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    /*
     * Brings the module up on demand: the fixture is built at the top of a
     * test (declaration order), so the document the test measures is created
     * before its body's MuFixtureBegin() line would have run. Every other
     * suite puts the call first for the same reason; doing it here means a
     * test cannot get the order wrong.
     */
    MuFixtureBegin();
    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

static HtmlNodeHandle MuRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

/** Create an element named @p name under @p parent (attached). */
static HtmlNodeHandle MuElement(HtmlDocumentId id,
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
    RoboCheck(HtmlNodeHandleIsWellformed(&out));
    return out;
}

/** Create a text node with @p content under @p parent (attached). */
static HtmlNodeHandle MuText(HtmlDocumentId id,
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
    RoboCheck(HtmlNodeHandleIsWellformed(&out));
    return out;
}

/**
 * The whole-tree audit. Every mutating test ends with this, because the
 * audit reports *what* disagrees rather than only that something does.
 */
static void MuExpectClean(HtmlDocumentId id)
{
    HtmlTreeReport report;

    RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                      RoboOk);
    RoboCheckMsg(report.ok, "tree invariants broken after mutation");
    RoboCheckEqInt((int)report.orphans, 0);
    RoboCheckEqInt((int)report.badLinks, 0);
    RoboCheckEqInt((int)report.cycles, 0);
    RoboCheckEqInt((int)report.badParents, 0);
    RoboCheckEqInt((int)report.badCounts, 0);
    RoboCheckEqInt((int)report.siblingMismatches, 0);
}

/** Parse @p text into @p id with the real tree builder (finalizes it). */
static void MuBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

/** Child count of @p node, or SIZE_MAX when it does not resolve. */
static size_t MuChildCount(const HtmlNodeHandle *node)
{
    size_t count = SIZE_MAX;

    if (HtmlNodeChildCount(node, &count, NULL) != RoboOk) {
        return SIZE_MAX;
    }
    return count;
}

static uint32_t MuVersion(HtmlDocumentId id)
{
    uint32_t version = 0u;

    RoboCheckEqInt(HtmlNodeTreeVersion(id, &version, NULL), RoboOk);
    RoboCheck(version != 0u);
    return version;
}

/* ------------------------------------------------------------------ */
/* Append                                                              */
/* ------------------------------------------------------------------ */

static void TestMutationAppendFirst(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle body = MuElement(id, &root, "body");
    HtmlNodeHandle p = MuElement(id, &body, "p");
    HtmlNodeHandle first;
    HtmlNodeHandle last;

    MuFixtureBegin();

    RoboCheckEqInt(MuChildCount(&body), 1);
    RoboCheckEqInt(HtmlNodeFirstChild(&body, &first, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeLastChild(&body, &last, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&first, &p));
    RoboCheck(HtmlNodeHandleEqual(&last, &p));
    /* The only child has neither sibling. */
    {
        HtmlNodeHandle sib;

        RoboCheckEqInt(HtmlNodePrevSibling(&p, &sib, NULL),
                          RoboNotFound);
        RoboCheckEqInt(HtmlNodeNextSibling(&p, &sib, NULL),
                          RoboNotFound);
    }
    /* The parent relationship points back. */
    {
        HtmlNodeHandle back;

        RoboCheckEqInt(HtmlNodeParent(&p, &back, NULL), RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&back, &body));
    }
    MuExpectClean(id);
    MuFixtureEnd();
}

static void TestMutationAppendMultipleOrder(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlNodeHandle c = MuElement(id, &root, "c");
    HtmlNodeHandle first;
    HtmlNodeHandle last;
    HtmlNodeHandle mid;
    HtmlNodeHandle back;

    MuFixtureBegin();

    RoboCheckEqInt(MuChildCount(&root), 3);
    RoboCheckEqInt(HtmlNodeFirstChild(&root, &first, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeLastChild(&root, &last, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&first, &a));
    RoboCheck(HtmlNodeHandleEqual(&last, &c));

    /* a -> b -> c, and the inverse links. */
    RoboCheckEqInt(HtmlNodeNextSibling(&a, &mid, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&mid, &b));
    RoboCheckEqInt(HtmlNodeNextSibling(&b, &mid, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&mid, &c));
    RoboCheckEqInt(HtmlNodePrevSibling(&b, &mid, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&mid, &a));
    RoboCheckEqInt(HtmlNodePrevSibling(&c, &mid, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&mid, &b));
    RoboCheckEqInt(HtmlNodeParent(&c, &back, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&back, &root));

    MuExpectClean(id);
    MuFixtureEnd();
}

static void TestMutationAppendText(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle p = MuElement(id, &root, "p");
    HtmlNodeHandle text = MuText(id, &p, "Hello");
    const char *content;
    size_t length = 0u;

    MuFixtureBegin();

    RoboCheckEqInt(MuChildCount(&p), 1);
    RoboCheck(HtmlTextIsText(&text));
    content = HtmlTextContent(&text, &length);
    RoboCheck(content != NULL);
    RoboCheckEqInt((int)length, 5);
    RoboCheck(strcmp(content, "Hello") == 0);
    MuExpectClean(id);
    MuFixtureEnd();
}

static void TestMutationAppendRejectsAttached(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlDiag diag;
    uint32_t before;

    MuFixtureBegin();

    before = MuVersion(id);
    HtmlDiagReset(&diag);

    /* b already belongs to root: a silent move would be the bug. */
    RoboCheckEqInt(HtmlNodeAppendChild(&a, &b, &diag),
                      RoboAlreadyExists);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainMutation);
    /* Nothing moved, including the version — the refusal is provably inert. */
    {
        HtmlNodeHandle back;

        RoboCheckEqInt(HtmlNodeParent(&b, &back, NULL), RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&back, &root));
        RoboCheckEqInt(MuChildCount(&root), 2);
        RoboCheckEqInt(MuChildCount(&a), 0);
    }
    RoboCheckEqInt((int)MuVersion(id), (int)before);
    MuExpectClean(id);
    MuFixtureEnd();
}

static void TestMutationAppendRejectsSelf(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlDiag diag;
    uint32_t before;

    MuFixtureBegin();

    before = MuVersion(id);
    HtmlDiagReset(&diag);
    RoboCheckEqInt(HtmlNodeAppendChild(&a, &a, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)MuVersion(id), (int)before);
    MuExpectClean(id);
    MuFixtureEnd();
}

static void TestMutationAppendRejectsDocumentNode(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle body = MuElement(id, &root, "body");
    HtmlDiag diag;
    uint32_t before;

    MuFixtureBegin();

    before = MuVersion(id);
    HtmlDiagReset(&diag);
    /* A document has exactly one root and nothing here may add another. */
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &root, &diag),
                      RoboNotSupported);
    RoboCheckEqInt((int)MuVersion(id), (int)before);
    MuExpectClean(id);
    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Cycles                                                             */
/* ------------------------------------------------------------------ */

static void TestMutationCycleAncestorRefused(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &a, "b");
    HtmlNodeHandle c = MuElement(id, &b, "c");
    HtmlDiag diag;
    uint32_t before;
    int contains = -1;

    MuFixtureBegin();

    /*
     * The ancestor must be detached for the cycle rule to be the deciding
     * one: an attached node is refused earlier for having a parent, which
     * would make this test pass for the wrong reason. Detaching a takes its
     * whole subtree with it, so a > b > c still holds.
     */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &a, NULL), RoboOk);
    RoboCheck(MuChildCount(&root) == 0);

    before = MuVersion(id);
    HtmlDiagReset(&diag);

    /* Putting a under c would make a reachable from itself. */
    RoboCheckEqInt(HtmlNodeAppendChild(&c, &a, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainMutation);
    RoboCheckEqInt((int)MuVersion(id), (int)before);
    /* Nothing moved: a still holds b, and b still holds c. */
    RoboCheckEqInt(MuChildCount(&a), 1);
    RoboCheckEqInt(MuChildCount(&c), 0);

    /* The predicate agrees, and it is the same rule the refusal applied. */
    RoboCheckEqInt(HtmlNodeContains(&a, &c, &contains, NULL),
                      RoboOk);
    RoboCheckEqInt(contains, 1);
    RoboCheckEqInt(HtmlNodeContains(&c, &a, &contains, NULL),
                      RoboOk);
    RoboCheckEqInt(contains, 0);
    /* contains is reflexively true, like DOM's Node.contains. */
    RoboCheckEqInt(HtmlNodeContains(&a, &a, &contains, NULL),
                      RoboOk);
    RoboCheckEqInt(contains, 1);

    /* A node detached from that subtree is no longer inside a. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&b, &c, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeContains(&a, &c, &contains, NULL),
                      RoboOk);
    RoboCheckEqInt(contains, 0);
    /* ...and the operation that was a cycle is now simply legal. */
    RoboCheckEqInt(HtmlNodeAppendChild(&c, &a, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeContains(&a, &c, &contains, NULL),
                      RoboOk);
    RoboCheckEqInt(contains, 0);
    RoboCheckEqInt(HtmlNodeContains(&c, &a, &contains, NULL),
                      RoboOk);
    RoboCheckEqInt(contains, 1);
    MuExpectClean(id);

    /* Put the tree back the way the fixture started. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&c, &a, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&b, &c, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &a, NULL), RoboOk);
    MuExpectClean(id);
    MuFixtureEnd();
}

static void TestMutationCycleInsertBeforeRefused(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &a, "b");
    HtmlNodeHandle c = MuElement(id, &b, "c");
    HtmlDiag diag;
    uint32_t before;

    MuFixtureBegin();

    /* Detach a so the cycle rule, not the one-parent rule, decides. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &a, NULL), RoboOk);
    before = MuVersion(id);
    HtmlDiagReset(&diag);
    /*
     * Inserting a before c would place a inside its own descendant b: c is a
     * genuine child of b, so the anchor itself is valid and the refusal can
     * only come from the cycle rule.
     */
    RoboCheckEqInt(HtmlNodeInsertBefore(&b, &a, &c, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainMutation);
    RoboCheckEqInt((int)MuVersion(id), (int)before);
    RoboCheckEqInt(MuChildCount(&b), 1);
    MuExpectClean(id);

    /* Restore, then check the substitution path of the same rule. */
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &a, NULL), RoboOk);
    MuExpectClean(id);
    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Insert before                                                       */
/* ------------------------------------------------------------------ */

static void TestMutationInsertBeforePositions(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlNodeHandle c = MuElement(id, &root, "c");
    HtmlNodeHandle first;
    HtmlNodeHandle last;
    HtmlNodeHandle got;

    MuFixtureBegin();

    /* Detach c and put it before a: c must become the new first child. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &c, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeInsertBefore(&root, &c, &a, NULL),
                      RoboOk);
    RoboCheckEqInt(MuChildCount(&root), 3);
    RoboCheckEqInt(HtmlNodeFirstChild(&root, &first, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&first, &c));
    RoboCheckEqInt(HtmlNodeLastChild(&root, &last, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&last, &b));
    RoboCheckEqInt(HtmlNodeNextSibling(&c, &got, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &a));
    RoboCheckEqInt(HtmlNodePrevSibling(&a, &got, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &c));
    MuExpectClean(id);

    /* Middle: insert c before b, i.e. between a and b. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &c, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeInsertBefore(&root, &c, &b, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlNodeFirstChild(&root, &first, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&first, &a));
    RoboCheckEqInt(HtmlNodeLastChild(&root, &last, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&last, &b));
    RoboCheckEqInt(MuChildCount(&root), 3);
    MuExpectClean(id);

    /* Before the last child leaves the last child alone. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &c, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeInsertBefore(&root, &c, &b, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlNodeLastChild(&root, &last, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&last, &b));
    RoboCheckEqInt(MuChildCount(&root), 3);
    MuExpectClean(id);

    MuFixtureEnd();
}

static void TestMutationInsertBeforeRejectsForeignReference(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlNodeHandle other = MuElement(id, &root, "other");
    HtmlNodeHandle stray = MuElement(id, &a, "stray");
    HtmlDiag diag;
    uint32_t before;

    MuFixtureBegin();

    before = MuVersion(id);
    HtmlDiagReset(&diag);
    /* stray is a child of a, not of b: "insert before" must not guess. */
    RoboCheckEqInt(HtmlNodeInsertBefore(&b, &other, &stray, &diag),
                      RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainMutation);
    /* A stale handle is a miss, not a foreign reference. */
    {
        HtmlNodeHandle dead = stray;

        dead.nodeId = (HtmlNodeId)9999u;
        RoboCheckEqInt(HtmlNodeInsertBefore(&b, &other, &dead, NULL),
                          RoboNotFound);
    }
    RoboCheckEqInt((int)MuVersion(id), (int)before);
    MuExpectClean(id);
    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Remove                                                             */
/* ------------------------------------------------------------------ */

static void TestMutationRemovePositions(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlNodeHandle c = MuElement(id, &root, "c");
    HtmlNodeHandle first;
    HtmlNodeHandle last;

    MuFixtureBegin();

    /* Middle: a and c close the gap. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &b, NULL), RoboOk);
    RoboCheckEqInt(MuChildCount(&root), 2);
    RoboCheckEqInt(HtmlNodeNextSibling(&a, &first, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&first, &c));
    RoboCheckEqInt(HtmlNodePrevSibling(&c, &last, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&last, &a));
    RoboCheckEqInt(HtmlNodePrevSibling(&b, &first, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlNodeNextSibling(&b, &first, NULL),
                      RoboNotFound);
    MuExpectClean(id);

    /* First: the new first child has no previous sibling. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &a, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeFirstChild(&root, &first, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&first, &c));
    RoboCheckEqInt(HtmlNodeLastChild(&root, &last, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&last, &c));
    MuExpectClean(id);

    /* Only child: the parent is left valid and childless. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &c, NULL), RoboOk);
    RoboCheckEqInt(MuChildCount(&root), 0);
    RoboCheckEqInt(HtmlNodeFirstChild(&root, &first, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlNodeLastChild(&root, &last, NULL),
                      RoboNotFound);
    MuExpectClean(id);

    MuFixtureEnd();
}

static void TestMutationRemoveRejectsWrongParent(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlNodeHandle child = MuElement(id, &a, "child");
    HtmlDiag diag;
    uint32_t before;

    MuFixtureBegin();

    before = MuVersion(id);
    HtmlDiagReset(&diag);
    /* child belongs to a, so b is not its parent — nothing may be removed. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&b, &child, &diag),
                      RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainMutation);
    RoboCheckEqInt(MuChildCount(&a), 1);
    RoboCheckEqInt(MuChildCount(&b), 0);
    RoboCheckEqInt((int)MuVersion(id), (int)before);
    MuExpectClean(id);
    MuFixtureEnd();
}

static void TestMutationRemoveKeepsSubtree(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle holder = MuElement(id, &root, "holder");
    HtmlNodeHandle box = MuElement(id, &holder, "div");
    HtmlNodeHandle inner = MuElement(id, &box, "span");
    HtmlNodeHandle text = MuText(id, &inner, "kept");
    const char *content;
    size_t length = 0u;
    int contains = 0;

    MuFixtureBegin();

    RoboCheckEqInt(HtmlNodeRemoveChild(&holder, &box, NULL),
                      RoboOk);

    /* Everything inside the detached node is intact and still linked. */
    RoboCheckEqInt(MuChildCount(&box), 1);
    RoboCheckEqInt(MuChildCount(&inner), 1);
    content = HtmlTextContent(&text, &length);
    RoboCheck(content != NULL);
    RoboCheckEqInt((int)length, 4);
    RoboCheck(strcmp(content, "kept") == 0);

    /* The detached subtree is traversable in its own right... */
    RoboCheckEqInt(HtmlNodeContains(&box, &text, &contains, NULL),
                      RoboOk);
    RoboCheckEqInt(contains, 1);
    /* ...and is no longer inside the parent it came from. */
    RoboCheckEqInt(HtmlNodeContains(&holder, &box, &contains, NULL),
                      RoboOk);
    RoboCheckEqInt(contains, 0);
    /* Its depth restarts: a detached root sits at depth 0, like the document. */
    {
        size_t depth = 999u;

        RoboCheckEqInt(HtmlNodeDepth(&box, &depth, NULL), RoboOk);
        RoboCheckEqInt((int)depth, 0);
        RoboCheckEqInt(HtmlNodeDepth(&text, &depth, NULL), RoboOk);
        RoboCheckEqInt((int)depth, 2);
    }

    /* The document still owns it: a detached node is not destroyed. */
    RoboCheck(HtmlDocumentOwnsNode(id, &box));

    /* ...and it can be inserted somewhere else. */
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &box, NULL), RoboOk);
    RoboCheckEqInt(MuChildCount(&root), 2);
    MuExpectClean(id);

    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Replace                                                             */
/* ------------------------------------------------------------------ */

static void TestMutationReplacePositions(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlNodeHandle c = MuElement(id, &root, "c");
    HtmlNodeHandle x = MuElement(id, &root, "x");
    HtmlNodeHandle y = MuElement(id, &root, "y");
    HtmlNodeHandle first;
    HtmlNodeHandle last;
    HtmlNodeHandle got;

    MuFixtureBegin();

    /*
     * x and y start attached under root so they exist; they are pulled out
     * before each replacement, because a replacement never steals a node
     * that already has a parent.
     */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &x, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &y, NULL), RoboOk);
    RoboCheckEqInt(MuChildCount(&root), 3);

    /* First position. */
    RoboCheckEqInt(HtmlNodeReplaceChild(&root, &x, &a, NULL),
                      RoboOk);
    RoboCheckEqInt(MuChildCount(&root), 3);
    RoboCheckEqInt(HtmlNodeFirstChild(&root, &first, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&first, &x));
    RoboCheckEqInt(HtmlNodeLastChild(&root, &last, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&last, &c));
    /* The old child is detached, not destroyed. */
    RoboCheckEqInt(HtmlNodeNextSibling(&a, &got, NULL),
                      RoboNotFound);
    RoboCheck(HtmlDocumentOwnsNode(id, &a));
    MuExpectClean(id);

    /* Middle position: y takes b's slot, between x and c. */
    RoboCheckEqInt(HtmlNodeReplaceChild(&root, &y, &b, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlNodeFirstChild(&root, &first, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&first, &x));
    RoboCheckEqInt(HtmlNodeNextSibling(&x, &got, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &y));
    RoboCheckEqInt(HtmlNodeNextSibling(&y, &got, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &c));
    RoboCheckEqInt(HtmlNodePrevSibling(&y, &got, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &x));
    MuExpectClean(id);

    /*
     * Last position. a was detached by the first replacement, so it is the
     * free node here: root is [x, y, c] and becomes [x, y, a].
     */
    RoboCheckEqInt(HtmlNodeReplaceChild(&root, &a, &c, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlNodeLastChild(&root, &last, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&last, &a));
    RoboCheckEqInt(HtmlNodeFirstChild(&root, &first, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&first, &x));
    RoboCheckEqInt(MuChildCount(&root), 3);
    MuExpectClean(id);

    /* Only child: replacing it leaves a one-child parent. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &y, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &x, NULL), RoboOk);
    RoboCheckEqInt(MuChildCount(&root), 1);
    RoboCheckEqInt(HtmlNodeReplaceChild(&root, &b, &a, NULL),
                      RoboOk);
    RoboCheckEqInt(MuChildCount(&root), 1);
    RoboCheckEqInt(HtmlNodeFirstChild(&root, &first, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&first, &b));
    RoboCheckEqInt(HtmlNodeLastChild(&root, &last, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&last, &b));
    MuExpectClean(id);

    MuFixtureEnd();
}

static void TestMutationReplaceRejectsAttached(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlNodeHandle c = MuElement(id, &root, "c");
    HtmlDiag diag;
    uint32_t before;

    MuFixtureBegin();

    before = MuVersion(id);
    HtmlDiagReset(&diag);
    /* b is attached, so a must not lose its slot to a refusal. */
    RoboCheckEqInt(HtmlNodeReplaceChild(&root, &b, &a, &diag),
                      RoboAlreadyExists);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainMutation);
    RoboCheckEqInt((int)MuVersion(id), (int)before);
    RoboCheckEqInt(MuChildCount(&root), 3);
    {
        HtmlNodeHandle first;
        HtmlNodeHandle back;

        RoboCheckEqInt(HtmlNodeFirstChild(&root, &first, NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&first, &a));
        RoboCheckEqInt(HtmlNodeParent(&a, &back, NULL), RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&back, &root));
    }
    /* Replacing a node with itself is refused too. */
    RoboCheckEqInt(HtmlNodeReplaceChild(&root, &a, &a, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)MuVersion(id), (int)before);

    (void)c;
    MuExpectClean(id);
    MuFixtureEnd();
}

static void TestMutationReplaceRejectsCycle(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle outer = MuElement(id, &root, "outer");
    HtmlNodeHandle slot = MuElement(id, &outer, "slot");
    HtmlNodeHandle deep = MuElement(id, &slot, "deep");
    uint32_t before;

    MuFixtureBegin();

    /* Detached, so the cycle rule decides rather than the one-parent rule. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &outer, NULL),
                      RoboOk);
    before = MuVersion(id);
    /* outer cannot take deep's slot: outer is deep's ancestor. */
    RoboCheckEqInt(HtmlNodeReplaceChild(&slot, &outer, &deep, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)MuVersion(id), (int)before);
    RoboCheckEqInt(MuChildCount(&slot), 1);
    {
        HtmlNodeHandle first;

        RoboCheckEqInt(HtmlNodeFirstChild(&slot, &first, NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&first, &deep));
    }
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &outer, NULL),
                      RoboOk);
    MuExpectClean(id);
    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Bulk clear                                                          */
/* ------------------------------------------------------------------ */

static void TestMutationRemoveChildren(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle parent = MuElement(id, &root, "div");
    HtmlNodeHandle a = MuElement(id, &parent, "a");
    HtmlNodeHandle b = MuElement(id, &parent, "b");
    HtmlNodeHandle c = MuText(id, &parent, "text");
    size_t removed = 99u;
    size_t count = 99u;

    MuFixtureBegin();

    RoboCheckEqInt(HtmlNodeRemoveChildren(&parent, &removed, NULL),
                      RoboOk);
    RoboCheckEqInt((int)removed, 3);
    RoboCheckEqInt(MuChildCount(&parent), 0);
    /* Detached, not destroyed: all three are still live nodes of the doc. */
    RoboCheck(HtmlDocumentOwnsNode(id, &a));
    RoboCheck(HtmlDocumentOwnsNode(id, &b));
    RoboCheck(HtmlDocumentOwnsNode(id, &c));
    /* A second clear is a successful no-op. */
    RoboCheckEqInt(HtmlNodeRemoveChildren(&parent, &removed, NULL),
                      RoboOk);
    RoboCheckEqInt((int)removed, 0);
    RoboCheckEqInt(HtmlNodeChildCount(&parent, &count, NULL),
                      RoboOk);
    RoboCheckEqInt((int)count, 0);
    MuExpectClean(id);

    /* Re-insertion works, so the clear did not strand anything. */
    RoboCheckEqInt(HtmlNodeAppendChild(&parent, &c, NULL), RoboOk);
    RoboCheckEqInt(MuChildCount(&parent), 1);
    MuExpectClean(id);

    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Cross-document                                                      */
/* ------------------------------------------------------------------ */

static void TestMutationCrossDocumentRefused(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlDocumentId other = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle otherRoot = MuRoot(other);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle foreign = MuElement(other, &otherRoot, "foreign");
    HtmlDiag diag;
    uint32_t before;

    MuFixtureBegin();

    before = MuVersion(id);
    HtmlDiagReset(&diag);

    RoboCheckEqInt(HtmlNodeAppendChild(&a, &foreign, &diag),
                      RoboNotSupported);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainMutation);
    RoboCheckEqInt(HtmlNodeInsertBefore(&a, &foreign, &foreign,
                                                   NULL),
                      RoboNotSupported);
    RoboCheckEqInt(HtmlNodeRemoveChild(&a, &foreign, NULL),
                      RoboNotSupported);
    /* contains across documents is the same refusal, not a false answer. */
    {
        int contains = -1;

        RoboCheckEqInt(
            HtmlNodeContains(&a, &foreign, &contains, &diag),
            RoboNotSupported);
        RoboCheckEqInt(contains, 0);
    }
    /* Neither tree moved, and the foreign document is untouched too. */
    RoboCheckEqInt((int)MuVersion(id), (int)before);
    RoboCheckEqInt(MuChildCount(&a), 0);
    RoboCheckEqInt(MuChildCount(&otherRoot), 1);
    MuExpectClean(id);

    (void)HtmlDocumentDestroy(other, NULL);
    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Boundary + queries                                                  */
/* ------------------------------------------------------------------ */

static void TestMutationNullAndUnknownHandles(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle zero;
    uint32_t version = 0u;

    MuFixtureBegin();

    HtmlNodeHandleClear(&zero);
    RoboCheckEqInt(HtmlNodeAppendChild(NULL, &a, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeAppendChild(&root, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &zero, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeRemoveChild(&zero, &a, NULL),
                      RoboInvalidArgument);
    /* A handle naming no document is a miss, not a cross-document refusal. */
    {
        HtmlNodeHandle noDoc = a;

        noDoc.documentId = RoboHtmlDocumentIdNone;
        RoboCheckEqInt(HtmlNodeAppendChild(&root, &noDoc, NULL),
                          RoboInvalidArgument);
    }
    RoboCheckEqInt(MuChildCount(&root), 1);

    RoboCheckEqInt(HtmlNodeTreeVersion(id, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeTreeVersion(RoboHtmlDocumentIdNone,
                                                  &version, NULL),
                      RoboNotFound);
    RoboCheckEqInt((int)version, 0);
    MuExpectClean(id);
    MuFixtureEnd();
}

static void TestMutationVersionMovesOnlyOnSuccess(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlNodeHandle c = MuElement(id, &root, "c");
    uint32_t v0;
    uint32_t v1;

    MuFixtureBegin();

    v0 = MuVersion(id);
    /* A no-op removal of an absent name is not a tree change. */
    RoboCheckEqInt(HtmlNodeRemoveChildren(&a, NULL, NULL), RoboOk);
    RoboCheckEqInt((int)MuVersion(id), (int)v0);

    /* A refusal is not a tree change either. */
    RoboCheckEqInt(HtmlNodeAppendChild(&a, &b, NULL),
                      RoboAlreadyExists);
    RoboCheckEqInt((int)MuVersion(id), (int)v0);

    /* A success is. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &c, NULL), RoboOk);
    v1 = MuVersion(id);
    RoboCheck(v1 != v0);
    RoboCheckEqInt(HtmlNodeAppendChild(&a, &c, NULL), RoboOk);
    RoboCheck(MuVersion(id) != v1);

    MuExpectClean(id);
    MuFixtureEnd();
}

static void TestMutationCapabilityReportsItself(void)
{
    HtmlDomCapabilities caps;

    MuFixtureBegin();

    HtmlDomCapabilitiesInit(&caps);
    RoboCheckEqInt(HtmlDomQueryCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.mutation, 1);
    RoboCheckEqInt(caps.traversal, 1);
    RoboCheckEqInt(caps.treeBuilding, 1);
    /* Still later-phase work, and saying so is the point of the flags. */
    RoboCheckEqInt(caps.domNodes, 0);
    RoboCheckEqInt(caps.events, 0);
    RoboCheck(HtmlMutationKindIsKnown(
        RoboHtmlMutationTreeMutated));
    RoboCheck(!HtmlMutationKindIsKnown(
        (HtmlMutationKind)9));

    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Mutation hook                                                       */
/* ------------------------------------------------------------------ */

static int gHookMutations;
static int gHookRegistrations;

static void MutationHookCount(const HtmlMutationEvent *event,
                                void *userdata)
{
    int *seen = (int *)userdata;

    if (!event || !seen) {
        return;
    }
    if (event->kind == RoboHtmlMutationTreeMutated) {
        gHookMutations++;
    } else if (event->kind == RoboHtmlMutationNodeRegistered) {
        gHookRegistrations++;
    }
    (*seen)++;
}

static void TestMutationHookEmitted(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    int seen = 0;

    MuFixtureBegin();

    gHookMutations = 0;
    gHookRegistrations = 0;
    /*
     * Installed after the tree exists, because registration emits too and
     * this test is about the structural event.
     */
    RoboCheckEqInt(HtmlDocumentSetMutationHook(
                          id, MutationHookCount, &seen, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &b, NULL), RoboOk);
    RoboCheckEqInt(seen, 1);
    RoboCheckEqInt(gHookMutations, 1);
    RoboCheckEqInt(gHookRegistrations, 0);
    RoboCheckEqInt(HtmlNodeAppendChild(&a, &b, NULL), RoboOk);
    RoboCheckEqInt(seen, 2);
    /* A refusal emits nothing: there was no structural change to report. */
    RoboCheckEqInt(HtmlNodeAppendChild(&a, &b, NULL),
                      RoboAlreadyExists);
    RoboCheckEqInt(seen, 2);
    RoboCheckEqInt(HtmlDocumentSetMutationHook(id, NULL, NULL,
                                                           NULL),
                      RoboOk);

    MuExpectClean(id);
    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Traversal interaction                                               */
/* ------------------------------------------------------------------ */

static void TestMutationCursorInvalidated(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlNodeHandle c = MuElement(id, &root, "c");
    HtmlNodeHandle extra = MuElement(id, &root, "extra");
    HtmlChildCursor cursor;
    HtmlNodeHandle got;
    HtmlDiag diag;

    MuFixtureBegin();

    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &extra, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlChildCursorOpen(&cursor, &root, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &got, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &a));

    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &b, NULL), RoboOk);

    /*
     * The cursor's stored next id is now b, a node that is no longer a child
     * of root. Handing it out would report a child the parent does not have.
     */
    HtmlDiagReset(&diag);
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &got, &diag),
                      RoboError);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainMutation);
    RoboCheck(!HtmlNodeHandleIsWellformed(&got));
    /* The cursor is invalidated, not merely paused. */
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &got, NULL),
                      RoboNotFound);

    /* A fresh cursor on the mutated tree is correct. */
    RoboCheckEqInt(HtmlChildCursorOpen(&cursor, &root, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &got, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &a));
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &got, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&got, &c));
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &got, NULL),
                      RoboNotFound);

    MuExpectClean(id);
    MuFixtureEnd();
}

static void TestMutationCursorSurvivesRefusal(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlChildCursor cursor;
    HtmlNodeHandle got;
    size_t seen = 0u;

    MuFixtureBegin();

    RoboCheckEqInt(HtmlChildCursorOpen(&cursor, &root, NULL),
                      RoboOk);
    /* A refused mutation changed nothing, so the iteration is still valid. */
    RoboCheckEqInt(HtmlNodeAppendChild(&a, &b, NULL),
                      RoboAlreadyExists);
    while (HtmlChildCursorNext(&cursor, &got, NULL) == RoboOk) {
        seen++;
    }
    RoboCheckEqInt((int)seen, 2);

    MuFixtureEnd();
}

static int gWalkMutated;

static Result WalkMutatingVisitor(const HtmlNodeView *view,
                                           size_t depth,
                                           void *user)
{
    HtmlNodeHandle *mover = (HtmlNodeHandle *)user;

    (void)depth;
    if (!gWalkMutated && view->kind == RoboHtmlNodeKindText) {
        gWalkMutated = 1;
        /* Detach the subtree the walk is standing in. */
        return HtmlNodeRemoveChild(&view->parent, &view->handle, NULL);
    }
    (void)mover;
    return RoboOk;
}

static void TestMutationWalkInvalidated(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle body = MuElement(id, &root, "body");
    HtmlNodeHandle p = MuElement(id, &body, "p");
    HtmlNodeHandle moving = MuElement(id, &root, "moving");
    HtmlDiag diag;
    Result r;

    MuFixtureBegin();

    (void)MuText(id, &p, "inside");
    gWalkMutated = 0;
    HtmlDiagReset(&diag);

    r = HtmlNodeWalkPreOrder(&body, RoboHtmlNodeFilterAll,
                                      WalkMutatingVisitor, &moving, &diag);
    RoboCheckEqInt(r, RoboError);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainMutation);
    RoboCheckEqInt(gWalkMutated, 1);

    /* The walk stopped; the mutation it performed stands and is coherent. */
    RoboCheckEqInt(MuChildCount(&body), 1);
    RoboCheckEqInt(MuChildCount(&p), 0);

    /* A walk started after the change sees the new shape. */
    {
        HtmlNodeHandle found;

        RoboCheckEqInt(
            HtmlNodeFindFirst(&body, RoboHtmlNodeFilterText, NULL,
                                      NULL, &found, NULL),
            RoboNotFound);
    }
    MuExpectClean(id);
    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Parser-created trees                                                */
/* ------------------------------------------------------------------ */

static void TestMutationParserTreeReorder(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlNodeHandle heading;
    HtmlNodeHandle one;
    HtmlNodeHandle two;
    HtmlNodeHandle back;
    char text[128];
    size_t length = 0u;

    MuFixtureBegin();

    MuBuild(id,
             "<html><body><h1>Title</h1><p>One</p><p>Two</p></body></html>");

    /*
     * The builder finalizes what it built, so this is the case that matters:
     * a closed-for-construction document is still mutable.
     */
    RoboCheckEqInt(HtmlDocumentIsFinalized(id), 1);
    RoboCheckEqInt(HtmlDocumentAllowsConstruction(id), 0);

    root = MuRoot(id);
    RoboCheckEqInt(HtmlNodeFindElement(&root, "body", &body, NULL),
                      RoboOk);
    RoboCheckEqInt(MuChildCount(&body), 3);
    RoboCheckEqInt(HtmlNodeChildAt(&body, 0u, &heading, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlNodeChildAt(&body, 1u, &one, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeChildAt(&body, 2u, &two, NULL), RoboOk);

    RoboCheckEqInt(HtmlNodeSubtreeText(&body, text, sizeof(text),
                                                  &length, NULL),
                      RoboOk);
    RoboCheck(strcmp(text, "TitleOneTwo") == 0);

    /* Move the first paragraph to the end: Title, Two, One. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&body, &one, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&body, &one, NULL), RoboOk);
    RoboCheckEqInt(MuChildCount(&body), 3);
    RoboCheckEqInt(HtmlNodeChildAt(&body, 0u, &back, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&back, &heading));
    RoboCheckEqInt(HtmlNodeChildAt(&body, 2u, &back, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&back, &one));
    RoboCheckEqInt(HtmlNodeSubtreeText(&body, text, sizeof(text),
                                                  &length, NULL),
                      RoboOk);
    RoboCheck(strcmp(text, "TitleTwoOne") == 0);
    MuExpectClean(id);

    /* And put it back at the head, ahead of the heading. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&body, &one, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeInsertBefore(&body, &one, &heading, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlNodeSubtreeText(&body, text, sizeof(text),
                                                  &length, NULL),
                      RoboOk);
    RoboCheck(strcmp(text, "OneTitleTwo") == 0);
    MuExpectClean(id);

    (void)two;
    MuFixtureEnd();
}

static void TestMutationCreationStaysGated(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root;
    HtmlNodeHandle body;
    HtmlElementCreateInfo info;
    HtmlNodeHandle made;
    HtmlDiag diag;

    MuFixtureBegin();

    MuBuild(id, "<html><body><p>x</p></body></html>");
    root = MuRoot(id);
    RoboCheckEqInt(HtmlNodeFindElement(&root, "body", &body, NULL),
                      RoboOk);

    /*
     * The documented boundary of this phase: the DOM rearranges the nodes a
     * document has; registering a new one stays a construction operation,
     * which finalization closed. Pinned as a test so the seam is a decision
     * rather than an accident.
     */
    HtmlElementCreateInfoInit(&info);
    info.parent = body;
    info.origin = RoboHtmlNodeOriginDom;
    info.namespaceId = RoboHtmlNamespaceHtml;
    info.localName = "p";
    HtmlNodeHandleClear(&made);
    HtmlDiagReset(&diag);
    RoboCheckEqInt(HtmlElementCreate(id, &info, &made, &diag),
                      RoboError);
    RoboCheck(!HtmlNodeHandleIsWellformed(&made));

    /* Rearranging what is already there still works. */
    {
        HtmlNodeHandle p;

        RoboCheckEqInt(HtmlNodeFindElement(&root, "p", &p, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeRemoveChild(&body, &p, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeAppendChild(&body, &p, NULL),
                          RoboOk);
    }
    MuExpectClean(id);
    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Preservation                                                        */
/* ------------------------------------------------------------------ */

static void TestMutationPreservesAttributes(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle holder = MuElement(id, &root, "holder");
    HtmlNodeHandle target;
    HtmlElementCreateInfo info;
    HtmlAttribute attrs[2];
    const char *value;
    size_t valueLength = 0u;

    MuFixtureBegin();

    memset(attrs, 0, sizeof(attrs));
    attrs[0].name = "id";
    attrs[0].nameLength = 2u;
    attrs[0].value = "main";
    attrs[0].valueLength = 4u;
    attrs[1].name = "class";
    attrs[1].nameLength = 5u;
    attrs[1].value = "panel";
    attrs[1].valueLength = 5u;

    HtmlElementCreateInfoInit(&info);
    info.parent = root;
    info.origin = RoboHtmlNodeOriginTest;
    info.namespaceId = RoboHtmlNamespaceHtml;
    info.localName = "div";
    info.attributes = attrs;
    info.attributeCount = 2u;
    HtmlNodeHandleClear(&target);
    RoboCheckEqInt(HtmlElementCreate(id, &info, &target, NULL),
                      RoboOk);

    /* Move it around, twice, and re-read the attributes each time. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &target, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&holder, &target, NULL),
                      RoboOk);
    value = HtmlElementAttributeValue(&target, "id", &valueLength);
    RoboCheck(value != NULL);
    RoboCheckEqInt((int)valueLength, 4);
    RoboCheck(strcmp(value, "main") == 0);
    value = HtmlElementAttributeValue(&target, "class", &valueLength);
    RoboCheck(value != NULL);
    RoboCheck(strcmp(value, "panel") == 0);
    RoboCheck(HtmlElementHasAttribute(&target, "ID"));

    RoboCheckEqInt(HtmlNodeRemoveChild(&holder, &target, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &target, NULL),
                      RoboOk);
    {
        HtmlElementView view;

        RoboCheckEqInt(HtmlElementViewGet(&target, &view, NULL),
                          RoboOk);
        RoboCheckEqInt((int)view.attributeCount, 2);
        RoboCheckEqInt((int)view.namespaceId,
                          (int)RoboHtmlNamespaceHtml);
    }
    MuExpectClean(id);
    MuFixtureEnd();
}

static void TestMutationPreservesUnicodeText(void)
{
    static const char *const samples[] = {
        "Hello", "مرحبا", "فارسی", "اردو", "हिन्दी", "বাংলা",
        "中文", "日本語", "한국어", "😀 🚀 ❤️"
    };
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    size_t i;

    MuFixtureBegin();

    for (i = 0u; i < sizeof(samples) / sizeof(samples[0]); i++) {
        HtmlNodeHandle text = MuText(id, &a, samples[i]);
        const char *got;
        size_t length = 0u;

        /* Detach and re-attach: the bytes must survive the round trip. */
        RoboCheckEqInt(HtmlNodeRemoveChild(&a, &text, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeAppendChild(&b, &text, NULL),
                          RoboOk);
        got = HtmlTextContent(&text, &length);
        RoboCheck(got != NULL);
        RoboCheckEqInt((int)length, (int)strlen(samples[i]));
        RoboCheckMsg(strcmp(got, samples[i]) == 0,
                       "text bytes changed across a mutation");
        RoboCheckEqInt(HtmlNodeRemoveChild(&b, &text, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeAppendChild(&a, &text, NULL),
                          RoboOk);
    }
    MuExpectClean(id);
    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Long sequences + teardown                                           */
/* ------------------------------------------------------------------ */

static void TestMutationLongSequence(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlNodeHandle c = MuElement(id, &root, "c");
    HtmlNodeHandle d = MuElement(id, &root, "d");
    HtmlNodeHandle e = MuElement(id, &root, "e");

    MuFixtureBegin();

    /* d and e start attached so they exist; pull them out to re-insert. */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &d, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &e, NULL), RoboOk);

    /* append A, append B, append C is the fixture: a, b, c.
     * remove B (middle), insert D before C, replace A with E,
     * remove E, remove C, remove D.
     */
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &b, NULL), RoboOk);
    RoboCheckEqInt(MuChildCount(&root), 2);
    MuExpectClean(id);

    RoboCheckEqInt(HtmlNodeInsertBefore(&root, &d, &c, NULL),
                      RoboOk);
    RoboCheckEqInt(MuChildCount(&root), 3);
    MuExpectClean(id);

    RoboCheckEqInt(HtmlNodeReplaceChild(&root, &e, &a, NULL),
                      RoboOk);
    RoboCheckEqInt(MuChildCount(&root), 3);
    {
        HtmlNodeHandle first;

        RoboCheckEqInt(HtmlNodeFirstChild(&root, &first, NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&first, &e));
    }
    MuExpectClean(id);

    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &e, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &c, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &d, NULL), RoboOk);
    RoboCheckEqInt(MuChildCount(&root), 0);
    MuExpectClean(id);

    /* Everything is still live; the document just does not list it. */
    RoboCheck(HtmlDocumentOwnsNode(id, &a));
    RoboCheck(HtmlDocumentOwnsNode(id, &b));
    RoboCheck(HtmlDocumentOwnsNode(id, &c));
    RoboCheck(HtmlDocumentOwnsNode(id, &d));
    RoboCheck(HtmlDocumentOwnsNode(id, &e));

    MuFixtureEnd();
}

static void TestMutationDetachedSubtreeWalk(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle holder = MuElement(id, &root, "holder");
    HtmlNodeHandle div = MuElement(id, &holder, "div");
    HtmlNodeHandle span = MuElement(id, &div, "span");
    HtmlNodeHandle text = MuText(id, &span, "inner");
    char buffer[64];
    size_t length = 0u;

    MuFixtureBegin();

    RoboCheckEqInt(HtmlNodeRemoveChild(&holder, &div, NULL),
                      RoboOk);

    /* The detached subtree is a tree: a walk over it reaches the text. */
    RoboCheckEqInt(HtmlNodeSubtreeText(&div, buffer, sizeof(buffer),
                                                  &length, NULL),
                      RoboOk);
    RoboCheck(strcmp(buffer, "inner") == 0);
    /* And it is no longer part of the document's reachable tree. */
    {
        char whole[64];
        size_t wholeLength = 0u;

        RoboCheckEqInt(HtmlNodeSubtreeText(&root, whole,
                                                      sizeof(whole),
                                                      &wholeLength, NULL),
                          RoboOk);
        RoboCheckEqInt((int)wholeLength, 0);
    }
    /* The audit is clean whether or not a detached subtree exists: audit
     * counts only what the root reaches, and detached nodes are not orphans
     * of the tree, they are simply not in it. */
    MuExpectClean(id);

    (void)text;
    (void)span;
    MuFixtureEnd();
}

static void TestMutationTeardownAfterMutation(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &a, "b");
    HtmlNodeHandle text = MuText(id, &b, "x");

    MuFixtureBegin();

    /* a/b move around, b keeps and then loses its text, and the sequence
     * ends in a state the audit can check (a detached subtree is legal). */
    RoboCheckEqInt(HtmlNodeRemoveChild(&a, &b, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &b, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&b, &text, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&b, &text, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &b, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &a, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeAppendChild(&b, &a, NULL), RoboOk);
    MuExpectClean(id);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    /* Every handle from the destroyed document is stale; nothing dangles. */
    RoboCheck(HtmlNodeHandleIsStale(&root));
    RoboCheck(HtmlNodeHandleIsStale(&a));
    RoboCheck(HtmlNodeHandleIsStale(&b));
    RoboCheck(HtmlNodeHandleIsStale(&text));
    /* And the operations refuse rather than touch freed state. */
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &a, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &a, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlNodeRemoveChildren(&root, NULL, NULL),
                      RoboNotFound);
}

/**
 * The audit's two unreachable-node verdicts, side by side: a detached node
 * is reported and legal; a node that claims a parent while nothing lists it
 * is an orphan and is not.
 */
static void TestMutationAuditDistinguishesDetached(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &a, "b");
    HtmlNodeHandle text = MuText(id, &b, "inner");
    HtmlTreeReport rep;

    MuFixtureBegin();

    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &a, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &rep, NULL),
                      RoboOk);
    /* One detached root (a) is counted; b and the text are marked as its
     * subtree, not miscounted as orphans of their own. */
    RoboCheckEqInt((int)rep.detached, 1);
    RoboCheckEqInt((int)rep.orphans, 0);
    RoboCheckEqInt((int)rep.nodes, 1); /* only the root is reachable */
    RoboCheckEqInt((int)rep.registered, 4);
    RoboCheckEqInt(rep.ok, 1);

    /* Re-attaching makes the document whole again. */
    RoboCheckEqInt(HtmlNodeAppendChild(&root, &a, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &rep, NULL),
                      RoboOk);
    RoboCheckEqInt((int)rep.detached, 0);
    RoboCheckEqInt((int)rep.nodes, 4);
    RoboCheckEqInt(rep.ok, 1);
    MuExpectClean(id);

    (void)text;
    MuFixtureEnd();
}

static void TestMutationResetInvalidatesHandles(void)
{
    HtmlDocumentId id = MuDoc();
    HtmlNodeHandle root = MuRoot(id);
    HtmlNodeHandle a = MuElement(id, &root, "a");
    HtmlNodeHandle b = MuElement(id, &root, "b");
    HtmlNodeHandle rootAfter;

    MuFixtureBegin();

    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &a, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeRemoveChild(&root, &b, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);

    /* The tree is gone, so the old handles miss and the root carries on. */
    RoboCheckEqInt(HtmlNodeAppendChild(&a, &b, NULL),
                      RoboNotFound);
    rootAfter = MuRoot(id);
    RoboCheckEqInt(MuChildCount(&rootAfter), 0);
    MuExpectClean(id);

    MuFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlMutation(void);

void TestRegisterHtmlMutation(void)
{
    TestRegister("html.mutate.append_first",
                       TestMutationAppendFirst);
    TestRegister("html.mutate.append_multiple_order",
                       TestMutationAppendMultipleOrder);
    TestRegister("html.mutate.append_text",
                       TestMutationAppendText);
    TestRegister("html.mutate.append_rejects_attached",
                       TestMutationAppendRejectsAttached);
    TestRegister("html.mutate.append_rejects_self",
                       TestMutationAppendRejectsSelf);
    TestRegister("html.mutate.append_rejects_document_node",
                       TestMutationAppendRejectsDocumentNode);
    TestRegister("html.mutate.cycle_ancestor_refused",
                       TestMutationCycleAncestorRefused);
    TestRegister("html.mutate.cycle_insert_before_refused",
                       TestMutationCycleInsertBeforeRefused);
    TestRegister("html.mutate.insert_before_positions",
                       TestMutationInsertBeforePositions);
    TestRegister("html.mutate.insert_before_rejects_foreign_reference",
                       TestMutationInsertBeforeRejectsForeignReference);
    TestRegister("html.mutate.remove_positions",
                       TestMutationRemovePositions);
    TestRegister("html.mutate.remove_rejects_wrong_parent",
                       TestMutationRemoveRejectsWrongParent);
    TestRegister("html.mutate.remove_keeps_subtree",
                       TestMutationRemoveKeepsSubtree);
    TestRegister("html.mutate.replace_positions",
                       TestMutationReplacePositions);
    TestRegister("html.mutate.replace_rejects_attached",
                       TestMutationReplaceRejectsAttached);
    TestRegister("html.mutate.replace_rejects_cycle",
                       TestMutationReplaceRejectsCycle);
    TestRegister("html.mutate.remove_children",
                       TestMutationRemoveChildren);
    TestRegister("html.mutate.cross_document_refused",
                       TestMutationCrossDocumentRefused);
    TestRegister("html.mutate.null_and_unknown_handles",
                       TestMutationNullAndUnknownHandles);
    TestRegister("html.mutate.version_moves_only_on_success",
                       TestMutationVersionMovesOnlyOnSuccess);
    TestRegister("html.mutate.capability_reports_itself",
                       TestMutationCapabilityReportsItself);
    TestRegister("html.mutate.hook_emitted",
                       TestMutationHookEmitted);
    TestRegister("html.mutate.cursor_invalidated",
                       TestMutationCursorInvalidated);
    TestRegister("html.mutate.cursor_survives_refusal",
                       TestMutationCursorSurvivesRefusal);
    TestRegister("html.mutate.walk_invalidated",
                       TestMutationWalkInvalidated);
    TestRegister("html.mutate.parser_tree_reorder",
                       TestMutationParserTreeReorder);
    TestRegister("html.mutate.creation_stays_gated",
                       TestMutationCreationStaysGated);
    TestRegister("html.mutate.preserves_attributes",
                       TestMutationPreservesAttributes);
    TestRegister("html.mutate.preserves_unicode_text",
                       TestMutationPreservesUnicodeText);
    TestRegister("html.mutate.long_sequence",
                       TestMutationLongSequence);
    TestRegister("html.mutate.detached_subtree_walk",
                       TestMutationDetachedSubtreeWalk);
    TestRegister("html.mutate.teardown_after_mutation",
                       TestMutationTeardownAfterMutation);
    TestRegister("html.mutate.audit_distinguishes_detached",
                       TestMutationAuditDistinguishesDetached);
    TestRegister("html.mutate.reset_invalidates_handles",
                       TestMutationResetInvalidatesHandles);
}
