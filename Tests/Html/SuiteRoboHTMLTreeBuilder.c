/*
 * RoboHTML Phase 8.3 tree-builder suite:
 * document structure, implied elements, nesting, text merging, comments,
 * DOCTYPE, attributes, void/self-closing elements, open-element stack,
 * insertion-mode transitions, recovery, ownership, teardown, tree dump,
 * tokenizer integration, and the visual-demo document's expected tree.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLDocument.h"
#include "RoboHTML/RoboHTMLElement.h"
#include "RoboHTML/RoboHTMLNode.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLTreeBuilder.h"
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixture + helpers                                                   */
/* ------------------------------------------------------------------ */

static void TbFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void TbFixtureEnd(void)
{
    HtmlShutdown();
}

static HtmlDocumentId TbDoc(void)
{
    HtmlDocumentId id = RoboHtmlDocumentIdNone;

    RoboCheckEqInt(HtmlDocumentCreate(&id, NULL), RoboOk);
    RoboCheck(id != RoboHtmlDocumentIdNone);
    return id;
}

/** Build @p text into @p id with the one-shot API; result optional. */
static void TbBuild(HtmlDocumentId id,
                     const char *text,
                     HtmlTreeResult *out)
{
    HtmlSource src;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    RoboCheckEqInt(HtmlBuildTree(id, &src, out, NULL), RoboOk);
}

static HtmlNodeHandle TbRoot(HtmlDocumentId id)
{
    HtmlNodeHandle root = HtmlDocumentRoot(id);

    RoboCheck(HtmlNodeHandleIsWellformed(&root));
    return root;
}

/** Handle of the first descendant element named @p name. */
static HtmlNodeHandle TbFind(HtmlDocumentId id,
                                       const char *name)
{
    HtmlNodeHandle root = TbRoot(id);
    HtmlNodeHandle found;

    HtmlNodeHandleClear(&found);
    if (HtmlNodeFindElement(&root, name, &found, NULL) != RoboOk) {
        HtmlNodeHandleClear(&found);
    }
    return found;
}

/** Concatenated text of a subtree; "" when the node is unknown. */
static void TbText(HtmlNodeHandle h, char *buf, size_t cap)
{
    size_t length = 0u;

    buf[0] = '\0';
    if (HtmlNodeSubtreeText(&h, buf, cap, &length, NULL) != RoboOk) {
        buf[0] = '\0';
    }
}

static size_t TbChildCount(HtmlNodeHandle h)
{
    size_t count = 0u;

    RoboCheckEqInt(HtmlNodeChildCount(&h, &count, NULL), RoboOk);
    return count;
}

/** Text of the first descendant @p name's subtree. */
static void TbTextOf(HtmlDocumentId id,
                       const char *name,
                       char *buf,
                       size_t cap)
{
    HtmlNodeHandle h = TbFind(id, name);

    TbText(h, buf, cap);
}

/* ------------------------------------------------------------------ */
/* Module surface                                                      */
/* ------------------------------------------------------------------ */

static void TestTbModelHelpers(void)
{
    TbFixtureBegin();

    RoboCheck(HtmlInsertionModeIsKnown(RoboHtmlModeInitial));
    RoboCheck(HtmlInsertionModeIsKnown(RoboHtmlModeText));
    RoboCheck(!HtmlInsertionModeIsKnown((HtmlInsertionMode)42));
    RoboCheckStrEq(HtmlInsertionModeName(RoboHtmlModeInBody),
                      "in-body");
    RoboCheckStrEq(HtmlInsertionModeName(RoboHtmlModeText), "text");
    RoboCheckStrEq(HtmlInsertionModeName((HtmlInsertionMode)9),
                      "unknown");

    RoboCheck(HtmlTextModeIsKnown(RoboHtmlTextModeRcdata));
    RoboCheck(!HtmlTextModeIsKnown((HtmlTextMode)9));
    RoboCheckStrEq(HtmlTextModeName(RoboHtmlTextModeRawtext),
                      "rawtext");
    RoboCheckStrEq(HtmlTextModeName((HtmlTextMode)7),
                      "unknown");

    /* Token-kind helpers the tree builder's consume() depends on. */
    RoboCheck(HtmlTokenKindIsKnown(RoboHtmlTokenStartTag));
    RoboCheck(!HtmlTokenKindIsKnown((HtmlTokenKind)77));
    RoboCheckStrEq(HtmlTokenKindName(RoboHtmlTokenEndTag),
                      "end-tag");
    RoboCheckStrEq(HtmlTokenKindName((HtmlTokenKind)77),
                      "unknown");

    TbFixtureEnd();
}

static void TestTbCreateMisuse(void)
{
    HtmlDocumentId id;
    HtmlTreeBuilder *builder = NULL;

    TbFixtureBegin();
    id = TbDoc();

    RoboCheckEqInt(HtmlTreeBuilderCreate(id, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(
        HtmlTreeBuilderCreate(RoboHtmlDocumentIdNone, &builder, NULL),
        RoboNotFound);
    RoboCheckEqInt(builder, 0);

    RoboCheckEqInt(HtmlTreeBuilderCreate(id, &builder, NULL), RoboOk);
    RoboCheck(builder != NULL);

    /* The document lease is exclusive: a second builder is refused. */
    {
        HtmlTreeBuilder *second = NULL;

        RoboCheckEqInt(HtmlTreeBuilderCreate(id, &second, NULL),
                          RoboBusy);
        RoboCheckEqInt(second, 0);
    }

    /* Nothing works before begin(). */
    RoboCheckEqInt(HtmlTreeBuilderNext(builder, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(
        HtmlTreeBuilderConsume(builder, NULL, NULL),
        RoboInvalidArgument);

    HtmlTreeBuilderDestroy(builder);
    /*
     * And the pointer is **spent** -- `destroy` frees the builder, so there is no second call to
     * make and no "already destroyed" answer to check. The magic-word guard inside `destroy`
     * cannot provide one: it lives in the struct the first call just freed, so asking it again
     * would read freed memory rather than report a no-op. `NULL` is the only argument a second
     * call can safely take, and that is the one asserted here.
     */
    builder = NULL;
    HtmlTreeBuilderDestroy(builder);
    HtmlTreeBuilderDestroy(NULL);

    /* The lease is gone, so a new builder can take it. */
    RoboCheckEqInt(HtmlTreeBuilderCreate(id, &builder, NULL), RoboOk);
    HtmlTreeBuilderDestroy(builder);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    TbFixtureEnd();
}

static void TestTbBeginMisuse(void)
{
    HtmlDocumentId id;
    HtmlTreeBuilder *builder = NULL;
    HtmlSource src;
    HtmlTreeResult result;
    HtmlParserState state;
    HtmlInsertionMode mode;
    HtmlTextMode textMode;
    HtmlNodeHandle node;

    TbFixtureBegin();
    id = TbDoc();
    RoboCheckEqInt(HtmlTreeBuilderCreate(id, &builder, NULL), RoboOk);

    /* No source → refused. NONE-kind source → refused. */
    RoboCheckEqInt(HtmlTreeBuilderBegin(builder, NULL, NULL),
                      RoboInvalidArgument);
    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlTreeBuilderBegin(builder, &src, NULL),
                      RoboInvalidArgument);

    /* Introspection before begin. */
    RoboCheckEqInt(HtmlTreeBuilderGetState(builder, &state), RoboOk);
    RoboCheckEqInt(state, RoboHtmlParserStateIdle);
    RoboCheckEqInt(HtmlTreeBuilderGetMode(builder, &mode), RoboOk);
    RoboCheckEqInt(mode, RoboHtmlModeInitial);
    RoboCheckEqInt(HtmlTreeBuilderGetTextMode(builder, &textMode),
                      RoboOk);
    RoboCheckEqInt(textMode, RoboHtmlTextModeNone);
    RoboCheckEqInt(HtmlTreeBuilderResult(builder, &result), RoboOk);
    RoboCheckEqInt(result.nodeCount, 0);
    RoboCheckEqInt(HtmlTreeBuilderDiagnosticCount(builder), 0);
    RoboCheckEqInt(
        HtmlTreeBuilderCurrentNode(builder, &node), RoboOk);
    RoboCheck(HtmlNodeHandleEqual(&node, &(HtmlNodeHandle){
                                                     id, 0u, 0u}) == 0);

    /* Text mode cannot change before the build starts. */
    RoboCheckEqInt(HtmlTreeBuilderSetTextMode(
                          builder, RoboHtmlTextModeRcdata, NULL),
                      RoboInitError);

    RoboCheckEqInt(HtmlSourceString(&src, "<p>x</p>", NULL), RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderBegin(builder, &src, NULL), RoboOk);

    /* Unknown text mode enumerator → rejected while building. */
    RoboCheckEqInt(HtmlTreeBuilderSetTextMode(
                          builder, (HtmlTextMode)33, NULL),
                      RoboInvalidArgument);

    /* A consume() of an ill-formed token is refused, not applied. */
    {
        HtmlToken tok;

        HtmlTokenInit(&tok);
        RoboCheckEqInt(HtmlTreeBuilderConsume(builder, &tok, NULL),
                          RoboInvalidArgument);
    }

    /* Re-entering begin() on an in-flight build is refused, not restarted. */
    RoboCheckEqInt(HtmlTreeBuilderBegin(builder, &src, NULL),
                      RoboError);

    /* That begin() attached the source, so drive it to completion. */
    {
        int i;

        for (i = 0; i < 64; i++) {
            HtmlParserState step = RoboHtmlParserStateIdle;

            RoboCheckEqInt(HtmlTreeBuilderNext(builder, NULL), RoboOk);
            RoboCheckEqInt(HtmlTreeBuilderGetState(builder, &step),
                              RoboOk);
            if (step == RoboHtmlParserStateComplete) {
                break;
            }
        }
    }
    RoboCheckEqInt(HtmlTreeBuilderGetState(builder, &state), RoboOk);
    RoboCheckEqInt(state, RoboHtmlParserStateComplete);
    /* finish() is idempotent, and build() re-parses from a complete state. */
    RoboCheckEqInt(HtmlTreeBuilderFinish(builder, NULL), RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderBuild(builder, &src, NULL), RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderResult(builder, &result), RoboOk);
    RoboCheckEqInt(result.elementCount, 4); /* html head body p */

    HtmlTreeBuilderDestroy(builder);
    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Document structure                                                  */
/* ------------------------------------------------------------------ */

static void TestTbEmptyDocument(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;

    TbFixtureBegin();
    id = TbDoc();
    RoboCheck(HtmlDocumentIsEmpty(id));

    TbBuild(id, "", &r);
    RoboCheck(!HtmlDocumentIsEmpty(id));
    /* Browsers synthesize html/head/body even for empty input. */
    RoboCheckEqInt(r.doctypeCount, 0);
    RoboCheckEqInt(r.commentCount, 0);
    RoboCheckEqInt(r.textCount, 0);
    RoboCheckEqInt(r.elementCount, 3);
    RoboCheckEqInt(r.nodeCount, 4);
    RoboCheckEqInt(r.maxDepth, 2);
    RoboCheckEqInt(r.recoveredErrors, 0);
    RoboCheckEqInt(r.truncated, 0);
    RoboCheckEqInt(HtmlDocumentNodeCount(id), 4);

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    TbFixtureEnd();
}

static void TestTbBasicDocument(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    char buf[64];

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id,
             "<!DOCTYPE html><html><head><title>T</title></head>"
             "<body><h1>H</h1></body></html>",
             &r);

    RoboCheckEqInt(r.doctypeCount, 1);
    RoboCheckEqInt(r.elementCount, 5); /* html head title body h1 */
    RoboCheckEqInt(r.textCount, 2);
    RoboCheckEqInt(r.commentCount, 0);
    RoboCheckEqInt(r.recoveredErrors, 0);
    RoboCheckEqInt(r.maxDepth, 3); /* html > head > title */

    TbTextOf(id, "title", buf, sizeof(buf));
    RoboCheckStrEq(buf, "T");
    TbTextOf(id, "h1", buf, sizeof(buf));
    RoboCheckStrEq(buf, "H");
    TbTextOf(id, "head", buf, sizeof(buf));
    RoboCheckStrEq(buf, "T");
    TbTextOf(id, "body", buf, sizeof(buf));
    RoboCheckStrEq(buf, "H");

    /* The document node owns the tree, and the DOM boundary reports it. */
    {
        HtmlDomCapabilities caps;

        RoboCheckEqInt(HtmlDomQueryCapabilities(&caps), RoboOk);
        RoboCheckEqInt(caps.treeBuilding, 1);
        RoboCheckEqInt(caps.domNodes, 0);
        RoboCheckEqInt(caps.events, 0);
    }

    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbImpliedStructuralElements(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    char buf[64];

    TbFixtureBegin();

    /* Missing <html>: implied around the content. */
    id = TbDoc();
    TbBuild(id, "<p>hi</p>", &r);
    RoboCheckEqInt(r.elementCount, 4); /* html head body p */
    TbTextOf(id, "body", buf, sizeof(buf));
    RoboCheckStrEq(buf, "hi");
    (void)HtmlDocumentDestroy(id, NULL);

    /* Missing <head>: implied before <body>. */
    id = TbDoc();
    TbBuild(id, "<html><body><p>x</p></body></html>", &r);
    RoboCheckEqInt(r.elementCount, 4);
    TbTextOf(id, "html", buf, sizeof(buf));
    RoboCheckStrEq(buf, "x");
    (void)HtmlDocumentDestroy(id, NULL);

    /* Missing <body>. */
    id = TbDoc();
    TbBuild(id, "<p>y</p>", &r);
    RoboCheckEqInt(r.elementCount, 4);
    TbTextOf(id, "body", buf, sizeof(buf));
    RoboCheckStrEq(buf, "y");
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

static void TestTbTextBeforeHtml(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle body;
    HtmlNodeHandle first;
    char buf[64];

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "stray<p>inside</p>", &r);

    body = TbFind(id, "body");
    RoboCheck(HtmlNodeHandleIsWellformed(&body));
    RoboCheckEqInt(TbChildCount(body), 2);
    RoboCheckEqInt(
        HtmlNodeChildAt(&body, 0u, &first, NULL), RoboOk);
    {
        HtmlNodeView view;

        RoboCheckEqInt(HtmlNodeGetView(&first, &view, NULL), RoboOk);
        RoboCheckEqInt(view.kind, RoboHtmlNodeKindText);
        RoboCheckStrEq(view.text, "stray");
    }
    TbText(body, buf, sizeof(buf));
    RoboCheckStrEq(buf, "strayinside");
    /* <head> exists but is empty of text. */
    TbTextOf(id, "head", buf, sizeof(buf));
    RoboCheckStrEq(buf, "");

    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbElementInsertionAndNesting(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle div;
    HtmlNodeHandle section;
    HtmlNodeHandle p;
    char buf[64];

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<div><section><p>a</p></section></div>", &r);

    RoboCheckEqInt(r.elementCount, 6); /* html head body div section p */
    RoboCheckEqInt(r.maxDepth, 5);     /* html > body > div > section > p */

    div = TbFind(id, "div");
    section = TbFind(id, "section");
    p = TbFind(id, "p");
    RoboCheck(HtmlNodeHandleIsWellformed(&div));
    RoboCheck(HtmlNodeHandleIsWellformed(&section));
    RoboCheck(HtmlNodeHandleIsWellformed(&p));

    {
        HtmlNodeView view;

        /* Parent/child relationships line up. */
        RoboCheckEqInt(HtmlNodeGetView(&section, &view, NULL), RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&view.parent, &div));
        RoboCheckEqInt(HtmlNodeGetView(&p, &view, NULL), RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&view.parent, &section));
        RoboCheckEqInt(TbChildCount(div), 1);
        RoboCheckEqInt(TbChildCount(section), 1);
        RoboCheckEqInt(TbChildCount(p), 1);
    }
    TbText(p, buf, sizeof(buf));
    RoboCheckStrEq(buf, "a");

    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbSiblingsAndChildOrder(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle body;
    char texts[4][16];
    size_t i;

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<p>one</p><p>two</p><span>three</span>", &r);

    body = TbFind(id, "body");
    RoboCheckEqInt(TbChildCount(body), 3);
    for (i = 0u; i < 3u; i++) {
        HtmlNodeHandle child;

        RoboCheckEqInt(HtmlNodeChildAt(&body, i, &child, NULL),
                          RoboOk);
        texts[i][0] = '\0';
        {
            size_t length = 0u;

            RoboCheckEqInt(
                HtmlNodeSubtreeText(&child, texts[i], sizeof(texts[i]),
                                            &length, NULL),
                RoboOk);
        }
    }
    RoboCheckStrEq(texts[0], "one");
    RoboCheckStrEq(texts[1], "two");
    RoboCheckStrEq(texts[2], "three");

    /* Out-of-range child lookups report NOT_FOUND, never a bogus handle. */
    {
        HtmlNodeHandle bogus;

        RoboCheckEqInt(HtmlNodeChildAt(&body, 9u, &bogus, NULL),
                          RoboNotFound);
    }

    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbTextNodesAndMerging(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle p;
    char buf[128];

    TbFixtureBegin();
    id = TbDoc();

    /* A character reference in the middle must not fragment the text run. */
    TbBuild(id, "<p>Tom &amp; Jerry</p>", &r);
    RoboCheckEqInt(r.textCount, 1);
    p = TbFind(id, "p");
    RoboCheckEqInt(TbChildCount(p), 1);
    TbText(p, buf, sizeof(buf));
    RoboCheckStrEq(buf, "Tom & Jerry");
    (void)HtmlDocumentDestroy(id, NULL);

    /* Empty text is never materialized as a node. */
    id = TbDoc();
    TbBuild(id, "<p></p>", &r);
    RoboCheckEqInt(r.textCount, 0);
    p = TbFind(id, "p");
    RoboCheckEqInt(TbChildCount(p), 0);
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

static void TestTbComments(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle div;
    HtmlNodeHandle child;
    HtmlNodeView view;

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<!--lead--><div><!-- inner --></div><!--tail-->", &r);

    RoboCheckEqInt(r.commentCount, 3);
    /* Leading comment attaches to the document; trailing one too. */
    {
        HtmlNodeHandle root = TbRoot(id);
        HtmlNodeHandle first;

        RoboCheckEqInt(HtmlNodeChildAt(&root, 0u, &first, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeGetView(&first, &view, NULL), RoboOk);
        RoboCheckEqInt(view.kind, RoboHtmlNodeKindComment);
        RoboCheckStrEq(view.text, "lead");
    }
    div = TbFind(id, "div");
    RoboCheckEqInt(TbChildCount(div), 1);
    RoboCheckEqInt(HtmlNodeChildAt(&div, 0u, &child, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeGetView(&child, &view, NULL), RoboOk);
    RoboCheckEqInt(view.kind, RoboHtmlNodeKindComment);
    RoboCheckStrEq(view.text, " inner ");

    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbDoctype(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle root;
    HtmlNodeHandle child;
    HtmlNodeView view;
    HtmlElementView element;

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<!DOCTYPE html><p>x</p>", &r);
    RoboCheckEqInt(r.doctypeCount, 1);

    root = TbRoot(id);
    RoboCheckEqInt(HtmlNodeChildAt(&root, 0u, &child, NULL), RoboOk);
    RoboCheckEqInt(HtmlNodeGetView(&child, &view, NULL), RoboOk);
    RoboCheckEqInt(view.kind, RoboHtmlNodeKindDocumentType);
    RoboCheckStrEq(view.name, "html");
    (void)element;
    (void)HtmlDocumentDestroy(id, NULL);

    /* A malformed DOCTYPE is recovered, not fatal. */
    id = TbDoc();
    TbBuild(id, "<!DOCTYPE><p>y</p>", &r);
    RoboCheckEqInt(r.elementCount, 4);
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

static void TestTbAttributes(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle a;
    HtmlElementView view;
    HtmlAttribute attr;

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<a href=\"/x\" target='_blank' disabled>link</a>", &r);

    RoboCheckEqInt(r.attributeCount, 3);
    a = TbFind(id, "a");
    RoboCheckEqInt(HtmlElementViewGet(&a, &view, NULL), RoboOk);
    RoboCheckStrEq(view.name, "a");
    RoboCheckEqInt(view.attributeCount, 3);
    RoboCheckEqInt(view.selfClosing, 0);

    RoboCheckEqInt(HtmlElementFindAttribute(&a, "href", &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.value, "/x");
    RoboCheckEqInt(
        HtmlElementFindAttribute(&a, "TARGET", &attr, NULL), RoboOk);
    RoboCheckStrEq(attr.value, "_blank");
    /* A valueless attribute stores an empty string. */
    RoboCheckEqInt(
        HtmlElementFindAttribute(&a, "disabled", &attr, NULL), RoboOk);
    RoboCheckStrEq(attr.value, "");
    RoboCheckEqInt(
        HtmlElementFindAttribute(&a, "missing", &attr, NULL),
        RoboNotFound);

    /* Attribute order is source order. */
    RoboCheckEqInt(HtmlElementAttributeAt(&a, 0u, &attr, NULL),
                      RoboOk);
    RoboCheckStrEq(attr.name, "href");
    RoboCheckEqInt(HtmlElementAttributeAt(&a, 3u, &attr, NULL),
                      RoboNotFound);

    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbVoidAndSelfClosingElements(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle img;
    HtmlElementView view;
    char buf[64];

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<p>a<img src=\"x.png\"/>b<br>c</p>", &r);

    img = TbFind(id, "img");
    RoboCheck(HtmlNodeHandleIsWellformed(&img));
    RoboCheckEqInt(TbChildCount(img), 0);
    RoboCheckEqInt(HtmlElementViewGet(&img, &view, NULL), RoboOk);
    RoboCheckEqInt(view.selfClosing, 1);

    /* Void elements never swallow following content. */
    TbTextOf(id, "p", buf, sizeof(buf));
    RoboCheckStrEq(buf, "abc");
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

static void TestTbStartEndTagMatching(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle div;
    char buf[64];

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<div><p>a</p>b</div>", &r);

    div = TbFind(id, "div");
    RoboCheckEqInt(TbChildCount(div), 2);
    TbText(div, buf, sizeof(buf));
    RoboCheckStrEq(buf, "ab");
    /* A matching </p> closes exactly the open <p>. */
    RoboCheckEqInt(r.recoveredErrors, 0);
    (void)HtmlDocumentDestroy(id, NULL);

    /* A structural end tag pops back to it and drops inner elements. */
    id = TbDoc();
    TbBuild(id, "<div><span>a</div>b", &r);
    div = TbFind(id, "div");
    TbText(div, buf, sizeof(buf));
    RoboCheckStrEq(buf, "a");
    TbTextOf(id, "body", buf, sizeof(buf));
    RoboCheckStrEq(buf, "ab");
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

static void TestTbUnexpectedEndTags(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    char buf[64];

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "</div>a</span>", &r);

    RoboCheck(r.recoveredErrors >= 2);
    TbTextOf(id, "body", buf, sizeof(buf));
    RoboCheckStrEq(buf, "a");
    RoboCheckEqInt(r.elementCount, 3); /* html head body */
    (void)HtmlDocumentDestroy(id, NULL);

    /* </br> is re-run as <br>, per the HTML model. */
    id = TbDoc();
    TbBuild(id, "<p>a</br>b</p>", &r);
    RoboCheck(r.recoveredErrors >= 1);
    {
        HtmlNodeHandle br = TbFind(id, "br");

        RoboCheck(HtmlNodeHandleIsWellformed(&br));
    }
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

static void TestTbUnclosedElementsAtEof(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle div;
    char buf[64];

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<div><p>x", &r);

    RoboCheck(r.recoveredErrors >= 2); /* <div> and <p> left open */
    div = TbFind(id, "div");
    RoboCheck(HtmlNodeHandleIsWellformed(&div));
    TbText(div, buf, sizeof(buf));
    RoboCheckStrEq(buf, "x");
    /* Unclosed elements still end up linked into the tree. */
    RoboCheckEqInt(HtmlDocumentNodeCount(id), 7u);
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

static void TestTbDuplicateStructuralTags(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<html><html><body><body><p>x</p></body></html>", &r);

    RoboCheckEqInt(r.elementCount, 4); /* html head body p */
    RoboCheck(r.recoveredErrors >= 2);
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

static void TestTbTitleRcdata(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle title;
    char buf[64];

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<title>Roboticela &amp; Runtime</title><p>x</p>", &r);

    title = TbFind(id, "title");
    RoboCheck(HtmlNodeHandleIsWellformed(&title));
    TbText(title, buf, sizeof(buf));
    RoboCheckStrEq(buf, "Roboticela & Runtime");
    RoboCheckEqInt(r.textCount, 2);
    (void)HtmlDocumentDestroy(id, NULL);

    /* Markup inside RCDATA is text, not elements. */
    id = TbDoc();
    TbBuild(id, "<title><b>not an element</b></title>", &r);
    RoboCheckEqInt(r.elementCount, 4); /* html head title + implied body */
    TbTextOf(id, "title", buf, sizeof(buf));
    RoboCheckStrEq(buf, "<b>not an element</b>");
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

static void TestTbDivSpanButtonStructures(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle div;
    HtmlNodeHandle span;
    HtmlNodeHandle button;
    char buf[64];

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<div><span>t</span><button>Demo Button</button></div>", &r);

    div = TbFind(id, "div");
    span = TbFind(id, "span");
    button = TbFind(id, "button");
    RoboCheckEqInt(TbChildCount(div), 2);
    {
        HtmlNodeView view;

        RoboCheckEqInt(HtmlNodeGetView(&span, &view, NULL), RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&view.parent, &div));
    }
    TbText(button, buf, sizeof(buf));
    RoboCheckStrEq(buf, "Demo Button");
    (void)HtmlDocumentDestroy(id, NULL);

    /* Nested <button> closes the outer one instead of nesting. */
    id = TbDoc();
    TbBuild(id, "<button>a<button>b</button>", &r);
    RoboCheck(r.recoveredErrors >= 1);
    RoboCheckEqInt(HtmlDocumentNodeCount(id), 8u);
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

static void TestTbAnchorImageFormInput(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle form;
    HtmlNodeHandle input;
    HtmlElementView view;
    char buf[64];

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id,
             "<a href=\"/x\">link</a>"
             "<form action=\"/search\"><input name=\"q\" value=\"v\"></form>"
             "<img src=\"logo.png\" alt=\"logo\">",
             &r);

    {
        HtmlNodeHandle a = TbFind(id, "a");

        RoboCheck(HtmlNodeHandleIsWellformed(&a));
    }
    TbTextOf(id, "a", buf, sizeof(buf));
    RoboCheckStrEq(buf, "link");

    form = TbFind(id, "form");
    input = TbFind(id, "input");
    RoboCheckEqInt(TbChildCount(form), 1);
    {
        HtmlNodeView nodeView;

        RoboCheckEqInt(HtmlNodeGetView(&input, &nodeView, NULL),
                          RoboOk);
        RoboCheck(HtmlNodeHandleEqual(&nodeView.parent, &form));
    }
    RoboCheckEqInt(HtmlElementViewGet(&input, &view, NULL), RoboOk);
    RoboCheckEqInt(view.attributeCount, 2);
    RoboCheckEqInt(TbChildCount(input), 0);

    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbOpenElementStack(void)
{
    HtmlDocumentId id;
    HtmlTreeBuilder *builder = NULL;
    HtmlSource src;
    HtmlNodeHandle node;
    HtmlNodeView view;
    size_t count = 99u;

    TbFixtureBegin();
    id = TbDoc();
    RoboCheckEqInt(HtmlTreeBuilderCreate(id, &builder, NULL), RoboOk);
    RoboCheckEqInt(HtmlSourceString(&src, "<div><p>x</p></div>", NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderBegin(builder, &src, NULL), RoboOk);
    RoboCheckEqInt(
        HtmlTreeBuilderOpenElementCount(builder, &count), RoboOk);
    RoboCheckEqInt(count, 0);
    RoboCheckEqInt(HtmlTreeBuilderOpenElement(builder, 0u, &node),
                      RoboNotFound);

    /*
     * Drive token by token: <div> <p> x — the stack is html, body, div, p
     * with <p> as the current node.
     */
    {
        int i;

        for (i = 0; i < 3; i++) {
            RoboCheckEqInt(HtmlTreeBuilderNext(builder, NULL), RoboOk);
        }
    }
    RoboCheckEqInt(
        HtmlTreeBuilderCurrentNode(builder, &node), RoboOk);
    RoboCheckEqInt(HtmlNodeGetView(&node, &view, NULL), RoboOk);
    RoboCheckStrEq(view.name, "p");
    RoboCheckEqInt(
        HtmlTreeBuilderOpenElementCount(builder, &count), RoboOk);
    RoboCheckEqInt(count, 4); /* html, body, div, p */

    {
        HtmlNodeHandle outer;

        RoboCheckEqInt(HtmlTreeBuilderOpenElement(builder, 0u, &outer),
                          RoboOk);
        RoboCheckEqInt(HtmlNodeGetView(&outer, &view, NULL), RoboOk);
        RoboCheckStrEq(view.name, "html");
    }

    while (HtmlTreeBuilderNext(builder, NULL) == RoboOk) {
        HtmlParserState state;

        RoboCheckEqInt(HtmlTreeBuilderGetState(builder, &state),
                          RoboOk);
        if (state == RoboHtmlParserStateComplete) {
            break;
        }
    }
    RoboCheckEqInt(
        HtmlTreeBuilderOpenElementCount(builder, &count), RoboOk);
    RoboCheckEqInt(count, 0);

    HtmlTreeBuilderDestroy(builder);
    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbInsertionModeTransitions(void)
{
    HtmlDocumentId id;
    HtmlTreeBuilder *builder = NULL;
    HtmlSource src;
    HtmlInsertionMode mode;
    HtmlTextMode textMode;
    HtmlTreeResult result;

    TbFixtureBegin();
    id = TbDoc();
    RoboCheckEqInt(HtmlTreeBuilderCreate(id, &builder, NULL), RoboOk);
    RoboCheckEqInt(
        HtmlSourceString(&src, "<html><head><title>T</title></head>"
                                      "<body><h1>H</h1></body></html>",
                                 NULL),
        RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderBegin(builder, &src, NULL), RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderGetMode(builder, &mode), RoboOk);
    RoboCheckEqInt(mode, RoboHtmlModeInitial);

    /* <html> */
    RoboCheckEqInt(HtmlTreeBuilderNext(builder, NULL), RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderGetMode(builder, &mode), RoboOk);
    RoboCheckEqInt(mode, RoboHtmlModeBeforeHead);
    /* <head> */
    RoboCheckEqInt(HtmlTreeBuilderNext(builder, NULL), RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderGetMode(builder, &mode), RoboOk);
    RoboCheckEqInt(mode, RoboHtmlModeInHead);
    /* <title> → TEXT/RCDATA */
    RoboCheckEqInt(HtmlTreeBuilderNext(builder, NULL), RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderGetMode(builder, &mode), RoboOk);
    RoboCheckEqInt(mode, RoboHtmlModeText);
    RoboCheckEqInt(HtmlTreeBuilderGetTextMode(builder, &textMode),
                      RoboOk);
    RoboCheckEqInt(textMode, RoboHtmlTextModeRcdata);

    /*
     * Drive the rest of the stream. build() would re-begin the build, so
     * finish the in-flight one through next() instead.
     */
    {
        int i;

        for (i = 0; i < 64; i++) {
            HtmlParserState state;

            RoboCheckEqInt(HtmlTreeBuilderNext(builder, NULL), RoboOk);
            RoboCheckEqInt(HtmlTreeBuilderGetState(builder, &state),
                              RoboOk);
            if (state == RoboHtmlParserStateComplete) {
                break;
            }
        }
    }
    RoboCheckEqInt(HtmlTreeBuilderGetMode(builder, &mode), RoboOk);
    /* Trailing </html> leaves the builder in the final mode. */
    RoboCheckEqInt(mode, RoboHtmlModeAfterAfterBody);
    RoboCheckEqInt(HtmlTreeBuilderGetTextMode(builder, &textMode),
                      RoboOk);
    RoboCheckEqInt(textMode, RoboHtmlTextModeNone);
    RoboCheckEqInt(HtmlTreeBuilderResult(builder, &result), RoboOk);
    RoboCheckEqInt(result.elementCount, 5);
    {
        HtmlDocumentLifecycle life;

        RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
        /*
         * The build consumed a complete (non-stream) source, so parsing
         * finished and no further input can arrive: the document is done.
         */
        RoboCheckEqInt(life, RoboHtmlDocumentLifecycleComplete);
    }

    HtmlTreeBuilderDestroy(builder);
    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbOwnershipAndStaleHandles(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle h1;
    HtmlNodeKind kind = RoboHtmlNodeKindFragment;

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<h1>H</h1>", &r);

    h1 = TbFind(id, "h1");
    RoboCheckEqInt(HtmlNodeHandleResolve(&h1, &kind), RoboOk);
    RoboCheckEqInt(kind, RoboHtmlNodeKindElement);
    RoboCheck(!HtmlNodeHandleIsStale(&h1));

    /* Rebuilding the document retires every previously handed-out node. */
    TbBuild(id, "<p>P</p>", &r);
    RoboCheck(HtmlNodeHandleIsStale(&h1));
    RoboCheckEqInt(HtmlNodeHandleResolve(&h1, &kind), RoboNotFound);

    /* Ill-formed handles are argument errors, not stale lookups. */
    {
        HtmlNodeHandle bogus;

        HtmlNodeHandleClear(&bogus);
        RoboCheckEqInt(HtmlNodeHandleResolve(&bogus, &kind),
                          RoboInvalidArgument);        RoboCheckEqInt(HtmlNodeHandleResolve(NULL, &kind),
                          RoboInvalidArgument);
        /* outKind is optional, so a pure validation succeeds on a live node. */
        {
            HtmlNodeHandle live = TbFind(id, "p");

            RoboCheckEqInt(HtmlNodeHandleResolve(&live, NULL), RoboOk);
            RoboCheckEqInt(HtmlNodeHandleResolve(&h1, NULL),
                              RoboNotFound);
        }
        RoboCheckEqInt(HtmlNodeHandleResolve(&(HtmlNodeHandle){
                              9999u, 1u, 1u},
                                                          &kind),
                          RoboNotFound);
    }

    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbDocumentTeardown(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    HtmlNodeHandle root;
    HtmlNodeHandle h1;

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<div><p>x</p></div>", &r);
    root = TbRoot(id);
    h1 = TbFind(id, "div");

    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboOk);
    /* Every node handle is retired with its document. */
    RoboCheck(HtmlNodeHandleIsStale(&root));
    RoboCheck(HtmlNodeHandleIsStale(&h1));
    RoboCheckEqInt(HtmlDocumentNodeCount(id), 0);
    RoboCheck(!HtmlDocumentIsLive(id));
    RoboCheckEqInt(HtmlDocumentDestroy(id, NULL), RoboNotFound);
    /* Building into a destroyed document fails cleanly. */
    {
        HtmlSource src;

        HtmlSourceInit(&src);
        RoboCheckEqInt(HtmlSourceString(&src, "<p>x</p>", NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlBuildTree(id, &src, NULL, NULL),
                          RoboNotFound);
    }

    TbFixtureEnd();
}

static void TestTbMalformedRecovery(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    char buf[128];

    TbFixtureBegin();

    /* Unclosed div with nested span: closes on </div>, text continues. */
    id = TbDoc();
    TbBuild(id, "<div><p>a<span>b</div>c", &r);
    RoboCheckEqInt(HtmlDocumentNodeCount(id), 10u);
    TbTextOf(id, "body", buf, sizeof(buf));
    RoboCheckStrEq(buf, "abc");
    (void)HtmlDocumentDestroy(id, NULL);

    /* Stray markup characters are text, not elements. */
    id = TbDoc();
    TbBuild(id, "<p>1 < 2 > 3</p>", &r);
    TbTextOf(id, "p", buf, sizeof(buf));
    RoboCheckStrEq(buf, "1 < 2 > 3");
    (void)HtmlDocumentDestroy(id, NULL);

    /* An element that is never closed still carries its text. */
    id = TbDoc();
    TbBuild(id, "<p>unterminated", &r);
    RoboCheckEqInt(r.elementCount, 4);
    TbTextOf(id, "p", buf, sizeof(buf));
    RoboCheckStrEq(buf, "unterminated");
    (void)HtmlDocumentDestroy(id, NULL);

    /* Deeply-mismatched end tags still leave a usable tree. */
    id = TbDoc();
    TbBuild(id, "</p></p></div></body></html></html>", &r);
    RoboCheck(r.recoveredErrors >= 1);
    RoboCheckEqInt(r.elementCount, 3);
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

static void TestTbLargeNestedTree(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    char html[4096];
    size_t used = 0u;
    int i;

    TbFixtureBegin();
    id = TbDoc();
    for (i = 0; i < 200; i++) {
        html[used++] = '<';
        html[used++] = 'd';
        html[used++] = 'i';
        html[used++] = 'v';
        html[used++] = '>';
    }
    html[used++] = 'x';
    html[used] = '\0';

    TbBuild(id, html, &r);
    RoboCheckEqInt(r.elementCount, 203); /* html head body + 200 divs */
    RoboCheckEqInt(r.maxDepth, 202);
    RoboCheckEqInt(r.truncated, 0);
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

static void TestTbNodeCeilingTruncates(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    char html[32768];
    size_t used = 0u;
    int i;

    TbFixtureBegin();
    id = TbDoc();
    for (i = 0; i < 4200 && used + 6u < sizeof(html); i++) {
        html[used++] = '<';
        html[used++] = 's';
        html[used++] = 'p';
        html[used++] = 'a';
        html[used++] = 'n';
        html[used++] = '>';
    }
    html[used] = '\0';

    /* Each <span> costs one node plus its element payload: over the ceiling. */
    RoboCheckEqInt(HtmlBuildTree(id, &(HtmlSource){0}, NULL,
                                          NULL),
                      RoboInvalidArgument);
    TbBuild(id, html, &r);
    RoboCheckEqInt(r.truncated, 1);
    RoboCheck(r.nodeCount <= RoboHtmlMaxNodesPerDocument);
    RoboCheck(HtmlLiveDocumentCount() >= 1u);

    /* The document is still usable and still owns a valid tree. */
    {
        HtmlNodeHandle root = TbRoot(id);

        RoboCheck(HtmlNodeHandleIsWellformed(&root));
    }
    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbUnicodeText(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    char buf[256];
    const char *text = "h\xc3\xa9llo \xe2\x80\x94 \xe4\xb8\x96\xe7\x95\x8c "
                       "\xf0\x9f\x98\x80";

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<p>h\xc3\xa9llo \xe2\x80\x94 \xe4\xb8\x96\xe7\x95\x8c "
                 "\xf0\x9f\x98\x80</p>",
             &r);

    TbTextOf(id, "p", buf, sizeof(buf));
    RoboCheckStrEq(buf, text);
    RoboCheckEqInt(r.textCount, 1);
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Tree dump                                                           */
/* ------------------------------------------------------------------ */

static Result TbCollectSink(const char *text, size_t length, void *user)
{
    char *buffer = (char *)user;
    size_t used = strlen(buffer);

    memcpy(buffer + used, text, length);
    buffer[used + length] = '\0';
    return RoboOk;
}

static Result TbFailingSink(const char *text, size_t length, void *user)
{
    (void)text;
    (void)length;
    (void)user;
    return RoboError;
}

static void TestTbTreeDump(void)
{
    HtmlDocumentId id;
    static char buffer[8192];
    size_t length = 0u;

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id, "<!DOCTYPE html><html><head><title>RoboticelaOS Runtime"
                 "</title></head><body><h1>Heading</h1><p>Body &amp; text</p>"
                 "<button>Demo Button</button><img src=\"a.png\"/></body></html>",
             NULL);

    buffer[0] = '\0';
    RoboCheckEqInt(HtmlTreeDump(id, TbCollectSink, buffer, NULL),
                      RoboOk);
    RoboCheck(strstr(buffer, "Document") != NULL);
    RoboCheck(strstr(buffer, "doctype html") != NULL);
    RoboCheck(strstr(buffer, "  html\n") != NULL);
    RoboCheck(strstr(buffer, "    head\n") != NULL);
    RoboCheck(strstr(buffer, "        Text: \"RoboticelaOS Runtime\"\n") !=
               NULL);
    RoboCheck(strstr(buffer, "    body\n") != NULL);
    RoboCheck(strstr(buffer, "      h1\n") != NULL);
    RoboCheck(strstr(buffer, "        Text: \"Heading\"") != NULL);
    RoboCheck(strstr(buffer, "\"Body & text\"") != NULL);
    RoboCheck(strstr(buffer, "button\n") != NULL);
    RoboCheck(strstr(buffer, "\"Demo Button\"") != NULL);
    RoboCheck(strstr(buffer, "img src=\"a.png\" /") != NULL);
    RoboCheck(strstr(buffer, "#comment") == NULL);

    /* Buffer variant reports the untruncated length. */
    {
        char small[24];

        RoboCheckEqInt(
            HtmlTreeDumpToBuffer(id, small, sizeof(small), &length,
                                          NULL),
            RoboOk);
        RoboCheck(length > sizeof(small));
        RoboCheckEqInt((int)strlen(small), (int)sizeof(small) - 1);
    }

    /* Misuse: NULL sink, unknown document, empty buffer. */
    RoboCheckEqInt(HtmlTreeDump(id, NULL, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(
        HtmlTreeDump(RoboHtmlDocumentIdNone, TbCollectSink, NULL,
                            NULL),
        RoboNotFound);
    RoboCheckEqInt(HtmlTreeDumpToBuffer(id, NULL, 32u, &length, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(
        HtmlTreeDumpToBuffer(id, buffer, 0u, &length, NULL),
        RoboInvalidArgument);
    /* A sink that refuses aborts the dump with its own code. */
    RoboCheckEqInt(HtmlTreeDump(id, TbFailingSink, NULL, NULL),
                      RoboError);

    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Integration + the visual-demo document                              */
/* ------------------------------------------------------------------ */

static void TestTbTokenizerIntegration(void)
{
    HtmlDocumentId id;
    HtmlTreeBuilder *builder = NULL;
    HtmlSource src;
    HtmlTreeResult result;
    size_t diagnostics;
    size_t i;

    TbFixtureBegin();
    id = TbDoc();
    RoboCheckEqInt(HtmlTreeBuilderCreate(id, &builder, NULL), RoboOk);
    RoboCheckEqInt(
        HtmlSourceString(&src,
                                "<!DOCTYPE html><div id=\"a\">one<!--c--></div>",
                                NULL),
        RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderBegin(builder, &src, NULL), RoboOk);

    /* Bounded: never more iterations than the input can produce. */
    for (i = 0u; i < 64u; i++) {
        HtmlParserState state;

        RoboCheckEqInt(HtmlTreeBuilderNext(builder, NULL), RoboOk);
        RoboCheckEqInt(HtmlTreeBuilderGetState(builder, &state),
                          RoboOk);
        if (state == RoboHtmlParserStateComplete) {
            break;
        }
    }
    RoboCheck(i < 64u);

    RoboCheckEqInt(HtmlTreeBuilderResult(builder, &result), RoboOk);
    RoboCheckEqInt(result.doctypeCount, 1);
    RoboCheckEqInt(result.elementCount, 4); /* html head body div */
    RoboCheckEqInt(result.commentCount, 1);
    RoboCheckEqInt(result.attributeCount, 1);
    RoboCheckEqInt(result.recoveredErrors, 0);

    diagnostics = HtmlTreeBuilderDiagnosticCount(builder);
    for (i = 0u; i < diagnostics; i++) {
        HtmlDiag diag;

        RoboCheckEqInt(
            HtmlTreeBuilderGetDiagnostic(builder, i, &diag), RoboOk);
        RoboCheckEqInt(diag.domain, RoboHtmlDomainParser);
    }
    RoboCheckEqInt(HtmlTreeBuilderGetDiagnostic(builder, diagnostics,
                                                            &(HtmlDiag){0}),
                      RoboNotFound);

    HtmlTreeBuilderDestroy(builder);

    /* Destroying the builder leaves the tree owned by the document. */
    RoboCheckEqInt(HtmlDocumentNodeCount(id), 8u);
    {
        HtmlDocumentLifecycle life;

        RoboCheckEqInt(HtmlDocumentGetLifecycle(id, &life), RoboOk);
        /*
         * The build consumed a complete (non-stream) source, so parsing
         * finished and no further input can arrive: the document is done.
         */
        RoboCheckEqInt(life, RoboHtmlDocumentLifecycleComplete);
    }
    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbDiagnosticsForRecovery(void)
{
    HtmlDocumentId id;
    HtmlTreeBuilder *builder = NULL;
    HtmlSource src;
    HtmlDiag diag;
    size_t count;

    TbFixtureBegin();
    id = TbDoc();
    RoboCheckEqInt(HtmlTreeBuilderCreate(id, &builder, NULL), RoboOk);
    RoboCheckEqInt(HtmlSourceString(&src, "</div><span>a", NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlTreeBuilderBuild(builder, &src, NULL), RoboOk);

    count = HtmlTreeBuilderDiagnosticCount(builder);
    RoboCheck(count >= 2u); /* stray end tag + unclosed <span> */
    RoboCheckEqInt(HtmlTreeBuilderGetDiagnostic(builder, 0u, &diag),
                      RoboOk);
    RoboCheckEqInt(diag.code, RoboParseError);
    RoboCheckEqInt(diag.domain, RoboHtmlDomainParser);
    RoboCheck(diag.message != NULL);

    HtmlTreeBuilderDestroy(builder);
    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbDemoDocumentTree(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    char buf[256];

    TbFixtureBegin();
    id = TbDoc();
    TbBuild(id,
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
             &r);

    RoboCheckEqInt(r.doctypeCount, 1);
    RoboCheckEqInt(r.elementCount, 8); /* html head title body h1 p p button */
    RoboCheckEqInt(r.truncated, 0);
    RoboCheckEqInt(r.recoveredErrors, 0);

    TbTextOf(id, "title", buf, sizeof(buf));
    RoboCheckStrEq(buf, "RoboticelaOS Runtime");
    TbTextOf(id, "h1", buf, sizeof(buf));
    RoboCheckStrEq(buf, "RoboticelaOS Runtime");
    TbTextOf(id, "button", buf, sizeof(buf));
    RoboCheckStrEq(buf, "Demo Button");

    /* The two paragraphs are siblings under body, in source order. */
    {
        HtmlNodeHandle body = TbFind(id, "body");
        HtmlNodeHandle child;
        int seenP = 0;

        /* 6 text (body, before/after each block, and after </body>) + 4 elements */
        RoboCheckEqInt(TbChildCount(body), 10u);
        {
            size_t i;
            size_t children = TbChildCount(body);

            for (i = 0u; i < children; i++) {
                HtmlElementView view;

                RoboCheckEqInt(HtmlNodeChildAt(&body, i, &child, NULL),
                                  RoboOk);
                if (HtmlElementViewGet(&child, &view, NULL) == RoboOk &&
                    strcmp(view.name, "p") == 0) {
                    seenP++;
                }
            }
        }
        RoboCheckEqInt(seenP, 2);
    }

    (void)HtmlDocumentDestroy(id, NULL);
    TbFixtureEnd();
}

static void TestTbWhitespaceIsPreservedAsText(void)
{
    HtmlDocumentId id;
    HtmlTreeResult r;
    char buf[32];

    TbFixtureBegin();
    /* Indentation between body children becomes real text nodes. */
    id = TbDoc();
    TbBuild(id, "<body>\n  <p>a</p>\n  <p>b</p>\n</body>", &r);
    RoboCheckEqInt(r.elementCount, 5); /* html head body p p */
    RoboCheckEqInt(r.textCount, 5);    /* "\n  " a "\n  " b "\n" */
    TbTextOf(id, "body", buf, sizeof(buf));
    RoboCheckStrEq(buf, "\n  a\n  b\n");
    (void)HtmlDocumentDestroy(id, NULL);

    /* Whitespace before <html> is dropped, not stored. */
    id = TbDoc();
    TbBuild(id, "\n\n  <p>x</p>", &r);
    RoboCheckEqInt(r.textCount, 1);
    (void)HtmlDocumentDestroy(id, NULL);

    TbFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlTreeBuilder(void)
{
    TestRegister("html.tree.model_helpers", TestTbModelHelpers);
    TestRegister("html.tree.create_misuse", TestTbCreateMisuse);
    TestRegister("html.tree.begin_misuse", TestTbBeginMisuse);
    TestRegister("html.tree.empty_document", TestTbEmptyDocument);
    TestRegister("html.tree.basic_document", TestTbBasicDocument);
    TestRegister("html.tree.implied_structure",
                       TestTbImpliedStructuralElements);
    TestRegister("html.tree.text_before_html", TestTbTextBeforeHtml);
    TestRegister("html.tree.nesting",
                       TestTbElementInsertionAndNesting);
    TestRegister("html.tree.siblings", TestTbSiblingsAndChildOrder);
    TestRegister("html.tree.text_merge", TestTbTextNodesAndMerging);
    TestRegister("html.tree.comments", TestTbComments);
    TestRegister("html.tree.doctype", TestTbDoctype);
    TestRegister("html.tree.attributes", TestTbAttributes);
    TestRegister("html.tree.void_elements",
                       TestTbVoidAndSelfClosingElements);
    TestRegister("html.tree.tag_matching",
                       TestTbStartEndTagMatching);
    TestRegister("html.tree.unexpected_end_tags",
                       TestTbUnexpectedEndTags);
    TestRegister("html.tree.unclosed_at_eof",
                       TestTbUnclosedElementsAtEof);
    TestRegister("html.tree.duplicate_structural",
                       TestTbDuplicateStructuralTags);
    TestRegister("html.tree.title_rcdata", TestTbTitleRcdata);
    TestRegister("html.tree.div_span_button",
                       TestTbDivSpanButtonStructures);
    TestRegister("html.tree.anchor_image_form",
                       TestTbAnchorImageFormInput);
    TestRegister("html.tree.open_element_stack",
                       TestTbOpenElementStack);
    TestRegister("html.tree.insertion_modes",
                       TestTbInsertionModeTransitions);
    TestRegister("html.tree.ownership",
                       TestTbOwnershipAndStaleHandles);
    TestRegister("html.tree.teardown", TestTbDocumentTeardown);
    TestRegister("html.tree.recovery", TestTbMalformedRecovery);
    TestRegister("html.tree.large_tree", TestTbLargeNestedTree);
    TestRegister("html.tree.node_ceiling", TestTbNodeCeilingTruncates);
    TestRegister("html.tree.unicode", TestTbUnicodeText);
    TestRegister("html.tree.dump", TestTbTreeDump);
    TestRegister("html.tree.tokenizer_integration",
                       TestTbTokenizerIntegration);
    TestRegister("html.tree.diagnostics", TestTbDiagnosticsForRecovery);
    TestRegister("html.tree.demo_document", TestTbDemoDocumentTree);
    TestRegister("html.tree.whitespace", TestTbWhitespaceIsPreservedAsText);
}
