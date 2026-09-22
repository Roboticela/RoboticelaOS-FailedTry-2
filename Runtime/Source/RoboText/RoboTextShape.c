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
    RoboTextGlyphRun **glyphRuns;
    size_t glyphRunCount;
    RoboTextMeasurement measurement;
};

/* ------------------------------------------------------------------ */
/* The measurement                                                     */
/* ------------------------------------------------------------------ */

void RoboTextMeasurementInit(RoboTextMeasurement *measurement)
{
    if (!measurement) {
        return;
    }
    memset(measurement, 0, sizeof(*measurement));
}

int RoboTextMeasurementEqual(const RoboTextMeasurement *a, const RoboTextMeasurement *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->width == b->width && a->ascent == b->ascent && a->descent == b->descent &&
           a->lineHeight == b->lineHeight && a->glyphCount == b->glyphCount &&
           a->glyphRunCount == b->glyphRunCount && a->scalarCount == b->scalarCount &&
           a->missingGlyphs == b->missingGlyphs && a->advanceMax == b->advanceMax;
}

Result RoboTextMeasurementWrite(const RoboTextMeasurement *measurement,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength)
{
    char line[192];
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!measurement || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';

    length = RoboTextWriterText(line, sizeof(line), length, "width=");
    length = RoboTextWriterFloat(line, sizeof(line), length, measurement->width);
    length = RoboTextWriterText(line, sizeof(line), length, " ascent=");
    length = RoboTextWriterFloat(line, sizeof(line), length, measurement->ascent);
    length = RoboTextWriterText(line, sizeof(line), length, " descent=");
    length = RoboTextWriterFloat(line, sizeof(line), length, measurement->descent);
    length = RoboTextWriterText(line, sizeof(line), length, " lineHeight=");
    length = RoboTextWriterFloat(line, sizeof(line), length, measurement->lineHeight);
    length = RoboTextWriterText(line, sizeof(line), length, " advanceMax=");
    length = RoboTextWriterFloat(line, sizeof(line), length, measurement->advanceMax);
    length = RoboTextWriterText(line, sizeof(line), length, " glyphs=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)measurement->glyphCount);
    length = RoboTextWriterText(line, sizeof(line), length, " runs=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)measurement->glyphRunCount);
    length = RoboTextWriterText(line, sizeof(line), length, " scalars=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, measurement->scalarCount);
    length = RoboTextWriterText(line, sizeof(line), length, " missing=");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)measurement->missingGlyphs);
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    if (length + 1u > capacity) {
        if (outLength) {
            *outLength = length;
        }
        return RoboNotSupported;
    }
    memcpy(buffer, line, length + 1u);
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The pipeline                                                        */
/* ------------------------------------------------------------------ */

void RoboTextShapeResultDestroy(RoboTextShapeResult *result)
{
    if (!result) {
        return;
    }
    for (size_t index = 0u; index < result->glyphRunCount; index++) {
        RoboTextGlyphRunDestroy(result->glyphRuns[index]);
    }
    RoboFree(result->glyphRuns);
    RoboTextRunSetDestroy(result->runs);
    RoboFree(result);
}

/**
 * Fill @p measurement from the already-generated runs, and the font context they share.
 *
 * Shared by `RoboTextShape` and by nothing else, because the measurement must be computed from *these*
 * glyph runs rather than from a second generation: a width computed by re-running the generator could
 * differ from the glyphs a painter walks, which is the disagreement this phase exists to end.
 */
static Result ShapeMeasure(const RoboTextRunSet *runs,
                           RoboTextGlyphRun *const *glyphRuns,
                           size_t glyphRunCount,
                           const RoboTextFontContext *font,
                           RoboTextMeasurement *out,
                           RoboTextDiag *diag)
{
    RoboTextFontMetrics metrics;
    double width = 0.0;
    Result status;

    RoboTextMeasurementInit(out);
    out->glyphRunCount = glyphRunCount;
    out->scalarCount = RoboTextRunSetScalarCount(runs);
    for (size_t index = 0u; index < glyphRunCount; index++) {
        const RoboTextGlyphRun *run = glyphRuns[index];
        size_t count = RoboTextGlyphRunCount(run);

        out->glyphCount += count;
        out->missingGlyphs += RoboTextGlyphRunMissingGlyphs(run);
        width += (double)RoboTextGlyphRunAdvance(run);
        for (size_t glyphIndex = 0u; glyphIndex < count; glyphIndex++) {
            const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)glyphIndex);

            if (glyph->advanceX > out->advanceMax) {
                out->advanceMax = glyph->advanceX;
            }
        }
    }
    /* One rounding, here, of a sum accumulated in `double` — the property the header states. */
    out->width = (float)width;

    status = RoboTextFontContextMetrics(font, &metrics, diag);
    if (status != RoboOk) {
        return status;
    }
    out->ascent = metrics.ascentPx;
    out->descent = metrics.descentPx;
    out->lineHeight = metrics.lineHeightPx;
    return RoboOk;
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

    {
        Result status = ShapeMeasure(result->runs, result->glyphRuns, result->glyphRunCount, font,
                                     &result->measurement, diag);

        if (status != RoboOk) {
            RoboTextShapeResultDestroy(result);
            return status;
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
        out->glyphRuns += stats.glyphRuns;
        out->glyphs += stats.glyphs;
        out->missing += stats.missing;
        out->multiScalarSources += stats.multiScalarSources;
        out->positioned += stats.positioned;
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
