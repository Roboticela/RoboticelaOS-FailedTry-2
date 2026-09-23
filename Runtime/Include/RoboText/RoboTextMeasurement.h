/*
 * RoboText's measurement (Phases 12.4, 12.15) — how much space an already-shaped text requires.
 *
 * ## Measurement consumes shaped output; it never shapes
 *
 * The pipeline is `GlyphRunList → TextMeasurement`, and that is the whole of it:
 *
 *     RoboTextShape        text → glyph runs → GlyphRunList
 *     RoboTextMeasurement  GlyphRunList → width, advances, metrics, bounds
 *     RoboLayout           TextMeasurement → boxes
 *
 * Nothing in this header decodes UTF-8, selects a font, resolves a script or a level, runs GSUB or GPOS,
 * applies kerning or substitutes a ligature. The shaped result is already final, so the width a line is
 * justified to and the pen the painter accumulates are sums of the *same* numbers by construction. A door
 * here that re-shaped would be a door that could disagree with the paint, which is the disagreement 12.4
 * removed and 12.15's first invariant keeps removed.
 *
 * ## The three geometries, kept apart
 *
 *   advance        how far the pen moves. `width` (the horizontal advance) and `advanceY`; a whitespace
 *                  glyph has an advance and no ink, which is exactly why the two are different fields.
 *   logical bounds the box layout places: the advance wide, the line tall, on the baseline.
 *   ink bounds     what the glyphs' outlines actually occupy. A combining mark contributes no advance and
 *                  still paints above its base, so ink is wider *or taller* than logical and never equal to
 *                  it by assumption.
 *
 * Folding any one of the three into another is a defect rather than a simplification — the same rule the
 * glyph mask layer states one phase down about coverage, alpha and opacity.
 *
 * ## The baseline is zero
 *
 * Every y in this record is measured from the **baseline**, with y increasing *upward*: `ascent` is above
 * it and `descent` is below it as a positive magnitude. `baseline` is a field and it is always `0.0f`,
 * because a measurement is a statement about text and not about where a renderer put it. Layout adds a
 * line's baseline to the geometry when it places the line; a measurement that carried a top-left origin
 * would be a second coordinate space that has to agree with layout's, and it never does.
 *
 * ## What is deliberately absent
 *
 * No line breaking, no wrapping, no justification, no hyphenation and no paragraph arrangement (Phase 19).
 * A newline is an ordinary scalar here and measures as the face draws it; arranging lines is
 * `TextLayout`'s job, and the boundary is stated rather than blurred: this answers *how much space*, layout
 * answers *where the space goes*.
 */
#ifndef ROBO_TEXT_MEASUREMENT_H
#define ROBO_TEXT_MEASUREMENT_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextFont.h"
#include "RoboText/RoboTextRunList.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

/**
 * A rectangle in the measurement's own space: origin plus size, `x` to the right of the run's origin and `y`
 * **up** from the baseline.
 *
 * Origin-plus-size rather than four edges, because that is the shape `GfxRectF` one layer down uses and a
 * rectangle that has to be converted between two conventions is a rectangle that will be converted wrongly
 * once. An empty rectangle is a non-positive extent — the same rule `GfxRectIsEmpty` states — which makes
 * "no ink yet" and "ink of no area" the same value on purpose: both contribute nothing to a union, and nothing
 * in this module needs to tell them apart (the `hasInkBounds` flag on the measurement answers *whether* bounds
 * were computed).
 */
typedef struct RoboTextMeasureRect {
    /** Origin on x: the left edge. */
    float x;
    /** Origin on y: the **bottom** edge, y increasing up from the baseline. */
    float y;
    /** Extent on x; never negative in a valid rect. */
    float width;
    /** Extent on y; never negative in a valid rect. */
    float height;
} RoboTextMeasureRect;

/** Clear @p rect to the empty rect: zero origin, zero extent. Safe on NULL. */
void RoboTextMeasureRectInit(RoboTextMeasureRect *rect);

/** Non-zero when @p rect covers no area, by the `GfxRectIsEmpty` rule. Safe on NULL. */
int RoboTextMeasureRectIsEmpty(const RoboTextMeasureRect *rect);

/**
 * Grow @p rect to cover @p other.
 *
 * An **empty** @p rect adopts @p other rather than unioning with a zero origin, which is what makes this the
 * accumulator an ink walk wants: the walk starts from an initialised rect and unions each glyph's box, without
 * carrying a "have I seen one yet" flag beside it. An empty @p other contributes nothing. NULL is ignored.
 */
void RoboTextMeasureRectUnion(RoboTextMeasureRect *rect, const RoboTextMeasureRect *other);

/**
 * Grow @p rect to cover the box whose edges are (@p minX, @p minY)–(@p maxX, @p maxY).
 *
 * Inverted edges denote an empty box and contribute nothing. An empty @p rect adopts this box; see
 * @ref RoboTextMeasureRectUnion for why that is the useful convention rather than a surprise.
 */
void RoboTextMeasureRectUnionLtrb(RoboTextMeasureRect *rect,
                                  float minX,
                                  float minY,
                                  float maxX,
                                  float maxY);

/** Width of @p rect, or 0 for a NULL or empty one. Never negative. */
float RoboTextMeasureRectWidth(const RoboTextMeasureRect *rect);

/** See @ref RoboTextMeasureRectWidth: the height. */
float RoboTextMeasureRectHeight(const RoboTextMeasureRect *rect);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextMeasureRectEqual(const RoboTextMeasureRect *a, const RoboTextMeasureRect *b);

/**
 * The line metrics a line box uses (Phase 12.15.15), aggregated over every run that participates.
 *
 * The policy, and it is a policy rather than a coincidence: `ascent` is the **maximum** participating
 * ascent, `descent` the maximum descent, `lineGap` the maximum gap, and `height` their sum. A line of
 * `"Hello 世界"` in two faces is as tall as its tallest run, which is what makes a mixed-font line's
 * baseline placement deterministic instead of dependent on which run happened to be first.
 *
 * `lineHeight` is different from `height`, and the difference is real: `lineHeight` is what a line box
 * *occupies* (the context's declared leading when it declares one, else the same sum), while `height` is
 * the typographic sum of the three metrics. A caller with an explicit `line-height` reads the first; one
 * computing leading itself reads the second.
 */
typedef struct RoboTextMeasureLineMetrics {
    /** Above the baseline, in px: the maximum over participating runs. */
    float ascent;
    /** Below the baseline, in px, as a magnitude: the maximum over participating runs. */
    float descent;
    /** The face's own line gap, in px: the maximum over participating runs. */
    float lineGap;
    /** `ascent + descent + lineGap`. */
    float height;
    /** The leading a line box uses: the declared one when a run declares it, else @ref height. */
    float lineHeight;
    /** Runs that contributed. Zero when the text had no runs, in which case a metrics font was asked. */
    size_t runCount;
    /** Non-zero when the metrics came from a face rather than from this build's documented estimate. */
    int fromFace;
} RoboTextMeasureLineMetrics;

/** Zero @p metrics. Safe on NULL. */
void RoboTextMeasureLineMetricsInit(RoboTextMeasureLineMetrics *metrics);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextMeasureLineMetricsEqual(const RoboTextMeasureLineMetrics *a,
                                    const RoboTextMeasureLineMetrics *b);

/* ------------------------------------------------------------------ */
/* The request                                                         */
/* ------------------------------------------------------------------ */

/**
 * What a caller wants out of a measurement.
 *
 * The bits exist because the three geometries cost different amounts: advances and metrics are sums over
 * arrays that already exist, while ink bounds ask the face for each distinct glyph's outline box. A caller
 * that wants a width for a line-break candidate — asked once per candidate, thousands of times a frame —
 * must not pay for outlines it never reads.
 */
typedef enum RoboTextMeasureOption {
    /** Advances, counts and line metrics. The cheapest and always computed. */
    RoboTextMeasureOptionNone = 0,
    /** Also fill `logicalBounds`: the box layout places. */
    RoboTextMeasureOptionLogicalBounds = 1u << 0,
    /** Also fill `inkBounds`, by asking the face for each glyph's outline box. */
    RoboTextMeasureOptionInkBounds = 1u << 1
} RoboTextMeasureOption;

/** A bit word of @ref RoboTextMeasureOption. */
typedef uint32_t RoboTextMeasureOptions;

/** Every option: the complete measurement, ink bounds included. */
#define RoboTextMeasureOptionsAll \
    ((RoboTextMeasureOptions)(RoboTextMeasureOptionLogicalBounds | RoboTextMeasureOptionInkBounds))

/** Non-zero when @p option is a defined single option. */
int RoboTextMeasureOptionIsKnown(RoboTextMeasureOption option);

/** Name of a single @p option (`"ink-bounds"`); `"none"` for @ref RoboTextMeasureOptionNone. Never NULL. */
const char *RoboTextMeasureOptionName(RoboTextMeasureOption option);

/** @p options as `"logical-bounds|ink-bounds"`. Complete or absent, like every dump here. */
Result RoboTextMeasureOptionsWrite(RoboTextMeasureOptions options,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength);

/** What a caller wants. A value, so it can be copied and held. */
typedef struct RoboTextMeasureRequest {
    /** Which geometries to compute; at least the advances and the metrics are always computed. */
    RoboTextMeasureOptions options;
} RoboTextMeasureRequest;

/** Fill @p request with the defaults: advances, counts and line metrics, no bounds. */
void RoboTextMeasureRequestInit(RoboTextMeasureRequest *request);

/** Every option on: the request a diagnostic or a test uses. */
void RoboTextMeasureRequestInitFull(RoboTextMeasureRequest *request);

/* ------------------------------------------------------------------ */
/* The result                                                          */
/* ------------------------------------------------------------------ */

/**
 * What one text measures to, at one font context (Phases 12.4, 12.15).
 *
 * A value with no pointers, so it may be copied, compared and held by a caller without a lifetime question.
 * `width` is the horizontal advance and is the number a layout engine justifies against; `advanceY` is its
 * vertical twin and is **0** for every text this build produces, because vertical writing is 12.20's and a
 * consumer accumulating progress in an axis it has not been given needs the field rather than two APIs.
 */
typedef struct RoboTextMeasurement {
    /** Total horizontal advance, in device pixels: the sum of every glyph run's own advance, rounded once. */
    float width;
    /**
     * Total vertical advance, in device pixels (Phase 12.15.23): zero for horizontal text.
     *
     * Present rather than absent so that a vertical-writing goal changes the value and not the record.
     */
    float advanceY;
    /** Above the baseline, in px: the maximum participating run's ascent. */
    float ascent;
    /** Below the baseline, in px, as a magnitude. */
    float descent;
    /** The face's own line gap, in px, as a magnitude. */
    float lineGap;
    /** The leading a line box uses: the context's declared line height, or the face's line extent. */
    float lineHeight;
    /** The baseline, in the measurement's own space. Always `0.0f`; see the header. */
    float baseline;
    /** Glyphs generated. */
    size_t glyphCount;
    /** Glyph runs the text produced. */
    size_t glyphRunCount;
    /** Scalars the text covered. */
    RoboTextScalarIndex scalarCount;
    /** Scalars the face had no glyph for. */
    size_t missingGlyphs;
    /** Widest single glyph advance, in px. Zero for empty text. */
    float advanceMax;
    /**
     * What the glyphs' outlines occupy: ascent above the baseline, descent below it, and overhang on either
     * side. Meaningful only when @ref hasInkBounds is non-zero.
     *
     * Wider or taller than @ref logicalBounds wherever a mark, a swash or an italic overhang reaches past the
     * advance — which is the case this distinction exists for.
     */
    RoboTextMeasureRect inkBounds;
    /** Non-zero when @ref inkBounds was computed **and** every glyph the face could answer for had a box. */
    int hasInkBounds;
    /** The box layout places: origin at (0,0) on the baseline, `width` across, ascent+descent tall. */
    RoboTextMeasureRect logicalBounds;
    /** Non-zero when @ref logicalBounds was computed. */
    int hasLogicalBounds;
    /** The face's metrics at the context's size, for a caller that wants the record rather than three fields. */
    RoboTextMeasureLineMetrics lineMetrics;
} RoboTextMeasurement;

/** Clear @p measurement to an empty measurement of no text. Safe on NULL. */
void RoboTextMeasurementInit(RoboTextMeasurement *measurement);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextMeasurementEqual(const RoboTextMeasurement *a, const RoboTextMeasurement *b);

/** Human-readable one-line summary. RoboNotSupported when it does not fit @p capacity. */
Result RoboTextMeasurementWrite(const RoboTextMeasurement *measurement,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength);

/* ------------------------------------------------------------------ */
/* The engine                                                          */
/* ------------------------------------------------------------------ */

/**
 * Measure an already-shaped text: the one door a layout pass needs (Phase 12.15).
 *
 * Reads the runs' own advances, metrics and glyph arrays and computes nothing by shaping. For each run the
 * glyphs' device advances are summed in `double` and rounded **once**, which is the same accumulation the
 * painter's pen performs, so `width` and the painted pen agree by construction rather than by two people
 * writing the same three lines carefully.
 *
 * Mixed fonts, which fallback makes ordinary (Phase 12.14.17): a run whose source continues the previous one
 * contributes its own advance to `width`, and its own metrics to the line box by the maximum rule. Nothing
 * here reverses a right-to-left run: the total is the sum of the logical run sequence's advances, because a
 * direction changes *where* a run sits and never how wide it is.
 *
 * @param list     the shaped text. **Not** re-shaped, not reordered. May be NULL only with @p out NULL.
 * @param metricsFont the context whose metrics answer for a text with **no runs at all** (Phase 12.15.17).
 *                 Borrowed, may be NULL, and is never consulted when the list has a run — a run already
 *                 carries the metrics of the face it was set in, and asking a font again would be the
 *                 second reading this phase exists to prevent.
 * @param request  what to compute. May be NULL, which means the default (no bounds).
 * @param out      receives the measurement. Cleared even on failure.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p list or @p out; RoboError when a bound could not be
 *         computed because the face's metrics will not parse.
 */
Result RoboTextMeasureGlyphRunList(const RoboTextGlyphRunList *list,
                                   const RoboTextFontContext *metricsFont,
                                   const RoboTextMeasureRequest *request,
                                   RoboTextMeasurement *out,
                                   RoboTextDiag *diag);

/**
 * The line metrics of @p list alone (Phase 12.15.15), by the same maximum rule the engine uses.
 *
 * One policy, one implementation: this and the engine's `ascent`/`descent`/`lineGap` cannot disagree, because
 * the engine calls this. A caller that wants the record rather than the three fields reads it here.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument; RoboError when a face's metrics will not parse.
 */
Result RoboTextMeasureGlyphRunListLineMetrics(const RoboTextGlyphRunList *list,
                                              const RoboTextFontContext *metricsFont,
                                              RoboTextMeasureLineMetrics *out,
                                              RoboTextDiag *diag);

/**
 * The ink bounds of @p list alone (Phase 12.15.10): what the glyphs' outlines occupy.
 *
 * Each glyph's outline box comes from the face through `RoboTextFontGlyphMetrics`, scaled by the **run's own**
 * context — the module's one scaling door — translated by the glyph's placement offset and by the pen position
 * the run's own prefix advances put it at. The advance box is deliberately not used as the ink box: a space has
 * an advance and no ink, and a mark has no advance and ink.
 *
 * A glyph whose face carries no outline box (a CFF face, or a glyph with no outline data) is **skipped and
 * counted** rather than approximated: `outComplete` is false and the bounds cover the glyphs the face could
 * answer for. Answering with the advance box for such a glyph would report an ink rectangle that paints
 * nothing, which is the one thing this door must not do.
 *
 * @param outComplete receives non-zero when every glyph contributed a box.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument; RoboError when a face's metrics will not parse.
 */
Result RoboTextMeasureGlyphRunListInkBounds(const RoboTextGlyphRunList *list,
                                            RoboTextMeasureRect *out,
                                            int *outComplete,
                                            RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* Tolerance, validation and diagnostics                               */
/* ------------------------------------------------------------------ */

/**
 * The numerical tolerance two measurements are compared within: 1/64 px.
 *
 * The project's subpixel precision, and the rule is that tests compare within it rather than with `==`. It is
 * *not* a licence for an inexact pipeline: two measurements of the same unchanged text are bit-identical,
 * because the accumulation is a fixed order of `double` adds. The tolerance exists for a comparison across
 * two *different* but equivalent paths — a shape result's measurement against the same list measured again
 * with a fuller request — where the arithmetic order may legitimately differ.
 */
#define RoboTextMeasureTolerancePx (1.0f / 64.0f)

/** Non-zero when |@p a - @p b| <= @p tolerance. Non-finite values compare equal only to themselves. */
int RoboTextMeasureFloatEqual(float a, float b, float tolerance);

/** Every field of @p a and @p b within @p tolerance, with the counts compared exactly. Safe on NULL. */
int RoboTextMeasurementApproxEqual(const RoboTextMeasurement *a,
                                   const RoboTextMeasurement *b,
                                   float tolerance);

/**
 * Why a measurement is not a valid record (Phase 12.15.33).
 *
 * One enumerator per invariant, because a validation door that answers only "no" is a door a failing test
 * cannot use. A measurement is produced by a walk over finalized runs, so an invalid one means the walk and the
 * runs disagree — which is exactly the class of bug a test rather than a reader should find.
 */
typedef enum RoboTextMeasureProblem {
    /** Every invariant holds. */
    RoboTextMeasureProblemNone = 0,
    /** A width, an advance, a metric or a bound is `NaN` or infinite. */
    RoboTextMeasureProblemNotFinite,
    /** The stored width disagrees with the sum of the runs' own advances. */
    RoboTextMeasureProblemWidth,
    /** The ascent, the descent or the line gap is negative. */
    RoboTextMeasureProblemMetrics,
    /** The reported glyph or run count disagrees with the runs. */
    RoboTextMeasureProblemCounts,
    /** An ink or logical bound is inverted (its max is below its min). */
    RoboTextMeasureProblemBounds,
    /** @ref RoboTextMeasurement::logicalBounds disagrees with the width and metrics it is derived from. */
    RoboTextMeasureProblemLogicalBounds
} RoboTextMeasureProblem;

/** Non-zero when @p problem is a defined enumerator. */
int RoboTextMeasureProblemIsKnown(RoboTextMeasureProblem problem);

/** Name of @p problem (`"logical-bounds"`); `"none"` for the valid answer. Never NULL. */
const char *RoboTextMeasureProblemName(RoboTextMeasureProblem problem);

/**
 * Check every measurement invariant (Phase 12.15.33). O(runs), and O(glyphs) only for the width.
 *
 * The runs are required because the two facts worth checking are *relations*: the width is the sum of the runs'
 * own advances, and the counts are what the runs hold. A door that read the measurement alone could only check
 * that a number is finite, which is not the question — a plausible wrong width is exactly the failure a
 * measurement layer produces and a reader does not notice.
 *
 * @return RoboOk when valid; RoboInvalidArgument for a NULL argument or an invalid measurement, with
 *         @p outProblem naming the first broken invariant.
 */
Result RoboTextMeasureValidate(const RoboTextMeasurement *measurement,
                               const RoboTextGlyphRunList *list,
                               RoboTextMeasureProblem *outProblem,
                               RoboTextDiag *diag);

/**
 * The measurement as a multi-line diagnostic block (Phase 12.15.32): the totals, then a line per run and per
 * glyph.
 *
 * Complete or absent, like every dump here, and `INVALID` first when validation fails over @p list. The block a
 * typography bug is diagnosed from: a width that is 2px out is either a kerning adjustment, an unmapped glyph's
 * `.notdef` advance or a run that fell back to another face, and the per-glyph lines say which.
 *
 * @return RoboOk and @p outLength; RoboInvalidArgument for a NULL argument; RoboNotSupported when @p buffer
 *         cannot hold the block.
 */
Result RoboTextMeasureWrite(const RoboTextMeasurement *measurement,
                            const RoboTextGlyphRunList *list,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_MEASUREMENT_H */
