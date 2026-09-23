/*
 * RoboText's glyph substitution engine (Phase 12.10) — OpenType GSUB, applied to a shaping buffer.
 *
 * ## The one rule
 *
 * **GSUB changes glyph identity and glyph sequence structure. It never changes the text.** The source is a
 * `RoboTextView` that this module only reads; what a lookup rewrites is a @ref RoboTextShapingBuffer, whose
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
 * four limits in @ref RoboTextOtLimits bound the work a hostile table can ask for.
 */
#ifndef ROBO_TEXT_GSUB_H
#define ROBO_TEXT_GSUB_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontOtCommon.h"
#include "RoboText/RoboTextBidi.h"
#include "RoboText/RoboTextOtLayout.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/**
 * Resolve @p features against @p gsub — the plan the substitution engine then applies.
 *
 * The shared layout core's resolution (`RoboTextOtPlanResolve`) with **GSUB's** defaults for @p script
 * handed in, which is the whole of this function: a caller that substitutes names a script and its
 * features and gets the lookups the font turns on, without either table knowing about the other.
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
                               RoboTextOtPlan *out,
                               RoboTextDiag *diag);

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
    RoboTextShapingGlyphSynthetic = 1u << 7,
    /**
     * The face's GPOS moved this glyph: a non-zero adjustment is on it (Phase 12.11).
     *
     * A separate bit from the four adjustment fields being non-zero, because "the font had an opinion here"
     * and "the opinion happened to be zero" are different answers: a pair whose value record exists but
     * says `0` is a font that positioned the glyphs, and a diagnostic that conflated the two would report a
     * kerning table as absent.
     *
     * The bit is 8 rather than the next free low bit so that the flags a **substitution** sets keep their
     * values across the positioning stage — a reader comparing `flags` before and after positioning sees
     * only the addition of this one.
     */
    RoboTextShapingGlyphPositioned = 1u << 8
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
    /**
     * The **character** the glyph came from, or 0 when its source covers more than one scalar.
     *
     * The scalar *index* is @ref source, and this is the code point at it: the two are different facts and a
     * reader needs both (an index addresses the text, a code point is what the character is). It is carried
     * because a glyph's properties — whether it is a combining mark, above all — are properties of the
     * character, and after Phase 12.10's rebuild the text is no longer at hand to decode.
     *
     * 0 for a glyph whose source covers several scalars, which is the honest "no single character": a ligature
     * is `ffi` and not any one of its three. The two readings of 0 — "several scalars" and "the character really
     * is U+0000" — are told apart by @ref source's own length, which is always there.
     */
    RoboTextCodePoint codePoint;
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
    /**
     * Where the glyph sits relative to the pen, from the face's GPOS, in device pixels (Phase 12.11).
     *
     * An **adjustment**, not a position: the pen's own place is @ref RoboTextGlyphRunAdvanceUpTo's answer,
     * and a caller adds the two. Zero for a glyph the font said nothing about, which is every glyph of a
     * face with no GPOS and every glyph of a run whose plan turned none of its lookups on.
     */
    float offsetAdjustX;
    /** See @ref offsetAdjustX. */
    float offsetAdjustY;
    /**
     * How much further the pen moves after this glyph, from the face's GPOS, in device pixels.
     *
     * **In addition to** the face's own `hmtx` advance, never instead of it: the run build adds the two, so
     * a kerning pair is `advance = hmtx + kern` and no reader has to know which of the two moved.
     */
    float advanceAdjustX;
    /** See @ref advanceAdjustX. */
    float advanceAdjustY;
} RoboTextShapingGlyph;

/** Clear @p glyph to a null glyph: id 0, empty source, level none, no flags. Safe on NULL. */
void RoboTextShapingGlyphInit(RoboTextShapingGlyph *glyph);

/** Non-zero when @p glyph names an id and a non-empty source range. Safe on NULL. */
int RoboTextShapingGlyphIsValid(const RoboTextShapingGlyph *glyph);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextShapingGlyphEqual(const RoboTextShapingGlyph *a, const RoboTextShapingGlyph *b);

typedef struct RoboTextShapingBuffer RoboTextShapingBuffer;

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
RoboTextShapingBuffer *RoboTextShapingBufferCreate(size_t capacity);

/** Append @p glyph, growing if needed. @return RoboOk, or RoboInvalidArgument for a glyph with no source. */
Result RoboTextShapingBufferAppend(RoboTextShapingBuffer *buffer, const RoboTextShapingGlyph *glyph);

/** Glyphs held. */
size_t RoboTextShapingBufferCount(const RoboTextShapingBuffer *buffer);

/** Glyph @p index, or NULL when out of range. The pointer is invalidated by any mutation. */
const RoboTextShapingGlyph *RoboTextShapingBufferAt(const RoboTextShapingBuffer *buffer, size_t index);

/** Mutable glyph @p index, or NULL when out of range. For a caller building a buffer by hand. */
RoboTextShapingGlyph *RoboTextShapingBufferAtMutable(RoboTextShapingBuffer *buffer, size_t index);

/**
 * Replace glyph @p index's id, keeping its provenance — the single-substitution primitive.
 *
 * Kept separate from "write the whole record" because that is what a single substitution *is*: the id
 * changes and nothing else does, and a caller that had to reconstruct the record could lose `source` or the
 * level without noticing.
 */
Result RoboTextShapingBufferSetGlyph(RoboTextShapingBuffer *buffer, size_t index, GlyphId glyphId);

/**
 * Replace @p count glyphs starting at @p index with @p replacement.
 *
 * The one mutation a multiple substitution and a ligature share, and the reason both work: the replacement
 * array is copied in, `count` need not equal @p replacementCount (that is the whole point), and a
 * replacement of zero glyphs deletes. Refuses an empty replacement array for a non-empty @p count — a
 * deletion is expressed with `replacementCount == 0` and a NULL array, which says what it means.
 */
Result RoboTextShapingBufferReplace(RoboTextShapingBuffer *buffer,
                                 size_t index,
                                 size_t count,
                                 const RoboTextShapingGlyph *replacement,
                                 size_t replacementCount);

/** Release @p buffer. Safe on NULL. */
void RoboTextShapingBufferDestroy(RoboTextShapingBuffer *buffer);

/* ------------------------------------------------------------------ */
/* Limits, options, stats                                              */
/* ------------------------------------------------------------------ */


/* Declared before the options because they point at one; defined below. */
typedef struct RoboTextGsubEvent RoboTextGsubEvent;

/** What a call may be told beyond the plan. A zeroed struct is the default behaviour. */
typedef struct RoboTextGsubOptions {
    /** The run's direction. A lookup whose flag asks for one direction is applied only to it. */
    RoboTextDirection direction;
    /** The run's resolved bidi level, carried onto every glyph the call produces. */
    RoboTextBidiLevel bidiLevel;
    /** The limits for this call; NULL uses @ref RoboTextOtDefaultLimits. */
    const RoboTextOtLimits *limits;
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
    /**
     * Subtable offsets that pointed at no subtable at all: a zero offset, which OpenType states against the
     * lookup table and therefore resolves onto the lookup's own header, or any offset inside that header.
     * Counted once per lookup — the defect is the font's, and the number should not follow the text — and not
     * silently stepped over, because a font that says nothing here is a font worth being told about.
     */
    size_t subtablesMalformed;
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
    /** Probes the call spent (against @ref RoboTextOtLimits::maxGlyphProbes). */
    uint32_t probes;
    /** Non-zero when a limit stopped something, so a caller can tell a limit from a font that ended. */
    int limitReached;
} RoboTextGsubStats;

/** Clear @p stats. Safe on NULL. */
void RoboTextGsubStatsInit(RoboTextGsubStats *stats);

/**
 * Run the plan over @p buffer.
 *
 * The whole engine, and its order is the font's: the plan's @ref RoboTextOtPlan::lookupOrder is walked
 * once, each lookup's subtables are tried in order at each position, and a position whose match produced a
 * substitution continues *after* what the lookup produced — never re-matching the output with the same
 * lookup, which is the recursion the phase warns about and which no OpenType lookup asks for. A lookup that
 * fails, an unsupported type and a malformed subtable all **skip** (counted in @ref
 * RoboTextGsubStats::lookupsSkipped) and shaping continues with the glyphs produced so far: nothing here can
 * fail a run of text.
 *
 * @param plan a plan from @ref RoboTextOtPlanResolve; an empty plan applies nothing.
 * @param options may be NULL (the defaults).
 * @return RoboOk; RoboInvalidArgument for a NULL @p buffer or @p stats; RoboOutOfMemory when the buffer
 *         could not grow (the buffer is left holding what it had).
 */
Result RoboTextGsubApply(const FontOtLayout *gsub,
                         const FontGdef *gdef,
                         const RoboTextOtPlan *plan,
                         const RoboTextGsubOptions *options,
                         RoboTextShapingBuffer *buffer,
                         RoboTextGsubStats *outStats,
                         RoboTextDiag *diag);

/** One line per counter, plus the glyph counts. Complete-or-absent. */
Result RoboTextGsubStatsWrite(const RoboTextGsubStats *stats,
                              char *buffer,
                              size_t capacity,
                              size_t *outLength);

/** One line per glyph (`"#0 gid=42 src=[0,1) cluster=0 lvl=0 flags=0x1 from=41"`). Complete-or-absent. */
Result RoboTextShapingBufferWrite(const RoboTextShapingBuffer *buffer,
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
