/*
 * RoboLayout block-layout suite (Phase 10.3) — layout.block.*
 *
 * The first suite in this directory that asserts a **number**, and the shape of it
 * is determined by that: while the module computed nothing, a test could only ask
 * whether a box had geometry at all, and asking *which* geometry was impossible.
 * Now the interesting assertions are arithmetic, and they are asserted exactly
 * rather than as "greater than zero", because a block algorithm's whole output is
 * arithmetic over a document.
 *
 * Five groups, in the order the phase is built:
 *
 *   the vocabulary   the predicate this module deliberately does *not* own, and the
 *                    stats record
 *
 *   placement        one box, three siblings, nesting -- the containing-block
 *                    convention, which is the one thing a wrong build gets wrong in
 *                    a way that still produces plausible numbers
 *
 *   heights          declared, `auto`, percentage, and a declared height that is
 *                    smaller than the content it holds
 *
 *   the gate         what block layout declines to place, and why -- inline-level
 *                    boxes in a *block-direction* context (from 10.4 they are
 *                    placed when they are in an inline one), `display: none`, and
 *                    the invariant that a block-level box is never in a non-block
 *                    context
 *
 *   the pass         determinism, invalidation, the real CSS pipeline, and the
 *                    allocator
 *
 * The fixtures are *scripted* styles rather than a stylesheet, except in
 * `layout.block.pipeline`: a case whose subject is arithmetic should fail because
 * the arithmetic is wrong and not because a selector did not match, and a case
 * whose subject is "the numbers came from CSS" should go through CSS.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A scripted style source with heights                                */
/* ------------------------------------------------------------------ */

/** One scripted answer: this tag computes to this display, and this height. */
typedef struct layout_block_rule {
    const char *tag;
    robo_css_display_t display;
    /** Non-zero when this element declares `height: <height_px>px`. */
    int has_height;
    double height_px;
} layout_block_rule_t;

typedef struct layout_block_style {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
} layout_block_style_t;

#define LAYOUT_BLOCK_MAX_RULES 12u

typedef struct layout_block_source {
    layout_block_style_t rules[LAYOUT_BLOCK_MAX_RULES];
    size_t count;
} layout_block_source_t;

/**
 * A real computed style carrying a real DISPLAY and, when asked for, a real
 * LENGTH.
 *
 * Built through 9.8's own maker and container rather than a hand-written struct,
 * for the reason the 10.1 fixture gives for the same thing: a fake that fabricated
 * the value would prove the pass reads a struct, not that it reads a computed CSS
 * value. The height goes in as a LENGTH in `px` because that is what 9.8's computed
 * rule produces for an absolute unit — which is exactly the shape the pass has to
 * recognise, and therefore the shape worth constructing.
 */
static void layout_block_style_add(layout_block_source_t *source,
                                   const layout_block_rule_t *rule)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;

    layout_fixture();
    ROBO_CHECK_EQ_INT(source->count < LAYOUT_BLOCK_MAX_RULES, 1);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_display(&value, rule->display, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("display"),
                                                       0u, &specified, &value, NULL),
                      ROBO_OK);
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

static const robo_css_computed_style_t *layout_block_style_lookup(
    void *userdata,
    const robo_html_node_handle_t *element)
{
    layout_block_source_t *source = (layout_block_source_t *)userdata;
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
     * elements the parser synthesized. */
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

typedef struct layout_block_fixture {
    layout_document_t doc;
    layout_block_source_t source;
    robo_layout_style_source_t styles;
    robo_layout_tree_t *tree;
} layout_block_fixture_t;

/**
 * Set @p fixture up on a document the caller already built.
 *
 * The seam `layout_block_begin` is written on top of, and it exists because one
 * case needs to *keep owning* its document: a parsed document is finalized by the
 * build, so an element can no longer be created in it, and a hand-bound one has no
 * such edge (RoboLayoutTestSupport.h). Everything else is shared, which is the
 * point -- a test that assembled its own tree and style source would be testing a
 * different builder from the one every other case uses.
 */
static void layout_block_begin_from(layout_block_fixture_t *fixture,
                                    const layout_document_t *doc,
                                    const layout_block_rule_t *rules,
                                    size_t count)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    fixture->doc = *doc;
    for (index = 0u; index < count; index++) {
        layout_block_style_add(&fixture->source, &rules[index]);
    }
    robo_layout_style_source_init(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.computed_style_for = layout_block_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_block_begin(layout_block_fixture_t *fixture,
                               const char *html,
                               const layout_block_rule_t *rules,
                               size_t count)
{
    layout_document_t doc;

    layout_document_build(&doc, html);
    layout_block_begin_from(fixture, &doc, rules, count);
}

static void layout_block_end(layout_block_fixture_t *fixture)
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

/**
 * One pass with a viewport, which is what classifies *and* lays the tree out.
 *
 * A viewport of 800x600 throughout, so that a width in an assertion is a width the
 * test chose rather than one the display layer happened to have.
 */
#define LAYOUT_BLOCK_VIEWPORT_WIDTH 800
#define LAYOUT_BLOCK_VIEWPORT_HEIGHT 600

static void layout_block_pass(layout_block_fixture_t *fixture,
                              robo_layout_stats_t *out_stats,
                              robo_result_t *out_result)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;
    robo_result_t status;

    viewport.width = LAYOUT_BLOCK_VIEWPORT_WIDTH;
    viewport.height = LAYOUT_BLOCK_VIEWPORT_HEIGHT;
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

/** The box @p index of the tree, in creation order (which is document order). */
static robo_layout_node_id_t layout_block_box_at(layout_block_fixture_t *fixture, size_t index)
{
    robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

    ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture->tree, index, &id), ROBO_OK);
    return id;
}

/** The box built for the element @p tag. */
static robo_layout_node_id_t layout_block_box_of(layout_block_fixture_t *fixture,
                                                 const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&element), 1);
    return robo_layout_object_id_for_dom_node(fixture->tree, &element);
}

/**
 * Assert one box's geometry, and that the geometry is *known*.
 *
 * The four numbers in one expression, because a case about stacking that asserted a
 * y and forgot a height would pass on a build that stacked correctly and sized
 * everything to zero.
 */
static void layout_block_expect(layout_block_fixture_t *fixture,
                                robo_layout_node_id_t id,
                                int32_t x,
                                int32_t y,
                                int32_t width,
                                int32_t height)
{
    robo_layout_result_t result;
    robo_layout_point_t position;
    robo_layout_size_t size;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
    ROBO_CHECK_EQ_INT(position.x, x);
    ROBO_CHECK_EQ_INT(position.y, y);
    ROBO_CHECK_EQ_INT(size.width, width);
    ROBO_CHECK_EQ_INT(size.height, height);
    /* Nothing in this build sets an overflow flag: that a box overflows is 10.13's
     * fact, and a block pass that set one would be answering it. */
    ROBO_CHECK_EQ_INT(result.flags & ROBO_LAYOUT_RESULT_FLAG_ALL, 0);
}

static void layout_block_expect_unresolved(layout_block_fixture_t *fixture,
                                           robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_size_t size;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_UNRESOLVED);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_NOT_FOUND);
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

/**
 * The phase adds no display predicate, and its absence is the assertion.
 *
 * 9.12 answers "is this block-level?" off its own vocabulary table. A second
 * predicate in RoboLayout would be a second place for one answer to live — and the
 * worse copy, because the day `flex` adds a row with an outer of `block`, 9.12's
 * table is where that fact lands. So what is asserted here is the *table the pass
 * reads*, row by row, on the module's own terms: which of the four display values
 * generate a box that participates in a block flow.
 */
static void test_block_display_table(void)
{
    layout_fixture();

    ROBO_CHECK_EQ_INT(robo_css_display_is_block_level(ROBO_CSS_DISPLAY_BLOCK), 1);
    ROBO_CHECK_EQ_INT(robo_css_display_is_block_level(ROBO_CSS_DISPLAY_INLINE), 0);
    ROBO_CHECK_EQ_INT(robo_css_display_is_block_level(ROBO_CSS_DISPLAY_INLINE_BLOCK), 0);
    ROBO_CHECK_EQ_INT(robo_css_display_is_block_level(ROBO_CSS_DISPLAY_NONE), 0);

    /* And the *other* half of the gate agrees with it, which is the invariant
     * `layout.block.context_gate` asserts over a real tree: an outer of `block` is
     * a block container, and the two displays that are not block-level are not. */
    ROBO_CHECK_EQ_INT(robo_css_display_generates_box(ROBO_CSS_DISPLAY_BLOCK), 1);
    ROBO_CHECK_EQ_INT(robo_css_display_generates_box(ROBO_CSS_DISPLAY_NONE), 0);
    ROBO_CHECK_EQ_INT(robo_css_display_inner(ROBO_CSS_DISPLAY_BLOCK),
                      ROBO_CSS_DISPLAY_INNER_FLOW);
    ROBO_CHECK_EQ_INT(robo_css_display_inner(ROBO_CSS_DISPLAY_INLINE_BLOCK),
                      ROBO_CSS_DISPLAY_INNER_FLOW_ROOT);

    /*
     * `inline-block` is the value that separates the two questions, so it is worth
     * stating in the suite that reads them: it is *not* block-level (the box is laid
     * out along a line) while its inner box *is* a block container (its contents
     * stack). A pass that asked only one of the two questions would place it wrongly
     * in one direction or the other.
     */
    ROBO_CHECK_EQ_INT(robo_layout_fc_display_is_block_container(ROBO_CSS_DISPLAY_INLINE_BLOCK),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_display_is_block_level(ROBO_CSS_DISPLAY_INLINE_BLOCK), 0);
}

static void test_block_stats(void)
{
    robo_layout_block_stats_t a;
    robo_layout_block_stats_t b;

    layout_fixture();

    robo_layout_block_stats_init(&a);
    ROBO_CHECK_EQ_INT(a.containers, 0u);
    ROBO_CHECK_EQ_INT(a.blocks_placed, 0u);
    ROBO_CHECK_EQ_INT(a.inline_level_skipped, 0u);
    ROBO_CHECK_EQ_INT(a.unreached, 0u);
    ROBO_CHECK_EQ_INT(a.heights_explicit, 0u);
    ROBO_CHECK_EQ_INT(a.heights_from_content, 0u);
    ROBO_CHECK_EQ_INT(a.empty_containers, 0u);
    ROBO_CHECK_EQ_INT(a.depth_max, 0u);

    b = a;
    ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(&a, &b), 1);
    b.blocks_placed = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(&a, &b), 0);
    /* Every field is part of the comparison, and the one below is the field a
     * hand-written equality is most likely to forget -- it was added last. */
    b = a;
    b.empty_containers = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(&a, &b), 0);
    b = a;
    b.depth_max = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(&a, &b), 0);

    /* Safe on NULL, both ways round, and NULL is only equal to NULL. */
    ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(NULL, &a), 0);
    robo_layout_block_stats_init(NULL);
}

/* ------------------------------------------------------------------ */
/* The pass's preconditions                                            */
/* ------------------------------------------------------------------ */

static void test_block_preconditions(void)
{
    layout_block_fixture_t fixture;
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 }
    };
    robo_layout_size_t viewport;
    robo_layout_block_stats_t stats;
    robo_layout_block_stats_t zeros;
    robo_layout_inline_stats_t inline_stats;
    robo_layout_inline_stats_t inline_zeros;
    robo_layout_text_stats_t text_stats;
    robo_layout_text_stats_t text_zeros;
    robo_layout_diag_t diag;

    layout_fixture();
    layout_block_begin(&fixture, "<html><body></body></html>", rules, 2u);
    viewport.width = LAYOUT_BLOCK_VIEWPORT_WIDTH;
    viewport.height = LAYOUT_BLOCK_VIEWPORT_HEIGHT;
    robo_layout_block_stats_init(&zeros);
    robo_layout_inline_stats_init(&inline_zeros);
    robo_layout_text_stats_init(&text_zeros);

    /* An *argument* refusal happens before the output records are touched, so a
     * caller's stats are left exactly as they were rather than half-cleared -- all
     * three of them, which is why the inline and text records are asserted alongside
     * the block one. Since 10.5 the pass fills three, and "a refusal must not clear
     * what it did not answer" is one rule about three records.
     */
    stats = zeros;
    stats.blocks_placed = 99u;
    inline_stats.participants_placed = 99u;
    text_stats.runs_placed = 99u;
    ROBO_CHECK_EQ_INT(robo_layout_block_layout(NULL, viewport, NULL, &stats, &inline_stats,
                                              &text_stats, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(stats.blocks_placed, 99u);
    ROBO_CHECK_EQ_INT(inline_stats.participants_placed, 99u);
    ROBO_CHECK_EQ_INT(text_stats.runs_placed, 99u);
    viewport.width = -1;
    ROBO_CHECK_EQ_INT(robo_layout_block_layout(fixture.tree, viewport, NULL, &stats,
                                               &inline_stats, &text_stats, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(stats.blocks_placed, 99u);
    ROBO_CHECK_EQ_INT(inline_stats.participants_placed, 99u);
    ROBO_CHECK_EQ_INT(text_stats.runs_placed, 99u);
    viewport.width = LAYOUT_BLOCK_VIEWPORT_WIDTH;

    /*
     * Unclassified: refused rather than repaired, and the refusal happens *after* the
     * output is cleared, because it is a statement about the tree rather than about the
     * arguments. The placement gate *is* the formatting context, so a pass that invented
     * "every block-level box" would be inventing the second classifier this module
     * refuses to have.
     */
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_block_layout(fixture.tree, viewport, NULL, &stats,
                                               &inline_stats, &text_stats, &diag),
                      ROBO_ERROR);
    ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(&stats, &zeros), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(&inline_stats, &inline_zeros), 1);
    ROBO_CHECK_EQ_INT(robo_layout_text_stats_equal(&text_stats, &text_zeros), 1);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT);
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(diag.domain), "block-layout");

    layout_block_end(&fixture);

    /*
     * A document whose root element generates no box: there is nothing to place, and
     * that is an answer about the document rather than a failure. The pass over it
     * *is* legitimate and reports zero; a direct block-layout call answers
     * ROBO_NOT_FOUND, which is how the two doors differ.
     */
    {
        static const layout_block_rule_t hidden[] = {
            { "html", ROBO_CSS_DISPLAY_NONE, 0, 0.0 }
        };
        robo_layout_stats_t pass_stats;

        layout_block_begin(&fixture, "<html><body></body></html>", hidden, 1u);
        ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 0u);
        ROBO_CHECK_EQ_INT(robo_layout_tree_root(fixture.tree), ROBO_LAYOUT_NODE_ID_NONE);

        layout_block_pass(&fixture, &pass_stats, NULL);
        ROBO_CHECK_EQ_INT(pass_stats.objects, 0u);
        ROBO_CHECK_EQ_INT(pass_stats.boxes_computed, 0u);
        ROBO_CHECK_EQ_INT(pass_stats.block.blocks_placed, 0u);
        ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 1);
        ROBO_CHECK_EQ_INT(robo_layout_block_layout(fixture.tree, viewport, NULL, &stats, NULL,
                                                  NULL, NULL),
                          ROBO_NOT_FOUND);
        layout_block_end(&fixture);
    }
}

/* ------------------------------------------------------------------ */
/* Placement: the containing-block convention                          */
/* ------------------------------------------------------------------ */

static void test_block_single(void)
{
    layout_block_fixture_t fixture;
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_block_begin(&fixture, "<html><body></body></html>", rules, 2u);

    /* html, head, body. `<head>` is the parser's and nothing declared for it, so it
     * computes to `inline` and is not in the block flow. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 3u);

    layout_block_pass(&fixture, &stats, NULL);

    /* The root fills the viewport and is placed at its origin; `<body>` is the
     * only block-level child, so it fills what html filled. Every height is zero
     * because nothing declares one and neither box has block-level content. */
    layout_block_expect(&fixture, layout_block_box_at(&fixture, 0u), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 0);
    layout_block_expect(&fixture, layout_block_box_at(&fixture, 2u), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 0);
    layout_block_expect_unresolved(&fixture, layout_block_box_at(&fixture, 1u));

    ROBO_CHECK_EQ_INT(stats.objects, 3u);
    ROBO_CHECK_EQ_INT(stats.visited, 3u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 2u);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 1u);
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 2u);
    ROBO_CHECK_EQ_INT(stats.block.inline_level_skipped, 1u);
    ROBO_CHECK_EQ_INT(stats.block.containers, 1u);      /* html placed body */
    ROBO_CHECK_EQ_INT(stats.block.empty_containers, 1u); /* body has no children */
    ROBO_CHECK_EQ_INT(stats.block.unreached, 0u);
    ROBO_CHECK_EQ_INT(stats.block.heights_from_content, 2u);
    ROBO_CHECK_EQ_INT(stats.block.heights_explicit, 0u);
    /* The deepest *placed* box, which is `<body>` at depth 1: `<head>` is deeper
     * than nothing and is not placed, so it does not count. */
    ROBO_CHECK_EQ_INT(stats.block.depth_max, 1u);

    layout_block_end(&fixture);
}

static void test_block_siblings(void)
{
    layout_block_fixture_t fixture;
    /*
     * Three siblings with distinct declared heights, and the tags are distinct
     * because the scripted source answers per *tag*: a fixture that keyed off a class
     * would be exercising the selector matcher, and this case is about arithmetic.
     */
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "p", ROBO_CSS_DISPLAY_BLOCK, 1, 100.0 },
        { "h1", ROBO_CSS_DISPLAY_BLOCK, 1, 50.0 },
        { "ul", ROBO_CSS_DISPLAY_BLOCK, 1, 75.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_block_begin(&fixture, "<html><body><p></p><h1></h1><ul></ul></body></html>",
                       rules, 5u);
    /* html, head, body, p, h1, ul */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 6u);

    layout_block_pass(&fixture, &stats, NULL);

    /* Each sibling below the last, each filling the containing block's width. The
     * cursor is the *only* thing that carries between them, which is why the three
     * y values are asserted rather than just the last. */
    layout_block_expect(&fixture, layout_block_box_at(&fixture, 3u), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 100);
    layout_block_expect(&fixture, layout_block_box_at(&fixture, 4u), 0, 100,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 50);
    layout_block_expect(&fixture, layout_block_box_at(&fixture, 5u), 0, 150,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 75);
    /* `<body>`'s height is the extent its children reached, and html's is body's. */
    layout_block_expect(&fixture, layout_block_box_at(&fixture, 2u), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 225);
    layout_block_expect(&fixture, layout_block_box_at(&fixture, 0u), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 225);

    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 5u);
    ROBO_CHECK_EQ_INT(stats.block.inline_level_skipped, 1u);
    ROBO_CHECK_EQ_INT(stats.block.heights_explicit, 3u);
    ROBO_CHECK_EQ_INT(stats.block.heights_from_content, 2u);
    ROBO_CHECK_EQ_INT(stats.block.containers, 2u);       /* html, body */
    ROBO_CHECK_EQ_INT(stats.block.empty_containers, 3u); /* p, h1, ul */
    ROBO_CHECK_EQ_INT(stats.block.depth_max, 2u);
    ROBO_CHECK_EQ_INT(stats.block.unreached, 0u);

    layout_block_end(&fixture);
}

static void test_block_nested(void)
{
    layout_block_fixture_t fixture;
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "p", ROBO_CSS_DISPLAY_BLOCK, 1, 30.0 },
        { "div", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "h1", ROBO_CSS_DISPLAY_BLOCK, 1, 20.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_block_begin(&fixture,
                       "<html><body><p></p><div><h1></h1></div></body></html>",
                       rules, 5u);
    /* html, head, body, p, div, h1 */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 6u);

    layout_block_pass(&fixture, &stats, NULL);

    /*
     * The assertion this case exists for. `<h1>` is at y = 0 — in the space of the
     * `<div>` that contains it — while the `<div>` is at y = 30 in the space of
     * `<body>`. A pass that wrote absolute coordinates would put `<h1>` at 30, and a
     * pass that ignored nesting would put it at 0 with the wrong parent.
     *
     * The document-space position is deliberately *not* asserted, because computing
     * it means walking the chain of containing blocks and that is 10.12's job: what
     * this phase fixes is that a result is stated relative to a box's containing
     * block, and a test that summed the chain here would be asserting a convention
     * the module has not promised.
     */
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "h1"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 20);
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "div"), 0, 30,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 20);
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "p"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 30);
    /* body's extent is 30 + the div's 20, and html's is body's. */
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 50);
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "html"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 50);

    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 5u);
    ROBO_CHECK_EQ_INT(stats.block.containers, 3u);      /* html, body, div */
    ROBO_CHECK_EQ_INT(stats.block.empty_containers, 2u); /* p, h1 */
    ROBO_CHECK_EQ_INT(stats.block.depth_max, 3u);
    /* The div's height came from its content and it has one child, so it is a
     * container; the div itself is not an empty container. */
    ROBO_CHECK_EQ_INT(stats.block.heights_from_content, 3u);
    ROBO_CHECK_EQ_INT(stats.block.heights_explicit, 2u);

    layout_block_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Heights                                                             */
/* ------------------------------------------------------------------ */

/**
 * A declared height that is smaller than the content does not clip it, and does not
 * raise a flag.
 *
 * Both halves matter. The children are still placed — they are boxes and they have
 * geometry — and their extents run past their parent's, which is a *fact* rather
 * than an error: CSS says the content overflows, and which axis overflows is what
 * the result's two flags are for. They stay 0 because deciding it is 10.13's, and a
 * block pass that decided it here would be answering a question with a containing
 * block it does not have.
 */
static void test_block_explicit_height(void)
{
    layout_block_fixture_t fixture;
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "div", ROBO_CSS_DISPLAY_BLOCK, 1, 10.0 },
        { "p", ROBO_CSS_DISPLAY_BLOCK, 1, 30.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_block_begin(&fixture,
                       "<html><body><div><p></p><p></p></div></body></html>",
                       rules, 4u);

    layout_block_pass(&fixture, &stats, NULL);

    /* Two `<p>`s at 0 and 30 inside a `<div>` that declares 10. */
    layout_block_expect(&fixture, layout_block_box_at(&fixture, 4u), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 30);
    layout_block_expect(&fixture, layout_block_box_at(&fixture, 5u), 0, 30,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 30);
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "div"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 10);
    /* The parent's declared height is what propagates, not the content's 60. */
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 10);
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "html"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 10);

    ROBO_CHECK_EQ_INT(stats.block.heights_explicit, 3u);
    ROBO_CHECK_EQ_INT(stats.block.heights_from_content, 2u);

    /* And a fractional declared height is rounded rather than truncated, which is
     * the decision the implementation states: 10.5 has to become one of two
     * integers, and truncating would make every fractional height come out a pixel
     * short. */
    layout_block_end(&fixture);
    {
        static const layout_block_rule_t fractional[] = {
            { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
            { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
            { "p", ROBO_CSS_DISPLAY_BLOCK, 1, 10.5 }
        };

        layout_block_begin(&fixture, "<html><body><p></p></body></html>", fractional, 3u);
        layout_block_pass(&fixture, &stats, NULL);
        layout_block_expect(&fixture, layout_block_box_of(&fixture, "p"), 0, 0,
                            LAYOUT_BLOCK_VIEWPORT_WIDTH, 11);
    }

    layout_block_end(&fixture);
}

/**
 * A height this build cannot resolve takes the `auto` path.
 *
 * `50%` needs a containing block height and `2em` needs a font; 9.10 keeps both in
 * their own kinds and units precisely so that a consumer can tell "I cannot resolve
 * this" from "this is ten pixels". The honest answer for both is the content's
 * extent — not a guess, and not a refusal of the whole document.
 */
/**
 * Replace @p tag's declared height with @p value, which is *moved* in.
 *
 * `set_entry` replaces an entry in place, so a case can declare a height once and
 * then vary what it *is* — and a second rule for the same tag would be invisible
 * anyway, because the lookup answers with the first match. That is the kind of
 * fixture bug that makes a test pass for a reason nobody wrote down, so this helper
 * exists rather than four near-copies of it.
 */
static void layout_block_set_height(layout_block_source_t *source,
                                   const char *tag,
                                   robo_css_computed_value_t *value)
{
    robo_css_value_t specified = ROBO_CSS_CLEARED;
    size_t index;

    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(
                                  source->rules[index].style, ROBO_CSS_PROPERTY_ID_NONE,
                                  robo_css_slice_from_text("height"), 0u, &specified, value,
                                  NULL),
                              ROBO_OK);
            return;
        }
    }
    ROBO_CHECK_MSG(0, "no rule for that tag");
}

/** Rebuild @p fixture's tree from the same document and the same style source. */
static void layout_block_rebuild(layout_block_fixture_t *fixture)
{
    robo_layout_build_options_t options;

    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void test_block_unresolvable_height(void)
{
    layout_block_fixture_t fixture;
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "p", ROBO_CSS_DISPLAY_BLOCK, 1, 40.0 },
        /* The `<div>` starts with no height entry at all: the presence check's case. */
        { "div", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 }
    };
    layout_block_source_t *source;
    robo_layout_stats_t stats;
    size_t index;

    layout_fixture();
    layout_block_begin(&fixture, "<html><body><div><p></p></div></body></html>", rules, 4u);
    source = &fixture.source;

    /* No entry at all: `auto`, which is the property's initial value. */
    layout_block_pass(&fixture, &stats, NULL);
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "div"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 40);
    ROBO_CHECK_EQ_INT(stats.block.heights_explicit, 1u); /* only the `<p>` */
    ROBO_CHECK_EQ_INT(stats.block.heights_from_content, 3u);

    /*
     * Then three declared heights this build cannot use, each failing a *different*
     * check: a relative unit fails the unit test, a percentage fails the kind test,
     * and a negative length passes both and is refused by the range test. All three
     * must produce the same 40 — the child's extent — so a build that guessed at any
     * of them shows up as a wrong number rather than as nothing.
     */
    for (index = 0u; index < 3u; index++) {
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;

        if (index == 0u) {
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, 5.0,
                                                                 ROBO_CSS_UNIT_EM, 0, NULL),
                              ROBO_OK);
        } else if (index == 1u) {
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_percentage(&value, 50.0, 1, NULL),
                              ROBO_OK);
        } else {
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, -4.0,
                                                                 ROBO_CSS_UNIT_PX, 0, NULL),
                              ROBO_OK);
        }
        layout_block_set_height(source, "div", &value);
        layout_block_rebuild(&fixture);
        layout_block_pass(&fixture, &stats, NULL);

        layout_block_expect(&fixture, layout_block_box_of(&fixture, "div"), 0, 0,
                            LAYOUT_BLOCK_VIEWPORT_WIDTH, 40);
        ROBO_CHECK_EQ_INT(stats.block.heights_explicit, 1u); /* only the `<p>` */
        ROBO_CHECK_EQ_INT(stats.block.heights_from_content, 3u);
    }

    /*
     * And a resolvable height still wins over the content, so the three refusals
     * above are about the *value* and not about the code path having been taken: 7
     * is neither 40 nor 0.
     */
    {
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, 7.0, ROBO_CSS_UNIT_PX,
                                                             0, NULL),
                          ROBO_OK);
        layout_block_set_height(source, "div", &value);
        layout_block_rebuild(&fixture);
        layout_block_pass(&fixture, &stats, NULL);
        layout_block_expect(&fixture, layout_block_box_of(&fixture, "div"), 0, 0,
                            LAYOUT_BLOCK_VIEWPORT_WIDTH, 7);
        ROBO_CHECK_EQ_INT(stats.block.heights_explicit, 2u);     /* `<div>` and `<p>` */
        ROBO_CHECK_EQ_INT(stats.block.heights_from_content, 2u); /* html, body */
    }

    layout_block_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The gate: what block layout declines to place                       */
/* ------------------------------------------------------------------ */

static void test_block_inline_skipped(void)
{
    layout_block_fixture_t fixture;
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "p", ROBO_CSS_DISPLAY_BLOCK, 1, 20.0 },
        /* An atomic inline: a box that belongs on a line, and whose own contents
         * would stack. It is *not* placed here -- and from 10.4 the reason is not
         * merely "it is inline-level" but "the context it participates in is the
         * root's, which is a block-direction one": 10.2 made `<body>` transparent
         * because it has a block-level child, so there is no line for the span to be
         * on and the inline direction never sees it. */
        { "span", ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 30.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_block_begin(&fixture, "<html><body><p></p><span></span></body></html>",
                       rules, 4u);

    layout_block_pass(&fixture, &stats, NULL);

    layout_block_expect(&fixture, layout_block_box_of(&fixture, "p"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 20);
    layout_block_expect_unresolved(&fixture, layout_block_box_of(&fixture, "span"));
    /* The inline-block contributes *nothing* to the cursor: `<p>` is at 0 and the
     * document's extent is 20, not 50. A build that measured an unplaced sibling
     * would stack it and report a height nobody computed. */
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 20);

    /* Three placed (html, body, p) and two declined: `<head>`, which nothing declared
     * for, and the atomic inline. */
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 3u);
    ROBO_CHECK_EQ_INT(stats.block.inline_level_skipped, 2u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 3u);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 2u);
    ROBO_CHECK_EQ_INT(stats.block.unreached, 0u);

    layout_block_end(&fixture);
}

/**
 * A `display: none` element is not in the tree at all, and its *children* are.
 *
 * This is 10.1's rule, restated where a number now depends on it: the element
 * generates no box, so it cannot be placed, skipped or counted by the block pass —
 * it simply is not there. The child inside it *does* generate a box, whose parent
 * is the hidden element's parent, which is exactly the position 10.1's builder
 * gives it.
 */
static void test_block_display_none(void)
{
    layout_block_fixture_t fixture;
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "div", ROBO_CSS_DISPLAY_NONE, 0, 0.0 },
        { "p", ROBO_CSS_DISPLAY_BLOCK, 1, 25.0 }
    };
    robo_layout_stats_t stats;

    layout_fixture();
    layout_block_begin(&fixture, "<html><body><p></p><div></div></body></html>",
                       rules, 4u);
    /* html, head, body, p — the div generates nothing. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 4u);
    {
        robo_html_node_handle_t div = layout_document_find(&fixture.doc, "div");

        ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&div), 1);
        ROBO_CHECK_EQ_INT(robo_layout_object_id_for_dom_node(fixture.tree, &div),
                          ROBO_LAYOUT_NODE_ID_NONE);
    }

    layout_block_pass(&fixture, &stats, NULL);

    layout_block_expect(&fixture, layout_block_box_of(&fixture, "p"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 25);
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 25);
    ROBO_CHECK_EQ_INT(stats.objects, 4u);
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 3u);
    ROBO_CHECK_EQ_INT(stats.block.inline_level_skipped, 1u);
    ROBO_CHECK_EQ_INT(stats.block.unreached, 0u);

    layout_block_end(&fixture);
}

/**
 * The invariant the two phases have to agree on.
 *
 * Every box this pass declines for a *stated* reason is inline-level, and every box
 * it places is block-level in a block-direction context. Those two rules are stated
 * in different modules — 9.12's display vocabulary and 10.2's classifier — and the
 * interesting case is where they could disagree: a block-level box inside a box
 * that establishes an **inline** context. 10.2's rules make that impossible (a box
 * establishes an inline context only when it has *no* block-level child, and such a
 * box would be one), so the pass checks it rather than assuming it and this case
 * proves the check is never reached.
 *
 * The `p` with text in it is the case worth having: a block container whose only
 * content is a text node establishes an inline context, so the *text* is inline —
 * and the box itself is still a block in the flow, placed at its parent's origin.
 */
static void test_block_context_gate(void)
{
    layout_block_fixture_t fixture;
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "p", ROBO_CSS_DISPLAY_BLOCK, 1, 18.0 },
        { "span", ROBO_CSS_DISPLAY_INLINE, 0, 0.0 },
        { "em", ROBO_CSS_DISPLAY_BLOCK, 1, 12.0 }
    };
    robo_layout_stats_t stats;
    size_t index;

    layout_fixture();
    layout_block_begin(&fixture,
                       "<html><body><p>text</p><span></span></body></html>",
                       rules, 5u);

    layout_block_pass(&fixture, &stats, NULL);

    /* `<p>` holds a text node, which is not a box: so it has no block-level child,
     * establishes an inline context, and is still placed *as a block*. */
    {
        robo_layout_node_id_t p_box = layout_block_box_of(&fixture, "p");
        robo_layout_fc_id_t owned = ROBO_LAYOUT_FC_ID_NONE;
        robo_layout_formatting_context_t context;

        ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(fixture.tree, p_box, &owned, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, owned, &context, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)context.kind, (int)ROBO_LAYOUT_FC_INLINE);
        layout_block_expect(&fixture, p_box, 0, 0, LAYOUT_BLOCK_VIEWPORT_WIDTH, 18);
    }

    /*
     * And the invariant, over every box in the tree: a placed box is block-level in
     * a block-direction context, and a block-level box is never in any other kind.
     * Walked rather than asserted per case, because the claim is about *all* boxes
     * and a case that checked one would not be checking it.
     */
    for (index = 0u; index < robo_layout_tree_object_count(fixture.tree); index++) {
        robo_layout_node_id_t id = layout_block_box_at(&fixture, index);
        robo_layout_result_t result;
        robo_layout_fc_id_t participating = ROBO_LAYOUT_FC_ID_NONE;
        robo_layout_formatting_context_t context;
        robo_css_display_t display = robo_layout_object_display(fixture.tree, id);

        ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, id, &participating, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, participating, &context, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &result), ROBO_OK);

        if (robo_css_display_is_block_level(display)) {
            /* A block-level box is always in a block-direction context -- the claim. */
            ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_block_container(context.kind), 1);
        } else if (robo_layout_fc_kind_is_block_container(context.kind)) {
            /* And an inline-level box in a *block-direction* context is never placed
             * by this pass -- it is the anonymous-box case, and it is counted rather
             * than dropped. The mirror case, an inline-level box in an inline context,
             * is *not* asserted here: 10.4 places it, and that claim belongs to
             * layout.inline. */
            ROBO_CHECK_EQ_INT(result.state, ROBO_LAYOUT_RESULT_UNRESOLVED);
        }
    }

    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 3u);      /* html, body, p */
    ROBO_CHECK_EQ_INT(stats.block.inline_level_skipped, 2u); /* head, span */
    ROBO_CHECK_EQ_INT(stats.block.unreached, 0u);

    layout_block_end(&fixture);
    /*
     * And the *mirror* case, which 10.4 moved: a block-level box inside an inline
     * box. `<body>`'s only child is a `<span>`, so body has no block-level child and
     * 10.2 makes it establish an inline formatting context -- which means the `<em>`
     * inside the span is a block-level box participating in an *inline* context, and
     * 10.4 declines it for the same reason 10.3 declines the inline-level boxes
     * above: the anonymous block box CSS would give it is a box with no element and
     * no style, and this build makes none.
     *
     * The span itself moved the other way, and that is the half worth asserting: it
     * used to be `inline_level_skipped` and is now *placed* by the inline direction
     * with the zero extent its declined child left it. So the two records each
     * decline exactly one box and the identity below closes over both directions.
     */
    {
        static const layout_block_rule_t nested[] = {
            { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
            { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
            { "span", ROBO_CSS_DISPLAY_INLINE, 0, 0.0 },
            { "em", ROBO_CSS_DISPLAY_BLOCK, 1, 12.0 }
        };

        layout_block_begin(&fixture, "<html><body><span><em></em></span></body></html>",
                           nested, 4u);
        layout_block_pass(&fixture, &stats, NULL);

        layout_block_expect_unresolved(&fixture, layout_block_box_of(&fixture, "em"));
        layout_block_expect(&fixture, layout_block_box_of(&fixture, "span"), 0, 0, 0, 0);
        ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 2u);      /* html, body */
        ROBO_CHECK_EQ_INT(stats.block.inline_level_skipped, 1u); /* head */
        ROBO_CHECK_EQ_INT(stats.block.unreached, 1u);          /* em */
        ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 1u); /* span */
        ROBO_CHECK_EQ_INT(stats.inline_flow.block_level_declined, 1u); /* em */
        ROBO_CHECK_EQ_INT(stats.boxes_computed, 3u);
        ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 2u);
        /*
         * The accounting closes over both directions: every box is placed, declined by
         * one of the two, or unreached, and nothing is in two of those at once.
         * Asserted as an identity rather than as four numbers, because the claim is
         * that the pass *accounts for* the tree -- a build that dropped a box would
         * have right-looking counts and a document that did not add up.
         */
        ROBO_CHECK_EQ_INT(stats.block.blocks_placed + stats.block.inline_level_skipped +
                              stats.inline_flow.participants_placed + stats.block.unreached,
                          stats.objects);
    }

    layout_block_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pass: determinism, invalidation, the real pipeline              */
/* ------------------------------------------------------------------ */

static void test_block_no_viewport(void)
{
    layout_block_fixture_t fixture;
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "p", ROBO_CSS_DISPLAY_BLOCK, 1, 40.0 }
    };
    robo_layout_context_t *context = NULL;
    robo_layout_stats_t stats;
    size_t index;

    layout_fixture();
    layout_block_begin(&fixture, "<html><body><p></p></body></html>", rules, 3u);

    /*
     * The precondition, stated rather than silently satisfied: the root box's
     * containing block is the initial containing block, which is the viewport, and
     * RoboLayout owns no window to ask. So a pass with no viewport classifies and
     * places nothing -- and says so in two independent places, which is what this
     * case checks.
     */
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                      (int)ROBO_LAYOUT_PASS_WALKED);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.objects, 4u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 0u);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 4u);
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(&stats.block, &(robo_layout_block_stats_t){0}),
                      1);
    /* The classification still happened: the pass is not "half a pass", it is the
     * part of the work the input allowed. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 1);
    ROBO_CHECK_EQ_INT(stats.contexts > 0u, 1);
    for (index = 0u; index < 4u; index++) {
        layout_block_expect_unresolved(&fixture, layout_block_box_at(&fixture, index));
    }
    robo_layout_context_destroy(context);

    /* And the same tree, given a viewport, is laid out -- which is what makes the
     * zero above a statement about the *input* rather than about the build. */
    layout_block_pass(&fixture, &stats, NULL);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 3u);
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "p"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 40);

    layout_block_end(&fixture);
}

static void test_block_determinism(void)
{
    layout_block_fixture_t fixture;
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "p", ROBO_CSS_DISPLAY_BLOCK, 1, 30.0 },
        { "div", ROBO_CSS_DISPLAY_BLOCK, 1, 12.0 },
        { "h1", ROBO_CSS_DISPLAY_BLOCK, 1, 7.0 }
    };
    robo_layout_stats_t first;
    robo_layout_stats_t second;
    robo_layout_result_t before[8];
    size_t count;
    size_t index;

    layout_fixture();
    layout_block_begin(&fixture,
                       "<html><body><p></p><div><h1></h1></div></body></html>",
                       rules, 5u);
    count = robo_layout_tree_object_count(fixture.tree);
    ROBO_CHECK(count <= 8u);

    /*
     * Two passes on **one** context, which is the point: a helper that made a fresh
     * context per call would prove the geometry is reproducible and say nothing about
     * whether a pass stacks on the one before it. The second run must not turn each
     * box's height into its own height plus its content's -- the failure a cumulative
     * pass has -- and the context's own `passes` count is what proves both runs were
     * the same context.
     */
    {
        robo_layout_context_t *context = NULL;
        robo_layout_size_t viewport;

        viewport.width = LAYOUT_BLOCK_VIEWPORT_WIDTH;
        viewport.height = LAYOUT_BLOCK_VIEWPORT_HEIGHT;
        ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
        robo_layout_context_stats(context, &first);
        for (index = 0u; index < count; index++) {
            robo_layout_node_id_t id = layout_block_box_at(&fixture, index);

            ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &before[index]),
                              ROBO_OK);
        }

        ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
        robo_layout_context_stats(context, &second);
        ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                          (int)ROBO_LAYOUT_PASS_WALKED);
        robo_layout_context_destroy(context);
    }
    ROBO_CHECK_EQ_INT(first.passes, 1u);
    ROBO_CHECK_EQ_INT(second.passes, 2u);
    ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(&first.block, &second.block), 1);
    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id = layout_block_box_at(&fixture, index);
        robo_layout_result_t now;

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &now), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_result_equal(&before[index], &now), 1);
    }

    /*
     * And a second *tree* from the same document. Both borrow the same styles, so
     * this is determinism of the algorithm rather than of the build: the geometry
     * must be equal box for box, not merely equal in count.
     */
    {
        layout_block_fixture_t twin;
        robo_layout_stats_t twin_stats;

        memset(&twin, 0, sizeof(twin));
        layout_document_build(&twin.doc, "<html><body><p></p><div><h1></h1></div></body></html>");
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
        layout_block_pass(&twin, &twin_stats, NULL);
        ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(&first.block, &twin_stats.block), 1);
        ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(twin.tree), count);
        for (index = 0u; index < count; index++) {
            robo_layout_node_id_t id = layout_block_box_at(&twin, index);
            robo_layout_result_t now;

            ROBO_CHECK_EQ_INT(robo_layout_object_result(twin.tree, id, &now), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_layout_result_equal(&before[index], &now), 1);
        }
        robo_layout_tree_destroy(twin.tree);
        layout_document_release(&twin.doc);
    }

    layout_block_end(&fixture);
}

/**
 * Geometry is derived from the classification, so releasing the classification
 * releases it.
 *
 * The finding this case was written for: before it, a result stayed `COMPUTED`
 * across an invalidation, holding coordinates computed from a classification that
 * no longer existed -- and `COMPUTED` is precisely the state that is not allowed to
 * mean "true a moment ago". Both doors that invalidate are exercised, because they
 * are the same door and a fix in one of them would leave the other lying.
 */
static void test_block_invalidation(void)
{
    layout_block_fixture_t fixture;
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "p", ROBO_CSS_DISPLAY_BLOCK, 1, 30.0 },
        /* The tag the second door adds later: a style waiting for an element that
         * does not exist yet, which is the shape the scripted source is built for. */
        { "section", ROBO_CSS_DISPLAY_BLOCK, 1, 30.0 }
    };
    robo_layout_stats_t stats;
    robo_layout_node_id_t added = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_object_info_t info;
    robo_layout_result_t fresh_result;
    robo_layout_size_t fresh_size;
    robo_html_node_handle_t body;
    robo_html_node_handle_t section;

    /*
     * The document is assembled through the DOM seam rather than parsed, and that is
     * load-bearing rather than taste: this case's second door needs an element that
     * appears *after* the tree was built, and every parsed document is finalized by
     * the build -- which closes the construction gate `robo_html_element_create`
     * writes through. A seam-built document is never finalized, so it can keep
     * growing while the tree built from it is being tested.
     *
     * `<head>` is bound by hand because the parser synthesizes it, and leaving it
     * out would quietly change what this case covers: it is the box the block pass
     * *declines* (nothing styles it, so it computes to `inline`), and the first pass
     * below asserts three placed out of four for that reason.
     */
    layout_fixture();
    {
        layout_document_t doc;
        robo_html_node_handle_t document_root;
        robo_html_node_handle_t html;

        layout_document_create_empty(&doc);
        document_root = robo_html_document_root(doc.id);
        html = layout_document_add_element(&doc, &document_root, "html");
        (void)layout_document_add_element(&doc, &html, "head");
        body = layout_document_add_element(&doc, &html, "body");
        (void)layout_document_add_element(&doc, &body, "p");
        layout_block_begin_from(&fixture, &doc, rules, 4u);
    }

    layout_block_pass(&fixture, &stats, NULL);
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 3u);
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 30);

    /* Door one: an explicit invalidation. */
    robo_layout_tree_invalidate_contexts(fixture.tree);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 0);
    {
        size_t index;

        for (index = 0u; index < 4u; index++) {
            layout_block_expect_unresolved(&fixture, layout_block_box_at(&fixture, index));
        }
    }

    /* Re-running recalculates, so the release is not destructive. */
    layout_block_pass(&fixture, &stats, NULL);
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 30);

    /*
     * Door two: adding a box. It invalidates the classification implicitly, because
     * a new child changes the composition one of 10.2's three inputs is made of --
     * and therefore changes every height above it, which is why the *geometry* has to
     * go with it.
     *
     * The element the box models is created here, after the tree exists, because that
     * is the situation: a document that grew a node and a tree that has to be told
     * about it. Its style comes from the same scripted source the builder reads, so
     * the box is the box the next build would have made -- one declaration, thirty
     * pixels, nothing this door invents.
     */
    section = layout_document_add_element(&fixture.doc, &body, "section");

    robo_layout_object_info_init(&info);
    info.parent = layout_block_box_of(&fixture, "body");
    info.dom_node = section;
    info.display = ROBO_CSS_DISPLAY_BLOCK;
    info.style = layout_block_style_lookup(&fixture.source, &section);
    ROBO_CHECK(info.style != NULL);
    ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &added, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_layout_object_is_live(fixture.tree, added));
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 0);

    /*
     * And its state is NONE, where the four boxes above read UNRESOLVED -- the
     * distinction the two states exist to make and the one a blunt release would
     * erase.
     *
     * The four were COMPUTED a moment ago: they *had* geometry and it has been
     * withdrawn, so the honest answer is "a box exists, nobody has computed it". A
     * box that has just been added never had geometry to withdraw, and if the
     * release manufactured UNRESOLVED for it then NONE would be a state no path in
     * this module produces -- a header documenting a value nothing can return. Both
     * answers say "I cannot tell you where this box is"; only one of them says
     * whether anybody ever could. */
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, added, &fresh_result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)fresh_result.state, (int)ROBO_LAYOUT_RESULT_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_result_is_computed(&fresh_result), 0);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&fresh_result, &fresh_size), ROBO_NOT_FOUND);
    /* The four it did not touch are still UNRESOLVED, read back one apiece -- so the
     * conditional is exercised on both of its branches in one case rather than in
     * two suites that could drift apart. */
    {
        size_t index;

        for (index = 0u; index < 4u; index++) {
            layout_block_expect_unresolved(&fixture, layout_block_box_at(&fixture, index));
        }
    }

    /* And a fresh pass sees the new box: body's extent is 30 + 30. */
    layout_block_pass(&fixture, &stats, NULL);
    ROBO_CHECK_EQ_INT(stats.objects, 5u);
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 4u);
    layout_block_expect(&fixture, added, 0, 30, LAYOUT_BLOCK_VIEWPORT_WIDTH, 30);
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "body"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 60);
    layout_block_expect(&fixture, layout_block_box_of(&fixture, "html"), 0, 0,
                        LAYOUT_BLOCK_VIEWPORT_WIDTH, 60);

    layout_block_end(&fixture);
}

/**
 * The numbers came from CSS.
 *
 * A real stylesheet through the real cascade, and the assertions are chosen to say
 * what each phase reads and what it deliberately does not: `height` is honoured,
 * `width` was *not* until 10.8 and `margin` was *not* until 10.9, while
 * `padding`/`border` still change nothing. Each of the ignored declarations is the
 * reason a capability field is 0, and putting them in the same document as the
 * honoured ones is what makes that visible in one place.
 */
static void test_block_pipeline(void)
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
    robo_html_node_handle_t card;
    robo_layout_node_id_t card_box;

    layout_fixture();
    layout_document_build(&doc,
                          "<html>\n"
                          "  <body>\n"
                          "    <div class=\"card\"><p>Hello</p></div>\n"
                          "  </body>\n"
                          "</html>\n");
    layout_css_begin(&css);
    layout_css_sheet(&css,
                     "html, body, p { display: block }\n"
                     ".card {\n"
                     "  display: block;\n"
                     "  height: 100px;\n"
                     "  width: 200px;\n"
                     "  margin: 10px 20px;\n"
                     "  padding: 5px;\n"
                     "  border: 4px solid black;\n"
                     "  position: relative;\n"
                     "  top: 12px;\n"
                     "}\n");
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(css.context, doc.id, &dom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(dom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(dom, &source, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &source;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(tree, doc.id, &options, NULL, NULL),
                      ROBO_OK);

    /* html, head, body, div.card, p -- five boxes, plus the text object `<p>`'s text
     * node generates (10.5). That object is inline-level and the paragraph owns an
     * inline context, so the inline pass places it; with no metrics source in this
     * case it measures zero, which is why the extents below are unchanged. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), 6u);

    viewport.width = LAYOUT_BLOCK_VIEWPORT_WIDTH;
    viewport.height = LAYOUT_BLOCK_VIEWPORT_HEIGHT;
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, tree, NULL), ROBO_OK);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                      (int)ROBO_LAYOUT_PASS_WALKED);

    card = layout_document_find(&doc, "div");
    card_box = robo_layout_object_id_for_dom_node(tree, &card);
    {
        robo_layout_result_t result;
        robo_layout_point_t position;
        robo_layout_size_t size;

        ROBO_CHECK_EQ_INT(robo_layout_object_result(tree, card_box, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
        ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);

        /*
         * `height: 100px` and `width: 200px` reach the result -- and since 10.11 the
         * numbers here are the card's **border** box, because `padding: 5px` puts five on
         * each of its four sides *and* `border: 4px solid black` puts four more outside
         * those. This case is where 10.3 recorded that a declared width was **ignored**
         * ("a block box fills its containing block, and resolving a declared width is
         * 10.8"), where 10.8 recorded it being honoured, and where 10.10 moved the number
         * by the padding. 10.11 is the fourth phase to move it and the **last**: the box
         * model is complete, so `width: 200px` is the *content* box under this build's
         * `content-box` model and the box a reader sees is 218 by 118 -- 200 + 10 of
         * padding + 8 of border, and 100 + 10 + 8. A build that had read the declaration
         * as a border box would still give 200 and 100 here, which is why the pair is worth
         * writing down.
         */
        ROBO_CHECK_EQ_INT(size.height, 118);
        ROBO_CHECK_EQ_INT(size.width, 218);
        /*
         * `margin: 10px 20px` reaches geometry since 10.9, and neither `border` nor
         * `padding` ever touches this number: the box is at its parent's content origin
         * **plus its own margins**. A margin is a declaration about the gap beside a box, so
         * it moves the box and leaves the size alone; a padding and a border are declarations
         * about the space inside one, so they change the size and leave the flow position
         * alone. And since 10.12 the card also declares `position: relative; top: 12px`, so
         * the **final** position is that flow position displaced -- y is 10 + 12 = 22 while
         * x keeps the margins' 20 (`left`/`right` are absent, and an absent side displaces
         * nothing). The displacement adds outside the four assertions above it: the size is
         * the border box still, and the following sibling still starts where the *flow*
         * position says.
         */
        ROBO_CHECK_EQ_INT(position.x, 20);
        ROBO_CHECK_EQ_INT(position.y, 22);
    }
    /* `<p>` is a block inside the card and declares no height, so the card's extent
     * would be 0 -- and the declared 100 is what propagates up the chain. */
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 4u);
    ROBO_CHECK_EQ_INT(stats.block.heights_explicit, 1u);
    ROBO_CHECK_EQ_INT(stats.block.heights_from_content, 3u);

    /*
     * The accounting closes here too, and it is worth asserting on a document whose
     * box count the *parser* decided (it synthesizes `<head>`) rather than one the
     * fixture wrote.
     *
     * The four terms are the four answers to "why has this object no geometry?", and
     * the fourth is 10.4's: a participant the *inline* pass placed is explained by that
     * pass rather than by this one. `<p>`'s text run is exactly such an object, which
     * is why the sum omits it and still has to add up -- a build that dropped it from
     * the identity would have six objects and five explanations.
     */
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed + stats.block.inline_level_skipped +
                          stats.inline_flow.participants_placed + stats.block.unreached,
                      stats.objects);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.objects, 6u);

    robo_layout_context_destroy(context);
    robo_layout_tree_destroy(tree);
    robo_layout_dom_destroy(dom);
    layout_css_end(&css);
    layout_document_release(&doc);
}

static void test_block_allocator_balance(void)
{
    layout_block_fixture_t fixture;
    static const layout_block_rule_t rules[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "body", ROBO_CSS_DISPLAY_BLOCK, 0, 0.0 },
        { "p", ROBO_CSS_DISPLAY_BLOCK, 1, 30.0 },
        { "div", ROBO_CSS_DISPLAY_BLOCK, 1, 12.0 }
    };
    size_t before;
    int pass;

    layout_fixture();
    before = layout_live_blocks();

    for (pass = 0; pass < 8; pass++) {
        robo_layout_stats_t stats;

        layout_block_begin(&fixture,
                           "<html><body><p></p><div></div></body></html>", rules, 4u);
        layout_block_pass(&fixture, &stats, NULL);
        layout_block_pass(&fixture, &stats, NULL);
        layout_block_end(&fixture);
    }

    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout_block(void);

void robo_test_register_layout_block(void)
{
    robo_test_register("layout.block.display_table", test_block_display_table);
    robo_test_register("layout.block.stats", test_block_stats);
    robo_test_register("layout.block.preconditions", test_block_preconditions);
    robo_test_register("layout.block.single", test_block_single);
    robo_test_register("layout.block.siblings", test_block_siblings);
    robo_test_register("layout.block.nested", test_block_nested);
    robo_test_register("layout.block.explicit_height", test_block_explicit_height);
    robo_test_register("layout.block.unresolvable_height", test_block_unresolvable_height);
    robo_test_register("layout.block.inline_skipped", test_block_inline_skipped);
    robo_test_register("layout.block.display_none", test_block_display_none);
    robo_test_register("layout.block.context_gate", test_block_context_gate);
    robo_test_register("layout.block.no_viewport", test_block_no_viewport);
    robo_test_register("layout.block.determinism", test_block_determinism);
    robo_test_register("layout.block.invalidation", test_block_invalidation);
    robo_test_register("layout.block.pipeline", test_block_pipeline);
    robo_test_register("layout.block.allocator_balance", test_block_allocator_balance);
}
