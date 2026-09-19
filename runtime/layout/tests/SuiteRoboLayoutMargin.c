/*
 * RoboLayout margin suite (Phase 10.9) — layout.margins.*
 *
 * Four things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary** -- three kinds, four sides, and the predicates and names over
 *   them -- which is pure;
 *
 *   the **record**: what a style declared and what the four sides *came to*, which
 *   are two different numbers and the reason this phase exists;
 *
 *   the two **policies**: the block arithmetic (distribution of the leftover beside a
 *   declared width) and the inline one (every `auto` is zero), driven directly, so
 *   that a wrong distribution fails at the arithmetic rather than three boxes later in
 *   a coordinate;
 *
 *   the **pass**: margins reaching geometry -- a box's origin, the width its own `auto`
 *   fills, the extent its parent stacks by, and an atomic inline's advance along a
 *   line.
 *
 * The scripts here are *computed styles*, built through 9.8's makers exactly as the
 * sizing suite builds them, because a fabricated struct would prove the pass reads a
 * struct rather than a computed CSS value. Where a case needs a declaration the CSS
 * property table does not know -- a negative margin is known, a percentage is known,
 * but a `var()`-shaped unresolved length is not -- the value is flagged the way the
 * computed layer flags one and the classification is what is under test.
 *
 * Elements are resolved **by tag** (`layout_margin_box_for`) rather than by arena
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
typedef enum layout_margin_decl_kind {
    LAYOUT_MARGIN_DECL_PX = 0,
    /** A px length flagged unresolved, which is what a `var()` arrives as. */
    LAYOUT_MARGIN_DECL_UNRESOLVED_PX = 1,
    LAYOUT_MARGIN_DECL_PERCENTAGE = 2,
    LAYOUT_MARGIN_DECL_EM = 3,
    LAYOUT_MARGIN_DECL_KEYWORD = 4
} layout_margin_decl_kind_t;

typedef struct layout_margin_decl {
    const char *name;
    layout_margin_decl_kind_t kind;
    double number;
    const char *keyword;
} layout_margin_decl_t;

#define LAYOUT_MARGIN_MAX_DECLARATIONS 10u
#define LAYOUT_MARGIN_MAX_RULES 8u

typedef struct layout_margin_rule {
    const char *tag;
    robo_css_display_t display;
    layout_margin_decl_t declarations[LAYOUT_MARGIN_MAX_DECLARATIONS];
    size_t declarations_count;
} layout_margin_rule_t;

static void layout_margin_rule_init(layout_margin_rule_t *rule,
                                    const char *tag,
                                    robo_css_display_t display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static void layout_margin_rule_add(layout_margin_rule_t *rule,
                                   const char *name,
                                   layout_margin_decl_kind_t kind,
                                   double number,
                                   const char *keyword)
{
    ROBO_CHECK_EQ_INT(rule->declarations_count < LAYOUT_MARGIN_MAX_DECLARATIONS, 1);
    rule->declarations[rule->declarations_count].name = name;
    rule->declarations[rule->declarations_count].kind = kind;
    rule->declarations[rule->declarations_count].number = number;
    rule->declarations[rule->declarations_count].keyword = keyword;
    rule->declarations_count++;
}

static void layout_margin_rule_px(layout_margin_rule_t *rule, const char *name, double px)
{
    layout_margin_rule_add(rule, name, LAYOUT_MARGIN_DECL_PX, px, NULL);
}

static void layout_margin_rule_auto(layout_margin_rule_t *rule, const char *name)
{
    layout_margin_rule_add(rule, name, LAYOUT_MARGIN_DECL_KEYWORD, 0.0, "auto");
}

static void layout_margin_rule_percentage(layout_margin_rule_t *rule,
                                          const char *name,
                                          double percent)
{
    layout_margin_rule_add(rule, name, LAYOUT_MARGIN_DECL_PERCENTAGE, percent, NULL);
}

static void layout_margin_rule_em(layout_margin_rule_t *rule, const char *name, double em)
{
    layout_margin_rule_add(rule, name, LAYOUT_MARGIN_DECL_EM, em, NULL);
}

static void layout_margin_rule_unresolved(layout_margin_rule_t *rule,
                                          const char *name,
                                          double px)
{
    layout_margin_rule_add(rule, name, LAYOUT_MARGIN_DECL_UNRESOLVED_PX, px, NULL);
}

typedef struct layout_margin_style {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
    size_t answers;
} layout_margin_style_t;

typedef struct layout_margin_source {
    layout_margin_style_t rules[LAYOUT_MARGIN_MAX_RULES];
    size_t count;
} layout_margin_source_t;

/**
 * Build the style @p rule describes: a real DISPLAY value, plus whatever the rule
 * declares, all through 9.8's own makers and container.
 *
 * `display` is set even when the rule declares nothing else, because 9.12's initial
 * value is `inline` and a fixture that wanted a block box would otherwise be asserting
 * a document made of inline boxes.
 */
static robo_css_computed_style_t *layout_margin_style_build(const layout_margin_rule_t *rule)
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
        const layout_margin_decl_t *declaration = &rule->declarations[index];

        value = (robo_css_computed_value_t)ROBO_CSS_CLEARED;
        specified = (robo_css_value_t)ROBO_CSS_CLEARED;
        switch (declaration->kind) {
        case LAYOUT_MARGIN_DECL_PERCENTAGE:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_percentage(&value,
                                                                     declaration->number, 0,
                                                                     NULL),
                              ROBO_OK);
            break;
        case LAYOUT_MARGIN_DECL_KEYWORD:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                                  &value, robo_css_slice_from_text(declaration->keyword), NULL),
                              ROBO_OK);
            break;
        case LAYOUT_MARGIN_DECL_UNRESOLVED_PX:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(
                                  &value, declaration->number, ROBO_CSS_UNIT_PX, 0, NULL),
                              ROBO_OK);
            /* The flag the computed layer sets for a `var()` or a `calc()`: carried
             * intact and not a value *yet*, whatever its kind says. */
            value.unresolved = 1;
            break;
        case LAYOUT_MARGIN_DECL_EM:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(
                                  &value, declaration->number, ROBO_CSS_UNIT_EM, 0, NULL),
                              ROBO_OK);
            break;
        case LAYOUT_MARGIN_DECL_PX:
        default:
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
static robo_css_computed_style_t *layout_margin_style_add(layout_margin_source_t *source,
                                                         const layout_margin_rule_t *rule)
{
    size_t slot;
    robo_css_computed_style_t *style;

    layout_fixture();
    ROBO_CHECK_EQ_INT(source->count < LAYOUT_MARGIN_MAX_RULES, 1);
    slot = source->count;
    style = layout_margin_style_build(rule);
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
 * *first* row whose tag matches, so an appended second `div` row would be shadowed by the
 * one it was meant to replace and the case would assert the old declaration while
 * appearing to have changed it.
 */
static void layout_margin_style_replace(layout_margin_source_t *source,
                                       const char *tag,
                                       const layout_margin_rule_t *rule)
{
    size_t index;

    layout_fixture();
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            robo_css_computed_style_destroy(source->rules[index].style);
            source->rules[index].style = layout_margin_style_build(rule);
            return;
        }
    }
    ROBO_CHECK_MSG(0, "the tag to replace is not in this source");
}

static const robo_css_computed_style_t *layout_margin_style_lookup(
    void *userdata,
    const robo_html_node_handle_t *element)
{
    layout_margin_source_t *source = (layout_margin_source_t *)userdata;
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
    /* An element nothing was declared for: `display: inline`, no declared margins, and
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
 */
static int32_t layout_margin_entry_px(const robo_css_computed_style_t *style,
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

typedef struct layout_margin_fixture {
    layout_document_t doc;
    layout_margin_source_t source;
    robo_layout_style_source_t styles;
    robo_layout_tree_t *tree;
} layout_margin_fixture_t;

static void layout_margin_begin(layout_margin_fixture_t *fixture,
                                const char *html,
                                const layout_margin_rule_t *rules,
                                size_t rule_count)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    for (index = 0u; index < rule_count; index++) {
        (void)layout_margin_style_add(&fixture->source, &rules[index]);
    }
    robo_layout_style_source_init(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.computed_style_for = layout_margin_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_margin_end(layout_margin_fixture_t *fixture)
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

#define LAYOUT_MARGIN_VIEWPORT_WIDTH 800
#define LAYOUT_MARGIN_VIEWPORT_HEIGHT 600

/** One pass at @p viewport_width. */
static void layout_margin_pass(layout_margin_fixture_t *fixture,
                               int32_t viewport_width,
                               robo_layout_stats_t *out_stats)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;

    viewport.width = viewport_width;
    viewport.height = LAYOUT_MARGIN_VIEWPORT_HEIGHT;
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
static robo_layout_node_id_t layout_margin_box_for(layout_margin_fixture_t *fixture,
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
static robo_layout_point_t layout_margin_origin_of(layout_margin_fixture_t *fixture,
                                                   robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_point_t position;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
    return position;
}

/** The size a placed box was given. */
static robo_layout_size_t layout_margin_size_of(layout_margin_fixture_t *fixture,
                                                robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_size_t size;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
    return size;
}

/** The used margins a placed box reports. */
static robo_layout_margin_edges_t layout_margin_edges_of(layout_margin_fixture_t *fixture,
                                                        robo_layout_node_id_t id)
{
    robo_layout_margin_edges_t edges;

    ROBO_CHECK_EQ_INT(robo_layout_object_margins(fixture->tree, id, &edges, NULL), ROBO_OK);
    return edges;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void test_margins_kind_vocabulary(void)
{
    robo_layout_margin_kind_t kinds[ROBO_LAYOUT_MARGIN_KIND_COUNT];
    size_t index;
    size_t seen[ROBO_LAYOUT_MARGIN_KIND_COUNT];

    layout_fixture();

    kinds[0] = ROBO_LAYOUT_MARGIN_ABSENT;
    kinds[1] = ROBO_LAYOUT_MARGIN_AUTO;
    kinds[2] = ROBO_LAYOUT_MARGIN_RESOLVED;
    kinds[3] = ROBO_LAYOUT_MARGIN_UNRESOLVED;
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_MARGIN_KIND_COUNT, 4u);

    memset(seen, 0, sizeof(seen));
    for (index = 0u; index < ROBO_LAYOUT_MARGIN_KIND_COUNT; index++) {
        robo_layout_margin_kind_t kind = kinds[index];

        /* The kinds are walked and each is asserted to be inside the count, so a fourth
         * enumerator that nobody gave a name to would fail here rather than at a
         * stranger's switch statement. */
        ROBO_CHECK_EQ_INT(robo_layout_margin_kind_is_known(kind), 1);
        ROBO_CHECK((size_t)kind < ROBO_LAYOUT_MARGIN_KIND_COUNT);
        seen[(size_t)kind]++;
        ROBO_CHECK(robo_layout_margin_kind_name(kind) != NULL);
    }
    for (index = 0u; index < ROBO_LAYOUT_MARGIN_KIND_COUNT; index++) {
        /* Every index named exactly once: the vocabulary is a partition of its range
         * rather than a list with a hole in it. */
        ROBO_CHECK_EQ_INT(seen[index], 1u);
    }

    ROBO_CHECK_STR_EQ(robo_layout_margin_kind_name(ROBO_LAYOUT_MARGIN_ABSENT), "absent");
    ROBO_CHECK_STR_EQ(robo_layout_margin_kind_name(ROBO_LAYOUT_MARGIN_AUTO), "auto");
    ROBO_CHECK_STR_EQ(robo_layout_margin_kind_name(ROBO_LAYOUT_MARGIN_RESOLVED), "resolved");
    ROBO_CHECK_STR_EQ(robo_layout_margin_kind_name(ROBO_LAYOUT_MARGIN_UNRESOLVED),
                      "unresolved");
    ROBO_CHECK_STR_EQ(robo_layout_margin_kind_name((robo_layout_margin_kind_t)7), "unknown");
    ROBO_CHECK_EQ_INT(robo_layout_margin_kind_is_known((robo_layout_margin_kind_t)4), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_kind_is_known((robo_layout_margin_kind_t)99), 0);
}

static void test_margins_one_side(void)
{
    robo_layout_margin_t zero = robo_layout_margin_zero();
    robo_layout_margin_t other_zero;
    robo_layout_margin_t resolved = robo_layout_margin_resolved(10);
    robo_layout_margin_t other_resolved = robo_layout_margin_resolved(11);
    robo_layout_margin_t negative = robo_layout_margin_resolved(-10);
    robo_layout_margin_t auto_margin = robo_layout_margin_auto();
    robo_layout_margin_t other_auto;
    robo_layout_margin_t unresolved = robo_layout_margin_unresolved();
    robo_layout_margin_t cleared;
    robo_layout_margin_t other_absent;
    robo_layout_margin_t *nothing = NULL;
    robo_gfx_pixel_x_t length = -1;

    layout_fixture();

    /*
     * `ABSENT` is the zero of the kind vocabulary, so a record a caller merely cleared
     * says "nobody declared this side" rather than meaning `auto`. That is the same
     * decision RoboCSS makes for `position: static`, and it is why a forgotten
     * initialisation cannot turn a box into a centred one.
     *
     * It is *not* the same record as `robo_layout_margin_zero()`, and the two are one
     * used value and two facts: `margin: 0` is a declaration an author wrote, an absent
     * side is one nobody did, and only the counters can tell them apart. Both are zeros
     * in geometry, and a cleared record's `length` is 0 for the same reason a C struct's
     * is -- which is why reading one is a *miss* rather than a zero.
     */
    memset(&cleared, 0, sizeof(cleared));
    other_absent = robo_layout_margin_absent();
    other_zero = robo_layout_margin_zero();
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_absent(&cleared), 1);
    ROBO_CHECK_EQ_INT(cleared.length, 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_valid(&cleared), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_resolved(&cleared), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_equal(&cleared, &other_absent), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margin_equal(&cleared, &zero), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_equal(&zero, &other_zero), 1);
    length = -1;
    ROBO_CHECK_EQ_INT(robo_layout_margin_length(&cleared, &length), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(length, -1);

    ROBO_CHECK_EQ_INT(robo_layout_margin_is_auto(&zero), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_resolved(&zero), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_unresolved(&zero), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_length(&zero, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 0);

    /* The distinction the phase exists to keep: `auto` distributes and `0` does not, so
     * one of them is a number and the other is a question. */
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_auto(&auto_margin), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_resolved(&auto_margin), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_equal(&auto_margin, &zero), 0);
    other_auto = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margin_equal(&auto_margin, &other_auto), 1);

    /* A *negative* length is a value, which is the one respect this vocabulary differs
     * from a box dimension's. */
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_resolved(&negative), 1);
    ROBO_CHECK_EQ_INT(negative.length, -10);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_valid(&negative), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margin_length(&negative, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, -10);

    ROBO_CHECK_EQ_INT(robo_layout_margin_is_unresolved(&unresolved), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margin_equal(&unresolved, &zero), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_equal(&resolved, &other_resolved), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_equal(&resolved, &negative), 0);

    /* A length read from a kind that carries none is a *miss* rather than the zero the
     * field happens to hold -- the same door 10.8's dimension has, for the same reason. */
    length = -1;
    ROBO_CHECK_EQ_INT(robo_layout_margin_length(&auto_margin, &length), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(length, -1);
    ROBO_CHECK_EQ_INT(robo_layout_margin_length(&unresolved, &length), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(length, -1);

    ROBO_CHECK_EQ_INT(robo_layout_margin_is_valid(&zero), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_valid(&auto_margin), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_valid(&unresolved), 1);
    /* An out-of-range magnitude is refused as a *value*, and an `auto` whose meaningless
     * length field is garbage is still valid -- the field is not read for that kind. */
    {
        robo_layout_margin_t huge = robo_layout_margin_resolved(0);

        huge.length = (robo_gfx_pixel_x_t)0x7FFFFFFF;
        ROBO_CHECK_EQ_INT(robo_layout_margin_is_valid(&huge), 1);
    }
    {
        robo_layout_margin_t bogus;

        bogus.kind = (robo_layout_margin_kind_t)9;
        bogus.length = 0;
        ROBO_CHECK_EQ_INT(robo_layout_margin_is_valid(&bogus), 0);
    }

    ROBO_CHECK_EQ_INT(robo_layout_margin_is_auto(nothing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_resolved(nothing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_unresolved(nothing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_absent(nothing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_valid(nothing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_length(nothing, &length), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_margin_length(&zero, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_margin_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margin_equal(&zero, NULL), 0);
}

static void test_margins_sides(void)
{
    robo_layout_margins_t margins;
    robo_layout_margins_t other;
    robo_layout_margins_t *nothing = NULL;
    robo_layout_margin_t side;

    layout_fixture();

    /* The cleared four sides are the initial state: four absent sides, whose used value
     * is CSS's initial value -- a zero nobody declared. */
    robo_layout_margins_init(&margins);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_absent(&margins.top), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_absent(&margins.left), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margins_is_valid(&margins), 1);
    ROBO_CHECK_EQ_INT(margins.top.length, 0);
    ROBO_CHECK_EQ_INT(margins.right.length, 0);
    ROBO_CHECK_EQ_INT(margins.bottom.length, 0);
    ROBO_CHECK_EQ_INT(margins.left.length, 0);

    margins.top = robo_layout_margin_resolved(1);
    margins.right = robo_layout_margin_resolved(2);
    margins.bottom = robo_layout_margin_resolved(3);
    margins.left = robo_layout_margin_resolved(-4);

    /* The four sides are addressable by 9.11's own enum, so the order is the CSS
     * shorthand's order and not a second definition of it. */
    ROBO_CHECK_EQ_INT(robo_layout_margins_side(&margins, ROBO_CSS_BOX_SIDE_TOP, &side),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(side.length, 1);
    ROBO_CHECK_EQ_INT(robo_layout_margins_side(&margins, ROBO_CSS_BOX_SIDE_RIGHT, &side),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(side.length, 2);
    ROBO_CHECK_EQ_INT(robo_layout_margins_side(&margins, ROBO_CSS_BOX_SIDE_BOTTOM, &side),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(side.length, 3);
    ROBO_CHECK_EQ_INT(robo_layout_margins_side(&margins, ROBO_CSS_BOX_SIDE_LEFT, &side),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(side.length, -4);
    ROBO_CHECK_EQ_INT(ROBO_CSS_BOX_SIDE_COUNT, 4u);

    side = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_side(&margins, (robo_css_box_side_t)4, &side),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_auto(&side), 1); /* untouched */

    other = margins;
    ROBO_CHECK_EQ_INT(robo_layout_margins_equal(&margins, &other), 1);
    other.left = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_equal(&margins, &other), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margins_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margins_equal(&margins, NULL), 0);

    ROBO_CHECK_EQ_INT(robo_layout_margins_is_valid(nothing), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margins_side(nothing, ROBO_CSS_BOX_SIDE_TOP, &side),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_margins_side(&margins, ROBO_CSS_BOX_SIDE_TOP, NULL),
                      ROBO_INVALID_ARGUMENT);
    /* `init` on NULL is a no-op rather than a crash, asserted by calling it. */
    robo_layout_margins_init(NULL);
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

static void test_margins_seam(void)
{
    layout_margin_source_t source;
    layout_margin_rule_t div_rule;
    layout_margin_rule_t span_rule;
    layout_margin_rule_t em_rule;
    layout_margin_rule_t var_rule;
    layout_margin_rule_t plain_rule;
    robo_layout_margins_t margins;
    robo_layout_margins_t initial;
    const robo_css_computed_style_t *style;

    memset(&source, 0, sizeof(source));
    layout_fixture();

    /* One style with all four kinds on it at once, because the reader is a loop over
     * four longhands and a case that read one declaration per style would not prove the
     * loop. */
    layout_margin_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&div_rule, "margin-top", -5.0);
    layout_margin_rule_auto(&div_rule, "margin-right");
    layout_margin_rule_percentage(&div_rule, "margin-bottom", 25.0);
    layout_margin_rule_px(&div_rule, "margin-left", 12.0);

    /* A relative unit is a declaration this build cannot use, and it is *not* the same
     * answer as `auto`. */
    layout_margin_rule_init(&em_rule, "em", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_em(&em_rule, "margin-left", 2.0);

    /* And the shape a `var()` arrives in. */
    layout_margin_rule_init(&var_rule, "i", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_unresolved(&var_rule, "margin-left", 30.0);

    /* A style that declares no margin at all, and one that declares a *fractional*
     * length so the rounding rule is visible. */
    layout_margin_rule_init(&plain_rule, "p", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_init(&span_rule, "span", ROBO_CSS_DISPLAY_INLINE);
    layout_margin_rule_px(&span_rule, "margin-left", -10.5);

    style = layout_margin_style_add(&source, &div_rule);
    ROBO_CHECK(style != NULL);
    (void)layout_margin_style_add(&source, &em_rule);
    (void)layout_margin_style_add(&source, &var_rule);
    (void)layout_margin_style_add(&source, &plain_rule);
    (void)layout_margin_style_add(&source, &span_rule);

    /* div: a negative length is usable, `auto` is its own kind, a percentage is not. */
    margins = robo_layout_margins_of(style);
    ROBO_CHECK_EQ_INT((int)margins.top.kind, (int)ROBO_LAYOUT_MARGIN_RESOLVED);
    ROBO_CHECK_EQ_INT(margins.top.length, -5);
    ROBO_CHECK_EQ_INT((int)margins.right.kind, (int)ROBO_LAYOUT_MARGIN_AUTO);
    ROBO_CHECK_EQ_INT((int)margins.bottom.kind, (int)ROBO_LAYOUT_MARGIN_UNRESOLVED);
    ROBO_CHECK_EQ_INT((int)margins.left.kind, (int)ROBO_LAYOUT_MARGIN_RESOLVED);
    ROBO_CHECK_EQ_INT(margins.left.length, 12);
    ROBO_CHECK_EQ_INT(robo_layout_margins_is_valid(&margins), 1);

    /* An `em` margin: declared, unusable, and **not** silently a length. And `top`,
     * which the rule never wrote, is *absent* -- the state that a declared `margin-top:
     * 0` is not. */
    margins = robo_layout_margins_of(source.rules[1].style);
    ROBO_CHECK_EQ_INT((int)margins.left.kind, (int)ROBO_LAYOUT_MARGIN_UNRESOLVED);
    ROBO_CHECK_EQ_INT((int)margins.top.kind, (int)ROBO_LAYOUT_MARGIN_ABSENT);
    ROBO_CHECK_EQ_INT(margins.top.length, 0);

    /* And one flagged unresolved, which is a length by kind and not a value. */
    margins = robo_layout_margins_of(source.rules[2].style);
    ROBO_CHECK_EQ_INT((int)margins.left.kind, (int)ROBO_LAYOUT_MARGIN_UNRESOLVED);

    /* A style that declares no margin reads as four *absent* sides: CSS's initial value
     * is what the geometry uses for them, and the record still says nobody wrote one. */
    initial = (robo_layout_margins_t){0};
    robo_layout_margins_init(&initial);
    margins = robo_layout_margins_of(source.rules[3].style);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_absent(&margins.top), 1);
    ROBO_CHECK_EQ_INT(margins.top.length, 0);
    ROBO_CHECK_EQ_INT(robo_layout_margins_equal(&margins, &initial), 1);
    /* And a record whose sides *were* declared is not that one, because the comparison
     * reads every side's kind and not only its length -- which is what makes "did this
     * document declare any margins?" a question the record can answer. */
    margins = robo_layout_margins_of(style);
    ROBO_CHECK_EQ_INT(robo_layout_margins_equal(&margins, &initial), 0);

    /* Half away from zero, which 10.9 made true for a negative length: the door has
     * claimed it since 10.8 and nothing signed had reached it until now. */
    margins = robo_layout_margins_of(source.rules[4].style);
    ROBO_CHECK_EQ_INT(margins.left.length, -11);

    /* A NULL style is not a failure: a box with no style has made no declarations, and
     * the record a caller already initialised says so. */
    margins = robo_layout_margins_of(NULL);
    ROBO_CHECK_EQ_INT(robo_layout_margin_is_absent(&margins.top), 1);
    ROBO_CHECK_EQ_INT(margins.top.length, 0);
    ROBO_CHECK_EQ_INT(robo_layout_margins_equal(&margins, &initial), 1);
}

static void test_margins_edges_record(void)
{
    robo_layout_margin_edges_t edges;
    robo_layout_margin_edges_t other;
    int32_t outer = -1;

    layout_fixture();

    robo_layout_margin_edges_init(&edges, LAYOUT_MARGIN_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(edges.top, 0);
    ROBO_CHECK_EQ_INT(edges.right, 0);
    ROBO_CHECK_EQ_INT(edges.bottom, 0);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.available_content_width, LAYOUT_MARGIN_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(edges.resolved, 0u);
    ROBO_CHECK_EQ_INT(edges.autos_used, 0u);
    ROBO_CHECK_EQ_INT(edges.unresolved, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_is_valid(&edges), 1);

    /* A containing block of nothing is zero available rather than a negative one. */
    robo_layout_margin_edges_init(&edges, -5);
    ROBO_CHECK_EQ_INT(edges.available_content_width, 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_is_valid(&edges), 1);

    /* The two refusals: a negative availability is a size arithmetic that can produce a
     * rectangle nobody can draw, and counters that exceed four sides are five sides. */
    edges.available_content_width = -1;
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_is_valid(&edges), 0);
    edges.available_content_width = 10;
    edges.resolved = 2u;
    edges.autos_used = 2u;
    edges.unresolved = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_is_valid(&edges), 0);
    edges.autos_used = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_is_valid(&edges), 1);

    other = edges;
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_equal(&edges, &other), 1);
    /* Every field, because the record is a *conclusion*: two resolutions that agreed
     * about the numbers and disagreed about how many declarations they could not use
     * would be two different answers to one document. */
    other.unresolved = 0u;
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_equal(&edges, &other), 0);
    other = edges;
    other.left = -7;
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_equal(&edges, &other), 0);
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_equal(&edges, NULL), 0);

    /* The advance an inline participant moves the cursor by. Negative margins make it
     * smaller than the content -- which is a layout an author can ask for, so it is
     * deliberately not clamped. */
    robo_layout_margin_edges_init(&edges, LAYOUT_MARGIN_VIEWPORT_WIDTH);
    edges.left = 5;
    edges.right = 15;
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_outer_width(&edges, 100, &outer), ROBO_OK);
    ROBO_CHECK_EQ_INT(outer, 120);
    edges.left = -5;
    edges.right = -15;
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_outer_width(&edges, 100, &outer), ROBO_OK);
    ROBO_CHECK_EQ_INT(outer, 80);
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_outer_width(NULL, 100, &outer),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_outer_width(&edges, 100, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_is_valid(NULL), 0);
    robo_layout_margin_edges_init(NULL, 10); /* no-op, asserted by not crashing */
}

/* ------------------------------------------------------------------ */
/* The two policies                                                    */
/* ------------------------------------------------------------------ */

/** A four-side record with the two horizontal sides set. */
static robo_layout_margins_t layout_margin_pair(int32_t left, int32_t right)
{
    robo_layout_margins_t margins;

    robo_layout_margins_init(&margins);
    margins.left = robo_layout_margin_resolved(left);
    margins.right = robo_layout_margin_resolved(right);
    return margins;
}

static void layout_margin_expect_edges(const robo_layout_margin_edges_t *edges,
                                       int32_t top,
                                       int32_t right,
                                       int32_t bottom,
                                       int32_t left,
                                       int32_t available,
                                       size_t resolved,
                                       size_t autos,
                                       size_t unresolved)
{
    ROBO_CHECK_EQ_INT(edges->top, top);
    ROBO_CHECK_EQ_INT(edges->right, right);
    ROBO_CHECK_EQ_INT(edges->bottom, bottom);
    ROBO_CHECK_EQ_INT(edges->left, left);
    ROBO_CHECK_EQ_INT(edges->available_content_width, available);
    ROBO_CHECK_EQ_INT(edges->resolved, resolved);
    ROBO_CHECK_EQ_INT(edges->autos_used, autos);
    ROBO_CHECK_EQ_INT(edges->unresolved, unresolved);
    ROBO_CHECK_EQ_INT(robo_layout_margin_edges_is_valid(edges), 1);
}

static void test_margins_block_basic(void)
{
    robo_layout_margins_t margins;
    robo_layout_margin_edges_t edges;
    robo_layout_dimension_t auto_width = robo_layout_dimension_auto();

    layout_fixture();

    robo_layout_margins_init(&margins);
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &auto_width, &edges,
                                                       NULL),
                      ROBO_OK);
    /* No declaration anywhere, so nothing was applied and nothing was left over. */
    layout_margin_expect_edges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);

    /* A `width: 0` box and a box with no width are the same document as far as the
     * margins are concerned -- the *distribution* is what a settled width changes. */
    {
        robo_layout_dimension_t zero_width = robo_layout_dimension_definite(0);

        ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &zero_width,
                                                           &edges, NULL),
                          ROBO_OK);
        layout_margin_expect_edges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);
    }

    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(NULL, 800, &auto_width, &edges,
                                                       NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &auto_width, NULL,
                                                       NULL),
                      ROBO_INVALID_ARGUMENT);
    /* A NULL dimension is not a failure: "this box declares no width" and "the caller
     * has no dimension to show" both mean nothing to distribute. */
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, NULL, &edges, NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);
}

static void test_margins_block_sides(void)
{
    robo_layout_margins_t margins = layout_margin_pair(10, 20);
    robo_layout_margin_edges_t edges;
    robo_layout_dimension_t auto_width = robo_layout_dimension_auto();

    layout_fixture();

    margins.top = robo_layout_margin_resolved(3);
    margins.bottom = robo_layout_margin_resolved(4);

    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &auto_width, &edges,
                                                       NULL),
                      ROBO_OK);
    /* Used as written, every one of the four, and the containing block's contents left
     * over after the two horizontal ones. */
    layout_margin_expect_edges(&edges, 3, 20, 4, 10, 770, 4u, 0u, 0u);

    /* The vertical sides do **not** come out of the available width: a box's height is
     * not a number its containing block offers it in this direction. */
    ROBO_CHECK_EQ_INT(edges.available_content_width, 800 - 10 - 20);
}

static void test_margins_block_negative(void)
{
    robo_layout_margins_t margins = layout_margin_pair(-10, -20);
    robo_layout_margin_edges_t edges;
    robo_layout_dimension_t auto_width = robo_layout_dimension_auto();

    layout_fixture();
    margins.top = robo_layout_margin_resolved(-5);
    margins.bottom = robo_layout_margin_resolved(-6);

    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &auto_width, &edges,
                                                       NULL),
                      ROBO_OK);
    /*
     * A negative margin is honoured in the edges themselves -- the box then overhangs
     * its containing block -- while the width left for its *contents* grows, because
     * taking away a negative leaves more. Nothing is clamped on the way in; the one
     * clamp is on the availability, and it is a floor rather than a bound.
     */
    layout_margin_expect_edges(&edges, -5, -20, -6, -10, 830, 4u, 0u, 0u);

    /* A large negative pair grows the availability rather than being refused: nothing is
     * clamped on the way in, and there is no upper bound a containing block has to obey. */
    margins = layout_margin_pair(-900, -900);
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &auto_width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, -900, 0, -900, 2600, 2u, 0u, 0u);

    /* And it is a *floor* rather than a bound: a pair of margins wider than the
     * containing block leaves zero for the contents rather than a negative width. */
    margins = layout_margin_pair(900, 900);
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &auto_width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 900, 0, 900, 0, 2u, 0u, 0u);
}

static void test_margins_block_auto_center(void)
{
    robo_layout_margins_t margins;
    robo_layout_margin_edges_t edges;
    robo_layout_dimension_t width = robo_layout_dimension_definite(200);

    layout_fixture();

    /* `margin: 0 auto` with a declared width: the two autos split the leftover, which is
     * what centring a block in CSS *is*. */
    margins = layout_margin_pair(0, 0);
    margins.left = robo_layout_margin_auto();
    margins.right = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 300, 0, 300, 200, 0u, 2u, 0u);

    /* One `auto` takes all of it. */
    margins = layout_margin_pair(0, 0);
    margins.left = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 0, 0, 600, 200, 1u, 1u, 0u);

    margins = layout_margin_pair(0, 0);
    margins.right = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 600, 0, 0, 200, 1u, 1u, 0u);

    /* And a declared side is *added* to rather than replaced: `margin-left: 100px` with
     * `margin-right: auto` gives the right side 500. */
    margins = layout_margin_pair(100, 0);
    margins.right = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 500, 0, 100, 200, 1u, 1u, 0u);
}

static void test_margins_block_auto_odd(void)
{
    robo_layout_margins_t margins;
    robo_layout_margin_edges_t edges;
    robo_layout_dimension_t width = robo_layout_dimension_definite(199);

    layout_fixture();

    /*
     * The odd pixel goes **right**, so a centred box in an odd leftover sits one pixel
     * left of centre. CSS says the two used values are equal, which integers cannot
     * always do; this case is where the choice is written down.
     */
    margins = layout_margin_pair(0, 0);
    margins.left = robo_layout_margin_auto();
    margins.right = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 301, 0, 300, 199, 0u, 2u, 0u);
}

static void test_margins_block_auto_under_auto_width(void)
{
    robo_layout_margins_t margins;
    robo_layout_margin_edges_t edges;
    robo_layout_dimension_t auto_width = robo_layout_dimension_auto();
    robo_layout_dimension_t percent_width;

    layout_fixture();

    percent_width = robo_layout_dimension_unresolved();

    /*
     * An `auto` **width** fills everything, so an `auto` margin beside it has nothing to
     * take: CSS says its used value is 0. This is the case that says why the block door
     * takes the *dimension* rather than a number -- the same declaration means 300 when
     * the width is settled and 0 when it is not.
     */
    margins = layout_margin_pair(0, 0);
    margins.left = robo_layout_margin_auto();
    margins.right = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &auto_width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);

    /* A width this build cannot resolve takes the same path, because the *fill* rule for
     * it is the same rule. */
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &percent_width,
                                                       &edges, NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);

    /* A declared side is still applied, and the autos are still zero: the box overflows
     * nothing, it fills what is left. */
    margins = layout_margin_pair(30, 0);
    margins.left = robo_layout_margin_resolved(30);
    margins.right = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &auto_width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 0, 0, 30, 770, 1u, 0u, 0u);
}

static void test_margins_block_overconstrained(void)
{
    robo_layout_margins_t margins;
    robo_layout_margin_edges_t edges;
    robo_layout_dimension_t width = robo_layout_dimension_definite(900);

    layout_fixture();

    /*
     * Over-constrained: the width alone is wider than the containing block, so there is
     * no leftover to distribute and both autos stay at zero. CSS resolves this by
     * adjusting `margin-right`; this build does not adjust a margin its author did not
     * leave `auto`, and the overflow is 10.13's to report.
     */
    margins = layout_margin_pair(0, 0);
    margins.left = robo_layout_margin_auto();
    margins.right = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);

    /* A leftover of exactly zero is the same case rather than a third one. */
    width = robo_layout_dimension_definite(800);
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);
}

static void test_margins_block_unresolved(void)
{
    robo_layout_margins_t margins;
    robo_layout_margin_edges_t edges;
    robo_layout_dimension_t width = robo_layout_dimension_definite(200);

    layout_fixture();

    /*
     * A percentage resolves against the containing block's **width** in CSS, for every
     * side -- and this build has a containing width, so it *could*. It does not, because
     * 10.8 left every percentage unresolved and resolving one here would make
     * `margin: 5%` the only percentage in the module that means something. The used
     * value is zero and the declaration is *counted*, so a document spaced with
     * percentages lays out flush and says so.
     */
    robo_layout_margins_init(&margins);
    margins.left = robo_layout_margin_unresolved();
    margins.bottom = robo_layout_margin_unresolved();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 2u);

    /* And an unresolved side next to a resolved one does not stop the resolved one, or
     * the distribution: the counting is orthogonal to the arithmetic. */
    margins.left = robo_layout_margin_resolved(50);
    margins.right = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 550, 0, 50, 200, 1u, 1u, 1u);
}

static void test_margins_block_vertical_auto(void)
{
    robo_layout_margins_t margins;
    robo_layout_margin_edges_t edges;
    robo_layout_dimension_t auto_width = robo_layout_dimension_auto();

    layout_fixture();

    /*
     * `margin-top: auto` distributes nothing in normal flow -- the keyword means
     * something vertically only for an absolutely positioned box, which is 10.12 -- so it
     * is zero and it is **not** counted as used. A caller that saw `autos_used == 2` for
     * this document would be told a centring happened.
     */
    robo_layout_margins_init(&margins);
    margins.top = robo_layout_margin_auto();
    margins.bottom = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_block(&margins, 800, &auto_width, &edges,
                                                       NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);
}

static void test_margins_inline_policy(void)
{
    robo_layout_margins_t margins;
    robo_layout_margin_edges_t edges;

    layout_fixture();

    /*
     * The same declarations the block policy centres with, and every `auto` is zero:
     * distribution is a block-level normal-flow rule, and an inline-level participant has
     * no width constraint to be under- or over-. This is why there are two doors rather
     * than one with a flag -- a caller that reached for the block one would centre an
     * `inline-block` inside its line, which is not a thing CSS does.
     */
    margins = layout_margin_pair(0, 0);
    margins.left = robo_layout_margin_auto();
    margins.right = robo_layout_margin_auto();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_inline(&margins, 800, &edges, NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 0u);

    /* Declared sides *are* used, and they come out of what an `auto` width may fill: an
     * `inline-block` with a 5px left and a 15px right margin has 780 to fill. */
    margins = layout_margin_pair(5, 15);
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_inline(&margins, 800, &edges, NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 15, 0, 5, 780, 2u, 0u, 0u);

    /* Negative margins grow it, and the floor still applies. */
    margins = layout_margin_pair(-5, -15);
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_inline(&margins, 800, &edges, NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, -15, 0, -5, 820, 2u, 0u, 0u);

    margins = layout_margin_pair(900, 900);
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_inline(&margins, 800, &edges, NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 900, 0, 900, 0, 2u, 0u, 0u);

    /* A percentage is unusable here for the same reason it is there. */
    margins.left = robo_layout_margin_unresolved();
    margins.right = robo_layout_margin_unresolved();
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_inline(&margins, 800, &edges, NULL),
                      ROBO_OK);
    layout_margin_expect_edges(&edges, 0, 0, 0, 0, 800, 0u, 0u, 2u);

    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_inline(NULL, 800, &edges, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_margins_resolve_inline(&margins, 800, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

static void test_margins_block_geometry(void)
{
    layout_margin_fixture_t fixture;
    layout_margin_rule_t html_rule;
    layout_margin_rule_t body_rule;
    layout_margin_rule_t div_rule;
    layout_margin_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_point_t origin;
    robo_layout_size_t size;
    robo_layout_margin_edges_t edges;

    layout_fixture();

    layout_margin_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&div_rule, "width", 300.0);
    layout_margin_rule_px(&div_rule, "height", 40.0);
    layout_margin_rule_px(&div_rule, "margin-top", 10.0);
    layout_margin_rule_px(&div_rule, "margin-right", 20.0);
    layout_margin_rule_px(&div_rule, "margin-bottom", 30.0);
    layout_margin_rule_px(&div_rule, "margin-left", 40.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_margin_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_margin_pass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);

    /*
     * The box is placed at its **margin origin** while its size stays its content box:
     * `origin.x` is the containing block's content origin, and the box starts 40px and
     * 10px further in. A build that added the margins to the size would fail both halves
     * of this pair.
     */
    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "div"));
    size = layout_margin_size_of(&fixture, layout_margin_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 40);
    ROBO_CHECK_EQ_INT(origin.y, 10);
    ROBO_CHECK_EQ_INT(size.width, 300);
    ROBO_CHECK_EQ_INT(size.height, 40);

    /* And the used margins are readable from the box, which is what a paint phase needs
     * and what the result cannot carry. */
    edges = layout_margin_edges_of(&fixture, layout_margin_box_for(&fixture, "div"));
    layout_margin_expect_edges(&edges, 10, 20, 30, 40, 800 - 60, 4u, 0u, 0u);

    /* The parent's height is the child's **margin box**: 10 + 40 + 30. */
    size = layout_margin_size_of(&fixture, layout_margin_box_for(&fixture, "body"));
    ROBO_CHECK_EQ_INT(size.height, 80);
    size = layout_margin_size_of(&fixture, layout_margin_box_for(&fixture, "html"));
    ROBO_CHECK_EQ_INT(size.height, 80);

    ROBO_CHECK_EQ_INT(stats.block.margins_applied, 4u);
    ROBO_CHECK_EQ_INT(stats.block.margins_auto_used, 0u);
    ROBO_CHECK_EQ_INT(stats.block.margins_unresolved, 0u);

    layout_margin_end(&fixture);
}

static void test_margins_auto_center_geometry(void)
{
    layout_margin_fixture_t fixture;
    layout_margin_rule_t body_rule;
    layout_margin_rule_t div_rule;
    layout_margin_rule_t p_rule;
    layout_margin_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_point_t origin;
    robo_layout_size_t size;

    layout_fixture();

    layout_margin_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&div_rule, "width", 300.0);
    layout_margin_rule_px(&div_rule, "height", 20.0);
    layout_margin_rule_auto(&div_rule, "margin-left");
    layout_margin_rule_auto(&div_rule, "margin-right");
    layout_margin_rule_init(&p_rule, "p", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&p_rule, "height", 5.0);
    rules[0] = body_rule;
    rules[1] = div_rule;
    rules[2] = p_rule;

    layout_margin_begin(&fixture, "<html><body><div><p></p></div></body></html>", rules,
                       3u);
    layout_margin_pass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);

    /* Centred: (800 - 300) / 2. */
    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 250);
    ROBO_CHECK_EQ_INT(origin.y, 0);

    /*
     * And the *child* resolves against the card's 300 rather than the viewport's 800,
     * which is the half of the claim a build that centred the card and then handed its
     * children the viewport would pass by accident.
     */
    size = layout_margin_size_of(&fixture, layout_margin_box_for(&fixture, "p"));
    ROBO_CHECK_EQ_INT(size.width, 300);
    ROBO_CHECK_EQ_INT(stats.block.margins_auto_used, 2u);
    ROBO_CHECK_EQ_INT(stats.block.margins_applied, 0u);

    layout_margin_end(&fixture);
}

static void test_margins_margin_box_flows_down(void)
{
    layout_margin_fixture_t fixture;
    layout_margin_rule_t body_rule;
    layout_margin_rule_t div_rule;
    layout_margin_rule_t p_rule;
    layout_margin_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_point_t origin;
    robo_layout_size_t size;

    layout_fixture();

    /*
     * A parent with horizontal margins and an `auto` width: the parent is 100 in from the
     * viewport and 600 wide, and its child -- which declares nothing -- is 600 wide at
     * the parent's own content origin. Two levels of the same rule, asserted together
     * because a build that reduced the available width for one level and not the other
     * would pass either half alone.
     */
    layout_margin_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&body_rule, "margin-left", 100.0);
    layout_margin_rule_px(&body_rule, "margin-right", 100.0);
    layout_margin_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&div_rule, "height", 10.0);
    layout_margin_rule_init(&p_rule, "p", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&p_rule, "height", 5.0);
    rules[0] = body_rule;
    rules[1] = div_rule;
    rules[2] = p_rule;

    layout_margin_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_margin_pass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);

    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "body"));
    size = layout_margin_size_of(&fixture, layout_margin_box_for(&fixture, "body"));
    ROBO_CHECK_EQ_INT(origin.x, 100);
    ROBO_CHECK_EQ_INT(size.width, 600);

    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "div"));
    size = layout_margin_size_of(&fixture, layout_margin_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(size.width, 600);

    layout_margin_end(&fixture);
}

static void test_margins_block_stacking(void)
{
    layout_margin_fixture_t fixture;
    layout_margin_rule_t body_rule;
    layout_margin_rule_t p_rule;
    layout_margin_rule_t h1_rule;
    layout_margin_rule_t ul_rule;
    layout_margin_rule_t rules[4];
    robo_layout_stats_t stats;
    robo_layout_point_t origin;
    robo_layout_size_t size;

    layout_fixture();

    /*
     * Three siblings with distinct heights **and distinct tags**, because the scripted
     * source answers per tag: a fixture keyed off a class would be exercising the
     * selector matcher, and this case is about the cursor.
     */
    layout_margin_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_init(&p_rule, "p", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&p_rule, "height", 100.0);
    layout_margin_rule_px(&p_rule, "margin-top", 10.0);
    layout_margin_rule_px(&p_rule, "margin-bottom", 20.0);
    layout_margin_rule_init(&h1_rule, "h1", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&h1_rule, "height", 50.0);
    layout_margin_rule_px(&h1_rule, "margin-top", 5.0);
    layout_margin_rule_px(&h1_rule, "margin-bottom", 15.0);
    layout_margin_rule_init(&ul_rule, "ul", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&ul_rule, "height", 75.0);
    rules[0] = body_rule;
    rules[1] = p_rule;
    rules[2] = h1_rule;
    rules[3] = ul_rule;

    layout_margin_begin(&fixture, "<html><body><p></p><h1></h1><ul></ul></body></html>",
                       rules, 4u);
    layout_margin_pass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);

    /* Each sibling below the last one's **margin box**: the cursor carries
     * `height + margin-bottom + the next one's margin-top`, with no collapsing. */
    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "p"));
    ROBO_CHECK_EQ_INT(origin.y, 10);
    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "h1"));
    ROBO_CHECK_EQ_INT(origin.y, 10 + 100 + 20 + 5);
    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "ul"));
    ROBO_CHECK_EQ_INT(origin.y, 135 + 50 + 15 + 0);

    /* `<body>`'s height is the extent the three margin boxes reached. */
    size = layout_margin_size_of(&fixture, layout_margin_box_for(&fixture, "body"));
    ROBO_CHECK_EQ_INT(size.height, 200 + 75);

    ROBO_CHECK_EQ_INT(stats.block.margins_applied, 4u); /* p and h1, two each */
    layout_margin_end(&fixture);
}

static void test_margins_block_no_collapse(void)
{
    layout_margin_fixture_t fixture;
    layout_margin_rule_t body_rule;
    layout_margin_rule_t p_rule;
    layout_margin_rule_t h1_rule;
    layout_margin_rule_t rules[3];
    robo_layout_stats_t stats;
    robo_layout_point_t origin;
    robo_layout_size_t size;

    layout_fixture();

    layout_margin_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_init(&p_rule, "p", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&p_rule, "height", 10.0);
    layout_margin_rule_px(&p_rule, "margin-bottom", 20.0);
    layout_margin_rule_init(&h1_rule, "h1", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&h1_rule, "height", 10.0);
    layout_margin_rule_px(&h1_rule, "margin-top", 30.0);
    rules[0] = body_rule;
    rules[1] = p_rule;
    rules[2] = h1_rule;

    layout_margin_begin(&fixture, "<html><body><p></p><h1></h1></body></html>", rules, 3u);
    layout_margin_pass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);

    /*
     * **The one place this phase knowingly differs from the specification.** Two adjacent
     * vertical margins add here (20 + 30 = 50) where CSS would collapse them to the
     * larger (30). This case asserts the *sum* deliberately: it is the assertion a later
     * goal changes when collapsing arrives, and the reason it is a case rather than a
     * comment is that a build which lost the margin arithmetic by collapsing early would
     * otherwise look correct in every document whose margins happened to be equal.
     */
    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "h1"));
    ROBO_CHECK_EQ_INT(origin.y, 10 + 20 + 30);

    size = layout_margin_size_of(&fixture, layout_margin_box_for(&fixture, "body"));
    ROBO_CHECK_EQ_INT(size.height, 60 + 10);

    layout_margin_end(&fixture);
}

static void test_margins_inline_advance(void)
{
    layout_margin_fixture_t fixture;
    layout_margin_rule_t html_rule;
    layout_margin_rule_t body_rule;
    layout_margin_rule_t div_rule;
    layout_margin_rule_t i_rule;
    layout_margin_rule_t b_rule;
    layout_margin_rule_t rules[5];
    robo_layout_stats_t stats;
    robo_layout_point_t origin;
    robo_layout_size_t size;
    robo_layout_line_box_t line;
    robo_layout_line_id_t line_id = ROBO_LAYOUT_LINE_ID_NONE;
    layout_margin_rule_t widthless_rule;
    layout_margin_rule_t rules2[4];
    layout_margin_fixture_t second;

    layout_fixture();

    /*
     * `html` and `body` are declared **block**, and that is a fact about this fixture
     * rather than about margins: an element nothing declared for computes to `display:
     * inline` (9.12's initial value, and the point of `layout_margin_style_lookup`
     * answering NULL), so a document that declared only the subject would put the
     * subject's ancestors in the inline flow and never reach the subject at all -- it
     * would be placed by no pass and the case would be measuring a document it did not
     * write. Every other suite here declares them for the same reason.
     */
    layout_margin_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&div_rule, "height", 30.0);
    layout_margin_rule_init(&i_rule, "i", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_margin_rule_px(&i_rule, "width", 100.0);
    layout_margin_rule_px(&i_rule, "height", 20.0);
    layout_margin_rule_px(&i_rule, "margin-left", 5.0);
    layout_margin_rule_px(&i_rule, "margin-right", 15.0);
    layout_margin_rule_init(&b_rule, "b", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_margin_rule_px(&b_rule, "width", 50.0);
    layout_margin_rule_px(&b_rule, "height", 10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = i_rule;
    rules[4] = b_rule;

    layout_margin_begin(&fixture, "<html><body><div><i></i><b></b></div></body></html>",
                       rules, 5u);
    layout_margin_pass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);

    /*
     * The advance is the **margin box**: the first atomic is placed 5px in from the
     * cursor, is 100 wide, and pushes the next one 15px further on. So the second starts
     * at 120 -- and a build that advanced by the content box alone would start it at 100.
     */
    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "i"));
    size = layout_margin_size_of(&fixture, layout_margin_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(origin.x, 5);
    ROBO_CHECK_EQ_INT(size.width, 100);
    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "b"));
    ROBO_CHECK_EQ_INT(origin.x, 120);

    /* The line's accounted width is the sum of the advances, and the two members are on
     * one line because they fit. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture.tree, 0u, &line_id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, line_id, &line, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(line.size.width, 120 + 50);
    ROBO_CHECK_EQ_INT(line.members, 2u);

    ROBO_CHECK_EQ_INT(stats.inline_flow.margins_applied, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.margins_unresolved, 0u);

    layout_margin_end(&fixture);

    /*
     * And an `auto` width fills what the margins left: the same participant with no
     * declared width is 780 wide (800 - 5 - 15) rather than the whole line. 10.4's
     * fallback, with the margins taken out -- which is the one number an inline
     * participant's sizing layer needs from this phase.
     */
    layout_fixture();
    layout_margin_rule_init(&widthless_rule, "i", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_margin_rule_px(&widthless_rule, "height", 20.0);
    layout_margin_rule_px(&widthless_rule, "margin-left", 5.0);
    layout_margin_rule_px(&widthless_rule, "margin-right", 15.0);
    rules2[0] = html_rule;
    rules2[1] = body_rule;
    rules2[2] = div_rule;
    rules2[3] = widthless_rule;
    layout_margin_begin(&second, "<html><body><div><i></i></div></body></html>", rules2,
                       4u);
    layout_margin_pass(&second, LAYOUT_MARGIN_VIEWPORT_WIDTH, NULL);
    size = layout_margin_size_of(&second, layout_margin_box_for(&second, "i"));
    ROBO_CHECK_EQ_INT(size.width, 780);
    layout_margin_end(&second);
}

static void test_margins_inline_baseline(void)
{
    layout_margin_fixture_t fixture;
    layout_margin_rule_t html_rule;
    layout_margin_rule_t body_rule;
    layout_margin_rule_t div_rule;
    layout_margin_rule_t i_rule;
    layout_margin_rule_t rules[4];
    robo_layout_line_box_t line;
    robo_layout_line_id_t line_id = ROBO_LAYOUT_LINE_ID_NONE;
    robo_layout_point_t origin;
    robo_layout_size_t size;
    robo_layout_baseline_metrics_t metrics;

    layout_fixture();

    /*
     * An `inline-block` with a bottom margin. CSS specifies that its baseline -- with no
     * in-flow line box of its own -- is its **bottom margin edge**, so the box sits 6px
     * above the floor and makes the line 26px tall rather than 20. A build that passed
     * `height` alone would put the box's border edge on the floor and quietly ignore a
     * declaration the author wrote.
     */
    layout_margin_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_init(&i_rule, "i", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_margin_rule_px(&i_rule, "width", 40.0);
    layout_margin_rule_px(&i_rule, "height", 20.0);
    layout_margin_rule_px(&i_rule, "margin-bottom", 6.0);
    layout_margin_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = i_rule;

    layout_margin_begin(&fixture, "<html><body><div><i></i></div></body></html>", rules,
                       4u);
    layout_margin_pass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, NULL);

    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture.tree, 0u, &line_id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, line_id, &line, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(line.size.height, 26);
    ROBO_CHECK_EQ_INT(line.baseline.ascent, 26);
    ROBO_CHECK_EQ_INT(line.baseline.descent, 0);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline(&line, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(metrics.ascent, 26);
    ROBO_CHECK_EQ_INT(line.size.width, 40);

    /* The box itself is 20 tall -- a margin never changes a box's size -- and it is
     * placed at the line's top, so its bottom *margin* edge is what rests on the floor. */
    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "i"));
    size = layout_margin_size_of(&fixture, layout_margin_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(origin.y, 0);
    ROBO_CHECK_EQ_INT(size.height, 20);

    /* And the owner's height is the line's, which is the margin box's. */
    size = layout_margin_size_of(&fixture, layout_margin_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.height, 26);

    layout_margin_end(&fixture);
}

static void test_margins_object_door(void)
{
    layout_margin_fixture_t fixture;
    layout_margin_rule_t html_rule;
    layout_margin_rule_t body_rule;
    layout_margin_rule_t div_rule;
    layout_margin_rule_t rules[3];
    robo_layout_margin_edges_t edges;
    robo_layout_node_id_t div_id;
    robo_layout_node_id_t text_id = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_node_id_t span_id;
    robo_layout_node_id_t inline_id;
    size_t count;
    size_t index;

    layout_fixture();

    layout_margin_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&div_rule, "margin-left", 7.0);
    layout_margin_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_margin_begin(&fixture,
                       "<html><body><div>hello</div><span></span><b></b></body></html>",
                       rules, 3u);
    layout_margin_pass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, NULL);

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

    div_id = layout_margin_box_for(&fixture, "div");
    span_id = layout_margin_box_for(&fixture, "span");
    inline_id = layout_margin_box_for(&fixture, "b");

    /* A placed block box: the door answers, and answers the *used* values. */
    ROBO_CHECK_EQ_INT(robo_layout_object_margins(fixture.tree, div_id, &edges, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(edges.left, 7);
    ROBO_CHECK_EQ_INT(edges.resolved, 1u);

    /*
     * A text run is not a box at all, and a transparent inline box is one CSS applies no
     * margins to -- both are *misses* rather than zeros, because a zero with a count of
     * resolved sides would tell a caller the author's margin had been honoured and come
     * to nothing.
     */
    ROBO_CHECK(text_id != ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_object_margins(fixture.tree, text_id, &edges, NULL),
                      ROBO_NOT_FOUND);
    /* Cleared on the way out rather than left holding whatever the caller's stack had. */
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.available_content_width, 0);
    ROBO_CHECK_EQ_INT(robo_layout_object_margins(fixture.tree, span_id, &edges, NULL),
                      ROBO_NOT_FOUND);

    /* An inline-level box in a *block* context is skipped rather than placed, so nothing
     * has used its margins either. */
    ROBO_CHECK_EQ_INT(robo_layout_object_margins(fixture.tree, inline_id, &edges, NULL),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_layout_object_margins(NULL, div_id, &edges, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_margins(fixture.tree, ROBO_LAYOUT_NODE_ID_NONE,
                                                &edges, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_margins(fixture.tree, div_id, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_margins(fixture.tree,
                                                (robo_layout_node_id_t)(count + 99u), &edges,
                                                NULL),
                      ROBO_NOT_FOUND);

    layout_margin_end(&fixture);
}

static void test_margins_relayout(void)
{
    layout_margin_fixture_t fixture;
    layout_margin_rule_t body_rule;
    layout_margin_rule_t div_rule;
    layout_margin_rule_t rules[2];
    robo_layout_stats_t stats;
    robo_layout_point_t origin;
    robo_layout_point_t settled;

    layout_fixture();

    layout_margin_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_margin_rule_px(&div_rule, "width", 100.0);
    layout_margin_rule_px(&div_rule, "height", 10.0);
    layout_margin_rule_px(&div_rule, "margin-left", 10.0);
    rules[0] = body_rule;
    rules[1] = div_rule;

    layout_margin_begin(&fixture, "<html><body><div></div></body></html>", rules, 2u);
    ROBO_CHECK_EQ_INT(layout_margin_entry_px(fixture.source.rules[1].style, "margin-left"),
                      10);

    layout_margin_pass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, &stats);
    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 10);

    /* A second pass over an unchanged tree is the same document: the records agree field
     * for field, which is what makes "the margin layer holds no state between calls" a
     * fact rather than a hope. */
    {
        robo_layout_margin_edges_t first = layout_margin_edges_of(
            &fixture, layout_margin_box_for(&fixture, "div"));
        robo_layout_margin_edges_t again;

        layout_margin_pass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, NULL);
        again = layout_margin_edges_of(&fixture, layout_margin_box_for(&fixture, "div"));
        ROBO_CHECK_EQ_INT(robo_layout_margin_edges_equal(&first, &again), 1);
    }

    /*
     * And a *changed* declaration is a changed document. The style is rebuilt with a
     * different `margin-left`, the tree is rebuilt against it, and the pass runs again:
     * the box moves, which is the whole of the requirement that a margin change
     * invalidates the geometry derived from it. (The invalidation itself is 10.3's --
     * a rebuild releases the classification and every result with it -- and this case
     * drives it rather than re-implementing it.)
     */
    layout_margin_rule_px(&div_rule, "margin-left", 40.0);
    layout_margin_style_replace(&fixture.source, "div", &div_rule);
    {
        robo_layout_build_options_t options;

        ROBO_CHECK_EQ_INT(
            layout_margin_entry_px(fixture.source.rules[1].style, "margin-left"), 40);
        robo_layout_build_options_init(&options);
        options.styles = &fixture.styles;
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture.tree, fixture.doc.id,
                                                              &options, NULL, NULL),
                          ROBO_OK);
    }
    layout_margin_pass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, NULL);
    origin = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 40);

    /* And a changed *viewport* leaves a **declared** margin where it is: the declaration
     * is not a function of the containing block, and this asserts that the phase did not
     * make one. (A distributed `auto`, by contrast, moves -- which
     * `layout.margins.auto_center_geometry` reads at another width.) */
    layout_margin_pass(&fixture, 300, NULL);
    settled = layout_margin_origin_of(&fixture, layout_margin_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(settled.x, 40);
    ROBO_CHECK_EQ_INT(settled.y, 0);

    layout_margin_end(&fixture);
}

static void test_margins_allocator_balance(void)
{
    size_t before = layout_live_blocks();
    size_t settled;
    int round;

    layout_fixture();

    for (round = 0; round < 3; round++) {
        layout_margin_fixture_t fixture;
        layout_margin_rule_t body_rule;
        layout_margin_rule_t div_rule;
        layout_margin_rule_t rules[2];

        layout_margin_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
        layout_margin_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_margin_rule_px(&div_rule, "width", 250.0);
        layout_margin_rule_px(&div_rule, "height", 20.0);
        layout_margin_rule_auto(&div_rule, "margin-left");
        layout_margin_rule_auto(&div_rule, "margin-right");
        rules[0] = body_rule;
        rules[1] = div_rule;

        layout_margin_begin(&fixture, "<html><body><div></div></body></html>", rules, 2u);
        layout_margin_pass(&fixture, LAYOUT_MARGIN_VIEWPORT_WIDTH, NULL);
        settled = layout_live_blocks();
        /* The margin layer allocates nothing of its own: the records are values on the
         * stack and on the object, and the pass adds no arena. */
        ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
        layout_margin_pass(&fixture, 400, NULL);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
        layout_margin_end(&fixture);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_layout_margin(void);

void robo_test_register_layout_margin(void)
{
    robo_test_register("layout.margins.kind_vocabulary", test_margins_kind_vocabulary);
    robo_test_register("layout.margins.one_side", test_margins_one_side);
    robo_test_register("layout.margins.sides", test_margins_sides);
    robo_test_register("layout.margins.seam", test_margins_seam);
    robo_test_register("layout.margins.edges_record", test_margins_edges_record);
    robo_test_register("layout.margins.block_basic", test_margins_block_basic);
    robo_test_register("layout.margins.block_sides", test_margins_block_sides);
    robo_test_register("layout.margins.block_negative", test_margins_block_negative);
    robo_test_register("layout.margins.block_auto_center", test_margins_block_auto_center);
    robo_test_register("layout.margins.block_auto_odd", test_margins_block_auto_odd);
    robo_test_register("layout.margins.block_auto_under_auto_width",
                       test_margins_block_auto_under_auto_width);
    robo_test_register("layout.margins.block_overconstrained",
                       test_margins_block_overconstrained);
    robo_test_register("layout.margins.block_unresolved", test_margins_block_unresolved);
    robo_test_register("layout.margins.block_vertical_auto",
                       test_margins_block_vertical_auto);
    robo_test_register("layout.margins.inline_policy", test_margins_inline_policy);
    robo_test_register("layout.margins.block_geometry", test_margins_block_geometry);
    robo_test_register("layout.margins.auto_center_geometry",
                       test_margins_auto_center_geometry);
    robo_test_register("layout.margins.margin_box_flows_down",
                       test_margins_margin_box_flows_down);
    robo_test_register("layout.margins.block_stacking", test_margins_block_stacking);
    robo_test_register("layout.margins.block_no_collapse", test_margins_block_no_collapse);
    robo_test_register("layout.margins.inline_advance", test_margins_inline_advance);
    robo_test_register("layout.margins.inline_baseline", test_margins_inline_baseline);
    robo_test_register("layout.margins.object_door", test_margins_object_door);
    robo_test_register("layout.margins.relayout", test_margins_relayout);
    robo_test_register("layout.margins.allocator_balance", test_margins_allocator_balance);
}
