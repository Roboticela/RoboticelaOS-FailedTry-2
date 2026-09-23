/*
 * RoboText's glyph run list (Phase 12.14) — see RoboTextRunList.h.
 *
 * The file is four pieces, in the order a caller meets them:
 *
 *   **the key**       what makes two runs different runs, and *which* field made them differ.
 *   **the list**      ownership, logical order, and the four questions a paragraph asks of a shaped set.
 *   **the order**     the visual permutation, read out of Phase 12.8's paragraph rather than computed.
 *   **coalescing**    two runs into one, by concatenation and re-finalization.
 *
 * ## Why the reordering is not here
 *
 * A run list has to publish a visual order, and the tempting way to do it is to write L2 — reverse the runs at
 * each level from the highest down — in this file, over the runs' levels. That would be a *second*
 * implementation of the Unicode bidirectional algorithm in a tree that has one (`RoboTextBidi.c`, Phase 12.8),
 * and the two would drift the first time a level resolution changed. So this file does not reorder: it reads
 * `RoboTextBidiRun::visualIndex` out of the paragraph and uses it as a **sort key**. The paragraph already
 * answered "where does this level run sit visually", once, for the whole text; a list of runs asks the same
 * question of the answer rather than of the text.
 *
 * That decision has a visible consequence worth stating: a list ordered by a paragraph places a glyph run by
 * the level run its source *starts* in. A shaping boundary and a bidi boundary need not coincide — a run can
 * straddle two level runs — and when they do, the earlier one wins, because a run has one level by
 * construction and the run's own `bidiLevel` is the level of its first scalar (Phase 12.8).
 *
 * ## Why coalescing rebuilds rather than appends
 *
 * Merging two runs is the one operation in this module that writes a `RoboTextGlyphRun`, and it does it by
 * handing the concatenation to `RoboTextGlyphRunFinalize` — the same function generation ends with. A merge
 * that summed the totals and kept the first run's clusters would produce a run whose glyph indices point at
 * clusters that ended three glyphs ago, and `RoboTextGlyphRunValidate` would refuse it in a test. There is one
 * writer of a run's derived fields and this is not it.
 */

#include "RoboText/RoboTextRunList.h"

#include "RoboMem/RoboMem.h"
#include "RoboTextGlyphsInternal.h"
#include "RoboTextInternal.h"

#include <stdint.h>
#include <string.h>

struct RoboTextGlyphRunList {
    /**
     * The runs, in logical order. Index *is* the logical index.
     *
     * Owned when @ref ownsRuns is set and borrowed otherwise; the two constructions are separate public doors
     * (`RoboTextGlyphRunListCreate` and `...CreateView`) so that a caller never has to ask which it got, and the
     * flag exists only so that the destructor and the coalescer can tell.
     */
    RoboTextGlyphRun **runs;
    /** Non-zero when the runs are this list's to free. */
    int ownsRuns;
    size_t count;
    /** One key per run, derived once at creation and after a merge: a key is read by the coalescer per pair. */
    RoboTextGlyphRunKey *keys;
    /** The visual order: `visual[visualIndex]` is a logical index. NULL until an order is asked for. */
    size_t *visual;
    /** The inverse of @p visual: `inverse[logicalIndex]` is a visual index. NULL with @p visual. */
    size_t *inverse;
    /** Non-zero when @p visual is the logical order — either because none was computed, or because the
     *  paragraph's order happened to be the logical one. */
    int visualIsLogical;
    double advanceTotal;
    double advanceTotalY;
};

/* ------------------------------------------------------------------ */
/* The key (Phase 12.14.13)                                            */
/* ------------------------------------------------------------------ */

int RoboTextGlyphRunKeyFieldIsKnown(RoboTextGlyphRunKeyField field)
{
    return field >= RoboTextGlyphRunKeyFieldNone && field <= RoboTextGlyphRunKeyFieldMode;
}

const char *RoboTextGlyphRunKeyFieldName(RoboTextGlyphRunKeyField field)
{
    switch (field) {
        case RoboTextGlyphRunKeyFieldNone:
            return "none";
        case RoboTextGlyphRunKeyFieldFont:
            return "font";
        case RoboTextGlyphRunKeyFieldScript:
            return "script";
        case RoboTextGlyphRunKeyFieldDirection:
            return "direction";
        case RoboTextGlyphRunKeyFieldBidiLevel:
            return "bidi-level";
        case RoboTextGlyphRunKeyFieldBaseDirection:
            return "base-direction";
        case RoboTextGlyphRunKeyFieldLanguage:
            return "language";
        case RoboTextGlyphRunKeyFieldFeatures:
            return "features";
        case RoboTextGlyphRunKeyFieldMode:
            return "mode";
        default:
            return "unknown";
    }
}

Result RoboTextGlyphRunKeyOf(const RoboTextGlyphRun *run, RoboTextGlyphRunKey *out)
{
    if (!run || !out) {
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboInvalidArgument,
                               "a run and an out pointer are required for a key");
    }
    memset(out, 0, sizeof(*out));
    out->font = run->font;
    out->script = run->script;
    out->direction = run->direction;
    out->bidiLevel = run->bidiLevel;
    out->baseDirection = run->baseDirection;
    out->language = run->language;
    out->features = run->features;
    out->mode = run->mode;
    return RoboOk;
}

int RoboTextGlyphRunKeyEqual(const RoboTextGlyphRunKey *a, const RoboTextGlyphRunKey *b)
{
    if (a == b) {
        return 1;
    }
    if (!a || !b) {
        return 0;
    }
    return RoboTextGlyphRunKeyDifference(a, b) == RoboTextGlyphRunKeyFieldNone;
}

RoboTextGlyphRunKeyField RoboTextGlyphRunKeyDifference(const RoboTextGlyphRunKey *a, const RoboTextGlyphRunKey *b)
{
    if (!a || !b) {
        return RoboTextGlyphRunKeyFieldNone;
    }
    /*
     * The fields are compared **in the enumerator's order and not short-circuited past it**, so that the answer
     * is a property of the pair rather than of the order someone wrote the tests in. A pair differing in two
     * fields answers the field that comes first in the enum, every time, in every caller.
     */
    if (!RoboTextFontContextEqual(&a->font, &b->font)) {
        return RoboTextGlyphRunKeyFieldFont;
    }
    if (a->script != b->script) {
        return RoboTextGlyphRunKeyFieldScript;
    }
    if (a->direction != b->direction) {
        return RoboTextGlyphRunKeyFieldDirection;
    }
    if (a->bidiLevel != b->bidiLevel) {
        return RoboTextGlyphRunKeyFieldBidiLevel;
    }
    if (a->baseDirection != b->baseDirection) {
        return RoboTextGlyphRunKeyFieldBaseDirection;
    }
    if (!RoboTextLanguageEqual(&a->language, &b->language)) {
        return RoboTextGlyphRunKeyFieldLanguage;
    }
    if (!RoboTextFeatureSetEqual(&a->features, &b->features)) {
        return RoboTextGlyphRunKeyFieldFeatures;
    }
    if (a->mode != b->mode) {
        return RoboTextGlyphRunKeyFieldMode;
    }
    return RoboTextGlyphRunKeyFieldNone;
}

/* ------------------------------------------------------------------ */
/* The list                                                            */
/* ------------------------------------------------------------------ */

/**
 * Release the list's own storage — its key array, its order arrays and the list itself — and nothing else.
 *
 * The two callers that need this are the failure paths of `RunListCreate`, and the reason is an ownership rule
 * rather than tidiness: **a construction that fails must not consume what it was handed.** A caller that hands
 * over an array and gets a status back has to be free to release its own runs, and a constructor that freed
 * them on the way out would leave that caller with dangling pointers it has no way to know about.
 */
static void RunListFreeShell(RoboTextGlyphRunList *list)
{
    RoboFree(list->keys);
    RoboFree(list->visual);
    RoboFree(list->inverse);
    RoboFree(list);
}

void RoboTextGlyphRunListDestroy(RoboTextGlyphRunList *list)
{
    if (!list) {
        return;
    }
    if (list->ownsRuns) {
        for (size_t index = 0u; index < list->count; index++) {
            RoboTextGlyphRunDestroy(list->runs ? list->runs[index] : NULL);
        }
        /* Only an adopting list owns the array it was handed; a view's array belongs to its owner. */
        RoboFree(list->runs);
    }
    RunListFreeShell(list);
}

int RoboTextGlyphRunListOwnsRuns(const RoboTextGlyphRunList *list)
{
    return list ? list->ownsRuns : 0;
}

/**
 * Fill @p list's keys and totals from its runs, and clear the visual order.
 *
 * Returns a @ref Result rather than discarding the key door's answer. The door cannot fail here — every slot
 * holds a non-NULL run, which `RunListCheckOrder` established — but "cannot fail" and "not compared" are
 * different things, and a door whose status is dropped is a door whose failure mode nobody will notice when the
 * guarantee does change.
 */
static Result RunListRefresh(RoboTextGlyphRunList *list)
{
    list->advanceTotal = 0.0;
    list->advanceTotalY = 0.0;
    for (size_t index = 0u; index < list->count; index++) {
        const RoboTextGlyphRun *run = list->runs[index];
        Result status = RoboTextGlyphRunKeyOf(run, &list->keys[index]);

        if (status != RoboOk) {
            return status;
        }
        list->advanceTotal += (double)RoboTextGlyphRunAdvance(run);
        list->advanceTotalY += (double)RoboTextGlyphRunAdvanceY(run);
    }
    /*
     * The order is cleared rather than kept, because a merge changes the run count and a permutation of the old
     * count names runs that no longer exist. Clearing it means "the order has not been computed", which the
     * accessors report as the logical order — the honest answer, and never a stale permutation.
     */
    RoboFree(list->visual);
    RoboFree(list->inverse);
    list->visual = NULL;
    list->inverse = NULL;
    list->visualIsLogical = 1;
    return RoboOk;
}

/**
 * The invariant a set of runs must have before it can *be* a list: ascending sources that do not overlap.
 *
 * Checked at creation rather than left to `RoboTextGlyphRunListValidate`, because the list's whole interface is
 * indexing — `run[i]` and `visual[i]` — and a set that overlaps is a set where "the run covering scalar 7" has
 * two answers. A gap is allowed (a caller may hold the runs of a text whose middle was shaped elsewhere);
 * overlap is not.
 */
static Result RunListCheckOrder(RoboTextGlyphRun *const *runs, size_t count, RoboTextDiag *diag)
{
    for (size_t index = 0u; index < count; index++) {
        if (!runs[index]) {
            return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                                   "a run list cannot hold a NULL run");
        }
        if (index != 0u) {
            RoboTextRange previous = runs[index - 1u]->source;
            RoboTextRange current = runs[index]->source;

            if (RoboTextRangeIsInverted(current)) {
                return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                                       "a run list's run has an inverted source range");
            }
            if (current.start < previous.start) {
                return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                                       "a run list's runs must be in ascending source order");
            }
            if (RoboTextRangeIntersects(previous, current)) {
                return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                                       "two runs of a list must not cover the same scalar");
            }
        }
    }
    return RoboOk;
}

/**
 * The body of both public constructions.
 *
 * @p ownsRuns is the one difference between them, and it is a parameter rather than a copied function because
 * everything else — the ordering check, the key array, the totals, the cleared visual order — must be identical
 * for the two kinds of list. A view that skipped the ordering check would be a list whose indices mean nothing.
 */
static Result RunListCreate(RoboTextGlyphRun **runs,
                            size_t count,
                            int ownsRuns,
                            RoboTextGlyphRunList **out,
                            RoboTextDiag *diag)
{
    RoboTextGlyphRunList *list;
    Result status;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a run list needs an out pointer");
    }
    *out = NULL;
    if (!runs && count != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a run list of non-zero count needs an array");
    }
    status = RunListCheckOrder(runs, count, diag);
    if (status != RoboOk) {
        return status;
    }

    list = (RoboTextGlyphRunList *)RoboCalloc(1u, sizeof(*list));
    if (!list) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the run list could not be allocated");
    }
    /*
     * The array is **adopted**, not copied: `Create` documents that it takes ownership, and a copy would leave
     * the caller holding pointers whose runs the list would free — two owners, one lifetime, and a double free
     * the first time a caller did the natural thing. A view adopts the *pointers* and not the array, which is
     * why the destructor frees one and not the other.
     */
    list->runs = runs;
    list->count = count;
    list->ownsRuns = ownsRuns;
    if (count != 0u) {
        list->keys = (RoboTextGlyphRunKey *)RoboCalloc(count, sizeof(*list->keys));
        if (!list->keys) {
            RunListFreeShell(list);
            return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                                   "the run list's keys could not be allocated");
        }
    }
    status = RunListRefresh(list);
    if (status != RoboOk) {
        RunListFreeShell(list);
        return status;
    }
    /*
     * Past this line the list owns its runs, and every path above returned without touching them: the array was
     * adopted into `list->runs` but `Destroy` was never called on the shell, which is what keeps the ownership
     * transfer to a single point (`*out`) rather than a claim made at the top of the function.
     */
    *out = list;
    return RoboOk;
}

Result RoboTextGlyphRunListCreate(RoboTextGlyphRun **runs,
                                  size_t count,
                                  RoboTextGlyphRunList **out,
                                  RoboTextDiag *diag)
{
    return RunListCreate(runs, count, 1, out, diag);
}

Result RoboTextGlyphRunListCreateView(RoboTextGlyphRun *const *runs,
                                      size_t count,
                                      RoboTextGlyphRunList **out,
                                      RoboTextDiag *diag)
{
    /* The cast is the view's whole point: the array is not this list's to write through, and every accessor
     * below hands out a `const RoboTextGlyphRun *` so that no caller can reach a mutable run through it. */
    return RunListCreate((RoboTextGlyphRun **)(uintptr_t)runs, count, 0, out, diag);
}

size_t RoboTextGlyphRunListCount(const RoboTextGlyphRunList *list)
{
    return list ? list->count : 0u;
}

const RoboTextGlyphRun *RoboTextGlyphRunListRun(const RoboTextGlyphRunList *list, size_t logicalIndex)
{
    if (!list || logicalIndex >= list->count) {
        return NULL;
    }
    return list->runs[logicalIndex];
}

Result RoboTextGlyphRunListKey(const RoboTextGlyphRunList *list,
                               size_t logicalIndex,
                               RoboTextGlyphRunKey *out)
{
    if (!list || !out) {
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboInvalidArgument,
                               "a list and an out pointer are required");
    }
    if (logicalIndex >= list->count) {
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboInvalidArgument,
                               "that logical index is past the list");
    }
    *out = list->keys[logicalIndex];
    return RoboOk;
}

RoboTextRange RoboTextGlyphRunListSourceRange(const RoboTextGlyphRunList *list)
{
    if (!list || list->count == 0u) {
        return RoboTextRangeAt(0u);
    }
    return RoboTextRangeMake(list->runs[0]->source.start, list->runs[list->count - 1u]->source.end);
}

float RoboTextGlyphRunListAdvance(const RoboTextGlyphRunList *list)
{
    return list ? (float)list->advanceTotal : 0.0f;
}

float RoboTextGlyphRunListAdvanceY(const RoboTextGlyphRunList *list)
{
    return list ? (float)list->advanceTotalY : 0.0f;
}

Result RoboTextGlyphRunListMetrics(const RoboTextGlyphRunList *list, RoboTextFontMetrics *out)
{
    if (!list || !out) {
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboInvalidArgument,
                               "a list and an out pointer are required");
    }
    if (list->count == 0u) {
        /*
         * No runs, no face, and therefore no metrics — reported rather than answered with zeroes, because a
         * caller with an empty list has nothing to lay out and a zeroed line box would let it pretend it had.
         */
        memset(out, 0, sizeof(*out));
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboNotFound,
                               "an empty run list has no metrics");
    }
    return RoboTextGlyphRunMetrics(list->runs[0], out);
}

/* ------------------------------------------------------------------ */
/* Visual order (Phase 12.14.9, 12.14.28)                              */
/* ------------------------------------------------------------------ */

size_t RoboTextGlyphRunListVisualCount(const RoboTextGlyphRunList *list)
{
    return list ? list->count : 0u;
}

Result RoboTextGlyphRunListVisualRun(const RoboTextGlyphRunList *list,
                                     size_t visual,
                                     size_t *outLogical)
{
    if (!list || !outLogical) {
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboInvalidArgument,
                               "a list and an out pointer are required");
    }
    if (visual >= list->count) {
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboInvalidArgument,
                               "that visual index is past the list");
    }
    *outLogical = list->visual ? list->visual[visual] : visual;
    return RoboOk;
}

Result RoboTextGlyphRunListVisualIndex(const RoboTextGlyphRunList *list,
                                       size_t logical,
                                       size_t *outVisual)
{
    if (!list || !outVisual) {
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboInvalidArgument,
                               "a list and an out pointer are required");
    }
    if (logical >= list->count) {
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboInvalidArgument,
                               "that logical index is past the list");
    }
    *outVisual = list->inverse ? list->inverse[logical] : logical;
    return RoboOk;
}

int RoboTextGlyphRunListVisualIsLogical(const RoboTextGlyphRunList *list)
{
    return list ? list->visualIsLogical : 1;
}

/** The index of the level run @p paragraph covers @p scalar with, or the paragraph's run count. */
static size_t LevelRunContaining(const RoboTextBidiParagraph *paragraph, RoboTextScalarIndex scalar)
{
    size_t count = RoboTextBidiParagraphRunCount(paragraph);

    for (size_t index = 0u; index < count; index++) {
        const RoboTextBidiRun *run = RoboTextBidiParagraphRun(paragraph, index);

        if (run && RoboTextRangeContains(run->range, scalar)) {
            return index;
        }
    }
    return count;
}

Result RoboTextGlyphRunListOrderVisual(RoboTextGlyphRunList *list,
                                       const RoboTextBidiParagraph *paragraph,
                                       RoboTextDiag *diag)
{
    size_t *visual = NULL;
    size_t *inverse = NULL;
    size_t *keys = NULL;
    size_t count;

    if (!list || !paragraph) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a list and a paragraph are required to order a list");
    }
    count = list->count;
    if (count == 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "an empty run list has no order to compute");
    }
    if (RoboTextBidiParagraphScalarCount(paragraph) < list->runs[count - 1u]->source.end) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "the paragraph does not cover the runs it would order");
    }

    visual = (size_t *)RoboCalloc(count, sizeof(*visual));
    inverse = (size_t *)RoboCalloc(count, sizeof(*inverse));
    keys = (size_t *)RoboCalloc(count, sizeof(*keys));
    if (!visual || !inverse || !keys) {
        RoboFree(visual);
        RoboFree(inverse);
        RoboFree(keys);
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the run list's order array could not be allocated");
    }

    /*
     * The sort key, and it is worth saying exactly what it is. Each run is placed by **the visual index of the
     * level run its source starts in** — the paragraph's own L2 answer, read rather than recomputed — and ties
     * inside one level run are broken by logical index, which is the sense L2 keeps for runs of equal level.
     *
     * The key is kept as a bare `visualIndex` rather than packed with the logical index into one integer,
     * because packing would impose a digit budget on the run count for no gain: the comparison below reads two
     * plain numbers and there is nothing to unpack.
     */
    for (size_t index = 0u; index < count; index++) {
        size_t levelRun = LevelRunContaining(paragraph, list->runs[index]->source.start);
        const RoboTextBidiRun *run = RoboTextBidiParagraphRun(paragraph, levelRun);

        if (!run) {
            RoboFree(visual);
            RoboFree(inverse);
            RoboFree(keys);
            return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                                   "a run's first scalar is outside every level run");
        }
        keys[index] = run->visualIndex;
    }

    /* Insertion sort over the pair (key, logical index): stable by construction, and a run list is short. */
    for (size_t index = 0u; index < count; index++) {
        visual[index] = index;
    }
    for (size_t index = 1u; index < count; index++) {
        size_t value = visual[index];
        size_t position = index;

        while (position > 0u) {
            size_t previous = visual[position - 1u];
            int after = keys[previous] > keys[value] ||
                        (keys[previous] == keys[value] && previous > value);

            if (!after) {
                break;
            }
            visual[position] = previous;
            position--;
        }
        visual[position] = value;
    }
    for (size_t visualIndex = 0u; visualIndex < count; visualIndex++) {
        inverse[visual[visualIndex]] = visualIndex;
    }

    RoboFree(list->visual);
    RoboFree(list->inverse);
    list->visual = visual;
    list->inverse = inverse;
    list->visualIsLogical = 1;
    for (size_t index = 0u; index < count; index++) {
        if (visual[index] != index) {
            list->visualIsLogical = 0;
            break;
        }
    }
    RoboFree(keys);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Coalescing (Phase 12.14.12)                                         */
/* ------------------------------------------------------------------ */

Result RoboTextGlyphRunListCoalesce(RoboTextGlyphRunList *list, RoboTextDiag *diag)
{
    size_t write = 0u;
    size_t read;
    size_t merged = 0u;

    if (!list) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a run list is required to coalesce");
    }
    if (!list->ownsRuns) {
        /*
         * Refused rather than ignored. A merge replaces a run with a new one and frees what it replaced, so on a
         * view it would free runs whose owner is still holding them — and a caller that got `RoboOk` back would
         * have no way to know it had just been handed a dangling list.
         */
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboNotSupported,
                               "a run list that does not own its runs cannot merge them");
    }
    if (list->count < 2u) {
        return RoboOk;
    }

    /*
     * One pass, in place, over a list that only ever shrinks: @p read walks the original array and @p write
     * marks the runs that survive. A merge that fails leaves the list exactly as it was — the loop allocates
     * the merged run first and only then frees what it replaces, so there is no window in which a slot holds
     * neither the old run nor the new one.
     */
    for (read = 1u; read < list->count; read++) {
        RoboTextGlyphRun *previous = list->runs[write];
        RoboTextGlyphRun *current = list->runs[read];
        RoboTextGlyphRun *joined = NULL;
        Result status;

        if (previous->source.end == current->source.start &&
            RoboTextGlyphRunKeyEqual(&list->keys[write], &list->keys[read])) {
            status = RoboTextGlyphRunConcat(previous, current, &joined, diag);
            if (status != RoboOk) {
                /* Leave the list as it was: the caller's runs are still theirs, unmerged. */
                return status;
            }
            RoboTextGlyphRunDestroy(previous);
            RoboTextGlyphRunDestroy(current);
            list->runs[write] = joined;
            /* The merged run's key is the pair's, so the slot's key stands — but it is refreshed below from
             * the run itself, which is what keeps the two from ever disagreeing. */
            merged++;
            continue;
        }
        write++;
        if (write != read) {
            /*
             * The run *and its key* move together. Moving only the pointer would leave the slot holding the
             * key of whichever run used to live there, and the next comparison would then be asking whether two
             * runs are one shape run by consulting a third run's font.
             */
            list->runs[write] = current;
            list->keys[write] = list->keys[read];
        }
    }

    if (merged != 0u) {
        size_t kept = write + 1u;

        for (size_t index = kept; index < list->count; index++) {
            list->runs[index] = NULL;
        }
        list->count = kept;
    }
    return RunListRefresh(list);
}

/* ------------------------------------------------------------------ */
/* Validation (Phase 12.14.36, 12.14.37)                               */
/* ------------------------------------------------------------------ */

int RoboTextGlyphRunListProblemIsKnown(RoboTextGlyphRunListProblem problem)
{
    return problem >= RoboTextGlyphRunListProblemNone && problem <= RoboTextGlyphRunListProblemTotal;
}

const char *RoboTextGlyphRunListProblemName(RoboTextGlyphRunListProblem problem)
{
    switch (problem) {
        case RoboTextGlyphRunListProblemNone:
            return "none";
        case RoboTextGlyphRunListProblemOrder:
            return "order";
        case RoboTextGlyphRunListProblemOverlap:
            return "overlap";
        case RoboTextGlyphRunListProblemRun:
            return "run";
        case RoboTextGlyphRunListProblemVisualOrder:
            return "visual-order";
        case RoboTextGlyphRunListProblemTotal:
            return "total";
        default:
            return "unknown";
    }
}

Result RoboTextGlyphRunListValidate(const RoboTextGlyphRunList *list,
                                    RoboTextGlyphRunListProblem *outProblem,
                                    RoboTextDiag *diag)
{
    RoboTextGlyphRunListProblem problem = RoboTextGlyphRunListProblemNone;
    double sum = 0.0;

    if (!list || !outProblem) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a list and an out pointer are required to validate");
    }
    *outProblem = RoboTextGlyphRunListProblemNone;

    for (size_t index = 0u; index < list->count; index++) {
        RoboTextGlyphRunProblem runProblem = RoboTextGlyphRunProblemNone;

        if (!list->runs[index]) {
            problem = RoboTextGlyphRunListProblemRun;
            break;
        }
        if (index != 0u) {
            RoboTextRange previous = list->runs[index - 1u]->source;
            RoboTextRange current = list->runs[index]->source;

            if (current.start < previous.start) {
                problem = RoboTextGlyphRunListProblemOrder;
                break;
            }
            if (RoboTextRangeIntersects(previous, current)) {
                problem = RoboTextGlyphRunListProblemOverlap;
                break;
            }
        }
        /*
         * The run's own validation, run *per run* and reported as one problem rather than passed through: the
         * list's answer is "one of my runs is not a valid run", and which one is in the diagnostic message. A
         * caller that wants the run's own problem calls `RoboTextGlyphRunValidate` on the run, which is the door
         * that answers it field by field.
         */
        if (RoboTextGlyphRunValidate(list->runs[index], &runProblem, NULL) != RoboOk) {
            problem = RoboTextGlyphRunListProblemRun;
            break;
        }
        sum += (double)RoboTextGlyphRunAdvance(list->runs[index]);
    }

    if (problem == RoboTextGlyphRunListProblemNone && list->visual) {
        /* A permutation, checked as one: every logical index exactly once. */
        unsigned char *seen = (unsigned char *)RoboCalloc(list->count, sizeof(*seen));

        if (!seen) {
            return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                                   "the visual order could not be checked");
        }
        for (size_t visualIndex = 0u; visualIndex < list->count; visualIndex++) {
            size_t logical = list->visual[visualIndex];

            if (logical >= list->count || seen[logical]) {
                problem = RoboTextGlyphRunListProblemVisualOrder;
                break;
            }
            seen[logical] = 1u;
        }
        RoboFree(seen);
        if (problem == RoboTextGlyphRunListProblemNone && list->inverse) {
            for (size_t logical = 0u; logical < list->count; logical++) {
                if (list->inverse[list->visual[logical]] != logical) {
                    problem = RoboTextGlyphRunListProblemVisualOrder;
                    break;
                }
            }
        }
    }
    if (problem == RoboTextGlyphRunListProblemNone &&
        (list->advanceTotal < sum - 1e-4 || list->advanceTotal > sum + 1e-4)) {
        problem = RoboTextGlyphRunListProblemTotal;
    }

    *outProblem = problem;
    if (problem != RoboTextGlyphRunListProblemNone) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               RoboTextGlyphRunListProblemName(problem));
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The block (Phase 12.14.35)                                          */
/* ------------------------------------------------------------------ */

Result RoboTextGlyphRunListWrite(const RoboTextGlyphRunList *list,
                                 char *buffer,
                                 size_t capacity,
                                 size_t *outLength)
{
    size_t length = 0u;
    RoboTextGlyphRunListProblem problem = RoboTextGlyphRunListProblemNone;

    if (outLength) {
        *outLength = 0u;
    }
    if (!list || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';

    if (RoboTextGlyphRunListValidate(list, &problem, NULL) != RoboOk) {
        length = RoboTextWriterText(buffer, capacity, length, "INVALID ");
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterText(buffer, capacity, length, RoboTextGlyphRunListProblemName(problem));
        }
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterText(buffer, capacity, length, "\n");
        }
        if (length == RoboTextWriterOverflow) {
            buffer[0] = '\0';
            return RoboNotSupported;
        }
    }

    length = RoboTextWriterText(buffer, capacity, length, "GlyphRunList {\n  runs ");
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)list->count);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "  source [");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, RoboTextGlyphRunListSourceRange(list).start);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "..");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length, RoboTextGlyphRunListSourceRange(list).end);
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, ")  advance ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterFloat(buffer, capacity, length, RoboTextGlyphRunListAdvance(list));
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, ", ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterFloat(buffer, capacity, length, RoboTextGlyphRunListAdvanceY(list));
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "  visualIsLogical ");
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterUnsigned(buffer, capacity, length,
                                        (uint32_t)RoboTextGlyphRunListVisualIsLogical(list));
    }
    if (length != RoboTextWriterOverflow) {
        length = RoboTextWriterText(buffer, capacity, length, "\n");
    }

    for (size_t visualIndex = 0u; length != RoboTextWriterOverflow && visualIndex < list->count; visualIndex++) {
        size_t logical = 0u;
        size_t visualOfLogical = 0u;

        if (RoboTextGlyphRunListVisualRun(list, visualIndex, &logical) != RoboOk) {
            length = RoboTextWriterOverflow;
            break;
        }
        if (RoboTextGlyphRunListVisualIndex(list, logical, &visualOfLogical) != RoboOk) {
            length = RoboTextWriterOverflow;
            break;
        }
        length = RoboTextWriterText(buffer, capacity, length, "  run ");
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)logical);
        }
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterText(buffer, capacity, length, " visual ");
        }
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)visualOfLogical);
        }
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterText(buffer, capacity, length, " key ");
        }
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterText(buffer, capacity, length,
                                        RoboTextGlyphGenerationModeName(list->keys[logical].mode));
        }
        if (length != RoboTextWriterOverflow) {
            length = RoboTextWriterText(buffer, capacity, length, "\n");
        }
    }

    for (size_t index = 0u; length != RoboTextWriterOverflow && index < list->count; index++) {
        size_t runLength = 0u;

        if (RoboTextGlyphRunWrite(list->runs[index], buffer + length, capacity - length, &runLength) != RoboOk) {
            length = RoboTextWriterOverflow;
            break;
        }
        length += runLength;
    }

    if (length == RoboTextWriterOverflow) {
        buffer[0] = '\0';
        return RoboNotSupported;
    }
    length = RoboTextWriterText(buffer, capacity, length, "}\n");
    if (length == RoboTextWriterOverflow) {
        buffer[0] = '\0';
        return RoboNotSupported;
    }
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}
