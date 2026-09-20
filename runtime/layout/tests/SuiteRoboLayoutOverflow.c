/*
 * RoboLayout overflow suite (Phase 10.13) — layout.overflow.*
 *
 * Five things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary** -- an axis record with three kinds (`absent`, `resolved`,
 *   `unresolved`) over two axes, and an *extent* record that is four non-negative
 *   numbers measured from the padding box. The zero of the axis vocabulary is
 *   `ABSENT`, so a cleared record holds CSS's initial state rather than a claim that
 *   somebody wrote `visible`;
 *
 *   the **seam**: what a computed style declared. The longhand first, the `overflow`
 *   shorthand only for an axis the longhand said nothing about -- which is why
 *   `overflow-x: visible; overflow: hidden` clips vertically and *not* horizontally,
 *   and why a declaration this build cannot use arrives as `UNRESOLVED` rather than as
 *   an absence;
 *
 *   the **policy**: the three rules driven over fabricated records, so a wrong
 *   counter fails at the record rather than three boxes later in an extent;
 *
 *   the **arithmetic**: four per-side *maxima* (not a union), the region that is the
 *   padding box grown by them, and the clip rectangle that is the padding box. The
 *   distinction that makes the recursion work -- a child contributes its **region**,
 *   so a child that clips contributes only what it kept -- is asserted at both ends:
 *   on the fabricated records here, and on a three-level document below;
 *
 *   the **pass**: a box that keeps the size its declarations asked for while the
 *   region its contents reached is recorded *beside* it. The witness is the pair of
 *   assertions every single-box test could pass without honouring -- the box's
 *   `size` is unchanged and its **ancestor** did not grow -- because the most
 *   plausible way to get this phase wrong is to fold an overflow into a size.
 *
 * The scripts here are *computed styles*, built through 9.8's makers exactly as the
 * sizing, margin, padding, border and position suites build them, because a fabricated
 * struct would prove the pass reads a struct rather than a computed CSS value. The
 * value is 10.13's own OVERFLOW kind, and `scroll` -- a real CSS overflow value this
 * build has not added -- arrives as a *keyword*, which is exactly how the seam meets
 * an unusable declaration.
 *
 * Elements are resolved **by tag** (`layout_overflow_box_for`) rather than by arena
 * index, which is the lesson 10.8's suite wrote down, so the tags here are distinct
 * and one scripted style per element is enough.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fabricated style that can declare anything                        */
/* ------------------------------------------------------------------ */

/** One declaration to put on a fabricated style. */
typedef enum layout_overflow_decl_kind {
    LAYOUT_OVERFLOW_DECL_PX = 0,
    /** A px length **above** the style seam's maximum, for the saturation cases. */
    LAYOUT_OVERFLOW_DECL_HUGE_PX = 1,
    /** A px length that is negative: legal for an offset, refused for an extent. */
    LAYOUT_OVERFLOW_DECL_NEGATIVE_PX = 2,
    /** A px length flagged unresolved, which is what a `var()` arrives as. */
    LAYOUT_OVERFLOW_DECL_UNRESOLVED_PX = 3,
    /**
     * An identifier. `scroll` and `auto` are real CSS overflow values this build has
     * not added, and they arrive at the seam as this -- a keyword the overflow reader
     * cannot classify -- which is the phase's `UNRESOLVED` case.
     */
    LAYOUT_OVERFLOW_DECL_KEYWORD = 4,
    /** A `robo_css_computed_value_t` of kind OVERFLOW: 10.13's typed vocabulary. */
    LAYOUT_OVERFLOW_DECL_OVERFLOW = 5,
    /** A `robo_css_computed_value_t` of kind POSITION: 9.13's, for the relative cases. */
    LAYOUT_OVERFLOW_DECL_POSITION = 6,
    /** A typed `border-*-style`, which is what makes a declared width a *used* one. */
    LAYOUT_OVERFLOW_DECL_BORDER_STYLE = 7
} layout_overflow_decl_kind_t;

typedef struct layout_overflow_decl {
    const char *name;
    layout_overflow_decl_kind_t kind;
    double number;
    const char *keyword;
    robo_css_overflow_t overflow;
    robo_css_positioning_t position;
    robo_css_border_style_t border_style;
} layout_overflow_decl_t;

#define LAYOUT_OVERFLOW_MAX_DECLARATIONS 24u
#define LAYOUT_OVERFLOW_MAX_RULES 8u

typedef struct layout_overflow_rule {
    const char *tag;
    robo_css_display_t display;
    layout_overflow_decl_t declarations[LAYOUT_OVERFLOW_MAX_DECLARATIONS];
    size_t declarations_count;
} layout_overflow_rule_t;

static void layout_overflow_rule_init(layout_overflow_rule_t *rule,
                                      const char *tag,
                                      robo_css_display_t display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static layout_overflow_decl_t *layout_overflow_rule_add(layout_overflow_rule_t *rule,
                                                        const char *name,
                                                        layout_overflow_decl_kind_t kind)
{
    layout_overflow_decl_t *declaration;

    ROBO_CHECK_EQ_INT(rule->declarations_count < LAYOUT_OVERFLOW_MAX_DECLARATIONS, 1);
    declaration = &rule->declarations[rule->declarations_count];
    memset(declaration, 0, sizeof(*declaration));
    declaration->name = name;
    declaration->kind = kind;
    declaration->overflow = ROBO_CSS_OVERFLOW_VISIBLE;
    declaration->position = ROBO_CSS_POSITIONING_STATIC;
    rule->declarations_count++;
    return declaration;
}

static void layout_overflow_rule_px(layout_overflow_rule_t *rule, const char *name, double px)
{
    layout_overflow_rule_add(rule, name, LAYOUT_OVERFLOW_DECL_PX)->number = px;
}

static void layout_overflow_rule_keyword(layout_overflow_rule_t *rule,
                                         const char *name,
                                         const char *keyword)
{
    layout_overflow_rule_add(rule, name, LAYOUT_OVERFLOW_DECL_KEYWORD)->keyword = keyword;
}

static void layout_overflow_rule_overflow(layout_overflow_rule_t *rule,
                                          const char *name,
                                          robo_css_overflow_t overflow)
{
    layout_overflow_rule_add(rule, name, LAYOUT_OVERFLOW_DECL_OVERFLOW)->overflow = overflow;
}

static void layout_overflow_rule_position(layout_overflow_rule_t *rule,
                                          robo_css_positioning_t mode)
{
    layout_overflow_rule_add(rule, "position", LAYOUT_OVERFLOW_DECL_POSITION)->position = mode;
}

static void layout_overflow_rule_border_style(layout_overflow_rule_t *rule,
                                              const char *name,
                                              robo_css_border_style_t style)
{
    layout_overflow_rule_add(rule, name, LAYOUT_OVERFLOW_DECL_BORDER_STYLE)->border_style =
        style;
}

/** `border-width: Npx solid`, all four sides -- the shorthand this build has no expansion for. */
static void layout_overflow_rule_border_all(layout_overflow_rule_t *rule, double px)
{
    layout_overflow_rule_px(rule, "border-top-width", px);
    layout_overflow_rule_px(rule, "border-right-width", px);
    layout_overflow_rule_px(rule, "border-bottom-width", px);
    layout_overflow_rule_px(rule, "border-left-width", px);
    layout_overflow_rule_border_style(rule, "border-top-style", ROBO_CSS_BORDER_STYLE_SOLID);
    layout_overflow_rule_border_style(rule, "border-right-style", ROBO_CSS_BORDER_STYLE_SOLID);
    layout_overflow_rule_border_style(rule, "border-bottom-style", ROBO_CSS_BORDER_STYLE_SOLID);
    layout_overflow_rule_border_style(rule, "border-left-style", ROBO_CSS_BORDER_STYLE_SOLID);
}

/** `padding: Npx`, four longhands. */
static void layout_overflow_rule_padding_all(layout_overflow_rule_t *rule, double px)
{
    layout_overflow_rule_px(rule, "padding-top", px);
    layout_overflow_rule_px(rule, "padding-right", px);
    layout_overflow_rule_px(rule, "padding-bottom", px);
    layout_overflow_rule_px(rule, "padding-left", px);
}

typedef struct layout_overflow_style_slot {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
    size_t answers;
} layout_overflow_style_slot_t;

typedef struct layout_overflow_source {
    layout_overflow_style_slot_t rules[LAYOUT_OVERFLOW_MAX_RULES];
    size_t count;
} layout_overflow_source_t;

/**
 * Build the style @p rule describes: a real DISPLAY value plus whatever the rule
 * declares, all through 9.8's own makers and container.
 *
 * `display` is set even when the rule declares nothing else, because 9.12's initial
 * value is `inline` and a fixture that wanted a block box would otherwise be asserting
 * a document made of inline boxes.
 */
static robo_css_computed_style_t *layout_overflow_style_build(const layout_overflow_rule_t *rule)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;
    size_t index;

    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_display(&value, rule->display, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("display"), 0u,
                                                       &specified, &value, NULL),
                      ROBO_OK);

    for (index = 0u; index < rule->declarations_count; index++) {
        const layout_overflow_decl_t *declaration = &rule->declarations[index];

        value = (robo_css_computed_value_t)ROBO_CSS_CLEARED;
        specified = (robo_css_value_t)ROBO_CSS_CLEARED;
        switch (declaration->kind) {
        case LAYOUT_OVERFLOW_DECL_OVERFLOW:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_overflow(&value, declaration->overflow,
                                                                    NULL),
                              ROBO_OK);
            break;
        case LAYOUT_OVERFLOW_DECL_POSITION:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_position(&value, declaration->position,
                                                                    NULL),
                              ROBO_OK);
            break;
        case LAYOUT_OVERFLOW_DECL_BORDER_STYLE:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_border_style(
                                  &value, declaration->border_style, NULL),
                              ROBO_OK);
            break;
        case LAYOUT_OVERFLOW_DECL_KEYWORD:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                                  &value, robo_css_slice_from_text(declaration->keyword), NULL),
                              ROBO_OK);
            break;
        case LAYOUT_OVERFLOW_DECL_UNRESOLVED_PX:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, declaration->number,
                                                                 ROBO_CSS_UNIT_PX, 0, NULL),
                              ROBO_OK);
            /* The flag the computed layer sets for a `var()` or a `calc()`: carried
             * intact and not a value *yet*, whatever its kind says. */
            value.unresolved = 1;
            break;
        case LAYOUT_OVERFLOW_DECL_HUGE_PX:
        case LAYOUT_OVERFLOW_DECL_NEGATIVE_PX:
        case LAYOUT_OVERFLOW_DECL_PX:
        default:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, declaration->number,
                                                                 ROBO_CSS_UNIT_PX, 0, NULL),
                              ROBO_OK);
            break;
        }
        ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(
                              style, ROBO_CSS_PROPERTY_ID_NONE,
                              robo_css_slice_from_text(declaration->name), 0u, &specified, &value,
                              NULL),
                          ROBO_OK);
    }
    return style;
}

/** Add @p rule to @p source, and answer the style it built. */
static robo_css_computed_style_t *layout_overflow_style_add(layout_overflow_source_t *source,
                                                            const layout_overflow_rule_t *rule)
{
    size_t slot;
    robo_css_computed_style_t *style;

    layout_fixture();
    ROBO_CHECK_EQ_INT(source->count < LAYOUT_OVERFLOW_MAX_RULES, 1);
    slot = source->count;
    style = layout_overflow_style_build(rule);
    strncpy(source->rules[slot].tag, rule->tag, sizeof(source->rules[slot].tag) - 1u);
    source->rules[slot].tag[sizeof(source->rules[slot].tag) - 1u] = '\0';
    source->rules[slot].style = style;
    source->rules[slot].answers = 0u;
    source->count++;
    return style;
}

/**
 * Rebuild the style @p source answers @p tag with, **in the same slot**.
 *
 * Replacing rather than appending, and that is not tidiness: the source answers the
 * *first* row whose tag matches, so an appended second row would be shadowed by the one
 * it was meant to replace and the case would assert the old declaration while appearing
 * to have changed it. The rule is 10.9's, 10.10's, 10.11's and 10.12's.
 */
static void layout_overflow_style_replace(layout_overflow_source_t *source,
                                          const char *tag,
                                          const layout_overflow_rule_t *rule)
{
    size_t index;

    layout_fixture();
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            robo_css_computed_style_destroy(source->rules[index].style);
            source->rules[index].style = layout_overflow_style_build(rule);
            return;
        }
    }
    ROBO_CHECK_MSG(0, "the tag to replace is not in this source");
}

static const robo_css_computed_style_t *layout_overflow_style_lookup(
    void *userdata,
    const robo_html_node_handle_t *element)
{
    layout_overflow_source_t *source = (layout_overflow_source_t *)userdata;
    const char *name;
    size_t index;

    if (!source || !element) {
        return NULL;
    }
    name = robo_html_element_name(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, name) == 0) {
            source->rules[index].answers++;
            return source->rules[index].style;
        }
    }
    /* An element nothing was declared for: `display: inline` and two undeclared
     * overflow axes, which are CSS's initial values rather than a fixture default. */
    return NULL;
}

typedef struct layout_overflow_fixture {
    layout_document_t doc;
    layout_overflow_source_t source;
    robo_layout_style_source_t styles;
    robo_layout_tree_t *tree;
} layout_overflow_fixture_t;

static void layout_overflow_begin(layout_overflow_fixture_t *fixture,
                                  const char *html,
                                  const layout_overflow_rule_t *rules,
                                  size_t rule_count)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    for (index = 0u; index < rule_count; index++) {
        (void)layout_overflow_style_add(&fixture->source, &rules[index]);
    }
    robo_layout_style_source_init(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.computed_style_for = layout_overflow_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_overflow_end(layout_overflow_fixture_t *fixture)
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

#define LAYOUT_OVERFLOW_VIEWPORT_WIDTH 800
#define LAYOUT_OVERFLOW_VIEWPORT_HEIGHT 600

/** One pass at @p viewport_width. */
static void layout_overflow_pass(layout_overflow_fixture_t *fixture,
                                 int32_t viewport_width,
                                 robo_layout_stats_t *out_stats)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;

    viewport.width = viewport_width;
    viewport.height = LAYOUT_OVERFLOW_VIEWPORT_HEIGHT;
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture->tree, NULL), ROBO_OK);
    robo_layout_context_stats(context, &stats);
    robo_layout_context_destroy(context);
    if (out_stats) {
        *out_stats = stats;
    }
}

/** The layout box of the first @p tag in the fixture's document. */
static robo_layout_node_id_t layout_overflow_box_for(layout_overflow_fixture_t *fixture,
                                                     const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);
    robo_layout_node_id_t id;

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&element), 1);
    id = robo_layout_object_id_for_dom_node(fixture->tree, &element);
    ROBO_CHECK(id != ROBO_LAYOUT_NODE_ID_NONE);
    return id;
}

/** The first object in @p fixture's tree of @p kind -- how a text run is found. */
static robo_layout_node_id_t layout_overflow_first_of_kind(layout_overflow_fixture_t *fixture,
                                                           robo_layout_object_kind_t kind)
{
    size_t total = robo_layout_tree_object_count(fixture->tree);
    size_t index;

    for (index = 0u; index < total; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

        if (robo_layout_object_id_at(fixture->tree, index, &id) != ROBO_OK) {
            break;
        }
        if (robo_layout_object_kind(fixture->tree, id) == kind) {
            return id;
        }
    }
    return ROBO_LAYOUT_NODE_ID_NONE;
}

static robo_layout_result_t layout_overflow_result_of(layout_overflow_fixture_t *fixture,
                                                     robo_layout_node_id_t id)
{
    robo_layout_result_t result;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_result_state(&result),
                      (int)ROBO_LAYOUT_RESULT_COMPUTED);
    return result;
}

static robo_layout_point_t layout_overflow_origin_of(layout_overflow_fixture_t *fixture,
                                                     robo_layout_node_id_t id)
{
    robo_layout_result_t result = layout_overflow_result_of(fixture, id);
    robo_layout_point_t position;

    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
    return position;
}

static robo_layout_size_t layout_overflow_size_of(layout_overflow_fixture_t *fixture,
                                                  robo_layout_node_id_t id)
{
    robo_layout_result_t result = layout_overflow_result_of(fixture, id);
    robo_layout_size_t size;

    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
    return size;
}

/** The record @p id was measured with, through the public door. */
static robo_layout_overflow_edges_t layout_overflow_edges_of(
    layout_overflow_fixture_t *fixture,
    robo_layout_node_id_t id)
{
    robo_layout_overflow_edges_t edges;

    ROBO_CHECK_EQ_INT(robo_layout_object_overflow(fixture->tree, id, &edges, NULL), ROBO_OK);
    return edges;
}

/** The box's **padding box**, in its own space: the border box less the borders. */
static robo_layout_rect_t layout_overflow_padding_box_of(layout_overflow_fixture_t *fixture,
                                                        robo_layout_node_id_t id)
{
    robo_layout_size_t size = layout_overflow_size_of(fixture, id);
    robo_layout_border_edges_t borders;
    robo_layout_rect_t rect;

    ROBO_CHECK_EQ_INT(robo_layout_object_borders(fixture->tree, id, &borders, NULL), ROBO_OK);
    rect.x = 0;
    rect.y = 0;
    rect.width = size.width - borders.left.width - borders.right.width;
    rect.height = size.height - borders.top.width - borders.bottom.width;
    return rect;
}

/**
 * Rebuild the layout tree against the styles @p fixture was built with.
 *
 * The step a changed declaration needs and the reason it is not optional: the tree
 * **borrows** one computed style per element for as long as the bridge lives, so a
 * replacement that left the old tree standing would leave it holding a destroyed style.
 */
static void layout_overflow_rebuild(layout_overflow_fixture_t *fixture)
{
    robo_layout_build_options_t options;

    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static int layout_overflow_axis_count_of(const robo_layout_overflow_edges_t *edges)
{
    return (int)(edges->axes_resolved + edges->axes_unresolved);
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void test_overflow_kind_vocabulary(void)
{
    size_t index;

    layout_fixture();

    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_OVERFLOW_KIND_COUNT, 3);
    /*
     * The zero of the vocabulary is `ABSENT`, and that is a decision rather than a
     * numbering accident: a cleared record then holds the axis's *initial state*, so a
     * forgotten initialisation is not a claim that somebody declared `visible`. The
     * same decision the margin, padding, border and position vocabularies made.
     */
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_OVERFLOW_ABSENT, 0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_OVERFLOW_RESOLVED, 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_OVERFLOW_UNRESOLVED, 2);

    for (index = 0u; index < ROBO_LAYOUT_OVERFLOW_KIND_COUNT; index++) {
        robo_layout_overflow_kind_t kind = (robo_layout_overflow_kind_t)index;

        ROBO_CHECK_EQ_INT(robo_layout_overflow_kind_is_known(kind), 1);
        ROBO_CHECK(robo_layout_overflow_kind_name(kind) != NULL);
    }
    ROBO_CHECK_EQ_INT(robo_layout_overflow_kind_is_known((robo_layout_overflow_kind_t)3), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_kind_is_known((robo_layout_overflow_kind_t)77), 0);
    ROBO_CHECK_STR_EQ(robo_layout_overflow_kind_name(ROBO_LAYOUT_OVERFLOW_ABSENT), "absent");
    ROBO_CHECK_STR_EQ(robo_layout_overflow_kind_name(ROBO_LAYOUT_OVERFLOW_RESOLVED), "resolved");
    ROBO_CHECK_STR_EQ(robo_layout_overflow_kind_name(ROBO_LAYOUT_OVERFLOW_UNRESOLVED),
                      "unresolved");
    ROBO_CHECK_STR_EQ(robo_layout_overflow_kind_name((robo_layout_overflow_kind_t)3), "unknown");
    ROBO_CHECK(robo_layout_overflow_kind_name((robo_layout_overflow_kind_t)77) != NULL);

    /* And the domain this phase added, spelled and counted: 21 now, with `overflow` the
     * twentieth -- the first domain in this module about what happens when the box was
     * the wrong size. */
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_COUNT, 21);
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(ROBO_LAYOUT_DOMAIN_OVERFLOW),
                      "overflow");
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_OVERFLOW,
                      (int)ROBO_LAYOUT_DOMAIN_COUNT - 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_POSITION,
                      (int)ROBO_LAYOUT_DOMAIN_OVERFLOW - 1);
}

static void test_overflow_axis_vocabulary(void)
{
    size_t index;

    layout_fixture();

    /* Two, and named for the direction rather than for a letter, because every door
     * takes one and a build that indexed the pair by 0 and 1 would make `clips(1)` a
     * question about the document's writing conventions. */
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_OVERFLOW_AXIS_COUNT, 2);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL, 0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL, 1);

    for (index = 0u; index < ROBO_LAYOUT_OVERFLOW_AXIS_COUNT; index++) {
        robo_layout_overflow_axis_id_t axis = (robo_layout_overflow_axis_id_t)index;

        ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_known(axis), 1);
        ROBO_CHECK(robo_layout_overflow_axis_name(axis) != NULL);
    }
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_known((robo_layout_overflow_axis_id_t)2), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_known((robo_layout_overflow_axis_id_t)91), 0);
    ROBO_CHECK_STR_EQ(robo_layout_overflow_axis_name(ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL),
                      "horizontal");
    ROBO_CHECK_STR_EQ(robo_layout_overflow_axis_name(ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL),
                      "vertical");
    ROBO_CHECK_STR_EQ(robo_layout_overflow_axis_name((robo_layout_overflow_axis_id_t)2),
                      "unknown");
    ROBO_CHECK(robo_layout_overflow_axis_name((robo_layout_overflow_axis_id_t)91) != NULL);
}

static void test_overflow_axis_record(void)
{
    robo_layout_overflow_axis_t absent = robo_layout_overflow_axis_absent();
    robo_layout_overflow_axis_t visible =
        robo_layout_overflow_axis_resolved(ROBO_CSS_OVERFLOW_VISIBLE);
    robo_layout_overflow_axis_t hidden =
        robo_layout_overflow_axis_resolved(ROBO_CSS_OVERFLOW_HIDDEN);
    robo_layout_overflow_axis_t unresolved = robo_layout_overflow_axis_unresolved();
    robo_layout_overflow_axis_t same_absent = robo_layout_overflow_axis_absent();
    robo_layout_overflow_axis_t nonsensical;
    robo_css_overflow_t mode = ROBO_CSS_OVERFLOW_HIDDEN;

    layout_fixture();

    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_absent(&absent), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_resolved(&absent), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_unresolved(&absent), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_valid(&absent), 1);

    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_absent(&visible), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_resolved(&visible), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_valid(&visible), 1);

    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_resolved(&unresolved), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_unresolved(&unresolved), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_valid(&unresolved), 1);

    /* The mode door refuses the two kinds whose `mode` field means nothing, and it
     * refuses them with NOT_FOUND rather than by handing back the `visible` they happen
     * to hold -- which is a different statement from a mode that *is* `visible`. */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_mode(&hidden, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)mode, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    mode = ROBO_CSS_OVERFLOW_HIDDEN;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_mode(&absent, &mode), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)mode, (int)ROBO_CSS_OVERFLOW_HIDDEN); /* untouched */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_mode(&unresolved, &mode), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)mode, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_mode(NULL, &mode), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_mode(&hidden, NULL), ROBO_INVALID_ARGUMENT);

    /* A *kind* is what makes two records equal in the two states where the mode does
     * not mean anything: an absent axis equals another absent axis whatever stale
     * `mode` either holds. */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_equal(&absent, &same_absent), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_equal(&absent, &visible), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_equal(&visible, &hidden), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_equal(&visible, &visible), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_equal(&unresolved, &unresolved), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_equal(&absent, NULL), 0);

    /* A mode nothing defined is stored as `visible` rather than refused, and the
     * record is still a record: the door that catches a hand-built one is *_is_valid. */
    nonsensical = robo_layout_overflow_axis_resolved((robo_css_overflow_t)7);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_valid(&nonsensical), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_mode(&nonsensical, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)mode, (int)ROBO_CSS_OVERFLOW_VISIBLE);

    /* And the record rules: a kind nothing defines, or a mode nothing defines, is a
     * record this build may not read. */
    nonsensical = visible;
    nonsensical.kind = (robo_layout_overflow_kind_t)9;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_valid(&nonsensical), 0);
    nonsensical = visible;
    nonsensical.mode = (robo_css_overflow_t)9;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_valid(&nonsensical), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_absent(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_resolved(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_unresolved(NULL), 0);
}

static void test_overflow_declared_record(void)
{
    robo_layout_overflow_t declared;
    robo_layout_overflow_t other;
    robo_layout_overflow_axis_t axis;
    robo_layout_overflow_axis_t fallback;

    layout_fixture();

    robo_layout_overflow_init(&declared);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_is_valid(&declared), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_absent(&declared.horizontal), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_absent(&declared.vertical), 1);

    robo_layout_overflow_init(&other);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_equal(&declared, &other), 1);
    other.horizontal = robo_layout_overflow_axis_resolved(ROBO_CSS_OVERFLOW_HIDDEN);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_equal(&declared, &other), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_equal(&declared, NULL), 0);

    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_of(&other,
                                                   ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL, &axis),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_resolved(&axis), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_of(&other, ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL,
                                                   &axis),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_absent(&axis), 1);
    fallback = axis;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_of(&other, (robo_layout_overflow_axis_id_t)5,
                                                   &axis),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_equal(&axis, &fallback), 1); /* untouched */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_of(NULL, ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL,
                                                   &axis),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_of(&other,
                                                   ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_is_valid(NULL), 0);

    /* A NULL style declares nothing: "no style" and "no entry" are one case, exactly
     * as they are for `display`. */
    declared = robo_layout_overflow_of(NULL);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_absent(&declared.horizontal), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_absent(&declared.vertical), 1);
}

static void test_overflow_edges_record(void)
{
    robo_layout_overflow_edges_t edges;
    robo_layout_overflow_edges_t other;
    robo_layout_overflow_edges_t bad;
    int32_t extent = 0;

    layout_fixture();

    robo_layout_overflow_edges_init(&edges);
    /*
     * The cleared record is a *real* box -- `overflow: visible` with nothing sticking
     * out is the most common box in every document -- which is why the zero of the
     * mode vocabulary is a mode rather than a "no answer" enumerator.
     */
    ROBO_CHECK_EQ_INT((int)edges.horizontal, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT((int)edges.vertical, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.top, 0);
    ROBO_CHECK_EQ_INT(edges.right, 0);
    ROBO_CHECK_EQ_INT(edges.bottom, 0);
    ROBO_CHECK_EQ_INT(layout_overflow_axis_count_of(&edges), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(&edges), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow(&edges), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow_axis(
                          &edges, ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL),
                      0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow_axis(
                          &edges, ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL),
                      0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips_any(&edges), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips(
                          &edges, ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL),
                      0);
    ROBO_CHECK_EQ_INT((int)robo_layout_overflow_edges_mode(
                          &edges, ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL),
                      (int)ROBO_CSS_OVERFLOW_VISIBLE);
    /* An unknown axis answers `visible`, for the reason an unknown overflow value
     * answers "does not clip": a record nothing can read must not throw contents away. */
    ROBO_CHECK_EQ_INT((int)robo_layout_overflow_edges_mode(
                          &edges, (robo_layout_overflow_axis_id_t)3),
                      (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips(&edges,
                                                       (robo_layout_overflow_axis_id_t)3),
                      0);
    ROBO_CHECK_EQ_INT((int)robo_layout_overflow_edges_mode(
                          NULL, ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL),
                      (int)ROBO_CSS_OVERFLOW_VISIBLE);

    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_extent(&edges, ROBO_CSS_BOX_SIDE_TOP, &extent),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(extent, 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_extent(&edges, ROBO_CSS_BOX_SIDE_RIGHT,
                                                        &extent),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_extent(&edges, ROBO_CSS_BOX_SIDE_BOTTOM,
                                                        &extent),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_extent(&edges, ROBO_CSS_BOX_SIDE_LEFT, &extent),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_extent(&edges, (robo_css_box_side_t)9, &extent),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_extent(NULL, ROBO_CSS_BOX_SIDE_TOP, &extent),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_extent(&edges, ROBO_CSS_BOX_SIDE_TOP, NULL),
                      ROBO_INVALID_ARGUMENT);

    other = edges;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(&edges, &other), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(&edges, NULL), 0);
    other.right = 1;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(&edges, &other), 0);
    other = edges;
    other.horizontal = ROBO_CSS_OVERFLOW_HIDDEN;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(&edges, &other), 0);
    other = edges;
    other.axes_resolved = 1u;
    /* The *counters* are part of the record: two conclusions that agreed about the
     * geometry and disagreed about how many declarations they used would be two
     * different answers to one document. */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(&edges, &other), 0);

    other = edges;
    other.right = 12;
    other.bottom = 7;
    other.horizontal = ROBO_CSS_OVERFLOW_HIDDEN;
    other.axes_resolved = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(&other), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips_any(&other), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips(&other,
                                                       ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL),
                      0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow(&other), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow_axis(
                          &other, ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL),
                      1);
    /* Both extents are above zero, so both verbs are verbs about this record. */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow_axis(
                          &other, ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL),
                      1);

    /* Then the three records this build may not read, each for its own reason. */
    bad = other;
    bad.left = -1;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(&bad), 0);
    bad = other;
    bad.vertical = (robo_css_overflow_t)9;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(&bad), 0);
    bad = other;
    bad.axes_resolved = 5u;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(&bad), 0);
    bad = other;
    bad.axes_resolved = 1u;
    bad.axes_unresolved = 2u;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(&bad), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(NULL), 0);
}

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

static void test_overflow_seam_declarations(void)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;
    robo_layout_overflow_t declared;
    robo_layout_overflow_axis_t axis;
    robo_css_overflow_t mode = ROBO_CSS_OVERFLOW_VISIBLE;

    layout_fixture();

    /* Nothing declared: two undeclared axes, which is CSS's initial state and the
     * state in which `visible` applies because it applies rather than because
     * somebody wrote it. */
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    declared = robo_layout_overflow_of(style);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_absent(&declared.horizontal), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_absent(&declared.vertical), 1);

    /* `overflow: hidden` -- the **shorthand**, which this build reads rather than
     * expands. One declaration, two axes; that is the whole of its shorthand handling
     * and the reason the fallback lives in the reader rather than at three call
     * sites. */
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_overflow(&value, ROBO_CSS_OVERFLOW_HIDDEN,
                                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("overflow"), 0u,
                                                       &specified, &value, NULL),
                      ROBO_OK);
    declared = robo_layout_overflow_of(style);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_resolved(&declared.horizontal), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_resolved(&declared.vertical), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_mode(&declared.horizontal, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)mode, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_mode(&declared.vertical, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)mode, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    robo_css_computed_style_destroy(style);

    /* `overflow-x: visible` beside the same shorthand. The longhand speaks for its own
     * axis and the shorthand is consulted only for the other one -- which is the case
     * a build that fell back on a *resolved* `visible` would get wrong by clipping
     * both axes. */
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_overflow(&value, ROBO_CSS_OVERFLOW_VISIBLE,
                                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("overflow-x"), 0u,
                                                       &specified, &value, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_overflow(&value, ROBO_CSS_OVERFLOW_HIDDEN,
                                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("overflow"), 0u,
                                                       &specified, &value, NULL),
                      ROBO_OK);
    declared = robo_layout_overflow_of(style);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_mode(&declared.horizontal, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)mode, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_mode(&declared.vertical, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)mode, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    robo_css_computed_style_destroy(style);

    /* `overflow-y` alone: the other axis stays undeclared rather than becoming
     * `visible`-as-a-declaration. */
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_overflow(&value, ROBO_CSS_OVERFLOW_HIDDEN,
                                                            NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("overflow-y"), 0u,
                                                       &specified, &value, NULL),
                      ROBO_OK);
    declared = robo_layout_overflow_of(style);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_absent(&declared.horizontal), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_mode(&declared.vertical, &mode), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)mode, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    robo_css_computed_style_destroy(style);

    /*
     * And `overflow-x: scroll` -- a real CSS overflow value this build has not added.
     * It arrives as a *keyword*, which the overflow reader cannot classify, so the axis
     * is `UNRESOLVED` and its used mode is `visible`. Counted rather than dropped: a
     * document whose clipping depends on one should be visible as such.
     */
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                          &value, robo_css_slice_from_text("scroll"), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("overflow-x"), 0u,
                                                       &specified, &value, NULL),
                      ROBO_OK);
    declared = robo_layout_overflow_of(style);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_unresolved(&declared.horizontal), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_absent(&declared.vertical), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_mode(&declared.horizontal, &mode),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_of(&declared,
                                                   ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL, &axis),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_axis_is_unresolved(&axis), 1);
    robo_css_computed_style_destroy(style);
}

/* ------------------------------------------------------------------ */
/* The one policy                                                      */
/* ------------------------------------------------------------------ */

/** A declared record: an axis kind per side, built from the vocabulary's own makers. */
static robo_layout_overflow_t layout_overflow_declared_of(robo_layout_overflow_axis_t horizontal,
                                                          robo_layout_overflow_axis_t vertical)
{
    robo_layout_overflow_t declared;

    declared.horizontal = horizontal;
    declared.vertical = vertical;
    return declared;
}

static void test_overflow_resolve_modes(void)
{
    robo_layout_overflow_t declared;
    robo_layout_overflow_edges_t edges;
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;

    layout_fixture();

    /* Nobody declared anything: two `visible` axes, no counter moved, no extents. This
     * is the most common box in every document and it is a *stated* answer. */
    declared = layout_overflow_declared_of(robo_layout_overflow_axis_absent(),
                                          robo_layout_overflow_axis_absent());
    ROBO_CHECK_EQ_INT(robo_layout_overflow_resolve(&declared, &edges, &diag), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)edges.horizontal, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT((int)edges.vertical, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(edges.axes_resolved, 0u);
    ROBO_CHECK_EQ_INT(edges.axes_unresolved, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(&edges), 1);

    /* An axis declared `visible` is a declaration an author wrote, and it is counted:
     * "the author wrote nothing" and "the author wrote `visible`" are two declarations
     * and the counters are where the difference lives. */
    declared = layout_overflow_declared_of(
        robo_layout_overflow_axis_resolved(ROBO_CSS_OVERFLOW_VISIBLE),
        robo_layout_overflow_axis_absent());
    ROBO_CHECK_EQ_INT(robo_layout_overflow_resolve(&declared, &edges, &diag), ROBO_OK);
    ROBO_CHECK_EQ_INT(edges.axes_resolved, 1u);
    ROBO_CHECK_EQ_INT(edges.axes_unresolved, 0u);
    ROBO_CHECK_EQ_INT((int)edges.horizontal, (int)ROBO_CSS_OVERFLOW_VISIBLE);

    declared = layout_overflow_declared_of(
        robo_layout_overflow_axis_resolved(ROBO_CSS_OVERFLOW_HIDDEN),
        robo_layout_overflow_axis_resolved(ROBO_CSS_OVERFLOW_HIDDEN));
    ROBO_CHECK_EQ_INT(robo_layout_overflow_resolve(&declared, &edges, &diag), ROBO_OK);
    ROBO_CHECK_EQ_INT(edges.axes_resolved, 2u);
    ROBO_CHECK_EQ_INT(edges.axes_unresolved, 0u);
    ROBO_CHECK_EQ_INT(edges.axes_resolved + edges.axes_unresolved,
                      (size_t)ROBO_LAYOUT_OVERFLOW_AXIS_COUNT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips_any(&edges), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips(&edges,
                                                       ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL),
                      1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips(&edges,
                                                       ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL),
                      1);

    /* The mixture: one axis resolved, the other a declaration this build cannot use.
     * Its used mode is `visible` -- 10.8's rule for a dimension nothing can resolve --
     * and it is counted so that it is not invisible in the record. */
    declared = layout_overflow_declared_of(
        robo_layout_overflow_axis_resolved(ROBO_CSS_OVERFLOW_HIDDEN),
        robo_layout_overflow_axis_unresolved());
    ROBO_CHECK_EQ_INT(robo_layout_overflow_resolve(&declared, &edges, &diag), ROBO_OK);
    ROBO_CHECK_EQ_INT(edges.axes_resolved, 1u);
    ROBO_CHECK_EQ_INT(edges.axes_unresolved, 1u);
    ROBO_CHECK_EQ_INT((int)edges.horizontal, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    ROBO_CHECK_EQ_INT((int)edges.vertical, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips_any(&edges), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips(&edges,
                                                       ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL),
                      0);

    /* And a mode nothing defines cannot leak through: the resolve reads it as
     * `visible` because it is not a mode this build has. */
    declared = layout_overflow_declared_of(
        robo_layout_overflow_axis_resolved((robo_css_overflow_t)6),
        robo_layout_overflow_axis_resolved((robo_css_overflow_t)6));
    ROBO_CHECK_EQ_INT(robo_layout_overflow_resolve(&declared, &edges, &diag), ROBO_OK);
    ROBO_CHECK_EQ_INT(edges.axes_resolved, 2u);
    ROBO_CHECK_EQ_INT((int)edges.horizontal, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT((int)edges.vertical, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips_any(&edges), 0);

    ROBO_CHECK_EQ_INT(robo_layout_overflow_resolve(NULL, &edges, &diag), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_resolve(&declared, NULL, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_resolve(&declared, &edges, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* The accumulation rule                                               */
/* ------------------------------------------------------------------ */

static robo_layout_rect_t layout_overflow_rect(int32_t x, int32_t y, int32_t width,
                                               int32_t height)
{
    robo_layout_rect_t rect;

    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    return rect;
}

static void test_overflow_include_extents(void)
{
    robo_layout_overflow_edges_t edges;
    robo_layout_overflow_edges_t before;
    robo_layout_rect_t padding_box = layout_overflow_rect(0, 0, 100, 50);

    layout_fixture();
    robo_layout_overflow_edges_init(&edges);

    /* A child that reaches right and below: each side is its own number, and each one
     * is how far *past* the edge the region got. */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_include(
                          &edges, padding_box, layout_overflow_rect(0, 0, 300, 80), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.top, 0);
    ROBO_CHECK_EQ_INT(edges.right, 200);
    ROBO_CHECK_EQ_INT(edges.bottom, 30);

    /* And a child that reaches the other way, from the far edges rather than from a
     * width: the region's own coordinates place it. This one reaches left and no other
     * way, and that is the per-axis half of the same statement. */
    robo_layout_overflow_edges_init(&edges);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_include(
                          &edges, padding_box, layout_overflow_rect(-20, 10, 50, 20), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(edges.left, 20);
    ROBO_CHECK_EQ_INT(edges.top, 0);
    ROBO_CHECK_EQ_INT(edges.right, 0);
    ROBO_CHECK_EQ_INT(edges.bottom, 0);
    /* The extents are a measurement, and a measurement of a region that stayed inside
     * is zero rather than negative: contents that reached *inside* the box are a
     * different statement from contents that reached past it. */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow(&edges), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow_axis(
                          &edges, ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL),
                      1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow_axis(
                          &edges, ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL),
                      0);

    /* A **maximum**, not a union, and the difference is the second fold: the outermost
     * thing any of a box's contents reached, not the last one measured. */
    robo_layout_overflow_edges_init(&edges);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_include(
                          &edges, padding_box, layout_overflow_rect(0, 0, 150, 50), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(edges.right, 50);
    before = edges;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_include(
                          &edges, padding_box, layout_overflow_rect(0, 0, 120, 50), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(&edges, &before), 1);

    /* Idempotent for a region that was already inside, which is the same rule from the
     * other side. */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_include(
                          &edges, padding_box, layout_overflow_rect(10, 10, 20, 20), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(&edges, &before), 1);

    /* The refusals: no record, a negative size, and a record that is not one. */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_include(
                          NULL, padding_box, layout_overflow_rect(0, 0, 10, 10), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_include(
                          &edges, padding_box, layout_overflow_rect(0, 0, -1, 10), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_include(
                          &edges, layout_overflow_rect(0, 0, 10, -1),
                          layout_overflow_rect(0, 0, 10, 10), NULL),
                      ROBO_INVALID_ARGUMENT);
    before = edges;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_include(
                          &edges, padding_box, layout_overflow_rect(0, 0, 900, 900), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(&edges, &before), 0);
    edges.axes_resolved = 9u;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_include(
                          &edges, padding_box, layout_overflow_rect(0, 0, 10, 10), NULL),
                      ROBO_ERROR);
}

static void test_overflow_include_saturates(void)
{
    robo_layout_overflow_edges_t edges;
    robo_layout_rect_t padding_box = layout_overflow_rect(0, 0, 1, 1);

    layout_fixture();
    robo_layout_overflow_edges_init(&edges);

    /*
     * The terms are each bounded by the seam's maximum and a sum of two of them is not,
     * so the four maxima are performed in 64 bits and clamped. A box at the seam's
     * maximum reached by a child at the seam's maximum gets a maximum -- never a
     * wrapped negative, which is the failure this door exists to make impossible.
     */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_include(
                          &edges, padding_box,
                          layout_overflow_rect(-2147483647, -2147483647, 1, 1), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(edges.left, 2147483647);
    ROBO_CHECK_EQ_INT(edges.top, 2147483647);
    ROBO_CHECK_EQ_INT(edges.left > 0, 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(&edges), 1);

    robo_layout_overflow_edges_init(&edges);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_include(
                          &edges, padding_box,
                          layout_overflow_rect(2147483646, 2147483646, 2147483647,
                                               2147483647),
                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(edges.right, 2147483647);
    ROBO_CHECK_EQ_INT(edges.bottom, 2147483647);
    ROBO_CHECK_EQ_INT(edges.right >= 0, 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(&edges), 1);
}

/* ------------------------------------------------------------------ */
/* The region and the clip rectangle                                   */
/* ------------------------------------------------------------------ */

static void test_overflow_region_per_axis(void)
{
    robo_layout_overflow_edges_t edges;
    robo_layout_rect_t padding_box = layout_overflow_rect(0, 0, 100, 50);
    robo_layout_rect_t region;

    layout_fixture();
    robo_layout_overflow_edges_init(&edges);
    edges.right = 200;
    edges.bottom = 30;

    /* Both axes `visible`: the padding box grown by the extents -- what the contents
     * occupy in the end, and what a parent folds in. */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, padding_box, &region, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(region.x, 0);
    ROBO_CHECK_EQ_INT(region.y, 0);
    ROBO_CHECK_EQ_INT(region.width, 300);
    ROBO_CHECK_EQ_INT(region.height, 80);

    /*
     * Then one axis at a time, and this is the phase's recursion in three lines: on an
     * axis whose mode **clips**, the region is the padding box itself -- a box that
     * hides an axis keeps what it hides -- while the *record* still says how far the
     * contents reached. Two consumers, two answers, one record.
     */
    edges.horizontal = ROBO_CSS_OVERFLOW_HIDDEN;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, padding_box, &region, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(region.width, 100);
    ROBO_CHECK_EQ_INT(region.height, 80);
    ROBO_CHECK_EQ_INT(edges.right, 200); /* untouched: the record is the measurement */

    edges.horizontal = ROBO_CSS_OVERFLOW_VISIBLE;
    edges.vertical = ROBO_CSS_OVERFLOW_HIDDEN;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, padding_box, &region, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(region.width, 300);
    ROBO_CHECK_EQ_INT(region.height, 50);

    edges.horizontal = ROBO_CSS_OVERFLOW_HIDDEN;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, padding_box, &region, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(region.width, 100);
    ROBO_CHECK_EQ_INT(region.height, 50);

    /* A record with no overflow and two `visible` axes answers the padding box, which
     * is a legitimate region and not an error: the commonest box in a document. */
    robo_layout_overflow_edges_init(&edges);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, padding_box, &region, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(region.width, 100);
    ROBO_CHECK_EQ_INT(region.height, 50);

    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, padding_box, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, layout_overflow_rect(0, 0, -1, 1),
                                                        &region, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(NULL, padding_box, &region, NULL),
                      ROBO_INVALID_ARGUMENT);
    edges.axes_unresolved = 4u;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, padding_box, &region, NULL),
                      ROBO_ERROR);
}

static void test_overflow_clip_rect_refusals(void)
{
    robo_layout_overflow_edges_t edges;
    robo_layout_rect_t padding_box = layout_overflow_rect(3, 7, 100, 50);
    robo_layout_rect_t clip = layout_overflow_rect(-1, -1, -1, -1);
    robo_layout_rect_t untouched = clip;

    layout_fixture();
    robo_layout_overflow_edges_init(&edges);

    /*
     * A box with no boundary has **no clip rectangle**, and the door refuses rather
     * than answering the padding box anyway -- because answering it would tell a paint
     * pass to clip a box that paints its contents outside itself, which is the failure
     * this phase exists to prevent arriving from the other side.
     */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(&edges, padding_box, &clip, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(&edges, &edges), 1);
    ROBO_CHECK_EQ_INT(clip.width, untouched.width);
    ROBO_CHECK_EQ_INT(clip.x, untouched.x);

    edges.horizontal = ROBO_CSS_OVERFLOW_HIDDEN;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(&edges, padding_box, &clip, NULL),
                      ROBO_OK);
    /* The padding box and nothing else: CSS's own boundary for `overflow: hidden`, so
     * the door neither shrinks nor grows it. */
    ROBO_CHECK_EQ_INT(clip.x, 3);
    ROBO_CHECK_EQ_INT(clip.y, 7);
    ROBO_CHECK_EQ_INT(clip.width, 100);
    ROBO_CHECK_EQ_INT(clip.height, 50);

    edges.vertical = ROBO_CSS_OVERFLOW_HIDDEN;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(&edges, padding_box, &clip, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(clip.width, 100);

    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(NULL, padding_box, &clip, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(&edges, padding_box, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(&edges,
                                                           layout_overflow_rect(0, 0, 1, -1),
                                                           &clip, NULL),
                      ROBO_INVALID_ARGUMENT);
    edges.axes_resolved = 3u;
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(&edges, padding_box, &clip, NULL),
                      ROBO_ERROR);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

/**
 * `html` and `body` as blocks, then @p rules -- the fixture shape every pass case here
 * shares.
 *
 * Both are written out rather than left to the builder, because a box with no style
 * computes to `display: inline` (9.12's initial value) and a document made of inline
 * boxes is not the document any of these cases is about.
 */
static void layout_overflow_blocks(layout_overflow_rule_t *html_rule,
                                  layout_overflow_rule_t *body_rule)
{
    layout_overflow_rule_init(html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_init(body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
}

static void test_overflow_pass_fitting(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t div_rule;
    layout_overflow_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_node_id_t id;
    robo_layout_overflow_edges_t edges;
    robo_layout_result_t result;

    layout_fixture();

    /* A box whose contents fit: four zeroes, two `visible` axes, no declaration
     * counted, and two flags that are not set. The commonest case, and a *stated*
     * answer rather than an absent one. */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&div_rule, "width", 100.0);
    layout_overflow_rule_px(&div_rule, "height", 50.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_overflow_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, &stats);

    id = layout_overflow_box_for(&fixture, "div");
    edges = layout_overflow_edges_of(&fixture, id);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(&edges), 1);
    ROBO_CHECK_EQ_INT((int)edges.horizontal, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT((int)edges.vertical, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.top, 0);
    ROBO_CHECK_EQ_INT(edges.right, 0);
    ROBO_CHECK_EQ_INT(edges.bottom, 0);
    ROBO_CHECK_EQ_INT(edges.axes_resolved, 0u);
    ROBO_CHECK_EQ_INT(edges.axes_unresolved, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow(&edges), 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips_any(&edges), 0);

    result = layout_overflow_result_of(&fixture, id);
    ROBO_CHECK_EQ_INT(robo_layout_result_flags(&result), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_result_has_flag(
                          &result, ROBO_LAYOUT_RESULT_FLAG_OVERFLOW_X),
                      0);
    ROBO_CHECK_EQ_INT(robo_layout_result_has_flag(
                          &result, ROBO_LAYOUT_RESULT_FLAG_OVERFLOW_Y),
                      0);
    /* And the clip door agrees with the flags: there is nothing to clip to. */
    {
        robo_layout_rect_t clip;

        ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(
                              &edges, layout_overflow_padding_box_of(&fixture, id), &clip, NULL),
                          ROBO_NOT_FOUND);
    }

    ROBO_CHECK_EQ_INT(stats.block.overflow_clipped, 0u);
    ROBO_CHECK_EQ_INT(stats.block.overflowing, 0u);
    ROBO_CHECK_EQ_INT(stats.block.overflow_unresolved, 0u);

    layout_overflow_end(&fixture);
}

static void test_overflow_pass_child_right(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t outer_rule;
    layout_overflow_rule_t inner_rule;
    layout_overflow_rule_t rules[4];
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;
    robo_layout_overflow_edges_t edges;
    robo_layout_size_t size;
    robo_layout_result_t result;

    layout_fixture();

    /* A child wider than the box that holds it, and the two numbers every case here
     * turns on: the child's region reaches 200 past the parent's padding box on the
     * right, and the *parent* is still 100 wide. */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&outer_rule, "width", 100.0);
    layout_overflow_rule_px(&outer_rule, "height", 50.0);
    layout_overflow_rule_init(&inner_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&inner_rule, "width", 300.0);
    layout_overflow_rule_px(&inner_rule, "height", 20.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = outer_rule;
    rules[3] = inner_rule;

    layout_overflow_begin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = layout_overflow_box_for(&fixture, "div");
    inner = layout_overflow_box_for(&fixture, "section");

    edges = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.top, 0);
    ROBO_CHECK_EQ_INT(edges.right, 200);
    ROBO_CHECK_EQ_INT(edges.bottom, 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow_axis(
                          &edges, ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL),
                      1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow_axis(
                          &edges, ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL),
                      0);

    size = layout_overflow_size_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(size.width, 100);
    ROBO_CHECK_EQ_INT(size.height, 50);

    /* The child is a leaf: it kept its own geometry and measured nothing of its own. */
    size = layout_overflow_size_of(&fixture, inner);
    ROBO_CHECK_EQ_INT(size.width, 300);
    ROBO_CHECK_EQ_INT(size.height, 20);
    edges = layout_overflow_edges_of(&fixture, inner);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow(&edges), 0);

    result = layout_overflow_result_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(robo_layout_result_has_flag(
                          &result, ROBO_LAYOUT_RESULT_FLAG_OVERFLOW_X),
                      1);
    ROBO_CHECK_EQ_INT(robo_layout_result_has_flag(
                          &result, ROBO_LAYOUT_RESULT_FLAG_OVERFLOW_Y),
                      0);

    /* And the region a parent would fold in: the padding box grown by the extent. The
     * `outer`'s record is re-read here rather than reused, because `edges` was
     * overwritten with the *child*'s above -- and a region computed from a leaf's
     * cleared record would be the padding box itself. */
    {
        robo_layout_overflow_edges_t outer_edges = layout_overflow_edges_of(&fixture, outer);
        robo_layout_rect_t region;

        ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(
                              &outer_edges, layout_overflow_padding_box_of(&fixture, outer),
                              &region, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(region.width, 300);
        ROBO_CHECK_EQ_INT(region.height, 50);
        ROBO_CHECK_EQ_INT(region.x, 0);
    }

    layout_overflow_end(&fixture);
}

static void test_overflow_pass_child_below(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t outer_rule;
    layout_overflow_rule_t inner_rule;
    layout_overflow_rule_t rules[4];
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;
    robo_layout_overflow_edges_t edges;
    robo_layout_result_t result;

    layout_fixture();

    /* The same fact the other way up: an explicit height smaller than the contents, and
     * a child that reaches 50 past the bottom edge. Nothing collapses and nothing
     * grows -- the box is 30 tall and says so. */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&outer_rule, "width", 100.0);
    layout_overflow_rule_px(&outer_rule, "height", 30.0);
    layout_overflow_rule_init(&inner_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&inner_rule, "height", 80.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = outer_rule;
    rules[3] = inner_rule;

    layout_overflow_begin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = layout_overflow_box_for(&fixture, "div");
    inner = layout_overflow_box_for(&fixture, "section");

    edges = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(edges.right, 0);
    ROBO_CHECK_EQ_INT(edges.bottom, 50);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.top, 0);

    /* The child's `width: auto` fills the containing block, so it reaches neither side;
     * its height is the explicit one. */
    ROBO_CHECK_EQ_INT(layout_overflow_size_of(&fixture, inner).width, 100);
    ROBO_CHECK_EQ_INT(layout_overflow_size_of(&fixture, inner).height, 80);

    result = layout_overflow_result_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(robo_layout_result_has_flag(
                          &result, ROBO_LAYOUT_RESULT_FLAG_OVERFLOW_Y),
                      1);
    ROBO_CHECK_EQ_INT(robo_layout_result_has_flag(
                          &result, ROBO_LAYOUT_RESULT_FLAG_OVERFLOW_X),
                      0);

    layout_overflow_end(&fixture);
}

static void test_overflow_pass_size_unchanged(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t outer_rule;
    layout_overflow_rule_t inner_rule;
    layout_overflow_rule_t rules[4];
    robo_layout_node_id_t outer;
    robo_layout_node_id_t html;
    robo_layout_node_id_t body;
    robo_layout_overflow_edges_t edges;

    layout_fixture();

    /*
     * The phase in one case, and the assertion a build that folded an overflow into a
     * size would fail: the box is 100 by 30 with 200 of contents sticking out to the
     * right and 50 below, **and its two ancestors are exactly as tall as the box's
     * margin box**. An overflow that reached flow would show up here as a taller `body`
     * and a taller `html`, and would look perfectly correct in every single-box test.
     */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&outer_rule, "width", 100.0);
    layout_overflow_rule_px(&outer_rule, "height", 30.0);
    layout_overflow_rule_init(&inner_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&inner_rule, "width", 300.0);
    layout_overflow_rule_px(&inner_rule, "height", 80.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = outer_rule;
    rules[3] = inner_rule;

    layout_overflow_begin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = layout_overflow_box_for(&fixture, "div");
    body = layout_overflow_box_for(&fixture, "body");
    html = layout_overflow_box_for(&fixture, "html");

    ROBO_CHECK_EQ_INT(layout_overflow_size_of(&fixture, outer).width, 100);
    ROBO_CHECK_EQ_INT(layout_overflow_size_of(&fixture, outer).height, 30);
    edges = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(edges.right, 200);
    ROBO_CHECK_EQ_INT(edges.bottom, 50);

    ROBO_CHECK_EQ_INT(layout_overflow_size_of(&fixture, body).height, 30);
    ROBO_CHECK_EQ_INT(layout_overflow_size_of(&fixture, html).height, 30);
    /*
     * And the *pair* of facts that separates an overflow from a size: the two ancestors
     * are 30 tall and their own records say 50 of contents reached past them, because the
     * `div`'s **region** -- its padding box grown by its extents, which is what a parent
     * folds in -- is 80 tall while the `div`'s *margin box* is 30. Flow stopped at the
     * margin box; the record did not.
     *
     * A build that had instead grown `body` to 80 would satisfy every assertion above it
     * here and be wrong, which is why both halves are asserted.
     */
    {
        robo_layout_overflow_edges_t body_edges = layout_overflow_edges_of(&fixture, body);
        robo_layout_overflow_edges_t html_edges = layout_overflow_edges_of(&fixture, html);

        ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow(&body_edges), 1);
        ROBO_CHECK_EQ_INT(body_edges.bottom, 50);
        ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow(&html_edges), 1);
        ROBO_CHECK_EQ_INT(html_edges.bottom, 50);
    }

    layout_overflow_end(&fixture);
}

static void test_overflow_pass_hidden_clips(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t outer_rule;
    layout_overflow_rule_t inner_rule;
    layout_overflow_rule_t rules[4];
    robo_layout_stats_t stats;
    robo_layout_node_id_t outer;
    robo_layout_overflow_edges_t edges;
    robo_layout_rect_t padding_box;
    robo_layout_rect_t region;
    robo_layout_rect_t clip;

    layout_fixture();

    /*
     * `overflow: hidden` records the same extent a visible box records -- the record is
     * the *measurement* -- and adds a boundary: the region becomes the padding box and
     * the clip rectangle exists. The flags are set from the extents, so a box that hides
     * what does not fit still says that something did not fit, which is the difference
     * between "the contents are too big" and "the contents are drawn outside".
     */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&outer_rule, "width", 100.0);
    layout_overflow_rule_px(&outer_rule, "height", 30.0);
    layout_overflow_rule_overflow(&outer_rule, "overflow", ROBO_CSS_OVERFLOW_HIDDEN);
    layout_overflow_rule_init(&inner_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&inner_rule, "height", 80.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = outer_rule;
    rules[3] = inner_rule;

    layout_overflow_begin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, &stats);

    outer = layout_overflow_box_for(&fixture, "div");
    edges = layout_overflow_edges_of(&fixture, outer);

    ROBO_CHECK_EQ_INT((int)edges.horizontal, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    ROBO_CHECK_EQ_INT((int)edges.vertical, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    ROBO_CHECK_EQ_INT(edges.axes_resolved, 2u);
    ROBO_CHECK_EQ_INT(edges.axes_unresolved, 0u);
    ROBO_CHECK_EQ_INT(edges.bottom, 50);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips_any(&edges), 1);
    {
        robo_layout_result_t result = layout_overflow_result_of(&fixture, outer);

        ROBO_CHECK_EQ_INT(
            robo_layout_result_has_flag(&result, ROBO_LAYOUT_RESULT_FLAG_OVERFLOW_Y), 1);
    }

    padding_box = layout_overflow_padding_box_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(padding_box.width, 100);
    ROBO_CHECK_EQ_INT(padding_box.height, 30);

    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, padding_box, &region, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(region.width, 100);
    ROBO_CHECK_EQ_INT(region.height, 30);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(&edges, padding_box, &clip, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(clip.width, 100);
    ROBO_CHECK_EQ_INT(clip.height, 30);

    /* A box that clips and a box that overflows are two different facts, and neither
     * implies the other: one box here clips, one box here overflows. */
    ROBO_CHECK_EQ_INT(stats.block.overflow_clipped, 1u);
    ROBO_CHECK_EQ_INT(stats.block.overflowing, 1u);
    ROBO_CHECK_EQ_INT(stats.block.overflow_unresolved, 0u);

    layout_overflow_end(&fixture);
}

static void test_overflow_pass_axis_split(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t outer_rule;
    layout_overflow_rule_t inner_rule;
    layout_overflow_rule_t rules[4];
    robo_layout_node_id_t outer;
    robo_layout_overflow_edges_t edges;
    robo_layout_rect_t padding_box;
    robo_layout_rect_t region;

    layout_fixture();

    /*
     * `overflow-x: hidden; overflow-y: visible`, and the axis is never collapsed into
     * the pair: the region is 100 wide (clipped) and 80 tall (grown), which is a
     * rectangle neither mode could have produced alone.
     *
     * And no **axis promotion**: CSS promotes `visible` to `auto` when the other axis
     * scrolls, because a box cannot scroll one axis and paint outside itself on the
     * other -- a rule that exists for scrolling, which this build has none of. So
     * `visible` beside `hidden` stays `visible` here, and this case is what the phase
     * that adds scrolling changes.
     */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&outer_rule, "width", 100.0);
    layout_overflow_rule_px(&outer_rule, "height", 50.0);
    layout_overflow_rule_overflow(&outer_rule, "overflow-x", ROBO_CSS_OVERFLOW_HIDDEN);
    layout_overflow_rule_overflow(&outer_rule, "overflow-y", ROBO_CSS_OVERFLOW_VISIBLE);
    layout_overflow_rule_init(&inner_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&inner_rule, "width", 300.0);
    layout_overflow_rule_px(&inner_rule, "height", 80.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = outer_rule;
    rules[3] = inner_rule;

    layout_overflow_begin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = layout_overflow_box_for(&fixture, "div");
    edges = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT((int)edges.horizontal, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    ROBO_CHECK_EQ_INT((int)edges.vertical, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(edges.axes_resolved, 2u);
    ROBO_CHECK_EQ_INT(edges.right, 200);
    ROBO_CHECK_EQ_INT(edges.bottom, 30);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips(&edges,
                                                       ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL),
                      1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips(&edges,
                                                       ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL),
                      0);

    padding_box = layout_overflow_padding_box_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, padding_box, &region, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(region.width, 100);
    ROBO_CHECK_EQ_INT(region.height, 80);

    layout_overflow_end(&fixture);
}

static void test_overflow_pass_recursion_clipped(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t outer_rule;
    layout_overflow_rule_t mid_rule;
    layout_overflow_rule_t deep_rule;
    layout_overflow_rule_t rules[5];
    robo_layout_node_id_t outer;
    robo_layout_node_id_t mid;
    robo_layout_overflow_edges_t edges;

    layout_fixture();

    /*
     * Three levels and one declared mode, and this is the recursion the phase is built
     * around: a child contributes its **region**, not its extents. `mid` reaches 50 past
     * its own box in both runs -- its record never changes -- and what reaches `outer`
     * is 40 in the first run and nothing at all in the second, because a box that hides
     * an axis keeps what it hides.
     *
     * The alternative -- fold the child's contents into the parent and forget the child
     * clipped -- is how a clipped subtree comes to leak into an ancestor's scrollable
     * area.
     */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&outer_rule, "height", 40.0);
    layout_overflow_rule_init(&mid_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&mid_rule, "height", 30.0);
    layout_overflow_rule_init(&deep_rule, "article", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&deep_rule, "height", 80.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = outer_rule;
    rules[3] = mid_rule;
    rules[4] = deep_rule;

    layout_overflow_begin(&fixture,
                          "<html><body><div><section><article></article></section></div>"
                          "</body></html>",
                          rules, 5u);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = layout_overflow_box_for(&fixture, "div");
    mid = layout_overflow_box_for(&fixture, "section");

    edges = layout_overflow_edges_of(&fixture, mid);
    ROBO_CHECK_EQ_INT(edges.bottom, 50);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips_any(&edges), 0);

    edges = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(edges.bottom, 40);

    /* Then the one declaration that changes, and the ancestor follows it down. */
    layout_overflow_rule_init(&mid_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&mid_rule, "height", 30.0);
    layout_overflow_rule_overflow(&mid_rule, "overflow", ROBO_CSS_OVERFLOW_HIDDEN);
    layout_overflow_style_replace(&fixture.source, "section", &mid_rule);
    layout_overflow_rebuild(&fixture);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = layout_overflow_box_for(&fixture, "div");
    mid = layout_overflow_box_for(&fixture, "section");

    /* The measurement is unchanged: `mid` still reaches 50 past its own box. */
    edges = layout_overflow_edges_of(&fixture, mid);
    ROBO_CHECK_EQ_INT(edges.bottom, 50);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips_any(&edges), 1);
    /* And what it *contributes* is now only what it kept: 30 against `outer`'s 40, so
     * nothing reaches past `outer` at all. */
    edges = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(edges.bottom, 0);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow(&edges), 0);

    layout_overflow_end(&fixture);
}

static void test_overflow_pass_relative_child(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t outer_rule;
    layout_overflow_rule_t inner_rule;
    layout_overflow_rule_t rules[4];
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;
    robo_layout_overflow_edges_t edges;
    robo_layout_point_t origin;
    robo_layout_position_offset_t offset;

    layout_fixture();

    /*
     * A `relative` child, and the one thing 10.13 had to settle about 10.12: overflow is
     * measured against the **final** position, because that is where the contents are.
     * The child is 80 wide and displaced 40 to the right, so it reaches 20 past a
     * 100-wide box -- not zero, which is what measuring the flow position would give,
     * and the box it *would* have fitted inside is the one it left.
     */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&outer_rule, "width", 100.0);
    layout_overflow_rule_px(&outer_rule, "height", 50.0);
    layout_overflow_rule_init(&inner_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&inner_rule, "width", 80.0);
    layout_overflow_rule_px(&inner_rule, "height", 20.0);
    layout_overflow_rule_position(&inner_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_overflow_rule_px(&inner_rule, "left", 40.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = outer_rule;
    rules[3] = inner_rule;

    layout_overflow_begin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = layout_overflow_box_for(&fixture, "div");
    inner = layout_overflow_box_for(&fixture, "section");

    origin = layout_overflow_origin_of(&fixture, inner);
    ROBO_CHECK_EQ_INT(origin.x, 40);
    ROBO_CHECK_EQ_INT(origin.y, 0);
    ROBO_CHECK_EQ_INT(robo_layout_object_position(fixture.tree, inner, &offset, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(offset.dx, 40);

    edges = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(edges.right, 20);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.top, 0);
    ROBO_CHECK_EQ_INT(edges.bottom, 0);
    /* The displacement did not change the box either: 10.12's claim, re-asserted here
     * because it is the fact the extent above is derived from. */
    ROBO_CHECK_EQ_INT(layout_overflow_size_of(&fixture, inner).width, 80);

    layout_overflow_end(&fixture);
}

static void test_overflow_pass_stats_and_unresolved(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t clipper_rule;
    layout_overflow_rule_t overflower_rule;
    layout_overflow_rule_t spreader_rule;
    layout_overflow_rule_t unusable_rule;
    layout_overflow_rule_t rules[6];
    robo_layout_stats_t stats;
    robo_layout_node_id_t spreader;
    robo_layout_overflow_edges_t edges;

    layout_fixture();

    /*
     * Three boxes, three different facts, and the counters are where they are told
     * apart: one clips and does not overflow, one overflows and does not clip, and one
     * declared an overflow this build cannot use. The third is the one a build would
     * most easily lose -- its used mode is `visible`, so nothing about its *geometry*
     * says an author asked for something.
     */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&clipper_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&clipper_rule, "height", 40.0);
    layout_overflow_rule_overflow(&clipper_rule, "overflow-x", ROBO_CSS_OVERFLOW_HIDDEN);
    layout_overflow_rule_init(&overflower_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&overflower_rule, "height", 30.0);
    layout_overflow_rule_init(&spreader_rule, "article", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&spreader_rule, "width", 300.0);
    layout_overflow_rule_px(&spreader_rule, "height", 20.0);
    layout_overflow_rule_init(&unusable_rule, "aside", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&unusable_rule, "height", 10.0);
    layout_overflow_rule_keyword(&unusable_rule, "overflow-y", "scroll");
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = clipper_rule;
    rules[3] = overflower_rule;
    rules[4] = spreader_rule;
    rules[5] = unusable_rule;

    layout_overflow_begin(&fixture,
                          "<html><body><div><section></section></div><article></article>"
                          "<aside></aside></body></html>",
                          rules, 6u);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, &stats);

    /* The `div` clips on one axis and has nothing sticking out of it. */
    ROBO_CHECK_EQ_INT(stats.block.overflow_clipped, 1u);
    /* The `article` declares 300 where its containing block is 800, so it reaches past
     * *nothing*: the counter below is about its own contents, and it has none. */
    spreader = layout_overflow_box_for(&fixture, "article");
    edges = layout_overflow_edges_of(&fixture, spreader);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_has_overflow(&edges), 0);
    /* `section` is 30 tall with no contents of its own, and `div` clips rather than
     * overflowing -- so exactly nothing overflows here. */
    ROBO_CHECK_EQ_INT(stats.block.overflowing, 0u);
    /* And the declared-but-unusable axis is counted, which is the only place it is
     * visible at all. */
    ROBO_CHECK_EQ_INT(stats.block.overflow_unresolved, 1u);
    edges = layout_overflow_edges_of(&fixture, layout_overflow_box_for(&fixture, "aside"));
    ROBO_CHECK_EQ_INT(edges.axes_resolved, 0u);
    ROBO_CHECK_EQ_INT(edges.axes_unresolved, 1u);
    ROBO_CHECK_EQ_INT((int)edges.vertical, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips_any(&edges), 0);

    layout_overflow_end(&fixture);
}

static void test_overflow_pass_box_model(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t outer_rule;
    layout_overflow_rule_t inner_rule;
    layout_overflow_rule_t rules[4];
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;
    robo_layout_overflow_edges_t edges;
    robo_layout_size_t size;
    robo_layout_point_t origin;
    robo_layout_rect_t padding_box;

    layout_fixture();

    /*
     * The reference is the **padding box**, and this case is the one that pins it: a
     * border of 2 and a padding of 10 on every side, and a 150-wide child. Measured
     * against the padding box the extent is 40; measured against the *border* box it
     * would be 36, and a build that used the border box -- or the content box, which
     * would give 50 -- would be wrong by exactly one box layer.
     *
     * And the margin is *outside* all of it: the box's own position moves by 10 and its
     * extents do not, which is the difference between external spacing and internal
     * overflow stated in two assertions.
     */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&outer_rule, "width", 100.0);
    layout_overflow_rule_px(&outer_rule, "height", 50.0);
    layout_overflow_rule_px(&outer_rule, "margin-left", 10.0);
    layout_overflow_rule_px(&outer_rule, "margin-top", 10.0);
    layout_overflow_rule_padding_all(&outer_rule, 10.0);
    layout_overflow_rule_border_all(&outer_rule, 2.0);
    layout_overflow_rule_init(&inner_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&inner_rule, "width", 150.0);
    layout_overflow_rule_px(&inner_rule, "height", 90.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = outer_rule;
    rules[3] = inner_rule;

    layout_overflow_begin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = layout_overflow_box_for(&fixture, "div");
    inner = layout_overflow_box_for(&fixture, "section");

    /* The border box is the content box plus the padding plus the border -- 10.10's and
     * 10.11's arithmetic, re-asserted because the extent below is measured against the
     * box those two phases built. */
    size = layout_overflow_size_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(size.width, 124);
    ROBO_CHECK_EQ_INT(size.height, 74);
    ROBO_CHECK_EQ_INT(layout_overflow_size_of(&fixture, inner).width, 150);

    padding_box = layout_overflow_padding_box_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(padding_box.width, 120);
    ROBO_CHECK_EQ_INT(padding_box.height, 70);

    /* The reference is the 120-wide, 70-tall *padding* box, and the child begins 10
     * inside it on both axes because that is where the content origin is: 10 + 150 - 120
     * on the right and 10 + 90 - 70 at the bottom. Against the border box the two would
     * be 36 and 26, and against the content box 50 and 40. */
    edges = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.top, 0);
    ROBO_CHECK_EQ_INT(edges.right, 40);
    ROBO_CHECK_EQ_INT(edges.bottom, 30);

    /* The margin put the box at 10, 10 in its containing block and changed neither
     * extent: a margin is spacing outside the box, an overflow is contents inside it. */
    origin = layout_overflow_origin_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(origin.x, 10);
    ROBO_CHECK_EQ_INT(origin.y, 10);

    layout_overflow_end(&fixture);
}

static void test_overflow_pass_extreme_width(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t outer_rule;
    layout_overflow_rule_t inner_rule;
    layout_overflow_rule_t rules[4];
    robo_layout_node_id_t outer;
    robo_layout_overflow_edges_t edges;
    robo_layout_size_t size;

    layout_fixture();

    /*
     * Extreme geometry, and no invalid geometry may escape: a one-pixel box holding a
     * two-billion-pixel child produces one large, finite, positive extent and a box that
     * is still one pixel wide. Nothing wraps, nothing goes negative, and the record is
     * still a record.
     */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&outer_rule, "width", 1.0);
    layout_overflow_rule_px(&outer_rule, "height", 1.0);
    layout_overflow_rule_init(&inner_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&inner_rule, "width", 2000000000.0);
    layout_overflow_rule_px(&inner_rule, "height", 1.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = outer_rule;
    rules[3] = inner_rule;

    layout_overflow_begin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = layout_overflow_box_for(&fixture, "div");
    edges = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(&edges), 1);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.top, 0);
    ROBO_CHECK_EQ_INT(edges.bottom, 0);
    ROBO_CHECK_EQ_INT(edges.right > 0, 1);
    ROBO_CHECK_EQ_INT(edges.right > 1000000000, 1);

    size = layout_overflow_size_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(size.width, 1);
    ROBO_CHECK_EQ_INT(size.height, 1);

    layout_overflow_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The object door                                                     */
/* ------------------------------------------------------------------ */

static void test_overflow_object_door(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t div_rule;
    layout_overflow_rule_t rules[3];
    robo_layout_overflow_edges_t edges;
    robo_layout_overflow_edges_t zeroed;
    robo_layout_node_id_t div;
    robo_layout_node_id_t text;

    layout_fixture();

    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&div_rule, "height", 40.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_overflow_begin(&fixture, "<html><body><div>text</div></body></html>", rules, 3u);
    robo_layout_overflow_edges_init(&zeroed);
    div = layout_overflow_box_for(&fixture, "div");

    /* Before any pass: this box has no overflow *yet*, and the door says so rather than
     * answering the cleared record -- a cleared record is a legal value (no overflow is
     * the common case), so the door asks the result rather than reading the record's
     * silence. */
    robo_layout_overflow_edges_init(&edges);
    edges.right = 999;
    ROBO_CHECK_EQ_INT(robo_layout_object_overflow(fixture.tree, div, &edges, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(&edges, &zeroed), 1);

    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    div = layout_overflow_box_for(&fixture, "div");
    ROBO_CHECK_EQ_INT(robo_layout_object_overflow(fixture.tree, div, &edges, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_is_valid(&edges), 1);

    /* A text run is not a box at all, and it is declined: its contents contribute to its
     * parent through the region the parent's measurement reads, which is where the
     * honest answer lives. */
    text = layout_overflow_first_of_kind(&fixture, ROBO_LAYOUT_OBJECT_TEXT);
    ROBO_CHECK(text != ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_object_overflow(fixture.tree, text, &edges, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(&edges, &zeroed), 1);

    /* And the four refusals every door in this module makes. */
    ROBO_CHECK_EQ_INT(robo_layout_object_overflow(NULL, div, &edges, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_overflow(fixture.tree, ROBO_LAYOUT_NODE_ID_NONE,
                                                  &edges, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_overflow(fixture.tree, (robo_layout_node_id_t)9999,
                                                  &edges, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_overflow(fixture.tree, div, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    layout_overflow_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Re-layout and determinism                                           */
/* ------------------------------------------------------------------ */

static void test_overflow_relayout(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t outer_rule;
    layout_overflow_rule_t inner_rule;
    layout_overflow_rule_t rules[4];
    robo_layout_node_id_t outer;
    robo_layout_overflow_edges_t edges;
    robo_layout_rect_t clip;

    layout_fixture();

    /*
     * Changing a *content* size and changing an *overflow declaration*, both through the
     * module's one invalidation route -- a rebuilt tree and another pass -- and neither
     * of which is a second architecture. The extent follows the width, and the clip
     * rectangle appears when the mode does.
     */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&outer_rule, "width", 100.0);
    layout_overflow_rule_px(&outer_rule, "height", 50.0);
    layout_overflow_rule_init(&inner_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&inner_rule, "width", 300.0);
    layout_overflow_rule_px(&inner_rule, "height", 20.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = outer_rule;
    rules[3] = inner_rule;

    layout_overflow_begin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = layout_overflow_box_for(&fixture, "div");
    edges = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(edges.right, 200);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(
                          &edges, layout_overflow_padding_box_of(&fixture, outer), &clip, NULL),
                      ROBO_NOT_FOUND);

    layout_overflow_rule_init(&inner_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&inner_rule, "width", 500.0);
    layout_overflow_rule_px(&inner_rule, "height", 20.0);
    layout_overflow_style_replace(&fixture.source, "section", &inner_rule);
    layout_overflow_rebuild(&fixture);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = layout_overflow_box_for(&fixture, "div");
    edges = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(edges.right, 400);

    layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&outer_rule, "width", 100.0);
    layout_overflow_rule_px(&outer_rule, "height", 50.0);
    layout_overflow_rule_overflow(&outer_rule, "overflow", ROBO_CSS_OVERFLOW_HIDDEN);
    layout_overflow_style_replace(&fixture.source, "div", &outer_rule);
    layout_overflow_rebuild(&fixture);
    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);

    outer = layout_overflow_box_for(&fixture, "div");
    edges = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(edges.right, 400); /* the measurement is unchanged */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clips_any(&edges), 1);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(
                          &edges, layout_overflow_padding_box_of(&fixture, outer), &clip, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(clip.width, 100);

    layout_overflow_end(&fixture);
}

static void test_overflow_determinism(void)
{
    layout_overflow_fixture_t fixture;
    layout_overflow_rule_t html_rule;
    layout_overflow_rule_t body_rule;
    layout_overflow_rule_t outer_rule;
    layout_overflow_rule_t inner_rule;
    layout_overflow_rule_t rules[4];
    robo_layout_node_id_t outer;
    robo_layout_overflow_edges_t first;
    robo_layout_overflow_edges_t second;
    robo_layout_stats_t first_stats;
    robo_layout_stats_t second_stats;
    size_t index;

    layout_fixture();

    /*
     * Identical input, identical geometry: the same tree laid out twice produces the
     * same record and the same counters, and changing only the *viewport* -- which
     * cannot move a box with every dimension declared -- produces them again. Repeated
     * more than twice because a pass that cached something across runs would show up
     * only on the third.
     */
    layout_overflow_blocks(&html_rule, &body_rule);
    layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&outer_rule, "width", 100.0);
    layout_overflow_rule_px(&outer_rule, "height", 50.0);
    layout_overflow_rule_overflow(&outer_rule, "overflow-x", ROBO_CSS_OVERFLOW_HIDDEN);
    layout_overflow_rule_init(&inner_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_overflow_rule_px(&inner_rule, "width", 300.0);
    layout_overflow_rule_px(&inner_rule, "height", 80.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = outer_rule;
    rules[3] = inner_rule;

    layout_overflow_begin(&fixture, "<html><body><div><section></section></div></body></html>",
                          rules, 4u);

    layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, &first_stats);
    outer = layout_overflow_box_for(&fixture, "div");
    first = layout_overflow_edges_of(&fixture, outer);
    ROBO_CHECK_EQ_INT(first.right, 200);
    ROBO_CHECK_EQ_INT(first.bottom, 30);

    for (index = 0u; index < 3u; index++) {
        layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, &second_stats);
        outer = layout_overflow_box_for(&fixture, "div");
        second = layout_overflow_edges_of(&fixture, outer);
        ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_equal(&first, &second), 1);
        ROBO_CHECK_EQ_INT(robo_layout_block_stats_equal(&first_stats.block, &second_stats.block),
                          1);
        ROBO_CHECK_EQ_INT(
            robo_layout_inline_stats_equal(&first_stats.inline_flow, &second_stats.inline_flow),
            1);
    }

    layout_overflow_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The module owns nothing                                             */
/* ------------------------------------------------------------------ */

static void test_overflow_allocator_balance(void)
{
    size_t before;
    size_t settled;
    size_t index;

    layout_fixture();
    before = layout_live_blocks();

    /* One full cycle, several times: build, pass, pass again, destroy -- and the live
     * block count comes back to where it started every time. A record that owned an
     * allocation would show up here and nowhere else. */
    for (index = 0u; index < 3u; index++) {
        layout_overflow_fixture_t fixture;
        layout_overflow_rule_t html_rule;
        layout_overflow_rule_t body_rule;
        layout_overflow_rule_t outer_rule;
        layout_overflow_rule_t inner_rule;
        layout_overflow_rule_t rules[4];

        layout_overflow_blocks(&html_rule, &body_rule);
        layout_overflow_rule_init(&outer_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_overflow_rule_px(&outer_rule, "width", 100.0);
        layout_overflow_rule_overflow(&outer_rule, "overflow", ROBO_CSS_OVERFLOW_HIDDEN);
        layout_overflow_rule_init(&inner_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
        layout_overflow_rule_px(&inner_rule, "width", 300.0);
        rules[0] = html_rule;
        rules[1] = body_rule;
        rules[2] = outer_rule;
        rules[3] = inner_rule;

        layout_overflow_begin(&fixture,
                              "<html><body><div><section></section></div></body></html>",
                              rules, 4u);
        layout_overflow_pass(&fixture, LAYOUT_OVERFLOW_VIEWPORT_WIDTH, NULL);
        settled = layout_live_blocks();
        /* The overflow layer allocates nothing of its own across a pass: the record is a
         * value on the object, the region and the clip rectangle are locals, and the
         * context is destroyed inside the helper. A second pass at another viewport
         * changes no count either. */
        ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
        layout_overflow_pass(&fixture, 400, NULL);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
        layout_overflow_end(&fixture);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
    }
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout_overflow(void);

void robo_test_register_layout_overflow(void)
{
    robo_test_register("layout.overflow.kind_vocabulary", test_overflow_kind_vocabulary);
    robo_test_register("layout.overflow.axis_vocabulary", test_overflow_axis_vocabulary);
    robo_test_register("layout.overflow.axis_record", test_overflow_axis_record);
    robo_test_register("layout.overflow.declared_record", test_overflow_declared_record);
    robo_test_register("layout.overflow.edges_record", test_overflow_edges_record);
    robo_test_register("layout.overflow.seam_declarations", test_overflow_seam_declarations);
    robo_test_register("layout.overflow.resolve_modes", test_overflow_resolve_modes);
    robo_test_register("layout.overflow.include_extents", test_overflow_include_extents);
    robo_test_register("layout.overflow.include_saturates", test_overflow_include_saturates);
    robo_test_register("layout.overflow.region_per_axis", test_overflow_region_per_axis);
    robo_test_register("layout.overflow.clip_rect_refusals", test_overflow_clip_rect_refusals);
    robo_test_register("layout.overflow.pass_fitting", test_overflow_pass_fitting);
    robo_test_register("layout.overflow.pass_child_right", test_overflow_pass_child_right);
    robo_test_register("layout.overflow.pass_child_below", test_overflow_pass_child_below);
    robo_test_register("layout.overflow.pass_size_unchanged", test_overflow_pass_size_unchanged);
    robo_test_register("layout.overflow.pass_hidden_clips", test_overflow_pass_hidden_clips);
    robo_test_register("layout.overflow.pass_axis_split", test_overflow_pass_axis_split);
    robo_test_register("layout.overflow.pass_recursion_clipped",
                       test_overflow_pass_recursion_clipped);
    robo_test_register("layout.overflow.pass_relative_child",
                       test_overflow_pass_relative_child);
    robo_test_register("layout.overflow.pass_stats_and_unresolved",
                       test_overflow_pass_stats_and_unresolved);
    robo_test_register("layout.overflow.pass_box_model", test_overflow_pass_box_model);
    robo_test_register("layout.overflow.pass_extreme_width", test_overflow_pass_extreme_width);
    robo_test_register("layout.overflow.object_door", test_overflow_object_door);
    robo_test_register("layout.overflow.relayout", test_overflow_relayout);
    robo_test_register("layout.overflow.determinism", test_overflow_determinism);
    robo_test_register("layout.overflow.allocator_balance", test_overflow_allocator_balance);
}
