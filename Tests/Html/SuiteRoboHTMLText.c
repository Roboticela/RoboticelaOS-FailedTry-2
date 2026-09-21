/*
 * RoboHTML Phase 8.7 text-node suite:
 * creation through the model seam, kind identification, content access,
 * set/append mutation, document ownership and cross-document rejection,
 * parser integration (runs, merging, character references), whitespace and
 * Unicode preservation, traversal, teardown, malformed recovery, the tree
 * dump, and the visual demo document's text.
 *
 * Note on child indices: the tree builder keeps whitespace between tags as
 * text nodes (the 8.3 suite documents that), so a child index is a real
 * index into the child list. Tests that care about *which* children exist
 * walk the list and classify by kind rather than assuming a layout.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLText.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboTest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void TxFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void TxFixtureEnd(void)
{
    HtmlShutdown();
}

static HtmlDocumentId TxDoc(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

/** Build @p text into @p id with the one-shot API; result optional. */
static void TxBuild(HtmlDocumentId id,
                     const char *text,
                     HtmlTreeResult *out)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, out, NULL), RoboOk);
}

static HtmlNodeHandle TxRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

/** Handle of the first descendant element named @p name (invalid when none). */
static HtmlNodeHandle TxFind(HtmlDocumentId id,
                                       const char *name)
{
    HtmlNodeHandle root = TxRoot(id);
    HtmlNodeHandle found;

    HtmlNodeHandleClear(&found);
    if (HtmlNodeFindElement(&root, name, &found, NULL) != RoboOk) {
        HtmlNodeHandleClear(&found);
    }
    return found;
}

/** Child @p index (any kind); invalid when out of range. */
static HtmlNodeHandle TxChild(const HtmlNodeHandle *parent,
                                        size_t index)
{
    HtmlNodeHandle out;

    HtmlNodeHandleClear(&out);
    if (HtmlNodeChildAt(parent, index, &out, NULL) != RoboOk) {
        HtmlNodeHandleClear(&out);
    }
    return out;
}

/** Direct child count; 0 when the handle is not live. */
static size_t TxChildCount(const HtmlNodeHandle *parent)
{
    size_t count = 0u;

    if (HtmlNodeChildCount(parent, &count, NULL) != RoboOk) {
        return 0u;
    }
    return count;
}

/** Concatenated subtree text into @p buf ("" when the call fails). */
static void TxText(const HtmlNodeHandle *h, char *buf, size_t cap)
{
    size_t length = 0u;

    buf[0] = '\0';
    if (HtmlNodeSubtreeText(h, buf, cap, &length, NULL) != RoboOk) {
        buf[0] = '\0';
    }
}

/** Subtree text of the first descendant element named @p name. */
static void TxTextOf(HtmlDocumentId id,
                       const char *name,
                       char *buf,
                       size_t cap)
{
    HtmlNodeHandle h = TxFind(id, name);

    TxText(&h, buf, cap);
}

/** Child @p index of @p parent as text bytes ("" when it is not text). */
static void TxChildText(const HtmlNodeHandle *parent,
                          size_t index,
                          char *buf,
                          size_t cap)
{
    HtmlNodeHandle child = TxChild(parent, index);
    size_t length = 0u;
    const char *content = HtmlTextContent(&child, &length);

    buf[0] = '\0';
    if (!content || length >= cap) {
        return;
    }
    memcpy(buf, content, length);
    buf[length] = '\0';
}

/** Index of the @p nth text child of @p parent; -1 when there is none. */
static long TxTextChildIndex(const HtmlNodeHandle *parent, size_t n)
{
    size_t children = TxChildCount(parent);
    size_t seen = 0u;
    size_t i;

    for (i = 0u; i < children; i++) {
        HtmlNodeHandle child = TxChild(parent, i);

        if (!HtmlTextIsText(&child)) {
            continue;
        }
        if (seen == n) {
            return (long)i;
        }
        seen++;
    }
    return -1;
}

/** Count of direct text children of @p parent. */
static size_t TxTextChildCount(const HtmlNodeHandle *parent)
{
    size_t children = TxChildCount(parent);
    size_t texts = 0u;
    size_t i;

    for (i = 0u; i < children; i++) {
        HtmlNodeHandle child = TxChild(parent, i);

        if (HtmlTextIsText(&child)) {
            texts++;
        }
    }
    return texts;
}

/** Create an element under @p parent; invalid handle on failure. */
static HtmlNodeHandle TxCreateElement(
    HtmlDocumentId id,
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
    if (HtmlElementCreate(id, &info, &out, NULL) != RoboOk) {
        HtmlNodeHandleClear(&out);
    }
    return out;
}

/** Create a text node under @p parent via the model seam. */
static HtmlNodeHandle TxCreateText(
    HtmlDocumentId id,
    const HtmlNodeHandle *parent,
    const char *content,
    size_t length,
    HtmlNodeOrigin origin,
    Result *outCode)
{
    HtmlTextCreateInfo info;
    HtmlNodeHandle out;
    Result r;

    HtmlTextCreateInfoInit(&info);
    info.parent = *parent;
    info.origin = origin;
    info.content = content;
    info.length = length;

    HtmlNodeHandleClear(&out);
    r = HtmlTextCreate(id, &info, &out, NULL);
    if (outCode) {
        *outCode = r;
    }
    if (r != RoboOk) {
        HtmlNodeHandleClear(&out);
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

static void TestTxCreateInfoInit(void)
{
    HtmlTextCreateInfo info;

    memset(&info, 0xA5, sizeof(info));
    HtmlTextCreateInfoInit(&info);
    RoboCheck(!HtmlNodeHandleIsWellformed(&info.parent));
    RoboCheckEqInt((int)info.origin, (int)RoboHtmlNodeOriginNone);
    RoboCheck(info.content == NULL);
    RoboCheckEqInt((int)info.length, 0);

    /* Safe on NULL. */
    HtmlTextCreateInfoInit(NULL);
}

static void TestTxCreateBasic(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle div;
    HtmlNodeHandle text;
    HtmlTextView view;
    HtmlNodeHandle back;
    size_t length = 0u;
    const char *content;

    TxFixtureBegin();
    id = TxDoc();

    /* A document with no build yet: the root exists, so a parent does too. */
    root = TxRoot(id);
    div = TxCreateElement(id, &root, "div");
    RoboCheck(HtmlElementIsElement(&div));

    text = TxCreateText(id, &div, "Hello", 5u, RoboHtmlNodeOriginTest,
                          NULL);
    RoboCheck(HtmlTextIsText(&text));
    RoboCheck(!HtmlElementIsElement(&text));

    /* Identity: it is a real, live, resolvable node of this document. */
    RoboCheck(!HtmlNodeHandleIsStale(&text));
    RoboCheckEqInt((int)HtmlNodeHandleDocument(&text), (int)id);
    RoboCheck(HtmlNodeHandleEqual(&text, &text));

    content = HtmlTextContent(&text, &length);
    RoboCheck(content != NULL);
    RoboCheckEqInt((int)length, 5);
    RoboCheckStrEq(content, "Hello");
    RoboCheckEqInt((int)HtmlTextLength(&text), 5);
    RoboCheckEqInt(HtmlTextIsEmpty(&text), 0);

    RoboCheckEqInt(HtmlTextViewGet(&text, &view, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&view.handle, &text));
    RoboCheckEqInt((int)view.length, 5);
    RoboCheckStrEq(view.content, "Hello");
    RoboCheck(HtmlNodeHandleEqual(&view.parent, &div));
    /* Created by the DOM seam, not the parser. */
    RoboCheckEqInt((int)(view.state & RoboHtmlTextStateParserCreated),
                      0);
    RoboCheck((view.state & RoboHtmlTextStateConnected) != 0u);

    /* It is attached: last child of its parent. */
    RoboCheckEqInt((int)TxChildCount(&div), 1);
    back = TxChild(&div, 0u);
    RoboCheck(HtmlNodeHandleEqual(&back, &text));
    RoboCheckEqInt(HtmlTextParent(&text, &back, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&back, &div));

    RoboCheckEqInt(HtmlTextParent(&root, &back, NULL),
                      RoboNotFound);

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

static void TestTxCreateEmpty(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle div;
    HtmlNodeHandle empty;
    size_t length = 123u;
    const char *content;

    TxFixtureBegin();
    id = TxDoc();
    root = TxRoot(id);
    div = TxCreateElement(id, &root, "div");

    /*
     * The model permits an empty text node (a zero-length run is a legal
     * value); only the parser declines to produce one, because a zero-length
     * character run carries nothing.
     */
    empty = TxCreateText(id, &div, NULL, 0u, RoboHtmlNodeOriginTest,
                           NULL);
    RoboCheck(HtmlTextIsText(&empty));
    content = HtmlTextContent(&empty, &length);
    /* "" with length 0, never NULL: present-and-empty, not absent. */
    RoboCheck(content != NULL);
    RoboCheckEqInt((int)length, 0);
    RoboCheckStrEq(content, "");
    RoboCheckEqInt((int)HtmlTextLength(&empty), 0);
    RoboCheckEqInt(HtmlTextIsEmpty(&empty), 1);
    RoboCheckEqInt((int)TxChildCount(&div), 1);

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

static void TestTxCreateValidation(void)
{
    HtmlDocumentId id;
    HtmlDocumentId other;
    HtmlNodeHandle root;
    HtmlNodeHandle div;
    HtmlNodeHandle out;
    HtmlNodeHandle stranger;
    HtmlTextCreateInfo info;
    Result code = RoboOk;

    TxFixtureBegin();
    id = TxDoc();
    other = TxDoc();
    root = TxRoot(id);
    div = TxCreateElement(id, &root, "div");
    HtmlTextCreateInfoInit(&info);

    /* NULL arguments. */
    RoboCheckEqInt(HtmlTextCreate(id, NULL, &out, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlTextCreate(id, &info, NULL, NULL),
                      RoboInvalidArgument);

    /* NULL content with a non-zero length. */
    info.parent = div;
    info.origin = RoboHtmlNodeOriginTest;
    info.content = NULL;
    info.length = 3u;
    RoboCheckEqInt(HtmlTextCreate(id, &info, &out, NULL),
                      RoboInvalidArgument);

    /* Origin NONE and an undefined origin are both refused. */
    info.content = "abc";
    info.origin = RoboHtmlNodeOriginNone;
    RoboCheckEqInt(HtmlTextCreate(id, &info, &out, NULL),
                      RoboInvalidArgument);
    info.origin = (HtmlNodeOrigin)77;
    RoboCheckEqInt(HtmlTextCreate(id, &info, &out, NULL),
                      RoboInvalidArgument);

    /* Ill-formed parent handle. */
    info.origin = RoboHtmlNodeOriginTest;
    HtmlNodeHandleClear(&info.parent);
    RoboCheckEqInt(HtmlTextCreate(id, &info, &out, NULL),
                      RoboInvalidArgument);

    /* Unknown document. */
    info.parent = div;
    RoboCheckEqInt(
        HtmlTextCreate(RoboHtmlDocumentIdNone, &info, &out, NULL),
        RoboNotFound);

    /*
     * A parent that belongs to another document is refused before anything is
     * resolved — the whole reason the handle records its owning document.
     */
    {
        HtmlNodeHandle otherRoot = TxRoot(other);
        HtmlNodeHandle otherDiv =
            TxCreateElement(other, &otherRoot, "div");

        info.parent = otherDiv;
        RoboCheckEqInt(HtmlTextCreate(id, &info, &out, NULL),
                          RoboInvalidArgument);
        RoboCheck(!HtmlTextIsText(&out) || out.nodeId == 0u);
    }

    /* A handle that claims this document but names no live node. */
    stranger.documentId = id;
    stranger.nodeId = 9999u;
    stranger.generation = 1u;
    info.parent = stranger;
    RoboCheckEqInt(HtmlTextCreate(id, &info, &out, NULL),
                      RoboNotFound);

    /* Every rejection left @p out cleared and created nothing. */
    RoboCheckEqInt((int)TxChildCount(&div), 0);

    code = RoboOk;
    info.parent = div;
    {
        HtmlNodeHandle ok =
            TxCreateText(id, &div, "ok", 2u, RoboHtmlNodeOriginTest,
                           &code);

        RoboCheck(HtmlTextIsText(&ok));
    }
    RoboCheckEqInt((int)code, (int)RoboOk);

    (void)HtmlDocumentDestroy(id, NULL);
    (void)HtmlDocumentDestroy(other, NULL);
    TxFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Kind identification and the not-text answers                        */
/* ------------------------------------------------------------------ */

static void TestTxKindIdentification(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle paragraph;
    HtmlNodeHandle text;
    HtmlNodeHandle comment = {0u, 0u, 0u};
    HtmlNodeHandle first;
    HtmlNodeView view;
    HtmlNodeKind kind = RoboHtmlNodeKindDocument;

    TxFixtureBegin();
    id = TxDoc();
    TxBuild(id, "<!DOCTYPE html><html><body><p>Hello<!--x--></p></body>"
                 "</html>",
             NULL);

    paragraph = TxFind(id, "p");
    RoboCheck(HtmlElementIsElement(&paragraph));
    RoboCheck(!HtmlTextIsText(&paragraph));

    /* The <p> has a text child and a comment child. */
    RoboCheckEqInt((int)TxChildCount(&paragraph), 2);
    first = TxChild(&paragraph, 0u);
    RoboCheck(HtmlTextIsText(&first));
    RoboCheck(!HtmlElementIsElement(&first));
    RoboCheckEqInt(HtmlNodeGetView(&first, &view, NULL), RoboOk);
    RoboCheckEqInt(view.kind, RoboHtmlNodeKindText);

    comment = TxChild(&paragraph, 1u);
    RoboCheckEqInt(HtmlNodeGetView(&comment, &view, NULL), RoboOk);
    RoboCheckEqInt(view.kind, RoboHtmlNodeKindComment);
    /* A comment is not text, so the text model refuses it. */
    RoboCheck(!HtmlTextIsText(&comment));

    /* The document node and the root element are not text either. */
    root = TxRoot(id);
    RoboCheck(!HtmlTextIsText(&root));
    RoboCheck(!HtmlTextIsText(NULL));

    /* Role via the node handle, not a string comparison. */
    text = first;
    RoboCheckEqInt(HtmlNodeHandleResolve(&text, &kind), RoboOk);
    RoboCheckEqInt(kind, RoboHtmlNodeKindText);
    RoboCheckEqInt(HtmlNodeHandleResolve(&paragraph, &kind),
                      RoboOk);
    RoboCheckEqInt(kind, RoboHtmlNodeKindElement);

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

static void TestTxNotTextAnswers(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle paragraph;
    HtmlNodeHandle out;
    HtmlTextView view;
    size_t length = 99u;

    TxFixtureBegin();
    id = TxDoc();
    TxBuild(id, "<html><body><p>Hello</p></body></html>", NULL);
    root = TxRoot(id);
    paragraph = TxFind(id, "p");

    /* Content is NULL — "not a text node", distinct from "" (empty run). */
    RoboCheck(HtmlTextContent(&paragraph, &length) == NULL);
    RoboCheckEqInt((int)length, 0);
    RoboCheckEqInt((int)HtmlTextLength(&paragraph), 0);
    RoboCheckEqInt(HtmlTextIsEmpty(&paragraph), 0);
    RoboCheck(HtmlTextContent(NULL, &length) == NULL);
    RoboCheck(HtmlTextContent(&root, NULL) == NULL);

    /* Structural accessors report the misuse rather than guessing. */
    RoboCheckEqInt(HtmlTextViewGet(&paragraph, &view, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlTextViewGet(&paragraph, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlTextViewGet(NULL, &view, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlTextViewGet(&root, &view, NULL),
                      RoboNotFound);

    /* Mutation on a non-text node is refused, not applied to the parent. */
    RoboCheckEqInt(HtmlTextSet(&paragraph, "x", 1u, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlTextAppend(&paragraph, "x", 1u, NULL),
                      RoboNotFound);

    /* Ill-formed handles are argument errors, not lookups that missed. */
    HtmlNodeHandleClear(&out);
    /* A well-formed handle that names nothing live is a miss, not misuse. */
    RoboCheckEqInt(HtmlTextSet(&out, "x", 1u, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlTextAppend(&out, "x", 1u, NULL),
                      RoboNotFound);
    /* A NULL handle is misuse. */
    RoboCheckEqInt(HtmlTextSet(NULL, "x", 1u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlTextAppend(NULL, "x", 1u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlTextParent(&out, &paragraph, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlTextParent(&paragraph, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheck(!HtmlTextIsText(&out));

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Mutation                                                            */
/* ------------------------------------------------------------------ */

static void TestTxSet(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle div;
    HtmlNodeHandle text;
    HtmlNodeHandle sibling;
    HtmlTextView before;
    HtmlTextView after;
    size_t length = 0u;
    const char *content;

    TxFixtureBegin();
    id = TxDoc();
    root = TxRoot(id);
    div = TxCreateElement(id, &root, "div");
    text = TxCreateText(id, &div, "first", 5u, RoboHtmlNodeOriginTest,
                          NULL);
    RoboCheckEqInt(HtmlTextViewGet(&text, &before, NULL), RoboOk);

    /* Replace with something longer. */
    RoboCheckEqInt(HtmlTextSet(&text, "a longer run", 12u, NULL),
                      RoboOk);
    content = HtmlTextContent(&text, &length);
    RoboCheck(content != NULL);
    RoboCheckEqInt((int)length, 12);
    RoboCheckStrEq(content, "a longer run");

    /* Replace with something shorter. */
    RoboCheckEqInt(HtmlTextSet(&text, "s", 1u, NULL), RoboOk);
    RoboCheckStrEq(HtmlTextContent(&text, NULL), "s");
    RoboCheckEqInt((int)HtmlTextLength(&text), 1);

    /* Replace with empty: "" and length 0, still a live text node. */
    RoboCheckEqInt(HtmlTextSet(&text, NULL, 0u, NULL), RoboOk);
    RoboCheckEqInt(HtmlTextIsEmpty(&text), 1);
    content = HtmlTextContent(&text, &length);
    RoboCheck(content != NULL);
    RoboCheckEqInt((int)length, 0);

    /* Identity, parent, siblings and position are untouched by mutation. */
    RoboCheckEqInt(HtmlTextViewGet(&text, &after, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&before.handle, &after.handle));
    RoboCheck(HtmlNodeHandleEqual(&after.parent, &div));
    RoboCheckEqInt((int)TxChildCount(&div), 1);
    sibling = TxChild(&div, 0u);
    RoboCheck(HtmlNodeHandleEqual(&sibling, &text));

    /* Repeated sets do not accumulate nodes or lose the last value. */
    {
        int i;

        for (i = 0; i < 32; i++) {
            char value[32];

            (void)snprintf(value, sizeof(value), "run-%d", i);
            RoboCheckEqInt(
                HtmlTextSet(&text, value, strlen(value), NULL), RoboOk);
        }
        RoboCheckStrEq(HtmlTextContent(&text, NULL), "run-31");
        RoboCheckEqInt((int)TxChildCount(&div), 1);
    }

    /* Argument errors. */
    RoboCheckEqInt(HtmlTextSet(&text, NULL, 4u, NULL),
                      RoboInvalidArgument);

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

static void TestTxAppend(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle root;
    HtmlNodeHandle div;
    HtmlNodeHandle text;
    HtmlNodeHandle after;
    size_t length = 0u;
    const char *content;

    TxFixtureBegin();
    id = TxDoc();
    root = TxRoot(id);
    div = TxCreateElement(id, &root, "div");
    text = TxCreateText(id, &div, "Hello", 5u, RoboHtmlNodeOriginTest,
                          NULL);

    /* In-place growth: the run still ends the arena. */
    RoboCheckEqInt(HtmlTextAppend(&text, ", world", 7u, NULL),
                      RoboOk);
    content = HtmlTextContent(&text, &length);
    RoboCheck(content != NULL);
    RoboCheckEqInt((int)length, 12);
    RoboCheckStrEq(content, "Hello, world");

    /* A zero-length append is a successful no-op. */
    RoboCheckEqInt(HtmlTextAppend(&text, NULL, 0u, NULL), RoboOk);
    RoboCheckStrEq(HtmlTextContent(&text, NULL), "Hello, world");
    RoboCheckEqInt(HtmlTextAppend(&text, "ignored", 0u, NULL),
                      RoboOk);
    RoboCheckStrEq(HtmlTextContent(&text, NULL), "Hello, world");

    /* NULL bytes with a non-zero length is an argument error. */
    RoboCheckEqInt(HtmlTextAppend(&text, NULL, 3u, NULL),
                      RoboInvalidArgument);

    /*
     * Slow path: another payload now sits behind the run, so it cannot grow in
     * place and the model has to write the extended run somewhere else. The
     * result must be indistinguishable from the fast path.
     */
    after = TxCreateElement(id, &div, "span");
    RoboCheck(HtmlElementIsElement(&after));
    {
        HtmlNodeHandle spanText =
            TxCreateText(id, &after, "behind", 6u,
                           RoboHtmlNodeOriginTest, NULL);

        RoboCheck(HtmlTextIsText(&spanText));
    }
    RoboCheckEqInt(HtmlTextAppend(&text, "!", 1u, NULL), RoboOk);
    RoboCheckStrEq(HtmlTextContent(&text, NULL), "Hello, world!");
    RoboCheckEqInt((int)HtmlTextLength(&text), 13);

    /* The node behind it is untouched. */
    {
        HtmlNodeHandle span = TxFind(id, "span");
        char buf[32];

        TxText(&span, buf, sizeof(buf));
        RoboCheckStrEq(buf, "behind");
    }

    /* Appending to an empty node is a plain set-equivalent. */
    {
        HtmlNodeHandle fresh =
            TxCreateText(id, &div, NULL, 0u, RoboHtmlNodeOriginTest,
                           NULL);

        RoboCheckEqInt(HtmlTextAppend(&fresh, "born", 4u, NULL),
                          RoboOk);
        RoboCheckStrEq(HtmlTextContent(&fresh, NULL), "born");
    }

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

static void TestTxMutationAfterFinalization(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle paragraph;
    HtmlNodeHandle text;
    char buf[64];

    TxFixtureBegin();
    id = TxDoc();
    TxBuild(id, "<html><body><p>Hello</p></body></html>", NULL);

    /*
     * 8.4 closed tree *construction*, not the document: character data stays
     * mutable, which is what a later DOM needs.
     */
    paragraph = TxFind(id, "p");
    text = TxChild(&paragraph, 0u);
    RoboCheck(HtmlTextIsText(&text));
    RoboCheckEqInt(HtmlTextSet(&text, "Goodbye", 7u, NULL), RoboOk);
    TxText(&paragraph, buf, sizeof(buf));
    RoboCheckStrEq(buf, "Goodbye");

    RoboCheckEqInt(HtmlTextAppend(&text, "!", 1u, NULL), RoboOk);
    TxText(&paragraph, buf, sizeof(buf));
    RoboCheckStrEq(buf, "Goodbye!");

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Parser integration                                                  */
/* ------------------------------------------------------------------ */

static void TestTxParserCreatesTextNodes(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle paragraph;
    HtmlNodeHandle bold;
    HtmlNodeHandle child;
    HtmlTextView view;
    char buf[64];

    TxFixtureBegin();
    id = TxDoc();
    TxBuild(id, "<html><body><p>Hello Roboticela</p></body></html>", NULL);

    paragraph = TxFind(id, "p");
    /* Exactly one child, and it is a text node — not an element property. */
    RoboCheckEqInt((int)TxChildCount(&paragraph), 1);
    child = TxChild(&paragraph, 0u);
    RoboCheck(HtmlTextIsText(&child));
    RoboCheckEqInt(HtmlTextViewGet(&child, &view, NULL), RoboOk);
    RoboCheckEqInt(view.state & RoboHtmlTextStateParserCreated,
                      RoboHtmlTextStateParserCreated);
    RoboCheckEqInt(view.state & RoboHtmlTextStateConnected,
                      RoboHtmlTextStateConnected);
    RoboCheck(HtmlNodeHandleEqual(&view.parent, &paragraph));
    RoboCheckStrEq(view.content, "Hello Roboticela");
    RoboCheckEqInt((int)view.length, 16);

    /* Text, element, text — three children, in document order. */
    (void)HtmlDocumentDestroy(id, NULL);

    id = TxDoc();
    TxBuild(id, "<html><body><p>Hello <b>Roboticela</b> Runtime</p></body>"
                 "</html>",
             NULL);
    paragraph = TxFind(id, "p");
    RoboCheckEqInt((int)TxChildCount(&paragraph), 3);

    child = TxChild(&paragraph, 0u);
    RoboCheck(HtmlTextIsText(&child));
    RoboCheckStrEq(HtmlTextContent(&child, NULL), "Hello ");

    bold = TxChild(&paragraph, 1u);
    RoboCheck(HtmlElementIsElement(&bold));
    RoboCheck(HtmlElementIsNamed(&bold, "b"));
    RoboCheckEqInt((int)TxChildCount(&bold), 1);
    {
        HtmlNodeHandle boldText = TxChild(&bold, 0u);

        RoboCheck(HtmlTextIsText(&boldText));
        RoboCheckStrEq(HtmlTextContent(&boldText, NULL),
                          "Roboticela");
    }

    child = TxChild(&paragraph, 2u);
    RoboCheck(HtmlTextIsText(&child));
    RoboCheckStrEq(HtmlTextContent(&child, NULL), " Runtime");

    /* The concatenation is the paragraph's text, in order. */
    TxText(&paragraph, buf, sizeof(buf));
    RoboCheckStrEq(buf, "Hello Roboticela Runtime");

    /* <div>one<span>two</span>three</div> — same shape, different names. */
    (void)HtmlDocumentDestroy(id, NULL);

    id = TxDoc();
    TxBuild(id, "<html><body><div>one<span>two</span>three</div></body>"
                 "</html>",
             NULL);
    {
        HtmlNodeHandle div = TxFind(id, "div");
        char first[16];
        char second[16];

        RoboCheckEqInt((int)TxChildCount(&div), 3);
        TxChildText(&div, 0u, first, sizeof(first));
        RoboCheckStrEq(first, "one");
        TxChildText(&div, 2u, second, sizeof(second));
        RoboCheckStrEq(second, "three");
        TxText(&div, buf, sizeof(buf));
        RoboCheckStrEq(buf, "onetwothree");
    }

    /* <div>Hello <strong>Roboticela</strong> Runtime</div> */
    (void)HtmlDocumentDestroy(id, NULL);

    id = TxDoc();
    TxBuild(id, "<html><body><div>Hello <strong>Roboticela</strong> Runtime"
                 "</div></body></html>",
             NULL);
    {
        HtmlNodeHandle div = TxFind(id, "div");
        HtmlNodeHandle strong;

        RoboCheckEqInt((int)TxChildCount(&div), 3);
        strong = TxChild(&div, 1u);
        RoboCheck(HtmlElementIsNamed(&strong, "strong"));
        TxText(&div, buf, sizeof(buf));
        RoboCheckStrEq(buf, "Hello Roboticela Runtime");
    }

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

static void TestTxRunsAndMerging(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle paragraph;
    HtmlNodeHandle div;
    HtmlTreeResult result;
    char buf[64];

    TxFixtureBegin();

    /*
     * A run arrives as many character tokens (buffer boundaries, character
     * references, split-up text) and becomes exactly ONE node. The tokenizer's
     * segmentation is not observable in the tree, which is the point.
     */
    id = TxDoc();
    TxBuild(id, "<html><body><p>alpha &amp; beta &#65; gamma</p></body>"
                 "</html>",
             &result);
    paragraph = TxFind(id, "p");
    RoboCheckEqInt((int)TxChildCount(&paragraph), 1);
    RoboCheckEqInt((int)TxTextChildCount(&paragraph), 1);
    TxText(&paragraph, buf, sizeof(buf));
    RoboCheckStrEq(buf, "alpha & beta A gamma");
    (void)HtmlDocumentDestroy(id, NULL);

    /* An element breaks the run: text either side stays separate. */
    id = TxDoc();
    TxBuild(id, "<html><body><p>before<em>mid</em>after</p></body></html>",
             NULL);
    paragraph = TxFind(id, "p");
    RoboCheckEqInt((int)TxChildCount(&paragraph), 3);
    RoboCheckEqInt((int)TxTextChildCount(&paragraph), 2);
    TxText(&paragraph, buf, sizeof(buf));
    RoboCheckStrEq(buf, "beforemidafter");
    (void)HtmlDocumentDestroy(id, NULL);

    /* A comment breaks the run as well. */
    id = TxDoc();
    TxBuild(id, "<html><body><p>one<!--c-->two</p></body></html>", NULL);
    paragraph = TxFind(id, "p");
    RoboCheckEqInt((int)TxChildCount(&paragraph), 3);
    RoboCheckEqInt((int)TxTextChildCount(&paragraph), 2);
    TxText(&paragraph, buf, sizeof(buf));
    RoboCheckStrEq(buf, "onetwo");
    (void)HtmlDocumentDestroy(id, NULL);

    /*
     * Decoded character references join the run they sit in rather than
     * splitting it: the div is Text("A \xA0 B \xA9 C ") + <i>, and only the
     * element breaks the run.
     */
    id = TxDoc();
    TxBuild(id, "<html><body><div>A &nbsp; B &copy; C <i>d</i>"
                 "</div></body></html>",
             NULL);
    div = TxFind(id, "div");
    RoboCheckEqInt((int)TxChildCount(&div), 2);
    RoboCheckEqInt((int)TxTextChildCount(&div), 1);
    TxText(&div, buf, sizeof(buf));
    RoboCheckStrEq(buf, "A \xC2\xA0 B \xC2\xA9 C d");

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

static void TestTxWhitespacePreserved(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle div;
    HtmlNodeHandle text;
    HtmlTextView view;
    char buf[128];

    TxFixtureBegin();

    /* Runs of spaces are not collapsed at this layer. */
    id = TxDoc();
    TxBuild(id, "<html><body><div>Hello    world</div></body></html>", NULL);
    div = TxFind(id, "div");
    RoboCheckEqInt((int)TxChildCount(&div), 1);
    text = TxChild(&div, 0u);
    RoboCheckEqInt(HtmlTextViewGet(&text, &view, NULL), RoboOk);
    RoboCheckStrEq(view.content, "Hello    world");
    RoboCheckEqInt((int)view.length, 14);

    /* Newlines and tabs inside a run survive byte-for-byte. */
    (void)HtmlDocumentDestroy(id, NULL);
    id = TxDoc();
    TxBuild(id, "<html><body><div>a\tb\nc</div></body></html>", NULL);
    div = TxFind(id, "div");
    text = TxChild(&div, 0u);
    RoboCheckEqInt(HtmlTextViewGet(&text, &view, NULL), RoboOk);
    RoboCheckStrEq(view.content, "a\tb\nc");
    RoboCheckEqInt((int)view.length, 5);

    /* Leading and trailing whitespace is not trimmed; nothing to trim with. */
    (void)HtmlDocumentDestroy(id, NULL);
    id = TxDoc();
    TxBuild(id, "<html><body><div>  padded  </div></body></html>", NULL);
    div = TxFind(id, "div");
    text = TxChild(&div, 0u);
    RoboCheckEqInt(HtmlTextViewGet(&text, &view, NULL), RoboOk);
    RoboCheckStrEq(view.content, "  padded  ");

    /*
     * The whitespace between tags is real text too: <body> holds the block
     * plus the newline runs around it.
     */
    (void)HtmlDocumentDestroy(id, NULL);
    id = TxDoc();
    TxBuild(id, "<html>\n<body>\n  <h1>Title</h1>\n  <p>Body</p>\n</body>\n"
                 "</html>\n",
             NULL);
    {
        HtmlNodeHandle body = TxFind(id, "body");
        size_t children = TxChildCount(&body);
        size_t i;
        size_t blanks = 0u;

        /* The two tagged runs around the blocks, plus the run after </body>,
         * which HTML re-inserts into the body: four whitespace nodes. */
        RoboCheckEqInt((int)children, 6);
        for (i = 0u; i < children; i++) {
            HtmlNodeHandle child = TxChild(&body, i);

            if (HtmlTextIsText(&child)) {
                blanks++;
            }
        }
        RoboCheckEqInt((int)blanks, 4);
        /* Document order concatenation keeps the layout newlines. */
        TxText(&body, buf, sizeof(buf));
        RoboCheck(strstr(buf, "\n  ") != NULL);
    }

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

static void TestTxCharReferences(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle paragraph;
    HtmlNodeHandle text;
    size_t length = 0u;
    const char *content;

    TxFixtureBegin();
    id = TxDoc();
    TxBuild(id, "<html><body><p>&amp;&lt;&gt;&#65;&#x42;</p></body>"
                 "</html>",
             NULL);
    paragraph = TxFind(id, "p");
    RoboCheckEqInt((int)TxChildCount(&paragraph), 1);
    text = TxChild(&paragraph, 0u);
    RoboCheck(HtmlTextIsText(&text));

    /* Decoding is the tokenizer's job; the node holds the decoded bytes. */
    content = HtmlTextContent(&text, &length);
    RoboCheck(content != NULL);
    RoboCheckEqInt((int)length, 5);
    RoboCheckStrEq(content, "&<>AB");

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

static void TestTxUnicodePreserved(void)
{
    static const char *const kCases[] = {
        "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7",             /* Arabic  */
        "\xD9\x81\xD8\xA7\xD8\xB1\xD8\xB3\xDB\x8C",             /* Persian */
        "\xD8\xA7\xD8\xB1\xD8\xAF\xD9\x88",                     /* Urdu    */
        "\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D\xE0"
        "\xA4\xA6\xE0\xA5\x80",                                 /* Hindi   */
        "\xE0\xA6\xAC\xE0\xA6\xBE\xE0\xA6\x82\xE0\xA6\xB2\xE0"
        "\xA6\xBE",                                             /* Bengali */
        "\xE4\xB8\xAD\xE6\x96\x87",                             /* Chinese */
        "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",                 /* Japanese*/
        "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4",                 /* Korean  */
        "\xF0\x9F\x98\x80 \xF0\x9F\x9A\x80 \xE2\x9D\xA4\xEF"
        "\xB8\x8F"                                              /* emoji   */
    };
    HtmlDocumentId id = RoboHtmlDocumentIdNone;
    size_t i;

    TxFixtureBegin();

    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        HtmlNodeHandle paragraph;
        HtmlNodeHandle text;
        HtmlTextView view;
        char source[256];

        (void)snprintf(source, sizeof(source),
                       "<html><body><p>%s</p></body></html>", kCases[i]);
        id = TxDoc();
        TxBuild(id, source, NULL);
        paragraph = TxFind(id, "p");

        RoboCheckEqInt((int)TxChildCount(&paragraph), 1);
        text = TxChild(&paragraph, 0u);
        RoboCheck(HtmlTextIsText(&text));
        RoboCheckEqInt(HtmlTextViewGet(&text, &view, NULL), RoboOk);
        /* Byte-exact round trip; no normalization, no re-encoding. */
        RoboCheckStrEq(view.content, kCases[i]);
        RoboCheckEqInt((int)view.length, (int)strlen(kCases[i]));

        (void)HtmlDocumentDestroy(id, NULL);
    }

    /* Mixed scripts plus a character reference in one run. */
    id = TxDoc();
    TxBuild(id, "<html><body><p>ASCII \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8"
                 "\xD8\xA7 \xE4\xB8\xAD\xE6\x96\x87 \xF0\x9F\x98\x80"
                 " &mdash; end</p></body></html>",
             NULL);
    {
        HtmlNodeHandle paragraph = TxFind(id, "p");
        HtmlNodeHandle text = TxChild(&paragraph, 0u);
        HtmlTextView view;

        RoboCheckEqInt((int)TxChildCount(&paragraph), 1);
        RoboCheckEqInt(HtmlTextViewGet(&text, &view, NULL), RoboOk);
        RoboCheck(strstr(view.content, "ASCII ") == view.content);
        RoboCheck(strstr(view.content, "\xE4\xB8\xAD\xE6\x96\x87") != NULL);
        RoboCheck(strstr(view.content, "\xF0\x9F\x98\x80") != NULL);
        RoboCheck(strstr(view.content, "end") != NULL);
    }

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

static void TestTxLongText(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle paragraph;
    HtmlNodeHandle text;
    HtmlTextView view;
    static char run[6001];
    char source[8192];
    size_t i;

    TxFixtureBegin();

    for (i = 0u; i < 6000u; i++) {
        run[i] = (char)('a' + (int)(i % 26));
    }
    run[6000] = '\0';

    (void)snprintf(source, sizeof(source),
                   "<html><body><p>%s</p></body></html>", run);
    id = TxDoc();
    TxBuild(id, source, NULL);
    paragraph = TxFind(id, "p");
    RoboCheckEqInt((int)TxChildCount(&paragraph), 1);
    text = TxChild(&paragraph, 0u);
    RoboCheck(HtmlTextIsText(&text));
    RoboCheckEqInt(HtmlTextViewGet(&text, &view, NULL), RoboOk);
    RoboCheckEqInt((int)view.length, 6000);
    /* One node, not one per token, and byte-exact. */
    RoboCheck(memcmp(view.content, run, 6000u) == 0);

    /* Extending a long run keeps it one node. */
    RoboCheckEqInt(HtmlTextAppend(&text, "TAIL", 4u, NULL), RoboOk);
    RoboCheckEqInt((int)HtmlTextLength(&text), 6004);
    RoboCheckEqInt((int)TxChildCount(&paragraph), 1);
    RoboCheckEqInt(HtmlTextViewGet(&text, &view, NULL), RoboOk);
    RoboCheck(memcmp(view.content + 6000, "TAIL", 4u) == 0);

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Traversal                                                           */
/* ------------------------------------------------------------------ */

static void TestTxTraversal(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle body;
    HtmlNodeHandle first;
    HtmlNodeHandle last;
    HtmlNodeHandle middle;
    HtmlNodeHandle viewChild;
    HtmlNodeView view;
    size_t children;
    size_t i;
    size_t walked = 0u;

    TxFixtureBegin();

    /*
     * Text and elements interleave as real siblings, so the traversal is
     * exercised on a mixed child list rather than a uniform one.
     */
    id = TxDoc();
    TxBuild(id, "<html><body>alpha<span>mid</span>omega</body></html>",
             NULL);
    body = TxFind(id, "body");
    children = TxChildCount(&body);
    RoboCheckEqInt((int)children, 3);

    /* Child iteration reaches every child in document order, by kind. */
    for (i = 0u; i < children; i++) {
        HtmlNodeHandle child = TxChild(&body, i);

        if (i == 1u) {
            RoboCheck(HtmlElementIsNamed(&child, "span"));
        } else {
            RoboCheck(HtmlTextIsText(&child));
        }
        walked++;
    }
    RoboCheckEqInt((int)walked, 3);
    RoboCheckEqInt((int)TxTextChildCount(&body), 2);

    first = TxChild(&body, 0u);
    middle = TxChild(&body, 1u);
    last = TxChild(&body, 2u);

    /* firstChild / lastChild through the node view. */
    RoboCheckEqInt(HtmlNodeGetView(&body, &view, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&view.firstChild, &first));
    RoboCheck(HtmlNodeHandleEqual(&view.lastChild, &last));
    RoboCheckEqInt((int)view.childCount, 3);

    /* Sibling links are the ones the tree already maintains, text included. */
    RoboCheckEqInt(HtmlNodeGetView(&first, &view, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&view.NextSibling, &middle));
    RoboCheck(!HtmlNodeHandleIsWellformed(&view.prevSibling));
    RoboCheck(HtmlNodeHandleEqual(&view.parent, &body));

    RoboCheckEqInt(HtmlNodeGetView(&middle, &view, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&view.prevSibling, &first));
    RoboCheck(HtmlNodeHandleEqual(&view.NextSibling, &last));

    RoboCheckEqInt(HtmlNodeGetView(&last, &view, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&view.prevSibling, &middle));
    RoboCheck(!HtmlNodeHandleIsWellformed(&view.NextSibling));

    /* The text model reports the same parent the tree does. */
    {
        HtmlNodeHandle parent = {0u, 0u, 0u};

        RoboCheckEqInt(HtmlTextParent(&first, &parent, NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&parent, &body));
    }

    /* A text node is a leaf: no children, and index 0 is out of range. */
    RoboCheckEqInt((int)TxChildCount(&first), 0);
    viewChild = TxChild(&first, 0u);
    RoboCheck(!HtmlNodeHandleIsWellformed(&viewChild));

    /* Recursive walk over the whole tree sees text and elements. */
    {
        size_t texts = 0u;
        size_t elements = 0u;
        HtmlNodeHandle stack[64];
        size_t depth = 0u;

        stack[depth++] = TxRoot(id);
        while (depth > 0u) {
            HtmlNodeHandle node = stack[--depth];
            size_t count = TxChildCount(&node);
            size_t k;

            if (HtmlTextIsText(&node)) {
                texts++;
            } else if (HtmlElementIsElement(&node)) {
                elements++;
            }
            for (k = 0u; k < count && depth < 64u; k++) {
                stack[depth++] = TxChild(&node, k);
            }
        }
        RoboCheckEqInt((int)texts, 3);    /* alpha, mid, omega */
        RoboCheckEqInt((int)elements, 4); /* html head body span */
    }

    (void)HtmlDocumentDestroy(id, NULL);

    /* Text under text-level elements is reachable one level down. */
    id = TxDoc();
    TxBuild(id, "<html><body><p>one</p><p>two</p><p>three</p></body>"
                 "</html>",
             NULL);
    body = TxFind(id, "body");
    RoboCheckEqInt((int)TxChildCount(&body), 3);
    RoboCheckEqInt((int)TxTextChildCount(&body), 0);
    {
        static const char *const kWords[] = {"one", "two", "three"};

        for (i = 0u; i < 3u; i++) {
            HtmlNodeHandle paragraph = TxChild(&body, i);
            HtmlNodeHandle text;
            char buf[16];

            RoboCheck(HtmlElementIsNamed(&paragraph, "p"));
            RoboCheckEqInt((int)TxChildCount(&paragraph), 1);
            text = TxChild(&paragraph, 0u);
            RoboCheck(HtmlTextIsText(&text));
            RoboCheckStrEq(HtmlTextContent(&text, NULL),
                              kWords[i]);
            TxText(&paragraph, buf, sizeof(buf));
            RoboCheckStrEq(buf, kWords[i]);
        }
    }

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Ownership and teardown                                              */
/* ------------------------------------------------------------------ */

static void TestTxOwnershipAndStaleHandles(void)
{
    HtmlDocumentId id;
    HtmlNodeHandle saved;

    TxFixtureBegin();
    id = TxDoc();
    TxBuild(id, "<html><body><p>Hello</p></body></html>", NULL);
    {
        HtmlNodeHandle before = TxFind(id, "p");

        saved = TxChild(&before, 0u);
    }
    RoboCheck(HtmlTextIsText(&saved));

    /*
     * Rebuilding the document's tree retires the old nodes: the generation
     * guard is what makes the saved handle resolve as stale instead of
     * aliasing whatever now occupies that id.
     */
    RoboCheckEqInt(HtmlDocumentReset(id, NULL), RoboOk);
    RoboCheck(HtmlNodeHandleIsStale(&saved));
    RoboCheck(!HtmlTextIsText(&saved));
    RoboCheck(HtmlTextContent(&saved, NULL) == NULL);
    RoboCheckEqInt(HtmlTextSet(&saved, "x", 1u, NULL),
                      RoboNotFound);
    RoboCheckEqInt(HtmlTextAppend(&saved, "x", 1u, NULL),
                      RoboNotFound);

    /* The rebuilt tree has its own text nodes, unaffected by the old handle. */
    TxBuild(id, "<html><body><p>Second</p></body></html>", NULL);
    {
        HtmlNodeHandle second = TxFind(id, "p");
        HtmlNodeHandle fresh = TxChild(&second, 0u);
        char buf[32];

        RoboCheck(HtmlTextIsText(&fresh));
        RoboCheck(!HtmlNodeHandleEqual(&fresh, &saved));
        TxText(&second, buf, sizeof(buf));
        RoboCheckStrEq(buf, "Second");
    }

    /* Destroying the document retires every handle it issued. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheck(!HtmlTextIsText(&saved));
    RoboCheck(HtmlTextContent(&saved, NULL) == NULL);
    RoboCheckEqInt((int)HtmlTextLength(&saved), 0);
    RoboCheckEqInt(HtmlTextIsEmpty(&saved), 0);
    RoboCheckEqInt(HtmlTextViewGet(&saved, NULL, NULL),
                      RoboInvalidArgument);

    TxFixtureEnd();
}

static void TestTxTeardown(void)
{
    HtmlDocumentId id;
    HtmlTreeReport report;

    TxFixtureBegin();
    id = TxDoc();
    TxBuild(id, "<!DOCTYPE html><html><head><title>T</title></head>"
                 "<body><h1>H</h1><p>one</p><p>two</p>"
                 "<div>a<span>b</span>c</div></body></html>",
             NULL);

    /* The tree is internally consistent with text nodes in it. */
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                      RoboOk);
    RoboCheck(report.ok);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                      RoboNotFound);

    TxFixtureEnd();
}

static void TestTxLargeTreeTeardown(void)
{
    HtmlDocumentId id;
    static char source[16000];
    size_t used = 0u;
    int i;

    TxFixtureBegin();
    id = TxDoc();

    used += (size_t)snprintf(source + used, sizeof(source) - used,
                             "<html><body>");
    for (i = 0; i < 80 && used < sizeof(source) - 64u; i++) {
        used += (size_t)snprintf(source + used, sizeof(source) - used,
                                 "<p>paragraph %d</p>", i);
    }
    (void)snprintf(source + used, sizeof(source) - used, "</body></html>");

    TxBuild(id, source, NULL);
    {
        HtmlNodeHandle body = TxFind(id, "body");
        size_t texts = 0u;
        size_t i;

        RoboCheckEqInt((int)TxChildCount(&body), 80);
        /* Each <p> holds exactly one text run. */
        for (i = 0u; i < 80u; i++) {
            HtmlNodeHandle paragraph = TxChild(&body, i);
            HtmlNodeHandle text;

            RoboCheck(HtmlElementIsNamed(&paragraph, "p"));
            RoboCheckEqInt((int)TxChildCount(&paragraph), 1);
            text = TxChild(&paragraph, 0u);
            if (HtmlTextIsText(&text)) {
                texts++;
            }
        }
        RoboCheckEqInt((int)texts, 80);
    }

    /* One teardown releases the registry and the payload arena together. */
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    TxFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Malformed input                                                     */
/* ------------------------------------------------------------------ */

static void TestTxMalformedInput(void)
{
    static const char *const kCases[] = {
        "",
        "Hello",
        "   \n\t  ",
        "<p>unclosed",
        "<p>a<div>b</div>",
        "</p>stray close",
        "<html><body><p>&amp;</p>",
        "<html><body><div>   </div></body></html>",
        "<html><body><p></p></body></html>",
        "<p><b>a<i>b</b>c</i></p>",
        "<html><body><p>\xF0\x9F\x98\x80</p></body></html>",
        "<!DOCTYPE html><html><body><br>text</body></html>"
    };
    size_t i;

    TxFixtureBegin();

    /*
     * Every one of these must build without crashing, produce a tree the
     * document validator accepts, and keep whatever text it did have. Full
     * HTML5 error recovery is not this phase's scope.
     */
    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        HtmlDocumentId id = TxDoc();
        HtmlTreeReport report;

        TxBuild(id, kCases[i], NULL);
        RoboCheckEqInt(HtmlDocumentValidateTree(id, &report, NULL),
                          RoboOk);
        RoboCheck(report.ok);
        RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    }

    /* Text-only input lands in body as a real text node. */
    {
        HtmlDocumentId id = TxDoc();
        char buf[64];

        TxBuild(id, "just text", NULL);
        TxTextOf(id, "body", buf, sizeof(buf));
        RoboCheckStrEq(buf, "just text");
        (void)HtmlDocumentDestroy(id, NULL);
    }

    /* Whitespace-only input keeps its whitespace and creates no empty node. */
    {
        HtmlDocumentId id = TxDoc();
        HtmlNodeHandle body;
        char buf[32];

        TxBuild(id, "<html><body>   </body></html>", NULL);
        body = TxFind(id, "body");
        RoboCheckEqInt((int)TxChildCount(&body), 1);
        RoboCheckEqInt((int)TxTextChildCount(&body), 1);
        TxText(&body, buf, sizeof(buf));
        RoboCheckStrEq(buf, "   ");
        (void)HtmlDocumentDestroy(id, NULL);
    }

    /* An empty element creates no text child rather than an empty one. */
    {
        HtmlDocumentId id = TxDoc();
        HtmlNodeHandle paragraph;

        TxBuild(id, "<html><body><p></p></body></html>", NULL);
        paragraph = TxFind(id, "p");
        RoboCheck(HtmlElementIsElement(&paragraph));
        RoboCheckEqInt((int)TxChildCount(&paragraph), 0);
        (void)HtmlDocumentDestroy(id, NULL);
    }

    TxFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Tree dump                                                           */
/* ------------------------------------------------------------------ */

static Result TxCollectSink(const char *text, size_t length,
                                     void *user)
{
    char *buffer = (char *)user;
    size_t used = strlen(buffer);

    memcpy(buffer + used, text, length);
    buffer[used + length] = '\0';
    return RoboOk;
}

static void TestTxTreeDump(void)
{
    HtmlDocumentId id;
    static char buffer[8192];

    TxFixtureBegin();
    id = TxDoc();
    TxBuild(id, "<!DOCTYPE html><html><body><h1>Heading</h1>"
                 "<p>line one\nline two\tend</p></body></html>",
             NULL);

    buffer[0] = '\0';
    RoboCheckEqInt(HtmlTreeDump(id, TxCollectSink, buffer, NULL),
                      RoboOk);

    /*
     * A text node is labelled and its whitespace is escaped, which is what
     * makes a run legible in a dump (`Text: "line one\nline two\tend"`).
     */
    RoboCheck(strstr(buffer, "Text: \"Heading\"") != NULL);
    RoboCheck(strstr(buffer, "Text: \"line one\\nline two\\tend\"") !=
               NULL);

    (void)HtmlDocumentDestroy(id, NULL);

    /* An empty text node prints its empty run rather than being elided. */
    id = TxDoc();
    {
        HtmlNodeHandle root = TxRoot(id);
        HtmlNodeHandle div = TxCreateElement(id, &root, "div");
        HtmlNodeHandle empty =
            TxCreateText(id, &div, NULL, 0u, RoboHtmlNodeOriginTest,
                           NULL);

        RoboCheck(HtmlTextIsText(&empty));
        buffer[0] = '\0';
        RoboCheckEqInt(
            HtmlTreeDump(id, TxCollectSink, buffer, NULL), RoboOk);
        RoboCheck(strstr(buffer, "Text: \"\"") != NULL);
    }

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Capability + visual demo document                                   */
/* ------------------------------------------------------------------ */

static void TestTxCapabilityFlag(void)
{
    HtmlDomCapabilities caps;

    HtmlDomCapabilitiesInit(&caps);
    RoboCheckEqInt(caps.textNodes, 0);
    RoboCheckEqInt(HtmlDomQueryCapabilities(&caps), RoboOk);
    /* The text model is available; the node DOM and events still are not. */
    RoboCheckEqInt(caps.textNodes, 1);
    RoboCheckEqInt(caps.treeBuilding, 1);
    RoboCheckEqInt(caps.domNodes, 0);
    RoboCheckEqInt(caps.events, 0);
}

static void TestTxDemoDocument(void)
{
    static const char *const kDemo =
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
        "</html>\n";
    HtmlDocumentId id;
    HtmlTreeResult result;
    HtmlNodeHandle body;
    HtmlNodeHandle head;
    HtmlNodeHandle title;
    char text[128];

    TxFixtureBegin();
    id = TxDoc();
    TxBuild(id, kDemo, &result);

    /*
     * The demo document's text is carried by real text nodes: 6 whitespace
     * runs around the 4 body blocks, each its own node.
     */
    body = TxFind(id, "body");
    RoboCheckEqInt((int)TxChildCount(&body), 10);
    RoboCheckEqInt((int)TxTextChildCount(&body), 6);

    /* Each block's visible text comes from a text node child, not a field. */
    {
        static const char *const kNames[] = {"h1", "p", "p", "button"};
        size_t i;

        for (i = 0u; i < 4u; i++) {
            HtmlNodeHandle element = TxFind(id, kNames[i]);

            RoboCheck(HtmlElementIsElement(&element));
            RoboCheckEqInt((int)TxChildCount(&element), 1);
            if (i == 0u) {
                HtmlNodeHandle child = TxChild(&element, 0u);

                RoboCheck(HtmlTextIsText(&child));
                RoboCheckStrEq(HtmlTextContent(&child, NULL),
                                  "RoboticelaOS Runtime");
            }
        }
    }

    TxTextOf(id, "h1", text, sizeof(text));
    RoboCheckStrEq(text, "RoboticelaOS Runtime");
    TxTextOf(id, "button", text, sizeof(text));
    RoboCheckStrEq(text, "Demo Button");

    head = TxFind(id, "head");
    title = TxFind(id, "title");
    RoboCheck(HtmlElementIsElement(&head));
    RoboCheck(HtmlElementIsElement(&title));
    RoboCheckEqInt((int)TxChildCount(&title), 1);
    TxTextOf(id, "title", text, sizeof(text));
    RoboCheckStrEq(text, "RoboticelaOS Runtime");

    /* The body's whitespace runs are what separate the blocks. */
    {
        long run = TxTextChildIndex(&body, 0u);
        char whitespace[16];

        RoboCheck(run >= 0);
        TxChildText(&body, (size_t)run, whitespace, sizeof(whitespace));
        RoboCheckStrEq(whitespace, "\n");
    }

    RoboCheckEqInt((int)result.elementCount, 8);
    RoboCheckEqInt((int)result.truncated, 0);

    (void)HtmlDocumentDestroy(id, NULL);
    TxFixtureEnd();
}

/* ------------------------------------------------------------------ */

void TestRegisterHtmlText(void)
{
    TestRegister("html.text.create_info_init", TestTxCreateInfoInit);
    TestRegister("html.text.create_basic", TestTxCreateBasic);
    TestRegister("html.text.create_empty", TestTxCreateEmpty);
    TestRegister("html.text.create_validation", TestTxCreateValidation);
    TestRegister("html.text.kind_identification",
                       TestTxKindIdentification);
    TestRegister("html.text.not_text_answers", TestTxNotTextAnswers);
    TestRegister("html.text.set", TestTxSet);
    TestRegister("html.text.append", TestTxAppend);
    TestRegister("html.text.mutation_after_finalization",
                       TestTxMutationAfterFinalization);
    TestRegister("html.text.parser_integration",
                       TestTxParserCreatesTextNodes);
    TestRegister("html.text.runs_and_merging", TestTxRunsAndMerging);
    TestRegister("html.text.whitespace", TestTxWhitespacePreserved);
    TestRegister("html.text.char_references", TestTxCharReferences);
    TestRegister("html.text.unicode", TestTxUnicodePreserved);
    TestRegister("html.text.long_text", TestTxLongText);
    TestRegister("html.text.traversal", TestTxTraversal);
    TestRegister("html.text.ownership", TestTxOwnershipAndStaleHandles);
    TestRegister("html.text.teardown", TestTxTeardown);
    TestRegister("html.text.large_tree_teardown",
                       TestTxLargeTreeTeardown);
    TestRegister("html.text.malformed", TestTxMalformedInput);
    TestRegister("html.text.tree_dump", TestTxTreeDump);
    TestRegister("html.text.capability_flag", TestTxCapabilityFlag);
    TestRegister("html.text.demo_document", TestTxDemoDocument);
}
