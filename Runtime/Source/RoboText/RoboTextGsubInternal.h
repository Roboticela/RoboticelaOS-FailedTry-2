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
 * context keeps the last @ref RoboTextGsubCacheSlots parsed objects, keyed by absolute offset, and frees
 * them when the call ends. A shaping call therefore parses each coverage it touches once, which is the
 * property that matters, while the *authoritative* parse of the table's structure (ScriptList, FeatureList,
 * LookupList) stays where the phase asks for it: cached on the face, shared by every call, immutable.
 *
 * Everything the subtable readers need is bounds-checked through `FontReader` (`RoboFontBinary.h`) and
 * offset-resolved through `FontOtResolveOffset16/32`, so a malformed font cannot make either file read
 * outside the table.
 */
#ifndef ROBO_TEXT_GSUB_INTERNAL_H
#define ROBO_TEXT_GSUB_INTERNAL_H

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontOtCommon.h"
#include "RoboText/RoboTextGsub.h"
#include "RoboTextInternal.h"

/** Parsed coverage/class definitions one call keeps at once. Eight covers every real lookup. */
#define RoboTextGsubCacheSlots 8u

/** One cached parse: a coverage **or** a class definition, keyed by its absolute offset. */
typedef struct RoboTextGsubCacheEntry {
    size_t offset;
    /** 1 = coverage, 2 = class definition. */
    int kind;
    FontOtCoverage coverage;
    FontOtClassDef classDef;
} RoboTextGsubCacheEntry;

/** The state one shaping call carries while a lookup runs. */
typedef struct RoboTextGsubCall {
    const FontOtLayout *gsub;
    const FontGdef *gdef;
    const RoboTextGsubPlan *plan;
    const RoboTextGsubLimits *limits;
    RoboTextGsubBuffer *buffer;
    RoboTextGsubStats stats;
    RoboTextGsubEvent *trace;
    size_t traceCapacity;
    size_t traceCount;
    RoboTextDirection direction;
    RoboTextBidiLevel bidiLevel;
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
    RoboTextGsubCacheEntry cache[RoboTextGsubCacheSlots];
    size_t cacheCount;
} RoboTextGsubCall;

/* ---- The cache ---------------------------------------------------- */

/** Coverage at absolute @p offset, parsed once per call, or NULL when the table is malformed. */
const FontOtCoverage *RoboTextGsubCacheCoverage(RoboTextGsubCall *apply, size_t offset);

/** Class definition at absolute @p offset, parsed once per call, or NULL. */
const FontOtClassDef *RoboTextGsubCacheClassDef(RoboTextGsubCall *apply, size_t offset);

/** Free every cached parse. Safe to call twice; the context is not usable for further lookups after. */
void RoboTextGsubCacheClear(RoboTextGsubCall *apply);

/* ---- Matching helpers --------------------------------------------- */

/** Non-zero when @p glyphId is covered by the coverage at absolute @p offset; @p outIndex is its index. */
int RoboTextGsubCovered(RoboTextGsubCall *apply, size_t offset, GlyphId glyphId, uint16_t *outIndex);

/** The class of @p glyphId in the class definition at @p offset (0 when it is not listed). */
uint16_t RoboTextGsubClassOf(RoboTextGsubCall *apply, size_t offset, GlyphId glyphId);

/**
 * Non-zero when the **current lookup's** flags say @p glyphId does not participate in matching.
 *
 * The four answers OpenType defines, in the order the flags ask them: `IgnoreBaseGlyphs`,
 * `IgnoreLigatures`, `IgnoreMarks` (from GDEF's glyph class definition), and `UseMarkFilteringSet`/
 * `markAttachmentType` (marks that belong to another set). A glyph that is not ignored is matched; an
 * ignored glyph is skipped *over* — never removed, never hidden from the buffer, which is the phase's
 * §"ignored glyphs" and what keeps a combining mark in the text a lookup steps past.
 */
int RoboTextGsubSkipped(RoboTextGsubCall *apply, size_t index);

/**
 * The next buffer index at or after @p index whose glyph is not skipped, or the buffer's count when there
 * is none. Matching walks with this rather than with `index + 1`.
 *
 * Not `const`: the answer is a class lookup, and asking it may fill the call's coverage cache — which is a
 * cache of *the font's* data and changes no text, but it is state the call owns either way.
 */
size_t RoboTextGsubNextParticipating(RoboTextGsubCall *apply, size_t index);

/** The buffer index @p steps participating glyphs **before** @p index, or the buffer's count when there is
 *  none (a sentinel that is never a valid index, so a caller can test it). */
size_t RoboTextGsubPrevParticipating(RoboTextGsubCall *apply, size_t index, size_t steps);

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
#define RoboTextGsubMaxMatchGlyphs 64u

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
 *         @ref RoboTextGsubCall::limitReached is set).
 */
int RoboTextGsubSpendProbes(RoboTextGsubCall *apply, uint32_t count);

/** Non-zero when a buffer holding @p after glyphs would exceed @ref RoboTextGsubLimits::maxGlyphs. */
int RoboTextGsubExceedsGlyphLimit(const RoboTextGsubCall *apply, size_t after);

/* ---- Subtable application ----------------------------------------- */

/**
 * Apply the subtable at absolute @p offset, whose lookup type is @p lookupType, at buffer position @p index.
 *
 * @param outNext receives the position the scan continues at when this returns 1.
 * @return 1 when the subtable substituted at @p index; 0 when it did not match (or could not be read, which
 *         is a skip rather than a failure: the caller records it and tries the next subtable).
 */
int RoboTextGsubApplySubtable(RoboTextGsubCall *apply,
                              uint16_t lookupType,
                              size_t offset,
                              size_t index,
                              size_t *outNext);

/**
 * Apply lookup @p lookupIndex at @p position **without scanning** — what a contextual subtable does with its
 * substitution records. Bounded by @ref RoboTextGsubLimits::maxNesting and by the application counter.
 *
 * @return 1 when the nested lookup substituted; 0 when it did not, or when the limits refused it.
 */
int RoboTextGsubApplyLookupAt(RoboTextGsubCall *apply,
                              uint16_t lookupIndex,
                              size_t position,
                              size_t *outNext);

/** Record a substitution event, if the caller asked for tracing. Safe on NULL trace. */
void RoboTextGsubTrace(RoboTextGsubCall *apply,
                       size_t position,
                       const GlyphId *from,
                       size_t fromCount,
                       const GlyphId *to,
                       size_t toCount,
                       RoboTextRange source);

/** The current lookup's `RightToLeft` flag, honoured against the run's own direction. */
int RoboTextGsubLookupAppliesToDirection(const RoboTextGsubCall *apply);

#endif /* ROBO_TEXT_GSUB_INTERNAL_H */
