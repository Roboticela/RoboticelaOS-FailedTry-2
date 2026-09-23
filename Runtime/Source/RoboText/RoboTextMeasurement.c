/*
 * RoboText's measurement engine (Phases 12.4, 12.15) — see RoboTextMeasurement.h.
 *
 * The file is the engine, in the order it reads a shaped text:
 *
 *     line metrics  →  advances and counts  →  logical bounds  →  ink bounds
 *
 * and three decisions are worth reading in code rather than only in the header.
 *
 * **The width is the runs' own totals, accumulated in `double`.** A run's total is already the sum of its
 * glyphs' `float` device advances in `double`, so adding the totals adds no second rounding and the answer is
 * the sum of the glyphs' advances *themselves* — which is what the painter's pen accumulates. Summing the
 * rounded per-run `float`s instead would put one rounding per run into the width, and a width that disagrees
 * with the pen by a fraction of a pixel is exactly the disagreement 12.4 removed.
 *
 * **The line metrics are the maximum over the participating runs, and one function computes them.** A mixed
 * font line is as tall as its tallest run; the alternative — the first run's metrics — would make a line's
 * height depend on which side of it the fallback happened to fall, which is a layout that changes when text is
 * edited somewhere else on the line. `RoboTextMeasureGlyphRunListLineMetrics` is the same function the engine
 * calls, so the record and the three fields cannot disagree.
 *
 * **Ink bounds skip glyphs whose face has no box, and say so.** The alternative — using the advance box for a
 * glyph whose outline the face cannot report — would answer with a rectangle that paints nothing, and a caller
 * clipping to it would clip to a guess. `outComplete` is the flag that turns "mostly right" into a fact a
 * caller can act on.
 */

#include "RoboText/RoboTextMeasurement.h"

#include "RoboMem/RoboMem.h"
#include "RoboTextGlyphsInternal.h"
#include "RoboTextInternal.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Finiteness, without math.h                                          */
/* ------------------------------------------------------------------ */

/**
 * @p value is a number paint may receive: Phase 12.15.33's "no NaN or infinity".
 *
 * `v == v` and a magnitude bound rather than `isfinite`, because this file is compiled into the guest program
 * and the freestanding target has no `math.h` — the same check `RoboTextGlyphs.c`, `RoboGraphicsText.c` and
 * `RoboFontStyle.c` each make for the same reason.
 */
static int ValueIsFinite(float value)
{
    return (value == value) && (value <= 3.402823466e+38f) && (value >= -3.402823466e+38f);
}

/* ------------------------------------------------------------------ */
/* Rectangles                                                          */
/* ------------------------------------------------------------------ */

void RoboTextMeasureRectInit(RoboTextMeasureRect *rect)
{
    if (!rect) {
        return;
    }
    rect->x = 0.0f;
    rect->y = 0.0f;
    rect->width = 0.0f;
    rect->height = 0.0f;
}

int RoboTextMeasureRectIsEmpty(const RoboTextMeasureRect *rect)
{
    return !rect || !(rect->width > 0.0f) || !(rect->height > 0.0f);
}

void RoboTextMeasureRectUnion(RoboTextMeasureRect *rect, const RoboTextMeasureRect *other)
{
    float minX;
    float minY;
    float maxX;
    float maxY;
    float otherMinX;
    float otherMinY;
    float otherMaxX;
    float otherMaxY;

    if (!rect || RoboTextMeasureRectIsEmpty(other)) {
        return;
    }
    /* An empty accumulator adopts rather than unions: `RoboTextMeasureRectInit` leaves the origin at zero and
     * a union against it would drag every box back to the baseline's origin. */
    if (RoboTextMeasureRectIsEmpty(rect)) {
        *rect = *other;
        return;
    }
    minX = rect->x;
    minY = rect->y;
    maxX = rect->x + rect->width;
    maxY = rect->y + rect->height;
    otherMinX = other->x;
    otherMinY = other->y;
    otherMaxX = other->x + other->width;
    otherMaxY = other->y + other->height;
    if (otherMinX < minX) {
        minX = otherMinX;
    }
    if (otherMinY < minY) {
        minY = otherMinY;
    }
    if (otherMaxX > maxX) {
        maxX = otherMaxX;
    }
    if (otherMaxY > maxY) {
        maxY = otherMaxY;
    }
    rect->x = minX;
    rect->y = minY;
    rect->width = maxX - minX;
    rect->height = maxY - minY;
}

void RoboTextMeasureRectUnionLtrb(RoboTextMeasureRect *rect,
                                  float minX,
                                  float minY,
                                  float maxX,
                                  float maxY)
{
    RoboTextMeasureRect box;

    if (!rect) {
        return;
    }
    if (!(maxX >= minX) || !(maxY >= minY)) {
        /* An inverted pair is an empty box, not a rectangle with a negative extent: a glyph with no outline
         * contributes nothing to an ink union, and letting the negative extent through would corrupt the
         * accumulator. */
        return;
    }
    box.x = minX;
    box.y = minY;
    box.width = maxX - minX;
    box.height = maxY - minY;
    RoboTextMeasureRectUnion(rect, &box);
}

float RoboTextMeasureRectWidth(const RoboTextMeasureRect *rect)
{
    return rect && rect->width > 0.0f ? rect->width : 0.0f;
}

float RoboTextMeasureRectHeight(const RoboTextMeasureRect *rect)
{
    return rect && rect->height > 0.0f ? rect->height : 0.0f;
}

int RoboTextMeasureRectEqual(const RoboTextMeasureRect *a, const RoboTextMeasureRect *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

/* ------------------------------------------------------------------ */
/* Line metrics                                                        */
/* ------------------------------------------------------------------ */

void RoboTextMeasureLineMetricsInit(RoboTextMeasureLineMetrics *metrics)
{
    if (!metrics) {
        return;
    }
    memset(metrics, 0, sizeof(*metrics));
}

int RoboTextMeasureLineMetricsEqual(const RoboTextMeasureLineMetrics *a,
                                    const RoboTextMeasureLineMetrics *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->ascent == b->ascent && a->descent == b->descent && a->lineGap == b->lineGap &&
           a->height == b->height && a->lineHeight == b->lineHeight && a->runCount == b->runCount &&
           a->fromFace == b->fromFace;
}

/**
 * The one implementation of the line-metrics policy (Phase 12.15.15): the maximum over the participating runs,
 * or the metrics font's answer for a text with no runs at all.
 *
 * `RoboTextMeasureGlyphRunList` fills its three fields from this and
 * `RoboTextMeasureGlyphRunListLineMetrics` publishes the record; neither computes a number of its own, which is
 * what keeps "the measurement's ascent" and "the line metrics' ascent" the same fact.
 */
static Result MeasureLineMetricsOf(const RoboTextGlyphRunList *list,
                                   const RoboTextFontContext *metricsFont,
                                   RoboTextMeasureLineMetrics *out,
                                   RoboTextDiag *diag)
{
    size_t count;

    RoboTextMeasureLineMetricsInit(out);
    count = list ? RoboTextGlyphRunListCount(list) : 0u;
    for (size_t index = 0u; index < count; index++) {
        const RoboTextGlyphRun *run = RoboTextGlyphRunListRun(list, index);
        RoboTextFontMetrics metrics;

        if (RoboTextGlyphRunMetrics(run, &metrics) != RoboOk) {
            return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboError,
                                   "the run's metrics could not be read");
        }
        if (metrics.ascentPx > out->ascent) {
            out->ascent = metrics.ascentPx;
        }
        if (metrics.descentPx > out->descent) {
            out->descent = metrics.descentPx;
        }
        if (metrics.lineGapPx > out->lineGap) {
            out->lineGap = metrics.lineGapPx;
        }
        if (metrics.lineHeightPx > out->lineHeight) {
            out->lineHeight = metrics.lineHeightPx;
        }
        if (metrics.fromFace) {
            out->fromFace = 1;
        }
        out->runCount++;
    }

    if (count == 0u) {
        /*
         * A text with no runs still has a line, and the answer comes from the context the caller measured
         * against (Phase 12.15.17). This is the only place the metrics font is consulted: a list with a run
         * carries the face it was set in, and asking a font again would be the second reading the header
         * refuses.
         */
        if (metricsFont) {
            RoboTextFontMetrics metrics;

            if (RoboTextFontContextMetrics(metricsFont, &metrics, diag) == RoboOk) {
                out->ascent = metrics.ascentPx;
                out->descent = metrics.descentPx;
                out->lineGap = metrics.lineGapPx;
                out->lineHeight = metrics.lineHeightPx;
                out->fromFace = metrics.fromFace;
            }
        }
    }

    out->height = out->ascent + out->descent + out->lineGap;
    if (!(out->lineHeight > 0.0f)) {
        /* A run whose face could not be asked reports a zero line height. Falling back to the typographic sum
         * keeps a line visible rather than collapsing it, which is the same rule `RoboTextFontContextMetrics`
         * states one layer down for a face with no line metrics. */
        out->lineHeight = out->height;
    }
    return RoboOk;
}

Result RoboTextMeasureGlyphRunListLineMetrics(const RoboTextGlyphRunList *list,
                                              const RoboTextFontContext *metricsFont,
                                              RoboTextMeasureLineMetrics *out,
                                              RoboTextDiag *diag)
{
    if (!list || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a glyph run list and an out record are required");
    }
    return MeasureLineMetricsOf(list, metricsFont, out, diag);
}

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

int RoboTextMeasureOptionIsKnown(RoboTextMeasureOption option)
{
    return option == RoboTextMeasureOptionNone || option == RoboTextMeasureOptionLogicalBounds ||
           option == RoboTextMeasureOptionInkBounds;
}

const char *RoboTextMeasureOptionName(RoboTextMeasureOption option)
{
    switch (option) {
    case RoboTextMeasureOptionNone:
        return "none";
    case RoboTextMeasureOptionLogicalBounds:
        return "logical-bounds";
    case RoboTextMeasureOptionInkBounds:
        return "ink-bounds";
    default:
        return "unknown";
    }
}

Result RoboTextMeasureOptionsWrite(RoboTextMeasureOptions options,
                                   char *buffer,
                                   size_t capacity,
                                   size_t *outLength)
{
    char line[96];
    size_t length = 0u;
    int first = 1;

    if (outLength) {
        *outLength = 0u;
    }
    if (!buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';
    if (options == RoboTextMeasureOptionNone) {
        length = RoboTextWriterText(line, sizeof(line), length, "none");
    }
    for (int bit = 0; bit <= 1 && length != RoboTextWriterOverflow; bit++) {
        RoboTextMeasureOptions mask =
            bit == 0 ? (RoboTextMeasureOptions)RoboTextMeasureOptionLogicalBounds
                     : (RoboTextMeasureOptions)RoboTextMeasureOptionInkBounds;

        if ((options & mask) == 0u) {
            continue;
        }
        if (!first) {
            length = RoboTextWriterText(line, sizeof(line), length, "|");
        }
        length = RoboTextWriterText(line, sizeof(line), length,
                                    RoboTextMeasureOptionName((RoboTextMeasureOption)mask));
        first = 0;
    }
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

void RoboTextMeasureRequestInit(RoboTextMeasureRequest *request)
{
    if (!request) {
        return;
    }
    request->options = RoboTextMeasureOptionNone;
}

void RoboTextMeasureRequestInitFull(RoboTextMeasureRequest *request)
{
    if (!request) {
        return;
    }
    request->options = RoboTextMeasureOptionsAll;
}

/* ------------------------------------------------------------------ */
/* The measurement record                                              */
/* ------------------------------------------------------------------ */

void RoboTextMeasurementInit(RoboTextMeasurement *measurement)
{
    if (!measurement) {
        return;
    }
    memset(measurement, 0, sizeof(*measurement));
    RoboTextMeasureRectInit(&measurement->inkBounds);
    RoboTextMeasureRectInit(&measurement->logicalBounds);
    RoboTextMeasureLineMetricsInit(&measurement->lineMetrics);
    /* Stated rather than left implicit: a measurement's y is from its baseline, so the baseline is zero. */
    measurement->baseline = 0.0f;
}

int RoboTextMeasurementEqual(const RoboTextMeasurement *a, const RoboTextMeasurement *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return a->width == b->width && a->advanceY == b->advanceY && a->ascent == b->ascent &&
           a->descent == b->descent && a->lineGap == b->lineGap && a->lineHeight == b->lineHeight &&
           a->baseline == b->baseline && a->glyphCount == b->glyphCount &&
           a->glyphRunCount == b->glyphRunCount && a->scalarCount == b->scalarCount &&
           a->missingGlyphs == b->missingGlyphs && a->advanceMax == b->advanceMax &&
           RoboTextMeasureRectEqual(&a->inkBounds, &b->inkBounds) &&
           a->hasInkBounds == b->hasInkBounds &&
           RoboTextMeasureRectEqual(&a->logicalBounds, &b->logicalBounds) &&
           a->hasLogicalBounds == b->hasLogicalBounds &&
           RoboTextMeasureLineMetricsEqual(&a->lineMetrics, &b->lineMetrics);
}

/**
 * The one-line summary, and it stays one line.
 *
 * Deliberately **not** extended with the bounds: this door's callers are a stable comparison and a
 * `192`-byte buffer convention, and a line that grows whenever the record does is a line that eventually
 * refuses a buffer a caller has always had. The bounded record has its own block — @ref RoboTextMeasureWrite —
 * which is multi-line because it has more to say.
 */
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
    length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)measurement->scalarCount);
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
/* Ink bounds                                                          */
/* ------------------------------------------------------------------ */

/**
 * The ink of one glyph, in the run's own device space and relative to the run's origin.
 *
 * The font's box is in font units relative to the glyph's own origin (that is what `xMin`/`lsb` mean in `glyf`),
 * so scaling it and adding the pen position and the glyph's **placement** offset is the whole translation:
 *
 *     ink = glyphBbox * (size / upem) + offset + pen
 *
 * The placement offset is added rather than assumed zero because GPOS marks move a glyph's ink without moving
 * its advance (Phase 12.11), and an ink box that ignored that would clip every attached mark.
 *
 * @return RoboOk; RoboError when the face's metrics will not parse; RoboNotSupported when the run has no face.
 */
static Result GlyphInkBox(const RoboTextGlyphRun *run,
                          const RoboTextGlyph *glyph,
                          float penX,
                          RoboTextMeasureRect *out,
                          int *outHasBox,
                          RoboTextDiag *diag)
{
    const RoboTextFontContext *context = RoboTextGlyphRunFont(run);
    FontGlyphMetrics metrics;
    float xMin;
    float yMin;
    float xMax;
    float yMax;
    Result status;

    RoboTextMeasureRectInit(out);
    *outHasBox = 0;
    if (!context || !context->face) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboNotSupported,
                               "a glyph's ink needs the face it is set in");
    }
    if (glyph->control) {
        /* A bidi formatting control is not a glyph: no id, no advance, nothing drawn. It has no ink, and that
         * is a fact rather than an unavailable box — so it does not make the result incomplete. */
        *outHasBox = 1;
        return RoboOk;
    }
    memset(&metrics, 0, sizeof(metrics));
    status = RoboTextFontGlyphMetrics(context, glyph->glyphId, &metrics, NULL, diag);
    if (status != RoboOk) {
        return status;
    }
    if (!metrics.hasBbox) {
        /*
         * No outline box: a CFF face, or a glyph with no outline data (`RoboFontMetrics.c` explains both).
         * Skipped rather than approximated with the advance box, and the caller reports the absence.
         */
        return RoboOk;
    }
    status = RoboTextFontUnitsToDevice(context, (double)metrics.bbox.xMin, &xMin, diag);
    if (status == RoboOk) {
        status = RoboTextFontUnitsToDevice(context, (double)metrics.bbox.yMin, &yMin, diag);
    }
    if (status == RoboOk) {
        status = RoboTextFontUnitsToDevice(context, (double)metrics.bbox.xMax, &xMax, diag);
    }
    if (status == RoboOk) {
        status = RoboTextFontUnitsToDevice(context, (double)metrics.bbox.yMax, &yMax, diag);
    }
    if (status != RoboOk) {
        return status;
    }
    RoboTextMeasureRectUnionLtrb(out, penX + glyph->offsetX + xMin, glyph->offsetY + yMin,
                                 penX + glyph->offsetX + xMax, glyph->offsetY + yMax);
    *outHasBox = 1;
    return RoboOk;
}

Result RoboTextMeasureGlyphRunListInkBounds(const RoboTextGlyphRunList *list,
                                            RoboTextMeasureRect *out,
                                            int *outComplete,
                                            RoboTextDiag *diag)
{
    size_t runCount;
    int complete = 1;

    if (outComplete) {
        *outComplete = 0;
    }
    if (!list || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a glyph run list and an out rect are required");
    }
    RoboTextMeasureRectInit(out);
    runCount = RoboTextGlyphRunListCount(list);
    for (size_t index = 0u; index < runCount; index++) {
        const RoboTextGlyphRun *run = RoboTextGlyphRunListRun(list, index);
        size_t glyphCount = RoboTextGlyphRunCount(run);
        double pen = 0.0;

        for (size_t glyphIndex = 0u; glyphIndex < glyphCount; glyphIndex++) {
            const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)glyphIndex);
            RoboTextMeasureRect box;
            int hasBox = 0;
            Result status =
                GlyphInkBox(run, glyph, (float)pen, &box, &hasBox, diag);

            if (status != RoboOk) {
                return status;
            }
            if (!hasBox) {
                complete = 0;
                /*
                 * The pen still moves: an unboxed glyph is skipped from the ink, not from the layout. This is
                 * what makes a partial answer honest rather than shifted — the glyphs after it sit where they
                 * paint.
                 */
                pen += (double)glyph->advanceX;
                continue;
            }
            RoboTextMeasureRectUnion(out, &box);
            pen += (double)glyph->advanceX;
        }
    }
    if (outComplete) {
        *outComplete = complete;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The engine                                                          */
/* ------------------------------------------------------------------ */

Result RoboTextMeasureGlyphRunList(const RoboTextGlyphRunList *list,
                                   const RoboTextFontContext *metricsFont,
                                   const RoboTextMeasureRequest *request,
                                   RoboTextMeasurement *out,
                                   RoboTextDiag *diag)
{
    RoboTextMeasureOptions options = RoboTextMeasureOptionNone;
    double width = 0.0;
    double advanceY = 0.0;
    size_t runCount;
    Result status;

    if (!list || !out) {
        if (out) {
            RoboTextMeasurementInit(out);
        }
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a glyph run list and an out measurement are required");
    }
    if (request) {
        options = request->options;
    }
    RoboTextMeasurementInit(out);

    runCount = RoboTextGlyphRunListCount(list);
    out->glyphRunCount = runCount;
    for (size_t index = 0u; index < runCount; index++) {
        const RoboTextGlyphRun *run = RoboTextGlyphRunListRun(list, index);
        size_t glyphCount = RoboTextGlyphRunCount(run);

        out->glyphCount += glyphCount;
        out->missingGlyphs += RoboTextGlyphRunMissingGlyphs(run);
        out->scalarCount = (RoboTextScalarIndex)(out->scalarCount + RoboTextGlyphRunScalarCount(run));
        /*
         * The run's own total, which is its glyphs' advances accumulated in `double`. Adding the totals rather
         * than the rounded per-run `float`s is what keeps the width the sum of the glyph advances themselves;
         * see the file header.
         */
        width += run->advanceTotal;
        advanceY += run->advanceTotalY;
        for (size_t glyphIndex = 0u; glyphIndex < glyphCount; glyphIndex++) {
            const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)glyphIndex);

            if (glyph->advanceX > out->advanceMax) {
                out->advanceMax = glyph->advanceX;
            }
        }
    }
    /* One rounding, here, of a sum accumulated in `double` — the property the header states. */
    out->width = (float)width;
    out->advanceY = (float)advanceY;
    out->baseline = 0.0f;

    status = MeasureLineMetricsOf(list, metricsFont, &out->lineMetrics, diag);
    if (status != RoboOk) {
        return status;
    }
    out->ascent = out->lineMetrics.ascent;
    out->descent = out->lineMetrics.descent;
    out->lineGap = out->lineMetrics.lineGap;
    out->lineHeight = out->lineMetrics.lineHeight;

    if ((options & (RoboTextMeasureOptions)RoboTextMeasureOptionLogicalBounds) != 0u) {
        /*
         * The box layout places: from the baseline's origin, `width` across and the line tall. `y` is the
         * **bottom** edge, so it is the descent below the baseline and the height reaches to the ascent.
         */
        out->logicalBounds.x = 0.0f;
        out->logicalBounds.y = -out->descent;
        out->logicalBounds.width = out->width;
        out->logicalBounds.height = out->ascent + out->descent;
        out->hasLogicalBounds = 1;
    }
    if ((options & (RoboTextMeasureOptions)RoboTextMeasureOptionInkBounds) != 0u) {
        int complete = 0;

        status = RoboTextMeasureGlyphRunListInkBounds(list, &out->inkBounds, &complete, diag);
        if (status != RoboOk) {
            return status;
        }
        /*
         * Reported only when the bounds are the glyphs' rather than a subset's **and** there was a glyph to
         * measure. A flags-only flag would let a caller clip to an empty rectangle believing it was the ink.
         */
        out->hasInkBounds = complete && out->glyphCount != 0u;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Tolerance                                                           */
/* ------------------------------------------------------------------ */

int RoboTextMeasureFloatEqual(float a, float b, float tolerance)
{
    float difference;

    if (!ValueIsFinite(a) || !ValueIsFinite(b)) {
        return a == b;
    }
    difference = a - b;
    if (difference < 0.0f) {
        difference = -difference;
    }
    return difference <= tolerance;
}

int RoboTextMeasurementApproxEqual(const RoboTextMeasurement *a,
                                   const RoboTextMeasurement *b,
                                   float tolerance)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return RoboTextMeasureFloatEqual(a->width, b->width, tolerance) &&
           RoboTextMeasureFloatEqual(a->advanceY, b->advanceY, tolerance) &&
           RoboTextMeasureFloatEqual(a->ascent, b->ascent, tolerance) &&
           RoboTextMeasureFloatEqual(a->descent, b->descent, tolerance) &&
           RoboTextMeasureFloatEqual(a->lineGap, b->lineGap, tolerance) &&
           RoboTextMeasureFloatEqual(a->lineHeight, b->lineHeight, tolerance) &&
           RoboTextMeasureFloatEqual(a->baseline, b->baseline, tolerance) &&
           RoboTextMeasureFloatEqual(a->advanceMax, b->advanceMax, tolerance) &&
           a->glyphCount == b->glyphCount && a->glyphRunCount == b->glyphRunCount &&
           a->scalarCount == b->scalarCount && a->missingGlyphs == b->missingGlyphs;
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

int RoboTextMeasureProblemIsKnown(RoboTextMeasureProblem problem)
{
    return problem >= RoboTextMeasureProblemNone && problem <= RoboTextMeasureProblemLogicalBounds;
}

const char *RoboTextMeasureProblemName(RoboTextMeasureProblem problem)
{
    switch (problem) {
    case RoboTextMeasureProblemNone:
        return "none";
    case RoboTextMeasureProblemNotFinite:
        return "not-finite";
    case RoboTextMeasureProblemWidth:
        return "width";
    case RoboTextMeasureProblemMetrics:
        return "metrics";
    case RoboTextMeasureProblemCounts:
        return "counts";
    case RoboTextMeasureProblemBounds:
        return "bounds";
    case RoboTextMeasureProblemLogicalBounds:
        return "logical-bounds";
    default:
        return "unknown";
    }
}

/** Both members of a rectangle are a number paint may receive. */
static int RectIsFinite(const RoboTextMeasureRect *rect)
{
    return ValueIsFinite(rect->x) && ValueIsFinite(rect->y) && ValueIsFinite(rect->width) &&
           ValueIsFinite(rect->height);
}

Result RoboTextMeasureValidate(const RoboTextMeasurement *measurement,
                               const RoboTextGlyphRunList *list,
                               RoboTextMeasureProblem *outProblem,
                               RoboTextDiag *diag)
{
    size_t runCount;
    size_t glyphs = 0u;
    double advanceSum = 0.0;

    if (outProblem) {
        *outProblem = RoboTextMeasureProblemNone;
    }
    if (!measurement || !list) {
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a measurement and the list it measured are required");
    }
    if (!ValueIsFinite(measurement->width) || !ValueIsFinite(measurement->advanceY) ||
        !ValueIsFinite(measurement->ascent) || !ValueIsFinite(measurement->descent) ||
        !ValueIsFinite(measurement->lineGap) || !ValueIsFinite(measurement->lineHeight) ||
        !ValueIsFinite(measurement->baseline) || !ValueIsFinite(measurement->advanceMax) ||
        !RectIsFinite(&measurement->inkBounds) || !RectIsFinite(&measurement->logicalBounds)) {
        if (outProblem) {
            *outProblem = RoboTextMeasureProblemNotFinite;
        }
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a measurement carries a value that is not a finite number");
    }
    if (measurement->ascent < 0.0f || measurement->descent < 0.0f || measurement->lineGap < 0.0f) {
        if (outProblem) {
            *outProblem = RoboTextMeasureProblemMetrics;
        }
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a line metric is negative");
    }
    if (measurement->baseline != 0.0f) {
        if (outProblem) {
            *outProblem = RoboTextMeasureProblemMetrics;
        }
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "a measurement's baseline is its own origin and must be zero");
    }

    runCount = RoboTextGlyphRunListCount(list);
    if (measurement->glyphRunCount != runCount) {
        if (outProblem) {
            *outProblem = RoboTextMeasureProblemCounts;
        }
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "the run count disagrees with the list");
    }
    for (size_t index = 0u; index < runCount; index++) {
        const RoboTextGlyphRun *run = RoboTextGlyphRunListRun(list, index);

        glyphs += RoboTextGlyphRunCount(run);
        advanceSum += run->advanceTotal;
    }
    if (measurement->glyphCount != glyphs) {
        if (outProblem) {
            *outProblem = RoboTextMeasureProblemCounts;
        }
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "the glyph count disagrees with the runs");
    }
    /*
     * Compared within the tolerance rather than exactly, because the two sums are legitimately different
     * arithmetic: this one adds the runs' `double` totals, and a second measurement of the same list adds them
     * in the same order, so they are equal in fact — the tolerance is here for the *comparison* a caller makes
     * between a measurement and a differently-derived width, and the door should not be stricter than the rule
     * it publishes.
     */
    if (!RoboTextMeasureFloatEqual(measurement->width, (float)advanceSum, RoboTextMeasureTolerancePx)) {
        if (outProblem) {
            *outProblem = RoboTextMeasureProblemWidth;
        }
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "the stored width is not the sum of the runs' own advances");
    }
    if (!RoboTextMeasureRectIsEmpty(&measurement->inkBounds) &&
        (!(measurement->inkBounds.width > 0.0f) || !(measurement->inkBounds.height > 0.0f))) {
        if (outProblem) {
            *outProblem = RoboTextMeasureProblemBounds;
        }
        return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                               "an ink rectangle has a negative extent");
    }
    if (measurement->hasLogicalBounds) {
        /*
         * The derivation is checked rather than trusted: the box is a function of the width and the two
         * metrics, and a stored one that disagreed would be a picture of a line that does not exist. This is
         * the one duplicated fact in the record, and it is duplicated *checked*.
         */
        if (!RoboTextMeasureFloatEqual(measurement->logicalBounds.x, 0.0f, RoboTextMeasureTolerancePx) ||
            !RoboTextMeasureFloatEqual(measurement->logicalBounds.y, -measurement->descent,
                                       RoboTextMeasureTolerancePx) ||
            !RoboTextMeasureFloatEqual(measurement->logicalBounds.width, measurement->width,
                                       RoboTextMeasureTolerancePx) ||
            !RoboTextMeasureFloatEqual(measurement->logicalBounds.height,
                                       measurement->ascent + measurement->descent,
                                       RoboTextMeasureTolerancePx)) {
            if (outProblem) {
                *outProblem = RoboTextMeasureProblemLogicalBounds;
            }
            return RoboTextDiagMsg(diag, RoboTextDomainFont, RoboInvalidArgument,
                                   "the logical bounds disagree with the width and metrics they are derived from");
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

Result RoboTextMeasureWrite(const RoboTextMeasurement *measurement,
                            const RoboTextGlyphRunList *list,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength)
{
    size_t length = 0u;
    size_t runCount;
    RoboTextMeasureProblem problem = RoboTextMeasureProblemNone;

    if (outLength) {
        *outLength = 0u;
    }
    if (!measurement || !list || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';

    /*
     * Validity first, and only when it fails: a block that prints is a block a reader trusts, so a record that
     * breaks its own invariants says so on the first line. `RoboTextMeasureValidate` is the same door a test
     * calls, so the mark on the block and the failure in a suite cannot disagree about what "invalid" means.
     */
    if (RoboTextMeasureValidate(measurement, list, &problem, NULL) != RoboOk) {
        length = RoboTextWriterText(buffer, capacity, length, "INVALID ");
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterText(buffer, capacity, length, RoboTextMeasureProblemName(problem));
        }
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterText(buffer, capacity, length, "\n");
        }
        if (length == RoboTextWriterOverflow) {
            buffer[0] = '\0';
            return RoboNotSupported;
        }
    }

    length = RoboTextWriterText(buffer, capacity, length, "TextMeasurement {\n");
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "  width ");
        length = RoboTextWriterFloat(buffer, capacity, length, measurement->width);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "  advanceY ");
        length = RoboTextWriterFloat(buffer, capacity, length, measurement->advanceY);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "  baseline ");
        length = RoboTextWriterFloat(buffer, capacity, length, measurement->baseline);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "\n  ascent ");
        length = RoboTextWriterFloat(buffer, capacity, length, measurement->ascent);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "  descent ");
        length = RoboTextWriterFloat(buffer, capacity, length, measurement->descent);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "  lineGap ");
        length = RoboTextWriterFloat(buffer, capacity, length, measurement->lineGap);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "  lineHeight ");
        length = RoboTextWriterFloat(buffer, capacity, length, measurement->lineHeight);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "\n  glyphs ");
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)measurement->glyphCount);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "  runs ");
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)measurement->glyphRunCount);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "  scalars ");
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)measurement->scalarCount);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "  missing ");
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)measurement->missingGlyphs);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "  advanceMax ");
        length = RoboTextWriterFloat(buffer, capacity, length, measurement->advanceMax);
    }

    /* The two boxes, each on its own line and each saying whether it was computed at all — an all-zero
     * rectangle and an uncomputed one are the same value, and only the flag tells them apart. */
    for (int which = 0; which < 2 && length != RoboTextWriterOverflow; which++) {
        const RoboTextMeasureRect *rect =
            which == 0 ? &measurement->logicalBounds : &measurement->inkBounds;
        int computed = which == 0 ? measurement->hasLogicalBounds : measurement->hasInkBounds;

        length = RoboTextWriterText(buffer, capacity, length, which == 0 ? "\n  logicalBounds " : "\n  inkBounds ");
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterText(buffer, capacity, length, computed ? "[" : "absent [");
        }
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterFloat(buffer, capacity, length, rect->x);
            length = RoboTextWriterText(buffer, capacity, length, ", ");
            length = RoboTextWriterFloat(buffer, capacity, length, rect->y);
            length = RoboTextWriterText(buffer, capacity, length, ", ");
            length = RoboTextWriterFloat(buffer, capacity, length, rect->width);
            length = RoboTextWriterText(buffer, capacity, length, ", ");
            length = RoboTextWriterFloat(buffer, capacity, length, rect->height);
            length = RoboTextWriterText(buffer, capacity, length, "]");
        }
    }

    runCount = RoboTextGlyphRunListCount(list);
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "\n  runs ");
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)runCount);
    }
    for (size_t index = 0u; index < runCount && length != RoboTextWriterOverflow; index++) {
        const RoboTextGlyphRun *run = RoboTextGlyphRunListRun(list, index);
        RoboTextRange source = RoboTextGlyphRunSourceRange(run);
        size_t glyphCount = RoboTextGlyphRunCount(run);
        double pen = 0.0;

        length = RoboTextWriterText(buffer, capacity, length, "\n  run[");
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)index);
        length = RoboTextWriterText(buffer, capacity, length, "] source [");
        length = RoboTextWriterUnsigned(buffer, capacity, length, source.start);
        length = RoboTextWriterText(buffer, capacity, length, "..");
        length = RoboTextWriterUnsigned(buffer, capacity, length, source.end);
        length = RoboTextWriterText(buffer, capacity, length, ") script ");
        length = RoboTextWriterText(buffer, capacity, length, RoboTextScriptName(RoboTextGlyphRunScript(run)));
        length = RoboTextWriterText(buffer, capacity, length, " direction ");
        length = RoboTextWriterText(buffer, capacity, length,
                                    RoboTextDirectionName(RoboTextGlyphRunDirection(run)));
        length = RoboTextWriterText(buffer, capacity, length, " level ");
        length = RoboTextWriterSigned(buffer, capacity, length,
                                      (int32_t)RoboTextGlyphRunBidiLevel(run));
        length = RoboTextWriterText(buffer, capacity, length, " advance ");
        length = RoboTextWriterFloat(buffer, capacity, length, RoboTextGlyphRunAdvance(run));
        length = RoboTextWriterText(buffer, capacity, length, " ascent ");
        length = RoboTextWriterFloat(buffer, capacity, length, RoboTextGlyphRunAscent(run));
        length = RoboTextWriterText(buffer, capacity, length, " descent ");
        length = RoboTextWriterFloat(buffer, capacity, length, RoboTextGlyphRunDescent(run));
        length = RoboTextWriterText(buffer, capacity, length, " glyphs ");
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)glyphCount);
        length = RoboTextWriterText(buffer, capacity, length, " clusters ");
        length = RoboTextWriterUnsigned(buffer, capacity, length,
                                        (uint32_t)RoboTextGlyphRunClusterCount(run));
        length = RoboTextWriterText(buffer, capacity, length, " missing ");
        length = RoboTextWriterUnsigned(buffer, capacity, length,
                                        (uint32_t)RoboTextGlyphRunMissingGlyphs(run));

        /*
         * Then a line per glyph, and this is the part `RoboTextGlyphRunWrite` cannot supply: the **pen
         * position** each glyph is drawn at and the **ink box** it contributes. A width that is a pixel out is
         * either a kerned pair, an unmapped glyph's `.notdef` advance or an overhanging italic, and the two
         * numbers that tell those apart are exactly these.
         */
        for (size_t glyphIndex = 0u; glyphIndex < glyphCount && length != RoboTextWriterOverflow;
             glyphIndex++) {
            const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(run, (RoboTextGlyphIndex)glyphIndex);
            RoboTextMeasureRect ink;
            int hasInk = 0;

            /* No diagnostic sink here: this is a dump, and a face that will not answer for one glyph's box
             * makes the block absent rather than half-written — the same complete-or-absent rule the rest of
             * the dump follows. The status is not lost: `RoboTextMeasureGlyphRunListInkBounds` reports it. */
            if (GlyphInkBox(run, glyph, (float)pen, &ink, &hasInk, NULL) != RoboOk) {
                buffer[0] = '\0';
                return RoboNotSupported;
            }
            length = RoboTextWriterText(buffer, capacity, length, "\n    glyph[");
            length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)glyphIndex);
            length = RoboTextWriterText(buffer, capacity, length, "] pen ");
            length = RoboTextWriterFloat(buffer, capacity, length, (float)pen);
            length = RoboTextWriterText(buffer, capacity, length, " id ");
            length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)glyph->glyphId);
            length = RoboTextWriterText(buffer, capacity, length, " advance ");
            length = RoboTextWriterFloat(buffer, capacity, length, glyph->advanceX);
            length = RoboTextWriterText(buffer, capacity, length, " offset ");
            length = RoboTextWriterFloat(buffer, capacity, length, glyph->offsetX);
            length = RoboTextWriterText(buffer, capacity, length, ",");
            length = RoboTextWriterFloat(buffer, capacity, length, glyph->offsetY);
            length = RoboTextWriterText(buffer, capacity, length, " cluster ");
            length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)glyph->cluster);
            length = RoboTextWriterText(buffer, capacity,
                                        length, hasInk && !RoboTextMeasureRectIsEmpty(&ink) ? " ink ["
                                                                                            : " ink none");
            if (hasInk && !RoboTextMeasureRectIsEmpty(&ink)) {
                length = RoboTextWriterFloat(buffer, capacity, length, ink.x);
                length = RoboTextWriterText(buffer, capacity, length, ", ");
                length = RoboTextWriterFloat(buffer, capacity, length, ink.y);
                length = RoboTextWriterText(buffer, capacity, length, ", ");
                length = RoboTextWriterFloat(buffer, capacity, length, ink.x + ink.width);
                length = RoboTextWriterText(buffer, capacity, length, ", ");
                length = RoboTextWriterFloat(buffer, capacity, length, ink.y + ink.height);
                length = RoboTextWriterText(buffer, capacity, length, "]");
            }
            pen += (double)glyph->advanceX;
        }
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "\n}");
    }

    if (length == RoboTextWriterOverflow) {
        buffer[0] = '\0';
        return RoboNotSupported;
    }
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}
