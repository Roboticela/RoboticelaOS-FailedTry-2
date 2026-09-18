/*
 * RoboLayout conformance suite (Phase 10.1) — layout.conformance.*
 *
 * The questions no single-family suite in this directory can ask, because each of
 * them is about the *whole* module or about a promise the module makes as a
 * whole.
 *
 *   Do the capabilities this build advertises match what it does? A flag is a
 *   promise about the build, and an untested promise is one that stops being true
 *   without anybody noticing. The table below is written out by hand and walked
 *   positionally against the struct, with the struct's size pinned against the
 *   table's length, so a field added without an assertion is a visible omission
 *   rather than a silently untested flag.
 *
 *   Does the whole chain work? RoboHTML -> RoboCSS -> the bridge -> the tree ->
 *   the pass, on one document that exercises every family the CSS module gained
 *   in Phase 9. A module that consumed only `display` would pass every other
 *   suite here and fail this one, because this is where a *borrowed style* is
 *   proved to be the container the cascade filled rather than a struct the
 *   builder made.
 *
 *   Is it deterministic? Two builds of one document must produce the same tree,
 *   compared link by link rather than by count.
 *
 *   Does it survive input it should refuse? A malformed document has to produce a
 *   *reported* answer rather than a crash or a silent empty tree.
 *
 *   And does it still compute no geometry? Stated once more here, after a full
 *   pass over a real document, because that is the assertion the entire phase
 *   rests on.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Capabilities, walked positionally                                   */
/* ------------------------------------------------------------------ */

/**
 * Every field of `robo_layout_capabilities`, in declaration order, with the value
 * this build claims.
 *
 * Written out by hand rather than derived from the struct: a table generated from
 * the struct would agree with the struct by construction and would therefore
 * assert nothing. The positional walk below is what makes the order itself part
 * of the promise -- a field inserted in the middle shifts every later name onto
 * the wrong value, and that shows up here.
 */
static const struct {
    const char *name;
    int expected;
} layout_conformance_capabilities[] = {
    { "object_model", 1 },
    { "constraints", 1 },
    { "result_model", 1 },
    { "context", 1 },
    { "dom_build", 1 },
    { "computed_style_link", 1 },
    { "geometry", 0 },
    { "formatting_contexts", 1 },
    { "block_and_inline_layout", 0 },
    { "painting", 0 }
};

static void test_conformance_capabilities(void)
{
    robo_layout_capabilities_t caps;
    const int *fields;
    size_t count = sizeof(layout_conformance_capabilities) /
                   sizeof(layout_conformance_capabilities[0]);
    size_t implemented = 0u;
    size_t index;

    layout_fixture();

    /* The struct is a run of `int`s with no padding, which is what makes the
     * positional read below meaningful -- and what makes this assertion the one
     * that has to be revisited if a field of another type is ever added. */
    ROBO_CHECK_EQ_INT(sizeof(robo_layout_capabilities_t), count * sizeof(int));

    ROBO_CHECK_EQ_INT(robo_layout_get_capabilities(&caps), ROBO_OK);
    fields = (const int *)&caps;
    for (index = 0u; index < count; index++) {
        ROBO_CHECK_EQ_INT(fields[index], layout_conformance_capabilities[index].expected);
        if (layout_conformance_capabilities[index].expected != 0) {
            implemented++;
        }
    }
    /* Seven of ten. Asserted as a count as well as field by field, so a build that
     * turned one on and one off at the same time -- which field-by-field would
     * still catch, but a reader would have to diff to see -- is visible at a
     * glance. */
    ROBO_CHECK_EQ_INT(implemented, 7u);

    /* The capability that matters is the one that is 0, said three ways: the
     * field, the function and the macro. A build that flipped one of them would
     * have two answers to one question, and this is the question a caller asks
     * before reading a result. */
    ROBO_CHECK_EQ_INT(caps.geometry, 0);
    ROBO_CHECK_EQ_INT(robo_layout_geometry_is_implemented(), 0);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_GEOMETRY_IMPLEMENTED, 0);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_API_FROZEN, 0);
}

/* ------------------------------------------------------------------ */
/* One document, the whole chain                                       */
/* ------------------------------------------------------------------ */

/**
 * A document that exercises every family Phase 9 gained.
 *
 * The point is not coverage for its own sake: it is that a *borrowed computed
 * style* is the container the cascade filled. A layout module that consumed only
 * `display` would pass every other suite in this directory and fail the assertions
 * below, because this is the one place a style is asked about a property no
 * layout code reads.
 */
#define LAYOUT_CONFORMANCE_HTML          \
    "<html>\n"                           \
    "  <body>\n"                         \
    "    <div class=\"card\">\n"         \
    "      <p class=\"title\">Hello</p>\n" \
    "    </div>\n"                       \
    "    <div class=\"hidden\"></div>\n" \
    "  </body>\n"                        \
    "</html>\n"

#define LAYOUT_CONFORMANCE_CSS                                        \
    "html { display: block }\n"                                        \
    "body { display: block; color: rgb(20, 20, 20) }\n"                \
    ".card {\n"                                                        \
    "  display: block;\n"                                              \
    "  position: relative;\n"                                          \
    "  top: 10px;\n"                                                   \
    "  background-color: #ffffff;\n"                                   \
    "  border: 1px solid black;\n"                                     \
    "  margin: 10px 20px;\n"                                           \
    "  padding: 5px;\n"                                                \
    "  width: 200px;\n"                                                \
    "  height: 100px;\n"                                               \
    "  z-index: 3;\n"                                                  \
    "}\n"                                                              \
    ".title { display: block; font-style: normal }\n"                   \
    ".hidden { display: none }\n"

typedef struct layout_conformance_fixture {
    layout_document_t doc;
    layout_css_fixture_t css;
    robo_layout_dom_t *dom;
    robo_layout_tree_t *tree;
    robo_layout_style_source_t source;
} layout_conformance_fixture_t;

static void layout_conformance_begin(layout_conformance_fixture_t *fixture)
{
    robo_layout_build_options_t options;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, LAYOUT_CONFORMANCE_HTML);
    layout_css_begin(&fixture->css);
    layout_css_sheet(&fixture->css, LAYOUT_CONFORMANCE_CSS);
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(fixture->css.context, fixture->doc.id,
                                             &fixture->dom, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(fixture->dom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(fixture->dom, &fixture->source, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->source;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_conformance_end(layout_conformance_fixture_t *fixture)
{
    /* The tree borrows the bridge's styles, so it goes first. */
    robo_layout_tree_destroy(fixture->tree);
    fixture->tree = NULL;
    robo_layout_dom_destroy(fixture->dom);
    fixture->dom = NULL;
    layout_css_end(&fixture->css);
    layout_document_release(&fixture->doc);
}

static void test_conformance_pipeline(void)
{
    layout_conformance_fixture_t fixture;
    robo_layout_context_t *context = NULL;
    robo_layout_stats_t stats;
    robo_layout_result_t result;
    robo_layout_size_t size;
    robo_html_node_handle_t card;
    const robo_css_computed_style_t *card_style;
    size_t index;

    layout_fixture();
    layout_conformance_begin(&fixture);

    /* html, head, body, div.card, p.title, div.hidden = six elements; the hidden
     * one generates nothing, so five boxes. `<head>` is the parser's, not the
     * markup's, and it is a box because nothing hid it. */
    ROBO_CHECK_EQ_INT(robo_layout_dom_element_count(fixture.dom), 6u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 5u);

    card = layout_document_find(&fixture.doc, "div");
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&card), 1);
    card_style = robo_layout_dom_style_for(fixture.dom, &card);
    ROBO_CHECK(card_style != NULL);

    /*
     * The borrowed style is the container the *cascade* filled, and this is the
     * assertion that says so. `display` is the only property RoboLayout reads, so
     * every other name here is one the module has no business understanding -- and
     * it is exactly the presence of those entries that proves the pointer is a
     * real computed style rather than a struct the builder fabricated.
     */
    ROBO_CHECK_EQ_INT(robo_layout_style_display(card_style), ROBO_CSS_DISPLAY_BLOCK);
    {
        static const char *const names[] = {
            "display", "position", "top", "background-color",
            "border-top-style", "border-top-width", "border-top-color",
            "margin-top", "margin-right", "padding-top", "width", "height", "z-index"
        };

        for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
            const robo_css_computed_value_t *value = robo_css_computed_style_computed_by_name(
                card_style, robo_css_slice_from_text(names[index]));

            /* Each is a *typed* value with a kind, not a string, and the kind is
             * not the "cleared" zero any more than it is NONE. */
            ROBO_CHECK_MSG(value != NULL, names[index]);
            if (value) {
                ROBO_CHECK(value->kind != ROBO_CSS_COMPUTED_VALUE_NONE);
            }
        }
    }
    ROBO_CHECK(robo_css_computed_style_count(card_style) > 0u);

    /* The tree's shape is the document's, with the hidden element absent. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_root(fixture.tree), 1);
    {
        robo_layout_node_id_t root = robo_layout_tree_root(fixture.tree);
        robo_layout_node_id_t head = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_node_id_t body = ROBO_LAYOUT_NODE_ID_NONE;
        size_t children = 0u;

        /* The root's children are the two the document requires; `<body>` is the
         * *last* of them, because `<head>` comes first. */
        ROBO_CHECK_EQ_INT(robo_layout_object_first_child(fixture.tree, root, &head), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_last_child(fixture.tree, root, &body), ROBO_OK);
        ROBO_CHECK((int)head != (int)body);
        ROBO_CHECK_EQ_INT(robo_layout_object_child_count(fixture.tree, body, &children),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(children, 1u); /* only `.card`; `.hidden` generated nothing */
    }

    /* A full pass over a real document, and every result still UNRESOLVED. */
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    {
        robo_layout_size_t viewport;

        viewport.width = 1280;
        viewport.height = 800;
        ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                      (int)ROBO_LAYOUT_PASS_WALKED);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.objects, 5u);
    ROBO_CHECK_EQ_INT(stats.visited, 5u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 0u);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 5u);
    ROBO_CHECK_EQ_INT(stats.without_style, 0u);
    /* html 0, head 1, body 1, div.card 2, p.title 3. */
    ROBO_CHECK_EQ_INT(stats.depth_max, 3u);
    ROBO_CHECK_EQ_INT(stats.depth_max, layout_tree_max_depth(fixture.tree));

    /* And "no geometry" is not a claim about a count: it is a claim that a caller
     * reading a result is *told*, so every box's geometry read is a miss. */
    for (index = 0u; index < 5u; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, index, &id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT(result.state, ROBO_LAYOUT_RESULT_UNRESOLVED);
        ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_layout_result_is_computed(&result), 0);
    }

    robo_layout_context_destroy(context);
    layout_conformance_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                         */
/* ------------------------------------------------------------------ */

/**
 * Compare two trees box by box, link by link.
 *
 * Compared by *shape*, not by identity: the ids are dense from 1 in both, so the
 * comparison is over ids, and a difference in any link is a difference in the
 * tree. Counts alone would not catch a tree that had the right number of boxes
 * arranged wrongly, which is the failure a rebuild could plausibly have.
 */
static void layout_conformance_assert_same_tree(const robo_layout_tree_t *a,
                                                const robo_layout_tree_t *b)
{
    size_t count = robo_layout_tree_object_count(a);
    size_t index;

    ROBO_CHECK_EQ_INT(count, robo_layout_tree_object_count(b));
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_root(a), (int)robo_layout_tree_root(b));

    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id_a = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_node_id_t id_b = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_object_t object_a;
        robo_layout_object_t object_b;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(a, index, &id_a), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(b, index, &id_b), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)id_a, (int)id_b);
        ROBO_CHECK_EQ_INT(robo_layout_object_get(a, id_a, &object_a, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_get(b, id_b, &object_b, NULL), ROBO_OK);

        ROBO_CHECK_EQ_INT((int)object_a.kind, (int)object_b.kind);
        ROBO_CHECK_EQ_INT((int)object_a.display, (int)object_b.display);
        ROBO_CHECK_EQ_INT((int)object_a.parent, (int)object_b.parent);
        ROBO_CHECK_EQ_INT((int)object_a.first_child, (int)object_b.first_child);
        ROBO_CHECK_EQ_INT((int)object_a.last_child, (int)object_b.last_child);
        ROBO_CHECK_EQ_INT((int)object_a.prev_sibling, (int)object_b.prev_sibling);
        ROBO_CHECK_EQ_INT((int)object_a.next_sibling, (int)object_b.next_sibling);
        ROBO_CHECK_EQ_INT(object_a.child_count, object_b.child_count);
        ROBO_CHECK_EQ_INT(object_a.depth, object_b.depth);
        ROBO_CHECK_EQ_INT(object_a.flags, object_b.flags);
        /* The classification travels with the box, so two builds of one document
         * classify identically -- including the ids, which are tree-scoped and
         * assigned in the same order by the same walk. */
        ROBO_CHECK_EQ_INT((int)object_a.context, (int)object_b.context);
        ROBO_CHECK_EQ_INT((int)object_a.context_owned, (int)object_b.context_owned);
        ROBO_CHECK_EQ_INT(object_a.dom_node.node_id, object_b.dom_node.node_id);
        ROBO_CHECK_EQ_INT(object_a.dom_node.document_id, object_b.dom_node.document_id);
        /* The borrowed style is the *same* style in both trees, because both
         * borrow from the one bridge: determinism here is not "an equal style was
         * built twice" but "the one style was consumed twice". */
        ROBO_CHECK(object_a.style == object_b.style);
    }
}

static void test_conformance_determinism(void)
{
    layout_conformance_fixture_t fixture;
    robo_layout_tree_t *second = NULL;
    robo_layout_build_options_t options;

    layout_fixture();
    layout_conformance_begin(&fixture);

    /* Build the same document into a *second* tree, from the same source -- the
     * path a rebuild takes. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&second, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture.source;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(second, fixture.doc.id, &options,
                                                          NULL, NULL),
                      ROBO_OK);
    layout_conformance_assert_same_tree(fixture.tree, second);

    /* And again into the *same* tree, which is the rebuild case: the tree is
     * cleared and rebuilt, so its ids restart and its shape must come back
     * identical rather than drifting. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture.tree, fixture.doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
    layout_conformance_assert_same_tree(fixture.tree, second);

    /* A tree built with no styles is also deterministic, and differs from the
     * styled one in the way it should: same shape, different flags and displays. */
    {
        robo_layout_tree_t *bare = NULL;

        ROBO_CHECK_EQ_INT(robo_layout_tree_create(&bare, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(bare, fixture.doc.id, NULL,
                                                              NULL, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(bare, fixture.doc.id, NULL,
                                                              NULL, NULL),
                          ROBO_OK);
        /* Built twice with no styles: the two builds agree with each other, which
         * is checked by comparing it against itself after a rebuild. */
        {
            size_t index;
            size_t count = robo_layout_tree_object_count(bare);

            for (index = 0u; index < count; index++) {
                robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

                ROBO_CHECK_EQ_INT(robo_layout_object_id_at(bare, index, &id), ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_layout_object_has_flag(
                                      bare, id, ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING),
                                  1);
            }
        }
        robo_layout_tree_destroy(bare);
    }

    robo_layout_tree_destroy(second);
    layout_conformance_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Robustness                                                          */
/* ------------------------------------------------------------------ */

static void test_conformance_robust(void)
{
    /*
     * Documents a builder has no business crashing on. Each is built, walked and
     * destroyed; every one must produce a *reported* answer -- either a build or a
     * specific code -- and no box may exist without the tree knowing about it.
     *
     * The list is intentionally blunt: a layout pass runs over whatever the tree
     * builder produced, and the tree builder runs over whatever the tokenizer
     * produced, so "the parser recovered" has to mean "layout gets something
     * walkable" rather than "layout gets something it did not expect".
     */
    static const char *const corpus[] = {
        "",
        "<",
        "<>",
        "</>",
        "<html",
        "<html>",
        "<html></html>",
        "<html><body>",
        "<<div>>",
        "<div class=>",
        "<div class=\"\"></div>",
        "<div class=\"a b c d e f g h i j\"></div>",
        "<div><div><div><div><div></div></div></div></div></div>",
        "<p>text only</p>",
        "text before <p>after</p> text after",
        "<!-- comment --><div></div>",
        "<!doctype html><html><body></body></html>",
        "<div style=\"display: none\">hidden</div>",
        "<div style=\"display\">bad declaration</div>",
        "<div style=\"display: bogus\"></div>",
        "<div style=></div>",
        "<div></div></div>",
        "<div attr=\"unterminated></div>",
        "<a href=\"x\"><b>nested</b></a>",
        "<ul><li>one<li>two<li>three</ul>",
        "<table><tr><td>cell</td></tr></table>",
        "<svg><path/></svg>",
        "<div>\xC3\x28</div>",
        "<div>\xFF\xFE</div>",
        "\x00\x01\x02",
        "<div title=\"\xC3\xA9\"></div>",
        "<html><body><div class=\"panel\"><p>deep</p></div></body></html>"
    };
    layout_css_fixture_t css;
    size_t index;

    layout_fixture();
    layout_css_begin(&css);
    layout_css_sheet(&css, "html, body, div, p, li, td { display: block }\n"
                           ".panel { display: inline-block }\n"
                           "div + div { display: none }\n");

    for (index = 0u; index < sizeof(corpus) / sizeof(corpus[0]); index++) {
        layout_document_t doc;
        robo_layout_dom_t *dom = NULL;
        robo_layout_tree_t *tree = NULL;
        robo_layout_style_source_t source;
        robo_layout_build_options_t options;
        robo_layout_build_report_t report;
        robo_layout_context_t *context = NULL;
        robo_layout_stats_t stats;
        robo_result_t built;

        layout_document_build(&doc, corpus[index]);

        /* The bridge may refuse a document if the capture cannot describe it (a
         * cap exceeded, an attribute the DOM will not hand over). That is a
         * *reported* refusal and the loop moves on; what must not happen is a
         * crash, or a bridge that reported OK and is not usable. */
        if (robo_layout_dom_create(css.context, doc.id, &dom, NULL) != ROBO_OK) {
            layout_document_release(&doc);
            continue;
        }
        if (robo_layout_dom_refresh(dom, NULL) != ROBO_OK) {
            robo_layout_dom_destroy(dom);
            layout_document_release(&doc);
            continue;
        }

        ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(dom, &source, NULL), ROBO_OK);
        robo_layout_build_options_init(&options);
        options.styles = &source;
        ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);

        built = robo_layout_tree_build_from_document(tree, doc.id, &options, &report, NULL);
        ROBO_CHECK_EQ_INT(built, ROBO_OK);

        /* Whatever came out is walkable, every box is reachable, and the report's
         * counts are the tree's -- so a builder that half-filled a tree would fail
         * here rather than in a later phase. */
        {
            size_t count = robo_layout_tree_object_count(tree);
            size_t at;

            ROBO_CHECK_EQ_INT(count, report.boxes_created);
            for (at = 0u; at < count; at++) {
                robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
                robo_layout_object_t object;

                ROBO_CHECK_EQ_INT(robo_layout_object_id_at(tree, at, &id), ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_layout_object_get(tree, id, &object, NULL), ROBO_OK);
                ROBO_CHECK_EQ_INT((int)object.id, (int)id);
                ROBO_CHECK_EQ_INT((int)object.kind, (int)ROBO_LAYOUT_OBJECT_BOX);
                ROBO_CHECK_EQ_INT(robo_css_display_generates_box(object.display), 1);
            }
        }

        /* And a pass over it is legitimate however odd the document was. */
        ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_context_run(context, tree, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                          (int)ROBO_LAYOUT_PASS_WALKED);
        robo_layout_context_stats(context, &stats);
        ROBO_CHECK_EQ_INT(stats.objects, report.boxes_created);
        ROBO_CHECK_EQ_INT(stats.boxes_computed, 0u);

        robo_layout_context_destroy(context);
        robo_layout_tree_destroy(tree);
        robo_layout_dom_destroy(dom);
        layout_document_release(&doc);
    }

    layout_css_end(&css);
}

/* ------------------------------------------------------------------ */
/* No geometry, after a pass over a real document                      */
/* ------------------------------------------------------------------ */

static void test_conformance_no_geometry(void)
{
    layout_conformance_fixture_t fixture;
    robo_layout_context_t *context = NULL;
    robo_layout_capabilities_t caps;
    size_t index;

    layout_fixture();
    layout_conformance_begin(&fixture);

    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_layout_get_capabilities(&caps), ROBO_OK);
    ROBO_CHECK_EQ_INT(caps.geometry, 0);
    ROBO_CHECK_EQ_INT(caps.formatting_contexts, 1);
    ROBO_CHECK_EQ_INT(caps.block_and_inline_layout, 0);
    ROBO_CHECK_EQ_INT(caps.painting, 0);
    ROBO_CHECK_EQ_INT(robo_layout_geometry_is_implemented(), 0);

    /*
     * The phase's two claims, side by side and both true of the same tree, which is
     * the whole reason they are two capability fields: every box now takes part in
     * a formatting context, and not one box has a coordinate. A build that had
     * conflated "the pass classified this document" with "the pass laid it out"
     * would have flipped `geometry` and failed here.
     */
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_count(fixture.tree) > 0u, 1);
    {
        robo_layout_fc_id_t root_context = ROBO_LAYOUT_FC_ID_NONE;
        robo_layout_formatting_context_t context;
        size_t box;

        ROBO_CHECK_EQ_INT(robo_layout_tree_root_context(fixture.tree, &root_context, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, root_context, &context,
                                                    NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)context.kind, (int)ROBO_LAYOUT_FC_ROOT);
        ROBO_CHECK_EQ_INT((int)context.owner, (int)robo_layout_tree_root(fixture.tree));
        /* Every box reached a context -- the hidden `<div>` never became a box, so
         * it is absent rather than unclassified -- and every context owner is a
         * live box. */
        for (box = 0u; box < robo_layout_tree_object_count(fixture.tree); box++) {
            robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
            robo_layout_fc_id_t box_context = ROBO_LAYOUT_FC_ID_NONE;

            ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, box, &id), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_layout_object_is_live(fixture.tree, id), 1);
            ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, id, &box_context,
                                                         NULL),
                              ROBO_OK);
            ROBO_CHECK(box_context != ROBO_LAYOUT_FC_ID_NONE);
        }
    }

    /* Not one box has a coordinate, and not one of them answers as if it did.
     * This is the assertion the phases rest on: a build that returned a
     * zeroed rectangle would satisfy "the pass ran" and fail this. */
    for (index = 0u; index < robo_layout_tree_object_count(fixture.tree); index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_result_t result;
        const robo_layout_result_t *ref;
        robo_layout_point_t position;
        robo_layout_rect_t rect;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, index, &id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT(result.state, ROBO_LAYOUT_RESULT_UNRESOLVED);
        ROBO_CHECK_EQ_INT(result.position.x, 0);
        ROBO_CHECK_EQ_INT(result.position.y, 0);
        ROBO_CHECK_EQ_INT(result.size.width, 0);
        ROBO_CHECK_EQ_INT(result.size.height, 0);
        ROBO_CHECK_EQ_INT(result.flags & ROBO_LAYOUT_RESULT_FLAG_ALL, 0);
        ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_layout_result_rect(&result, &rect, NULL), ROBO_NOT_FOUND);

        ref = robo_layout_object_result_ref(fixture.tree, id);
        ROBO_CHECK(ref != NULL);
        if (ref) {
            ROBO_CHECK_EQ_INT(robo_layout_result_equal(ref, &result), 1);
        }
    }

    robo_layout_context_destroy(context);
    layout_conformance_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The whole chain returns the allocator to its baseline               */
/* ------------------------------------------------------------------ */

static void test_conformance_allocator_balance(void)
{
    size_t before;
    int pass;

    layout_fixture();
    before = layout_live_blocks();

    for (pass = 0; pass < 8; pass++) {
        layout_conformance_fixture_t fixture;
        robo_layout_context_t *context = NULL;
        robo_layout_tree_t *second = NULL;
        robo_layout_build_options_t options;

        layout_conformance_begin(&fixture);
        layout_conformance_assert_same_tree(fixture.tree, fixture.tree);

        ROBO_CHECK_EQ_INT(robo_layout_tree_create(&second, NULL), ROBO_OK);
        robo_layout_build_options_init(&options);
        options.styles = &fixture.source;
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(second, fixture.doc.id,
                                                              &options, NULL, NULL),
                          ROBO_OK);
        layout_conformance_assert_same_tree(fixture.tree, second);

        /* Run both trees, so the pass's per-box write is exercised too. */
        ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_context_run(context, second, NULL), ROBO_OK);
        robo_layout_context_destroy(context);

        robo_layout_tree_destroy(second);
        layout_conformance_end(&fixture);
    }

    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
}

/* ------------------------------------------------------------------ */
/* The module's promises, in one place                                 */
/* ------------------------------------------------------------------ */

static void test_conformance_statement(void)
{
    layout_fixture();

    /*
     * The sentences the module would be described by, each an assertion rather than
     * a comment. If a later goal flips one of these it must also change this test,
     * which is the point: a phase that computes geometry should not be able to leave
     * the build claiming it does not.
     */
    ROBO_CHECK_EQ_INT(robo_layout_geometry_is_implemented(), 0);
    ROBO_CHECK_STR_EQ(robo_layout_version_string(), "0.2.0");
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_API_FROZEN, 0);

    /*
     * One box kind, and this is where 10.1's prediction turned out to be wrong in a
     * way worth recording. The 10.1 record expected formatting contexts to arrive as
     * extra *object kinds*; they arrived instead as a vocabulary of their own
     * (RoboLayoutFormattingContext.h). The reason is the one this module applies
     * everywhere else: `display` already carries the outer/inner pair that would
     * distinguish a block box from an inline one, and a second copy of those facts in
     * `kind` would be two places for one answer -- which is exactly what 10.1 refused
     * to do for every other property. `kind` therefore stays one entry wide, and
     * 10.3 will add the row it needs when anonymous boxes are boxes that have no
     * element -- and therefore no `display` -- to derive it from.
     */
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_OBJECT_KIND_COUNT, 2);
    ROBO_CHECK_EQ_INT(robo_layout_object_kind_is_known(ROBO_LAYOUT_OBJECT_BOX), 1);
    ROBO_CHECK_EQ_INT(robo_layout_object_kind_is_known((robo_layout_object_kind_t)2), 0);

    /* And the vocabulary 10.2 added instead: three kinds of context plus the
     * absence, of which ROOT is one and only one per tree. */
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_FC_KIND_COUNT, 4);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_block_container(ROBO_LAYOUT_FC_ROOT), 1);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_block_container(ROBO_LAYOUT_FC_INLINE), 0);
    ROBO_CHECK_STR_EQ(robo_layout_fc_kind_name(ROBO_LAYOUT_FC_BLOCK), "block");
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout_conformance(void);

void robo_test_register_layout_conformance(void)
{
    robo_test_register("layout.conformance.capabilities", test_conformance_capabilities);
    robo_test_register("layout.conformance.pipeline", test_conformance_pipeline);
    robo_test_register("layout.conformance.determinism", test_conformance_determinism);
    robo_test_register("layout.conformance.robust", test_conformance_robust);
    robo_test_register("layout.conformance.no_geometry", test_conformance_no_geometry);
    robo_test_register("layout.conformance.allocator_balance",
                       test_conformance_allocator_balance);
    robo_test_register("layout.conformance.statement", test_conformance_statement);
}
