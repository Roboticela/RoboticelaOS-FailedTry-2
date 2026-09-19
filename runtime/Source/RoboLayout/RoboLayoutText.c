/*
 * RoboLayout text layout (Phase 10.5) — see RoboLayoutText.h.
 *
 * Three jobs, in the order the header argues them: the range arithmetic (code
 * points, not bytes), the style resolution (four properties, each with the CSS
 * initial value when the style carries no answer), and the seam driver (a call
 * that produces a run whatever the source says, including when there is no
 * source).
 *
 * Nothing here measures anything. The arithmetic of a glyph advance is RoboFont's
 * and lives in RoboLayoutTextFont.c; this file is the boundary, and keeping the
 * two apart is what lets the whole module be tested with a source that answers in
 * round numbers.
 */

#include "RoboLayout/RoboLayoutText.h"

#include "RoboCSS/RoboCSSComputedValue.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboUnicode/RoboUnicode.h"

#include "RoboLayoutInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Ranges                                                              */
/* ------------------------------------------------------------------ */

robo_layout_text_range_t robo_layout_text_range_at(uint32_t at)
{
    robo_layout_text_range_t range;

    range.start = at;
    range.end = at;
    return range;
}

robo_layout_text_range_t robo_layout_text_range_full(uint32_t code_points)
{
    robo_layout_text_range_t range;

    range.start = 0u;
    range.end = code_points;
    return range;
}

uint32_t robo_layout_text_range_length(robo_layout_text_range_t range)
{
    if (range.end <= range.start) {
        return 0u;
    }
    return range.end - range.start;
}

int robo_layout_text_range_is_empty(robo_layout_text_range_t range)
{
    return range.start >= range.end;
}

int robo_layout_text_range_is_inverted(robo_layout_text_range_t range)
{
    return range.start > range.end;
}

int robo_layout_text_range_is_valid(robo_layout_text_range_t range, uint32_t code_points)
{
    /*
     * An inverted range is refused before anything is compared against the text:
     * `start > end` is not a span of anything, however long the text is, and a
     * caller that built one by subtracting has a bug this check is the first place
     * that can see.
     */
    if (range.start > range.end) {
        return 0;
    }
    return range.end <= code_points;
}

int robo_layout_text_range_contains(robo_layout_text_range_t range, uint32_t index)
{
    return index >= range.start && index < range.end;
}

/* ------------------------------------------------------------------ */
/* Direction                                                           */
/* ------------------------------------------------------------------ */

int robo_layout_text_direction_is_known(robo_layout_text_direction_t direction)
{
    return direction == ROBO_LAYOUT_TEXT_DIRECTION_LTR ||
           direction == ROBO_LAYOUT_TEXT_DIRECTION_RTL;
}

const char *robo_layout_text_direction_name(robo_layout_text_direction_t direction)
{
    switch (direction) {
    case ROBO_LAYOUT_TEXT_DIRECTION_LTR:
        return "ltr";
    case ROBO_LAYOUT_TEXT_DIRECTION_RTL:
        return "rtl";
    default:
        break;
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* Style                                                               */
/* ------------------------------------------------------------------ */

void robo_layout_text_style_init(robo_layout_text_style_t *style)
{
    if (!style) {
        return;
    }
    style->font_size_px = ROBO_LAYOUT_TEXT_INITIAL_FONT_SIZE_PX;
    style->font_weight = ROBO_LAYOUT_TEXT_WEIGHT_NORMAL;
    style->italic = 0;
    style->direction = ROBO_LAYOUT_TEXT_DIRECTION_LTR;
}

/** One computed value's keyword text, or an absent slice when there is none. */
static robo_css_slice_t text_style_keyword(const robo_css_computed_style_t *computed,
                                           const char *property)
{
    const robo_css_computed_value_t *value;

    if (!computed || !property) {
        return robo_css_slice_empty();
    }
    value = robo_css_computed_style_computed_by_name(computed,
                                                     robo_css_slice_from_text(property));
    if (!value || value->kind != ROBO_CSS_COMPUTED_VALUE_KEYWORD) {
        return robo_css_slice_empty();
    }
    return robo_css_computed_value_text(value);
}

/**
 * The font weight @p computed declares, or the initial weight.
 *
 * A NUMBER is the author's own number, clamped to CSS's own valid range so that a
 * document saying `font-weight: 5000` measures with 1000 rather than with a
 * number no font has a face for. The four keywords are read by name, and `bolder`
 * and `lighter` are answered *relative to the initial weight* -- which is what CSS
 * does when there is no inherited weight to be relative to, and this record has no
 * parent by construction.
 */
static int32_t text_style_weight(const robo_css_computed_style_t *computed)
{
    const robo_css_computed_value_t *value;
    robo_css_slice_t keyword;

    if (!computed) {
        return ROBO_LAYOUT_TEXT_WEIGHT_NORMAL;
    }
    value = robo_css_computed_style_computed_by_name(computed,
                                                     robo_css_slice_from_text("font-weight"));
    if (!value) {
        return ROBO_LAYOUT_TEXT_WEIGHT_NORMAL;
    }
    if (value->kind == ROBO_CSS_COMPUTED_VALUE_NUMBER) {
        double weight = value->number;

        if (!(weight >= 1.0)) {
            return 1;
        }
        if (weight > 1000.0) {
            return 1000;
        }
        return (int32_t)weight;
    }
    if (value->kind != ROBO_CSS_COMPUTED_VALUE_KEYWORD) {
        return ROBO_LAYOUT_TEXT_WEIGHT_NORMAL;
    }
    keyword = robo_css_computed_value_text(value);
    if (robo_css_slice_equals_ascii_ci(keyword, "bold")) {
        return ROBO_LAYOUT_TEXT_WEIGHT_BOLD;
    }
    if (robo_css_slice_equals_ascii_ci(keyword, "bolder")) {
        /* CSS's relative keyword, resolved against the initial weight because
         * there is no parent weight here (see above). */
        return ROBO_LAYOUT_TEXT_WEIGHT_BOLD;
    }
    if (robo_css_slice_equals_ascii_ci(keyword, "lighter")) {
        return 100;
    }
    return ROBO_LAYOUT_TEXT_WEIGHT_NORMAL;
}

robo_result_t robo_layout_text_style_resolve(const robo_css_computed_style_t *computed,
                                             robo_layout_text_style_t *out_style)
{
    double size = 0.0;
    robo_css_slice_t keyword;

    if (!out_style) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_layout_text_style_init(out_style);

    /*
     * `font-size` through 10.3's own reader, so that "a declared value this build
     * can resolve is a computed LENGTH in px, finite, non-negative and inside the
     * geometry's range" stays one rule with one implementation. A percentage, a
     * relative unit and a named size all fail that test and take the initial value
     * below, which is the property's own initial value and not a guess.
     */
    if (robo_layout_style_px_length(computed, "font-size", &size) && size > 0.0) {
        out_style->font_size_px = size;
    }
    out_style->font_weight = text_style_weight(computed);

    keyword = text_style_keyword(computed, "font-style");
    if (robo_css_slice_equals_ascii_ci(keyword, "italic") ||
        robo_css_slice_equals_ascii_ci(keyword, "oblique")) {
        out_style->italic = 1;
    }

    keyword = text_style_keyword(computed, "direction");
    if (robo_css_slice_equals_ascii_ci(keyword, "rtl")) {
        out_style->direction = ROBO_LAYOUT_TEXT_DIRECTION_RTL;
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Request and run                                                     */
/* ------------------------------------------------------------------ */

void robo_layout_text_run_init(robo_layout_text_run_t *run)
{
    if (!run) {
        return;
    }
    memset(run, 0, sizeof(*run));
    /* memset leaves an ill-formed handle, which is what "no source" should look
     * like: `robo_html_node_handle_is_wellformed` answers 0 for it. */
    run->direction = ROBO_LAYOUT_TEXT_DIRECTION_LTR;
}

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

void robo_layout_text_metrics_init(robo_layout_text_metrics_t *metrics)
{
    if (!metrics) {
        return;
    }
    memset(metrics, 0, sizeof(*metrics));
}

int robo_layout_text_metrics_is_usable(const robo_layout_text_metrics_t *metrics)
{
    return metrics && metrics->measure != NULL;
}

robo_result_t robo_layout_text_measure(const robo_layout_text_metrics_t *metrics,
                                       const robo_layout_text_request_t *request,
                                       robo_layout_text_run_t *out_run,
                                       robo_layout_diag_t *diag)
{
    if (!out_run) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "text measurement needs a run to fill");
    }
    robo_layout_text_run_init(out_run);

    if (!request) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "text measurement needs a request");
    }
    if (!robo_html_node_handle_is_wellformed(&request->text_node)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "text measurement needs a well-formed text node");
    }
    if (!request->style) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "text measurement needs resolved font inputs");
    }

    /*
     * The source is echoed onto the run before anything measures, so that every
     * answer below -- including the zero one -- names what it is about. A run that
     * did not say which text it measured would be unusable the moment a caller
     * held two of them.
     */
    out_run->source = request->text_node;
    out_run->range = request->range;
    out_run->direction = request->style->direction;

    /*
     * No source is the documented degradation rather than an error: the run stays
     * zero, the range above is echoed, and the pass counts it as *unmeasured*. That
     * is what keeps every 10.4 expectation about a textless container true in this
     * build, and it is why the counters distinguish "nothing measured this" from
     * "this measured to nothing".
     */
    if (!robo_layout_text_metrics_is_usable(metrics)) {
        return ROBO_OK;
    }
    return metrics->measure(metrics->userdata, request, out_run);
}

robo_result_t robo_layout_text_measure_node(const robo_html_node_handle_t *text_node,
                                           const robo_css_computed_style_t *owner_style,
                                           const robo_layout_text_metrics_t *metrics,
                                           robo_layout_text_run_t *out_run,
                                           robo_layout_diag_t *diag)
{
    robo_layout_text_style_t style;
    robo_layout_text_request_t request;
    uint32_t code_points = 0u;
    robo_result_t status;

    if (!out_run) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "measuring a text node needs a run to fill");
    }
    robo_layout_text_run_init(out_run);
    if (!text_node || !robo_html_node_handle_is_wellformed(text_node)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "measuring a text node needs a well-formed handle");
    }

    /*
     * The range first, from the node's own *code points* -- see the header. This
     * is also the call that proves the node is a text node at all, so the two
     * facts arrive together rather than being checked twice.
     */
    status = robo_layout_text_node_code_points(text_node, &code_points, diag);
    if (status != ROBO_OK) {
        return status;
    }

    status = robo_layout_text_style_resolve(owner_style, &style);
    if (status != ROBO_OK) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, status,
                                "the font inputs could not be resolved");
    }

    memset(&request, 0, sizeof(request));
    request.text_node = *text_node;
    request.range = robo_layout_text_range_full(code_points);
    request.style = &style;
    request.computed = owner_style;
    return robo_layout_text_measure(metrics, &request, out_run, diag);
}

/* ------------------------------------------------------------------ */
/* What the text direction saw                                         */
/* ------------------------------------------------------------------ */

void robo_layout_text_stats_init(robo_layout_text_stats_t *stats)
{
    if (!stats) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
}

int robo_layout_text_stats_equal(const robo_layout_text_stats_t *a,
                                 const robo_layout_text_stats_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->runs_placed == b->runs_placed && a->measured == b->measured &&
           a->unmeasured == b->unmeasured && a->empty == b->empty &&
           a->code_points == b->code_points &&
           a->glyphs_missing == b->glyphs_missing && a->rtl == b->rtl &&
           a->width_max == b->width_max && a->height_max == b->height_max &&
           a->depth_max == b->depth_max;
}

/* ------------------------------------------------------------------ */
/* Reading a text node                                                 */
/* ------------------------------------------------------------------ */

/** Borrowed text and byte length of @p text_node's payload. */
static robo_result_t text_node_payload(const robo_html_node_handle_t *text_node,
                                       const char **out_text,
                                       size_t *out_length,
                                       robo_layout_diag_t *diag)
{
    robo_html_node_view_t view;

    if (!text_node || !out_text || !out_length) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "reading a text node needs a handle and two outs");
    }
    if (!robo_html_node_handle_is_wellformed(text_node)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "reading a text node needs a well-formed handle");
    }
    if (robo_html_node_get_view(text_node, &view, NULL) != ROBO_OK) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_NOT_FOUND,
                                "a text node handle did not resolve");
    }
    if (view.kind != ROBO_HTML_NODE_KIND_TEXT) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "a text run can only be measured inside a text node");
    }
    *out_text = view.text ? view.text : "";
    *out_length = view.text ? view.text_length : 0u;
    return ROBO_OK;
}

robo_result_t robo_layout_text_node_code_points(const robo_html_node_handle_t *text_node,
                                               uint32_t *out_count,
                                               robo_layout_diag_t *diag)
{
    const char *text = NULL;
    size_t length = 0u;
    size_t count = 0u;
    int ok = 0;
    robo_result_t status;

    if (!out_count) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "counting code points needs an out");
    }
    *out_count = 0u;
    status = text_node_payload(text_node, &text, &length, diag);
    if (status != ROBO_OK) {
        return status;
    }

    /*
     * REPLACE rather than STRICT: the tokenizer already decided what these bytes
     * mean, so a byte the DOM kept is a byte this count has to account for. STRICT
     * would answer "unmeasurable document" over one truncated sequence, which is a
     * refusal the layer above has no way to act on.
     */
    count = robo_unicode_utf8_count(text, length, ROBO_UNICODE_REPLACE, &ok);
    if (count > (size_t)0xFFFFFFFFu) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "a text node holds more code points than a range can index");
    }
    *out_count = (uint32_t)count;
    return ROBO_OK;
}

robo_result_t robo_layout_text_range_bytes(const robo_html_node_handle_t *text_node,
                                          robo_layout_text_range_t range,
                                          size_t *out_start,
                                          size_t *out_length,
                                          robo_layout_diag_t *diag)
{
    const char *text = NULL;
    size_t length = 0u;
    size_t offset = 0u;
    size_t index = 0u;
    size_t start = 0u;
    robo_result_t status;

    if (!out_start || !out_length) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "a byte span needs two outs");
    }
    *out_start = 0u;
    *out_length = 0u;
    if (robo_layout_text_range_is_inverted(range)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "a text range cannot end before it starts");
    }
    status = text_node_payload(text_node, &text, &length, diag);
    if (status != ROBO_OK) {
        return status;
    }

    /*
     * Walk code point by code point rather than counting then seeking, and for a
     * reason the unit test for this function is written around: the walk is what
     * *proves* both endpoints landed on boundaries. A count-then-index
     * implementation would answer for a range that ended mid-sequence by landing
     * on the replacement character's start, which is a silently moved range.
     */
    while (index < range.start) {
        size_t next = offset;
        robo_uchar32_t code_point = 0;
        robo_unicode_status_t decoded;

        if (offset >= length) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                    "a text range starts past the end of the text");
        }
        /*
         * REPLACE mode answers OK for a malformed lead byte, consuming one byte
         * as U+FFFD (RoboUnicode.c's own rule), so a document the DOM kept is a
         * document this walk can always advance through. The `next <= offset`
         * guard is therefore not reachable through that path -- it is here
         * because a decode that stopped advancing would turn this loop into a
         * hang, and a hang is the one failure a range check cannot report.
         */
        decoded = robo_unicode_utf8_decode(text, length, offset, ROBO_UNICODE_REPLACE,
                                          &code_point, &next);
        if (decoded != ROBO_UNICODE_OK) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_ERROR,
                                    "the text node's own bytes could not be decoded");
        }
        if (next <= offset) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_ERROR,
                                    "decoding a text node's bytes stopped advancing");
        }
        offset = next;
        index++;
    }
    start = offset;

    while (index < range.end) {
        size_t next = offset;
        robo_uchar32_t code_point = 0;
        robo_unicode_status_t decoded;

        if (offset >= length) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                    "a text range ends past the end of the text");
        }
        decoded = robo_unicode_utf8_decode(text, length, offset, ROBO_UNICODE_REPLACE,
                                          &code_point, &next);
        if (decoded != ROBO_UNICODE_OK) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_ERROR,
                                    "the text node's own bytes could not be decoded");
        }
        if (next <= offset) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_ERROR,
                                    "decoding a text node's bytes stopped advancing");
        }
        offset = next;
        index++;
    }

    *out_start = start;
    *out_length = offset - start;
    return ROBO_OK;
}

int robo_layout_text_is_collapsible_whitespace(const char *text, size_t length)
{
    size_t index;

    if (!text || length == 0u) {
        return 1;
    }
    for (index = 0u; index < length; index++) {
        switch (text[index]) {
        case ' ':
        case '\t':
        case '\n':
        case '\f':
        case '\r':
            break; /* CSS's collapsible whitespace, and the whole of the set. */
        default:
            return 0;
        }
    }
    return 1;
}
