/*
 * The OpenType layout core GSUB and GPOS share (Phases 12.10, 12.11, 12.13).
 *
 * Both layout tables answer the same three questions before either engine runs a lookup: *which script
 * record does this text use*, *which LangSys inside it*, and *which of that LangSys's features does the
 * caller's setting turn on*. Answering those twice would be two implementations of one registry lookup and
 * one fallback rule, and they would drift — so they are answered once, here, and both engines resolve their
 * plan with it.
 *
 * What is shared is deliberately narrow: a **tag** and its two conversions, the **script tag** a
 * @ref RoboTextScript maps to, the **plan** (the enabled features and the lookup indices they name), and its
 * resolution. What is *not* shared is any behaviour: the defaults for a script are the engine's
 * (@ref RoboTextGsubDefaultFeatureSet for substitution, @ref RoboTextGposDefaultFeatureSet for positioning),
 * the lookup types are each table's own, and the readers that interpret a subtable are each engine's.
 *
 * The implementation of everything declared here lives in `RoboTextGsub.c`, in the file's tags and plan
 * sections: 12.10 wrote them there and 12.11's positioning resolves with the same code rather than a copy.
 */
#ifndef ROBO_TEXT_OT_LAYOUT_H
#define ROBO_TEXT_OT_LAYOUT_H

#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontOtCommon.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Tags                                                                */
/* ------------------------------------------------------------------ */

/**
 * The OpenType tag a four-byte feature or script tag denotes, as the big-endian word a font stores.
 *
 * `"liga"` answers `0x6C696761`. A tag that is not exactly four bytes answers 0, which no real tag is.
 */
uint32_t RoboTextOtTagOf(const char *tag);

/** Write @p tag back as four characters into @p buffer (which needs 5 bytes). Never fails. */
void RoboTextOtTagWrite(uint32_t tag, char *buffer);

/* ------------------------------------------------------------------ */
/* The feature plan                                                    */
/* ------------------------------------------------------------------ */

/** Features one resolved plan can hold. A font may carry more; the plan keeps the enabled ones. */
#define RoboTextOtMaxPlanFeatures 32u

/**
 * One feature the plan turns on, and the lookups the **font** says it owns.
 *
 * `lookupIndices` is borrowed from the font's own Feature table and never copied, so a plan is valid for
 * exactly as long as the layout it was resolved against — which is the same lifetime as the face, and the
 * reason a plan is a value a caller builds per face rather than per run.
 */
typedef struct RoboTextOtFeature {
    /** The feature tag, four ASCII bytes and a terminator. */
    char tag[RoboTextFeatureTagBytes];
    /** The tag as a word, for comparing against the font's records. */
    uint32_t tagValue;
    /** The index of the feature record in the font's FeatureList. */
    uint16_t featureIndex;
    /** Lookups this feature names, in the font's order. Borrowed from the font. */
    const uint16_t *lookupIndices;
    uint16_t lookupIndexCount;
    /** The feature's parameter: an alternate index for the alternate features, else 0. */
    uint32_t value;
} RoboTextOtFeature;

/**
 * The lookups one shaping call will run, in the order the font defines them.
 *
 * Resolution is two steps and both are the font's, not this module's:
 *
 *   the **script**     a `TextScript` is mapped to the four-byte script tag a font uses
 *                      (`RoboTextOtScriptTag`), the font's ScriptList is asked for that record, and the
 *                      `DFLT` record answers when the font has no record for the script. A script the font
 *                      does not know is not an error: the answer is an empty plan, which runs nothing.
 *   the **language**   the caller's `TextLanguage`, if any, selects a LangSys in that script record; when
 *                      the font has no such LangSys the **script's default LangSys** answers, which is the
 *                      deterministic fallback the phase requires. A LangSys's `requiredFeatureIndex`
 *                      (rare, and real) is added whichever way its feature list reads.
 *
 * Only features the script's LangSys *lists* are reachable: a feature tag that exists in the font but is not
 * in this script's LangSys is not applied, which is what stops a `latn`-only `liga` from running on Arabic.
 *
 * The plan is **one type for both tables**, because the questions are the same ones: a GSUB plan and a GPOS
 * plan differ in the defaults an unstated feature gets and in nothing else. Which table it belongs to is
 * carried by the caller and by the `lookupIndices` it borrowed, not by the value.
 */
typedef struct RoboTextOtPlan {
    /** The script record the plan resolved against. */
    uint32_t scriptTag;
    /** Non-zero when the `DFLT` record answered because the font had no record for the script. */
    int usedDefaultScript;
    /** The LangSys the plan resolved against; 0 when the script's default LangSys answered. */
    uint32_t languageTag;
    /** Non-zero when the default LangSys answered (either the language was absent, or none was named). */
    int usedDefaultLangSys;
    /** Non-zero when the font has no script or feature records this plan could use. */
    int empty;
    /** The enabled features, in the order the font's LangSys lists them. */
    RoboTextOtFeature features[RoboTextOtMaxPlanFeatures];
    size_t featureCount;
    /**
     * Lookup indices to apply, ascending and deduplicated — the execution order.
     *
     * Built by collecting every enabled feature's lookups and sorting them, which is the order the **Lookup
     * List** defines and therefore the order the font intends: OpenType numbers lookups so that a later
     * lookup sees an earlier lookup's work, and applying them in feature order instead would reverse that
     * for every font that lists them out of order (most do). A lookup two features both name runs **once**,
     * at its own index — dedup by index cannot change the font's answer, because the answer is a function of
     * the lookup sequence rather than of the paths that reached it.
     */
    uint16_t lookupOrder[RoboFontOtMaxLookups];
    /**
     * The **position mask** each lookup applies at, indexed by the **Lookup List** index the engine reads it
     * with — which is `lookupOrder`'s own numbers, not their positions in it (Phase 12.19).
     *
     * Zero means "every position", which is what every lookup of a Latin plan is. A non-zero mask is a bit
     * field the *caller* supplies per position — a glyph position whose own mask word ANDs with it is a
     * position this lookup runs at — and it is the whole of how a complex script asks a feature to apply
     * *here and not there*: Arabic's contextual forms, Devanagari's reph and half forms.
     *
     * The masks are filled by `RoboTextComplexStageApplyMasks` from a strategy's stage, and they live in the
     * plan rather than in the engine because the mapping from a feature to the positions it applies at is the
     * *strategy's* knowledge and not the table's: the engine knows a lookup and a position, and nothing else.
     */
    uint16_t lookupMasks[RoboFontOtMaxLookups];
    /** Lookups in @ref lookupOrder. */
    size_t lookupCount;
} RoboTextOtPlan;

/** Clear @p plan to "nothing resolved": no script, no features, no lookups. Safe on NULL. */
void RoboTextOtPlanInit(RoboTextOtPlan *plan);

/** Non-zero when @p plan is a plan this build produced (@ref RoboTextOtPlanInit at least). */
int RoboTextOtPlanIsValid(const RoboTextOtPlan *plan);

/**
 * The four-byte OpenType script tag for @p script, or 0 when the script has no registered tag.
 *
 * The mapping is the OpenType registry's, and it is *data*: Latin is `latn`, Arabic `arab`, Hebrew `hebr`,
 * Devanagari `deva`, Han `hani`, Hangul `hang`, Common/Inherited/Unknown answer 0 — a font records common
 * features under `DFLT`, so "no tag" is the right answer for a script that has none rather than a made-up
 * one. The phase's §"script selection" is this function plus the `DFLT` fallback below: no shaping decision
 * anywhere in this module branches on a script, and none may be added.
 */
uint32_t RoboTextOtScriptTag(RoboTextScript script);

/**
 * Resolve @p features against @p layout — the plan an engine then applies.
 *
 * @param layout the face's GSUB or GPOS layout, from `RoboTextFontGsub`/`RoboTextFontGpos`. NULL resolves an
 *               empty plan (a face without the table is not an error).
 * @param features the caller's settings; NULL means "the engine's defaults".
 * @param scriptDefaults the engine's defaults for @p script, answering what an unstated feature gets. A
 *               feature the caller named is always the caller's answer, whichever set it is in; this is only
 *               consulted for the tags the caller said nothing about. NULL means "no default is on", which
 *               is what a caller that wants to resolve an explicitly-configured set does.
 * @return RoboOk (including the empty plan); RoboInvalidArgument for a NULL @p out.
 */
Result RoboTextOtPlanResolve(const FontOtLayout *layout,
                             RoboTextScript script,
                             const RoboTextLanguage *language,
                             const RoboTextFeatureSet *features,
                             const RoboTextFeatureSet *scriptDefaults,
                             RoboTextOtPlan *out,
                             RoboTextDiag *diag);

/** Human-readable plan summary (`"latn/ur  features: ccmp liga  lookups: 3 5 8"`). Complete-or-absent. */
Result RoboTextOtPlanWrite(const RoboTextOtPlan *plan,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength);

/* ------------------------------------------------------------------ */
/* The bounds a layout engine is held to                               */
/* ------------------------------------------------------------------ */

/**
 * The bounds a hostile font is held to. Centralised because they are a policy, not a constant of any
 * algorithm, and because a limit discovered five call frames down is a limit nobody can tune.
 *
 * **One type for both tables**: substitution and positioning are bounded by the same four questions — how
 * many lookups, how many glyph probes, how many glyphs, how deep — and the answers a font's own counts
 * cannot be trusted for are the same ones. What differs is only which counters the limit stops, and that
 * belongs to each engine's own statistics rather than to the bound.
 */
typedef struct RoboTextOtLimits {
    /**
     * Lookup applications one call may perform — the outer loop's own bound.
     *
     * A lookup is applied once per call by construction, so this bounds *nested* work too (a contextual
     * lookup's inner lookups), and it exists because a font can nest extension lookups and contextual
     * lookups until a naive engine does exponential work.
     */
    uint32_t maxLookupApplications;
    /**
     * Glyph-to-glyph probes one call may perform — the inner bound.
     *
     * Every match attempt costs O(pattern length); this bounds the product of positions and patterns, which
     * is the shape a pathological context table attacks. Hitting it abandons the lookup, not the call.
     */
    uint32_t maxGlyphProbes;
    /** Glyphs one call's buffer may hold, so a multiple substitution cannot grow it without bound. */
    size_t maxGlyphs;
    /** Nesting depth for a contextual lookup that invokes another lookup (and for extension unwrapping). */
    uint32_t maxNesting;
} RoboTextOtLimits;

/** The defaults: 4096 lookup applications, 1 048 576 probes, 65 536 glyphs, nesting 6. */
extern const RoboTextOtLimits RoboTextOtDefaultLimits;

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_OT_LAYOUT_H */
