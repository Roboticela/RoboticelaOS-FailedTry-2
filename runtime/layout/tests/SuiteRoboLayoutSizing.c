/*
 * RoboLayout sizing suite (Phase 10.8) — layout.sizing.*
 *
 * Three things are under test and they are told apart deliberately:
 *
 *   the **vocabulary** -- the three dimension kinds, the four sources, and the
 *   predicates and names over them -- which is pure and needs no record at all;
 *
 *   the **record**: what a box is asked for and what it came out as, its doors'
 *   refusals, and the two resolvers -- which is where the phase's *rules* live and
 *   is therefore the largest group here;
 *
 *   the **pass**: a declared `width` reaching the geometry of a block box *and* the
 *   width its children are laid out into, which is the one thing 10.3 could not do
 *   and the one thing every earlier suite would have gone on passing without.
 *
 * The geometry cases drive a *fabricated* computed style rather than a stylesheet,
 * and that is not a shortcut: `min-width` and its three siblings are not properties
 * RoboCSS registers, so the only way to prove the clamping path is **live** rather
 * than merely present is to hand the pass a style that declares them. A case built
 * from a stylesheet could only prove that a property the property table does not know
 * does nothing, which is a fact about the table.
 *
 * Where a stylesheet *is* the subject -- `layout.sizing.block_declared_width` and its
 * neighbours -- the case goes through CSS, because the point there is that a real
 * declared `width` arrives.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fabricated style that can declare anything                        */
/* ------------------------------------------------------------------ */

/** One declaration to put on a fabricated style. */
typedef enum layout_sizing_decl_kind {
    LAYOUT_SIZING_DECL_PX = 0,
    LAYOUT_SIZING_DECL_PERCENTAGE = 1,
    LAYOUT_SIZING_DECL_EM = 2,
    LAYOUT_SIZING_DECL_KEYWORD = 3
} layout_sizing_decl_kind_t;

typedef struct layout_sizing_decl {
    const char *name;
    layout_sizing_decl_kind_t kind;
    double number;
    const char *keyword;
} layout_sizing_decl_t;

#define LAYOUT_SIZING_MAX_DECLARATIONS 8u
#define LAYOUT_SIZING_MAX_RULES 8u

typedef struct layout_sizing_rule {
    const char *tag;
    robo_css_display_t display;
    layout_sizing_decl_t declarations[LAYOUT_SIZING_MAX_DECLARATIONS];
    size_t declarations_count;
} layout_sizing_rule_t;

static void layout_sizing_rule_init(layout_sizing_rule_t *rule,
                                    const char *tag,
                                    robo_css_display_t display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static void layout_sizing_rule_add(layout_sizing_rule_t *rule,
                                   const char *name,
                                   layout_sizing_decl_kind_t kind,
                                   double number,
                                   const char *keyword)
{
    ROBO_CHECK_EQ_INT(rule->declarations_count < LAYOUT_SIZING_MAX_DECLARATIONS, 1);
    rule->declarations[rule->declarations_count].name = name;
    rule->declarations[rule->declarations_count].kind = kind;
    rule->declarations[rule->declarations_count].number = number;
    rule->declarations[rule->declarations_count].keyword = keyword;
    rule->declarations_count++;
}

static void layout_sizing_rule_px(layout_sizing_rule_t *rule, const char *name, double px)
{
    layout_sizing_rule_add(rule, name, LAYOUT_SIZING_DECL_PX, px, NULL);
}

static void layout_sizing_rule_percentage(layout_sizing_rule_t *rule,
                                          const char *name,
                                          double percent)
{
    layout_sizing_rule_add(rule, name, LAYOUT_SIZING_DECL_PERCENTAGE, percent, NULL);
}

static void layout_sizing_rule_em(layout_sizing_rule_t *rule, const char *name, double em)
{
    layout_sizing_rule_add(rule, name, LAYOUT_SIZING_DECL_EM, em, NULL);
}

static void layout_sizing_rule_keyword(layout_sizing_rule_t *rule,
                                       const char *name,
                                       const char *keyword)
{
    layout_sizing_rule_add(rule, name, LAYOUT_SIZING_DECL_KEYWORD, 0.0, keyword);
}

typedef struct layout_sizing_style {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
    size_t answers;
} layout_sizing_style_t;

typedef struct layout_sizing_source {
    layout_sizing_style_t rules[LAYOUT_SIZING_MAX_RULES];
    size_t count;
} layout_sizing_source_t;

static robo_css_unit_t layout_sizing_decl_unit(const layout_sizing_decl_t *declaration)
{
    return declaration->kind == LAYOUT_SIZING_DECL_EM ? ROBO_CSS_UNIT_EM : ROBO_CSS_UNIT_PX;
}

/**
 * Build the style a rule describes: a real DISPLAY value, plus whatever the rule
 * declares, all through 9.8's own makers and container.
 *
 * A fabricated struct would prove the pass reads a struct; this proves it reads a
 * computed CSS value, which is the only input it is allowed to have.
 */
static void layout_sizing_style_add(layout_sizing_source_t *source,
                                    const layout_sizing_rule_t *rule)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;
    size_t index;
    size_t slot;

    layout_fixture();
    ROBO_CHECK_EQ_INT(source->count < LAYOUT_SIZING_MAX_RULES, 1);
    slot = source->count;
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_display(&value, rule->display, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("display"),
                                                       0u, &specified, &value, NULL),
                      ROBO_OK);

    for (index = 0u; index < rule->declarations_count; index++) {
        const layout_sizing_decl_t *declaration = &rule->declarations[index];

        value = (robo_css_computed_value_t)ROBO_CSS_CLEARED;
        specified = (robo_css_value_t)ROBO_CSS_CLEARED;
        switch (declaration->kind) {
        case LAYOUT_SIZING_DECL_PERCENTAGE:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_percentage(&value,
                                                                     declaration->number, 0,
                                                                     NULL),
                              ROBO_OK);
            break;
        case LAYOUT_SIZING_DECL_KEYWORD:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                                  &value, robo_css_slice_from_text(declaration->keyword), NULL),
                              ROBO_OK);
            break;
        case LAYOUT_SIZING_DECL_EM:
        case LAYOUT_SIZING_DECL_PX:
        default:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(
                                  &value, declaration->number,
                                  layout_sizing_decl_unit(declaration), 0, NULL),
                              ROBO_OK);
            break;
        }
        ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(
                              style, ROBO_CSS_PROPERTY_ID_NONE,
                              robo_css_slice_from_text(declaration->name), 0u, &specified,
                              &value, NULL),
                          ROBO_OK);
    }

    strncpy(source->rules[slot].tag, rule->tag, sizeof(source->rules[slot].tag) - 1u);
    source->rules[slot].tag[sizeof(source->rules[slot].tag) - 1u] = '\0';
    source->rules[slot].style = style;
    source->rules[slot].answers = 0u;
    source->count++;
}

static const robo_css_computed_style_t *layout_sizing_style_lookup(
    void *userdata,
    const robo_html_node_handle_t *element)
{
    layout_sizing_source_t *source = (layout_sizing_source_t *)userdata;
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
    /* An element nothing was declared for: `auto` everywhere, and `inline` by CSS's
     * initial value -- which is what keeps a document's geometry a fact about the
     * declarations the case wrote rather than about the elements the parser
     * synthesized. */
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

typedef struct layout_sizing_fixture {
    layout_document_t doc;
    layout_sizing_source_t source;
    robo_layout_style_source_t styles;
    robo_layout_tree_t *tree;
} layout_sizing_fixture_t;

static void layout_sizing_begin(layout_sizing_fixture_t *fixture,
                                const char *html,
                                const layout_sizing_rule_t *rules,
                                size_t rule_count)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    for (index = 0u; index < rule_count; index++) {
        layout_sizing_style_add(&fixture->source, &rules[index]);
    }
    robo_layout_style_source_init(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.computed_style_for = layout_sizing_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_sizing_end(layout_sizing_fixture_t *fixture)
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

#define LAYOUT_SIZING_VIEWPORT_WIDTH 800
#define LAYOUT_SIZING_VIEWPORT_HEIGHT 600

/** One pass at @p viewport_width, optionally with a measurer installed. */
static void layout_sizing_pass(layout_sizing_fixture_t *fixture,
                               int32_t viewport_width,
                               const robo_layout_text_metrics_t *metrics,
                               robo_layout_stats_t *out_stats)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;

    viewport.width = viewport_width;
    viewport.height = LAYOUT_SIZING_VIEWPORT_HEIGHT;
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    if (metrics) {
        ROBO_CHECK_EQ_INT(robo_layout_context_set_text_metrics(context, metrics), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture->tree, NULL), ROBO_OK);
    robo_layout_context_stats(context, &stats);
    robo_layout_context_destroy(context);
    if (out_stats) {
        *out_stats = stats;
    }
}

/**
 * The layout box of the first @p tag in the fixture's document.
 *
 * By tag rather than by arena index, and that is not a preference: RoboHTML's tree
 * builder inserts the elements a well-formed document requires -- an implicit
 * `<head>` above all -- so a fixture's *indices* are a fact about the parser and a
 * case that counted them would be asserting the wrong thing the day the parser
 * gained one more. The block suite resolves its card the same way and for the same
 * reason.
 */
static robo_layout_node_id_t layout_sizing_box_for(layout_sizing_fixture_t *fixture,
                                                   const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);
    robo_layout_node_id_t id;

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&element), 1);
    id = robo_layout_object_id_for_dom_node(fixture->tree, &element);
    ROBO_CHECK(id != ROBO_LAYOUT_NODE_ID_NONE);
    return id;
}

/** The size a placed box was given. */
static robo_layout_size_t layout_sizing_size_of(layout_sizing_fixture_t *fixture,
                                                robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_size_t size;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
    return size;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void test_sizing_dimension(void)
{
    robo_layout_dimension_t auto_dimension = robo_layout_dimension_auto();
    robo_layout_dimension_t other_auto;
    robo_layout_dimension_t definite = robo_layout_dimension_definite(10);
    robo_layout_dimension_t other_definite = robo_layout_dimension_definite(11);
    robo_layout_dimension_t unresolved = robo_layout_dimension_unresolved();
    robo_layout_dimension_t negative = robo_layout_dimension_definite(-1);
    robo_layout_dimension_t cleared;
    robo_layout_dimension_t *nothing = NULL;
    robo_gfx_pixel_x_t length = -1;

    layout_fixture();

    /* A cleared record is `auto`, not a zero length: 0 is a size an author can ask
     * for, and a default that meant both would be read as one. */
    robo_layout_dimension_init(&cleared);
    robo_layout_dimension_init(&other_auto);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(&cleared), 1);
    ROBO_CHECK_EQ_INT(cleared.length, 0);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_equal(&cleared, &auto_dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_equal(&auto_dimension, &other_auto), 1);

    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(&definite), 0);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_definite(&definite), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_unresolved(&definite), 0);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_length(&definite, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 10);

    /* The distinction the phase exists to keep: `50%` and `auto` resolve the same
     * way today and must not be the same value. */
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(&unresolved), 0);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_unresolved(&unresolved), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_equal(&unresolved, &auto_dimension), 0);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_equal(&definite, &other_definite), 0);

    /* A length read from a non-definite dimension is a miss rather than a zero --
     * and the miss is what tells a caller it has nothing to use. */
    ROBO_CHECK_EQ_INT(robo_layout_dimension_length(&auto_dimension, &length), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(length, 10); /* untouched */
    ROBO_CHECK_EQ_INT(robo_layout_dimension_length(&unresolved, &length), ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_valid(&auto_dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_valid(&unresolved), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_valid(&definite), 1);
    /* A negative length is refused as a *value*, and it is refused where it is
     * stored rather than clamped where it is used. */
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_valid(&negative), 0);

    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(nothing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_definite(nothing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_valid(nothing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_length(nothing, &length), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_length(&definite, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_equal(&definite, nothing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_equal(nothing, nothing), 1);

    ROBO_CHECK_STR_EQ(robo_layout_dimension_kind_name(ROBO_LAYOUT_DIMENSION_AUTO), "auto");
    ROBO_CHECK_STR_EQ(robo_layout_dimension_kind_name(ROBO_LAYOUT_DIMENSION_DEFINITE),
                      "definite");
    ROBO_CHECK_STR_EQ(robo_layout_dimension_kind_name(ROBO_LAYOUT_DIMENSION_UNRESOLVED),
                      "unresolved");
    ROBO_CHECK_STR_EQ(robo_layout_dimension_kind_name((robo_layout_dimension_kind_t)77),
                      "unknown");
}

static void test_sizing_vocabulary(void)
{
    layout_fixture();

    ROBO_CHECK_EQ_INT(robo_layout_dimension_kind_is_known(ROBO_LAYOUT_DIMENSION_AUTO), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_kind_is_known(ROBO_LAYOUT_DIMENSION_DEFINITE), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_kind_is_known(ROBO_LAYOUT_DIMENSION_UNRESOLVED), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_kind_is_known((robo_layout_dimension_kind_t)9), 0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DIMENSION_KIND_COUNT, 3);

    ROBO_CHECK_EQ_INT(robo_layout_dimension_source_is_known(ROBO_LAYOUT_DIMENSION_SOURCE_NONE),
                      1);
    ROBO_CHECK_EQ_INT(
        robo_layout_dimension_source_is_known(ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED), 1);
    ROBO_CHECK_EQ_INT(
        robo_layout_dimension_source_is_known(ROBO_LAYOUT_DIMENSION_SOURCE_AVAILABLE), 1);
    ROBO_CHECK_EQ_INT(
        robo_layout_dimension_source_is_known(ROBO_LAYOUT_DIMENSION_SOURCE_CONTENT), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_source_is_known((robo_layout_dimension_source_t)9),
                      0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DIMENSION_SOURCE_COUNT, 4);

    ROBO_CHECK_STR_EQ(robo_layout_dimension_source_name(ROBO_LAYOUT_DIMENSION_SOURCE_NONE),
                      "none");
    ROBO_CHECK_STR_EQ(robo_layout_dimension_source_name(ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED),
                      "declared");
    ROBO_CHECK_STR_EQ(robo_layout_dimension_source_name(ROBO_LAYOUT_DIMENSION_SOURCE_AVAILABLE),
                      "available");
    ROBO_CHECK_STR_EQ(robo_layout_dimension_source_name(ROBO_LAYOUT_DIMENSION_SOURCE_CONTENT),
                      "content");
    ROBO_CHECK_STR_EQ(robo_layout_dimension_source_name((robo_layout_dimension_source_t)77),
                      "unknown");
    ROBO_CHECK_EQ_INT(robo_layout_dimension_source_equal(ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED,
                                                        ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED),
                      1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_source_equal(ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED,
                                                        ROBO_LAYOUT_DIMENSION_SOURCE_CONTENT),
                      0);
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

static void test_sizing_record(void)
{
    robo_layout_sizing_t sizing;
    robo_layout_sizing_t other;
    robo_layout_dimension_t dimension;
    robo_layout_size_t value;
    robo_gfx_pixel_x_t width = -1;
    robo_gfx_pixel_y_t height = -1;

    layout_fixture();

    robo_layout_sizing_init(&sizing);
    /* A cleared record describes a box nothing has been said about, and it is a
     * *legal* record rather than an invalid one. */
    ROBO_CHECK_EQ_INT(robo_layout_sizing_is_valid(&sizing), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(&sizing.width), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(&sizing.height), 1);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_available_width(&sizing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_available_height(&sizing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_minimum(&sizing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_maximum(&sizing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_content(&sizing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_height_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolved(&sizing, &value), ROBO_NOT_FOUND);

    /* The two availability flags are separate, and that is the phase's answer to a
     * block box: it is given a width and *no* height, because a block's height is its
     * contents'. A single flag would have to claim the height was zero. */
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 640, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_available_width(&sizing), 1);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_available_height(&sizing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_available_width(&sizing, &width), ROBO_OK);
    ROBO_CHECK_EQ_INT(width, 640);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_available_height(&sizing, &height),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_height(&sizing, 480, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_available_height(&sizing, &height), ROBO_OK);
    ROBO_CHECK_EQ_INT(height, 480);

    /* A known zero is accepted and a negative extent is not: the two are different
     * inputs and a caller that sent -1 meant something it cannot mean. */
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_available_width(&sizing, &width), ROBO_OK);
    ROBO_CHECK_EQ_INT(width, 0);

    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, -1, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_height(&sizing, -1, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(NULL, 1, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* A negative declared length is refused where it is *stored*. */
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_width(&sizing,
                                                  robo_layout_dimension_definite(-4), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_width(&sizing,
                                                  robo_layout_dimension_definite(120), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_height(&sizing,
                                                   robo_layout_dimension_unresolved(), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width(&sizing).kind,
                      ROBO_LAYOUT_DIMENSION_DEFINITE);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width(&sizing).length, 120);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_height(&sizing).kind,
                      ROBO_LAYOUT_DIMENSION_UNRESOLVED);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_width(NULL, robo_layout_dimension_auto(), NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_height(NULL, robo_layout_dimension_auto(), NULL),
                      ROBO_INVALID_ARGUMENT);

    /* The bounds, and the one thing this record has to catch before the clamp sees
     * it: a pair that contradicts itself is *invalid*, not clamped into an answer. */
    value.width = 100;
    value.height = 50;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_minimum(&sizing, value, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_minimum(&sizing), 1);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_is_valid(&sizing), 1);
    value.width = 40;
    value.height = 50;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_maximum(&sizing, value, NULL), ROBO_OK);
    /* 100 minimum against a 40 maximum: no size can satisfy both. */
    ROBO_CHECK_EQ_INT(robo_layout_sizing_is_valid(&sizing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_minimum(&sizing, &value), ROBO_OK);
    ROBO_CHECK_EQ_INT(value.width, 100);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_maximum(&sizing, &value), ROBO_OK);
    ROBO_CHECK_EQ_INT(value.width, 40);
    value.width = -1;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_minimum(&sizing, value, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* The contents, and its own refusal. */
    value.width = 30;
    value.height = 7;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_content(&sizing, value, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_content(&sizing), 1);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_content(&sizing, &value), ROBO_OK);
    ROBO_CHECK_EQ_INT(value.height, 7);
    value.height = -1;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_content(&sizing, value, NULL),
                      ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_clamped(&sizing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_height_clamped(&sizing), 0);

    /* Equality, and the rule every record in this module follows: only the fields
     * that are *set* are compared, because an unset field's bytes are the caller's
     * stack. */
    other = sizing;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_equal(&sizing, &other), 1);
    other.has_content = 0;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_equal(&sizing, &other), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_equal(&sizing, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_equal(NULL, NULL), 1);

    /* Every getter answers for NULL, because a caller does not have to own a record
     * to ask about one -- and a missing record is "nothing is known", which is what
     * the cleared state already means. */
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_available_width(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_minimum(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_content(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_clamped(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_height_clamped(NULL), 0);
    dimension = robo_layout_sizing_width(NULL);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(&dimension), 1);
    dimension = robo_layout_sizing_height(NULL);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(&dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_source(NULL), ROBO_LAYOUT_DIMENSION_SOURCE_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_height_source(NULL),
                      ROBO_LAYOUT_DIMENSION_SOURCE_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_available_width(NULL, &width), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_minimum(NULL, &value), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_maximum(NULL, &value), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_content(NULL, &value), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolved(NULL, &value), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_is_valid(NULL), 0);

    robo_layout_sizing_clear(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(&sizing.width), 1);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_has_minimum(&sizing), 0);
    robo_layout_sizing_clear(NULL);
}

/* ------------------------------------------------------------------ */
/* Resolving                                                           */
/* ------------------------------------------------------------------ */

static void test_sizing_width(void)
{
    robo_layout_sizing_t sizing;
    robo_layout_size_t resolved;
    robo_gfx_pixel_x_t width = 0;

    layout_fixture();

    /* A declared length is taken as it stands, and the *source* says so. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 640, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_width(&sizing,
                                                  robo_layout_dimension_definite(300), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_width(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_available_width(&sizing, &width), ROBO_OK);
    ROBO_CHECK_EQ_INT(width, 640);
    ROBO_CHECK_EQ_INT(sizing.resolved.width, 300);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_clamped(&sizing), 0);

    /* An `auto` width fills the containing block, and the source says *that* -- the
     * same number from a different question, which is the whole reason the source
     * exists. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 640, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_width(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.width, 640);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_AVAILABLE);

    /* An unresolvable width takes the same branch and the same source, and the
     * *dimension* remembers what it was: this is the merge happening at resolution
     * rather than at the seam. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 640, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_width(&sizing,
                                                  robo_layout_dimension_unresolved(), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_width(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.width, 640);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_AVAILABLE);
    ROBO_CHECK_EQ_INT(sizing.width.kind, ROBO_LAYOUT_DIMENSION_UNRESOLVED);

    /* An `auto` width with no containing block is a *failure*, not a zero: a
     * caller that has not said how wide the containing block is has not given this
     * layer an input, and a zero-sized box would be the silent wrong answer. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_width(&sizing, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(sizing.resolved.width, 0);

    /* A known zero *is* an answer, and it is not the failure above. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_width(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.width, 0);

    /* An undefined kind is refused rather than quietly treated as `auto`. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 640, NULL), ROBO_OK);
    sizing.width.kind = (robo_layout_dimension_kind_t)42;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_width(&sizing, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_width(NULL, NULL), ROBO_INVALID_ARGUMENT);

    /* The pair, and the half that is not rolled back on failure. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 640, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve(&sizing, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolved(&sizing, &resolved), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(sizing.resolved.width, 640); /* resolved, and kept */
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_AVAILABLE);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_height_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve(NULL, NULL), ROBO_INVALID_ARGUMENT);
}

static void test_sizing_height(void)
{
    robo_layout_sizing_t sizing;
    robo_layout_size_t content;
    robo_layout_size_t resolved;

    layout_fixture();

    content.width = 100;
    content.height = 44;

    /* A declared height, and an `auto` height from the contents. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_height(&sizing,
                                                   robo_layout_dimension_definite(90), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_content(&sizing, content, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_height(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.height, 90);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_height_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED);

    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_content(&sizing, content, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_height(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.height, 44);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_height_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_CONTENT);

    /* A percentage height is `UNRESOLVED` and takes the contents -- today. There is
     * deliberately no available *height* anywhere in this case, which is the phase's
     * statement that a block box's height is never its containing block's. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_height(&sizing, 600, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_height(&sizing,
                                                   robo_layout_dimension_unresolved(), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_content(&sizing, content, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_height(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.height, 44);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_height_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_CONTENT);
    ROBO_CHECK_EQ_INT(sizing.height.kind, ROBO_LAYOUT_DIMENSION_UNRESOLVED);

    /* An `auto` height with no contents extent is a *different* failure from an
     * `auto` width with no available width, and the two are told apart by which half
     * failed rather than by one code for both. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_height(&sizing, NULL), ROBO_NOT_FOUND);

    /* A contents extent of zero is an answer: an empty box is zero tall. */
    content.width = 0;
    content.height = 0;
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_content(&sizing, content, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_height(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.height, 0);

    /* Both at once, which is the block pass's shape. */
    content.height = 25;
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 500, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_content(&sizing, content, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolved(&sizing, &resolved), ROBO_OK);
    ROBO_CHECK_EQ_INT(resolved.width, 500);
    ROBO_CHECK_EQ_INT(resolved.height, 25);

    sizing.height.kind = (robo_layout_dimension_kind_t)42;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_height(&sizing, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_height(NULL, NULL), ROBO_INVALID_ARGUMENT);
}

static void test_sizing_clamp(void)
{
    robo_layout_sizing_t sizing;
    robo_layout_sizing_t none;
    robo_layout_size_t bound;

    layout_fixture();

    /* A maximum pulls the declared width down, and the flag says it moved. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_width(&sizing,
                                                  robo_layout_dimension_definite(300), NULL),
                      ROBO_OK);
    bound.width = 200;
    bound.height = 1000;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_maximum(&sizing, bound, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_width(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.width, 200);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_clamped(&sizing), 1);
    /* The declared dimension is untouched: the *used* value moved, not the record of
     * what was asked for. */
    ROBO_CHECK_EQ_INT(sizing.width.length, 300);

    /* A minimum pulls an `auto` width up. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 100, NULL), ROBO_OK);
    bound.width = 250;
    bound.height = 0;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_minimum(&sizing, bound, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_width(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.width, 250);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_clamped(&sizing), 1);

    /* Minimum wins a tie -- 10.1's clamp decided that once, and this record reaches it
     * rather than restating it. */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 100, NULL), ROBO_OK);
    bound.width = 300;
    bound.height = 0;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_minimum(&sizing, bound, NULL), ROBO_OK);
    bound.width = 300;
    bound.height = 0;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_maximum(&sizing, bound, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_width(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.width, 300);

    /* A height clamp, which is the same arithmetic on the other field. */
    robo_layout_sizing_init(&sizing);
    bound.width = 0;
    bound.height = 120;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_minimum(&sizing, bound, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_height(&sizing, NULL), ROBO_NOT_FOUND);
    bound.width = 0;
    bound.height = 0;
    {
        robo_layout_size_t content;

        content.width = 0;
        content.height = 40;
        ROBO_CHECK_EQ_INT(robo_layout_sizing_set_content(&sizing, content, NULL), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_height(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.height, 120);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_height_clamped(&sizing), 1);

    /* Contradictory bounds are *reported*, in the same code path that would have
     * clamped, rather than resolved in either's favour. */
    {
        robo_layout_sizing_t contradictory;
        robo_layout_size_t contradicting;

        robo_layout_sizing_init(&contradictory);
        contradicting.width = 80;
        contradicting.height = 0;
        ROBO_CHECK_EQ_INT(robo_layout_sizing_set_minimum(&contradictory, contradicting, NULL),
                          ROBO_OK);
        contradicting.width = 40;
        contradicting.height = 0;
        ROBO_CHECK_EQ_INT(robo_layout_sizing_set_maximum(&contradictory, contradicting, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_sizing_is_valid(&contradictory), 0);
        ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_width(&contradictory, NULL),
                          ROBO_NOT_FOUND);
    }

    /* Nothing bound is not a clamp that moved nothing -- the flag stays clear, so a
     * caller can tell "the bounds left it alone" from "there were no bounds". */
    robo_layout_sizing_init(&none);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_width(&none,
                                                  robo_layout_dimension_definite(77), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve_width(&none, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(none.resolved.width, 77);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_clamped(&none), 0);
}

static void test_sizing_reresolve(void)
{
    robo_layout_sizing_t sizing;
    robo_layout_sizing_t first;
    robo_layout_size_t content;
    size_t round;

    layout_fixture();

    /*
     * The re-layout story, and it is a property of the shape rather than a feature:
     * nothing here is cached, so resolving again over a changed input gets the new
     * answer and no stale half of the old one. This is what §17 of the phase asks
     * for and what a caller changing a viewport, a declaration or a font relies on.
     */
    robo_layout_sizing_init(&sizing);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 800, NULL), ROBO_OK);
    content.width = 800;
    content.height = 30;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_content(&sizing, content, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.width, 800);

    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_available_width(&sizing, 360, NULL), ROBO_OK);
    content.width = 360;
    content.height = 60;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_content(&sizing, content, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.width, 360);
    ROBO_CHECK_EQ_INT(sizing.resolved.height, 60);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_AVAILABLE);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_height_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_CONTENT);

    /* A declared width arriving later replaces the filled one, source and all. */
    ROBO_CHECK_EQ_INT(robo_layout_sizing_set_width(&sizing,
                                                  robo_layout_dimension_definite(120), NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve(&sizing, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(sizing.resolved.width, 120);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_width_source(&sizing),
                      ROBO_LAYOUT_DIMENSION_SOURCE_DECLARED);
    ROBO_CHECK_EQ_INT(sizing.resolved.height, 60);

    /* And resolving twice over unchanged inputs is idempotent, which is the
     * determinism every later goal's invalidation scheme depends on. */
    first = sizing;
    for (round = 0u; round < 5u; round++) {
        robo_layout_sizing_t again = sizing;

        ROBO_CHECK_EQ_INT(robo_layout_sizing_resolve(&again, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_sizing_equal(&again, &first), 1);
    }
}

/* ------------------------------------------------------------------ */
/* The CSS seam                                                        */
/* ------------------------------------------------------------------ */

/**
 * A computed style carrying @p declarations, for the seam cases below.
 *
 * The suite's own builder, reached without a document: the seam is one function and
 * these cases are about what it answers for a style nobody had to lay out.
 */
static robo_css_computed_style_t *layout_sizing_seam_style(const char *name,
                                                          layout_sizing_decl_kind_t kind,
                                                          double number,
                                                          const char *keyword)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;

    layout_fixture();
    if (!name) {
        return NULL;
    }
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    switch (kind) {
    case LAYOUT_SIZING_DECL_PERCENTAGE:
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_percentage(&value, number, 0, NULL),
                          ROBO_OK);
        break;
    case LAYOUT_SIZING_DECL_EM:
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, number, ROBO_CSS_UNIT_EM,
                                                             0, NULL),
                          ROBO_OK);
        break;
    case LAYOUT_SIZING_DECL_KEYWORD:
        ROBO_CHECK_EQ_INT(
            robo_css_computed_value_make_keyword(&value, robo_css_slice_from_text(keyword), NULL),
            ROBO_OK);
        break;
    case LAYOUT_SIZING_DECL_PX:
    default:
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, number, ROBO_CSS_UNIT_PX,
                                                             0, NULL),
                          ROBO_OK);
        break;
    }
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text(name), 0u,
                                                       &specified, &value, NULL),
                      ROBO_OK);
    return style;
}

static void test_sizing_seam(void)
{
    robo_css_computed_style_t *absent = NULL;
    robo_css_computed_style_t *keyword_auto;
    robo_css_computed_style_t *keyword_other;
    robo_css_computed_style_t *px;
    robo_css_computed_style_t *fractional;
    robo_css_computed_style_t *percent;
    robo_css_computed_style_t *em;
    robo_layout_dimension_t dimension;
    robo_gfx_pixel_x_t length = 0;

    /* A style with no entry at all: `auto`, by CSS's initial value. */
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&absent, NULL), ROBO_OK);

    keyword_auto = layout_sizing_seam_style("width", LAYOUT_SIZING_DECL_KEYWORD, 0.0, "auto");
    keyword_other = layout_sizing_seam_style("width", LAYOUT_SIZING_DECL_KEYWORD, 0.0,
                                             "inherit");
    px = layout_sizing_seam_style("width", LAYOUT_SIZING_DECL_PX, 300.0, NULL);
    fractional = layout_sizing_seam_style("width", LAYOUT_SIZING_DECL_PX, 10.5, NULL);
    percent = layout_sizing_seam_style("width", LAYOUT_SIZING_DECL_PERCENTAGE, 50.0, NULL);
    em = layout_sizing_seam_style("width", LAYOUT_SIZING_DECL_EM, 2.0, NULL);

    /* Absent, and the keyword `auto`: one kind, because CSS makes them one value. */
    robo_layout_dimension_init(&dimension);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_read_dimension(absent, "width", &dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(&dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_read_dimension(keyword_auto, "width", &dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(&dimension), 1);

    /* A keyword that names no length is *declared* and unusable, which is
     * `UNRESOLVED` and not `auto`: an author asked for something, and folding that
     * into CSS's default would lose the fact. */
    ROBO_CHECK_EQ_INT(robo_layout_sizing_read_dimension(keyword_other, "width", &dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_unresolved(&dimension), 1);

    /* A px length, and the rounding: `10.5px` becomes 11, because truncating would
     * make every fractional declared width come out a pixel short of what was
     * written. */
    ROBO_CHECK_EQ_INT(robo_layout_sizing_read_dimension(px, "width", &dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_definite(&dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_length(&dimension, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 300);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_read_dimension(fractional, "width", &dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_length(&dimension, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 11);

    /* A percentage and a relative unit: `UNRESOLVED`, which is the kind that will
     * change when a containing dimension is known. */
    ROBO_CHECK_EQ_INT(robo_layout_sizing_read_dimension(percent, "width", &dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_unresolved(&dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_read_dimension(em, "width", &dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_unresolved(&dimension), 1);

    /* A property the style does not declare is `auto` even when the style is
     * rich -- the read is per property, not per style. */
    ROBO_CHECK_EQ_INT(robo_layout_sizing_read_dimension(px, "height", &dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(&dimension), 1);

    /* And the contract that lets a caller read four properties without a branch:
     * `out` is left as it found it when the question cannot be asked. */
    dimension = robo_layout_dimension_definite(9);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_read_dimension(NULL, "width", &dimension), 0);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_length(&dimension, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 9);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_read_dimension(px, NULL, &dimension), 0);
    ROBO_CHECK_EQ_INT(robo_layout_sizing_read_dimension(px, "width", NULL), 0);

    robo_css_computed_style_destroy(absent);
    robo_css_computed_style_destroy(keyword_auto);
    robo_css_computed_style_destroy(keyword_other);
    robo_css_computed_style_destroy(px);
    robo_css_computed_style_destroy(fractional);
    robo_css_computed_style_destroy(percent);
    robo_css_computed_style_destroy(em);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

/**
 * A declared width reaches the geometry -- *and* the width the children are laid
 * out into.
 *
 * The second half is the phase's real content. A build that resolved its own width
 * and then handed its children the containing block's would put a 300px box's block
 * child at 800px wide, which is a plausible-looking number and the wrong document.
 */
static void test_sizing_block_declared_width(void)
{
    layout_sizing_fixture_t fixture;
    layout_sizing_rule_t rules[4];
    robo_layout_stats_t stats;
    robo_layout_size_t size;

    layout_sizing_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_px(&rules[2], "width", 300.0);
    layout_sizing_rule_init(&rules[3], "section", ROBO_CSS_DISPLAY_BLOCK);

    layout_sizing_begin(&fixture,
                        "<html><body><div><section></section></div></body></html>", rules, 4u);
    layout_sizing_pass(&fixture, LAYOUT_SIZING_VIEWPORT_WIDTH, NULL, &stats);

    /* html, head, body, div, section: five boxes, of which `<head>` is inline-level
     * because nothing declared a display for it -- which is CSS's initial value and
     * this fixture's way of saying "this document declares what it declares". */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 5u);
    ROBO_CHECK_EQ_INT(stats.block.blocks_placed, 4u);
    /* One box obeyed its author and three filled what they were given. */
    ROBO_CHECK_EQ_INT(stats.block.widths_declared, 1u);
    ROBO_CHECK_EQ_INT(stats.block.widths_filled, 3u);
    ROBO_CHECK_EQ_INT(stats.block.widths_declared + stats.block.widths_filled, 4u);

    size = layout_sizing_size_of(&fixture, layout_sizing_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 300);
    /* The child of the 300px box is 300 wide, not the viewport's 800 -- which is
     * what makes the declared width *contain* something rather than only describe
     * the box itself. */
    size = layout_sizing_size_of(&fixture, layout_sizing_box_for(&fixture, "section"));
    ROBO_CHECK_EQ_INT(size.width, 300);
    /* And an ancestor above the declared box still fills the viewport. */
    size = layout_sizing_size_of(&fixture, layout_sizing_box_for(&fixture, "body"));
    ROBO_CHECK_EQ_INT(size.width, LAYOUT_SIZING_VIEWPORT_WIDTH);

    layout_sizing_end(&fixture);
}

/**
 * An `auto` width fills the containing block, at every level -- 10.3's rule, now
 * *decided* by the sizing layer rather than assumed by it.
 */
static void test_sizing_block_auto_width(void)
{
    layout_sizing_fixture_t fixture;
    layout_sizing_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_size_t size;

    layout_sizing_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);

    layout_sizing_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_sizing_pass(&fixture, 640, NULL, &stats);

    ROBO_CHECK_EQ_INT(stats.block.widths_declared, 0u);
    ROBO_CHECK_EQ_INT(stats.block.widths_filled, 3u);

    size = layout_sizing_size_of(&fixture, layout_sizing_box_for(&fixture, "html"));
    ROBO_CHECK_EQ_INT(size.width, 640);
    size = layout_sizing_size_of(&fixture, layout_sizing_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 640);

    /* A viewport is an *input*, so a second pass at another width is another
     * document: 800 here, and the nested box is 800 too. */
    layout_sizing_pass(&fixture, 800, NULL, &stats);
    size = layout_sizing_size_of(&fixture, layout_sizing_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 800);

    layout_sizing_end(&fixture);
}

/**
 * A percentage width is *read* as `UNRESOLVED` and *resolved* by filling the
 * containing block, and the two halves of that are both asserted.
 *
 * The dimension is the half a later goal will change, so the case reaches for it
 * through the seam rather than only through the geometry -- a build that folded
 * `50%` into `auto` at the seam would produce the same 300px box here and be wrong
 * about the document.
 */
static void test_sizing_block_unresolved_width(void)
{
    layout_sizing_fixture_t fixture;
    layout_sizing_rule_t rules[3];
    robo_css_computed_style_t *style;
    robo_layout_dimension_t dimension;
    robo_layout_stats_t stats;
    robo_layout_size_t size;

    layout_sizing_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_percentage(&rules[2], "width", 50.0);

    layout_sizing_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_sizing_pass(&fixture, 300, NULL, &stats);

    /* The seam's answer, on the very style the pass used. */
    style = fixture.source.rules[2].style;
    ROBO_CHECK_EQ_INT(robo_layout_sizing_read_dimension(style, "width", &dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_unresolved(&dimension), 1);
    ROBO_CHECK_EQ_INT(robo_layout_dimension_is_auto(&dimension), 0);

    /* And the pass's: the containing block's width, counted as a *filled* width
     * rather than a declared one, because that is what it is. */
    size = layout_sizing_size_of(&fixture, layout_sizing_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 300);
    ROBO_CHECK_EQ_INT(stats.block.widths_declared, 0u);
    ROBO_CHECK_EQ_INT(stats.block.widths_filled, 3u);

    layout_sizing_end(&fixture);
}

/**
 * `auto` and a relative unit, declared rather than absent, reach the same two
 * branches -- and the *counters* say which one each took.
 *
 * A `width: auto` written on purpose and a width nothing declared at all are one
 * value in CSS, and this case is what proves the pass reads them that way rather
 * than by accident: the keyword arrives as a `KEYWORD` value and the unit as a
 * `LENGTH` in `em`, and both must fill the containing block while a `px` length
 * does not.
 */
static void test_sizing_block_keyword_and_unit(void)
{
    layout_sizing_fixture_t fixture;
    layout_sizing_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_size_t size;

    layout_sizing_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_keyword(&rules[2], "width", "auto");
    layout_sizing_rule_em(&rules[2], "height", 2.0);

    layout_sizing_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_sizing_pass(&fixture, 512, NULL, &stats);

    /* The declared `auto` fills the containing block, exactly as an absent width
     * does -- and both are counted as *filled*, because that is the answer the
     * resolver reached. */
    size = layout_sizing_size_of(&fixture, layout_sizing_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 512);
    /* The `2em` is a length this build cannot turn into pixels, so the height comes
     * from the contents -- which are none, so zero. */
    ROBO_CHECK_EQ_INT(size.height, 0);
    ROBO_CHECK_EQ_INT(stats.block.widths_declared, 0u);
    ROBO_CHECK_EQ_INT(stats.block.widths_filled, 3u);
    ROBO_CHECK_EQ_INT(stats.block.heights_explicit, 0u);
    ROBO_CHECK_EQ_INT(stats.block.heights_from_content, 3u);

    layout_sizing_end(&fixture);
}

/**
 * The bounds path, live -- driven by a fabricated style, because no property
 * RoboCSS registers feeds it.
 *
 * This is the case that proves the "clean place" 10.8 was asked to leave for
 * `min-width` is a working path and not a field nothing can reach.
 */
static void test_sizing_block_bounds(void)
{
    layout_sizing_fixture_t fixture;
    layout_sizing_rule_t rules[3];
    robo_layout_size_t size;

    layout_sizing_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_px(&rules[2], "width", 300.0);
    layout_sizing_rule_px(&rules[2], "min-width", 40.0);
    layout_sizing_rule_px(&rules[2], "max-width", 200.0);
    layout_sizing_rule_px(&rules[2], "min-height", 44.0);
    layout_sizing_rule_px(&rules[2], "max-height", 120.0);

    layout_sizing_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_sizing_pass(&fixture, LAYOUT_SIZING_VIEWPORT_WIDTH, NULL, NULL);

    /* The declared 300 is pulled down to the 200 maximum, and the 0-height box is
     * raised to the 44 minimum. */
    size = layout_sizing_size_of(&fixture, layout_sizing_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 200);
    ROBO_CHECK_EQ_INT(size.height, 44);

    /* And the clamp propagates the way a size does: the box's own 200 is what its
     * contents would be laid out into, and its parent is still the viewport's. */
    size = layout_sizing_size_of(&fixture, layout_sizing_box_for(&fixture, "body"));
    ROBO_CHECK_EQ_INT(size.width, LAYOUT_SIZING_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(size.height, 44);

    layout_sizing_end(&fixture);
}

/**
 * A bound whose *other* dimension is not declared is not recorded, and the case
 * says so by outcome: a `max-width` on its own must not turn every height into the
 * zero this build has no `max-height` for.
 */
static void test_sizing_block_half_bound(void)
{
    layout_sizing_fixture_t fixture;
    layout_sizing_rule_t rules[3];
    robo_layout_size_t size;

    layout_sizing_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_px(&rules[2], "width", 300.0);
    /* Only one dimension of the maximum is known. */
    layout_sizing_rule_px(&rules[2], "max-width", 120.0);
    layout_sizing_rule_px(&rules[2], "height", 60.0);

    layout_sizing_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_sizing_pass(&fixture, LAYOUT_SIZING_VIEWPORT_WIDTH, NULL, NULL);

    /* The width is *not* clamped, and the height is not zeroed: a bound is recorded
     * only when both of its dimensions name a length, because 10.1's constraints
     * record holds a bound as one size. */
    size = layout_sizing_size_of(&fixture, layout_sizing_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 300);
    ROBO_CHECK_EQ_INT(size.height, 60);

    layout_sizing_end(&fixture);
}

/**
 * An atomic inline's width is its declaration and its auto height is its contents'
 * extent -- the two-phase resolve, asserted where it is a *document* rather than a
 * record.
 */
static void test_sizing_atomic_inline(void)
{
    layout_sizing_fixture_t fixture;
    layout_sizing_rule_t rules[4];
    robo_layout_stats_t stats;
    robo_layout_size_t size;

    layout_sizing_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[2], "p", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[3], "i", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_sizing_rule_px(&rules[3], "width", 150.0);

    layout_sizing_begin(&fixture,
                        "<html><body><p><i><b></b></i></p></body></html>", rules, 4u);
    layout_sizing_pass(&fixture, LAYOUT_SIZING_VIEWPORT_WIDTH, NULL, &stats);

    /* The atomic inline's declared width, and its height from the contents -- which
     * are one `<b>` the pass stacks, and which has no declared height. */
    {
        robo_layout_node_id_t atomic = layout_sizing_box_for(&fixture, "i");

        size = layout_sizing_size_of(&fixture, atomic);
        ROBO_CHECK_EQ_INT(size.width, 150);
        ROBO_CHECK_EQ_INT(size.height, 0);
    }
    ROBO_CHECK_EQ_INT(stats.inline_flow.atomic_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.widths_declared, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.widths_filled, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.heights_declared, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.heights_from_content, 1u);

    layout_sizing_end(&fixture);
}

/**
 * Sizing must not turn line boxes into CSS boxes.
 *
 * A wrapped inline document's lines are still derived from the participants they
 * placed: each line is as wide as what was put on it, the context's width is what
 * the *participants* could take, and nothing in the sizing layer is consulted for a
 * line at all. Asserted over a document that really wraps, so the difference between
 * "the line's own width" and "the context's width" is visible.
 */
static void test_sizing_line_regression(void)
{
    layout_sizing_fixture_t fixture;
    layout_sizing_rule_t rules[6];
    robo_layout_stats_t stats;
    size_t lines;
    size_t index;

    layout_sizing_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[2], "p", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_init(&rules[3], "span", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_sizing_rule_px(&rules[3], "width", 60.0);
    layout_sizing_rule_px(&rules[3], "height", 20.0);
    layout_sizing_rule_init(&rules[4], "em", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_sizing_rule_px(&rules[4], "width", 90.0);
    layout_sizing_rule_px(&rules[4], "height", 20.0);
    layout_sizing_rule_init(&rules[5], "b", ROBO_CSS_DISPLAY_BLOCK);
    layout_sizing_rule_px(&rules[5], "height", 10.0);

    /* 60 + 90 + 90 = 240 into a 200-wide line: the third one wraps. The point is
     * that the *line* is 150 wide -- what it placed -- where the context is 200. */
    layout_sizing_begin(&fixture,
                        "<html><body><p><span></span><em></em><em></em></p></body></html>",
                        rules, 6u);
    layout_sizing_pass(&fixture, 200, NULL, &stats);

    lines = robo_layout_tree_line_count(fixture.tree);
    ROBO_CHECK_EQ_INT(lines, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.line_members, 3u);

    for (index = 0u; index < lines; index++) {
        robo_layout_line_id_t id = ROBO_LAYOUT_LINE_ID_NONE;
        robo_layout_line_box_t line;

        ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture.tree, index, &id, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, id, &line, NULL), ROBO_OK);
        /* Every line fits inside the context and none of them *is* the context:
         * a build that had made each line as wide as the available width would make
         * the first line 200 rather than 150. */
        ROBO_CHECK_EQ_INT(line.available_width, 200);
        if (index == 0u) {
            ROBO_CHECK_EQ_INT(line.size.width, 150);
        } else {
            ROBO_CHECK_EQ_INT(line.size.width, 90);
        }
        ROBO_CHECK_EQ_INT(line.size.height, 20);
    }

    /* The declared heights are the members' and the lines' -- a line is exactly as
     * tall as its tallest member, which is 10.7's rule and unchanged here. */
    ROBO_CHECK_EQ_INT(stats.inline_flow.heights_declared, 3u);

    layout_sizing_end(&fixture);
}

/**
 * Nothing is cached between passes and nothing leaks, asserted over a whole
 * document rather than over a record.
 *
 * The balance is taken *before the tree exists* so it brackets the arenas' whole
 * lifetime, which is the shape every other `allocator_balance` case in this
 * directory settled on -- a counter that only brackets the reuse would pass while a
 * tree's own arena leaked.
 */
static void test_sizing_allocator_balance(void)
{
    size_t before;
    int pass;
    int width;

    layout_fixture();
    before = layout_live_blocks();

    for (pass = 0; pass < 6; pass++) {
        layout_sizing_fixture_t fixture;
        layout_sizing_rule_t rules[3];
        robo_layout_stats_t stats;
        size_t settled;

        layout_sizing_rule_init(&rules[0], "html", ROBO_CSS_DISPLAY_BLOCK);
        layout_sizing_rule_init(&rules[1], "body", ROBO_CSS_DISPLAY_BLOCK);
        layout_sizing_rule_init(&rules[2], "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_sizing_rule_px(&rules[2], "width", 250.0);

        layout_sizing_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

        /* The viewport is an input, so a pass at each of two widths is two answers
         * from one tree -- and the second must not leave the first's numbers or its
         * arena's. The declared 250 is the *same* number in both, which is the point:
         * an author's width does not move when the window does. */
        for (width = 0; width < 2; width++) {
            robo_layout_size_t size;
            int32_t viewport = width == 0 ? LAYOUT_SIZING_VIEWPORT_WIDTH : 300;

            layout_sizing_pass(&fixture, viewport, NULL, &stats);
            size = layout_sizing_size_of(&fixture, layout_sizing_box_for(&fixture, "div"));
            ROBO_CHECK_EQ_INT(size.width, 250);
            ROBO_CHECK_EQ_INT(stats.block.widths_declared, 1u);
            settled = layout_live_blocks();
            ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
        }

        layout_sizing_end(&fixture);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_layout_sizing(void);

void robo_test_register_layout_sizing(void)
{
    robo_test_register("layout.sizing.dimension", test_sizing_dimension);
    robo_test_register("layout.sizing.vocabulary", test_sizing_vocabulary);
    robo_test_register("layout.sizing.record", test_sizing_record);
    robo_test_register("layout.sizing.width", test_sizing_width);
    robo_test_register("layout.sizing.height", test_sizing_height);
    robo_test_register("layout.sizing.clamp", test_sizing_clamp);
    robo_test_register("layout.sizing.reresolve", test_sizing_reresolve);
    robo_test_register("layout.sizing.seam", test_sizing_seam);
    robo_test_register("layout.sizing.block_declared_width", test_sizing_block_declared_width);
    robo_test_register("layout.sizing.block_auto_width", test_sizing_block_auto_width);
    robo_test_register("layout.sizing.block_unresolved_width",
                       test_sizing_block_unresolved_width);
    robo_test_register("layout.sizing.block_keyword_and_unit",
                       test_sizing_block_keyword_and_unit);
    robo_test_register("layout.sizing.block_bounds", test_sizing_block_bounds);
    robo_test_register("layout.sizing.block_half_bound", test_sizing_block_half_bound);
    robo_test_register("layout.sizing.atomic_inline", test_sizing_atomic_inline);
    robo_test_register("layout.sizing.line_regression", test_sizing_line_regression);
    robo_test_register("layout.sizing.allocator_balance", test_sizing_allocator_balance);
}
