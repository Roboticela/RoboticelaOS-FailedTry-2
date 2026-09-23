/*
 * A glyph run list: the shaped text of one paragraph, in logical order, with a visual order beside it
 * (Phase 12.14).
 *
 * ## What this type is for
 *
 * 12.4 introduced `RoboTextGlyphRun` — one run of glyphs, one font, one script, one level — and
 * `RoboTextShapeResult` holds a set of them. A set reached by `RoboTextShapeResultGlyphRun(result, i)` is
 * enough to *draw*, and it was enough for the three phases that followed. It is not enough for the four
 * questions the phases *after* this one ask, and this header exists for exactly those:
 *
 *   - **which runs are one paragraph**, and what the whole of it measures to;
 *   - **where each run sits visually**, which is not where it sits logically;
 *   - **whether two neighbouring runs are the same shape run** — so that a merge is provably safe and a
 *     split is provably necessary, rather than a comparison someone wrote twice and got wrong once;
 *   - **whether the set is internally consistent** — no overlapping sources, no gaps, an order that is the
 *     text's own.
 *
 * ## Logical order is the list, visual order is an array beside it
 *
 * The list **is** the logical order: `RoboTextGlyphRunListRun(list, i)` is the @p i-th run in source order,
 * and that index is its logical index (Phase 12.14.27). The visual order is a **permutation of those
 * indices**, in a separate array (Phase 12.14.28), and it is deliberately not a second list of run
 * pointers: a painter walking `visual 0..n-1` and asking for the logical run each time cannot accidentally
 * destroy the source mapping that selection, hit testing and a caret all read, because there is nothing to
 * destroy. The two answers to "which run is a right-to-left run's first" are both available for as long as
 * the list lives.
 *
 * Reordering nothing but the *indices* is the whole of what this module does with direction: the runs carry
 * levels resolved by Phase 12.8's paragraph, and this type publishes them in L2's order. It contains no
 * bidirectional algorithm and no reordering pass of its own.
 *
 * ## Ownership
 *
 * The list **owns** the runs it is given, and releases them when it is released. That is the lifetime a
 * shape result wants — it produces the runs and then hands them over — and it is why `Create` takes the
 * array rather than copying the pointers. A caller that wants to keep a run must build its own list, and
 * `RoboTextShapeResultGlyphRunList` is the borrowed view of a result's own list.
 */
#ifndef ROBO_TEXT_RUN_LIST_H
#define ROBO_TEXT_RUN_LIST_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextBidi.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextFont.h"
#include "RoboText/RoboTextGlyphs.h"
#include "RoboText/RoboTextRuns.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Everything that makes two runs *different* runs, by the shaping state that produced them (Phase 12.14.13).
 *
 * The record a run is split on and merged by, so that "these two may be one run" is one comparison a suite can
 * call rather than two call sites that have to agree. It carries **no source range**: a range says where a run
 * is, and this key says what it is. Two runs at different places in the text are the same shape run, which is
 * exactly the fact a cache keyed on this needs and exactly the fact a merge check adds contiguity to.
 *
 * `font` is the whole context — the borrowed face **and** the style it is set at — because a 16px Inter and a
 * 32px Inter are not one shaped result and a key that compared the family name would say they were. The face
 * is compared by identity, never by name: two faces with the same family are still two faces (Phase 12.14.15).
 */
typedef struct RoboTextGlyphRunKey {
    RoboTextFontContext font;
    RoboTextScript script;
    RoboTextDirection direction;
    RoboTextBidiLevel bidiLevel;
    RoboTextDirection baseDirection;
    RoboTextLanguage language;
    /**
     * The caller's feature settings, by value.
     *
     * The field that makes a `kern`-off run a different run from a `kern`-on one, and the reason a
     * range-specific override (Phase 12.13 §9) produces two runs rather than one run with a footnote.
     */
    RoboTextFeatureSet features;
    /**
     * How far the face's tables got with this run.
     *
     * Part of the key, which is worth stating because it looks like an *output*: a run the cmap produced and a
     * run the same text produces after GSUB are different glyph arrays, and a cache that keyed only on the
     * inputs could hand a substituted run's glyphs to a caller who asked for the unsubstituted ones. The mode
     * is a function of the face and the settings, both of which are in the key, so it never *splits* two runs
     * that could have merged; it only refuses to merge two that measured differently.
     */
    RoboTextGlyphGenerationMode mode;
} RoboTextGlyphRunKey;

/**
 * Which part of two keys differed (Phase 12.14.11).
 *
 * The difference *named* rather than merely detected, because "these two runs are separate" is a claim a
 * diagnostic should be able to justify: a text split into three runs where the reader expected one is either a
 * font fallback, a script change, a level change or a feature override, and the enumerator says which. It is
 * also what makes deterministic splitting testable without a second segmentation pass — the test asserts the
 * *reason*, not just the count.
 */
typedef enum RoboTextGlyphRunKeyField {
    /** The keys are equal. */
    RoboTextGlyphRunKeyFieldNone = 0,
    RoboTextGlyphRunKeyFieldFont,
    RoboTextGlyphRunKeyFieldScript,
    RoboTextGlyphRunKeyFieldDirection,
    RoboTextGlyphRunKeyFieldBidiLevel,
    RoboTextGlyphRunKeyFieldBaseDirection,
    RoboTextGlyphRunKeyFieldLanguage,
    RoboTextGlyphRunKeyFieldFeatures,
    RoboTextGlyphRunKeyFieldMode
} RoboTextGlyphRunKeyField;

/** Non-zero when @p field is a defined enumerator. */
int RoboTextGlyphRunKeyFieldIsKnown(RoboTextGlyphRunKeyField field);

/** Name of @p field (`"script"`); `"none"` for an equal pair. Never NULL. */
const char *RoboTextGlyphRunKeyFieldName(RoboTextGlyphRunKeyField field);

/** @p run's key. RoboInvalidArgument for a NULL argument. */
Result RoboTextGlyphRunKeyOf(const RoboTextGlyphRun *run, RoboTextGlyphRunKey *out);

/** Field-for-field equality. Safe on NULL (NULL == NULL). */
int RoboTextGlyphRunKeyEqual(const RoboTextGlyphRunKey *a, const RoboTextGlyphRunKey *b);

/**
 * The first field of @p a that differs from @p b, or @ref RoboTextGlyphRunKeyFieldNone when they are equal.
 *
 * The *first in the enumerator's order*, not the first that happens to be checked: a caller comparing two
 * diagnostic dumps gets the same reason for the same pair every time.
 */
RoboTextGlyphRunKeyField RoboTextGlyphRunKeyDifference(const RoboTextGlyphRunKey *a, const RoboTextGlyphRunKey *b);

/**
 * Why a set of runs is not a valid list (Phase 12.14.37).
 *
 * A type of its own rather than run problems reused, because the two questions are different: "is this run a
 * valid record" is asked of one run, and "is this set one list" is asked of the arrangement. The one place they
 * meet is @ref RoboTextGlyphRunListProblemRun, which means "a run of mine is not a valid run" and leaves *which
 * run*, and *which of its invariants*, to the diagnostic message and to `RoboTextGlyphRunValidate`.
 *
 * @ref RoboTextGlyphRunListProblemOrder and @ref RoboTextGlyphRunListProblemOverlap are the two a *constructor*
 * also refuses (`RoboTextGlyphRunListCreate`), so no list this module builds can reach them. They are kept and
 * checked anyway, and that is deliberate rather than dead code: the constructor's refusal is about what a caller
 * may hand over, this is about what a list may *be*, and a second reading of the same rule at the point where
 * the record is published is what makes the two impossible to disagree.
 */
typedef enum RoboTextGlyphRunListProblem {
    /** Every invariant holds. */
    RoboTextGlyphRunListProblemNone = 0,
    /** The runs are not in ascending source order. */
    RoboTextGlyphRunListProblemOrder,
    /** Two runs cover the same scalar. */
    RoboTextGlyphRunListProblemOverlap,
    /** A run of the list is not a valid run. */
    RoboTextGlyphRunListProblemRun,
    /** The visual order is not a permutation of `0..count-1`. */
    RoboTextGlyphRunListProblemVisualOrder,
    /** The stored total is not the sum of the runs' own advances. */
    RoboTextGlyphRunListProblemTotal
} RoboTextGlyphRunListProblem;

/** Non-zero when @p problem is a defined enumerator. */
int RoboTextGlyphRunListProblemIsKnown(RoboTextGlyphRunListProblem problem);

/** Name of @p problem (`"overlap"`); `"none"` for the valid answer. Never NULL. */
const char *RoboTextGlyphRunListProblemName(RoboTextGlyphRunListProblem problem);

typedef struct RoboTextGlyphRunList RoboTextGlyphRunList;

/**
 * A list over @p runs, taking ownership of the array and of every run in it.
 *
 * The list is created in **logical order** and its visual order is the logical one until someone says
 * otherwise (Phase 12.14.28), which is what `RoboTextGlyphRunListVisualIsLogical` reports. Nothing about
 * creating a list requires a bidi paragraph: a caller that has one asks `RoboTextGlyphRunListOrderVisual`, and a
 * caller that does not gets a list that is honest about not having been reordered rather than one that
 * silently claims an order it did not compute.
 *
 * The runs must not overlap in source and must be in ascending order (Phase 12.14.37); a list that was handed
 * a set which breaks that is refused rather than ordered, because "the list fixes it" would mean two callers
 * disagreeing about which run an index names.
 *
 * **Ownership transfers only on success.** A refusal or an allocation failure leaves @p runs and every run in it
 * with the caller, who is free to release them; past a returned @ref RoboOk they belong to the list. That is one
 * rule rather than two so that a caller has one thing to remember, and it is the reason the constructor does not
 * call the full destructor on its own failure paths.
 *
 * @param runs   the array to adopt, in logical order. May be NULL when @p count is 0.
 * @param out    receives the list; freed with @ref RoboTextGlyphRunListDestroy, which also frees the runs.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument, a NULL run in @p runs, or an out-of-order
 *         set; RoboOutOfMemory. On any failure @p out is NULL and the runs are still the caller's.
 */
Result RoboTextGlyphRunListCreate(RoboTextGlyphRun **runs,
                                  size_t count,
                                  RoboTextGlyphRunList **out,
                                  RoboTextDiag *diag);

/**
 * A list over runs it does **not** own: the view a shape result hands out (Phase 12.14.26).
 *
 * The same list, a different lifetime, and the difference is worth a second door rather than a flag on the
 * first: a caller reading a result's shaped runs must not be able to free them by destroying a view, and a
 * caller building its own set must not have to know that it may hand them over. So the two constructions are
 * separate functions with separate contracts, and `RoboTextGlyphRunListCoalesce` — the one operation that
 * replaces a run — **refuses** a view, because a merge would free runs that belong to someone else.
 *
 * The runs must outlive the list, which is what makes a view safe: it holds pointers into an array its owner
 * keeps, and it holds them for exactly as long as the owner does.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL argument, a NULL run in @p runs, or an out-of-order
 *         set; RoboOutOfMemory.
 */
Result RoboTextGlyphRunListCreateView(RoboTextGlyphRun *const *runs,
                                      size_t count,
                                      RoboTextGlyphRunList **out,
                                      RoboTextDiag *diag);

/**
 * Release @p list and the order arrays. A list built by @ref RoboTextGlyphRunListCreate also releases the runs
 * it adopted; a view (see @ref RoboTextGlyphRunListCreateView) releases only the list itself. Safe on NULL.
 */
void RoboTextGlyphRunListDestroy(RoboTextGlyphRunList *list);

/**
 * Non-zero when @p list owns the runs it was built over.
 *
 * The fact `RoboTextGlyphRunListCoalesce` refuses on, published so that a caller does not have to find out by
 * having a merge refused: a diagnostic can say which kind of list it is looking at.
 */
int RoboTextGlyphRunListOwnsRuns(const RoboTextGlyphRunList *list);

/** Runs held, which is also the number of logical indices. */
size_t RoboTextGlyphRunListCount(const RoboTextGlyphRunList *list);

/** The run at logical index @p logicalIndex, borrowed, or NULL when out of range. */
const RoboTextGlyphRun *RoboTextGlyphRunListRun(const RoboTextGlyphRunList *list, size_t logicalIndex);

/** The key of logical run @p logicalIndex. RoboInvalidArgument for a NULL argument or an index past the list. */
Result RoboTextGlyphRunListKey(const RoboTextGlyphRunList *list,
                               size_t logicalIndex,
                               RoboTextGlyphRunKey *out);

/**
 * The scalars the whole list covers: from the first run's start to the last run's end.
 *
 * The empty range at 0 for an empty list, so a caller can ask without checking first.
 */
RoboTextRange RoboTextGlyphRunListSourceRange(const RoboTextGlyphRunList *list);

/** The sum of every run's advance, in device pixels: what the paragraph measures to (Phase 12.14.19). */
float RoboTextGlyphRunListAdvance(const RoboTextGlyphRunList *list);

/** See @ref RoboTextGlyphRunListAdvance: the vertical total, zero for horizontal text. */
float RoboTextGlyphRunListAdvanceY(const RoboTextGlyphRunList *list);

/**
 * The metrics a line box uses for the list (Phase 12.14.18).
 *
 * Taken from the **first** run, because every run in a list is set in a font, and a paragraph whose runs
 * disagree about their metrics is a paragraph whose lines do not line up. The choice is stated rather than
 * hidden: a caller that wants a per-run answer reads the run, and one that wants the paragraph's reads this.
 */
Result RoboTextGlyphRunListMetrics(const RoboTextGlyphRunList *list, RoboTextFontMetrics *out);

/** Visual positions, which is the run count: the visual order is a permutation, not a subset. */
size_t RoboTextGlyphRunListVisualCount(const RoboTextGlyphRunList *list);

/**
 * The logical index of the @p visual-th run to paint (UAX #9 L2's answer, via Phase 12.8).
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument, a NULL @p outLogical, or @p visual past the count.
 */
Result RoboTextGlyphRunListVisualRun(const RoboTextGlyphRunList *list,
                                     size_t visual,
                                     size_t *outLogical);

/**
 * The inverse of @ref RoboTextGlyphRunListVisualRun: where logical run @p logical sits visually.
 *
 * Both directions, because the two are asked by different callers: a painter walks visual order, a caret maps
 * a source position to a place on the line and therefore goes logical → visual.
 */
Result RoboTextGlyphRunListVisualIndex(const RoboTextGlyphRunList *list,
                                       size_t logical,
                                       size_t *outVisual);

/**
 * Non-zero when the visual order is the logical one.
 *
 * True for a single-run list, for a left-to-right paragraph, and for a list whose order has not been computed.
 * Reported rather than inferred from "the arrays are equal", because a caller *checking* that fact is doing
 * work, and this says it in one call.
 */
int RoboTextGlyphRunListVisualIsLogical(const RoboTextGlyphRunList *list);

/**
 * Put the list in @p paragraph's **visual** order (Phase 12.14.9).
 *
 * The one reordering door, and it reorders **indices**, never the runs: each run is placed where the level run
 * containing its source sits, and runs inside one level run keep their logical order — which is L2 applied to
 * runs by *reading* the paragraph's own answer (`RoboTextBidiRun::visualIndex`) rather than by running the
 * algorithm a second time. A glyph run that spans two level runs is placed with the one its source *starts*
 * in, and that is a real (if rare) case: it is why a shaping boundary and a bidi boundary need not coincide.
 *
 * @param paragraph  the paragraph the runs were resolved against. It must cover the list's source: a paragraph
 *                   that does not is refused rather than allowed to place a run by a level it never gave.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument, a paragraph that does not cover the list, or an
 *         empty list; RoboOutOfMemory when the order array cannot be allocated (in which case the previous
 *         order is left intact rather than half-written).
 */
Result RoboTextGlyphRunListOrderVisual(RoboTextGlyphRunList *list,
                                       const RoboTextBidiParagraph *paragraph,
                                       RoboTextDiag *diag);

/**
 * Merge adjacent runs that are provably one shape run (Phase 12.14.12).
 *
 * Two runs are merged when `RoboTextGlyphRunKeyEqual` holds **and** the first's source ends exactly where the
 * second's begins. Both halves are required and neither is sufficient: equal keys across a gap would merge two
 * runs with text between them that the list does not own, and contiguous sources with different keys would
 * merge a Latin run into an Arabic one.
 *
 * The merged run is a real run, built by concatenation and re-finalized, so its clusters, flags, totals and
 * glyph indices are the concatenation's. That is why this lives in the module and not in a caller: a merge that
 * added the two totals and kept the first run's clusters would be a run whose own validation fails.
 *
 * The visual order is recomputed as the logical one, because the run count changed and the old permutation
 * names runs that no longer exist. A caller that wants a reordered list orders it again afterwards.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL argument or a **view** (see
 *         @ref RoboTextGlyphRunListCreateView — a merge replaces runs, and a view does not own theirs);
 *         RoboOutOfMemory when a merge cannot be allocated, in which case the list is left as it was rather than
 *         partially merged.
 */
Result RoboTextGlyphRunListCoalesce(RoboTextGlyphRunList *list, RoboTextDiag *diag);

/**
 * Check every list invariant, and every run's (Phases 12.14.36, 12.14.37). O(glyphs).
 *
 * The list's own invariants, on top of each run's:
 *
 *   - runs in ascending source order, and **no two overlapping** (Phase 12.14.37);
 *   - every run's source inside the list's;
 *   - the visual order a permutation of `0..count-1` with each index exactly once;
 *   - the total advance the sum of the runs' own.
 *
 * A list with a gap between two runs is **valid**: a caller may hold the shaped runs of a text whose middle was
 * shaped elsewhere, and refusing that would refuse a legitimate set. Overlap is refused because two runs
 * claiming one scalar is not a set of runs at all.
 *
 * @return RoboOk when valid; RoboInvalidArgument for a NULL argument or an invalid list — with @p outProblem
 *         naming the first broken invariant, including a run's own problem passed through.
 */
Result RoboTextGlyphRunListValidate(const RoboTextGlyphRunList *list,
                                    RoboTextGlyphRunListProblem *outProblem,
                                    RoboTextDiag *diag);

/**
 * The list as a multi-line diagnostic block (Phase 12.14.35): a header line per run, then each run's own block.
 *
 * Complete or absent, like every dump here, and `INVALID` first when validation fails — see
 * `RoboTextGlyphRunWrite`, whose block this composes and whose rules it follows.
 *
 * @return RoboOk and @p outLength; RoboInvalidArgument for a NULL argument; RoboNotSupported when @p buffer
 *         cannot hold the block.
 */
Result RoboTextGlyphRunListWrite(const RoboTextGlyphRunList *list,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_RUN_LIST_H */
