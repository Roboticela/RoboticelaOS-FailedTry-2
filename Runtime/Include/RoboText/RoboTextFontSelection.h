/*
 * RoboText's font selection and coverage (Phase 12.5) — the policy that turns "a requested setting plus a
 * code point" into "the face this build will use, and why".
 *
 * ## Where discovery ends and selection begins
 *
 * RoboFont owns discovery, loading, the registry and — at Phase 5 — coverage and fallback *resolvers*.
 * RoboText owns **policy**: which of the faces a caller offers is chosen for a given scalar, in which
 * order, and what identity the choice is recorded as. So the selector never scans a directory, never opens
 * a file and never names `/System/Fonts/`; it is handed a **primary context** and an ordered **fallback
 * chain** of candidate faces, and its whole job is a deterministic walk of that order under one hard gate:
 *
 *     a candidate is eligible for a code point only if the face has a usable glyph for it.
 *
 * That gate is the difference between the 12.4 behaviour and this one. 12.4's generator saw one face and
 * counted the misses; 12.5 turns a miss into a *reason to try the next candidate*, and records which
 * candidate answered.
 *
 * ## Selection order, and why it is data
 *
 *     requested family (the primary context)
 *           ↓ requested weight / style / stretch   (already resolved by whoever built the context)
 *           ↓ coverage check for the requested code point
 *           ↓ fallback family, in the chain's own order
 *           ↓ system fallback (the last chain entries — the caller's own tail)
 *
 * Nothing here is hard-coded and nothing is random: the chain's order *is* the policy, which is what makes
 * a fallback change a data change and a determinism test meaningful. `RoboTextResolvedFontCompare` gives a
 * total order over the results, so "same text, same configuration, same database ⇒ same selection" is a
 * thing a test can assert field by field rather than by pointer identity.
 *
 * ## Coverage is cached, and by the layer that already caches it
 *
 * `FontFaceParseCoverage` is RoboFont's lazy, idempotent coverage build: the first query parses the `cmap`
 * once and every later query reads an immutable range table. This module therefore does **not** need a
 * coverage database of its own, and adding one would be a second thing to invalidate. What the selector
 * adds is the cheap part a *text run* actually costs: a small direct-mapped memo of "this face, this code
 * point → covered?", which keeps a repeated query — the same space between two words, the same digits in a
 * column — from walking the range table again. Its counters are exposed so a test can prove the memo is
 * doing something rather than merely existing.
 *
 * ## Font identity, not font files
 *
 * `RoboTextResolvedFont` is the identity a run carries: the *face id* (stable while the face lives, from
 * Phase 5), the borrowed face pointer for the immediate caller, the requested rendering parameters, and
 * the provenance — primary or which fallback candidate answered. It copies no font data: a run that
 * embedded a face would make every copy a lifetime question, which is the same argument
 * `RoboTextFontContext` already makes one header over.
 */
#ifndef ROBO_TEXT_FONT_SELECTION_H
#define ROBO_TEXT_FONT_SELECTION_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontCoverage.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextFont.h"
#include "RoboText/RoboTextTypes.h"
#include "RoboText/RoboTextUnicode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Resolution                                                          */
/* ------------------------------------------------------------------ */

/** Where a selected face came from. */
typedef enum RoboTextFontSource {
    /** No candidate covered the code point: the honest answer, and not a failure. */
    RoboTextFontSourceNone = 0,
    /** The requested primary face. */
    RoboTextFontSourcePrimary = 1,
    /** A candidate from the fallback chain; `candidateIndex` says which. */
    RoboTextFontSourceFallback = 2
} RoboTextFontSource;

/** `candidateIndex` for a result that came from no candidate. */
#define RoboTextFontNoCandidate ((size_t)-1)

/** Non-zero if @p source is a defined enumerator. */
int RoboTextFontSourceIsKnown(RoboTextFontSource source);

/** Lower-case name (`"none"`, `"primary"`, `"fallback"`); never NULL. */
const char *RoboTextFontSourceName(RoboTextFontSource source);

/**
 * One face offered to the selector, with the family name it was resolved under.
 *
 * `family` is a borrowed label for diagnostics and tests; it is **not** consulted during selection, because
 * selection is by coverage and order rather than by name (that is the font database's job, and RoboText
 * naming a family string would be the module reaching around it). A NULL `family` reads as `""`.
 *
 * `weight` 0 and `stretch` 0 mean "the candidate does not declare one"; the selector then records the
 * requested primary setting, so a chain entry that only adds coverage does not silently claim a weight.
 */
typedef struct RoboTextFontCandidate {
    const char *family;
    FontFace *face;
    FontWeight weight;
    FontStyle style;
    FontStretch stretch;
} RoboTextFontCandidate;

/**
 * The ordered fallback chain, borrowed.
 *
 * A value, not a handle: the caller owns the array (typically the font database's own list) and the
 * selector only reads it. An empty chain is legal and means "primary or nothing" — the 12.4 behaviour,
 * expressed as data rather than as a special case.
 */
typedef struct RoboTextFontFallback {
    const RoboTextFontCandidate *entries;
    size_t count;
} RoboTextFontFallback;

/** The identity of a selected font: what a run is set in, and where it came from. */
typedef struct RoboTextResolvedFont {
    /** The face's stable identity (0 for "no face"). */
    FontFaceId faceId;
    /** The borrowed face; NULL exactly when @ref source is `RoboTextFontSourceNone`. */
    FontFace *face;
    /** The family label the candidate carried, or `""`. Never NULL for a resolved record. */
    const char *family;
    /** The requested size, in CSS pixels. */
    float sizePx;
    /** The weight the selection is recorded at. */
    FontWeight weight;
    /** The style the selection is recorded at. */
    FontStyle style;
    /** The stretch the selection is recorded at. */
    FontStretch stretch;
    /** Which candidate answered. */
    RoboTextFontSource source;
    /** 0 for the primary, `i + 1` for chain entry @p i, @ref RoboTextFontNoCandidate for none. */
    size_t candidateIndex;
    /** Non-zero when the selection covers everything it was asked about. */
    int complete;
} RoboTextResolvedFont;

/** Clear @p font to "nothing selected". Safe on NULL. */
void RoboTextResolvedFontInit(RoboTextResolvedFont *font);

/** Non-zero when @p font names a face. Safe on NULL. */
int RoboTextResolvedFontIsSet(const RoboTextResolvedFont *font);

/**
 * Field-for-field equality over the **identity** fields: face id, the four rendering parameters and the
 * provenance. The borrowed pointer is deliberately not compared — two records naming the same face must be
 * equal even if one was copied through a different handle, and the run-splitting walk depends on it.
 */
int RoboTextResolvedFontEqual(const RoboTextResolvedFont *a, const RoboTextResolvedFont *b);

/** Total order over the identity fields, for a determinism check. Never 0 for two different records. */
int RoboTextResolvedFontCompare(const RoboTextResolvedFont *a, const RoboTextResolvedFont *b);

/** Human-readable one-line summary. RoboNotSupported when it does not fit @p capacity. */
Result RoboTextResolvedFontWrite(const RoboTextResolvedFont *font,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength);

/* ------------------------------------------------------------------ */
/* Coverage                                                            */
/* ------------------------------------------------------------------ */

/**
 * Non-zero in `*outSupported` when @p face has a **usable** glyph for @p codePoint.
 *
 * The face's own coverage query, through RoboFont's lazy build: the first call parses the `cmap` once and
 * every later call reads the immutable range table, so this is the door a text-run walk may call freely.
 * A mapping to glyph 0 (`.notdef`) is **not** coverage, and a code point that is not a scalar is not
 * either — both answer 0 rather than failing.
 *
 * @return RoboOk with @p outSupported; RoboInvalidArgument for a NULL @p face or @p outSupported;
 *         RoboError when the face has a `cmap` that will not parse.
 */
Result RoboTextFontFaceSupportsCodePoint(FontFace *face,
                                         RoboTextCodePoint codePoint,
                                         int *outSupported,
                                         RoboTextDiag *diag);

/**
 * How many of @p codePoints @p face does **not** cover, out of @p count.
 *
 * The run-level form of the door above, and the one the selector's range policy uses: a fallback decision
 * that considered each scalar independently is exactly the decision that would split a base character from
 * its combining mark.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or a count without code points; RoboError when
 *         the face's `cmap` will not parse.
 */
Result RoboTextFontFaceSupportsRange(FontFace *face,
                                     const uint32_t *codePoints,
                                     size_t count,
                                     size_t *outMissing,
                                     RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* The selector                                                        */
/* ------------------------------------------------------------------ */

typedef struct RoboTextFontSelector RoboTextFontSelector;

/** What the selector's coverage memo has been asked and answered. */
typedef struct RoboTextCoverageCacheStats {
    /** Code-point queries the selector answered. */
    uint64_t queries;
    /** Memo entries served without touching the face's coverage. */
    uint64_t hits;
    /** Queries that reached the face. */
    uint64_t misses;
    /** Distinct candidates the memo currently holds. */
    uint64_t entries;
} RoboTextCoverageCacheStats;

/**
 * A selector: a primary context, an ordered fallback chain and a coverage memo.
 *
 * Mutable per thread and **not** shared: a selector holds a memo, and the thread-safety the phase asks for
 * is that the *inputs* — the face's coverage, RoboFont's metrics, the Unicode tables — are immutable and
 * safely readable by any number of selectors at once. Creating one selector per layout context is the
 * intended shape; a single global one would be the mutable global state the phase forbids.
 */
Result RoboTextFontSelectorCreate(RoboTextFontSelector **out, RoboTextDiag *diag);

/** Release @p selector. Safe on NULL. */
void RoboTextFontSelectorDestroy(RoboTextFontSelector *selector);

/** Non-zero when @p selector names a primary face or a non-empty chain. */
int RoboTextFontSelectorIsUsable(const RoboTextFontSelector *selector);

/**
 * Set the requested primary context (borrowed by value; its face is borrowed).
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p selector or a context whose style this build cannot
 *         draw (`RoboTextFontStyleIsValid`). A context with no face is **accepted**: it is the documented
 *         "ask the fallback chain" configuration.
 */
Result RoboTextFontSelectorSetPrimary(RoboTextFontSelector *selector,
                                      const RoboTextFontContext *primary,
                                      RoboTextDiag *diag);

/** The primary context by pointer, or NULL. */
const RoboTextFontContext *RoboTextFontSelectorPrimary(const RoboTextFontSelector *selector);

/**
 * Set the fallback chain (borrowed by value; the entries and their faces are borrowed).
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p selector or a chain with a NULL entries array and a
 *         non-zero count.
 */
Result RoboTextFontSelectorSetFallback(RoboTextFontSelector *selector,
                                       const RoboTextFontFallback *chain,
                                       RoboTextDiag *diag);

/** The chain by pointer, or NULL. */
const RoboTextFontFallback *RoboTextFontSelectorFallback(const RoboTextFontSelector *selector);

/**
 * Select for one code point.
 *
 * The primary is tried first and only when it has usable coverage; then the chain, in order, first
 * covering candidate wins. When nothing covers, @p out is a `RoboTextFontSourceNone` record — an answer,
 * not a failure.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument; RoboError when a candidate's `cmap`
 *         will not parse.
 */
Result RoboTextFontSelectorSelectScalar(RoboTextFontSelector *selector,
                                        RoboTextCodePoint codePoint,
                                        RoboTextResolvedFont *out,
                                        RoboTextDiag *diag);

/**
 * Select for a whole run of code points.
 *
 * The run-level door the phase asks for, and the reason it exists: fallback must operate on **coherent
 * sequences**, because a base character and its combining mark must not end up in two faces. The policy is
 * the scalar one read over a sequence:
 *
 *   - the first candidate (primary, then chain order) that covers **every** code point wins, `complete = 1`;
 *   - when none does, the first candidate that covers the run's **base** — its first scalar that is not a
 *     combining mark — wins, `complete = 0`;
 *   - when none covers even that, the result is `RoboTextFontSourceNone`.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or a count without code points; RoboError when a
 *         candidate's `cmap` will not parse.
 */
Result RoboTextFontSelectorSelectRange(RoboTextFontSelector *selector,
                                       const RoboTextCodePoint *codePoints,
                                       size_t count,
                                       RoboTextResolvedFont *out,
                                       RoboTextDiag *diag);

/** @ref RoboTextFontSelectorSelectRange over a text range: the convenience an analysis walk uses. */
Result RoboTextFontSelectorSelectRangeView(RoboTextFontSelector *selector,
                                           RoboTextView view,
                                           RoboTextRange range,
                                           RoboTextResolvedFont *out,
                                           RoboTextDiag *diag);

/** Counters for the coverage memo. RoboInvalidArgument for NULL arguments. */
Result RoboTextFontSelectorCacheStats(const RoboTextFontSelector *selector,
                                      RoboTextCoverageCacheStats *out);

/** Empty the memo. The next query reaches the face again. Safe on NULL. */
void RoboTextFontSelectorCacheClear(RoboTextFontSelector *selector);

/** Fuzz entry: select over arbitrary scalars with a selector that names no face. Never crashes. */
Result RoboTextFuzzFontSelection(const uint32_t *codePoints, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_FONT_SELECTION_H */
