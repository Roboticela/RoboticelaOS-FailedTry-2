/*
 * RoboLayout text layout (Phase 10.5).
 *
 * 10.3 placed boxes down the page and 10.4 placed boxes along a line. Both
 * placed *boxes*, and that left one gap this phase fills: a text node is not an
 * element and generated nothing, so a `<span>` holding only text was zero-wide
 * because nothing in the build measured text. That zero was documented loudly in
 * 10.4's own header as the honest answer *at the time*, and this is the phase
 * that replaces it.
 *
 * What a text object is
 *
 * A text node now generates a layout object of kind TEXT (RoboLayoutTypes.h). It
 * is not an element box: it has no computed style of its own, no children, and no
 * `display` declaration that could change its kind. What it has is
 *
 *   a DOM node      the text node's own handle, in the same `dom_node` field an
 *                   element box uses, because a handle is a node handle and a
 *                   text node is a node;
 *   a style         its **parent element's** computed style, borrowed. That is
 *                   not a shortcut: every property a text run needs -- font
 *                   family, size, weight, style, `direction`, `color` -- is an
 *                   *inherited* property in CSS, and RoboCSS already distributes
 *                   inherited values to descendants. A text node's computed
 *                   style therefore *is* its parent's, for exactly the
 *                   properties in question, and pointing at the parent's is the
 *                   one representation that cannot go stale;
 *   a content       the whole text node, from its first code point to its last.
 *                   The *range* type below exists for the callers that will
 *                   split it -- 10.6's line boxes and 10.15's selection mapping
 *                   -- and this phase passes the full range, because a partial
 *                   range is the thing splitting produces and not a thing to
 *                   invent before anything splits.
 *
 * Where the measurements come from: a seam, and why
 *
 * `robo_css_computed_style_t` arrived through a callback in 10.1 for one reason,
 * and the same reason applies here verbatim: **the pass must be testable without
 * the dependency**. A suite whose subject is cursor arithmetic has no business
 * loading a font file, and a phase that made it do so would be a phase whose
 * layout tests are really font tests.
 *
 * So measurement is a second callback seam, shaped like the first:
 *
 *   robo_layout_text_metrics_t     who answers "how wide is this run?"
 *   the real supplier              RoboLayoutTextFont.h, a RoboFont face asked
 *                                  through RoboFont's own public API
 *
 * And the arithmetic is genuinely RoboFont's, not a second copy of it: the
 * measurer sums glyph advances in **font units** and converts the sum once, using
 * `robo_font_scale_font_units`. Summing in font units and scaling once is not a
 * micro-optimisation -- it is the difference between one rounding for a run and
 * one per glyph, and the second quietly accumulates.
 *
 * What an absent source means, and why that is the interesting case
 *
 * A pass with no metrics source measures every run as **zero**, counts it
 * (`robo_layout_text_stats_t.unmeasured`), and lays the document out exactly as
 * 10.4 did. That is deliberate, and it is the property that makes this phase's
 * arrival safe: every 10.4 suite that asserted a textless container was zero-wide
 * still passes, unchanged, because "no source" and "10.4" are the same build
 * behaviour with a count attached. A source that answers nothing is *told* apart
 * from a source that measured zero by that counter, which is the same distinction
 * 10.1 draws with STYLE_MISSING and 10.4 draws with `empty_contexts`.
 *
 * Ranges are code points, not bytes
 *
 * `robo_layout_text_range_t` is a half-open span of **Unicode code points**, and
 * the conversion to a byte span goes through RoboUnicode
 * (`robo_unicode_utf8_decode`), the same decoder the rest of the runtime uses.
 * A byte offset is not a character position the moment a document contains "é",
 * "م" or "👋", and the whole point of having built RoboUnicode in Phase 4 is that
 * a later phase does not have to rediscover that. A range whose endpoints do not
 * land on code-point boundaries is **refused** rather than rounded
 * (`robo_layout_text_range_bytes`), because a range that silently moved is a
 * selection that highlights the wrong bytes.
 *
 * What is deliberately missing
 *
 * Shaping (GlyphSubstitution/GlyphPositioning, ligatures, kerning), the bidi
 * algorithm, Unicode line breaking, whitespace collapsing, line boxes, baselines,
 * `line-height`, font fallback and paint. The measurer is unshaped by design: it
 * sums the advances the font's own `hmtx` gives, in logical order, which is
 * exactly what CSS calls the *advance width* of a run before layout reorders it.
 * Naming that limit is the difference between a foundation and a claim.
 */

#ifndef ROBO_LAYOUT_TEXT_H
#define ROBO_LAYOUT_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboHTML/RoboHTMLNode.h"

#include "RoboLayout/RoboLayoutDiagnostics.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Ranges                                                              */
/* ------------------------------------------------------------------ */

/**
 * A half-open span of Unicode code points: @p start inclusive, @p end exclusive.
 *
 * Offsets count **code points**, not bytes and not UTF-16 units, and the unit is
 * stated in the type's name because a range whose unit is implicit is a range
 * that will be compared against a byte offset one day. An empty range is one
 * where `start == end`, which is legal and useful: it is what a collapsed
 * selection is, and it measures to nothing rather than failing.
 */
typedef struct robo_layout_text_range {
    /** First code point of the range. */
    uint32_t start;
    /** One past the last code point of the range. */
    uint32_t end;
} robo_layout_text_range_t;

/** The empty range at @p at: a position in the text rather than a span of it. */
robo_layout_text_range_t robo_layout_text_range_at(uint32_t at);

/** The whole of a text @p code_points long. */
robo_layout_text_range_t robo_layout_text_range_full(uint32_t code_points);

/** Number of code points the range covers; 0 for an inverted or empty range. */
uint32_t robo_layout_text_range_length(robo_layout_text_range_t range);

/** Non-zero when the range covers nothing (`start >= end`). */
int robo_layout_text_range_is_empty(robo_layout_text_range_t range);

/** Non-zero when `start >= end` -- the one shape that is never a position. */
int robo_layout_text_range_is_inverted(robo_layout_text_range_t range);

/** Non-zero when @p range lies inside a text @p code_points long. */
int robo_layout_text_range_is_valid(robo_layout_text_range_t range, uint32_t code_points);

/** Non-zero when @p range contains code point index @p index. */
int robo_layout_text_range_contains(robo_layout_text_range_t range, uint32_t index);

/* ------------------------------------------------------------------ */
/* Direction                                                           */
/* ------------------------------------------------------------------ */

/**
 * The direction a run is laid out in, taken from the computed `direction`.
 *
 * Two values, and that is the whole of the claim: this is the *declared*
 * direction, not a resolved one. The Unicode bidirectional algorithm -- which
 * finds directional runs *inside* a paragraph, and reorders them -- is a later
 * phase, and a build that resolved it here would be answering that phase's
 * question with this one's input.
 */
typedef enum robo_layout_text_direction {
    ROBO_LAYOUT_TEXT_DIRECTION_LTR = 0,
    ROBO_LAYOUT_TEXT_DIRECTION_RTL = 1
} robo_layout_text_direction_t;

/** Number of defined directions. */
#define ROBO_LAYOUT_TEXT_DIRECTION_COUNT 2u

/** Non-zero if @p direction is a defined enumerator. */
int robo_layout_text_direction_is_known(robo_layout_text_direction_t direction);

/** Human-readable direction name ("ltr", "rtl"); never NULL; "unknown" when
 *  undefined. */
const char *robo_layout_text_direction_name(robo_layout_text_direction_t direction);

/* ------------------------------------------------------------------ */
/* Style                                                               */
/* ------------------------------------------------------------------ */

/**
 * The CSS font size this build assumes when nothing declares one.
 *
 * `medium` is `font-size`'s initial value in CSS (RoboCSSPropertyRegistry.h
 * records it as such), and CSS's own reference table resolves it to 16px. The
 * number is here, spelled once, so that every reader of a text style gets the
 * same answer and none of them invents one.
 */
#define ROBO_LAYOUT_TEXT_INITIAL_FONT_SIZE_PX 16.0

/** The CSS numeric weight this build assumes for `font-weight: normal`. */
#define ROBO_LAYOUT_TEXT_WEIGHT_NORMAL 400
/** The CSS numeric weight this build assumes for `font-weight: bold`. */
#define ROBO_LAYOUT_TEXT_WEIGHT_BOLD 700

/**
 * The font inputs a measurement needs, resolved from a computed style.
 *
 * A *resolved* record rather than the computed style itself, because a measurer
 * that took a `robo_css_computed_style_t` would be a measurer that had to know
 * CSS. The seam's whole purpose is that the thing behind it does not: it is handed
 * a size in pixels, a weight, an italic flag and a direction, and it answers a
 * width. Everything CSS about that conversion happens on this side of the seam, in
 * `robo_layout_text_style_resolve`, once.
 *
 * Where a value cannot be resolved the *initial* value is used, and that is a
 * CSS fact rather than a fallback: an element nothing declared `font-size` for has
 * `font-size: medium`, exactly as an element nothing declared `display` for is
 * `display: inline` (RoboLayoutStyle.h makes the same argument for the same
 * reason). `font-size: 2em` resolves to nothing at this stage -- a relative unit
 * needs a parent font, which is 10.8's -- and takes the initial value rather than
 * a guess.
 */
typedef struct robo_layout_text_style {
    /** Font size in CSS pixels; always finite and > 0. */
    double font_size_px;
    /** CSS numeric font weight (100..900 for the standard scale). */
    int32_t font_weight;
    /** Non-zero for `font-style: italic` or `oblique`. */
    int italic;
    /** The declared direction. */
    robo_layout_text_direction_t direction;
} robo_layout_text_style_t;

/** Clear @p style to the CSS initial values (16px, normal weight, upright, LTR).
 *  Safe on NULL. */
void robo_layout_text_style_init(robo_layout_text_style_t *style);

/**
 * Resolve the font inputs @p computed declares, filling @p out_style.
 *
 * Reads exactly four properties, each through the one generic door 10.3 uses for
 * a declared length (`robo_css_computed_style_computed_by_name`), each with the
 * property's initial value when the style carries no answer:
 *
 *   font-size    a computed LENGTH in px. RoboCSS computes an *absolute* unit to
 *                px (9.8's rule), so `20px` and `1in` both arrive usable; `2em`,
 *                `50%` and `larger` do not, and take the initial.
 *   font-weight  a NUMBER (`700`) or a KEYWORD (`normal`, `bold`, `bolder`,
 *                `lighter`). The two relative keywords are resolved against the
 *                *initial* weight, which is what CSS does when there is no
 *                inherited weight to be relative to -- and this record has no
 *                parent, so that is the honest reading.
 *   font-style   a KEYWORD: `italic` or `oblique` set the flag.
 *   direction    a KEYWORD: `rtl` selects RTL, everything else LTR.
 *
 * @p computed may be NULL, which means "nothing was computed" and yields the
 * initial values rather than a failure -- the same reading RoboLayoutStyle.h
 * gives a NULL style for `display`.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p out_style. A style this
 *         build cannot read is never a failure: it is written as the initial value
 *         it would have had.
 */
robo_result_t robo_layout_text_style_resolve(const robo_css_computed_style_t *computed,
                                             robo_layout_text_style_t *out_style);

/* ------------------------------------------------------------------ */
/* Request and run                                                     */
/* ------------------------------------------------------------------ */

/**
 * What to measure: a span of one text node, and the font inputs to measure it
 * with.
 *
 * The node is a **borrowed handle**, so a request is a value a caller can build
 * on its stack and pass by pointer with no ownership question. `range` is in code
 * points (above) and `style` is the resolved record, never the computed style --
 * see its own note.
 */
typedef struct robo_layout_text_request {
    /** The text node whose code points the range indexes. Must be well formed. */
    robo_html_node_handle_t text_node;
    /** The code-point span to measure. */
    robo_layout_text_range_t range;
    /** The resolved font inputs. Must not be NULL when the seam is called. */
    const robo_layout_text_style_t *style;
    /**
     * The style the inputs were resolved from, borrowed and optional.
     *
     * Carried for a measurer that can use more of CSS than the four inputs
     * resolve to -- a fallback chain needs `font-family`, and 10.6's line box
     * will want `line-height`. A measurer that needs none of it ignores it, which
     * is what this build's does. It is never a substitute for @ref style: the
     * inputs are already resolved, so a measurer that read only this would be
     * re-deriving them, which is the second copy of a fact this module refuses
     * to keep.
     */
    const robo_css_computed_style_t *computed;
} robo_layout_text_request_t;

/**
 * What a measurement produced: the run, measured.
 *
 * The record a text object's geometry comes from, and it names its own source so
 * that a run held on its own -- in a test, in a diagnostic, in a later phase's
 * glyph-run list -- still says which text and which span it is. Everything in it
 * is already in the integer pixel space the results use, so the pass writes these
 * numbers into a `robo_layout_result_t` without a second rounding.
 *
 * Zeroed by `robo_layout_text_run_init` before a measurer fills it, so a measurer
 * that fills only what it knows leaves the rest as an honest zero rather than as
 * whatever the stack held.
 */
typedef struct robo_layout_text_run {
    /** The text node this run came from. */
    robo_html_node_handle_t source;
    /** The span, echoed back. A measurer that ignores it is a bug, not a
     *  simplification: the run's geometry is the geometry *of the range*. */
    robo_layout_text_range_t range;
    /** The direction the run was measured in. */
    robo_layout_text_direction_t direction;

    /** Advance width: the sum of every glyph's advance, in px. */
    int32_t width;
    /** Line extent: ascent + descent + line gap, in px. */
    int32_t height;
    /** Above the baseline, in px. The baseline itself is 10.7's. */
    int32_t ascent;
    /** Below the baseline, in px, as a positive magnitude. */
    int32_t descent;

    /** Code points the measurement covered. */
    uint32_t code_points;
    /**
     * Code points the font had no glyph for.
     *
     * Counted rather than dropped, and never zero-by-accident: a font that lacks
     * `م` still measures the run -- at `.notdef`'s advance -- and this counter is
     * how a caller learns that the width it read is a width for a run the font
     * cannot draw. Font *fallback* is a later phase's; noticing the miss is this
     * one's, because a build that reported a plausible width for undrawable text
     * would be the single failure no later phase could detect.
     */
    uint32_t glyphs_missing;
} robo_layout_text_run_t;

/** Clear @p run: no source, empty range, LTR, every measurement zero. Safe on
 *  NULL. */
void robo_layout_text_run_init(robo_layout_text_run_t *run);

/* ------------------------------------------------------------------ */
/* The seam                                                            */
/* ------------------------------------------------------------------ */

/**
 * Answer "how wide is this run?".
 *
 * @p request is filled and valid whenever the seam calls (node well formed, range
 * inside the node, style non-NULL).
 * @p out_run is cleared before the call and must be filled in place.
 *
 * Returning ROBO_OK means the answer in @p out_run is a measurement. A measurer
 * that cannot measure -- no font, an unmapped code point, a face whose metrics
 * will not parse -- has two honest options and must pick one deliberately: answer
 * ROBO_OK with zeros and set `glyphs_missing`, which says "I looked and found
 * nothing"; or return a failure, which says "I could not look". It must not
 * return a plausible number it does not have.
 */
typedef robo_result_t (*robo_layout_text_measure_fn)(
    void *userdata,
    const robo_layout_text_request_t *request,
    robo_layout_text_run_t *out_run);

/** Who measures, and what it knows. A record by value, owned by the caller. */
typedef struct robo_layout_text_metrics {
    void *userdata;
    /** The callback. NULL means "this source measures nothing". */
    robo_layout_text_measure_fn measure;
} robo_layout_text_metrics_t;

/** Clear @p metrics to "measures nothing". Safe on NULL. */
void robo_layout_text_metrics_init(robo_layout_text_metrics_t *metrics);

/** Non-zero when @p metrics has a callback. Says nothing about whether any
 *  particular run will measure to anything. */
int robo_layout_text_metrics_is_usable(const robo_layout_text_metrics_t *metrics);

/**
 * Measure @p request through @p metrics.
 *
 * The one call the pass makes, so that "no source", "a source with no callback"
 * and "the callback failed" need not be handled three times at the call site --
 * the same shape `robo_layout_style_source_style` has.
 *
 * A NULL or unusable @p metrics is **not** a failure: it produces a run measured
 * as zero with the range echoed, which is what "nothing measured this" looks like
 * as geometry, and which leaves a document laid out exactly as 10.4 left it. The
 * caller counts that case separately (RoboLayoutInline.h) rather than having to
 * infer it.
 *
 * @p out_run is cleared first, so a failure never leaves a half-filled run.
 *
 * @return ROBO_OK whenever a run was produced -- including a zero one, which is
 *         an answer about the input rather than a failure of this call;
 *         ROBO_INVALID_ARGUMENT for a NULL @p request, @p out_run, an
 *         ill-formed node, or a NULL style; whatever the callback returned when
 *         it failed.
 */
robo_result_t robo_layout_text_measure(const robo_layout_text_metrics_t *metrics,
                                       const robo_layout_text_request_t *request,
                                       robo_layout_text_run_t *out_run,
                                       robo_layout_diag_t *diag);

/**
 * Measure a whole text node, using the computed style of the element that owns
 * it.
 *
 * The one call the layout pass makes, and it exists so that the pass never has to
 * know three things it would otherwise each get subtly wrong:
 *
 *   the **range**. It is the full node, built from the node's own code-point count
 *   through RoboUnicode rather than from a byte length -- a text node holding "hé"
 *   is two code points and three bytes, and a range built from the byte count would
 *   ask to measure a character that is not there.
 *
 *   the **style**. It is @p owner_style, the *element's* computed style, and the
 *   resolution to font inputs happens here (RoboLayoutText.h's style section). A
 *   pass that resolved it itself would be re-deriving inherited values, which is the
 *   second copy of a fact this module refuses to keep.
 *
 *   the **initial values**. A NULL @p owner_style is not a failure: it is "nothing
 *   was computed for this element", and the run is measured with the CSS initial
 *   font inputs, exactly as 10.1's builder treats a style-less element as a fact
 *   rather than an error.
 *
 * The range is the *whole* node. Splitting a node into several runs is what line
 * breaking does, and line breaking is 10.6's -- so this phase's runs are one per
 * text node, which is the honest shape before anything splits them.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or an ill-formed
 *         handle; ROBO_NOT_FOUND when the node does not resolve; whatever the
 *         measurer returned when it failed. A node whose text is empty measures to
 *         a zero-size run rather than failing: an empty run is a thing a line can
 *         hold.
 */
robo_result_t robo_layout_text_measure_node(const robo_html_node_handle_t *text_node,
                                           const robo_css_computed_style_t *owner_style,
                                           const robo_layout_text_metrics_t *metrics,
                                           robo_layout_text_run_t *out_run,
                                           robo_layout_diag_t *diag);

/* ------------------------------------------------------------------ */
/* What the text direction saw                                         */
/* ------------------------------------------------------------------ */

/**
 * Counters for one pass's text work. A record by value, owned by nobody.
 *
 * The third of the pass's records, beside `robo_layout_block_stats_t` and
 * `robo_layout_inline_stats_t`, and the one that answers the question neither of
 * them can: **how much of the text did anything actually measure?** A document
 * laid out with no metrics source and one laid out with a font produce the same
 * boxes, the same lines and -- for a build that measured nothing -- the same
 * geometry, so a reader who saw only the geometry could not tell them apart. That
 * is the failure 10.4's header named and this record closes.
 *
 * `measured` and `unmeasured` are the pair that matters, and they are deliberately
 * not one counter and a zero: `unmeasured` counts runs the seam was asked about
 * with no usable source, which is the documented degradation, while `measured`
 * counts runs a *source answered*. A source that answers zero for every run is
 * therefore visible as `measured` with `width_max` 0 -- "I looked and found
 * nothing" -- which is a different statement from "nobody looked".
 */
typedef struct robo_layout_text_stats {
    /** Text runs given geometry: text objects placed along a line. This plus a
     *  block pass's `blocks_placed` and an inline pass's box participants is every
     *  object in a laid-out document that has a result. */
    size_t runs_placed;
    /** Runs a metrics source measured. Zero for a pass with no source, which is
     *  also what a pass over a document with no text reports -- and the two are
     *  told apart by `runs_placed`. */
    size_t measured;
    /** Runs the seam answered with zero because no usable source was installed.
     *  This is the phase's arrival condition: it is 0 in a build that measures, and
     *  it is what keeps a document laid out exactly as 10.4 left it when nothing
     *  can measure. */
    size_t unmeasured;
    /** Of the placed runs, those whose range covered no code point at all: an
     *  empty text node, or one whose entire content is collapsible whitespace. They
     *  are placed, they measure to zero, and they are counted separately because
     *  "this line has an empty run on it" is a different document from "this line
     *  has no run". */
    size_t empty;
    /** Code points across every placed run. The one number that says how much text
     *  the pass walked, and the number a suite compares against the document's own
     *  code-point count rather than against a guess. */
    size_t code_points;
    /**
     * Code points with no glyph in the face that measured them.
     *
     * Counted rather than dropped: a run the font cannot draw still has a
     * plausible-looking width (it is measured at `.notdef`'s advance), and this
     * counter is the only way a caller learns that the geometry it read describes
     * text that would render as boxes. Font *fallback* is a later goal's.
     */
    size_t glyphs_missing;
    /** Of the placed runs, those whose declared direction was RTL. A count and not
     *  an ordering: the bidi algorithm, which reorders runs *inside* a paragraph,
     *  is a later goal's and this record does not pretend to it. */
    size_t rtl;
    /** Widest placed run, in px. Zero for an unmeasured pass, which is a fact
     *  about the source rather than about the document. */
    int32_t width_max;
    /** Tallest placed run, in px: the widest and tallest are the two numbers a
     *  line box's height and a container's extent come from, and 10.6 will read
     *  them rather than re-measuring. */
    int32_t height_max;
    /** Deepest text object placed. */
    uint32_t depth_max;
} robo_layout_text_stats_t;

/** Clear @p stats to zero. Safe on NULL. */
void robo_layout_text_stats_init(robo_layout_text_stats_t *stats);

/** Field-by-field equality. Safe on NULL. */
int robo_layout_text_stats_equal(const robo_layout_text_stats_t *a,
                                 const robo_layout_text_stats_t *b);

/* ------------------------------------------------------------------ */
/* Reading a text node                                                 */
/* ------------------------------------------------------------------ */

/**
 * Number of Unicode code points in @p text_node.
 *
 * Through RoboUnicode's decoder in REPLACE mode, so a lone surrogate or a
 * truncated sequence counts as the one replacement character it will be treated
 * as rather than aborting the count -- the HTML tokenizer already decided what the
 * bytes mean, and a text layer that refused to measure a document over one bad
 * byte would be refusing a document the DOM accepted.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or an ill-formed
 *         handle; ROBO_NOT_FOUND for a handle that does not resolve.
 */
robo_result_t robo_layout_text_node_code_points(const robo_html_node_handle_t *text_node,
                                               uint32_t *out_count,
                                               robo_layout_diag_t *diag);

/**
 * The byte span a code-point @p range denotes inside @p text_node.
 *
 * The bridge between this header's offsets and the DOM's bytes, and the reason
 * the two units never have to be mixed by a caller. Both endpoints must land on a
 * code-point boundary: @p out_start is the byte index of code point `range.start`
 * and @p out_length is the number of bytes the range's code points occupy, which
 * is also the length of the byte span that *decodes to* exactly those code points.
 *
 * An empty range is legal and yields a zero-length span at the right byte offset,
 * which is what a caret position is.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments, an ill-formed handle
 *         or a NULL @p out; ROBO_NOT_FOUND for a handle that does not resolve;
 *         ROBO_INVALID_ARGUMENT when the range does not lie inside the node, which
 *         includes both an endpoint past the last code point and a range that ends
 *         mid-sequence.
 *
 *         A range outside the text is reported as an invalid *argument* rather than
 *         as a range error, and that is a fact about the runtime rather than a
 *         preference: `robo_result_t` (RoboBase/RoboError.h) is a small shared enum
 *         with no range code in it, so inventing one here would be a second error
 *         vocabulary for one module. The *reason* is carried by the diagnostic --
 *         "a text range ends past the end of the text" is not the same sentence as
 *         "a text range cannot end before it starts" -- which is what a caller reads
 *         when it needs to know which of the two it passed.
 */
robo_result_t robo_layout_text_range_bytes(const robo_html_node_handle_t *text_node,
                                          robo_layout_text_range_t range,
                                          size_t *out_start,
                                          size_t *out_length,
                                          robo_layout_diag_t *diag);

/**
 * Non-zero when every code point of @p text is one of CSS's collapsible
 * whitespace characters (space, tab, LF, FF, CR).
 *
 * The builder's one text rule (RoboLayoutBuilder.h argues it): a text node whose
 * content is *entirely* collapsible whitespace generates no text object. That is
 * not whitespace collapsing -- which is a line-box algorithm this build does not
 * have -- it is its first consequence, and it is what keeps the newline between
 * two elements in a pretty-printed document from becoming a run of spaces.
 *
 * An empty text is whitespace-only, vacuously. @p text may be NULL, which is
 * whitespace-only for the same reason an empty document has no text in it.
 */
int robo_layout_text_is_collapsible_whitespace(const char *text, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_TEXT_H */
