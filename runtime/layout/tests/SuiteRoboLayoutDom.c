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

typedef struct layout_dom_fixture {
    layout_document_t doc;
    layout_css_fixture_t css;
    robo_layout_dom_t *dom;
} layout_dom_fixture_t;

/**
 * Build a document, bring a real style context up, parse @p css into it, and
 * create the bridge. Nothing is refreshed: a caller that wants to see the
 * before-refresh state has to say so.
 */
static void layout_dom_fixture_begin(layout_dom_fixture_t *fixture,
                                     const char *html,
                                     const char *css)
{
    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    layout_css_begin(&fixture->css);
    if (css) {
        layout_css_sheet(&fixture->css, css);
    }
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(fixture->css.context, fixture->doc.id,
                                             &fixture->dom, NULL),
                      ROBO_OK);
    ROBO_CHECK(fixture->dom != NULL);
}

/**
 * Tear down in the one order that is legal: the tree first (it borrows the
 * bridge's styles), then the bridge, then the style context it borrowed.
 */
static void layout_dom_fixture_end(layout_dom_fixture_t *fixture)
{
    robo_layout_dom_destroy(fixture->dom);
    fixture->dom = NULL;
    layout_css_end(&fixture->css);
    layout_document_release(&fixture->doc);
}

/**
 * The computed display the bridge holds for the element at @p index of the
 * capture order, which is document order.
 *
 * Going through the capture order rather than by tag name is deliberate: three
 * `<div>`s in one document cannot be told apart by `robo_html_node_find_element`,
 * and a suite that could only reach the first of them would quietly assert about
 * the wrong element.
 */
static robo_css_display_t layout_dom_display_at(layout_dom_fixture_t *fixture, size_t index)
{
    robo_html_node_handle_t element;

    ROBO_CHECK_EQ_INT(robo_layout_dom_element_at(fixture->dom, index, &element, NULL),
                      ROBO_OK);
    return robo_layout_style_display(robo_layout_dom_style_for(fixture->dom, &element));
}

/* ------------------------------------------------------------------ */
/* Lifecycle and validation                                            */
/* ------------------------------------------------------------------ */

static void test_dom_lifecycle(void)
{
    layout_css_fixture_t css;
    layout_document_t doc;
    robo_layout_dom_t *dom = NULL;
    robo_layout_style_source_t source;
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;
    robo_html_node_handle_t handle;

    layout_fixture();
    layout_document_build(&doc, "<html><body></body></html>");
    layout_css_begin(&css);

    /* Null arguments and a document that is gone are the two refusals, and they
     * are told apart. */
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(NULL, doc.id, &dom, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_DOM);
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(css.context, doc.id, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_layout_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(css.context, ROBO_HTML_DOCUMENT_ID_NONE, &dom,
                                             &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK(dom == NULL);
    robo_layout_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(css.context, (robo_html_document_id_t)99999u,
                                             &dom, &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_NOT_FOUND);

    /* A bridge that has captured nothing answers nothing, and every read door
     * answers a sane thing rather than crashing. */
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(css.context, doc.id, &dom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_count(dom), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_dom_is_ready(dom), 0);
    ROBO_CHECK_EQ_INT((int)robo_layout_dom_document(dom), (int)doc.id);
    ROBO_CHECK_EQ_INT(robo_layout_dom_without_style_count(dom), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_at(dom, 0u, &handle, NULL), ROBO_NOT_FOUND);

    /* The source is usable *before* a refresh and answers nothing until one has
     * run: a caller that wants a tree with no styles should get one with no
     * styles rather than an error. */
    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(dom, &source, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_style_source_is_usable(&source), 1);
    {
        robo_html_node_handle_t html = layout_document_element(&doc);

        ROBO_CHECK(robo_layout_style_source_style(&source, &html) == NULL);
    }

    /* NULL reads answer rather than fault. */
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_count(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_dom_is_ready(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_dom_without_style_count(NULL), 0u);
    ROBO_CHECK_EQ_INT((int)robo_layout_dom_document(NULL), (int)ROBO_HTML_DOCUMENT_ID_NONE);
    ROBO_CHECK(robo_layout_dom_style_for(NULL, &handle) == NULL);
    ROBO_CHECK(robo_layout_dom_style_for(dom, NULL) == NULL);
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_at(NULL, 0u, &handle, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_at(dom, 0u, NULL, NULL), ROBO_INVALID_ARGUMENT);
    robo_layout_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(NULL, &diag), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(NULL, &source, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(dom, NULL, NULL), ROBO_INVALID_ARGUMENT);
    robo_layout_dom_destroy(NULL);

    robo_layout_dom_destroy(dom);
    layout_css_end(&css);
    layout_document_release(&doc);
}

/* ------------------------------------------------------------------ */
/* Capture                                                             */
/* ------------------------------------------------------------------ */

static void test_dom_capture(void)
{
    layout_dom_fixture_t fixture;
    robo_html_node_handle_t handle;
    size_t index;

    layout_dom_fixture_begin(&fixture,
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
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_count(fixture.dom), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_dom_is_ready(fixture.dom), 0);

    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(fixture.dom, NULL), ROBO_OK);

    /* Five elements, in document order -- which is the order the tree's builder
     * and every walk in this module depend on. */
    /* html, head (inserted by the parser because there is a body), body, div, p,
     * span -- six, in document order, which is the order the builder and every walk
     * here depend on. */
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_count(fixture.dom), 6u);
    ROBO_CHECK_EQ_INT(robo_layout_dom_is_ready(fixture.dom), 1);
    {
        static const char *const tags[] = { "html", "head", "body", "div", "p", "span" };

        for (index = 0u; index < 6u; index++) {
            ROBO_CHECK_EQ_INT(robo_layout_dom_element_at(fixture.dom, index, &handle, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&handle), 1);
            ROBO_CHECK_STR_EQ(robo_html_element_name(&handle, NULL), tags[index]);
        }
    }

    /* Past the end is a miss; a missing output is a caller bug. */
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_at(fixture.dom, 6u, &handle, NULL),
                      ROBO_NOT_FOUND);

    /* A second refresh describes *one* document rather than two generations of
     * one, so the count is stable rather than doubled -- and a refresh releases
     * the previous generation, which is what keeps the styles it owns from
     * outliving the descriptions of them. */
    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(fixture.dom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_count(fixture.dom), 6u);
    ROBO_CHECK_EQ_INT(robo_layout_dom_is_ready(fixture.dom), 1);

    layout_dom_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Computed styles, per element                                        */
/* ------------------------------------------------------------------ */

static void test_dom_styles(void)
{
    layout_dom_fixture_t fixture;
    robo_html_node_handle_t div;
    robo_html_node_handle_t p;
    robo_html_node_handle_t body;
    robo_html_node_handle_t foreign;
    const robo_css_computed_style_t *div_style;
    const robo_css_computed_style_t *p_style;

    layout_dom_fixture_begin(&fixture,
                             "<html>\n"
                             "  <body>\n"
                             "    <div class=\"panel\"><p>Hello</p></div>\n"
                             "  </body>\n"
                             "</html>\n",
                             "html, body { display: block }\n"
                             "div { display: block }\n"
                             ".panel { display: inline-block }\n"
                             "p { display: block }\n");

    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(fixture.dom, NULL), ROBO_OK);

    div = layout_document_find(&fixture.doc, "div");
    p = layout_document_find(&fixture.doc, "p");
    body = layout_document_find(&fixture.doc, "body");

    /* The element reference is the DOM node id, so each element's style is its
     * own: `.panel` beats `div` on specificity and `p` is neither. */
    div_style = robo_layout_dom_style_for(fixture.dom, &div);
    p_style = robo_layout_dom_style_for(fixture.dom, &p);
    ROBO_CHECK(div_style != NULL);
    ROBO_CHECK(p_style != NULL);
    ROBO_CHECK(div_style != p_style);
    ROBO_CHECK_EQ_INT((int)robo_layout_style_display(div_style),
                      (int)ROBO_CSS_DISPLAY_INLINE_BLOCK);
    ROBO_CHECK_EQ_INT((int)robo_layout_style_display(p_style), (int)ROBO_CSS_DISPLAY_BLOCK);
    /* `<span>` is not in this document, so the un-covered case is the `before`
     * state below rather than an element here. */

    /* An element this bridge does not describe gets no style rather than somebody
     * else's: a cleared handle, and an id that names a slot the bridge never
     * captured. */
    robo_html_node_handle_clear(&foreign);
    ROBO_CHECK(robo_layout_dom_style_for(fixture.dom, &foreign) == NULL);
    foreign = body;
    foreign.node_id += 1000u;
    ROBO_CHECK(robo_layout_dom_style_for(fixture.dom, &foreign) == NULL);
    /* And the real element still answers its own style afterwards, so the miss
     * was a miss and not a corruption of the lookup. */
    ROBO_CHECK(robo_layout_dom_style_for(fixture.dom, &body) != NULL);

    /* The without-style count can never exceed the capture, whatever the sheet
     * said: it is a count *of* the captured elements. */
    ROBO_CHECK(robo_layout_dom_without_style_count(fixture.dom) <=
               robo_layout_dom_element_count(fixture.dom));

    layout_dom_fixture_end(&fixture);
}

static void test_dom_styles_uncovered(void)
{
    layout_dom_fixture_t fixture;
    robo_html_node_handle_t span;
    const robo_css_computed_style_t *style;

    /*
     * An element the sheet does not cover still has a *style* -- one whose
     * entries are what the cascade had to say, which for `<span>` here is nothing
     * -- and reading its display answers the property's initial value, because a
     * non-inherited property nothing declared is answered by its initial value
     * rather than by an entry.
     */
    layout_dom_fixture_begin(&fixture,
                             "<html><body><span></span></body></html>",
                             "html, body { display: block }\n");
    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(fixture.dom, NULL), ROBO_OK);

    span = layout_document_find(&fixture.doc, "span");
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&span), 1);
    style = robo_layout_dom_style_for(fixture.dom, &span);
    ROBO_CHECK(style != NULL);
    ROBO_CHECK_EQ_INT(robo_layout_style_display(style), ROBO_CSS_DISPLAY_INLINE);

    /* A style pointer that is NULL answers the same initial value on purpose:
     * "nothing styled this element" and "nothing declared display on it" are one
     * case, and a bridge that made them different would make the tree's
     * STYLE_MISSING flag mean two things. */
    ROBO_CHECK_EQ_INT(robo_layout_style_display(NULL), ROBO_CSS_DISPLAY_INLINE);

    layout_dom_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The style source                                                    */
/* ------------------------------------------------------------------ */

static void test_dom_style_source(void)
{
    layout_dom_fixture_t fixture;
    robo_layout_style_source_t source;
    robo_layout_style_source_t empty;
    robo_html_node_handle_t div;
    robo_html_node_handle_t html;
    robo_html_node_handle_t foreign;

    layout_dom_fixture_begin(&fixture,
                             "<html><body><div></div></body></html>",
                             "div { display: block }\n");

    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(fixture.dom, &source, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_style_source_is_usable(&source), 1);
    ROBO_CHECK(source.userdata != NULL);

    div = layout_document_find(&fixture.doc, "div");
    html = layout_document_find(&fixture.doc, "html");

    /* Answers nothing before a refresh... */
    ROBO_CHECK(robo_layout_style_source_style(&source, &div) == NULL);
    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(fixture.dom, NULL), ROBO_OK);

    /* ...and, afterwards, the same pointer the direct door returns. That equality
     * *is* the lifetime rule: the tree stores whatever this returns, so if the
     * source built a fresh style per call the tree would hold one that leaks. */
    ROBO_CHECK(robo_layout_style_source_style(&source, &div) != NULL);
    ROBO_CHECK(robo_layout_style_source_style(&source, &div) ==
               robo_layout_dom_style_for(fixture.dom, &div));
    ROBO_CHECK(robo_layout_style_source_style(&source, &html) ==
               robo_layout_dom_style_for(fixture.dom, &html));
    ROBO_CHECK(robo_layout_style_source_style(&source, &div) !=
               robo_layout_style_source_style(&source, &html));

    /* An element the bridge does not describe answers nothing. */
    robo_html_node_handle_clear(&foreign);
    ROBO_CHECK(robo_layout_style_source_style(&source, &foreign) == NULL);
    foreign = html;
    foreign.node_id += 77u;
    ROBO_CHECK(robo_layout_style_source_style(&source, &foreign) == NULL);

    /* A source with no callback and a NULL source both answer nothing rather than
     * crashing: "answers nothing" is a legitimate source, and it is what makes the
     * builder testable without CSS. */
    robo_layout_style_source_init(&empty);
    ROBO_CHECK_EQ_INT(robo_layout_style_source_is_usable(&empty), 0);
    ROBO_CHECK(robo_layout_style_source_style(&empty, &div) == NULL);
    ROBO_CHECK(robo_layout_style_source_style(NULL, &div) == NULL);
    ROBO_CHECK(robo_layout_style_source_style(&source, NULL) == NULL);
    robo_layout_style_source_init(NULL);

    layout_dom_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pipeline: DOM + CSS -> bridge -> tree -> pass                   */
/* ------------------------------------------------------------------ */

static void test_dom_pipeline(void)
{
    layout_dom_fixture_t fixture;
    robo_layout_tree_t *tree = NULL;
    robo_layout_style_source_t source;
    robo_layout_build_options_t options;
    robo_layout_build_report_t report;
    robo_layout_context_t *context = NULL;
    robo_layout_stats_t stats;

    layout_dom_fixture_begin(
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

    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(fixture.dom, NULL), ROBO_OK);
    /* html, head, body, div.panel, p, div.hidden, span. */
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_count(fixture.dom), 7u);

    /* The styles the cascade produced, element by element, before the builder
     * sees any of them -- so a wrong box count below cannot be blamed on the
     * sheet. `<head>` is unmatched, which is why its display is the property's
     * initial value rather than a block. */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 0u),
                      (int)ROBO_CSS_DISPLAY_BLOCK);      /* html */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 1u),
                      (int)ROBO_CSS_DISPLAY_INLINE);     /* head, unmatched */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 2u),
                      (int)ROBO_CSS_DISPLAY_BLOCK);      /* body */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 3u),
                      (int)ROBO_CSS_DISPLAY_INLINE_BLOCK); /* div.panel */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 4u),
                      (int)ROBO_CSS_DISPLAY_BLOCK);      /* p */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 5u),
                      (int)ROBO_CSS_DISPLAY_NONE);       /* div.hidden */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 6u),
                      (int)ROBO_CSS_DISPLAY_BLOCK);      /* span */

    /* The bridge's own source, which is the supported way to ask for the real
     * styles rather than a scripted answer. */
    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(fixture.dom, &source, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &source;

    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, fixture.doc.id, &options,
                                                          &report, NULL),
                      ROBO_OK);

    /* Seven elements, one hidden: six boxes and one skip. */
    ROBO_CHECK_EQ_INT(report.elements_seen, 7u);
    ROBO_CHECK_EQ_INT(report.skipped_display_none, 1u);
    ROBO_CHECK_EQ_INT(report.boxes_created, 6u);
    ROBO_CHECK_EQ_INT(report.boxes_without_style, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), 6u);

    /* Every box carries the typed display the sheet meant, and the *bridge's*
     * style for its own element -- which is the borrowed-pointer claim arriving
     * from a real cascade. */
    {
        size_t index;
        size_t inline_block = 0u;
        size_t block = 0u;

        for (index = 0u; index < 6u; index++) {
            robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
            robo_html_node_handle_t element;
            robo_css_display_t display;

            ROBO_CHECK_EQ_INT(robo_layout_object_id_at(tree, index, &id), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(tree, id, &element), ROBO_OK);
            display = robo_layout_object_display(tree, id);
            ROBO_CHECK_EQ_INT(robo_css_display_generates_box(display), 1);
            if (display == ROBO_CSS_DISPLAY_INLINE_BLOCK) {
                inline_block++;
            } else if (display == ROBO_CSS_DISPLAY_BLOCK) {
                block++;
            }
            ROBO_CHECK(robo_layout_object_style(tree, id) ==
                       robo_layout_dom_style_for(fixture.dom, &element));
            ROBO_CHECK_EQ_INT(robo_layout_style_display(robo_layout_object_style(tree, id)),
                              display);
        }
        /* One inline-block (`.panel`), four blocks (html, body, p, span) and one
         * inline (`<head>`, which nothing matched). */
        ROBO_CHECK_EQ_INT(inline_block, 1u);
        ROBO_CHECK_EQ_INT(block, 4u);
        ROBO_CHECK_EQ_INT(inline_block + block, 5u);
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
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, tree, NULL), ROBO_OK);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.objects, 6u);
    ROBO_CHECK_EQ_INT(stats.visited, 6u);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 6u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 0u);
    ROBO_CHECK_EQ_INT(stats.without_style, 0u);
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_geometry_is_implemented(), 1);

    /*
     * The same pass classifies, and it classifies from the *real* cascade rather
     * than from a scripted answer: every display below came through a stylesheet,
     * the matcher, the cascade and 9.8's normaliser before the builder read it.
     *
     * Four contexts. `<html>` establishes the initial one. `.panel` is
     * `display: inline-block`, whose inner display 9.12 records as `flow-root` --
     * so it establishes a *block* context while itself participating in the root's,
     * which is the atomic-inline case arriving from a real sheet. `<p>` and
     * `<span>` are block containers with no children, so each establishes an inline
     * context. `<body>` is transparent because `span` computes to `display: block`,
     * and `<head>` -- which nothing matched -- is inline and establishes nothing,
     * because a non-inherited property nothing declared is the property's initial
     * value.
     */
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(tree), 1);
    ROBO_CHECK_EQ_INT(stats.contexts, 4u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_ROOT], 1u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_BLOCK], 1u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_INLINE], 2u);
    ROBO_CHECK_EQ_INT(stats.boxes_establishing, 4u);
    ROBO_CHECK_EQ_INT(stats.boxes_participating_only, 2u);
    /*
     * Two levels, and the second one is the point: `<p>` lives inside the atomic
     * inline, so its inline context nests in the panel's block context rather than
     * beside it. A build that parented every context to the root would still
     * report four contexts of the right three kinds, which is why the depth and
     * the parent link below are asserted and not just the counts.
     */
    ROBO_CHECK_EQ_INT(stats.context_depth_max, 2u);
    {
        robo_layout_fc_id_t root_context = ROBO_LAYOUT_FC_ID_NONE;
        robo_layout_formatting_context_t one;

        ROBO_CHECK_EQ_INT(robo_layout_tree_root_context(tree, &root_context, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_context(tree, root_context, &one, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)one.owner, (int)robo_layout_tree_root(tree));
        /* The owner and the three participants below: `<head>` (inline), `<body>`
         * (transparent) and `<span>`, plus the atomic inline itself, which
         * participates in the root's context *and* owns one of its own. `<p>` is
         * not among them -- it participates in the panel's. */
        ROBO_CHECK_EQ_INT(one.participants, 5u);
        ROBO_CHECK_EQ_INT(one.depth, 0u);
        ROBO_CHECK_EQ_INT((int)one.parent, (int)ROBO_LAYOUT_FC_ID_NONE);

        /* The two transparent-or-inline boxes and the atomic inline, asked one at
         * a time so a build that got the count right by accident would fail. */
        {
            robo_html_node_handle_t panel = layout_document_find(&fixture.doc, "div");
            robo_layout_node_id_t panel_box =
                robo_layout_object_id_for_dom_node(tree, &panel);
            robo_layout_fc_id_t owned = ROBO_LAYOUT_FC_ID_NONE;

            ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(tree, panel_box, &owned,
                                                               NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_layout_tree_context(tree, owned, &one, NULL), ROBO_OK);
            ROBO_CHECK_EQ_INT((int)one.kind, (int)ROBO_LAYOUT_FC_BLOCK);
            ROBO_CHECK_EQ_INT((int)one.parent, (int)root_context);
            ROBO_CHECK_EQ_INT(one.depth, 1u);
            ROBO_CHECK_EQ_INT(one.participants, 1u);

            /* The box the panel contains is a *block container*, so what it
             * establishes is an inline context -- and its parent is the panel's
             * block context, not the root, which is the nesting claim. */
            {
                robo_html_node_handle_t paragraph = layout_document_find(&fixture.doc, "p");
                robo_layout_node_id_t paragraph_box =
                    robo_layout_object_id_for_dom_node(tree, &paragraph);
                robo_layout_fc_id_t inner = ROBO_LAYOUT_FC_ID_NONE;

                ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(tree, paragraph_box,
                                                                   &inner, NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT((int)inner != (int)ROBO_LAYOUT_FC_ID_NONE, 1);
                ROBO_CHECK_EQ_INT(robo_layout_tree_context(tree, inner, &one, NULL), ROBO_OK);
                ROBO_CHECK_EQ_INT((int)one.kind, (int)ROBO_LAYOUT_FC_INLINE);
                ROBO_CHECK_EQ_INT((int)one.parent, (int)owned);
                ROBO_CHECK_EQ_INT(one.depth, 2u);
            }
        }
        {
            robo_html_node_handle_t head = layout_document_find(&fixture.doc, "head");
            robo_layout_node_id_t head_box = robo_layout_object_id_for_dom_node(tree, &head);

            /* Inline boxes establish nothing, which is the rule that keeps
             * `inline` from being modelled as "a small block". */
            ROBO_CHECK_EQ_INT(robo_layout_object_establishes_context(tree, head_box), 0);
        }
    }

    /* And the DOM is untouched: seven elements before the build and seven after,
     * with one of them generating no box. Classification reads the DOM and never
     * writes it, and the box set is derived state rather than a second tree. */
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_count(fixture.dom), 7u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), 6u);

    /* The tree goes first: it borrows the bridge's styles. */
    robo_layout_context_destroy(context);
    robo_layout_tree_destroy(tree);
    layout_dom_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The relation callbacks                                              */
/* ------------------------------------------------------------------ */

static void test_dom_relations(void)
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
    layout_dom_fixture_t fixture;
    robo_layout_tree_t *tree = NULL;
    robo_layout_style_source_t source;
    robo_layout_build_options_t options;
    robo_layout_build_report_t report;

    layout_dom_fixture_begin(&fixture,
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
    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(fixture.dom, NULL), ROBO_OK);

    /* html, head, body, div.a, p(inside), div.b, div.c, p(outside). */
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_count(fixture.dom), 8u);

    /* The child combinator: the `<p>` *inside* a `<div>` is hidden and the one
     * that is a direct child of `<body>` is not -- same tag, same document, one
     * hidden. Only the parent relation can tell them apart. */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 2u),
                      (int)ROBO_CSS_DISPLAY_BLOCK);       /* body */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 3u),
                      (int)ROBO_CSS_DISPLAY_INLINE);      /* div.a, unmatched */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 4u),
                      (int)ROBO_CSS_DISPLAY_NONE);        /* p inside div.a */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 7u),
                      (int)ROBO_CSS_DISPLAY_INLINE);      /* p outside */

    /* The sibling combinator: `.b` follows `.a` (a `<div>`), so both `.b` and
     * `.c` are hidden -- `.c` because it follows `.b`. A sibling walk that counted
     * *nodes* would find the text between them and match neither. */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 5u),
                      (int)ROBO_CSS_DISPLAY_NONE);        /* div.b */
    ROBO_CHECK_EQ_INT((int)layout_dom_display_at(&fixture, 6u),
                      (int)ROBO_CSS_DISPLAY_NONE);        /* div.c */

    /* And the tree agrees: html, head, body, div.a, p(outside) = five boxes, with
     * three elements skipped. */
    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(fixture.dom, &source, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &source;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, fixture.doc.id, &options,
                                                          &report, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(report.elements_seen, 8u);
    ROBO_CHECK_EQ_INT(report.skipped_display_none, 3u);
    ROBO_CHECK_EQ_INT(report.boxes_created, 5u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), 5u);

    robo_layout_tree_destroy(tree);
    layout_dom_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The inline style attribute                                          */
/* ------------------------------------------------------------------ */

static void test_dom_inline_style(void)
{
    layout_dom_fixture_t fixture;
    robo_html_node_handle_t styled;
    robo_html_node_handle_t plain;

    /*
     * The `style` attribute is the seam's inline-style callback: an author sheet
     * that says nothing about `<div>` is overridden by the element's own
     * declaration. A bridge that did not answer the callback would leave the div
     * at its initial `inline`.
     */
    layout_dom_fixture_begin(&fixture,
                             "<html><body>\n"
                             "  <div style=\"display: none\"></div>\n"
                             "  <span></span>\n"
                             "</body></html>",
                             "html, body { display: block }\n");
    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(fixture.dom, NULL), ROBO_OK);

    styled = layout_document_find(&fixture.doc, "div");
    plain = layout_document_find(&fixture.doc, "span");
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&styled), 1);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&plain), 1);
    ROBO_CHECK_EQ_INT(
        (int)robo_layout_style_display(robo_layout_dom_style_for(fixture.dom, &styled)),
        (int)ROBO_CSS_DISPLAY_NONE);
    ROBO_CHECK_EQ_INT(
        (int)robo_layout_style_display(robo_layout_dom_style_for(fixture.dom, &plain)),
        (int)ROBO_CSS_DISPLAY_INLINE);

    /* And it reaches the tree: five elements (html, head, body, div, span), four
     * boxes -- the div's own declaration removed one. */
    {
        robo_layout_tree_t *tree = NULL;
        robo_layout_style_source_t source;
        robo_layout_build_options_t options;
        robo_layout_build_report_t report;

        ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(fixture.dom, &source, NULL), ROBO_OK);
        robo_layout_build_options_init(&options);
        options.styles = &source;
        ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, fixture.doc.id, &options,
                                                              &report, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(report.elements_seen, 5u);
        ROBO_CHECK_EQ_INT(report.skipped_display_none, 1u);
        ROBO_CHECK_EQ_INT(report.boxes_created, 4u);
        robo_layout_tree_destroy(tree);
    }

    layout_dom_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The allocator returns to its baseline                               */
/* ------------------------------------------------------------------ */

static void test_dom_allocator_balance(void)
{
    size_t before;
    layout_document_t doc;
    layout_css_fixture_t css;
    int pass;

    layout_fixture();
    layout_document_build(&doc,
                          "<html><body><div class=\"panel\"><p>Hello</p></div>"
                          "<span></span></body></html>");
    layout_css_begin(&css);
    layout_css_sheet(&css, "html, body, div, p, span { display: block }\n"
                           ".panel { display: inline-block }\n");

    before = layout_live_blocks();
    for (pass = 0; pass < 8; pass++) {
        robo_layout_dom_t *dom = NULL;
        robo_layout_tree_t *tree = NULL;
        robo_layout_style_source_t source;
        robo_layout_build_options_t options;

        ROBO_CHECK_EQ_INT(robo_layout_dom_create(css.context, doc.id, &dom, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(dom, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(dom, &source, NULL), ROBO_OK);
        robo_layout_build_options_init(&options);
        options.styles = &source;
        ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, &options, NULL,
                                                               NULL),
                          ROBO_OK);

        /* The one legal order: the tree borrows the bridge's styles, so it goes
         * first. The second refresh releases the previous generation of styles,
         * which is the path a leak would hide in. */
        robo_layout_tree_destroy(tree);
        ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(dom, NULL), ROBO_OK);
        robo_layout_dom_destroy(dom);
    }
    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);

    layout_css_end(&css);
    layout_document_release(&doc);
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout_dom(void);

void robo_test_register_layout_dom(void)
{
    robo_test_register("layout.dom.lifecycle", test_dom_lifecycle);
    robo_test_register("layout.dom.capture", test_dom_capture);
    robo_test_register("layout.dom.styles", test_dom_styles);
    robo_test_register("layout.dom.styles.uncovered", test_dom_styles_uncovered);
    robo_test_register("layout.dom.style_source", test_dom_style_source);
    robo_test_register("layout.dom.pipeline", test_dom_pipeline);
    robo_test_register("layout.dom.relations", test_dom_relations);
    robo_test_register("layout.dom.inline_style", test_dom_inline_style);
    robo_test_register("layout.dom.allocator_balance", test_dom_allocator_balance);
}
