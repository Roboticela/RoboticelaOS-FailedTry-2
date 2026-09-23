/*
 * RoboText's glyph substitution engine (Phase 12.10) — OpenType GSUB, applied to a shaping buffer.
 *
 * ## The one rule
 *
 * **GSUB changes glyph identity and glyph sequence structure. It never changes the text.** The source is a
 * `RoboTextView` that this module only reads; what a lookup rewrites is a @ref RoboTextGsubBuffer, whose
 * entries carry the source range they came from. So `"f" "f" "i"` becoming one glyph is a change to the
 * glyph buffer and *not* to the three scalars, and the ligature glyph's `source` still names all three —
 * which is what makes a selection, a caret and a copy answer with `ffi` the way the author wrote it.
 *
 * ## Where the tables come from
 *
 * The font owns its bytes and its tables, and Phase 5 already parses the GSUB *structure*:
 * `FontOtLayout` holds the ScriptList, the FeatureList and the LookupList, with one `FontOtLookup` per
 * lookup carrying its type, its flags and its subtable offsets (`RoboFontOtCommon.h`). This module does not
 * re-find the table, re-validate its header or re-read its version — it takes that parsed layout, resolves
 * *which* lookups the caller's features turn on, and then interprets the subtables those offsets name,
 * through Phase 5's own offset resolver (`FontOtResolveOffset16`) and its own Coverage and ClassDef readers
 * (`FontOtCoverageParse`/`…Index`, `FontOtClassDefParse`/`…Get`). There is one OpenType reader in this tree
 * and this file is not a second one: everything it does to font bytes goes through `RoboFontBinary.h`'s
 * bounds-checked `FontReader`.
 *
 * The face caches the parsed layout (`FontFaceParseOtLayout`, reached from a `RoboTextFontContext` through
 * `RoboTextFontGsub`), so the *table* is parsed once per face no matter how much text is shaped with it. The
 * subtables are interpreted per shaping call, which is a documented cost rather than an oversight: a parsed
 * subtable is an owned, immutable object per lookup, and giving one a home (the face, with its own cache and
 * its own lifetime) is the increment that makes a face-level subtable cache possible. What this file
 * guarantees instead is that **no work happens per position**: a coverage or a class definition is parsed at
 * most once per lookup per call, and every probe after that is a binary search or a range lookup.
 *
 * ## What it does not do
 *
 * No positioning (that is GPOS, 12.11), no `cmap` (RoboFont's, through `RoboTextFontLookupGlyph`), no text
 * decoding or segmentation (the run's own layers), and no shaping policy that a font does not state: Arabic
 * joining, Indic reordering and every other complex-script behaviour arrive *through* a font's lookups —
 * which is exactly why the contextual and chaining lookup types are supported here rather than being left
 * for a script-specific pass to invent. Direction is honoured as *the direction of the run* (a lookup that
 * sets its `RightToLeft` flag is applied only to a right-to-left run), and never by reversing anything.
 *
 * ## Reliability
 *
 * A font is untrusted input — a web page can supply one — so every read is bounds-checked, every count is
 * checked against the bytes the table actually has, and every failure is a *skip*: a malformed subtable is
 * reported, its lookup is abandoned, and the caller keeps the glyphs it has. `RoboTextGsubApply` cannot
 * crash on a malformed table and never returns a buffer in a state that violates its own invariants. The
 * four limits in @ref RoboTextGsubLimits bound the work a hostile table can ask for.
 */
#ifndef ROBO_TEXT_GSUB_H
#define ROBO_TEXT_GSUB_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontOtCommon.h"
#include "RoboText/RoboTextBidi.h"
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
uint32_t RoboTextGsubTagOf(const char *tag);

/** Write @p tag back as four characters into @p buffer (which needs 5 bytes). Never fails. */
void RoboTextGsubTagWrite(uint32_t tag, char *buffer);

/* ------------------------------------------------------------------ */
/* The feature plan                                                    */
/* ------------------------------------------------------------------ */

/** Features one resolved plan can hold. A font may carry more; the plan keeps the enabled ones. */
#define RoboTextGsubMaxPlanFeatures 32u

/**
 * One feature the plan turns on, and the lookups the **font** says it owns.
 *
 * `lookupIndices` is borrowed from the font's own Feature table and never copied, so a plan is valid for
 * exactly as long as the layout it was resolved against — which is the same lifetime as the face, and the
 * reason a plan is a value a caller builds per face rather than per run.
 */
typedef struct RoboTextGsubFeature {
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
} RoboTextGsubFeature;

/**
 * The lookups one shaping call will run, in the order the font defines them.
 *
 * Resolution is two steps and both are the font's, not this module's:
 *
 *   the **script**     a `TextScript` is mapped to the four-byte script tag a font uses
 *                      (`RoboTextGsubScriptTag`), the font's ScriptList is asked for that record, and the
 *                      `DFLT` record answers when the font has no record for the script. A script the font
 *                      does not know is not an error: the answer is an empty plan, which runs nothing.
 *   the **language**   the caller's `TextLanguage`, if any, selects a LangSys in that script record; when
 *                      the font has no such LangSys the **script's default LangSys** answers, which is the
 *                      deterministic fallback the phase requires. A LangSys's `requiredFeatureIndex`
 *                      (rare, and real) is added whichever way its feature list reads.
 *
 * Only features the script's LangSys *lists* are reachable: a feature tag that exists in the font but is not
 * in this script's LangSys is not applied, which is what stops a `latn`-only `liga` from running on Arabic.
 */
typedef struct RoboTextGsubPlan {
    /** The script record the plan resolved against. */
    uint32_t scriptTag;
    /** Non-zero when the `DFLT` record answered because the font had no record for the script. */
    int usedDefaultScript;
    /** The LangSys the plan resolved against; 0 when the script's default LangSys answered. */
    uint32_t languageTag;
    /** Non-zero when the default LangSys answered (either the language was absent, or none was named). */
    int usedDefaultLangSys;
    /** Non-zero when the font has no GSUB script or feature records this plan could use. */
    int empty;
    /** The enabled features, in the order the font's LangSys lists them. */
    RoboTextGsubFeature features[RoboTextGsubMaxPlanFeatures];
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
    /** Lookups in @ref lookupOrder. */
    size_t lookupCount;
} RoboTextGsubPlan;

/** Clear @p plan to "nothing resolved": no script, no features, no lookups. Safe on NULL. */
void RoboTextGsubPlanInit(RoboTextGsubPlan *plan);

/** Non-zero when @p plan is a plan this build produced (@ref RoboTextGsubPlanInit at least). */
int RoboTextGsubPlanIsValid(const RoboTextGsubPlan *plan);

/**
 * The four-byte OpenType script tag for @p script, or 0 when the script has no registered tag.
 *
 * The mapping is the OpenType registry's, and it is *data*: Latin is `latn`, Arabic `arab`, Hebrew `hebr`,
 * Devanagari `deva`, Han `hani`, Hangul `hang`, Common/Inherited/Unknown answer 0 — a font records common
 * features under `DFLT`, so "no tag" is the right answer for a script that has none rather than a made-up
 * one. The phase's §"script selection" is this function plus the `DFLT` fallback below: no shaping decision
 * anywhere in this module branches on a script, and none may be added.
 */
uint32_t RoboTextGsubScriptTag(RoboTextScript script);

/**
 * Resolve @p features against @p gsub — the plan the engine then applies.
 *
 * @param gsub the face's GSUB layout, from `RoboTextFontGsub`. NULL resolves an empty plan (no font
 *             substitution is not an error).
 * @param features the caller's settings; NULL means "the script's defaults".
 * @return RoboOk (including the empty plan); RoboInvalidArgument for a NULL @p out.
 */
Result RoboTextGsubPlanResolve(const FontOtLayout *gsub,
                               RoboTextScript script,
                               const RoboTextLanguage *language,
                               const RoboTextFeatureSet *features,
                               RoboTextGsubPlan *out,
                               RoboTextDiag *diag);

/**
 * The features a script's text gets when the caller names none.
 *
 * The set every shaper applies by default, and it is small on purpose: `ccmp` (compose and decompose),
 * `locl` (locale forms), `rlig` (required ligatures), `liga` and `clig` (standard and contextual
 * ligatures), `calt` (contextual alternates), `rclt` (required contextual alternates) and, for a script
 * whose shaping depends on them, the joining/reordering features (`init`, `medi`, `fina`, `isol`, `rphf`,
 * `pref`, `blwf`, `half`, `pstf`, `abvs`, `blws`, `psts`, `haln`, `cjct`, `nukt`, `akhn`). A font that has
 * none of them runs nothing, which is the common case for a Latin-only UI face: Inter has `ccmp`, `locl`
 * and `calt` and no `liga` at all.
 *
 * `RoboTextFeatureSet` is the *caller's* view, so a caller's `Off` setting overrides this set — which is
 * what makes `font-feature-settings: "liga" 0` mean something.
 */
void RoboTextGsubDefaultFeatureSet(RoboTextScript script, RoboTextFeatureSet *out);

/** Human-readable plan summary (`"latn/ur  features: ccmp liga  lookups: 3 5 8"`). Complete-or-absent. */
Result RoboTextGsubPlanWrite(const RoboTextGsubPlan *plan,
                             char *buffer,
                             size_t capacity,
                             size_t *outLength);

/* ------------------------------------------------------------------ */
/* The shaping glyph and its buffer                                    */
/* ------------------------------------------------------------------ */

/** Bits in @ref RoboTextShapingGlyph::flags. */
typedef enum RoboTextShapingGlyphFlags {
    /** No flags: the glyph is exactly what the `cmap` produced. */
    RoboTextShapingGlyphNone = 0,
    /** The id came from a lookup rather than from the `cmap`. */
    RoboTextShapingGlyphSubstituted = 1u << 0,
    /** The glyph stands for two or more source scalars (a ligature's product). */
    RoboTextShapingGlyphLigature = 1u << 1,
    /** The glyph is one of several a single source scalar produced. */
    RoboTextShapingGlyphMultiple = 1u << 2,
    /** The id is one of a feature's alternates. */
    RoboTextShapingGlyphAlternate = 1u << 3,
    /**
     * The glyph stands for a bidi formatting control: it keeps its slot and is **not matched**.
     *
     * Phase 12.8's §"formatting characters" in the buffer's vocabulary. A control has no id and no advance,
     * but it has a source position, and a lookup that matched across it would join text the control separates
     * — so it stays in the array, where it holds the indices of everything after it, and no applier sees it.
     */
    RoboTextShapingGlyphControl = 1u << 5,
    /**
     * The id came from the UAX #9 L4 mirrored code point rather than from the scalar itself.
     *
     * Carried so a run can be rebuilt from the buffer — which is what the generator does when a lookup has
     * changed the glyph count — without losing which glyphs were drawn mirrored.
     */
    RoboTextShapingGlyphMirrored = 1u << 6,
    /**
     * Reserved for a glyph no source scalar produced — a dotted circle, a synthetic mark.
     *
     * **Nothing in this build sets it.** It exists so that the flags a later goal needs are named here
     * rather than added with a different meaning, and a glyph with no source is a bug in whatever produced
     * it, not a state a consumer must handle.
     */
    RoboTextShapingGlyphSynthetic = 1u << 7
} RoboTextShapingGlyphFlags;

/**
 * One glyph while it is being shaped: an id, where it came from, and what happened to it.
 *
 * Deliberately small and a value, because the buffer is searched, spliced and copied: twenty-odd bytes of
 * `uint16_t`s and one range, with no pointer anywhere. `source` is the **source** — a range of scalars in
 * the text — and it is the field that must survive every substitution intact (Phase 12.10 §"glyph
 * provenance"): a ligature's product carries the union of its components' ranges, and a multiple
 * substitution's products each carry the single range of the glyph they came from.
 */
typedef struct RoboTextShapingGlyph {
    /** The font-local glyph id. `RoboGlyphIdNone` (0) means the face had no glyph — `.notdef`. */
    GlyphId glyphId;
    /**
     * The scalars responsible for this glyph.
     *
     * Never empty and never inverted, for a glyph this module produced: the buffer refuses an append that
     * would create one, because a glyph that cannot answer "where did I come from" is a glyph that breaks
     * selection. A ligature's range covers every component (and any ignored scalar between them).
     */
    RoboTextRange source;
    /** The glyph's cluster: the source index a caret treats as one unit. The `source.start` by default. */
    RoboTextClusterIndex cluster;
    /** The resolved bidi level the glyph is shaped at. Carried, never changed by substitution. */
    RoboTextBidiLevel bidiLevel;
    /** @ref RoboTextShapingGlyphFlags bits. */
    uint32_t flags;
    /**
     * The id this glyph was substituted **from**, or 0 when nothing changed it.
     *
     * The provenance a diagnostic reads (`"gid 42 -> 91"`) and the reason a lookup can be applied without
     * losing what it replaced. Not a Unicode scalar index and not a code point: GSUB works in glyph ids.
     */
    GlyphId inputGlyphId;
    /**
     * For a ligature's product, how many components it was built from; 0 otherwise.
     *
     * Enough to answer "is this the ligature's whole product or one of its parts" without a second array,
     * and the count the trace records.
     */
    uint16_t ligatureComponents;
} RoboTextShapingGlyph;

/** Clear @p glyph to a null glyph: id 0, empty source, level none, no flags. Safe on NULL. */
void RoboTextShapingGlyphInit(RoboTextShapingGlyph *glyph);

/** Non-zero when @p glyph names an id and a non-empty source range. Safe on NULL. */
int RoboTextShapingGlyphIsValid(const RoboTextShapingGlyph *glyph);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextShapingGlyphEqual(const RoboTextShapingGlyph *a, const RoboTextShapingGlyph *b);

typedef struct RoboTextGsubBuffer RoboTextGsubBuffer;

/**
 * A buffer holding @p capacity glyphs before it must grow.
 *
 * The mutable intermediate the phase asks for, and the reason there is one: GSUB changes the *number* of
 * glyphs, so the final `RoboTextGlyphRun` — a fixed array of glyphs with one entry per lookup already
 * applied — cannot be the thing a lookup edits. The buffer grows by doubling, never shrinks under a
 * caller's feet, and every mutation keeps the invariants the header states: ids and sources stay within the
 * text, sources stay non-empty, and the array stays in logical order.
 *
 * @return NULL when @p capacity is 0 or the allocation fails.
 */
RoboTextGsubBuffer *RoboTextGsubBufferCreate(size_t capacity);

/** Append @p glyph, growing if needed. @return RoboOk, or RoboInvalidArgument for a glyph with no source. */
Result RoboTextGsubBufferAppend(RoboTextGsubBuffer *buffer, const RoboTextShapingGlyph *glyph);

/** Glyphs held. */
size_t RoboTextGsubBufferCount(const RoboTextGsubBuffer *buffer);

/** Glyph @p index, or NULL when out of range. The pointer is invalidated by any mutation. */
const RoboTextShapingGlyph *RoboTextGsubBufferAt(const RoboTextGsubBuffer *buffer, size_t index);

/** Mutable glyph @p index, or NULL when out of range. For a caller building a buffer by hand. */
RoboTextShapingGlyph *RoboTextGsubBufferAtMutable(RoboTextGsubBuffer *buffer, size_t index);

/**
 * Replace glyph @p index's id, keeping its provenance — the single-substitution primitive.
 *
 * Kept separate from "write the whole record" because that is what a single substitution *is*: the id
 * changes and nothing else does, and a caller that had to reconstruct the record could lose `source` or the
 * level without noticing.
 */
Result RoboTextGsubBufferSetGlyph(RoboTextGsubBuffer *buffer, size_t index, GlyphId glyphId);

/**
 * Replace @p count glyphs starting at @p index with @p replacement.
 *
 * The one mutation a multiple substitution and a ligature share, and the reason both work: the replacement
 * array is copied in, `count` need not equal @p replacementCount (that is the whole point), and a
 * replacement of zero glyphs deletes. Refuses an empty replacement array for a non-empty @p count — a
 * deletion is expressed with `replacementCount == 0` and a NULL array, which says what it means.
 */
Result RoboTextGsubBufferReplace(RoboTextGsubBuffer *buffer,
                                 size_t index,
                                 size_t count,
                                 const RoboTextShapingGlyph *replacement,
                                 size_t replacementCount);

/** Release @p buffer. Safe on NULL. */
void RoboTextGsubBufferDestroy(RoboTextGsubBuffer *buffer);

/* ------------------------------------------------------------------ */
/* Limits, options, stats                                              */
/* ------------------------------------------------------------------ */

/**
 * The bounds a hostile font is held to. Centralised because they are a policy, not a constant of any
 * algorithm, and because a limit discovered five call frames down is a limit nobody can tune.
 */
typedef struct RoboTextGsubLimits {
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
} RoboTextGsubLimits;

/** The defaults: 4096 lookup applications, 1 048 576 probes, 65 536 glyphs, nesting 6. */
extern const RoboTextGsubLimits RoboTextGsubDefaultLimits;

/* Declared before the options because they point at one; defined below. */
typedef struct RoboTextGsubEvent RoboTextGsubEvent;

/** What a call may be told beyond the plan. A zeroed struct is the default behaviour. */
typedef struct RoboTextGsubOptions {
    /** The run's direction. A lookup whose flag asks for one direction is applied only to it. */
    RoboTextDirection direction;
    /** The run's resolved bidi level, carried onto every glyph the call produces. */
    RoboTextBidiLevel bidiLevel;
    /** The limits for this call; NULL uses @ref RoboTextGsubDefaultLimits. */
    const RoboTextGsubLimits *limits;
    /**
     * Where to record substitution events, or NULL to record none.
     *
     * A pointer to an array the **caller** owns, so tracing costs nothing when it is off and cannot fail
     * when it is on. @ref traceCapacity is how many entries that array holds.
     */
    RoboTextGsubEvent *trace;
    size_t traceCapacity;
} RoboTextGsubOptions;

/** One substitution, as a diagnostic prints it: what a lookup did, and to what. */
typedef struct RoboTextGsubEvent {
    /** The feature whose lookup did it, as its four bytes (unterminated here; see the writer). */
    char feature[4];
    /** The lookup's index in the font's Lookup List. */
    uint16_t lookupIndex;
    /** The lookup's type, after unwrapping an extension lookup. */
    uint16_t lookupType;
    /** The buffer position the substitution started at. */
    size_t position;
    /** Glyph ids consumed (a ligature's components, or the one glyph a single substitution replaced). */
    GlyphId from[4];
    size_t fromCount;
    /** Glyph ids produced. */
    GlyphId to[4];
    size_t toCount;
    /** The source range the produced glyphs cover. */
    RoboTextRange source;
} RoboTextGsubEvent;

/** Glyphs one event records; a longer ligature is recorded truncated, with the counts telling the truth. */
#define RoboTextGsubEventGlyphs 4u

/** Counters for a dump or an assertion. */
typedef struct RoboTextGsubStats {
    /** Glyphs before any lookup ran. */
    size_t glyphsBefore;
    /** Glyphs after the last lookup. */
    size_t glyphsAfter;
    /** Lookups applied (including nested ones). */
    size_t lookupsApplied;
    /** Lookups abandoned: unsupported type, malformed subtable, or a limit. */
    size_t lookupsSkipped;
    /** Subtables tried across every lookup. */
    size_t subtablesTried;
    /** Single substitutions performed. */
    size_t singleSubstitutions;
    /** Multiple substitutions performed (one source glyph becoming several). */
    size_t multipleSubstitutions;
    /** Alternate substitutions performed. */
    size_t alternateSubstitutions;
    /** Ligature substitutions performed (several source glyphs becoming one). */
    size_t ligatureSubstitutions;
    /** Contextual matches that led to a nested lookup being applied. */
    size_t contextualMatches;
    /** Reverse-chaining substitutions performed. */
    size_t reverseSubstitutions;
    /** Glyphs the `cmap` could not map, out of the glyphs that entered the call. */
    size_t unmappedGlyphs;
    /**
     * Substitution events recorded, which is the `count` a caller passes to @ref RoboTextGsubEventsWrite.
     *
     * Reported here rather than through an out-parameter because it is a count of work done, and because a
     * caller that traced and a caller that did not should read the same record of the call: when tracing is
     * off this is 0 and the two are otherwise identical.
     */
    size_t traceEvents;
    /** Probes the call spent (against @ref RoboTextGsubLimits::maxGlyphProbes). */
    uint32_t probes;
    /** Non-zero when a limit stopped something, so a caller can tell a limit from a font that ended. */
    int limitReached;
} RoboTextGsubStats;

/** Clear @p stats. Safe on NULL. */
void RoboTextGsubStatsInit(RoboTextGsubStats *stats);

/**
 * Run the plan over @p buffer.
 *
 * The whole engine, and its order is the font's: the plan's @ref RoboTextGsubPlan::lookupOrder is walked
 * once, each lookup's subtables are tried in order at each position, and a position whose match produced a
 * substitution continues *after* what the lookup produced — never re-matching the output with the same
 * lookup, which is the recursion the phase warns about and which no OpenType lookup asks for. A lookup that
 * fails, an unsupported type and a malformed subtable all **skip** (counted in @ref
 * RoboTextGsubStats::lookupsSkipped) and shaping continues with the glyphs produced so far: nothing here can
 * fail a run of text.
 *
 * @param plan a plan from @ref RoboTextGsubPlanResolve; an empty plan applies nothing.
 * @param options may be NULL (the defaults).
 * @return RoboOk; RoboInvalidArgument for a NULL @p buffer or @p stats; RoboOutOfMemory when the buffer
 *         could not grow (the buffer is left holding what it had).
 */
Result RoboTextGsubApply(const FontOtLayout *gsub,
                         const FontGdef *gdef,
                         const RoboTextGsubPlan *plan,
                         const RoboTextGsubOptions *options,
                         RoboTextGsubBuffer *buffer,
                         RoboTextGsubStats *outStats,
                         RoboTextDiag *diag);

/** One line per counter, plus the glyph counts. Complete-or-absent. */
Result RoboTextGsubStatsWrite(const RoboTextGsubStats *stats,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength);

/** One line per glyph (`"#0 gid=42 src=[0,1) cluster=0 lvl=0 flags=0x1 from=41"`). Complete-or-absent. */
Result RoboTextGsubBufferWrite(const RoboTextGsubBuffer *buffer,
                               char *bufferOut,
                               size_t capacity,
                               size_t *outLength);

/** One line per event (`"liga lookup 4 (type 4) at 3: 42 42 57 -> 91 src=[0,3)"`). Complete-or-absent. */
Result RoboTextGsubEventsWrite(const RoboTextGsubEvent *events,
                               size_t count,
                               char *buffer,
                               size_t capacity,
                               size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_GSUB_H */
