/*
 * RoboText's bidirectional stage (Phase 12.8) — see `Documentation/Runtime/Text/BidirectionalText.md`.
 *
 *     UTF-8 → decode → scalars ─┬─ script runs        (12.6)
 *                               ├─ classified direction (12.7)
 *                               └─ **bidi resolution** ← this file
 *                                        │
 *                    levels (per scalar) · logical runs · visual order
 *                                        │
 *                        analysis runs · shape requests · glyph runs
 *
 * ## Resolution, not classification — what 12.7 is and what this is
 *
 * 12.7 (`RoboTextDirection.h`) *classifies*: it projects each scalar's `Bidi_Class` to a direction
 * class and resolves the neutrals against the paragraph direction, so a run says "these scalars are
 * strong RTL" or "these scalars are a number". It computes **no level**, honours **no** explicit
 * formatting and reorders **nothing**; its runs are an answer about the text, and they happen to
 * coincide with the algorithm's level runs on ordinary mixed text.
 *
 * This file *resolves*. It consumes RoboUnicode's implementation of the Unicode Bidirectional
 * Algorithm (UAX #9, `UnicodeBidiAnalyze`) and turns its result into the objects the rest of the
 * text layer reads:
 *
 *   - the paragraph's **base direction and base level** (P2/P3, or the caller's declaration);
 *   - a **level per scalar**, with the X9-removed formatting characters honestly marked;
 *   - **logical runs**: maximal stretches sharing one level, in source order, contiguous and complete;
 *   - the **visual order** of those runs (L2) and of the visible scalars;
 *   - the **mirrored code point** a right-to-left scalar must be drawn with (L4).
 *
 * The source text is never reversed, reordered or copied into another order. A run points at the
 * scalars it came from, in logical order; the visual order is an *index array beside* the runs, which
 * is what makes a caret, a selection and a hit test able to speak the source's language while the
 * painter walks the visual one.
 *
 * ## The algorithm is Phase 4's, and this file is a window onto it
 *
 * There is one UBA in this tree (`RoboUnicodeBidi.h`, UAX #9, 91 707/91 707 conformance cases) and this
 * stage does not add a second. What it adds is the *text layer's* half:
 *
 *   **Index spaces.** The algorithm works in code-point indices; everything above this module works in
 *   `RoboTextScalarIndex` and `RoboTextRange`, and a caller outside the text layer wants UTF-8 byte
 *   ranges. `RoboTextUnicodeDecodeAll` already answers the first, and the paragraph keeps the view so a
 *   diagnostic can print the second.
 *
 *   **Vocabulary.** `RoboTextBidiLevel` is `uint8_t` with even = left-to-right and odd = right-to-left,
 *   because that is the one fact about a level anything above here uses. `RoboTextBidiLevelRemoved` is
 *   the algorithm's own X9 answer for a scalar that was *removed from reordering* (a `BN`, or one of the
 *   nine explicit formatting controls) — a scalar that has no level rather than level zero.
 *
 *   **§5.2 retention.** For painting and for run construction a removed scalar takes the level of the
 *   preceding scalar (the paragraph's level when it is at the start), so a run has one level and the run
 *   list has no gaps. Both answers are available and both are named: `RoboTextBidiParagraphLevel` is the
 *   primitive (and may answer `RoboTextBidiLevelRemoved`), `RoboTextBidiParagraphResolvedLevel` is the
 *   §5.2 one (and never does), and `RoboTextBidiParagraphScalarIsDrawable` is the flag a painter skips a
 *   control with.
 *
 *   **Two kinds of invisible scalar, and one predicate for a painter.** X9 *removes* `BN` and the five
 *   explicit embeddings (`LRE`/`RLE`/`LRO`/`RLO`/`PDF`); §5.2 *keeps* the four isolate formatting
 *   characters (`LRI`/`RLI`/`FSI`/`PDI`), because they are what determines an isolate's paragraph level,
 *   and gives them levels of their own. Neither half is drawn. So `RoboTextBidiParagraphScalarIsRemoved`
 *   answers the algorithm's question ("was this removed from reordering?") and
 *   `RoboTextBidiParagraphScalarIsDrawable` answers the painter's — and the visual scalar order is built
 *   over the drawable ones, so an isolate control occupies no visual slot rather than a box's worth.
 *
 * ## The paragraph base: P2/P3, and *not* `Unknown`
 *
 * 12.7's `RoboTextDirectionResolveParagraph` answers `RoboTextDirectionUnknown` for an `Auto` paragraph
 * with no strong scalar and nothing declared, and its header explains why: a *classification* has no
 * business claiming a direction UAX #9 has not assigned. A *resolution* does, because P3 assigns it:
 * with no strong character the paragraph level is 0. So this stage takes 12.7's answer and, when it is
 * `Unknown`, applies P3's level-0 default and reports that it did —
 * `RoboTextBidiParagraphBaseWasDefaulted` — rather than pretending the text said something.
 *
 * An explicit mode is a declaration and wins; `Auto` with a declared fallback answers the first strong
 * scalar or the fallback; `Auto` with neither answers the fallback and then P3's default. The paragraph
 * level fed to the algorithm is therefore always 0 or 1, which is what the algorithm's P2/P3 would have
 * produced from the same text.
 *
 * ## Mirroring
 *
 * UAX #9 L4: a scalar whose *resolved* level is odd is depicted by its mirrored glyph. The pairing is
 * `Bidi_Mirroring_Glyph` and it lives in RoboUnicode (`RoboBidiMirror.h`), so this stage only *asks*:
 * `RoboTextBidiParagraphMirrors` answers "this scalar is right-to-left and here is the code point to
 * draw". Drawing it does not touch the text — `RoboTextGlyph.source` still names the original scalar, so
 * selection, copy and accessibility see `(`, not `)`.
 *
 * ## Cost
 *
 * A paragraph allocates once for its scalars, once for its levels and once for its runs (plus the
 * algorithm's own two arrays); nothing allocates per scalar. Ordinary left-to-right text **skips the
 * algorithm entirely**: `RoboTextBidiScalarNeedsAlgorithm` answers 0 for every scalar whose class cannot
 * change a level in a left-to-right paragraph — which is every letter and every neutral, so a UI label
 * never reaches the tables — and the resolver then assigns the paragraph level directly. That shortcut is
 * *exactly* equivalent, not approximately: no scalar in such a text can be the subject of W2–W7 or of I1's
 * upward pressure, so every level really is the paragraph's. `text.bidi.ascii_fast_path` proves it against
 * the full algorithm scalar by scalar, and `text.bidi.performance` records the cost.
 *
 * ## What this stage deliberately does not do
 *
 * No caret movement, no selection, no line breaking, no justification, no Arabic joining, no GSUB/GPOS, no
 * mirrored-glyph *selection from a font's alternate glyphs* (L4's other half, which is a shaping concern
 * and arrives with it), and no reversal of the text. `RoboTextCapabilities` reports the algorithm as
 * present and each of those as absent.
 */
#ifndef ROBO_TEXT_BIDI_H
#define ROBO_TEXT_BIDI_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextBuffer.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextTypes.h"
#include "RoboText/RoboTextUnicode.h"
#include "RoboUnicode/RoboUnicodeBidi.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Levels                                                              */
/* ------------------------------------------------------------------ */

/**
 * A resolved embedding level.
 *
 * **Even is left-to-right and odd is right-to-left**, and nothing else about a level is anybody's
 * business above this header. `RoboTextBidiLevelRemoved` is not a level: it is X9's answer for a scalar
 * the algorithm removed from reordering, and it exists so that "no level" cannot be mistaken for level 0.
 */
typedef uint8_t RoboTextBidiLevel;

/** X9 removed this scalar from reordering: a `BN` or one of the nine explicit formatting controls. */
#define RoboTextBidiLevelRemoved ((RoboTextBidiLevel)0xFFu)

/**
 * No level at all: the record describes a stretch of text that was **classified** rather than resolved.
 *
 * Phase 12.7's runs and a caller's hand-built `RoboTextRun` have no level, and that is a different fact
 * from level 0 — which is why this value exists instead of a zero that would read as "left-to-right". A
 * record that is level 0 because the algorithm said so and one that was never resolved must be told apart
 * by a reader, and `RoboTextBidiLevelNone` is how.
 */
#define RoboTextBidiLevelNone ((RoboTextBidiLevel)0xFEu)

/** The deepest level the algorithm can assign (UAX #9's max_depth + 1, as RoboUnicode defines it). */
#define RoboTextBidiLevelMax ((RoboTextBidiLevel)125u)

/** Non-zero when @p level is a level the algorithm assigned: neither @ref RoboTextBidiLevelRemoved nor
 *  @ref RoboTextBidiLevelNone. */
int RoboTextBidiLevelIsResolved(RoboTextBidiLevel level);

/** Non-zero when @p level is a resolved, even level — left-to-right. */
int RoboTextBidiLevelIsLeftToRight(RoboTextBidiLevel level);

/** Non-zero when @p level is a resolved, odd level — right-to-left. */
int RoboTextBidiLevelIsRightToLeft(RoboTextBidiLevel level);

/** The direction @p level implies; `RoboTextDirectionUnknown` for `RoboTextBidiLevelRemoved`. */
RoboTextDirection RoboTextBidiLevelDirection(RoboTextBidiLevel level);

/** The level a declared paragraph direction fixes: `Ltr` → 0, `Rtl` → 1, `Unknown` → 0 (P3's default). */
RoboTextBidiLevel RoboTextBidiLevelForDirection(RoboTextDirection direction);

/**
 * Non-zero when @p codePoint can change a level in a left-to-right paragraph — that is, when the text
 * containing it must go through the algorithm.
 *
 * A scalar is "inert" when its class cannot be the subject of W2–W7 or of I1's upward pressure: `L`, and
 * the neutrals (`WS`, `ON`, `B`, `S`, `ES`, `ET`, `CS`). A scalar needs the algorithm when its class is
 * `R`, `AL`, `EN` or `AN`, or when it is one of the nine explicit formatting controls.
 *
 * The ASCII block is answered without a table lookup — no ASCII scalar is `R`, `AL`, `AN` or a format
 * control, so the only ASCII scalars that need the algorithm are the ten digits — and
 * `text.bidi.ascii_fast_path` proves that shortcut equals the table over the whole block rather than
 * assuming it.
 */
int RoboTextBidiScalarNeedsAlgorithm(RoboTextCodePoint codePoint);

/**
 * Non-zero when @p codePoint is one of the nine bidi formatting controls — the five X9 removes
 * (`LRE`/`RLE`/`LRO`/`RLO`/`PDF`) and the four §5.2 keeps (`LRI`/`RLI`/`FSI`/`PDI`).
 *
 * The two halves differ in what the algorithm does with them (one is removed from reordering, the other
 * gets a level of its own) and agree on the one thing a painter cares about: neither is drawn. This is the
 * scalar-level door; `RoboTextBidiParagraphScalarIsDrawable` is the paragraph-level answer that also
 * accounts for X9's `BN` removals.
 */
int RoboTextBidiScalarIsFormattingControl(RoboTextCodePoint codePoint);

/**
 * The code point whose glyph depicts @p codePoint in a right-to-left context, when the UCD lists one.
 *
 * UAX #9 L4's pairing, through RoboUnicode's `BidiMirroring.txt` table: `(` answers `)`. The answer is a
 * *drawing* instruction and never a substitution — the text keeps the original scalar.
 *
 * @param outMirror receives the mirroring code point, or @p codePoint when there is none.
 * @return 1 when a distinct mirroring glyph exists; 0 otherwise. A scalar that is left-to-right is not
 *         refused here — mirroring applies to the scalar *in a right-to-left context*, which is a fact
 *         about the paragraph and is `RoboTextBidiParagraphMirrors`'s question, not this door's.
 */
int RoboTextBidiMirrorOf(RoboTextCodePoint codePoint, RoboTextCodePoint *outMirror);

/* ------------------------------------------------------------------ */
/* A logical bidi run                                                  */
/* ------------------------------------------------------------------ */

/**
 * A stretch of scalars sharing one resolved level.
 *
 * Invariants, for every paragraph: runs are contiguous in **source order**, the first starts at 0, the
 * last ends at the scalar count, no two neighbours are at the same level, and each run's `level` is a
 * resolved level. Visual order is *not* the order of this array — see @ref
 * RoboTextBidiParagraphVisualRun.
 *
 * `logicalIndex` is the run's own position in the logical array (what a later stage's `logicalIndex`
 * field needs to be comparable with) and `visualIndex` is where the run sits in the visual order, so the
 * two readings are both available from one record.
 */
typedef struct RoboTextBidiRun {
    /** The scalars the run covers, in the text's own index space. */
    RoboTextRange range;
    /** The level every scalar in the run resolves to (@ref RoboTextBidiLevelRemoved is impossible here). */
    RoboTextBidiLevel level;
    /** The direction @p level implies. */
    RoboTextDirection direction;
    /** The run's index in the logical array. Equal to its position in the array. */
    size_t logicalIndex;
    /** The run's index in the visual order. */
    size_t visualIndex;
} RoboTextBidiRun;

/**
 * Where a paragraph's base direction came from.
 *
 * Four answers, because "RTL" alone is three different facts to a caller: the author declared it, the
 * text's own first strong scalar said it, the context's fallback answered it, or P3's level-0 default was
 * applied because nothing else did. A document bridge that wants to know whether it needs to *write* a
 * `dir` attribute reads this; a caller that only cares about the direction reads the direction.
 */
typedef enum RoboTextBidiBaseSource {
    /** The context's explicit mode stated it (CSS's `direction`, an author's `dir`). */
    RoboTextBidiBaseDeclared = 0,
    /** The text's first strong scalar — UAX #9 P2/P3. */
    RoboTextBidiBaseStrong = 1,
    /** The context's `Auto` fallback, with no strong scalar in the text. */
    RoboTextBidiBaseFallback = 2,
    /** P3's level-0 default, because neither the text nor the context said anything. */
    RoboTextBidiBaseDefault = 3
} RoboTextBidiBaseSource;

/** Number of defined sources. */
#define RoboTextBidiBaseSourceCount 4u

/** Non-zero if @p source is a defined enumerator. */
int RoboTextBidiBaseSourceIsKnown(RoboTextBidiBaseSource source);

/** Lower-case name (`"declared"`, `"strong"`, `"fallback"`, `"default"`); never NULL. */
const char *RoboTextBidiBaseSourceName(RoboTextBidiBaseSource source);

/** Counters for a dump or an assertion. RoboInvalidArgument for NULL arguments. */
typedef struct RoboTextBidiStats {
    /** Logical runs. */
    size_t runs;
    /** Scalars the paragraph covers. */
    size_t scalars;
    /** Scalars a painter draws: not X9-removed and not one of the nine formatting controls. */
    size_t visibleScalars;
    /** Scalars X9 removed from reordering — `BN` and the five explicit embeddings/overrides. */
    size_t removedScalars;
    /** Scalars that are one of the nine formatting controls, whether or not X9 removed them. */
    size_t formattingControls;
    /** Scalars neither removed nor a formatting control — the same count as @ref visibleScalars. */
    size_t drawableScalars;
    /** Scalars whose resolved level is odd. */
    size_t rtlScalars;
    /** Runs whose level is odd. */
    size_t rtlRuns;
    /** Scalars that are right-to-left **and** have a mirroring glyph (L4 applies to them). */
    size_t mirroredScalars;
    /** The highest level assigned. */
    size_t maxLevel;
    /** The paragraph's level: 0 or 1. */
    RoboTextBidiLevel baseLevel;
    /** The paragraph's direction. */
    RoboTextDirection baseDirection;
    /** Non-zero when the base came from P3's level-0 default rather than from the text or a caller. */
    int baseDefaulted;
    /** Where the base direction came from. */
    RoboTextBidiBaseSource baseSource;
} RoboTextBidiStats;

/* ------------------------------------------------------------------ */
/* The paragraph                                                       */
/* ------------------------------------------------------------------ */

typedef struct RoboTextBidiParagraph RoboTextBidiParagraph;

/**
 * Resolve @p count scalars into one bidi paragraph.
 *
 * One paragraph, deliberately: UAX #9 resolves per paragraph and a caller holding a document must split
 * it at a paragraph separator first. This stage does not look for one — a `B` scalar is just a boundary
 * the algorithm handles inside a paragraph — and a caller that hands a whole document here gets a
 * document-wide base direction, which is a *different* answer rather than a crash.
 *
 * The paragraph **owns a copy of @p scalars**, so the caller may release its array as soon as this
 * returns. That is what lets the mirroring door and the diagnostic dump work from the object alone.
 *
 * @param scalars the decoded text. Borrowed for the duration of the call.
 * @param count   how many scalars.
 * @param context the paragraph context; NULL means `{ Auto, Unknown }` — first strong, else P3's level 0.
 * @param out     receives a new paragraph the caller destroys.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p out or a NULL @p scalars with a non-zero @p count, or
 *         an invalid context; RoboOutOfMemory.
 *
 *         **Empty text is a paragraph with no runs**, not a failure: it has a base level and no scalars,
 *         which is the honest description of an empty string.
 */
Result RoboTextBidiParagraphResolve(const Uchar32 *scalars,
                                    size_t count,
                                    const RoboTextDirectionContext *context,
                                    RoboTextBidiParagraph **out,
                                    RoboTextDiag *diag);

/** @ref RoboTextBidiParagraphResolve over a view: decodes once through the module's one decoder. */
Result RoboTextBidiParagraphResolveView(RoboTextView view,
                                        const RoboTextDirectionContext *context,
                                        RoboTextBidiParagraph **out,
                                        RoboTextDiag *diag);

/** Release @p paragraph. Safe on NULL. */
void RoboTextBidiParagraphDestroy(RoboTextBidiParagraph *paragraph);

/** Scalars the paragraph covers. */
RoboTextScalarIndex RoboTextBidiParagraphScalarCount(const RoboTextBidiParagraph *paragraph);

/** The paragraph's direction: the declared one, the first strong scalar's, or P3's `Ltr`. */
RoboTextDirection RoboTextBidiParagraphBaseDirection(const RoboTextBidiParagraph *paragraph);

/** The paragraph's level: 0 for left-to-right, 1 for right-to-left. */
RoboTextBidiLevel RoboTextBidiParagraphBaseLevel(const RoboTextBidiParagraph *paragraph);

/** Non-zero when the base came from P3's level-0 default — no strong scalar and nothing declared. */
int RoboTextBidiParagraphBaseWasDefaulted(const RoboTextBidiParagraph *paragraph);

/** Where the paragraph's base direction came from. */
RoboTextBidiBaseSource RoboTextBidiParagraphBaseSource(const RoboTextBidiParagraph *paragraph);

/** Logical runs, in source order. */
size_t RoboTextBidiParagraphRunCount(const RoboTextBidiParagraph *paragraph);

/** Logical run @p index, or NULL when out of range. */
const RoboTextBidiRun *RoboTextBidiParagraphRun(const RoboTextBidiParagraph *paragraph, size_t index);

/** The logical run containing @p scalar, or NULL at or past the last scalar (the caret convention). */
const RoboTextBidiRun *RoboTextBidiParagraphRunAtScalar(const RoboTextBidiParagraph *paragraph,
                                                        RoboTextScalarIndex scalar);

/**
 * The **visual** order of the logical runs (UAX #9 L2), as logical-run indices.
 *
 * @param outLogicalIndex receives the logical index of the @p visual-th run to paint.
 * @return RoboOk; RoboInvalidArgument for a NULL argument or @p visual past the run count.
 */
Result RoboTextBidiParagraphVisualRun(const RoboTextBidiParagraph *paragraph,
                                      size_t visual,
                                      size_t *outLogicalIndex,
                                      RoboTextDiag *diag);

/**
 * The level of @p scalar as the algorithm left it — @ref RoboTextBidiLevelRemoved for a scalar X9
 * removed, which is a *state* and not level zero.
 *
 * Use @ref RoboTextBidiParagraphResolvedLevel for the level a run or a painter wants.
 */
RoboTextBidiLevel RoboTextBidiParagraphLevel(const RoboTextBidiParagraph *paragraph,
                                             RoboTextScalarIndex scalar);

/**
 * The level of @p scalar with §5.2's retention applied: a removed scalar takes the level of the preceding
 * scalar, or the paragraph's level when it is at the start. Never @ref RoboTextBidiLevelRemoved.
 */
RoboTextBidiLevel RoboTextBidiParagraphResolvedLevel(const RoboTextBidiParagraph *paragraph,
                                                     RoboTextScalarIndex scalar);

/** Non-zero when X9 removed @p scalar from reordering (a `BN`, or an explicit embedding/override/`PDF`). */
int RoboTextBidiParagraphScalarIsRemoved(const RoboTextBidiParagraph *paragraph,
                                         RoboTextScalarIndex scalar);

/**
 * Non-zero when a painter draws @p scalar: not X9-removed, and not one of the nine formatting controls.
 *
 * The predicate to paint with, and the set the visual scalar order is built over — an isolate control has
 * a level and still no visual slot, so it can neither take a box's worth of ink nor displace the scalars
 * around it.
 */
int RoboTextBidiParagraphScalarIsDrawable(const RoboTextBidiParagraph *paragraph,
                                          RoboTextScalarIndex scalar);

/** The decoded scalar @p index, or U+FFFD when it is out of range. */
RoboTextCodePoint RoboTextBidiParagraphScalar(const RoboTextBidiParagraph *paragraph,
                                              RoboTextScalarIndex index);

/** Visible scalars, in visual order: the length of the array the doors below read. */
size_t RoboTextBidiParagraphVisualScalarCount(const RoboTextBidiParagraph *paragraph);

/**
 * The logical index of the @p visual-th visible scalar — L2's own output.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or @p visual past the visible count.
 */
Result RoboTextBidiParagraphVisualScalar(const RoboTextBidiParagraph *paragraph,
                                         size_t visual,
                                         RoboTextScalarIndex *outLogical,
                                         RoboTextDiag *diag);

/** A removed scalar's answer from **both** visual doors below: it has no visual position. */
#define RoboTextBidiVisualNone ((size_t)-1)

/**
 * The inverse of @ref RoboTextBidiParagraphVisualScalar: where @p logical sits visually.
 *
 * The mapping hit testing needs and the reason it is stored rather than searched — a caret must be able
 * to turn a source position into a visual one without walking the line. A removed scalar answers
 * `RoboTextBidiVisualNone`, because it is not painted and therefore has no visual position.
 *
 * @return RoboOk and @p outVisual; RoboInvalidArgument for a NULL argument or a scalar out of range.
 */
Result RoboTextBidiParagraphVisualOfScalar(const RoboTextBidiParagraph *paragraph,
                                           RoboTextScalarIndex logical,
                                           size_t *outVisual,
                                           RoboTextDiag *diag);

/**
 * The code point to **draw** for @p scalar: its mirror when the scalar is right-to-left and the UCD lists
 * one (UAX #9 L4), and the scalar itself otherwise.
 *
 * @param outDraw receives the code point whose glyph depicts the scalar.
 * @return 1 when a mirroring glyph applies; 0 when the scalar is drawn as itself (a left-to-right scalar,
 *         a removed control, or a character the UCD gives no mirror).
 */
int RoboTextBidiParagraphMirrors(const RoboTextBidiParagraph *paragraph,
                                 RoboTextScalarIndex scalar,
                                 RoboTextCodePoint *outDraw);

/** Fill @p out with the paragraph's counters. RoboInvalidArgument for NULL arguments. */
Result RoboTextBidiParagraphGetStats(const RoboTextBidiParagraph *paragraph, RoboTextBidiStats *out);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/** One logical run as a line: `"[0..4) lvl 0 ltr"`. RoboNotSupported when it does not fit. */
Result RoboTextBidiRunWrite(const RoboTextBidiRun *run,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength);

/**
 * A whole paragraph as a table — the dump Phase 12.8 §"diagnostics" asks for, and the one that makes a
 * mixed-direction bug findable:
 *
 * ```
 * Bidi Analysis
 * base: ltr level 0 (declared)  scalars 8  runs 3  visible 8
 * [0..4) scalars  bytes [0..4)   lvl 0  ltr
 * [4..8) scalars  bytes [4..12)  lvl 1  rtl
 * visual: 0 2 1
 * scalars:
 *   #0  U+0048  L    lvl 0
 *   #4  U+05D0  R    lvl 1
 * ```
 *
 * Four sections, in that order: the paragraph's own facts (its base direction, the level, where the base
 * came from, and its counters), the logical runs (both index spaces each, so a caller outside the text
 * layer can read the byte spans it works in), the visual run order (L2's answer, as logical indices), and
 * the per-scalar lines — the scalar's index, its code point, its `Bidi_Class` and its resolved level. `x`
 * in place of a level is a scalar X9 removed.
 *
 * The per-scalar section is capped at `RoboTextBidiDumpScalarLimit` lines; a longer paragraph prints the
 * runs in full and says how many scalars it did not print, because a dump that silently truncates reads
 * as a shorter paragraph.
 *
 * RoboNotSupported when the table does not fit @p capacity, with @p outLength receiving what it needed and
 * @p buffer left empty — a half-written table is worse than none.
 */
#define RoboTextBidiDumpScalarLimit 64u

Result RoboTextBidiParagraphWrite(const RoboTextBidiParagraph *paragraph,
                                  RoboTextView view,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_BIDI_H */
