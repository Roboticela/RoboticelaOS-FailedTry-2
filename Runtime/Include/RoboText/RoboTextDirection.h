/*
 * RoboText's direction stage (Phase 12.7) — see `Documentation/Runtime/Text/DirectionRuns.md`.
 *
 *     UTF-8 → TextBuffer → decoder → Bidi_Class per scalar → RoboTextDirectionClass
 *                                                                     │
 *                                            paragraph direction ─────┤ (from the context)
 *                                                                     ▼
 *                                                       one pass → RoboTextDirectionRun[]
 *
 * ## The one rule, and where the data comes from
 *
 * The `Bidi_Class` is RoboUnicode's (`RoboTextUnicodeBidiClass`); the paragraph direction is UAX #9's
 * P2/P3 first-strong rule (`RoboTextUnicodeBaseDirection`, which wraps `UnicodeBidiParagraphDirection`);
 * and this file is the only place the two are *combined*. The combination is the phase:
 *
 *   a scalar whose class is strong (`L`, `R`, `AL`)     keeps its own direction
 *   a nonspacing mark (`NSM`)                           takes the type of the character before it (W1)
 *   a block of number-class scalars containing EN/AN    is a **number**: left-to-right whenever the
 *                                                       paragraph direction is known, and — to the
 *                                                       neutrals beside it — `RTL`, because N1 says
 *                                                       numbers act as if they were `R`
 *   everything else (neutrals, and a number block
 *   with no digits in it)                               is resolved against **both** sides, N1: the
 *                                                       sides agreeing → that direction; differing, or
 *                                                       either side empty (N1's `sor`/`eor`, which are
 *                                                       the paragraph direction) → the paragraph's
 *
 * …and then adjacent equal directions are merged, so the output is the *minimum* number of runs.
 *
 * ## Why a number is left-to-right, and why that is not "digits are LTR"
 *
 * The phase's warning is that `digit → LTR` is wrong, and the warning is right: `2` is `EN` and `٣` is
 * `AN`, and neither is a strong class. What decides a number's direction is where the *paragraph* puts it,
 * and UAX #9's I1/I2 answer that identically for both bases: in an even (LTR) level an EN or an AN goes up
 * **two** levels, and in an odd (RTL) level it goes up **one** — so a number's level is even either way,
 * and an even level is left-to-right. `"مرحبا 123"` is therefore `RTL`, `RTL` (the space, N1) and `LTR`
 * (the digits) — three runs, not one, which is the property the phase is after: *an RTL paragraph is not a
 * paragraph in which every character is RTL*.
 *
 * Two things keep that honest rather than convenient. The number-ness is read from the class, not
 * assumed: `RoboTextDirectionClassOf` answers `Number` for `EN`/`AN`/`ES`/`ET`/`CS`, and a block of those
 * classes with no `EN`/`AN` in it (a lone `,`, a lone `.`) demotes to a neutral, because a separator
 * standing between two words is punctuation. And when the paragraph has **no** strong character and the
 * context declared none, there is no level at all, so a number stays `Unknown` — the `"123 456"` case the
 * phase's own `RoboTextDirectionUnknown` exists for.
 *
 * That second rule is also why the runs this file produces **coincide with the algorithm's level runs** on
 * ordinary mixed text — the level of a run is even exactly when this file says left-to-right — while still
 * being a classification rather than a resolution: no level is computed, no character is reordered, and the
 * one property a later stage cannot recover here is the explicit embedding, whose contents are classified
 * from the contents' own classes.
 *
 * ## What this stage deliberately does not do
 *
 * No levels, no explicit embeddings (the formatting controls are classified **neutral**, and only their
 * contents are classified — by the contents' own classes), no reordering, no mirrored glyphs, no
 * bidi-aware caret. Source text is **never** reversed, reordered or copied: a run points at the scalars it
 * came from, in logical order, and the visual order is Phase 12.8's
 * (`UnicodeBidiAnalyze`, whose capability flag is `RoboTextCapabilities.bidiAlgorithm` = 0). The single
 * overlap with 12.8 is the paragraph direction, and consuming P2/P3 here is what lets a run carry a
 * direction at all.
 */
#ifndef ROBO_TEXT_DIRECTION_H
#define ROBO_TEXT_DIRECTION_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextBuffer.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextTypes.h"
#include "RoboText/RoboTextUnicode.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

/**
 * The direction class of @p codePoint, **straight from its `Bidi_Class` and nothing else**.
 *
 * A pure function of the scalar — no context, no paragraph, no text — which is what makes it the layer a
 * test can pin and the layer a caller can reason about. It has no ASCII shortcut inside it, deliberately:
 * this is the door the shortcut is *proved* against, and a shortcut here would make the proof vacuous. The
 * resolver takes @ref RoboTextDirectionAsciiClassOf first and calls this for everything else.
 */
RoboTextDirectionClass RoboTextDirectionClassOf(RoboTextCodePoint codePoint);

/**
 * The ASCII fast path, as its own door: fills @p outClass and answers 1 for ASCII, 0 otherwise.
 *
 * The table-lookup-free classification the resolver takes for ordinary UI text, and a door rather than a
 * `static` in the resolver because the *suite* proves the shortcut equals the table for all 128 scalars
 * (`text.direction.ascii_fast_path`) — the same bargain `RoboTextUnicodeAsciiScript` makes. It is **exact**,
 * which is what the proof requires: letters are `L`, digits are `EN`, and the weak classes the block really
 * contains are spelled out (`ET` is `#`/`$`/`%`, `ES` is `+`/`-`, `CS` is `,`/`.`/`/`/`:`). Treating those
 * as neutral would be a plausible simplification with a visible defect — `"1,234"` splitting at the comma
 * in a right-to-left paragraph.
 */
int RoboTextDirectionAsciiClassOf(RoboTextCodePoint codePoint, RoboTextDirectionClass *outClass);

/* ------------------------------------------------------------------ */
/* The paragraph direction                                             */
/* ------------------------------------------------------------------ */

/**
 * The direction @p scalars is analysed in, given @p context.
 *
 * An explicit mode states it; `Auto` takes the text's own first strong scalar (P2/P3) and otherwise the
 * context's declared fallback, which is `Unknown` for a context that declared none. It is a separate door
 * from the run resolution because a caller often wants only this answer — a paragraph's direction is a
 * property of the paragraph, not of its runs.
 *
 * @return a defined direction; `RoboTextDirectionUnknown` when `Auto` found no strong scalar and the
 *         context's fallback is `Unknown`. NULL @p scalars with a non-zero @p count is `Unknown`.
 */
RoboTextDirection RoboTextDirectionResolveParagraph(const Uchar32 *scalars,
                                                    size_t count,
                                                    const RoboTextDirectionContext *context);

/* ------------------------------------------------------------------ */
/* Runs                                                                */
/* ------------------------------------------------------------------ */

/**
 * Resolve @p count scalars into direction runs.
 *
 * `outDirections` and `outRuns` are both optional, and both are filled in the same pass: the per-scalar
 * array is what the analysis layer intersects with its script runs, and the run array is the answer. A
 * caller that wants only the runs passes NULL for the array.
 *
 * The runs satisfy the invariants the phase names: non-empty, contiguous, no gaps and no overlaps, the
 * first starting at 0 and the last ending at @p count, and no two neighbours carrying the same direction —
 * merging is by construction rather than a second pass. **Empty text produces no runs** rather than one
 * empty run.
 *
 * @param scalars       the text, decoded. Borrowed; NULL is only legal when @p count is 0.
 * @param count         scalars to resolve.
 * @param context       the paragraph context. NULL means the default (`{ Auto, Unknown }`).
 * @param outDirections optional per-scalar array of @p count entries.
 * @param outRuns       optional run array of @p runCapacity entries.
 * @param runCapacity   entries in @p outRuns.
 * @param outRunCount   receives the runs produced, whether or not they fit. Required.
 * @param outBase       optional: receives the paragraph direction the runs were resolved against.
 * @param diag          optional failure record.
 *
 * @return RoboOk; RoboInvalidArgument for a missing @p outRunCount or scalars with a non-zero count;
 *         RoboNotSupported when @p outRuns cannot hold the runs (nothing is truncated, and @p outRunCount
 *         still reports how many there were).
 */
Result RoboTextDirectionRunsResolve(const Uchar32 *scalars,
                                    size_t count,
                                    const RoboTextDirectionContext *context,
                                    RoboTextDirection *outDirections,
                                    RoboTextDirectionRun *outRuns,
                                    size_t runCapacity,
                                    size_t *outRunCount,
                                    RoboTextDirection *outBase,
                                    RoboTextDiag *diag);

/** @ref RoboTextDirectionRunsResolve over a view: decodes once, then resolves. */
Result RoboTextDirectionRunsResolveView(RoboTextView view,
                                        const RoboTextDirectionContext *context,
                                        RoboTextDirection *outDirections,
                                        RoboTextDirectionRun *outRuns,
                                        size_t runCapacity,
                                        size_t *outRunCount,
                                        RoboTextDirection *outBase,
                                        RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/** One run as a line: `"[0..6) ltr"`. RoboNotSupported when it does not fit @p capacity. */
Result RoboTextDirectionRunWrite(const RoboTextDirectionRun *run,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength);

/**
 * A whole run set as a table, with the header `"Direction Analysis"`.
 *
 * Every row names **both** index spaces — the scalar range the resolver works in and the UTF-8 byte range
 * a caller outside the text layer works in — because a table that printed only one of them is the table
 * that makes a reader guess which one they are looking at. With a view the runs are checked against it
 * first, and a run outside the text is refused rather than printed.
 */
Result RoboTextDirectionRunsWrite(RoboTextView view,
                                  const RoboTextDirectionRun *runs,
                                  size_t count,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_DIRECTION_H */
