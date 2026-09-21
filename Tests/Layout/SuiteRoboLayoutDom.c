/*
 * RoboLayout's DOM bridge suite (Phase 10.1) — layout.dom.*
 *
 * The first place in the runtime where RoboHTML and RoboCSS meet, and the only
 * suite in this directory that drives the *real* pipeline: a real document, a real
 * author sheet, the real cascade, and one computed style per element owned by the
 * bridge so a tree can borrow it.
 *
 * Four things only this suite can prove, and each is asserted against the failure
 * its absence would cause rather than against the code that implements it:
 *
 *   the element reference is the DOM node id, so a style computed for one element
 *   is never returned for another -- and an id the bridge does not describe
 *   answers *nothing* rather than somebody else's style;
 *
 *   styles are owned by the bridge, so the pointer the tree stores is the pointer
 *   the *source* returns. That is what makes the lifetime rule checkable rather
 *   than a comment, and it is why the tree is destroyed before the bridge in every
 *   teardown here;
 *
 *   element facts are captured before any style is computed, because computing one
 *   walks its ancestors. A bridge that computed during the capture pass would
 *   answer about ancestors it had not captured yet, and the symptom is a
 *   descendant/sibling combinator that matches nothing -- which is exactly what
 *   layout.dom.relations drives;
 *
 *   `display: none` reaches the tree as an absent box rather than a zero-sized
 *   one, through a real sheet rather than a scripted answer.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A bridge over a real document                                       */
/* ------------------------------------------------------------------ */

typedef struct layoutDomFixture {
    LayoutDocument doc;
    LayoutCssFixture css;
    LayoutDom *dom;
} LayoutDomFixture;

/**
 * Build a document, bring a real style context up, parse @p css into it, and
 * create the bridge. Nothing is refreshed: a caller that wants to see the
 * before-refresh state has to say so.
 */
static void LayoutDomFixtureBegin(LayoutDomFixture *fixture,
                                     const char *html,
                                     const char *css)
{
    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    LayoutCssBegin(&fixture->css);
    if (css) {
        LayoutCssSheet(&fixture->css, css);
    }
    RoboCheckEqInt(LayoutDomCreate(fixture->css.context, fixture->doc.id,
                                             &fixture->dom, NULL),
                      RoboOk);
    RoboCheck(fixture->dom != NULL);
}

/**
 * Tear down in the one order that is legal: the tree first (it borrows the
 * bridge's styles), then the bridge, then the style context it borrowed.
 */
static void LayoutDomFixtureEnd(LayoutDomFixture *fixture)
{
    LayoutDomDestroy(fixture->dom);
    fixture->dom = NULL;
    LayoutCssEnd(&fixture->css);
    LayoutDocumentRelease(&fixture->doc);
}

/**
 * The computed display the bridge holds for the element at @p index of the
 * capture order, which is document order.
 *
 * Going through the capture order rather than by tag name is deliberate: three
 * `<div>`s in one document cannot be told apart by `HtmlNodeFindElement`,
 * and a suite that could only reach the first of them would quietly assert about
 * the wrong element.
 */
static CssDisplay LayoutDomDisplayAt(LayoutDomFixture *fixture, size_t index)
{
    HtmlNodeHandle element;

    RoboCheckEqInt(LayoutDomElementAt(fixture->dom, index, &element, NULL),
                      RoboOk);
    return LayoutStyleDisplay(LayoutDomStyleFor(fixture->dom, &element));
}

/* ------------------------------------------------------------------ */
/* Lifecycle and validation                                            */
/* ------------------------------------------------------------------ */

static void TestDomLifecycle(void)
{
    LayoutCssFixture css;
    LayoutDocument doc;
    LayoutDom *dom = NULL;
    LayoutStyleSource source;
    LayoutDiag diag = RoboLayoutCleared;
    HtmlNodeHandle handle;

    LayoutFixture();
    LayoutDocumentBuild(&doc, "<html><body></body></html>");
    LayoutCssBegin(&css);

    /* Null arguments and a document that is gone are the two refusals, and they
     * are told apart. */
    RoboCheckEqInt(LayoutDomCreate(NULL, doc.id, &dom, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(diag.code, RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainDom);
    RoboCheckEqInt(LayoutDomCreate(css.context, doc.id, NULL, NULL),
                      RoboInvalidArgument);
    LayoutDiagReset(&diag);
    RoboCheckEqInt(LayoutDomCreate(css.context, RoboHtmlDocumentIdNone, &dom,
                                             &diag),
                      RoboNotFound);
    RoboCheck(dom == NULL);
    LayoutDiagReset(&diag);
    RoboCheckEqInt(LayoutDomCreate(css.context, (HtmlDocumentId)99999u,
                                             &dom, &diag),
                      RoboNotFound);
    RoboCheckEqInt(diag.code, RoboNotFound);

    /* A bridge that has captured nothing answers nothing, and every read door
     * answers a sane thing rather than crashing. */
    RoboCheckEqInt(LayoutDomCreate(css.context, doc.id, &dom, NULL), RoboOk);
    RoboCheckEqInt(LayoutDomElementCount(dom), 0u);
    RoboCheckEqInt(LayoutDomIsReady(dom), 0);
    RoboCheckEqInt((int)LayoutDomDocument(dom), (int)doc.id);
    RoboCheckEqInt(LayoutDomWithoutStyleCount(dom), 0u);
    RoboCheckEqInt(LayoutDomElementAt(dom, 0u, &handle, NULL), RoboNotFound);

    /* The source is usable *before* a refresh and answers nothing until one has
     * run: a caller that wants a tree with no styles should get one with no
     * styles rather than an error. */
    RoboCheckEqInt(LayoutDomStyleSource(dom, &source, NULL), RoboOk);
    RoboCheckEqInt(LayoutStyleSourceIsUsable(&source), 1);
    {
        HtmlNodeHandle html = LayoutDocumentElement(&doc);

        RoboCheck(LayoutStyleSourceStyle(&source, &html) == NULL);
    }

    /* NULL reads answer rather than fault. */
    RoboCheckEqInt(LayoutDomElementCount(NULL), 0u);
    RoboCheckEqInt(LayoutDomIsReady(NULL), 0);
    RoboCheckEqInt(LayoutDomWithoutStyleCount(NULL), 0u);
    RoboCheckEqInt((int)LayoutDomDocument(NULL), (int)RoboHtmlDocumentIdNone);
    RoboCheck(LayoutDomStyleFor(NULL, &handle) == NULL);
    RoboCheck(LayoutDomStyleFor(dom, NULL) == NULL);
    RoboCheckEqInt(LayoutDomElementAt(NULL, 0u, &handle, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutDomElementAt(dom, 0u, NULL, NULL), RoboInvalidArgument);
    LayoutDiagReset(&diag);
    RoboCheckEqInt(LayoutDomRefresh(NULL, &diag), RoboInvalidArgument);
    RoboCheckEqInt(diag.code, RoboInvalidArgument);
    RoboCheckEqInt(LayoutDomStyleSource(NULL, &source, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutDomStyleSource(dom, NULL, NULL), RoboInvalidArgument);
    LayoutDomDestroy(NULL);

    LayoutDomDestroy(dom);
    LayoutCssEnd(&css);
    LayoutDocumentRelease(&doc);
}

/* ------------------------------------------------------------------ */
/* Capture                                                             */
/* ------------------------------------------------------------------ */

static void TestDomCapture(void)
{
    LayoutDomFixture fixture;
    HtmlNodeHandle handle;
    size_t index;

    LayoutDomFixtureBegin(&fixture,
                             "<html>\n"
                             "  <body>\n"
                             "    <div class=\"panel\"><p>Hello</p></div>\n"
                             "    <span></span>\n"
                             "  </body>\n"
                             "</html>\n",
                             "html, body, div, p, span { display: block }\n"
                             ".panel { display: inline-block }\n");

    /* Before a refresh: nothing captured, and the bridge says so rather than
     * guessing from the document. */
    RoboCheckEqInt(LayoutDomElementCount(fixture.dom), 0u);
    RoboCheckEqInt(LayoutDomIsReady(fixture.dom), 0);

    RoboCheckEqInt(LayoutDomRefresh(fixture.dom, NULL), RoboOk);

    /* Five elements, in document order -- which is the order the tree's builder
     * and every walk in this module depend on. */
    /* html, head (inserted by the parser because there is a body), body, div, p,
     * span -- six, in document order, which is the order the builder and every walk
     * here depend on. */
    RoboCheckEqInt(LayoutDomElementCount(fixture.dom), 6u);
    RoboCheckEqInt(LayoutDomIsReady(fixture.dom), 1);
    {
        static const char *const tags[] = { "html", "head", "body", "div", "p", "span" };

        for (index = 0u; index < 6u; index++) {
            RoboCheckEqInt(LayoutDomElementAt(fixture.dom, index, &handle, NULL),
                              RoboOk);
            RoboCheckEqInt(HtmlNodeHandleIsWellformed(&handle), 1);
            RoboCheckStrEq(HtmlElementName(&handle, NULL), tags[index]);
        }
    }

    /* Past the end is a miss; a missing output is a caller bug. */
    RoboCheckEqInt(LayoutDomElementAt(fixture.dom, 6u, &handle, NULL),
                      RoboNotFound);

    /* A second refresh describes *one* document rather than two generations of
     * one, so the count is stable rather than doubled -- and a refresh releases
     * the previous generation, which is what keeps the styles it owns from
     * outliving the descriptions of them. */
    RoboCheckEqInt(LayoutDomRefresh(fixture.dom, NULL), RoboOk);
    RoboCheckEqInt(LayoutDomElementCount(fixture.dom), 6u);
    RoboCheckEqInt(LayoutDomIsReady(fixture.dom), 1);

    LayoutDomFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Computed styles, per element                                        */
/* ------------------------------------------------------------------ */

static void TestDomStyles(void)
{
    LayoutDomFixture fixture;
    HtmlNodeHandle div;
    HtmlNodeHandle p;
    HtmlNodeHandle body;
    HtmlNodeHandle foreign;
    const CssComputedStyle *divStyle;
    const CssComputedStyle *pStyle;

    LayoutDomFixtureBegin(&fixture,
                             "<html>\n"
                             "  <body>\n"
                             "    <div class=\"panel\"><p>Hello</p></div>\n"
                             "  </body>\n"
                             "</html>\n",
                             "html, body { display: block }\n"
                             "div { display: block }\n"
                             ".panel { display: inline-block }\n"
                             "p { display: block }\n");

    RoboCheckEqInt(LayoutDomRefresh(fixture.dom, NULL), RoboOk);

    div = LayoutDocumentFind(&fixture.doc, "div");
    p = LayoutDocumentFind(&fixture.doc, "p");
    body = LayoutDocumentFind(&fixture.doc, "body");

    /* The element reference is the DOM node id, so each element's style is its
     * own: `.panel` beats `div` on specificity and `p` is neither. */
    divStyle = LayoutDomStyleFor(fixture.dom, &div);
    pStyle = LayoutDomStyleFor(fixture.dom, &p);
    RoboCheck(divStyle != NULL);
    RoboCheck(pStyle != NULL);
    RoboCheck(divStyle != pStyle);
    RoboCheckEqInt((int)LayoutStyleDisplay(divStyle),
                      (int)RoboCssDisplayInlineBlock);
    RoboCheckEqInt((int)LayoutStyleDisplay(pStyle), (int)RoboCssDisplayBlock);
    /* `<span>` is not in this document, so the un-covered case is the `before`
     * state below rather than an element here. */

    /* An element this bridge does not describe gets no style rather than somebody
     * else's: a cleared handle, and an id that names a slot the bridge never
     * captured. */
    HtmlNodeHandleClear(&foreign);
    RoboCheck(LayoutDomStyleFor(fixture.dom, &foreign) == NULL);
    foreign = body;
    foreign.nodeId += 1000u;
    RoboCheck(LayoutDomStyleFor(fixture.dom, &foreign) == NULL);
    /* And the real element still answers its own style afterwards, so the miss
     * was a miss and not a corruption of the lookup. */
    RoboCheck(LayoutDomStyleFor(fixture.dom, &body) != NULL);

    /* The without-style count can never exceed the capture, whatever the sheet
     * said: it is a count *of* the captured elements. */
    RoboCheck(LayoutDomWithoutStyleCount(fixture.dom) <=
               LayoutDomElementCount(fixture.dom));

    LayoutDomFixtureEnd(&fixture);
}

static void TestDomStylesUncovered(void)
{
    LayoutDomFixture fixture;
    HtmlNodeHandle span;
    const CssComputedStyle *style;

    /*
     * An element the sheet does not cover still has a *style* -- one whose
     * entries are what the cascade had to say, which for `<span>` here is nothing
     * -- and reading its display answers the property's initial value, because a
     * non-inherited property nothing declared is answered by its initial value
     * rather than by an entry.
     */
    LayoutDomFixtureBegin(&fixture,
                             "<html><body><span></span></body></html>",
                             "html, body { display: block }\n");
    RoboCheckEqInt(LayoutDomRefresh(fixture.dom, NULL), RoboOk);

    span = LayoutDocumentFind(&fixture.doc, "span");
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&span), 1);
    style = LayoutDomStyleFor(fixture.dom, &span);
    RoboCheck(style != NULL);
    RoboCheckEqInt(LayoutStyleDisplay(style), RoboCssDisplayInline);

    /* A style pointer that is NULL answers the same initial value on purpose:
     * "nothing styled this element" and "nothing declared display on it" are one
     * case, and a bridge that made them different would make the tree's
     * STYLE_MISSING flag mean two things. */
    RoboCheckEqInt(LayoutStyleDisplay(NULL), RoboCssDisplayInline);

    LayoutDomFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The style source                                                    */
/* ------------------------------------------------------------------ */

static void TestDomStyleSource(void)
{
    LayoutDomFixture fixture;
    LayoutStyleSource source;
    LayoutStyleSource empty;
    HtmlNodeHandle div;
    HtmlNodeHandle html;
    HtmlNodeHandle foreign;

    LayoutDomFixtureBegin(&fixture,
                             "<html><body><div></div></body></html>",
                             "div { display: block }\n");

    RoboCheckEqInt(LayoutDomStyleSource(fixture.dom, &source, NULL), RoboOk);
    RoboCheckEqInt(LayoutStyleSourceIsUsable(&source), 1);
    RoboCheck(source.userdata != NULL);

    div = LayoutDocumentFind(&fixture.doc, "div");
    html = LayoutDocumentFind(&fixture.doc, "html");

    /* Answers nothing before a refresh... */
    RoboCheck(LayoutStyleSourceStyle(&source, &div) == NULL);
    RoboCheckEqInt(LayoutDomRefresh(fixture.dom, NULL), RoboOk);

    /* ...and, afterwards, the same pointer the direct door returns. That equality
     * *is* the lifetime rule: the tree stores whatever this returns, so if the
     * source built a fresh style per call the tree would hold one that leaks. */
    RoboCheck(LayoutStyleSourceStyle(&source, &div) != NULL);
    RoboCheck(LayoutStyleSourceStyle(&source, &div) ==
               LayoutDomStyleFor(fixture.dom, &div));
    RoboCheck(LayoutStyleSourceStyle(&source, &html) ==
               LayoutDomStyleFor(fixture.dom, &html));
    RoboCheck(LayoutStyleSourceStyle(&source, &div) !=
               LayoutStyleSourceStyle(&source, &html));

    /* An element the bridge does not describe answers nothing. */
    HtmlNodeHandleClear(&foreign);
    RoboCheck(LayoutStyleSourceStyle(&source, &foreign) == NULL);
    foreign = html;
    foreign.nodeId += 77u;
    RoboCheck(LayoutStyleSourceStyle(&source, &foreign) == NULL);

    /* A source with no callback and a NULL source both answer nothing rather than
     * crashing: "answers nothing" is a legitimate source, and it is what makes the
     * builder testable without CSS. */
    LayoutStyleSourceInit(&empty);
    RoboCheckEqInt(LayoutStyleSourceIsUsable(&empty), 0);
    RoboCheck(LayoutStyleSourceStyle(&empty, &div) == NULL);
    RoboCheck(LayoutStyleSourceStyle(NULL, &div) == NULL);
    RoboCheck(LayoutStyleSourceStyle(&source, NULL) == NULL);
    LayoutStyleSourceInit(NULL);

    LayoutDomFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pipeline: DOM + CSS -> bridge -> tree -> pass                   */
/* ------------------------------------------------------------------ */

static void TestDomPipeline(void)
{
    LayoutDomFixture fixture;
    LayoutTree *tree = NULL;
    LayoutStyleSource source;
    LayoutBuildOptions options;
    LayoutBuildReport report;
    LayoutContext *context = NULL;
    LayoutStats stats;

    LayoutDomFixtureBegin(
        &fixture,
        "<html>\n"
        "  <body>\n"
        "    <div class=\"panel\"><p>Hello</p></div>\n"
        "    <div class=\"hidden\"></div>\n"
        "    <span>Inline</span>\n"
        "  </body>\n"
        "</html>\n",
        "html, body, div, p, span { display: block }\n"
        ".panel { display: inline-block }\n"
        ".hidden { display: none }\n");

    RoboCheckEqInt(LayoutDomRefresh(fixture.dom, NULL), RoboOk);
    /* html, head, body, div.panel, p, div.hidden, span. */
    RoboCheckEqInt(LayoutDomElementCount(fixture.dom), 7u);

    /* The styles the cascade produced, element by element, before the builder
     * sees any of them -- so a wrong box count below cannot be blamed on the
     * sheet. `<head>` is unmatched, which is why its display is the property's
     * initial value rather than a block. */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 0u),
                      (int)RoboCssDisplayBlock);      /* html */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 1u),
                      (int)RoboCssDisplayInline);     /* head, unmatched */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 2u),
                      (int)RoboCssDisplayBlock);      /* body */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 3u),
                      (int)RoboCssDisplayInlineBlock); /* div.panel */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 4u),
                      (int)RoboCssDisplayBlock);      /* p */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 5u),
                      (int)RoboCssDisplayNone);       /* div.hidden */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 6u),
                      (int)RoboCssDisplayBlock);      /* span */

    /* The bridge's own source, which is the supported way to ask for the real
     * styles rather than a scripted answer. */
    RoboCheckEqInt(LayoutDomStyleSource(fixture.dom, &source, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &source;

    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, fixture.doc.id, &options,
                                                          &report, NULL),
                      RoboOk);

    /* Seven elements, one hidden: six boxes and one skip. */
    RoboCheckEqInt(report.elementsSeen, 7u);
    RoboCheckEqInt(report.skippedDisplayNone, 1u);
    RoboCheckEqInt(report.boxesCreated, 6u);
    RoboCheckEqInt(report.boxesWithoutStyle, 0u);
    RoboCheckEqInt(LayoutTreeObjectCount(tree),
                      report.boxesCreated + report.textsCreated);

    /*
     * Every **object** carries the typed display the sheet meant and a style that
     * is the bridge's own for the right node -- which is the borrowed-pointer claim
     * arriving from a real cascade.
     *
     * The walk covers both kinds, because a text object is an object and skipping it
     * would leave the assertion silently narrower than its name. The two kinds answer
     * the style question differently *on purpose*, and that difference is the phase's
     * central decision: a box's style is the bridge's style for its own element, and a
     * text object's is the bridge's style for its **parent element** -- which is not a
     * shortcut but the same pointer, since every property a run needs is inherited.
     */
    {
        size_t index;
        size_t inlineBlock = 0u;
        size_t block = 0u;
        size_t texts = 0u;

        for (index = 0u; index < report.boxesCreated + report.textsCreated; index++) {
            LayoutNodeId id = RoboLayoutNodeIdNone;
            HtmlNodeHandle node;
            CssDisplay display;

            RoboCheckEqInt(LayoutObjectIdAt(tree, index, &id), RoboOk);
            RoboCheckEqInt(LayoutObjectDomNode(tree, id, &node), RoboOk);
            display = LayoutObjectDisplay(tree, id);

            if (MakeLayoutObjectKind(tree, id) == RoboLayoutObjectText) {
                HtmlNodeHandle parent;

                /* A text object is inline-level *by derivation*: it has no
                 * declaration and no box of its own, and no sheet can change it. */
                RoboCheckEqInt((int)display, (int)RoboCssDisplayInline);
                RoboCheckEqInt(HtmlNodeParent(&node, &parent, NULL), RoboOk);
                RoboCheck(LayoutObjectStyle(tree, id) ==
                           LayoutDomStyleFor(fixture.dom, &parent));
                texts++;
                continue;
            }

            RoboCheckEqInt(MakeLayoutObjectKind(tree, id), RoboLayoutObjectBox);
            RoboCheckEqInt(CssDisplayGeneratesBox(display), 1);
            if (display == RoboCssDisplayInlineBlock) {
                inlineBlock++;
            } else if (display == RoboCssDisplayBlock) {
                block++;
            }
            RoboCheck(LayoutObjectStyle(tree, id) ==
                       LayoutDomStyleFor(fixture.dom, &node));
            RoboCheckEqInt(LayoutStyleDisplay(LayoutObjectStyle(tree, id)),
                              display);
        }
        /* One inline-block (`.panel`), four blocks (html, body, p, span) and one
         * inline (`<head>`, which nothing matched) -- five boxes of six, plus the
         * text objects the document's text nodes contributed. */
        RoboCheckEqInt(inlineBlock, 1u);
        RoboCheckEqInt(block, 4u);
        RoboCheckEqInt(inlineBlock + block, 5u);
        RoboCheckEqInt(texts, report.textsCreated);
    }

    /*
     * And this pass -- no viewport recorded, so nothing to fill -- leaves every one
     * of them UNRESOLVED, with the counter that says so.
     *
     * The build *can* compute a coordinate (`geometry` is 1 since 10.3); the
     * difference between the two facts is the whole reason the per-box result state
     * survived a phase that made geometry possible, and the pipeline cases above run
     * the other half of the same document with a viewport to show the other answer.
     */
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.objects, report.boxesCreated + report.textsCreated);
    RoboCheckEqInt(stats.visited, report.boxesCreated + report.textsCreated);
    RoboCheckEqInt(stats.boxesUnresolved,
                      report.boxesCreated + report.textsCreated);
    RoboCheckEqInt(stats.boxesComputed, 0u);
    RoboCheckEqInt(stats.withoutStyle, 0u);
    RoboCheckEqInt(stats.block.blocksPlaced, 0u);
    RoboCheckEqInt(LayoutGeometryIsImplemented(), 1);

    /*
     * The same pass classifies, and it classifies from the *real* cascade rather
     * than from a scripted answer: every display below came through a stylesheet,
     * the matcher, the cascade and 9.8's normaliser before the builder read it.
     *
     * Four contexts. `<html>` establishes the initial one. `.panel` is
     * `display: inline-block`, whose inner display 9.12 records as `flow-root` --
     * so it establishes a *block* context while itself participating in the root's,
     * which is the atomic-inline case arriving from a real sheet. `<p>` and
     * `<span>` are block containers whose only child is a text run, so each
     * establishes an *inline* context -- the shape the commonest document there is has
     * -- and `<div class="hidden">` establishes nothing because it generates nothing.
     * `<body>` is transparent because its children are block-level boxes, and
     * `<head>` -- which nothing matched -- is inline and establishes nothing, because
     * a non-inherited property nothing declared is the property's initial value.
     */
    RoboCheckEqInt(LayoutTreeIsClassified(tree), 1);
    RoboCheckEqInt(stats.contexts, 4u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcRoot], 1u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcBlock], 1u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcInline], 2u);
    RoboCheckEqInt(stats.boxesEstablishing, 4u);
    /*
     * And four participate without establishing: `<head>` (inline, in the root's
     * context), `<body>` (transparent), and the two **text runs** -- `Hello` in the
     * paragraph's inline context and `Inline` in the span's. A text object
     * participates wherever its enclosing box puts it, which is why it belongs in this
     * count and in neither of the box counters.
     */
    RoboCheckEqInt(stats.boxesParticipatingOnly, 4u);
    /*
     * Two levels, and the second one is the point: `<p>` lives inside the atomic
     * inline, so its inline context nests in the panel's block context rather than
     * beside it. A build that parented every context to the root would still
     * report four contexts of the right three kinds, which is why the depth and
     * the parent link below are asserted and not just the counts.
     */
    RoboCheckEqInt(stats.contextDepthMax, 2u);
    {
        LayoutFcId rootContext = RoboLayoutFcIdNone;
        LayoutFormattingContext one;

        RoboCheckEqInt(LayoutTreeRootContext(tree, &rootContext, NULL), RoboOk);
        RoboCheckEqInt(LayoutTreeContext(tree, rootContext, &one, NULL), RoboOk);
        RoboCheckEqInt((int)one.owner, (int)LayoutTreeRoot(tree));
        /* The owner and the three participants below: `<head>` (inline), `<body>`
         * (transparent) and `<span>`, plus the atomic inline itself, which
         * participates in the root's context *and* owns one of its own. `<p>` is
         * not among them -- it participates in the panel's. */
        RoboCheckEqInt(one.participants, 5u);
        RoboCheckEqInt(one.depth, 0u);
        RoboCheckEqInt((int)one.parent, (int)RoboLayoutFcIdNone);

        /* The two transparent-or-inline boxes and the atomic inline, asked one at
         * a time so a build that got the count right by accident would fail. */
        {
            HtmlNodeHandle panel = LayoutDocumentFind(&fixture.doc, "div");
            LayoutNodeId panelBox =
                LayoutObjectIdForDomNode(tree, &panel);
            LayoutFcId owned = RoboLayoutFcIdNone;

            RoboCheckEqInt(LayoutObjectContextOwned(tree, panelBox, &owned,
                                                               NULL),
                              RoboOk);
            RoboCheckEqInt(LayoutTreeContext(tree, owned, &one, NULL), RoboOk);
            RoboCheckEqInt((int)one.kind, (int)RoboLayoutFcBlock);
            RoboCheckEqInt((int)one.parent, (int)rootContext);
            RoboCheckEqInt(one.depth, 1u);
            RoboCheckEqInt(one.participants, 1u);

            /* The box the panel contains is a *block container*, so what it
             * establishes is an inline context -- and its parent is the panel's
             * block context, not the root, which is the nesting claim. */
            {
                HtmlNodeHandle paragraph = LayoutDocumentFind(&fixture.doc, "p");
                LayoutNodeId paragraphBox =
                    LayoutObjectIdForDomNode(tree, &paragraph);
                LayoutFcId inner = RoboLayoutFcIdNone;

                RoboCheckEqInt(LayoutObjectContextOwned(tree, paragraphBox,
                                                                   &inner, NULL),
                                  RoboOk);
                RoboCheckEqInt((int)inner != (int)RoboLayoutFcIdNone, 1);
                RoboCheckEqInt(LayoutTreeContext(tree, inner, &one, NULL), RoboOk);
                RoboCheckEqInt((int)one.kind, (int)RoboLayoutFcInline);
                RoboCheckEqInt((int)one.parent, (int)owned);
                RoboCheckEqInt(one.depth, 2u);
            }
        }
        {
            HtmlNodeHandle head = LayoutDocumentFind(&fixture.doc, "head");
            LayoutNodeId headBox = LayoutObjectIdForDomNode(tree, &head);

            /* Inline boxes establish nothing, which is the rule that keeps
             * `inline` from being modelled as "a small block". */
            RoboCheckEqInt(LayoutObjectEstablishesContext(tree, headBox), 0);
        }
    }

    /* And the DOM is untouched: seven elements before the build and seven after,
     * with one of them generating no box. Classification reads the DOM and never
     * writes it, and the box set is derived state rather than a second tree. */
    RoboCheckEqInt(LayoutDomElementCount(fixture.dom), 7u);
    RoboCheckEqInt(LayoutTreeObjectCount(tree),
                      report.boxesCreated + report.textsCreated);

    /* The tree goes first: it borrows the bridge's styles. */
    LayoutContextDestroy(context);
    LayoutTreeDestroy(tree);
    LayoutDomFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The relation callbacks                                              */
/* ------------------------------------------------------------------ */

static void TestDomRelations(void)
{
    /*
     * `div > p` needs the *parent* callback and `div + div` needs the *sibling*
     * callback, so a bridge that answered either badly shows up as a display that
     * stayed at its initial `inline` -- and an inline box is a box, so the symptom
     * is a box that should not exist rather than a crash.
     *
     * The document is written one element per line on purpose: every pair of
     * sibling elements has a text node between them, so a bridge that stepped over
     * siblings by *node* rather than by element would find no adjacent pair at
     * all.
     */
    LayoutDomFixture fixture;
    LayoutTree *tree = NULL;
    LayoutStyleSource source;
    LayoutBuildOptions options;
    LayoutBuildReport report;

    LayoutDomFixtureBegin(&fixture,
                             "<html>\n"
                             "  <body>\n"
                             "    <div class=\"a\"><p>inside</p></div>\n"
                             "    <div class=\"b\"></div>\n"
                             "    <div class=\"c\"></div>\n"
                             "    <p>outside</p>\n"
                             "  </body>\n"
                             "</html>\n",
                             "html, body { display: block }\n"
                             "div > p { display: none }\n"
                             "div + div { display: none }\n");
    RoboCheckEqInt(LayoutDomRefresh(fixture.dom, NULL), RoboOk);

    /* html, head, body, div.a, p(inside), div.b, div.c, p(outside). */
    RoboCheckEqInt(LayoutDomElementCount(fixture.dom), 8u);

    /* The child combinator: the `<p>` *inside* a `<div>` is hidden and the one
     * that is a direct child of `<body>` is not -- same tag, same document, one
     * hidden. Only the parent relation can tell them apart. */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 2u),
                      (int)RoboCssDisplayBlock);       /* body */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 3u),
                      (int)RoboCssDisplayInline);      /* div.a, unmatched */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 4u),
                      (int)RoboCssDisplayNone);        /* p inside div.a */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 7u),
                      (int)RoboCssDisplayInline);      /* p outside */

    /* The sibling combinator: `.b` follows `.a` (a `<div>`), so both `.b` and
     * `.c` are hidden -- `.c` because it follows `.b`. A sibling walk that counted
     * *nodes* would find the text between them and match neither. */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 5u),
                      (int)RoboCssDisplayNone);        /* div.b */
    RoboCheckEqInt((int)LayoutDomDisplayAt(&fixture, 6u),
                      (int)RoboCssDisplayNone);        /* div.c */

    /* And the tree agrees: html, head, body, div.a, p(outside) = five boxes, with
     * three elements skipped. */
    RoboCheckEqInt(LayoutDomStyleSource(fixture.dom, &source, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &source;
    RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, fixture.doc.id, &options,
                                                          &report, NULL),
                      RoboOk);
    RoboCheckEqInt(report.elementsSeen, 8u);
    RoboCheckEqInt(report.skippedDisplayNone, 3u);
    RoboCheckEqInt(report.boxesCreated, 5u);
    /* Five boxes, and the text objects for the two paragraphs that survived: the
     * hidden `.b`/`.c` and the hidden `<p>` generate no box *and* no text object,
     * because a subtree with `display: none` on its root generates nothing. */
    RoboCheckEqInt(report.textsCreated, 1u); /* `inside` is inside the hidden p */
    RoboCheckEqInt(LayoutTreeObjectCount(tree),
                      report.boxesCreated + report.textsCreated);

    LayoutTreeDestroy(tree);
    LayoutDomFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The inline style attribute                                          */
/* ------------------------------------------------------------------ */

static void TestDomInlineStyle(void)
{
    LayoutDomFixture fixture;
    HtmlNodeHandle styled;
    HtmlNodeHandle plain;

    /*
     * The `style` attribute is the seam's inline-style callback: an author sheet
     * that says nothing about `<div>` is overridden by the element's own
     * declaration. A bridge that did not answer the callback would leave the div
     * at its initial `inline`.
     */
    LayoutDomFixtureBegin(&fixture,
                             "<html><body>\n"
                             "  <div style=\"display: none\"></div>\n"
                             "  <span></span>\n"
                             "</body></html>",
                             "html, body { display: block }\n");
    RoboCheckEqInt(LayoutDomRefresh(fixture.dom, NULL), RoboOk);

    styled = LayoutDocumentFind(&fixture.doc, "div");
    plain = LayoutDocumentFind(&fixture.doc, "span");
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&styled), 1);
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&plain), 1);
    RoboCheckEqInt(
        (int)LayoutStyleDisplay(LayoutDomStyleFor(fixture.dom, &styled)),
        (int)RoboCssDisplayNone);
    RoboCheckEqInt(
        (int)LayoutStyleDisplay(LayoutDomStyleFor(fixture.dom, &plain)),
        (int)RoboCssDisplayInline);

    /* And it reaches the tree: five elements (html, head, body, div, span), four
     * boxes -- the div's own declaration removed one. */
    {
        LayoutTree *tree = NULL;
        LayoutStyleSource source;
        LayoutBuildOptions options;
        LayoutBuildReport report;

        RoboCheckEqInt(LayoutDomStyleSource(fixture.dom, &source, NULL), RoboOk);
        LayoutBuildOptionsInit(&options);
        options.styles = &source;
        RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
        RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, fixture.doc.id, &options,
                                                              &report, NULL),
                          RoboOk);
        RoboCheckEqInt(report.elementsSeen, 5u);
        RoboCheckEqInt(report.skippedDisplayNone, 1u);
        RoboCheckEqInt(report.boxesCreated, 4u);
        LayoutTreeDestroy(tree);
    }

    LayoutDomFixtureEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The allocator returns to its baseline                               */
/* ------------------------------------------------------------------ */

static void TestDomAllocatorBalance(void)
{
    size_t before;
    LayoutDocument doc;
    LayoutCssFixture css;
    int pass;

    LayoutFixture();
    LayoutDocumentBuild(&doc,
                          "<html><body><div class=\"panel\"><p>Hello</p></div>"
                          "<span></span></body></html>");
    LayoutCssBegin(&css);
    LayoutCssSheet(&css, "html, body, div, p, span { display: block }\n"
                           ".panel { display: inline-block }\n");

    before = LayoutLiveBlocks();
    for (pass = 0; pass < 8; pass++) {
        LayoutDom *dom = NULL;
        LayoutTree *tree = NULL;
        LayoutStyleSource source;
        LayoutBuildOptions options;

        RoboCheckEqInt(LayoutDomCreate(css.context, doc.id, &dom, NULL), RoboOk);
        RoboCheckEqInt(LayoutDomRefresh(dom, NULL), RoboOk);
        RoboCheckEqInt(LayoutDomStyleSource(dom, &source, NULL), RoboOk);
        LayoutBuildOptionsInit(&options);
        options.styles = &source;
        RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);
        RoboCheckEqInt(LayoutTreeBuildFromDocument(tree, doc.id, &options, NULL,
                                                               NULL),
                          RoboOk);

        /* The one legal order: the tree borrows the bridge's styles, so it goes
         * first. The second refresh releases the previous generation of styles,
         * which is the path a leak would hide in. */
        LayoutTreeDestroy(tree);
        RoboCheckEqInt(LayoutDomRefresh(dom, NULL), RoboOk);
        LayoutDomDestroy(dom);
    }
    RoboCheckEqInt(LayoutLiveBlocks(), before);

    LayoutCssEnd(&css);
    LayoutDocumentRelease(&doc);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutDom(void);

void TestRegisterLayoutDom(void)
{
    TestRegister("layout.dom.lifecycle", TestDomLifecycle);
    TestRegister("layout.dom.capture", TestDomCapture);
    TestRegister("layout.dom.styles", TestDomStyles);
    TestRegister("layout.dom.styles.uncovered", TestDomStylesUncovered);
    TestRegister("layout.dom.style_source", TestDomStyleSource);
    TestRegister("layout.dom.pipeline", TestDomPipeline);
    TestRegister("layout.dom.relations", TestDomRelations);
    TestRegister("layout.dom.inline_style", TestDomInlineStyle);
    TestRegister("layout.dom.allocator_balance", TestDomAllocatorBalance);
}
