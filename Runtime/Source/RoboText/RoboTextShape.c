/*
 * RoboText's pipeline and measurement (Phase 12.4) — see RoboTextShape.h.
 *
 * The file is the pipeline itself, in the order the header draws it:
 *
 *     RunSetCreate  →  GlyphRunGenerate (once per text run)  →  the measurement, summed across runs
 *
 * and its two decisions are worth reading in code rather than only in the header.
 *
 * **The measurement is summed from the glyphs, not from the font.** Width is `Σ glyph.advanceX` in
 * `double`, rounded once. The alternative — summing font units and scaling at the end — is what the
 * measurer this replaces did, and it is why the measurer and the painter disagreed by a fraction of a
 * pixel: two arithmetic chains over one font. Summing the *same numbers the painter accumulates* is what
 * removes the disagreement rather than shrinking it, and it is why an unmapped scalar's `.notdef` advance
 * is in the total (see the glyph file): the width a line is justified to has to be the width that gets
 * painted, `.notdef` boxes included.
 *
 * **Only two absences are refusals.** "No face" and "the cmap will not parse" mean this build cannot say
 * anything about the text, so it says nothing (`RoboNotSupported` / `RoboError`). Everything else is
 * answered: an unmapped scalar becomes `.notdef` and a counted miss, and text with no strong direction
 * gets `Unknown` runs. A text layer that refused those would push a decision into the caller that has no
 * better answer, and a layer that answered them as zero would report a plausible width for text it cannot
 * draw — which is the failure `missingGlyphs` exists to make visible.
 */

#include "RoboText/RoboTextShape.h"

#include "RoboMem/RoboMem.h"
#include "RoboTextInternal.h"

#include <string.h>

struct RoboTextShapeResult {
    RoboTextRunSet *runs;
    /** The runs themselves, owned here: the array is the storage the list views (Phase 12.14). */
    RoboTextGlyphRun **glyphRuns;
    size_t glyphRunCount;
    RoboTextMeasurement measurement;
    /**
     * The logical list over @ref glyphRuns, ordered visually from the run set's own bidi paragraph.
     *
     * A **view**: the runs belong to @ref glyphRuns and the list only indexes them. It exists so that the four
     * questions a paragraph asks — what it measures to, where each run sits visually, whether two runs are one
     * shape run, whether the set is consistent — are answered by the module rather than by each caller. It is
     * built once, at the end of shaping, so that a result is *complete* rather than complete-on-demand.
     */
    RoboTextGlyphRunList *list;
    /**
     * The context the text was shaped with, **borrowed** for exactly as long as the runs borrow its face.
     *
     * Kept because `RoboTextShapeResultMeasure` re-measures the result's own list and the engine needs a context
     * to answer for a text with no runs at all (an empty text still has a line). It is the same borrowed lifetime
     * the runs already have, so it adds no lifetime a caller did not already owe.
     */
    RoboTextFontContext font;
};

/* ------------------------------------------------------------------ */
/* The pipeline                                                        */
/* ------------------------------------------------------------------ */

void RoboTextShapeResultDestroy(RoboTextShapeResult *result)
{
    if (!result) {
        return;
    }
    /*
     * The list first: it is a view, so destroying it releases the list and its order arrays and **not** the runs
     * below, which are this result's. The other order would leave the list holding freed pointers for the length
     * of a destroy, which is exactly the kind of window a double free lives in.
     */
    RoboTextGlyphRunListDestroy(result->list);
    for (size_t index = 0u; index < result->glyphRunCount; index++) {
        RoboTextGlyphRunDestroy(result->glyphRuns[index]);
    }
    RoboFree(result->glyphRuns);
    RoboTextRunSetDestroy(result->runs);
    RoboFree(result);
}

/**
 * Fill @p measurement from the already-generated runs.
 *
 * The **engine's** door (Phase 12.15), not a second walk: `RoboTextMeasureGlyphRunList` computes the advances,
 * the aggregated line metrics (from the runs' own metrics, by the maximum rule) and — when the request asks —
 * the bounds. This function exists to supply the two things the engine deliberately does not: the list, which is
 * built from the runs, and the font context, which answers for a text with **no runs at all**.
 *
 * The request is the default one, and that is a decision rather than an omission: ink bounds ask the face for a
 * glyph's outline box per glyph, and a shaping call whose caller asked for a width must not pay for outlines it
 * will never read. A caller that wants them asks `RoboTextShapeResultMeasure` or the engine, both of which run
 * over this same list and therefore report the same advances.
 */
static Result ShapeMeasure(const RoboTextGlyphRunList *list,
                           const RoboTextFontContext *font,
                           RoboTextMeasurement *out,
                           RoboTextDiag *diag)
{
    RoboTextMeasureRequest request;

    RoboTextMeasureRequestInit(&request);
    return RoboTextMeasureGlyphRunList(list, font, &request, out, diag);
}

Result RoboTextShape(RoboTextView view,
                     const RoboTextFontContext *font,
                     const RoboTextShapeOptions *options,
                     RoboTextShapeResult **out,
                     RoboTextDiag *diag)
{
    RoboTextShapeResult *result;

    if (!out || !font) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a font context and an out pointer are required");
    }
    *out = NULL;
    if (!font->face) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboNotSupported,
                               "no face is bound to the font context");
    }

    result = (RoboTextShapeResult *)RoboCalloc(1u, sizeof(*result));
    if (!result) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboOutOfMemory,
                               "the shape result could not be allocated");
    }
    result->font = *font;
    {
        Result status = RoboTextRunSetCreate(view, font, options, &result->runs, diag);

        if (status != RoboOk) {
            RoboTextShapeResultDestroy(result);
            return status;
        }
    }

    result->glyphRunCount = RoboTextRunSetTextRunCount(result->runs);
    if (result->glyphRunCount != 0u) {
        result->glyphRuns =
            (RoboTextGlyphRun **)RoboCalloc(result->glyphRunCount, sizeof(*result->glyphRuns));
        if (!result->glyphRuns) {
            RoboTextShapeResultDestroy(result);
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboOutOfMemory,
                                   "the glyph run array could not be allocated");
        }
        for (size_t index = 0u; index < result->glyphRunCount; index++) {
            const RoboTextRun *run = RoboTextRunSetTextRun(result->runs, index);
            Result status = RoboTextGlyphRunGenerate(view, run, &result->glyphRuns[index], diag);

            if (status != RoboOk) {
                /* `glyphRunCount` is the count of runs the *result owns*, so it is set as the array is
                 * filled rather than before the loop: a destroy on this path frees exactly what exists. */
                result->glyphRunCount = index;
                RoboTextShapeResultDestroy(result);
                return status;
            }
        }
    }

    /*
     * The list, before the measurement rather than after it.
     *
     * The two orders are the same work in the same place except for one thing: since 12.15 the measurement **is**
     * a door over the list (`RoboTextMeasureGlyphRunList`), so building the list first is what lets the result's
     * own measurement and a caller's second measurement of the same result be one implementation. There is no
     * second walk here for them to disagree in. A run count of zero is legal and gets a list with no runs rather
     * than none at all — "this text shaped to nothing" is an answer, and a result whose list accessor could be
     * NULL would make every caller write the same check.
     */
    {
        Result status = RoboTextGlyphRunListCreateView(result->glyphRuns, result->glyphRunCount,
                                                       &result->list, diag);

        if (status != RoboOk) {
            RoboTextShapeResultDestroy(result);
            return status;
        }
    }

    {
        Result status = ShapeMeasure(result->list, font, &result->measurement, diag);

        if (status != RoboOk) {
            RoboTextShapeResultDestroy(result);
            return status;
        }
    }

    if (result->glyphRunCount != 0u) {
        /*
         * The visual order from the paragraph the run set already resolved (Phase 12.8) — read, never recomputed.
         * A paragraph that does not cover the runs is a state this function cannot produce and does not pretend
         * to handle: the runs came from the run set that owns the paragraph, so the two agree by construction, and
         * the ordering door refuses what it cannot place rather than placing it by a level it never gave.
         */
        const RoboTextBidiParagraph *paragraph = RoboTextRunSetBidiParagraph(result->runs);

        if (paragraph) {
            Result status = RoboTextGlyphRunListOrderVisual(result->list, paragraph, diag);

            if (status != RoboOk) {
                RoboTextShapeResultDestroy(result);
                return status;
            }
        }
    }
    *out = result;
    return RoboOk;
}

const RoboTextRunSet *RoboTextShapeResultRuns(const RoboTextShapeResult *result)
{
    return result ? result->runs : NULL;
}

size_t RoboTextShapeResultGlyphRunCount(const RoboTextShapeResult *result)
{
    return result ? result->glyphRunCount : 0u;
}

const RoboTextGlyphRunList *RoboTextShapeResultGlyphRunList(const RoboTextShapeResult *result)
{
    return result ? result->list : NULL;
}

const RoboTextGlyphRun *RoboTextShapeResultGlyphRun(const RoboTextShapeResult *result, size_t index)
{
    if (!result || index >= result->glyphRunCount) {
        return NULL;
    }
    return result->glyphRuns[index];
}

const RoboTextGlyphRun *RoboTextShapeResultGlyphRunForScalar(const RoboTextShapeResult *result,
                                                             RoboTextScalarIndex scalar)
{
    size_t low;
    size_t high;

    if (!result || result->glyphRunCount == 0u) {
        return NULL;
    }
    low = 0u;
    high = result->glyphRunCount - 1u;
    while (low <= high) {
        size_t mid = low + (high - low) / 2u;
        RoboTextRange range = RoboTextGlyphRunSourceRange(result->glyphRuns[mid]);

        if (RoboTextRangeContains(range, scalar)) {
            return result->glyphRuns[mid];
        }
        if (scalar < range.start) {
            if (mid == 0u) {
                break;
            }
            high = mid - 1u;
        } else {
            low = mid + 1u;
        }
    }
    return NULL;
}

const RoboTextMeasurement *RoboTextShapeResultMeasurement(const RoboTextShapeResult *result)
{
    return result ? &result->measurement : NULL;
}

Result RoboTextShapeResultMeasure(const RoboTextShapeResult *result,
                                  const RoboTextMeasureRequest *request,
                                  RoboTextMeasurement *out,
                                  RoboTextDiag *diag)
{
    if (!result || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a shape result and an out measurement are required");
    }
    /*
     * The result's own list, through the engine — never a second walk over the runs. The face the result was
     * shaped with answers only for a text with no runs at all; a list with runs carries them already.
     */
    return RoboTextMeasureGlyphRunList(result->list, &result->font, request, out, diag);
}

Result RoboTextShapeResultGetGlyphStats(const RoboTextShapeResult *result, RoboTextGlyphStats *out)
{
    RoboTextGlyphStats stats;

    if (!result || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    for (size_t index = 0u; index < result->glyphRunCount; index++) {
        if (RoboTextGlyphRunGetStats(result->glyphRuns[index], &stats) != RoboOk) {
            return RoboError;
        }
        /*
         * **Every** field of the record, and that is a correction rather than a formality: this loop used to
         * sum six of the eleven, so `ligatureGlyphs`, `ligatureScalars`, `mirrored`, `controls` and the two
         * 12.14 counters answered zero for a whole text however many runs held them. A record documented as
         * "the sum of every glyph run's counters" has to be the sum of *every* counter, because the caller
         * cannot tell an absent count from a zeroed one without walking the runs itself — which is the work
         * this door exists to save.
         */
        out->glyphRuns += stats.glyphRuns;
        out->glyphs += stats.glyphs;
        out->missing += stats.missing;
        out->multiScalarSources += stats.multiScalarSources;
        out->ligatureGlyphs += stats.ligatureGlyphs;
        out->ligatureScalars += stats.ligatureScalars;
        out->positioned += stats.positioned;
        out->mirrored += stats.mirrored;
        out->controls += stats.controls;
        out->marks += stats.marks;
        out->clusters += stats.clusters;
        out->advancePx += stats.advancePx;
    }
    return RoboOk;
}

Result RoboTextMeasureText(RoboTextView view,
                           const RoboTextFontContext *font,
                           const RoboTextShapeOptions *options,
                           RoboTextMeasurement *out,
                           RoboTextDiag *diag)
{
    RoboTextShapeResult *result = NULL;
    Result status;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "an out measurement is required");
    }
    RoboTextMeasurementInit(out);
    status = RoboTextShape(view, font, options, &result, diag);
    if (status != RoboOk) {
        return status;
    }
    *out = *RoboTextShapeResultMeasurement(result);
    RoboTextShapeResultDestroy(result);
    return RoboOk;
}

Result RoboTextMeasureBuffer(const RoboTextBuffer *buffer,
                             const RoboTextFontContext *font,
                             const RoboTextShapeOptions *options,
                             RoboTextMeasurement *out,
                             RoboTextDiag *diag)
{
    if (!buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainBuffer, RoboInvalidArgument,
                               "a buffer is required");
    }
    return RoboTextMeasureText(
        RoboTextViewMake(RoboTextBufferBytes(buffer), (size_t)RoboTextBufferByteLength(buffer)), font,
        options, out, diag);
}

Result RoboTextFuzzShape(const uint8_t *data, size_t size)
{
    RoboTextView view;
    RoboTextFontContext font;
    RoboTextShapeResult *result = NULL;
    Result status;

    if (!data && size != 0u) {
        return RoboInvalidArgument;
    }
    view = RoboTextViewMake((const char *)data, size);
    RoboTextFontContextInit(&font);
    status = RoboTextShape(view, &font, NULL, &result, NULL);
    if (status == RoboNotSupported) {
        /* The documented answer for a context with no face, and the path the decode and the segmentation
         * run on: arbitrary bytes, no font. Not a failure. */
        return RoboOk;
    }
    if (status != RoboOk) {
        return status;
    }
    RoboTextShapeResultDestroy(result);
    return RoboOk;
}
