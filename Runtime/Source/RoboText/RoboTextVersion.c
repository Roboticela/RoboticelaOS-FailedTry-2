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
    static const char version[] = "1.3.0";

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
     * The generator, and it is the placeholder: one cmap lookup per scalar, in logical order, with no
     * substitution. True on purpose — the flag exists so this fact is *queryable* rather than only
     * written in a comment, and every goal that replaces it flips it.
     */
    out->glyphGenerationIsPlaceholder = 1;

    /*
     * The algorithms, all absent, and each one is a row in the phase plan rather than an omission:
     * substitution is 12.10, positioning 12.11, kerning 12.13, ligatures 12.12, bidi 12.8, complex
     * scripts 12.19, vertical text 12.20. Variable-axis application is unowned by a numbered goal and
     * is reported absent until RoboFont interpolates outlines (`FontOutlineVariationApplied` is 0).
     */
    out->variableAxisApplication = 0;
    out->substitution = 0;
    out->positioning = 0;
    out->kerning = 0;
    out->ligatures = 0;
    out->bidiAlgorithm = 0;
    out->complexScripts = 0;
    out->verticalText = 0;
    return RoboOk;
}
