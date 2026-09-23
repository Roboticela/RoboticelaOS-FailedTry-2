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
 *   the **algorithms** GPOS, kerning, complex scripts. Ligatures and GSUB arrived in 12.10 and 12.12; each
 *                      one flips a flag here in the same change. 12.8 is where the split 12.7 announced
 *                      closed: `directionRuns`/`paragraphDirection` are still the *classification*, and
 *                      `bidiAlgorithm` with its six companions below is the *resolution* — levels, level
 *                      runs, L2's visual order, isolates, L4's mirroring and the dump, each one a flag
 *                      and each one a case in the suite.
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
 * paragraph context and the direction-run resolver; 12.8 the bidirectional stage — the paragraph base, the
 * levels, the level runs, L2's visual order, L4's mirroring and the paragraph dump. The run/glyph record
 * shapes gained a level field rather than changing shape, so the major stays and the minor moves.
 *
 * 12.10 added the glyph-substitution engine (`RoboTextGsub.h`: the feature plan, the lookup readers and the
 * shaping buffer), 12.12 the ligature layer (`RoboTextLigature.h`: the five-tag policy, the stylesheet's
 * variant vocabulary and the glyph-run report), 12.11 the positioning engine (`RoboTextGpos.h`: the value
 * records, the pair adjustments in both formats, the extension wrapper and the per-glyph adjustments) and
 * 12.13 the kerning layer (`RoboTextKerning.h`: the `kern` policy, the source resolution and the per-glyph
 * report). All four are additive — the glyph record grew the font's own part of its two vectors and kept its
 * shape — so the minor moves once for each and the major stays. */
#define RoboTextVersionMinor 7
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

    /**
     * One cmap lookup per scalar, with **no** substitution anywhere. **0** since Phase 12.10.
     *
     * It was 1 through 12.9, when the generator was the placeholder that phase named. What 12.10 changed is
     * that a face with a GSUB record for the run's script now substitutes, so the claim "there is no
     * substitution pass" is no longer true of any text — the generators are `RoboTextGsub.c` and
     * `RoboTextGsubSubst.c`, and the mode a run reports says which of the two paths produced it.
     */
    int glyphGenerationIsPlaceholder;
    /** Variable-axis application (`wght`/`opsz` beyond face selection). **0**: a later goal. */
    int variableAxisApplication;
    /**
     * GSUB / glyph substitution. **1** since Phase 12.10: the table is parsed once per face, the feature plan
     * is resolved from the face's own ScriptList/LangSys/FeatureList, and all eight lookup types run, the
     * extension wrapper included.
     */
    int substitution;
    /**
     * GPOS / glyph positioning. **1** since Phase 12.11: the face's table is parsed once, the feature plan is
     * resolved from the face's own ScriptList/LangSys/FeatureList with positioning's defaults, and the single,
     * pair (both formats) and extension lookups are read into per-glyph adjustments.
     *
     * What this does *not* claim: cursive attachment, the three mark attachments and the two contextual
     * lookups are recognised and **skipped** — `RoboTextGposStats::lookupsUnsupported` counts them — so a face
     * whose marks ride on GPOS attachment is positioned by its advances alone.
     */
    int positioning;
    /**
     * Kerning. **1** since Phase 12.13: the `kern` feature is a caller-visible policy
     * (`RoboTextKerningPolicy`), a run's kerning is resolved against the face and reported
     * (`RoboTextKerningResolution`), and the adjustments the font made are readable per glyph
     * (`RoboTextKerningReport`).
     *
     * What this does *not* claim: the legacy `kern` table is **not read** — the policy's fallback names the
     * shape it will take (`RoboTextKerningLegacyIfNoGposKern`) and the resolution says whether the face even
     * has one (`legacyTableAvailable`), which is the honest record until Phase 5 parses it. Optical kerning,
     * tracking and letter-spacing are not here either.
     */
    int kerning;
    /** The `kern` feature as a policy: on by default, off on request, converted into a feature set. */
    int kerningPolicy;
    /** Kerning diagnostics: what applies and from where, and the adjustments a run received. */
    int kerningReport;
    /**
     * Ligatures. **1** since Phase 12.12: many-to-one substitution through the font's own `liga`/`clig`/
     * `rlig`/`dlig`/`hlig` lookups, with the source range and component count of every product kept.
     *
     * What this does *not* claim: no ligature is synthesized, no `fi` table lives in this module, and a face
     * that ships no such feature gets no substitution — the font is the only authority (Phase 12.10 §14).
     */
    int ligatures;
    /** The five-tag ligature policy and the stylesheet's variant vocabulary over it (Phase 12.12). */
    int ligaturePolicy;
    /** A ligature glyph's provenance: the scalars it stands for and how many components produced it. */
    int ligatureProvenance;
    /**
     * The Unicode bidirectional algorithm, including reordering. **1** since Phase 12.8.
     *
     * The *classification* is 12.7's (`directionRuns`); this is the resolution: UAX #9's levels, runs and
     * visual order, consumed from RoboUnicode's own implementation. Nothing is reordered in the source
     * text — the visual order is an index array beside the runs.
     */
    int bidiAlgorithm;

    /* --- Phase 12.8: the bidirectional stage --- */

    /** A paragraph's base direction and level, resolved from a declaration or from P2/P3 (P3's default
     *  included, and reported as such rather than as a silent LTR). */
    int bidiParagraphs;
    /** A resolved level per scalar, with X9's removals marked as "no level" rather than level 0. */
    int bidiLevels;
    /** L2's visual order, over the level runs and over the visible scalars, with the inverse mapping a
     *  hit test needs. */
    int bidiVisualOrder;
    /** Directional isolates (`LRI`/`RLI`/`FSI`/`PDI`) and the explicit embeddings, resolved by the
     *  algorithm this stage consumes. */
    int bidiIsolates;
    /** L4's mirrored glyph selection, from the UCD's `Bidi_Mirroring_Glyph`, without touching the text. */
    int bidiMirroring;
    /** A paragraph can be written as a table: base, level runs in both index spaces, visual order and the
     *  per-scalar class-and-level lines. */
    int bidiDiagnostics;
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
