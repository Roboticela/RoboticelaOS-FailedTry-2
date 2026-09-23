/*
 * The shaping-conformance case model (Phase 12.18) — a machine-readable expectation, the assertions that
 * check it, and the failure text that makes a regression readable.
 *
 * ## Correctness is several answers, not one picture
 *
 * The phase's §2 is the reason this header exists. "The screenshot looks roughly right" is not a definition
 * of correct shaping: a visually similar result with wrong clusters is still wrong, and correct glyph ids
 * with wrong GPOS placement are still wrong. So a case states what it can check at each layer —
 *
 *     glyph count, glyph sequence, clusters, advances, total width, ligature provenance, the run structure
 *     (script and direction), and the font the run was shaped with
 *
 * — and each field it leaves unset is a claim it does not make. `-1` for a count and `NULL` for a list are
 * "not checked", which is how a glyph-only case and a position-only case live in one type without one of
 * them asserting the other's silence as agreement.
 *
 * ## Why the assertions are not macros
 *
 * A failed conformance case has to print the input, the font, the features and both answers, because the
 * reader's next question is always "which of those was wrong". A `RoboCheck(a == b)` prints a line number;
 * `ConfExpectGlyphs` prints
 *
 *     Shaping conformance failure: latin.ligature.ffi
 *       text:     "office"
 *       glyph ids
 *         expected: 32 513 35 34 33 37
 *         actual:   32 32 35 35 34 33 37
 *
 * which is the difference the phase's §42 asks for. The framework is still `Tests/Framework/RoboTest.h` —
 * this is a fixture, like every other suite's own support header, not a second harness.
 *
 * ## Tolerances
 *
 * A position that has been through `font units → device pixels` is never compared with `==`. The phase's §11
 * asks for one named tolerance and this header has exactly one: `ConfPositionTolerance`, a quarter of a
 * device pixel at 1x, which is below anything a reader could see and above every rounding chain this build
 * has. Exact equality is used only for integers — ids, counts, cluster bounds, source ranges — where it is
 * the right question.
 */

#ifndef ROBO_TEXT_CONFORMANCE_SUPPORT_H
#define ROBO_TEXT_CONFORMANCE_SUPPORT_H

#include "RoboTextConformanceFont.h"
#include "RoboTest.h"
#include "RoboText/RoboText.h"

#include <stdio.h>
#include <string.h>

/**
 * The one floating-point tolerance the conformance suites use, in device pixels.
 *
 * A font-unit value scaled to device pixels is exact when the scale is a power of two and carries a rounding
 * error otherwise; a quarter pixel is far above that error and far below a wrong advance, which at 16px is
 * whole pixels. Contrast the two failures the suites must tell apart: `advance` moved by the wrong kern pair
 * is 1.28px, and a sum rounded per glyph instead of once is under 0.01px.
 */
#define ConfPositionTolerance 0.25f

/** The tolerance for a total width, where the accumulation is in `double` and the error is smaller still. */
#define ConfAdvanceTolerance 0.05f

/* ------------------------------------------------------------------ */
/* Reading a result regardless of how many runs it is                   */
/* ------------------------------------------------------------------ */

/** Glyphs across every run of @p result, in logical order. */
static inline size_t ConfTotalGlyphs(const RoboTextShapeResult *result)
{
    size_t total = 0u;

    for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
        total += RoboTextGlyphRunCount(RoboTextShapeResultGlyphRun(result, run));
    }
    return total;
}

/** The @p index'th glyph across every run, or NULL when @p index is past the last. */
static inline const RoboTextGlyph *ConfGlyphAt(const RoboTextShapeResult *result, size_t index)
{
    size_t seen = 0u;

    for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
        const RoboTextGlyphRun *glyphRun = RoboTextShapeResultGlyphRun(result, run);
        size_t count = RoboTextGlyphRunCount(glyphRun);

        if (index < seen + count) {
            return RoboTextGlyphRunGlyph(glyphRun, (RoboTextGlyphIndex)(index - seen));
        }
        seen += count;
    }
    return NULL;
}

/** The total advance across every run, in device pixels. */
static inline float ConfTotalAdvance(const RoboTextShapeResult *result)
{
    double total = 0.0;

    for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
        total += (double)RoboTextGlyphRunAdvance(RoboTextShapeResultGlyphRun(result, run));
    }
    return (float)total;
}

/** Glyphs whose id came from a substitution, across every run. */
static inline size_t ConfTotalSubstituted(const RoboTextShapeResult *result)
{
    size_t total = 0u;

    for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
        total += RoboTextGlyphRunSubstituted(RoboTextShapeResultGlyphRun(result, run));
    }
    return total;
}

/** Glyphs a ligature produced, across every run. */
static inline size_t ConfTotalLigatures(const RoboTextShapeResult *result)
{
    size_t total = 0u;

    for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
        total += RoboTextGlyphRunLigatures(RoboTextShapeResultGlyphRun(result, run));
    }
    return total;
}

/** Glyphs the face's GPOS positioned, across every run. */
static inline size_t ConfTotalPositioned(const RoboTextShapeResult *result)
{
    size_t total = 0u;

    for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
        total += RoboTextGlyphRunPositioned(RoboTextShapeResultGlyphRun(result, run));
    }
    return total;
}

/** Scalars the face had no glyph for, across every run. */
static inline size_t ConfTotalMissing(const RoboTextShapeResult *result)
{
    size_t total = 0u;

    for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
        total += RoboTextGlyphRunMissingGlyphs(RoboTextShapeResultGlyphRun(result, run));
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* Shaping                                                             */
/* ------------------------------------------------------------------ */

/** A view over a NUL-terminated string. */
static inline RoboTextView ConfView(const char *text)
{
    return RoboTextViewMake(text, strlen(text));
}

/** Shape @p text with @p options over @p face, asserting success and that a result came back. */
static inline RoboTextShapeResult *ConfShape(const ConfFace *face,
                                             const char *text,
                                             const RoboTextShapeOptions *options)
{
    RoboTextShapeResult *result = NULL;

    RoboCheckEqInt((int)RoboTextShape(ConfView(text), &face->font, options, &result, NULL), (int)RoboOk);
    RoboCheckMsg(result != NULL, "shaping produced no result");
    return result;
}

/** Shape @p text with the defaults of @p face. */
static inline RoboTextShapeResult *ConfShapeDefault(const ConfFace *face, const char *text)
{
    RoboTextShapeOptions options;

    RoboTextShapeOptionsInit(&options);
    options.baseDirection = RoboTextDirectionLtr;
    return ConfShape(face, text, &options);
}

/* ------------------------------------------------------------------ */
/* Failure diagnostics                                                 */
/* ------------------------------------------------------------------ */

/**
 * Report one conformance failure with the input, the thing being compared and both answers.
 *
 * Printed rather than asserted away because the phase's §42 asks for it: a conformance failure a reader
 * cannot act on is a conformance failure that gets muted.
 */
static inline void ConfReport(const char *label,
                              const char *text,
                              const char *what,
                              const char *expected,
                              const char *actual)
{
    fprintf(stdout,
            "\nShaping conformance failure: %s\n"
            "  text:     \"%s\"\n"
            "  %s\n"
            "    expected: %s\n"
            "    actual:   %s\n",
            label ? label : "(unnamed case)", text ? text : "", what,
            expected ? expected : "(none)", actual ? actual : "(none)");
    fflush(stdout);
}

/** @p ids as `"32 513 35"`, into @p buffer. */
static inline const char *ConfIdsText(const uint16_t *ids, size_t count, char *buffer, size_t capacity)
{
    size_t used = 0u;

    if (capacity == 0u) {
        return "";
    }
    buffer[0] = '\0';
    for (size_t i = 0u; i < count; i++) {
        int written = snprintf(buffer + used, capacity - used, "%s%u", (i == 0u) ? "" : " ", (unsigned)ids[i]);

        if (written < 0 || (size_t)written >= capacity - used) {
            break;
        }
        used += (size_t)written;
    }
    return buffer;
}

/** The result's glyph ids as text, into @p buffer. */
static inline const char *ConfActualGlyphs(const RoboTextShapeResult *result, char *buffer, size_t capacity)
{
    size_t used = 0u;
    size_t index = 0u;

    if (capacity == 0u) {
        return "";
    }
    buffer[0] = '\0';
    for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
        const RoboTextGlyphRun *glyphRun = RoboTextShapeResultGlyphRun(result, run);

        for (size_t g = 0u; g < RoboTextGlyphRunCount(glyphRun); g++) {
            const RoboTextGlyph *glyph = RoboTextGlyphRunGlyph(glyphRun, (RoboTextGlyphIndex)g);
            int written = snprintf(buffer + used, capacity - used, "%s%u", (index == 0u) ? "" : " ",
                                   (unsigned)glyph->glyphId);

            if (written < 0 || (size_t)written >= capacity - used) {
                return buffer;
            }
            used += (size_t)written;
            index++;
        }
    }
    return buffer;
}

/* ------------------------------------------------------------------ */
/* The assertions                                                      */
/* ------------------------------------------------------------------ */

/** Assert that @p result's glyph ids are exactly @p ids, naming the case and the text on failure. */
static inline void ConfExpectGlyphs(const RoboTextShapeResult *result,
                                    const uint16_t *ids,
                                    size_t count,
                                    const char *label,
                                    const char *text)
{
    char expected[512];
    char actual[512];

    if (ConfTotalGlyphs(result) != count) {
        ConfReport(label, text, "glyph count",
                   (snprintf(expected, sizeof(expected), "%u", (unsigned)count), expected),
                   (snprintf(actual, sizeof(actual), "%u", (unsigned)ConfTotalGlyphs(result)), actual));
        RoboCheckMsg(0, "glyph count differs");
        return;
    }
    for (size_t i = 0u; i < count; i++) {
        const RoboTextGlyph *glyph = ConfGlyphAt(result, i);

        if (!glyph || glyph->glyphId != ids[i]) {
            ConfReport(label, text, "glyph ids", ConfIdsText(ids, count, expected, sizeof(expected)),
                       ConfActualGlyphs(result, actual, sizeof(actual)));
            RoboCheckMsg(0, "glyph sequence differs");
            return;
        }
    }
}

/** Assert the glyph count, without naming the ids — the assertion a "which font answered" case wants. */
static inline void ConfExpectGlyphCount(const RoboTextShapeResult *result,
                                        size_t count,
                                        const char *label,
                                        const char *text)
{
    char expected[64];
    char actual[64];

    if (ConfTotalGlyphs(result) != count) {
        snprintf(expected, sizeof(expected), "%u", (unsigned)count);
        snprintf(actual, sizeof(actual), "%u", (unsigned)ConfTotalGlyphs(result));
        ConfReport(label, text, "glyph count", expected, actual);
        RoboCheckMsg(0, "glyph count differs");
    }
}

/**
 * Assert @p run's clusters: their source spans and their glyph counts, in order.
 *
 * The phase's §6 and §38. `clusters` is a flat list of `(sourceStart, sourceEnd, glyphCount)` triples, which
 * keeps the expectation readable in a table and makes "one glyph covering three scalars" as easy to state as
 * "one glyph covering one".
 */
static inline void ConfExpectClusters(const RoboTextShapeResult *result,
                                      size_t runIndex,
                                      const uint32_t *clusters,
                                      size_t count,
                                      const char *label,
                                      const char *text)
{
    const RoboTextGlyphRun *run = RoboTextShapeResultGlyphRun(result, runIndex);

    if (!run) {
        ConfReport(label, text, "cluster count", "a run", "no run at that index");
        RoboCheckMsg(0, "no run to read clusters from");
        return;
    }
    if (RoboTextGlyphRunClusterCount(run) != count) {
        char expected[64];
        char actual[64];

        snprintf(expected, sizeof(expected), "%u", (unsigned)count);
        snprintf(actual, sizeof(actual), "%u", (unsigned)RoboTextGlyphRunClusterCount(run));
        ConfReport(label, text, "cluster count", expected, actual);
        RoboCheckMsg(0, "cluster count differs");
        return;
    }
    for (size_t i = 0u; i < count; i++) {
        const RoboTextGlyphCluster *cluster =
            RoboTextGlyphRunCluster(run, (RoboTextClusterIndex)i);
        uint32_t wantStart = clusters[i * 3u];
        uint32_t wantEnd = clusters[i * 3u + 1u];
        uint32_t wantGlyphs = clusters[i * 3u + 2u];

        if (!cluster) {
            RoboCheckMsg(0, "a cluster is missing");
            return;
        }
        if (cluster->source.start != wantStart || cluster->source.end != wantEnd ||
            (uint32_t)(cluster->glyphEnd - cluster->glyphStart) != wantGlyphs) {
            char expected[256];
            char actual[256];

            snprintf(expected, sizeof(expected), "[%u..%u) glyphs %u", (unsigned)wantStart,
                     (unsigned)wantEnd, (unsigned)wantGlyphs);
            snprintf(actual, sizeof(actual), "[%u..%u) glyphs %u", (unsigned)cluster->source.start,
                     (unsigned)cluster->source.end,
                     (unsigned)(cluster->glyphEnd - cluster->glyphStart));
            ConfReport(label, text, "cluster mapping", expected, actual);
            RoboCheckMsg(0, "cluster mapping differs");
            return;
        }
    }
}

/**
 * Assert @p actual is @p expected within @ref ConfPositionTolerance, naming what was compared.
 *
 * The one place a floating-point comparison happens. A value that has been through `font units → device
 * pixels` is never compared with `==`; the tolerance is stated once and every case shares it.
 */
static inline void ConfExpectFloatClose(float actual,
                                       float expected,
                                       const char *label,
                                       const char *what)
{
    float delta = actual - expected;
    char expectedText[64];
    char actualText[64];

    if (delta < 0.0f) {
        delta = -delta;
    }
    if (delta > ConfPositionTolerance) {
        snprintf(expectedText, sizeof(expectedText), "%.4f", (double)expected);
        snprintf(actualText, sizeof(actualText), "%.4f", (double)actual);
        ConfReport(label, what, "value", expectedText, actualText);
        RoboCheckMsg(0, "value differs by more than the stated tolerance");
    }
}

/** Assert the total advance, within @ref ConfAdvanceTolerance. */
static inline void ConfExpectAdvance(const RoboTextShapeResult *result,
                                     float expected,
                                     const char *label,
                                     const char *text)
{
    float actual = ConfTotalAdvance(result);
    float delta = actual - expected;

    if (delta < 0.0f) {
        delta = -delta;
    }
    if (delta > ConfAdvanceTolerance) {
        char expectedText[64];
        char actualText[64];

        snprintf(expectedText, sizeof(expectedText), "%.4f", (double)expected);
        snprintf(actualText, sizeof(actualText), "%.4f", (double)actual);
        ConfReport(label, text, "total advance", expectedText, actualText);
        RoboCheckMsg(0, "advance differs");
    }
}

/** Assert that @p index's glyph is a ligature of @p components scalars. */
static inline void ConfExpectLigature(const RoboTextShapeResult *result,
                                      size_t index,
                                      uint16_t components,
                                      const char *label,
                                      const char *text)
{
    const RoboTextGlyph *glyph = ConfGlyphAt(result, index);

    if (!glyph || glyph->ligatureComponents != components) {
        char expected[64];
        char actual[64];

        snprintf(expected, sizeof(expected), "ligature of %u", (unsigned)components);
        snprintf(actual, sizeof(actual), "%s",
                 glyph ? (glyph->ligatureComponents ? "a different ligature" : "not a ligature")
                       : "no glyph");
        ConfReport(label, text, "ligature provenance", expected, actual);
        RoboCheckMsg(0, "ligature provenance differs");
    }
}

/** Assert the number of glyph runs, which is the *run structure* a case about scripts or fonts states. */
static inline void ConfExpectRunCount(const RoboTextShapeResult *result,
                                      size_t count,
                                      const char *label,
                                      const char *text)
{
    if (RoboTextShapeResultGlyphRunCount(result) != count) {
        char expected[64];
        char actual[64];

        snprintf(expected, sizeof(expected), "%u", (unsigned)count);
        snprintf(actual, sizeof(actual), "%u", (unsigned)RoboTextShapeResultGlyphRunCount(result));
        ConfReport(label, text, "glyph run count", expected, actual);
        RoboCheckMsg(0, "glyph run count differs");
    }
}

/** Assert @p run's script and direction, the two classifications a shaping case must not skip (§27, §28). */
static inline void ConfExpectRunClassification(const RoboTextShapeResult *result,
                                               size_t runIndex,
                                               RoboTextScript script,
                                               RoboTextDirection direction,
                                               const char *label,
                                               const char *text)
{
    const RoboTextGlyphRun *run = RoboTextShapeResultGlyphRun(result, runIndex);

    if (!run) {
        RoboCheckMsg(0, "no run at that index");
        return;
    }
    if (RoboTextGlyphRunScript(run) != script) {
        ConfReport(label, text, "run script", RoboTextScriptName(script),
                   RoboTextScriptName(RoboTextGlyphRunScript(run)));
        RoboCheckMsg(0, "run script differs");
    }
    if (RoboTextGlyphRunDirection(run) != direction) {
        ConfReport(label, text, "run direction", RoboTextDirectionName(direction),
                   RoboTextDirectionName(RoboTextGlyphRunDirection(run)));
        RoboCheckMsg(0, "run direction differs");
    }
}

/** Assert every run held by @p result is a valid record, which is the §39 determinism precondition. */
static inline void ConfExpectValid(const RoboTextShapeResult *result, const char *label)
{
    for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(result); run++) {
        RoboTextGlyphRunProblem problem = RoboTextGlyphRunProblemNone;

        RoboCheckEqInt((int)RoboTextGlyphRunValidate(RoboTextShapeResultGlyphRun(result, run), &problem, NULL),
                       (int)RoboOk);
        if (problem != RoboTextGlyphRunProblemNone) {
            ConfReport(label, "", "run validation", "a valid run", RoboTextGlyphRunProblemName(problem));
        }
    }
}

/** Field-for-field equality of two shape results' glyph runs: ids, sources, clusters, advances, offsets. */
static inline int ConfResultsEqual(const RoboTextShapeResult *a, const RoboTextShapeResult *b)
{
    if (RoboTextShapeResultGlyphRunCount(a) != RoboTextShapeResultGlyphRunCount(b)) {
        return 0;
    }
    for (size_t run = 0u; run < RoboTextShapeResultGlyphRunCount(a); run++) {
        const RoboTextGlyphRun *ra = RoboTextShapeResultGlyphRun(a, run);
        const RoboTextGlyphRun *rb = RoboTextShapeResultGlyphRun(b, run);

        if (RoboTextGlyphRunCount(ra) != RoboTextGlyphRunCount(rb) ||
            RoboTextGlyphRunClusterCount(ra) != RoboTextGlyphRunClusterCount(rb) ||
            RoboTextGlyphRunAdvance(ra) != RoboTextGlyphRunAdvance(rb)) {
            return 0;
        }
        for (size_t g = 0u; g < RoboTextGlyphRunCount(ra); g++) {
            const RoboTextGlyph *ga = RoboTextGlyphRunGlyph(ra, (RoboTextGlyphIndex)g);
            const RoboTextGlyph *gb = RoboTextGlyphRunGlyph(rb, (RoboTextGlyphIndex)g);

            if (ga->glyphId != gb->glyphId || ga->advanceX != gb->advanceX ||
                ga->offsetX != gb->offsetX || ga->offsetY != gb->offsetY ||
                ga->cluster != gb->cluster || !RoboTextRangeEqual(&ga->source, &gb->source)) {
                return 0;
            }
        }
    }
    return 1;
}

#endif /* ROBO_TEXT_CONFORMANCE_SUPPORT_H */
