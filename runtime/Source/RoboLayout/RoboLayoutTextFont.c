/*
 * RoboLayout's RoboFont-backed text measurer (Phase 10.5) — see
 * RoboLayoutTextFont.h.
 *
 * One walk over the code points of a range, one accumulation in font units, one
 * conversion. The header argues the accumulation; this file is where it has to
 * survive a face that is missing glyphs, a face with no hmtx, and a text node
 * whose bytes are not what its author meant.
 *
 * What is deliberately not here: shaping, kerning, fallback, bidi, and any cache
 * of a measurement. Two runs of the same text measure the same because the
 * arithmetic is a function of the face and the range -- not because anything
 * remembered.
 */

#include "RoboLayout/RoboLayoutTextFont.h"

#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboUnicode/RoboUnicode.h"

#include "RoboLayout/RoboLayoutText.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

void robo_layout_text_font_init(robo_layout_text_font_t *font)
{
    if (!font) {
        return;
    }
    memset(font, 0, sizeof(*font));
}

robo_result_t robo_layout_text_font_attach(robo_layout_text_font_t *font,
                                          robo_font_face_t *face)
{
    if (!font || !face) {
        return ROBO_INVALID_ARGUMENT;
    }
    font->face = face;
    font->parsed = 0;
    font->runs_measured = 0u;
    font->glyphs_missing = 0u;
    return ROBO_OK;
}

void robo_layout_text_font_detach(robo_layout_text_font_t *font)
{
    if (!font) {
        return;
    }
    font->face = NULL;
    font->parsed = 0;
}

int robo_layout_text_font_is_attached(const robo_layout_text_font_t *font)
{
    return font && font->face != NULL;
}

/* ------------------------------------------------------------------ */
/* The face's tables, once per attach                                  */
/* ------------------------------------------------------------------ */

/**
 * Parse what measuring needs: the cmap and the layout metrics.
 *
 * Both are RoboFont's lazy, idempotent parses, so asking twice would be correct
 * and merely wasteful -- which is why the flag exists as an optimisation and not
 * as a correctness device, and why it is reset by `detach` rather than trusted
 * across faces.
 */
static robo_result_t measure_prepare(robo_layout_text_font_t *font,
                                     robo_layout_diag_t *diag)
{
    if (!robo_layout_text_font_is_attached(font)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_ERROR,
                                "text measurement needs an attached font face");
    }
    if (font->parsed) {
        return ROBO_OK;
    }
    if (robo_font_face_parse_cmap(font->face) != ROBO_OK) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_ERROR,
                                "the font face has no usable character map");
    }
    if (robo_font_face_parse_metrics(font->face) != ROBO_OK) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_ERROR,
                                "the font face has no usable metrics");
    }
    font->parsed = 1;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The walk                                                            */
/* ------------------------------------------------------------------ */

/**
 * Sum one code point's advance into @p out_units, and report whether the face had
 * a glyph for it.
 *
 * Three answers from RoboFont, and each is handled rather than collapsed:
 *
 *   MAPPED     the glyph's own `hmtx` advance. The common case.
 *   UNMAPPED   no glyph: the run is measured at **glyph 0**'s advance, which is
 *              what every engine does with `.notdef`, and the miss is counted so
 *              a reader learns the width is for text the font cannot draw.
 *   INVALID    not a Unicode scalar, or not a face this build read. Counted as a
 *              miss as well, because from the run's point of view it is the same
 *              fact: nothing here corresponds to anything the font can draw.
 *
 * A glyph whose metrics will not parse is a miss for the same reason, and NOT a
 * zero: falling back to zero would make a broken font produce a *narrower* run
 * than a working one, which reads as a plausible measurement and is exactly the
 * failure this phase is built to avoid.
 */
static void measure_code_point(robo_layout_text_font_t *font,
                               uint32_t code_point,
                               int64_t *io_units,
                               robo_layout_text_run_t *run)
{
    robo_font_cmap_lookup_result_t lookup;
    robo_font_glyph_metrics_t metrics;
    robo_glyph_id_t glyph;
    int mapped;

    memset(&lookup, 0, sizeof(lookup));
    mapped = robo_font_face_lookup_glyph(font->face, code_point, &lookup) == ROBO_OK &&
             lookup.status == ROBO_FONT_CMAP_MAPPED;
    if (!mapped) {
        /*
         * No glyph. The *code point* is counted, once, and the measurement
         * continues at glyph 0 -- which is what `.notdef` is and what every engine
         * measures undrawable text at. Counting here rather than skipping is the
         * whole point: the width this run ends up with is a real width for text the
         * font cannot draw, and this counter is the only thing that says so.
         */
        glyph = ROBO_GLYPH_ID_NONE;
        run->glyphs_missing++;
        font->glyphs_missing++;
    } else {
        glyph = lookup.glyph_id;
    }

    /*
     * The glyph's own metrics, and the *status* rather than only the result code:
     * RoboFont answers ROBO_OK with a status field for the three ways a glyph can
     * have no advance (`INVALID_ID` for an id past the face's own glyph count,
     * `UNAVAILABLE` for a face with no `hmtx`, `MALFORMED` for a table that will not
     * parse), and treating those as a measurement of zero would make a broken font
     * produce a *narrower* run than a working one -- a plausible-looking number, and
     * therefore the one failure a later phase could not detect.
     */
    memset(&metrics, 0, sizeof(metrics));
    if (robo_font_face_glyph_metrics(font->face, glyph, &metrics) != ROBO_OK ||
        metrics.status != ROBO_FONT_GLYPH_METRICS_OK) {
        /* Counted only when the cmap had *claimed* the code point: an unmapped one
         * is already counted above, and a count of two for one character would make
         * the number mean "problems" rather than "characters". */
        if (mapped) {
            run->glyphs_missing++;
            font->glyphs_missing++;
        }
        return;
    }
    *io_units += (int64_t)metrics.advance_width;
}

/**
 * The line extent of the face, in pixels.
 *
 * `ascent + descent + line_gap`, which is what RoboFont records as the canonical
 * layout metrics (`robo_font_metrics_line_height`) and what the header calls the
 * run's height. Each term is scaled separately rather than summing in font units
 * first, and the difference is deliberate: a line extent is reported to a caller
 * as three *components* -- above the baseline, below it, and the leading between
 * lines -- and 10.7 needs the first two to place a baseline. Summing first would
 * produce the same total and lose the halves.
 */
static void measure_line_extent(robo_layout_text_font_t *font,
                                const robo_layout_text_style_t *style,
                                robo_layout_text_run_t *run)
{
    robo_font_metrics_t line;
    float ascent = 0.0f;
    float descent = 0.0f;
    float gap = 0.0f;

    memset(&line, 0, sizeof(line));
    if (robo_font_face_layout_metrics(font->face, &line) != ROBO_OK ||
        !robo_font_units_per_em_is_valid(line.units_per_em)) {
        /*
         * No usable line metrics. The honest answer is the *font size* as the
         * extent, which is what CSS's own `normal` line height reduces to for a
         * face nothing is known about -- and not zero, which would silently
         * collapse every line a document has.
         */
        run->ascent = (int32_t)style->font_size_px;
        run->descent = 0;
        run->height = (int32_t)style->font_size_px;
        return;
    }

    if (robo_font_scale_font_units(line.ascent, line.units_per_em,
                                   (robo_font_pixel_size_t)style->font_size_px,
                                   ROBO_FONT_ROUND_NEAREST, &ascent) != ROBO_OK) {
        ascent = (float)style->font_size_px;
    }
    if (robo_font_scale_font_units(line.descent, line.units_per_em,
                                   (robo_font_pixel_size_t)style->font_size_px,
                                   ROBO_FONT_ROUND_NEAREST, &descent) != ROBO_OK) {
        descent = 0.0f;
    }
    if (robo_font_scale_font_units(line.line_gap, line.units_per_em,
                                   (robo_font_pixel_size_t)style->font_size_px,
                                   ROBO_FONT_ROUND_NEAREST, &gap) != ROBO_OK) {
        gap = 0.0f;
    }

    /*
     * A font whose descent is authored positive is reported as a positive
     * magnitude by RoboFont (RoboFontMetrics.h says so), but a face in the wild
     * can carry either sign convention. Normalising to a magnitude here keeps the
     * record's stated meaning true whatever the table said: `descent` is a
     * magnitude, so `height` is always ascent + descent + gap and never a
     * subtraction.
     */
    if (descent < 0.0f) {
        descent = -descent;
    }
    if (gap < 0.0f) {
        gap = 0.0f;
    }
    run->ascent = (int32_t)(ascent + 0.5f);
    run->descent = (int32_t)(descent + 0.5f);
    run->height = run->ascent + run->descent + (int32_t)(gap + 0.5f);
}

/* ------------------------------------------------------------------ */
/* Measuring                                                           */
/* ------------------------------------------------------------------ */

robo_result_t robo_layout_text_font_measure(robo_layout_text_font_t *font,
                                           const robo_layout_text_request_t *request,
                                           robo_layout_text_run_t *out_run,
                                           robo_layout_diag_t *diag)
{
    robo_html_node_view_t view;
    robo_layout_text_range_t range;
    size_t offset = 0u;
    size_t index = 0u;
    size_t range_start_byte = 0u;
    size_t range_byte_length = 0u;
    int64_t units = 0;
    float width = 0.0f;
    robo_result_t status;

    if (!font || !request || !out_run) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "measuring needs a measurer, a request and a run");
    }
    robo_layout_text_run_init(out_run);
    if (!request->style) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "measuring needs resolved font inputs");
    }

    out_run->source = request->text_node;
    out_run->range = request->range;
    out_run->direction = request->style->direction;

    if (robo_html_node_get_view(&request->text_node, &view, NULL) != ROBO_OK) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_NOT_FOUND,
                                "the text node to measure did not resolve");
    }
    if (view.kind != ROBO_HTML_NODE_KIND_TEXT) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                "a text run can only be measured inside a text node");
    }

    /*
     * The range's validity first, through RoboLayoutText.h's own bridge, so that
     * "both endpoints land on code-point boundaries and inside the text" is
     * established once by the code that owns the range type. The byte span itself
     * is discarded -- this measurer walks to decode, because it needs the code
     * points and not the bytes -- but the *check* is worth the call rather than a
     * second copy of the boundary arithmetic below.
     */
    status = robo_layout_text_range_bytes(&request->text_node, request->range,
                                         &range_start_byte, &range_byte_length, diag);
    if (status != ROBO_OK) {
        return status;
    }
    range = request->range;

    status = measure_prepare(font, diag);
    if (status != ROBO_OK) {
        return status;
    }

    /*
     * Advance the byte cursor to the range's first code point, then walk the
     * range's own code points. The first loop exists so the sum starts at the
     * right place: a run is measured from its own beginning, and starting at byte
     * zero would measure the whole node whatever range was asked for.
     */
    while (index < range.start) {
        size_t next = offset;
        robo_uchar32_t code_point = 0;

        if (offset >= view.text_length) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                    "a text range starts past the end of the text");
        }
        if (robo_unicode_utf8_decode(view.text, view.text_length, offset, ROBO_UNICODE_REPLACE,
                                    &code_point, &next) != ROBO_UNICODE_OK ||
            next <= offset) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_ERROR,
                                    "the text node's own bytes could not be decoded");
        }
        offset = next;
        index++;
    }

    while (index < range.end) {
        size_t next = offset;
        robo_uchar32_t code_point = 0;

        if (offset >= view.text_length) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                    "a text range ends past the end of the text");
        }
        if (robo_unicode_utf8_decode(view.text, view.text_length, offset, ROBO_UNICODE_REPLACE,
                                    &code_point, &next) != ROBO_UNICODE_OK ||
            next <= offset) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_ERROR,
                                    "the text node's own bytes could not be decoded");
        }
        measure_code_point(font, (uint32_t)code_point, &units, out_run);
        out_run->code_points++;
        offset = next;
        index++;
    }

    /*
     * One conversion for the whole run, which is the header's central decision.
     * `robo_font_scale_font_units` carries the overflow check and the rounding
     * policy, so an absurd font size is refused rather than wrapped, and the
     * conversion is the same one RoboFont uses everywhere else.
     */
    {
        robo_font_metrics_t line;
        uint16_t upem = 0u;

        memset(&line, 0, sizeof(line));
        if (robo_font_face_layout_metrics(font->face, &line) == ROBO_OK) {
            upem = line.units_per_em;
        }
        if (!robo_font_units_per_em_is_valid(upem)) {
            upem = 1000u; /* The SFNT convention for a font that did not say. */
        }
        if (robo_font_scale_font_units((robo_font_unit_t)units, upem,
                                       (robo_font_pixel_size_t)request->style->font_size_px,
                                       ROBO_FONT_ROUND_NEAREST, &width) != ROBO_OK) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT, ROBO_INVALID_ARGUMENT,
                                    "a run's advance does not fit the pixel space");
        }
    }

    if (width < 0.0f) {
        width = 0.0f;
    }
    out_run->width = (int32_t)(width + 0.5f);
    measure_line_extent(font, request->style, out_run);
    if (out_run->height < 0) {
        out_run->height = 0;
    }

    font->runs_measured++;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* The source                                                          */
/* ------------------------------------------------------------------ */

static robo_result_t text_font_measure_cb(void *userdata,
                                          const robo_layout_text_request_t *request,
                                          robo_layout_text_run_t *out_run)
{
    return robo_layout_text_font_measure((robo_layout_text_font_t *)userdata, request, out_run,
                                        NULL);
}

robo_layout_text_metrics_t robo_layout_text_font_metrics(robo_layout_text_font_t *font)
{
    robo_layout_text_metrics_t metrics;

    robo_layout_text_metrics_init(&metrics);
    /*
     * A **detached** measurer yields an empty record, not a record whose callback
     * fails. That is what makes "no source" and "a source that answered nothing"
     * the same build behaviour with a count attached (RoboLayoutText.h): the seam
     * reads an absent callback as *measures nothing* and the pass counts the run as
     * unmeasured, where a callback that returned an error would abort the pass for a
     * document whose only sin is having no font attached.
     */
    if (font && robo_layout_text_font_is_attached(font)) {
        metrics.userdata = font;
        metrics.measure = text_font_measure_cb;
    }
    return metrics;
}
