/*
 * RoboText's shape result and measurement (Phase 12.4) — the one call a layout pass and a painter share.
 *
 * ## One pipeline, two consumers
 *
 *     text ── runs (script, direction, font) ── glyph runs ──┬── measurement  → RoboLayout
 *                                                            └── glyph runs   → RoboPaint → RoboGraphics
 *
 * Before this phase those two consumers each did their own part of it: the layout measurer summed font
 * units through the cmap, and the painter looked up glyphs and accumulated advances again. They agreed
 * because two people wrote the same three lines carefully. After this phase they agree because there is
 * one answer, and the answer is a record the layout pass reads and the painter walks.
 *
 * ## Why a result object rather than a glyph run
 *
 * A text is not one run. `"Hello שלום"` is two text runs and (today) two glyph runs, and while this
 * build's segmentation is coarse it is not a single run: a text with a digit at the front and a Latin
 * letter after it is two direction runs by construction, and a text with Arabic inside English is two
 * script runs. So the unit that comes out of shaping is a **set** of runs with one measurement, and the
 * measurement is computed across the whole set in visual-order-independent logical order — which is
 * what a line-breaking pass wants and what a per-run API could not answer.
 *
 * The result **owns** the run set and the glyph runs and borrows only the text and the face. Destroying
 * it releases everything, which is the lifetime a caller wants and the reason nothing here hands out a
 * pointer the caller must free.
 *
 * ## Measurement is a boundary, and it now lives next door
 *
 * `RoboTextMeasurement` is what the layout seam consumes, and since Phase 12.15 it is computed by
 * `RoboTextMeasurement.h`'s engine over this result's own **run list** — so the number a caller reads here and
 * the number `RoboTextMeasureGlyphRunList` returns for the same list are one implementation, not two. What the
 * result keeps is the *default* measurement: advances, counts and line metrics, with bounds absent because
 * asking for a glyph's outline box per glyph is work a caller who wants a width must not pay for. A caller that
 * wants the bounded measurement asks the engine for it, or calls @ref RoboTextShapeResultMeasure.
 *
 * The three properties that matter for correctness:
 *
 *   - `width` is the sum of the glyphs' own device advances, accumulated in `double` and rounded once;
 *   - `ascent`/`descent`/`lineGap` are the **maximum** over the participating runs, and `lineHeight` honours
 *     the context's declared leading when it declares one;
 *   - `missingGlyphs` counts the scalars the face could not draw, which is the one number that keeps a
 *     plausible width for undrawable text from passing as a measurement.
 */
#ifndef ROBO_TEXT_SHAPE_H
#define ROBO_TEXT_SHAPE_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextBuffer.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextFont.h"
#include "RoboText/RoboTextGlyphs.h"
#include "RoboText/RoboTextMeasurement.h"
#include "RoboText/RoboTextRunList.h"
#include "RoboText/RoboTextRuns.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RoboTextShapeResult RoboTextShapeResult;

/**
 * Segment, generate and measure @p view in one call.
 *
 * The pipeline the header draws, and the only door above it: a caller that wanted the runs *and* the
 * measurement separately would otherwise run two of the three stages twice.
 *
 * A text whose font could not be used at all — an empty context, or a face whose cmap will not parse —
 * is **refused** rather than measured as zero. Every other absence is answered: a face with no glyph for
 * a scalar gets `.notdef` and a counted miss, and text with no strong direction and no declared base
 * gets `RoboTextDirectionUnknown` runs.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument; RoboNotSupported when the font
 *         context names no face; RoboError when the face's cmap will not parse; RoboOutOfMemory.
 */
Result RoboTextShape(RoboTextView view,
                     const RoboTextFontContext *font,
                     const RoboTextShapeOptions *options,
                     RoboTextShapeResult **out,
                     RoboTextDiag *diag);

/** Release @p result and everything it owns. Safe on NULL. */
void RoboTextShapeResultDestroy(RoboTextShapeResult *result);

/** The run set, borrowed from @p result. NULL for a NULL result. */
const RoboTextRunSet *RoboTextShapeResultRuns(const RoboTextShapeResult *result);

/** Glyph runs held. */
size_t RoboTextShapeResultGlyphRunCount(const RoboTextShapeResult *result);

/**
 * The result as a **list** (Phase 12.14.26), borrowed and complete.
 *
 * The same runs @ref RoboTextShapeResultGlyphRun hands out one at a time, arranged the way the rest of the
 * pipeline wants them: logical order as the list, the visual order L2 resolved beside it, one key per run, and
 * the four doors — metrics, total advance, split reason, validation — that a layout pass and a diagnostic ask.
 *
 * It is a **view**, so it is valid exactly as long as @p result is, and destroying the result releases it.
 * Ordering is done; `RoboTextGlyphRunListVisualIsLogical` answers whether the paragraph needed any.
 *
 * Non-NULL for any result this module produced, including one over empty text, so a caller never has to test
 * for the list before reading it.
 */
const RoboTextGlyphRunList *RoboTextShapeResultGlyphRunList(const RoboTextShapeResult *result);

/** Glyph run @p index, borrowed, or NULL when out of range. */
const RoboTextGlyphRun *RoboTextShapeResultGlyphRun(const RoboTextShapeResult *result, size_t index);

/**
 * The glyph run that covers scalar @p scalar, borrowed, or NULL when the text has no run there.
 *
 * The lookup a hit test and a caret need, and the reason the runs are in ascending source order.
 */
const RoboTextGlyphRun *RoboTextShapeResultGlyphRunForScalar(const RoboTextShapeResult *result,
                                                             RoboTextScalarIndex scalar);

/** The measurement, borrowed from @p result. NULL for a NULL result. */
const RoboTextMeasurement *RoboTextShapeResultMeasurement(const RoboTextShapeResult *result);

/**
 * Measure @p result again with @p request, without shaping anything a second time (Phase 12.15).
 *
 * The door for a caller that wants something the result's own measurement does not carry — ink bounds above
 * all, which every shaping call deliberately does not compute. It runs the **same** engine over the same
 * finalized run list, so the advances it reports are bit-identical to the ones the result already holds and
 * the bounds are additional facts rather than a second opinion.
 *
 * @param request may be NULL, which means the default (advances and metrics, no bounds).
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL @p result or @p out; RoboError when a face's
 *         metrics will not parse.
 */
Result RoboTextShapeResultMeasure(const RoboTextShapeResult *result,
                                  const RoboTextMeasureRequest *request,
                                  RoboTextMeasurement *out,
                                  RoboTextDiag *diag);

/**
 * The sum of every glyph run's counters.
 *
 * The record a dump and an assertion read, and — because it is a *sum over runs* rather than a copy of
 * the measurement — the two can be compared: a `glyphs` that disagrees with the measurement's
 * `glyphCount` is a result that is internally inconsistent, and the conformance suite asserts exactly
 * that.
 */
Result RoboTextShapeResultGetGlyphStats(const RoboTextShapeResult *result, RoboTextGlyphStats *out);

/**
 * Measure @p view without keeping the runs.
 *
 * `RoboTextShape` followed by a copy of the measurement and a destroy, and offered because that is what
 * the layout seam does per text node and it should not have to allocate a result object it immediately
 * frees… it does anyway. The difference is ownership: a caller that only wants numbers does not have to
 * hold a handle, and the pipeline above is not duplicated.
 */
Result RoboTextMeasureText(RoboTextView view,
                           const RoboTextFontContext *font,
                           const RoboTextShapeOptions *options,
                           RoboTextMeasurement *out,
                           RoboTextDiag *diag);

/** `RoboTextMeasureText` over an owned buffer's bytes. */
Result RoboTextMeasureBuffer(const RoboTextBuffer *buffer,
                             const RoboTextFontContext *font,
                             const RoboTextShapeOptions *options,
                             RoboTextMeasurement *out,
                             RoboTextDiag *diag);

/**
 * Fuzz entry: shape arbitrary bytes with no face. Never crashes; a result is destroyed if it was made.
 *
 * Exists because the segmentation path decodes attacker-controlled bytes (a document's text) with
 * `RoboUnicodeReplace`, and a decoder plus a segmenter plus an index builder is exactly the shape a
 * malformed input finds.
 */
Result RoboTextFuzzShape(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_SHAPE_H */
