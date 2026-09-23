/*
 * RoboText version and capabilities (Phase 12.4).
 *
 * The two doors every module in this runtime has — a version string and a **capability record** —
 * and for this module the second one is unusually load-bearing, because 12.4's whole point is the
 * *architecture* rather than the algorithm. A reader must be able to ask the build what its text
 * layer actually does, and get a truthful answer that separates three things that are easy to
 * conflate:
 *
 *   the **model**      text, ranges, runs, fonts, glyphs, mapping. All present.
 *   the **generator**  how a glyph id is obtained from a scalar: one cmap lookup per scalar, with
 *                      no substitution. Present, and *named as the placeholder it is*.
 *   the **algorithms** GSUB, GPOS, kerning, ligatures, bidi reordering, complex scripts. Absent, and
 *                      each goal that adds one flips a flag here in the same change — with 12.7's
 *                      `directionRuns`/`paragraphDirection` and 12.8's `bidiAlgorithm` the split that
 *                      matters most: the *classification* is here and the *resolution* is not.
 *
 * `RoboTextGlyphGenerationIsPlaceholder` is the flag that matters most this phase, and it is written
 * the way the phase's boundary demands: the cmap-per-scalar generator is **isolated and labelled**,
 * not extended indefinitely and not pretended to be shaping.
 */
#ifndef ROBO_TEXT_VERSION_H
#define ROBO_TEXT_VERSION_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Major version: changes only when the run/glyph API shape does. */
#define RoboTextVersionMajor 1
/* 12.5 added the Unicode doors, the font selector and the analysis runs; 12.6 the script-extension facts,
 * the contextual script resolver and its diagnostics; 12.7 the direction stage — the class projection, the
 * paragraph context and the direction-run resolver. The run/glyph record shapes are unchanged throughout,
 * so the major stays and the minor moves. */
#define RoboTextVersionMinor 3
#define RoboTextVersionPatch 0

/** Version as `"1.0.0"`; never NULL. */
const char *RoboTextVersionString(void);

/** Version components. RoboInvalidArgument for a NULL out pointer. */
Result RoboTextGetVersion(int32_t *outMajor, int32_t *outMinor, int32_t *outPatch);

/**
 * What this build's text layer does and does not do.
 *
 * Read positionally or by name, never by count.
 */
typedef struct RoboTextCapabilities {
    /** The canonical text model: an owned UTF-8 buffer with scalar random access. */
    int buffer;
    /** Explicit units: byte offsets and scalar indices are distinct types, and one converts. */
    int indexing;
    /** Half-open scalar ranges, with the containment algebra a caller needs. */
    int ranges;
    /** A font context: a borrowed face plus the numbers a run is set at. */
    int fontContext;
    /** Script segmentation, through RoboUnicode's own tables. */
    int scriptRuns;
    /**
     * Direction *classification* runs (not the bidi algorithm — see `bidiAlgorithm`).
     *
     * Since 12.7 these are the runs of `RoboTextDirection.h`: a `Bidi_Class` projection, a paragraph context
     * and the neutral/number rules, with the level and the order still 12.8's.
     */
    int directionRuns;
    /** Text runs: the intersection of script and direction, carrying the font context. */
    int textRuns;
    /** The glyph record: an id, its source scalars, its advance and its offset. */
    int glyphs;
    /** Glyph runs: an owned glyph array with its font, script, direction and source span. */
    int glyphRuns;
    /** Source mapping: glyph → scalar range, and scalar → the glyph that covers it. */
    int sourceMapping;
    /** Measurement from the glyph run, so a measurer and a painter share one advance. */
    int measurement;
    /** A shape result owning every run of one text, with the measurement beside them. */
    int shapeResult;

    /* --- Phase 12.5: Unicode and font integration --- */

    /** RoboText consumes RoboUnicode's decoder and property tables; it keeps no Unicode data of its own. */
    int unicodeIntegration;
    /** An explicit code-point vocabulary with validation (scalar vs surrogate vs out of range). */
    int codePoints;
    /** Combining-mark classification, and the rule that a mark keeps its base's font. */
    int combiningMarks;
    /** The shared script resolver, including the Common/Inherited neutrality rule. */
    int scriptResolver;
    /** Font coverage queries through the face, cached by RoboFont's own lazy coverage build. */
    int fontCoverage;
    /** Deterministic font selection over an ordered fallback chain, coverage-gated. */
    int fontSelection;
    /** The selector's coverage memo. */
    int coverageCache;
    /** Analysis runs: one script, one classified direction and one font over one range. */
    int analysisRuns;
    /** A fully populated shape request (real range, script, direction, font and language). */
    int shapeRequest;

    /* --- Phase 12.6: script runs --- */

    /** `Script_Extensions` is read as its own property, beside `Script` (UAX #24). */
    int scriptExtensions;
    /** Common/Inherited scalars are resolved against **both** neighbours, not just the one on the left. */
    int contextualScriptResolution;
    /** A script run can be written as a line, and a whole run set as a table in both index spaces. */
    int scriptRunDiagnostics;

    /* --- Phase 12.7: direction runs --- */

    /**
     * A paragraph direction, resolved from a declared mode or from the text's first strong character.
     *
     * P2/P3 and nothing after them: the *levels* P2/P3 feed are `bidiAlgorithm`'s, and it is **0**.
     */
    int paragraphDirection;
    /** An explicit direction context (declared mode plus fallback) that a caller supplies per span. */
    int directionContext;
    /**
     * A scalar's `Bidi_Class` projected to a direction class — strong, number, attaching mark or neutral —
     * with the `EN`/`AN` digits kept apart from the `ES`/`ET`/`CS` separators. **This is what makes "a
     * digit is not left-to-right" a fact about the code rather than a claim in a comment.**
     */
    int directionClasses;
    /** A direction run can be written as a line, and a whole run set as a table in both index spaces. */
    int directionRunDiagnostics;

    /** One cmap lookup per scalar (no substitution). **1**. */
    int glyphGenerationIsPlaceholder;
    /** Variable-axis application (`wght`/`opsz` beyond face selection). **0**: a later goal. */
    int variableAxisApplication;
    /** GSUB / glyph substitution. **0**: Phase 12.10. */
    int substitution;
    /** GPOS / glyph positioning. **0**: Phase 12.11. */
    int positioning;
    /** Kerning, including the `kern` table. **0**: Phase 12.13. */
    int kerning;
    /** Ligatures. **0**: Phase 12.12. */
    int ligatures;
    /** The Unicode bidirectional algorithm, including reordering. **0**: Phase 12.8. */
    int bidiAlgorithm;
    /** Complex-script shaping. **0**: Phase 12.19. */
    int complexScripts;
    /** Vertical text. **0**: Phase 12.20. */
    int verticalText;
} RoboTextCapabilities;

/** Zero @p capabilities (every feature reported absent). Safe on NULL. */
void RoboTextCapabilitiesInit(RoboTextCapabilities *capabilities);

/** Fill @p out with this build's capabilities. RoboInvalidArgument for a NULL @p out. */
Result RoboTextGetCapabilities(RoboTextCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_VERSION_H */
