/*
 * RoboLayout padding suite (Phase 10.10) — layout.paddings.*
 *
 * Four things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary** -- three kinds, four sides, and the predicates and names over
 *   them -- which is pure, and whose one difference from 10.9's is that a *negative*
 *   length is not one of the states a side may be in;
 *
 *   the **seam**: what a computed style declared, which is the one place this module
 *   reads a padding and the one place a declaration it cannot use becomes a count;
 *
 *   the two **derived numbers** -- the content box a border box leaves and the border
 *   box a content box asks for -- driven directly, so that a wrong subtraction fails
 *   at the arithmetic rather than three boxes later in a coordinate;
 *
 *   the **pass**: padding reaching geometry, which since this phase means a box's
 *   `size` is its border box, an `auto` width fills what the padding left, and an
 *   atomic inline's advance along a line carries it.
 *
 * The shape the phase is about is asserted every way it can be seen: a padding
 * changes a box's **size** and never its **position**, which is the exact mirror of
 * 10.9's claim and the reason the two phases are two. A case that only checked the
 * size would pass for a build that grew the box outward from its content origin, and
 * `layout.paddings.relayout` is the one that fails it.
 *
 * The scripts here are *computed styles*, built through 9.8's makers exactly as the
 * sizing and margin suites build them, because a fabricated struct would prove the
 * pass reads a struct rather than a computed CSS value. Where a case needs a
 * declaration the fixture cannot express through a CSS property table -- a negative
 * length is expressible, a percentage is, but a `var()`-shaped unresolved length is
 * not -- the value is flagged the way the computed layer flags one and the
 * classification is what is under test.
 *
 * Elements are resolved **by tag** (`layout_padding_box_for`) rather than by arena
 * index, which is the lesson 10.8's suite wrote down: a fixture's indices are a fact
 * about the parser, and the tags here are distinct anyway so that one scripted style
 * per element is enough.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fabricated style that can declare anything                        */
/* ------------------------------------------------------------------ */

/** One declaration to put on a fabricated style. */
typedef enum layout_padding_decl_kind {
    LAYOUT_PADDING_DECL_PX = 0,
    /** A px length that is **negative**: invalid CSS for this property, which is the
     *  one declaration the seam is asked to refuse rather than use. */
    LAYOUT_PADDING_DECL_NEGATIVE_PX = 1,
    /** A px length flagged unresolved, which is what a `var()` arrives as. */
    LAYOUT_PADDING_DECL_UNRESOLVED_PX = 2,
    LAYOUT_PADDING_DECL_PERCENTAGE = 3,
    LAYOUT_PADDING_DECL_EM = 4,
    LAYOUT_PADDING_DECL_KEYWORD = 5
} layout_padding_decl_kind_t;

typedef struct layout_padding_decl {
    const char *name;
    layout_padding_decl_kind_t kind;
    double number;
    const char *keyword;
} layout_padding_decl_t;

#define LAYOUT_PADDING_MAX_DECLARATIONS 10u
#define LAYOUT_PADDING_MAX_RULES 8u

typedef struct layout_padding_rule {
    const char *tag;
    robo_css_display_t display;
    layout_padding_decl_t declarations[LAYOUT_PADDING_MAX_DECLARATIONS];
    size_t declarations_count;
} layout_padding_rule_t;

static void layout_padding_rule_init(layout_padding_rule_t *rule,
                                     const char *tag,
                                     robo_css_display_t display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static void layout_padding_rule_add(layout_padding_rule_t *rule,
                                    const char *name,
                                    layout_padding_decl_kind_t kind,
                                    double number,
                                    const char *keyword)
{
    ROBO_CHECK_EQ_INT(rule->declarations_count < LAYOUT_PADDING_MAX_DECLARATIONS, 1);
    rule->declarations[rule->declarations_count].name = name;
    rule->declarations[rule->declarations_count].kind = kind;
    rule->declarations[rule->declarations_count].number = number;
    rule->declarations[rule->declarations_count].keyword = keyword;
    rule->declarations_count++;
}

static void layout_padding_rule_px(layout_padding_rule_t *rule, const char *name, double px)
{
    layout_padding_rule_add(rule, name, LAYOUT_PADDING_DECL_PX, px, NULL);
}

/** A *negative* px length, which CSS forbids for this property and the seam refuses. */
static void layout_padding_rule_negative(layout_padding_rule_t *rule,
                                         const char *name,
                                         double px)
{
    layout_padding_rule_add(rule, name, LAYOUT_PADDING_DECL_NEGATIVE_PX, px, NULL);
}

/**
 * A single-value shorthand written the way a stylesheet would.
 *
 * `padding: 10px` is four declarations one layer down, and that is the fact a case
 * asserts by writing the shorthand and reading four sides of geometry: the expansion
 * is 9.11's, and this fixture writing it does not make it this suite's.
 */
static void layout_padding_rule_shorthand(layout_padding_rule_t *rule, double px)
{
    layout_padding_rule_px(rule, "padding-top", px);
    layout_padding_rule_px(rule, "padding-right", px);
    layout_padding_rule_px(rule, "padding-bottom", px);
    layout_padding_rule_px(rule, "padding-left", px);
}

static void layout_padding_rule_percentage(layout_padding_rule_t *rule,
                                           const char *name,
                                           double percent)
{
    layout_padding_rule_add(rule, name, LAYOUT_PADDING_DECL_PERCENTAGE, percent, NULL);
}

static void layout_padding_rule_em(layout_padding_rule_t *rule, const char *name, double em)
{
    layout_padding_rule_add(rule, name, LAYOUT_PADDING_DECL_EM, em, NULL);
}

static void layout_padding_rule_keyword(layout_padding_rule_t *rule,
                                        const char *name,
                                        const char *keyword)
{
    layout_padding_rule_add(rule, name, LAYOUT_PADDING_DECL_KEYWORD, 0.0, keyword);
}

static void layout_padding_rule_unresolved(layout_padding_rule_t *rule,
                                           const char *name,
                                           double px)
{
    layout_padding_rule_add(rule, name, LAYOUT_PADDING_DECL_UNRESOLVED_PX, px, NULL);
}

typedef struct layout_padding_style {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
    size_t answers;
} layout_padding_style_t;

typedef struct layout_padding_source {
    layout_padding_style_t rules[LAYOUT_PADDING_MAX_RULES];
    size_t count;
} layout_padding_source_t;

/**
 * Build the style @p rule describes: a real DISPLAY value plus whatever the rule
 * declares, all through 9.8's own makers and container.
 *
 * `display` is set even when the rule declares nothing else, because 9.12's initial
 * value is `inline` and a fixture that wanted a block box would otherwise be asserting
 * a document made of inline boxes.
 */
static robo_css_computed_style_t *layout_padding_style_build(const layout_padding_rule_t *rule)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;
    size_t index;

    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_display(&value, rule->display, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("display"),
                                                       0u, &specified, &value, NULL),
                      ROBO_OK);

    for (index = 0u; index < rule->declarations_count; index++) {
        const layout_padding_decl_t *declaration = &rule->declarations[index];

        value = (robo_css_computed_value_t)ROBO_CSS_CLEARED;
        specified = (robo_css_value_t)ROBO_CSS_CLEARED;
        switch (declaration->kind) {
        case LAYOUT_PADDING_DECL_PERCENTAGE:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_percentage(&value,
                                                                     declaration->number, 0,
                                                                     NULL),
                              ROBO_OK);
            break;
        case LAYOUT_PADDING_DECL_KEYWORD:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                                  &value, robo_css_slice_from_text(declaration->keyword), NULL),
                              ROBO_OK);
            break;
        case LAYOUT_PADDING_DECL_UNRESOLVED_PX:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(
                                  &value, declaration->number, ROBO_CSS_UNIT_PX, 0, NULL),
                              ROBO_OK);
            /* The flag the computed layer sets for a `var()` or a `calc()`: carried
             * intact and not a value *yet*, whatever its kind says. */
            value.unresolved = 1;
            break;
        case LAYOUT_PADDING_DECL_EM:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(
                                  &value, declaration->number, ROBO_CSS_UNIT_EM, 0, NULL),
                              ROBO_OK);
            break;
        case LAYOUT_PADDING_DECL_NEGATIVE_PX:
        case LAYOUT_PADDING_DECL_PX:
        default:
            /* Written the same way for both, and that is the point: the *style* holds
             * what it was handed -- a negative length is a computed value like any other
             * -- and it is the classification that refuses one, not the maker. A fixture
             * that could not build the declaration could not test the refusal. */
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(
                                  &value, declaration->number, ROBO_CSS_UNIT_PX, 0, NULL),
                              ROBO_OK);
            break;
        }
        ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(
                              style, ROBO_CSS_PROPERTY_ID_NONE,
                              robo_css_slice_from_text(declaration->name), 0u, &specified,
                              &value, NULL),
                          ROBO_OK);
    }
    return style;
}

/** Add @p rule to @p source, and answer the style it built. */
static robo_css_computed_style_t *layout_padding_style_add(layout_padding_source_t *source,
                                                          const layout_padding_rule_t *rule)
{
    size_t slot;
    robo_css_computed_style_t *style;

    layout_fixture();
    ROBO_CHECK_EQ_INT(source->count < LAYOUT_PADDING_MAX_RULES, 1);
    slot = source->count;
    style = layout_padding_style_build(rule);
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
 * *first* row whose tag matches, so an appended second `div` row would be shadowed by
 * the one it was meant to replace and the case would assert the old declaration while
 * appearing to have changed it.
 */
static void layout_padding_style_replace(layout_padding_source_t *source,
                                        const char *tag,
                                        const layout_padding_rule_t *rule)
{
    size_t index;

    layout_fixture();
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            robo_css_computed_style_destroy(source->rules[index].style);
            source->rules[index].style = layout_padding_style_build(rule);
            return;
        }
    }
    ROBO_CHECK_MSG(0, "the tag to replace is not in this source");
}

static const robo_css_computed_style_t *layout_padding_style_lookup(
    void *userdata,
    const robo_html_node_handle_t *element)
{
    layout_padding_source_t *source = (layout_padding_source_t *)userdata;
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
    /* An element nothing was declared for: `display: inline`, no declared padding, and
     * that is CSS's initial value rather than a fixture default. */
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/**
 * The px number @p style declares for @p property, asserted to be a usable length.
 *
 * The read the *fixture* needs, not the pass: a case that replaces a style wants to
 * assert the replacement landed rather than that the geometry moved for another reason.
 * Signed, because the negative-declaration case reads a declaration this build refuses
 * and has to see that the style still holds it.
 */
static int32_t layout_padding_entry_px(const robo_css_computed_style_t *style,
                                       const char *property)
{
    const robo_css_computed_value_t *value;

    ROBO_CHECK(style != NULL);
    value = robo_css_computed_style_computed_by_name(style,
                                                    robo_css_slice_from_text(property));
    ROBO_CHECK(value != NULL);
    ROBO_CHECK_EQ_INT((int)value->kind, (int)ROBO_CSS_COMPUTED_VALUE_LENGTH);
    ROBO_CHECK_EQ_INT((int)value->unit, (int)ROBO_CSS_UNIT_PX);
    ROBO_CHECK_EQ_INT(value->unresolved, 0);
    /* Half away from zero, which is the rule 10.8's door states -- and the fixture
     * rounds rather than the module, because this helper is reading a style to check a
     * replacement landed, not resolving a dimension. */
    return value->number < 0.0 ? (int32_t)(value->number - 0.5)
                               : (int32_t)(value->number + 0.5);
}

typedef struct layout_padding_fixture {
    layout_document_t doc;
    layout_padding_source_t source;
    robo_layout_style_source_t styles;
    robo_layout_tree_t *tree;
} layout_padding_fixture_t;

static void layout_padding_begin(layout_padding_fixture_t *fixture,
                                 const char *html,
                                 const layout_padding_rule_t *rules,
                                 size_t rule_count)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    for (index = 0u; index < rule_count; index++) {
        (void)layout_padding_style_add(&fixture->source, &rules[index]);
    }
    robo_layout_style_source_init(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.computed_style_for = layout_padding_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_padding_end(layout_padding_fixture_t *fixture)
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

#define LAYOUT_PADDING_VIEWPORT_WIDTH 800
#define LAYOUT_PADDING_VIEWPORT_HEIGHT 600

/** One pass at @p viewport_width. */
static void layout_padding_pass(layout_padding_fixture_t *fixture,
                                int32_t viewport_width,
                                robo_layout_stats_t *out_stats)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;

    viewport.width = viewport_width;
    viewport.height = LAYOUT_PADDING_VIEWPORT_HEIGHT;
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
static robo_layout_node_id_t layout_padding_box_for(layout_padding_fixture_t *fixture,
                                                    const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);
    robo_layout_node_id_t id;

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&element), 1);
    id = robo_layout_object_id_for_dom_node(fixture->tree, &element);
    ROBO_CHECK(id != ROBO_LAYOUT_NODE_ID_NONE);
    return id;
}

/** The origin a placed box was given, in its containing block's space. */
static robo_layout_point_t layout_padding_origin_of(layout_padding_fixture_t *fixture,
                                                    robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_point_t position;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
    return position;
}

/** The size a placed box was given -- its **border** box since 10.10. */
static robo_layout_size_t layout_padding_size_of(layout_padding_fixture_t *fixture,
                                                 robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_size_t size;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
    return size;
}

/** The used padding a placed box reports. */
static robo_layout_padding_edges_t layout_padding_edges_of(layout_padding_fixture_t *fixture,
                                                          robo_layout_node_id_t id)
{
    robo_layout_padding_edges_t edges;

    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture->tree, id, &edges, NULL), ROBO_OK);
    return edges;
}

/** Assert every field of a used-padding record at once. */
static void layout_padding_expect_edges(const robo_layout_padding_edges_t *edges,
                                        int32_t top,
                                        int32_t right,
                                        int32_t bottom,
                                        int32_t left,
                                        size_t resolved,
                                        size_t unresolved)
{
    ROBO_CHECK_EQ_INT(edges->top, top);
    ROBO_CHECK_EQ_INT(edges->right, right);
    ROBO_CHECK_EQ_INT(edges->bottom, bottom);
    ROBO_CHECK_EQ_INT(edges->left, left);
    ROBO_CHECK_EQ_INT(edges->resolved, resolved);
    ROBO_CHECK_EQ_INT(edges->unresolved, unresolved);
}

/** A record of four used values, for the derived doors below. */
static robo_layout_padding_edges_t layout_padding_edges(int32_t top,
                                                       int32_t right,
                                                       int32_t bottom,
                                                       int32_t left)
{
    robo_layout_padding_edges_t edges;

    robo_layout_padding_edges_init(&edges);
    edges.top = top;
    edges.right = right;
    edges.bottom = bottom;
    edges.left = left;
    edges.resolved = 4u;
    return edges;
}

/** A record of four declarations, for the policy and seam cases. */
static robo_layout_paddings_t layout_padding_quad(robo_gfx_pixel_x_t top,
                                                 robo_gfx_pixel_x_t right,
                                                 robo_gfx_pixel_x_t bottom,
                                                 robo_gfx_pixel_x_t left)
{
    robo_layout_paddings_t paddings;

    robo_layout_paddings_init(&paddings);
    paddings.top = robo_layout_padding_resolved(top);
    paddings.right = robo_layout_padding_resolved(right);
    paddings.bottom = robo_layout_padding_resolved(bottom);
    paddings.left = robo_layout_padding_resolved(left);
    return paddings;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void test_paddings_kind_vocabulary(void)
{
    size_t index;

    layout_fixture();

    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_PADDING_KIND_COUNT, 3);
    /*
     * The zero of the vocabulary is `ABSENT`, and that is a decision rather than a
     * numbering accident: a cleared record then holds the side's *initial state*, so a
     * forgotten initialisation is not a claim that somebody wrote a padding.
     */
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_PADDING_ABSENT, 0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_PADDING_RESOLVED, 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_PADDING_UNRESOLVED, 2);

    for (index = 0u; index < ROBO_LAYOUT_PADDING_KIND_COUNT; index++) {
        robo_layout_padding_kind_t kind = (robo_layout_padding_kind_t)index;

        ROBO_CHECK_EQ_INT(robo_layout_padding_kind_is_known(kind), 1);
        ROBO_CHECK(robo_layout_padding_kind_name(kind) != NULL);
    }
    ROBO_CHECK_EQ_INT(robo_layout_padding_kind_is_known((robo_layout_padding_kind_t)3), 0);
    ROBO_CHECK_EQ_INT(robo_layout_padding_kind_is_known((robo_layout_padding_kind_t)77), 0);
    ROBO_CHECK_STR_EQ(robo_layout_padding_kind_name(ROBO_LAYOUT_PADDING_ABSENT), "absent");
    ROBO_CHECK_STR_EQ(robo_layout_padding_kind_name(ROBO_LAYOUT_PADDING_RESOLVED), "resolved");
    ROBO_CHECK_STR_EQ(robo_layout_padding_kind_name(ROBO_LAYOUT_PADDING_UNRESOLVED),
                      "unresolved");
    ROBO_CHECK_STR_EQ(robo_layout_padding_kind_name((robo_layout_padding_kind_t)3), "unknown");
    ROBO_CHECK(robo_layout_padding_kind_name((robo_layout_padding_kind_t)77) != NULL);
}

static void test_paddings_one_side(void)
{
    robo_layout_padding_t zero = robo_layout_padding_zero();
    robo_layout_padding_t absent = robo_layout_padding_absent();
    robo_layout_padding_t resolved = robo_layout_padding_resolved(12);
    robo_layout_padding_t unresolved = robo_layout_padding_unresolved();
    robo_gfx_pixel_x_t length = 0;

    layout_fixture();

    /* `padding: 0` is an inset an author asked not to have; `ABSENT` is a side nobody
     * declared. Both are used as zero and they are different records -- the entire
     * reason this vocabulary has three kinds. */
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_resolved(&zero), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_absent(&zero), 0);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_valid(&zero), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_length(&zero, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 0);
    ROBO_CHECK_EQ_INT(robo_layout_padding_equal(&zero, &absent), 0);

    ROBO_CHECK_EQ_INT(robo_layout_padding_is_absent(&absent), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_resolved(&absent), 0);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_unresolved(&absent), 0);
    ROBO_CHECK_EQ_INT(robo_layout_padding_length(&absent, &length), ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_layout_padding_is_resolved(&resolved), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_length(&resolved, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 12);
    ROBO_CHECK_EQ_INT(robo_layout_padding_equal(&resolved, &zero), 0);
    {
        robo_layout_padding_t same = robo_layout_padding_resolved(12);

        ROBO_CHECK_EQ_INT(robo_layout_padding_equal(&resolved, &same), 1);
    }

    ROBO_CHECK_EQ_INT(robo_layout_padding_is_unresolved(&unresolved), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_length(&unresolved, &length), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_valid(&unresolved), 1);

    /*
     * And the one check this vocabulary has that 10.9's does not: a padding is a
     * distance *inward*, so a negative one is not a small padding -- it is a record this
     * build will not put into a size. Checked in the predicate rather than only at the
     * seam, because a caller may build the record itself.
     */
    {
        robo_layout_padding_t negative = robo_layout_padding_resolved(-1);
        robo_layout_padding_t positive = robo_layout_padding_resolved(1);

        ROBO_CHECK_EQ_INT(robo_layout_padding_is_valid(&negative), 0);
        ROBO_CHECK_EQ_INT(robo_layout_padding_is_valid(&positive), 1);
    }

    /* An unknown *kind* is not a record either, and that is a separate failure from a
     * bad length: one is a value nobody defined and the other is a value this property
     * does not allow. */
    {
        robo_layout_padding_t bogus = resolved;

        bogus.kind = (robo_layout_padding_kind_t)9;
        ROBO_CHECK_EQ_INT(robo_layout_padding_is_valid(&bogus), 0);
        ROBO_CHECK_EQ_INT(robo_layout_padding_is_resolved(&bogus), 0);
        ROBO_CHECK_EQ_INT(robo_layout_padding_is_absent(&bogus), 0);
        ROBO_CHECK_EQ_INT(robo_layout_padding_is_unresolved(&bogus), 0);
    }

    /* NULL is never a padding: every predicate answers "no" and every door refuses. */
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_resolved(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_absent(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_unresolved(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_padding_length(NULL, &length), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_padding_length(&resolved, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_padding_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_equal(&resolved, NULL), 0);
}

static void test_paddings_sides(void)
{
    robo_layout_paddings_t paddings = layout_padding_quad(1, 2, 3, 4);
    robo_layout_paddings_t same = layout_padding_quad(1, 2, 3, 4);
    robo_layout_paddings_t other = layout_padding_quad(1, 2, 3, 5);
    robo_layout_padding_t side;
    size_t index;

    layout_fixture();

    ROBO_CHECK_EQ_INT(robo_layout_paddings_is_valid(&paddings), 1);
    ROBO_CHECK_EQ_INT(robo_layout_paddings_equal(&paddings, &same), 1);
    ROBO_CHECK_EQ_INT(robo_layout_paddings_equal(&paddings, &other), 0);

    /*
     * Every side through the one door, in the order CSS writes them: top, right,
     * bottom, left. The enum is borrowed from 9.11 rather than restated, and this case is
     * what makes that a fact -- a transposed `padding: 1px 2px 3px 4px` would be two
     * vocabularies disagreeing about which side is which.
     */
    for (index = 0u; index < ROBO_CSS_BOX_SIDE_COUNT; index++) {
        robo_css_box_side_t which = (robo_css_box_side_t)index;

        ROBO_CHECK_EQ_INT(robo_layout_paddings_side(&paddings, which, &side), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_padding_is_resolved(&side), 1);
        switch (which) {
        case ROBO_CSS_BOX_SIDE_TOP:
            ROBO_CHECK_EQ_INT(side.length, 1);
            break;
        case ROBO_CSS_BOX_SIDE_RIGHT:
            ROBO_CHECK_EQ_INT(side.length, 2);
            break;
        case ROBO_CSS_BOX_SIDE_BOTTOM:
            ROBO_CHECK_EQ_INT(side.length, 3);
            break;
        case ROBO_CSS_BOX_SIDE_LEFT:
        default:
            ROBO_CHECK_EQ_INT(side.length, 4);
            break;
        }
    }

    /* A side that is not one of the four is a *miss*, not a fourth answer: the record
     * has four sides and a caller asking for a fifth has a bug. */
    side = robo_layout_padding_zero();
    ROBO_CHECK_EQ_INT(robo_layout_paddings_side(&paddings, (robo_css_box_side_t)4, &side),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_paddings_side(&paddings, (robo_css_box_side_t)99, &side),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_paddings_side(NULL, ROBO_CSS_BOX_SIDE_TOP, &side),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_paddings_side(&paddings, ROBO_CSS_BOX_SIDE_TOP, NULL),
                      ROBO_INVALID_ARGUMENT);

    /*
     * A cleared record is four `ABSENT` sides and not four zeros, which is asserted by
     * the *kind* rather than by the number: both read as zero in geometry, and only one
     * of them is a declaration.
     */
    {
        robo_layout_paddings_t cleared;

        robo_layout_paddings_init(&cleared);
        ROBO_CHECK_EQ_INT(robo_layout_paddings_is_valid(&cleared), 1);
        ROBO_CHECK_EQ_INT(robo_layout_paddings_equal(&cleared, &paddings), 0);
        for (index = 0u; index < ROBO_CSS_BOX_SIDE_COUNT; index++) {
            ROBO_CHECK_EQ_INT(robo_layout_paddings_side(&cleared,
                                                        (robo_css_box_side_t)index, &side),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_layout_padding_is_absent(&side), 1);
        }
        /* And `is_valid` is about the record rather than about whether anybody wrote
         * anything: one bad side makes the whole record one this build will not size a box
         * with. */
        cleared.left = robo_layout_padding_resolved(-3);
        ROBO_CHECK_EQ_INT(robo_layout_paddings_is_valid(&cleared), 0);
    }

    ROBO_CHECK_EQ_INT(robo_layout_paddings_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_paddings_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_paddings_equal(&paddings, NULL), 0);
}

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

static void test_paddings_seam(void)
{
    layout_padding_rule_t rule;
    robo_css_computed_style_t *style;
    robo_layout_paddings_t paddings;
    robo_layout_padding_edges_t edges;
    robo_layout_padding_t side;
    size_t index;

    layout_fixture();

    /*
     * A px length, a zero, a percentage and a relative unit: two sides this build can use
     * and two it cannot. The percentage is the interesting one -- CSS resolves
     * `padding: 5%` against the containing block's *width* for every side including the
     * vertical ones, so this build arguably could; it does not, because 10.8 left every
     * percentage unresolved and resolving one here would make `padding: 5%` the only
     * percentage in the module that means something. It is `UNRESOLVED`, used as zero and
     * *counted*.
     */
    layout_padding_rule_init(&rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&rule, "padding-top", 10.0);
    layout_padding_rule_px(&rule, "padding-right", 0.0);
    layout_padding_rule_percentage(&rule, "padding-bottom", 5.0);
    layout_padding_rule_em(&rule, "padding-left", 3.0);
    style = layout_padding_style_build(&rule);

    paddings = robo_layout_paddings_of(style);
    ROBO_CHECK_EQ_INT(robo_layout_paddings_is_valid(&paddings), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_resolved(&paddings.top), 1);
    ROBO_CHECK_EQ_INT(paddings.top.length, 10);
    /* `padding-right: 0` is a declaration and is counted as one -- the distinction the
     * `ABSENT` kind exists for. */
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_resolved(&paddings.right), 1);
    ROBO_CHECK_EQ_INT(paddings.right.length, 0);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_unresolved(&paddings.bottom), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_unresolved(&paddings.left), 1);

    ROBO_CHECK_EQ_INT(robo_layout_padding_resolve(&paddings, &edges, NULL), ROBO_OK);
    layout_padding_expect_edges(&edges, 10, 0, 0, 0, 2u, 2u);

    robo_css_computed_style_destroy(style);

    /*
     * And the three other shapes a declaration arrives in. `auto` is not a padding value
     * at all -- the grammar has no such production, and 9.11 rejects `padding: auto`
     * outright -- so a style carrying one has violated its own grammar; it is counted as a
     * declaration this build cannot use rather than given a kind of its own. A *flagged*
     * px length is what a `var()` or a `calc()` becomes: intact, and not a value yet. And
     * a **negative** length is the declaration no box dimension and no padding accepts,
     * which is the one respect in which this reader differs from 10.9's.
     */
    layout_padding_rule_init(&rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_keyword(&rule, "padding-top", "auto");
    layout_padding_rule_unresolved(&rule, "padding-right", 4.0);
    layout_padding_rule_negative(&rule, "padding-bottom", -6.0);
    layout_padding_rule_px(&rule, "padding-left", 7.6);
    style = layout_padding_style_build(&rule);

    paddings = robo_layout_paddings_of(style);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_unresolved(&paddings.top), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_unresolved(&paddings.right), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_unresolved(&paddings.bottom), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_resolved(&paddings.left), 1);
    /* Rounded through 10.8's door, half away from zero: `padding: 7.6px` is 8, which is
     * what a browser does with it too. */
    ROBO_CHECK_EQ_INT(paddings.left.length, 8);

    /* And the declaration the seam refused is still in the style, unchanged: refusing a
     * value is not editing the document. */
    ROBO_CHECK_EQ_INT(layout_padding_entry_px(style, "padding-bottom"), -6);

    ROBO_CHECK_EQ_INT(robo_layout_padding_resolve(&paddings, &edges, NULL), ROBO_OK);
    layout_padding_expect_edges(&edges, 0, 0, 0, 8, 1u, 3u);

    robo_css_computed_style_destroy(style);

    /* A rule that declares nothing: four `ABSENT` sides, which is what `padding: 0` is
     * *not*. */
    layout_padding_rule_init(&rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    style = layout_padding_style_build(&rule);
    paddings = robo_layout_paddings_of(style);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_absent(&paddings.top), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_absent(&paddings.right), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_absent(&paddings.bottom), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_is_absent(&paddings.left), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_resolve(&paddings, &edges, NULL), ROBO_OK);
    layout_padding_expect_edges(&edges, 0, 0, 0, 0, 0u, 0u);
    robo_css_computed_style_destroy(style);

    /*
     * And a box with **no style at all** has made no declarations, which is an answer
     * rather than a failure: the same four `ABSENT` sides a cleared record holds.
     */
    paddings = robo_layout_paddings_of(NULL);
    ROBO_CHECK_EQ_INT(robo_layout_paddings_is_valid(&paddings), 1);
    for (index = 0u; index < ROBO_CSS_BOX_SIDE_COUNT; index++) {
        ROBO_CHECK_EQ_INT(robo_layout_paddings_side(&paddings, (robo_css_box_side_t)index,
                                                    &side),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_padding_is_absent(&side), 1);
    }
}

/* ------------------------------------------------------------------ */
/* The used values                                                     */
/* ------------------------------------------------------------------ */

static void test_paddings_edges_record(void)
{
    robo_layout_padding_edges_t edges;
    robo_layout_padding_edges_t other;
    robo_layout_paddings_t paddings;

    layout_fixture();

    robo_layout_padding_edges_init(&edges);
    ROBO_CHECK_EQ_INT(edges.top, 0);
    ROBO_CHECK_EQ_INT(edges.right, 0);
    ROBO_CHECK_EQ_INT(edges.bottom, 0);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.resolved, 0u);
    ROBO_CHECK_EQ_INT(edges.unresolved, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_is_valid(&edges), 1);

    other = edges;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_equal(&edges, &other), 1);
    other.unresolved = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_equal(&edges, &other), 0);
    /* The counters are part of the record rather than decoration: two resolutions that
     * agreed about the four numbers and disagreed about how many declarations they could
     * not use would be two answers to one document. */
    other.left = 0;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_is_valid(&other), 1);

    /*
     * Invalid records, and each is a different way for one to be wrong: a negative side
     * is a size no renderer can draw, a count above four is a partition that does not add
     * up, and a *pair* of counts whose sum exceeds four is a record some later arithmetic
     * built wrong. The door that derives a size refuses all three -- here, so that the
     * refusal happens before a box is sized.
     */
    edges = layout_padding_edges(1, 2, 3, 4);
    edges.left = -1;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_is_valid(&edges), 0);
    edges = layout_padding_edges(1, 2, 3, 4);
    edges.resolved = 5u;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_is_valid(&edges), 0);
    edges = layout_padding_edges(1, 2, 3, 4);
    edges.resolved = 3u;
    edges.unresolved = 2u;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_is_valid(&edges), 0);
    edges.unresolved = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_is_valid(&edges), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_equal(&edges, NULL), 0);

    /*
     * The one policy, driven at its boundary: a side nobody declared and a side declared
     * with a value this build cannot use both come out as **zero** and are told apart only
     * by the counters -- which is what makes the counters the honest part of the record.
     */
    paddings = layout_padding_quad(1, 2, 3, 4);
    ROBO_CHECK_EQ_INT(robo_layout_padding_resolve(&paddings, &edges, NULL), ROBO_OK);
    layout_padding_expect_edges(&edges, 1, 2, 3, 4, 4u, 0u);

    paddings = layout_padding_quad(1, 2, 3, 4);
    paddings.bottom = robo_layout_padding_absent();
    paddings.left = robo_layout_padding_unresolved();
    ROBO_CHECK_EQ_INT(robo_layout_padding_resolve(&paddings, &edges, NULL), ROBO_OK);
    layout_padding_expect_edges(&edges, 1, 2, 0, 0, 2u, 1u);

    /* Refusals: the argument boundary, and nothing else -- this function cannot fail on a
     * document, because every declaration has a stated answer. */
    ROBO_CHECK_EQ_INT(robo_layout_padding_resolve(NULL, &edges, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_padding_resolve(&paddings, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
}

static void test_paddings_derived_boxes(void)
{
    robo_layout_padding_edges_t edges = layout_padding_edges(0, 0, 0, 0);
    robo_layout_padding_edges_t invalid;
    robo_layout_size_t content;
    robo_layout_size_t box;
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;
    int32_t width = 0;
    int32_t sentinel = 4242;

    layout_fixture();

    /*
     * The content box a border box leaves -- the first derived number, and the one an
     * `auto` width goes through. CSS's constraint equation has a padding term on each
     * side, so a box with `padding: 10px` and no declared width is exactly as wide as its
     * containing block rather than 20px wider.
     */
    edges = layout_padding_edges(0, 0, 0, 0);
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_content_width(&edges, 800, &width, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(width, 800);

    edges = layout_padding_edges(0, 20, 0, 10);
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_content_width(&edges, 800, &width, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(width, 770);

    /*
     * Floored at zero, once, and *not* refused: a border box narrower than its own
     * padding has no room for contents, and "no room" is a width of zero rather than a
     * negative one -- a negative available width would make every `auto` dimension in the
     * subtree depend on which of two impossible numbers a subtraction produced.
     */
    edges = layout_padding_edges(0, 5, 0, 10);
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_content_width(&edges, 7, &width, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(width, 0);

    /* NULL arguments are refused *without* a diagnostic the caller did not ask for, and
     * the out is left alone when the record is refused. */
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_content_width(NULL, 800, &width, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_content_width(&edges, 800, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    invalid = layout_padding_edges(0, 0, 0, -1);
    width = sentinel;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_content_width(&invalid, 800, &width, &diag),
                      ROBO_ERROR);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_PADDING);
    /* The refused call wrote nothing: a caller that ignored the code reads its own
     * sentinel rather than a number derived from a record this module never produces. */
    ROBO_CHECK_EQ_INT(width, sentinel);

    /*
     * The border box a content box asks for -- the second derived number, and the phase in
     * one call: the content box plus the padding on each axis, which is what a result's
     * `size` carries. Both axes at once because a size is what a result holds.
     */
    edges = layout_padding_edges(5, 5, 5, 5);
    content.width = 100;
    content.height = 50;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_padding_box(&edges, content, &box, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(box.width, 110);
    ROBO_CHECK_EQ_INT(box.height, 60);

    /* Asymmetric, so the two axes are not one answer: top/bottom 1 and 3, left/right 4 and
     * 2 -- the border box is the content plus each pair. */
    edges = layout_padding_edges(1, 2, 3, 4);
    content.width = 100;
    content.height = 50;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_padding_box(&edges, content, &box, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(box.width, 106);
    ROBO_CHECK_EQ_INT(box.height, 54);

    /*
     * And it saturates rather than wrapping. A content box already at the geometry's
     * maximum plus a padding is a document an author can write -- `width: 2147483647px`
     * with `padding: 1px` -- and the honest answer is a size that saturates, not a
     * negative rectangle a renderer would draw inside out. The clamp is on the *sum*, so
     * the box keeps the padding it cannot express rather than losing it to a subtraction.
     */
    edges = layout_padding_edges(1, 1, 1, 1);
    content.width = 2147483647;
    content.height = 2147483647;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_padding_box(&edges, content, &box, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(box.width, 2147483647);
    ROBO_CHECK_EQ_INT(box.height, 2147483647);

    /* Refusals: NULL, a negative content box (a box's own extent is never negative), and
     * a record no size can be derived from. */
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_padding_box(NULL, content, &box, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_padding_box(&edges, content, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    content.width = -1;
    content.height = 50;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_padding_box(&edges, content, &box, NULL),
                      ROBO_INVALID_ARGUMENT);
    content.width = 100;
    ROBO_CHECK_EQ_INT(robo_layout_padding_edges_padding_box(&invalid, content, &box, &diag),
                      ROBO_ERROR);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_PADDING);
}

/* ------------------------------------------------------------------ */
/* Block layout                                                        */
/* ------------------------------------------------------------------ */

static void test_paddings_block_declared_width(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_padding_edges_t edges;
    robo_layout_point_t origin;
    robo_layout_size_t size;

    layout_fixture();

    /*
     * `html` and `body` are declared **block**, and that is a fact about this fixture
     * rather than about padding: an element nothing declared for computes to `display:
     * inline` (9.12's initial value, and the point of `layout_padding_style_lookup`
     * answering NULL), so a document that declared only the subject would put the
     * subject's ancestors in the inline flow and never reach the subject at all.
     */
    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "width", 200.0);
    layout_padding_rule_px(&div_rule, "height", 100.0);
    layout_padding_rule_shorthand(&div_rule, 5.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_padding_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, &stats);

    /*
     * The phase in one assertion, and it is the `content-box` model rather than an
     * implementation detail: `width: 200px` is the **content** box (CSS's initial
     * `box-sizing`, which this build is), so the box a reader sees is 200 plus the padding
     * on each axis -- 210 by 110. A build that read the declaration as a *border* box
     * would give a child 190 and a result of 200 here.
     */
    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 210);
    ROBO_CHECK_EQ_INT(size.height, 110);

    /* And the padding never moved it: the origin is the containing block's content origin
     * plus the four *margins*, which here are all zero. That is the whole of the
     * difference between this phase and the last one, asserted where a reader can see it. */
    origin = layout_padding_origin_of(&fixture, layout_padding_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(origin.y, 0);

    edges = layout_padding_edges_of(&fixture, layout_padding_box_for(&fixture, "div"));
    layout_padding_expect_edges(&edges, 5, 5, 5, 5, 4u, 0u);

    /* `<body>` is as tall as the child's margin box, which spans the padding because the
     * padding is inside the border box the margin box is built around. */
    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "body"));
    ROBO_CHECK_EQ_INT(size.width, 800);
    ROBO_CHECK_EQ_INT(size.height, 110);

    /* `padding: 5px` is four declarations one layer down and four sides of geometry here,
     * while `html` and `body` declare none -- so the count is four and not twelve, which is
     * the whole reason an `ABSENT` side is counted nowhere. */
    ROBO_CHECK_EQ_INT(stats.block.paddings_applied, 4u);
    ROBO_CHECK_EQ_INT(stats.block.paddings_unresolved, 0u);

    layout_padding_end(&fixture);
}

static void test_paddings_block_auto_width(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t rules[3];
    robo_layout_padding_edges_t edges;
    robo_layout_size_t size;
    robo_layout_point_t origin;

    layout_fixture();

    /*
     * The second consequence of the model, and the one a build could plausibly get wrong
     * without any other case noticing: an `auto` width fills what the margins **and the
     * padding** left, because CSS's constraint equation has a term for each. So a box with
     * `padding: 10px` and no declared width is exactly as wide as its containing block --
     * 800 -- rather than 820, and the padding comes out of the *contents'* room: the content
     * box is 780.
     */
    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "height", 10.0);
    layout_padding_rule_shorthand(&div_rule, 10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_padding_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 800);
    ROBO_CHECK_EQ_INT(size.height, 30);

    /* The content box, recovered the one way a caller can: the border box less the four
     * sides. That is the arithmetic this phase added so that nobody has to re-derive it,
     * and asserting it here is what makes "the padding came out of the contents' room"
     * checkable rather than implied. */
    edges = layout_padding_edges_of(&fixture, layout_padding_box_for(&fixture, "div"));
    layout_padding_expect_edges(&edges, 10, 10, 10, 10, 4u, 0u);
    ROBO_CHECK_EQ_INT(size.width - (edges.left + edges.right), 780);
    ROBO_CHECK_EQ_INT(size.height - (edges.top + edges.bottom), 10);

    origin = layout_padding_origin_of(&fixture, layout_padding_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(origin.y, 0);

    layout_padding_end(&fixture);
}

static void test_paddings_block_sides(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t rules[3];
    robo_layout_padding_edges_t edges;
    robo_layout_size_t size;
    robo_layout_size_t owner;
    robo_layout_point_t origin;
    robo_layout_node_id_t div_id;

    layout_fixture();

    /*
     * Four different sides, so that a transposed pair is caught by the *geometry* rather
     * than only by the record: the border box is `width + right + left` by
     * `height + top + bottom`, and a build that had swapped left and right would give the
     * same width and a different content origin.
     */
    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "width", 100.0);
    layout_padding_rule_px(&div_rule, "height", 50.0);
    layout_padding_rule_px(&div_rule, "padding-top", 1.0);
    layout_padding_rule_px(&div_rule, "padding-right", 2.0);
    layout_padding_rule_px(&div_rule, "padding-bottom", 3.0);
    layout_padding_rule_px(&div_rule, "padding-left", 4.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_padding_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    div_id = layout_padding_box_for(&fixture, "div");
    edges = layout_padding_edges_of(&fixture, div_id);
    layout_padding_expect_edges(&edges, 1, 2, 3, 4, 4u, 0u);

    size = layout_padding_size_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(size.width, 106);
    ROBO_CHECK_EQ_INT(size.height, 54);

    /* Still where its margins put it, which is nowhere: a padding is *inside* the box and
     * adds to its height without moving the box below it by any amount of its own. */
    origin = layout_padding_origin_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(origin.y, 0);

    /* The content origin a later phase will want is the box's origin plus the top and left
     * padding -- and it is derived from the record rather than stored twice, which is what
     * the door is for. */
    ROBO_CHECK_EQ_INT(origin.x + edges.left, 4);
    ROBO_CHECK_EQ_INT(origin.y + edges.top, 1);

    owner = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "body"));
    ROBO_CHECK_EQ_INT(owner.height, 54);

    layout_padding_end(&fixture);
}

static void test_paddings_block_vertical(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t p_rule;
    layout_padding_rule_t rules[4];
    robo_layout_point_t origin;
    robo_layout_size_t size;
    robo_layout_node_id_t div_id;

    layout_fixture();

    /*
     * Vertical padding, in the two directions it can be seen. Top and bottom padding grow
     * the box the author sized in the *block* direction -- which is the one thing this
     * phase changes about a height -- and the next sibling therefore starts lower, because
     * what the cursor advances by is this box's margin box and the padding is inside it.
     */
    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "height", 10.0);
    layout_padding_rule_px(&div_rule, "padding-top", 5.0);
    layout_padding_rule_px(&div_rule, "padding-bottom", 7.0);
    layout_padding_rule_init(&p_rule, "p", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&p_rule, "height", 10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = p_rule;

    layout_padding_begin(&fixture, "<html><body><div></div><p></p></body></html>", rules, 4u);
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    div_id = layout_padding_box_for(&fixture, "div");
    size = layout_padding_size_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(size.height, 22);

    /* The second block starts at the first one's *margin box*, which is 22 because the
     * padding is inside the border box rather than beside it. A build that had treated the
     * padding as external spacing would put it at 22 too -- which is why the case that
     * tells them apart is `layout.paddings.block_declared_width`: here the two agree. */
    origin = layout_padding_origin_of(&fixture, layout_padding_box_for(&fixture, "p"));
    ROBO_CHECK_EQ_INT(origin.y, 22);
    ROBO_CHECK_EQ_INT(origin.x, 0);

    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "body"));
    ROBO_CHECK_EQ_INT(size.height, 32);

    layout_padding_end(&fixture);
}

static void test_paddings_block_stacking(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t rules[3];
    robo_layout_point_t origin;
    robo_layout_node_id_t div_id;

    layout_fixture();

    /*
     * And the other way round, so that both sides of the vertical pair are seen: a box with
     * `padding-top` only puts its *contents* 5px in rather than itself 5px down. A child
     * block therefore starts at the parent's content origin, which is not shifted in the
     * child's coordinates -- the origin a child is placed in *is* its containing block's
     * content origin (RoboLayoutResult.h), so nothing has to be added for the child and
     * nothing may be.
     *
     * The parent's own declared height grows by the padding, which is where a build that
     * confused the two would fail.
     */
    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "padding-top", 5.0);
    layout_padding_rule_px(&div_rule, "height", 10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_padding_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    div_id = layout_padding_box_for(&fixture, "div");
    origin = layout_padding_origin_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(origin.y, 0);

    /* A parent with no declared height and a padded top is as tall as its contents plus the
     * padding: the extent the cursor reached is 0 here, so the box is exactly the padding.
     */
    {
        layout_padding_rule_t tall_rule;
        layout_padding_fixture_t second;
        layout_padding_rule_t rules2[3];

        layout_fixture();
        layout_padding_rule_init(&tall_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_padding_rule_px(&tall_rule, "padding-top", 5.0);
        layout_padding_rule_px(&tall_rule, "padding-bottom", 7.0);
        rules2[0] = html_rule;
        rules2[1] = body_rule;
        rules2[2] = tall_rule;
        layout_padding_begin(&second, "<html><body><div></div></body></html>", rules2, 3u);
        layout_padding_pass(&second, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
        ROBO_CHECK_EQ_INT(layout_padding_size_of(&second,
                                                 layout_padding_box_for(&second, "div")).height,
                          12);
        layout_padding_end(&second);
    }

    layout_padding_end(&fixture);
}

static void test_paddings_block_unresolved(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_padding_edges_t edges;
    robo_layout_size_t size;

    layout_fixture();

    /*
     * A document with declarations this build cannot use, which is a document and not a
     * failure: `padding-left: 5%`, a relative unit, and the `auto` the grammar does not
     * allow for this property. All three are used as **zero** and *counted*, so the box
     * lays out flush and the numbers say so -- which is the alternative to an author's `5%`
     * being silently a confident zero nobody can see.
     */
    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "width", 100.0);
    layout_padding_rule_px(&div_rule, "height", 10.0);
    layout_padding_rule_percentage(&div_rule, "padding-left", 5.0);
    layout_padding_rule_em(&div_rule, "padding-top", 3.0);
    layout_padding_rule_keyword(&div_rule, "padding-bottom", "auto");
    layout_padding_rule_px(&div_rule, "padding-right", 2.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_padding_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, &stats);

    edges = layout_padding_edges_of(&fixture, layout_padding_box_for(&fixture, "div"));
    layout_padding_expect_edges(&edges, 0, 2, 0, 0, 1u, 3u);

    /* The one usable side reaches the size and the three unusable ones do not: 100 + 2. */
    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 102);
    ROBO_CHECK_EQ_INT(size.height, 10);

    ROBO_CHECK_EQ_INT(stats.block.paddings_applied, 1u);
    ROBO_CHECK_EQ_INT(stats.block.paddings_unresolved, 3u);

    layout_padding_end(&fixture);
}

static void test_paddings_block_negative(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_padding_edges_t edges;
    robo_layout_size_t size;

    layout_fixture();

    /*
     * `padding: -10px` is invalid CSS, and the *one* parameter's worth of difference
     * between this reader and 10.9's is what it does with it: a margin uses a negative
     * length, a padding refuses it. Nothing else in the two readers differs -- one
     * classification, asked two ways.
     *
     * Asserted end to end rather than only at the seam, because "refused" has to mean the
     * *geometry* did not move: a build that had accepted the declaration and then clamped
     * it would pass a seam-level check and box a rectangle smaller than its own contents.
     */
    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "width", 100.0);
    layout_padding_rule_px(&div_rule, "height", 10.0);
    layout_padding_rule_negative(&div_rule, "padding-left", -10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_padding_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, &stats);

    edges = layout_padding_edges_of(&fixture, layout_padding_box_for(&fixture, "div"));
    layout_padding_expect_edges(&edges, 0, 0, 0, 0, 0u, 1u);
    ROBO_CHECK_EQ_INT(edges.left >= 0, 1);

    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 100);
    ROBO_CHECK_EQ_INT(size.height, 10);

    ROBO_CHECK_EQ_INT(stats.block.paddings_applied, 0u);
    ROBO_CHECK_EQ_INT(stats.block.paddings_unresolved, 1u);

    /* And an auto-width box with a refused padding is still exactly its containing block:
     * a refusal contributes zero rather than being carried as a negative that would have
     * made the box *wider* than its parent. */
    {
        layout_padding_fixture_t second;
        layout_padding_rule_t auto_rule;
        layout_padding_rule_t rules2[3];

        layout_fixture();
        layout_padding_rule_init(&auto_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_padding_rule_px(&auto_rule, "height", 10.0);
        layout_padding_rule_negative(&auto_rule, "padding-left", -400.0);
        layout_padding_rule_negative(&auto_rule, "padding-top", -400.0);
        rules2[0] = html_rule;
        rules2[1] = body_rule;
        rules2[2] = auto_rule;
        layout_padding_begin(&second, "<html><body><div></div></body></html>", rules2, 3u);
        layout_padding_pass(&second, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
        size = layout_padding_size_of(&second, layout_padding_box_for(&second, "div"));
        ROBO_CHECK_EQ_INT(size.width, 800);
        ROBO_CHECK_EQ_INT(size.height, 10);
        layout_padding_end(&second);
    }

    layout_padding_end(&fixture);
}

static void test_paddings_margin_interaction(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t rules[3];
    robo_layout_margin_edges_t margins;
    robo_layout_padding_edges_t padding;
    robo_layout_point_t origin;
    robo_layout_size_t size;
    robo_layout_size_t owner;
    robo_layout_node_id_t div_id;

    layout_fixture();

    /*
     * Both at once, on one box, because that is the pair an author writes and the pair a
     * build can most plausibly confuse: a margin is *outside* the box and a padding is
     * *inside* it, and the two produce different numbers for every door but `size`.
     *
     * `margin: 10px 0 0 20px`-shaped here as the four longhands, with `padding: 5px` and a
     * declared 200 by 100. The box sits at (20, 10) -- the margins -- and its size is
     * 210 by 110 -- the declaration plus the padding. A build that had folded the two
     * together would give one of those numbers to the wrong door and the other to the
     * wrong face.
     */
    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "width", 200.0);
    layout_padding_rule_px(&div_rule, "height", 100.0);
    layout_padding_rule_shorthand(&div_rule, 5.0);
    layout_padding_rule_px(&div_rule, "margin-top", 10.0);
    layout_padding_rule_px(&div_rule, "margin-left", 20.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_padding_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    div_id = layout_padding_box_for(&fixture, "div");
    origin = layout_padding_origin_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(origin.x, 20);
    ROBO_CHECK_EQ_INT(origin.y, 10);

    size = layout_padding_size_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(size.width, 210);
    ROBO_CHECK_EQ_INT(size.height, 110);

    /* The two records are separate facts about the box, and neither is derivable from the
     * other: the margins are on the *outside* of the result's origin and the padding is on
     * the *inside* of its size. */
    ROBO_CHECK_EQ_INT(robo_layout_object_margins(fixture.tree, div_id, &margins, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(margins.top, 10);
    ROBO_CHECK_EQ_INT(margins.left, 20);
    ROBO_CHECK_EQ_INT(margins.right, 0);
    ROBO_CHECK_EQ_INT(margins.bottom, 0);

    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture.tree, div_id, &padding, NULL),
                      ROBO_OK);
    layout_padding_expect_edges(&padding, 5, 5, 5, 5, 4u, 0u);

    /* The **content** origin, which is what a child of this box is placed against: the
     * margin origin plus the top and left padding. A build that had put the padding in the
     * same place as the margins would put a child at (20, 10) instead of (25, 15). */
    ROBO_CHECK_EQ_INT(origin.x + padding.left, 25);
    ROBO_CHECK_EQ_INT(origin.y + padding.top, 15);

    /* And the parent stacks by the margin box, which is now three boxes deep: 10 + 110 +
     * 0 -- the padding being inside the 110 rather than beside it. */
    owner = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "body"));
    ROBO_CHECK_EQ_INT(owner.height, 120);

    layout_padding_end(&fixture);
}

static void test_paddings_children_get_content_width(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t p_rule;
    layout_padding_rule_t rules[4];
    robo_layout_size_t size;

    layout_fixture();

    /*
     * The one number that travels *down*: a padded parent hands its children the **content**
     * box, not the border box. So a `<div>` with `width: 200px` and `padding: 10px` is 220
     * wide outside and gives its child 200 -- the declared number, because `content-box` is
     * what this build is.
     *
     * This is the case that fails for a build which had resolved a box's own width from the
     * declaration and then handed its children the *containing block's* width, or handed them
     * the border box: both are one line away and neither shows up in the parent's own size.
     */
    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "width", 200.0);
    layout_padding_rule_shorthand(&div_rule, 10.0);
    layout_padding_rule_init(&p_rule, "p", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&p_rule, "height", 10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = p_rule;

    layout_padding_begin(&fixture, "<html><body><div><p></p></div></body></html>", rules, 4u);
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 220);
    ROBO_CHECK_EQ_INT(size.height, 30);

    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "p"));
    ROBO_CHECK_EQ_INT(size.width, 200);
    ROBO_CHECK_EQ_INT(size.height, 10);

    layout_padding_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Inline layout                                                       */
/* ------------------------------------------------------------------ */

static void test_paddings_inline_atomic(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t i_rule;
    layout_padding_rule_t b_rule;
    layout_padding_rule_t rules[5];
    robo_layout_stats_t stats;
    robo_layout_point_t origin;
    robo_layout_size_t size;
    robo_layout_line_box_t line;
    robo_layout_line_id_t line_id = ROBO_LAYOUT_LINE_ID_NONE;
    layout_padding_rule_t widthless_rule;
    layout_padding_rule_t rules2[4];
    layout_padding_fixture_t second;

    layout_fixture();

    /*
     * An atomic inline's advance is its **border** box since this phase, which is the same
     * sentence the block pass gained one direction over: a padding is inside the box, so an
     * `inline-block` with `padding-left: 5px` and `padding-right: 15px` occupies 120 on the
     * line when it declares a 100-wide box, and the participant after it starts 120 further
     * on. A build that advanced by the content box alone would start the second at 100.
     */
    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "height", 30.0);
    layout_padding_rule_init(&i_rule, "i", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_padding_rule_px(&i_rule, "width", 100.0);
    layout_padding_rule_px(&i_rule, "height", 20.0);
    layout_padding_rule_px(&i_rule, "padding-left", 5.0);
    layout_padding_rule_px(&i_rule, "padding-right", 15.0);
    layout_padding_rule_init(&b_rule, "b", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_padding_rule_px(&b_rule, "width", 50.0);
    layout_padding_rule_px(&b_rule, "height", 10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = i_rule;
    rules[4] = b_rule;

    layout_padding_begin(&fixture, "<html><body><div><i></i><b></b></div></body></html>",
                         rules, 5u);
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, &stats);

    origin = layout_padding_origin_of(&fixture, layout_padding_box_for(&fixture, "i"));
    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(size.width, 120);
    ROBO_CHECK_EQ_INT(size.height, 20);

    origin = layout_padding_origin_of(&fixture, layout_padding_box_for(&fixture, "b"));
    ROBO_CHECK_EQ_INT(origin.x, 120);

    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture.tree, 0u, &line_id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, line_id, &line, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(line.size.width, 120 + 50);
    ROBO_CHECK_EQ_INT(line.members, 2u);

    /* The line's floor is the tallest participant's border box, and the baseline of an
     * `inline-block` with no in-flow line box of its own is its bottom margin edge -- so a
     * padded participant makes the line taller by its padding too. */
    ROBO_CHECK_EQ_INT(line.size.height, 20);
    ROBO_CHECK_EQ_INT(line.baseline.ascent, 20);
    ROBO_CHECK_EQ_INT(line.baseline.descent, 0);

    ROBO_CHECK_EQ_INT(stats.inline_flow.paddings_applied, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.paddings_unresolved, 0u);

    layout_padding_end(&fixture);

    /*
     * And an `auto` width fills what the padding left, which is this direction's copy of the
     * block pass's rule: 800 less 20 less 20 is the 760 inside a participant that is exactly
     * as wide as its containing block. Four sides rather than two, because an atomic inline
     * is sized in both directions by the same door.
     */
    layout_fixture();
    layout_padding_rule_init(&widthless_rule, "i", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_padding_rule_px(&widthless_rule, "height", 20.0);
    layout_padding_rule_px(&widthless_rule, "padding-left", 20.0);
    layout_padding_rule_px(&widthless_rule, "padding-right", 20.0);
    rules2[0] = html_rule;
    rules2[1] = body_rule;
    rules2[2] = div_rule;
    rules2[3] = widthless_rule;
    layout_padding_begin(&second, "<html><body><div><i></i></div></body></html>", rules2, 4u);
    layout_padding_pass(&second, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
    size = layout_padding_size_of(&second, layout_padding_box_for(&second, "i"));
    ROBO_CHECK_EQ_INT(size.width, 800);
    layout_padding_end(&second);
}

static void test_paddings_inline_transparent_not_applied(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t i_rule;
    layout_padding_rule_t rules[4];
    robo_layout_stats_t stats;
    robo_layout_size_t size;
    robo_layout_node_id_t i_id;

    layout_fixture();

    /*
     * A **transparent inline box** with a padding, which is the one participant this build
     * deliberately does not pad. CSS does grow such a box's inline advance by its horizontal
     * padding and its line box by its vertical padding; this build applies neither, and the
     * reason is not economy: an inline box's rectangle is derived from its *contents* rather
     * than placed, so padding on it is a rule about fragmenting an inline box across lines,
     * which is the anonymous-box and line-breaking work this module has not done.
     *
     * Asserted as a value -- zero extent, no padding applied, and the door saying so -- so
     * that the goal which implements it changes an assertion rather than discovering one.
     */
    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&i_rule, "i", ROBO_CSS_DISPLAY_INLINE);
    layout_padding_rule_shorthand(&i_rule, 20.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = i_rule;

    layout_padding_begin(&fixture, "<html><body><div><i></i></div></body></html>", rules, 4u);
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, &stats);

    i_id = layout_padding_box_for(&fixture, "i");
    size = layout_padding_size_of(&fixture, i_id);
    ROBO_CHECK_EQ_INT(size.width, 0);
    ROBO_CHECK_EQ_INT(size.height, 0);

    /* And the box it is inside is as tall as the line, which is zero -- a padding on a
     * transparent inline box changes no geometry at all in this build. */
    ROBO_CHECK_EQ_INT(layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "div"))
                          .height,
                      0);

    /* The door *says* so rather than answering a zero that reads like an honoured
     * declaration that came to nothing. */
    {
        robo_layout_padding_edges_t edges;
        robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;

        edges.left = 77;
        ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture.tree, i_id, &edges, &diag),
                          ROBO_NOT_FOUND);
        /* Cleared on the way out: a caller that ignores the code reads a stated absence. */
        ROBO_CHECK_EQ_INT(edges.left, 0);
        ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_OBJECT);
    }

    ROBO_CHECK_EQ_INT(stats.inline_flow.paddings_applied, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.non_atomic_placed, 1u);

    layout_padding_end(&fixture);

    /*
     * And the contrast, in one number: the same box declared `inline-block` *does* get its
     * padding, so "not applied" is a fact about the transparent box rather than about this
     * build's arithmetic. Here the participant's border box is 40 by 40 -- a 20px padding on
     * each of the four sides of a box with no content extent of its own -- and the line is
     * 40 tall because that is its floor.
     */
    {
        layout_padding_fixture_t second;
        layout_padding_rule_t atomic_rule;
        layout_padding_rule_t rules2[4];

        layout_fixture();
        layout_padding_rule_init(&atomic_rule, "i", ROBO_CSS_DISPLAY_INLINE_BLOCK);
        layout_padding_rule_shorthand(&atomic_rule, 20.0);
        rules2[0] = html_rule;
        rules2[1] = body_rule;
        rules2[2] = div_rule;
        rules2[3] = atomic_rule;
        layout_padding_begin(&second, "<html><body><div><i></i></div></body></html>", rules2,
                             4u);
        layout_padding_pass(&second, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
        size = layout_padding_size_of(&second, layout_padding_box_for(&second, "i"));
        ROBO_CHECK_EQ_INT(size.height, 40);
        ROBO_CHECK_EQ_INT(layout_padding_size_of(&second,
                                                 layout_padding_box_for(&second, "div")).height,
                          40);
        layout_padding_end(&second);
    }
}

/* ------------------------------------------------------------------ */
/* The read side                                                       */
/* ------------------------------------------------------------------ */

static void test_paddings_object_door(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t rules[3];
    robo_layout_padding_edges_t edges;
    robo_layout_node_id_t div_id;
    robo_layout_node_id_t text_id = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_node_id_t span_id;
    robo_layout_node_id_t inline_id;
    size_t count;
    size_t index;

    layout_fixture();

    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "padding-left", 7.0);
    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_padding_begin(
        &fixture, "<html><body><div>hello</div><span></span><i></i></body></html>", rules, 3u);
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);

    count = robo_layout_tree_object_count(fixture.tree);
    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_object_t object;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, index, &id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_get(fixture.tree, id, &object, NULL), ROBO_OK);
        if (object.kind == ROBO_LAYOUT_OBJECT_TEXT) {
            text_id = id;
        }
    }

    div_id = layout_padding_box_for(&fixture, "div");
    span_id = layout_padding_box_for(&fixture, "span");
    inline_id = layout_padding_box_for(&fixture, "i");

    /* A placed block box: the door answers, and answers the *used* values. */
    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture.tree, div_id, &edges, NULL), ROBO_OK);
    layout_padding_expect_edges(&edges, 0, 0, 0, 7, 1u, 0u);

    /*
     * A text run is not a box at all, and a transparent inline box is one CSS applies no
     * padding to -- both are *misses* rather than zeros, because a zero with a count of
     * resolved sides would tell a caller the author's padding had been honoured and come to
     * nothing.
     */
    ROBO_CHECK(text_id != ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture.tree, text_id, &edges, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.resolved, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture.tree, span_id, &edges, NULL),
                      ROBO_NOT_FOUND);

    /* An inline-level box in a *block* context is skipped rather than placed, so nothing has
     * sized it either. */
    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture.tree, inline_id, &edges, NULL),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_layout_object_padding(NULL, div_id, &edges, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture.tree, ROBO_LAYOUT_NODE_ID_NONE, &edges,
                                                NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture.tree, div_id, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture.tree,
                                                (robo_layout_node_id_t)(count + 99u), &edges,
                                                NULL),
                      ROBO_NOT_FOUND);

    layout_padding_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Re-layout, determinism and balance                                  */
/* ------------------------------------------------------------------ */

static void test_paddings_relayout(void)
{
    layout_padding_fixture_t fixture;
    layout_padding_rule_t html_rule;
    layout_padding_rule_t body_rule;
    layout_padding_rule_t div_rule;
    layout_padding_rule_t rules[3];
    robo_layout_point_t origin;
    robo_layout_point_t settled;
    robo_layout_size_t size;
    robo_layout_padding_edges_t edges;
    robo_layout_padding_edges_t again;

    layout_fixture();

    layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "width", 100.0);
    layout_padding_rule_px(&div_rule, "height", 10.0);
    layout_padding_rule_px(&div_rule, "padding-left", 5.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_padding_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    ROBO_CHECK_EQ_INT(layout_padding_entry_px(fixture.source.rules[2].style, "padding-left"),
                      5);

    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 105);

    /*
     * A second pass over an unchanged tree is the same document: the records agree field
     * for field, which is what makes "the padding layer holds no state between calls" a fact
     * rather than a hope.
     */
    {
        robo_layout_padding_edges_t first =
            layout_padding_edges_of(&fixture, layout_padding_box_for(&fixture, "div"));

        layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
        again = layout_padding_edges_of(&fixture, layout_padding_box_for(&fixture, "div"));
        ROBO_CHECK_EQ_INT(robo_layout_padding_edges_equal(&first, &again), 1);
    }

    /*
     * And a *changed* declaration is a changed document. The style is rebuilt with a
     * different `padding-left`, the tree is rebuilt against it, and the pass runs again --
     * and the box gets **wider while staying put**, which is the phase's shape in one pair
     * of assertions. A build that had grown the box from its content origin would move the
     * second number and this is the case that fails it.
     */
    layout_padding_rule_px(&div_rule, "padding-left", 40.0);
    layout_padding_style_replace(&fixture.source, "div", &div_rule);
    {
        robo_layout_build_options_t options;

        ROBO_CHECK_EQ_INT(
            layout_padding_entry_px(fixture.source.rules[2].style, "padding-left"), 40);
        robo_layout_build_options_init(&options);
        options.styles = &fixture.styles;
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture.tree, fixture.doc.id,
                                                              &options, NULL, NULL),
                          ROBO_OK);
    }
    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "div"));
    origin = layout_padding_origin_of(&fixture, layout_padding_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 140);
    ROBO_CHECK_EQ_INT(origin.x, 0);

    /* And a changed *viewport* leaves a **declared** width where it is: the declaration is
     * not a function of the containing block, so neither is the border box built around it.
     */
    layout_padding_pass(&fixture, 300, NULL);
    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "div"));
    settled = layout_padding_origin_of(&fixture, layout_padding_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 140);
    ROBO_CHECK_EQ_INT(settled.x, 0);
    ROBO_CHECK_EQ_INT(settled.y, 0);

    layout_padding_end(&fixture);

    /*
     * The other half of that pair, which is the one number that *does* follow the containing
     * block: an `auto` width fills what the padding left, so the same box without a declared
     * width is its containing block exactly -- 800 and then 300. This is what makes "a
     * padding never makes a box wider than its containing block" checkable rather than a
     * claim about one viewport.
     */
    layout_fixture();
    layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_padding_rule_px(&div_rule, "height", 10.0);
    layout_padding_rule_shorthand(&div_rule, 10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    layout_padding_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

    layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 800);
    edges = layout_padding_edges_of(&fixture, layout_padding_box_for(&fixture, "div"));

    layout_padding_pass(&fixture, 300, NULL);
    size = layout_padding_size_of(&fixture, layout_padding_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 300);
    ROBO_CHECK_EQ_INT(size.width - (edges.left + edges.right), 280);

    layout_padding_end(&fixture);
}

static void test_paddings_allocator_balance(void)
{
    size_t before = layout_live_blocks();
    size_t settled;
    int round;

    layout_fixture();

    for (round = 0; round < 3; round++) {
        layout_padding_fixture_t fixture;
        layout_padding_rule_t html_rule;
        layout_padding_rule_t body_rule;
        layout_padding_rule_t div_rule;
        layout_padding_rule_t rules[3];

        layout_padding_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
        layout_padding_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
        layout_padding_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_padding_rule_px(&div_rule, "width", 250.0);
        layout_padding_rule_px(&div_rule, "height", 20.0);
        layout_padding_rule_shorthand(&div_rule, 12.0);
        rules[0] = html_rule;
        rules[1] = body_rule;
        rules[2] = div_rule;

        layout_padding_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
        layout_padding_pass(&fixture, LAYOUT_PADDING_VIEWPORT_WIDTH, NULL);
        settled = layout_live_blocks();
        /* The padding layer allocates nothing of its own: the records are values on the
         * stack and on the object, and the pass adds no arena. */
        ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
        layout_padding_pass(&fixture, 400, NULL);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
        layout_padding_end(&fixture);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_layout_padding(void);

void robo_test_register_layout_padding(void)
{
    robo_test_register("layout.paddings.kind_vocabulary", test_paddings_kind_vocabulary);
    robo_test_register("layout.paddings.one_side", test_paddings_one_side);
    robo_test_register("layout.paddings.sides", test_paddings_sides);
    robo_test_register("layout.paddings.seam", test_paddings_seam);
    robo_test_register("layout.paddings.edges_record", test_paddings_edges_record);
    robo_test_register("layout.paddings.derived_boxes", test_paddings_derived_boxes);
    robo_test_register("layout.paddings.block_declared_width",
                       test_paddings_block_declared_width);
    robo_test_register("layout.paddings.block_auto_width", test_paddings_block_auto_width);
    robo_test_register("layout.paddings.block_sides", test_paddings_block_sides);
    robo_test_register("layout.paddings.block_vertical", test_paddings_block_vertical);
    robo_test_register("layout.paddings.block_stacking", test_paddings_block_stacking);
    robo_test_register("layout.paddings.block_unresolved", test_paddings_block_unresolved);
    robo_test_register("layout.paddings.block_negative", test_paddings_block_negative);
    robo_test_register("layout.paddings.margin_interaction",
                       test_paddings_margin_interaction);
    robo_test_register("layout.paddings.children_get_content_width",
                       test_paddings_children_get_content_width);
    robo_test_register("layout.paddings.inline_atomic", test_paddings_inline_atomic);
    robo_test_register("layout.paddings.inline_transparent_not_applied",
                       test_paddings_inline_transparent_not_applied);
    robo_test_register("layout.paddings.object_door", test_paddings_object_door);
    robo_test_register("layout.paddings.relayout", test_paddings_relayout);
    robo_test_register("layout.paddings.allocator_balance", test_paddings_allocator_balance);
}
