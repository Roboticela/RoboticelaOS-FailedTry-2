/*
 * RoboLayout line-box suite (Phase 10.6).
 *
 * Three things are under test and they are told apart deliberately:
 *
 *   the **vocabulary** -- the state pair, the record, and the rectangle formed from
 *   it -- which is pure and needs no tree at all;
 *
 *   the **arena**: a line is a record the tree owns, released by the next pass and by
 *   anything that releases the classification, and every read door validates its id;
 *
 *   the **pass**: lines are opened when the first participant lands, finalized at a
 *   wrap and at the end of their context, and the block extent of an inline
 *   formatting context *is* the bottom edge of its last line.
 *
 * The one thing the suite asserts repeatedly, because it is the phase's central
 * distinction: a line's **members** are the objects the line *placed*, which is a
 * smaller set than the context's participants. A transparent inline box with contents
 * is a participant and a member of no single line, and a case that could not tell those
 * apart would pass while the line's width double-counted its children.
 *
 * Geometry comes from two scripted sources and neither is a font: a per-code-point
 * measurer for the arithmetic, and a declared width for the atomic cases. Every number
 * a case asserts is therefore derivable from the fixture rather than from a face, which
 * is the same discipline the 10.5 suite records.
 */

#include "RoboLayoutTestSupport.h"

#include "RoboFont/RoboFont.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A scripted measurer                                                 */
/* ------------------------------------------------------------------ */

/** One scripted answer: this many px per code point, and this line extent. */
typedef struct layout_line_script {
    int32_t per_code_point;
    int32_t height;
    size_t calls;
} layout_line_script_t;

/**
 * The measurer: `per_code_point` px per code point, and a fixed line extent.
 *
 * It fills the whole run record -- source, range, direction, extents, code-point count
 * -- because a measurer that filled only a width would let this suite pass while the
 * record it produced was half-empty, and the record is part of the surface 10.5 froze.
 * `height` is what a case varies when it wants to see a line's height track its tallest
 * member, and it is deliberately *not* per code point: the height belongs to the run,
 * not to the world.
 */
static robo_result_t layout_line_script_measure(void *userdata,
                                               const robo_layout_text_request_t *request,
                                               robo_layout_text_run_t *out_run)
{
    layout_line_script_t *script = (layout_line_script_t *)userdata;

    if (!script || !request || !out_run || !request->style) {
        return ROBO_INVALID_ARGUMENT;
    }
    script->calls++;
    out_run->source = request->text_node;
    out_run->range = request->range;
    out_run->direction = request->style->direction;
    out_run->code_points = robo_layout_text_range_length(request->range);
    out_run->width = (int32_t)out_run->code_points * script->per_code_point;
    out_run->height = script->height;
    out_run->ascent = script->height;
    out_run->descent = 0;
    return ROBO_OK;
}

static robo_layout_text_metrics_t layout_line_script_metrics(layout_line_script_t *script)
{
    robo_layout_text_metrics_t metrics;

    robo_layout_text_metrics_init(&metrics);
    metrics.userdata = script;
    metrics.measure = layout_line_script_measure;
    return metrics;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/**
 * One scripted answer: an element with this tag computes to this.
 *
 * Wider than the text suite's rule because a line's *geometry* is what this phase is
 * about and two of the cases need a width the pass can resolve without a font: a
 * declared `width` and `height` in `px`, which is the one shape 10.3 and 10.4 both read
 * as a computed length (RoboLayoutInternal.h). Zero means "declared nothing", which is
 * also an honest width for an empty box -- and the cases that care write an explicit
 * non-zero one.
 */
typedef struct layout_line_rule {
    const char *tag;
    robo_css_display_t display;
    double width_px;
    double height_px;
} layout_line_rule_t;

#define LAYOUT_LINE_MAX_RULES 8u

typedef struct layout_line_style {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
} layout_line_style_t;

typedef struct layout_line_fixture {
    layout_document_t doc;
    layout_line_style_t rules[LAYOUT_LINE_MAX_RULES];
    size_t rule_count;
    robo_layout_style_source_t styles;
    robo_layout_tree_t *tree;
} layout_line_fixture_t;

static const robo_css_computed_style_t *layout_line_style_lookup(
    void *userdata,
    const robo_html_node_handle_t *element)
{
    layout_line_fixture_t *fixture = (layout_line_fixture_t *)userdata;
    const char *name;
    size_t i;

    if (!fixture || !element) {
        return NULL;
    }
    name = robo_html_element_name(element, NULL);
    if (!name || name[0] == '\0') {
        return NULL;
    }
    for (i = 0u; i < fixture->rule_count; i++) {
        if (strcmp(fixture->rules[i].tag, name) == 0) {
            return fixture->rules[i].style;
        }
    }
    /* Nothing declared: a box with no style, whose display is the property's initial
     * value. Same rule every other layout suite states. */
    return NULL;
}

/** Add a computed style for @p rule's tag, with the entries the rule asked for. */
static void layout_line_style_add(layout_line_fixture_t *fixture,
                                  const layout_line_rule_t *rule)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;
    size_t existing;

    layout_fixture();
    ROBO_CHECK_EQ_INT(fixture->rule_count < LAYOUT_LINE_MAX_RULES, 1);
    for (existing = 0u; existing < fixture->rule_count; existing++) {
        ROBO_CHECK(strcmp(fixture->rules[existing].tag, rule->tag) != 0);
    }
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_make_display(&value, rule->display, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                       robo_css_slice_from_text("display"),
                                                       0u, &specified, &value, NULL),
                      ROBO_OK);
    /* A *real* computed LENGTH, through 9.8's own maker: the pass resolves a declared
     * size by asking for a LENGTH in px (RoboLayoutInternal.h), so a suite that wrote
     * the value by hand would be testing a struct rather than the rule. */
    if (rule->width_px > 0.0) {
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, rule->width_px,
                                                             ROBO_CSS_UNIT_PX, 0, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                           robo_css_slice_from_text("width"),
                                                           0u, &specified, &value, NULL),
                          ROBO_OK);
    }
    if (rule->height_px > 0.0) {
        ROBO_CHECK_EQ_INT(robo_css_computed_value_make_length(&value, rule->height_px,
                                                             ROBO_CSS_UNIT_PX, 0, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_computed_style_set_entry(style, ROBO_CSS_PROPERTY_ID_NONE,
                                                           robo_css_slice_from_text("height"),
                                                           0u, &specified, &value, NULL),
                          ROBO_OK);
    }
    strncpy(fixture->rules[fixture->rule_count].tag, rule->tag,
            sizeof(fixture->rules[fixture->rule_count].tag) - 1u);
    fixture->rules[fixture->rule_count].tag[sizeof(fixture->rules[fixture->rule_count].tag) -
                                            1u] = '\0';
    fixture->rules[fixture->rule_count].style = style;
    fixture->rule_count++;
}

static void layout_line_begin(layout_line_fixture_t *fixture,
                              const char *html,
                              const layout_line_rule_t *rules,
                              size_t count)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    for (index = 0u; index < count; index++) {
        layout_line_style_add(fixture, &rules[index]);
    }
    robo_layout_style_source_init(&fixture->styles);
    fixture->styles.userdata = fixture;
    fixture->styles.computed_style_for = layout_line_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_line_end(layout_line_fixture_t *fixture)
{
    size_t index;

    robo_layout_tree_destroy(fixture->tree);
    fixture->tree = NULL;
    for (index = 0u; index < fixture->rule_count; index++) {
        robo_css_computed_style_destroy(fixture->rules[index].style);
        fixture->rules[index].style = NULL;
    }
    fixture->rule_count = 0u;
    layout_document_release(&fixture->doc);
}

/** One pass, with a viewport and optionally a measurer installed on the context. */
static void layout_line_pass(layout_line_fixture_t *fixture,
                             int32_t viewport_width,
                             const robo_layout_text_metrics_t *metrics,
                             robo_layout_stats_t *out_stats)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;

    viewport.width = viewport_width;
    viewport.height = 600;
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

/** A viewport wider than any fixture's content, so a case only wraps on purpose. */
#define LAYOUT_LINE_WIDE_VIEWPORT 800

/** The block container every fixture below shares: `<html><body><p>`. */
#define LAYOUT_LINE_BLOCK_RULES                                                 \
    { "html", ROBO_CSS_DISPLAY_BLOCK, 0.0, 0.0 },                               \
    { "body", ROBO_CSS_DISPLAY_BLOCK, 0.0, 0.0 },                               \
    { "p", ROBO_CSS_DISPLAY_BLOCK, 0.0, 0.0 }

/** The box built for the first element named @p tag, asserted to exist. */
static robo_layout_node_id_t layout_line_box_of(layout_line_fixture_t *fixture,
                                                const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);
    robo_layout_node_id_t id;

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&element), 1);
    id = robo_layout_object_id_for_dom_node(fixture->tree, &element);
    ROBO_CHECK(id != ROBO_LAYOUT_NODE_ID_NONE);
    return id;
}

/** The first **text node** inside the first element named @p tag. */
static robo_html_node_handle_t layout_line_first_text_child(
    const robo_html_node_handle_t *element)
{
    robo_html_node_view_t view;
    robo_html_node_handle_t child;

    robo_html_node_handle_clear(&child);
    if (!element || !robo_html_node_handle_is_wellformed(element)) {
        return child;
    }
    if (robo_html_node_get_view(element, &view, NULL) != ROBO_OK) {
        return child;
    }
    child = view.first_child;
    while (robo_html_node_handle_is_wellformed(&child)) {
        robo_html_node_view_t child_view;

        if (robo_html_node_get_view(&child, &child_view, NULL) != ROBO_OK) {
            break;
        }
        if (child_view.kind == ROBO_HTML_NODE_KIND_TEXT) {
            return child;
        }
        child = child_view.next_sibling;
    }
    robo_html_node_handle_clear(&child);
    return child;
}

/** The layout object the first text node inside the element @p tag became. */
static robo_layout_node_id_t layout_line_text_of(layout_line_fixture_t *fixture,
                                                 const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);
    robo_html_node_handle_t node = layout_line_first_text_child(&element);

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&node), 1);
    return robo_layout_object_id_for_dom_node(fixture->tree, &node);
}

/** Read one line, asserted to exist. */
static robo_layout_line_box_t layout_line_get(layout_line_fixture_t *fixture,
                                              robo_layout_line_id_t id)
{
    robo_layout_line_box_t line;

    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture->tree, id, &line, NULL), ROBO_OK);
    return line;
}

/** The id of the @p index'th line, asserted to exist. */
static robo_layout_line_id_t layout_line_at(layout_line_fixture_t *fixture, size_t index)
{
    robo_layout_line_id_t id = ROBO_LAYOUT_LINE_ID_NONE;

    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture->tree, index, &id, NULL), ROBO_OK);
    return id;
}

/** Assert one record's geometry and identity, field by field. */
static void layout_line_expect(const robo_layout_line_box_t *line,
                               robo_layout_node_id_t owner,
                               size_t index,
                               int32_t x,
                               int32_t y,
                               int32_t width,
                               int32_t height)
{
    ROBO_CHECK_EQ_INT((int)line->state, (int)ROBO_LAYOUT_LINE_FINAL);
    ROBO_CHECK_EQ_INT((int)line->owner, (int)owner);
    ROBO_CHECK_EQ_INT(line->index, index);
    ROBO_CHECK_EQ_INT(line->origin.x, x);
    ROBO_CHECK_EQ_INT(line->origin.y, y);
    ROBO_CHECK_EQ_INT(line->size.width, width);
    ROBO_CHECK_EQ_INT(line->size.height, height);
    ROBO_CHECK_EQ_INT(line->context != ROBO_LAYOUT_FC_ID_NONE, 1);
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void test_line_state_vocabulary(void)
{
    size_t index;

    layout_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_LINE_STATE_COUNT, 2);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_LINE_OPEN, 0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_LINE_FINAL, 1);
    ROBO_CHECK_EQ_INT(robo_layout_line_state_is_known(ROBO_LAYOUT_LINE_OPEN), 1);
    ROBO_CHECK_EQ_INT(robo_layout_line_state_is_known(ROBO_LAYOUT_LINE_FINAL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_line_state_is_known((robo_layout_line_state_t)2), 0);
    ROBO_CHECK_EQ_INT(robo_layout_line_state_is_known((robo_layout_line_state_t)99), 0);

    /* Walked, so a table whose rows had drifted would still answer is_known for every
     * one and show only as a name that printed as another's keyword. */
    for (index = 0u; index < ROBO_LAYOUT_LINE_STATE_COUNT; index++) {
        robo_layout_line_state_t state = (robo_layout_line_state_t)index;

        ROBO_CHECK_EQ_INT(robo_layout_line_state_is_known(state), 1);
        ROBO_CHECK(robo_layout_line_state_name(state) != NULL);
    }
    ROBO_CHECK_STR_EQ(robo_layout_line_state_name(ROBO_LAYOUT_LINE_OPEN), "open");
    ROBO_CHECK_STR_EQ(robo_layout_line_state_name(ROBO_LAYOUT_LINE_FINAL), "final");
    ROBO_CHECK_STR_EQ(robo_layout_line_state_name((robo_layout_line_state_t)7), "unknown");
    ROBO_CHECK(robo_layout_line_state_name((robo_layout_line_state_t)7) != NULL);
}

static void test_line_record(void)
{
    robo_layout_line_box_t line;
    robo_layout_line_box_t other;
    robo_layout_rect_t rect;

    layout_fixture();
    memset(&line, 0xFF, sizeof(line));
    robo_layout_line_box_init(&line);
    ROBO_CHECK_EQ_INT((int)line.id, (int)ROBO_LAYOUT_LINE_ID_NONE);
    ROBO_CHECK_EQ_INT((int)line.state, (int)ROBO_LAYOUT_LINE_OPEN);
    ROBO_CHECK_EQ_INT((int)line.context, (int)ROBO_LAYOUT_FC_ID_NONE);
    ROBO_CHECK_EQ_INT((int)line.owner, (int)ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT(line.index, 0u);
    ROBO_CHECK_EQ_INT(line.origin.x, 0);
    ROBO_CHECK_EQ_INT(line.origin.y, 0);
    ROBO_CHECK_EQ_INT(line.size.width, 0);
    ROBO_CHECK_EQ_INT(line.size.height, 0);
    ROBO_CHECK_EQ_INT(line.available_width, 0);
    ROBO_CHECK_EQ_INT(line.members, 0u);

    /* Init is safe on NULL, and a cleared record equals a cleared record -- which is
     * the assertion that says init wrote every field rather than most of them. */
    robo_layout_line_box_init(NULL);
    other = line;
    ROBO_CHECK_EQ_INT(robo_layout_line_box_equal(&line, &other), 1);
    other.size.height = 1;
    ROBO_CHECK_EQ_INT(robo_layout_line_box_equal(&line, &other), 0);
    /* NULL compares by identity, so two NULLs agree and one NULL does not. */
    ROBO_CHECK_EQ_INT(robo_layout_line_box_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_equal(&line, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_equal(NULL, &line), 0);

    /* The rectangle, formed through RoboGraphics rather than by hand. */
    line.origin.x = 3;
    line.origin.y = 7;
    line.size.width = 40;
    line.size.height = 12;
    ROBO_CHECK_EQ_INT(robo_layout_line_box_rect(&line, &rect), ROBO_OK);
    ROBO_CHECK_EQ_INT(rect.x, 3);
    ROBO_CHECK_EQ_INT(rect.y, 7);
    ROBO_CHECK_EQ_INT(rect.width, 40);
    ROBO_CHECK_EQ_INT(rect.height, 12);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_rect(&line, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_rect(NULL, &rect), ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* One line                                                            */
/* ------------------------------------------------------------------ */

/**
 * A paragraph holding one measured run: one line, one member, and the run is it.
 *
 * The shortest case that says what a line box *is*: `AB` at ten px per code point is
 * twenty px wide, so the line is twenty wide and as tall as the run; it is owned by the
 * paragraph; its `index` is 0; and the object back-link agrees with the member list,
 * which is the pair that has to agree for a caller to move between the two views.
 */
static void test_line_single_run(void)
{
    layout_line_fixture_t fixture;
    static const layout_line_rule_t rules[] = { LAYOUT_LINE_BLOCK_RULES };
    layout_line_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_stats_t stats;
    robo_layout_line_box_t line;
    robo_layout_line_id_t id;
    robo_layout_line_id_t back;
    robo_layout_node_id_t member = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_node_id_t p_box;
    robo_layout_node_id_t text_box;

    layout_fixture();
    layout_line_begin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);
    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.height = 20;
    metrics = layout_line_script_metrics(&script);

    /* Before a pass: no lines, and the door says so rather than inventing one. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture.tree, 0u, &id, NULL), ROBO_NOT_FOUND);

    layout_line_pass(&fixture, LAYOUT_LINE_WIDE_VIEWPORT, &metrics, &stats);

    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.line_members, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 0u);

    p_box = layout_line_box_of(&fixture, "p");
    text_box = layout_line_text_of(&fixture, "p");
    id = layout_line_at(&fixture, 0u);
    ROBO_CHECK_EQ_INT((int)id, 1);
    line = layout_line_get(&fixture, id);
    layout_line_expect(&line, p_box, 0u, 0, 0, 20, 20);
    ROBO_CHECK_EQ_INT(line.available_width, LAYOUT_LINE_WIDE_VIEWPORT);
    ROBO_CHECK_EQ_INT(line.members, 1u);

    /* The member is the text object, and the back-link names the same line. */
    ROBO_CHECK_EQ_INT(robo_layout_line_member(fixture.tree, id, 0u, &member, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)member, (int)text_box);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_kind(fixture.tree, member),
                      (int)ROBO_LAYOUT_OBJECT_TEXT);
    ROBO_CHECK_EQ_INT(robo_layout_object_line(fixture.tree, text_box, &back, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)back, (int)id);

    /* The line's width *is* the member's width, which is what makes it a sum rather
     * than a parallel number that happens to agree. */
    {
        robo_layout_result_t result;
        robo_layout_size_t size;

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, text_box, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
        ROBO_CHECK_EQ_INT(size.width, line.size.width);
        ROBO_CHECK_EQ_INT(size.height, line.size.height);
    }

    layout_line_end(&fixture);
}

/**
 * Members are the *placements*: a transparent inline box with contents is not one.
 *
 * The phase's central distinction, and the case is built so that getting it wrong
 * changes a number: `<p><span>ab</span>cd</p>` has three participants of the context --
 * the span, its run and the run after it -- and two members, because the span's own
 * rectangle is the union of what it contains. The line's width is therefore 40 (the two
 * runs) and not 60, and a build that counted the span would produce the second number
 * while every other assertion in this suite still passed.
 *
 * The empty `span` in the second half is the mirror image and *is* a member: it has no
 * contents, so it is placed as a unit of its own -- zero wide, and it advances the
 * cursor by nothing.
 */
static void test_line_members_are_placements(void)
{
    layout_line_fixture_t fixture;
    static const layout_line_rule_t rules[] = {
        LAYOUT_LINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 }
    };
    layout_line_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_stats_t stats;
    robo_layout_line_box_t line;
    robo_layout_node_id_t span_box;
    robo_layout_node_id_t span_line;
    robo_layout_node_id_t member = ROBO_LAYOUT_NODE_ID_NONE;

    layout_fixture();
    layout_line_begin(&fixture, "<html><body><p><span>ab</span>cd</p></body></html>",
                      rules, 4u);
    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.height = 20;
    metrics = layout_line_script_metrics(&script);
    layout_line_pass(&fixture, LAYOUT_LINE_WIDE_VIEWPORT, &metrics, &stats);

    /* Three participants, two members: the transparent box encloses a placement. */
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 3u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.line_members, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    line = layout_line_get(&fixture, layout_line_at(&fixture, 0u));
    ROBO_CHECK_EQ_INT(line.members, 2u);
    ROBO_CHECK_EQ_INT(line.size.width, 40);
    ROBO_CHECK_EQ_INT(line.size.height, 20);

    /* And the two members are the two *runs*, in layout order -- the span's own run
     * first, then the one after the span. */
    {
        robo_layout_node_id_t first = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_node_id_t second = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_result_t result;
        robo_layout_point_t position;

        ROBO_CHECK_EQ_INT(robo_layout_line_member(fixture.tree, line.id, 0u, &first, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_line_member(fixture.tree, line.id, 1u, &second, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_layout_object_kind(fixture.tree, first),
                          (int)ROBO_LAYOUT_OBJECT_TEXT);
        ROBO_CHECK_EQ_INT((int)robo_layout_object_kind(fixture.tree, second),
                          (int)ROBO_LAYOUT_OBJECT_TEXT);
        ROBO_CHECK_EQ_INT(first != second, 1);

        /* Placed one after the other: the second run starts where the first ended. */
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, second, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
        ROBO_CHECK_EQ_INT(position.x, 20);

        /* The span is a participant and on *no* line -- the documented miss. */
        span_box = layout_line_box_of(&fixture, "span");
        ROBO_CHECK_EQ_INT(robo_layout_object_line(fixture.tree, span_box, &span_line, NULL),
                          ROBO_NOT_FOUND);
        /* And it is still a real box with a real rectangle: the union of its contents. */
        {
            robo_layout_result_t span_result;
            robo_layout_size_t span_size;

            ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, span_box, &span_result),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_layout_result_size(&span_result, &span_size), ROBO_OK);
            ROBO_CHECK_EQ_INT(span_size.width, 20);
        }
    }
    ROBO_CHECK_EQ_INT(robo_layout_line_member(fixture.tree, line.id, 2u, &member, NULL),
                      ROBO_NOT_FOUND);

    layout_line_end(&fixture);
}

static void test_line_empty_span_is_a_member(void)
{
    layout_line_fixture_t fixture;
    static const layout_line_rule_t rules[] = {
        LAYOUT_LINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 }
    };
    layout_line_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_stats_t stats;
    robo_layout_line_box_t line;
    robo_layout_node_id_t span_box;
    robo_layout_node_id_t first = ROBO_LAYOUT_NODE_ID_NONE;

    layout_fixture();
    layout_line_begin(&fixture, "<html><body><p><span></span>xy</p></body></html>", rules, 4u);
    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.height = 20;
    metrics = layout_line_script_metrics(&script);
    layout_line_pass(&fixture, LAYOUT_LINE_WIDE_VIEWPORT, &metrics, &stats);

    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 2u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.line_members, 2u);
    line = layout_line_get(&fixture, layout_line_at(&fixture, 0u));
    /* Two members of which one is zero wide: the width is the run's alone, and the
     * zero-width member did not move the cursor. */
    ROBO_CHECK_EQ_INT(line.members, 2u);
    ROBO_CHECK_EQ_INT(line.size.width, 20);
    ROBO_CHECK_EQ_INT(robo_layout_line_member(fixture.tree, line.id, 0u, &first, NULL), ROBO_OK);
    span_box = layout_line_box_of(&fixture, "span");
    ROBO_CHECK_EQ_INT((int)first, (int)span_box);
    {
        robo_layout_line_id_t back = ROBO_LAYOUT_LINE_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_layout_object_line(fixture.tree, span_box, &back, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)back, (int)line.id);
    }

    layout_line_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Wrapping                                                            */
/* ------------------------------------------------------------------ */

/**
 * Three atomic inlines a hundred px wide, in a viewport that holds two, then one.
 *
 * The wrap is the whole subject, and the case runs it twice with the *same* fixture
 * so that the two answers differ only in the viewport: at 250 the first line holds two
 * boxes and the second holds one, and at 150 each line holds one. That is what makes
 * the assertion about the algorithm rather than about a lucky width -- and the y
 * progression is asserted in both, because a line's position is the sum of the heights
 * above it and nothing else.
 *
 * The `index` fields are the other half: they count lines *within the context*, so a
 * build that numbered lines globally would pass the geometry assertions and fail here.
 */
static void test_line_wrapping(void)
{
    layout_line_fixture_t fixture;
    static const layout_line_rule_t rules[] = {
        LAYOUT_LINE_BLOCK_RULES,
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 100.0, 20.0 }
    };
    robo_layout_stats_t stats;
    robo_layout_node_id_t p_box;
    size_t pass;

    layout_fixture();
    layout_line_begin(&fixture, "<html><body><p><i>a</i><i>b</i><i>c</i></p></body></html>",
                      rules, 4u);
    p_box = layout_line_box_of(&fixture, "p");

    /* No measurer: the boxes' sizes come from their declarations, which is what makes
     * this case independent of any font. */
    for (pass = 0u; pass < 2u; pass++) {
        int32_t viewport = (pass == 0u) ? 250 : 150;
        int expected_lines = (pass == 0u) ? 2 : 3;

        layout_line_pass(&fixture, viewport, NULL, &stats);
        ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), (size_t)expected_lines);
        ROBO_CHECK_EQ_INT(stats.inline_flow.lines, (size_t)expected_lines);
        ROBO_CHECK_EQ_INT(stats.inline_flow.line_members, 3u);
        ROBO_CHECK_EQ_INT(stats.inline_flow.block_level_declined, 0u);

        if (pass == 0u) {
            robo_layout_line_box_t first = layout_line_get(&fixture, layout_line_at(&fixture, 0u));
            robo_layout_line_box_t second = layout_line_get(&fixture, layout_line_at(&fixture, 1u));

            layout_line_expect(&first, p_box, 0u, 0, 0, 200, 20);
            ROBO_CHECK_EQ_INT(first.members, 2u);
            layout_line_expect(&second, p_box, 1u, 0, 20, 100, 20);
            ROBO_CHECK_EQ_INT(second.members, 1u);
            /* The available width is recorded, so "why did this wrap?" is answerable
             * from the line alone -- and it is the *viewport's* width here, because the
             * paragraph is the body's full-width child. */
            ROBO_CHECK_EQ_INT(first.available_width, viewport);
            ROBO_CHECK_EQ_INT(second.available_width, viewport);
            ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 1u);
        } else {
            size_t index;

            ROBO_CHECK_EQ_INT(stats.inline_flow.wraps, 2u);
            for (index = 0u; index < 3u; index++) {
                robo_layout_line_box_t line = layout_line_get(&fixture, layout_line_at(&fixture,
                                                                                      index));

                /* Each line holds exactly one box, and its top is twenty px below the
                 * one before it -- so three lines occupy sixty px and nothing overlaps. */
                layout_line_expect(&line, p_box, index, 0, (int32_t)(index * 20u), 100, 20);
                ROBO_CHECK_EQ_INT(line.members, 1u);
            }
        }
    }

    /* The block extent the paragraph got is the *last line's* bottom edge, which is the
     * derivation 10.6 introduced: a block containing lines obtains its block-flow extent
     * from them. */
    {
        robo_layout_result_t result;
        robo_layout_size_t size;

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, p_box, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
        ROBO_CHECK_EQ_INT(size.height, 60);
    }

    layout_line_end(&fixture);
}

/**
 * The same tree, the same pass, twice: a second pass *replaces* the lines.
 *
 * Two claims in one case, and they are the same claim. The first is determinism -- the
 * identities, geometry and membership of the second pass equal the first's. The second
 * is that lines do not accumulate: a build that appended would report six lines after
 * two passes over a document that has three, and every reader after it would be reading
 * a mixture of two layouts.
 */
static void test_line_repeat_pass_replaces(void)
{
    layout_line_fixture_t fixture;
    static const layout_line_rule_t rules[] = {
        LAYOUT_LINE_BLOCK_RULES,
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 100.0, 20.0 }
    };
    robo_layout_line_box_t first[3];
    robo_layout_line_box_t second[3];
    robo_layout_stats_t stats;
    size_t index;

    layout_fixture();
    layout_line_begin(&fixture, "<html><body><p><i>a</i><i>b</i><i>c</i></p></body></html>",
                      rules, 4u);

    layout_line_pass(&fixture, 150, NULL, &stats);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 3u);
    for (index = 0u; index < 3u; index++) {
        first[index] = layout_line_get(&fixture, layout_line_at(&fixture, index));
    }

    layout_line_pass(&fixture, 150, NULL, &stats);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 3u);
    for (index = 0u; index < 3u; index++) {
        second[index] = layout_line_get(&fixture, layout_line_at(&fixture, index));
        ROBO_CHECK_EQ_INT(robo_layout_line_box_equal(&first[index], &second[index]), 1);
    }
    /* The ids restarted rather than continuing, which is the fact that makes a line id
     * valid only until the next pass -- and it is asserted because a caller holding an
     * id across a pass would otherwise read the *wrong* line silently. */
    ROBO_CHECK_EQ_INT((int)second[0].id, 1);

    layout_line_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Empty content                                                       */
/* ------------------------------------------------------------------ */

/**
 * An inline formatting context with nothing on a line produces **no** line boxes.
 *
 * CSS's rule, and the case has to be built carefully: `<p></p>` has no children at all,
 * so the paragraph is a block container with no block-level child and *does* establish
 * an inline context (10.2) whose contents are empty. A build that emitted a zero-height
 * line for it would make \"no content\" and \"one empty line\" the same document.
 */
static void test_line_empty_context(void)
{
    layout_line_fixture_t fixture;
    static const layout_line_rule_t rules[] = { LAYOUT_LINE_BLOCK_RULES };
    robo_layout_stats_t stats;
    robo_layout_node_id_t p_box;
    robo_layout_line_box_t line;
    robo_layout_line_id_t id;

    layout_fixture();
    layout_line_begin(&fixture, "<html><body><p></p></body></html>", rules, 3u);
    layout_line_pass(&fixture, LAYOUT_LINE_WIDE_VIEWPORT, NULL, &stats);

    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.line_members, 0u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.empty_contexts, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.participants_placed, 0u);

    /* And the arena refuses an id it never handed out, rather than answering from
     * whatever was in the slot. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, (robo_layout_line_id_t)1u, &line,
                                           NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)line.id, (int)ROBO_LAYOUT_LINE_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture.tree, 0u, &id, NULL), ROBO_NOT_FOUND);

    /* The paragraph still has a geometry -- zero tall, the extent of no lines. */
    p_box = layout_line_box_of(&fixture, "p");
    {
        robo_layout_result_t result;
        robo_layout_size_t size;

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, p_box, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
        ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
        ROBO_CHECK_EQ_INT(size.height, 0);
    }

    layout_line_end(&fixture);
}

/**
 * An unmeasured run is a member of a line that exists and is 0 x 0.
 *
 * The distinction 10.5 drew, arriving in 10.6's terms: \"no line\" and \"a line with
 * nothing measurable on it\" are different documents. The run *was* placed -- it is a
 * member, and it has a back-link -- and the line it is on is simply zero wide and zero
 * tall, because nothing measured it. A build that skipped the placement would report no
 * lines for a paragraph that has text in it, which is the failure this case exists to
 * catch.
 */
static void test_line_unmeasured_run(void)
{
    layout_line_fixture_t fixture;
    static const layout_line_rule_t rules[] = { LAYOUT_LINE_BLOCK_RULES };
    robo_layout_stats_t stats;
    robo_layout_line_box_t line;
    robo_layout_node_id_t text_box;
    robo_layout_line_id_t back = ROBO_LAYOUT_LINE_ID_NONE;

    layout_fixture();
    layout_line_begin(&fixture, "<html><body><p>Hello</p></body></html>", rules, 3u);
    layout_line_pass(&fixture, LAYOUT_LINE_WIDE_VIEWPORT, NULL, &stats);

    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.lines, 1u);
    ROBO_CHECK_EQ_INT(stats.inline_flow.line_members, 1u);
    ROBO_CHECK_EQ_INT(stats.text.runs_placed, 1u);
    ROBO_CHECK_EQ_INT(stats.text.unmeasured, 1u);
    ROBO_CHECK_EQ_INT(stats.text.measured, 0u);

    line = layout_line_get(&fixture, layout_line_at(&fixture, 0u));
    ROBO_CHECK_EQ_INT(line.members, 1u);
    ROBO_CHECK_EQ_INT(line.size.width, 0);
    ROBO_CHECK_EQ_INT(line.size.height, 0);

    text_box = layout_line_text_of(&fixture, "p");
    ROBO_CHECK_EQ_INT(robo_layout_object_line(fixture.tree, text_box, &back, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)back, (int)line.id);

    layout_line_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The back-link, and the arena's lifetime                             */
/* ------------------------------------------------------------------ */

/**
 * A line is released by anything that releases the classification.
 *
 * The property a later phase depends on: an id read out of a released arena must be a
 * reported miss and not a number. Adding a box invalidates the classification --
 * because a classification describes a box set and the set changed -- and the lines
 * describe the *same* pass, so they go with it, `object->line` is cleared, and the read
 * doors refuse both the line and the back-link.
 *
 * The assertions are written as \"NOT_FOUND\" rather than as \"not id 1\" because the
 * point is that a stale id cannot be *read back*, not that it stopped existing: ids
 * restart at 1, so handed one back would name a different line.
 */
static void test_line_released(void)
{
    layout_line_fixture_t fixture;
    static const layout_line_rule_t rules[] = {
        LAYOUT_LINE_BLOCK_RULES,
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 100.0, 20.0 }
    };
    robo_layout_stats_t stats;
    robo_layout_line_id_t id;
    robo_layout_node_id_t text_box;
    robo_layout_node_id_t back_link;
    robo_layout_line_box_t line;

    layout_fixture();
    layout_line_begin(&fixture, "<html><body><p><i>a</i>zz</p></body></html>", rules, 4u);
    layout_line_pass(&fixture, 150, NULL, &stats);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    id = layout_line_at(&fixture, 0u);
    text_box = layout_line_text_of(&fixture, "p");
    ROBO_CHECK_EQ_INT(robo_layout_object_line(fixture.tree, text_box, &back_link, NULL),
                      ROBO_OK);

    /* An explicit invalidation: the classification goes, and the lines with it. */
    robo_layout_tree_invalidate_contexts(fixture.tree);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, id, &line, NULL), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_line(fixture.tree, text_box, &back_link, NULL),
                      ROBO_NOT_FOUND);
    /* The boxes are untouched: releasing a pass's conclusions is not clearing a tree. */
    ROBO_CHECK_EQ_INT(robo_layout_object_kind(fixture.tree, text_box),
                      ROBO_LAYOUT_OBJECT_TEXT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree) > 0u, 1);

    /* And an add releases them too, for the same reason. */
    {
        robo_html_node_handle_t element = layout_document_find(&fixture.doc, "p");
        robo_html_node_handle_t span;
        robo_layout_object_info_t info;
        robo_layout_node_id_t added = ROBO_LAYOUT_NODE_ID_NONE;

        layout_line_pass(&fixture, 150, NULL, &stats);
        ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
        /* A second box for an element that already has one, so the added box needs a
         * node of its own: the tree is a box set and not a mirror of the DOM. */
        span = layout_line_box_of(&fixture, "i") == ROBO_LAYOUT_NODE_ID_NONE
                   ? element
                   : element;
        robo_layout_object_info_init(&info);
        info.parent = layout_line_box_of(&fixture, "body");
        info.dom_node = span;
        info.display = ROBO_CSS_DISPLAY_BLOCK;
        ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &added, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 0u);
        ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, id, &line, NULL), ROBO_NOT_FOUND);
    }

    layout_line_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

static void test_line_validation(void)
{
    layout_line_fixture_t fixture;
    static const layout_line_rule_t rules[] = { LAYOUT_LINE_BLOCK_RULES };
    robo_layout_line_box_t line;
    robo_layout_line_id_t id = ROBO_LAYOUT_LINE_ID_NONE;
    robo_layout_node_id_t member = ROBO_LAYOUT_NODE_ID_NONE;

    layout_fixture();
    layout_line_begin(&fixture, "<html><body><p>AB</p></body></html>", rules, 3u);

    /* A NULL tree, and a NONE id: argument refusals, before anything is read. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(NULL), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(NULL, 0u, &id, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture.tree, 0u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(NULL, (robo_layout_line_id_t)1u, &line, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, ROBO_LAYOUT_LINE_ID_NONE, &line, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, (robo_layout_line_id_t)1u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_line_member(fixture.tree, (robo_layout_line_id_t)1u, 0u, NULL,
                                              NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_line(fixture.tree, ROBO_LAYOUT_NODE_ID_NONE, &id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_line(fixture.tree, (robo_layout_node_id_t)1u, NULL,
                                              NULL),
                      ROBO_INVALID_ARGUMENT);
    /* An id that names no object at all, on a tree that has objects. */
    ROBO_CHECK_EQ_INT(robo_layout_object_line(fixture.tree,
                                              (robo_layout_node_id_t)(robo_layout_tree_object_count(
                                                  fixture.tree) + 1u),
                                              &id, NULL),
                      ROBO_NOT_FOUND);

    /* And a miss leaves the output cleared rather than half-filled. */
    ROBO_CHECK_EQ_INT((int)id, (int)ROBO_LAYOUT_LINE_ID_NONE);
    ROBO_CHECK_EQ_INT((int)member, (int)ROBO_LAYOUT_NODE_ID_NONE);

    layout_line_end(&fixture);
}

static void test_line_allocator_balance(void)
{
    layout_line_fixture_t fixture;
    static const layout_line_rule_t rules[] = {
        LAYOUT_LINE_BLOCK_RULES,
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 40.0, 20.0 },
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 }
    };
    layout_line_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_stats_t stats;
    size_t before;
    size_t settled;
    int pass;

    layout_fixture();

    /*
     * Taken **before the tree exists**, so the two assertions below bracket the whole
     * lifetime of a line arena rather than only the passes: nothing a line box costs may
     * outlive the tree that owned it.
     *
     * Equality rather than a strict decrease, and the reason is the one every other
     * `allocator_balance` case in this directory is written on: the counter behind
     * `layout_live_blocks` is a debug-build instrument and reads 0 in a release build,
     * so an assertion that required a *drop* would pass in Debug and fail in Release
     * while saying nothing about line boxes in either. `==` is the claim that is true in
     * both -- and in Debug it is still the exact claim that matters, because the tree's
     * line arena and member arena are released by the tree and by nothing else.
     */
    before = layout_live_blocks();
    layout_line_begin(&fixture,
                      "<html><body><p><span>ab</span>cd<i>e</i>fg</p></body></html>",
                      rules, 5u);
    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.height = 20;
    metrics = layout_line_script_metrics(&script);

    memset(&stats, 0, sizeof(stats));

    /*
     * One *settled* pass, so the first is the one that may grow the line arena and
     * every later one must not. The baseline for the loop is therefore taken after a pass
     * rather than before it: an arena that reserves on demand and keeps what it reserved
     * is not a leak, and a test that called it one would fail as soon as the capacity
     * fitted -- which is the shape of a false positive, so the distinction has to be in
     * the test and not in the reader's head.
     *
     * What the loop asserts is the *repeat*: four further passes over an unchanged tree
     * must leave the live count exactly where the settled one found it, which is what an
     * accumulating arena -- one that appended a fresh capacity per pass instead of
     * reusing the last -- would violate on the second pass.
     */
    layout_line_pass(&fixture, 60, &metrics, &stats);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree) > 0u, 1);
    settled = layout_live_blocks();

    for (pass = 0; pass < 4; pass++) {
        layout_line_pass(&fixture, 60, &metrics, &stats);
        ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree) > 0u, 1);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
    }

    /* And the arena is the tree's: destroying the tree takes the line arena and its
     * member arena down with it, which is the whole of what ownership means here. */
    layout_line_end(&fixture);
    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_layout_line_box(void)
{
    robo_test_register("layout.line.state_vocabulary", test_line_state_vocabulary);
    robo_test_register("layout.line.record", test_line_record);
    robo_test_register("layout.line.single_run", test_line_single_run);
    robo_test_register("layout.line.members_are_placements", test_line_members_are_placements);
    robo_test_register("layout.line.empty_span_is_a_member", test_line_empty_span_is_a_member);
    robo_test_register("layout.line.wrapping", test_line_wrapping);
    robo_test_register("layout.line.repeat_pass_replaces", test_line_repeat_pass_replaces);
    robo_test_register("layout.line.empty_context", test_line_empty_context);
    robo_test_register("layout.line.unmeasured_run", test_line_unmeasured_run);
    robo_test_register("layout.line.released", test_line_released);
    robo_test_register("layout.line.validation", test_line_validation);
    robo_test_register("layout.line.allocator_balance", test_line_allocator_balance);
}
