/*
 * RoboHTML Phase 8.8 traversal suite:
 * relationships in both directions, the child cursor, deterministic
 * pre-order/document order, the node filters, bounded search, parser
 * integration, Unicode and deep trees, defensive input, ownership and
 * lifetime, and the debug dump that now runs on the walker.
 *
 * The exact-order tests build the tree through the element and text models
 * rather than through the parser, because the parser's source position and
 * whitespace handling are the 8.3 suite's subject: here the tree is the
 * fixture, so every expected order is stated literally.
 *
 * The conceptual tree the order tests share (task spec section 33):
 *
 *   Document
 *     └── html
 *         └── body
 *             ├── Text("Hello ")
 *             ├── div
 *             │   ├── Text("Roboticela ")
 *             │   └── span
 *             │       └── Text("Runtime")
 *             └── Text("!")
 *
 * Note on aliasing: no accessor here is ever called with the same handle as
 * both input and output. A single-link accessor clears its output before it
 * resolves its input, so aliasing would silently turn every call into a miss
 * and make a wrong expectation look green.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTMLDomTestSupport.h" /* DomCheckpointHtml: the document of record */
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLDom.h"
#include "RoboHTML/RoboHTMLElement.h"
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

static void TvFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void TvFixtureEnd(void)
{
    HtmlShutdown();
}

static HtmlDocumentId TvDoc(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

/** Build @p text into @p id with the one-shot API. */
static void TvBuild(HtmlDocumentId id, const char *text)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL), RoboOk);
}

static HtmlNodeHandle TvRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

/** Append a child element named @p name under @p parent. */
static HtmlNodeHandle TvElement(HtmlDocumentId id,
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

/** Append a text node carrying @p content under @p parent. */
static HtmlNodeHandle TvText(HtmlDocumentId id,
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

/** The shared section-33 tree. */
typedef struct tvTree {
    HtmlDocumentId id;
    HtmlNodeHandle document;
    HtmlNodeHandle html;
    HtmlNodeHandle body;
    HtmlNodeHandle leadText; /* "Hello " */
    HtmlNodeHandle div;
    HtmlNodeHandle innerText; /* "Roboticela " */
    HtmlNodeHandle span;
    HtmlNodeHandle deepText; /* "Runtime" */
    HtmlNodeHandle tailText; /* "!" */
} TvTree;

static void TvTreeBuild(TvTree *tree)
{
    memset(tree, 0, sizeof(*tree));
    tree->id = TvDoc();
    tree->document = TvRoot(tree->id);

    tree->html = TvElement(tree->id, &tree->document, "html");
    tree->body = TvElement(tree->id, &tree->html, "body");

    tree->leadText = TvText(tree->id, &tree->body, "Hello ");
    tree->div = TvElement(tree->id, &tree->body, "div");
    tree->innerText = TvText(tree->id, &tree->div, "Roboticela ");
    tree->span = TvElement(tree->id, &tree->div, "span");
    tree->deepText = TvText(tree->id, &tree->span, "Runtime");
    tree->tailText = TvText(tree->id, &tree->body, "!");
}

/** Handle of the first descendant element named @p name (invalid when none). */
static HtmlNodeHandle TvFind(HtmlDocumentId id,
                                       const char *name)
{
    HtmlNodeHandle root = TvRoot(id);
    HtmlNodeHandle found;

    HtmlNodeHandleClear(&found);
    if (HtmlNodeFindElement(&root, name, &found, NULL) != RoboOk) {
        HtmlNodeHandleClear(&found);
    }
    return found;
}

/** Bounded copy that never leaves @p out unterminated. */
static void TvCopy(char *out, size_t cap, const char *src, size_t length)
{
    size_t take = length;

    if (!out || cap == 0u) {
        return;
    }
    if (take > cap - 1u) {
        take = cap - 1u;
    }
    if (src && take > 0u) {
        memcpy(out, src, take);
    }
    out[take] = '\0';
}

/* ------------------------------------------------------------------ */
/* Walk recorder                                                       */
/* ------------------------------------------------------------------ */

#define TV_MAX_VISITS 64

typedef struct tvLog {
    size_t count;
    size_t recorded;
    HtmlNodeKind kinds[TV_MAX_VISITS];
    size_t depths[TV_MAX_VISITS];
    char names[TV_MAX_VISITS][32];
    char texts[TV_MAX_VISITS][80];
    /* stop the walk once count reaches this many visits (0 = never) */
    size_t stopAfter;
    Result stopCode;
    /* if set, the first visit destroys the document instead of recording */
    int destroyDocument;
} TvLog;

static void TvLogInit(TvLog *log)
{
    memset(log, 0, sizeof(*log));
}

static Result TvVisit(const HtmlNodeView *view,
                              size_t depth,
                              void *user)
{
    TvLog *log = (TvLog *)user;

    if (log->destroyDocument) {
        log->destroyDocument = 0;
        (void)HtmlDocumentDestroy(view->handle.documentId, NULL);
        return RoboOk;
    }
    if (log->count < TV_MAX_VISITS) {
        size_t slot = log->count;

        log->kinds[slot] = view->kind;
        log->depths[slot] = depth;
        TvCopy(log->names[slot], sizeof(log->names[slot]), view->name,
                view->name ? strlen(view->name) : 0u);
        TvCopy(log->texts[slot], sizeof(log->texts[slot]), view->text,
                view->textLength);
        log->recorded = slot + 1u;
    }
    log->count++;
    if (log->stopAfter != 0u && log->count >= log->stopAfter) {
        return log->stopCode;
    }
    return RoboOk;
}

/** Index of the first recorded visit whose text equals @p text, or -1. */
static long TvIndexOfText(const TvLog *log, const char *text)
{
    size_t i;

    for (i = 0u; i < log->recorded; i++) {
        if (log->kinds[i] == RoboHtmlNodeKindText &&
            strcmp(log->texts[i], text) == 0) {
            return (long)i;
        }
    }
    return -1;
}

/** Index of the first recorded visit whose element name equals @p name. */
static long TvIndexOfName(const TvLog *log, const char *name)
{
    size_t i;

    for (i = 0u; i < log->recorded; i++) {
        if (log->kinds[i] == RoboHtmlNodeKindElement &&
            strcmp(log->names[i], name) == 0) {
            return (long)i;
        }
    }
    return -1;
}

/** Counting visitor with no upper bound (deep-tree test). */
typedef struct tvCounter {
    size_t count;
    size_t maxDepth;
} TvCounter;

static Result TvCountVisit(const HtmlNodeView *view,
                                    size_t depth,
                                    void *user)
{
    TvCounter *counter = (TvCounter *)user;

    (void)view;
    counter->count++;
    if (depth > counter->maxDepth) {
        counter->maxDepth = depth;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Relationships                                                       */
/* ------------------------------------------------------------------ */

static void TestTvChildRelationships(void)
{
    TvTree tree;
    HtmlNodeHandle out;
    HtmlNodeHandle next;
    size_t count = 0u;

    TvFixtureBegin();
    TvTreeBuild(&tree);

    /* body's children are exactly the three nodes the fixture created. */
    RoboCheckEqInt(HtmlNodeFirstChild(&tree.body, &out, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.leadText));
    RoboCheckEqInt(HtmlNodeChildCount(&tree.body, &count, NULL),
                      RoboOk);
    RoboCheckEqInt((int)count, 3);
    RoboCheckEqInt(HtmlNodeLastChild(&tree.body, &out, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.tailText));

    /* Text and element children are siblings on equal terms. */
    RoboCheckEqInt(HtmlNodeNextSibling(&tree.leadText, &out, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.div));
    RoboCheckEqInt(HtmlNodeNextSibling(&tree.div, &out, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.tailText));
    RoboCheckEqInt(HtmlNodePrevSibling(&tree.tailText, &out, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.div));

    /* The ends of a child list are misses, and the output is cleared. */
    RoboCheckEqInt(HtmlNodePrevSibling(&tree.leadText, &out, NULL),
                      RoboNotFound);
    RoboCheck(!HtmlNodeHandleIsWellformed(&out));
    RoboCheckEqInt(HtmlNodeNextSibling(&tree.tailText, &out, NULL),
                      RoboNotFound);
    RoboCheck(!HtmlNodeHandleIsWellformed(&out));

    /* Leaf nodes answer "no children" with a miss too. */
    RoboCheckEqInt(HtmlNodeFirstChild(&tree.deepText, &out, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlNodeLastChild(&tree.deepText, &out, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlNodeChildCount(&tree.deepText, &count, NULL),
                      RoboOk);
    RoboCheckEqInt((int)count, 0);

    /* Walking out and back in is symmetric. */
    RoboCheckEqInt(HtmlNodeNextSibling(&tree.div, &next, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlNodePrevSibling(&next, &out, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.div));

    (void)HtmlDocumentDestroy(tree.id, NULL);
    TvFixtureEnd();
}

static void TestTvParentAndRoot(void)
{
    TvTree tree;
    HtmlNodeHandle out;
    size_t depth = 0u;

    TvFixtureBegin();
    TvTreeBuild(&tree);

    /* Text -> element, one step at a time. */
    RoboCheckEqInt(HtmlNodeParent(&tree.deepText, &out, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.span));
    RoboCheckEqInt(HtmlNodeParent(&tree.span, &out, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.div));
    RoboCheckEqInt(HtmlNodeParent(&tree.div, &out, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.body));
    RoboCheckEqInt(HtmlNodeParent(&tree.body, &out, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.html));
    RoboCheckEqInt(HtmlNodeParent(&tree.html, &out, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.document));

    /* The document node is the top: no parent is an answer, not an error. */
    RoboCheckEqInt(HtmlNodeParent(&tree.document, &out, NULL),
                      RoboNotFound);
    RoboCheck(!HtmlNodeHandleIsWellformed(&out));

    /* Root traversal walks the same chain to the document node. */
    RoboCheckEqInt(HtmlNodeRoot(&tree.deepText, &out, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.document));
    RoboCheckEqInt(HtmlNodeRoot(&tree.document, &out, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.document));
    /* A text node and its element ancestor share a root. */
    RoboCheckEqInt(HtmlNodeRoot(&tree.span, &out, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.document));

    /* Depth is measured from the document node. */
    RoboCheckEqInt(HtmlNodeDepth(&tree.document, &depth, NULL),
                      RoboOk);
    RoboCheckEqInt((int)depth, 0);
    RoboCheckEqInt(HtmlNodeDepth(&tree.html, &depth, NULL), RoboOk);
    RoboCheckEqInt((int)depth, 1);
    RoboCheckEqInt(HtmlNodeDepth(&tree.body, &depth, NULL), RoboOk);
    RoboCheckEqInt((int)depth, 2);
    RoboCheckEqInt(HtmlNodeDepth(&tree.div, &depth, NULL), RoboOk);
    RoboCheckEqInt((int)depth, 3);
    RoboCheckEqInt(HtmlNodeDepth(&tree.span, &depth, NULL), RoboOk);
    RoboCheckEqInt((int)depth, 4);
    RoboCheckEqInt(HtmlNodeDepth(&tree.deepText, &depth, NULL),
                      RoboOk);
    RoboCheckEqInt((int)depth, 5);

    (void)HtmlDocumentDestroy(tree.id, NULL);
    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Document order                                                      */
/* ------------------------------------------------------------------ */

static void TestTvDocumentOrder(void)
{
    TvTree tree;
    TvLog log;
    TvLog again;

    TvFixtureBegin();
    TvTreeBuild(&tree);
    TvLogInit(&log);

    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &tree.html, RoboHtmlNodeFilterAll, TvVisit,
                          &log, NULL),
                      RoboOk);

    /* Exactly the eight spec-section-33 nodes, in this order. */
    RoboCheckEqInt((int)log.count, 8);
    RoboCheckEqInt((int)log.recorded, 8);

    RoboCheckEqInt((int)log.kinds[0], RoboHtmlNodeKindElement);
    RoboCheckStrEq(log.names[0], "html");
    RoboCheckEqInt((int)log.depths[0], 1);

    RoboCheckEqInt((int)log.kinds[1], RoboHtmlNodeKindElement);
    RoboCheckStrEq(log.names[1], "body");
    RoboCheckEqInt((int)log.depths[1], 2);

    RoboCheckEqInt((int)log.kinds[2], RoboHtmlNodeKindText);
    RoboCheckStrEq(log.texts[2], "Hello ");
    RoboCheckEqInt((int)log.depths[2], 3);

    RoboCheckEqInt((int)log.kinds[3], RoboHtmlNodeKindElement);
    RoboCheckStrEq(log.names[3], "div");
    RoboCheckEqInt((int)log.depths[3], 3);

    RoboCheckEqInt((int)log.kinds[4], RoboHtmlNodeKindText);
    RoboCheckStrEq(log.texts[4], "Roboticela ");
    RoboCheckEqInt((int)log.depths[4], 4);

    RoboCheckEqInt((int)log.kinds[5], RoboHtmlNodeKindElement);
    RoboCheckStrEq(log.names[5], "span");
    RoboCheckEqInt((int)log.depths[5], 4);

    RoboCheckEqInt((int)log.kinds[6], RoboHtmlNodeKindText);
    RoboCheckStrEq(log.texts[6], "Runtime");
    RoboCheckEqInt((int)log.depths[6], 5);

    RoboCheckEqInt((int)log.kinds[7], RoboHtmlNodeKindText);
    RoboCheckStrEq(log.texts[7], "!");
    RoboCheckEqInt((int)log.depths[7], 3);

    /* Order is a pure function of the tree: a second walk is identical. */
    TvLogInit(&again);
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &tree.html, RoboHtmlNodeFilterAll, TvVisit,
                          &again, NULL),
                      RoboOk);
    RoboCheckEqInt((int)again.count, (int)log.count);
    RoboCheckStrEq(again.texts[7], log.texts[7]);
    RoboCheckStrEq(again.names[5], log.names[5]);
    RoboCheckEqInt((int)again.depths[6], (int)log.depths[6]);

    (void)HtmlDocumentDestroy(tree.id, NULL);
    TvFixtureEnd();
}

static void TestTvPreOrderFromDocument(void)
{
    TvTree tree;
    TvLog log;

    TvFixtureBegin();
    TvTreeBuild(&tree);
    TvLogInit(&log);

    /* Starting at the document node includes it and everything below. */
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &tree.document, RoboHtmlNodeFilterAll, TvVisit,
                          &log, NULL),
                      RoboOk);
    RoboCheckEqInt((int)log.count, 9);
    RoboCheckEqInt((int)log.kinds[0], RoboHtmlNodeKindDocument);
    RoboCheckEqInt((int)log.depths[0], 0);
    RoboCheckStrEq(log.names[1], "html");

    /* Descendants skips the start: one fewer visit, same order after it. */
    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkDescendants(
                          &tree.document, RoboHtmlNodeFilterAll, TvVisit,
                          &log, NULL),
                      RoboOk);
    RoboCheckEqInt((int)log.count, 8);
    RoboCheckStrEq(log.names[0], "html");

    /* A subtree walk is bounded by its root, and still reports absolute
     * depth — a visitor can indent without walking the parent chain. */
    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &tree.span, RoboHtmlNodeFilterAll, TvVisit,
                          &log, NULL),
                      RoboOk);
    RoboCheckEqInt((int)log.count, 2);
    RoboCheckStrEq(log.names[0], "span");
    RoboCheckEqInt((int)log.depths[0], 4);
    RoboCheckStrEq(log.texts[1], "Runtime");
    RoboCheckEqInt((int)log.depths[1], 5);

    /* A lone node is a one-visit walk. */
    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &tree.deepText, RoboHtmlNodeFilterAll, TvVisit,
                          &log, NULL),
                      RoboOk);
    RoboCheckEqInt((int)log.count, 1);
    RoboCheckEqInt((int)log.kinds[0], RoboHtmlNodeKindText);

    (void)HtmlDocumentDestroy(tree.id, NULL);
    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Filters                                                             */
/* ------------------------------------------------------------------ */

static void TestTvElementFilter(void)
{
    TvTree tree;
    TvLog log;

    TvFixtureBegin();
    TvTreeBuild(&tree);
    TvLogInit(&log);

    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &tree.html, RoboHtmlNodeFilterElements, TvVisit,
                          &log, NULL),
                      RoboOk);

    /* Text nodes are excluded from the visit but still stepped through, so
     * the element order is exactly the document order of the elements. */
    RoboCheckEqInt((int)log.count, 4);
    RoboCheckStrEq(log.names[0], "html");
    RoboCheckStrEq(log.names[1], "body");
    RoboCheckStrEq(log.names[2], "div");
    RoboCheckStrEq(log.names[3], "span");
    RoboCheckEqInt((int)log.depths[3], 4);

    /* A text node between two elements cannot hide the second element: the
     * filter projects the same walk rather than skipping subtrees. */
    RoboCheckEqInt((int)log.kinds[2], RoboHtmlNodeKindElement);
    RoboCheckEqInt((int)log.depths[2], 3);

    (void)HtmlDocumentDestroy(tree.id, NULL);
    TvFixtureEnd();
}

static void TestTvTextFilter(void)
{
    TvTree tree;
    TvLog log;

    TvFixtureBegin();
    TvTreeBuild(&tree);
    TvLogInit(&log);

    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &tree.html, RoboHtmlNodeFilterText, TvVisit,
                          &log, NULL),
                      RoboOk);

    RoboCheckEqInt((int)log.count, 4);
    RoboCheckStrEq(log.texts[0], "Hello ");
    RoboCheckStrEq(log.texts[1], "Roboticela ");
    RoboCheckStrEq(log.texts[2], "Runtime");
    RoboCheckStrEq(log.texts[3], "!");
    RoboCheckEqInt((int)log.depths[0], 3);
    RoboCheckEqInt((int)log.depths[2], 5);

    /* Elements-only from the same start, to prove the filters differ. */
    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkDescendants(
                          &tree.body, RoboHtmlNodeFilterElements, TvVisit,
                          &log, NULL),
                      RoboOk);
    RoboCheckEqInt((int)log.count, 2);
    RoboCheckStrEq(log.names[0], "div");
    RoboCheckStrEq(log.names[1], "span");

    (void)HtmlDocumentDestroy(tree.id, NULL);
    TvFixtureEnd();
}

static void TestTvFilterApi(void)
{
    TvTree tree;

    TvFixtureBegin();
    TvTreeBuild(&tree);

    RoboCheckEqInt(HtmlNodeFilterIsKnown(RoboHtmlNodeFilterAll),
                      1);
    RoboCheckEqInt(
        HtmlNodeFilterIsKnown(RoboHtmlNodeFilterElements), 1);
    RoboCheckEqInt(HtmlNodeFilterIsKnown(RoboHtmlNodeFilterText),
                      1);
    RoboCheckEqInt(HtmlNodeFilterIsKnown(
                          (HtmlNodeFilter)99),
                      0);
    RoboCheckStrEq(HtmlNodeFilterName(RoboHtmlNodeFilterAll),
                      "all");
    RoboCheckStrEq(
        HtmlNodeFilterName(RoboHtmlNodeFilterElements), "elements");
    RoboCheckStrEq(HtmlNodeFilterName(RoboHtmlNodeFilterText),
                      "text");
    RoboCheckStrEq(HtmlNodeFilterName((HtmlNodeFilter)99),
                      "unknown");

    /* The exposed predicate agrees with the walker's rule. */
    RoboCheckEqInt(
        HtmlNodeMatchesFilter(&tree.span, RoboHtmlNodeFilterElements),
        1);
    RoboCheckEqInt(
        HtmlNodeMatchesFilter(&tree.span, RoboHtmlNodeFilterText),
        0);
    RoboCheckEqInt(HtmlNodeMatchesFilter(
                          &tree.deepText, RoboHtmlNodeFilterText),
                      1);
    RoboCheckEqInt(HtmlNodeMatchesFilter(
                          &tree.deepText, RoboHtmlNodeFilterElements),
                      0);
    RoboCheckEqInt(HtmlNodeMatchesFilter(&tree.deepText,
                                                    RoboHtmlNodeFilterAll),
                      1);
    RoboCheckEqInt(HtmlNodeMatchesFilter(
                          &tree.deepText, (HtmlNodeFilter)99),
                      0);
    RoboCheckEqInt(
        HtmlNodeMatchesFilter(NULL, RoboHtmlNodeFilterAll), 0);

    (void)HtmlDocumentDestroy(tree.id, NULL);
    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Child cursor                                                        */
/* ------------------------------------------------------------------ */

static void TestTvChildCursor(void)
{
    TvTree tree;
    HtmlChildCursor cursor;
    HtmlChildCursor copy;
    HtmlNodeHandle child;
    HtmlNodeHandle byIndex;
    size_t seen = 0u;

    TvFixtureBegin();
    TvTreeBuild(&tree);

    RoboCheckEqInt(HtmlChildCursorOpen(&cursor, &tree.body, NULL),
                      RoboOk);
    while (HtmlChildCursorNext(&cursor, &child, NULL) == RoboOk) {
        /* The cursor's order is child_at's order, one lookup per child. */
        RoboCheckEqInt(HtmlNodeChildAt(&tree.body, seen, &byIndex,
                                                  NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&child, &byIndex));
        seen++;
    }
    RoboCheckEqInt((int)seen, 3);

    /* An exhausted cursor keeps answering NOT_FOUND. */
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &child, NULL),
                      RoboNotFound);
    RoboCheck(!HtmlNodeHandleIsWellformed(&child));

    /* The three children, in order. */
    RoboCheckEqInt(HtmlChildCursorOpen(&cursor, &tree.body, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &child, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&child, &tree.leadText));
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &child, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&child, &tree.div));
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &child, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&child, &tree.tailText));

    /* A childless node opens successfully and is immediately exhausted: an
     * empty iteration is a normal result, not an error. */
    RoboCheckEqInt(
        HtmlChildCursorOpen(&cursor, &tree.deepText, NULL), RoboOk);
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &child, NULL),
                      RoboNotFound);

    /* A copy continues independently — the cursor owns no shared state. */
    RoboCheckEqInt(HtmlChildCursorOpen(&cursor, &tree.body, NULL),
                      RoboOk);
    copy = cursor;
    RoboCheckEqInt(HtmlChildCursorNext(&copy, &child, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&child, &tree.leadText));
    /* The copy runs to the second child without dragging the original. */
    RoboCheckEqInt(HtmlChildCursorNext(&copy, &child, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&child, &tree.div));
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &child, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&child, &tree.leadText));
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &child, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&child, &tree.div));

    /* Init and NULL handling. */
    HtmlChildCursorInit(&cursor);
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &child, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlChildCursorOpen(NULL, &tree.body, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlChildCursorOpen(&cursor, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, NULL, NULL),
                      RoboInvalidArgument);
    HtmlChildCursorInit(NULL); /* safe on NULL */

    /* An unresolvable parent is a lookup miss, not a silent empty cursor. */
    {
        HtmlNodeHandle stale;
        HtmlDiag diag;

        HtmlNodeHandleClear(&stale);
        HtmlDiagReset(&diag);
        RoboCheckEqInt(HtmlChildCursorOpen(&cursor, &stale, &diag),
                          RoboNotFound);
        RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainTraversal);
    }

    (void)HtmlDocumentDestroy(tree.id, NULL);
    TvFixtureEnd();
}

static void TestTvCursorAfterDestroy(void)
{
    TvTree tree;
    HtmlChildCursor cursor;
    HtmlNodeHandle child;

    TvFixtureBegin();
    TvTreeBuild(&tree);

    RoboCheckEqInt(HtmlChildCursorOpen(&cursor, &tree.body, NULL),
                      RoboOk);
    (void)HtmlDocumentDestroy(tree.id, NULL);

    /* The cursor is inert, not dangerous: it reports the loss. */
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &child, NULL),
                      RoboNotFound);
    RoboCheck(!HtmlNodeHandleIsWellformed(&child));
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &child, NULL),
                      RoboNotFound);

    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Search                                                              */
/* ------------------------------------------------------------------ */

typedef struct nameQuery {
    const char *name;
} NameQuery;

static int NameMatches(const HtmlNodeView *view, void *user)
{
    const NameQuery *query = (const NameQuery *)user;

    return strcmp(view->name, query->name) == 0;
}

static void TestTvFind(void)
{
    TvTree tree;
    HtmlNodeHandle out;
    NameQuery query;

    TvFixtureBegin();
    TvTreeBuild(&tree);

    /* First element at or below, and first element inside: these differ
     * exactly when the start node itself matches. */
    query.name = "div";
    RoboCheckEqInt(HtmlNodeFindFirst(
                          &tree.div, RoboHtmlNodeFilterElements,
                          NameMatches, &query, &out, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.div));
    /* There is no nested div, so the inside-only search finds nothing. */
    RoboCheckEqInt(HtmlNodeFindDescendant(
                          &tree.div, RoboHtmlNodeFilterElements,
                          NameMatches, &query, &out, NULL),
                      RoboNotFound);
    RoboCheck(!HtmlNodeHandleIsWellformed(&out));
    query.name = "span";
    RoboCheckEqInt(HtmlNodeFindDescendant(
                          &tree.div, RoboHtmlNodeFilterElements,
                          NameMatches, &query, &out, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.span));

    /* A NULL predicate is the plain "first of this kind". */
    RoboCheckEqInt(HtmlNodeFindFirst(
                          &tree.div, RoboHtmlNodeFilterText, NULL, NULL,
                          &out, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.innerText));
    RoboCheckEqInt(HtmlNodeFindFirst(
                          &tree.body, RoboHtmlNodeFilterElements, NULL,
                          NULL, &out, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.body));
    RoboCheckEqInt(HtmlNodeFindDescendant(
                          &tree.body, RoboHtmlNodeFilterElements, NULL,
                          NULL, &out, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.div));

    query.name = "nothing-here";
    RoboCheckEqInt(HtmlNodeFindFirst(
                          &tree.div, RoboHtmlNodeFilterElements,
                          NameMatches, &query, &out, NULL),
                      RoboNotFound);
    RoboCheck(!HtmlNodeHandleIsWellformed(&out));

    /* An invalid start is an error, not an empty result. */
    {
        HtmlNodeHandle stale;

        HtmlNodeHandleClear(&stale);
        RoboCheckEqInt(HtmlNodeFindFirst(
                              &stale, RoboHtmlNodeFilterAll, NULL, NULL,
                              &out, NULL),
                          RoboNotFound);
        RoboCheckEqInt(HtmlNodeFindDescendant(
                              &stale, RoboHtmlNodeFilterAll, NULL, NULL,
                              &out, NULL),
                          RoboNotFound);
    }

    /* find_element keeps its documented "inside" contract. */
    RoboCheckEqInt(HtmlNodeFindElement(&tree.div, "div", &out, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlNodeFindElement(&tree.div, "span", &out, NULL),
                      RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &tree.span));

    (void)HtmlDocumentDestroy(tree.id, NULL);
    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Parser integration                                                  */
/* ------------------------------------------------------------------ */

static void TestTvParserDocumentOrder(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle div;
    HtmlNodeHandle root;
    TvLog log;

    TvFixtureBegin();

    id = TvDoc();
    TvBuild(id, "<div>Hello <span>Roboticela</span> Runtime</div>");
    div = TvFind(id, "div");
    RoboCheck(HtmlElementIsElement(&div));

    /* div → Text("Hello ") → span → Text("Roboticela") → Text(" Runtime") */
    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkDescendants(
                          &div, RoboHtmlNodeFilterAll, TvVisit, &log,
                          NULL),
                      RoboOk);
    RoboCheckEqInt((int)log.count, 4);
    RoboCheckEqInt((int)log.kinds[0], RoboHtmlNodeKindText);
    RoboCheckStrEq(log.texts[0], "Hello ");
    RoboCheckEqInt((int)log.kinds[1], RoboHtmlNodeKindElement);
    RoboCheckStrEq(log.names[1], "span");
    RoboCheckStrEq(log.texts[2], "Roboticela");
    RoboCheckStrEq(log.texts[3], " Runtime");

    /* Text nodes reached by traversal carry the same bytes subtree_text
     * concatenates, in the same order. */
    {
        char text[64];
        size_t length = 0u;

        RoboCheckEqInt(HtmlNodeSubtreeText(&div, text,
                                                      sizeof(text), &length,
                                                      NULL),
                          RoboOk);
        RoboCheckStrEq(text, "Hello Roboticela Runtime");
        RoboCheckEqInt((int)length, 24);
    }

    (void)HtmlDocumentDestroy(id, NULL);

    /* The document-order walk from the document node is the same sequence. */
    id = TvDoc();
    TvBuild(id, "<html><body><p>Hello <b>Roboticela</b>!</p></body></html>");
    root = TvRoot(id);
    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &root, RoboHtmlNodeFilterText, TvVisit, &log,
                          NULL),
                      RoboOk);
    RoboCheckEqInt((int)log.count, 3);
    RoboCheckStrEq(log.texts[0], "Hello ");
    RoboCheckStrEq(log.texts[1], "Roboticela");
    RoboCheckStrEq(log.texts[2], "!");
    /* Text after an element is the element's next sibling, so document order
     * cannot be reconstructed from the elements alone. */
    {
        long hello = TvIndexOfText(&log, "Hello ");
        long robot = TvIndexOfText(&log, "Roboticela");

        RoboCheck(hello >= 0);
        RoboCheck(robot > hello);
    }
    /* The b element sits between the first two text nodes. */
    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkDescendants(
                          &root, RoboHtmlNodeFilterElements, TvVisit,
                          &log, NULL),
                      RoboOk);
    RoboCheck(TvIndexOfName(&log, "p") < TvIndexOfName(&log, "b"));

    (void)HtmlDocumentDestroy(id, NULL);
    TvFixtureEnd();
}

static void TestTvParserAttributesReachable(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle div;
    HtmlNodeHandle out;
    HtmlAttribute attr;
    TvLog log;
    size_t count = 0u;
    size_t attrCount = 0u;

    TvFixtureBegin();

    id = TvDoc();
    TvBuild(id, "<div id=\"main\" class=\"container\">Hello</div>");
    div = TvFind(id, "div");
    RoboCheck(HtmlElementIsElement(&div));

    /* Traversal hands back a handle on which the attribute model still
     * works: nothing is duplicated into traversal-specific storage. */
    RoboCheckEqInt(HtmlNodeFirstChild(&div, &out, NULL), RoboOk);
    RoboCheck(HtmlTextIsText(&out));
    RoboCheckStrEq(HtmlTextContent(&out, NULL), "Hello");
    RoboCheck(HtmlElementHasAttribute(&div, "id"));
    RoboCheck(HtmlElementHasAttribute(&div, "class"));
    RoboCheckEqInt(HtmlElementAttributeCount(&div, &attrCount,
                                                        NULL),
                      RoboOk);
    RoboCheckEqInt((int)attrCount, 2);

    RoboCheckEqInt(HtmlElementAttributeAt(&div, 0u, &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.name, "id");
    RoboCheckStrEq(attr.value, "main");
    RoboCheckEqInt(HtmlElementAttributeAt(&div, 1u, &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.name, "class");
    RoboCheckStrEq(attr.value, "container");

    /* An element found through traversal has the same children a walk sees. */
    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkDescendants(
                          &div, RoboHtmlNodeFilterAll, TvVisit, &log,
                          NULL),
                      RoboOk);
    RoboCheckEqInt((int)log.count, 1);
    RoboCheckEqInt(HtmlNodeChildCount(&div, &count, NULL), RoboOk);
    RoboCheckEqInt((int)count, 1);

    (void)HtmlDocumentDestroy(id, NULL);
    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Unicode                                                             */
/* ------------------------------------------------------------------ */

static void TestTvUnicode(void)
{
    static const char *const runs[] = {
        "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7", /* Arabic */
        "\xd9\x81\xd8\xa7\xd8\xb1\xd8\xb3\xdb\x8c", /* Persian */
        "\xd8\xa7\xd8\xb1\xd8\xaf\xd9\x88",         /* Urdu */
        "\xe0\xa4\xb9\xe0\xa4\xbf\xe0\xa4\xa8\xe0\xa5\x8d\xe0\xa4\xa6\xe0\xa5\x80",
        "\xe0\xa6\xac\xe0\xa6\xbe\xe0\xa6\x82\xe0\xa6\xb2\xe0\xa6\xbe",
        "\xe4\xb8\xad\xe6\x96\x87",       /* Chinese */
        "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", /* Japanese */
        "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4", /* Korean */
        "\xf0\x9f\x98\x80\xf0\x9f\x9a\x80",       /* emoji */
        "  padded  "
    };
    const size_t runCount = sizeof(runs) / sizeof(runs[0]);
    HtmlDocumentId id;
    HtmlNodeHandle root;
    TvLog log;
    size_t i;

    TvFixtureBegin();

    /* Each run in its own element, then walked back out byte-for-byte. */
    id = TvDoc();
    root = TvRoot(id);
    for (i = 0u; i < runCount; i++) {
        HtmlNodeHandle parent = TvElement(id, &root, "span");
        HtmlNodeHandle text = TvText(id, &parent, runs[i]);
        HtmlNodeHandle back;

        RoboCheckEqInt(HtmlNodeFirstChild(&parent, &back, NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&back, &text));
        RoboCheckStrEq(HtmlTextContent(&back, NULL), runs[i]);
        RoboCheck(HtmlNodeMatchesFilter(&back,
                                                 RoboHtmlNodeFilterText));
    }
    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkDescendants(
                          &root, RoboHtmlNodeFilterText, TvVisit, &log,
                          NULL),
                      RoboOk);
    RoboCheckEqInt((int)log.count, (int)runCount);
    RoboCheckEqInt((int)log.recorded, (int)runCount);
    for (i = 0u; i < runCount; i++) {
        RoboCheckStrEq(log.texts[i], runs[i]);
    }
    (void)HtmlDocumentDestroy(id, NULL);

    /* The same bytes through the parser, reached by traversal. */
    id = TvDoc();
    TvBuild(id, "<div><p>Hello \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7"
                 "</p></div>");
    {
        HtmlNodeHandle paragraph = TvFind(id, "p");
        char text[64];

        RoboCheck(HtmlElementIsElement(&paragraph));
        RoboCheckEqInt(HtmlNodeSubtreeText(&paragraph, text,
                                                      sizeof(text), NULL, NULL),
                          RoboOk);
        RoboCheckStrEq(text,
                          "Hello \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7");
    }
    (void)HtmlDocumentDestroy(id, NULL);

    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Deep and empty trees                                                */
/* ------------------------------------------------------------------ */

static void TestTvDeepTree(void)
{
    enum { DEPTH = 1000 };
    HtmlDocumentId id;
    HtmlNodeHandle parent;
    HtmlNodeHandle leaf;
    HtmlNodeHandle up;
    HtmlNodeHandle next;
    HtmlNodeHandle root;
    TvCounter counter;
    size_t depth = 0u;
    size_t steps = 0u;
    int i;

    TvFixtureBegin();

    id = TvDoc();
    parent = TvRoot(id);
    for (i = 0; i < DEPTH; i++) {
        parent = TvElement(id, &parent, "div");
    }
    leaf = TvText(id, &parent, "bottom");
    root = TvRoot(id);

    counter.count = 0u;
    counter.maxDepth = 0u;
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &parent, RoboHtmlNodeFilterAll, TvCountVisit,
                          &counter, NULL),
                      RoboOk);
    RoboCheckEqInt((int)counter.count, 2);
    /* Depth is absolute, not relative to the walk's start. */
    RoboCheckEqInt((int)counter.maxDepth, DEPTH + 1);

    RoboCheckEqInt(HtmlNodeDepth(&leaf, &depth, NULL), RoboOk);
    RoboCheckEqInt((int)depth, DEPTH + 1);

    /* Parent traversal reaches the document node and terminates. */
    up = leaf;
    while (HtmlNodeParent(&up, &next, NULL) == RoboOk) {
        up = next;
        steps++;
    }
    RoboCheckEqInt((int)steps, DEPTH + 1);
    RoboCheck(HtmlNodeHandleEqual(&up, &root));

    /* The whole document walks in one bounded pass — 1000 levels of nesting
     * is a stack-overflow vector for a recursive walk and a non-event here. */
    counter.count = 0u;
    counter.maxDepth = 0u;
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &root, RoboHtmlNodeFilterAll, TvCountVisit,
                          &counter, NULL),
                      RoboOk);
    RoboCheckEqInt((int)counter.count, DEPTH + 2);
    RoboCheckEqInt((int)counter.maxDepth, DEPTH + 1);

    (void)HtmlDocumentDestroy(id, NULL);
    TvFixtureEnd();
}

static void TestTvEmptyDocument(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle out;
    HtmlChildCursor cursor;
    TvLog log;
    size_t depth = 99u;

    TvFixtureBegin();

    /* A document with nothing in it traverses as a single node. */
    id = TvDoc();
    root = TvRoot(id);

    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &root, RoboHtmlNodeFilterAll, TvVisit, &log,
                          NULL),
                      RoboOk);
    RoboCheckEqInt((int)log.count, 1);
    RoboCheckEqInt((int)log.kinds[0], RoboHtmlNodeKindDocument);

    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkDescendants(
                          &root, RoboHtmlNodeFilterAll, TvVisit, &log,
                          NULL),
                      RoboOk);
    RoboCheckEqInt((int)log.count, 0);

    RoboCheckEqInt(HtmlNodeFirstChild(&root, &out, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlNodeLastChild(&root, &out, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlChildCursorOpen(&cursor, &root, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlChildCursorNext(&cursor, &out, NULL),
                      RoboNotFound);

    /* A childless document is still its own root at depth 0. */
    RoboCheckEqInt(HtmlNodeRoot(&root, &out, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&out, &root));
    RoboCheckEqInt(HtmlNodeDepth(&root, &depth, NULL), RoboOk);
    RoboCheckEqInt((int)depth, 0);

    /* Empty input still parses to a traversable document. */
    (void)HtmlDocumentDestroy(id, NULL);
    id = TvDoc();
    TvBuild(id, "");
    root = TvRoot(id);
    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &root, RoboHtmlNodeFilterAll, TvVisit, &log,
                          NULL),
                      RoboOk);
    RoboCheck((int)log.count >= 1);
    RoboCheckEqInt((int)log.kinds[0], RoboHtmlNodeKindDocument);

    (void)HtmlDocumentDestroy(id, NULL);
    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Defensive input                                                     */
/* ------------------------------------------------------------------ */

static void TestTvDefensive(void)
{
    TvTree tree;
    HtmlNodeHandle out;
    HtmlDiag diag;
    HtmlNodeHandle stale;
    size_t depth = 0u;
    TvLog log;

    TvFixtureBegin();
    TvTreeBuild(&tree);
    TvLogInit(&log);

    /* NULL arguments are argument errors. */
    RoboCheckEqInt(HtmlNodeParent(NULL, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeFirstChild(&tree.body, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeLastChild(NULL, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeNextSibling(NULL, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodePrevSibling(NULL, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeDepth(&tree.body, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeDepth(NULL, &depth, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeRoot(&tree.body, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeRoot(NULL, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          NULL, RoboHtmlNodeFilterAll, TvVisit, &log,
                          NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &tree.body, RoboHtmlNodeFilterAll, NULL, &log,
                          NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &tree.body, (HtmlNodeFilter)99, TvVisit,
                          &log, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlNodeFindFirst(
                          &tree.body, RoboHtmlNodeFilterAll, NULL, NULL,
                          NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)log.count, 0);

    /* A cleared handle is a lookup that missed, not misuse. */
    HtmlNodeHandleClear(&stale);
    HtmlDiagReset(&diag);
    RoboCheckEqInt(HtmlNodeParent(&stale, &out, &diag),
                      RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainTraversal);
    RoboCheck(diag.message != NULL);
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &stale, RoboHtmlNodeFilterAll, TvVisit, &log,
                          NULL),
                      RoboNotFound);
    RoboCheckEqInt((int)log.count, 0);

    /* A stale handle from a destroyed document resolves to nothing. */
    (void)HtmlDocumentDestroy(tree.id, NULL);
    RoboCheckEqInt(HtmlNodeParent(&tree.body, &out, NULL),
                      RoboNotFound);
    RoboCheckEqInt(
        HtmlNodeWalkPreOrder(&tree.document, RoboHtmlNodeFilterAll,
                                      TvVisit, &log, NULL),
        RoboNotFound);
    RoboCheckEqInt((int)log.count, 0);
    RoboCheckEqInt(HtmlNodeDepth(&tree.document, &depth, NULL),
                      RoboNotFound);

    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Visitor control                                                     */
/* ------------------------------------------------------------------ */

static void TestTvVisitorControlsWalk(void)
{
    TvTree tree;
    TvLog log;

    TvFixtureBegin();
    TvTreeBuild(&tree);

    /* A visitor that stops early turns its own code into the walk's result. */
    TvLogInit(&log);
    log.stopAfter = 3u;
    log.stopCode = RoboBusy;
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &tree.html, RoboHtmlNodeFilterAll, TvVisit,
                          &log, NULL),
                      RoboBusy);
    RoboCheckEqInt((int)log.count, 3);

    /* A visitor that refuses aborts with its own error code. */
    TvLogInit(&log);
    log.stopAfter = 1u;
    log.stopCode = RoboError;
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &tree.html, RoboHtmlNodeFilterAll, TvVisit,
                          &log, NULL),
                      RoboError);

    /* A visitor may walk reentrantly: the walker holds no document state. */
    {
        TvLog inner;
        HtmlNodeHandle span = tree.span;

        TvLogInit(&inner);
        RoboCheckEqInt(HtmlNodeWalkDescendants(
                              &span, RoboHtmlNodeFilterText, TvVisit,
                              &inner, NULL),
                          RoboOk);
        RoboCheckEqInt((int)inner.count, 1);
        RoboCheckStrEq(inner.texts[0], "Runtime");
    }

    /* Destroying the document from inside a visitor is reported, not
     * dereferenced. */
    TvLogInit(&log);
    log.destroyDocument = 1;
    RoboCheckEqInt(HtmlNodeWalkPreOrder(
                          &tree.html, RoboHtmlNodeFilterAll, TvVisit,
                          &log, NULL),
                      RoboNotFound);

    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Debug dump                                                          */
/* ------------------------------------------------------------------ */

static Result TvCollectSink(const char *text, size_t length,
                                     void *user)
{
    char *out = (char *)user;
    size_t used = strlen(out);

    if (used + length < 8192u) {
        memcpy(out + used, text, length);
        out[used + length] = '\0';
    }
    return RoboOk;
}

static void TestTvTreeDumpOnWalker(void)
{
    HtmlDocumentId id;
    static char buffer[8192];
    size_t length = 0u;

    TvFixtureBegin();

    /* Newlines in the source become whitespace text nodes, which is the
     * case the dump's quoting and labelling exist for. */
    id = TvDoc();
    TvBuild(id, "<!DOCTYPE html>\n<html>\n<head>\n"
                 "<title>RoboticelaOS Runtime</title>\n</head>\n<body>\n"
                 "<h1>Heading</h1>\n<p>Body text</p>\n"
                 "<div id=\"main\"><span>deep</span></div>\n"
                 "</body>\n</html>\n");

    buffer[0] = '\0';
    RoboCheckEqInt(HtmlTreeDump(id, TvCollectSink, buffer, NULL),
                      RoboOk);

    /* The walker supplies the order and the depth, so nesting is indented
     * and every parent appears before its children. */
    /*
     * The root line carries the document's own lifecycle state, read from the
     * document rather than assumed (Phase 8.10). This build consumed a
     * complete source, so the document is done.
     */
    RoboCheck(strstr(buffer, "Document lifecycle=complete\n") != NULL);
    RoboCheck(strstr(buffer, "doctype html\n") != NULL);
    RoboCheck(strstr(buffer, "  html\n") != NULL);
    RoboCheck(strstr(buffer, "    head\n") != NULL);
    RoboCheck(strstr(buffer, "      title\n") != NULL);
    RoboCheck(strstr(buffer, "        Text: \"RoboticelaOS Runtime\"\n") !=
               NULL);
    RoboCheck(strstr(buffer, "    body\n") != NULL);
    RoboCheck(strstr(buffer, "      h1\n") != NULL);
    RoboCheck(strstr(buffer, "        Text: \"Heading\"\n") != NULL);
    RoboCheck(strstr(buffer, "        Text: \"Body text\"\n") != NULL);
    RoboCheck(strstr(buffer, "      div id=\"main\"\n") != NULL);
    RoboCheck(strstr(buffer, "        span\n") != NULL);
    RoboCheck(strstr(buffer, "          Text: \"deep\"\n") != NULL);
    /* A newline-only run is escaped, so it is legible rather than silent. */
    RoboCheck(strstr(buffer, "Text: \"\\n\"") != NULL);

    {
        const char *document = strstr(buffer, "Document lifecycle=complete");
        const char *div = strstr(buffer, "div id=\"main\"");
        const char *deep = strstr(buffer, "Text: \"deep\"");

        RoboCheck(document != NULL);
        RoboCheck(div != NULL);
        RoboCheck(deep != NULL);
        RoboCheck(document < div);
        RoboCheck(div < deep);
    }

    /* Buffer variant still reports the untruncated length. */
    {
        char small[24];

        RoboCheckEqInt(HtmlTreeDumpToBuffer(id, small,
                                                        sizeof(small), &length,
                                                        NULL),
                          RoboOk);
        RoboCheck(length > sizeof(small));
        RoboCheckEqInt((int)strlen(small), (int)sizeof(small) - 1);
    }

    (void)HtmlDocumentDestroy(id, NULL);

    /* The same dump for an explicitly built tree shows the section-33 shape,
     * proving the dump reads the tree, not the parser's bookkeeping. */
    {
        TvTree tree;

        TvTreeBuild(&tree);
        buffer[0] = '\0';
        RoboCheckEqInt(
            HtmlTreeDump(tree.id, TvCollectSink, buffer, NULL),
            RoboOk);
        /* Document 0, html 1, body 2, text/div 3, span/text 4, text 5. */
        RoboCheck(strstr(buffer, "  html\n") != NULL);
        RoboCheck(strstr(buffer, "    body\n") != NULL);
        RoboCheck(strstr(buffer, "      Text: \"Hello \"\n") != NULL);
        RoboCheck(strstr(buffer, "      div\n") != NULL);
        RoboCheck(strstr(buffer, "        Text: \"Roboticela \"\n") != NULL);
        RoboCheck(strstr(buffer, "        span\n") != NULL);
        RoboCheck(strstr(buffer, "          Text: \"Runtime\"\n") != NULL);
        RoboCheck(strstr(buffer, "      Text: \"!\"\n") != NULL);
        (void)HtmlDocumentDestroy(tree.id, NULL);
    }

    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Capability + diagnostics                                            */
/* ------------------------------------------------------------------ */

static void TestTvCapabilitiesAndDiag(void)
{
    HtmlDomCapabilities caps;
    HtmlDiag diag;
    HtmlNodeHandle stale;
    HtmlNodeHandle out;

    TvFixtureBegin();

    RoboCheckEqInt(HtmlDomQueryCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.traversal, 1);
    RoboCheckEqInt(caps.treeBuilding, 1);
    /*
     * Phase 8.9 added the mutation API, so the tree can now be changed —
     * which is exactly why the traversal above snapshots the tree version
     * rather than assuming it reads a frozen tree. Events are still later
     * phase work.
     */
    RoboCheckEqInt(caps.mutation, 1);
    RoboCheckEqInt(caps.events, 0);

    /* The traversal domain is a defined, documented one. */
    RoboCheckEqInt(
        HtmlDiagnosticDomainIsKnown(RoboHtmlDomainTraversal), 1);
    RoboCheckStrEq(
        HtmlDiagnosticDomainName(RoboHtmlDomainTraversal),
        "traversal");

    /* A failure records it. */
    HtmlNodeHandleClear(&stale);
    HtmlDiagReset(&diag);
    RoboCheckEqInt(HtmlNodeRoot(&stale, &out, &diag), RoboNotFound);
    RoboCheckEqInt((int)diag.code, (int)RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboHtmlDomainTraversal);
    RoboCheck(diag.message != NULL);

    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* The document of record                                              */
/* ------------------------------------------------------------------ */

static void TestTvCheckpointDocument(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle body;
    HtmlNodeHandle first;
    HtmlNodeHandle last;
    TvLog log;

    TvFixtureBegin();

    id = TvDoc();
    TvBuild(id, DomCheckpointHtml());

    body = TvFind(id, "body");
    RoboCheck(HtmlElementIsElement(&body));

    /* Every element kind the checkpoint document uses is reachable through traversal. */
    TvLogInit(&log);
    RoboCheckEqInt(HtmlNodeWalkDescendants(
                          &body, RoboHtmlNodeFilterElements, TvVisit,
                          &log, NULL),
                      RoboOk);
    RoboCheck(TvIndexOfName(&log, "h1") >= 0);
    RoboCheck(TvIndexOfName(&log, "p") >= 0);
    RoboCheck(TvIndexOfName(&log, "button") >= 0);
    RoboCheck(TvIndexOfName(&log, "h1") < TvIndexOfName(&log, "button"));

    /* The text each block paints is a text node, reachable from the block. */
    {
        HtmlNodeHandle heading = TvFind(id, "h1");
        HtmlNodeHandle text;

        RoboCheckEqInt(HtmlNodeFirstChild(&heading, &text, NULL),
                          RoboOk);
        RoboCheck(HtmlTextIsText(&text));
        RoboCheckStrEq(HtmlTextContent(&text, NULL),
                          "RoboticelaOS Runtime");
    }

    RoboCheckEqInt(HtmlNodeFirstChild(&body, &first, NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlNodeLastChild(&body, &last, NULL), RoboOk);
    RoboCheck(!HtmlNodeHandleEqual(&first, &last));
    /* The body's first node is the leading newline; its last is the closing
     * newline. Neither end has an outside neighbour. */
    {
        HtmlNodeHandle neighbour;

        RoboCheckEqInt(HtmlNodePrevSibling(&first, &neighbour, NULL),
                          RoboNotFound);
        RoboCheckEqInt(HtmlNodeNextSibling(&last, &neighbour, NULL),
                          RoboNotFound);
    }

    (void)HtmlDocumentDestroy(id, NULL);
    TvFixtureEnd();
}

/* ------------------------------------------------------------------ */

void TestRegisterHtmlTraversal(void)
{
    TestRegister("html.traverse.child_relationships",
                       TestTvChildRelationships);
    TestRegister("html.traverse.parent_and_root",
                       TestTvParentAndRoot);
    TestRegister("html.traverse.document_order", TestTvDocumentOrder);
    TestRegister("html.traverse.pre_order_from_document",
                       TestTvPreOrderFromDocument);
    TestRegister("html.traverse.element_filter", TestTvElementFilter);
    TestRegister("html.traverse.text_filter", TestTvTextFilter);
    TestRegister("html.traverse.filter_api", TestTvFilterApi);
    TestRegister("html.traverse.child_cursor", TestTvChildCursor);
    TestRegister("html.traverse.cursor_after_destroy",
                       TestTvCursorAfterDestroy);
    TestRegister("html.traverse.find", TestTvFind);
    TestRegister("html.traverse.parser_document_order",
                       TestTvParserDocumentOrder);
    TestRegister("html.traverse.parser_attributes_reachable",
                       TestTvParserAttributesReachable);
    TestRegister("html.traverse.unicode", TestTvUnicode);
    TestRegister("html.traverse.deep_tree", TestTvDeepTree);
    TestRegister("html.traverse.empty_document",
                       TestTvEmptyDocument);
    TestRegister("html.traverse.defensive", TestTvDefensive);
    TestRegister("html.traverse.visitor_controls_walk",
                       TestTvVisitorControlsWalk);
    TestRegister("html.traverse.tree_dump_on_walker",
                       TestTvTreeDumpOnWalker);
    TestRegister("html.traverse.capabilities_and_diag",
                       TestTvCapabilitiesAndDiag);
    TestRegister("html.traverse.checkpoint_document", TestTvCheckpointDocument);
}
