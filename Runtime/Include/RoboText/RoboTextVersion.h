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
 *                      each goal that adds one flips a flag here in the same change.
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
/* 12.5 added the Unicode doors, the font selector and the analysis runs; the run/glyph record shapes are
 * unchanged, so the major stays and the minor moves. */
#define RoboTextVersionMinor 1
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
    /** Direction *classification* runs (not the bidi algorithm — see `bidiAlgorithm`). */
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
    /** True when glyph ids come from one cmap lookup per scalar (no substitution). **1**. */
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
