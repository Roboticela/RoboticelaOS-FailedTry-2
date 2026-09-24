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
 * shape — so the minor moves once for each and the major stays.
 *
 * 12.14 finalized the run (`RoboTextGlyphs.h`: the cluster array, the per-glyph flag word, the run's own
 * metrics and its totals) and added the list over runs (`RoboTextRunList.h`: the key, the two orders, the
 * merge and the validation). Additive once more — the glyph record gained a `codePoint` field, the run's own
 * struct gained the four derived records, and every existing door kept its signature — so the minor moves
 * again and the major stays.
 *
 * 12.15 moved the measurement record into its own header (`RoboTextMeasurement.h`) and added the engine over a
 * finalized run list: the rect, the request and its options, the line-metrics policy, the tolerance, the
 * validation and the diagnostic block. Additive for every door — the record grew fields and no existing
 * signature changed — with one *architectural* consequence worth the minor: the result's own measurement is now
 * this engine over the result's own list rather than a second walk in `RoboTextShape.c`.
 *
 * 12.19 is the first change to this module that is additive in its files and **not** additive in its behaviour:
 * a new public contract (`RoboTextComplex.h`'s strategy/plan/clusters, `RoboTextArabic.h`'s forms,
 * `RoboTextIndic.h`'s rules) sits on top of engines that gained abilities rather than callers — GPOS runs
 * lookup types 3–6, GSUB and GPOS both honour a strategy's positional mask, and `ShapeRun` walks a plan's
 * stages. The glyph record's shape is unchanged, so the minor moves once and the major stays. */
#define RoboTextVersionMinor 11
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
    /* --- Phase 12.14: glyph runs --- */

    /**
     * A run **finalized**: the records its glyph array implies — clusters, per-glyph flags, the face's metrics
     * and its own totals — derived once at the end of generation and carried, rather than recomputed by each
     * consumer.
     *
     * Distinct from @ref glyphRuns, which has been true since 12.4: that is the run *type* (an owned glyph
     * array with its font, script, direction and source span), and this is the derived half of the record.
     */
    int glyphRunFinalization;
    /**
     * Clusters: the many-to-many mapping between scalars and glyphs, in **logical** source order.
     *
     * What this does *not* claim: grapheme segmentation. The rule is "a combining mark continues the cluster of
     * the glyph before it", which is correct for the scripts this build shapes and is *named* as the
     * approximation it is (UAX #29 is not implemented).
     */
    int glyphClusters;
    /** The per-glyph flag word: cluster start/continuation, mark, ligature and the two unsafe-to-boundary
     *  facts a later line breaker needs. */
    int glyphFlags;
    /** A run's own metrics, so that line layout never asks the font parser again. */
    int glyphRunMetrics;
    /** The run list: logical order as the list, the visual order L2 resolved beside it, and the two inverse
     *  doors a painter and a caret use. */
    int glyphRunList;
    /** A run's key and the *field* that differs, which is what makes splitting and merging deterministic
     *  rather than two comparisons someone has to keep in step. */
    int glyphRunKeys;
    /** Two adjacent runs provably one shape run, merged by concatenation and re-finalization. */
    int glyphRunCoalescing;
    /** The run and list invariants, checked by a door a test calls (`RoboTextGlyphRunValidate`). */
    int glyphRunValidation;
    /** A run and a list as a diagnostic block, with the invalid case marked rather than printed silently. */
    int glyphRunDiagnostics;
    /**
     * A shaping cache. **0**, and deliberately so: the *key* a cache would use is here
     * (`RoboTextGlyphRunKey`), the cache is not, because nothing has measured a case where it pays. The
     * phase's own rule is that caching must never be required for correctness (Phase 12.14.33), so an absent
     * cache is a capability this build reports honestly rather than one it pretends to have.
     */
    int shapingCache;
    /**
     * Complex-script shaping: a script's shaping behaviour as a **plan** (a strategy, stages with positional
     * masks, and the reorder steps between them), with Arabic's joining and Devanagari's syllables built on it.
     *
     * `1` since 12.19, and the honest reading of it is the pair below: the *architecture* is here — a second
     * complex script is a row in a table, not a second engine — while the scripts this build actually shapes
     * are named by @ref complexScriptLanguages. A caller that reads only this flag would think every script
     * 12.19 lists is shaped; the flags beside it say which are.
     */
    int complexScripts;
    /**
     * The **strategies** this build has a shaper for: `None`, `Arabic` (contextual forms, ligatures, marks,
     * cursive attachment where the face states it) and `Indic` (syllables, a base, the reph, pre-base vowel
     * signs, per-position feature masks).
     *
     * `SoutheastAsian` (Thai, Khmer, Myanmar) resolves to a strategy with **no stages**: those scripts shape
     * through the ordinary path, and reporting them as shaped would be the over-claim 12.19's own §23 warns
     * against. The strategy exists so the gap is a named value rather than a script that looks like Latin.
     */
    int complexScriptStrategies;
    /**
     * Complex clusters: a stretch of source that must shape — and break — as one unit, with a base inside it,
     * the flags a reorder needs, and the forward half of the safe-boundary rule.
     */
    int complexClusters;
    /**
     * Positional feature masks: a strategy says *where* a feature applies (`init` here, `half` there), and both
     * engines ask the question per position rather than applying a feature to a whole run.
     */
    int positionalFeatures;
    /**
     * The GPOS **attachments**: cursive attachment, mark-to-base, mark-to-ligature and mark-to-mark (lookup
     * types 3–6), which place a glyph relative to another glyph's anchor.
     *
     * They need the face's advance for the pen distance, so a call that supplies no font context counts them
     * as unsupported rather than attaching to a distance nobody knows.
     */
    int gposAttachments;
    /** Reordering: the shaping buffer is moved between GSUB stages, by glyph and never by text index, so every
     *  glyph keeps the source range it came from. */
    int reordering;
    /** A complex plan as a diagnostic block, and a cluster table through the same writer the run tables use. */
    int complexDiagnostics;
    /**
     * Vertical text: `RoboTextWritingMode` is a first-class setting on the shape options and on every run,
     * and a vertical run positions its glyphs on the y axis through the **same** `RoboTextGlyphRun` the
     * horizontal pipeline produces. Set by 12.20.
     *
     * What this does *not* claim: a second text engine. The placement is one helper beside the `hmtx` line
     * in `RoboTextGlyphs.c`, the vertical substitutions are 12.10's GSUB, and the vertical positioning is
     * 12.11's GPOS. A caller reading this flag is reading "the axis is data", not "there is a vertical
     * shaper" — the flags below name the parts.
     */
    int verticalText;
    /**
     * The writing mode's **axes** as an explicit inline/block pair (`RoboTextWritingAxes`), so a layout or a
     * measurement pass converts "the pen moved 12px" to a physical vector through one door rather than
     * writing the x/y swap itself. The two vertical modes differ in their **block** axis, which a boolean
     * could not state.
     */
    int writingAxes;
    /**
     * Per-glyph orientation: `RoboTextGlyphOrientation` and the rotation a painter composes
     * (`RoboTextGlyphOrientationRotation`). A run records the orientation its glyphs share, and the run set
     * cuts on it — a change of orientation is a run boundary, so an upright ideograph never shares a run with
     * the sideways Latin beside it.
     */
    int verticalOrientation;
    /**
     * Vertical font metrics: `RoboTextFontGlyphVerticalMetrics` over `vmtx`/`vhea`, and the vertical origin
     * folded into the glyph's placement offset so a painter applies an offset and never learns what a
     * vertical origin is. A face with no vertical metrics **synthesises** the advance from its horizontal
     * metrics and the run counts it (`RoboTextGlyphRunVerticalSynthesised`), which is reported rather than
     * passed off as the font's own answer.
     */
    int verticalMetrics;
    /**
     * The `vert`/`vrt2` hook: a vertical mode names both tags in the run's feature set, so a vertical
     * alternate reaches 12.10's GSUB through the one door every feature uses. A caller's own setting for
     * either tag is left alone, and a horizontal mode writes nothing.
     */
    int verticalFeatures;
    /** A writing setting as a diagnostic line (`RoboTextWritingModeWrite`). */
    int verticalDiagnostics;

    /* --- Phase 12.15: text measurement --- */

    /**
     * Measurement is a **door over a finalized run list**: a width is the runs' own advances, and nothing in
     * the measurement path decodes, selects a font, resolves a script or a level, or runs GSUB/GPOS.
     *
     * Distinct from @ref measurement, which has been true since 12.4: that is the *existence* of a measurement,
     * this is the property that makes it authoritative rather than a second opinion. `RoboTextShape.c` calls the
     * engine, so the result's own measurement and a caller's second measurement of the same result are one
     * implementation.
     */
    int measurementEngine;
    /**
     * Line metrics over a mixed-font line aggregate by the **maximum** rule (ascent, descent and gap each the
     * maximum over the participating runs) rather than the first run's.
     *
     * What this does *not* claim: a CSS `line-height` algorithm. A declared leading is honoured as declared, and
     * `lineMetrics.height` is the typographic sum beside it — the two are different questions and both answers
     * are published.
     */
    int lineMetricsPolicy;
    /**
     * Ink bounds, from each glyph's own outline box as the face reports it (`glyf`'s per-glyph bbox), scaled
     * through the module's one scaling door and translated by the pen and the glyph's placement offset.
     *
     * What this does *not* claim: a box for every glyph. A face whose outlines carry no box (a CFF face) leaves
     * the measurement's `hasInkBounds` **0** and its `outComplete` false, which is reported rather than
     * approximated with the advance box — a rectangle that paints nothing is worse than an absent one.
     */
    int inkBounds;
    /** Logical bounds and ink bounds kept apart, so a mark that overhangs its base is a fact and not a
     *  correction to the width. */
    int measurementBounds;
    /** The measurement invariants, by a door a test calls (`RoboTextMeasureValidate`). */
    int measurementValidation;
    /** A measurement as a diagnostic block: the totals, a line per run and a line per glyph with its pen
     *  position and its ink box. */
    int measurementDiagnostics;
    /** The numerical tolerance a measurement comparison uses, published as `RoboTextMeasureTolerancePx`. */
    int measurementTolerance;
} RoboTextCapabilities;

/** Zero @p capabilities (every feature reported absent). Safe on NULL. */
void RoboTextCapabilitiesInit(RoboTextCapabilities *capabilities);

/** Fill @p out with this build's capabilities. RoboInvalidArgument for a NULL @p out. */
Result RoboTextGetCapabilities(RoboTextCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_VERSION_H */
