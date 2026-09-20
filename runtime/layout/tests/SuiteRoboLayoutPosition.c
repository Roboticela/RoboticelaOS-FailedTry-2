/*
 * RoboLayout positioning suite (Phase 10.12) — layout.positioning.*
 *
 * Five things are under test, and the boundaries between them are the phase:
 *
 *   the **vocabulary** -- four inset kinds over four sides, and the one respect in
 *   which this vocabulary differs from the three before it: an inset is **signed**,
 *   like a margin and unlike a padding or a border, because `left: -10px` is a legal
 *   declaration and a real technique;
 *
 *   the **seam**: what a computed style declared, which is the one place this module
 *   reads `position` and its four offsets, and the one place the shared
 *   classification is asked with the margin's `allow_negative` rather than the
 *   dimension's;
 *
 *   the **policy**: the precedence rule -- left over right, top over bottom, an
 *   `auto` side saying nothing -- driven directly over fabricated position records,
 *   so a wrong precedence fails at the arithmetic rather than three boxes later in a
 *   coordinate;
 *
 *   the two **coordinate spaces**: the apply that turns a flow position into a final
 *   one, and the object record that turns a final position back into a flow one;
 *
 *   the **pass**: a displacement reaching geometry, which since this phase means a
 *   `relative` box is drawn where its offset puts it while its `size`, its baseline
 *   and -- the assertion a build could pass every single-box test without honouring
 *   -- its **flow contribution** are untouched. The following sibling is the witness:
 *   `layout.positioning.flow_preserved` fails a build that moved the box *and* the
 *   cursor, which is the most plausible way to get this phase wrong.
 *
 * The scripts here are *computed styles*, built through 9.8's makers exactly as the
 * sizing, margin, padding and border suites build them, because a fabricated struct
 * would prove the pass reads a struct rather than a computed CSS value. The mode is
 * 9.13's own POSITION value and the insets are the box model's EDGE_OFFSET shape,
 * which is why `top: 20%` and `margin-top: 20%` arrive here normalised the same way.
 *
 * Elements are resolved **by tag** (`layout_position_box_for`) rather than by arena
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
typedef enum layout_position_decl_kind {
    LAYOUT_POSITION_DECL_PX = 0,
    /** A px length that is **negative**: legal CSS for an inset, and the case the
     *  signedness exists for. */
    LAYOUT_POSITION_DECL_NEGATIVE_PX = 1,
    /** A px length flagged unresolved, which is what a `var()` arrives as. */
    LAYOUT_POSITION_DECL_UNRESOLVED_PX = 2,
    LAYOUT_POSITION_DECL_PERCENTAGE = 3,
    LAYOUT_POSITION_DECL_EM = 4,
    /** The keyword `auto`, or any other identifier the grammar allows. */
    LAYOUT_POSITION_DECL_KEYWORD = 5,
    /** A typed `border-*-style` (the border suite's path): a keyword the *style*
     *  layer folds into a kinded value, which is what the border reader consults. */
    LAYOUT_POSITION_DECL_BORDER_STYLE = 7,
    /** A `robo_css_computed_value_t` of kind POSITION -- 9.13's typed vocabulary. */
    LAYOUT_POSITION_DECL_POSITION = 6
} layout_position_decl_kind_t;

typedef struct layout_position_decl {
    const char *name;
    layout_position_decl_kind_t kind;
    double number;
    const char *keyword;
    robo_css_positioning_t position;
    robo_css_border_style_t border_style;
} layout_position_decl_t;

#define LAYOUT_POSITION_MAX_DECLARATIONS 20u
#define LAYOUT_POSITION_MAX_RULES 8u

typedef struct layout_position_rule {
    const char *tag;
    robo_css_display_t display;
    layout_position_decl_t declarations[LAYOUT_POSITION_MAX_DECLARATIONS];
    size_t declarations_count;
} layout_position_rule_t;

static void layout_position_rule_init(layout_position_rule_t *rule,
                                      const char *tag,
                                      robo_css_display_t display)
{
    memset(rule, 0, sizeof(*rule));
    rule->tag = tag;
    rule->display = display;
}

static layout_position_decl_t *layout_position_rule_add(layout_position_rule_t *rule,
                                                        const char *name,
                                                        layout_position_decl_kind_t kind)
{
    layout_position_decl_t *declaration;

    ROBO_CHECK_EQ_INT(rule->declarations_count < LAYOUT_POSITION_MAX_DECLARATIONS, 1);
    declaration = &rule->declarations[rule->declarations_count];
    memset(declaration, 0, sizeof(*declaration));
    declaration->name = name;
    declaration->kind = kind;
    declaration->position = ROBO_CSS_POSITIONING_STATIC;
    rule->declarations_count++;
    return declaration;
}

static void layout_position_rule_px(layout_position_rule_t *rule, const char *name, double px)
{
    layout_position_rule_add(rule, name, LAYOUT_POSITION_DECL_PX)->number = px;
}

/** A *negative* px length, which CSS allows for an inset and this layer honours. */
static void layout_position_rule_negative(layout_position_rule_t *rule,
                                          const char *name,
                                          double px)
{
    layout_position_rule_add(rule, name, LAYOUT_POSITION_DECL_NEGATIVE_PX)->number = -px;
}

static void layout_position_rule_unresolved(layout_position_rule_t *rule,
                                            const char *name,
                                            double px)
{
    layout_position_rule_add(rule, name, LAYOUT_POSITION_DECL_UNRESOLVED_PX)->number = px;
}

static void layout_position_rule_percentage(layout_position_rule_t *rule,
                                            const char *name,
                                            double percent)
{
    layout_position_rule_add(rule, name, LAYOUT_POSITION_DECL_PERCENTAGE)->number = percent;
}

static void layout_position_rule_em(layout_position_rule_t *rule, const char *name, double em)
{
    layout_position_rule_add(rule, name, LAYOUT_POSITION_DECL_EM)->number = em;
}

static void layout_position_rule_keyword(layout_position_rule_t *rule,
                                         const char *name,
                                         const char *keyword)
{
    layout_position_rule_add(rule, name, LAYOUT_POSITION_DECL_KEYWORD)->keyword = keyword;
}

static void layout_position_rule_border_style(layout_position_rule_t *rule,
                                              const char *name,
                                              robo_css_border_style_t style)
{
    layout_position_rule_add(rule, name, LAYOUT_POSITION_DECL_BORDER_STYLE)->border_style =
        style;
}

static void layout_position_rule_position(layout_position_rule_t *rule,
                                          robo_css_positioning_t mode)
{
    layout_position_rule_add(rule, "position", LAYOUT_POSITION_DECL_POSITION)->position = mode;
}

typedef struct layout_position_style_slot {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
    size_t answers;
} layout_position_style_slot_t;

typedef struct layout_position_source {
    layout_position_style_slot_t rules[LAYOUT_POSITION_MAX_RULES];
    size_t count;
} layout_position_source_t;

/**
 * Build the style @p rule describes: a real DISPLAY value plus whatever the rule
 * declares, all through 9.8's own makers and container.
 *
 * `display` is set even when the rule declares nothing else, because 9.12's initial
 * value is `inline` and a fixture that wanted a block box would otherwise be asserting
 * a document made of inline boxes.
 */
static robo_css_computed_style_t *layout_position_style_build(const layout_position_rule_t *rule)
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
        const layout_position_decl_t *declaration = &rule->declarations[index];

        value = (robo_css_computed_value_t)ROBO_CSS_CLEARED;
        specified = (robo_css_value_t)ROBO_CSS_CLEARED;
        switch (declaration->kind) {
        case LAYOUT_POSITION_DECL_POSITION:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_position(&value, declaration->position,
                                                                    NULL),
                              ROBO_OK);
            break;
        case LAYOUT_POSITION_DECL_PERCENTAGE:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_percentage(&value, declaration->number,
                                                                     0, NULL),
                              ROBO_OK);
            break;
        case LAYOUT_POSITION_DECL_KEYWORD:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_keyword(
                                  &value, robo_css_slice_from_text(declaration->keyword), NULL),
                              ROBO_OK);
            break;
        case LAYOUT_POSITION_DECL_BORDER_STYLE:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_border_style(
                                  &value, declaration->border_style, NULL),
                              ROBO_OK);
            break;
        case LAYOUT_POSITION_DECL_UNRESOLVED_PX:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, declaration->number,
                                                                 ROBO_CSS_UNIT_PX, 0, NULL),
                              ROBO_OK);
            /* The flag the computed layer sets for a `var()` or a `calc()`: carried
             * intact and not a value *yet*, whatever its kind says. */
            value.unresolved = 1;
            break;
        case LAYOUT_POSITION_DECL_EM:
            ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, declaration->number,
                                                                 ROBO_CSS_UNIT_EM, 0, NULL),
                              ROBO_OK);
            break;
        case LAYOUT_POSITION_DECL_NEGATIVE_PX:
        case LAYOUT_POSITION_DECL_PX:
        default:
            /* Written the same way for both, and that is the point: the *style* holds
             * what it was handed -- a negative inset is a computed value like any other
             * and the layer honours it -- while a negative *border width* is refused.
             * The grammar decides, not the maker. */
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
static robo_css_computed_style_t *layout_position_style_add(layout_position_source_t *source,
                                                            const layout_position_rule_t *rule)
{
    size_t slot;
    robo_css_computed_style_t *style;

    layout_fixture();
    ROBO_CHECK_EQ_INT(source->count < LAYOUT_POSITION_MAX_RULES, 1);
    slot = source->count;
    style = layout_position_style_build(rule);
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
static void layout_position_style_replace(layout_position_source_t *source,
                                          const char *tag,
                                          const layout_position_rule_t *rule)
{
    size_t index;

    layout_fixture();
    for (index = 0u; index < source->count; index++) {
        if (strcmp(source->rules[index].tag, tag) == 0) {
            robo_css_computed_style_destroy(source->rules[index].style);
            source->rules[index].style = layout_position_style_build(rule);
            return;
        }
    }
    ROBO_CHECK_MSG(0, "the tag to replace is not in this source");
}

static const robo_css_computed_style_t *layout_position_style_lookup(
    void *userdata,
    const robo_html_node_handle_t *element)
{
    layout_position_source_t *source = (layout_position_source_t *)userdata;
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
    /* An element nothing was declared for: `display: inline`, `position: static`, and
     * those are CSS's initial values rather than a fixture default. */
    return NULL;
}

typedef struct layout_position_fixture {
    layout_document_t doc;
    layout_position_source_t source;
    robo_layout_style_source_t styles;
    robo_layout_tree_t *tree;
} layout_position_fixture_t;

static void layout_position_begin(layout_position_fixture_t *fixture,
                                  const char *html,
                                  const layout_position_rule_t *rules,
                                  size_t rule_count)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    for (index = 0u; index < rule_count; index++) {
        (void)layout_position_style_add(&fixture->source, &rules[index]);
    }
    robo_layout_style_source_init(&fixture->styles);
    fixture->styles.userdata = &fixture->source;
    fixture->styles.computed_style_for = layout_position_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_position_end(layout_position_fixture_t *fixture)
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

#define LAYOUT_POSITION_VIEWPORT_WIDTH 800
#define LAYOUT_POSITION_VIEWPORT_HEIGHT 600

/** One pass at @p viewport_width. */
static void layout_position_pass(layout_position_fixture_t *fixture,
                                 int32_t viewport_width,
                                 robo_layout_stats_t *out_stats)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;

    viewport.width = viewport_width;
    viewport.height = LAYOUT_POSITION_VIEWPORT_HEIGHT;
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
static robo_layout_node_id_t layout_position_box_for(layout_position_fixture_t *fixture,
                                                     const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);
    robo_layout_node_id_t id;

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&element), 1);
    id = robo_layout_object_id_for_dom_node(fixture->tree, &element);
    ROBO_CHECK(id != ROBO_LAYOUT_NODE_ID_NONE);
    return id;
}

/** The origin a placed box was given -- its **final** position since 10.12. */
static robo_layout_point_t layout_position_origin_of(layout_position_fixture_t *fixture,
                                                     robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_point_t position;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
    return position;
}

/** The size a placed box was given -- its **border** box, untouched by any offset. */
static robo_layout_size_t layout_position_size_of(layout_position_fixture_t *fixture,
                                                  robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_size_t size;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
    return size;
}

/** The used displacement a placed box reports. */
static robo_layout_position_offset_t layout_position_offset_of(
    layout_position_fixture_t *fixture,
    robo_layout_node_id_t id)
{
    robo_layout_position_offset_t offset;

    ROBO_CHECK_EQ_INT(robo_layout_object_position(fixture->tree, id, &offset, NULL), ROBO_OK);
    return offset;
}

/**
 * Rebuild the layout tree against the styles @p fixture was built with.
 *
 * The step a changed declaration needs and the reason it is not optional: the tree
 * **borrows** one computed style per element for as long as the bridge lives, so a
 * replacement that left the old tree standing would leave it holding a destroyed style.
 * The rule is 10.9's, 10.10's and 10.11's, and it is what makes "the second answer is a
 * fact about the second style" checkable rather than a hope.
 */
static void layout_position_rebuild(layout_position_fixture_t *fixture)
{
    robo_layout_build_options_t options;

    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* A hand-built position, for the policy                               */
/* ------------------------------------------------------------------ */

/** A declared position: a mode and four insets, each built from its own kind. */
static robo_layout_position_t layout_position_declared(robo_css_positioning_t mode,
                                                       robo_layout_inset_t top,
                                                       robo_layout_inset_t right,
                                                       robo_layout_inset_t bottom,
                                                       robo_layout_inset_t left)
{
    robo_layout_position_t position;

    position.mode = mode;
    position.insets.top = top;
    position.insets.right = right;
    position.insets.bottom = bottom;
    position.insets.left = left;
    return position;
}

/** The declared record plus its resolve, so a policy case reads as one call. */
static robo_result_t layout_position_resolved(robo_css_positioning_t mode,
                                              robo_layout_inset_t top,
                                              robo_layout_inset_t right,
                                              robo_layout_inset_t bottom,
                                              robo_layout_inset_t left,
                                              robo_layout_position_offset_t *offset)
{
    robo_layout_position_t declared = layout_position_declared(mode, top, right, bottom, left);

    return robo_layout_position_resolve(&declared, offset, NULL);
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void test_positioning_kind_vocabulary(void)
{
    size_t index;

    layout_fixture();

    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_INSET_KIND_COUNT, 4);
    /*
     * The zero of the vocabulary is `ABSENT`, and that is a decision rather than a
     * numbering accident: a cleared record then holds the side's *initial state*, so a
     * forgotten initialisation is not a claim that somebody wrote an inset. The same
     * decision the margin, padding and border vocabularies made.
     */
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_INSET_ABSENT, 0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_INSET_RESOLVED, 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_INSET_AUTO, 2);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_INSET_UNRESOLVED, 3);

    for (index = 0u; index < ROBO_LAYOUT_INSET_KIND_COUNT; index++) {
        robo_layout_inset_kind_t kind = (robo_layout_inset_kind_t)index;

        ROBO_CHECK_EQ_INT(robo_layout_inset_kind_is_known(kind), 1);
        ROBO_CHECK(robo_layout_inset_kind_name(kind) != NULL);
    }
    ROBO_CHECK_EQ_INT(robo_layout_inset_kind_is_known((robo_layout_inset_kind_t)4), 0);
    ROBO_CHECK_EQ_INT(robo_layout_inset_kind_is_known((robo_layout_inset_kind_t)77), 0);
    ROBO_CHECK_STR_EQ(robo_layout_inset_kind_name(ROBO_LAYOUT_INSET_ABSENT), "absent");
    ROBO_CHECK_STR_EQ(robo_layout_inset_kind_name(ROBO_LAYOUT_INSET_RESOLVED), "resolved");
    ROBO_CHECK_STR_EQ(robo_layout_inset_kind_name(ROBO_LAYOUT_INSET_AUTO), "auto");
    ROBO_CHECK_STR_EQ(robo_layout_inset_kind_name(ROBO_LAYOUT_INSET_UNRESOLVED), "unresolved");
    ROBO_CHECK_STR_EQ(robo_layout_inset_kind_name((robo_layout_inset_kind_t)4), "unknown");
    ROBO_CHECK(robo_layout_inset_kind_name((robo_layout_inset_kind_t)77) != NULL);

    /* And the domain this phase added, spelled and counted: 22 since 10.14 appended its
     * own, with `position` the nineteenth -- the first domain in this module about *where*
     * rather than about *how big*, and no longer the last since 10.13 and 10.14 followed it. */
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_COUNT, 22);
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(ROBO_LAYOUT_DOMAIN_POSITION),
                      "position");
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_OVERFLOW,
                      (int)ROBO_LAYOUT_DOMAIN_POSITION + 1);
}

static void test_positioning_inset_record(void)
{
    robo_layout_inset_t zero = robo_layout_inset_zero();
    robo_layout_inset_t absent = robo_layout_inset_absent();
    robo_layout_inset_t resolved = robo_layout_inset_resolved(12);
    robo_layout_inset_t negative = robo_layout_inset_resolved(-7);
    robo_layout_inset_t auto_inset = robo_layout_inset_auto();
    robo_layout_inset_t unresolved = robo_layout_inset_unresolved();
    robo_layout_inset_t bad_kind;
    robo_gfx_pixel_x_t length = 0;

    layout_fixture();

    /* `top: 0` is an inset an author asked for; `ABSENT` is a side nobody declared.
     * Both contribute nothing and they are different records -- the reason this
     * vocabulary has kinds at all, inherited from 10.9. */
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_resolved(&zero), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_absent(&zero), 0);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_valid(&zero), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inset_length(&zero, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, 0);
    ROBO_CHECK_EQ_INT(robo_layout_inset_equal(&zero, &absent), 0);
    ROBO_CHECK_EQ_INT(robo_layout_inset_equal(&zero, &zero), 1);

    ROBO_CHECK_EQ_INT(robo_layout_inset_is_absent(&absent), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_valid(&absent), 1);
    /* A miss rather than a zero: reading the field of a side nobody declared would be
     * reading a number that means nothing. */
    length = 77;
    ROBO_CHECK_EQ_INT(robo_layout_inset_length(&absent, &length), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(length, 77);
    ROBO_CHECK_EQ_INT(robo_layout_inset_equal(&absent, &absent), 1);

    {
        robo_layout_inset_t other = robo_layout_inset_resolved(13);

        ROBO_CHECK_EQ_INT(robo_layout_inset_is_resolved(&resolved), 1);
        ROBO_CHECK_EQ_INT(robo_layout_inset_is_valid(&resolved), 1);
        ROBO_CHECK_EQ_INT(robo_layout_inset_length(&resolved, &length), ROBO_OK);
        ROBO_CHECK_EQ_INT(length, 12);
        ROBO_CHECK_EQ_INT(robo_layout_inset_equal(&resolved, &resolved), 1);
        ROBO_CHECK_EQ_INT(robo_layout_inset_equal(&resolved, &other), 0);
    }

    /*
     * And the check this vocabulary does **not** have that the border one does: a
     * negative inset is *valid*. `left: -7px` is legal CSS and the signedness is the
     * one respect in which this record differs from a padding or a border -- which is
     * why the predicate below asserts the opposite of `border_width_is_valid` does.
     */
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_resolved(&negative), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_valid(&negative), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inset_length(&negative, &length), ROBO_OK);
    ROBO_CHECK_EQ_INT(length, -7);

    ROBO_CHECK_EQ_INT(robo_layout_inset_is_auto(&auto_inset), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_valid(&auto_inset), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inset_length(&auto_inset, &length), ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(robo_layout_inset_is_unresolved(&unresolved), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_valid(&unresolved), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inset_length(&unresolved, &length), ROBO_NOT_FOUND);

    bad_kind = robo_layout_inset_zero();
    bad_kind.kind = (robo_layout_inset_kind_t)99;
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_valid(&bad_kind), 0);

    /* Two `ABSENT` sides are equal whatever their stale `length` fields hold -- the
     * same rule the kind exists for, restated here because the equality is what a
     * re-layout comparison leans on. */
    {
        robo_layout_inset_t stale = robo_layout_inset_absent();

        stale.length = 42;
        ROBO_CHECK_EQ_INT(robo_layout_inset_equal(&absent, &stale), 1);
    }
}

static void test_positioning_position_record(void)
{
    robo_layout_position_t cleared;
    robo_layout_position_t relative;
    robo_layout_position_t invalid;
    robo_layout_position_t same;
    robo_layout_position_t different;

    layout_fixture();

    /*
     * A cleared record is CSS's initial state: `static` and four undeclared sides.
     * `STATIC` is 9.13's zero for exactly this reason, and the suite pins the mode
     * beside the vocabulary so the two zeros cannot drift apart.
     */
    robo_layout_position_init(&cleared);
    ROBO_CHECK_EQ_INT((int)cleared.mode, (int)ROBO_CSS_POSITIONING_STATIC);
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_POSITIONING_STATIC, 0);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_absent(&cleared.insets.top), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_absent(&cleared.insets.left), 1);
    ROBO_CHECK_EQ_INT(robo_layout_position_is_valid(&cleared), 1);
    ROBO_CHECK_EQ_INT(robo_layout_position_is_relative(&cleared), 0);

    relative = layout_position_declared(ROBO_CSS_POSITIONING_RELATIVE,
                                        robo_layout_inset_resolved(5),
                                        robo_layout_inset_absent(),
                                        robo_layout_inset_auto(),
                                        robo_layout_inset_resolved(10));
    ROBO_CHECK_EQ_INT(robo_layout_position_is_relative(&relative), 1);
    ROBO_CHECK_EQ_INT(robo_layout_position_is_valid(&relative), 1);
    ROBO_CHECK_EQ_INT(robo_layout_position_equal(&relative, &relative), 1);

    same = relative;
    ROBO_CHECK_EQ_INT(robo_layout_position_equal(&relative, &same), 1);
    different = relative;
    different.mode = ROBO_CSS_POSITIONING_STATIC;
    ROBO_CHECK_EQ_INT(robo_layout_position_equal(&relative, &different), 0);
    different = relative;
    different.insets.top = robo_layout_inset_resolved(6);
    ROBO_CHECK_EQ_INT(robo_layout_position_equal(&relative, &different), 0);

    invalid = relative;
    invalid.mode = (robo_css_positioning_t)99;
    ROBO_CHECK_EQ_INT(robo_layout_position_is_valid(&invalid), 0);

    /* And a NULL style reads as the cleared record -- a box with no style has made no
     * declarations, which is an answer rather than a failure. */
    {
        robo_layout_position_t from_null = robo_layout_position_of(NULL);

        ROBO_CHECK_EQ_INT(robo_layout_position_equal(&from_null, &cleared), 1);
    }
}

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

static void test_positioning_seam(void)
{
    layout_position_rule_t rule;
    layout_position_source_t source;
    robo_css_computed_style_t *style;
    robo_layout_position_t position;

    layout_fixture();

    /*
     * The twelve-declaration read of the border suite is here a **five**-declaration
     * read: the mode and four insets. The cases below walk every kind the grammar
     * allows, one side at a time, so a wrong classification names its side.
     */
    layout_position_rule_init(&rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&rule, "top", 5.0);
    layout_position_rule_negative(&rule, "right", 4.0);
    layout_position_rule_keyword(&rule, "bottom", "auto");
    layout_position_rule_em(&rule, "left", 2.0);
    source.count = 0u;
    style = layout_position_style_add(&source, &rule);

    position = robo_layout_position_of(style);
    ROBO_CHECK_EQ_INT((int)position.mode, (int)ROBO_CSS_POSITIONING_RELATIVE);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_resolved(&position.insets.top), 1);
    ROBO_CHECK_EQ_INT(position.insets.top.length, 5);
    /* A negative inset is *honoured*, which is the one respect this seam differs from
     * the border seam beside it: the grammar allows one and refuses the other. */
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_resolved(&position.insets.right), 1);
    ROBO_CHECK_EQ_INT(position.insets.right.length, -4);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_auto(&position.insets.bottom), 1);
    /* An `em` is declared and unusable here, exactly as it is in every layer since
     * 10.8: no font metrics reached this read. */
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_unresolved(&position.insets.left), 1);

    /*
     * A percentage -- legal CSS for an inset, and `UNRESOLVED` here for the reason
     * 10.8 set and every layer since inherited.
     */
    layout_position_style_replace(&source, "div", &rule);
    layout_position_rule_init(&rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_percentage(&rule, "left", 10.0);
    layout_position_style_replace(&source, "div", &rule);
    position = robo_layout_position_of(source.rules[0].style);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_unresolved(&position.insets.left), 1);

    /*
     * A `var()`-flagged length: declared, kinded as a length, and not a value yet.
     * A fifth reason on the same branch, so a document whose offsets depend on a
     * custom property is visible as one rather than flush for no stated reason.
     */
    layout_position_rule_init(&rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_unresolved(&rule, "top", 9.0);
    layout_position_style_replace(&source, "div", &rule);
    position = robo_layout_position_of(source.rules[0].style);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_unresolved(&position.insets.top), 1);

    /*
     * An absent `position` is `STATIC` -- CSS's initial value and 9.13's zero -- while
     * an absent inset is `ABSENT` and not a zero. The asymmetry is the two
     * vocabularies', not this reader's.
     */
    layout_position_rule_init(&rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_px(&rule, "top", 3.0);
    layout_position_style_replace(&source, "div", &rule);
    position = robo_layout_position_of(source.rules[0].style);
    ROBO_CHECK_EQ_INT((int)position.mode, (int)ROBO_CSS_POSITIONING_STATIC);
    ROBO_CHECK_EQ_INT(robo_layout_inset_is_resolved(&position.insets.top), 1);

    /* An unknown `position` identifier answers STATIC through 9.13's own door -- the
     * same ambiguity every keyword vocabulary has, resolved by a kind check the
     * *maker* already made for a real computed value. */
    layout_position_rule_init(&rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_keyword(&rule, "position", "banana");
    layout_position_style_replace(&source, "div", &rule);
    position = robo_layout_position_of(source.rules[0].style);
    ROBO_CHECK_EQ_INT((int)position.mode, (int)ROBO_CSS_POSITIONING_STATIC);

    /* And the four sides are the box vocabulary's order, readable one at a time. */
    {
        robo_layout_inset_t side;

        position = robo_layout_position_of(NULL);
        ROBO_CHECK_EQ_INT(robo_layout_insets_side(&position.insets, ROBO_CSS_BOX_SIDE_LEFT,
                                                  &side),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_inset_is_absent(&side), 1);
        ROBO_CHECK_EQ_INT(robo_layout_insets_side(&position.insets, (robo_css_box_side_t)9,
                                                  &side),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_layout_insets_is_valid(&position.insets), 1);
    }

    /* Destroy what the source *currently* holds, not the pointer the first build
     * handed back: every replace above destroyed and rebuilt the row's style, so the
     * first pointer has been dangling since the second case and freeing it here is a
     * double free (the crash this case used to end with). */
    {
        size_t index;

        for (index = 0u; index < source.count; index++) {
            robo_css_computed_style_destroy(source.rules[index].style);
            source.rules[index].style = NULL;
        }
        source.count = 0u;
    }
}

static void test_positioning_offset_record(void)
{
    robo_layout_position_offset_t offset;
    robo_layout_position_offset_t same;
    robo_layout_position_offset_t bad;

    layout_fixture();

    robo_layout_position_offset_init(&offset);
    ROBO_CHECK_EQ_INT(offset.dx, 0);
    ROBO_CHECK_EQ_INT(offset.dy, 0);
    ROBO_CHECK_EQ_INT(robo_layout_position_offset_is_valid(&offset), 1);

    offset.dx = 10;
    offset.dy = -5;
    offset.insets_resolved = 2u;
    ROBO_CHECK_EQ_INT(robo_layout_position_offset_is_valid(&offset), 1);

    same = offset;
    ROBO_CHECK_EQ_INT(robo_layout_position_offset_equal(&offset, &same), 1);
    same.dx = 11;
    ROBO_CHECK_EQ_INT(robo_layout_position_offset_equal(&offset, &same), 0);
    same = offset;
    same.insets_declined = 1u;
    ROBO_CHECK_EQ_INT(robo_layout_position_offset_equal(&offset, &same), 0);

    /* Five counted sides is a record no resolve produced: the counters' whole claim is
     * that they account for four declarations, so a hand-built record that cannot is
     * refused before anybody derives a position from it. */
    bad = offset;
    bad.insets_declined = 3u;
    ROBO_CHECK_EQ_INT(robo_layout_position_offset_is_valid(&bad), 0);
    bad.insets_declined = 2u;
    ROBO_CHECK_EQ_INT(robo_layout_position_offset_is_valid(&bad), 1);
}

/* ------------------------------------------------------------------ */
/* The policy                                                          */
/* ------------------------------------------------------------------ */

static void test_positioning_policy_precedence(void)
{
    robo_layout_position_offset_t offset;

    layout_fixture();

    /*
     * The phase's one rule, driven directly over hand-built records so a wrong
     * precedence fails at the arithmetic: **left wins over right, top wins over
     * bottom, and an `auto` side says nothing.**
     */

    /* One side each: the four basic displacements, with the sign flip the two "end"
     * sides carry -- `right` and `bottom` are distances *from* an edge. */
    ROBO_CHECK_EQ_INT(layout_position_resolved(                          ROBO_CSS_POSITIONING_RELATIVE,
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_resolved(10),
                          &offset),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(offset.dx, 10);
    ROBO_CHECK_EQ_INT(offset.dy, 0);
    ROBO_CHECK_EQ_INT(offset.insets_resolved, 1u);
    ROBO_CHECK_EQ_INT(offset.insets_declined, 0u);

    ROBO_CHECK_EQ_INT(layout_position_resolved(                          ROBO_CSS_POSITIONING_RELATIVE,
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_resolved(7),
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                          &offset),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(offset.dx, -7);
    ROBO_CHECK_EQ_INT(offset.dy, 0);

    ROBO_CHECK_EQ_INT(layout_position_resolved(                          ROBO_CSS_POSITIONING_RELATIVE,
                                                      robo_layout_inset_resolved(4),
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                          &offset),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(offset.dx, 0);
    ROBO_CHECK_EQ_INT(offset.dy, 4);

    ROBO_CHECK_EQ_INT(layout_position_resolved(                          ROBO_CSS_POSITIONING_RELATIVE,
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_resolved(9),
                                                      robo_layout_inset_absent(),
                          &offset),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(offset.dx, 0);
    ROBO_CHECK_EQ_INT(offset.dy, -9);

    /* Opposing pairs: `left` and `top` win, *and both sides are counted* -- the
     * counters are about declarations that reached this layer, and precedence is a
     * separate fact about which of two the displacement uses. */
    ROBO_CHECK_EQ_INT(layout_position_resolved(                          ROBO_CSS_POSITIONING_RELATIVE,
                                                      robo_layout_inset_resolved(4),
                                                      robo_layout_inset_resolved(7),
                                                      robo_layout_inset_resolved(9),
                                                      robo_layout_inset_resolved(10),
                          &offset),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(offset.dx, 10);
    ROBO_CHECK_EQ_INT(offset.dy, 4);
    ROBO_CHECK_EQ_INT(offset.insets_resolved, 4u);

    /* An `auto` side says nothing and is counted as the declaration it is -- never
     * folded onto `ABSENT`, which is the distinction 10.9's vocabulary exists for. */
    ROBO_CHECK_EQ_INT(layout_position_resolved(                          ROBO_CSS_POSITIONING_RELATIVE,
                                                      robo_layout_inset_auto(),
                                                      robo_layout_inset_resolved(7),
                                                      robo_layout_inset_auto(),
                                                      robo_layout_inset_auto(),
                          &offset),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(offset.dx, -7);
    ROBO_CHECK_EQ_INT(offset.dy, 0);
    ROBO_CHECK_EQ_INT(offset.insets_auto, 3u);
    ROBO_CHECK_EQ_INT(offset.insets_resolved, 1u);

    /* An unresolved side is used as zero and counted: a document displaced with
     * `left: 10%` lays out flush here, and the count is what says so. */
    ROBO_CHECK_EQ_INT(layout_position_resolved(                          ROBO_CSS_POSITIONING_RELATIVE,
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_unresolved(),
                          &offset),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(offset.dx, 0);
    ROBO_CHECK_EQ_INT(offset.dy, 0);
    ROBO_CHECK_EQ_INT(offset.insets_unresolved, 1u);

    /* A `relative` box that declared nothing at all resolves to the zero offset --
     * the common case, and a *value* rather than a refusal. */
    ROBO_CHECK_EQ_INT(layout_position_resolved(                          ROBO_CSS_POSITIONING_RELATIVE,
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                          &offset),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(offset.dx, 0);
    ROBO_CHECK_EQ_INT(offset.dy, 0);
    ROBO_CHECK_EQ_INT(offset.insets_resolved, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_position_offset_is_valid(&offset), 1);
}

static void test_positioning_policy_static_declines(void)
{
    robo_layout_position_offset_t offset;
    robo_css_positioning_t modes[4];
    size_t index;

    layout_fixture();

    /*
     * Rule 1: any mode that is not `relative` produces **no displacement** and
     * *declines* every declared side. Two reasons reach the branch and the mode tells
     * them apart -- `static` is CSS's initial value and CSS applies no offset to one;
     * `absolute`, `fixed` and `sticky` are placed against something this build does
     * not compute yet. The counter is the same on both paths by design, and the mode
     * is what a reader of the record consults.
     */
    modes[0] = ROBO_CSS_POSITIONING_STATIC;
    modes[1] = ROBO_CSS_POSITIONING_ABSOLUTE;
    modes[2] = ROBO_CSS_POSITIONING_FIXED;
    modes[3] = ROBO_CSS_POSITIONING_STICKY;

    for (index = 0u; index < 4u; index++) {
        ROBO_CHECK_EQ_INT(layout_position_resolved(                              modes[index],
                                                          robo_layout_inset_resolved(4),
                                                          robo_layout_inset_resolved(7),
                                                          robo_layout_inset_absent(),
                                                          robo_layout_inset_resolved(10),
                              &offset),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(offset.dx, 0);
        ROBO_CHECK_EQ_INT(offset.dy, 0);
        ROBO_CHECK_EQ_INT(offset.insets_resolved, 0u);
        ROBO_CHECK_EQ_INT(offset.insets_declined, 3u);
        ROBO_CHECK_EQ_INT(robo_layout_position_offset_is_valid(&offset), 1);
    }

    /* And a non-relative box that declared nothing declines nothing -- the counter
     * counts *declarations*, not sides that exist. */
    ROBO_CHECK_EQ_INT(layout_position_resolved(                          ROBO_CSS_POSITIONING_STATIC,
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                                                      robo_layout_inset_absent(),
                          &offset),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(offset.insets_declined, 0u);
}

static void test_positioning_apply(void)
{
    robo_layout_point_t flow;
    robo_layout_point_t final_point;
    robo_layout_position_offset_t offset;
    robo_layout_position_offset_t bad;

    layout_fixture();

    /*
     * The one place the two coordinate spaces meet, driven directly: the flow position
     * plus the displacement, in 64 bits and clamped.
     */
    flow.x = 100;
    flow.y = 50;
    robo_layout_position_offset_init(&offset);
    ROBO_CHECK_EQ_INT(robo_layout_position_apply(flow, &offset, &final_point, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(final_point.x, 100);
    ROBO_CHECK_EQ_INT(final_point.y, 50);

    offset.dx = 10;
    offset.dy = -5;
    ROBO_CHECK_EQ_INT(robo_layout_position_apply(flow, &offset, &final_point, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(final_point.x, 110);
    ROBO_CHECK_EQ_INT(final_point.y, 45);

    /*
     * Saturation, and the reason it clamps rather than wraps: the flow position and the
     * displacement are each bounded by the seam's maximum and their sum is not, so an
     * extreme pair must saturate rather than arrive on the other side of the screen.
     * 10.10 and 10.11 reached the same decision for a *size*; a position is the same
     * arithmetic one axis over.
     */
    flow.x = 2147483647;
    offset.dx = 100;
    ROBO_CHECK_EQ_INT(robo_layout_position_apply(flow, &offset, &final_point, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(final_point.x, 2147483647);

    flow.x = -2147483647;
    offset.dx = -100;
    ROBO_CHECK_EQ_INT(robo_layout_position_apply(flow, &offset, &final_point, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(final_point.x, -2147483647);

    /* And a record no resolve produced is refused before it moves anything -- and
     * before it touches the out, which is why the sentinel below survives. */
    bad = offset;
    bad.insets_declined = 9u;
    flow.x = 0;
    final_point.x = 55;
    ROBO_CHECK_EQ_INT(robo_layout_position_apply(flow, &bad, &final_point, NULL), ROBO_ERROR);
    ROBO_CHECK_EQ_INT(final_point.x, 55);
}

/* ------------------------------------------------------------------ */
/* The pass                                                            */
/* ------------------------------------------------------------------ */

static void test_positioning_block_relative(void)
{
    layout_position_fixture_t fixture;
    layout_position_rule_t html_rule;
    layout_position_rule_t body_rule;
    layout_position_rule_t div_rule;
    layout_position_rule_t after_rule;
    layout_position_rule_t rules[4];
    robo_layout_point_t origin;
    robo_layout_size_t size;
    robo_layout_position_offset_t offset;

    layout_fixture();

    /*
     * The phase's central claim in three numbers: a `relative` box with `left: 30px;
     * top: 20px` is **drawn** 30 right and 20 down from where flow put it, its **size**
     * is untouched, and the box after it starts exactly where it always would have.
     */
    layout_position_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&div_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&div_rule, "height", 40.0);
    layout_position_rule_px(&div_rule, "left", 30.0);
    layout_position_rule_px(&div_rule, "top", 20.0);
    layout_position_rule_init(&after_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_px(&after_rule, "height", 25.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = after_rule;

    layout_position_begin(&fixture, "<html><body><div></div><section></section></body></html>",
                          rules, 4u);
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);

    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 30);
    ROBO_CHECK_EQ_INT(origin.y, 20);
    size = layout_position_size_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, LAYOUT_POSITION_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(size.height, 40);

    /* The record a reader turns the final position back into the flow one with. */
    offset = layout_position_offset_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(offset.dx, 30);
    ROBO_CHECK_EQ_INT(offset.dy, 20);
    ROBO_CHECK_EQ_INT(offset.insets_resolved, 2u);

    /* And the witness: `section` at the flow position the `div` *would* have had. */
    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "section"));
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(origin.y, 40);

    layout_position_end(&fixture);
}

static void test_positioning_flow_preserved(void)
{
    layout_position_fixture_t fixture;
    layout_position_rule_t html_rule;
    layout_position_rule_t body_rule;
    layout_position_rule_t first_rule;
    layout_position_rule_t second_rule;
    layout_position_rule_t third_rule;
    layout_position_rule_t rules[5];
    robo_layout_point_t origin;
    robo_layout_stats_t stats;
    robo_layout_position_offset_t offset;

    layout_fixture();

    /*
     * The brief's visual case, as geometry: a normal box, a relative box shifted
     * 100px down, and a box after it. The relative box moves; its original flow space
     * stays occupied; the following element does not move into it -- and `after`'s
     * origin is *below* the relative box's *unshifted* bottom edge, which is the one
     * assertion that separates "the offset applied" from "the box moved".
     */
    layout_position_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&first_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_px(&first_rule, "height", 30.0);
    layout_position_rule_init(&second_rule, "span", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&second_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&second_rule, "height", 30.0);
    layout_position_rule_px(&second_rule, "top", 100.0);
    layout_position_rule_init(&third_rule, "b", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_px(&third_rule, "height", 30.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = first_rule;
    rules[3] = second_rule;
    rules[4] = third_rule;

    layout_position_begin(&fixture,
                          "<html><body><div></div><span></span><b></b></body></html>", rules,
                          5u);
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, &stats);

    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.y, 0);

    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "span"));
    ROBO_CHECK_EQ_INT(origin.y, 130);
    offset = layout_position_offset_of(&fixture, layout_position_box_for(&fixture, "span"));
    ROBO_CHECK_EQ_INT(offset.dy, 100);

    /* Not 60 (drawn into the hole) and not 230 (flow moved by the offset): 60 is the
     * flow position and the offset lives only on `span`. */
    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "b"));
    ROBO_CHECK_EQ_INT(origin.y, 60);

    ROBO_CHECK_EQ_INT(stats.block.boxes_shifted, 1u);
    ROBO_CHECK_EQ_INT(stats.block.offsets_applied, 1u);

    layout_position_end(&fixture);
}

static void test_positioning_block_negative(void)
{
    layout_position_fixture_t fixture;
    layout_position_rule_t html_rule;
    layout_position_rule_t body_rule;
    layout_position_rule_t div_rule;
    layout_position_rule_t rules[3];
    robo_layout_point_t origin;
    robo_layout_size_t size;
    robo_layout_position_offset_t offset;

    layout_fixture();

    /*
     * Negative offsets are legal and honoured -- the signedness the vocabulary exists
     * for. A box shifted up and left produces negative *positions* and a perfectly
     * valid rectangle: nothing clamps, nothing refuses, and the size is untouched.
     */
    layout_position_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&div_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&div_rule, "height", 40.0);
    layout_position_rule_negative(&div_rule, "left", 10.0);
    layout_position_rule_negative(&div_rule, "top", 5.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_position_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);

    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, -10);
    ROBO_CHECK_EQ_INT(origin.y, -5);
    size = layout_position_size_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, LAYOUT_POSITION_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(size.height, 40);

    offset = layout_position_offset_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(offset.dx, -10);
    ROBO_CHECK_EQ_INT(offset.dy, -5);
    ROBO_CHECK_EQ_INT(robo_layout_position_offset_is_valid(&offset), 1);

    layout_position_end(&fixture);
}

static void test_positioning_block_static_declines(void)
{
    layout_position_fixture_t fixture;
    layout_position_rule_t html_rule;
    layout_position_rule_t body_rule;
    layout_position_rule_t div_rule;
    layout_position_rule_t rules[3];
    robo_layout_point_t origin;
    robo_layout_stats_t stats;
    robo_layout_position_offset_t offset;

    layout_fixture();

    /*
     * The case that separates two documents whose displacement is the same zero: a
     * `static` box with four insets and a `relative` box with none. CSS applies no
     * offset to a static box -- 9.13's header names the mistake this rule prevents --
     * so the insets change nothing here and are **counted as declined**, which is what
     * makes the first document visible as one.
     */
    layout_position_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&div_rule, ROBO_CSS_POSITIONING_STATIC);
    layout_position_rule_px(&div_rule, "height", 40.0);
    layout_position_rule_px(&div_rule, "top", 50.0);
    layout_position_rule_px(&div_rule, "left", 60.0);
    layout_position_rule_px(&div_rule, "right", 70.0);
    layout_position_rule_px(&div_rule, "bottom", 80.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_position_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, &stats);

    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(origin.y, 0);

    offset = layout_position_offset_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(offset.dx, 0);
    ROBO_CHECK_EQ_INT(offset.dy, 0);
    ROBO_CHECK_EQ_INT(offset.insets_declined, 4u);
    ROBO_CHECK_EQ_INT(offset.insets_resolved, 0u);

    ROBO_CHECK_EQ_INT(stats.block.boxes_shifted, 0u);
    ROBO_CHECK_EQ_INT(stats.block.offsets_declined, 4u);

    layout_position_end(&fixture);
}

static void test_positioning_box_model_interaction(void)
{
    layout_position_fixture_t fixture;
    layout_position_rule_t html_rule;
    layout_position_rule_t body_rule;
    layout_position_rule_t a_rule;
    layout_position_rule_t b_rule;
    layout_position_rule_t c_rule;
    layout_position_rule_t rules[5];
    robo_layout_point_t origin;
    robo_layout_size_t size;

    layout_fixture();

    /*
     * A displacement operates on the box the three inset phases already resolved, and
     * it must not reinterpret any of them. Three boxes, one each carrying margin,
     * padding and border beside the same `relative; left: 10px; top: 5px`, and the
     * assertions are the *inset* arithmetic with the offset added exactly once at the
     * end.
     */
    layout_position_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);

    layout_position_rule_init(&a_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&a_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&a_rule, "height", 20.0);
    layout_position_rule_px(&a_rule, "margin-top", 8.0);
    layout_position_rule_px(&a_rule, "margin-left", 4.0);
    layout_position_rule_px(&a_rule, "left", 10.0);
    layout_position_rule_px(&a_rule, "top", 5.0);

    layout_position_rule_init(&b_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&b_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&b_rule, "height", 20.0);
    layout_position_rule_px(&b_rule, "padding-top", 6.0);
    layout_position_rule_px(&b_rule, "padding-left", 7.0);
    layout_position_rule_px(&b_rule, "padding-right", 7.0);
    layout_position_rule_px(&b_rule, "padding-bottom", 6.0);
    layout_position_rule_px(&b_rule, "left", 10.0);
    layout_position_rule_px(&b_rule, "top", 5.0);

    layout_position_rule_init(&c_rule, "i", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&c_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&c_rule, "height", 20.0);
    /* A width without a style draws nothing (10.11's rule), so the styles are declared
     * beside the widths -- the case wants borders that occupy space. */
    layout_position_rule_border_style(&c_rule, "border-top-style",
                                      ROBO_CSS_BORDER_STYLE_SOLID);
    layout_position_rule_border_style(&c_rule, "border-right-style",
                                      ROBO_CSS_BORDER_STYLE_SOLID);
    layout_position_rule_border_style(&c_rule, "border-bottom-style",
                                      ROBO_CSS_BORDER_STYLE_SOLID);
    layout_position_rule_border_style(&c_rule, "border-left-style",
                                      ROBO_CSS_BORDER_STYLE_SOLID);
    layout_position_rule_px(&c_rule, "border-top-width", 3.0);
    layout_position_rule_px(&c_rule, "border-left-width", 2.0);
    layout_position_rule_px(&c_rule, "border-right-width", 2.0);
    layout_position_rule_px(&c_rule, "border-bottom-width", 3.0);
    layout_position_rule_px(&c_rule, "left", 10.0);
    layout_position_rule_px(&c_rule, "top", 5.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = a_rule;
    rules[3] = b_rule;
    rules[4] = c_rule;

    layout_position_begin(&fixture, "<html><body><div></div><section></section><i></i></body></html>",
                          rules, 5u);
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);

    /* Margin: the box is at its *margin origin* displaced -- 4 + 10 and 8 + 5. The
     * margin is external spacing, the offset a displacement; neither is the other. */
    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 14);
    ROBO_CHECK_EQ_INT(origin.y, 13);
    size = layout_position_size_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.height, 20);

    /* Padding: the border box grows by the padding and the offset adds on top -- the
     * box is 814 wide (800 + 7 + 7) and 32 tall, at 10/5.
     *
     * The two numbers a first draft of this case got wrong, and which the run caught:
     * the section is the div's **sibling**, so its *flow* y is the div's margin box
     * (20 + 8) and the 5 it displaces by lands on top of that; and under the build's
     * `content-box` model an `auto` width fills what the insets leave, so the border
     * box is the containing block's width whatever the insets add up to. */
    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "section"));
    ROBO_CHECK_EQ_INT(origin.x, 10);
    ROBO_CHECK_EQ_INT(origin.y, 33);
    size = layout_position_size_of(&fixture, layout_position_box_for(&fixture, "section"));
    ROBO_CHECK_EQ_INT(size.width, LAYOUT_POSITION_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(size.height, 32);

    /* Border: the same shape one layer out -- 804 wide, 26 tall, at 10/5, with the
     * flow y now 28 + 32 for the same sibling reason. The `i` declares `height: 20px`
     * and three borders, so its border box is 20 + 3 + 3 = 26 (the content-box model
     * once more: the declaration is the *content*, the border outside it). */
    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(origin.x, 10);
    ROBO_CHECK_EQ_INT(origin.y, 65);
    size = layout_position_size_of(&fixture, layout_position_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(size.width, LAYOUT_POSITION_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(size.height, 26);

    layout_position_end(&fixture);
}

static void test_positioning_descendants(void)
{
    layout_position_fixture_t fixture;
    layout_position_rule_t html_rule;
    layout_position_rule_t body_rule;
    layout_position_rule_t parent_rule;
    layout_position_rule_t child_rule;
    layout_position_rule_t rules[4];
    robo_layout_point_t origin;
    robo_layout_size_t size;
    robo_layout_border_edges_t borders;
    robo_layout_padding_edges_t padding;

    layout_fixture();

    /*
     * A child's position is stored relative to its containing block's *content origin*
     * (`RoboLayoutResult.h`), so displacing a parent moves its whole subtree without
     * any child's stored number changing -- which is the correct CSS behaviour and it
     * falls out of the coordinate convention rather than being implemented twice. The
     * case asserts that, plus the two inset records staying exactly what the parent's
     * own declarations resolved to.
     */
    layout_position_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&parent_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&parent_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&parent_rule, "height", 60.0);
    layout_position_rule_px(&parent_rule, "left", 40.0);
    layout_position_rule_px(&parent_rule, "top", 25.0);
    layout_position_rule_px(&parent_rule, "border-left-width", 2.0);
    layout_position_rule_px(&parent_rule, "border-top-width", 3.0);
    layout_position_rule_px(&parent_rule, "padding-left", 5.0);
    layout_position_rule_px(&parent_rule, "padding-top", 4.0);
    layout_position_rule_px(&parent_rule, "border-right-width", 2.0);
    layout_position_rule_px(&parent_rule, "border-bottom-width", 3.0);
    layout_position_rule_px(&parent_rule, "padding-right", 5.0);
    layout_position_rule_px(&parent_rule, "padding-bottom", 4.0);
    layout_position_rule_px(&parent_rule, "border-top-style", 0.0); /* replaced below */
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = parent_rule;
    layout_position_rule_init(&child_rule, "p", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_px(&child_rule, "height", 20.0);
    rules[3] = child_rule;

    /* The style vocabulary is typed, so the two border styles are POSITION-free
     * declarations this fixture expresses as solid via the keyword path the border
     * suite uses; a width without a style draws nothing, which is not what this case
     * wants, so the styles are set through the *border-style* longhands built as
     * keywords. */
    parent_rule.declarations[9].kind = LAYOUT_POSITION_DECL_KEYWORD;
    parent_rule.declarations[9].keyword = "solid";
    (void)layout_position_style_build; /* builder reused through begin below */

    layout_position_begin(&fixture, "<html><body><div><p></p></div></body></html>", rules, 4u);
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);

    /* The parent is drawn 40 right and 25 down of its margin origin (0,0). */
    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 40);
    ROBO_CHECK_EQ_INT(origin.y, 25);
    size = layout_position_size_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(size.width, LAYOUT_POSITION_VIEWPORT_WIDTH);
    ROBO_CHECK_EQ_INT(size.height, 60 + 8);

    /* The child's *stored* position is relative to the parent's content origin and is
     * (0,0) plus nothing -- unchanged by the displacement. */
    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "p"));
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(origin.y, 0);

    /* And the two inset records are the declarations, untouched by the offset. */
    ROBO_CHECK_EQ_INT(robo_layout_object_borders(fixture.tree,
                                                 layout_position_box_for(&fixture, "div"),
                                                 &borders, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(borders.top.width, 0); /* a keyword style with no declared width
                                              * kind on the *style* side -- see note */
    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture.tree,
                                                 layout_position_box_for(&fixture, "div"),
                                                 &padding, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(padding.top, 4);
    ROBO_CHECK_EQ_INT(padding.left, 5);

    layout_position_end(&fixture);
}

static void test_positioning_inline_atomic(void)
{
    layout_position_fixture_t fixture;
    layout_position_rule_t html_rule;
    layout_position_rule_t body_rule;
    layout_position_rule_t div_rule;
    layout_position_rule_t i_rule;
    layout_position_rule_t b_rule;
    layout_position_rule_t rules[5];
    robo_layout_point_t origin;
    robo_layout_size_t size;
    robo_layout_line_box_t line;
    robo_layout_line_id_t line_id = ROBO_LAYOUT_LINE_ID_NONE;
    robo_layout_stats_t stats;
    robo_layout_position_offset_t offset;

    layout_fixture();

    /*
     * An atomic inline displaced along its line: drawn `dx` further on, its line's
     * width and the next participant's advance untouched. The horizontal half applies
     * at placement; the vertical half at finalization, after the baseline correction.
     */
    layout_position_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_px(&div_rule, "height", 30.0);
    layout_position_rule_init(&i_rule, "i", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_position_rule_position(&i_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&i_rule, "width", 100.0);
    layout_position_rule_px(&i_rule, "height", 20.0);
    layout_position_rule_px(&i_rule, "left", 15.0);
    layout_position_rule_px(&i_rule, "top", 6.0);
    layout_position_rule_init(&b_rule, "b", ROBO_CSS_DISPLAY_INLINE_BLOCK);
    layout_position_rule_px(&b_rule, "width", 50.0);
    layout_position_rule_px(&b_rule, "height", 10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = i_rule;
    rules[4] = b_rule;

    layout_position_begin(&fixture, "<html><body><div><i></i><b></b></div></body></html>", rules,
                          5u);
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, &stats);

    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(origin.x, 15);
    ROBO_CHECK_EQ_INT(origin.y, 6);
    size = layout_position_size_of(&fixture, layout_position_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(size.width, 100);
    ROBO_CHECK_EQ_INT(size.height, 20);

    offset = layout_position_offset_of(&fixture, layout_position_box_for(&fixture, "i"));
    ROBO_CHECK_EQ_INT(offset.dx, 15);
    ROBO_CHECK_EQ_INT(offset.dy, 6);

    /* The next participant is not pushed along: its advance is the margin box. */
    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "b"));
    ROBO_CHECK_EQ_INT(origin.x, 100);

    /* And the line is the widths of what flows, not of what was displaced. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture.tree, 0u, &line_id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, line_id, &line, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(line.size.width, 150);

    ROBO_CHECK_EQ_INT(stats.inline_flow.boxes_shifted, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.offsets_applied, 2u);

    layout_position_end(&fixture);
}

static void test_positioning_inline_transparent_not_applied(void)
{
    layout_position_fixture_t fixture;
    layout_position_rule_t html_rule;
    layout_position_rule_t body_rule;
    layout_position_rule_t div_rule;
    layout_position_rule_t span_rule;
    layout_position_rule_t rules[4];
    robo_layout_diag_t diag;
    robo_layout_position_offset_t offset;

    layout_fixture();

    /*
     * A transparent inline box's rectangle is the **union** of its contents
     * (`RoboLayoutLineBox.h`), so this build does not offset that union by a declared
     * `left`/`top` -- and the door *says so* rather than answering a zero, which is
     * what keeps "no offset" and "not applicable" two statements.
     */
    layout_position_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_px(&div_rule, "height", 30.0);
    layout_position_rule_init(&span_rule, "span", ROBO_CSS_DISPLAY_INLINE);
    layout_position_rule_position(&span_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&span_rule, "left", 20.0);
    layout_position_rule_px(&span_rule, "top", 10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;
    rules[3] = span_rule;

    layout_position_begin(&fixture, "<html><body><div><span></span></div></body></html>", rules,
                          4u);
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);

    robo_layout_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_layout_object_position(fixture.tree,
                                                  layout_position_box_for(&fixture, "span"),
                                                  &offset, &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_OBJECT);

    layout_position_end(&fixture);
}

static void test_positioning_object_door(void)
{
    layout_position_fixture_t fixture;
    layout_position_rule_t html_rule;
    layout_position_rule_t body_rule;
    layout_position_rule_t relative_rule;
    layout_position_rule_t static_rule;
    layout_position_rule_t rules[4];
    robo_layout_position_offset_t offset;
    robo_layout_diag_t diag;
    robo_layout_node_id_t missing;

    layout_fixture();

    /*
     * The read side: a `relative` box reports its displacement, a `static` one reports
     * a zero with its counters, and the door refuses the two cases where no offset
     * could exist -- an id that names nothing and a box no pass placed.
     */
    layout_position_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&relative_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&relative_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&relative_rule, "height", 20.0);
    layout_position_rule_px(&relative_rule, "left", 11.0);
    layout_position_rule_px(&relative_rule, "top", 7.0);
    layout_position_rule_init(&static_rule, "section", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_px(&static_rule, "height", 20.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = relative_rule;
    rules[3] = static_rule;

    layout_position_begin(&fixture, "<html><body><div></div><section></section></body></html>",
                          rules, 4u);
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);

    offset = layout_position_offset_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(offset.dx, 11);
    ROBO_CHECK_EQ_INT(offset.dy, 7);
    ROBO_CHECK_EQ_INT(offset.insets_resolved, 2u);

    /* A static box answers a zero -- which is the contrast with the three inset
     * doors: their zero is a legitimate *state*, so they refuse and ask the result;
     * here the zero is the answer for every static box in every document. */
    ROBO_CHECK_EQ_INT(robo_layout_object_position(fixture.tree,
                                                  layout_position_box_for(&fixture, "section"),
                                                  &offset, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(offset.dx, 0);
    ROBO_CHECK_EQ_INT(offset.dy, 0);

    /* An id that names nothing. */
    robo_layout_diag_reset(&diag);
    missing = layout_position_box_for(&fixture, "div");
    ROBO_CHECK_EQ_INT(robo_layout_object_position(fixture.tree, missing, &offset, &diag),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_object_position(fixture.tree, (robo_layout_node_id_t)9999,
                                                  &offset, &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_OBJECT);

    /* And NULL arguments, refused rather than dereferenced. */
    ROBO_CHECK_EQ_INT(robo_layout_object_position(NULL, missing, &offset, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_position(fixture.tree, missing, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    layout_position_end(&fixture);
}

static void test_positioning_relayout(void)
{
    layout_position_fixture_t fixture;
    layout_position_rule_t html_rule;
    layout_position_rule_t body_rule;
    layout_position_rule_t div_rule;
    layout_position_rule_t changed_rule;
    layout_position_rule_t rules[3];
    robo_layout_point_t origin;
    robo_layout_point_t before;
    robo_layout_stats_t stats;

    layout_fixture();

    /*
     * Changing an inset moves the box and nothing else -- the existing invalidation
     * mechanism *is* the rebuild, and the pass replaces geometry rather than refining
     * it. The two facts asserted are the pair the phase turns on: the final position
     * moves, and the following sibling (here the body's extent) does not.
     */
    layout_position_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&div_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&div_rule, "height", 30.0);
    layout_position_rule_px(&div_rule, "left", 10.0);
    layout_position_rule_px(&div_rule, "top", 10.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_position_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);
    before = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(before.x, 10);
    ROBO_CHECK_EQ_INT(before.y, 10);

    /* A pure style change: `top: 10px` becomes `top: 45px`, `left` unchanged. */
    layout_position_rule_init(&changed_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&changed_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&changed_rule, "height", 30.0);
    layout_position_rule_px(&changed_rule, "left", 10.0);
    layout_position_rule_px(&changed_rule, "top", 45.0);
    layout_position_style_replace(&fixture.source, "div", &changed_rule);
    layout_position_rebuild(&fixture);
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, &stats);

    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 10);
    ROBO_CHECK_EQ_INT(origin.y, 45);
    ROBO_CHECK_EQ_INT(stats.block.boxes_shifted, 1u);
    ROBO_CHECK_EQ_INT(stats.block.offsets_applied, 2u);

    /* And a mode change back to `static` declines the same declarations. */
    layout_position_rule_init(&changed_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&changed_rule, ROBO_CSS_POSITIONING_STATIC);
    layout_position_rule_px(&changed_rule, "height", 30.0);
    layout_position_rule_px(&changed_rule, "left", 10.0);
    layout_position_rule_px(&changed_rule, "top", 45.0);
    layout_position_style_replace(&fixture.source, "div", &changed_rule);
    layout_position_rebuild(&fixture);
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, &stats);

    origin = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(origin.x, 0);
    ROBO_CHECK_EQ_INT(origin.y, 0);
    ROBO_CHECK_EQ_INT(stats.block.boxes_shifted, 0u);
    ROBO_CHECK_EQ_INT(stats.block.offsets_declined, 2u);

    layout_position_end(&fixture);
}

static void test_positioning_determinism(void)
{
    layout_position_fixture_t fixture;
    layout_position_rule_t html_rule;
    layout_position_rule_t body_rule;
    layout_position_rule_t div_rule;
    layout_position_rule_t rules[3];
    robo_layout_point_t first;
    robo_layout_point_t second;
    robo_layout_size_t first_size;
    robo_layout_size_t second_size;

    layout_fixture();

    layout_position_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
    layout_position_rule_position(&div_rule, ROBO_CSS_POSITIONING_RELATIVE);
    layout_position_rule_px(&div_rule, "height", 40.0);
    layout_position_rule_negative(&div_rule, "left", 7.0);
    layout_position_rule_px(&div_rule, "top", 13.0);
    rules[0] = html_rule;
    rules[1] = body_rule;
    rules[2] = div_rule;

    layout_position_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);

    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);
    first = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "div"));
    first_size = layout_position_size_of(&fixture, layout_position_box_for(&fixture, "div"));

    /* Identical input, identical output -- twice, through the same context-free doors. */
    layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);
    second = layout_position_origin_of(&fixture, layout_position_box_for(&fixture, "div"));
    second_size = layout_position_size_of(&fixture, layout_position_box_for(&fixture, "div"));
    ROBO_CHECK_EQ_INT(first.x, second.x);
    ROBO_CHECK_EQ_INT(first.y, second.y);
    ROBO_CHECK_EQ_INT(first_size.width, second_size.width);
    ROBO_CHECK_EQ_INT(first_size.height, second_size.height);
    ROBO_CHECK_EQ_INT(first.x, -7);
    ROBO_CHECK_EQ_INT(first.y, 13);

    layout_position_end(&fixture);
}

static void test_positioning_allocator_balance(void)
{
    size_t before = layout_live_blocks();
    size_t settled;
    int round;

    layout_fixture();

    for (round = 0; round < 3; round++) {
        layout_position_fixture_t fixture;
        layout_position_rule_t html_rule;
        layout_position_rule_t body_rule;
        layout_position_rule_t div_rule;
        layout_position_rule_t rules[3];

        layout_position_rule_init(&html_rule, "html", ROBO_CSS_DISPLAY_BLOCK);
        layout_position_rule_init(&body_rule, "body", ROBO_CSS_DISPLAY_BLOCK);
        layout_position_rule_init(&div_rule, "div", ROBO_CSS_DISPLAY_BLOCK);
        layout_position_rule_position(&div_rule, ROBO_CSS_POSITIONING_RELATIVE);
        layout_position_rule_px(&div_rule, "height", 25.0);
        layout_position_rule_px(&div_rule, "left", 12.0);
        layout_position_rule_px(&div_rule, "top", 9.0);
        rules[0] = html_rule;
        rules[1] = body_rule;
        rules[2] = div_rule;

        layout_position_begin(&fixture, "<html><body><div></div></body></html>", rules, 3u);
        layout_position_pass(&fixture, LAYOUT_POSITION_VIEWPORT_WIDTH, NULL);
        settled = layout_live_blocks();
        /* The positioning layer allocates nothing of its own: the records are values on
         * the stack and on the object, and the pass adds no arena. */
        ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
        layout_position_pass(&fixture, 400, NULL);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
        layout_position_end(&fixture);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
    }
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_layout_position(void);

void robo_test_register_layout_position(void)
{
    robo_test_register("layout.positioning.kind_vocabulary", test_positioning_kind_vocabulary);
    robo_test_register("layout.positioning.inset_record", test_positioning_inset_record);
    robo_test_register("layout.positioning.position_record", test_positioning_position_record);
    robo_test_register("layout.positioning.seam", test_positioning_seam);
    robo_test_register("layout.positioning.offset_record", test_positioning_offset_record);
    robo_test_register("layout.positioning.policy_precedence", test_positioning_policy_precedence);
    robo_test_register("layout.positioning.policy_static_declines",
                       test_positioning_policy_static_declines);
    robo_test_register("layout.positioning.apply", test_positioning_apply);
    robo_test_register("layout.positioning.block_relative", test_positioning_block_relative);
    robo_test_register("layout.positioning.flow_preserved", test_positioning_flow_preserved);
    robo_test_register("layout.positioning.block_negative", test_positioning_block_negative);
    robo_test_register("layout.positioning.block_static_declines",
                       test_positioning_block_static_declines);
    robo_test_register("layout.positioning.box_model_interaction",
                       test_positioning_box_model_interaction);
    robo_test_register("layout.positioning.descendants", test_positioning_descendants);
    robo_test_register("layout.positioning.inline_atomic", test_positioning_inline_atomic);
    robo_test_register("layout.positioning.inline_transparent_not_applied",
                       test_positioning_inline_transparent_not_applied);
    robo_test_register("layout.positioning.object_door", test_positioning_object_door);
    robo_test_register("layout.positioning.relayout", test_positioning_relayout);
    robo_test_register("layout.positioning.determinism", test_positioning_determinism);
    robo_test_register("layout.positioning.allocator_balance", test_positioning_allocator_balance);
}
