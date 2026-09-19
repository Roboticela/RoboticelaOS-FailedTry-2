/*
 * RoboLayout inline-layout suite (Phase 10.4) — layout.inline.*
 *
 * The second suite in this directory that asserts a number, and the numbers it
 * asserts are *line* numbers: a cursor that advances, a line that moves down when a
 * participant does not fit, and a union that grows as a transparent inline box's
 * contents are placed.
 *
 * Five groups, in the order the phase is built:
 *
 *   the vocabulary   the stats record, and the one display predicate this module
 *                    deliberately does not own
 *
 *   placement        atomics on a line, the cursor arithmetic, and the union that a
 *                    non-atomic inline box *is*
 *
 *   wrapping         the cursor returning to the line start, and the two cases that
 *                    must not wrap -- a box wider than its line, and a transparent
 *                    box, which is not a unit and wraps inside itself instead
 *
 *   nesting          a transparent box inside a transparent box, which is where the
 *                    coordinate convention (a result is in its *containing block's*
 *                    space) has to survive a rebase
 *
 *   the pass         determinism, invalidation, a viewport that changes the answer,
 *                    the real CSS pipeline, and the allocator
 *
 * The fixtures are *scripted* styles rather than a stylesheet, except in
 * `layout.inline.pipeline`: a case whose subject is cursor arithmetic should fail
 * because the arithmetic is wrong and not because a selector did not match, and a
 * case whose subject is "these numbers came from CSS" should go through CSS.
 *
 * One fact about every fixture below, because it is what makes the assertions
 * readable: a block box fills its containing block (10.3), so the *line width* an
 * inline formatting context gets is the viewport width. A case that wants a wrap
 * asks for a narrow viewport rather than for a declared width, because a declared
 * width is not read in the block direction until 10.8.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A scripted style source with sizes                                  */
/* ------------------------------------------------------------------ */

/** One scripted answer: this tag computes to this display, and these declarations.
 *
 *  `has_width`/`has_height` are separate from the numbers because the difference
 *  between "declares 0px" and "declares nothing" is exactly the difference between
 *  a box that is zero-sized on purpose and a box whose size comes from somewhere
 *  else. A fixture that conflated them could not test either. */
typedef struct layout_inline_rule {
    const char *tag;
    robo_css_display_t display;
    int has_width;
    double width_px;
    int has_height;
    double height_px;
} layout_inline_rule_t;

typedef struct layout_inline_style {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
} layout_inline_style_t;

#define LAYOUT_INLINE_MAX_RULES 16u

typedef struct layout_inline_source {
    layout_inline_style_t rules[LAYOUT_INLINE_MAX_RULES];
    size_t count;
} layout_inline_source_t;

/**
 * A real computed style carrying a real DISPLAY and, when asked for, real
 * LENGTHs.
 *
 * Built through 9.8's own maker and container rather than a hand-written struct,
 * for the reason the 10.1 fixture gives for the same thing: a fake that fabricated
 * the value would prove the pass reads a struct, not that it reads a computed CSS
 * value. Both lengths go in as LENGTHs in `px` because that is what 9.8's computed
 * rule produces for an absolute unit -- which is exactly the shape the pass has to
 * recognise, and therefore the shape worth constructing.
 */
static void layout_inline_style_add(layout_inline_source_t *source,
                                    const layout_inline_rule_t *rule)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;
    size_t existing;

    layout_fixture();
    ROBO_CHECK_EQ_INT(source->count < LAYOUT_INLINE_MAX_RULES, 1);

    /*
     * Two rules for one tag is refused here rather than left to the lookup, which
     * answers with the first match -- so a case that declared a tag twice would
     * quietly test the *earlier* rule and pass or fail for a reason its own text
     * does not state. A fixture is a script, and a script with two lines for one
     * element is a mistake in the test, not a document.
     */
    for (existing = 0u; existing < source->count; existing++) {
        ROBO_CHECK(strcmp(source->rules[existing].tag, rule->tag) != 0);
    }
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_display(&value, rule->display, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("display"),
                                                       0u, &specified, &value, NULL),
                      ROBO_OK);

    if (rule->has_width) {
        value = (robo_css_computed_value_t)ROBO_CSS_CLEARED;
        specified = (robo_css_value_t)ROBO_CSS_CLEARED;
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, rule->width_px,
                                                             ROBO_CSS_UNIT_PX, 0, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                           robo_css_slice_from_text("width"),
                                                           0u, &specified, &value, NULL),
                          ROBO_OK);
    }
    if (rule->has_height) {
        value = (robo_css_computed_value_t)ROBO_CSS_CLEARED;
        specified = (robo_css_value_t)ROBO_CSS_CLEARED;
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, rule->height_px,
                                                             ROBO_CSS_UNIT_PX, 0, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                           robo_css_slice_from_text("height"),
                                                           0u, &specified, &value, NULL),
                          ROBO_OK);
    }

    strncpy(source->rules[source->count].tag, rule->tag,
            sizeof(source->rules[source->count].tag) - 1u);
    source->rules[source->count].tag[sizeof(source->rules[source->count].tag) - 1u] = '\0';
    source->rules[source->count].style = style;
    source->count++;
}

static const robo_css_computed_style_t *layout_inline_style_lookup(
    void *userdata,
    const robo_html_node_handle_t *element)
{
    layout_inline_source_t *source = (layout_inline_source_t *)userdata;
    const char *name;
    size_t i;

    if (!source || !element) {
        return NULL;
    }
    name = robo_html_element_name(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (i = 0u; i < source->count; i++) {
        if (strcmp(source->rules[i].tag, name) == 0) {
            return source->rules[i].style;
        }
    }
    /* An element nothing declared for: the property's *initial* value, which is
     * `inline` and therefore not in the block flow. That is what makes a document's
     * geometry depend on the declarations the test wrote rather than on the
     * elements the parser synthesized -- and it is why the parser's `<head>` is an
     * inline box this build declines. */
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

typedef struct layout_inline_fixture {
    layout_document_t doc;
    layout_inline_source_t source;
    robo_layout_style_source_t styles;
    robo_layout_tree_t *tree;
} layout_inline_fixture_t;

static void layout_inline_begin(layout_inline_fixture_t *fixture,
                                const char *html,
                                const layout_inline_rule_t *rules,
                                size_t count)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    for (index = 0u; index < count; index++) {
        layout_inline_style_add(&fixture->source, &rules[index]);
    }
    robo_layout_style_source_init(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.computed_style_for = layout_inline_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_inline_end(layout_inline_fixture_t *fixture)
{
    size_t index;

    robo_layout_tree_destroy(fixture->tree);
    fixture->tree = NULL;
    for (index = 0u; index < fixture->source.count; index++) {
        robo_css_computed_style_destroy(fixture->source.rules[index].style);
        fixture->source.rules[index].style = NULL;
    }
    fixture->source.count = 0u;
    layout_document_release(&fixture->doc);
}

/** One pass with a viewport, and both records reported. */
static void layout_inline_pass_with(layout_inline_fixture_t *fixture,
                                    int32_t viewport_width,
                                    robo_layout_stats_t *out_stats,
                                    robo_result_t *out_result)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;
    robo_result_t status;

    viewport.width = viewport_width;
    viewport.height = 600;
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    status = robo_layout_context_run(context, fixture->tree, NULL);
    robo_layout_context_stats(context, &stats);
    robo_layout_context_destroy(context);
    if (out_stats) {
        *out_stats = stats;
    }
    if (out_result) {
        *out_result = status;
    }
}

/** The default: the viewport every case gets unless it is testing a wrap. */
#define LAYOUT_INLINE_WIDE_VIEWPORT 800

static void layout_inline_pass(layout_inline_fixture_t *fixture, robo_layout_stats_t *out_stats)
{
    layout_inline_pass_with(fixture, LAYOUT_INLINE_WIDE_VIEWPORT, out_stats, NULL);
}

/** The box @p index of the tree, in creation order (which is document order). */
static robo_layout_node_id_t layout_inline_box_at(layout_inline_fixture_t *fixture, size_t index)
{
    robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

    ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture->tree, index, &id), ROBO_OK);
    return id;
}

/** The box built for the element @p tag. */
static robo_layout_node_id_t layout_inline_box_of(layout_inline_fixture_t *fixture,
                                                  const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&element), 1);
    return robo_layout_object_id_for_dom_node(fixture->tree, &element);
}

/** Assert one box's geometry, and that the geometry is *known*. */
static void layout_inline_expect_in(const robo_layout_tree_t *tree,
                                    robo_layout_node_id_t id,
                                    int32_t x,
                                    int32_t y,
                                    int32_t width,
                                    int32_t height)
{
    robo_layout_result_t result;
    robo_layout_point_t position;
    robo_layout_size_t size;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.x, x);
    ROBO_CHECK_EQ_INT(position.y, y);
    ROBO_CHECK_EQ_INT(size.width, width);
    ROBO_CHECK_EQ_INT(size.height, height);
    ROBO_CHECK_EQ_INT(result.flags & ROBO_LAYOUT_RESULT_FLAG_ALL, 0);
}

/** The same, for a case that holds a raw tree rather than a fixture -- the real
 *  CSS pipeline, whose boxes are the bridge's and whose styles are the cascade's. */
static void layout_inline_expect(layout_inline_fixture_t *fixture,
                                 robo_layout_node_id_t id,
                                 int32_t x,
                                 int32_t y,
                                 int32_t width,
                                 int32_t height)
{
    layout_inline_expect_in(fixture->tree, id, x, y, width, height);
}

static void layout_inline_expect_unresolved(layout_inline_fixture_t *fixture,
                                            robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_size_t size;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_UNRESOLVED);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_NOT_FOUND);
}

/*
 * The shape every case below shares: `<html><body><p>` where the container that
 * owns the inline formatting context is the `<p>`, and everything inside it is the
 * test's subject. Naming it once keeps each case's markup about its own subject.
 */
#define LAYOUT_INLINE_BLOCK_RULES                          \
    { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0, 0, 0.0 },    \
    { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0, 0, 0.0 },    \
    { "p", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0, 0, 0.0 }

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void test_inline_stats(void)
{
    robo_layout_inline_stats_t a;
    robo_layout_inline_stats_t b;

    layout_fixture();

    robo_layout_inline_stats_init(&a);
    ROBO_CHECK_EQ_INT(a.contexts, 0u);
    ROBO_CHECK_EQ_INT(a.empty_contexts, 0u);
    ROBO_CHECK_EQ_INT(a.participants_placed, 0u);
    ROBO_CHECK_EQ_INT(a.atomic_placed, 0u);
    ROBO_CHECK_EQ_INT(a.non_atomic_placed, 0u);
    ROBO_CHECK_EQ_INT(a.block_level_declined, 0u);
    ROBO_CHECK_EQ_INT(a.lines, 0u);
    ROBO_CHECK_EQ_INT(a.wraps, 0u);
    ROBO_CHECK_EQ_INT(a.widths_declared, 0u);
    ROBO_CHECK_EQ_INT(a.widths_filled, 0u);
    ROBO_CHECK_EQ_INT(a.heights_declared, 0u);
    ROBO_CHECK_EQ_INT(a.heights_from_content, 0u);
    ROBO_CHECK_EQ_INT(a.depth_max, 0u);

    b = a;
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(&a, &b), 1);

    /* Every field is part of the comparison, and the ones below are the fields a
     * hand-written equality is most likely to forget -- the last one added, and the
     * one whose name sits closest to another's. */
    b = a;
    b.wraps = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(&a, &b), 0);
    b = a;
    b.widths_filled = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(&a, &b), 0);
    b = a;
    b.block_level_declined = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(&a, &b), 0);
    b = a;
    b.depth_max = 3u;
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(&a, &b), 0);
    b = a;
    b.empty_contexts = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(&a, &b), 0);

    /* Safe on NULL, both ways round, and NULL is only equal to NULL. */
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(NULL, &a), 0);
    robo_layout_inline_stats_init(NULL);
}

/**
 * The display predicate this module deliberately does *not* own.
 *
 * The pass reads "is this inline-level?" from 9.12's vocabulary table, exactly as
 * 10.3 reads "is this block-level?" from the same table, and the *second* half of
 * the atomic question -- did this box establish a context of its own? -- comes from
 * 10.2's classifier. So the table asserted here is the one the phase reads, and the
 * rows that matter are the two inline-level ones, because they are the two that
 * end up on a line and they are not the same kind of thing:
 *
 *   `inline`        inline-level, establishes nothing  -> transparent to the line
 *   `inline-block`  inline-level, inner `flow-root`    -> atomic on the line
 *
 * A build that read only the outer display would treat both as one kind and get one
 * of them wrong in a way that still produces plausible numbers.
 */
static void test_inline_display_table(void)
{
    layout_fixture();

    ROBO_CHECK_EQ_INT(robo_css_display_is_inline_level(ROBO_CSS_DISPLAY_INLINE), 1);
    ROBO_CHECK_EQ_INT(robo_css_display_is_inline_level(ROBO_CSS_DISPLAY_INLINE_BLOCK), 1);
    ROBO_CHECK_EQ_INT(robo_css_display_is_inline_level(ROBO_CSS_DISPLAY_BLOCK), 0);
    ROBO_CHECK_EQ_INT(robo_css_display_is_inline_level(ROBO_CSS_DISPLAY_NONE), 0);

    ROBO_CHECK_EQ_INT(robo_css_display_inner(ROBO_CSS_DISPLAY_INLINE),
                      ROBO_CSS_DISPLAY_INNER_FLOW);
    ROBO_CHECK_EQ_INT(robo_css_display_inner(ROBO_CSS_DISPLAY_INLINE_BLOCK),
                      ROBO_CSS_DISPLAY_INNER_FLOW_ROOT);

    /*
     * And 10.2's classification says the same thing about the same two values, from
     * the other side: a plain block container with nothing block-level in it
     * establishes an INLINE context, and an `inline-block` establishes one of its
     * own whatever it holds. Those are the two facts the verdict in the pass is
     * built from.
     */
    ROBO_CHECK_EQ_INT(robo_layout_fc_establishes(ROBO_CSS_DISPLAY_BLOCK, 0, 0),
                      ROBO_LAYOUT_FC_INLINE);
    ROBO_CHECK_EQ_INT(robo_layout_fc_establishes(ROBO_CSS_DISPLAY_INLINE_BLOCK, 0, 0),
                      ROBO_LAYOUT_FC_BLOCK);
    ROBO_CHECK_EQ_INT(robo_layout_fc_establishes(ROBO_CSS_DISPLAY_INLINE, 0, 0),
                      ROBO_LAYOUT_FC_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_fc_display_is_block_container(ROBO_CSS_DISPLAY_INLINE), 0);
    ROBO_CHECK_EQ_INT(robo_layout_fc_display_is_block_container(ROBO_CSS_DISPLAY_INLINE_BLOCK),
                      1);
}

/**
 * A document with no inline participant at all still visits its contexts.
 *
 * `<p></p>` is a block container with no block-level child, so 10.2 makes it
 * establish an inline formatting context and 10.4 lays that context out -- and the
 * context is *empty*. Both halves are asserted because both are conclusions: the
 * context exists (so the phase really is driven by boxes), and it placed nothing
 * (so an empty context is a counted answer rather than a silent one).
 */
static void test_inline_empty_context(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture, "<html><body><p></p></body></html>", rules, 3u);
    layout_inline_pass(&fixture, &stats);

    ROBO_CHECK_EQ_INT(stats.inline_flow.contexts, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.empty_contexts, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.depth_max, 0u);

    /* `<p>` still has a geometry -- it is a block, and 10.3 places it. Its extent is
     * the lines' extent, which is zero: a block container with no measurable content
     * is a zero-height block, which is 10.3's answer restated where an inline context
     * now has a say in it. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 0);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed + stats.block.blocks_placed,
                      stats.boxes_computed);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 3u); /* html, body, p */

    layout_inline_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Placement                                                           */
/* ------------------------------------------------------------------ */

static void test_inline_single_atomic(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        /* An atomic inline: laid out along a line, sized by its declarations. */
        { "span", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 120.0, 1, 40.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture, "<html><body><p><span></span></p></body></html>", rules, 4u);
    layout_inline_pass(&fixture, &stats);

    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "span"), 0, 0, 120, 40);
    /* The container's own extent is the line's: one line, forty pixels tall. That
     * number is the whole reason the inline direction runs *inside* the block
     * recursion (RoboLayoutBlock.h) -- nothing else could hand it back. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 40);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "body"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 40);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "html"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 40);

    ROBO_CHECK_EQ_INT(stats.inline_flow.contexts, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.empty_contexts, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.atomic_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.non_atomic_placed, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.widths_declared, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.widths_filled, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.heights_declared, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.depth_max, 3u); /* the span's own depth */

    layout_inline_end(&fixture);
}

/**
 * Sequential placement: the cursor is the only thing that carries between siblings.
 *
 * The assertion the header calls the cursor's whole job — `B.x == A.x + A.width` —
 * checked on the third box as well, so a build that reset the cursor after the
 * second one would be caught rather than passing on a two-box document.
 */
static void test_inline_siblings(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 20.0 },
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 150.0, 1, 20.0 },
        { "em", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 50.0, 1, 20.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture,
                        "<html><body><p><b></b><i></i><em></em></p></body></html>", rules, 6u);
    layout_inline_pass(&fixture, &stats);

    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 0, 100, 20);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "i"), 100, 0, 150, 20);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "em"), 250, 0, 50, 20);
    /* And the container's extent is one line tall, not the sum of the three boxes'
     * heights: a line's height is the tallest thing on it. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 20);

    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 3u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.atomic_placed, 3u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.widths_declared, 3u);

    layout_inline_end(&fixture);
}

/**
 * A non-atomic inline box is the **union** of what its contents reached.
 *
 * This is the other half of the atomic/non-atomic distinction, and the case is
 * written so that the union cannot be confused with any other number: the box holds
 * two atomics of different widths and heights, so its width is their sum, its height
 * is the taller of the two, and its position is where the first of them landed.
 * A build that treated the box as a unit would have to invent a width for it (it
 * declares none, and CSS does not let it), and a build that treated it as
 * transparent but never wrote its geometry would leave it UNRESOLVED.
 */
static void test_inline_non_atomic_union(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0, 0.0, 0, 0.0 },
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 70.0, 1, 30.0 },
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 40.0, 1, 55.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture, "<html><body><p><span><b></b><i></i></span></p></body></html>",
                        rules, 6u);
    layout_inline_pass(&fixture, &stats);

    /* The transparent box's two children are participants of the *same* context --
     * `<p>`'s -- so they carry the cursor forward just as if they were `<p>`'s own
     * children. That is what "transparent to the line" means, and the two positions
     * are what it looks like.
     *
     * Their *verticals* differ, and 10.7 is why: both boxes take the bottom-edge
     * fallback, so both rest their feet on the line's floor -- 55 below the line's top,
     * because the taller of the two set the ascent -- and the 30-tall box therefore sits
     * at y = 25 rather than at the line's top. The x values are still the cursor's.
     */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 25, 70, 30);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "i"), 70, 0, 40, 55);

    /* The box itself: from the first child's left edge to the second's right edge,
     * and from the line top to the taller child's bottom. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "span"), 0, 0, 110, 55);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 55);

    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 3u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.atomic_placed, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.non_atomic_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    /* The transparent box declares nothing and is sized by nothing: no width, no
     * height, and therefore neither counter moved for it. */
    ROBO_CHECK_EQ_INT(stats.inline_flow.widths_declared, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.heights_declared, 2u);

    layout_inline_end(&fixture);
}

/**
 * A run nobody measured has no extent, and that is the honest answer.
 *
 * A paragraph holding text has a height in a browser, and this pass cannot know it
 * *here*: 10.5 made a text node measurable, but measurement is a **seam** and this
 * fixture installs no metrics source -- so the run is placed, it is a participant of
 * the line, and it measures zero. The case asserts the zero *and* the counter that
 * says why (`unmeasured`), rather than leaving a reader to guess whether the zero is
 * a placeholder: a pass given no font and a pass given a font that measures nothing
 * would otherwise look identical. The alternative — inventing a plausible width for
 * text nobody measured — is the one failure a later phase could not detect.
 *
 * Two empty spans are placed at the same cursor as the run, which is the second half
 * of the claim: a zero-width participant advances the cursor by nothing.
 */
static void test_inline_textless(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0, 0.0, 0, 0.0 },
        { "b", ROBO_CSS_DISPLAY_INLINE, 0, 0.0, 0, 0.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture,
                        "<html><body><p><span></span><b></b>text</p></body></html>", rules, 5u);
    layout_inline_pass(&fixture, &stats);

    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "span"), 0, 0, 0, 0);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 0, 0, 0);
    /* Not one pixel of extent, from either box or from the run after them: the line's
     * height is zero and the container is a zero-height block -- which is exactly what
     * 10.3 concluded about `<p>Hello</p>` before 10.5 existed, and is now a statement
     * about an *unmeasured* run rather than about a node that generated nothing. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 0);

    /* Three participants: the two empty spans and the text run, which is a
     * participant and *not* a box -- so `non_atomic_placed`, the box counter, stays
     * at two while `participants_placed` moves. */
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 3u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.non_atomic_placed, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.empty_contexts, 0u);
    /* And the reason the run is zero wide is recorded rather than implied. */
    ROBO_CHECK_EQ_INT(stats.text.runs_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.text.measured, 0u);
    ROBO_CHECK_EQ_INT(stats.text.unmeasured, 1u);

    layout_inline_end(&fixture);
}

/**
 * An atomic inline with no declared width fills its containing block.
 *
 * The rule is CSS's `width: auto`, read the same way 10.3 reads it for a block box,
 * and the honest reason it is a *fill* rather than a shrink-to-fit: shrinking to fit
 * is intrinsic sizing, which this build cannot do, and the header says so where the
 * counter is declared. It has a visible consequence worth asserting — the box takes
 * the whole line, so the next participant starts a new one.
 */
static void test_inline_atomic_fill(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE_BLOCK, 0, 0.0, 1, 12.0 },
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 30.0, 1, 12.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture, "<html><body><p><span></span><b></b></p></body></html>",
                        rules, 5u);
    layout_inline_pass(&fixture, &stats);

    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "span"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 12);
    /* The fill took the line, so `<b>` wraps to the next one — the fill rule's own
     * consequence, and the reason the counter is worth having: a build that guessed
     * a width here would have put `<b>` beside it. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 12, 30, 12);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 24);

    ROBO_CHECK_EQ_INT(stats.inline_flow.widths_filled, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.widths_declared, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 2u);

    layout_inline_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Wrapping                                                            */
/* ------------------------------------------------------------------ */

static void test_inline_wrapping(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 30.0 },
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 50.0 },
        { "em", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 20.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture,
                        "<html><body><p><b></b><i></i><em></em></p></body></html>", rules, 6u);
    /* 250 wide: two hundred-pixel boxes fit, the third does not. The line width is
     * the viewport's because a block box fills its containing block (10.3), which is
     * what makes a viewport the way to ask for a wrap. */
    layout_inline_pass_with(&fixture, 250, &stats, NULL);

    /* Both rest on the first line's floor, which the 50-tall box sets: so the 30-tall
     * one sits 20 down from the top and the 50-tall one starts there. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 20, 100, 30);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "i"), 100, 0, 100, 50);
    /* The third box wrapped, and the line it left was 50 tall -- the tallest thing
     * on it -- so it sits at y = 50 rather than at 30 or 40. The *line height* is
     * the number a build that advanced by the last participant's height would get
     * wrong, which is why the middle box is the tall one. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "em"), 0, 50, 100, 20);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0, 250, 70);

    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 3u);

    layout_inline_end(&fixture);
}

/**
 * A participant wider than its line is placed anyway, and on its *own* line.
 *
 * CSS's rule, and the one a build implementing "wrap when it does not fit" without
 * the `cursor > 0` guard gets wrong in the worst possible way: a box wider than the
 * containing block would start a new line forever, or be dropped. Here it overflows,
 * which is a fact 10.13 owns and this pass reports in no flag at all.
 */
static void test_inline_overflow_does_not_wrap(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 400.0, 1, 10.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture, "<html><body><p><b></b></p></body></html>", rules, 4u);
    layout_inline_pass_with(&fixture, 100, &stats, NULL);

    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 0, 400, 10);
    /* Placed once, on the first line, with no wrap at all -- and the flags stay 0,
     * because "this box overflows" is 10.13's fact. */
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 1u);

    layout_inline_end(&fixture);
}

/**
 * A transparent box is not a unit, so it does not wrap as one.
 *
 * The second half of the wrapping rule, and the place the two kinds of inline box
 * part company most visibly: a `span`'s width is not known before it is placed, so
 * the line cannot decide whether it fits. Its *contents* wrap instead, one
 * participant at a time, and the span's own rectangle becomes the union of the
 * fragments it produced — which is a wider and taller box than any single line.
 */
static void test_inline_transparent_wraps_inside(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0, 0.0, 0, 0.0 },
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 30.0 },
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 30.0 },
        { "em", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 20.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture,
                        "<html><body><p><span><b></b><i></i><em></em></span></p></body></html>",
                        rules, 7u);
    layout_inline_pass_with(&fixture, 250, &stats, NULL);

    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 0, 100, 30);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "i"), 100, 0, 100, 30);
    /* The third wrapped *inside the span*, at x = 0 of the second line -- and the
     * span's own left edge is the first line's zero, so the fragment at x = 0 is
     * inside the union rather than beside it. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "em"), 0, 30, 100, 20);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "span"), 0, 0, 200, 50);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0, 250, 50);

    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.non_atomic_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.atomic_placed, 3u);

    layout_inline_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Nesting                                                             */
/* ------------------------------------------------------------------ */

/**
 * A participant inside a transparent box is positioned in **that box's** space.
 *
 * The coordinate convention every result in this module is stated in
 * (RoboLayoutResult.h) -- a position is in the box's containing block's space -- and
 * for an inline participant the containing block is its *parent*, which for a box
 * inside a `span` is the span and not the context's owner. So the pass rebases:
 * participants are placed in the line's space while the context runs, and moved into
 * their parent's space once the parent's origin is known.
 *
 * The case is built so that the rebase is visible rather than vacuous: the atomic
 * inside the span sits at x = 60 in the line, so its position relative to the span
 * is zero and its position relative to `<p>` is 60. A build with no rebase would
 * answer 60 for both, and a build that never placed the span would leave it
 * UNRESOLVED.
 */
static void test_inline_nested_rebase(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 60.0, 1, 25.0 },
        { "span", ROBO_CSS_DISPLAY_INLINE, 0, 0.0, 0, 0.0 },
        { "em", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 45.0, 1, 25.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture, "<html><body><p><b></b><span><em></em></span></p></body></html>",
                        rules, 6u);
    layout_inline_pass(&fixture, &stats);

    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 0, 60, 25);
    /* In the line, `<em>` is at x = 60. Relative to its parent, the span, it is at
     * zero -- and the span itself is where the cursor was when it began. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "span"), 60, 0, 45, 25);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "em"), 0, 0, 45, 25);

    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 3u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.non_atomic_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.atomic_placed, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.depth_max, 4u); /* html, body, p, span/em */

    layout_inline_end(&fixture);
}

/**
 * Transparency reaches through more than one box.
 *
 * `span` inside `span` inside the container: neither establishes anything, so both
 * are participants of `<p>`'s context and both are written as unions — and the
 * innermost box's position is relative to the box that directly contains it, which
 * is the inner span, not the outer one. A build that flattened the chain would still
 * get the innermost *line* position right and the two parents wrong.
 */
static void test_inline_nested_two_levels(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 80.0, 1, 20.0 },
        { "span", ROBO_CSS_DISPLAY_INLINE, 0, 0.0, 0, 0.0 },
        { "i", ROBO_CSS_DISPLAY_INLINE, 0, 0.0, 0, 0.0 },
        { "em", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 35.0, 1, 20.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture,
                        "<html><body><p><b></b><span><i><em></em></i></span></p></body></html>",
                        rules, 7u);
    layout_inline_pass(&fixture, &stats);

    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 0, 80, 20);
    /* All three transparent-or-inside boxes start where `<em>` starts, so each is at
     * zero relative to the one above it -- which is the assertion: the rebase is
     * applied once per level, not once per context. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "em"), 0, 0, 35, 20);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "i"), 0, 0, 35, 20);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "span"), 80, 0, 35, 20);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 20);

    ROBO_CHECK_EQ_INT(stats.inline_flow.non_atomic_placed, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.atomic_placed, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 4u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.depth_max, 5u);

    layout_inline_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The gate: what the inline direction declines                        */
/* ------------------------------------------------------------------ */

/**
 * An inline-level box in a **block-direction** context is declined, not misplaced.
 *
 * `<div>` has a block-level child, so 10.2 makes it transparent and its contents
 * belong to the root's block-direction context. The `<span>` between its two
 * children is therefore inline-level with no line anywhere to put it on — the mixed
 * content case CSS solves with an anonymous block box, which this build makes none
 * of. It is counted by 10.3 and left UNRESOLVED, and the inline pass never sees it
 * at all, which is why `contexts` counts only the contexts this phase laid out.
 */
static void test_inline_mixed_declined(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "div", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0, 0, 0.0 },
        { "span", ROBO_CSS_DISPLAY_INLINE, 0, 0.0, 0, 0.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture, "<html><body><p></p><div><p></p><span></span></div></body></html>",
                        rules, 5u);
    layout_inline_pass(&fixture, &stats);

    layout_inline_expect_unresolved(&fixture, layout_inline_box_of(&fixture, "span"));
    /* Two contexts were laid out -- the two `<p>`s, each of which establishes an
     * inline one -- and both are empty, because a `<p>` with no element child has no
     * participant. The span is in neither of them. */
    ROBO_CHECK_EQ_INT(stats.inline_flow.contexts, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.empty_contexts, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 0u);

    layout_inline_end(&fixture);
}

/**
 * A **block-level** box inside an inline box is the mirror case, and the anonymous
 * box exists for it too.
 *
 * The container establishes an inline context (nothing block-level is its *direct*
 * child), the `<span>` inside it is transparent so the `<section>` participates in
 * that inline context — and a block-level box cannot be a participant of a line.
 * Declining it is the same answer as the case above, and it is counted in the inline
 * record where the first case is counted by 10.3: the counter that moves says which
 * direction found it.
 *
 * The structure is *this* one rather than a `<div>` inside a `<p>`, and that is a
 * fact about RoboHTML rather than about layout: the tree builder implements HTML's
 * rule that a block-level start tag closes an open paragraph, so
 * `<p><span><div></div></span></p>` parses as a `<p>` holding the span followed by a
 * *sibling* `<div>` — the nesting this case needs would never exist. A container
 * element with no paragraph on the stack gives it, and the `<head>` the parser
 * synthesizes is still the one box the block direction declines.
 *
 * The accounting identity closes over both directions here — three blocks, one
 * declined inline participant, one declined block — and it is asserted rather than
 * eyeballed, because a build that dropped a box would have four right-looking
 * numbers and a document that did not add up.
 */
static void test_inline_block_level_declined(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0, 0, 0.0 },
        { "div", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0, 0, 0.0 },
        { "span", ROBO_CSS_DISPLAY_INLINE, 0, 0.0, 0, 0.0 },
        { "section", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0, 0, 0.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture, "<html><body><div><span><section></section></span></div></body></html>",
                        rules, 5u);
    layout_inline_pass(&fixture, &stats);

    layout_inline_expect_unresolved(&fixture, layout_inline_box_of(&fixture, "section"));
    /* The span *is* placed: it is a participant of the line, it just has nothing in
     * it that could be measured. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "span"), 0, 0, 0, 0);

    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.block_level_declined, 1u);
    ROBO_CHECK_EQ_INT(stats.block.inline_level_skipped, 1u); /* the parser's `<head>` */
    ROBO_CHECK_EQ_INT(stats.block.unreached, 1u);            /* the `<section>` */
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 3u);        /* html, body, div */

    /* The identity, asserted as an identity: every box is placed, declined for a
     * stated reason by one of the two directions, or unreached. */
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed + stats.block.inline_level_skipped +
                          stats.inline_flow.participants_placed + stats.block.unreached,
                      stats.objects);

    layout_inline_end(&fixture);
}

/**
 * An atomic inline's contents are a **block** formatting context, in the box's own
 * space.
 *
 * 10.2's rule, and the reason an `inline-block` is atomic at all: its inner display
 * is `flow-root`, so what is inside it stacks. The pass hands those contents to block
 * layout with the box's own width as their containing block, which is why the `<p>`
 * inside comes out exactly as wide as the span and positioned relative to it.
 *
 * The span's *height* is then the extent its contents reached — the same rule 10.3
 * applies to a block box, applied by the inline pass because the span is a
 * participant of a line rather than of a block flow.
 */
static void test_inline_atomic_contents_are_blocks(void)
{
    layout_inline_fixture_t fixture;
    /*
     * Spelled out rather than composed from LAYOUT_INLINE_BLOCK_RULES, and the
     * reason is that the `<p>` here is the *subject*: the container macro already
     * declares one, with no height. Two rules for one tag is a fixture bug the
     * lookup would swallow (it answers with the first match), so the macro is not
     * used where it would mean declaring `p` twice.
     */
    static const layout_inline_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0, 0, 0.0 },
        { "span", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 300.0, 0, 0.0 },
        { "p", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0, 1, 25.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture, "<html><body><span><p></p></span></body></html>", rules, 4u);
    layout_inline_pass(&fixture, &stats);

    /* `<body>`'s only child is inline-level, so body establishes an inline context
     * and the span is placed on its line. */
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "span"), 0, 0, 300, 25);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0, 300, 25);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "body"), 0, 0,
                         LAYOUT_INLINE_WIDE_VIEWPORT, 25);

    ROBO_CHECK_EQ_INT(stats.inline_flow.atomic_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.heights_from_content, 1u);
    /* html, body, span, p: the span by the inline pass and the p by the block pass,
     * each counted once, which is the claim `boxes_computed` rests on. */
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 3u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 4u);

    layout_inline_end(&fixture);
}

/**
 * A `display: none` element is not in the tree at all, so it cannot be on a line.
 *
 * 10.1's rule, restated where a cursor now depends on it: the element generates no
 * box, so it neither advances the cursor nor contributes an extent — and the box
 * *after* it lands where it would have landed had the hidden element never been
 * written.
 */
static void test_inline_display_none(void)
{
    layout_inline_fixture_t fixture;
    /* `<u>` rather than a `<div>`, and the reason is RoboHTML's: a block-level start
     * tag closes an open paragraph, so a hidden `<div>` here would take the `<p>` down
     * with it and the case would be testing the parser rather than the cursor. */
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 20.0 },
        { "u", ROBO_CSS_DISPLAY_NONE, 0, 0.0, 0, 0.0 },
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 20.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture, "<html><body><p><b></b><u></u><i></i></p></body></html>",
                        rules, 6u);
    layout_inline_pass(&fixture, &stats);

    {
        robo_html_node_handle_t hidden = layout_document_find(&fixture.doc, "u");

        ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&hidden), 1);
        ROBO_CHECK_EQ_INT(robo_layout_object_id_for_dom_node(fixture.tree, &hidden),
                          ROBO_LAYOUT_NODE_ID_NONE);
    }

    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 0, 100, 20);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "i"), 100, 0, 100, 20);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 2u);

    layout_inline_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

static void test_inline_no_viewport(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 20.0 }
    };
    robo_layout_context_t *context = NULL;
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture, "<html><body><p><b></b></p></body></html>", rules, 4u);

    /*
     * The precondition, stated rather than silently satisfied: an inline formatting
     * context has no width to lay a line into without a containing block, and the
     * root's comes from the viewport. So a pass with no viewport classifies and
     * places nothing -- in *both* directions, which is what the two zero records say.
     */
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                      (int)ROBO_LAYOUT_PASS_WALKED);
    robo_layout_context_stats(context, &stats);
    /* Five boxes for four written tags: the tree builder inserts the `<head>` a
     * well-formed document requires, and nothing styles it (RoboLayoutTestSupport.h).
     * The number is the parser's, and it is asserted rather than derived so that the
     * two agree by being compared. */
    ROBO_CHECK_EQ_INT(stats.objects, 5u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 0u);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 5u);
    ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(&stats.block,
                                                    &(robo_layout_block_stats_t){0}),
                      1);
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(&stats.inline_flow,
                                                     &(robo_layout_inline_stats_t){0}),
                      1);
    /* The classification still happened: the pass is not half a pass, it is the part
     * of the work the input allowed. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 1);
    layout_inline_expect_unresolved(&fixture, layout_inline_box_of(&fixture, "b"));
    robo_layout_context_destroy(context);

    /* And the same tree, given a viewport, is laid out -- which is what makes the
     * zeros above a statement about the *input* rather than about the build. */
    layout_inline_pass(&fixture, &stats);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 4u);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 0, 100, 20);

    layout_inline_end(&fixture);
}

/**
 * The same input produces the same lines, twice running and in two trees.
 *
 * Two passes on **one** context, because a helper that made a fresh context per call
 * would prove the geometry is reproducible and say nothing about whether a second
 * pass stacks on the first: the failure a cumulative pass has is a cursor that never
 * resets, which would put the second pass's participants beside the first's. Then a
 * second *tree* from the same document, which is determinism of the algorithm rather
 * than of the build.
 */
static void test_inline_determinism(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 30.0 },
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 50.0 },
        { "em", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 20.0 },
        { "span", ROBO_CSS_DISPLAY_INLINE, 0, 0.0, 0, 0.0 }
    };
    static const char *const html =
        "<html><body><p><b></b><span><i></i></span><em></em></p></body></html>";
    robo_layout_stats_t first;
    robo_layout_stats_t second;
    robo_layout_result_t before[8];
    size_t count;
    size_t index;

    layout_fixture();
    layout_inline_begin(&fixture, html, rules, 7u);
    count = robo_layout_tree_object_count(fixture.tree);
    ROBO_CHECK(count <= 8u);

    {
        robo_layout_context_t *context = NULL;
        robo_layout_size_t viewport;

        viewport.width = 250;
        viewport.height = 600;
        ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
        robo_layout_context_stats(context, &first);
        for (index = 0u; index < count; index++) {
            robo_layout_node_id_t id = layout_inline_box_at(&fixture, index);

            ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &before[index]),
                              ROBO_OK);
        }

        ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
        robo_layout_context_stats(context, &second);
        robo_layout_context_destroy(context);
    }
    ROBO_CHECK_EQ_INT(first.passes, 1u);
    ROBO_CHECK_EQ_INT(second.passes, 2u);
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(&first.inline_flow, &second.inline_flow), 1);
    ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(&first.block, &second.block), 1);
    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id = layout_inline_box_at(&fixture, index);
        robo_layout_result_t now;

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &now), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_result_equal(&before[index], &now), 1);
    }

    {
        layout_inline_fixture_t twin;
        robo_layout_stats_t twin_stats;

        memset(&twin, 0, sizeof(twin));
        layout_document_build(&twin.doc, html);
        twin.styles = fixture.styles;
        ROBO_CHECK_EQ_INT(robo_layout_tree_create(&twin.tree, NULL), ROBO_OK);
        {
            robo_layout_build_options_t options;

            robo_layout_build_options_init(&options);
            options.styles = &twin.styles;
            ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(
                                  twin.tree, twin.doc.id, &options, NULL, NULL),
                              ROBO_OK);
        }
        layout_inline_pass_with(&twin, 250, &twin_stats, NULL);
        ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(&first.inline_flow,
                                                         &twin_stats.inline_flow),
                          1);
        ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(twin.tree), count);
        for (index = 0u; index < count; index++) {
            robo_layout_node_id_t id = layout_inline_box_at(&twin, index);
            robo_layout_result_t now;

            ROBO_CHECK_EQ_INT(robo_layout_object_result(twin.tree, id, &now), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_layout_result_equal(&before[index], &now), 1);
        }
        robo_layout_tree_destroy(twin.tree);
        layout_document_release(&twin.doc);
    }

    layout_inline_end(&fixture);
}

/** Releasing the classification releases the lines derived from it. */
static void test_inline_invalidation(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 30.0 }
    };
    robo_layout_stats_t stats;
    size_t index;

    layout_fixture();
    layout_inline_begin(&fixture, "<html><body><p><b></b></p></body></html>", rules, 4u);
    layout_inline_pass(&fixture, &stats);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 0, 100, 30);

    robo_layout_tree_invalidate_contexts(fixture.tree);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 0);
    for (index = 0u; index < 4u; index++) {
        layout_inline_expect_unresolved(&fixture, layout_inline_box_at(&fixture, index));
    }

    /* And re-running recalculates, so the release is not destructive. */
    layout_inline_pass(&fixture, &stats);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "b"), 0, 0, 100, 30);

    layout_inline_end(&fixture);
}

/**
 * The available width is an input, and changing it changes the lines.
 *
 * The same tree, the same styles, one different number: 800 wide puts three boxes on
 * one line, 250 wide wraps the third. This is the case that proves the line width is
 * *read* from the containing block rather than fixed at build time -- and it is the
 * reason the inline direction cannot be its own pass over a finished tree, since the
 * wrap changes every ancestor's height.
 */
static void test_inline_viewport_changes_layout(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 30.0 },
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 30.0 },
        { "em", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 30.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_inline_begin(&fixture,
                        "<html><body><p><b></b><i></i><em></em></p></body></html>", rules, 6u);

    layout_inline_pass_with(&fixture, 800, &stats, NULL);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "em"), 200, 0, 100, 30);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0, 800, 30);
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);

    layout_inline_pass_with(&fixture, 250, &stats, NULL);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "em"), 0, 30, 100, 30);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0, 250, 60);
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 2u);

    /* And back again: the pass replaces geometry rather than refining it, so the
     * wide answer comes back whole rather than leaving the wrapped one behind. */
    layout_inline_pass_with(&fixture, 800, &stats, NULL);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "em"), 200, 0, 100, 30);
    layout_inline_expect(&fixture, layout_inline_box_of(&fixture, "p"), 0, 0, 800, 30);

    layout_inline_end(&fixture);
}

/**
 * The numbers came from CSS.
 *
 * A real stylesheet through the real cascade: three `inline-block` boxes with
 * declared widths inside a `<div>`, which establishes an inline formatting context
 * because 10.2's rule found nothing block-level in it. The declarations that produce
 * a line are `display` and `width` -- both of which the *cascade* resolved, neither
 * of which this module parses -- and the assertion that the three boxes land at
 * 0/100/250 is the whole pipeline, from a selector to a cursor.
 */
static void test_inline_pipeline(void)
{
    layout_document_t doc;
    layout_css_fixture_t css;
    robo_layout_dom_t *dom = NULL;
    robo_layout_tree_t *tree = NULL;
    robo_layout_style_source_t source;
    robo_layout_build_options_t options;
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;
    robo_layout_result_t result;
    robo_layout_point_t position;
    robo_layout_size_t size;
    robo_layout_node_id_t box;
    robo_html_node_handle_t element;

    layout_fixture();
    layout_document_build(&doc,
                          "<html>\n"
                          "  <body>\n"
                          "    <div><span class=\"a\"></span><span class=\"b\"></span>"
                          "<span class=\"c\"></span></div>\n"
                          "  </body>\n"
                          "</html>\n");
    layout_css_begin(&css);
    layout_css_sheet(&css,
                     "html, body, div { display: block }\n"
                     "span { display: inline-block }\n"
                     ".a { width: 100px; height: 24px }\n"
                     ".b { width: 150px; height: 24px }\n"
                     ".c { width: 60px; height: 24px }\n");
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(css.context, doc.id, &dom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(dom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(dom, &source, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &source;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, &options, NULL, NULL),
                      ROBO_OK);

    /* html, head, body, div, three spans. `<head>` is the parser's and nothing
     * styles it, so it is an inline box the block direction declines. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), 7u);

    viewport.width = LAYOUT_INLINE_WIDE_VIEWPORT;
    viewport.height = 600;
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                      (int)ROBO_LAYOUT_PASS_WALKED);
    robo_layout_context_stats(context, &stats);

    element = layout_document_find(&doc, "span");
    box = robo_layout_object_id_for_dom_node(tree, &element);
    ROBO_CHECK(box != ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_object_result(tree, box, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.x, 0);
    ROBO_CHECK_EQ_INT(position.y, 0);
    ROBO_CHECK_EQ_INT(size.width, 100);
    ROBO_CHECK_EQ_INT(size.height, 24);

    /* The three spans, in document order, by their three declared widths -- which is
     * the cursor arithmetic done on values the cascade produced. The boxes are the
     * tree's 5th, 6th and 7th, and they are taken by *index* because
     * `layout_document_find` deliberately answers with the first element of a tag and
     * the three spans share one. */
    {
        static const int32_t expected_x[] = { 0, 100, 250 };
        static const int32_t expected_width[] = { 100, 150, 60 };
        size_t i;

        for (i = 0u; i < 3u; i++) {
            robo_layout_node_id_t span_box = ROBO_LAYOUT_NODE_ID_NONE;

            ROBO_CHECK_EQ_INT(robo_layout_object_id_at(tree, 4u + i, &span_box), ROBO_OK);
            layout_inline_expect_in(tree, span_box, expected_x[i], 0, expected_width[i], 24);
        }
    }
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 3u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.atomic_placed, 3u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.widths_declared, 3u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 0u);

    /* `boxes_computed` is the two placement counts added together, asserted on a
     * document whose box count the *parser* decided. */
    ROBO_CHECK_EQ_INT(stats.boxes_computed,
                      stats.block.blocks_placed + stats.inline_flow.participants_placed);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 6u);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 1u);

    robo_layout_context_destroy(context);
    robo_layout_tree_destroy(tree);
    robo_layout_dom_destroy(dom);
    layout_css_end(&css);
    layout_document_release(&doc);
}

static void test_inline_allocator_balance(void)
{
    layout_inline_fixture_t fixture;
    static const layout_inline_rule_t rules[] = {
        LAYOUT_INLINE_BLOCK_RULES,
        { "b", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 100.0, 1, 30.0 },
        { "span", ROBO_CSS_DISPLAY_INLINE, 0, 0.0, 0, 0.0 }
    };
    size_t before;
    int pass;

    layout_fixture();
    before = layout_live_blocks();

    for (pass = 0; pass < 8; pass++) {
        robo_layout_stats_t stats;

        layout_inline_begin(&fixture, "<html><body><p><span><b></b></span><b></b></p></body></html>",
                            rules, 5u);
        layout_inline_pass(&fixture, &stats);
        layout_inline_pass(&fixture, &stats);
        layout_inline_end(&fixture);
    }

    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout_inline(void);

void robo_test_register_layout_inline(void)
{
    robo_test_register("layout.inline.stats", test_inline_stats);
    robo_test_register("layout.inline.display_table", test_inline_display_table);
    robo_test_register("layout.inline.empty_context", test_inline_empty_context);
    robo_test_register("layout.inline.single_atomic", test_inline_single_atomic);
    robo_test_register("layout.inline.siblings", test_inline_siblings);
    robo_test_register("layout.inline.non_atomic_union", test_inline_non_atomic_union);
    robo_test_register("layout.inline.textless", test_inline_textless);
    robo_test_register("layout.inline.atomic_fill", test_inline_atomic_fill);
    robo_test_register("layout.inline.wrapping", test_inline_wrapping);
    robo_test_register("layout.inline.overflow_does_not_wrap",
                       test_inline_overflow_does_not_wrap);
    robo_test_register("layout.inline.transparent_wraps_inside",
                       test_inline_transparent_wraps_inside);
    robo_test_register("layout.inline.nested_rebase", test_inline_nested_rebase);
    robo_test_register("layout.inline.nested_two_levels", test_inline_nested_two_levels);
    robo_test_register("layout.inline.mixed_declined", test_inline_mixed_declined);
    robo_test_register("layout.inline.block_level_declined", test_inline_block_level_declined);
    robo_test_register("layout.inline.atomic_contents_are_blocks",
                       test_inline_atomic_contents_are_blocks);
    robo_test_register("layout.inline.display_none", test_inline_display_none);
    robo_test_register("layout.inline.no_viewport", test_inline_no_viewport);
    robo_test_register("layout.inline.determinism", test_inline_determinism);
    robo_test_register("layout.inline.invalidation", test_inline_invalidation);
    robo_test_register("layout.inline.viewport_changes_layout",
                       test_inline_viewport_changes_layout);
    robo_test_register("layout.inline.pipeline", test_inline_pipeline);
    robo_test_register("layout.inline.allocator_balance", test_inline_allocator_balance);
}
