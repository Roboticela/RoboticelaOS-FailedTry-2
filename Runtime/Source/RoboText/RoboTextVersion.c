/*
 * RoboText's version and capabilities (Phase 12.4) — see RoboTextVersion.h.
 *
 * One table. The capability record is filled field by field rather than from a bitmask because the
 * fields are heterogeneous (some are "this exists", some are "this deliberately does not") and because a
 * reader asking "does this build shape?" should get an `int` they can print next to a `documentation`
 * claim, not a bit they have to look up.
 */

#include "RoboText/RoboTextVersion.h"

#include <string.h>

const char *RoboTextVersionString(void)
{
    static const char version[] = "1.11.0";

    return version;
}

Result RoboTextGetVersion(int32_t *outMajor, int32_t *outMinor, int32_t *outPatch)
{
    if (!outMajor || !outMinor || !outPatch) {
        return RoboInvalidArgument;
    }
    *outMajor = RoboTextVersionMajor;
    *outMinor = RoboTextVersionMinor;
    *outPatch = RoboTextVersionPatch;
    return RoboOk;
}

void RoboTextCapabilitiesInit(RoboTextCapabilities *capabilities)
{
    if (!capabilities) {
        return;
    }
    memset(capabilities, 0, sizeof(*capabilities));
}

Result RoboTextGetCapabilities(RoboTextCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }

    /* The model: everything 12.4 establishes. */
    out->buffer = 1;
    out->indexing = 1;
    out->ranges = 1;
    out->fontContext = 1;
    out->scriptRuns = 1;
    out->directionRuns = 1;
    out->textRuns = 1;
    out->glyphs = 1;
    out->glyphRuns = 1;
    out->sourceMapping = 1;
    out->measurement = 1;
    out->shapeResult = 1;

    /*
     * The Unicode/font integration (12.5). Each flag is exercised by a case: the decoder and the property
     * doors by `text.unicode.*`, the resolver by `text.unicode.scripts`, coverage and selection by
     * `text.fontselection.*`, and the runs and the shape request by `text.analysis.*`.
     */
    out->unicodeIntegration = 1;
    out->codePoints = 1;
    out->combiningMarks = 1;
    out->scriptResolver = 1;
    out->fontCoverage = 1;
    out->fontSelection = 1;
    out->coverageCache = 1;
    out->analysisRuns = 1;
    out->shapeRequest = 1;

    /*
     * The script stage (12.6). The extension property is exercised by `text.script.extensions`, the two-sided
     * resolution by the neutrality and combining-mark cases, and the dump by the diagnostics cases.
     */
    out->scriptExtensions = 1;
    out->contextualScriptResolution = 1;
    out->scriptRunDiagnostics = 1;

    /*
     * The direction stage (12.7). `paragraphDirection` is P2/P3 and stops there — the flag beside it,
     * `bidiAlgorithm`, is 0 below and stays 0 until 12.8 resolves levels. `directionClasses` is the `EN`
     * versus `ES` distinction the phase asks to be *represented*, exercised by `text.direction.classes`.
     */
    out->paragraphDirection = 1;
    out->directionContext = 1;
    out->directionClasses = 1;
    out->directionRunDiagnostics = 1;

    /*
     * The generator is no longer the placeholder: since 12.10 a face with a GSUB record for the run's script
     * substitutes, and the pass that does it is `RoboTextGlyphs.c`'s GSUB step over `RoboTextGsub.c`. The
     * flag stays in the record and answers 0, which is what "the placeholder was replaced" looks like to a
     * caller that read it as 1 in 12.9.
     */
    out->glyphGenerationIsPlaceholder = 0;

    /*
     * Glyph substitution (12.10), ligatures (12.12), positioning (12.11) and kerning (12.13). `substitution`
     * is exercised by every `text.gsub.*` case — the plan, the eight lookup types, the flags, the limits and
     * the malformed table — and the three ligature flags by `text.ligature.*`: the policy and its variants,
     * the provenance of a product, and the report.
     *
     * `positioning` is exercised by `text.gpos.*` — the two pair formats, the value records, the device
     * corrections, the extension wrapper, the ignored-glyph walk and the shipped pack's own kerning reaching
     * the pen — and `kerning` by `text.kern.*`, which is the *policy* and the *report* over that same engine
     * rather than a second one. The two ks flags are what a caller reads to know that `kern` is a feature it
     * can state and that the font's adjustments are readable per glyph.
     */
    out->substitution = 1;
    out->ligatures = 1;
    out->ligaturePolicy = 1;
    out->ligatureProvenance = 1;
    out->positioning = 1;
    out->kerning = 1;
    out->kerningPolicy = 1;
    out->kerningReport = 1;

    /*
     * The finalized run and its list (12.14). One flag per record the phase adds, read field by field in
     * `Tests/Text/SuiteRoboTextConformance.c`, and one flag that is deliberately **0**: `shapingCache`.
     *
     * The key such a cache would be built on is here (`RoboTextGlyphRunKey`, compared and *diagnosed* by
     * `RoboTextGlyphRunKeyDifference`), and the cache is not — because the phase's own rule is that a cache is
     * an optimization and never a correctness requirement, and nothing in this tree has yet measured a shaping
     * workload where one pays. A false flag here would be the one thing the capability record must not be.
     */
    out->glyphRunFinalization = 1;
    out->glyphClusters = 1;
    out->glyphFlags = 1;
    out->glyphRunMetrics = 1;
    out->glyphRunList = 1;
    out->glyphRunKeys = 1;
    out->glyphRunCoalescing = 1;
    out->glyphRunValidation = 1;
    out->glyphRunDiagnostics = 1;
    out->shapingCache = 0;

    /*
     * Measurement (12.15). `measurementEngine` is the one that matters: the record existed since 12.4, and what
     * this phase changed is that the record is now computed by a door over the finalized run list rather than by
     * a second walk inside the shape call. Every flag is exercised by a case: the engine by
     * `text.measure.advances`, the aggregation by `text.measure.mixed_font`, the ink box by
     * `text.measure.ink_bounds`, the two-box distinction by `text.measure.mark_ink`, the validation by
     * `text.measure.validation` and the block by `text.measure.write`.
     */
    out->measurementEngine = 1;
    out->lineMetricsPolicy = 1;
    out->inkBounds = 1;
    out->measurementBounds = 1;
    out->measurementValidation = 1;
    out->measurementDiagnostics = 1;
    out->measurementTolerance = 1;

    /*
     * The bidi stage (12.8). Every flag is exercised by a case: the paragraph base and its four sources by
     * `text.bidi.paragraph`, the levels by `text.bidi.levels`, the level runs and L2 by
     * `text.bidi.visual_order`, the isolates by `text.bidi.isolates`, the mirroring by
     * `text.bidi.mirroring`, and the dump by `text.bidi.diagnostics`. `bidiAlgorithm` is the flag 12.7
     * declared absent and 12.8 flips; the classification flags above it are unchanged.
     */
    out->bidiAlgorithm = 1;
    out->bidiParagraphs = 1;
    out->bidiLevels = 1;
    out->bidiVisualOrder = 1;
    out->bidiIsolates = 1;
    out->bidiMirroring = 1;
    out->bidiDiagnostics = 1;

    /*
     * Complex scripts (12.19). One flag per record the phase adds, and the *pair* is the honest reading: the
     * architecture is here — a strategy table, stages with masks, reorder steps as data — and the scripts this
     * build actually shapes are the two whose suites carry a case per script: Arabic and Devanagari.
     *
     * Exercised case by case: the strategies and the plan by `text.complex.*`, Arabic's forms and its clusters
     * by `text.arabic.*`, Devanagari's syllables, base, reph and masks by `text.indic.*`, the masks themselves
     * by `text.complex.masks` and `text.indic.masks`, the attachments by `text.gpos.cursive` and
     * `text.gpos.mark_*`, and the reorder steps by `text.indic.reorder_*`.
     *
     * What `complexScripts == 1` does **not** mean: that every script §2 lists is shaped. Sindhi's and
     * Kurdish's enlargements of the Arabic alphabet are not in RoboUnicode's joining table (12.19 §9 names
     * that as a table change), the Indic scripts other than Devanagari carry their virama and nukta but no
     * positional categories (so nothing of theirs is reordered), and the Southeast Asian family resolves to a
     * strategy with no stages. Each of those is stated in `Documentation/Runtime/Text/ComplexScripts.md` in the
     * same words.
     */
    out->complexScripts = 1;
    out->complexScriptStrategies = 1;
    out->complexClusters = 1;
    out->positionalFeatures = 1;
    out->gposAttachments = 1;
    out->reordering = 1;
    out->complexDiagnostics = 1;

    /*
     * Vertical writing (12.20). The axis is data, the orientation is a class and a rotation, the metrics are
     * the face's `vmtx` (synthesised, and counted, when it has none), and the OpenType integration is two
     * feature tags. There is deliberately no `verticalShaper` flag: the phase adds no engine, and a flag
     * claiming one would be the over-claim this record exists to avoid.
     */
    out->verticalText = 1;
    out->writingAxes = 1;
    out->verticalOrientation = 1;
    out->verticalMetrics = 1;
    out->verticalFeatures = 1;
    out->verticalDiagnostics = 1;

    /*
     * Variable-axis application is unowned by a numbered goal and is reported absent until RoboFont
     * interpolates outlines (`FontOutlineVariationApplied` is 0).
     */
    out->variableAxisApplication = 0;
    return RoboOk;
}
