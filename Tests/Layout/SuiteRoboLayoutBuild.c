/*
 * RoboLayout builder suite (Phase 10.1) — layout.build.*
 *
 * DOM tree in, layout tree out. One rule wide:
 *
 *   an element generates a box unless its computed `display` says otherwise
 *
 * Everything asserted here is that rule or a consequence of it, and the tests are
 * written so that the *reason* the rule is what it is shows up as a failing assert
 * rather than as a comment:
 *
 *   `display: none` generates no box -- and neither does anything inside it, so a
 *   hidden root is one skipped element and its descendants are never visited at
 *   all. That is a different count from "visited and refused", and the report
 *   keeps them apart;
 *
 *   non-element nodes are stepped over, so the boxes are in *document order*
 *   while the document stores a newline between every pair of them -- the
 *   difference between a layout tree and a copy of the DOM's child list;
 *
 *   a box with no computed style is a box with no style and a flag, not a missing
 *   box: "this element generates a box" is a fact about layout and "nothing
 *   styled it" is a fact about CSS.
 *
 * The style source is the fake from the support header, so these are structural
 * tests: a suite that needed a style context, a property table, a registry and a
 * sheet to prove sibling order would be an integration test wearing a structural
 * test's name. The real pipeline is layout.dom.* and layout.conformance.*.
 */

#include "RoboLayoutTestSupport.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The document element is the root box                                */
/* ------------------------------------------------------------------ */

static void TestBuildRoot(void)
{
    LayoutDocument doc;
    LayoutTree *tree = NULL;
    LayoutBuildReport report;
    LayoutNodeId root = RoboLayoutNodeIdNone;
    HtmlNodeHandle element;
    uint32_t depth = 99u;
    int pass;

    LayoutFixture();
    LayoutDocumentBuild(&doc, "<html><body><p>text</p></body></html>");
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);

    /* Repeated, because "the tree is replaced, not merged" is the property a
     * rebuild relies on and a single build cannot show it. */
    for (pass = 0; pass < 3; pass++) {
        memset(&report, 0xAB, sizeof(report));
        RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, NULL, &report,
                                                               NULL),
                          RoboOk);

        /*
         * Four elements: html, head, body, p. `<head>` is not in the markup --
         * RoboHTML's tree builder inserts it, because a document with a `<body>`
         * has a head -- and the *number is derived from the DOM* rather than
         * guessed, because how many elements a fragment parses to is the parser's
         * fact and not this suite's.
         *
         * The text node inside `<p>` is not an element and is not counted as one:
         * since 10.5 it becomes a **text object** instead, and the two counters it
         * lands in are its own (RoboLayoutBuilder.h).
         */
        RoboCheckEqInt(LayoutDocumentElementCount(&doc), 4u);
        RoboCheckEqInt(report.elementsSeen, 4u);
        RoboCheckEqInt(report.boxesCreated, 4u);
        RoboCheckEqInt(report.textsCreated, 1u);
        RoboCheckEqInt(report.skippedDisplayNone, 0u);
        RoboCheckEqInt(report.boxesWithoutStyle, 4u);
        /* html 0, head 1, body 1, p 2 -- and the deepest *object* is now the text
         * inside p, at 3, because a text object is an object with a depth and not a
         * child the builder steps over. */
        RoboCheckEqInt(report.depthMax, 3u);
        RoboCheckEqInt(report.depthMax, LayoutTreeMaxDepth(tree));
        RoboCheckEqInt(LayoutTreeObjectCount(tree),
                          report.boxesCreated + report.textsCreated);

        /* The root box is the *document element*, not the document node: a
         * document is not an element, generates nothing, and a box of its own
         * would have no style and no parent. */
        root = LayoutTreeRoot(tree);
        RoboCheckEqInt(LayoutObjectIsRoot(tree, root), 1);
        element = LayoutDocumentElement(&doc);
        RoboCheckEqInt((int)LayoutObjectIdForDomNode(tree, &element),
                          (int)root);
        RoboCheckEqInt(LayoutObjectDepth(tree, root, &depth), RoboOk);
        RoboCheckEqInt(depth, 0u);
    }

    LayoutTreeDestroy(tree);
    LayoutDocumentRelease(&doc);
}

/* ------------------------------------------------------------------ */
/* Document order and element-only children                            */
/* ------------------------------------------------------------------ */

#define LAYOUT_BUILD_ORDER_HTML      \
    "<html>\n"                       \
    "  <head>\n"                     \
    "    <title>t</title>\n"         \
    "  </head>\n"                    \
    "  <body>\n"                     \
    "    <section>\n"                \
    "      <p>a</p>\n"               \
    "      <p>b</p>\n"               \
    "      <p>c</p>\n"               \
    "    </section>\n"               \
    "    <footer></footer>\n"        \
    "  </body>\n"                    \
    "</html>\n"

static void TestBuildOrder(void)
{
    LayoutDocument doc;
    LayoutTree *tree = NULL;
    LayoutBuildReport report;
    LayoutNodeId root;
    LayoutNodeId head = RoboLayoutNodeIdNone;
    LayoutNodeId body = RoboLayoutNodeIdNone;
    LayoutNodeId section = RoboLayoutNodeIdNone;
    LayoutNodeId p = RoboLayoutNodeIdNone;
    size_t seen = 0u;

    LayoutFixture();
    LayoutDocumentBuild(&doc, LAYOUT_BUILD_ORDER_HTML);
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, NULL, &report, NULL),
                      RoboOk);

    /* html, head, title, body, section, p, p, p, footer -- nine elements, all of
     * them in the markup this time. html 0, head 1, title 2, body 1, section 2,
     * p 3, footer 2, so the deepest *box* is a paragraph at depth 3 -- and the text
     * inside the three paragraphs sits at 4, which is what the report now says. */
    RoboCheckEqInt(LayoutDocumentElementCount(&doc), 9u);
    RoboCheckEqInt(report.elementsSeen, 9u);
    RoboCheckEqInt(report.boxesCreated, 9u);
    RoboCheckEqInt(report.textsCreated, 4u); /* title's t, and a, b, c */
    RoboCheckEqInt(report.depthMax, 4u);
    RoboCheckEqInt(report.depthMax, LayoutTreeMaxDepth(tree));
    RoboCheckEqInt(LayoutTreeObjectCount(tree),
                      report.boxesCreated + report.textsCreated);

    /*
     * The newline between every pair of elements is a text node, so a builder
     * that walked *nodes* would produce nine boxes with the sibling order half
     * whitespace. This walks the tree's own element links and checks the tags, so
     * the assertion is about the document rather than about ids.
     */
    root = LayoutTreeRoot(tree);
    RoboCheckEqInt(LayoutObjectChildCount(tree, root, &seen), RoboOk);
    RoboCheckEqInt(seen, 2u); /* head, body, and no text between them */
    RoboCheckEqInt(LayoutObjectFirstChild(tree, root, &head), RoboOk);
    RoboCheckEqInt(LayoutObjectLastChild(tree, root, &body), RoboOk);
    {
        HtmlNodeHandle handle;

        RoboCheckEqInt(LayoutObjectDomNode(tree, head, &handle), RoboOk);
        RoboCheckStrEq(HtmlElementName(&handle, NULL), "head");
        RoboCheckEqInt(LayoutObjectDomNode(tree, body, &handle), RoboOk);
        RoboCheckStrEq(HtmlElementName(&handle, NULL), "body");
        /* The first child of `<html>` is `<head>`, not the text before it. */
        RoboCheckEqInt(LayoutObjectDepth(tree, head, &report.depthMax), RoboOk);
        RoboCheckEqInt(report.depthMax, 1u);
    }

    /* Siblings are linked both ways: `<head>`'s next is `<body>` and back. */
    {
        LayoutNodeId next = RoboLayoutNodeIdNone;

        RoboCheckEqInt(LayoutObjectNextSibling(tree, head, &next), RoboOk);
        RoboCheckEqInt((int)next, (int)body);
        RoboCheckEqInt(LayoutObjectPreviousSibling(tree, body, &next), RoboOk);
        RoboCheckEqInt((int)next, (int)head);
        /* Neither end of a sibling list has a neighbour, which is a miss. */
        RoboCheckEqInt(LayoutObjectPreviousSibling(tree, head, &next),
                          RoboNotFound);
        RoboCheckEqInt(LayoutObjectNextSibling(tree, body, &next), RoboNotFound);
    }

    /* Three `<p>` under `<section>`, in order, each holding one child: its own text
     * node, which generates a text object (10.5) and no box. */
    RoboCheckEqInt(LayoutObjectFirstChild(tree, body, &section), RoboOk);
    RoboCheckEqInt(LayoutObjectChildCount(tree, body, &seen), RoboOk);
    RoboCheckEqInt(seen, 2u); /* section, footer */
    {
        HtmlNodeHandle handle;

        RoboCheckEqInt(LayoutObjectDomNode(tree, section, &handle), RoboOk);
        RoboCheckStrEq(HtmlElementName(&handle, NULL), "section");
    }

    RoboCheckEqInt(LayoutObjectFirstChild(tree, section, &p), RoboOk);
    {
        size_t paragraphs = 0u;

        for (;;) {
            LayoutNodeId after = RoboLayoutNodeIdNone;
            HtmlNodeHandle handle;

            RoboCheckEqInt(LayoutObjectChildCount(tree, p, &seen), RoboOk);
            RoboCheckEqInt(seen, 1u); /* the paragraph's text object */
            RoboCheckEqInt(LayoutObjectDomNode(tree, p, &handle), RoboOk);
            RoboCheckStrEq(HtmlElementName(&handle, NULL), "p");
            paragraphs++;
            if (LayoutObjectNextSibling(tree, p, &after) != RoboOk) {
                break;
            }
            p = after;
        }
        RoboCheckEqInt(paragraphs, 3u);
    }

    LayoutTreeDestroy(tree);
    LayoutDocumentRelease(&doc);
}

/* ------------------------------------------------------------------ */
/* display: none                                                       */
/* ------------------------------------------------------------------ */

static void TestBuildDisplayNone(void)
{
    /*
     * The hidden element has a tag of its own (`<aside>`), because the style
     * source is keyed by tag name: a document with three `<div>`s and one of them
     * hidden could not be expressed through a source that answers per tag, and a
     * test that could not express the case would be testing a different one.
     */
    static const char *const html =
        "<html>\n"
        "  <body>\n"
        "    <div class=\"a\"></div>\n"
        "    <aside class=\"hidden\">\n"
        "      <span></span>\n"
        "      <span></span>\n"
        "    </aside>\n"
        "    <div class=\"c\"></div>\n"
        "  </body>\n"
        "</html>\n";

    LayoutDocument doc;
    LayoutTree *tree = NULL;
    LayoutFakeSource fake;
    LayoutStyleSource styles;
    LayoutBuildOptions options;
    LayoutBuildReport report;
    HtmlNodeHandle aside;
    HtmlNodeHandle span;

    LayoutFixture();
    LayoutDocumentBuild(&doc, html);
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);

    LayoutFakeInit(&fake);
    LayoutFakeAdd(&fake, "html", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "body", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "div", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "aside", RoboCssDisplayNone);
    LayoutFakeAdd(&fake, "span", RoboCssDisplayBlock);
    styles = LayoutFakeSourceOf(&fake);
    LayoutBuildOptionsInit(&options);
    options.styles = &styles;

    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, &options, &report,
                                                           NULL),
                      RoboOk);

    /*
     * html, head, body, div.a, div.c = 5 boxes. The `<aside>` is refused, and the
     * two `<span>`s inside it are never reached -- a `display: none` element takes
     * its whole subtree with it, which is the rule applied one level down rather
     * than a special case. Six elements are *seen* and only one is counted
     * skipped: the spans are not "skipped", they are never visited, and the report
     * must not conflate the two.
     */
    RoboCheckEqInt(LayoutDocumentElementCount(&doc), 8u);
    RoboCheckEqInt(report.boxesCreated, 5u);
    RoboCheckEqInt(report.skippedDisplayNone, 1u);
    RoboCheckEqInt(report.elementsSeen, 6u);
    RoboCheckEqInt(report.boxesCreated + report.skippedDisplayNone, 6u);
    RoboCheckEqInt(LayoutTreeObjectCount(tree), 5u);

    /* The hidden element has no box, so the reverse lookup answers NONE -- which
     * is also what an element nobody built answers, which is why the *report* is
     * the door that tells "hidden" from "never reached". */
    aside = LayoutDocumentFind(&doc, "aside");
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&aside), 1);
    RoboCheckEqInt((int)LayoutObjectIdForDomNode(tree, &aside),
                      (int)RoboLayoutNodeIdNone);

    span = LayoutDocumentFind(&doc, "span");
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&span), 1);
    RoboCheckEqInt((int)LayoutObjectIdForDomNode(tree, &span),
                      (int)RoboLayoutNodeIdNone);

    /* The two visible `<div>`s are *adjacent siblings*: the hidden element is
     * simply absent from the tree rather than a hole in it, so `body` has two
     * children and the first one's next sibling is the last. */
    {
        LayoutNodeId root = LayoutTreeRoot(tree);
        LayoutNodeId body = RoboLayoutNodeIdNone;
        LayoutNodeId first = RoboLayoutNodeIdNone;
        LayoutNodeId last = RoboLayoutNodeIdNone;
        LayoutNodeId next = RoboLayoutNodeIdNone;
        HtmlNodeHandle handle;
        size_t count = 0u;

        /* `body` is the *last* child of `<html>`: `<head>` comes first. Taking the
         * first child here would make every assert below about the head. */
        RoboCheckEqInt(LayoutObjectLastChild(tree, root, &body), RoboOk);
        {
            HtmlNodeHandle handle;

            RoboCheckEqInt(LayoutObjectDomNode(tree, body, &handle), RoboOk);
            RoboCheckStrEq(HtmlElementName(&handle, NULL), "body");
        }
        RoboCheckEqInt(LayoutObjectChildCount(tree, body, &count), RoboOk);
        RoboCheckEqInt(count, 2u);
        RoboCheckEqInt(LayoutObjectFirstChild(tree, body, &first), RoboOk);
        RoboCheckEqInt(LayoutObjectLastChild(tree, body, &last), RoboOk);
        RoboCheck((int)first != (int)last);
        RoboCheckEqInt(LayoutObjectNextSibling(tree, first, &next), RoboOk);
        RoboCheckEqInt((int)next, (int)last);
        /* And the box after the hidden element is the last `<div>`, not the `a`
         * one -- so the hidden element was skipped in place rather than
         * substituted for. */
        RoboCheckEqInt(LayoutObjectDomNode(tree, last, &handle), RoboOk);
        RoboCheckEqInt(HtmlNodeHandleEqual(&handle, &aside), 0);
        RoboCheckEqInt(HtmlNodeHandleEqual(&handle, &span), 0);
    }

    LayoutTreeClear(tree);

    /*
     * The same document with `<span>` hidden instead: the identical four boxes,
     * but the two spans are now *seen and skipped* rather than never reached --
     * one word of stylesheet, five digits of difference in the report.
     */
    LayoutFakeDispose(&fake);
    LayoutFakeInit(&fake);
    LayoutFakeAdd(&fake, "html", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "body", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "div", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "aside", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "span", RoboCssDisplayNone);
    styles = LayoutFakeSourceOf(&fake);
    options.styles = &styles;

    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, &options, &report,
                                                           NULL),
                      RoboOk);
    /* html, head, body, div.a, aside, div.c = 6 boxes; the two spans are skipped. */
    RoboCheckEqInt(report.boxesCreated, 6u);
    RoboCheckEqInt(report.skippedDisplayNone, 2u);
    RoboCheckEqInt(report.elementsSeen, 8u);
    RoboCheckEqInt(report.elementsSeen, LayoutDocumentElementCount(&doc));

    /* `<aside>` is a box now, and it has no children: that is what "its subtree
     * generated nothing" looks like from the tree's side. */
    {
        LayoutNodeId asideBox = LayoutObjectIdForDomNode(tree, &aside);
        size_t count = 99u;

        RoboCheckEqInt(LayoutObjectIsLive(tree, asideBox), 1);
        RoboCheckEqInt(LayoutObjectHasChildren(tree, asideBox), 0);
        RoboCheckEqInt(LayoutObjectChildCount(tree, asideBox, &count), RoboOk);
        RoboCheckEqInt(count, 0u);
    }

    LayoutFakeDispose(&fake);
    LayoutTreeDestroy(tree);
    LayoutDocumentRelease(&doc);
}

static void TestBuildRootDisplayNone(void)
{
    LayoutDocument doc;
    LayoutTree *tree = NULL;
    LayoutFakeSource fake;
    LayoutStyleSource styles;
    LayoutBuildOptions options;
    LayoutBuildReport report;

    LayoutFixture();
    LayoutDocumentBuild(&doc, "<html><body><p>x</p></body></html>");
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
    LayoutFakeInit(&fake);
    LayoutFakeAdd(&fake, "html", RoboCssDisplayNone);
    styles = LayoutFakeSourceOf(&fake);
    LayoutBuildOptionsInit(&options);
    options.styles = &styles;

    /* The whole document generates nothing. An empty tree is the honest answer,
     * and the report says why -- so "no boxes" is never mistaken for "no
     * document". */
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, &options, &report,
                                                           NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeObjectCount(tree), 0u);
    RoboCheckEqInt(LayoutTreeIsEmpty(tree), 1);
    RoboCheckEqInt(report.boxesCreated, 0u);
    RoboCheckEqInt(report.elementsSeen, 1u);
    RoboCheckEqInt(report.skippedDisplayNone, 1u);

    /* And a pass over that empty tree is legitimate rather than an error. */
    {
        LayoutContext *context = NULL;
        LayoutStats stats;

        RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
        RoboCheckEqInt(LayoutContextRun(context, tree, NULL), RoboOk);
        RoboCheckEqInt((int)LayoutContextOutcome(context),
                          (int)RoboLayoutPassWalked);
        LayoutContextStats(context, &stats);
        RoboCheckEqInt(stats.objects, 0u);
        RoboCheckEqInt(stats.visited, 0u);
        LayoutContextDestroy(context);
    }

    LayoutFakeDispose(&fake);
    LayoutTreeDestroy(tree);
    LayoutDocumentRelease(&doc);
}

/* ------------------------------------------------------------------ */
/* Styles: present, absent, and the flag                               */
/* ------------------------------------------------------------------ */

static void TestBuildStyles(void)
{
    LayoutDocument doc;
    LayoutTree *tree = NULL;
    LayoutFakeSource fake;
    LayoutStyleSource styles;
    LayoutBuildOptions options;
    LayoutBuildReport report;
    LayoutNodeId id;
    size_t index;

    LayoutFixture();
    LayoutDocumentBuild(&doc, "<html><body><p>x</p><span>y</span></body></html>");
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);

    /* No options at all: every box is built with STYLE_MISSING, and the count
     * says so. A caller building a tree without CSS is doing something
     * legitimate, and it is told what it has. */
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, NULL, &report, NULL),
                      RoboOk);
    RoboCheckEqInt(report.elementsSeen, 5u);
    RoboCheckEqInt(report.boxesCreated, 5u);
    RoboCheckEqInt(report.boxesWithoutStyle, 5u);
    for (index = 0u; index < 5u; index++) {
        RoboCheckEqInt(LayoutObjectIdAt(tree, index, &id), RoboOk);
        RoboCheckEqInt(LayoutObjectHasFlag(tree, id,
                                                      RoboLayoutObjectFlagStyleMissing),
                          1);
        RoboCheck(LayoutObjectStyle(tree, id) == NULL);
        /* The display a box with no style is built under is the property's
         * *initial* value, because a non-inherited property nothing declared is
         * answered by its initial value rather than by an entry. */
        RoboCheckEqInt((int)LayoutObjectDisplay(tree, id),
                          (int)RoboCssDisplayInline);
    }

    /* Options with a source that answers two of four: the two styled boxes carry
     * the pointer, the two others carry the flag. */
    LayoutFakeInit(&fake);
    LayoutFakeAdd(&fake, "html", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "body", RoboCssDisplayBlock);
    styles = LayoutFakeSourceOf(&fake);
    LayoutBuildOptionsInit(&options);
    options.styles = &styles;

    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, &options, &report,
                                                           NULL),
                      RoboOk);
    RoboCheckEqInt(report.boxesCreated, 5u);
    /* html and body answered, head/p/span did not: three boxes without a style. */
    RoboCheckEqInt(report.boxesWithoutStyle, 3u);
    /* The source was actually consulted once per element, and answered for the
     * two the fake knows about: five elements, two answers. */
    RoboCheckEqInt(fake.answers, 2u);

    {
        HtmlNodeHandle html = LayoutDocumentFind(&doc, "html");
        LayoutNodeId htmlBox = LayoutObjectIdForDomNode(tree, &html);

        RoboCheckEqInt(LayoutObjectHasFlag(tree, htmlBox,
                                                      RoboLayoutObjectFlagStyleMissing),
                          0);
        RoboCheck(LayoutObjectStyle(tree, htmlBox) == fake.rules[0].style);
        RoboCheckEqInt((int)LayoutObjectDisplay(tree, htmlBox),
                          (int)RoboCssDisplayBlock);
    }
    {
        HtmlNodeHandle p = LayoutDocumentFind(&doc, "p");
        LayoutNodeId pBox = LayoutObjectIdForDomNode(tree, &p);

        RoboCheckEqInt(LayoutObjectHasFlag(tree, pBox,
                                                      RoboLayoutObjectFlagStyleMissing),
                          1);
        RoboCheck(LayoutObjectStyle(tree, pBox) == NULL);
        /* The element `<p>` generated a box even though nothing styled it: the two
         * facts are separate, which is the whole reason the flag exists. */
        RoboCheckEqInt(LayoutObjectIsLive(tree, pBox), 1);
    }

    LayoutFakeDispose(&fake);
    LayoutTreeDestroy(tree);
    LayoutDocumentRelease(&doc);
}

/* ------------------------------------------------------------------ */
/* Rebuild replaces                                                    */
/* ------------------------------------------------------------------ */

static void TestBuildRebuildReplaces(void)
{
    LayoutDocument doc;
    LayoutTree *tree = NULL;
    LayoutBuildReport first;
    LayoutBuildReport second;

    LayoutFixture();
    LayoutDocumentBuild(&doc, "<html><body><p>x</p></body></html>");
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);

    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, NULL, &first, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeObjectCount(tree),
                      first.boxesCreated + first.textsCreated);
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, NULL, &second, NULL),
                      RoboOk);

    /* A build clears first, so it always leaves the boxes of exactly one
     * document -- not one document's boxes twice. The tree is derived state and
     * there is no incremental path in 10.1 because nothing consumes a tree finely
     * enough to need one. */
    RoboCheckEqInt(LayoutTreeObjectCount(tree),
                      second.boxesCreated + second.textsCreated);
    RoboCheckEqInt(first.boxesCreated, second.boxesCreated);
    RoboCheckEqInt(first.textsCreated, second.textsCreated);
    RoboCheckEqInt((int)LayoutTreeRoot(tree), 1);

    LayoutTreeDestroy(tree);
    LayoutDocumentRelease(&doc);
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

static void TestBuildValidation(void)
{
    LayoutDocument doc;
    LayoutTree *tree = NULL;
    LayoutBuildReport report;
    LayoutBuildOptions options;
    LayoutDiag diag = RoboLayoutCleared;

    LayoutFixture();
    LayoutDocumentBuild(&doc, "<html><body></body></html>");
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);

    RoboCheckEqInt(LayoutTreeBuildFromDocument(NULL, doc.id, NULL, NULL, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.code, RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainBuilder);

    LayoutDiagReset(&diag);
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, RoboHtmlDocumentIdNone,
                                                           NULL, NULL, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainBuilder);

    /* A document that has gone is NOT_FOUND, which is a different answer from a
     * caller passing nonsense. */
    LayoutDiagReset(&diag);
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree,
                                                           (HtmlDocumentId)99999u,
                                                           NULL, NULL, &diag),
                      RoboNotFound);
    RoboCheckEqInt(diag.code, RoboNotFound);

    /* The report is optional, and an absent one is not a failure. */
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, NULL, NULL, NULL),
                      RoboOk);

    /* A report passed to a *failing* call is zeroed rather than left stale, so a
     * caller that reads it after a failure sees what was done rather than what the
     * last successful build did. */
    memset(&report, 0xAB, sizeof(report));
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, RoboHtmlDocumentIdNone,
                                                           NULL, &report, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(report.boxesCreated, 0u);
    RoboCheckEqInt(report.elementsSeen, 0u);
    RoboCheckEqInt(report.skippedDisplayNone, 0u);

    /* Options initialise to "no styles", and a NULL options pointer means the
     * same thing. */
    LayoutBuildOptionsInit(&options);
    RoboCheck(options.styles == NULL);
    LayoutBuildOptionsInit(NULL);

    LayoutTreeDestroy(tree);
    LayoutDocumentRelease(&doc);
}

/* ------------------------------------------------------------------ */
/* A document with no element at all                                   */
/* ------------------------------------------------------------------ */

static void TestBuildNoElement(void)
{
    LayoutDocument doc;
    LayoutTree *tree = NULL;
    LayoutBuildReport report;

    LayoutFixture();
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);

    /*
     * Empty markup does *not* produce a document with no element: RoboHTML's tree
     * builder synthesises the elements a well-formed document requires, so the
     * input that produces no element at all is a document whose tree was never
     * built. Asserted rather than assumed, because the difference decides whether
     * the builder has a "nothing to build" path that anything can reach.
     */
    LayoutDocumentBuild(&doc, "");
    RoboCheckEqInt(LayoutDocumentElementCount(&doc), 3u); /* html, head, body */
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, NULL, &report, NULL),
                      RoboOk);
    RoboCheckEqInt(report.elementsSeen, 3u);
    RoboCheckEqInt(report.boxesCreated, 3u);
    RoboCheckEqInt(LayoutTreeIsEmpty(tree), 0);

    /* Text with no markup is the same document, not an empty one: a text node is
     * not an element and generates nothing, but the document around it is a
     * document. */
    LayoutDocumentRelease(&doc);
    LayoutDocumentBuild(&doc, "just text, no markup at all");
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, NULL, &report, NULL),
                      RoboOk);
    RoboCheckEqInt(report.elementsSeen, 3u);
    RoboCheckEqInt(report.boxesCreated, 3u);

    LayoutDocumentRelease(&doc);

    /*
     * The zero-box build is a document whose tree was never built: the document
     * object exists, and it has no document element for the layout root to be. That
     * path has to answer OK with an empty tree rather than fail, because a caller
     * that builds before parsing is doing something legitimate.
     */
    {
        LayoutDocument bare;

        LayoutFixture();
        bare.id = RoboHtmlDocumentIdNone;
        RoboCheckEqInt(HtmlDocumentCreate(&bare.id, NULL), RoboOk);
        LayoutTreeClear(tree);
        RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, bare.id, NULL, &report,
                                                              NULL),
                          RoboOk);
        RoboCheckEqInt(report.boxesCreated, 0u);
        RoboCheckEqInt(report.elementsSeen, 0u);
        RoboCheckEqInt(report.skippedDisplayNone, 0u);
        RoboCheckEqInt(LayoutTreeIsEmpty(tree), 1);
        LayoutDocumentRelease(&bare);
    }

    LayoutTreeDestroy(tree);
}

/* ------------------------------------------------------------------ */
/* A deep document                                                     */
/* ------------------------------------------------------------------ */

static void TestBuildDeep(void)
{
    /*
     * Deep enough to be well past any comfortable recursion budget, and built
     * rather than written out so the depth is a constant rather than a count of
     * lines somebody has to keep in step. The builder's recursion is bounded by
     * the document's own node ceiling, and this is the test that the bound is
     * real rather than assumed.
     */
    enum { LAYOUT_BUILD_DEPTH = 128 };
    char html[LAYOUT_BUILD_DEPTH * 32 + 64];
    size_t offset = 0u;
    size_t i;
    LayoutDocument doc;
    LayoutTree *tree = NULL;
    LayoutBuildReport report;

    LayoutFixture();

    for (i = 0u; i < (size_t)LAYOUT_BUILD_DEPTH; i++) {
        int written = snprintf(html + offset, sizeof(html) - offset, "<div>");

        if (written <= 0 || (size_t)written >= sizeof(html) - offset) {
            break;
        }
        offset += (size_t)written;
    }
    for (i = 0u; i < (size_t)LAYOUT_BUILD_DEPTH; i++) {
        int written = snprintf(html + offset, sizeof(html) - offset, "</div>");

        if (written <= 0 || (size_t)written >= sizeof(html) - offset) {
            break;
        }
        offset += (size_t)written;
    }
    html[offset] = '\0';
    RoboCheck(offset > 0u);
    /* The buffer held the whole document: a truncated fixture would build a
     * shallower tree and the depth assert below would be about the buffer. */
    RoboCheck(offset < sizeof(html) - 1u);

    LayoutDocumentBuild(&doc, html);
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, NULL, &report, NULL),
                      RoboOk);

    /*
     * The parser wraps the bare fragment, so the document is html, head, body and
     * then the 128 divs. The count is asserted against the DOM's own walk *and*
     * against the arithmetic, so a wrapper change shows up here rather than as a
     * mysterious depth.
     */
    RoboCheckEqInt(LayoutDocumentElementCount(&doc), (size_t)LAYOUT_BUILD_DEPTH + 3u);
    RoboCheckEqInt(report.elementsSeen, (size_t)LAYOUT_BUILD_DEPTH + 3u);
    RoboCheckEqInt(report.boxesCreated, (size_t)LAYOUT_BUILD_DEPTH + 3u);
    RoboCheckEqInt(LayoutTreeObjectCount(tree),
                      (size_t)LAYOUT_BUILD_DEPTH + 3u);

    /* html 0, head 1, body 1, div1 2 ... div128 129. */
    RoboCheckEqInt(report.depthMax, (uint32_t)(LAYOUT_BUILD_DEPTH + 1));
    RoboCheckEqInt(report.depthMax, LayoutTreeMaxDepth(tree));

    /*
     * And the chain is real rather than merely deep: walking down `lastChild` from
     * the root reaches every div, one level at a time, with the depth matching the
     * number of steps. A tree that lost a link when the arena grew would stop
     * early, and the step count is what says so.
     */
    {
        LayoutNodeId id = LayoutTreeRoot(tree);
        uint32_t depth = 0u;
        size_t steps = 0u;

        for (;;) {
            LayoutNodeId child = RoboLayoutNodeIdNone;

            RoboCheckEqInt(LayoutObjectDepth(tree, id, &depth), RoboOk);
            RoboCheckEqInt(depth, (uint32_t)steps);
            if (steps >= (size_t)LAYOUT_BUILD_DEPTH + 1u) {
                break;
            }
            if (LayoutObjectLastChild(tree, id, &child) != RoboOk) {
                break;
            }
            id = child;
            steps++;
        }
        RoboCheckEqInt(steps, (size_t)LAYOUT_BUILD_DEPTH + 1u);
        RoboCheckEqInt(depth, (uint32_t)(LAYOUT_BUILD_DEPTH + 1));
    }

    LayoutTreeDestroy(tree);
    LayoutDocumentRelease(&doc);
}

/* ------------------------------------------------------------------ */
/* The allocator returns to its baseline                               */
/* ------------------------------------------------------------------ */

static void TestBuildAllocatorBalance(void)
{
    size_t before;
    LayoutDocument doc;
    LayoutFakeSource fake;
    LayoutStyleSource styles;
    LayoutBuildOptions options;
    int pass;

    LayoutFixture();
    LayoutDocumentBuild(&doc, "<html><body><section><p>a</p></section></body></html>");
    LayoutFakeInit(&fake);
    LayoutFakeAdd(&fake, "html", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "body", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "section", RoboCssDisplayBlock);
    LayoutFakeAdd(&fake, "p", RoboCssDisplayBlock);
    styles = LayoutFakeSourceOf(&fake);
    LayoutBuildOptionsInit(&options);
    options.styles = &styles;

    before = LayoutLiveBlocks();
    for (pass = 0; pass < 16; pass++) {
        LayoutTree *tree = NULL;

        RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
        RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, &options, NULL,
                                                               NULL),
                          RoboOk);
        /* Building into a tree that already has boxes replaces, which is the path
         * a rebuild takes and the one a leak would hide in. */
        RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, &options, NULL,
                                                               NULL),
                          RoboOk);
        /* And a failing build must not leak either. */
        RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, RoboHtmlDocumentIdNone,
                                                               &options, NULL, NULL),
                          RoboInvalidArgument);
        LayoutTreeDestroy(tree);
    }
    RoboCheckEqInt(LayoutLiveBlocks(), before);

    LayoutFakeDispose(&fake);
    LayoutDocumentRelease(&doc);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutBuild(void);

void TestRegisterLayoutBuild(void)
{
    TestRegister("layout.build.root", TestBuildRoot);
    TestRegister("layout.build.order", TestBuildOrder);
    TestRegister("layout.build.display_none", TestBuildDisplayNone);
    TestRegister("layout.build.root_display_none", TestBuildRootDisplayNone);
    TestRegister("layout.build.styles", TestBuildStyles);
    TestRegister("layout.build.rebuild", TestBuildRebuildReplaces);
    TestRegister("layout.build.validation", TestBuildValidation);
    TestRegister("layout.build.no_element", TestBuildNoElement);
    TestRegister("layout.build.deep", TestBuildDeep);
    TestRegister("layout.build.allocator_balance", TestBuildAllocatorBalance);
}
