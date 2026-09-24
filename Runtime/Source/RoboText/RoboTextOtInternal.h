/*
 * The GSUB engine's internal machinery (Phase 12.10) — not a public header.
 *
 * Two things live here that `RoboTextGsub.h` has no business naming: the **apply context** (the state one
 * shaping call carries: the tables, the plan's current lookup, the limits, the counters and the trace) and
 * the **parsed-subtable cache** that keeps a coverage or a class definition from being parsed once per
 * position.
 *
 * The cache is the file's one design decision worth arguing. Phase 5 owns Coverage and ClassDef parsing
 * (`FontOtCoverageParse`, `FontOtClassDefParse`) and this module must not write a second one, but those
 * doors *allocate*: calling them per probe would parse a table thousands of times for one word. So an apply
 * context keeps the last @ref RoboTextOtCacheSlots parsed objects, keyed by absolute offset, and frees
 * them when the call ends. A shaping call therefore parses each coverage it touches once, which is the
 * property that matters, while the *authoritative* parse of the table's structure (ScriptList, FeatureList,
 * LookupList) stays where the phase asks for it: cached on the face, shared by every call, immutable.
 *
 * Everything the subtable readers need is bounds-checked through `FontReader` (`RoboFontBinary.h`) and
 * offset-resolved through `FontOtResolveOffset16/32`, so a malformed font cannot make either file read
 * outside the table.
 */
#ifndef ROBO_TEXT_OT_INTERNAL_H
#define ROBO_TEXT_OT_INTERNAL_H

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontOtCommon.h"
#include "RoboText/RoboTextGsub.h"
#include "RoboText/RoboTextGpos.h"
#include "RoboTextInternal.h"

/** Parsed coverage/class definitions one call keeps at once. Eight covers every real lookup. */
#define RoboTextOtCacheSlots 8u

/** One cached parse: a coverage **or** a class definition, keyed by its absolute offset. */
typedef struct RoboTextOtCacheEntry {
    size_t offset;
    /** 1 = coverage, 2 = class definition. */
    int kind;
    FontOtCoverage coverage;
    FontOtClassDef classDef;
} RoboTextOtCacheEntry;

/** The state one shaping call carries while a lookup runs. */
typedef struct RoboTextOtCall {
    const FontOtLayout *layout;
    const FontGdef *gdef;
    const RoboTextOtPlan *plan;
    const RoboTextOtLimits *limits;
    RoboTextShapingBuffer *buffer;
    RoboTextGsubStats stats;
    RoboTextGsubEvent *trace;
    size_t traceCapacity;
    size_t traceCount;
    RoboTextDirection direction;
    RoboTextBidiLevel bidiLevel;
    /*
     * The strategy's **position masks** (Phase 12.19), or NULL when the caller supplied none.
     *
     * `positionMasks[i]` is the mask word of the scalar `maskBase + i`, built by the strategy that owns the
     * script (`RoboTextArabicMasksBuild`, `RoboTextIndicMasksBuild`), and `plan->lookupMasks[lookup]` is the
     * word of the stage feature whose lookup it is. A lookup runs at a position only when the two intersect,
     * which is how "the font's `init` here and `medi` there" is a fact about *positions* rather than a second
     * substitution pass.
     *
     * Both engines read them and neither owns them: the masks are the strategy's knowledge, the plan's array
     * is where they are declared, and this record is how they reach the loop that asks the question.
     */
    const uint32_t *positionMasks;
    size_t positionMaskCount;
    RoboTextScalarIndex maskBase;
    /** The lookup being applied, and the feature whose plan named it (for the trace's `feature` bytes). */
    const FontOtLookup *lookup;
    uint16_t lookupIndex;
    char featureTag[4];
    /** The lookup's type with an extension already unwrapped. */
    uint16_t lookupType;
    /** Nesting depth: 0 for a lookup the plan named, +1 per contextual invocation. */
    uint32_t depth;
    /** Set when the buffer could not grow: the call stops applying and reports it. */
    int failed;
    /** Set when a limit stopped work: counted, and the lookup abandoned. */
    int limitReached;
    /*
     * The counters below belong to whichever engine owns the call. A call is one engine's call at a time —
     * substitution *then* positioning, never inside each other — so the record carries one set of counters
     * per engine instead of a union, which is what keeps both engines' readers type-checked.
     */
    /**
     * Device pixels per em for device-table corrections, and device pixels per **font unit** for value
     * records (Phase 12.11). 0 in either means "unknown": a device delta is then not evaluated and a record
     * is not applied at all, each counted, because a number in the wrong unit is worse than no number.
     */
    uint16_t devicePpem;
    float deviceScale;
    /**
     * The face context a **cursive or mark attachment** places a glyph against (Phase 12.19), or NULL.
     *
     * The one place this engine asks a font anything: an attachment puts the mark where the base's anchor
     * is, and the pen's distance between the two is the face's advance — a number GPOS does not carry. NULL
     * means the caller supplied none, and then a lookup of types 3–6 is counted unsupported rather than
     * attached to a distance nobody knows.
     */
    const RoboTextFontContext *gposFont;
    /**
     * The run's **writing mode** (Phase 12.20), which decides which advance the pen distance is.
     *
     * `RoboTextWritingHorizontal` by default, and only the attachment readers read it: the pen's step from
     * one glyph to the next is the run's *inline* advance, so a vertical mark is placed against `vmtx`
     * rather than against an `hmtx` number the vertical pen never moves on.
     */
    RoboTextWritingMode writingMode;
    /**
     * Non-zero when the **positioning** engine owns this call.
     *
     * A call is one engine's call at a time, and the shared helpers that charge the call's budget need to know
     * whose counters to charge: `RoboTextOtSpendProbes` keeps the probe count in the owning engine's own
     * statistics record, because a stat that reports one engine's work while the other did it is worse than no
     * stat at all.
     */
    int positioning;
    RoboTextGposStats gposStats;
    RoboTextGposEvent *gposTrace;
    size_t gposTraceCapacity;
    size_t gposTraceCount;
    RoboTextOtCacheEntry cache[RoboTextOtCacheSlots];
    size_t cacheCount;
} RoboTextOtCall;

/* ---- The two engines' subtable doors ------------------------------ */

/**
 * Apply the **positioning** subtable at absolute @p offset, whose lookup type is @p lookupType, at buffer
 * position @p index. The GPOS half of @ref RoboTextGsubApplySubtable: same contract, different readers.
 *
 * @param outNext receives the position the positioning scan continues at (always @p index + 1, because
 *                positioning never changes the glyph count).
 * @return 1 when the subtable adjusted something at @p index; 0 when it did not match or could not be read.
 */
int RoboTextGposApplySubtable(RoboTextOtCall *apply,
                             uint16_t lookupType,
                             size_t offset,
                             size_t index,
                             size_t *outNext);

/**
 * Non-zero when this build executes positioning lookup type @p type.
 *
 * Types 3–8 (cursive attachment, the three mark attachments and the two contextual forms) are recognised by
 * Phase 5 and **not** executed here: the engine counts them and shapes past them rather than reading bytes
 * as a format they are not. `Documentation/Runtime/Text/Positioning.md` names the goal that adds each.
 */
int RoboTextGposTypeIsExecuted(uint16_t type);

/** The current lookup's `RightToLeft` flag, honoured against the run's own direction. Shared by both engines. */
int RoboTextOtLookupAppliesToDirection(const RoboTextOtCall *apply);

/**
 * Non-zero when lookup @p lookupIndex may run **at buffer position @p index** (Phase 12.19).
 *
 * The positional test the complex-script strategies need, and the whole of it is one AND: the lookup's mask
 * word from the plan's `lookupMasks`, and the mask word of the *source scalar* the glyph at @p index came
 * from. `RoboTextComplexMaskAll` (0) on either side means "everywhere", so a plan that names no masks —
 * which is every Latin, Greek and Cyrillic run, and every stage whose features are unmasked — asks this and
 * gets `1` without a mask being read at all. That is why the test can sit in the scan loop of both engines
 * without costing a plain run anything.
 *
 * Two answers that look like failures and are not: a glyph whose source starts **before** the mask array
 * (a caller shaped a range and wrote glyphs outside it) and a glyph past its end both answer `0` — the
 * lookup does not run — rather than reading a mask that belongs to another scalar. A lookup that should have
 * run in such a case is a caller that built the two from different ranges, and the refusal is the honest
 * reading of a contradiction rather than a guess.
 */
int RoboTextOtMaskAllows(const RoboTextOtCall *apply, uint16_t lookupIndex, size_t index);

/** Record a positioning event, if the caller asked for tracing. Safe on NULL trace. */
void RoboTextGposTrace(RoboTextOtCall *apply,
                       size_t position,
                       size_t secondPosition,
                       const RoboTextGposValue *first,
                       const RoboTextGposValue *second,
                       uint16_t secondGlyph,
                       uint16_t firstClass,
                       uint16_t secondClass);

/* ---- The four reads every reader below goes through --------------- */

/**
 * A big-endian field at @p offset, bounds-checked against @p table before a byte is read.
 *
 * Phase 5 owns the reading (`FontReadU16be` and `FontCheckedRange` in `RoboFontBinary.h`); these are the
 * two-line wrappers that make an OpenType field readable without a `FontReader` at every call site, and
 * they are shared by both engines' readers rather than written twice.
 *
 * **Always called through the `GsubU16`/`GposU16` macros, never directly**: these answer a `Result` and
 * `RoboOk` is zero, so `if (!RoboTextOtReadU32(...))` inverts the question. That mistake hid
 * single-substitution format 1, the extension wrapper and GDEF's mark filtering set once; the macros make
 * the mistake unrepresentable.
 */
Result RoboTextOtReadU16(FontSpan table, size_t offset, uint16_t *out);
Result RoboTextOtReadI16(FontSpan table, size_t offset, int16_t *out);
Result RoboTextOtReadU32(FontSpan table, size_t offset, uint32_t *out);

/**
 * @p offset resolved against @p base, or 0 when the field is 0.
 *
 * A zero offset is OpenType's "there is no such table here" in every format below, and Phase 5's resolver
 * cannot express it: it would answer @p base itself, which is a real location in the parent and would then
 * be parsed as a coverage table. So absent is answered as absent.
 */
size_t RoboTextOtOffset16(FontSpan table, size_t base, uint16_t offset);
size_t RoboTextOtOffset32(FontSpan table, size_t base, uint32_t offset);

/** The feature whose plan first named @p lookupIndex, for a trace's `feature` bytes, or spaces. */
const char *RoboTextOtFeatureForLookup(const RoboTextOtPlan *plan, uint16_t lookupIndex);

/* ---- The cache ---------------------------------------------------- */

/** Coverage at absolute @p offset, parsed once per call, or NULL when the table is malformed. */
const FontOtCoverage *RoboTextOtCacheCoverage(RoboTextOtCall *apply, size_t offset);

/** Class definition at absolute @p offset, parsed once per call, or NULL. */
const FontOtClassDef *RoboTextOtCacheClassDef(RoboTextOtCall *apply, size_t offset);

/** Free every cached parse. Safe to call twice; the context is not usable for further lookups after. */
void RoboTextOtCacheClear(RoboTextOtCall *apply);

/* ---- Matching helpers --------------------------------------------- */

/** Non-zero when @p glyphId is covered by the coverage at absolute @p offset; @p outIndex is its index. */
int RoboTextOtCovered(RoboTextOtCall *apply, size_t offset, GlyphId glyphId, uint16_t *outIndex);

/** The class of @p glyphId in the class definition at @p offset (0 when it is not listed). */
uint16_t RoboTextOtClassOf(RoboTextOtCall *apply, size_t offset, GlyphId glyphId);

/**
 * Non-zero when the **current lookup's** flags say @p glyphId does not participate in matching.
 *
 * The four answers OpenType defines, in the order the flags ask them: `IgnoreBaseGlyphs`,
 * `IgnoreLigatures`, `IgnoreMarks` (from GDEF's glyph class definition), and `UseMarkFilteringSet`/
 * `markAttachmentType` (marks that belong to another set). A glyph that is not ignored is matched; an
 * ignored glyph is skipped *over* — never removed, never hidden from the buffer, which is the phase's
 * §"ignored glyphs" and what keeps a combining mark in the text a lookup steps past.
 */
int RoboTextOtSkipped(RoboTextOtCall *apply, size_t index);

/**
 * The next buffer index at or after @p index whose glyph is not skipped, or the buffer's count when there
 * is none. Matching walks with this rather than with `index + 1`.
 *
 * Not `const`: the answer is a class lookup, and asking it may fill the call's coverage cache — which is a
 * cache of *the font's* data and changes no text, but it is state the call owns either way.
 */
size_t RoboTextOtNextParticipating(RoboTextOtCall *apply, size_t index);

/** The buffer index @p steps participating glyphs **before** @p index, or the buffer's count when there is
 *  none (a sentinel that is never a valid index, so a caller can test it). */
size_t RoboTextOtPrevParticipating(RoboTextOtCall *apply, size_t index, size_t steps);

/**
 * Non-zero when @p offset lands inside @p lookup's **own header** — six bytes, plus the mark filtering
 * set — and therefore points at no subtable at all.
 *
 * OpenType states a lookup's subtable offsets against the lookup table itself, so the format's "there is no
 * subtable here" (a zero offset) resolves to the lookup's first byte rather than to nothing: Phase 5's
 * resolver cannot express absent, and reading the header as a coverage offset is how a table that means
 * nothing becomes a table that means garbage. This is the check that stops it, and it is the engine's
 * because a font may mean it for one lookup while the rest of its lookups are fine — refusing the whole
 * table, which is what a parser can do, would throw away a document's kerning over one bad record.
 */
int RoboTextOtSubtableInsideLookup(const FontOtLookup *lookup, size_t offset);

/* ---- Lookup types and the two bounds ------------------------------ */

/**
 * The GSUB lookup types, by the number a font stores.
 *
 * Named here rather than written as literals because a switch on a number is where a reader stops being
 * able to tell a typo from a type. @ref RoboTextGsubLookupExtension is 7: an extension lookup is a wrapper
 * whose whole content is "the real type is X and the real subtable is elsewhere" (Phase 12.10 §"extension
 * lookups"), which is why it unwraps inside @ref RoboTextGsubApplySubtable instead of being a lookup of its
 * own.
 */
#define RoboTextGsubLookupSingle 1u
#define RoboTextGsubLookupMultiple 2u
#define RoboTextGsubLookupAlternate 3u
#define RoboTextGsubLookupLigature 4u
#define RoboTextGsubLookupContext 5u
#define RoboTextGsubLookupChainingContext 6u
#define RoboTextGsubLookupExtension 7u
#define RoboTextGsubLookupReverseChaining 8u
#define RoboTextGsubLookupTypeCount 8u

/**
 * Glyphs one match may span — a lookup's input pattern, or a ligature's component list.
 *
 * A bound rather than a convenience: the readers hold the positions they matched on the stack, so this is
 * the array size, and it is part of the limits policy because a font states these counts and can state a
 * large one. It is 64 because the longest real pattern in any shipped font is a handful: a table that asks
 * for more is refused and counted rather than trusted.
 */
#define RoboTextOtMaxMatchGlyphs 64u

/*
 * `FontGsubLookupTypeIsKnown` (RoboFontOt.h, Goal 5.4) is the "is this a type at all" question, and Phase 5
 * owns it: it answers 1..8, which is every GSUB lookup type including the extension wrapper. This module
 * therefore has one dispatch question left — "does *this* build execute it" — and the answer is that it
 * executes all eight, each with a reader below. A future type (9, if the registry grows one) would be
 * rejected by Phase 5 first, and the engine would count it as a skip rather than guess.
 */

/**
 * Charge @p count probes against the call's budget.
 *
 * Every glyph comparison the readers make goes through this, and it is the bound that keeps a font from
 * turning one word into quadratic work: a context table whose pattern is long and whose text is longer is
 * the shape a hostile font attacks with, and this is where it stops.
 *
 * @return 1 when the call may keep matching, 0 when the budget is spent (the lookup is abandoned and
 *         @ref RoboTextOtCall::limitReached is set).
 */
int RoboTextOtSpendProbes(RoboTextOtCall *apply, uint32_t count);

/** Non-zero when a buffer holding @p after glyphs would exceed @ref RoboTextOtLimits::maxGlyphs. */
int RoboTextOtExceedsGlyphLimit(const RoboTextOtCall *apply, size_t after);

/* ---- Subtable application ----------------------------------------- */

/**
 * Apply the subtable at absolute @p offset, whose lookup type is @p lookupType, at buffer position @p index.
 *
 * @param outNext receives the position the scan continues at when this returns 1.
 * @return 1 when the subtable substituted at @p index; 0 when it did not match (or could not be read, which
 *         is a skip rather than a failure: the caller records it and tries the next subtable).
 */
int RoboTextGsubApplySubtable(RoboTextOtCall *apply,
                              uint16_t lookupType,
                              size_t offset,
                              size_t index,
                              size_t *outNext);

/**
 * Apply lookup @p lookupIndex at @p position **without scanning** — what a contextual subtable does with its
 * substitution records. Bounded by @ref RoboTextOtLimits::maxNesting and by the application counter.
 *
 * @return 1 when the nested lookup substituted; 0 when it did not, or when the limits refused it.
 */
int RoboTextGsubApplyLookupAt(RoboTextOtCall *apply,
                              uint16_t lookupIndex,
                              size_t position,
                              size_t *outNext);

/** Record a substitution event, if the caller asked for tracing. Safe on NULL trace. */
void RoboTextGsubTrace(RoboTextOtCall *apply,
                       size_t position,
                       const GlyphId *from,
                       size_t fromCount,
                       const GlyphId *to,
                       size_t toCount,
                       RoboTextRange source);

/** The current lookup's `RightToLeft` flag, honoured against the run's own direction. */
int RoboTextOtLookupAppliesToDirection(const RoboTextOtCall *apply);

#endif /* ROBO_TEXT_OT_INTERNAL_H */
