/*
 * RoboText's glyph positioning engine (Phase 12.11) — the GPOS half of the module's shaping.
 *
 * GSUB answers *which glyphs*; GPOS answers *where they sit*. The two are one pipeline in this module:
 * the substitution engine mutates a @ref RoboTextShapingBuffer, and this one then adjusts the glyphs it
 * produced — never the Unicode text, never the ids, never the order.
 *
 * ## What this engine reads
 *
 * The font's **GPOS** table, resolved through the same shared layout core GSUB uses
 * (`RoboTextOtPlanResolve`, the script tag, the LangSys fallback, the sorted lookup order) but with
 * **positioning's** defaults for an unstated feature: `kern`, `mark`, `mkmk`, `curs`, `dist`. A caller's
 * own setting always wins over those, which is what makes `font-kerning: none` an answer this module can
 * honour rather than something the paint layer has to know about.
 *
 * ## What it writes
 *
 * Four device-pixel numbers on each glyph, and **only** those: @ref RoboTextShapingGlyph::offsetAdjustX /
 * ::offsetAdjustY (where the glyph sits relative to the pen) and ::advanceAdjustX / ::advanceAdjustY (how
 * far the pen moves after it, *in addition to* the face's own `hmtx` advance). Keeping the adjustment
 * separate from the face's advance is the point: the run build adds the two, so a dump can say "the
 * advance is 12.4px, of which -1.25 is the font's kerning", and no caller has to guess which part moved.
 *
 * Positions are float device pixels and are **not** rounded here. The module's rounding policy is
 * "precision through shaping and layout, round once at the rasterizer", so a `-0.35px` pair stays
 * `-0.35px` until something has to draw it.
 *
 * ## Lookup types
 *
 *   1  single adjustment     one value record for a covered glyph (formats 1 and 2)
 *   2  pair adjustment       the kerning path: an explicit pair set (format 1) or a class matrix
 *                            (format 2), with a value record for each side of the pair
 *   9  extension             a wrapper whose whole content is "the real type is X, elsewhere"
 *
 * Types 3–8 (cursive attachment, mark-to-base, mark-to-ligature, mark-to-mark, and the two contextual
 * forms) are **recognised and skipped**, each counted in @ref RoboTextGposStats so a caller can see what a
 * face wanted and did not get. They are not wrong answers: a skipped lookup leaves the glyph where the
 * advance put it, which is exactly what this build drew before positioning existed. §"known limitations" of
 * `Documentation/Runtime/Text/Positioning.md` says which goal adds them.
 *
 * ## Bounds
 *
 * Every read goes through `FontReader` and `FontOtResolveOffset16/32`, the parsed coverage and class
 * definitions are cached per call (@ref RoboTextOtCoverageCache in `RoboTextOtInternal.h`), and a malformed
 * subtable **skips**: it is counted and the scan continues. A hostile font can waste this engine's probes,
 * never its memory or its stack.
 */
#ifndef ROBO_TEXT_GPOS_H
#define ROBO_TEXT_GPOS_H

#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboText/RoboTextGsub.h"
#include "RoboText/RoboTextOtLayout.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Lookup types                                                        */
/* ------------------------------------------------------------------ */

/** The GPOS lookup types, by the number a font stores. Types 3–8 are skipped by this build. */
#define RoboTextGposLookupSingle 1u
#define RoboTextGposLookupPair 2u
#define RoboTextGposLookupCursive 3u
#define RoboTextGposLookupMarkToBase 4u
#define RoboTextGposLookupMarkToLigature 5u
#define RoboTextGposLookupMarkToMark 6u
#define RoboTextGposLookupContext 7u
#define RoboTextGposLookupChainingContext 8u
#define RoboTextGposLookupExtension 9u
#define RoboTextGposLookupTypeCount 9u

/** A first or second glyph that is not there (`RoboGlyphIdNone`-safe: this is not a glyph id). */
#define RoboTextGposNoGlyph 0xFFFFu
/** A class index that was not asked for: a single adjustment is not a class pair. */
#define RoboTextGposNoClass 0xFFFFu

/* ------------------------------------------------------------------ */
/* Values                                                              */
/* ------------------------------------------------------------------ */

/**
 * One value record, already converted to device pixels.
 *
 * The four fields are the font's, in the font's units, scaled once by this module's one scaling door
 * (`RoboTextFontUnitsToDevice`) and with any device-table delta already folded in — @ref deviceCorrections
 * counts how many records were not purely font units, so a diagnostic can tell "the font's number" from
 * "the font's number plus its hinting for this size".
 */
typedef struct RoboTextGposValue {
    /** Where the glyph sits relative to the pen, in device pixels. */
    float xPlacement;
    float yPlacement;
    /** How far the pen moves after the glyph, **in addition** to the face's own advance. */
    float xAdvance;
    float yAdvance;
    /** Non-zero when a device or variation table contributed to any field above. */
    int deviceApplied;
} RoboTextGposValue;

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */


/* ------------------------------------------------------------------ */
/* Options, trace and stats                                            */
/* ------------------------------------------------------------------ */

typedef struct RoboTextGposEvent RoboTextGposEvent;

/** What the caller hands one positioning call. Zero it and set what matters; the rest are defaults. */
typedef struct RoboTextGposOptions {
    /** NULL means @ref RoboTextGposDefaultLimits. */
    const RoboTextOtLimits *limits;
    /** The run's resolved direction: a lookup whose flag is `RightToLeft` runs only on RTL text. */
    RoboTextDirection direction;
    /** The run's bidi level, carried for the trace and for a caller's own diagnostics. */
    RoboTextBidiLevel bidiLevel;
    /**
     * Device pixels per em, for device-table corrections.
     *
     * 0 (the default) means "no size is known", and a device offset is then **not evaluated**: the
     * adjustment keeps the font's own value and @ref RoboTextGposStats::deviceUnevaluated counts it. That
     * is a deliberate answer rather than a quiet zero — a device delta applied at the wrong size is a worse
     * lie than no delta.
     */
    uint16_t devicePpem;
    /**
     * Device pixels per **font unit**, from the module's one scaling door:
     * `RoboTextFontUnitsToDevice(context, 1.0, &scale, diag)`.
     *
     * The engine **multiplies** rather than scales, and that is deliberate: a value record is a font-unit
     * number, and `RoboTextFontUnitsToDevice` is the only place in this module where a font unit becomes a
     * pixel. Asking it once for the unit and multiplying here keeps that true while costing one call per
     * run instead of one per adjustment.
     *
     * Zero means "no scale was supplied", and then **no adjustment is applied at all** (every record is
     * counted in @ref RoboTextGposStats::recordsUnevaluated). Half-applying a font's numbers in units nobody
     * converted would put glyphs an `upem` away from where they belong; refusing is the honest answer.
     */
    float deviceScale;
    /** Optional trace buffer; NULL traces nothing. Overflow stops tracing, never shaping. */
    RoboTextGposEvent *trace;
    size_t traceCapacity;
} RoboTextGposOptions;

/**
 * One adjustment, for a diagnostic.
 *
 * `first` is the glyph the record was read for and `second` is the other side of a pair, or
 * @ref RoboTextGposNoGlyph for a single adjustment. The classes are @ref RoboTextGposNoClass when the pair
 * was matched by glyph id rather than by class.
 */
typedef struct RoboTextGposEvent {
    /** The feature whose plan named the lookup, four ASCII bytes and a terminator. */
    char feature[RoboTextFeatureTagBytes];
    uint16_t lookupIndex;
    /** The lookup's type with any extension already unwrapped. */
    uint16_t lookupType;
    /** The buffer position the adjustment applied at. */
    size_t position;
    /** The buffer position of the pair's second glyph, or @ref RoboTextGposNoGlyph. */
    size_t secondPosition;
    uint16_t firstGlyph;
    uint16_t secondGlyph;
    uint16_t firstClass;
    uint16_t secondClass;
    /** The font's numbers for the first glyph, in device pixels. */
    RoboTextGposValue first;
    /** The font's numbers for the second glyph, or zeroes for a single adjustment. */
    RoboTextGposValue second;
    /** The scalars the adjusted glyph stands for. */
    RoboTextRange source;
} RoboTextGposEvent;

/**
 * What one call did.
 *
 * The counters that matter for a *kerning* question are @ref pairAdjustments and
 * @ref classPairAdjustments: between them they say whether the font had anything to say about the glyph
 * sequence this run produced, and their being zero is how a caller tells "the font kerns nothing here" from
 * "kerning was never on".
 */
typedef struct RoboTextGposStats {
    /** Glyphs in the buffer when the call began. */
    size_t glyphsBefore;
    /** Glyphs in the buffer when it ended. Positioning never changes this; it is recorded so a caller can. */
    size_t glyphsAfter;
    /** Lookups the plan named that this call ran. */
    size_t lookupsApplied;
    /** Lookups the plan named that were skipped: a type this build does not run, or a malformed record. */
    size_t lookupsSkipped;
    /** Subtables the scan tried. */
    size_t subtablesTried;
    /** Subtables whose bytes would not read: skipped, counted, and shaping continued. */
    size_t subtablesMalformed;
    /** Single adjustments applied (lookup type 1). */
    size_t singleAdjustments;
    /** Pair adjustments applied by an explicit pair set (lookup type 2 format 1). */
    size_t pairAdjustments;
    /** Pair adjustments applied by a class matrix (lookup type 2 format 2). */
    size_t classPairAdjustments;
    /** Lookups of a type this build recognises and does not run (3–8). */
    size_t lookupsUnsupported;
    /** Value records whose device or variation correction was evaluated. */
    size_t deviceCorrections;
    /** Value records whose device correction was **not** evaluated, because no size was known. */
    size_t deviceUnevaluated;
    /**
     * Value records that were not read at all, because no device scale was supplied.
     *
     * Zero for every call this module makes — the generator always supplies the face's scale — and non-zero
     * only for a caller that positioned without one, which is a fact worth reporting rather than hiding.
     */
    size_t recordsUnevaluated;
    /** Glyphs left with a non-zero placement or advance adjustment. */
    size_t adjustedGlyphs;
    /** Glyph comparisons charged against @ref RoboTextOtLimits::maxGlyphProbes. */
    uint32_t probes;
    /** Non-zero when a limit stopped work: the call finished early and says so. */
    int limitReached;
    /** Events the trace recorded. */
    size_t traceEvents;
} RoboTextGposStats;

/** Clear @p stats to zeroes. Safe on NULL. */
void RoboTextGposStatsInit(RoboTextGposStats *stats);

/* ------------------------------------------------------------------ */
/* The plan and the call                                               */
/* ------------------------------------------------------------------ */

/** The features a script's text gets when the caller names none. */
void RoboTextGposDefaultFeatureSet(RoboTextScript script, RoboTextFeatureSet *out);

/** Resolve @p features against @p gpos, with positioning's defaults. See @ref RoboTextOtPlanResolve. */
Result RoboTextGposPlanResolve(const FontOtLayout *gpos,
                               RoboTextScript script,
                               const RoboTextLanguage *language,
                               const RoboTextFeatureSet *features,
                               RoboTextOtPlan *out,
                               RoboTextDiag *diag);

/**
 * Apply @p plan's lookups to @p buffer, writing each glyph's adjustment.
 *
 * @param gpos the face's GPOS table, or NULL (then nothing runs and the buffer is unchanged).
 * @param gdef the face's GDEF table, for the lookup flags that skip a class of glyph; NULL is fine.
 * @param plan a plan from @ref RoboTextGposPlanResolve; an empty plan applies nothing.
 * @param options limits, direction, device size and trace; NULL means the defaults.
 * @return RoboOk (including "the plan was empty" and "the font's bytes were malformed", which are skips and
 *         not failures); RoboInvalidArgument for a NULL buffer or stats; RoboOutOfMemory when a buffer
 *         could not grow.
 */
Result RoboTextGposApply(const FontOtLayout *gpos,
                         const FontGdef *gdef,
                         const RoboTextOtPlan *plan,
                         const RoboTextGposOptions *options,
                         RoboTextShapingBuffer *buffer,
                         RoboTextGposStats *outStats,
                         RoboTextDiag *diag);

/** Human-readable counters, one `label value` per line. Complete-or-absent. */
Result RoboTextGposStatsWrite(const RoboTextGposStats *stats,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength);

/** Human-readable adjustment trace, one line per event. Complete-or-absent. */
Result RoboTextGposEventsWrite(const RoboTextGposEvent *events,
                               size_t count,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_GPOS_H */
