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

static void test_build_root(void)
{
    layout_document_t doc;
    robo_layout_tree_t *tree = NULL;
    robo_layout_build_report_t report;
    robo_layout_node_id_t root = ROBO_LAYOUT_NODE_ID_NONE;
    robo_html_node_handle_t element;
    uint32_t depth = 99u;
    int pass;

    layout_fixture();
    layout_document_build(&doc, "<html><body><p>text</p></body></html>");
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);

    /* Repeated, because "the tree is replaced, not merged" is the property a
     * rebuild relies on and a single build cannot show it. */
    for (pass = 0; pass < 3; pass++) {
        memset(&report, 0xAB, sizeof(report));
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, NULL, &report,
                                                               NULL),
                          ROBO_OK);

        /*
         * Four elements: html, head, body, p. `<head>` is not in the markup --
         * RoboHTML's tree builder inserts it, because a document with a `<body>`
         * has a head -- and the *number is derived from the DOM* rather than
         * guessed, because how many elements a fragment parses to is the parser's
         * fact and not this suite's. The text node inside p is stepped over.
         */
        ROBO_CHECK_EQ_INT(layout_document_element_count(&doc), 4u);
        ROBO_CHECK_EQ_INT(report.elements_seen, 4u);
        ROBO_CHECK_EQ_INT(report.boxes_created, 4u);
        ROBO_CHECK_EQ_INT(report.skipped_display_none, 0u);
        ROBO_CHECK_EQ_INT(report.boxes_without_style, 4u);
        /* html 0, head 1, body 1, p 2. */
        ROBO_CHECK_EQ_INT(report.depth_max, 2u);
        ROBO_CHECK_EQ_INT(report.depth_max, layout_tree_max_depth(tree));
        ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), 4u);

        /* The root box is the *document element*, not the document node: a
         * document is not an element, generates nothing, and a box of its own
         * would have no style and no parent. */
        root = robo_layout_tree_root(tree);
        ROBO_CHECK_EQ_INT(robo_layout_object_is_root(tree, root), 1);
        element = layout_document_element(&doc);
        ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(tree, &element),
                          (int)root);
        ROBO_CHECK_EQ_INT(robo_layout_object_depth(tree, root, &depth), ROBO_OK);
        ROBO_CHECK_EQ_INT(depth, 0u);
    }

    robo_layout_tree_destroy(tree);
    layout_document_release(&doc);
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

static void test_build_order(void)
{
    layout_document_t doc;
    robo_layout_tree_t *tree = NULL;
    robo_layout_build_report_t report;
    robo_layout_node_id_t root;
    robo_layout_node_id_t head = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_node_id_t body = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_node_id_t section = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_node_id_t p = ROBO_LAYOUT_NODE_ID_NONE;
    size_t seen = 0u;

    layout_fixture();
    layout_document_build(&doc, LAYOUT_BUILD_ORDER_HTML);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, NULL, &report, NULL),
                      ROBO_OK);

    /* html, head, title, body, section, p, p, p, footer -- nine elements, all of
     * them in the markup this time. html 0, head 1, title 2, body 1, section 2,
     * p 3, footer 2, so the deepest box is a paragraph at depth 3. */
    ROBO_CHECK_EQ_INT(layout_document_element_count(&doc), 9u);
    ROBO_CHECK_EQ_INT(report.elements_seen, 9u);
    ROBO_CHECK_EQ_INT(report.boxes_created, 9u);
    ROBO_CHECK_EQ_INT(report.depth_max, 3u);
    ROBO_CHECK_EQ_INT(report.depth_max, layout_tree_max_depth(tree));
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), 9u);

    /*
     * The newline between every pair of elements is a text node, so a builder
     * that walked *nodes* would produce nine boxes with the sibling order half
     * whitespace. This walks the tree's own element links and checks the tags, so
     * the assertion is about the document rather than about ids.
     */
    root = robo_layout_tree_root(tree);
    ROBO_CHECK_EQ_INT(robo_layout_object_child_count(tree, root, &seen), ROBO_OK);
    ROBO_CHECK_EQ_INT(seen, 2u); /* head, body, and no text between them */
    ROBO_CHECK_EQ_INT(robo_layout_object_first_child(tree, root, &head), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_object_last_child(tree, root, &body), ROBO_OK);
    {
        robo_html_node_handle_t handle;

        ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(tree, head, &handle), ROBO_OK);
        ROBO_CHECK_STR_EQ(robo_html_element_name(&handle, NULL), "head");
        ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(tree, body, &handle), ROBO_OK);
        ROBO_CHECK_STR_EQ(robo_html_element_name(&handle, NULL), "body");
        /* The first child of `<html>` is `<head>`, not the text before it. */
        ROBO_CHECK_EQ_INT(robo_layout_object_depth(tree, head, &report.depth_max), ROBO_OK);
        ROBO_CHECK_EQ_INT(report.depth_max, 1u);
    }

    /* Siblings are linked both ways: `<head>`'s next is `<body>` and back. */
    {
        robo_layout_node_id_t next = ROBO_LAYOUT_NODE_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_layout_object_next_sibling(tree, head, &next), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)next, (int)body);
        ROBO_CHECK_EQ_INT(robo_layout_object_previous_sibling(tree, body, &next), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)next, (int)head);
        /* Neither end of a sibling list has a neighbour, which is a miss. */
        ROBO_CHECK_EQ_INT(robo_layout_object_previous_sibling(tree, head, &next),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_layout_object_next_sibling(tree, body, &next), ROBO_NOT_FOUND);
    }

    /* Three `<p>` under `<section>`, in order, each with no child of its own --
     * the text inside is not a box. */
    ROBO_CHECK_EQ_INT(robo_layout_object_first_child(tree, body, &section), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_object_child_count(tree, body, &seen), ROBO_OK);
    ROBO_CHECK_EQ_INT(seen, 2u); /* section, footer */
    {
        robo_html_node_handle_t handle;

        ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(tree, section, &handle), ROBO_OK);
        ROBO_CHECK_STR_EQ(robo_html_element_name(&handle, NULL), "section");
    }

    ROBO_CHECK_EQ_INT(robo_layout_object_first_child(tree, section, &p), ROBO_OK);
    {
        size_t paragraphs = 0u;

        for (;;) {
            robo_layout_node_id_t after = ROBO_LAYOUT_NODE_ID_NONE;
            robo_html_node_handle_t handle;

            ROBO_CHECK_EQ_INT(robo_layout_object_child_count(tree, p, &seen), ROBO_OK);
            ROBO_CHECK_EQ_INT(seen, 0u);
            ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(tree, p, &handle), ROBO_OK);
            ROBO_CHECK_STR_EQ(robo_html_element_name(&handle, NULL), "p");
            paragraphs++;
            if (robo_layout_object_next_sibling(tree, p, &after) != ROBO_OK) {
                break;
            }
            p = after;
        }
        ROBO_CHECK_EQ_INT(paragraphs, 3u);
    }

    robo_layout_tree_destroy(tree);
    layout_document_release(&doc);
}

/* ------------------------------------------------------------------ */
/* display: none                                                       */
/* ------------------------------------------------------------------ */

static void test_build_display_none(void)
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

    layout_document_t doc;
    robo_layout_tree_t *tree = NULL;
    layout_fake_source_t fake;
    robo_layout_style_source_t styles;
    robo_layout_build_options_t options;
    robo_layout_build_report_t report;
    robo_html_node_handle_t aside;
    robo_html_node_handle_t span;

    layout_fixture();
    layout_document_build(&doc, html);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);

    layout_fake_init(&fake);
    layout_fake_add(&fake, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "aside", ROBO_CSS_DISPLAY_NONE);
    layout_fake_add(&fake, "span", ROBO_CSS_DISPLAY_BLOCK);
    styles = layout_fake_source_of(&fake);
    robo_layout_build_options_init(&options);
    options.styles = &styles;

    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, &options, &report,
                                                           NULL),
                      ROBO_OK);

    /*
     * html, head, body, div.a, div.c = 5 boxes. The `<aside>` is refused, and the
     * two `<span>`s inside it are never reached -- a `display: none` element takes
     * its whole subtree with it, which is the rule applied one level down rather
     * than a special case. Six elements are *seen* and only one is counted
     * skipped: the spans are not "skipped", they are never visited, and the report
     * must not conflate the two.
     */
    ROBO_CHECK_EQ_INT(layout_document_element_count(&doc), 8u);
    ROBO_CHECK_EQ_INT(report.boxes_created, 5u);
    ROBO_CHECK_EQ_INT(report.skipped_display_none, 1u);
    ROBO_CHECK_EQ_INT(report.elements_seen, 6u);
    ROBO_CHECK_EQ_INT(report.boxes_created + report.skipped_display_none, 6u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), 5u);

    /* The hidden element has no box, so the reverse lookup answers NONE -- which
     * is also what an element nobody built answers, which is why the *report* is
     * the door that tells "hidden" from "never reached". */
    aside = layout_document_find(&doc, "aside");
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&aside), 1);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(tree, &aside),
                      (int)ROBO_LAYOUT_NODE_ID_NONE);

    span = layout_document_find(&doc, "span");
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&span), 1);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(tree, &span),
                      (int)ROBO_LAYOUT_NODE_ID_NONE);

    /* The two visible `<div>`s are *adjacent siblings*: the hidden element is
     * simply absent from the tree rather than a hole in it, so `body` has two
     * children and the first one's next sibling is the last. */
    {
        robo_layout_node_id_t root = robo_layout_tree_root(tree);
        robo_layout_node_id_t body = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_node_id_t first = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_node_id_t last = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_node_id_t next = ROBO_LAYOUT_NODE_ID_NONE;
        robo_html_node_handle_t handle;
        size_t count = 0u;

        /* `body` is the *last* child of `<html>`: `<head>` comes first. Taking the
         * first child here would make every assert below about the head. */
        ROBO_CHECK_EQ_INT(robo_layout_object_last_child(tree, root, &body), ROBO_OK);
        {
            robo_html_node_handle_t handle;

            ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(tree, body, &handle), ROBO_OK);
            ROBO_CHECK_STR_EQ(robo_html_element_name(&handle, NULL), "body");
        }
        ROBO_CHECK_EQ_INT(robo_layout_object_child_count(tree, body, &count), ROBO_OK);
        ROBO_CHECK_EQ_INT(count, 2u);
        ROBO_CHECK_EQ_INT(robo_layout_object_first_child(tree, body, &first), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_last_child(tree, body, &last), ROBO_OK);
        ROBO_CHECK((int)first != (int)last);
        ROBO_CHECK_EQ_INT(robo_layout_object_next_sibling(tree, first, &next), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)next, (int)last);
        /* And the box after the hidden element is the last `<div>`, not the `a`
         * one -- so the hidden element was skipped in place rather than
         * substituted for. */
        ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(tree, last, &handle), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_handle_equal(&handle, &aside), 0);
        ROBO_CHECK_EQ_INT(robo_html_node_handle_equal(&handle, &span), 0);
    }

    robo_layout_tree_clear(tree);

    /*
     * The same document with `<span>` hidden instead: the identical four boxes,
     * but the two spans are now *seen and skipped* rather than never reached --
     * one word of stylesheet, five digits of difference in the report.
     */
    layout_fake_dispose(&fake);
    layout_fake_init(&fake);
    layout_fake_add(&fake, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "aside", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "span", ROBO_CSS_DISPLAY_NONE);
    styles = layout_fake_source_of(&fake);
    options.styles = &styles;

    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, &options, &report,
                                                           NULL),
                      ROBO_OK);
    /* html, head, body, div.a, aside, div.c = 6 boxes; the two spans are skipped. */
    ROBO_CHECK_EQ_INT(report.boxes_created, 6u);
    ROBO_CHECK_EQ_INT(report.skipped_display_none, 2u);
    ROBO_CHECK_EQ_INT(report.elements_seen, 8u);
    ROBO_CHECK_EQ_INT(report.elements_seen, layout_document_element_count(&doc));

    /* `<aside>` is a box now, and it has no children: that is what "its subtree
     * generated nothing" looks like from the tree's side. */
    {
        robo_layout_node_id_t aside_box = robo_layout_object_id_for_dom_node(tree, &aside);
        size_t count = 99u;

        ROBO_CHECK_EQ_INT(robo_layout_object_is_live(tree, aside_box), 1);
        ROBO_CHECK_EQ_INT(robo_layout_object_has_children(tree, aside_box), 0);
        ROBO_CHECK_EQ_INT(robo_layout_object_child_count(tree, aside_box, &count), ROBO_OK);
        ROBO_CHECK_EQ_INT(count, 0u);
    }

    layout_fake_dispose(&fake);
    robo_layout_tree_destroy(tree);
    layout_document_release(&doc);
}

static void test_build_root_display_none(void)
{
    layout_document_t doc;
    robo_layout_tree_t *tree = NULL;
    layout_fake_source_t fake;
    robo_layout_style_source_t styles;
    robo_layout_build_options_t options;
    robo_layout_build_report_t report;

    layout_fixture();
    layout_document_build(&doc, "<html><body><p>x</p></body></html>");
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
    layout_fake_init(&fake);
    layout_fake_add(&fake, "html", ROBO_CSS_DISPLAY_NONE);
    styles = layout_fake_source_of(&fake);
    robo_layout_build_options_init(&options);
    options.styles = &styles;

    /* The whole document generates nothing. An empty tree is the honest answer,
     * and the report says why -- so "no boxes" is never mistaken for "no
     * document". */
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, &options, &report,
                                                           NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_empty(tree), 1);
    ROBO_CHECK_EQ_INT(report.boxes_created, 0u);
    ROBO_CHECK_EQ_INT(report.elements_seen, 1u);
    ROBO_CHECK_EQ_INT(report.skipped_display_none, 1u);

    /* And a pass over that empty tree is legitimate rather than an error. */
    {
        robo_layout_context_t *context = NULL;
        robo_layout_stats_t stats;

        ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_context_run(context, tree, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                          (int)ROBO_LAYOUT_PASS_WALKED);
        robo_layout_context_stats(context, &stats);
        ROBO_CHECK_EQ_INT(stats.objects, 0u);
        ROBO_CHECK_EQ_INT(stats.visited, 0u);
        robo_layout_context_destroy(context);
    }

    layout_fake_dispose(&fake);
    robo_layout_tree_destroy(tree);
    layout_document_release(&doc);
}

/* ------------------------------------------------------------------ */
/* Styles: present, absent, and the flag                               */
/* ------------------------------------------------------------------ */

static void test_build_styles(void)
{
    layout_document_t doc;
    robo_layout_tree_t *tree = NULL;
    layout_fake_source_t fake;
    robo_layout_style_source_t styles;
    robo_layout_build_options_t options;
    robo_layout_build_report_t report;
    robo_layout_node_id_t id;
    size_t index;

    layout_fixture();
    layout_document_build(&doc, "<html><body><p>x</p><span>y</span></body></html>");
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);

    /* No options at all: every box is built with STYLE_MISSING, and the count
     * says so. A caller building a tree without CSS is doing something
     * legitimate, and it is told what it has. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, NULL, &report, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(report.elements_seen, 5u);
    ROBO_CHECK_EQ_INT(report.boxes_created, 5u);
    ROBO_CHECK_EQ_INT(report.boxes_without_style, 5u);
    for (index = 0u; index < 5u; index++) {
        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(tree, index, &id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_has_flag(tree, id,
                                                      ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING),
                          1);
        ROBO_CHECK(robo_layout_object_style(tree, id) == NULL);
        /* The display a box with no style is built under is the property's
         * *initial* value, because a non-inherited property nothing declared is
         * answered by its initial value rather than by an entry. */
        ROBO_CHECK_EQ_INT((int)robo_layout_object_display(tree, id),
                          (int)ROBO_CSS_DISPLAY_INLINE);
    }

    /* Options with a source that answers two of four: the two styled boxes carry
     * the pointer, the two others carry the flag. */
    layout_fake_init(&fake);
    layout_fake_add(&fake, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "body", ROBO_CSS_DISPLAY_BLOCK);
    styles = layout_fake_source_of(&fake);
    robo_layout_build_options_init(&options);
    options.styles = &styles;

    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, &options, &report,
                                                           NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(report.boxes_created, 5u);
    /* html and body answered, head/p/span did not: three boxes without a style. */
    ROBO_CHECK_EQ_INT(report.boxes_without_style, 3u);
    /* The source was actually consulted once per element, and answered for the
     * two the fake knows about: five elements, two answers. */
    ROBO_CHECK_EQ_INT(fake.answers, 2u);

    {
        robo_html_node_handle_t html = layout_document_find(&doc, "html");
        robo_layout_node_id_t html_box = robo_layout_object_id_for_dom_node(tree, &html);

        ROBO_CHECK_EQ_INT(robo_layout_object_has_flag(tree, html_box,
                                                      ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING),
                          0);
        ROBO_CHECK(robo_layout_object_style(tree, html_box) == fake.rules[0].style);
        ROBO_CHECK_EQ_INT((int)robo_layout_object_display(tree, html_box),
                          (int)ROBO_CSS_DISPLAY_BLOCK);
    }
    {
        robo_html_node_handle_t p = layout_document_find(&doc, "p");
        robo_layout_node_id_t p_box = robo_layout_object_id_for_dom_node(tree, &p);

        ROBO_CHECK_EQ_INT(robo_layout_object_has_flag(tree, p_box,
                                                      ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING),
                          1);
        ROBO_CHECK(robo_layout_object_style(tree, p_box) == NULL);
        /* The element `<p>` generated a box even though nothing styled it: the two
         * facts are separate, which is the whole reason the flag exists. */
        ROBO_CHECK_EQ_INT(robo_layout_object_is_live(tree, p_box), 1);
    }

    layout_fake_dispose(&fake);
    robo_layout_tree_destroy(tree);
    layout_document_release(&doc);
}

/* ------------------------------------------------------------------ */
/* Rebuild replaces                                                    */
/* ------------------------------------------------------------------ */

static void test_build_rebuild_replaces(void)
{
    layout_document_t doc;
    robo_layout_tree_t *tree = NULL;
    robo_layout_build_report_t first;
    robo_layout_build_report_t second;

    layout_fixture();
    layout_document_build(&doc, "<html><body><p>x</p></body></html>");
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, NULL, &first, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), first.boxes_created);
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, NULL, &second, NULL),
                      ROBO_OK);

    /* A build clears first, so it always leaves the boxes of exactly one
     * document -- not one document's boxes twice. The tree is derived state and
     * there is no incremental path in 10.1 because nothing consumes a tree finely
     * enough to need one. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), second.boxes_created);
    ROBO_CHECK_EQ_INT(first.boxes_created, second.boxes_created);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_root(tree), 1);

    robo_layout_tree_destroy(tree);
    layout_document_release(&doc);
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

static void test_build_validation(void)
{
    layout_document_t doc;
    robo_layout_tree_t *tree = NULL;
    robo_layout_build_report_t report;
    robo_layout_build_options_t options;
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;

    layout_fixture();
    layout_document_build(&doc, "<html><body></body></html>");
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(NULL, doc.id, NULL, NULL, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_BUILDER);

    robo_layout_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, ROBO_HTML_DOCUMENT_ID_NONE,
                                                           NULL, NULL, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_BUILDER);

    /* A document that has gone is NOT_FOUND, which is a different answer from a
     * caller passing nonsense. */
    robo_layout_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree,
                                                           (robo_html_document_id_t)99999u,
                                                           NULL, NULL, &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_NOT_FOUND);

    /* The report is optional, and an absent one is not a failure. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, NULL, NULL, NULL),
                      ROBO_OK);

    /* A report passed to a *failing* call is zeroed rather than left stale, so a
     * caller that reads it after a failure sees what was done rather than what the
     * last successful build did. */
    memset(&report, 0xAB, sizeof(report));
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, ROBO_HTML_DOCUMENT_ID_NONE,
                                                           NULL, &report, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(report.boxes_created, 0u);
    ROBO_CHECK_EQ_INT(report.elements_seen, 0u);
    ROBO_CHECK_EQ_INT(report.skipped_display_none, 0u);

    /* Options initialise to "no styles", and a NULL options pointer means the
     * same thing. */
    robo_layout_build_options_init(&options);
    ROBO_CHECK(options.styles == NULL);
    robo_layout_build_options_init(NULL);

    robo_layout_tree_destroy(tree);
    layout_document_release(&doc);
}

/* ------------------------------------------------------------------ */
/* A document with no element at all                                   */
/* ------------------------------------------------------------------ */

static void test_build_no_element(void)
{
    layout_document_t doc;
    robo_layout_tree_t *tree = NULL;
    robo_layout_build_report_t report;

    layout_fixture();
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);

    /*
     * Empty markup does *not* produce a document with no element: RoboHTML's tree
     * builder synthesises the elements a well-formed document requires, so the
     * input that produces no element at all is a document whose tree was never
     * built. Asserted rather than assumed, because the difference decides whether
     * the builder has a "nothing to build" path that anything can reach.
     */
    layout_document_build(&doc, "");
    ROBO_CHECK_EQ_INT(layout_document_element_count(&doc), 3u); /* html, head, body */
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, NULL, &report, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(report.elements_seen, 3u);
    ROBO_CHECK_EQ_INT(report.boxes_created, 3u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_empty(tree), 0);

    /* Text with no markup is the same document, not an empty one: a text node is
     * not an element and generates nothing, but the document around it is a
     * document. */
    layout_document_release(&doc);
    layout_document_build(&doc, "just text, no markup at all");
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, NULL, &report, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(report.elements_seen, 3u);
    ROBO_CHECK_EQ_INT(report.boxes_created, 3u);

    layout_document_release(&doc);

    /*
     * The zero-box build is a document whose tree was never built: the document
     * object exists, and it has no document element for the layout root to be. That
     * path has to answer OK with an empty tree rather than fail, because a caller
     * that builds before parsing is doing something legitimate.
     */
    {
        layout_document_t bare;

        layout_fixture();
        bare.id = ROBO_HTML_DOCUMENT_ID_NONE;
        ROBO_CHECK_EQ_INT(robo_html_document_create(&bare.id, NULL), ROBO_OK);
        robo_layout_tree_clear(tree);
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, bare.id, NULL, &report,
                                                              NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(report.boxes_created, 0u);
        ROBO_CHECK_EQ_INT(report.elements_seen, 0u);
        ROBO_CHECK_EQ_INT(report.skipped_display_none, 0u);
        ROBO_CHECK_EQ_INT(robo_layout_tree_is_empty(tree), 1);
        layout_document_release(&bare);
    }

    robo_layout_tree_destroy(tree);
}

/* ------------------------------------------------------------------ */
/* A deep document                                                     */
/* ------------------------------------------------------------------ */

static void test_build_deep(void)
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
    layout_document_t doc;
    robo_layout_tree_t *tree = NULL;
    robo_layout_build_report_t report;

    layout_fixture();

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
    ROBO_CHECK(offset > 0u);
    /* The buffer held the whole document: a truncated fixture would build a
     * shallower tree and the depth assert below would be about the buffer. */
    ROBO_CHECK(offset < sizeof(html) - 1u);

    layout_document_build(&doc, html);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, NULL, &report, NULL),
                      ROBO_OK);

    /*
     * The parser wraps the bare fragment, so the document is html, head, body and
     * then the 128 divs. The count is asserted against the DOM's own walk *and*
     * against the arithmetic, so a wrapper change shows up here rather than as a
     * mysterious depth.
     */
    ROBO_CHECK_EQ_INT(layout_document_element_count(&doc), (size_t)LAYOUT_BUILD_DEPTH + 3u);
    ROBO_CHECK_EQ_INT(report.elements_seen, (size_t)LAYOUT_BUILD_DEPTH + 3u);
    ROBO_CHECK_EQ_INT(report.boxes_created, (size_t)LAYOUT_BUILD_DEPTH + 3u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree),
                      (size_t)LAYOUT_BUILD_DEPTH + 3u);

    /* html 0, head 1, body 1, div1 2 ... div128 129. */
    ROBO_CHECK_EQ_INT(report.depth_max, (uint32_t)(LAYOUT_BUILD_DEPTH + 1));
    ROBO_CHECK_EQ_INT(report.depth_max, layout_tree_max_depth(tree));

    /*
     * And the chain is real rather than merely deep: walking down `last_child` from
     * the root reaches every div, one level at a time, with the depth matching the
     * number of steps. A tree that lost a link when the arena grew would stop
     * early, and the step count is what says so.
     */
    {
        robo_layout_node_id_t id = robo_layout_tree_root(tree);
        uint32_t depth = 0u;
        size_t steps = 0u;

        for (;;) {
            robo_layout_node_id_t child = ROBO_LAYOUT_NODE_ID_NONE;

            ROBO_CHECK_EQ_INT(robo_layout_object_depth(tree, id, &depth), ROBO_OK);
            ROBO_CHECK_EQ_INT(depth, (uint32_t)steps);
            if (steps >= (size_t)LAYOUT_BUILD_DEPTH + 1u) {
                break;
            }
            if (robo_layout_object_last_child(tree, id, &child) != ROBO_OK) {
                break;
            }
            id = child;
            steps++;
        }
        ROBO_CHECK_EQ_INT(steps, (size_t)LAYOUT_BUILD_DEPTH + 1u);
        ROBO_CHECK_EQ_INT(depth, (uint32_t)(LAYOUT_BUILD_DEPTH + 1));
    }

    robo_layout_tree_destroy(tree);
    layout_document_release(&doc);
}

/* ------------------------------------------------------------------ */
/* The allocator returns to its baseline                               */
/* ------------------------------------------------------------------ */

static void test_build_allocator_balance(void)
{
    size_t before;
    layout_document_t doc;
    layout_fake_source_t fake;
    robo_layout_style_source_t styles;
    robo_layout_build_options_t options;
    int pass;

    layout_fixture();
    layout_document_build(&doc, "<html><body><section><p>a</p></section></body></html>");
    layout_fake_init(&fake);
    layout_fake_add(&fake, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_fake_add(&fake, "p", ROBO_CSS_DISPLAY_BLOCK);
    styles = layout_fake_source_of(&fake);
    robo_layout_build_options_init(&options);
    options.styles = &styles;

    before = layout_live_blocks();
    for (pass = 0; pass < 16; pass++) {
        robo_layout_tree_t *tree = NULL;

        ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, &options, NULL,
                                                               NULL),
                          ROBO_OK);
        /* Building into a tree that already has boxes replaces, which is the path
         * a rebuild takes and the one a leak would hide in. */
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, &options, NULL,
                                                               NULL),
                          ROBO_OK);
        /* And a failing build must not leak either. */
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, ROBO_HTML_DOCUMENT_ID_NONE,
                                                               &options, NULL, NULL),
                          ROBO_INVALID_ARGUMENT);
        robo_layout_tree_destroy(tree);
    }
    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);

    layout_fake_dispose(&fake);
    layout_document_release(&doc);
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout_build(void);

void robo_test_register_layout_build(void)
{
    robo_test_register("layout.build.root", test_build_root);
    robo_test_register("layout.build.order", test_build_order);
    robo_test_register("layout.build.display_none", test_build_display_none);
    robo_test_register("layout.build.root_display_none", test_build_root_display_none);
    robo_test_register("layout.build.styles", test_build_styles);
    robo_test_register("layout.build.rebuild", test_build_rebuild_replaces);
    robo_test_register("layout.build.validation", test_build_validation);
    robo_test_register("layout.build.no_element", test_build_no_element);
    robo_test_register("layout.build.deep", test_build_deep);
    robo_test_register("layout.build.allocator_balance", test_build_allocator_balance);
}
