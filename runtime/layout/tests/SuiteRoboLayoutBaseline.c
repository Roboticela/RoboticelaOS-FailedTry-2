/*
 * RoboLayout baseline suite (Phase 10.7) — layout.baseline.*
 *
 * The phase where a participant stops being aligned to the top of its line and
 * starts being aligned to a *floor*. Everything here is one of two claims: that the
 * metrics arithmetic is exact, or that two participants which should share a floor do.
 *
 * Six groups, in the order the phase is built:
 *
 *   the vocabulary   three kinds walked rather than a representative typed out, with
 *                    the one fact that separates NONE from a zero extent
 *
 *   the record       init, equality, validity, extent and the offset door, including
 *                    the sum that cannot overflow
 *
 *   the run rule     half-leading: the gap split across the two terms, exact to the
 *                    pixel for an odd gap, and the degradation an unmeasured run takes
 *
 *   the fallback     the bottom-edge rule, and the negative extent that must not
 *                    become a negative ascent
 *
 *   the line         a real pass: every line's height is its ascent plus its descent,
 *                    its baseline sits where the two say, and a **mixed-size line** is
 *                    as tall as its members need to sit on one floor
 *
 *   the pass         object metrics, the two-line case, empty content, re-layout,
 *                    determinism, and the accounting over a whole tree
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The scripted measurer                                               */
/* ------------------------------------------------------------------ */

/** One scripted answer: a width per code point, and a font's three line terms. */
typedef struct layout_baseline_script {
    int32_t per_code_point;
    /**
     * Additional ascent/descent **per code point**, so one script can hand two runs
     * different metrics.
     *
     * Without these the measurer answers every run identically, and a case claiming
     * "two sizes, one floor" would be testing two objects that happened to be the same
     * size -- which is exactly the shape the phase is supposed not to need. `0` for both
     * leaves every other case in the suite measuring as before.
     */
    int32_t ascent_per_code_point;
    int32_t descent_per_code_point;
    int32_t ascent;
    int32_t descent;
    int32_t gap;
    size_t calls;
} layout_baseline_script_t;

/**
 * The measurer: `per_code_point` px per code point, and a font whose line metrics
 * are the three terms the case asked for.
 *
 * `gap` is a field here where the line suite's script had none, and that is what the
 * phase needs: the run's `height` is `ascent + descent + gap`, so a script that could
 * not set a gap could not tell "the metrics' extent equals the run's height" from
 * "the gap was dropped".
 */
static robo_result_t layout_baseline_script_measure(void *userdata,
                                                    const robo_layout_text_request_t *request,
                                                    robo_layout_text_run_t *out_run)
{
    layout_baseline_script_t *script = (layout_baseline_script_t *)userdata;

    if (!script || !request || !out_run || !request->style) {
        return ROBO_INVALID_ARGUMENT;
    }
    script->calls++;
    out_run->source = request->text_node;
    out_run->range = request->range;
    out_run->direction = request->style->direction;
    out_run->code_points = robo_layout_text_range_length(request->range);
    out_run->width = (int32_t)out_run->code_points * script->per_code_point;
    out_run->ascent =
        script->ascent + (int32_t)out_run->code_points * script->ascent_per_code_point;
    out_run->descent =
        script->descent + (int32_t)out_run->code_points * script->descent_per_code_point;
    out_run->height = out_run->ascent + out_run->descent + script->gap;
    return ROBO_OK;
}

static robo_layout_text_metrics_t layout_baseline_script_metrics(
    layout_baseline_script_t *script)
{
    robo_layout_text_metrics_t metrics;

    robo_layout_text_metrics_init(&metrics);
    metrics.userdata = script;
    metrics.measure = layout_baseline_script_measure;
    return metrics;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/** One scripted answer: an element with this tag computes to this. */
typedef struct layout_baseline_rule {
    const char *tag;
    robo_css_display_t display;
    double width_px;
    double height_px;
} layout_baseline_rule_t;

#define LAYOUT_BASELINE_MAX_RULES 8u

/**
 * The number of rules in a `layout_baseline_rule_t` array, derived from the array.
 *
 * Written as a macro over the array rather than as a literal at each call site because
 * a literal is a second statement of a fact the compiler already knows, and the two can
 * disagree silently: a case whose array grew a rule it then forgot to count would run
 * with its last few rules *dropped*, and the symptoms -- a box with the initial display,
 * or an atomic with no declared size -- read as layout bugs rather than as a truncated
 * fixture.
 */
#define LAYOUT_BASELINE_RULE_COUNT(rules) (sizeof(rules) / sizeof((rules)[0]))

typedef struct layout_baseline_style {
    char tag[16];
    robo_css_computed_style_t *style; /* owned */
} layout_baseline_style_t;

typedef struct layout_baseline_fixture {
    layout_document_t doc;
    layout_baseline_style_t rules[LAYOUT_BASELINE_MAX_RULES];
    size_t rule_count;
    robo_layout_style_source_t styles;
    robo_layout_tree_t *tree;
} layout_baseline_fixture_t;

static const robo_css_computed_style_t *layout_baseline_style_lookup(
    void *userdata,
    const robo_html_node_handle_t *element)
{
    layout_baseline_fixture_t *fixture = (layout_baseline_fixture_t *)userdata;
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
static void layout_baseline_style_add(layout_baseline_fixture_t *fixture,
                                      const layout_baseline_rule_t *rule)
{
    robo_css_computed_style_t *style = NULL;
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    robo_css_value_t specified = ROBO_CSS_CLEARED;
    size_t existing;

    layout_fixture();
    ROBO_CHECK_EQ_INT(fixture->rule_count < LAYOUT_BASELINE_MAX_RULES, 1);
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
    /*
     * Read the display back out of the style that was just built, so a rule whose
     * container silently refused the entry is a fixture failure at the rule rather than a
     * layout surprise later in the case: the builder reads exactly this door
     * (`robo_layout_style_display`), so "the rule's display" and "what a box built from
     * it sees" are the same question and are asked here once.
     */
    ROBO_CHECK_EQ_INT((int)robo_layout_style_display(style), (int)rule->display);
    /* A *real* computed LENGTH, through 9.8's own maker: the pass resolves a declared
     * size by asking for a LENGTH in px (RoboLayoutInternal.h), so a suite that wrote
     * the value by hand would be testing a struct rather than the rule. */
    if (rule->width_px > 0.0) {
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
    if (rule->height_px > 0.0) {
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
    strncpy(fixture->rules[fixture->rule_count].tag, rule->tag,
            sizeof(fixture->rules[fixture->rule_count].tag) - 1u);
    fixture->rules[fixture->rule_count]
        .tag[sizeof(fixture->rules[fixture->rule_count].tag) - 1u] = '\0';
    fixture->rules[fixture->rule_count].style = style;
    fixture->rule_count++;
}

static void layout_baseline_begin(layout_baseline_fixture_t *fixture,
                                  const char *html,
                                  const layout_baseline_rule_t *rules,
                                  size_t count)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    for (index = 0u; index < count; index++) {
        layout_baseline_style_add(fixture, &rules[index]);
    }
    robo_layout_style_source_init(&fixture->styles);
    fixture->styles.userdata = fixture;
    fixture->styles.computed_style_for = layout_baseline_style_lookup;
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_baseline_end(layout_baseline_fixture_t *fixture)
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
static void layout_baseline_pass(layout_baseline_fixture_t *fixture,
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
#define LAYOUT_BASELINE_WIDE_VIEWPORT 800

/** The block container every fixture below shares: `<html><body><p>`. */
#define LAYOUT_BASELINE_BLOCK_RULES                              \
    { "html", ROBO_CSS_DISPLAY_BLOCK, 0.0, 0.0 },                \
    { "body", ROBO_CSS_DISPLAY_BLOCK, 0.0, 0.0 },                \
    { "p", ROBO_CSS_DISPLAY_BLOCK, 0.0, 0.0 }

/** The box built for the first element named @p tag, asserted to exist. */
static robo_layout_node_id_t layout_baseline_box_of(layout_baseline_fixture_t *fixture,
                                                    const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);
    robo_layout_node_id_t id;

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&element), 1);
    id = robo_layout_object_id_for_dom_node(fixture->tree, &element);
    ROBO_CHECK(id != ROBO_LAYOUT_NODE_ID_NONE);
    return id;
}

/**
 * The first **text object** at or below @p parent, in layout order.
 *
 * Depth-first rather than direct-children-only, and the difference is not a
 * convenience: a run lives inside the inline box that holds it, so `<p><span>ab</span>`
 * has a text object one level down from the paragraph and a walk that looked only at
 * the paragraph's children would find no run at all -- and would then report a
 * missing run where the tree actually has one, which is the failure mode this helper
 * exists to avoid. Recursion terminates because a text object is a leaf.
 */
static robo_layout_node_id_t layout_baseline_text_object(
    layout_baseline_fixture_t *fixture,
    robo_layout_node_id_t parent)
{
    robo_layout_node_id_t child = ROBO_LAYOUT_NODE_ID_NONE;
    robo_result_t status;

    status = robo_layout_object_first_child(fixture->tree, parent, &child);
    if (status != ROBO_OK) {
        return ROBO_LAYOUT_NODE_ID_NONE;
    }
    for (;;) {
        if (robo_layout_object_kind(fixture->tree, child) == ROBO_LAYOUT_OBJECT_TEXT) {
            return child;
        }
        {
            robo_layout_node_id_t found = layout_baseline_text_object(fixture, child);

            if (found != ROBO_LAYOUT_NODE_ID_NONE) {
                return found;
            }
        }
        status = robo_layout_object_next_sibling(fixture->tree, child, &child);
        if (status != ROBO_OK) {
            return ROBO_LAYOUT_NODE_ID_NONE;
        }
    }
}

/** Read line @p index of @p tree, asserted to exist. */
static void layout_baseline_line(layout_baseline_fixture_t *fixture,
                                 size_t index,
                                 robo_layout_line_box_t *out)
{
    robo_layout_line_id_t id = ROBO_LAYOUT_LINE_ID_NONE;

    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture->tree, index, &id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture->tree, id, out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out->state, (int)ROBO_LAYOUT_LINE_FINAL);
}

/** Read object @p id's geometry, asserted to be COMPUTED. */
static void layout_baseline_geometry(layout_baseline_fixture_t *fixture,
                                     robo_layout_node_id_t id,
                                     robo_layout_point_t *out_position,
                                     robo_layout_size_t *out_size)
{
    robo_layout_result_t result;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, out_position), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, out_size), ROBO_OK);
}

/** Read object @p id's baseline metrics, asserted to be usable. */
static void layout_baseline_of(layout_baseline_fixture_t *fixture,
                               robo_layout_node_id_t id,
                               robo_layout_baseline_metrics_t *out)
{
    ROBO_CHECK_EQ_INT(robo_layout_object_baseline(fixture->tree, id, out, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_is_valid(out), 1);
}

/** Object @p id's y as stored, which is relative to the box that contains it. */
static int32_t layout_baseline_y_of(layout_baseline_fixture_t *fixture,
                                    robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_point_t position;

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
    return position.y;
}

/**
 * Object @p id's y in @p owner's space, by summing the boxes between them.
 *
 * The stored y of a box is relative to its parent (RoboLayoutInline.c's rebasing
 * pass), so a member nested inside a transparent inline box is stated relative to
 * that box and not to the line. Adding the chain up to the context owner is what makes
 * a member comparable with the line it is on -- and the chain, rather than a single
 * parent, because the nesting can be deeper than one box.
 */
static int32_t layout_baseline_y_in_owner(layout_baseline_fixture_t *fixture,
                                          robo_layout_node_id_t id,
                                          robo_layout_node_id_t owner)
{
    int32_t y = layout_baseline_y_of(fixture, id);
    robo_layout_node_id_t walk = id;
    robo_layout_node_id_t parent = ROBO_LAYOUT_NODE_ID_NONE;

    while (walk != owner && robo_layout_object_parent(fixture->tree, walk, &parent) == ROBO_OK &&
           parent != ROBO_LAYOUT_NODE_ID_NONE) {
        y += layout_baseline_y_of(fixture, parent);
        walk = parent;
    }
    ROBO_CHECK_EQ_INT((int)walk, (int)owner);
    return y;
}

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void test_baseline_kind_vocabulary(void)
{
    static const struct {
        robo_layout_baseline_kind_t kind;
        const char *name;
        int usable;
    } rows[] = {
        { ROBO_LAYOUT_BASELINE_NONE, "none", 0 },
        { ROBO_LAYOUT_BASELINE_TEXT, "text", 1 },
        { ROBO_LAYOUT_BASELINE_BOX, "box", 1 }
    };
    size_t index;

    /*
     * Walked rather than spot-checked, the discipline every closed enumeration in this
     * module gets (RoboLayoutTypes.c, RoboLayoutFormattingContext.c): an enumerator
     * added without a row is a visible omission rather than a gap.
     */
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_BASELINE_KIND_COUNT, 3);
    ROBO_CHECK_EQ_INT((int)(sizeof(rows) / sizeof(rows[0])),
                      (int)ROBO_LAYOUT_BASELINE_KIND_COUNT);
    for (index = 0u; index < sizeof(rows) / sizeof(rows[0]); index++) {
        ROBO_CHECK_EQ_INT(robo_layout_baseline_kind_is_known(rows[index].kind), 1);
        ROBO_CHECK_STR_EQ(robo_layout_baseline_kind_name(rows[index].kind), rows[index].name);
        ROBO_CHECK_EQ_INT(robo_layout_baseline_kind_is_usable(rows[index].kind),
                          rows[index].usable);
    }

    /*
     * The pair the phase turns on: NONE is *known* and *unusable*, and a zero extent is
     * usable. A build that answered "is this usable?" with "is this a kind?" would let
     * a cleared record be used as a baseline, and a build that refused a zero extent
     * would fail on a document nothing measured -- which is a legitimate way to run
     * this module (RoboLayoutText.h).
     */
    ROBO_CHECK_EQ_INT(robo_layout_baseline_kind_is_known(ROBO_LAYOUT_BASELINE_NONE), 1);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_kind_is_usable(ROBO_LAYOUT_BASELINE_NONE), 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_kind_is_usable(ROBO_LAYOUT_BASELINE_TEXT), 1);

    /* One past the last row, and a negative: both refused, so the vocabulary is closed
     * at three rather than merely observed to have three members. */
    ROBO_CHECK_EQ_INT(robo_layout_baseline_kind_is_known((robo_layout_baseline_kind_t)3), 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_kind_is_usable((robo_layout_baseline_kind_t)3), 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_kind_is_known((robo_layout_baseline_kind_t)-1), 0);
    ROBO_CHECK_STR_EQ(robo_layout_baseline_kind_name((robo_layout_baseline_kind_t)3), "unknown");
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

static void test_baseline_metrics_record(void)
{
    robo_layout_baseline_metrics_t empty;
    robo_layout_baseline_metrics_t a;
    robo_layout_baseline_metrics_t b;
    int32_t offset = -1;

    robo_layout_baseline_metrics_init(&empty);
    ROBO_CHECK_EQ_INT((int)empty.kind, (int)ROBO_LAYOUT_BASELINE_NONE);
    ROBO_CHECK_EQ_INT(empty.ascent, 0);
    ROBO_CHECK_EQ_INT(empty.descent, 0);

    /* A cleared record is *invalid* -- usable kind required -- while a zero extent with
     * a real kind is valid. The distinction is the one `is_usable` draws. */
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_is_valid(&empty), 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_extent(&empty), 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_offset(&empty, &offset), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(offset, 0);

    a.kind = ROBO_LAYOUT_BASELINE_TEXT;
    a.ascent = 9;
    a.descent = 2;
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_is_valid(&a), 1);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_extent(&a), 11);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_offset(&a, &offset), ROBO_OK);
    ROBO_CHECK_EQ_INT(offset, 9);

    /* Equality includes the kind: two records with one extent and two anchors are two
     * answers, and a comparison that ignored the kind would call them the same. */
    b = a;
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_equal(&a, &b), 1);
    b.kind = ROBO_LAYOUT_BASELINE_BOX;
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_equal(&a, &b), 0);
    b = a;
    b.descent = 3;
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_equal(&a, &b), 0);

    /* A zero extent with a usable kind is valid, and it is the shape an unmeasured run
     * produces -- so it must not be refused. */
    b.kind = ROBO_LAYOUT_BASELINE_TEXT;
    b.ascent = 0;
    b.descent = 0;
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_is_valid(&b), 1);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_extent(&b), 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_offset(&b, &offset), ROBO_OK);
    ROBO_CHECK_EQ_INT(offset, 0);

    /* A negative term is refused rather than clamped: every other arithmetic in this
     * module refuses an impossible value instead of repairing it (RoboLayoutResult.h),
     * and a negative ascent would silently become a *placement* two phases later. */
    b.ascent = -1;
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_is_valid(&b), 0);
    b.ascent = 0;
    b.descent = -1;
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_is_valid(&b), 0);

    /* The sum is clamped rather than wrapped, so `extent` is total: an int32 wrap here
     * would hand a caller a negative height, which is the one value the rest of the
     * module treats as impossible. */
    a.kind = ROBO_LAYOUT_BASELINE_TEXT;
    a.ascent = INT32_MAX;
    a.descent = INT32_MAX;
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_extent(&a), INT32_MAX);

    /* NULL tolerance: the module's records are safe on NULL by convention. */
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_equal(&a, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_is_valid(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_extent(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_offset(NULL, &offset), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_offset(&a, NULL), ROBO_INVALID_ARGUMENT);
    robo_layout_baseline_metrics_init(NULL);
}

/* ------------------------------------------------------------------ */
/* The run rule: where the line gap becomes half-leading               */
/* ------------------------------------------------------------------ */

static void test_baseline_metrics_from_run(void)
{
    robo_layout_text_run_t run;
    robo_layout_baseline_metrics_t metrics;

    /* An even gap splits evenly, and the extent is the run's height to the pixel. */
    robo_layout_text_run_init(&run);
    run.ascent = 10;
    run.descent = 4;
    run.height = 18; /* gap 4 -> 12 above, 6 below */
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_from_run(&run, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)metrics.kind, (int)ROBO_LAYOUT_BASELINE_TEXT);
    ROBO_CHECK_EQ_INT(metrics.ascent, 12);
    ROBO_CHECK_EQ_INT(metrics.descent, 6);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_extent(&metrics), run.height);

    /*
     * An odd gap puts its spare pixel **below** the baseline, and the extent is still
     * the run's height exactly. This is the case a build that halved the gap twice
     * (as `gap / 2` on both terms) would fail by one pixel -- which is why the second
     * term is written as `gap - half` and why the case exists.
     */
    run.ascent = 10;
    run.descent = 4;
    run.height = 15; /* gap 1 -> 10 above, 5 below */
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_from_run(&run, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(metrics.ascent, 10);
    ROBO_CHECK_EQ_INT(metrics.descent, 5);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_extent(&metrics), 15);

    /* No gap at all: the metrics are the run's own two terms, unchanged. */
    run.ascent = 7;
    run.descent = 3;
    run.height = 10;
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_from_run(&run, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(metrics.ascent, 7);
    ROBO_CHECK_EQ_INT(metrics.descent, 3);

    /*
     * A run with **no measurement** -- the documented degradation -- yields a TEXT
     * baseline of zero extent, not a NONE one. The claim being tested is that an
     * unmeasured run is *anchored* and has nothing above or below it, which is a
     * statement about the document rather than a missing answer; a NONE kind here
     * would make the line unreportable and the arena would refuse to store it.
     */
    robo_layout_text_run_init(&run);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_from_run(&run, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)metrics.kind, (int)ROBO_LAYOUT_BASELINE_TEXT);
    ROBO_CHECK_EQ_INT(metrics.ascent, 0);
    ROBO_CHECK_EQ_INT(metrics.descent, 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_is_valid(&metrics), 1);

    /*
     * A third-party measurer can report a height *smaller* than ascent plus descent --
     * the run record's contract does not forbid it and a scripted measurer is exactly
     * where it happens. That is read as no gap, so the metrics' extent is the larger
     * of the two numbers and a line containing the run still encloses it.
     */
    robo_layout_text_run_init(&run);
    run.ascent = 12;
    run.descent = 5;
    run.height = 9;
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_from_run(&run, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(metrics.ascent, 12);
    ROBO_CHECK_EQ_INT(metrics.descent, 5);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_extent(&metrics), 17);

    /* A negative term in the run record -- which the seam's own contract forbids but a
     * hand-built record can carry -- is clamped to zero rather than propagated into a
     * negative ascent. */
    robo_layout_text_run_init(&run);
    run.ascent = -4;
    run.descent = -4;
    run.height = 0;
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_from_run(&run, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(metrics.ascent, 0);
    ROBO_CHECK_EQ_INT(metrics.descent, 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_is_valid(&metrics), 1);

    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_from_run(NULL, &metrics),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_from_run(&run, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* The fallback                                                        */
/* ------------------------------------------------------------------ */

static void test_baseline_metrics_from_extent(void)
{
    robo_layout_baseline_metrics_t metrics;

    /* The whole extent is above the baseline: the object's bottom edge is its floor. */
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_from_extent(24, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)metrics.kind, (int)ROBO_LAYOUT_BASELINE_BOX);
    ROBO_CHECK_EQ_INT(metrics.ascent, 24);
    ROBO_CHECK_EQ_INT(metrics.descent, 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_extent(&metrics), 24);

    /* Zero is a legitimate answer: an empty inline box sits on the baseline with no
     * extent, and the invariant `ascent + descent == extent` holds at zero. */
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_from_extent(0, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(metrics.ascent, 0);
    ROBO_CHECK_EQ_INT(metrics.descent, 0);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_is_valid(&metrics), 1);

    /* A negative extent is clamped, so the door is total and the invariant survives. */
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_from_extent(-5, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(metrics.ascent, 0);
    ROBO_CHECK_EQ_INT(metrics.descent, 0);

    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_from_extent(4, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* A line, after a real pass                                           */
/* ------------------------------------------------------------------ */

static void test_baseline_line_from_text(void)
{
    static const layout_baseline_rule_t rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 }
    };
    layout_baseline_fixture_t fixture;
    layout_baseline_script_t script;
    robo_layout_baseline_metrics_t metrics;
    robo_layout_text_metrics_t measurer;
    robo_layout_line_box_t line;
    int32_t offset = -1;
    int32_t baseline_y = -1;

    layout_fixture();
    layout_baseline_begin(&fixture, "<html><body><p><span>abc</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.ascent = 12;
    script.descent = 4;
    script.gap = 2;
    measurer = layout_baseline_script_metrics(&script);

    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);

    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    layout_baseline_line(&fixture, 0u, &line);

    /*
     * The line's baseline came from a font, its height is its two terms, and the three
     * numbers cannot disagree -- because the height *is* their sum rather than a fourth
     * quantity that happens to match.
     */
    ROBO_CHECK_EQ_INT((int)line.baseline.kind, (int)ROBO_LAYOUT_BASELINE_TEXT);
    ROBO_CHECK_EQ_INT(line.baseline.ascent, 13); /* 12 + gap/2 */
    ROBO_CHECK_EQ_INT(line.baseline.descent, 5); /* 4 + the rest */
    ROBO_CHECK_EQ_INT(line.size.height, 18);
    ROBO_CHECK_EQ_INT(line.size.height,
                      robo_layout_baseline_metrics_extent(&line.baseline));

    /* The two spaces the phase distinguishes: the line's own, and its owner's. */
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_offset(&line, &offset), ROBO_OK);
    ROBO_CHECK_EQ_INT(offset, 13);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_y(&line, &baseline_y), ROBO_OK);
    ROBO_CHECK_EQ_INT(baseline_y, line.origin.y + 13);

    /* The metrics by value, and equal to the fields. */
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline(&line, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_equal(&metrics, &line.baseline), 1);

    layout_baseline_end(&fixture);
}

static void test_baseline_object_metrics(void)
{
    static const layout_baseline_rule_t rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 }
    };
    layout_baseline_fixture_t fixture;
    layout_baseline_script_t script;
    robo_layout_baseline_metrics_t metrics;
    robo_layout_text_metrics_t measurer;
    robo_layout_node_id_t text_object;

    layout_fixture();
    layout_baseline_begin(&fixture, "<html><body><p><span>abc</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.ascent = 12;
    script.descent = 4;
    script.gap = 0;
    measurer = layout_baseline_script_metrics(&script);
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);

    text_object = layout_baseline_text_object(&fixture, layout_baseline_box_of(&fixture, "p"));
    ROBO_CHECK(text_object != ROBO_LAYOUT_NODE_ID_NONE);
    layout_baseline_of(&fixture, text_object, &metrics);

    /* A text object's baseline is its font's, without the gap. */
    ROBO_CHECK_EQ_INT((int)metrics.kind, (int)ROBO_LAYOUT_BASELINE_TEXT);
    ROBO_CHECK_EQ_INT(metrics.ascent, 12);
    ROBO_CHECK_EQ_INT(metrics.descent, 4);

    /*
     * An object that was **not placed** is a miss rather than a zero. The `<span>` here
     * is a transparent inline box that placed nothing (it holds only a text run, which
     * *is* placed, so the span is transparent-with-contents) -- so it is a participant
     * of the context and a member of no line, and there is no floor to report.
     * `ROBO_NOT_FOUND` is the documented answer (RoboLayoutObject.h), and asserting it
     * is what keeps \"it has no baseline\" from being read as \"its baseline is zero\".
     */
    {
        robo_layout_node_id_t span = layout_baseline_box_of(&fixture, "span");
        robo_layout_baseline_metrics_t span_metrics;
        robo_layout_result_t result;

        ROBO_CHECK_EQ_INT(robo_layout_object_baseline(fixture.tree, span, &span_metrics, NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT((int)span_metrics.kind, (int)ROBO_LAYOUT_BASELINE_NONE);
        /* ...and it *does* have geometry: the two facts are different, which is what
         * makes this a miss about membership and not about placement. */
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, span, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
    }

    /* NULL and id refusals, one each. */
    ROBO_CHECK_EQ_INT(robo_layout_object_baseline(NULL, text_object, &metrics, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_baseline(fixture.tree, text_object, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_baseline(fixture.tree, ROBO_LAYOUT_NODE_ID_NONE,
                                                 &metrics, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_baseline(fixture.tree, (robo_layout_node_id_t)999u,
                                                 &metrics, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)metrics.kind, (int)ROBO_LAYOUT_BASELINE_NONE);

    layout_baseline_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The phase's central claim: two sizes, one floor                     */
/* ------------------------------------------------------------------ */

static void test_baseline_shared_floor(void)
{
    static const layout_baseline_rule_t rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 },
        { "b", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 }
    };
    layout_baseline_fixture_t fixture;
    layout_baseline_script_t script;
    robo_layout_baseline_metrics_t tall_metrics;
    robo_layout_baseline_metrics_t short_metrics;
    robo_layout_text_metrics_t measurer;
    robo_layout_line_box_t line;
    robo_layout_point_t tall_position;
    robo_layout_size_t tall_size;
    robo_layout_point_t short_position;
    robo_layout_size_t short_size;
    int32_t tall_baseline_y;
    int32_t short_baseline_y;

    layout_fixture();
    /*
     * One line holding two text runs of **different font metrics**, which is the shape
     * the whole phase exists for: `<span>` is the taller run, `<b>` the shorter.
     */
    layout_baseline_begin(&fixture,
                          "<html><body><p><span>abcd</span><b>e</b></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    /* Four px of ascent and one of descent *per code point*, on top of a base, so the
     * four-code-point run and the one-code-point run genuinely differ: 20/5 against
     * 8/2. A script that answered both identically would leave this case asserting
     * that two equal things are equal. */
    script.ascent_per_code_point = 4;
    script.descent_per_code_point = 1;
    script.ascent = 4;
    script.descent = 1;
    script.gap = 0;
    measurer = layout_baseline_script_metrics(&script);
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);

    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    layout_baseline_line(&fixture, 0u, &line);

    /*
     * The point of the case is the *invariant* rather than the numbers: a line whose
     * members differ in size must still place them on one floor, and each member's own
     * baseline must land there. The two members are compared through the metrics the
     * tree stored for them, so the assertions do not assume the rule they check.
     *
     * The runs are read rather than the boxes that hold them: a transparent box's
     * rectangle is the union of its contents, so reading the box and the run would mix
     * two coordinate answers for one placement.
     */
    /*
     * A nested member's y is stated in its **parent's** space, not the line's: 10.4's
     * rebasing pass moves every participant into the box that contains it after the
     * unions are known. So a run inside a transparent inline box is read by adding that
     * box's own y -- which is why both numbers are read here rather than the run alone.
     */
    {
        robo_layout_node_id_t p = layout_baseline_box_of(&fixture, "p");
        robo_layout_node_id_t span_run =
            layout_baseline_text_object(&fixture, layout_baseline_box_of(&fixture, "span"));
        robo_layout_node_id_t b_run =
            layout_baseline_text_object(&fixture, layout_baseline_box_of(&fixture, "b"));

        ROBO_CHECK(span_run != ROBO_LAYOUT_NODE_ID_NONE);
        ROBO_CHECK(b_run != ROBO_LAYOUT_NODE_ID_NONE);
        layout_baseline_of(&fixture, span_run, &tall_metrics);
        layout_baseline_of(&fixture, b_run, &short_metrics);
        layout_baseline_geometry(&fixture, span_run, &tall_position, &tall_size);
        layout_baseline_geometry(&fixture, b_run, &short_position, &short_size);
        /* Restated in the paragraph's space, which is the space the line is in. */
        tall_position.y = layout_baseline_y_in_owner(&fixture, span_run, p);
        short_position.y = layout_baseline_y_in_owner(&fixture, b_run, p);
    }

    /* The two runs really are different sizes, which is what makes the floor shared
     * rather than trivially the same. */
    ROBO_CHECK_EQ_INT(tall_metrics.ascent, 20);
    ROBO_CHECK_EQ_INT(short_metrics.ascent, 8);
    ROBO_CHECK(tall_metrics.ascent != short_metrics.ascent);

    tall_baseline_y = tall_position.y + tall_metrics.ascent;
    short_baseline_y = short_position.y + short_metrics.ascent;
    ROBO_CHECK_EQ_INT(tall_baseline_y, short_baseline_y);

    /* And the shared floor *is* the line's, in the line's own space and its owner's. */
    ROBO_CHECK_EQ_INT(tall_baseline_y, line.origin.y + line.baseline.ascent);
    ROBO_CHECK_EQ_INT((int)line.baseline.kind, (int)ROBO_LAYOUT_BASELINE_TEXT);

    /* Every member is inside its line's rectangle, both vertically and horizontally --
     * the property the union walk depends on and the one a wrong maximum would break. */
    ROBO_CHECK(tall_position.y >= line.origin.y);
    ROBO_CHECK(tall_position.y + tall_size.height <= line.origin.y + line.size.height);
    ROBO_CHECK(short_position.y >= line.origin.y);
    ROBO_CHECK(short_position.y + short_size.height <= line.origin.y + line.size.height);

    layout_baseline_end(&fixture);
}

static void test_baseline_mixed_size_is_not_top_aligned(void)
{
    static const layout_baseline_rule_t rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 },
        /*
         * Both atomics declare a **width** as well as a height: an atomic inline with
         * `width: auto` takes the containing block's width (10.4's rule, and CSS's),
         * so two of them would each fill the line and the second would wrap -- and the
         * case would then compare a member against a line it is not on.
         *
         * `<i>` and `<u>` rather than two invented tags, and that is a fact about the
         * HTML parser rather than about layout: a start tag it does not know is not kept
         * inside the paragraph, so a fixture built on `<tall>` would place that box as a
         * *block sibling* after the paragraph -- and the case would then be comparing two
         * lines' geometry while claiming to compare one line's members.
         */
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 30.0, 10.0 },
        { "u", ROBO_CSS_DISPLAY_INLINE_BLOCK, 40.0, 40.0 }
    };
    layout_baseline_fixture_t fixture;
    layout_baseline_script_t script;
    robo_layout_text_metrics_t measurer;
    robo_layout_stats_t stats;
    robo_layout_line_box_t line;
    robo_layout_point_t small_position;
    robo_layout_size_t small_size;
    robo_layout_point_t tall_position;
    robo_layout_size_t tall_size;

    layout_fixture();
    layout_baseline_begin(&fixture,
                          "<html><body><p><i></i><u></u></p></body></html>", rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.ascent = 12;
    script.descent = 4;
    measurer = layout_baseline_script_metrics(&script);
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, &stats);

    /* The fixture's own claim, checked before the geometry that depends on it: both
     * boxes are atomics and both sizes came from their declarations rather than from
     * what they contained (nothing). Without this the case's numbers would be read as a
     * statement about baselines when they were a statement about a lost declaration. */
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.atomic_placed, 2);
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.heights_declared, 2);
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.widths_declared, 2);

    /* The paragraph really holds both boxes, checked before the geometry: a case whose
     * second box had been moved out of the paragraph by the parser would be measuring
     * some other element's rectangle while claiming to measure a member. */
    {
        robo_layout_node_id_t p = layout_baseline_box_of(&fixture, "p");
        robo_layout_node_id_t child = ROBO_LAYOUT_NODE_ID_NONE;
        int children = 0;

        if (robo_layout_object_first_child(fixture.tree, p, &child) == ROBO_OK) {
            for (;;) {
                children++;
                if (robo_layout_object_next_sibling(fixture.tree, child, &child) != ROBO_OK) {
                    break;
                }
            }
        }
        ROBO_CHECK_EQ_INT(children, 2);
    }
    /* And both boxes carry the declaration the rules gave them, which is the other
     * half of the same claim: a box inside the paragraph whose style never matched the
     * source would be a block or an unstyled inline, and the numbers below would then
     * be about a different document than the one the rules describe. */
    ROBO_CHECK_EQ_INT((int)robo_layout_object_display(fixture.tree,
                                                      layout_baseline_box_of(&fixture, "i")),
                      (int)ROBO_CSS_DISPLAY_INLINE_BLOCK);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_display(fixture.tree,
                                                      layout_baseline_box_of(&fixture, "u")),
                      (int)ROBO_CSS_DISPLAY_INLINE_BLOCK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    layout_baseline_line(&fixture, 0u, &line);

    /*
     * Two **atomic** inlines of different declared heights, so the phase's rule is
     * visible without text metrics in the way: both took the bottom-edge fallback, so
     * their baselines are their feet and their feet must coincide. Before this phase
     * both sat at the line's top and the 10px box's bottom edge was 30px above the
     * 40px box's -- the failure this case names.
     */
    layout_baseline_geometry(&fixture, layout_baseline_box_of(&fixture, "i"),
                             &small_position, &small_size);
    layout_baseline_geometry(&fixture, layout_baseline_box_of(&fixture, "u"),
                             &tall_position, &tall_size);

    ROBO_CHECK_EQ_INT(small_size.height, 10);
    ROBO_CHECK_EQ_INT(tall_size.height, 40);
    ROBO_CHECK_EQ_INT(small_position.y + small_size.height,
                      tall_position.y + tall_size.height);
    ROBO_CHECK_EQ_INT(tall_position.y, line.origin.y);
    ROBO_CHECK_EQ_INT(line.size.height, 40);
    ROBO_CHECK_EQ_INT((int)line.baseline.kind, (int)ROBO_LAYOUT_BASELINE_BOX);
    ROBO_CHECK_EQ_INT(line.baseline.ascent, 40);
    ROBO_CHECK_EQ_INT(line.baseline.descent, 0);

    layout_baseline_end(&fixture);
}

static void test_baseline_descent_deepens_the_line(void)
{
    static const layout_baseline_rule_t rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 }
    };
    layout_baseline_fixture_t fixture;
    layout_baseline_script_t script;
    robo_layout_baseline_metrics_t metrics;
    robo_layout_text_metrics_t measurer;
    robo_layout_line_box_t line;
    int32_t baseline_y = -1;

    layout_fixture();
    layout_baseline_begin(&fixture, "<html><body><p><span>ab</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    /*
     * A run whose descent is large and ascent small: the line's *floor* moves down to
     * make room below it, and the member is placed so that its own descent fits. A
     * build that only ever looked at the ascent would put the line's floor at the top
     * of this run and let it hang out of the bottom of the rectangle.
     */
    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.ascent = 4;
    script.descent = 26;
    script.gap = 0;
    measurer = layout_baseline_script_metrics(&script);
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);

    layout_baseline_line(&fixture, 0u, &line);
    ROBO_CHECK_EQ_INT(line.baseline.ascent, 4);
    ROBO_CHECK_EQ_INT(line.baseline.descent, 26);
    ROBO_CHECK_EQ_INT(line.size.height, 30);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_y(&line, &baseline_y), ROBO_OK);
    ROBO_CHECK_EQ_INT(baseline_y, line.origin.y + 4);

    layout_baseline_of(&fixture,
                       layout_baseline_text_object(&fixture, layout_baseline_box_of(&fixture, "p")),
                       &metrics);
    /* The run sits at the line's top, because the line's ascent *is* its ascent -- and
     * that is the check that the maximum is being read the right way round. */
    {
        robo_layout_point_t position;
        robo_layout_size_t size;

        layout_baseline_geometry(
            &fixture, layout_baseline_text_object(&fixture, layout_baseline_box_of(&fixture, "p")),
            &position, &size);
        ROBO_CHECK_EQ_INT(position.y, line.origin.y);
        ROBO_CHECK_EQ_INT(size.height, 30);
    }

    layout_baseline_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The pass: several lines, empty content, re-layout, accounting        */
/* ------------------------------------------------------------------ */

static void test_baseline_multiple_lines(void)
{
    static const layout_baseline_rule_t rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 }
    };
    layout_baseline_fixture_t fixture;
    layout_baseline_script_t script;
    robo_layout_text_metrics_t measurer;
    robo_layout_stats_t stats;
    robo_layout_line_box_t line;
    size_t index;
    int32_t expected_top = 0;

    layout_fixture();
    /*
     * Three runs and a viewport narrower than two of them, which is the arithmetic the
     * case needs: 40px per run against 50px of line means one run fits, the second
     * wraps, and the third wraps again. Two runs would give two lines however narrow
     * the viewport is, because a first member is placed even when it overflows.
     */
    layout_baseline_begin(&fixture,
                          "<html><body><p><span>aaaa</span><span>bbbb</span>"
                          "<span>cccc</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.per_code_point = 10; /* 40px per run, 50px viewport -> three lines */
    script.ascent = 12;
    script.descent = 4;
    script.gap = 0;
    measurer = layout_baseline_script_metrics(&script);
    layout_baseline_pass(&fixture, 50, &measurer, &stats);

    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 3u);
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.lines, 3);
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.lines_from_text_baseline, 3);
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.lines_from_fallback_baseline, 0);

    /*
     * Each line has its own baseline, at its own y, and the lines stack by
     * `ascent + descent` -- which with scripted metrics is the run's height, so the
     * vertical arithmetic is the number a reader can check by hand.
     */
    for (index = 0u; index < 3u; index++) {
        int32_t offset = -1;
        int32_t baseline_y = -1;

        layout_baseline_line(&fixture, index, &line);
        ROBO_CHECK_EQ_INT((int)line.index, (int)index);
        ROBO_CHECK_EQ_INT(line.origin.y, expected_top);
        ROBO_CHECK_EQ_INT(line.baseline.ascent, 12);
        ROBO_CHECK_EQ_INT(line.baseline.descent, 4);
        ROBO_CHECK_EQ_INT(line.size.height, 16);
        ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_offset(&line, &offset), ROBO_OK);
        ROBO_CHECK_EQ_INT(offset, 12);
        ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_y(&line, &baseline_y), ROBO_OK);
        ROBO_CHECK_EQ_INT(baseline_y, expected_top + 12);
        ROBO_CHECK_EQ_INT((int)line.baseline.kind, (int)ROBO_LAYOUT_BASELINE_TEXT);
        expected_top += 16;
    }

    layout_baseline_end(&fixture);
}

static void test_baseline_empty_content(void)
{
    static const layout_baseline_rule_t rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 }
    };
    layout_baseline_fixture_t fixture;
    layout_baseline_script_t script;
    robo_layout_text_metrics_t measurer;
    robo_layout_stats_t stats;
    robo_layout_line_box_t line;

    /* An **empty** block container: nothing on a line, so no line at all -- and
     * therefore no baseline to be confused about. The baseline model does not change
     * 10.6's rule that a line exists because something was placed on it. */
    layout_fixture();
    layout_baseline_begin(&fixture, "<html><body><p></p></body></html>", rules, LAYOUT_BASELINE_RULE_COUNT(rules));
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, NULL, &stats);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 0u);
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.lines, 0);
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.lines_from_text_baseline, 0);
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.lines_from_fallback_baseline, 0);
    layout_baseline_end(&fixture);

    /*
     * An **empty span** as the only content: the span is a leaf (no children to walk),
     * so it is a member of a line -- and its baseline is the fallback with no extent.
     * The line exists, is 0 tall, and its floor is at its top. That is the deterministic
     * answer for content with no metrics, and it is deliberately *not* \"no line\".
     */
    layout_baseline_begin(&fixture, "<html><body><p><span></span></p></body></html>", rules, LAYOUT_BASELINE_RULE_COUNT(rules));
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, NULL, &stats);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    layout_baseline_line(&fixture, 0u, &line);
    ROBO_CHECK_EQ_INT((int)line.baseline.kind, (int)ROBO_LAYOUT_BASELINE_BOX);
    ROBO_CHECK_EQ_INT(line.baseline.ascent, 0);
    ROBO_CHECK_EQ_INT(line.baseline.descent, 0);
    ROBO_CHECK_EQ_INT(line.size.height, 0);
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.lines_from_fallback_baseline, 1);
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.lines_from_text_baseline, 0);
    layout_baseline_end(&fixture);

    /*
     * Text that nothing measured: a TEXT baseline of zero extent. This is the row that
     * separates \"anchored on text, nothing measured\" from \"no baseline\", and it is
     * the case that keeps the arena's refusal from being reachable in the field.
     */
    layout_baseline_begin(&fixture, "<html><body><p><span>abc</span></p></body></html>", rules, LAYOUT_BASELINE_RULE_COUNT(rules));
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, NULL, &stats);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    layout_baseline_line(&fixture, 0u, &line);
    ROBO_CHECK_EQ_INT((int)line.baseline.kind, (int)ROBO_LAYOUT_BASELINE_TEXT);
    ROBO_CHECK_EQ_INT(line.baseline.ascent, 0);
    ROBO_CHECK_EQ_INT(line.baseline.descent, 0);
    ROBO_CHECK_EQ_INT(line.size.height, 0);
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.lines_from_text_baseline, 1);
    ROBO_CHECK_EQ_INT((int)stats.text.unmeasured, 1);
    layout_baseline_end(&fixture);

    /* And the script with a zero extent but one that *did* measure, so the difference
     * between the two rows above is the measurement and not the numbers. */
    layout_baseline_begin(&fixture, "<html><body><p><span>abc</span></p></body></html>", rules, LAYOUT_BASELINE_RULE_COUNT(rules));
    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.ascent = 0;
    script.descent = 0;
    script.gap = 0;
    measurer = layout_baseline_script_metrics(&script);
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, &stats);
    layout_baseline_line(&fixture, 0u, &line);
    ROBO_CHECK_EQ_INT((int)line.baseline.kind, (int)ROBO_LAYOUT_BASELINE_TEXT);
    ROBO_CHECK_EQ_INT(line.size.height, 0);
    ROBO_CHECK_EQ_INT((int)stats.text.measured, 1);
    ROBO_CHECK_EQ_INT((int)stats.text.unmeasured, 0);
    layout_baseline_end(&fixture);
}

static void test_baseline_relayout(void)
{
    static const layout_baseline_rule_t rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 }
    };
    layout_baseline_fixture_t fixture;
    layout_baseline_script_t script;
    robo_layout_text_metrics_t measurer;
    robo_layout_line_box_t line;
    robo_layout_line_box_t first_pass;

    layout_fixture();
    /* Three one-code-point runs, so a narrow viewport has something to wrap. */
    layout_baseline_begin(&fixture,
                          "<html><body><p><span>a</span><span>b</span><span>c</span></p>"
                          "</body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.ascent = 10;
    script.descent = 2;
    script.gap = 0;
    measurer = layout_baseline_script_metrics(&script);
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);
    layout_baseline_line(&fixture, 0u, &line);
    first_pass = line;
    ROBO_CHECK_EQ_INT(line.baseline.ascent, 10);
    ROBO_CHECK_EQ_INT(line.size.height, 12);

    /*
     * A second pass with **different metrics** must move the baseline, and the fact that
     * it can is what makes the first pass's numbers a measurement rather than a
     * constant. The line's id is reused (ids restart at 1), so this also pins that a
     * baseline is replaced rather than accumulated.
     */
    script.ascent = 30;
    script.descent = 6;
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);
    layout_baseline_line(&fixture, 0u, &line);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    ROBO_CHECK_EQ_INT(line.id, first_pass.id);
    ROBO_CHECK_EQ_INT(line.baseline.ascent, 30);
    ROBO_CHECK_EQ_INT(line.baseline.descent, 6);
    ROBO_CHECK_EQ_INT(line.size.height, 36);
    ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_equal(&line.baseline, &first_pass.baseline),
                      0);

    /* And a viewport that fits exactly one run re-wraps to three lines with three
     * baselines -- the dependent geometry recomputed from the same tree, each line
     * stacked by the extent the second pass's metrics gave it. */
    layout_baseline_pass(&fixture, 10, &measurer, NULL);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 3u);
    layout_baseline_line(&fixture, 1u, &line);
    ROBO_CHECK_EQ_INT(line.origin.y, 36);
    ROBO_CHECK_EQ_INT(line.baseline.ascent, 30);

    layout_baseline_end(&fixture);
}

static void test_baseline_determinism(void)
{
    static const layout_baseline_rule_t rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 },
        /* A declared width, so the atomic sits between the two runs on one line
         * instead of taking the whole line and wrapping. */
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 10.0, 18.0 }
    };
    layout_baseline_fixture_t fixture;
    layout_baseline_script_t script;
    robo_layout_text_metrics_t measurer;
    robo_layout_line_box_t first;
    robo_layout_line_box_t second;
    robo_layout_stats_t first_stats;
    robo_layout_stats_t second_stats;

    layout_fixture();
    layout_baseline_begin(&fixture,
                          "<html><body><p><span>ab</span><i></i><span>cd</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.ascent = 14;
    script.descent = 3;
    script.gap = 4;
    measurer = layout_baseline_script_metrics(&script);

    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, &first_stats);
    layout_baseline_line(&fixture, 0u, &first);

    /* The same pass again on the same tree: the record must come back identical, which
     * rules out an accumulator that carried a previous pass's maximum forward. */
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, &second_stats);
    layout_baseline_line(&fixture, 0u, &second);

    ROBO_CHECK_EQ_INT(robo_layout_line_box_equal(&first, &second), 1);
    ROBO_CHECK_EQ_INT(robo_layout_inline_stats_equal(&first_stats.inline_flow,
                                                    &second_stats.inline_flow),
                      1);
    /*
     * The line's extent is eighteen above and five below, and neither number is the
     * text's alone: the run's own half-leading gives 16/5, and the 18px atomic raises
     * the ascent to 18 while leaving the descent at 5. A build that took its ascent
     * from the text only would answer 21 here and would still satisfy every other
     * assertion in the case.
     */
    ROBO_CHECK_EQ_INT(first.baseline.ascent, 18);
    ROBO_CHECK_EQ_INT(first.baseline.descent, 5);
    ROBO_CHECK_EQ_INT(first.size.height, 23);
    /*
     * And the kind is the *box*'s, because the kind belongs to the first member among
     * the tallest and the 18px atomic out-reaches both text runs. A reader might expect
     * TEXT on a line that has text on it; the rule is stated this way so that the answer
     * is a fact about document order rather than about which participant a walk happened
     * to visit first, and this assertion is what keeps the rule from being quietly
     * rewritten into "prefer text".
     */
    ROBO_CHECK_EQ_INT((int)first.baseline.kind, (int)ROBO_LAYOUT_BASELINE_BOX);

    layout_baseline_end(&fixture);
}

static void test_baseline_accounting(void)
{
    static const layout_baseline_rule_t rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 },
        { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK, 10.0, 24.0 }
    };
    layout_baseline_fixture_t fixture;
    layout_baseline_script_t script;
    robo_layout_text_metrics_t measurer;
    robo_layout_stats_t stats;
    robo_layout_line_box_t line;
    robo_layout_line_id_t id = ROBO_LAYOUT_LINE_ID_NONE;
    size_t lines;
    size_t index;
    size_t members_total = 0u;

    layout_fixture();
    layout_baseline_begin(&fixture,
                          "<html><body><p><span>ab</span><i></i><span>cd</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.ascent = 20;
    script.descent = 5;
    script.gap = 0;
    measurer = layout_baseline_script_metrics(&script);
    layout_baseline_pass(&fixture, 60, &measurer, &stats);

    lines = robo_layout_tree_line_count(fixture.tree);
    ROBO_CHECK(lines > 0u);

    /*
     * The identity the phase adds to the record: the two baseline-kind counters
     * partition the lines. Every stored line has exactly one kind and this build
     * produces exactly two, so a build that invented a third or forgot to count one
     * fails here rather than in a later phase's arithmetic.
     */
    ROBO_CHECK_EQ_INT((int)(stats.inline_flow.lines_from_text_baseline +
                            stats.inline_flow.lines_from_fallback_baseline),
                      (int)lines);
    ROBO_CHECK_EQ_INT((int)stats.inline_flow.lines, (int)lines);

    /* And per line: the height is the extent, the floor is inside the rectangle, every
     * member is inside it, and the member's own baseline lands on the line's. */
    for (index = 0u; index < lines; index++) {
        robo_layout_node_id_t owner = layout_baseline_box_of(&fixture, "p");
        int32_t baseline_y = -1;
        size_t member_index;

        ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture.tree, index, &id, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, id, &line, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(line.size.height,
                          robo_layout_baseline_metrics_extent(&line.baseline));
        ROBO_CHECK_EQ_INT(robo_layout_baseline_metrics_is_valid(&line.baseline), 1);
        ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_y(&line, &baseline_y), ROBO_OK);
        ROBO_CHECK(baseline_y >= line.origin.y);
        ROBO_CHECK(baseline_y <= line.origin.y + line.size.height);
        ROBO_CHECK_EQ_INT(baseline_y, line.origin.y + line.baseline.ascent);

        /* Every member's own baseline is the line's, read back through the object door
         * so the assertion goes through the same surface a later phase would. */
        for (member_index = 0u; member_index < line.members; member_index++) {
            robo_layout_node_id_t node = ROBO_LAYOUT_NODE_ID_NONE;
            robo_layout_baseline_metrics_t member_metrics;
            robo_layout_point_t position;
            robo_layout_size_t size;

            ROBO_CHECK_EQ_INT(robo_layout_line_member(fixture.tree, id, member_index, &node,
                                                     NULL),
                              ROBO_OK);
            layout_baseline_of(&fixture, node, &member_metrics);
            layout_baseline_geometry(&fixture, node, &position, &size);
            /* A member's stored y is relative to the box that holds it, so the
             * comparison with the line is made in the owner's space. */
            position.y = layout_baseline_y_in_owner(&fixture, node, owner);
            ROBO_CHECK_EQ_INT(position.y + member_metrics.ascent, baseline_y);
            ROBO_CHECK(position.y >= line.origin.y);
            ROBO_CHECK(position.y + size.height <= line.origin.y + line.size.height);
        }
        members_total += line.members;
    }

    /* The membership count is still the placements, not the participants: the phase
     * changed where a member goes and not which objects are members. */
    ROBO_CHECK_EQ_INT((int)members_total, (int)stats.inline_flow.line_members);

    /* The extent the block pass needs is still the last line's bottom edge. */
    {
        robo_layout_node_id_t p = layout_baseline_box_of(&fixture, "p");
        robo_layout_result_t result;
        robo_layout_size_t size;

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, p, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_COMPUTED);
        ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
        ROBO_CHECK_EQ_INT(size.height, line.origin.y + line.size.height);
    }

    layout_baseline_end(&fixture);
}

static void test_baseline_validation(void)
{
    static const layout_baseline_rule_t rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 }
    };
    layout_baseline_fixture_t fixture;
    robo_layout_line_box_t line;
    robo_layout_baseline_metrics_t metrics;
    int32_t offset = -1;

    layout_fixture();
    layout_baseline_begin(&fixture, "<html><body><p></p></body></html>", rules, LAYOUT_BASELINE_RULE_COUNT(rules));
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, NULL, NULL);

    /* A tree with no lines: every line door reports the miss rather than answering
     * zero, which is the discipline the whole module follows. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture.tree, 0u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, ROBO_LAYOUT_LINE_ID_NONE, &line, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(NULL, (robo_layout_line_id_t)1u, &line, NULL),
                      ROBO_INVALID_ARGUMENT);

    /*
     * And a line record a caller built by hand has **no** usable baseline, so the two
     * baseline doors refuse it. That is the shape of a record `robo_layout_line_add`
     * would not have stored -- the arena refuses a NONE baseline -- so the doors are
     * consistent with the arena rather than merely tolerant.
     */
    robo_layout_line_box_init(&line);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline(&line, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)metrics.kind, (int)ROBO_LAYOUT_BASELINE_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_offset(&line, &offset), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(offset, 0);
    {
        int32_t baseline_y = -1;

        ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_y(&line, &baseline_y), ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(baseline_y, 0);
    }

    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline(NULL, &metrics), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline(&line, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_offset(NULL, &offset),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_offset(&line, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_y(NULL, &offset), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_y(&line, NULL), ROBO_INVALID_ARGUMENT);

    /* The offset conversion is clamped rather than wrapped, so a line near the top of
     * the pixel range cannot report a floor above its own origin. */
    line.origin.y = INT32_MAX;
    line.baseline.kind = ROBO_LAYOUT_BASELINE_BOX;
    line.baseline.ascent = 10;
    line.baseline.descent = 0;
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_y(&line, &offset), ROBO_OK);
    ROBO_CHECK_EQ_INT(offset, INT32_MAX);

    layout_baseline_end(&fixture);
}

static void test_baseline_allocator_balance(void)
{
    static const layout_baseline_rule_t rules[] = {
        LAYOUT_BASELINE_BLOCK_RULES,
        { "span", ROBO_CSS_DISPLAY_INLINE, 0.0, 0.0 }
    };
    layout_baseline_fixture_t fixture;
    layout_baseline_script_t script;
    robo_layout_text_metrics_t measurer;
    size_t before;
    size_t settled;
    int pass;

    layout_fixture();

    /*
     * Taken **before the tree exists**, so the final assertion brackets the whole
     * lifetime of the module's storage rather than only the passes: nothing a baseline
     * costs may outlive the tree that owned it. Equality rather than a strict decrease,
     * for the reason every other `allocator_balance` case in this directory gives -- the
     * counter behind `layout_live_blocks` is a debug-build instrument and reads 0 in a
     * release build, so an assertion that demanded a drop would pass in Debug and fail
     * in Release while saying nothing about baselines in either configuration.
     */
    before = layout_live_blocks();
    layout_baseline_begin(&fixture, "<html><body><p><span>abc</span></p></body></html>",
                          rules, LAYOUT_BASELINE_RULE_COUNT(rules));

    memset(&script, 0, sizeof(script));
    script.per_code_point = 10;
    script.ascent = 12;
    script.descent = 4;
    script.gap = 2;
    measurer = layout_baseline_script_metrics(&script);

    /*
     * One *settled* pass, so the loop's baseline is taken after the arena has reached
     * its working capacity -- an arena that reserves on demand and keeps what it
     * reserved is not a leak, and a case that called it one would start failing as soon
     * as the document needed a second line.
     *
     * What the loop then asserts is the *repeat*: four further passes over an unchanged
     * tree must leave the count exactly where the settled pass found it. The phase added
     * no arena of its own -- a baseline lives on the object and on the line -- so the
     * number must not move at all.
     */
    layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    settled = layout_live_blocks();

    for (pass = 0; pass < 4; pass++) {
        layout_baseline_pass(&fixture, LAYOUT_BASELINE_WIDE_VIEWPORT, &measurer, NULL);
        ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
        ROBO_CHECK_EQ_INT(layout_live_blocks(), settled);
    }

    layout_baseline_end(&fixture);
    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_layout_baseline(void)
{
    robo_test_register("layout.baseline.kind_vocabulary", test_baseline_kind_vocabulary);
    robo_test_register("layout.baseline.metrics_record", test_baseline_metrics_record);
    robo_test_register("layout.baseline.metrics_from_run", test_baseline_metrics_from_run);
    robo_test_register("layout.baseline.metrics_from_extent",
                       test_baseline_metrics_from_extent);
    robo_test_register("layout.baseline.line_from_text", test_baseline_line_from_text);
    robo_test_register("layout.baseline.object_metrics", test_baseline_object_metrics);
    robo_test_register("layout.baseline.shared_floor", test_baseline_shared_floor);
    robo_test_register("layout.baseline.mixed_size_is_not_top_aligned",
                       test_baseline_mixed_size_is_not_top_aligned);
    robo_test_register("layout.baseline.descent_deepens_the_line",
                       test_baseline_descent_deepens_the_line);
    robo_test_register("layout.baseline.multiple_lines", test_baseline_multiple_lines);
    robo_test_register("layout.baseline.empty_content", test_baseline_empty_content);
    robo_test_register("layout.baseline.relayout", test_baseline_relayout);
    robo_test_register("layout.baseline.determinism", test_baseline_determinism);
    robo_test_register("layout.baseline.accounting", test_baseline_accounting);
    robo_test_register("layout.baseline.validation", test_baseline_validation);
    robo_test_register("layout.baseline.allocator_balance", test_baseline_allocator_balance);
}
