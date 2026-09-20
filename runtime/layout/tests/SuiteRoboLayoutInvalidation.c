/*
 * RoboLayout invalidation suite (Phase 10.14) — layout.invalidation.*
 *
 * Six things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary**: four state bits that compose, a closed set of reasons, three
 *   style effects, and the name doors a failing assertion prints through. The empty
 *   state is `CLEAN` and not a bit, so a cleared record is a statement rather than a
 *   default;
 *
 *   the **classifier**: what changing a *property name* costs this build's layout.
 *   Driven over real spellings -- `width`, `padding-left`, `background-color`,
 *   `border-style`, `cursor`, `WIDTH` -- because the question is what a stylesheet
 *   author writes, and the two answers that matter are the ones a naive table gets
 *   wrong: `border-style: none` is a *geometry* change (10.11's used width is zero
 *   unless the style draws), and an unknown property is *nothing* rather than
 *   everything;
 *
 *   the **policy**: three marking doors and the shape each one makes. Every case
 *   asserts the dirty set box for box, because the phase's claim is not "something was
 *   invalidated" but "exactly these boxes were" -- a build that marked the document
 *   would pass every weaker assertion here;
 *
 *   the **pass contract**: a recomputation clears the layout bits and leaves STYLE, a
 *   pass with no viewport clears nothing, and no box survives a pass holding a stale
 *   layout bit;
 *
 *   the **decision**: `robo_layout_context_run_incremental` skips when nothing needs
 *   layout and runs when something does, and the four terms of that decision are each
 *   exercised on their own;
 *
 *   and the **outcome on real geometry**: a stacked document whose middle box grows,
 *   the incremental pass that follows, and the three statements the brief asks for --
 *   the changed box moved, the following flow moved with it, and the box before it did
 *   not.
 *
 * The fixture is 10.9's through 10.13's: real computed styles built through 9.8's own
 * makers, borrowed by a tree built by the real builder, and boxes resolved **by tag**
 * so that no case depends on arena order. What is new here is `layout_inv_style_set_px`,
 * which rewrites one declaration **on the style a built tree already borrows** -- the
 * only way to change what a pass will read without rebuilding the tree, and therefore
 * the only way the incremental door can be tested at all.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fixture that can declare px lengths                               */
/* ------------------------------------------------------------------ */

#define LAYOUT_INV_MAX_DECLARATIONS 12u
#define LAYOUT_INV_MAX_RULES 8u

typedef struct layout_inv_decl {
    const char *name;
    double px;
} layout_inv_decl_t;

typedef struct layout_inv_rule {
    const char *tag;
    robo_css_display_t display;
    layout_inv_decl_t declarations[LAYOUT_INV_MAX_DECLARATIONS];
    size_t declarations_count;
} layout_inv_rule_t;

static void layout_inv_rule_init(layout_inv_rule_t *rule,
                                 const char *tag,
                                 robo_css_display_t display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static void layout_inv_rule_px(layout_inv_rule_t *rule, const char *name, double px)
{
    ROBO_CHECK_EQ_INT(rule->declarations_count < LAYOUT_INV_MAX_DECLARATIONS, 1);
    rule->declarations[rule->declarations_count].name = name;
    rule->declarations[rule->declarations_count].px = px;
    rule->declarations_count++;
}

typedef struct layout_inv_style_slot {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
    size_t answers;
} layout_inv_style_slot_t;

typedef struct layout_inv_source {
    layout_inv_style_slot_t rules[LAYOUT_INV_MAX_RULES];
    size_t count;
} layout_inv_source_t;

static robo_css_computed_style_t *layout_inv_style_build(const layout_inv_rule_t *rule)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;
    size_t index;

    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_display(&value, rule->display, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("display"), 0u,
                                                       &specified, &value, NULL),
                      ROBO_OK);
    for (index = 0u; index < rule->declarations_count; index++) {
        value = (robo_css_computed_value_t)ROBO_CSS_CLEARED;
        specified = (robo_css_value_t)ROBO_CSS_CLEARED;
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value,
                                                             rule->declarations[index].px,
                                                             ROBO_CSS_UNIT_PX, 0, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(
                              style, ROBO_CSS_PROPERTY_ID_NONE,
                              robo_css_slice_from_text(rule->declarations[index].name), 0u,
                              &specified, &value, NULL),
                          ROBO_OK);
    }
    return style;
}

static void layout_inv_source_add(layout_inv_source_t *source, const layout_inv_rule_t *rule)
{
    size_t slot;

    layout_fixture();
    ROBO_CHECK_EQ_INT(source->count < LAYOUT_INV_MAX_RULES, 1);
    slot = source->count;
    strncpy(source->rules[slot].tag, rule->tag, sizeof(source->rules[slot].tag) - 1u);
    source->rules[slot].tag[sizeof(source->rules[slot].tag) - 1u] = '\0';
    source->rules[slot].style = layout_inv_style_build(rule);
    source->rules[slot].answers = 0u;
    source->count++;
}

/** The slot @p tag is answered from, or NULL. */
static layout_inv_style_slot_t *layout_inv_source_slot(layout_inv_source_t *source,
                                                       const char *tag)
{
    size_t index;

    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            return &source->rules[index];
        }
    }
    return NULL;
}

static const robo_css_computed_style_t *layout_inv_style_lookup(
    void *userdata,
    const robo_html_node_handle_t *element)
{
    layout_inv_source_t *source = (layout_inv_source_t *)userdata;
    const char *name;
    layout_inv_style_slot_t *slot;

    if (!source || !element) {
        return NULL;
    }
    name = robo_html_element_name(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    slot = layout_inv_source_slot(source, name);
    if (!slot) {
        return NULL;
    }
    slot->answers++;
    return slot->style;
}

typedef struct layout_inv_fixture {
    layout_document_t doc;
    layout_inv_source_t source;
    robo_layout_style_source_t styles;
    robo_layout_tree_t *tree;
} layout_inv_fixture_t;

static void layout_inv_begin(layout_inv_fixture_t *fixture,
                             const char *html,
                             const layout_inv_rule_t *rules,
                             size_t rule_count)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    for (index = 0u; index < rule_count; index++) {
        layout_inv_source_add(&fixture->source, &rules[index]);
    }
    robo_layout_style_source_init(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.computed_style_for = layout_inv_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_inv_end(layout_inv_fixture_t *fixture)
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
 * Rewrite one px declaration **on the style a built tree already borrows**.
 *
 * The whole reason this suite can test the incremental door: a caller that replaced the
 * style and rebuilt the tree would invalidate everything by construction, and the
 * decision under test would never be reached. Here the borrowed pointer stays valid and
 * only what the next pass *reads* changes.
 */
static void layout_inv_style_set_px(layout_inv_fixture_t *fixture,
                                    const char *tag,
                                    const char *name,
                                    double px)
{
    layout_inv_style_slot_t *slot = layout_inv_source_slot(&fixture->source, tag);
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;

    ROBO_CHECK(slot != NULL);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, px, ROBO_CSS_UNIT_PX, 0, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(
                          slot->style, ROBO_CSS_PROPERTY_ID_NONE,
                          robo_css_slice_from_text(name), 0u, &specified, &value, NULL),
                      ROBO_OK);
}

#define LAYOUT_INV_VIEWPORT_WIDTH 800
#define LAYOUT_INV_VIEWPORT_HEIGHT 600

static robo_layout_context_t *layout_inv_context(int32_t viewport_width)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;

    viewport.width = viewport_width;
    viewport.height = LAYOUT_INV_VIEWPORT_HEIGHT;
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    return context;
}

/** One full pass, with the context thrown away -- what most cases want. */
static void layout_inv_pass(layout_inv_fixture_t *fixture, robo_layout_stats_t *out_stats)
{
    robo_layout_context_t *context = layout_inv_context(LAYOUT_INV_VIEWPORT_WIDTH);
    robo_layout_stats_t stats;

    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture->tree, NULL), ROBO_OK);
    robo_layout_context_stats(context, &stats);
    robo_layout_context_destroy(context);
    if (out_stats) {
        *out_stats = stats;
    }
}

static robo_layout_node_id_t layout_inv_box_for(layout_inv_fixture_t *fixture,
                                                const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);
    robo_layout_node_id_t id;

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&element), 1);
    id = robo_layout_object_id_for_dom_node(fixture->tree, &element);
    ROBO_CHECK(id != ROBO_LAYOUT_NODE_ID_NONE);
    return id;
}

static uint32_t layout_inv_state_of(layout_inv_fixture_t *fixture, const char *tag)
{
    return robo_layout_tree_invalidation_state(fixture->tree, layout_inv_box_for(fixture, tag));
}

static robo_layout_invalidation_reason_t layout_inv_reason_of(layout_inv_fixture_t *fixture,
                                                              const char *tag)
{
    return robo_layout_tree_invalidation_reason(fixture->tree,
                                                layout_inv_box_for(fixture, tag));
}

/* ------------------------------------------------------------------ */
/* Vocabulary                                                          */
/* ------------------------------------------------------------------ */

static void test_inv_vocabulary(void)
{
    /*
     * The bits are a partition and the partition is the phase: STYLE is not part of
     * either of the two wider masks, and LAYOUT_BITS is a superset of RELAYOUT_BITS with
     * OVERFLOW as the difference. A build that changed one of these would pass every
     * "is it dirty?" assertion in this file and be wrong about *what*.
     */
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_INVALIDATION_CLEAN, 0);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_INVALIDATION_ALL,
                      ROBO_LAYOUT_INVALIDATION_STYLE | ROBO_LAYOUT_INVALIDATION_LAYOUT |
                          ROBO_LAYOUT_INVALIDATION_SUBTREE | ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS & ROBO_LAYOUT_INVALIDATION_STYLE, 0);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_INVALIDATION_OWN_BITS &
                          (ROBO_LAYOUT_INVALIDATION_SUBTREE | ROBO_LAYOUT_INVALIDATION_OVERFLOW),
                      0);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_INVALIDATION_OWN_BITS & ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS,
                      ROBO_LAYOUT_INVALIDATION_LAYOUT);

    ROBO_CHECK_EQ_INT(robo_layout_invalidation_state_is_known(ROBO_LAYOUT_INVALIDATION_ALL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_state_is_known(ROBO_LAYOUT_INVALIDATION_CLEAN), 1);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_state_is_known(
                          ROBO_LAYOUT_INVALIDATION_LAYOUT | ROBO_LAYOUT_INVALIDATION_SUBTREE),
                      1);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_state_is_known(1u << 7), 0);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_state_is_known(
                          ROBO_LAYOUT_INVALIDATION_ALL | (1u << 7)),
                      0);

    ROBO_CHECK_EQ_INT(strcmp(robo_layout_invalidation_state_name(ROBO_LAYOUT_INVALIDATION_CLEAN),
                             "clean"),
                      0);
    ROBO_CHECK_EQ_INT(strcmp(robo_layout_invalidation_state_name(
                                 ROBO_LAYOUT_INVALIDATION_LAYOUT | ROBO_LAYOUT_INVALIDATION_SUBTREE),
                             "layout+subtree"),
                      0);
    ROBO_CHECK_EQ_INT(strcmp(robo_layout_invalidation_state_name(ROBO_LAYOUT_INVALIDATION_ALL),
                             "style+layout+subtree+overflow"),
                      0);
    ROBO_CHECK_EQ_INT(strcmp(robo_layout_invalidation_state_name(1u << 7), "unknown"), 0);

    ROBO_CHECK_EQ_INT(robo_layout_invalidation_reason_is_known(ROBO_LAYOUT_INVALIDATION_REASON_NONE),
                      1);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_reason_is_known(
                          (robo_layout_invalidation_reason_t)ROBO_LAYOUT_INVALIDATION_REASON_COUNT),
                      0);
    ROBO_CHECK_EQ_INT(strcmp(robo_layout_invalidation_reason_name(
                                 ROBO_LAYOUT_INVALIDATION_REASON_PROPAGATED),
                             "propagated"),
                      0);
    ROBO_CHECK_EQ_INT(strcmp(robo_layout_invalidation_reason_name(
                                 (robo_layout_invalidation_reason_t)99),
                             "unknown"),
                      0);

    ROBO_CHECK_EQ_INT(robo_layout_style_effect_is_known(ROBO_LAYOUT_STYLE_EFFECT_NONE), 1);
    ROBO_CHECK_EQ_INT(robo_layout_style_effect_is_known(ROBO_LAYOUT_STYLE_EFFECT_LAYOUT), 1);
    ROBO_CHECK_EQ_INT(robo_layout_style_effect_is_known(
                          (robo_layout_style_effect_t)ROBO_LAYOUT_STYLE_EFFECT_COUNT),
                      0);
    ROBO_CHECK_EQ_INT(strcmp(robo_layout_style_effect_name(ROBO_LAYOUT_STYLE_EFFECT_PAINT),
                             "paint"),
                      0);
    ROBO_CHECK_EQ_INT(strcmp(robo_layout_style_effect_name((robo_layout_style_effect_t)42),
                             "unknown"),
                      0);
}

/* ------------------------------------------------------------------ */
/* The classifier                                                      */
/* ------------------------------------------------------------------ */

static void test_inv_classifier(void)
{
    /*
     * Geometry, in each of the four families that feed it: the box's own size, the box
     * model, the border family, and what decides where contents go.
     */
    static const char *geometry_names[] = {
        "display", "position", "top", "right", "bottom", "left",
        "width", "height", "min-width", "max-width", "min-height", "max-height",
        "box-sizing", "margin", "margin-left", "padding", "padding-left",
        "border", "border-width", "border-left-width", "border-style", "border-left-style",
        "overflow", "overflow-x", "overflow-y",
        "font-family", "font-size", "font-weight", "line-height", "letter-spacing",
        "word-spacing", "direction", "writing-mode"
    };
    static const char *paint_names[] = {
        "color", "background", "background-color", "background-image",
        "border-color", "border-top-color", "visibility", "opacity", "text-decoration"
    };
    static const char *none_names[] = {
        "cursor", "z-index", "transition", "content", "vertical-align", "text-transform"
    };
    size_t index;

    for (index = 0u; index < sizeof(geometry_names) / sizeof(geometry_names[0]); index++) {
        ROBO_CHECK_EQ_INT(robo_layout_style_effect_for_property(geometry_names[index]),
                          ROBO_LAYOUT_STYLE_EFFECT_LAYOUT);
    }
    for (index = 0u; index < sizeof(paint_names) / sizeof(paint_names[0]); index++) {
        ROBO_CHECK_EQ_INT(robo_layout_style_effect_for_property(paint_names[index]),
                          ROBO_LAYOUT_STYLE_EFFECT_PAINT);
    }
    for (index = 0u; index < sizeof(none_names) / sizeof(none_names[0]); index++) {
        ROBO_CHECK_EQ_INT(robo_layout_style_effect_for_property(none_names[index]),
                          ROBO_LAYOUT_STYLE_EFFECT_NONE);
    }

    /*
     * The border split, stated once more where a reader will look for it: the *width*
     * and the *style* are geometry (10.11 gives a width of zero to `none`, and to
     * `hidden`), and the *colour* is not. A table that grouped all twelve longhands
     * would be wrong in the direction this phase exists to prevent.
     */
    ROBO_CHECK_EQ_INT(robo_layout_style_effect_for_property("border-style"),
                      ROBO_LAYOUT_STYLE_EFFECT_LAYOUT);
    ROBO_CHECK_EQ_INT(robo_layout_style_effect_for_property("border-color"),
                      ROBO_LAYOUT_STYLE_EFFECT_PAINT);

    /* A spelling from a document, not from a table. */
    ROBO_CHECK_EQ_INT(robo_layout_style_effect_for_property("WIDTH"),
                      ROBO_LAYOUT_STYLE_EFFECT_LAYOUT);
    ROBO_CHECK_EQ_INT(robo_layout_style_effect_for_property("Padding-Left"),
                      ROBO_LAYOUT_STYLE_EFFECT_LAYOUT);
    ROBO_CHECK_EQ_INT(robo_layout_style_effect_for_property("Background-Color"),
                      ROBO_LAYOUT_STYLE_EFFECT_PAINT);

    /* Nothing, and not everything: an unknown name cannot have changed a conclusion
     * this module made, so it costs nothing -- and NULL is the same answer. */
    ROBO_CHECK_EQ_INT(robo_layout_style_effect_for_property("will-change"),
                      ROBO_LAYOUT_STYLE_EFFECT_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_style_effect_for_property(""),
                      ROBO_LAYOUT_STYLE_EFFECT_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_style_effect_for_property(NULL),
                      ROBO_LAYOUT_STYLE_EFFECT_NONE);
}

/* ------------------------------------------------------------------ */
/* Records                                                             */
/* ------------------------------------------------------------------ */

static void test_inv_records(void)
{
    robo_layout_invalidation_t a;
    robo_layout_invalidation_t b;
    robo_layout_invalidation_stats_t stats_a;
    robo_layout_invalidation_stats_t stats_b;

    robo_layout_invalidation_init(&a);
    ROBO_CHECK_EQ_INT((int)a.state, ROBO_LAYOUT_INVALIDATION_CLEAN);
    ROBO_CHECK_EQ_INT((int)a.reason, ROBO_LAYOUT_INVALIDATION_REASON_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_is_clean(&a), 1);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_state(&a), ROBO_LAYOUT_INVALIDATION_CLEAN);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_reason(&a),
                      ROBO_LAYOUT_INVALIDATION_REASON_NONE);

    /* NULL reads as the cleared record in every door, because "nobody said" and
     * "nothing is stale" are the same statement here. */
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_is_clean(NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_state(NULL), ROBO_LAYOUT_INVALIDATION_CLEAN);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_reason(NULL),
                      ROBO_LAYOUT_INVALIDATION_REASON_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_equal(&a, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_equal(NULL, NULL), 1);

    b = a;
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_equal(&a, &b), 1);
    b.state = ROBO_LAYOUT_INVALIDATION_STYLE;
    b.reason = ROBO_LAYOUT_INVALIDATION_REASON_PAINT;
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_equal(&a, &b), 0);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_is_clean(&b), 0);

    robo_layout_invalidation_stats_init(&stats_a);
    ROBO_CHECK_EQ_INT(stats_a.invalidated, 0u);
    ROBO_CHECK_EQ_INT(stats_a.style_only, 0u);
    ROBO_CHECK_EQ_INT((int)stats_a.root, (int)ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT((int)stats_a.reason, ROBO_LAYOUT_INVALIDATION_REASON_NONE);
    ROBO_CHECK_EQ_INT(stats_a.recomputed, 0);

    stats_b = stats_a;
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_stats_equal(&stats_a, &stats_b), 1);
    stats_b.recomputed = 1;
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_stats_equal(&stats_a, &stats_b), 0);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_stats_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_stats_equal(&stats_a, NULL), 0);
}

/* ------------------------------------------------------------------ */
/* A freshly built tree                                                */
/* ------------------------------------------------------------------ */

static void test_inv_fresh_tree(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[4];
    size_t objects;

    layout_fixture();

    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "main", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[3], "div", ROBO_CSS_DISPLAY_BLOCK);

    layout_inv_begin(&fixture,
                     "<html><body><main><div></div></main></body></html>",
                     rules, 4u);

    /*
     * Every box a build creates owes a recomputation, and it is the *tree* that says so
     * rather than a caller. The count is the whole object set, so a document that grew a
     * box cannot have grown a clean one.
     */
    objects = robo_layout_tree_object_count(fixture.tree);
    ROBO_CHECK(objects > 0u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(fixture.tree), objects);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_REASON_CONTENT);

    /* The shallowest box carrying a relayout bit is the root box: a document nobody has
     * laid out is a document a relayout would have to start from the top. */
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(fixture.tree),
                      (int)robo_layout_tree_root(fixture.tree));

    layout_inv_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pass contract                                                   */
/* ------------------------------------------------------------------ */

static void test_inv_pass_clears_layout_bits(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[3];
    robo_layout_stats_t stats;
    size_t objects;
    size_t index;
    size_t dirty = 0u;

    layout_fixture();

    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);

    layout_inv_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    objects = robo_layout_tree_object_count(fixture.tree);

    layout_inv_pass(&fixture, &stats);

    ROBO_CHECK_EQ_INT(stats.invalidation.recomputed, 1);
    ROBO_CHECK_EQ_INT(stats.invalidation.invalidated, objects);
    ROBO_CHECK_EQ_INT((int)stats.invalidation.root, (int)robo_layout_tree_root(fixture.tree));
    ROBO_CHECK_EQ_INT((int)stats.invalidation.reason,
                      ROBO_LAYOUT_INVALIDATION_REASON_CONTENT);

    /* Nothing survives the pass holding a layout bit, and the count agrees with a walk
     * rather than with itself. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(fixture.tree), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_style_only_count(fixture.tree), 0u);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(fixture.tree),
                      (int)ROBO_LAYOUT_NODE_ID_NONE);

    for (index = 0u; index < objects; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, index, &id), ROBO_OK);
        if (robo_layout_tree_invalidation_state(fixture.tree, id) != ROBO_LAYOUT_INVALIDATION_CLEAN) {
            dirty++;
        }
    }
    ROBO_CHECK_EQ_INT(dirty, 0u);

    layout_inv_end(&fixture);
}

static void test_inv_pass_without_viewport_clears_nothing(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[3];
    robo_layout_context_t *context = NULL;
    robo_layout_stats_t stats;
    size_t objects;

    layout_fixture();

    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);

    layout_inv_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    objects = robo_layout_tree_object_count(fixture.tree);

    /*
     * The pass that computed no coordinate may not claim any conclusion was recomputed.
     * Requirement 11's stale-clean state is exactly what a build that cleared the bits
     * here would produce -- and it would be invisible until a later pass skipped work
     * that had never been done.
     */
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
    robo_layout_context_stats(context, &stats);
    robo_layout_context_destroy(context);

    ROBO_CHECK_EQ_INT(stats.invalidation.recomputed, 0);
    ROBO_CHECK_EQ_INT(stats.invalidation.invalidated, objects);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(fixture.tree), objects);

    layout_inv_end(&fixture);
}

static void test_inv_mark_recomputed_leaves_style(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[3];
    robo_layout_node_id_t div;

    layout_fixture();

    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);

    layout_inv_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_inv_pass(&fixture, NULL);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);

    div = layout_inv_box_for(&fixture, "div");
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style(fixture.tree, div,
                                                 ROBO_LAYOUT_STYLE_EFFECT_PAINT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"), ROBO_LAYOUT_INVALIDATION_STYLE);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(fixture.tree), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_invalidation_style_only_count(fixture.tree), 1u);

    /*
     * And the clearing a pass performs is the narrow one: the layout bits go, the
     * repaint request stays. That pair is the whole reason STYLE is a separate bit.
     */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_recomputed(fixture.tree), ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"), ROBO_LAYOUT_INVALIDATION_STYLE);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 0);

    /* And the full reset takes it. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_clean(fixture.tree), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"), ROBO_LAYOUT_INVALIDATION_CLEAN);
    ROBO_CHECK_EQ_INT((int)layout_inv_reason_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_REASON_NONE);

    layout_inv_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The three policies                                                  */
/* ------------------------------------------------------------------ */

/*
 * The fixture the propagation cases share:
 *
 *   html
 *     body
 *       main
 *         div        <- what most cases invalidate
 *         section
 *       footer       <- main's following sibling, so the ancestor walk is exercised
 *
 * `div`'s following siblings are `section`, and `main`'s are `footer`, which is what
 * makes the transitive rule observable in two steps rather than in one.
 */
static void layout_inv_tree_rules(layout_inv_rule_t *rules)
{
    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "main", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[3], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[4], "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[5], "footer", ROBO_CSS_DISPLAY_BLOCK);
}

#define LAYOUT_INV_TREE_HTML \
    "<html><body><main><div></div><section></section></main><footer></footer></body></html>"

static void test_inv_layout_dirty_propagation(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[6];
    robo_layout_node_id_t div;

    layout_fixture();
    layout_inv_tree_rules(rules);
    layout_inv_begin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    layout_inv_pass(&fixture, NULL);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);

    div = layout_inv_box_for(&fixture, "div");
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(fixture.tree, div,
                                                        ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY,
                                                        NULL),
                      ROBO_OK);

    /* The box itself owes all three: its placement, its contents, and its measurement. */
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY);

    /* Its following sibling is the flow after it: a full re-derivation, propagated. */
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "section"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "section"),
                      ROBO_LAYOUT_INVALIDATION_REASON_PROPAGATED);

    /* Its ancestors are told their subtree and their measurement changed -- and
     * *not* that they must be placed again, because an ancestor's origin is computed
     * from its own containing block. */
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "main"),
                      ROBO_LAYOUT_INVALIDATION_SUBTREE | ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "body"),
                      ROBO_LAYOUT_INVALIDATION_SUBTREE | ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "html"),
                      ROBO_LAYOUT_INVALIDATION_SUBTREE | ROBO_LAYOUT_INVALIDATION_OVERFLOW);

    /* And the transitively following flow: `footer` is not a sibling of `div`, it is the
     * sibling of `div`'s ancestor -- which is the rule a one-deep version would miss. */
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "footer"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "footer"),
                      ROBO_LAYOUT_INVALIDATION_REASON_PROPAGATED);

    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(fixture.tree), 6u);
    /*
     * And the region, which is not `div` and not the root box either. Three boxes need
     * placing -- `div`, `section` and `footer` -- and no box below `body` contains all
     * three, because `footer` is `main`'s sibling rather than its descendant. So the
     * smallest subtree a relayout could start from is `body`'s, and that is the answer:
     * the phase reports the region rather than the leaf, because the region is what a pass
     * would actually have to redo.
     */
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(fixture.tree),
                      (int)layout_inv_box_for(&fixture, "body"));
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "body"),
                      ROBO_LAYOUT_INVALIDATION_REASON_PROPAGATED);

    layout_inv_end(&fixture);
}

static void test_inv_position_dirty_is_narrow(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[6];
    robo_layout_node_id_t div;

    layout_fixture();
    layout_inv_tree_rules(rules);
    layout_inv_begin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    layout_inv_pass(&fixture, NULL);

    div = layout_inv_box_for(&fixture, "div");
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_position_dirty(fixture.tree, div, NULL), ROBO_OK);

    /* The box and everything it contains: a displacement moves the contents with it. */
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_REASON_POSITION);

    /* Ancestors: measurement only. They are not told their subtree changed, because the
     * box still occupies the space flow gave it -- 10.12's contract, in one assertion. */
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "main"), ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "body"), ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "html"), ROBO_LAYOUT_INVALIDATION_OVERFLOW);

    /*
     * And the following flow is left **clean**: a sibling's placement is computed from
     * the flow position, which did not move. This is the assertion the brief's "must not
     * unnecessarily recompute unrelated normal-flow sizing" turns into.
     */
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "section"), ROBO_LAYOUT_INVALIDATION_CLEAN);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "footer"), ROBO_LAYOUT_INVALIDATION_CLEAN);

    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(fixture.tree), 4u);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(fixture.tree), (int)div);

    layout_inv_end(&fixture);
}

static void test_inv_overflow_dirty_is_narrowest(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[6];
    robo_layout_node_id_t div;
    size_t index;
    size_t objects;
    size_t relayout = 0u;

    layout_fixture();
    layout_inv_tree_rules(rules);
    layout_inv_begin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    layout_inv_pass(&fixture, NULL);

    div = layout_inv_box_for(&fixture, "div");
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_overflow_dirty(fixture.tree, div, NULL), ROBO_OK);

    /*
     * A mode is not a size: the box's geometry is correct, so nothing carries a relayout
     * bit -- no LAYOUT anywhere and no SUBTREE anywhere -- while the box and its
     * ancestors all owe a re-measurement. The pass still has to run (a boundary moved),
     * which is why `needs_layout` and `root` are two different questions.
     */
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"), ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "main"), ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "html"), ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "section"), ROBO_LAYOUT_INVALIDATION_CLEAN);

    objects = robo_layout_tree_object_count(fixture.tree);
    for (index = 0u; index < objects; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
        uint32_t state;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, index, &id), ROBO_OK);
        state = robo_layout_tree_invalidation_state(fixture.tree, id);
        if ((state & ROBO_LAYOUT_INVALIDATION_OWN_BITS) != 0u) {
            relayout++;
        }
    }
    ROBO_CHECK_EQ_INT(relayout, 0u);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(fixture.tree),
                      (int)ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 1);

    layout_inv_end(&fixture);
}

static void test_inv_subtree_dirty(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[6];
    robo_layout_node_id_t main_box;

    layout_fixture();
    layout_inv_tree_rules(rules);
    layout_inv_begin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    layout_inv_pass(&fixture, NULL);

    main_box = layout_inv_box_for(&fixture, "main");
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_subtree_dirty(fixture.tree, main_box,
                                                          ROBO_LAYOUT_INVALIDATION_REASON_CONTENT,
                                                          NULL),
                      ROBO_OK);

    /* The box and every descendant, with the *explicit* reason -- not the propagated
     * one, which is what makes "something was rebuilt here" readable. */
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "main"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "main"),
                      ROBO_LAYOUT_INVALIDATION_REASON_CONTENT);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_REASON_CONTENT);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "section"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);

    /* Ancestors and the following flow, exactly as a box-model change reaches them. */
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "body"),
                      ROBO_LAYOUT_INVALIDATION_SUBTREE | ROBO_LAYOUT_INVALIDATION_OVERFLOW);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "footer"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);

    /* `footer` is stale too, and it is a sibling of `main`, so the region is one level
     * above the box the mark was made on. */
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(fixture.tree),
                      (int)layout_inv_box_for(&fixture, "body"));

    layout_inv_end(&fixture);
}

static void test_inv_style_effects(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[6];
    robo_layout_node_id_t div;

    layout_fixture();
    layout_inv_tree_rules(rules);
    layout_inv_begin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    layout_inv_pass(&fixture, NULL);
    div = layout_inv_box_for(&fixture, "div");

    /* NONE marks nothing at all -- the answer that keeps an unread property from costing
     * a document's geometry. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style(fixture.tree, div,
                                                 ROBO_LAYOUT_STYLE_EFFECT_NONE, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);

    /* PAINT marks the box and nobody else: a colour is not inherited by a *box's*
     * geometry and moves nothing. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style(fixture.tree, div,
                                                 ROBO_LAYOUT_STYLE_EFFECT_PAINT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"), ROBO_LAYOUT_INVALIDATION_STYLE);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "main"), ROBO_LAYOUT_INVALIDATION_CLEAN);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "section"), ROBO_LAYOUT_INVALIDATION_CLEAN);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "html"), ROBO_LAYOUT_INVALIDATION_CLEAN);

    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_clean(fixture.tree), ROBO_OK);

    /* LAYOUT is the full policy, reached through the name door. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style_property(fixture.tree, div, "padding-left",
                                                           NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "section"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);

    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_clean(fixture.tree), ROBO_OK);

    /* And the classifier's own spelling, mixed case, through the same door. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style_property(fixture.tree, div, "Background-Color",
                                                           NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"), ROBO_LAYOUT_INVALIDATION_STYLE);

    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_clean(fixture.tree), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style_property(fixture.tree, div, "cursor", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);

    layout_inv_end(&fixture);
}

static void test_inv_mark_all(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[6];
    size_t objects;

    layout_fixture();
    layout_inv_tree_rules(rules);
    layout_inv_begin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    layout_inv_pass(&fixture, NULL);
    objects = robo_layout_tree_object_count(fixture.tree);

    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_all(fixture.tree,
                                                ROBO_LAYOUT_INVALIDATION_REASON_VIEWPORT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(fixture.tree), objects);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "section"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "section"),
                      ROBO_LAYOUT_INVALIDATION_REASON_VIEWPORT);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(fixture.tree),
                      (int)robo_layout_tree_root(fixture.tree));

    /* The reason is validated, so a typo cannot travel through the pipeline looking like
     * a fact about what happened. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_all(
                          fixture.tree, (robo_layout_invalidation_reason_t)99, NULL),
                      ROBO_INVALID_ARGUMENT);

    layout_inv_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Reason rule and idempotence                                         */
/* ------------------------------------------------------------------ */

static void test_inv_reason_rule(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[3];
    robo_layout_node_id_t div;

    layout_fixture();
    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_inv_pass(&fixture, NULL);
    div = layout_inv_box_for(&fixture, "div");

    /*
     * A propagated reason never replaces an explicit one, and an explicit one replaces
     * "something below you". Both directions are asserted here because the interesting
     * failure is the second: a subtree mark arriving after a geometry change would
     * otherwise erase the answer to "what happened?".
     */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style(fixture.tree, div,
                                                 ROBO_LAYOUT_STYLE_EFFECT_LAYOUT, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY);

    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_position_dirty(fixture.tree, div, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY);

    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_clean(fixture.tree), ROBO_OK);

    /* Propagated first, explicit second: the explicit cause is the one a reader wants. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_subtree_dirty(fixture.tree, div,
                                                         ROBO_LAYOUT_INVALIDATION_REASON_CONTENT,
                                                         NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "html"),
                      ROBO_LAYOUT_INVALIDATION_REASON_PROPAGATED);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(fixture.tree,
                                                        layout_inv_box_for(&fixture, "html"),
                                                        ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY,
                                                        NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "html"),
                      ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY);

    layout_inv_end(&fixture);
}

static void test_inv_idempotence(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[3];
    robo_layout_node_id_t div;

    layout_fixture();
    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_inv_pass(&fixture, NULL);
    div = layout_inv_box_for(&fixture, "div");

    /* Three marks, one box: the count is a count of dirty boxes and not of marks, which
     * is the property that makes it a read rather than a running total to keep in step. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(fixture.tree, div,
                                                        ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY,
                                                        NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(fixture.tree), 3u); /* div, body, html */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(fixture.tree, div,
                                                        ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY,
                                                        NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(fixture.tree, div,
                                                        ROBO_LAYOUT_INVALIDATION_REASON_CONTENT,
                                                        NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(fixture.tree), 3u);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_reason_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY);

    /* And marking clean twice is the same as marking it once. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_clean(fixture.tree), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_clean(fixture.tree), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);

    layout_inv_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Refusals                                                            */
/* ------------------------------------------------------------------ */

static void test_inv_refusals(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[3];
    robo_layout_node_id_t div;
    robo_layout_node_id_t past;

    layout_fixture();
    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_inv_pass(&fixture, NULL);

    div = layout_inv_box_for(&fixture, "div");
    past = ROBO_LAYOUT_NODE_ID_NONE;
    {
        robo_layout_node_id_t last = ROBO_LAYOUT_NODE_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(
                              fixture.tree,
                              robo_layout_tree_object_count(fixture.tree) - 1u, &last),
                          ROBO_OK);
        past = last + 100u;
    }

    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(
                          fixture.tree, past, ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_subtree_dirty(
                          fixture.tree, past, ROBO_LAYOUT_INVALIDATION_REASON_CONTENT, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_position_dirty(fixture.tree, past, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_overflow_dirty(fixture.tree, past, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style(fixture.tree, past,
                                                 ROBO_LAYOUT_STYLE_EFFECT_LAYOUT, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style_property(fixture.tree, past, "width", NULL),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(
                          fixture.tree, div, (robo_layout_invalidation_reason_t)99, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style(
                          fixture.tree, div, (robo_layout_style_effect_t)99, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(
                          NULL, div, ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style(NULL, div, ROBO_LAYOUT_STYLE_EFFECT_PAINT, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* A refused mark leaves the tree exactly as it was: nothing was invalidated by a call
     * that failed. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);

    /* And reading about an id that names nothing is `clean`, not an error -- the same
     * convention every flag read in this module uses. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_state(NULL, div), ROBO_LAYOUT_INVALIDATION_CLEAN);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_reason(fixture.tree, past),
                      ROBO_LAYOUT_INVALIDATION_REASON_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(NULL), 0u);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(NULL),
                      (int)ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_clean(NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_recomputed(NULL), ROBO_OK);

    layout_inv_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The dirty root                                                      */
/* ------------------------------------------------------------------ */

static void test_inv_root_is_the_common_ancestor(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[6];
    robo_layout_node_id_t div;
    robo_layout_node_id_t body;

    layout_fixture();
    layout_inv_tree_rules(rules);
    layout_inv_begin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    layout_inv_pass(&fixture, NULL);

    div = layout_inv_box_for(&fixture, "div");
    body = layout_inv_box_for(&fixture, "body");

    /*
     * A mark with **nothing after it** answers the box itself: `footer` is the last box in
     * the document, so no sibling anywhere up the chain is dragged along and the only box
     * that needs placing is the one that was marked.
     *
     * (`section` would not do here, and the reason is worth writing down: it is the last
     * child of `main`, but `main` itself has a following sibling, so marking `section`
     * reaches `footer` -- the ancestor walk is what makes "the last child" insufficient.)
     */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(
                          fixture.tree, layout_inv_box_for(&fixture, "footer"),
                          ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(fixture.tree),
                      (int)layout_inv_box_for(&fixture, "footer"));

    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_clean(fixture.tree), ROBO_OK);

    /*
     * And one whose damage spans branches answers their common ancestor. Marking `div`
     * dirties `section` (its sibling) and `footer` (its ancestor's sibling), and no box
     * below `body` contains `footer` along with `div` -- so the region is `body`, not the
     * shallower `footer` and not the leaf that changed.
     */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(fixture.tree, div,
                                                        ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY,
                                                        NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "footer"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(fixture.tree), (int)body);

    /* And a mark on the root box answers the root box. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_clean(fixture.tree), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(
                          fixture.tree, robo_layout_tree_root(fixture.tree),
                          ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(fixture.tree),
                      (int)robo_layout_tree_root(fixture.tree));

    layout_inv_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The incremental decision                                            */
/* ------------------------------------------------------------------ */

static void test_inv_incremental_skips_a_clean_tree(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[3];
    robo_layout_context_t *context;
    robo_layout_stats_t first;
    robo_layout_stats_t second;
    robo_layout_result_t before;
    robo_layout_result_t after;
    robo_layout_node_id_t div;

    layout_fixture();
    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

    context = layout_inv_context(LAYOUT_INV_VIEWPORT_WIDTH);

    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context), (int)ROBO_LAYOUT_PASS_WALKED);
    robo_layout_context_stats(context, &first);
    ROBO_CHECK_EQ_INT(first.invalidation.recomputed, 1);

    div = layout_inv_box_for(&fixture, "div");
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, div, &before), ROBO_OK);

    /*
     * Nothing changed, nothing was asked for: the second call is the same door and it does
     * no work. The outcome says so, `visited` is 0 because nothing was walked, and
     * `passes` still advances -- a pass that concluded "nothing to do" is a pass.
     */
    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context), (int)ROBO_LAYOUT_PASS_CLEAN);
    robo_layout_context_stats(context, &second);
    ROBO_CHECK_EQ_INT(second.invalidation.recomputed, 0);
    ROBO_CHECK_EQ_INT(second.invalidation.invalidated, 0u);
    ROBO_CHECK_EQ_INT(second.visited, 0u);
    ROBO_CHECK_EQ_INT(second.passes, 2u);
    ROBO_CHECK(second.objects > 0u);

    /* And the results a caller is holding are still current, which is the point. */
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, div, &after), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_result_equal(&before, &after), 1);

    robo_layout_context_destroy(context);
    layout_inv_end(&fixture);
}

static void test_inv_incremental_runs_after_a_mark(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[6];
    robo_layout_context_t *context;
    robo_layout_stats_t stats;
    robo_layout_node_id_t div;

    layout_fixture();
    layout_inv_tree_rules(rules);
    layout_inv_begin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);

    context = layout_inv_context(LAYOUT_INV_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);

    div = layout_inv_box_for(&fixture, "div");
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(fixture.tree, div,
                                                        ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY,
                                                        NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context), (int)ROBO_LAYOUT_PASS_WALKED);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.invalidation.recomputed, 1);
    ROBO_CHECK_EQ_INT(stats.invalidation.invalidated, 6u);
    /* The region's root, not the leaf: `div`'s damage reaches `footer`, which is
     * shallower, so the common ancestor is `body` -- and its reason is the propagated one,
     * which is the truth about it (the region was not marked by a cause, it was told). */
    ROBO_CHECK_EQ_INT((int)stats.invalidation.root,
                      (int)layout_inv_box_for(&fixture, "body"));
    ROBO_CHECK_EQ_INT((int)stats.invalidation.reason,
                      ROBO_LAYOUT_INVALIDATION_REASON_PROPAGATED);

    /* The pass consumed what it found. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);

    robo_layout_context_destroy(context);
    layout_inv_end(&fixture);
}

static void test_inv_incremental_runs_when_inputs_change(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[3];
    robo_layout_context_t *context;
    robo_layout_stats_t stats;
    robo_layout_size_t viewport;

    layout_fixture();
    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

    context = layout_inv_context(LAYOUT_INV_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context), (int)ROBO_LAYOUT_PASS_CLEAN);

    /*
     * The viewport is not part of the tree, so the model above cannot see it move -- the
     * context records that an *input* changed, and that is what makes the third call run
     * even though every box is clean. The conservative direction is deliberate: the same
     * size re-recorded costs one pass rather than a comparison of two floats.
     */
    viewport.width = LAYOUT_INV_VIEWPORT_WIDTH / 2;
    viewport.height = LAYOUT_INV_VIEWPORT_HEIGHT;
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context), (int)ROBO_LAYOUT_PASS_WALKED);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.invalidation.recomputed, 1);
    ROBO_CHECK_EQ_INT(stats.invalidation.invalidated, 0u);

    /* And a metrics source is an input for the same reason, in both directions. */
    ROBO_CHECK_EQ_INT(robo_layout_context_set_text_metrics(context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context), (int)ROBO_LAYOUT_PASS_WALKED);

    /* A different tree is the fourth term: a context has no results for it. */
    {
        layout_inv_fixture_t other;
        layout_inv_rule_t other_rules[3];

        layout_inv_rule_init(&other_rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
        layout_inv_rule_init(&other_rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
        layout_inv_rule_init(&other_rules[2], "p", ROBO_CSS_DISPLAY_BLOCK);
        layout_inv_begin(&other, "<html><body><p></p></body></html>", other_rules, 3u);

        ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, other.tree, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context), (int)ROBO_LAYOUT_PASS_WALKED);
        layout_inv_end(&other);
    }

    robo_layout_context_destroy(context);
    layout_inv_end(&fixture);
}

static void test_inv_incremental_skips_a_paint_only_change(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[3];
    robo_layout_context_t *context;
    robo_layout_stats_t stats;
    robo_layout_result_t before;
    robo_layout_result_t after;
    robo_layout_node_id_t div;

    layout_fixture();
    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

    context = layout_inv_context(LAYOUT_INV_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);

    div = layout_inv_box_for(&fixture, "div");
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, div, &before), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style_property(fixture.tree, div,
                                                          "background-color", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 0);

    /*
     * Requirement 13, in three assertions: a colour change is *not clean* and *does not
     * need layout*, so the pass is skipped -- and the repaint request survives the call
     * rather than being silently consumed by a pass that never ran.
     */
    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context), (int)ROBO_LAYOUT_PASS_CLEAN);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.invalidation.recomputed, 0);
    ROBO_CHECK_EQ_INT(stats.invalidation.style_only, 1u);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"), ROBO_LAYOUT_INVALIDATION_STYLE);
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, div, &after), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_result_equal(&before, &after), 1);

    robo_layout_context_destroy(context);
    layout_inv_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Real geometry: the brief's stacked-block scenario                   */
/* ------------------------------------------------------------------ */

static void test_inv_relayout_moves_the_following_flow(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[6];
    robo_layout_context_t *context;
    robo_layout_stats_t stats;
    robo_layout_result_t p_before;
    robo_layout_result_t p_after;
    robo_layout_result_t div_before;
    robo_layout_result_t div_after;
    robo_layout_result_t section_before;
    robo_layout_result_t section_after;
    robo_layout_node_id_t p;
    robo_layout_node_id_t div;
    robo_layout_node_id_t section;

    layout_fixture();

    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "main", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[3], "p", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_px(&rules[3], "height", 20.0);
    layout_inv_rule_init(&rules[4], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_px(&rules[4], "height", 40.0);
    /*
     * `section` needs a rule of its own, and the reason is worth stating: an element no
     * rule answers for gets **no style**, so its display is CSS's initial `inline` and it
     * is inline-level in a block context -- placed by nobody, `UNRESOLVED`, and a fixture
     * bug rather than a layout one. The three boxes here are the brief's
     * normal/relative/after, so all three are declared.
     */
    layout_inv_rule_init(&rules[5], "section", ROBO_CSS_DISPLAY_BLOCK);

    layout_inv_begin(&fixture,
                     "<html><body><main><p></p><div></div><section></section></main></body></html>",
                     rules, 6u);

    context = layout_inv_context(LAYOUT_INV_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);

    p = layout_inv_box_for(&fixture, "p");
    div = layout_inv_box_for(&fixture, "div");
    section = layout_inv_box_for(&fixture, "section");

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, p, &p_before), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, div, &div_before), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, section, &section_before), ROBO_OK);

    /* The stack: 20, then 40, then whatever follows. */
    ROBO_CHECK_EQ_INT(div_before.position.y, 20);
    ROBO_CHECK_EQ_INT(section_before.position.y, 60);
    ROBO_CHECK_EQ_INT(section_before.position.y - div_before.position.y, 40);

    /*
     * The middle box grows by 20. Its declaration is rewritten **on the style the tree
     * already borrows**, so the same tree is laid out again -- which is what makes the
     * incremental decision, and the policy behind it, reachable at all.
     */
    layout_inv_style_set_px(&fixture, "div", "height", 60.0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style_property(fixture.tree, div, "height", NULL),
                      ROBO_OK);

    /* The dirty set, before the pass runs: the box, and the flow after it. `p` -- the box
     * *before* it -- is untouched, which is the assertion a document-wide mark fails. */
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "div"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "section"),
                      ROBO_LAYOUT_INVALIDATION_LAYOUT_BITS);
    ROBO_CHECK_EQ_INT(layout_inv_state_of(&fixture, "p"), ROBO_LAYOUT_INVALIDATION_CLEAN);

    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context), (int)ROBO_LAYOUT_PASS_WALKED);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.invalidation.recomputed, 1);
    /* The changed box is `div` and its following sibling `section` moved: the region that
     * contains both is `main`, `div`'s parent -- which is the smallest subtree a relayout
     * could have started from, and therefore the honest answer. */
    ROBO_CHECK_EQ_INT((int)stats.invalidation.root,
                      (int)layout_inv_box_for(&fixture, "main"));

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, p, &p_after), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, div, &div_after), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, section, &section_after), ROBO_OK);

    /* 1. the changed box grew and did not move; 2. the flow after it moved by the delta;
     * 3. the box before it did not change at all. */
    ROBO_CHECK_EQ_INT(div_after.size.height, 60);
    ROBO_CHECK_EQ_INT(div_after.position.y, 20);
    ROBO_CHECK_EQ_INT(section_after.position.y, 80);
    ROBO_CHECK_EQ_INT(section_after.position.y - div_after.position.y, 60);
    ROBO_CHECK_EQ_INT(robo_layout_result_equal(&p_before, &p_after), 1);

    /* And no stale bit survives the pass, on any box. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(fixture.tree), 0u);

    robo_layout_context_destroy(context);
    layout_inv_end(&fixture);
}

static void test_inv_relayout_is_stable(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[4];
    robo_layout_context_t *context;
    robo_layout_result_t results[6];
    robo_layout_result_t repeat[6];
    size_t count;
    size_t index;
    int round;

    layout_fixture();

    layout_inv_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[2], "main", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_init(&rules[3], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_inv_rule_px(&rules[3], "height", 30.0);

    layout_inv_begin(&fixture, "<html><body><main><div></div></main></body></html>", rules, 4u);
    context = layout_inv_context(LAYOUT_INV_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);

    count = robo_layout_tree_object_count(fixture.tree);
    ROBO_CHECK(count <= 6u);
    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, index, &id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &results[index]), ROBO_OK);
    }

    /*
     * Invalidate, layout, invalidate, layout -- four times -- with the same input each
     * round. The geometry must be equal to the first round's box for box and the tree must
     * be clean at the end of every round: a cumulative pass would drift here, and a
     * leaky clearing would leave a bit behind on the third.
     */
    for (round = 0; round < 4; round++) {
        robo_layout_node_id_t div = layout_inv_box_for(&fixture, "div");
        robo_layout_stats_t stats;

        ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style_property(fixture.tree, div, "height", NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context), (int)ROBO_LAYOUT_PASS_WALKED);
        robo_layout_context_stats(context, &stats);
        ROBO_CHECK_EQ_INT(stats.invalidation.recomputed, 1);
        ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);
    }

    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, index, &id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &repeat[index]), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_result_equal(&results[index], &repeat[index]), 1);
    }

    robo_layout_context_destroy(context);
    layout_inv_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Determinism and allocation                                          */
/* ------------------------------------------------------------------ */

static void test_inv_determinism(void)
{
    layout_inv_fixture_t first;
    layout_inv_fixture_t second;
    layout_inv_rule_t rules[6];
    layout_inv_rule_t twin_rules[6];
    size_t index;

    layout_fixture();
    layout_inv_tree_rules(rules);
    layout_inv_tree_rules(twin_rules);

    layout_inv_begin(&first, LAYOUT_INV_TREE_HTML, rules, 6u);
    layout_inv_begin(&second, LAYOUT_INV_TREE_HTML, twin_rules, 6u);
    layout_inv_pass(&first, NULL);
    layout_inv_pass(&second, NULL);

    /*
     * One marking script, run over two trees built from one document, compared box for box
     * and bit for bit -- state, reason, counts and root. Anything read from an address, an
     * arena's history or an iteration order would disagree here.
     */
    {
        static const char *const tags[] = { "div", "section", "footer", "main", "html" };
        size_t t;

        for (t = 0u; t < sizeof(tags) / sizeof(tags[0]); t++) {
            robo_layout_node_id_t a = layout_inv_box_for(&first, tags[t]);
            robo_layout_node_id_t b = layout_inv_box_for(&second, tags[t]);

            if (t % 2u == 0u) {
                ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(
                                      first.tree, a, ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY,
                                      NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(
                                      second.tree, b, ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY,
                                      NULL),
                                  ROBO_OK);
            } else {
                ROBO_CHECK_EQ_INT(robo_layout_tree_mark_position_dirty(second.tree, b, NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT(robo_layout_tree_mark_position_dirty(first.tree, a, NULL),
                                  ROBO_OK);
            }
        }
    }

    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(first.tree),
                      robo_layout_tree_invalidation_count(second.tree));
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(first.tree),
                      (int)robo_layout_tree_invalidation_root(second.tree));
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(first.tree),
                      robo_layout_tree_needs_layout(second.tree));

    for (index = 0u; index < robo_layout_tree_object_count(first.tree); index++) {
        robo_layout_node_id_t a = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_node_id_t b = ROBO_LAYOUT_NODE_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(first.tree, index, &a), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(second.tree, index, &b), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_state(first.tree, a),
                          robo_layout_tree_invalidation_state(second.tree, b));
        ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_reason(first.tree, a),
                          (int)robo_layout_tree_invalidation_reason(second.tree, b));
    }

    layout_inv_end(&first);
    layout_inv_end(&second);
}

static void test_inv_allocator_balance(void)
{
    layout_inv_fixture_t fixture;
    layout_inv_rule_t rules[6];
    size_t baseline;
    size_t index;

    layout_fixture();
    baseline = layout_live_blocks();

    layout_inv_tree_rules(rules);
    layout_inv_begin(&fixture, LAYOUT_INV_TREE_HTML, rules, 6u);
    for (index = 0u; index < 8u; index++) {
        robo_layout_stats_t stats;

        if (index % 2u == 0u) {
            robo_layout_node_id_t div = layout_inv_box_for(&fixture, "div");

            ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(
                                  fixture.tree, div, ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY,
                                  NULL),
                              ROBO_OK);
        } else {
            robo_layout_node_id_t section = layout_inv_box_for(&fixture, "section");

            ROBO_CHECK_EQ_INT(robo_layout_tree_mark_overflow_dirty(fixture.tree, section, NULL),
                              ROBO_OK);
        }
        layout_inv_pass(&fixture, &stats);
    }
    layout_inv_end(&fixture);

    /* Nothing in this phase allocates per mark or per pass, which is the whole reason the
     * record lives on the box and the count lives on the tree. */
    ROBO_CHECK_EQ_INT(layout_live_blocks(), baseline);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_layout_invalidation(void)
{
    robo_test_register("layout.invalidation.vocabulary", test_inv_vocabulary);
    robo_test_register("layout.invalidation.classifier", test_inv_classifier);
    robo_test_register("layout.invalidation.records", test_inv_records);
    robo_test_register("layout.invalidation.fresh_tree", test_inv_fresh_tree);
    robo_test_register("layout.invalidation.pass_clears_layout_bits",
                       test_inv_pass_clears_layout_bits);
    robo_test_register("layout.invalidation.pass_without_viewport_clears_nothing",
                       test_inv_pass_without_viewport_clears_nothing);
    robo_test_register("layout.invalidation.mark_recomputed_leaves_style",
                       test_inv_mark_recomputed_leaves_style);
    robo_test_register("layout.invalidation.layout_dirty_propagation",
                       test_inv_layout_dirty_propagation);
    robo_test_register("layout.invalidation.position_dirty_is_narrow",
                       test_inv_position_dirty_is_narrow);
    robo_test_register("layout.invalidation.overflow_dirty_is_narrowest",
                       test_inv_overflow_dirty_is_narrowest);
    robo_test_register("layout.invalidation.subtree_dirty", test_inv_subtree_dirty);
    robo_test_register("layout.invalidation.style_effects", test_inv_style_effects);
    robo_test_register("layout.invalidation.mark_all", test_inv_mark_all);
    robo_test_register("layout.invalidation.reason_rule", test_inv_reason_rule);
    robo_test_register("layout.invalidation.idempotence", test_inv_idempotence);
    robo_test_register("layout.invalidation.refusals", test_inv_refusals);
    robo_test_register("layout.invalidation.root_is_the_common_ancestor",
                       test_inv_root_is_the_common_ancestor);
    robo_test_register("layout.invalidation.incremental_skips_a_clean_tree",
                       test_inv_incremental_skips_a_clean_tree);
    robo_test_register("layout.invalidation.incremental_runs_after_a_mark",
                       test_inv_incremental_runs_after_a_mark);
    robo_test_register("layout.invalidation.incremental_runs_when_inputs_change",
                       test_inv_incremental_runs_when_inputs_change);
    robo_test_register("layout.invalidation.incremental_skips_a_paint_only_change",
                       test_inv_incremental_skips_a_paint_only_change);
    robo_test_register("layout.invalidation.relayout_moves_the_following_flow",
                       test_inv_relayout_moves_the_following_flow);
    robo_test_register("layout.invalidation.relayout_is_stable", test_inv_relayout_is_stable);
    robo_test_register("layout.invalidation.determinism", test_inv_determinism);
    robo_test_register("layout.invalidation.allocator_balance", test_inv_allocator_balance);
}
