/*
 * RoboLayout integration suite (Phase 10.15) — layout.integration.*
 *
 * Phase 10.15 is the goal that consolidates the layout test layer, and this file is
 * the one thing the twenty suites before it could not be: the place where the
 * *families* are asked to compose. Every other suite in this directory proves one
 * layer against its own declarations -- sizing against a `width`, margin against a
 * `margin`, border against a `border`, position against an `inset`, overflow against
 * a `overflow` -- and a build that got each layer right *and composed them wrongly*
 * would pass all of them. What that looks like in practice is a content origin
 * computed from the padding but not the border, a final position that is the offset
 * but not the flow, a parent that resizes itself to hold an overflowing child, or a
 * region that folds in a clipped child's *contents*. Each of those is one line of
 * wrong arithmetic that no single-family assertion can see, and each of them is
 * asked here on a real document through the real chain:
 *
 *   RoboHTML -> RoboCSS -> the bridge -> the tree -> the pass
 *
 * The cases fall into four groups and the groups are the phase's requirements:
 *
 *   composition    a real stylesheet whose boxes use several features at once, with
 *                  the *derived* relationships asserted (content origin, content
 *                  width, margin-box stacking, line-to-baseline) rather than a pile
 *                  of magic numbers -- because the composition is the claim;
 *   determinism    one document laid out repeatedly -- and built into a second tree
 *                  -- with every result, inset, offset, overflow record and line
 *                  compared field by field, not merely counted;
 *   robustness     extreme input (a dimension near the geometry's maximum, a zero,
 *                  a negative margin, an unusable declaration, a deep chain), all
 *                  of which must produce finite, deterministic geometry;
 *   regressions    the *documented findings* of 10.11, 10.13 and 10.14, each pinned
 *                  as the minimal reproduction that failed before the fix, so a
 *                  later phase that reintroduces one fails here rather than in a
 *                  document nobody runs.
 *
 * The fixture is the conformance suite's -- a real author sheet through a real style
 * context -- because that is what makes a computed style the container the cascade
 * filled rather than a struct a test fabricated. The measurer is the line-box
 * suite's, because a document with no font measures every run as zero and would make
 * every line assertion vacuous.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite
 * depends on no Chromium header, library or test framework. See layout/docs/Tests.md
 * §20.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A scripted measurer                                                 */
/* ------------------------------------------------------------------ */

/**
 * One scripted answer: this many px per code point, and this line extent.
 *
 * The same shape `SuiteRoboLayoutLineBox.c` uses, and deliberately not shared with
 * it: a shared measurer would be a third thing to keep in step, and the two suites
 * vary it differently (that one varies the height to watch a line track its tallest
 * member; this one holds it fixed so a composed line's width is the sum of its
 * parts).
 */
typedef struct layout_int_script {
    int32_t per_code_point;
    int32_t height;
    size_t calls;
} layout_int_script_t;

static robo_result_t layout_int_measure(void *userdata,
                                        const robo_layout_text_request_t *request,
                                        robo_layout_text_run_t *out_run)
{
    layout_int_script_t *script = (layout_int_script_t *)userdata;

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

static robo_layout_text_metrics_t layout_int_metrics_of(layout_int_script_t *script)
{
    robo_layout_text_metrics_t metrics;

    robo_layout_text_metrics_init(&metrics);
    metrics.userdata = script;
    metrics.measure = layout_int_measure;
    return metrics;
}

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/** Everything one integration document needs: DOM, bridge, styles, tree. */
typedef struct layout_int_fixture {
    layout_document_t doc;
    layout_css_fixture_t css;
    robo_layout_dom_t *dom;
    robo_layout_tree_t *tree;
    robo_layout_style_source_t source;
} layout_int_fixture_t;

static void layout_int_begin(layout_int_fixture_t *fixture, const char *html, const char *css)
{
    robo_layout_build_options_t options;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    layout_css_begin(&fixture->css);
    layout_css_sheet(&fixture->css, css);
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(fixture->css.context, fixture->doc.id,
                                             &fixture->dom, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(fixture->dom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(fixture->dom, &fixture->source, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->source;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      ROBO_OK);
}

static void layout_int_end(layout_int_fixture_t *fixture)
{
    /* The tree borrows the bridge's styles, so it goes first. */
    robo_layout_tree_destroy(fixture->tree);
    fixture->tree = NULL;
    robo_layout_dom_destroy(fixture->dom);
    fixture->dom = NULL;
    layout_css_end(&fixture->css);
    layout_document_release(&fixture->doc);
}

/** A context with a viewport, and optionally a measurer -- nothing more. */
static robo_layout_context_t *layout_int_context(int32_t width,
                                                 int32_t height,
                                                 const robo_layout_text_metrics_t *metrics)
{
    robo_layout_context_t *context = NULL;
    robo_layout_size_t viewport;

    viewport.width = width;
    viewport.height = height;
    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_set_viewport(context, viewport, NULL), ROBO_OK);
    if (metrics) {
        ROBO_CHECK_EQ_INT(robo_layout_context_set_text_metrics(context, metrics), ROBO_OK);
    }
    return context;
}

/**
 * One full pass, with the context destroyed afterwards.
 *
 * The diagnostic is captured and *reported* rather than passed as NULL, which is the
 * harness half of requirement 21: a pass that refuses a document says why, and a
 * suite that discards the message turns "the pass failed" into an evening of
 * bisecting a fixture. The message is static text owned by the module, so printing
 * it here costs a pointer and no lifetime question.
 */
static void layout_int_run(layout_int_fixture_t *fixture,
                           int32_t width,
                           int32_t height,
                           const robo_layout_text_metrics_t *metrics,
                           robo_layout_stats_t *out_stats)
{
    robo_layout_context_t *context = layout_int_context(width, height, metrics);
    robo_layout_diag_t diag;
    robo_layout_stats_t stats;
    robo_result_t status;

    robo_layout_diag_reset(&diag);
    status = robo_layout_context_run(context, fixture->tree, &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the pass refused the document: %s (%s:%d)",
                       diag.message ? diag.message : "(no message)",
                       diag.file ? diag.file : "?", diag.line);
    }
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                      (int)ROBO_LAYOUT_PASS_WALKED);
    robo_layout_context_stats(context, &stats);
    robo_layout_context_destroy(context);
    if (out_stats) {
        *out_stats = stats;
    }
}

/**
 * The box built for the first element named @p tag, asserted to exist.
 *
 * Every integration document below uses a distinct tag per element it asserts, which
 * is why this can be a name lookup rather than an index: a document with two `<div>`s
 * and an index-based helper would silently assert the wrong one the day a fixture
 * gains a wrapper, and `layout_document_find` is the same walk the builder performed.
 */
static robo_layout_node_id_t layout_int_box(layout_int_fixture_t *fixture, const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);
    robo_layout_node_id_t id;

    ROBO_CHECK_EQ_INT((int)robo_html_node_handle_is_wellformed(&element), 1);
    id = robo_layout_object_id_for_dom_node(fixture->tree, &element);
    ROBO_CHECK(id != ROBO_LAYOUT_NODE_ID_NONE);
    return id;
}

/** A placed box's final rectangle, asserting that geometry exists. Zeroed first, so
 *  a state that is not COMPUTED reads as a stated absence rather than a leftover. */
static void layout_int_rect(layout_int_fixture_t *fixture,
                            robo_layout_node_id_t id,
                            robo_layout_rect_t *out)
{
    robo_layout_result_t result;

    memset(out, 0, sizeof(*out));
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_result_state(&result),
                      (int)ROBO_LAYOUT_RESULT_COMPUTED);
    if (robo_layout_result_rect(&result, out, NULL) != ROBO_OK) {
        memset(out, 0, sizeof(*out));
    }
}

/** A placed box's size, asserting that geometry exists. */
static robo_layout_size_t layout_int_size(layout_int_fixture_t *fixture,
                                          robo_layout_node_id_t id)
{
    robo_layout_result_t result;
    robo_layout_size_t size;

    memset(&size, 0, sizeof(size));
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_result_state(&result),
                      (int)ROBO_LAYOUT_RESULT_COMPUTED);
    if (robo_layout_result_size(&result, &size) != ROBO_OK) {
        memset(&size, 0, sizeof(size));
    }
    return size;
}

/* ------------------------------------------------------------------ */
/* Assertions                                                          */
/* ------------------------------------------------------------------ */

/*
 * Macros rather than helpers, and that is the diagnostics requirement rather than a
 * style choice: a `static` helper that called `ROBO_CHECK_EQ_INT` would report the
 * *header's* line and not the case's, which turns "which box was wrong?" into a
 * bisect. These expand inside the case, so a failure names the case's own line, the
 * box's label, and both rectangles in full -- the four numbers a reader needs and
 * would otherwise have to add a print to get.
 */

#define LAYOUT_INT_CHECK_RECT(label, fixture, id, ex, ey, ew, eh)                   \
    do {                                                                            \
        robo_layout_rect_t _rect;                                                   \
        layout_int_rect((fixture), (id), &_rect);                                   \
        if (_rect.x != (ex) || _rect.y != (ey) || _rect.width != (ew) ||            \
            _rect.height != (eh)) {                                                 \
            robo_test_fail(__FILE__, __LINE__,                                      \
                           "%s: rect (%d,%d %dx%d) != (%d,%d %dx%d)", (label),      \
                           (int)_rect.x, (int)_rect.y, (int)_rect.width,            \
                           (int)_rect.height, (int)(ex), (int)(ey), (int)(ew),      \
                           (int)(eh));                                              \
        }                                                                           \
    } while (0)

#define LAYOUT_INT_CHECK_SIZE(label, fixture, id, ew, eh)                           \
    do {                                                                            \
        robo_layout_size_t _size = layout_int_size((fixture), (id));                \
        if (_size.width != (ew) || _size.height != (eh)) {                          \
            robo_test_fail(__FILE__, __LINE__, "%s: size %dx%d != %dx%d", (label),  \
                           (int)_size.width, (int)_size.height, (int)(ew),          \
                           (int)(eh));                                              \
        }                                                                           \
    } while (0)

/** The four used margins of a box, by value. */
static robo_layout_margin_edges_t layout_int_margins(layout_int_fixture_t *fixture,
                                                     robo_layout_node_id_t id)
{
    robo_layout_margin_edges_t edges;

    robo_layout_margin_edges_init(&edges, 0);
    ROBO_CHECK_EQ_INT(robo_layout_object_margins(fixture->tree, id, &edges, NULL), ROBO_OK);
    return edges;
}

static robo_layout_padding_edges_t layout_int_padding(layout_int_fixture_t *fixture,
                                                      robo_layout_node_id_t id)
{
    robo_layout_padding_edges_t edges;

    robo_layout_padding_edges_init(&edges);
    ROBO_CHECK_EQ_INT(robo_layout_object_padding(fixture->tree, id, &edges, NULL), ROBO_OK);
    return edges;
}

static robo_layout_border_edges_t layout_int_borders(layout_int_fixture_t *fixture,
                                                     robo_layout_node_id_t id)
{
    robo_layout_border_edges_t edges;

    robo_layout_border_edges_init(&edges);
    ROBO_CHECK_EQ_INT(robo_layout_object_borders(fixture->tree, id, &edges, NULL), ROBO_OK);
    return edges;
}

static robo_layout_overflow_edges_t layout_int_overflow(layout_int_fixture_t *fixture,
                                                        robo_layout_node_id_t id)
{
    robo_layout_overflow_edges_t edges;

    robo_layout_overflow_edges_init(&edges);
    ROBO_CHECK_EQ_INT(robo_layout_object_overflow(fixture->tree, id, &edges, NULL), ROBO_OK);
    return edges;
}

static robo_layout_position_offset_t layout_int_offset(layout_int_fixture_t *fixture,
                                                       robo_layout_node_id_t id)
{
    robo_layout_position_offset_t offset;

    robo_layout_position_offset_init(&offset);
    ROBO_CHECK_EQ_INT(robo_layout_object_position(fixture->tree, id, &offset, NULL), ROBO_OK);
    return offset;
}

/**
 * The **page** origin of @p id: its own rect, plus every ancestor's rect and insets.
 *
 * A result's position is in its **containing block's content-box space** (the module's
 * stated coordinate space), so the four insets of every ancestor on the path are what
 * turn a local position into a page position. This helper performs exactly that walk
 * and nothing else, which makes it the assertion that the *nesting composes*: a build
 * whose child positions were already absolute would double-count here, and one whose
 * parent content origin forgot a border would be short by it.
 *
 * The root's own rect is the origin of the space, so the loop adds it like any other
 * box and the root's insets are zero.
 */
static robo_layout_point_t layout_int_page_origin(layout_int_fixture_t *fixture,
                                                  robo_layout_node_id_t id)
{
    robo_layout_point_t point;
    robo_layout_node_id_t walk = id;

    point.x = 0;
    point.y = 0;
    if (walk == ROBO_LAYOUT_NODE_ID_NONE) {
        return point;
    }

    /* The box's own rect is already stated in its containing block's content space,
     * so it is added once and its *own* insets are not: those are what turn its
     * content origin into a space for its children, and this box is not its own
     * child. They come back in at the next step, for the parent. */
    {
        robo_layout_rect_t rect;

        layout_int_rect(fixture, walk, &rect);
        point.x += rect.x;
        point.y += rect.y;
    }

    while (robo_layout_object_parent(fixture->tree, walk, &walk) == ROBO_OK) {
        robo_layout_rect_t rect;
        robo_layout_border_edges_t borders;
        robo_layout_padding_edges_t padding;

        layout_int_rect(fixture, walk, &rect);
        borders = layout_int_borders(fixture, walk);
        padding = layout_int_padding(fixture, walk);
        point.x += rect.x + borders.left.width + padding.left;
        point.y += rect.y + borders.top.width + padding.top;
    }
    return point;
}

/**
 * The content-box origin of @p id, derived the way a consumer must: the final rect's
 * origin, plus the border, plus the padding -- and in @p id's owner's space, because
 * that is the space `result.position` is stated in.
 */
static robo_layout_point_t layout_int_content_origin(layout_int_fixture_t *fixture,
                                                     robo_layout_node_id_t id)
{
    robo_layout_rect_t rect;
    robo_layout_border_edges_t borders = layout_int_borders(fixture, id);
    robo_layout_padding_edges_t padding = layout_int_padding(fixture, id);
    robo_layout_point_t point;

    layout_int_rect(fixture, id, &rect);
    point.x = rect.x + borders.left.width + padding.left;
    point.y = rect.y + borders.top.width + padding.top;
    return point;
}

/** The width a box's contents have: the border box less the borders and the padding. */
static int32_t layout_int_content_width(layout_int_fixture_t *fixture,
                                        robo_layout_node_id_t id)
{
    robo_layout_rect_t rect;
    robo_layout_border_edges_t borders = layout_int_borders(fixture, id);
    robo_layout_padding_edges_t padding = layout_int_padding(fixture, id);

    layout_int_rect(fixture, id, &rect);
    return rect.width - borders.left.width - borders.right.width - padding.left -
           padding.right;
}

/* ------------------------------------------------------------------ */
/* 1. Composition: the box model, composed                             */
/* ------------------------------------------------------------------ */

/**
 * One document whose every box is 200 wide and 100 tall *inside* a margin, a border
 * and a padding -- and the composition asserted rather than the declarations.
 *
 * The numbers are chosen so that a build which read one layer and not the next is
 * caught by an exact value: `.outer`'s border box is 214 x 114 and not 200 x 100
 * (which is what a build that stopped at the sizing layer would say), not 210 x 110
 * (padding and no border), and `.inner`'s is 58 x 26 while the *width its text
 * content may use* is 50. The `body`'s height is the outer's **margin box** --
 * 10 + 114 + 10 -- which is the one number that proves margins are external to the
 * border box rather than part of it.
 */
#define LAYOUT_INT_MODEL_HTML                          \
    "<html><body>"                                     \
    "<div class=\"outer\"><section class=\"inner\"></section></div>" \
    "</body></html>"

#define LAYOUT_INT_MODEL_CSS                                                \
    "html, body { display: block }\n"                                       \
    ".outer { display: block; width: 200px; height: 100px;\n"               \
    "         margin: 10px 20px; padding: 5px; border: 2px solid black }\n" \
    ".inner { display: block; width: 50px; height: 20px;\n"                 \
    "         margin: 4px; padding: 3px; border: 1px solid black }\n"

static void test_integration_box_model(void)
{
    layout_int_fixture_t fixture;
    robo_layout_node_id_t html_box;
    robo_layout_node_id_t body_box;
    robo_layout_node_id_t outer;
    robo_layout_node_id_t inner;
    robo_layout_margin_edges_t margins;
    robo_layout_border_edges_t borders;
    robo_layout_padding_edges_t padding;
    robo_layout_point_t origin;
    robo_layout_stats_t stats;

    layout_fixture();
    layout_int_begin(&fixture, LAYOUT_INT_MODEL_HTML, LAYOUT_INT_MODEL_CSS);

    /* html, head, body, div, section -- and `<head>` is the parser's, which is why
     * it is here and why it is the one box the pass declines. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 5u);

    html_box = layout_int_box(&fixture, "html");
    body_box = layout_int_box(&fixture, "body");
    outer = layout_int_box(&fixture, "div");
    inner = layout_int_box(&fixture, "section");

    layout_int_run(&fixture, 800, 600, NULL, &stats);
    ROBO_CHECK_EQ_INT(stats.objects, 5u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, 4u);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 1u); /* `<head>`: inline, nothing styles it */
    ROBO_CHECK_EQ_INT(stats.block.inline_level_skipped, 1u);

    /* The chain: the root fills the viewport, `body` fills the root, and the root's
     * height is `body`'s margin box. */
    LAYOUT_INT_CHECK_RECT("html", &fixture, html_box, 0, 0, 800, 134);
    LAYOUT_INT_CHECK_RECT("body", &fixture, body_box, 0, 0, 800, 134);

    /*
     * The outer box: placed at its **margin origin** (20, 10) -- the containing
     * block's content origin plus the two margins -- and sized as its **border**
     * box, which is the declared 200 x 100 *content* plus 5px of padding and 2px of
     * border on each side.
     */
    LAYOUT_INT_CHECK_RECT("div.outer", &fixture, outer, 20, 10, 214, 114);
    LAYOUT_INT_CHECK_SIZE("div.outer", &fixture, outer, 214, 114);

    margins = layout_int_margins(&fixture, outer);
    ROBO_CHECK_EQ_INT(margins.top, 10);
    ROBO_CHECK_EQ_INT(margins.right, 20);
    ROBO_CHECK_EQ_INT(margins.bottom, 10);
    ROBO_CHECK_EQ_INT(margins.left, 20);
    ROBO_CHECK_EQ_INT(margins.resolved, 4u);
    ROBO_CHECK_EQ_INT(margins.autos_used, 0u);

    padding = layout_int_padding(&fixture, outer);
    ROBO_CHECK_EQ_INT(padding.top, 5);
    ROBO_CHECK_EQ_INT(padding.left, 5);
    ROBO_CHECK_EQ_INT(padding.resolved, 4u);

    borders = layout_int_borders(&fixture, outer);
    ROBO_CHECK_EQ_INT(borders.top.width, 2);
    ROBO_CHECK_EQ_INT(borders.left.width, 2);
    ROBO_CHECK_EQ_INT(borders.borders_drawn, 4u);
    ROBO_CHECK_EQ_INT(borders.widths_resolved, 4u);

    /* The composition, stated as the relationship rather than as two numbers: the
     * content origin is the border-box origin plus the border plus the padding. */
    origin = layout_int_content_origin(&fixture, outer);
    ROBO_CHECK_EQ_INT(origin.x, 20 + 2 + 5);
    ROBO_CHECK_EQ_INT(origin.y, 10 + 2 + 5);
    /* And the content width is the declared 200, which is the `content-box` model
     * seen from the inside: not the 214 the box occupies and not the 204 a
     * `border-box` reading of the declaration would leave the contents. */
    ROBO_CHECK_EQ_INT(layout_int_content_width(&fixture, outer), 200);

    /*
     * The inner box, whose numbers are the same composition one level down -- and
     * whose origin is the *outer's content origin* plus its own margin, which is
     * the assertion that makes the two boxes a chain rather than two islands.
     */
    /* A child's position is in its containing block's content-box space, so this is
     * the child's own margins -- 4 and 4 -- and not a page coordinate. The box is
     * 50 + 6 + 2 by 20 + 6 + 2: the declared pair plus its padding plus its border. */
    LAYOUT_INT_CHECK_RECT("section.inner", &fixture, inner, 4, 4, 58, 28);
    ROBO_CHECK_EQ_INT(layout_int_content_width(&fixture, inner), 50);
    ROBO_CHECK_EQ_INT(layout_int_page_origin(&fixture, outer).x, 20);
    ROBO_CHECK_EQ_INT(layout_int_page_origin(&fixture, outer).y, 10);
    /* And one level down the page origin *is* the composition: the outer's content
     * origin plus the inner's own margins. Asserted rather than assumed, because a
     * build that reported absolute positions in the child would fail only here. */
    ROBO_CHECK_EQ_INT(layout_int_page_origin(&fixture, inner).x, 27 + 4);
    ROBO_CHECK_EQ_INT(layout_int_page_origin(&fixture, inner).y, 17 + 4);

    layout_int_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 2. Composition: inline, text and the baseline                       */
/* ------------------------------------------------------------------ */

/**
 * A paragraph of text, a transparent inline box around some of it, and more text --
 * which is 10.2's classification, 10.4's placement, 10.5's measurement and 10.6/10.7's
 * line and baseline, all on one document.
 *
 * The measurer answers 10px per code point and a 16px line extent, so the three text
 * runs are 30, 50 and 10 wide and the line is 90 -- the sum, which is the whole point:
 * a build that placed the runs but sized the line from one of them would be caught by
 * the total and not by any individual run.
 */
#define LAYOUT_INT_TEXT_HTML "<html><body><p>Hi <span>there</span>!</p></body></html>"

#define LAYOUT_INT_TEXT_CSS "html, body, p { display: block }\n"

static void test_integration_inline_text(void)
{
    layout_int_fixture_t fixture;
    layout_int_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_node_id_t paragraph;
    robo_layout_node_id_t span;
    robo_layout_line_box_t line;
    robo_layout_line_id_t line_id = ROBO_LAYOUT_LINE_ID_NONE;
    robo_layout_size_t size;
    int32_t baseline_offset = 0;
    int32_t baseline_y = 0;
    robo_layout_stats_t stats;
    size_t members = 0u;

    script.per_code_point = 10;
    script.height = 16;
    script.calls = 0u;
    metrics = layout_int_metrics_of(&script);

    layout_fixture();
    layout_int_begin(&fixture, LAYOUT_INT_TEXT_HTML, LAYOUT_INT_TEXT_CSS);

    /* html, head, body, p, span = five elements, plus the three text nodes:
     * "Hi ", "there" (inside the span) and "!". */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 8u);

    paragraph = layout_int_box(&fixture, "p");
    span = layout_int_box(&fixture, "span");

    layout_int_run(&fixture, 400, 300, &metrics, &stats);
    ROBO_CHECK_EQ_INT(stats.inline_flow.contexts, 1u);
    ROBO_CHECK_EQ_INT(stats.text.measured, 3u);
    ROBO_CHECK_EQ_INT(stats.text.unmeasured, 0u);
    ROBO_CHECK_EQ_INT(script.calls, 3u);

    /* One line, and it is 90 wide because it holds all three runs. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 1u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(fixture.tree, 0u, &line_id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_line(fixture.tree, line_id, &line, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(line.origin.x, 0);
    ROBO_CHECK_EQ_INT(line.origin.y, 0);
    ROBO_CHECK_EQ_INT(line.size.width, 90);
    ROBO_CHECK_EQ_INT(line.size.height, 16);
    ROBO_CHECK_EQ_INT((int)line.owner, (int)paragraph);
    ROBO_CHECK_EQ_INT(line.available_width, 400);

    /* Three placements and not four: the span is a *participant* of the context and a
     * member of no line, because its rectangle is the union of its contents (10.6). */
    members = line.members;
    ROBO_CHECK_EQ_INT(members, 3u);

    /* The baseline is the line's floor, its height *is* ascent+descent, and the two
     * doors agree: `baseline_offset` is line-relative, `baseline_y` is the owner's
     * content-box space, and this line's owner begins at the page origin. */
    ROBO_CHECK_EQ_INT(line.baseline.ascent, 16);
    ROBO_CHECK_EQ_INT(line.baseline.descent, 0);
    ROBO_CHECK_EQ_INT(line.size.height, line.baseline.ascent + line.baseline.descent);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_offset(&line, &baseline_offset), ROBO_OK);
    ROBO_CHECK_EQ_INT(baseline_offset, 16);
    ROBO_CHECK_EQ_INT(robo_layout_line_box_baseline_y(&line, &baseline_y), ROBO_OK);
    ROBO_CHECK_EQ_INT(baseline_y, baseline_offset);

    /* The transparent span is on no line, which is the same fact as `members == 3`
     * stated from the other side. */
    {
        robo_layout_line_id_t span_line = ROBO_LAYOUT_LINE_ID_NONE;
        ROBO_CHECK_EQ_INT(robo_layout_object_line(fixture.tree, span, &span_line, NULL),
                          ROBO_NOT_FOUND);
    }

    /* And the paragraph's height is the line's -- the text reached the containing
     * block the way 10.5 said it would. */
    size = layout_int_size(&fixture, paragraph);
    ROBO_CHECK_EQ_INT(size.height, 16);
    /* Its width is `auto` against `body`'s content width, less its own insets. */
    ROBO_CHECK_EQ_INT(layout_int_content_width(&fixture, paragraph), 400 - 0);

    layout_int_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Composition: position and overflow together                      */
/* ------------------------------------------------------------------ */

#define LAYOUT_INT_POS_HTML \
    "<html><body><div class=\"host\"><section class=\"child\"></section></div></body></html>"

#define LAYOUT_INT_POS_CSS                                                       \
    "html, body { display: block }\n"                                            \
    ".host { display: block; width: 100px; height: 50px;\n"                      \
    "        position: relative; left: 10px; top: 5px; overflow: visible }\n"    \
    ".child { display: block; width: 200px; height: 200px }\n"

/**
 * A relatively positioned box whose child does not fit, asked twice: once with the
 * overflow `visible` and once `hidden`.
 *
 * The pair is the phase's smallest complete statement about what a second rectangle
 * is. `visible`: the box's own size is untouched, its *drawn* place is the flow place
 * displaced, its flow place is `final - offset`, and its contents are recorded as
 * 100px and 150px past its padding box on two sides. `hidden`: the *same* extents
 * stand -- they are a measurement of what the contents did, not of what survived --
 * while the derived **region** collapses back to the padding box and the **clip
 * rectangle** appears where it was refused before. A build that clipped the extents
 * themselves would pass the second assertion and fail the first, which is why both
 * are here.
 */
static void test_integration_position_overflow(void)
{
    layout_int_fixture_t fixture;
    robo_layout_node_id_t host;
    robo_layout_node_id_t child;
    robo_layout_node_id_t body_box;
    robo_layout_overflow_edges_t edges;
    robo_layout_position_offset_t offset;
    robo_layout_rect_t padding_box;
    robo_layout_rect_t region;
    robo_layout_rect_t clip;
    robo_layout_result_t result;
    robo_layout_point_t final_position;
    robo_layout_size_t size;

    layout_fixture();
    layout_int_begin(&fixture, LAYOUT_INT_POS_HTML, LAYOUT_INT_POS_CSS);

    host = layout_int_box(&fixture, "div");
    child = layout_int_box(&fixture, "section");
    body_box = layout_int_box(&fixture, "body");

    layout_int_run(&fixture, 800, 600, NULL, NULL);

    /*
     * The host's *final* position is its flow place displaced by the offset -- and
     * its flow place is the result minus the used offset, which is the only way a
     * consumer recovers the two coordinate spaces this phase introduced.
     */
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, host, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &final_position), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
    ROBO_CHECK_EQ_INT(final_position.x, 10);
    ROBO_CHECK_EQ_INT(final_position.y, 5);

    offset = layout_int_offset(&fixture, host);
    ROBO_CHECK_EQ_INT(offset.dx, 10);
    ROBO_CHECK_EQ_INT(offset.dy, 5);
    ROBO_CHECK_EQ_INT(offset.insets_resolved, 2u);

    /* The flow place, recovered: the parent stacks by it, so it is the number that
     * must not have moved. `position: relative` never removes a box from flow. */
    ROBO_CHECK_EQ_INT(final_position.x - offset.dx, 0);
    ROBO_CHECK_EQ_INT(final_position.y - offset.dy, 0);

    /* The size is the declared pair and nothing about the displacement: a build that
     * added the offset into the size would say 110 or 210 here. */
    ROBO_CHECK_EQ_INT(size.width, 100);
    ROBO_CHECK_EQ_INT(size.height, 50);
    LAYOUT_INT_CHECK_RECT("div.host", &fixture, host, 10, 5, 100, 50);

    /* The child is placed in the host's *flow* space -- its content origin is the
     * host's flow origin, not the displaced one -- because flow is what placement
     * uses. Its own rect is therefore at (0, 0), and the host's rect is elsewhere. */
    LAYOUT_INT_CHECK_RECT("section.child", &fixture, child, 0, 0, 200, 200);

    /* The measurement: how far the contents reached past the **padding box**, which
     * for a box with no border or padding is its border box. */
    edges = layout_int_overflow(&fixture, host);
    ROBO_CHECK_EQ_INT((int)edges.horizontal, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT((int)edges.vertical, (int)ROBO_CSS_OVERFLOW_VISIBLE);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.top, 0);
    ROBO_CHECK_EQ_INT(edges.right, 100);  /* 200 wide child, 100 wide box */
    ROBO_CHECK_EQ_INT(edges.bottom, 150); /* 200 tall child, 50 tall box */

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, host, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_result_rect(&result, &padding_box, NULL), ROBO_OK);
    /* The region a parent would fold in, and the clip a paint pass would use: with
     * both axes `visible` the region is the padding box grown by the extents, and
     * there is no clip at all -- a refusal, not a zero rectangle. The region is
     * stated against the box it was derived from rather than against the origin, so
     * the assertion does not depend on where the box happens to be drawn. */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, padding_box, &region, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(region.x, padding_box.x - edges.left);
    ROBO_CHECK_EQ_INT(region.y, padding_box.y - edges.top);
    ROBO_CHECK_EQ_INT(region.width, 200);
    ROBO_CHECK_EQ_INT(region.height, 200);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(&edges, padding_box, &clip, NULL),
                      ROBO_NOT_FOUND);

    /*
     * And the *parent* did not resize: `body`'s extent is the host's margin box, and
     * 50 is the host's height -- a build that let an overflowing child grow its
     * ancestor would be 200 here.
     */
    LAYOUT_INT_CHECK_SIZE("body", &fixture, body_box, 800, 50);

    /* Now the same document with `overflow: hidden`, which changes the *derived*
     * rectangles and not the measurement. */
    layout_int_end(&fixture);
    layout_int_begin(&fixture, LAYOUT_INT_POS_HTML,
                     "html, body { display: block }\n"
                     ".host { display: block; width: 100px; height: 50px;\n"
                     "        overflow: hidden }\n"
                     ".child { display: block; width: 200px; height: 200px }\n");
    host = layout_int_box(&fixture, "div");
    layout_int_run(&fixture, 800, 600, NULL, NULL);

    edges = layout_int_overflow(&fixture, host);
    ROBO_CHECK_EQ_INT((int)edges.horizontal, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    ROBO_CHECK_EQ_INT((int)edges.vertical, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    /* The extents are unchanged: they say what the contents *did*. */
    ROBO_CHECK_EQ_INT(edges.right, 100);
    ROBO_CHECK_EQ_INT(edges.bottom, 150);

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, host, &result), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_result_rect(&result, &padding_box, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(padding_box.width, 100);
    ROBO_CHECK_EQ_INT(padding_box.height, 50);

    /* The region is clipped back to the padding box on both axes, and the clip
     * rectangle -- refused a moment ago -- is that box. */
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, padding_box, &region, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(region.x, padding_box.x);
    ROBO_CHECK_EQ_INT(region.y, padding_box.y);
    ROBO_CHECK_EQ_INT(region.width, 100);
    ROBO_CHECK_EQ_INT(region.height, 50);
    ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_clip_rect(&edges, padding_box, &clip, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(clip.width, 100);
    ROBO_CHECK_EQ_INT(clip.height, 50);

    layout_int_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. Composition: a nested tree, asserted whole                       */
/* ------------------------------------------------------------------ */

#define LAYOUT_INT_NESTED_HTML                                          \
    "<html><body><main>"                                                \
    "<section><span>Hello</span></section>"                             \
    "<div>World</div>"                                                  \
    "</main></body></html>"

#define LAYOUT_INT_NESTED_CSS                                                    \
    "html, body, main, section, div { display: block }\n"                        \
    "main { width: 300px; margin: 10px; padding: 10px; border: 1px solid black }\n" \
    "section { width: 100px; height: 40px; margin: 5px; padding: 5px;\n"          \
    "          border: 1px solid black }\n"                                       \
    "div { width: 80px; height: 20px; margin: 5px }\n"

/**
 * The document an integration test is most likely to be written for -- a wrapper, a
 * nested block with text, and a following sibling -- asserted box by box.
 *
 * Two things here are the composition rather than any one layer. `div`'s y is
 * *not* `section`'s y plus its height: the sibling is stacked by the **margin box**
 * of the box before it, which is margin-top + border box + margin-bottom, so the
 * 5px margins on both sides of both boxes appear in the following sibling's place.
 * And `main`'s height is the extent of those two margin boxes rather than of the two
 * border boxes -- 92 inside a 114 border box -- which is the same rule seen from the
 * parent.
 */
static void test_integration_nested_tree(void)
{
    layout_int_fixture_t fixture;
    robo_layout_node_id_t main_box;
    robo_layout_node_id_t section;
    robo_layout_node_id_t div;
    robo_layout_node_id_t body_box;
    robo_layout_point_t origin;
    layout_int_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_stats_t stats;

    script.per_code_point = 8;
    script.height = 12;
    script.calls = 0u;
    metrics = layout_int_metrics_of(&script);

    layout_fixture();
    layout_int_begin(&fixture, LAYOUT_INT_NESTED_HTML, LAYOUT_INT_NESTED_CSS);

    /* html, head, body, main, section, span, div = 7 elements, plus "Hello" and
     * "World" = 9 objects. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 9u);

    main_box = layout_int_box(&fixture, "main");
    section = layout_int_box(&fixture, "section");
    div = layout_int_box(&fixture, "div");
    body_box = layout_int_box(&fixture, "body");

    layout_int_run(&fixture, 600, 400, &metrics, &stats);

    ROBO_CHECK_EQ_INT(stats.objects, 9u);
    /* Two inline formatting contexts -- the span's contents and the div's -- so two
     * lines, and both are measured. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree), 2u);
    ROBO_CHECK_EQ_INT(stats.text.measured, 2u);

    /* main: margin origin (10, 10), border box 300+20+2 by 92+20+2. */
    LAYOUT_INT_CHECK_RECT("main", &fixture, main_box, 10, 10, 322, 114);
    origin = layout_int_content_origin(&fixture, main_box);
    ROBO_CHECK_EQ_INT(origin.x, 10 + 1 + 10);
    ROBO_CHECK_EQ_INT(origin.y, 10 + 1 + 10);
    ROBO_CHECK_EQ_INT(layout_int_content_width(&fixture, main_box), 300);

    /* section: its own 5px margin in `main`'s content space, sized 100+10+2 by
     * 40+10+2. */
    LAYOUT_INT_CHECK_RECT("section", &fixture, section, 5, 5, 112, 52);
    ROBO_CHECK_EQ_INT(layout_int_content_width(&fixture, section), 100);

    /* The following sibling, placed after section's *margin box*: 5 + 52 + 5 = 62 for
     * the flow cursor, then its own margin-top 5 -- so the 5px margins on *both* sides
     * of both boxes are in this number, which is the rule rather than an accident. */
    ROBO_CHECK_EQ_INT(5 + 52 + 5 + 5, 67);
    LAYOUT_INT_CHECK_RECT("div", &fixture, div, 5, 67, 80, 20);

    /* And the two local positions composed into page ones: `main`'s content origin is
     * (21, 21), so the section sits at (26, 26) and the following sibling at
     * (26, 88) on the page -- the numbers the whole-document picture would show. */
    ROBO_CHECK_EQ_INT(origin.x + 5, 26);
    ROBO_CHECK_EQ_INT(origin.y + 5, 26);
    ROBO_CHECK_EQ_INT(layout_int_page_origin(&fixture, section).x, 26);
    ROBO_CHECK_EQ_INT(layout_int_page_origin(&fixture, section).y, 26);
    ROBO_CHECK_EQ_INT(layout_int_page_origin(&fixture, div).x, 26);
    ROBO_CHECK_EQ_INT(layout_int_page_origin(&fixture, div).y, 88);

    /* And the parent's height is the two margin boxes: (5+52+5) + (5+20+5) = 92
     * inside a 114 border box, which is what `body` then stacks by. */
    LAYOUT_INT_CHECK_RECT("body", &fixture, body_box, 0, 0, 600, 134);

    layout_int_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 5. Determinism, over every record                                   */
/* ------------------------------------------------------------------ */

/** Bounded because an integration fixture is a dozen boxes, not a document. */
#define LAYOUT_INT_STATE_MAX 96u
#define LAYOUT_INT_LINE_MAX 32u

/** One box's whole concluded state: its result, and every record a layer put on it. */
typedef struct layout_int_box_state {
    robo_layout_node_id_t id;
    robo_layout_result_t result;
    int has_margins;
    int has_padding;
    int has_borders;
    int has_offset;
    int has_overflow;
    robo_layout_margin_edges_t margins;
    robo_layout_padding_edges_t padding;
    robo_layout_border_edges_t borders;
    robo_layout_position_offset_t offset;
    robo_layout_overflow_edges_t overflow;
} layout_int_box_state_t;

/**
 * Snapshot every box and every line, and compare two snapshots field by field.
 *
 * Field by field rather than by `memcmp`: a struct's padding bytes are not part of
 * its value, and a comparison that read them would be testing the compiler. Every
 * record that has an `equal` door uses it, so the comparison is the module's own
 * definition of equality wherever it has one.
 */
static size_t layout_int_capture(layout_int_fixture_t *fixture, layout_int_box_state_t *out)
{
    size_t count = robo_layout_tree_object_count(fixture->tree);
    size_t index;

    ROBO_CHECK_EQ_INT(count <= LAYOUT_INT_STATE_MAX, 1);
    for (index = 0u; index < count && index < LAYOUT_INT_STATE_MAX; index++) {
        layout_int_box_state_t *state = &out[index];
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

        memset(state, 0, sizeof(*state));
        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture->tree, index, &id), ROBO_OK);
        state->id = id;

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture->tree, id, &state->result),
                          ROBO_OK);
        state->has_margins = robo_layout_object_margins(fixture->tree, id, &state->margins,
                                                       NULL) == ROBO_OK;
        state->has_padding = robo_layout_object_padding(fixture->tree, id, &state->padding,
                                                       NULL) == ROBO_OK;
        state->has_borders = robo_layout_object_borders(fixture->tree, id, &state->borders,
                                                       NULL) == ROBO_OK;
        state->has_offset = robo_layout_object_position(fixture->tree, id, &state->offset,
                                                        NULL) == ROBO_OK;
        state->has_overflow = robo_layout_object_overflow(fixture->tree, id,
                                                          &state->overflow, NULL) == ROBO_OK;
    }
    return count;
}

static void layout_int_compare_states(const layout_int_box_state_t *a,
                                      const layout_int_box_state_t *b,
                                      size_t count)
{
    size_t index;

    for (index = 0u; index < count; index++) {
        ROBO_CHECK_EQ_INT((int)a[index].id, (int)b[index].id);
        ROBO_CHECK_EQ_INT(robo_layout_result_equal(&a[index].result, &b[index].result), 1);
        ROBO_CHECK_EQ_INT(a[index].has_margins, b[index].has_margins);
        ROBO_CHECK_EQ_INT(a[index].has_padding, b[index].has_padding);
        ROBO_CHECK_EQ_INT(a[index].has_borders, b[index].has_borders);
        ROBO_CHECK_EQ_INT(a[index].has_offset, b[index].has_offset);
        ROBO_CHECK_EQ_INT(a[index].has_overflow, b[index].has_overflow);

        ROBO_CHECK_EQ_INT(a[index].margins.top, b[index].margins.top);
        ROBO_CHECK_EQ_INT(a[index].margins.right, b[index].margins.right);
        ROBO_CHECK_EQ_INT(a[index].margins.bottom, b[index].margins.bottom);
        ROBO_CHECK_EQ_INT(a[index].margins.left, b[index].margins.left);
        ROBO_CHECK_EQ_INT(a[index].margins.resolved, b[index].margins.resolved);
        ROBO_CHECK_EQ_INT(a[index].margins.autos_used, b[index].margins.autos_used);
        ROBO_CHECK_EQ_INT(a[index].margins.unresolved, b[index].margins.unresolved);

        ROBO_CHECK_EQ_INT(a[index].padding.top, b[index].padding.top);
        ROBO_CHECK_EQ_INT(a[index].padding.right, b[index].padding.right);
        ROBO_CHECK_EQ_INT(a[index].padding.bottom, b[index].padding.bottom);
        ROBO_CHECK_EQ_INT(a[index].padding.left, b[index].padding.left);

        ROBO_CHECK_EQ_INT(a[index].borders.top.width, b[index].borders.top.width);
        ROBO_CHECK_EQ_INT(a[index].borders.borders_drawn, b[index].borders.borders_drawn);

        ROBO_CHECK_EQ_INT(a[index].offset.dx, b[index].offset.dx);
        ROBO_CHECK_EQ_INT(a[index].offset.dy, b[index].offset.dy);

        ROBO_CHECK_EQ_INT((int)a[index].overflow.horizontal,
                          (int)b[index].overflow.horizontal);
        ROBO_CHECK_EQ_INT((int)a[index].overflow.vertical,
                          (int)b[index].overflow.vertical);
        ROBO_CHECK_EQ_INT(a[index].overflow.left, b[index].overflow.left);
        ROBO_CHECK_EQ_INT(a[index].overflow.top, b[index].overflow.top);
        ROBO_CHECK_EQ_INT(a[index].overflow.right, b[index].overflow.right);
        ROBO_CHECK_EQ_INT(a[index].overflow.bottom, b[index].overflow.bottom);
    }
}

/** Every line's record, compared with the module's own line equality. */
static void layout_int_compare_lines(layout_int_fixture_t *a, layout_int_fixture_t *b)
{
    size_t count = robo_layout_tree_line_count(a->tree);
    size_t index;

    ROBO_CHECK_EQ_INT(count, robo_layout_tree_line_count(b->tree));
    ROBO_CHECK_EQ_INT(count <= LAYOUT_INT_LINE_MAX, 1);

    for (index = 0u; index < count && index < LAYOUT_INT_LINE_MAX; index++) {
        robo_layout_line_id_t id_a = ROBO_LAYOUT_LINE_ID_NONE;
        robo_layout_line_id_t id_b = ROBO_LAYOUT_LINE_ID_NONE;
        robo_layout_line_box_t line_a;
        robo_layout_line_box_t line_b;

        ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(a->tree, index, &id_a, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_line_at(b->tree, index, &id_b, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)id_a, (int)id_b);
        ROBO_CHECK_EQ_INT(robo_layout_tree_line(a->tree, id_a, &line_a, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_line(b->tree, id_b, &line_b, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_line_box_equal(&line_a, &line_b), 1);
    }
}

#define LAYOUT_INT_DET_HTML                                             \
    "<html><body><main>"                                                \
    "<h1>Title</h1>"                                                    \
    "<p class=\"lead\">Alpha <em>beta</em></p>"                         \
    "<section class=\"card\"><span>card</span></section>"               \
    "<div class=\"tail\"></div>"                                        \
    "</main></body></html>"

#define LAYOUT_INT_DET_CSS                                                       \
    "html, body, main, h1, p, section, div { display: block }\n"                 \
    "main { width: 400px; margin: 8px; padding: 6px; border: 1px solid black }\n" \
    "h1 { height: 20px; margin: 2px }\n"                                         \
    "p { margin: 3px 4px; padding: 2px }\n"                                      \
    "section { height: 30px; margin: 2px; position: relative; left: 4px; top: 6px;\n" \
    "          overflow: hidden }\n"                                             \
    "div { height: 12px }\n"

/**
 * One document laid out three times -- and built into a second tree -- with every
 * box's geometry, every inset record, every offset, every overflow record and every
 * line compared.
 *
 * The claim is stronger than "the same call twice" on purpose. A pass that cached
 * something keyed by a pointer, ordered a walk by an address, or read an
 * uninitialised byte would agree with itself on the *first* two layouts and diverge
 * on the third, which is why there are three; and comparing the second *tree* as well
 * as the second *pass* covers the builder, whose ordering is the other half of "the
 * same document".
 */
static void test_integration_determinism(void)
{
    layout_int_fixture_t fixture;
    layout_int_fixture_t second;
    layout_int_script_t script;
    robo_layout_text_metrics_t metrics;
    layout_int_box_state_t first[LAYOUT_INT_STATE_MAX];
    layout_int_box_state_t again[LAYOUT_INT_STATE_MAX];
    size_t count;

    script.per_code_point = 9;
    script.height = 14;
    script.calls = 0u;
    metrics = layout_int_metrics_of(&script);

    layout_fixture();
    layout_int_begin(&fixture, LAYOUT_INT_DET_HTML, LAYOUT_INT_DET_CSS);
    layout_int_run(&fixture, 700, 500, &metrics, NULL);
    count = layout_int_capture(&fixture, first);
    ROBO_CHECK(count > 0u);

    /* The same pass again: nothing may drift, including the line ids. */
    layout_int_run(&fixture, 700, 500, &metrics, NULL);
    layout_int_capture(&fixture, again);
    layout_int_compare_states(first, again, count);

    /* And a third, because a build with a one-deep cache would agree twice. */
    layout_int_run(&fixture, 700, 500, &metrics, NULL);
    layout_int_capture(&fixture, again);
    layout_int_compare_states(first, again, count);

    /* A second *tree* from the same document, through the same bridge. */
    layout_int_begin(&second, LAYOUT_INT_DET_HTML, LAYOUT_INT_DET_CSS);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(second.tree), count);
    layout_int_run(&second, 700, 500, &metrics, NULL);
    layout_int_capture(&second, again);
    layout_int_compare_states(first, again, count);
    layout_int_compare_lines(&fixture, &second);

    /* The lines themselves, so the module's own equality is exercised rather than
     * merely available: the two trees' lines must be equal by *its* rule. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_line_count(fixture.tree),
                      robo_layout_tree_line_count(second.tree));
    ROBO_CHECK(robo_layout_tree_line_count(fixture.tree) > 0u);

    layout_int_end(&second);
    layout_int_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. Robustness: extreme input                                        */
/* ------------------------------------------------------------------ */

#define LAYOUT_INT_EXTREME_CSS                                                      \
    "html, body, div { display: block }\n"                                          \
    ".huge { width: 2000000000px; height: 2000000000px }\n"                         \
    ".maxw { width: 2147483647px; height: 2147483647px;\n"                          \
    "        padding: 10px; border: 5px solid black }\n"                            \
    ".zero { width: 0; height: 0 }\n"                                               \
    ".neg { width: 10px; height: 10px; margin-left: -50px; margin-top: -20px }\n"   \
    ".pct { width: 50%; height: 25% }\n"                                            \
    ".bad { width: not-a-length; height: 12px }\n"                                  \
    ".deep { width: 20px; height: 20px; margin: 1px }\n"

/**
 * A document made of the numbers the module has the most to lose on: a dimension near
 * the geometry's maximum, an arithmetic overflow in the box model, a zero, a negative
 * margin, a declaration this build cannot use, and a deep chain.
 *
 * The assertions are the *properties* rather than the numbers -- every placed box's
 * extent is finite and non-negative, every overflow extent is non-negative, a
 * saturated box is still drawable -- because the point of the case is that no input
 * produces a rectangle that is not a rectangle. The one exact number is the negative
 * margin, which must reach geometry *as a signed origin* rather than being clamped to
 * zero.
 */
static void test_integration_extreme_input(void)
{
    static char html[8192];
    static const char *const head =
        "<html><body>"
        "<div class=\"huge\"></div><div class=\"maxw\"></div>"
        "<div class=\"zero\"></div><div class=\"neg\"></div>"
        "<div class=\"pct\"></div><div class=\"bad\"></div>";
    static const char *const open_tag = "<div class=\"deep\">";
    static const char *const close_tag = "</div>";
    static const char *const tail = "</body></html>";
    layout_int_fixture_t fixture;
    robo_layout_node_id_t neg;
    robo_layout_stats_t stats;
    size_t written = 0u;
    size_t index;
    size_t total;
    size_t objects;

    /* Assembled by hand rather than with `sprintf`: the string is a fixture, and a
     * formatted build of one is a second thing that can disagree with the document
     * it claims to describe. */
    memcpy(html + written, head, strlen(head));
    written += strlen(head);
    for (index = 0u; index < 16u; index++) {
        memcpy(html + written, open_tag, strlen(open_tag));
        written += strlen(open_tag);
    }
    for (index = 0u; index < 16u; index++) {
        memcpy(html + written, close_tag, strlen(close_tag));
        written += strlen(close_tag);
    }
    memcpy(html + written, tail, strlen(tail));
    written += strlen(tail);
    html[written] = '\0';

    layout_fixture();
    layout_int_begin(&fixture, html, LAYOUT_INT_EXTREME_CSS);

    /* html, head, body, six named divs and sixteen nested ones. */
    objects = robo_layout_tree_object_count(fixture.tree);
    total = 3u + 6u + 16u;
    ROBO_CHECK_EQ_INT(objects, total);

    layout_int_run(&fixture, 800, 600, NULL, &stats);
    /* `<head>` is the parser's and nothing styles it, so it is the one box the pass
     * declines -- which is why the two counters disagree by exactly one here. */
    ROBO_CHECK_EQ_INT(stats.objects, objects);
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 1u);
    ROBO_CHECK_EQ_INT(stats.boxes_computed, objects - 1u);

    /* Every box: a finite, non-negative extent, and an overflow record whose extents
     * are non-negative too. A build that wrapped a saturated sum would fail here with
     * a negative width, which is the failure the 64-bit clamps exist to prevent. */
    for (index = 0u; index < objects; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_result_t result;
        robo_layout_size_t size;
        robo_layout_overflow_edges_t edges;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, index, &id), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &result), ROBO_OK);
        /* Every box but `<head>`, which is the one the pass declines. */
        if (robo_layout_object_is_root(fixture.tree, id) ||
            (int)robo_layout_result_state(&result) == (int)ROBO_LAYOUT_RESULT_COMPUTED) {
            ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_OK);
            ROBO_CHECK_MSG(size.width >= 0, "a placed size must be non-negative");
            ROBO_CHECK_MSG(size.height >= 0, "a placed size must be non-negative");
        } else {
            ROBO_CHECK_EQ_INT((int)robo_layout_result_state(&result),
                              (int)ROBO_LAYOUT_RESULT_UNRESOLVED);
        }
        if (robo_layout_object_overflow(fixture.tree, id, &edges, NULL) == ROBO_OK) {
            ROBO_CHECK_MSG(edges.left >= 0, "an overflow extent must be non-negative");
            ROBO_CHECK_MSG(edges.top >= 0, "an overflow extent must be non-negative");
            ROBO_CHECK_MSG(edges.right >= 0, "an overflow extent must be non-negative");
            ROBO_CHECK_MSG(edges.bottom >= 0, "an overflow extent must be non-negative");
        }
    }

    /* The huge box is as large as it says, which is the assertion a saturation that
     * wrapped would fail by going negative. */
    {
        robo_layout_node_id_t huge = layout_int_box(&fixture, "div");
        robo_layout_size_t size = layout_int_size(&fixture, huge);
        ROBO_CHECK_MSG(size.width >= 2000000000,
                       "a declared 2e9 width must reach geometry");
        ROBO_CHECK_MSG(size.height >= 2000000000,
                       "a declared 2e9 height must reach geometry");
    }

    /* The negative margin: an origin at minus the margin, signed and not clamped. */
    neg = ROBO_LAYOUT_NODE_ID_NONE;
    {
        /* The fourth `div` in document order is `.neg`, and `layout_document_find`
         * answers the first by tag -- so it is reached by walking the tree's own
         * order, which is document order. */
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
        size_t divs = 0u;
        for (index = 0u; index < objects && neg == ROBO_LAYOUT_NODE_ID_NONE; index++) {
            robo_layout_object_t object;
            ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, index, &id), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_layout_object_get(fixture.tree, id, &object, NULL), ROBO_OK);
            if (object.kind == ROBO_LAYOUT_OBJECT_BOX &&
                object.display == ROBO_CSS_DISPLAY_BLOCK &&
                object.depth == 2u) {
                /* The body's children, in order: huge, maxw, zero, neg, pct, bad,
                 * then the deep chain. The fourth is `neg`. */
                divs++;
                if (divs == 4u) {
                    neg = id;
                }
            }
        }
    }
    ROBO_CHECK(neg != ROBO_LAYOUT_NODE_ID_NONE);
    if (neg != ROBO_LAYOUT_NODE_ID_NONE) {
        robo_layout_result_t result;
        robo_layout_point_t position;

        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, neg, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_OK);
        /* 0 + margin-left(-50), and the flow cursor of the preceding three boxes plus
         * margin-top(-20) in the vertical -- the horizontal number is the one that
         * proves a sign survived, because a clamp to zero would answer 0. */
        ROBO_CHECK_EQ_INT(position.x, -50);
        ROBO_CHECK_EQ_INT(layout_int_margins(&fixture, neg).left, -50);
    }

    /* And the whole thing is deterministic however extreme it is. */
    if (stats.boxes_computed > 0u) {
        layout_int_run(&fixture, 800, 600, NULL, &stats);
        ROBO_CHECK_EQ_INT(stats.objects, objects);
    }

    layout_int_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 7. No stale geometry survives an incremental pass                   */
/* ------------------------------------------------------------------ */

/**
 * Mark, recompute, and prove the result is the same answer a fresh pass would give.
 *
 * The comparison is against a **fresh tree built from the same document and laid out
 * from scratch**, which is the only honest oracle: asserting the incremental geometry
 * against the numbers the same incremental path just wrote would pass even if both
 * were wrong. The `STYLE` half is here too, because the two doors mean different
 * things -- after the pass the tree must be free of *layout* bits while the paint
 * signal has been consumed by `mark_clean`, which is the phase's whole point stated
 * as a test.
 */
static void test_integration_no_stale_geometry(void)
{
    layout_int_fixture_t fixture;
    layout_int_fixture_t fresh;
    layout_int_script_t script;
    robo_layout_text_metrics_t metrics;
    layout_int_box_state_t incremental[LAYOUT_INT_STATE_MAX];
    layout_int_box_state_t from_scratch[LAYOUT_INT_STATE_MAX];
    robo_layout_context_t *context;
    robo_layout_node_id_t card;
    robo_layout_stats_t stats;
    size_t count;

    script.per_code_point = 7;
    script.height = 11;
    script.calls = 0u;
    metrics = layout_int_metrics_of(&script);

    layout_fixture();
    layout_int_begin(&fixture, LAYOUT_INT_DET_HTML, LAYOUT_INT_DET_CSS);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 0); /* born dirty */
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 1);

    context = layout_int_context(700, 500, &metrics);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 0);

    /* Touch a box's width: geometry-affecting, so the box, its ancestors'
     * measurements and the following flow go dirty -- and the pass must run. */
    card = layout_int_box(&fixture, "section");
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style_property(fixture.tree, card, "width",
                                                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(fixture.tree) > 0u, 1);
    /*
     * The dirty root is a *region* -- not the document and not the box that changed.
     * Marking `section` also marks the flow after it, and the flow after it is
     * `div.tail`, a sibling of `section`; so the lowest common ancestor of the two
     * stale boxes is `main`. Asserted exactly, because the difference between this
     * answer and "the shallowest stale box" is the finding 10.14 recorded.
     */
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_invalidation_root(fixture.tree),
                      (int)layout_int_box(&fixture, "main"));

    ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                      (int)ROBO_LAYOUT_PASS_WALKED);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(stats.invalidation.recomputed, 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);

    /* The oracle: the same document, freshly built and laid out from nothing. */
    count = layout_int_capture(&fixture, incremental);
    layout_int_begin(&fresh, LAYOUT_INT_DET_HTML, LAYOUT_INT_DET_CSS);
    layout_int_run(&fresh, 700, 500, &metrics, NULL);
    layout_int_capture(&fresh, from_scratch);
    layout_int_compare_states(incremental, from_scratch, count);
    layout_int_compare_lines(&fixture, &fresh);
    layout_int_end(&fresh);

    /*
     * A paint-only change, and the three halves of the no-op contract: the door
     * *skips* the walk, the outcome says so, and the `STYLE` bit is still there
     * afterwards -- because a repaint is owed and a pass that cleared it would look
     * correct until the repaint never happened.
     */
    {
        robo_layout_stats_t skipped;

        /*
         * The *same* context, and that is not an implementation detail: the door's
         * four-term condition includes "this context has completed a pass on this
         * tree", so a fresh context would have to walk whatever the marking said --
         * which is the correct answer to a different question, and would make this
         * assertion vacuous.
         */
        ROBO_CHECK_EQ_INT(robo_layout_tree_mark_style_property(fixture.tree, card,
                                                              "background-color", NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 0);
        ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 0);
        ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                          (int)ROBO_LAYOUT_PASS_CLEAN);
        robo_layout_context_stats(context, &skipped);
        ROBO_CHECK_EQ_INT(skipped.invalidation.recomputed, 0);
        ROBO_CHECK_EQ_INT(skipped.invalidation.style_only, 1u);
        ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_state(fixture.tree, card),
                          ROBO_LAYOUT_INVALIDATION_STYLE);
    }

    robo_layout_context_destroy(context);
    layout_int_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 8. Performance sanity                                               */
/* ------------------------------------------------------------------ */

/** Enough boxes that an accidental quadratic is visible, few enough to be a test. */
#define LAYOUT_INT_PERF_BLOCKS 256u

/**
 * A tree of 256 blocks laid out repeatedly and invalidated repeatedly, with the
 * allocator asserted to return to its baseline.
 *
 * Deliberately *not* a benchmark: there is no timing assertion anywhere in this
 * suite, because a timing assertion on a shared machine fails for reasons that have
 * nothing to do with the module, and a phase that is forbidden from optimizing is
 * also forbidden from asserting that it is fast. What this catches is the class of
 * mistake that turns a linear walk into a quadratic one or leaks a node per pass --
 * both of which show up as a count or a live-block number rather than as a duration.
 *
 * The document is assembled through the **DOM seam** rather than parsed, which is the
 * one way a document can grow after its tree was built (RoboLayoutTestSupport.h), and
 * which keeps a 256-line markup string out of the file.
 */
static void test_integration_performance_sanity(void)
{
    layout_int_fixture_t fixture;
    robo_html_node_handle_t document;
    robo_html_node_handle_t html_element;
    robo_html_node_handle_t body_element;
    robo_layout_stats_t stats;
    size_t before;
    size_t index;
    size_t expected_objects = 2u + LAYOUT_INT_PERF_BLOCKS;
    int cycle;

    layout_fixture();
    before = layout_live_blocks();

    memset(&fixture, 0, sizeof(fixture));
    layout_document_create_empty(&fixture.doc);
    layout_css_begin(&fixture.css);
    layout_css_sheet(&fixture.css, "html, body, div { display: block }\n"
                                    "div { height: 10px; margin: 2px }\n");
    ROBO_CHECK_EQ_INT(robo_layout_dom_create(fixture.css.context, fixture.doc.id,
                                             &fixture.dom, NULL),
                      ROBO_OK);

    document = robo_html_document_root(fixture.doc.id);
    ROBO_CHECK_EQ_INT((int)robo_html_node_handle_is_wellformed(&document), 1);
    html_element = layout_document_add_element(&fixture.doc, &document, "html");
    body_element = layout_document_add_element(&fixture.doc, &html_element, "body");
    for (index = 0u; index < LAYOUT_INT_PERF_BLOCKS; index++) {
        layout_document_add_element(&fixture.doc, &body_element, "div");
    }

    ROBO_CHECK_EQ_INT(robo_layout_dom_refresh(fixture.dom, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_dom_style_source(fixture.dom, &fixture.source, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture.tree, NULL), ROBO_OK);
    {
        robo_layout_build_options_t options;

        robo_layout_build_options_init(&options);
        options.styles = &fixture.source;
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture.tree,
                                                              fixture.doc.id, &options,
                                                              NULL, NULL),
                          ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), expected_objects);

    /* Three full passes: the geometry must be identical each time and the tree's
     * object count must not grow. */
    for (cycle = 0; cycle < 3; cycle++) {
        layout_int_run(&fixture, 800, 6000, NULL, &stats);
        ROBO_CHECK_EQ_INT(stats.objects, expected_objects);
        ROBO_CHECK_EQ_INT(stats.boxes_computed, expected_objects);
        ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), expected_objects);
    }

    /* And repeated invalidate/layout, which is the cycle a live document is in. */
    {
        robo_layout_context_t *context = layout_int_context(800, 6000, NULL);
        robo_layout_node_id_t first = ROBO_LAYOUT_NODE_ID_NONE;

        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, 2u, &first), ROBO_OK);
        for (cycle = 0; cycle < 8; cycle++) {
            ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(
                                  fixture.tree, first, ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY,
                                  NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_layout_context_run_incremental(context, fixture.tree,
                                                                 NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT((int)robo_layout_context_outcome(context),
                              (int)ROBO_LAYOUT_PASS_WALKED);
            ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 0);
            ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree),
                              expected_objects);
        }
        robo_layout_context_destroy(context);
    }

    layout_int_end(&fixture);
    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
}

/* ------------------------------------------------------------------ */
/* 9. Regressions: the findings, pinned                                */
/* ------------------------------------------------------------------ */

/**
 * 10.11's finding: a border's *used* width is not its declared one.
 *
 * `border: 5px none` is the same box as no border at all -- `none` generates nothing,
 * so the width is used as zero and costs no geometry -- while `5px solid` grows the
 * border box by five on each side. The pair is pinned together because the defect this
 * catches is a reader that took the declared width and never asked the style, which
 * would answer 5 in the first case and pass a one-box test written only against
 * `solid`.
 */
static void test_integration_regression_border_style(void)
{
    layout_int_fixture_t fixture;
    robo_layout_border_edges_t edges;

    layout_fixture();
    layout_int_begin(&fixture, "<html><body>"
                               "<div class=\"none\"></div>"
                               "<div class=\"solid\"></div>"
                               "<div class=\"bare\"></div>"
                               "</body></html>",
                     "html, body, div { display: block }\n"
                     ".none { width: 100px; height: 10px; border: 5px none black }\n"
                     ".solid { width: 100px; height: 10px; border: 5px solid black }\n"
                     ".bare { width: 100px; height: 10px }\n");
    layout_int_run(&fixture, 800, 600, NULL, NULL);

    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 6u);

    /* Walk the body's three children in order: none, solid, bare. */
    {
        robo_layout_node_id_t body_id = layout_int_box(&fixture, "body");
        robo_layout_node_id_t child = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_size_t size;

        ROBO_CHECK_EQ_INT(robo_layout_object_first_child(fixture.tree, body_id, &child),
                          ROBO_OK);

        /* `.none`: geometry of a bare box. */
        size = layout_int_size(&fixture, child);
        ROBO_CHECK_EQ_INT(size.width, 100);
        ROBO_CHECK_EQ_INT(size.height, 10);
        edges = layout_int_borders(&fixture, child);
        ROBO_CHECK_EQ_INT(edges.top.width, 0);
        ROBO_CHECK_EQ_INT(edges.left.width, 0);
        ROBO_CHECK_EQ_INT(edges.borders_drawn, 0u);

        /* `.solid`: five on each side, all four drawn. */
        ROBO_CHECK_EQ_INT(robo_layout_object_next_sibling(fixture.tree, child, &child),
                          ROBO_OK);
        size = layout_int_size(&fixture, child);
        ROBO_CHECK_EQ_INT(size.width, 110);
        ROBO_CHECK_EQ_INT(size.height, 20);
        edges = layout_int_borders(&fixture, child);
        ROBO_CHECK_EQ_INT(edges.top.width, 5);
        ROBO_CHECK_EQ_INT(edges.borders_drawn, 4u);

        /* `.bare`: no declaration, and the same box as `.none` -- which is the
         * equality the case is really about. */
        ROBO_CHECK_EQ_INT(robo_layout_object_next_sibling(fixture.tree, child, &child),
                          ROBO_OK);
        size = layout_int_size(&fixture, child);
        ROBO_CHECK_EQ_INT(size.width, 100);
        ROBO_CHECK_EQ_INT(size.height, 10);
        edges = layout_int_borders(&fixture, child);
        ROBO_CHECK_EQ_INT(edges.borders_drawn, 0u);

        ROBO_CHECK_EQ_INT(robo_layout_object_next_sibling(fixture.tree, child, &child),
                          ROBO_NOT_FOUND);
    }

    layout_int_end(&fixture);
}

/**
 * 10.13's finding: a parent folds in a child's **region**, not its contents.
 *
 * A clipping box contributes only what it kept, so a grandparent's overflow is
 * measured against the clip's padding box and not against the 200px child behind it.
 * The check a naive implementation fails is the *grandparent's* record: folding the
 * contents in would report 150px of overflow on a document where the visible result
 * fits, and the error is invisible in every document with no clipping ancestor.
 *
 * The other half is stated here too: the clipping box's own extents are the *raw*
 * measurement of what its contents did, because those extents are how a scrollable
 * area will know it has something to scroll to.
 */
static void test_integration_regression_clipped_child(void)
{
    layout_int_fixture_t fixture;
    robo_layout_node_id_t body_box;
    robo_layout_node_id_t clip;
    robo_layout_overflow_edges_t edges;
    robo_layout_rect_t padding_box;
    robo_layout_rect_t region;

    layout_fixture();
    layout_int_begin(&fixture, "<html><body><div class=\"clip\">"
                               "<section class=\"wide\"></section>"
                               "</div></body></html>",
                     "html, body { display: block }\n"
                     ".clip { display: block; width: 50px; height: 50px; overflow: hidden }\n"
                     ".wide { display: block; width: 200px; height: 200px }\n");
    layout_int_run(&fixture, 800, 600, NULL, NULL);

    body_box = layout_int_box(&fixture, "body");
    clip = layout_int_box(&fixture, "div");

    /* The clip's own extents: the contents really did reach 150 past it. */
    edges = layout_int_overflow(&fixture, clip);
    ROBO_CHECK_EQ_INT(edges.right, 150);
    ROBO_CHECK_EQ_INT(edges.bottom, 150);
    ROBO_CHECK_EQ_INT((int)edges.horizontal, (int)ROBO_CSS_OVERFLOW_HIDDEN);

    ROBO_CHECK_EQ_INT(robo_layout_object_overflow(fixture.tree, body_box, &edges, NULL),
                      ROBO_OK);
    /* The grandparent folds in the clip's *region*, which is the clip's padding box:
     * nothing stuck out of `body`, so its extents are zero. */
    ROBO_CHECK_EQ_INT(edges.right, 0);
    ROBO_CHECK_EQ_INT(edges.bottom, 0);
    ROBO_CHECK_EQ_INT(edges.left, 0);
    ROBO_CHECK_EQ_INT(edges.top, 0);

    /* And the region is the padding box, which is the rectangle the fold used. */
    {
        robo_layout_result_t result;
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, clip, &result), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_result_rect(&result, &padding_box, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_overflow(fixture.tree, clip, &edges, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_overflow_edges_region(&edges, padding_box, &region,
                                                           NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(region.width, 50);
        ROBO_CHECK_EQ_INT(region.height, 50);
    }

    layout_int_end(&fixture);
}

/**
 * 10.14's finding: the dirty root is the **lowest common ancestor** of the boxes whose
 * own geometry is stale, not the shallowest stale box.
 *
 * The document is the one that failed the first implementation: marking a box in the
 * middle also marks the flow *after* its ancestors, and the flow after an ancestor is
 * a sibling that sits **higher** in the tree than the box that moved -- so a
 * "shallowest" rule names a box whose subtree does not contain the change. The
 * assertion is on the *subtree containment* rather than on an id, because that is the
 * property the answer must have.
 */
static void test_integration_regression_dirty_root(void)
{
    layout_int_fixture_t fixture;
    robo_layout_node_id_t section;
    robo_layout_node_id_t root;
    robo_layout_node_id_t ancestor;
    int contains = 0;

    layout_fixture();
    layout_int_begin(&fixture, "<html><body><main><article>"
                               "<section></section>"
                               "</article><footer></footer></main></body></html>",
                     "html, body, main, article, section, footer { display: block }\n"
                     "section { height: 20px }\n");
    layout_int_run(&fixture, 800, 600, NULL, NULL);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);

    section = layout_int_box(&fixture, "section");
    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_layout_dirty(
                          fixture.tree, section, ROBO_LAYOUT_INVALIDATION_REASON_GEOMETRY,
                          NULL),
                      ROBO_OK);

    root = robo_layout_tree_invalidation_root(fixture.tree);
    ROBO_CHECK(root != ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_MSG(root != robo_layout_tree_root(fixture.tree),
                   "the dirty root must be a region, not the document");

    /* The answer must be an ancestor-or-self of the marked box: walk up from the
     * marked box and see whether the answer is on the path. */
    ancestor = section;
    while (ancestor != ROBO_LAYOUT_NODE_ID_NONE) {
        if (ancestor == root) {
            contains = 1;
        }
        if (robo_layout_object_parent(fixture.tree, ancestor, &ancestor) != ROBO_OK) {
            break;
        }
    }
    ROBO_CHECK_MSG(contains, "the dirty root must contain the box that changed");

    /* And the following flow really was marked, which is what made the naive rule
     * wrong: `footer` sits higher than `section` and is stale too. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_invalidation_count(fixture.tree) > 1u, 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_needs_layout(fixture.tree), 1);

    ROBO_CHECK_EQ_INT(robo_layout_tree_mark_clean(fixture.tree), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_clean(fixture.tree), 1);

    layout_int_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 10. The Phase 10 showcase                                           */
/* ------------------------------------------------------------------ */

#define LAYOUT_INT_SHOWCASE_HTML                                              \
    "<html><body><main>"                                                      \
    "<header><h1>Showcase</h1></header>"                                      \
    "<p class=\"intro\">A paragraph of text.</p>"                             \
    "<section class=\"card\"><div class=\"badge\">New</div></section>"        \
    "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"        \
    "<footer><span>end</span></footer>"                                       \
    "</main></body></html>"

/* `aside`/`figure` rather than a third and fourth `div`: `layout_document_find`
 * answers by tag name, and the document's first `div` is the badge inside the card --
 * so a `div`-named clip would have been asserted against the badge and the case would
 * have "passed" while checking nothing. A distinct tag per asserted box is what keeps
 * a by-name lookup honest. */
#define LAYOUT_INT_SHOWCASE_CSS                                                    \
    "html, body, main, header, h1, p, section, footer, div,\n"                      \
    "aside, figure { display: block }\n"                                            \
    "main { width: 500px; margin: 20px auto; padding: 12px;\n"                      \
    "       border: 1px solid black }\n"                                            \
    "header { margin-bottom: 10px; padding: 4px; border-bottom: 2px solid black }\n" \
    "h1 { height: 24px }\n"                                                          \
    ".intro { margin: 6px 0; padding: 4px }\n"                                       \
    ".card { width: 200px; height: 60px; margin: 8px; padding: 6px;\n"              \
    "        border: 1px solid black; position: relative; left: 12px; top: 4px }\n" \
    ".badge { width: 40px; height: 16px; margin: 2px }\n"                            \
    ".clip { width: 80px; height: 24px; overflow: hidden }\n"                        \
    ".spill { width: 160px; height: 60px }\n"                                        \
    "footer { margin-top: 10px }\n"

/**
 * The Phase 10 showcase, at the only level this phase can show it: geometry.
 *
 * Several blocks, inline text, margins, padding, a border, a relatively positioned
 * card, and a box whose contents are clipped -- the document the brief asks for, with
 * the facts a reader would check by looking at a rendered page asserted instead:
 * the composition is one border box wide, the card is *drawn* off the flow it still
 * occupies, the clip is smaller than what it holds, and the whole document's height is
 * the flow's own extent.
 *
 * A rendered picture is Phase 11's. The host visual checkpoint this project has is run
 * against the **demo** path, which does not use RoboLayout at all (its page model is
 * its own), so there is no pixel harness here to extend and this phase does not build
 * one -- the geometry below is the artefact, and `layout/docs/Tests.md` §20 records
 * why.
 */
static void test_integration_showcase(void)
{
    layout_int_fixture_t fixture;
    layout_int_script_t script;
    robo_layout_text_metrics_t metrics;
    robo_layout_node_id_t main_box;
    robo_layout_node_id_t card;
    robo_layout_node_id_t clip;
    robo_layout_position_offset_t offset;
    robo_layout_overflow_edges_t edges;
    robo_layout_rect_t card_rect;
    robo_layout_point_t origin;
    robo_layout_stats_t stats;

    script.per_code_point = 6;
    script.height = 10;
    script.calls = 0u;
    metrics = layout_int_metrics_of(&script);

    layout_fixture();
    layout_int_begin(&fixture, LAYOUT_INT_SHOWCASE_HTML, LAYOUT_INT_SHOWCASE_CSS);

    main_box = layout_int_box(&fixture, "main");
    card = layout_int_box(&fixture, "section");
    clip = layout_int_box(&fixture, "aside");

    layout_int_run(&fixture, 900, 700, &metrics, &stats);

    /* The composition: main is centered by `margin: 20px auto`, so a 500px content
     * box inside a 900px viewport leaves 200 each side -- and the used margins are
     * that 200, which is what an `auto` margin *is*. */
    origin = layout_int_content_origin(&fixture, main_box);
    {
        robo_layout_margin_edges_t margins = layout_int_margins(&fixture, main_box);
        robo_layout_rect_t rect;

        layout_int_rect(&fixture, main_box, &rect);
        ROBO_CHECK_EQ_INT(margins.top, 20);
        ROBO_CHECK_EQ_INT(margins.autos_used, 2u);
        ROBO_CHECK_EQ_INT(margins.left, margins.right);
        /*
         * The documented rule, pinned as it stands: an `auto` margin takes its share
         * of the space beside the **declared width**, so the two used margins are
         * (900 - 500) / 2 = 200 -- and the box's own border and padding are *not*
         * terms of that distribution, which is why the border box plus the two
         * margins overhangs the containing block by exactly the insets (26).
         *
         * That is a boundary and not a bug this phase may fix: changing it means
         * changing 10.9's resolution *order* (the margins are resolved before the
         * border and the padding exist), which is a redesign of a completed layer,
         * and 10.15 is a testing goal. It is recorded as the phase's second finding
         * in `layout/docs/Tests.md` §20 so the next goal that touches the box model
         * meets it deliberately; the assertions here are written so that the *day* it
         * changes, this case fails and the finding is re-read.
         */
        ROBO_CHECK_EQ_INT(margins.left, 200);
        ROBO_CHECK_EQ_INT(margins.left + 500 + margins.right, 900);
        ROBO_CHECK_EQ_INT(margins.left + rect.width + margins.right - 900, 1 + 12 + 1 + 12);
        ROBO_CHECK_EQ_INT(origin.x, margins.left + 1 + 12);
        ROBO_CHECK_EQ_INT(origin.y, 20 + 1 + 12);
        ROBO_CHECK_EQ_INT(layout_int_content_width(&fixture, main_box), 500);
    }

    /* The card is drawn where its offset puts it, and the offset is *readable*, so
     * the flow place is recoverable -- the two coordinates this phase introduced. */
    layout_int_rect(&fixture, card, &card_rect);
    offset = layout_int_offset(&fixture, card);
    ROBO_CHECK_EQ_INT(offset.dx, 12);
    ROBO_CHECK_EQ_INT(offset.dy, 4);
    ROBO_CHECK_EQ_INT(card_rect.width, 200 + 12 + 2);
    ROBO_CHECK_EQ_INT(card_rect.height, 60 + 12 + 2);

    /* The clip is smaller than what it holds, and the record says so even though the
     * region does not. */
    edges = layout_int_overflow(&fixture, clip);
    ROBO_CHECK_EQ_INT((int)edges.horizontal, (int)ROBO_CSS_OVERFLOW_HIDDEN);
    ROBO_CHECK_EQ_INT(edges.right, 80);  /* 160 wide spill, 80 wide clip */
    ROBO_CHECK_EQ_INT(edges.bottom, 36); /* 60 tall spill, 24 tall clip */

    /* Every placed box has geometry, and every non-`head` box is placed: a showcase
     * with a silently unresolved box would render a hole. */
    ROBO_CHECK_EQ_INT(stats.boxes_unresolved, 1u); /* `<head>` only */
    ROBO_CHECK(stats.boxes_computed > 8u);
    ROBO_CHECK(robo_layout_tree_line_count(fixture.tree) >= 2u);

    layout_int_end(&fixture);
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout_integration(void);

void robo_test_register_layout_integration(void)
{
    robo_test_register("layout.integration.box_model", test_integration_box_model);
    robo_test_register("layout.integration.inline_text", test_integration_inline_text);
    robo_test_register("layout.integration.position_overflow",
                       test_integration_position_overflow);
    robo_test_register("layout.integration.nested_tree", test_integration_nested_tree);
    robo_test_register("layout.integration.determinism", test_integration_determinism);
    robo_test_register("layout.integration.extreme_input", test_integration_extreme_input);
    robo_test_register("layout.integration.no_stale_geometry",
                       test_integration_no_stale_geometry);
    robo_test_register("layout.integration.performance_sanity",
                       test_integration_performance_sanity);
    robo_test_register("layout.integration.regression_border_style",
                       test_integration_regression_border_style);
    robo_test_register("layout.integration.regression_clipped_child",
                       test_integration_regression_clipped_child);
    robo_test_register("layout.integration.regression_dirty_root",
                       test_integration_regression_dirty_root);
    robo_test_register("layout.integration.showcase", test_integration_showcase);
}
