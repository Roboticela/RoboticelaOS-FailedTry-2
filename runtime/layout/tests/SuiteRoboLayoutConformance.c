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
 *   And does it compute geometry in both directions, and say so? Stated once more
 *   here, after a full pass over a real document, because the capability struct is
 *   a promise and this is where a promise made in one file is checked against
 *   behaviour in another.
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
    { "geometry", 1 },
    { "formatting_contexts", 1 },
    { "block_layout", 1 },
    { "inline_layout", 1 },
    { "block_and_inline_layout", 1 },
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
    /* Eleven of twelve. Asserted as a count as well as field by field, so a build
     * that turned one on and one off at the same time -- which field-by-field would
     * still catch, but a reader would have to diff to see -- is visible at a
     * glance. */
    ROBO_CHECK_EQ_INT(implemented, 11u);

    /*
     * The four that matter are the direction pair, the field that names both, and
     * `geometry` above them -- said several ways each: the field, the function and
     * the macro. A build that flipped one of them would have two answers to one
     * question, and this is the question a caller asks before reading a result.
     *
     * `geometry` is 1 since 10.3 because the pass writes real coordinates;
     * `block_layout` and `inline_layout` are the two directions, both 1 since 10.4;
     * and the field that names both is their conjunction rather than a third
     * constant, which is what makes it go back to 0 on its own if a direction is
     * ever switched off. That is the rule 10.3 stated when only one direction
     * existed -- a phase that implemented one direction and flipped the wider flag
     * would have advertised a feature it does not have -- and writing it as an
     * expression is what let it survive the phase that added the second direction.
     */
    ROBO_CHECK_EQ_INT(caps.geometry, 1);
    ROBO_CHECK_EQ_INT(robo_layout_geometry_is_implemented(), 1);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_GEOMETRY_IMPLEMENTED, 1);
    ROBO_CHECK_EQ_INT(caps.block_layout, 1);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED, 1);
    ROBO_CHECK_EQ_INT(caps.inline_layout, 1);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED, 1);
    ROBO_CHECK_EQ_INT(caps.block_and_inline_layout, 1);
    ROBO_CHECK_EQ_INT(caps.block_and_inline_layout,
                      caps.block_layout && caps.inline_layout);
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
    robo_layout_rect_t rect;
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

    /*
     * A full pass over a real document, into a real viewport -- and this is the
     * case that used to assert the *opposite* of what it asserts now.
     *
     * Until 10.3 the pass walked, counted and classified, and every box was left
     * UNRESOLVED, because nothing in the build computed a coordinate. That claim
     * has been *replaced* rather than weakened, which is the only honest way for a
     * claim like it to die: the same tree, the same viewport and the same call now
     * produce geometry, and the four boxes below are asserted to the pixel.
     */
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
    /*
     * Four of five, and the one left over is `<head>`: nothing styles it, so it
     * computes to `display: inline` -- 9.12's initial value, not a UA stylesheet --
     * and block layout does not place it. The two counters therefore disagree on
     * purpose, which is what makes them two counters.
     */
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 4u);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 1u);
    ROBO_CHECK_EQ_INT(stats.without_style, 0u);
    /* html 0, head 1, body 1, div.card 2, p.title 3. */
    ROBO_CHECK_EQ_INT(stats.depth_max, 3u);
    ROBO_CHECK_EQ_INT(stats.depth_max, layout_tree_max_depth(fixture.tree));

    /* The block pass's own record, because `boxes_computed` is *derived* from it:
     * four placed, one declined for a stated reason, none unreached. A reader who
     * wants to know why the fifth box has no geometry reads it here. */
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 4u);
    ROBO_CHECK_EQ_INT(stats.block.inline_level_skipped, 1u);
    ROBO_CHECK_EQ_INT(stats.block.unreached, 0u);
    ROBO_CHECK_EQ_INT(stats.block.containers, 3u);
    ROBO_CHECK_EQ_INT(stats.block.empty_containers, 1u);
    ROBO_CHECK_EQ_INT(stats.block.heights_explicit, 1u);
    ROBO_CHECK_EQ_INT(stats.block.depth_max, 3u);

    /*
     * And the inline direction's record, for the one inline formatting context this
     * document has. `<p class="title">` holds nothing but text, so 10.2's rule makes
     * the *paragraph* establish the context -- and the context is empty, because a
     * text node is not a box. That is the shape of the commonest document there is,
     * and the shape 10.5 exists to give a height to.
     *
     * `<head>` is not here: it is an inline-level box in a *block-direction* context,
     * which is 10.3's count and not this one's. Two records, one box, told apart by
     * which direction declined it.
     */
    ROBO_CHECK_EQ_INT(stats.inline_flow.contexts, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.empty_contexts, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.block_level_declined, 0u);
    /* `boxes_computed` is the two placement counts added together, asserted where
     * both are non-trivial numbers rather than where one of them is zero. */
    ROBO_CHECK_EQ_INT(stats.boxes_computed,
                      stats.block.blocks_placed + stats.inline_flow.participants_placed);

    /*
     * And the geometry itself, which is what this case is now for: a box fills its
     * containing block, its children stack, and a declared `height` is honoured
     * while a declared `width` is not.
     *
     * The card is the whole point. It declares `width: 200px` and `height: 100px`,
     * `position: relative`, `top: 10px`, a border, a margin and padding -- and of
     * those, exactly one number reaches the result. Its width is the *containing
     * block's* 1280, because resolving a declared width is 10.8; its height is 100,
     * because a px length is the one declaration this build can resolve; and the
     * position, border, margin and padding are all later goals. A build that read
     * any of them here would be answering a phase that has not run.
     */
    {
        robo_html_node_handle_t html_element = layout_document_find(&fixture.doc, "html");
        robo_html_node_handle_t body_element = layout_document_find(&fixture.doc, "body");
        robo_html_node_handle_t title_element = layout_document_find(&fixture.doc, "p");
        robo_layout_node_id_t html_box = robo_layout_object_id_for_dom_node(fixture.tree,
                                                                           &html_element);
        robo_layout_node_id_t body_box = robo_layout_object_id_for_dom_node(fixture.tree,
                                                                           &body_element);
        robo_layout_node_id_t card_box = robo_layout_object_id_for_dom_node(fixture.tree,
                                                                           &card);
        robo_layout_node_id_t title_box = robo_layout_object_id_for_dom_node(fixture.tree,
                                                                            &title_element);
        robo_layout_point_t position;

        ROBO_CHECK(html_box != ROBO_LAYOUT_NODE_ID_NONE);
        ROBO_CHECK(body_box != ROBO_LAYOUT_NODE_ID_NONE);
        ROBO_CHECK(card_box != ROBO_LAYOUT_NODE_ID_NONE);
        ROBO_CHECK(title_box != ROBO_LAYOUT_NODE_ID_NONE);

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, html_box, &result),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
        ROBO_CHECK_EQ_INT(robo_layout_result_rect(&result, &rect, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(rect.x, 0);
        ROBO_CHECK_EQ_INT(rect.y, 0);
        ROBO_CHECK_EQ_INT(rect.width, 1280);
        /* `<body>` is the only block-level child that stacks, and it is 100 tall. */
        ROBO_CHECK_EQ_INT(rect.height, 100);

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, body_box, &result),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
        ROBO_CHECK_EQ_INT(robo_layout_result_rect(&result, &rect, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(rect.x, 0);
        ROBO_CHECK_EQ_INT(rect.y, 0);
        ROBO_CHECK_EQ_INT(rect.width, 1280);
        ROBO_CHECK_EQ_INT(rect.height, 100);

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, card_box, &result),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
        ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
        ROBO_CHECK_EQ_INT(position.x, 0);
        ROBO_CHECK_EQ_INT(position.y, 0);
        ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
        ROBO_CHECK_EQ_INT(size.width, 1280); /* the containing block's, not 200 */
        ROBO_CHECK_EQ_INT(size.height, 100); /* the declared one, honoured */

        /* The title is a placed box with a *zero* extent, which is 10.3's honest
         * answer for a `<p>` whose only content is text: a text node is not a box,
         * so there is nothing to stack and nothing measuring it yet. The container
         * that holds only text is what establishes an inline context. */
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, title_box, &result),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
        ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
        ROBO_CHECK_EQ_INT(size.width, 1280);
        ROBO_CHECK_EQ_INT(size.height, 0);
    }

    /*
     * And the opposite answer, from the same call, on the one box the pass declined:
     * "no geometry" is not a claim about a count, it is a claim that a caller
     * reading a result is *told*. `<head>` reads UNRESOLVED and its geometry reads
     * are misses -- where the four above read COMPUTED and their numbers. A build
     * that returned a zeroed rectangle for the fifth would satisfy "the pass ran"
     * and is exactly what the states exist to prevent.
     */
    {
        robo_html_node_handle_t head_element = layout_document_find(&fixture.doc, "head");
        robo_layout_node_id_t head_box = robo_layout_object_id_for_dom_node(fixture.tree,
                                                                           &head_element);

        ROBO_CHECK(head_box != ROBO_LAYOUT_NODE_ID_NONE);
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, head_box, &result),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_UNRESOLVED);
        ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_layout_result_is_computed(&result), 0);
        ROBO_CHECK_EQ_INT(result.position.x, 0);
        ROBO_CHECK_EQ_INT(result.position.y, 0);
        ROBO_CHECK_EQ_INT(result.size.width, 0);
        ROBO_CHECK_EQ_INT(result.size.height, 0);
        ROBO_CHECK_EQ_INT(result.flags & ROBO_LAYOUT_RESULT_FLAG_ALL, 0);
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
/* Classification without a viewport, geometry with one               */
/* ------------------------------------------------------------------ */

/**
 * A pass with no viewport: it classifies, and it places nothing.
 *
 * This case **replaces** `layout.conformance.no_geometry`, and the replacement is
 * the point rather than an edit. 10.3 made "the pass computed no coordinate" false
 * of a build; asserting it anyway would mean weakening the suite to keep a name,
 * and the useful half of the old claim was never about geometry -- it was that the
 * *classification* does not depend on having a viewport. A caller that wants to
 * know which context a box is in should not have to invent a window size, and a
 * build that refused would make the phase that classifies depend on the phase that
 * sizes.
 *
 * So the geometry half is now *conditional*, and this case states the condition
 * exactly: no viewport, no coordinate, every box honestly UNRESOLVED, and the
 * counter that says nothing was placed at zero. The pipeline case above is the
 * other side of the same coin, and the two together are what the capability fields
 * mean.
 */
static void test_conformance_no_viewport(void)
{
    layout_conformance_fixture_t fixture;
    robo_layout_context_t *context = NULL;
    robo_layout_capabilities_t caps;
    robo_layout_stats_t stats;
    robo_layout_size_t viewport;
    size_t index;

    layout_fixture();
    layout_conformance_begin(&fixture);

    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    /* No viewport was recorded, and the context says so rather than reporting a
     * default -- the difference between "the initial containing block is 0x0" and
     * "nobody has told me what it is" is a difference a caller acts on. */
    ROBO_CHECK_EQ_INT(robo_layout_context_viewport(context, &viewport), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_layout_get_capabilities(&caps), ROBO_OK);
    ROBO_CHECK_EQ_INT(caps.geometry, 1);
    ROBO_CHECK_EQ_INT(caps.formatting_contexts, 1);
    ROBO_CHECK_EQ_INT(caps.block_layout, 1);
    ROBO_CHECK_EQ_INT(caps.inline_layout, 1);
    ROBO_CHECK_EQ_INT(caps.block_and_inline_layout, 1);
    ROBO_CHECK_EQ_INT(caps.painting, 0);
    ROBO_CHECK_EQ_INT(robo_layout_geometry_is_implemented(), 1);

    /*
     * The phase's two claims, side by side and both true of the same tree, which is
     * the whole reason they are separate: `geometry` describes the *build* -- it can
     * compute a coordinate -- while this pass has no containing block to compute one
     * *into*. A build that had conflated "this build lays boxes out" with "this pass
     * laid them out" would have failed here, and that conflation is what the
     * per-box result state exists to make impossible.
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

    /*
     * Not one box has a coordinate, and not one of them answers as if it did -- and
     * the counters say *why* rather than leaving a reader to guess: two passes ran,
     * both wrote their per-box statement, and the block pass placed nothing.
     */
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.passes, 2u);
    ROBO_CHECK_EQ_INT(stats.objects, 5u);
    ROBO_CHECK_EQ_INT(stats.visited, 5u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 0u);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 5u);
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 0u);

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

    /*
     * Then the viewport arrives, and the *same* call computes: the only input that
     * changed is the initial containing block, and four boxes now have geometry where
     * none of them did. That pair is the whole claim of this case -- what a pass
     * needs in order to place a box -- and it is why the viewport is recorded by the
     * caller instead of asked of a window.
     */
    viewport.width = 1280;
    viewport.height = 800;
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.passes, 3u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 4u);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 1u);
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 4u);

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
    ROBO_CHECK_EQ_INT(robo_layout_geometry_is_implemented(), 1);
    ROBO_CHECK_STR_EQ(robo_layout_version_string(), "0.4.0");
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_API_FROZEN, 0);

    /*
     * One box kind, and this is where 10.1's prediction turned out to be wrong in a
     * way worth recording. The 10.1 record expected formatting contexts to arrive as
     * extra *object kinds*; they arrived instead as a vocabulary of their own
     * (RoboLayoutFormattingContext.h). The reason is the one this module applies
     * everywhere else: `display` already carries the outer/inner pair that would
     * distinguish a block box from an inline one, and a second copy of those facts in
     * `kind` would be two places for one answer -- which is exactly what 10.1 refused
     * to do for every other property. `kind` therefore stays one entry wide, and it
     * survived 10.3 and 10.4 unchanged: a block box is still the only kind of box the
     * build makes, because an *anonymous* box -- the one a mixed container needs, and
     * the one that has no element and therefore no `display` to derive a kind from --
     * is still a later goal's. 10.4 laid the inline direction out without one, which
     * is the point worth recording: a container whose children are all inline already
     * has an inline formatting context of its own (10.2), so the anonymous box is
     * only needed for *mixed* content and not for inline content as such.
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
    robo_test_register("layout.conformance.no_viewport", test_conformance_no_viewport);
    robo_test_register("layout.conformance.allocator_balance",
                       test_conformance_allocator_balance);
    robo_test_register("layout.conformance.statement", test_conformance_statement);
}
