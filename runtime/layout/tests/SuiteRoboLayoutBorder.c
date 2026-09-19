/*
 * RoboLayout border suite (Phase 10.11) — layout.borders.*
 *
 * Four things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary** -- three width kinds, four sides, ten styles and 9.9's colour
 *   record -- which is pure, and whose one difference from 10.10's padding is that a
 *   *non-negative* length is only half the story: a style that does not draw makes
 *   even a resolved five a used zero;
 *
 *   the **seam**: what a computed style declared, which is the one place this module
 *   reads twelve longhands and the one place `thin`, `medium` and `thick` become
 *   numbers;
 *
 *   the two **derived numbers** -- the padding box a containing block leaves once the
 *   borders are out of it, and the border box a padding box asks for -- driven
 *   directly, so that a wrong subtraction fails at the arithmetic rather than three
 *   boxes later in a coordinate;
 *
 *   the **pass**: a border reaching geometry, which since this phase means a box's
 *   `size` is its border box, an `auto` width fills what the margins, the border *and*
 *   the padding left, an atomic inline's advance carries it, and the whole record --
 *   used width, style and colour per side -- is on the object for paint.
 *
 * The rule the phase turns on is asserted every way it can be seen: **a border's used
 * width is zero unless its style generates one**. A `border: 5px none` box is the same
 * box as one with no border declaration at all, which is why
 * `layout.borders.none_style` and `layout.borders.edges_record` are two cases rather
 * than one, and why the third counter (`borders_drawn`) exists beside the two that
 * count declarations.
 *
 * The other claim worth pinning, and the one a build could pass every size assertion
 * without honouring: a border grows the box **inward**. `layout.borders.block_auto_width`
 * is the case that fails a build which grew the box past its containing block, and
 * `layout.borders.padding_interaction` is the one that fails a build which placed
 * contents at `padding-left` and forgot the border.
 *
 * The scripts here are *computed styles*, built through 9.8's makers exactly as the
 * sizing, margin and padding suites build them, because a fabricated struct would prove
 * the pass reads a struct rather than a computed CSS value. Where a case needs a
 * declaration the fixture's property table cannot express through a CSS literal -- an
 * unresolved length is a flag on the value, and the styles and colours are their own
 * makers -- the value is built the way the computed layer builds it and the seam is what
 * is under test.
 *
 * Elements are resolved **by tag** (`layout_border_box_for`) rather than by arena index,
 * which is the lesson 10.8's suite wrote down: a fixture's indices are a fact about the
 * parser, and the tags here are distinct anyway so that one scripted style per element is
 * enough.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A fabricated style that can declare anything                        */
/* ------------------------------------------------------------------ */

/** One declaration to put on a fabricated style. */
typedef enum layout_border_decl_kind {
    LAYOUT_BORDER_DECL_PX = 0,
    /** A px length that is **negative**: invalid CSS for a border width, and the one
     *  declaration the seam is asked to refuse rather than use. */
    LAYOUT_BORDER_DECL_NEGATIVE_PX = 1,
    /** A px length flagged unresolved, which is what a `var()` arrives as. */
    LAYOUT_BORDER_DECL_UNRESOLVED_PX = 2,
    LAYOUT_BORDER_DECL_PERCENTAGE = 3,
    LAYOUT_BORDER_DECL_EM = 4,
    LAYOUT_BORDER_DECL_KEYWORD = 5,
    /** A `robo_css_computed_value_t` of kind BORDER_STYLE -- 9.15's typed vocabulary. */
    LAYOUT_BORDER_DECL_BORDER_STYLE = 6,
    /** A `robo_css_computed_value_t` of kind COLOR -- 9.9's record. */
    LAYOUT_BORDER_DECL_COLOR = 7
} layout_border_decl_kind_t;

typedef struct layout_border_decl {
    const char *name;
    layout_border_decl_kind_t kind;
    double number;
    const char *keyword;
    robo_css_border_style_t border_style;
    robo_css_color_t color;
} layout_border_decl_t;

#define LAYOUT_BORDER_MAX_DECLARATIONS 24u
#define LAYOUT_BORDER_MAX_RULES 8u

typedef struct layout_border_rule {
    const char *tag;
    robo_css_display_t display;
    layout_border_decl_t declarations[LAYOUT_BORDER_MAX_DECLARATIONS];
    size_t declarations_count;
} layout_border_rule_t;

static void layout_border_rule_init(layout_border_rule_t *rule,
                                    const char *tag,
                                    robo_css_display_t display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static layout_border_decl_t *layout_border_rule_add(layout_border_rule_t *rule,
                                                    const char *name,
                                                    layout_border_decl_kind_t kind)
{
    layout_border_decl_t *declaration;

    ROBO_CHECK_EQ_INT(rule->declarations_count < LAYOUT_BORDER_MAX_DECLARATIONS, 1);
    declaration = &rule->declarations[rule->declarations_count];
    memset(declaration, 0, sizeof(*declaration));
    declaration->name = name;
    declaration->kind = kind;
    declaration->border_style = ROBO_CSS_BORDER_STYLE_NONE;
    declaration->color = robo_css_color_none();
    rule->declarations_count++;
    return declaration;
}

static void layout_border_rule_px(layout_border_rule_t *rule, const char *name, double px)
{
    layout_border_rule_add(rule, name, LAYOUT_BORDER_DECL_PX)->number = px;
}

/** A *negative* px length, which CSS forbids for a border width and the seam refuses. */
static void layout_border_rule_negative(layout_border_rule_t *rule,
                                        const char *name,
                                        double px)
{
    layout_border_rule_add(rule, name, LAYOUT_BORDER_DECL_NEGATIVE_PX)->number = px;
}

static void layout_border_rule_unresolved(layout_border_rule_t *rule,
                                          const char *name,
                                          double px)
{
    layout_border_rule_add(rule, name, LAYOUT_BORDER_DECL_UNRESOLVED_PX)->number = px;
}

static void layout_border_rule_percentage(layout_border_rule_t *rule,
                                          const char *name,
                                          double percent)
{
    layout_border_rule_add(rule, name, LAYOUT_BORDER_DECL_PERCENTAGE)->number = percent;
}

static void layout_border_rule_em(layout_border_rule_t *rule, const char *name, double em)
{
    layout_border_rule_add(rule, name, LAYOUT_BORDER_DECL_EM)->number = em;
}

/** A keyword, which for a width is `thin`, `medium`, `thick` -- or one the grammar does
 *  not allow, which the seam must report as a declaration it cannot use. */
static void layout_border_rule_keyword(layout_border_rule_t *rule,
                                       const char *name,
                                       const char *keyword)
{
    layout_border_rule_add(rule, name, LAYOUT_BORDER_DECL_KEYWORD)->keyword = keyword;
}

static void layout_border_rule_border_style(layout_border_rule_t *rule,
                                            const char *name,
                                            robo_css_border_style_t style)
{
    layout_border_rule_add(rule, name, LAYOUT_BORDER_DECL_BORDER_STYLE)->border_style = style;
}

static void layout_border_rule_color(layout_border_rule_t *rule,
                                     const char *name,
                                     robo_css_color_t color)
{
    layout_border_rule_add(rule, name, LAYOUT_BORDER_DECL_COLOR)->color = color;
}

/**
 * One `border-width` shorthand written the way a stylesheet would.
 *
 * `border-width: 5px` is four declarations one layer down, and that is the fact a case
 * asserts by writing the shorthand and reading four sides of geometry: the expansion is
 * 9.15's, and this fixture writing it does not make it this suite's.
 */
static void layout_border_rule_width_all(layout_border_rule_t *rule, double px)
{
    layout_border_rule_px(rule, "border-top-width", px);
    layout_border_rule_px(rule, "border-right-width", px);
    layout_border_rule_px(rule, "border-bottom-width", px);
    layout_border_rule_px(rule, "border-left-width", px);
}

static void layout_border_rule_style_all(layout_border_rule_t *rule,
                                         robo_css_border_style_t style)
{
    layout_border_rule_border_style(rule, "border-top-style", style);
    layout_border_rule_border_style(rule, "border-right-style", style);
    layout_border_rule_border_style(rule, "border-bottom-style", style);
    layout_border_rule_border_style(rule, "border-left-style", style);
}

static void layout_border_rule_color_all(layout_border_rule_t *rule, robo_css_color_t color)
{
    layout_border_rule_color(rule, "border-top-color", color);
    layout_border_rule_color(rule, "border-right-color", color);
    layout_border_rule_color(rule, "border-bottom-color", color);
    layout_border_rule_color(rule, "border-left-color", color);
}

/** The `border: Npx solid` a case means when it writes one. */
static void layout_border_rule_shorthand(layout_border_rule_t *rule, double px)
{
    layout_border_rule_width_all(rule, px);
    layout_border_rule_style_all(rule, ROBO_CSS_BORDER_STYLE_SOLID);
}

typedef struct layout_border_style_slot {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
    size_t answers;
} layout_border_style_slot_t;

typedef struct layout_border_source {
    layout_border_style_slot_t rules[LAYOUT_BORDER_MAX_RULES];
    size_t count;
} layout_border_source_t;

/**
 * Build the style @p rule describes: a real DISPLAY value plus whatever the rule
 * declares, all through 9.8's own makers and container.
 *
 * `display` is set even when the rule declares nothing else, because 9.12's initial
 * value is `inline` and a fixture that wanted a block box would otherwise be asserting
 * a document made of inline boxes.
 */
static robo_css_computed_style_t *layout_border_style_build(const layout_border_rule_t *rule)
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
        const layout_border_decl_t *declaration = &rule->declarations[index];

        value = (robo_css_computed_value_t)ROBO_CSS_CLEARED;
        specified = (robo_css_value_t)ROBO_CSS_CLEARED;
        switch (declaration->kind) {
        case LAYOUT_BORDER_DECL_PERCENTAGE:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_percentage(&value, declaration->number,
                                                                     0, NULL),
                              ROBO_OK);
            break;
        case LAYOUT_BORDER_DECL_KEYWORD:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                                  &value, robo_css_slice_from_text(declaration->keyword), NULL),
                              ROBO_OK);
            break;
        case LAYOUT_BORDER_DECL_BORDER_STYLE:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_border_style(
                                  &value, declaration->border_style, NULL),
                              ROBO_OK);
            break;
        case LAYOUT_BORDER_DECL_COLOR:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_color(&value, declaration->color, NULL),
                              ROBO_OK);
            break;
        case LAYOUT_BORDER_DECL_UNRESOLVED_PX:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, declaration->number,
                                                                 ROBO_CSS_UNIT_PX, 0, NULL),
                              ROBO_OK);
            /* The flag the computed layer sets for a `var()` or a `calc()`: carried
             * intact and not a value *yet*, whatever its kind says. */
            value.unresolved = 1;
            break;
        case LAYOUT_BORDER_DECL_EM:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, declaration->number,
                                                                 ROBO_CSS_UNIT_EM, 0, NULL),
                              ROBO_OK);
            break;
        case LAYOUT_BORDER_DECL_NEGATIVE_PX:
        case LAYOUT_BORDER_DECL_PX:
        default:
            /* Written the same way for both, and that is the point: the *style* holds
             * what it was handed -- a negative length is a computed value like any other
             * -- and it is the classification that refuses one, not the maker. A fixture
             * that could not build the declaration could not test the refusal. */
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
static robo_css_computed_style_t *layout_border_style_add(layout_border_source_t *source,
                                                          const layout_border_rule_t *rule)
{
    size_t slot;
    robo_css_computed_style_t *style;

    layout_fixture();
    ROBO_CHECK_EQ_INT(source->count < LAYOUT_BORDER_MAX_RULES, 1);
    slot = source->count;
    style = layout_border_style_build(rule);
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
static void layout_border_style_replace(layout_border_source_t *source,
                                       const char *tag,
                                       const layout_border_rule_t *rule)
{
    size_t index;

    layout_fixture();
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            robo_css_computed_style_destroy(source->rules[index].style);
            source->rules[index].style = layout_border_style_build(rule);
            return;
        }
    }
    ROBO_CHECK_MSG(0, "the tag to replace is not in this source");
}

static const robo_css_computed_style_t *layout_border_style_lookup(
    void *userdata,
    const robo_html_node_handle_t *element)
{
    layout_border_source_t *source = (layout_border_source_t *)userdata;
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
    /* An element nothing was declared for: `display: inline`, no declared border, and
     * that is CSS's initial value rather than a fixture default. */
    return NULL;
}

typedef struct layout_border_fixture {
    layout_document_t doc;
    layout_border_source_t source;
    robo_layout_style_source_t styles;
    robo_layout_tree_t *tree;
} layout_border_fixture_t;

static void layout_border_begin(layout_border_fixture_t *fixture,
                                const char *html,
                                const layout_border_rule_t *rules,
                                size_t rule_count)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    for (index = 0u; index < rule_count; index++) {
        (void)layout_border_style_add(&fixture->source, &rules[index]);
    }
    robo_layout_style_source_init(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.computed_style_for = layout_border_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_border_end(layout_border_fixture_t *fixture)
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

#define LAYOUT_BORDER_VIEWPORT_WIDTH 800
#define LAYOUT_BORDER_VIEWPORT_HEIGHT 600

/** One pass at @p viewport_width. */
static void layout_border_pass(layout_border_fixture_t *fixture,
                               int32_t viewport_width,
                               robo_layout_stats_t *out_stats)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;

    viewport.width = viewport_width;
    viewport.height = LAYOUT_BORDER_VIEWPORT_HEIGHT;
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
static robo_layout_node_id_t layout_border_box_for(layout_border_fixture_t *fixture,
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
static robo_layout_point_t layout_border_origin_of(layout_border_fixture_t *fixture,
                                                   robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_point_t position;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
    return position;
}

/** The size a placed box was given -- its **border** box since 10.11. */
static robo_layout_size_t layout_border_size_of(layout_border_fixture_t *fixture,
                                                robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_size_t size;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
    return size;
}

/** The used border a placed box reports. */
static robo_layout_border_edges_t layout_border_edges_of(layout_border_fixture_t *fixture,
                                                         robo_layout_node_id_t id)
{
    robo_layout_border_edges_t edges;

    ROBO_CHECK_EQ_INT(robo_layout_object_borders(fixture->tree, id, &edges, NULL), ROBO_OK);
    return edges;
}

/**
 * Rebuild the layout tree against the styles @p fixture was built with.
 *
 * The step a changed declaration needs and the reason it is not optional: the tree
 * **borrows** one computed style per element for as long as the bridge lives, so a
 * replacement that left the old tree standing would leave it holding a destroyed style.
 * The rule is 10.9's and 10.10's, and it is what makes "the second answer is a fact about
 * the second style" checkable rather than a hope.
 */
static void layout_border_rebuild(layout_border_fixture_t *fixture)
{
    robo_layout_build_options_t options;

    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

/** The four used widths of @p edges, in CSS's side order. */
static void layout_border_widths_of(const robo_layout_border_edges_t *edges,
                                    robo_gfx_pixel_x_t out[ROBO_CSS_BOX_SIDE_COUNT])
{
    size_t index;

    for (index = 0u; index < ROBO_CSS_BOX_SIDE_COUNT; index++) {
        ROBO_CHECK_EQ_INT(robo_layout_border_edges_width(edges, (robo_css_box_side_t)index,
                                                        &out[index]),
                          ROBO_OK);
    }
}

/* ------------------------------------------------------------------ */
/* A hand-built record, for the arithmetic                             */
/* ------------------------------------------------------------------ */

/** A declared side: a width kind, a style and a colour. */
static robo_layout_border_side_t layout_border_side_make(robo_layout_border_width_t width,
                                                        robo_css_border_style_t style,
                                                        robo_css_color_t color)
{
    robo_layout_border_side_t side;

    side.width = width;
    side.style = style;
    side.color = color;
    return side;
}

/** The four declared sides of a record, each built from its own three facts. */
static robo_layout_borders_t layout_border_declared(int32_t top_px,
                                                   robo_css_border_style_t top_style,
                                                   int32_t right_px,
                                                   robo_css_border_style_t right_style,
                                                   int32_t bottom_px,
                                                   robo_css_border_style_t bottom_style,
                                                   int32_t left_px,
                                                   robo_css_border_style_t left_style)
{
    robo_layout_borders_t borders;

    borders.top = layout_border_side_make(robo_layout_border_width_resolved(top_px), top_style,
                                          robo_css_color_none());
    borders.right = layout_border_side_make(robo_layout_border_width_resolved(right_px),
                                            right_style, robo_css_color_none());
    borders.bottom = layout_border_side_make(robo_layout_border_width_resolved(bottom_px),
                                             bottom_style, robo_css_color_none());
    borders.left = layout_border_side_make(robo_layout_border_width_resolved(left_px), left_style,
                                           robo_css_color_none());
    return borders;
}

/** A resolved used-side record: the widths given, every style generating a border. */
static robo_layout_border_edges_t layout_border_used(int32_t top,
                                                    int32_t right,
                                                    int32_t bottom,
                                                    int32_t left)
{
    robo_layout_border_edges_t edges;
    robo_layout_borders_t declared = layout_border_declared(
        top, ROBO_CSS_BORDER_STYLE_SOLID, right, ROBO_CSS_BORDER_STYLE_SOLID, bottom,
        ROBO_CSS_BORDER_STYLE_SOLID, left, ROBO_CSS_BORDER_STYLE_SOLID);

    ROBO_CHECK_EQ_INT(robo_layout_border_resolve(&declared, &edges, NULL), ROBO_OK);
    return edges;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void test_borders_kind_vocabulary(void)
{
    size_t index;

    layout_fixture();

    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_BORDER_WIDTH_KIND_COUNT, 3);
    /*
     * The zero of the vocabulary is `ABSENT`, and that is a decision rather than a
     * numbering accident: a cleared record then holds the side's *initial state*, so a
     * forgotten initialisation is not a claim that somebody wrote a border.
     */
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_BORDER_WIDTH_ABSENT, 0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_BORDER_WIDTH_RESOLVED, 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_BORDER_WIDTH_UNRESOLVED, 2);

    for (index = 0u; index < ROBO_LAYOUT_BORDER_WIDTH_KIND_COUNT; index++) {
        robo_layout_border_width_kind_t kind = (robo_layout_border_width_kind_t)index;

        ROBO_CHECK_EQ_INT(robo_layout_border_width_kind_is_known(kind), 1);
        ROBO_CHECK(robo_layout_border_width_kind_name(kind) != NULL);
    }
    ROBO_CHECK_EQ_INT(robo_layout_border_width_kind_is_known(
                          (robo_layout_border_width_kind_t)3),
                      0);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_kind_is_known(
                          (robo_layout_border_width_kind_t)77),
                      0);
    ROBO_CHECK_STR_EQ(robo_layout_border_width_kind_name(ROBO_LAYOUT_BORDER_WIDTH_ABSENT),
                      "absent");
    ROBO_CHECK_STR_EQ(robo_layout_border_width_kind_name(ROBO_LAYOUT_BORDER_WIDTH_RESOLVED),
                      "resolved");
    ROBO_CHECK_STR_EQ(robo_layout_border_width_kind_name(ROBO_LAYOUT_BORDER_WIDTH_UNRESOLVED),
                      "unresolved");
    ROBO_CHECK_STR_EQ(robo_layout_border_width_kind_name((robo_layout_border_width_kind_t)3),
                      "unknown");
    ROBO_CHECK(robo_layout_border_width_kind_name((robo_layout_border_width_kind_t)77) != NULL);

    /*
     * And the three numbers this build picks for CSS's three keywords, stated as values
     * because they are a rendering decision rather than a parse: 9.15 keeps `thin`,
     * `medium` and `thick` as keywords, and *here* is where they become 1, 3 and 5. A
     * later goal that changes them changes three macros and this case, and nothing else.
     */
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_BORDER_THIN_PX, 1);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_BORDER_MEDIUM_PX, 3);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_BORDER_THICK_PX, 5);
}

static void test_borders_width_record(void)
{
    robo_layout_border_width_t zero = robo_layout_border_width_zero();
    robo_layout_border_width_t absent = robo_layout_border_width_absent();
    robo_layout_border_width_t resolved = robo_layout_border_width_resolved(12);
    robo_layout_border_width_t unresolved = robo_layout_border_width_unresolved();
    robo_layout_border_width_t negative = robo_layout_border_width_resolved(-5);
    robo_layout_border_width_t bad_kind;
    robo_gfx_pixel_x_t length = 0;

    layout_fixture();

    /* `border-width: 0` is a border an author asked not to have; `ABSENT` is a side
     * nobody declared. Both are used as zero and they are different records -- the
     * entire reason this vocabulary has three kinds. */
    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_resolved(&zero), 1);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_absent(&zero), 0);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_valid(&zero), 1);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_length(&zero, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 0);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_equal(&zero, &absent), 0);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_equal(&zero, &zero), 1);

    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_absent(&absent), 1);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_valid(&absent), 1);
    /* A miss rather than a zero: reading the field of a side nobody declared would be
     * reading a number that means nothing. */
    length = 77;
    ROBO_CHECK_EQ_INT(robo_layout_border_width_length(&absent, &length), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(length, 77);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_equal(&absent, &absent), 1);

    {
        robo_layout_border_width_t other = robo_layout_border_width_resolved(13);

        ROBO_CHECK_EQ_INT(robo_layout_border_width_is_resolved(&resolved), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_is_valid(&resolved), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_length(&resolved, &length), ROBO_OK);
        ROBO_CHECK_EQ_INT(length, 12);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_equal(&resolved, &resolved), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_equal(&resolved, &other), 0);
    }

    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_unresolved(&unresolved), 1);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_valid(&unresolved), 1);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_length(&unresolved, &length), ROBO_NOT_FOUND);

    /*
     * And the check this vocabulary has that the margin one does not: a border width is
     * never negative. `border-width: -5px` is invalid CSS, so a record holding one was
     * not produced by this module's own reader -- which is exactly why stating it as a
     * predicate is worth the line.
     */
    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_resolved(&negative), 1);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_valid(&negative), 0);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_length(&negative, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, -5);

    bad_kind = robo_layout_border_width_zero();
    bad_kind.kind = (robo_layout_border_width_kind_t)99;
    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_valid(&bad_kind), 0);

    /* Two `ABSENT` sides are equal whatever their stale `length` fields hold -- the
     * reason the kind exists rather than the length alone. */
    {
        robo_layout_border_width_t first = robo_layout_border_width_absent();
        robo_layout_border_width_t second = robo_layout_border_width_absent();

        first.length = 999;
        second.length = -999;
        ROBO_CHECK_EQ_INT(robo_layout_border_width_equal(&first, &second), 1);
    }

    /* NULL is not a record: a predicate answers 0 and the equal comparison answers only
     * for two NULLs. */
    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_resolved(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_length(NULL, &length), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_length(&resolved, NULL), ROBO_INVALID_ARGUMENT);
}

static void test_borders_side_record(void)
{
    size_t index;

    layout_fixture();

    {
        robo_layout_border_side_t none = robo_layout_border_side_none();
        robo_layout_border_side_t other = robo_layout_border_side_none();

        ROBO_CHECK_EQ_INT(robo_layout_border_width_is_absent(&none.width), 1);
        ROBO_CHECK_EQ_INT((int)none.style, (int)ROBO_CSS_BORDER_STYLE_NONE);
        ROBO_CHECK_EQ_INT((int)none.color.kind, (int)ROBO_CSS_COLOR_NONE);
        ROBO_CHECK_EQ_INT(robo_layout_border_side_is_valid(&none), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_side_generates(&none), 0);
        ROBO_CHECK_EQ_INT(robo_layout_border_side_equal(&none, &other), 1);
    }

    /*
     * The `generates` question, walked over the whole style vocabulary rather than a
     * representative: a *declared* width of five draws with eight of the ten styles and
     * does not with `none` or `hidden`. The two that do not are kept as distinct values
     * that answer this one question the same way -- see the note two assertions below.
     */
    for (index = 0u; index < ROBO_CSS_BORDER_STYLE_COUNT; index++) {
        robo_css_border_style_t style = (robo_css_border_style_t)index;
        robo_layout_border_side_t side = layout_border_side_make(
            robo_layout_border_width_resolved(5), style, robo_css_color_none());

        ROBO_CHECK_EQ_INT(robo_css_border_style_is_known(style), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_side_generates(&side),
                          robo_css_border_style_generates_border(style));
        ROBO_CHECK(side.style == style);
    }
    ROBO_CHECK_EQ_INT(robo_css_border_style_generates_border(ROBO_CSS_BORDER_STYLE_NONE), 0);
    /* `hidden` is the *second* style that draws nothing, and it is 9.15's own answer
     * rather than this phase's: the two differ only in border collapsing, where `hidden`
     * outranks the rest -- a table rule this build has no tables for. So the two are kept
     * as distinct values that answer this one question the same way. */
    ROBO_CHECK_EQ_INT(robo_css_border_style_generates_border(ROBO_CSS_BORDER_STYLE_HIDDEN), 0);
    ROBO_CHECK_EQ_INT(robo_css_border_style_generates_border(ROBO_CSS_BORDER_STYLE_SOLID), 1);
    ROBO_CHECK_EQ_INT(robo_css_border_style_generates_border((robo_css_border_style_t)77), 0);

    /* And a width that is zero, nobody's or unusable draws nothing whatever the style
     * is, which is the other half of the same question. */
    {
        robo_layout_border_side_t zero = layout_border_side_make(
            robo_layout_border_width_zero(), ROBO_CSS_BORDER_STYLE_SOLID,
            robo_css_color_none());
        robo_layout_border_side_t absent = layout_border_side_make(
            robo_layout_border_width_absent(), ROBO_CSS_BORDER_STYLE_DOUBLE,
            robo_css_color_none());
        robo_layout_border_side_t unresolved = layout_border_side_make(
            robo_layout_border_width_unresolved(), ROBO_CSS_BORDER_STYLE_DASHED,
            robo_css_color_none());

        ROBO_CHECK_EQ_INT(robo_layout_border_side_generates(&zero), 0);
        ROBO_CHECK_EQ_INT(robo_layout_border_side_generates(&absent), 0);
        ROBO_CHECK_EQ_INT(robo_layout_border_side_generates(&unresolved), 0);
    }

    /* Validity: a style that is not one of the ten, and a negative width, are both
     * refused. */
    {
        robo_layout_border_side_t bad_style = layout_border_side_make(
            robo_layout_border_width_resolved(5), (robo_css_border_style_t)99,
            robo_css_color_none());
        robo_layout_border_side_t bad_width = layout_border_side_make(
            robo_layout_border_width_resolved(-1), ROBO_CSS_BORDER_STYLE_SOLID,
            robo_css_color_none());

        ROBO_CHECK_EQ_INT(robo_layout_border_side_is_valid(&bad_style), 0);
        ROBO_CHECK_EQ_INT(robo_layout_border_side_is_valid(&bad_width), 0);
        ROBO_CHECK_EQ_INT(robo_layout_border_side_is_valid(NULL), 0);
    }

    /* Equality is all three facts, and for the colour the *kind*: a `currentColor` and a
     * resolved colour that happened to share channels are different values. */
    {
        robo_layout_border_side_t current = layout_border_side_make(
            robo_layout_border_width_resolved(5), ROBO_CSS_BORDER_STYLE_SOLID,
            robo_css_color_current());
        robo_layout_border_side_t same = layout_border_side_make(
            robo_layout_border_width_resolved(5), ROBO_CSS_BORDER_STYLE_SOLID,
            robo_css_color_current());
        robo_layout_border_side_t black = layout_border_side_make(
            robo_layout_border_width_resolved(5), ROBO_CSS_BORDER_STYLE_SOLID,
            robo_css_color_make(0, 0, 0, 255));
        robo_layout_border_side_t other_style = layout_border_side_make(
            robo_layout_border_width_resolved(5), ROBO_CSS_BORDER_STYLE_DASHED,
            robo_css_color_current());

        ROBO_CHECK_EQ_INT(robo_layout_border_side_equal(&current, &same), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_side_equal(&current, &black), 0);
        ROBO_CHECK_EQ_INT(robo_layout_border_side_equal(&current, &other_style), 0);
    }
}

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

static void test_borders_seam(void)
{
    layout_border_rule_t rule;
    robo_css_computed_style_t *style;
    robo_layout_borders_t borders;
    robo_layout_border_side_t side;
    robo_gfx_pixel_x_t length = 0;

    layout_fixture();

    /*
     * Twelve longhands, four readers, and the one place `thin`, `medium` and `thick`
     * become numbers. The shared classification folds every keyword but `auto` onto
     * UNUSABLE -- correctly, because no box dimension takes a keyword that names a
     * length -- so the width reader consults the keyword vocabulary on that branch and
     * nowhere else.
     */
    layout_border_rule_init(&rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&rule, "border-top-width", 5.0);
    layout_border_rule_border_style(&rule, "border-top-style", ROBO_CSS_BORDER_STYLE_SOLID);
    layout_border_rule_color(&rule, "border-top-color", robo_css_color_make(255, 0, 0, 255));
    layout_border_rule_keyword(&rule, "border-right-width", "thin");
    layout_border_rule_border_style(&rule, "border-right-style", ROBO_CSS_BORDER_STYLE_DASHED);
    layout_border_rule_keyword(&rule, "border-bottom-width", "medium");
    layout_border_rule_border_style(&rule, "border-bottom-style", ROBO_CSS_BORDER_STYLE_DOUBLE);
    layout_border_rule_keyword(&rule, "border-left-width", "thick");
    layout_border_rule_border_style(&rule, "border-left-style", ROBO_CSS_BORDER_STYLE_DOTTED);
    style = layout_border_style_build(&rule);

    borders = robo_layout_borders_of(style);
    ROBO_CHECK_EQ_INT(robo_layout_borders_is_valid(&borders), 1);

    ROBO_CHECK_EQ_INT(robo_layout_border_width_length(&borders.top.width, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 5);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_length(&borders.right.width, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, ROBO_LAYOUT_BORDER_THIN_PX);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_length(&borders.bottom.width, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, ROBO_LAYOUT_BORDER_MEDIUM_PX);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_length(&borders.left.width, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, ROBO_LAYOUT_BORDER_THICK_PX);

    ROBO_CHECK_EQ_INT((int)borders.top.style, (int)ROBO_CSS_BORDER_STYLE_SOLID);
    ROBO_CHECK_EQ_INT((int)borders.right.style, (int)ROBO_CSS_BORDER_STYLE_DASHED);
    ROBO_CHECK_EQ_INT((int)borders.bottom.style, (int)ROBO_CSS_BORDER_STYLE_DOUBLE);
    ROBO_CHECK_EQ_INT((int)borders.left.style, (int)ROBO_CSS_BORDER_STYLE_DOTTED);

    /* The colour is 9.9's record, carried unaltered: red where the author wrote one, and
     * `NONE` -- "nobody declared one" rather than `transparent` -- on the other three. */
    ROBO_CHECK_EQ_INT((int)borders.top.color.kind, (int)ROBO_CSS_COLOR_RGBA);
    ROBO_CHECK_EQ_INT(borders.top.color.red, 255);
    ROBO_CHECK_EQ_INT(borders.top.color.green, 0);
    ROBO_CHECK_EQ_INT(borders.top.color.blue, 0);
    ROBO_CHECK_EQ_INT(borders.top.color.alpha, 255);
    ROBO_CHECK_EQ_INT((int)borders.right.color.kind, (int)ROBO_CSS_COLOR_NONE);

    ROBO_CHECK_EQ_INT(robo_layout_borders_side(&borders, ROBO_CSS_BOX_SIDE_TOP, &side), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_border_side_equal(&side, &borders.top), 1);
    ROBO_CHECK_EQ_INT(robo_layout_borders_side(&borders, (robo_css_box_side_t)9, &side),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_borders_side(NULL, ROBO_CSS_BOX_SIDE_TOP, &side),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_borders_side(&borders, ROBO_CSS_BOX_SIDE_TOP, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_css_computed_style_destroy(style);

    /*
     * `border-color: red blue green black` writes four longhands, so all four colours
     * have to reach their own side rather than one of them travelling for the rest. And
     * `currentColor` survives as a *contextual* colour: this layer has a style rather than
     * an element, so the layer that does have one resolves it -- copying channels here
     * would be inventing a second colour system.
     */
    {
        layout_border_rule_t coloured;

        layout_border_rule_init(&coloured, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_border_rule_color_all(&coloured, robo_css_color_make(1, 2, 3, 4));
        style = layout_border_style_build(&coloured);
        borders = robo_layout_borders_of(style);
        ROBO_CHECK_EQ_INT((int)borders.top.color.kind, (int)ROBO_CSS_COLOR_RGBA);
        ROBO_CHECK_EQ_INT(borders.right.color.red, 1);
        ROBO_CHECK_EQ_INT(borders.right.color.green, 2);
        ROBO_CHECK_EQ_INT(borders.bottom.color.blue, 3);
        ROBO_CHECK_EQ_INT(borders.left.color.alpha, 4);
        ROBO_CHECK_EQ_INT(borders.left.color.kind, ROBO_CSS_COLOR_RGBA);
        robo_css_computed_style_destroy(style);

        layout_border_rule_init(&coloured, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_border_rule_px(&coloured, "border-top-width", 3.0);
        layout_border_rule_border_style(&coloured, "border-top-style",
                                        ROBO_CSS_BORDER_STYLE_SOLID);
        layout_border_rule_color(&coloured, "border-top-color", robo_css_color_current());
        style = layout_border_style_build(&coloured);
        borders = robo_layout_borders_of(style);
        ROBO_CHECK_EQ_INT((int)borders.top.color.kind, (int)ROBO_CSS_COLOR_CURRENT);
        ROBO_CHECK_EQ_INT(robo_css_color_is_current(borders.top.color), 1);
        robo_css_computed_style_destroy(style);
    }

    /*
     * A document that declared nothing: four undeclared sides, `none` styles and no
     * colours. That is CSS's initial state read through this vocabulary -- and the
     * geometry must treat it as no border rather than as `medium`, which is what the
     * `ABSENT` kind is for.
     */
    {
        layout_border_rule_t bare;

        layout_border_rule_init(&bare, "div", ROBO_CSS_DISPLAY_BLOCK);
        style = layout_border_style_build(&bare);
        borders = robo_layout_borders_of(style);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_is_absent(&borders.top.width), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_is_absent(&borders.right.width), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_is_absent(&borders.bottom.width), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_is_absent(&borders.left.width), 1);
        ROBO_CHECK_EQ_INT((int)borders.top.style, (int)ROBO_CSS_BORDER_STYLE_NONE);
        ROBO_CHECK_EQ_INT((int)borders.top.color.kind, (int)ROBO_CSS_COLOR_NONE);
        ROBO_CHECK_EQ_INT(robo_layout_borders_equal(&borders,
                                                    &(robo_layout_borders_t){0}),
                          1);
        robo_css_computed_style_destroy(style);
    }

    /* A NULL style makes no declarations, which is four undeclared sides rather than a
     * failure -- a box with no style has declared nothing. */
    borders = robo_layout_borders_of(NULL);
    ROBO_CHECK_EQ_INT(robo_layout_borders_is_valid(&borders), 1);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_absent(&borders.top.width), 1);
    ROBO_CHECK_EQ_INT(robo_layout_border_width_is_absent(&borders.left.width), 1);

    /*
     * Every declaration this build cannot use, one per property shape: a relative unit,
     * a percentage (which CSS does not define for a border width at all -- so unlike a
     * percentage padding, an `UNRESOLVED` width here is never a percentage this build
     * declined to resolve), a negative length, and the `auto` the grammar forbids.
     */
    {
        layout_border_rule_t bad;

        layout_border_rule_init(&bad, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_border_rule_em(&bad, "border-top-width", 2.0);
        layout_border_rule_percentage(&bad, "border-right-width", 50.0);
        layout_border_rule_negative(&bad, "border-bottom-width", -5.0);
        layout_border_rule_keyword(&bad, "border-left-width", "auto");
        style = layout_border_style_build(&bad);
        borders = robo_layout_borders_of(style);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_is_unresolved(&borders.top.width), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_is_unresolved(&borders.right.width), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_is_unresolved(&borders.bottom.width), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_is_unresolved(&borders.left.width), 1);
        /* A negative length is the one that has a *value* to refuse rather than a shape,
         * so its number is still visible on the classification's refusal. */
        ROBO_CHECK_EQ_INT(robo_layout_borders_is_valid(&borders), 1);
        robo_css_computed_style_destroy(style);
    }

    /*
     * And a `var()`-shaped length: a px value flagged unresolved, which is the shape the
     * computed layer gives a declaration whose substitution has not happened. It reaches
     * the same `UNRESOLVED` kind for a different reason than the four above, and the
     * count is what a document depending on one is visible through.
     */
    {
        layout_border_rule_t flagged;
        robo_layout_border_edges_t edges;

        layout_border_rule_init(&flagged, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_border_rule_unresolved(&flagged, "border-top-width", 4.0);
        layout_border_rule_border_style(&flagged, "border-top-style",
                                        ROBO_CSS_BORDER_STYLE_SOLID);
        style = layout_border_style_build(&flagged);
        borders = robo_layout_borders_of(style);
        ROBO_CHECK_EQ_INT(robo_layout_border_width_is_unresolved(&borders.top.width), 1);
        ROBO_CHECK_EQ_INT(robo_layout_border_resolve(&borders, &edges, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(edges.top.width, 0);
        ROBO_CHECK_EQ_INT(edges.widths_unresolved, 1u);
        ROBO_CHECK_EQ_INT(edges.widths_resolved, 0u);
        robo_css_computed_style_destroy(style);
    }
}

/* ------------------------------------------------------------------ */
/* The used values                                                     */
/* ------------------------------------------------------------------ */

static void test_borders_edges_record(void)
{
    robo_layout_borders_t declared;
    robo_layout_border_edges_t edges;
    robo_gfx_pixel_x_t widths[ROBO_CSS_BOX_SIDE_COUNT];
    robo_gfx_pixel_x_t width = 0;

    layout_fixture();

    {
        robo_layout_border_edges_t cleared;

        robo_layout_border_edges_init(&cleared);
        ROBO_CHECK_EQ_INT(cleared.top.width, 0);
        ROBO_CHECK_EQ_INT(cleared.right.width, 0);
        ROBO_CHECK_EQ_INT(cleared.bottom.width, 0);
        ROBO_CHECK_EQ_INT(cleared.left.width, 0);
        ROBO_CHECK_EQ_INT(cleared.widths_resolved, 0u);
        ROBO_CHECK_EQ_INT(cleared.widths_unresolved, 0u);
        ROBO_CHECK_EQ_INT(cleared.borders_drawn, 0u);
        ROBO_CHECK_EQ_INT(robo_layout_border_edges_is_valid(&cleared), 1);
    }

    /*
     * The fourth resolve rule, and the phase's whole novelty, in one record: a declared
     * five whose style does not draw is a used **zero**. `right` is that side; `bottom`
     * is a declaration this build cannot use; `left` is a side nobody declared; and only
     * `top` takes room.
     */
    declared.top = layout_border_side_make(robo_layout_border_width_resolved(5),
                                          ROBO_CSS_BORDER_STYLE_SOLID,
                                          robo_css_color_make(10, 20, 30, 255));
    declared.right = layout_border_side_make(robo_layout_border_width_resolved(5),
                                            ROBO_CSS_BORDER_STYLE_NONE, robo_css_color_none());
    declared.bottom = layout_border_side_make(robo_layout_border_width_unresolved(),
                                              ROBO_CSS_BORDER_STYLE_SOLID,
                                              robo_css_color_none());
    declared.left = layout_border_side_make(robo_layout_border_width_absent(),
                                            ROBO_CSS_BORDER_STYLE_SOLID,
                                            robo_css_color_none());
    ROBO_CHECK_EQ_INT(robo_layout_borders_is_valid(&declared), 1);
    ROBO_CHECK_EQ_INT(robo_layout_border_resolve(&declared, &edges, NULL), ROBO_OK);

    layout_border_widths_of(&edges, widths);
    ROBO_CHECK_EQ_INT(widths[ROBO_CSS_BOX_SIDE_TOP], 5);
    ROBO_CHECK_EQ_INT(widths[ROBO_CSS_BOX_SIDE_RIGHT], 0);
    ROBO_CHECK_EQ_INT(widths[ROBO_CSS_BOX_SIDE_BOTTOM], 0);
    ROBO_CHECK_EQ_INT(widths[ROBO_CSS_BOX_SIDE_LEFT], 0);

    /*
     * And the three counters answer three questions: two sides were *declared* with a
     * width this build can use, one was declared with one it cannot, one was declared
     * with none at all, and exactly one takes room and will draw.
     */
    ROBO_CHECK_EQ_INT(edges.widths_resolved, 2u);
    ROBO_CHECK_EQ_INT(edges.widths_unresolved, 1u);
    ROBO_CHECK_EQ_INT(edges.borders_drawn, 1u);
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_is_valid(&edges), 1);

    /* The style and the colour travel with the side whatever its used width came to, so
     * a paint pass reads one record and re-reads no declaration. */
    ROBO_CHECK_EQ_INT((int)edges.top.style, (int)ROBO_CSS_BORDER_STYLE_SOLID);
    ROBO_CHECK_EQ_INT(edges.top.color.red, 10);
    ROBO_CHECK_EQ_INT(edges.top.color.green, 20);
    ROBO_CHECK_EQ_INT(edges.top.color.blue, 30);
    ROBO_CHECK_EQ_INT((int)edges.right.style, (int)ROBO_CSS_BORDER_STYLE_NONE);
    ROBO_CHECK_EQ_INT((int)edges.left.style, (int)ROBO_CSS_BORDER_STYLE_SOLID);

    ROBO_CHECK_EQ_INT(robo_layout_border_edges_width(&edges, ROBO_CSS_BOX_SIDE_TOP, &width),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(width, 5);
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_width(&edges, (robo_css_box_side_t)77, &width),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_width(NULL, ROBO_CSS_BOX_SIDE_TOP, &width),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_width(&edges, ROBO_CSS_BOX_SIDE_TOP, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Equality is every field including the counters: two records that agreed about the
     * four numbers and disagreed about how many declarations they could not use are two
     * different answers to one document. */
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_equal(&edges, &edges), 1);
    {
        robo_layout_border_edges_t same = edges;

        ROBO_CHECK_EQ_INT(robo_layout_border_edges_equal(&edges, &same), 1);
        same.borders_drawn = 0u;
        ROBO_CHECK_EQ_INT(robo_layout_border_edges_equal(&edges, &same), 0);
        same = edges;
        same.right.color = robo_css_color_make(1, 2, 3, 255);
        ROBO_CHECK_EQ_INT(robo_layout_border_edges_equal(&edges, &same), 0);
        same = edges;
        same.top.style = ROBO_CSS_BORDER_STYLE_DASHED;
        ROBO_CHECK_EQ_INT(robo_layout_border_edges_equal(&edges, &same), 0);
    }

    /*
     * And the invariant a hand-built record cannot smuggle past: a used width above zero
     * **implies** the style draws. A record that violated it would be a border geometry
     * charged for and paint drew as nothing.
     */
    {
        robo_layout_border_edges_t bad = layout_border_used(5, 0, 0, 0);

        ROBO_CHECK_EQ_INT(robo_layout_border_edges_is_valid(&bad), 1);
        bad.top.width = 5;
        bad.top.style = ROBO_CSS_BORDER_STYLE_NONE;
        ROBO_CHECK_EQ_INT(robo_layout_border_edges_is_valid(&bad), 0);

        bad = layout_border_used(5, 0, 0, 0);
        bad.left.width = -1;
        ROBO_CHECK_EQ_INT(robo_layout_border_edges_is_valid(&bad), 0);

        bad = layout_border_used(5, 5, 5, 5);
        bad.widths_resolved = 5u;
        ROBO_CHECK_EQ_INT(robo_layout_border_edges_is_valid(&bad), 0);

        bad = layout_border_used(5, 5, 5, 5);
        bad.borders_drawn = 5u;
        ROBO_CHECK_EQ_INT(robo_layout_border_edges_is_valid(&bad), 0);

        bad = layout_border_used(5, 5, 5, 5);
        bad.widths_resolved = 3u;
        ROBO_CHECK_EQ_INT(robo_layout_border_edges_is_valid(&bad), 0);

        ROBO_CHECK_EQ_INT(robo_layout_border_edges_is_valid(NULL), 0);
    }

    ROBO_CHECK_EQ_INT(robo_layout_border_resolve(NULL, &edges, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_border_resolve(&declared, NULL, NULL), ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* The two derived numbers                                             */
/* ------------------------------------------------------------------ */

static void test_borders_derived_boxes(void)
{
    robo_layout_border_edges_t edges = layout_border_used(5, 10, 15, 20);
    robo_layout_size_t padding_box;
    robo_layout_size_t border_box;

    padding_box.width = 0;
    padding_box.height = 0;
    border_box.width = 0;
    border_box.height = 0;
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;
    int32_t available = 0;

    layout_fixture();

    /*
     * Downward: the containing block less the two horizontal borders, which is the
     * number 10.10's own content-width door is then handed. A box whose borders are 20
     * on the left and 10 on the right has 770 of padding box inside an 800-wide
     * containing block -- not 800, and not 770 minus anything else.
     */
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_padding_box_width(&edges, 800, &available,
                                                                &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(available, 770);

    /* A containing block narrower than the box's own borders leaves **no** room rather
     * than a negative width, and the floor is on the result. */
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_padding_box_width(&edges, 12, &available, &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(available, 0);

    ROBO_CHECK_EQ_INT(robo_layout_border_edges_padding_box_width(&edges, 40, &available, &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(available, 10);

    /* A record no size can be derived from is refused *before* a size is derived from it,
     * with a diagnostic rather than a floor. */
    {
        robo_layout_border_edges_t bad = edges;
        robo_result_t status;
        int32_t untouched = 4242;

        bad.left.width = -3;
        status = robo_layout_border_edges_padding_box_width(&bad, 800, &untouched, &diag);
        ROBO_CHECK_EQ_INT(status, ROBO_ERROR);
        ROBO_CHECK_EQ_INT(untouched, 4242);
        ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_BORDER);
        ROBO_CHECK_EQ_INT((int)diag.code, (int)ROBO_ERROR);

        robo_layout_diag_reset(&diag);
        status = robo_layout_border_edges_border_box(&bad, padding_box, &border_box, &diag);
        ROBO_CHECK_EQ_INT(status, ROBO_ERROR);
        ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_BORDER);
    }

    /* Upward: the padding box plus the four sides, one addition per axis, which is the
     * last number before a result. */
    padding_box.width = 200;
    padding_box.height = 100;
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_border_box(&edges, padding_box, &border_box,
                                                         &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(border_box.width, 200 + 20 + 10);
    ROBO_CHECK_EQ_INT(border_box.height, 100 + 5 + 15);

    /* A padding box that is itself zero: the border box is the border alone, which is
     * the shape an `inline-block` with no contents has. */
    padding_box.width = 0;
    padding_box.height = 0;
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_border_box(&edges, padding_box, &border_box,
                                                         &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(border_box.width, 30);
    ROBO_CHECK_EQ_INT(border_box.height, 20);

    /*
     * And the saturation, which is the arithmetic finding of this layer: a padding box at
     * the seam's maximum is a number whose *sum* with a border is not representable, so it
     * clamps rather than wrapping into a negative rectangle. The two clamps compose with
     * 10.10's because each saturates at the same bound.
     */
    padding_box.width = 2147483647;
    padding_box.height = 2147483647;
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_border_box(&edges, padding_box, &border_box,
                                                         &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(border_box.width, 2147483647);
    ROBO_CHECK_EQ_INT(border_box.height, 2147483647);

    /* A negative padding box is a caller bug rather than a document: refused, with the
     * output untouched. */
    {
        robo_layout_size_t negative;
        robo_layout_size_t untouched;

        negative.width = -1;
        negative.height = 10;
        untouched.width = 4242;
        untouched.height = 4242;
        ROBO_CHECK_EQ_INT(robo_layout_border_edges_border_box(&edges, negative, &untouched,
                                                             &diag),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(untouched.width, 4242);
    }

    /* NULLs, both doors. */
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_padding_box_width(NULL, 800, &available, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_padding_box_width(&edges, 800, NULL, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_border_box(NULL, padding_box, &border_box,
                                                         &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_border_box(&edges, padding_box, NULL, &diag),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* The block pass                                                      */
/* ------------------------------------------------------------------ */

static void test_borders_block_declared_width(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_point_t origin;
    robo_layout_size_t size;
    robo_layout_border_edges_t edges;
    robo_layout_padding_edges_t padding;
    robo_layout_node_id_t div_id;

    layout_fixture();

    /*
     * A declared width is the **content** box under this build's `content-box` model, so
     * the box a caller reads is that plus the two horizontal borders: 200 declared, 5 and
     * 5 declared, 210 on screen. The `position` is the *border* box's origin, and a
     * border does not move a box -- it grows it -- so it is unchanged.
     */
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "width", 200.0);
    layout_border_rule_px(&div_rule, "height", 50.0);
    layout_border_rule_px(&div_rule, "border-left-width", 5.0);
    layout_border_rule_px(&div_rule, "border-right-width", 5.0);
    layout_border_rule_style_all(&div_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_border_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, &stats);

    div_id = layout_border_box_for(&fixture, "div");
    origin = layout_border_origin_of(&fixture, div_id);
    size = layout_border_size_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(origin.y, 0);
    ROBO_CHECK_EQ_INT(size.width, 210);
    ROBO_CHECK_EQ_INT(size.height, 50);

    /* And the record a paint pass reads, and the two insets that compose a content
     * origin: the border first, then the padding (none here). */
    edges = layout_border_edges_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(edges.left.width, 5);
    ROBO_CHECK_EQ_INT(edges.right.width, 5);
    ROBO_CHECK_EQ_INT(edges.top.width, 0);
    ROBO_CHECK_EQ_INT(edges.widths_resolved, 2u);
    ROBO_CHECK_EQ_INT(edges.borders_drawn, 2u);
    ROBO_CHECK_EQ_INT((int)edges.left.style, (int)ROBO_CSS_BORDER_STYLE_SOLID);

    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture.tree, div_id, &padding, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(padding.left, 0);
    ROBO_CHECK_EQ_INT(padding.top, 0);

    ROBO_CHECK_EQ_INT(stats.block.borders_applied, 2u);
    ROBO_CHECK_EQ_INT(stats.block.borders_unresolved, 0u);
    ROBO_CHECK_EQ_INT(stats.block.borders_drawn, 2u);

    layout_border_end(&fixture);
}

static void test_borders_block_auto_width(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t rules[3];
    robo_layout_size_t size;

    layout_fixture();

    /*
     * CSS's constraint equation has a border term on each side, so a box with
     * `border: 10px solid` and **no declared width** is exactly as wide as its containing
     * block rather than 20 wider. A build that grew the box outward would answer 820
     * here, and this is the case that fails it.
     */
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "height", 20.0);
    layout_border_rule_px(&div_rule, "border-left-width", 10.0);
    layout_border_rule_px(&div_rule, "border-right-width", 10.0);
    layout_border_rule_style_all(&div_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_border_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, LAYOUT_BORDER_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(size.height, 20);

    layout_border_end(&fixture);

    /* The contrast: the same box with no border is also 800 wide, so "it fills its
     * containing block" is a fact about both rather than about the border. */
    {
        layout_border_fixture_t second;
        layout_border_rule_t bare;

        layout_fixture();
        layout_border_rule_init(&bare, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_border_rule_px(&bare, "height", 20.0);
        layout_border_begin(&second, "<html><body><div></div></body></html>",
                            (layout_border_rule_t[]){html_rule, body_rule, bare}, 3u);
        layout_border_pass(&second, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
        size = layout_border_size_of(&second, layout_border_box_for(&second, "div"));
        ROBO_CHECK_EQ_INT(size.width, LAYOUT_BORDER_VIEWPORT_WIDTH);
        layout_border_end(&second);
    }
}

static void test_borders_block_sides(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t rules[3];
    robo_layout_border_edges_t edges;
    robo_layout_size_t size;
    robo_gfx_pixel_x_t widths[ROBO_CSS_BOX_SIDE_COUNT];
    robo_layout_node_id_t div_id;

    layout_fixture();

    /*
     * Four different sides at once, so that each contributes its own term and none is
     * silently read from another: the two horizontal ones come out of the width (which is
     * `auto`, so the box still fills its containing block exactly), and the two vertical
     * ones grow the height.
     */
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "height", 10.0);
    layout_border_rule_px(&div_rule, "border-top-width", 3.0);
    layout_border_rule_px(&div_rule, "border-right-width", 4.0);
    layout_border_rule_px(&div_rule, "border-bottom-width", 5.0);
    layout_border_rule_px(&div_rule, "border-left-width", 6.0);
    layout_border_rule_style_all(&div_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_border_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    div_id = layout_border_box_for(&fixture, "div");
    size = layout_border_size_of(&fixture, div_id);
    /* The containing block exactly, even though 10 of its 800 is border: 800 - 4 - 6 =
     * 790 of content, plus the 10 back = 800. */
    ROBO_CHECK_EQ_INT(size.width, LAYOUT_BORDER_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(size.height, 10 + 3 + 5);

    edges = layout_border_edges_of(&fixture, div_id);
    layout_border_widths_of(&edges, widths);
    ROBO_CHECK_EQ_INT(widths[ROBO_CSS_BOX_SIDE_TOP], 3);
    ROBO_CHECK_EQ_INT(widths[ROBO_CSS_BOX_SIDE_RIGHT], 4);
    ROBO_CHECK_EQ_INT(widths[ROBO_CSS_BOX_SIDE_BOTTOM], 5);
    ROBO_CHECK_EQ_INT(widths[ROBO_CSS_BOX_SIDE_LEFT], 6);
    ROBO_CHECK_EQ_INT(edges.borders_drawn, 4u);

    layout_border_end(&fixture);
}

static void test_borders_block_vertical(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t i_rule;
    layout_border_rule_t rules[4];
    robo_layout_size_t size;
    robo_layout_point_t origin;

    layout_fixture();

    /*
     * A border is inside the box, so `border-top: 15px` moves the contents **down**
     * without moving the box, and `border-bottom: 25px` grows the box below them. The
     * box's own origin is unchanged -- that is what makes a border a border rather than a
     * margin, and it is the assertion 10.9's suite makes the other way round.
     */
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "border-top-width", 15.0);
    layout_border_rule_px(&div_rule, "border-bottom-width", 25.0);
    layout_border_rule_style_all(&div_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    layout_border_rule_init(&i_rule, "i", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&i_rule, "height", 10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = i_rule;

    layout_border_begin(&fixture, "<html><body><div><i></i></div></body></html>", rules, 4u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    origin = layout_border_origin_of(&fixture, layout_border_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(origin.y, 0);

    /* The child is positioned relative to its containing block's **content** origin, so
     * it is at 0 whatever the border-top is -- the border is accounted for by the parent's
     * content origin, not by shifting the child. */
    origin = layout_border_origin_of(&fixture, layout_border_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(origin.y, 0);

    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.height, 10 + 15 + 25);
    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(size.height, 10);

    layout_border_end(&fixture);
}

static void test_borders_block_stacking(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t i_rule;
    layout_border_rule_t b_rule;
    layout_border_rule_t rules[5];
    robo_layout_size_t size;
    robo_layout_point_t origin;

    layout_fixture();

    /*
     * What a parent stacks by is the child's **border** box, and the margin it declared is
     * still *outside* it: `i` is 20 tall (10 declared plus 5 and 5 of border), `b` declares
     * a 4px top margin and 6px of border-top, so its origin is 24.
     */
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&i_rule, "i", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&i_rule, "height", 10.0);
    layout_border_rule_px(&i_rule, "border-top-width", 5.0);
    layout_border_rule_px(&i_rule, "border-bottom-width", 5.0);
    layout_border_rule_style_all(&i_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    layout_border_rule_init(&b_rule, "b", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&b_rule, "height", 10.0);
    layout_border_rule_px(&b_rule, "margin-top", 4.0);
    layout_border_rule_px(&b_rule, "border-top-width", 6.0);
    layout_border_rule_style_all(&b_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = i_rule;
    rules[4] = b_rule;

    layout_border_begin(&fixture, "<html><body><div><i></i><b></b></div></body></html>", rules,
                        5u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    origin = layout_border_origin_of(&fixture, layout_border_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(origin.y, 0);
    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(size.height, 20);

    origin = layout_border_origin_of(&fixture, layout_border_box_for(&fixture, "b"));
    ROBO_CHECK_EQ_INT(origin.y, 24);
    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "b"));
    ROBO_CHECK_EQ_INT(size.height, 16);

    layout_border_end(&fixture);
}

static void test_borders_block_unresolved(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_border_edges_t edges;
    robo_layout_size_t size;
    robo_layout_node_id_t div_id;

    layout_fixture();

    /*
     * A border this build cannot use takes no room, and is *counted* so that a document
     * whose geometry depends on one is visible as such rather than merely flush: the box
     * is 800 wide exactly as a box with no border is, and the pass says two declarations
     * were refused.
     */
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "height", 20.0);
    layout_border_rule_em(&div_rule, "border-left-width", 2.0);
    layout_border_rule_percentage(&div_rule, "border-right-width", 50.0);
    layout_border_rule_style_all(&div_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_border_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, &stats);

    div_id = layout_border_box_for(&fixture, "div");
    size = layout_border_size_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(size.width, LAYOUT_BORDER_VIEWPORT_WIDTH);

    edges = layout_border_edges_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(edges.left.width, 0);
    ROBO_CHECK_EQ_INT(edges.right.width, 0);
    ROBO_CHECK_EQ_INT(edges.widths_unresolved, 2u);
    ROBO_CHECK_EQ_INT(edges.borders_drawn, 0u);

    ROBO_CHECK_EQ_INT(stats.block.borders_applied, 0u);
    ROBO_CHECK_EQ_INT(stats.block.borders_unresolved, 2u);
    ROBO_CHECK_EQ_INT(stats.block.borders_drawn, 0u);

    layout_border_end(&fixture);
}

static void test_borders_none_style(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_border_edges_t edges;
    robo_layout_size_t size;
    robo_layout_node_id_t div_id;

    layout_fixture();

    /*
     * The phase's one new rule, and the most plausible way to get it wrong: twelve
     * declarations reached this layer and **not one of them takes room**, because
     * `border-style: none` gives every side a used width of zero. A build that used the
     * declared widths would answer 110 by 20 here.
     */
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "width", 100.0);
    layout_border_rule_px(&div_rule, "height", 10.0);
    layout_border_rule_width_all(&div_rule, 5.0);
    layout_border_rule_style_all(&div_rule, ROBO_CSS_BORDER_STYLE_NONE);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_border_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, &stats);

    div_id = layout_border_box_for(&fixture, "div");
    size = layout_border_size_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(size.width, 100);
    ROBO_CHECK_EQ_INT(size.height, 10);

    edges = layout_border_edges_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(edges.widths_resolved, 4u);
    ROBO_CHECK_EQ_INT(edges.widths_unresolved, 0u);
    ROBO_CHECK_EQ_INT(edges.borders_drawn, 0u);
    ROBO_CHECK_EQ_INT(edges.top.width, 0);
    ROBO_CHECK_EQ_INT(edges.left.width, 0);
    /* The *style* still travels with the side, so a paint pass reads `none` rather than
     * inferring it from a zero width. */
    ROBO_CHECK_EQ_INT((int)edges.left.style, (int)ROBO_CSS_BORDER_STYLE_NONE);

    /* Four declarations resolved and none drawn: the pair a single counter could not
     * report. */
    ROBO_CHECK_EQ_INT(stats.block.borders_applied, 4u);
    ROBO_CHECK_EQ_INT(stats.block.borders_drawn, 0u);

    layout_border_end(&fixture);

    /* And the contrast in one number: the same declarations with `solid` are 110 by 20. */
    {
        layout_border_fixture_t second;
        layout_border_rule_t solid;

        layout_fixture();
        layout_border_rule_init(&solid, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_border_rule_px(&solid, "width", 100.0);
        layout_border_rule_px(&solid, "height", 10.0);
        layout_border_rule_shorthand(&solid, 5.0);
        layout_border_begin(&second, "<html><body><div></div></body></html>",
                            (layout_border_rule_t[]){html_rule, body_rule, solid}, 3u);
        layout_border_pass(&second, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
        size = layout_border_size_of(&second, layout_border_box_for(&second, "div"));
        ROBO_CHECK_EQ_INT(size.width, 110);
        ROBO_CHECK_EQ_INT(size.height, 20);
        layout_border_end(&second);
    }

    /*
     * `hidden` is the *second* style that draws nothing, so it takes the same geometry
     * answer as `none` -- and the distinction is kept as a **value** rather than lost:
     * CSS's difference between the two is a border-collapsing rule for tables, and the
     * record a paint pass reads still says which of the two an author wrote. A build that
     * reported `none` for a `hidden` side would have thrown away the one fact that will
     * matter the day tables arrive.
     */
    {
        layout_border_fixture_t third;
        layout_border_rule_t hidden;

        layout_fixture();
        layout_border_rule_init(&hidden, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_border_rule_px(&hidden, "width", 100.0);
        layout_border_rule_px(&hidden, "height", 10.0);
        layout_border_rule_shorthand(&hidden, 5.0);
        layout_border_rule_style_all(&hidden, ROBO_CSS_BORDER_STYLE_HIDDEN);
        layout_border_begin(&third, "<html><body><div></div></body></html>",
                            (layout_border_rule_t[]){html_rule, body_rule, hidden}, 3u);
        layout_border_pass(&third, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
        size = layout_border_size_of(&third, layout_border_box_for(&third, "div"));
        ROBO_CHECK_EQ_INT(size.width, 100);
        ROBO_CHECK_EQ_INT(size.height, 10);
        edges = layout_border_edges_of(&third, layout_border_box_for(&third, "div"));
        ROBO_CHECK_EQ_INT((int)edges.left.style, (int)ROBO_CSS_BORDER_STYLE_HIDDEN);
        ROBO_CHECK_EQ_INT(edges.widths_resolved, 4u);
        ROBO_CHECK_EQ_INT(edges.borders_drawn, 0u);
        layout_border_end(&third);
    }
}

static void test_borders_margin_interaction(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t i_rule;
    layout_border_rule_t rules[4];
    robo_layout_point_t origin;
    robo_layout_size_t size;

    layout_fixture();

    /*
     * Margin outside, border and padding inside, in one box: `margin: 10px`,
     * `border: 5px solid`, `padding: 5px`, `width: 100px`. The origin is the margin's
     * (10, 10), the size is the *border* box (120 by 40), and what the parent stacks by is
     * that size plus the two margins (60). Three numbers, three different questions, and a
     * build that conflated any two of them would answer one of them wrong here.
     */
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "width", 100.0);
    layout_border_rule_px(&div_rule, "height", 20.0);
    layout_border_rule_px(&div_rule, "margin-top", 10.0);
    layout_border_rule_px(&div_rule, "margin-right", 10.0);
    layout_border_rule_px(&div_rule, "margin-bottom", 10.0);
    layout_border_rule_px(&div_rule, "margin-left", 10.0);
    layout_border_rule_shorthand(&div_rule, 5.0);
    layout_border_rule_px(&div_rule, "padding-top", 5.0);
    layout_border_rule_px(&div_rule, "padding-right", 5.0);
    layout_border_rule_px(&div_rule, "padding-bottom", 5.0);
    layout_border_rule_px(&div_rule, "padding-left", 5.0);
    layout_border_rule_init(&i_rule, "i", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&i_rule, "height", 3.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = i_rule;

    layout_border_begin(&fixture, "<html><body><div></div><i></i></body></html>", rules, 4u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    origin = layout_border_origin_of(&fixture, layout_border_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 10);
    ROBO_CHECK_EQ_INT(origin.y, 10);
    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 100 + 10 + 10);
    ROBO_CHECK_EQ_INT(size.height, 20 + 10 + 10);

    /* And the following sibling starts below the margin box: 40 of border box plus the
     * 10 below. */
    origin = layout_border_origin_of(&fixture, layout_border_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(origin.y, 60);

    layout_border_end(&fixture);

    /* One isolation, so the terms cannot cancel: a border with no margin moves nothing,
     * and a margin with no border changes no size. */
    {
        layout_border_fixture_t second;
        layout_border_rule_t bordered;

        layout_fixture();
        layout_border_rule_init(&bordered, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_border_rule_px(&bordered, "width", 100.0);
        layout_border_rule_px(&bordered, "height", 20.0);
        layout_border_rule_shorthand(&bordered, 5.0);
        layout_border_begin(&second, "<html><body><div></div><i></i></body></html>",
                            (layout_border_rule_t[]){html_rule, body_rule, bordered,
                                                     i_rule},
                            4u);
        layout_border_pass(&second, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
        origin = layout_border_origin_of(&second, layout_border_box_for(&second, "div"));
        ROBO_CHECK_EQ_INT(origin.x, 0);
        ROBO_CHECK_EQ_INT(origin.y, 0);
        origin = layout_border_origin_of(&second, layout_border_box_for(&second, "i"));
        ROBO_CHECK_EQ_INT(origin.y, 30);
        layout_border_end(&second);
    }
}

static void test_borders_padding_interaction(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t rules[3];
    robo_layout_border_edges_t borders;
    robo_layout_padding_edges_t padding;
    robo_layout_size_t size;
    robo_layout_node_id_t div_id;

    layout_fixture();

    /*
     * The order the box is built in, and the assertion that fails a build which placed the
     * contents at `padding-left` and forgot the border: `border-left: 5px` with
     * `padding-left: 10px` puts the contents 15 inside, not 10 and not 5. Both records are
     * on the object, and they compose in one order -- border first, then padding.
     */
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "width", 100.0);
    layout_border_rule_px(&div_rule, "height", 10.0);
    layout_border_rule_px(&div_rule, "border-left-width", 5.0);
    layout_border_rule_px(&div_rule, "border-top-width", 7.0);
    layout_border_rule_style_all(&div_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    layout_border_rule_px(&div_rule, "padding-left", 10.0);
    layout_border_rule_px(&div_rule, "padding-top", 3.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_border_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    div_id = layout_border_box_for(&fixture, "div");
    borders = layout_border_edges_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture.tree, div_id, &padding, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(borders.left.width, 5);
    ROBO_CHECK_EQ_INT(padding.left, 10);
    ROBO_CHECK_EQ_INT(borders.left.width + padding.left, 15);
    ROBO_CHECK_EQ_INT(borders.top.width, 7);
    ROBO_CHECK_EQ_INT(padding.top, 3);
    ROBO_CHECK_EQ_INT(borders.top.width + padding.top, 10);

    /* And the size is the composition of both, one addition per layer: 100 + 10 of
     * padding + 5 of border. */
    size = layout_border_size_of(&fixture, div_id);
    ROBO_CHECK_EQ_INT(size.width, 115);
    ROBO_CHECK_EQ_INT(size.height, 10 + 3 + 7);

    layout_border_end(&fixture);
}

static void test_borders_children_get_content_width(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t section_rule;
    layout_border_rule_t p_rule;
    layout_border_rule_t rules[5];
    robo_layout_size_t size;

    layout_fixture();

    /*
     * What a box's children are laid out into is its **content** width, so 20 of border on
     * each side of an 800-wide containing block leave 760 for a child that declares no
     * width of its own -- and the border is charged twice down the tree, once at each
     * level.
     */
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "border-left-width", 20.0);
    layout_border_rule_px(&div_rule, "border-right-width", 20.0);
    layout_border_rule_style_all(&div_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    layout_border_rule_init(&section_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&section_rule, "border-left-width", 10.0);
    layout_border_rule_style_all(&section_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    layout_border_rule_init(&p_rule, "p", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&p_rule, "height", 5.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = section_rule;
    rules[4] = p_rule;

    layout_border_begin(&fixture,
                        "<html><body><div><section><p></p></section></div></body></html>", rules,
                        5u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 800);

    /* 800 - 40 = 760 of content for the section, which then puts 10 of its own border
     * inside that, so the section is 760 wide and the paragraph is 750. */
    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "section"));
    ROBO_CHECK_EQ_INT(size.width, 760);
    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "p"));
    ROBO_CHECK_EQ_INT(size.width, 750);

    layout_border_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The inline pass                                                     */
/* ------------------------------------------------------------------ */

static void test_borders_inline_atomic(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t i_rule;
    layout_border_rule_t b_rule;
    layout_border_rule_t rules[5];
    robo_layout_stats_t stats;
    robo_layout_point_t origin;
    robo_layout_size_t size;
    robo_layout_line_box_t line;
    robo_layout_line_id_t line_id = ROBO_LAYOUT_LINE_ID_NONE;
    layout_border_rule_t widthless_rule;
    layout_border_fixture_t second;

    layout_fixture();

    /*
     * An atomic inline's advance is its **border** box since this phase, which is the same
     * sentence the block pass gained one direction over: an `inline-block` with 5 of
     * border-left and 15 of border-right occupies 120 on the line when it declares a
     * 100-wide box, and the participant after it starts 120 further on.
     */
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "height", 30.0);
    layout_border_rule_init(&i_rule, "i", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_border_rule_px(&i_rule, "width", 100.0);
    layout_border_rule_px(&i_rule, "height", 20.0);
    layout_border_rule_px(&i_rule, "border-left-width", 5.0);
    layout_border_rule_px(&i_rule, "border-right-width", 15.0);
    layout_border_rule_style_all(&i_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    layout_border_rule_init(&b_rule, "b", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_border_rule_px(&b_rule, "width", 50.0);
    layout_border_rule_px(&b_rule, "height", 10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = i_rule;
    rules[4] = b_rule;

    layout_border_begin(&fixture, "<html><body><div><i></i><b></b></div></body></html>", rules,
                        5u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, &stats);

    origin = layout_border_origin_of(&fixture, layout_border_box_for(&fixture, "i"));
    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(size.width, 120);
    ROBO_CHECK_EQ_INT(size.height, 20);

    origin = layout_border_origin_of(&fixture, layout_border_box_for(&fixture, "b"));
    ROBO_CHECK_EQ_INT(origin.x, 120);

    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture.tree, 0u, &line_id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, line_id, &line, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(line.size.width, 120 + 50);
    ROBO_CHECK_EQ_INT(line.members, 2u);
    ROBO_CHECK_EQ_INT(line.size.height, 20);
    ROBO_CHECK_EQ_INT(line.baseline.ascent, 20);
    ROBO_CHECK_EQ_INT(line.baseline.descent, 0);

    ROBO_CHECK_EQ_INT(stats.inline_flow.borders_applied, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.borders_unresolved, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.borders_drawn, 2u);

    layout_border_end(&fixture);

    /*
     * And an `auto` width fills what the borders left, which is this direction's copy of
     * the block pass's rule: 800 less 20 less 20 of border is the 760 inside a participant
     * that is exactly as wide as its containing block.
     */
    layout_fixture();
    layout_border_rule_init(&widthless_rule, "i", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_border_rule_px(&widthless_rule, "height", 20.0);
    layout_border_rule_px(&widthless_rule, "border-left-width", 20.0);
    layout_border_rule_px(&widthless_rule, "border-right-width", 20.0);
    layout_border_rule_style_all(&widthless_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    layout_border_begin(&second, "<html><body><div><i></i></div></body></html>",
                        (layout_border_rule_t[]){html_rule, body_rule, div_rule,
                                                 widthless_rule},
                        4u);
    layout_border_pass(&second, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
    size = layout_border_size_of(&second, layout_border_box_for(&second, "i"));
    ROBO_CHECK_EQ_INT(size.width, LAYOUT_BORDER_VIEWPORT_WIDTH);
    layout_border_end(&second);
}

static void test_borders_inline_transparent_not_applied(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t i_rule;
    layout_border_rule_t rules[4];
    robo_layout_stats_t stats;
    robo_layout_size_t size;
    robo_layout_node_id_t i_id;

    layout_fixture();

    /*
     * A **transparent inline box** with a border, which is the one participant this build
     * deliberately does not border. CSS does grow such a box's inline advance by its
     * horizontal border and its line box by its vertical one; this build applies neither,
     * and the reason is not economy: an inline box's rectangle is derived from its
     * *contents* rather than placed, so a border on it is a rule about fragmenting an
     * inline box across lines, which is the anonymous-box and line-breaking work this
     * module has not done.
     *
     * Asserted as a value -- zero extent, nothing applied, and the door saying so -- so
     * that the goal which implements it changes an assertion rather than discovering one.
     */
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&i_rule, "i", ROBO_CSS_DISPLAY_INLINE);
    layout_border_rule_shorthand(&i_rule, 20.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = i_rule;

    layout_border_begin(&fixture, "<html><body><div><i></i></div></body></html>", rules, 4u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, &stats);

    i_id = layout_border_box_for(&fixture, "i");
    size = layout_border_size_of(&fixture, i_id);
    ROBO_CHECK_EQ_INT(size.width, 0);
    ROBO_CHECK_EQ_INT(size.height, 0);
    ROBO_CHECK_EQ_INT(
        layout_border_size_of(&fixture, layout_border_box_for(&fixture, "div")).height, 0);

    /* The door *says* so rather than answering a zero that reads like an honoured
     * declaration that came to nothing. */
    {
        robo_layout_border_edges_t edges;
        robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;

        edges.left.width = 77;
        ROBO_CHECK_EQ_INT(robo_layout_object_borders(fixture.tree, i_id, &edges, &diag),
                          ROBO_NOT_FOUND);
        /* Cleared on the way out: a caller that ignores the code reads a stated absence. */
        ROBO_CHECK_EQ_INT(edges.left.width, 0);
        ROBO_CHECK_EQ_INT(edges.widths_resolved, 0u);
        ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_OBJECT);
    }

    ROBO_CHECK_EQ_INT(stats.inline_flow.borders_applied, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.non_atomic_placed, 1u);

    layout_border_end(&fixture);

    /*
     * And the contrast, in one number: the same box declared `inline-block` *does* get its
     * border, so "not applied" is a fact about the transparent box rather than about this
     * build's arithmetic. Here the participant's border box is 40 by 40 -- a 20px border on
     * each of the four sides of a box with no content extent of its own -- and the line is
     * 40 tall because that is its floor.
     */
    {
        layout_border_fixture_t second;
        layout_border_rule_t atomic_rule;
        layout_border_rule_t rules2[4];

        layout_fixture();
        layout_border_rule_init(&atomic_rule, "i", ROBO_CSS_DISPLAY_INLINE_BLOCK);
        layout_border_rule_shorthand(&atomic_rule, 20.0);
        rules2[0] = html_rule;
        rules2[1] = body_rule;
        rules2[2] = div_rule;
        rules2[3] = atomic_rule;
        layout_border_begin(&second, "<html><body><div><i></i></div></body></html>", rules2,
                            4u);
        layout_border_pass(&second, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
        size = layout_border_size_of(&second, layout_border_box_for(&second, "i"));
        ROBO_CHECK_EQ_INT(size.height, 40);
        ROBO_CHECK_EQ_INT(
            layout_border_size_of(&second, layout_border_box_for(&second, "div")).height, 40);
        layout_border_end(&second);
    }
}

/* ------------------------------------------------------------------ */
/* The read side                                                       */
/* ------------------------------------------------------------------ */

static void test_borders_object_door(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t rules[3];
    robo_layout_border_edges_t edges;
    robo_layout_node_id_t div_id;
    robo_layout_node_id_t text_id = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_node_id_t span_id;
    robo_layout_node_id_t inline_id;
    size_t count;
    size_t index;

    layout_fixture();

    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "border-left-width", 7.0);
    layout_border_rule_border_style(&div_rule, "border-left-style",
                                    ROBO_CSS_BORDER_STYLE_DOUBLE);
    layout_border_rule_color(&div_rule, "border-left-color", robo_css_color_make(9, 8, 7, 255));
    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_border_begin(&fixture, "<html><body><div>hello</div><span></span><i></i></body></html>",
                        rules, 3u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

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

    div_id = layout_border_box_for(&fixture, "div");
    span_id = layout_border_box_for(&fixture, "span");
    inline_id = layout_border_box_for(&fixture, "i");

    /* A placed block box: the door answers, and answers the *used* values -- one side
     * declared, one side drawn, and the style and colour that produced it. */
    ROBO_CHECK_EQ_INT(robo_layout_object_borders(fixture.tree, div_id, &edges, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(edges.left.width, 7);
    ROBO_CHECK_EQ_INT(edges.top.width, 0);
    ROBO_CHECK_EQ_INT(edges.right.width, 0);
    ROBO_CHECK_EQ_INT(edges.bottom.width, 0);
    ROBO_CHECK_EQ_INT(edges.widths_resolved, 1u);
    ROBO_CHECK_EQ_INT(edges.widths_unresolved, 0u);
    ROBO_CHECK_EQ_INT(edges.borders_drawn, 1u);
    ROBO_CHECK_EQ_INT((int)edges.left.style, (int)ROBO_CSS_BORDER_STYLE_DOUBLE);
    ROBO_CHECK_EQ_INT(edges.left.color.red, 9);
    ROBO_CHECK_EQ_INT(robo_layout_border_edges_is_valid(&edges), 1);

    /*
     * A text run is not a box at all, and a transparent inline box is one CSS applies no
     * border to -- both are *misses* rather than zeros, because a zero with a count of
     * resolved sides would tell a caller the author's border had been honoured and come to
     * nothing.
     */
    ROBO_CHECK(text_id != ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_object_borders(fixture.tree, text_id, &edges, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(edges.left.width, 0);
    ROBO_CHECK_EQ_INT(edges.widths_resolved, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_object_borders(fixture.tree, span_id, &edges, NULL),
                      ROBO_NOT_FOUND);

    /* An inline-level box in a *block* context is skipped rather than placed, so nothing has
     * sized it either. */
    ROBO_CHECK_EQ_INT(robo_layout_object_borders(fixture.tree, inline_id, &edges, NULL),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_layout_object_borders(NULL, div_id, &edges, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_borders(fixture.tree, ROBO_LAYOUT_NODE_ID_NONE, &edges,
                                                NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_borders(fixture.tree, div_id, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_borders(fixture.tree, (robo_layout_node_id_t)(count + 99u),
                                                &edges, NULL),
                      ROBO_NOT_FOUND);

    layout_border_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Re-layout, determinism and balance                                  */
/* ------------------------------------------------------------------ */

static void test_borders_relayout(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t rules[3];
    robo_layout_size_t size;
    robo_layout_point_t origin;

    layout_fixture();

    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "width", 100.0);
    layout_border_rule_px(&div_rule, "height", 10.0);
    layout_border_rule_px(&div_rule, "border-left-width", 2.0);
    layout_border_rule_style_all(&div_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_border_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 102);
    ROBO_CHECK_EQ_INT(size.height, 10);

    /*
     * The declaration changes and the pass runs again -- no invalidation flag, because
     * every door below is a function of the record it is handed and every record on a box is
     * rewritten by a pass. What a *re-layout* means here is that the second answer is a fact
     * about the second style and not a leftover of the first, which is why the geometry is
     * asserted *and* the record it came from.
     */
    layout_border_rule_px(&div_rule, "border-left-width", 30.0);
    layout_border_rule_px(&div_rule, "border-bottom-width", 4.0);
    layout_border_style_replace(&fixture.source, "div", &div_rule);
    layout_border_rebuild(&fixture);

    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);

    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 130);
    ROBO_CHECK_EQ_INT(size.height, 14);
    /* And the origin is still the box's own, because a border never moved it. */
    origin = layout_border_origin_of(&fixture, layout_border_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(origin.y, 0);
    {
        robo_layout_border_edges_t edges =
            layout_border_edges_of(&fixture, layout_border_box_for(&fixture, "div"));

        ROBO_CHECK_EQ_INT(edges.left.width, 30);
        ROBO_CHECK_EQ_INT(edges.bottom.width, 4);
        ROBO_CHECK_EQ_INT(edges.top.width, 0);
    }

    /*
     * A **style-only** change moves no geometry and is still visible: the same five becomes
     * `none`, the box returns to its declared 100, and the record says four declarations
     * arrived and none draws. This is the pair that makes the two counters earn their keep.
     */
    layout_border_rule_style_all(&div_rule, ROBO_CSS_BORDER_STYLE_NONE);
    layout_border_style_replace(&fixture.source, "div", &div_rule);
    layout_border_rebuild(&fixture);

    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
    size = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, 100);
    ROBO_CHECK_EQ_INT(size.height, 10);
    {
        robo_layout_border_edges_t edges =
            layout_border_edges_of(&fixture, layout_border_box_for(&fixture, "div"));

        /* Two widths still reached the layer and neither of them draws, which is the pair a
         * single counter could not report: the declared 30 and 4 are facts about a *style*,
         * and the used zero is the conclusion the fourth resolve rule draws from it. */
        ROBO_CHECK_EQ_INT(edges.widths_resolved, 2u);
        ROBO_CHECK_EQ_INT(edges.borders_drawn, 0u);
        ROBO_CHECK_EQ_INT(edges.left.width, 0);
        ROBO_CHECK_EQ_INT(edges.bottom.width, 0);
        ROBO_CHECK_EQ_INT((int)edges.left.style, (int)ROBO_CSS_BORDER_STYLE_NONE);
    }

    layout_border_end(&fixture);
}

static void test_borders_determinism(void)
{
    layout_border_fixture_t fixture;
    layout_border_rule_t html_rule;
    layout_border_rule_t body_rule;
    layout_border_rule_t div_rule;
    layout_border_rule_t rules[3];
    robo_layout_size_t first;
    robo_layout_size_t second;

    layout_fixture();

    layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_border_rule_px(&div_rule, "width", 120.0);
    layout_border_rule_px(&div_rule, "height", 40.0);
    layout_border_rule_px(&div_rule, "border-top-width", 1.0);
    layout_border_rule_px(&div_rule, "border-right-width", 2.0);
    layout_border_rule_px(&div_rule, "border-bottom-width", 3.0);
    layout_border_rule_px(&div_rule, "border-left-width", 4.0);
    layout_border_rule_style_all(&div_rule, ROBO_CSS_BORDER_STYLE_SOLID);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_border_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
    first = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "div"));

    /* Identical input, identical output -- twice, through the same context-free doors. */
    layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
    second = layout_border_size_of(&fixture, layout_border_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(first.width, second.width);
    ROBO_CHECK_EQ_INT(first.height, second.height);
    ROBO_CHECK_EQ_INT(first.width, 120 + 4 + 2);
    ROBO_CHECK_EQ_INT(first.height, 40 + 1 + 3);

    layout_border_end(&fixture);
}

static void test_borders_allocator_balance(void)
{
    size_t before = layout_live_blocks();
    size_t settled;
    int round;

    layout_fixture();

    for (round = 0; round < 3; round++) {
        layout_border_fixture_t fixture;
        layout_border_rule_t html_rule;
        layout_border_rule_t body_rule;
        layout_border_rule_t div_rule;
        layout_border_rule_t rules[3];

        layout_border_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
        layout_border_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
        layout_border_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_border_rule_px(&div_rule, "width", 250.0);
        layout_border_rule_px(&div_rule, "height", 20.0);
        layout_border_rule_shorthand(&div_rule, 12.0);
        rules[0] = html_rule;
        rules[1] = body_rule;
        rules[2] = div_rule;

        layout_border_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
        layout_border_pass(&fixture, LAYOUT_BORDER_VIEWPORT_WIDTH, NULL);
        settled = layout_live_blocks();
        /* The border layer allocates nothing of its own: the records are values on the
         * stack and on the object, and the pass adds no arena. */
        ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
        layout_border_pass(&fixture, 400, NULL);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
        layout_border_end(&fixture);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_layout_border(void);

void robo_test_register_layout_border(void)
{
    robo_test_register("layout.borders.kind_vocabulary", test_borders_kind_vocabulary);
    robo_test_register("layout.borders.width_record", test_borders_width_record);
    robo_test_register("layout.borders.side_record", test_borders_side_record);
    robo_test_register("layout.borders.seam", test_borders_seam);
    robo_test_register("layout.borders.edges_record", test_borders_edges_record);
    robo_test_register("layout.borders.derived_boxes", test_borders_derived_boxes);
    robo_test_register("layout.borders.block_declared_width", test_borders_block_declared_width);
    robo_test_register("layout.borders.block_auto_width", test_borders_block_auto_width);
    robo_test_register("layout.borders.block_sides", test_borders_block_sides);
    robo_test_register("layout.borders.block_vertical", test_borders_block_vertical);
    robo_test_register("layout.borders.block_stacking", test_borders_block_stacking);
    robo_test_register("layout.borders.block_unresolved", test_borders_block_unresolved);
    robo_test_register("layout.borders.none_style", test_borders_none_style);
    robo_test_register("layout.borders.margin_interaction", test_borders_margin_interaction);
    robo_test_register("layout.borders.padding_interaction", test_borders_padding_interaction);
    robo_test_register("layout.borders.children_get_content_width",
                       test_borders_children_get_content_width);
    robo_test_register("layout.borders.inline_atomic", test_borders_inline_atomic);
    robo_test_register("layout.borders.inline_transparent_not_applied",
                       test_borders_inline_transparent_not_applied);
    robo_test_register("layout.borders.object_door", test_borders_object_door);
    robo_test_register("layout.borders.relayout", test_borders_relayout);
    robo_test_register("layout.borders.determinism", test_borders_determinism);
    robo_test_register("layout.borders.allocator_balance", test_borders_allocator_balance);
}
