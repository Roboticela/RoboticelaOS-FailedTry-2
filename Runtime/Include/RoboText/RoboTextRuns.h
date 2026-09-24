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
 * algorithms, are consumed by different goals, and change independently: script segmentation comes from the
 * UCD through the script resolver, direction segmentation comes from the `Bidi_Class` through the direction
 * resolver (Phase 12.7), and the intersection is what stays stable across both.
 *
 * ## What the direction classification is, exactly
 *
 * **Not the Unicode bidirectional algorithm**, and the difference is worth stating precisely because
 * "we have direction runs" is the kind of claim that hides it:
 *
 *   this phase   each scalar is projected from its own `Bidi_Class` to a `RoboTextDirectionClass` — strong,
 *                number, attaching mark or neutral — and the classes are resolved into runs against the
 *                paragraph direction the caller named in a `RoboTextDirectionContext`: strong scalars keep
 *                their own direction, a mark takes the character before it (W1), a number is even-levelled
 *                and therefore left-to-right in either base (I1/I2), and a neutral block joins the sides that
 *                agree or takes the paragraph (N1). `RoboTextDirectionRunsResolve` is that pass, and
 *                [DirectionRuns.md](../../../../Documentation/Runtime/Text/DirectionRuns.md) is its contract;
 *   Phase 12.8   resolves *embedding levels*, honours the explicit formatting characters (LRE/RLE/PDF,
 *                LRI/RLI/FSI/PDI), applies the weak and neutral rules in full (W2/W4/W5, N0–N2) and
 *                reorders the runs (L1/L2).
 *
 * What this phase produces is therefore the *input* the algorithm classifies, not its output: a run
 * here says "these scalars are strong RTL" or "these scalars are a number", never "these scalars are at
 * level 1 and must be painted in this visual order". `RoboTextDirectionUnknown` is still what a run carries
 * when the text has no strong direction *and* the caller declared none, which is a real state (`"123 456"`)
 * and not a bug.
 *
 * ## Neutrals are resolved, not left unknown
 *
 * The alternative — giving each neutral its own `Unknown` run — produces `[L][?][L]` for `"abc 123
 * def"`, three runs where every consumer wants one. Resolving a neutral against the units on both sides is
 * what UAX #9's N1 does, it is the rule CSS's `direction` property implies for text that has no strong
 * character of its own, and it does not need a level: the answer here is a direction, and the level is
 * 12.8's.
 *
 * ## 12.8: the resolution, alongside the classification
 *
 * Since Phase 12.8 a run set also resolves a **bidi paragraph** (`RoboTextBidiParagraphResolve`) and
 * publishes it. The text runs and the level runs read it, and the two readings coexist on purpose:
 *
 *   `RoboTextRun.direction`/`bidiLevel`   the **resolution**: the direction the level implies, and the
 *                                         level itself;
 *   `RoboTextRunSetDirectionRun`          the **classification** (12.7), unchanged, `Unknown` included;
 *   `RoboTextRunSetBidiRun`               the algorithm's own level runs, plus L2's visual order.
 *
 * A text run is still cut where the *script* or the *direction* changes, not where the level changes: a
 * level move that does not change the parity does not change what a shaper does with the stretch, and
 * splitting there would hand every consumer two runs where one is intended. The level travels on the run,
 * so a consumer that needs it has it, and `RoboTextRunSetBidiRun` has the algorithm's own boundaries for
 * one that needs those instead.
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
#include "RoboText/RoboTextBidi.h"
#include "RoboText/RoboTextBuffer.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextFont.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A stretch of scalars sharing one script.
 *
 * Declared in `RoboTextTypes.h`: the script resolver produces these before a run set exists, and the two
 * must agree about the type. See `RoboTextScriptRunsResolve` in `RoboTextUnicode.h`.
 */

/**
 * A stretch of scalars sharing one classified direction.
 *
 * Declared in `RoboTextTypes.h` beside @ref RoboTextScriptRun, for the same reason: the direction resolver
 * (`RoboTextDirection.h`) produces these before a run set exists, and the two must agree about the type.
 */

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
    /**
     * The direction that **applies** to the run — since 12.8, the direction its resolved bidi level
     * implies rather than a classification of its own scalars.
     *
     * The two are the same answer for ordinary text and differ exactly where the algorithm adds
     * information: `"123 456"` with nothing declared is `Unknown` to 12.7 and level 0 to UAX #9, because
     * P2/P3 answer a paragraph with no strong character. A shaper needs the second, and a caller that
     * wants the first reads the direction runs — which are 12.7's classification, published beside these
     * (Phase 12.8 §"do not couple script and direction").
     */
    RoboTextDirection direction;
    /**
     * The resolved bidi level of the run's first scalar (Phase 12.8).
     *
     * `RoboTextBidiLevelNone` only for a record a caller built without resolving; every run this module
     * produces carries a level, because a paragraph always has one.
     */
    RoboTextBidiLevel bidiLevel;
    /** The direction the caller declared for the whole text. */
    RoboTextDirection baseDirection;
    /** The font the run is set in, by value; its `face` is borrowed. */
    RoboTextFontContext font;
    /** The language the caller declared, valid for the whole text and echoed per run. */
    RoboTextLanguage language;
    /**
     * The caller's OpenType feature settings, echoed per run (Phase 12.10).
     *
     * On the run rather than only on the options because a shaper consumes a *run*: the substitution engine
     * resolves the settings against the face's GSUB ScriptList/LangSys, which needs the run's script and
     * language — the other two fields here — and a generator handed a bare `RoboTextRange` would have to be
     * told them again. A run with no settings carries an empty set, which means "the script's defaults".
     */
    RoboTextFeatureSet features;
    /**
     * The primary axis the text runs along (Phase 12.20), echoed per run from the shape options.
     *
     * On the run rather than only on the options for the same reason `features` is: the glyph generator
     * consumes a *run*, and a run's advance axis, its `vmtx` lookup and its `vert`/`vrt2` settings are all
     * facts about it. A run built before this phase carries `Horizontal`, which is the zero value and the
     * correct answer for every run set that existed then.
     */
    RoboTextWritingMode writingMode;
    /** The requested `text-orientation` (Phase 12.20); inert for a horizontal run. */
    RoboTextTextOrientation textOrientation;
    /**
     * The **resolved** orientation of this run's glyphs (Phase 12.20): upright or sideways.
     *
     * One answer per run because the run is cut where the orientation changes, which is the phase's own
     * rule — a vertical line of `漢字ABC漢字` is three runs and no run mixes the two. The generator copies
     * it onto the glyph run so a painter never has to ask about a script to know how to draw a glyph.
     */
    RoboTextGlyphOrientation orientation;
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

/**
 * The base direction the **classification** ran with (Phase 12.7's answer).
 *
 * `RoboTextDirectionUnknown` is a real state here and 12.7's header argues for it: nothing was declared
 * and the text had no strong scalar, so the classification refuses to claim a direction. The base the
 * **resolution** ran with is @ref RoboTextRunSetBaseLevel — always 0 or 1, because UAX #9's P3 answers
 * level 0 — and @ref RoboTextRunSetBaseSource says where it came from, so a caller can tell the two
 * apart instead of having to choose between them.
 */
RoboTextDirection RoboTextRunSetBaseDirection(const RoboTextRunSet *set);

/* --- Phase 12.8: the resolution, beside the classification ------------------------------- */

/**
 * The paragraph's resolved base level: 0 for left-to-right, 1 for right-to-left.
 *
 * Never `RoboTextBidiLevelRemoved` and never `RoboTextBidiLevelNone`: an empty run set still has a
 * paragraph, and its base is whatever P2/P3 or the caller's declaration said.
 */
RoboTextBidiLevel RoboTextRunSetBaseLevel(const RoboTextRunSet *set);

/** Where the resolved base came from — declared, first strong, fallback, or P3's default. */
RoboTextBidiBaseSource RoboTextRunSetBaseSource(const RoboTextRunSet *set);

/**
 * The paragraph this set was resolved with: its levels, its logical runs and its visual order.
 *
 * Borrowed and owned by the set: it lives exactly as long as the set does. Never NULL for a set this
 * module produced, so `RoboTextBidiParagraphScalarCount` on it always answers.
 */
const RoboTextBidiParagraph *RoboTextRunSetBidiParagraph(const RoboTextRunSet *set);

/**
 * The **level runs** (UAX #9's own segmentation, `RoboTextBidiRun`): maximal spans sharing one level.
 *
 * Distinct from the direction runs beside them, and deliberately: a level run is a stretch of *one level*,
 * while a direction run — and a text run — is a stretch of one *parity*. They coincide for almost every
 * text and part company where a level moves without changing which way it runs: `"123 456"` in a
 * right-to-left paragraph is three level runs (2, 1, 2 — the digits keep their `EN` under an odd paragraph
 * level and I2 lifts them to 2, while the space between them takes the `R` they lend it under N1) and three
 * direction runs; the same text left-to-right is *one* level run at 0, because W7 finds the `sor` is `L`
 * and rewrites the digits as `L`. A consumer that needs the algorithm's own boundaries — a caret, a
 * mirrored glyph, a diagnostic — reads these; a consumer that needs shaping contexts reads the text runs.
 */
size_t RoboTextRunSetBidiRunCount(const RoboTextRunSet *set);

/** Level run @p index, or NULL when out of range. */
const RoboTextBidiRun *RoboTextRunSetBidiRun(const RoboTextRunSet *set, size_t index);

/**
 * The **visual** order of the level runs (L2), as logical level-run indices.
 *
 * What a painter walks. The direction runs and the text runs stay in source order: nothing in this module
 * is ever reordered, and the visual order is an array *beside* the runs.
 */
Result RoboTextRunSetVisualBidiRun(const RoboTextRunSet *set,
                                   size_t visual,
                                   size_t *outLogicalRunIndex,
                                   RoboTextDiag *diag);

/** The declared script hint, or `RoboTextScriptCommon` when none was given. */
/**
 * The writing mode every text run of @p set carries (Phase 12.20). Horizontal for a NULL set.
 *
 * The set-level copy of a run field, for the same reason the script hint has one: a caller that wants to
 * know how the text was laid out asks once, and a caller checking a run against the request compares one
 * field.
 */
RoboTextWritingMode RoboTextRunSetWritingMode(const RoboTextRunSet *set);

/** See @ref RoboTextRunSetWritingMode: the requested `text-orientation`. Mixed for a NULL set. */
RoboTextTextOrientation RoboTextRunSetTextOrientation(const RoboTextRunSet *set);

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
    /** Text runs whose direction is RTL. */
    size_t rtlRuns;
    /**
     * Text runs whose direction is `RoboTextDirectionUnknown`.
     *
     * **Zero for every set this build produces**, and that is the honest number rather than a dead field:
     * 12.8 resolves a level for every scalar, so every text run has a direction. The `Unknown` state 12.7
     * established now lives on the *direction runs*, which are published beside these and keep their
     * classification; a set built by a caller that skipped resolution would report it here again.
     */
    size_t unknownDirectionRuns;
    /** Distinct scripts across the text runs. */
    size_t distinctScripts;
    /** Scalars segmented. */
    RoboTextScalarIndex scalars;
    /** Level runs (Phase 12.8). */
    size_t bidiRuns;
    /** The resolved paragraph level: 0 or 1. */
    RoboTextBidiLevel baseLevel;
    /** Scalars X9 removed from reordering (format controls and `BN`). */
    size_t removedScalars;
    /** Scalars whose resolved level is odd. */
    size_t rtlScalars;
} RoboTextRunStats;

Result RoboTextRunSetGetStats(const RoboTextRunSet *set, RoboTextRunStats *out);

/** Fuzz entry: segment arbitrary bytes as text. Never crashes; a result is destroyed if it was made. */
Result RoboTextFuzzSegment(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_RUNS_H */
