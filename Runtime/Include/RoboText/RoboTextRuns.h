/*
 * RoboText's runs (Phase 12.4) — script, direction and text runs, and the segmentation that produces
 * them from a buffer, a font context and a set of shape options.
 *
 * ## Three run types, because three questions are asked
 *
 *   ScriptRun       "which script is this stretch of scalars in?" — asked by substitution (a `locl` or
 *                   an Arabic `init` applies *within* one script) and answered from RoboUnicode's own
 *                   script tables, not from a table this module keeps.
 *   DirectionRun    "which way does this stretch run?" — asked by positioning and by reordering.
 *   TextRun         the intersection of the two, carrying the font context and the language as well.
 *                   This is what a shaper will consume and what a painter walks.
 *
 * They are separate records rather than one with optional fields because they are produced by different
 * algorithms, are consumed by different goals, and change independently: script segmentation already
 * comes from the UCD, direction segmentation will be *replaced* by the bidirectional algorithm in
 * Phase 12.8, and the intersection is what stays stable across both.
 *
 * ## What the direction classification is, exactly
 *
 * **Not the Unicode bidirectional algorithm**, and the difference is worth stating precisely because
 * "we have direction runs" is the kind of claim that hides it:
 *
 *   this phase   each scalar is classified from its own `Bidi_Class`: `R`/`AL` are right-to-left, `L`
 *                is left-to-right, and everything else — digits, punctuation, spaces, format controls —
 *                takes the **base direction** (the one the caller declared in
 *                `RoboTextShapeOptions.baseDirection`, or, when nothing was declared, the first-strong
 *                direction of the text, which is UAX #9's P2/P3 and is what
 *                `UnicodeBidiParagraphDirection` answers);
 *   Phase 12.8   resolves *embedding levels*, honours the explicit formatting characters (LRE/RLE/PDF,
 *                LRI/RLI/FSI/PDI), applies the weak and neutral rules (W1–W7, N0–N2) and reorders the
 *                runs (L1/L2).
 *
 * What this phase produces is therefore the *input* the algorithm classifies, not its output: a run
 * here says "these scalars are strong RTL", never "these scalars are at level 1 and must be painted in
 * this visual order". `RoboTextDirectionUnknown` is not produced by the classification — it is what a
 * run carries when the text has no strong direction *and* the caller declared none, which is a real
 * state (`"123 456"`) and not a bug.
 *
 * ## Neutrals take the paragraph direction, and here is why that is the right default
 *
 * The alternative — giving each neutral its own `Unknown` run — produces `[L][?][L]` for `"abc 123
 * def"`, three runs where every consumer wants one. Resolving a neutral to the base direction is what
 * UAX #9 does for the *paragraph* level, it is the rule CSS's `direction` property implies for text
 * that has no strong character of its own, and it is stable: the classification of a neutral never
 * depends on which side of it a strong character sits.
 *
 * ## Ownership
 *
 * A run set is an opaque handle owning three arrays of run records. It borrows the buffer's bytes (the
 * caller's text outlives it) and the font context **by value**, so the only thing a caller has to keep
 * alive is the text and the face inside the context. That is a deliberate asymmetry: text is large and
 * must not be copied; a context is 40 bytes and copying it removes a lifetime question from every run.
 */
#ifndef ROBO_TEXT_RUNS_H
#define ROBO_TEXT_RUNS_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextBuffer.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextFont.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A stretch of scalars sharing one script. */
typedef struct RoboTextScriptRun {
    RoboTextRange range;
    RoboTextScript script;
} RoboTextScriptRun;

/** A stretch of scalars sharing one classified direction. */
typedef struct RoboTextDirectionRun {
    RoboTextRange range;
    RoboTextDirection direction;
} RoboTextDirectionRun;

/**
 * The intersection of a script run and a direction run, with the font it is set in.
 *
 * What a shaper consumes and what a glyph run is generated from. `baseDirection` is kept beside
 * `direction` on purpose: a run of digits classified to the base direction is *not* the same fact as a
 * run of Latin letters, and a later positioning stage wants both.
 */
typedef struct RoboTextRun {
    RoboTextRange range;
    RoboTextScript script;
    /** The direction the run's scalars were classified as. */
    RoboTextDirection direction;
    /** The direction the caller declared for the whole text. */
    RoboTextDirection baseDirection;
    /** The font the run is set in, by value; its `face` is borrowed. */
    RoboTextFontContext font;
    /** The language the caller declared, valid for the whole text and echoed per run. */
    RoboTextLanguage language;
} RoboTextRun;

typedef struct RoboTextRunSet RoboTextRunSet;

/* ------------------------------------------------------------------ */
/* Segmentation                                                        */
/* ------------------------------------------------------------------ */

/**
 * Segment @p view into script, direction and text runs.
 *
 * The three segmentations happen in one call because they share one decode of the text: a caller that
 * asked for them separately would decode the same bytes three times, and — more to the point — could
 * end up holding a script run and a direction run that disagree about what the text is.
 *
 * @param view      the text. Borrowed: it must outlive the run set.
 * @param font      the setting every text run carries. Borrowed by value; a context with no face is
 *                  accepted (a run's script and direction are properties of the text, not the font).
 * @param options   the declared direction, script hint and language. NULL means the defaults.
 * @param out       receives a new run set the caller destroys.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p out or a view that is not UTF-8 this build decodes;
 *         RoboOutOfMemory.
 *
 *         An **empty** view produces an empty run set rather than a failure: no text is a fact about a
 *         document, not an error in it.
 */
Result RoboTextRunSetCreate(RoboTextView view,
                            const RoboTextFontContext *font,
                            const RoboTextShapeOptions *options,
                            RoboTextRunSet **out,
                            RoboTextDiag *diag);

/** Release @p set. Safe on NULL. */
void RoboTextRunSetDestroy(RoboTextRunSet *set);

/** Script runs, in ascending text order. */
size_t RoboTextRunSetScriptRunCount(const RoboTextRunSet *set);

/** Script run @p index, or NULL when out of range. */
const RoboTextScriptRun *RoboTextRunSetScriptRun(const RoboTextRunSet *set, size_t index);

/** Direction runs, in ascending text order. */
size_t RoboTextRunSetDirectionRunCount(const RoboTextRunSet *set);

/** Direction run @p index, or NULL when out of range. */
const RoboTextDirectionRun *RoboTextRunSetDirectionRun(const RoboTextRunSet *set, size_t index);

/** Text runs, in ascending text order. Adjacent text runs never share both script and direction. */
size_t RoboTextRunSetTextRunCount(const RoboTextRunSet *set);

/** Text run @p index, or NULL when out of range. */
const RoboTextRun *RoboTextRunSetTextRun(const RoboTextRunSet *set, size_t index);

/** Scalars in the set: the whole text that was segmented. */
RoboTextScalarIndex RoboTextRunSetScalarCount(const RoboTextRunSet *set);

/** The base direction the set was segmented with. */
RoboTextDirection RoboTextRunSetBaseDirection(const RoboTextRunSet *set);

/** The declared script hint, or `RoboTextScriptCommon` when none was given. */
RoboTextScript RoboTextRunSetScriptHint(const RoboTextRunSet *set);

/** The language every run carries. Never NULL. */
const RoboTextLanguage *RoboTextRunSetLanguage(const RoboTextRunSet *set);

/**
 * The text run containing scalar @p index.
 *
 * The lookup a caret, a hit test and a later selection all need, and the reason the text runs are
 * stored in ascending order rather than in the order the two segmentations produced them. An empty run
 * set answers NULL.
 *
 * @return the run, or NULL when @p index is at or past the last scalar (which is the end-of-text caret
 *         position and has no run *containing* it — a caller that wants "the run before this position"
 *         should ask for `index - 1`).
 */
const RoboTextRun *RoboTextRunSetRunAtScalar(const RoboTextRunSet *set, RoboTextScalarIndex index);

/** Copy the set's counters for a dump or an assertion. RoboInvalidArgument for NULL arguments. */
typedef struct RoboTextRunStats {
    size_t scriptRuns;
    size_t directionRuns;
    size_t textRuns;
    /** Text runs whose classified direction is RTL. */
    size_t rtlRuns;
    /** Text runs whose classified direction is Unknown (no strong scalar, no declared base). */
    size_t unknownDirectionRuns;
    /** Distinct scripts across the text runs. */
    size_t distinctScripts;
    /** Scalars segmented. */
    RoboTextScalarIndex scalars;
} RoboTextRunStats;

Result RoboTextRunSetGetStats(const RoboTextRunSet *set, RoboTextRunStats *out);

/** Fuzz entry: segment arbitrary bytes as text. Never crashes; a result is destroyed if it was made. */
Result RoboTextFuzzSegment(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_RUNS_H */
