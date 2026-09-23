/*
 * RoboText's bidirectional stage (Phase 12.8) — see RoboTextBidi.h.
 *
 * One resolver, and the file exists so the *result* exists once. Three callers ask and none of them
 * re-derives: the analysis layer (`RoboTextAnalysis.c`) cuts its runs against the levels this pass
 * publishes, the shape request carries the direction the level implies, and the glyph generator asks
 * `RoboTextBidiParagraphMirrors` before it looks a glyph up. Where 12.7's direction resolver resolves a
 * *class projection* against the paragraph, this one asks RoboUnicode for the algorithm's own answer and
 * turns it into the text layer's objects:
 *
 *     Uchar32[] ── UnicodeBidiResolveLevels ─→ levels[]        (raw: RoboBidiLevelRemoved for X9-removed)
 *                     │     UnicodeBidiReorder ─→ visualToLogical[]  (visible scalars, visual order)
 *                     ▼
 *              §5.2 retention ─→ resolved[]  ── coalesced ─→ logical runs[] ── L2 ─→ visual run order[]
 *
 * ## What this file is *not* allowed to be
 *
 * It is not a second bidi algorithm. The nine explicit formatting controls, the isolates, W1–W7, N0–N2,
 * I1/I2 and the L1/L2 reordering are Phase 4's (`RoboUnicodeBidi.h`, conformance 91 707/91 707), and the
 * only arithmetic here is the bookkeeping a text layer owes: index spaces, the §5.2 level of a removed
 * scalar, run coalescing, the run-level visual order and the mirroring gate. A local implementation of
 * "the neutral between an R and an L takes the paragraph direction" would be exactly the defect the
 * module's rules name, and it would also be *wrong* the moment an isolate or an override appeared.
 *
 * ## The fast path, and why it is exact rather than approximate
 *
 * `RoboTextBidiScalarNeedsAlgorithm` answers 0 for a scalar that cannot move a level in a left-to-right
 * paragraph: `L`, and the neutrals. When no scalar in the text needs the algorithm *and* the base is
 * left-to-right, every level is 0 and the pass below assigns them directly, without a table lookup. The
 * claim is not "close enough": W2/W3 need an `AL`, W4/W5/W6/W7 need an `EN`, `AN` or `ET`, and I1's
 * upward pressure needs an `EN`, `AN` or `R` — none of which is present. Neutrals resolve to the
 * surrounding `L`, or to the paragraph, and the paragraph is left-to-right. So the shortcut's answer is
 * the algorithm's answer, and `text.bidi.ascii_fast_path` proves it scalar by scalar over the whole ASCII
 * block rather than trusting the argument.
 *
 * The one thing the fast path must still do is X9: a `BN` has *no* level, so it is marked removed even
 * here. That is why the branch below is a loop rather than a `memset`.
 */

#include "RoboText/RoboTextBidi.h"

#include "RoboMem/RoboMem.h"
#include "RoboTextInternal.h"
#include "RoboUnicode/RoboBidiMirror.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Levels                                                              */
/* ------------------------------------------------------------------ */

int RoboTextBidiLevelIsResolved(RoboTextBidiLevel level)
{
    return level != RoboTextBidiLevelRemoved && level != RoboTextBidiLevelNone;
}

int RoboTextBidiLevelIsLeftToRight(RoboTextBidiLevel level)
{
    return RoboTextBidiLevelIsResolved(level) && (level % 2u) == 0u;
}

int RoboTextBidiLevelIsRightToLeft(RoboTextBidiLevel level)
{
    return RoboTextBidiLevelIsResolved(level) && (level % 2u) == 1u;
}

RoboTextDirection RoboTextBidiLevelDirection(RoboTextBidiLevel level)
{
    if (!RoboTextBidiLevelIsResolved(level)) {
        return RoboTextDirectionUnknown;
    }
    return (level % 2u) == 0u ? RoboTextDirectionLtr : RoboTextDirectionRtl;
}

RoboTextBidiLevel RoboTextBidiLevelForDirection(RoboTextDirection direction)
{
    /* P3's default is level 0, so `Unknown` and `Ltr` fix the same level; the *difference* between them
     * is which one a caller declared, and the paragraph records that separately. */
    return (direction == RoboTextDirectionRtl) ? (RoboTextBidiLevel)1u : (RoboTextBidiLevel)0u;
}

/**
 * The table answer: does @p codePoint's class need the algorithm in a left-to-right paragraph?
 *
 * The five strong-and-weak classes I1/W2–W7 act on, plus the nine explicit formatting controls, whose
 * only effect is on levels and whose level is `RoboTextBidiLevelRemoved` either way. `NSM` is *not* in the
 * list: W1 makes a mark take the type of the character before it, and a mark after a left-to-right scalar
 * is left-to-right — but a mark after an *R* is not, and an R is itself in the list, so the text still
 * reaches the algorithm.
 */
static int BidiScalarNeedsAlgorithmTable(RoboTextCodePoint codePoint)
{
    switch (RoboTextUnicodeBidiClass(codePoint)) {
        case RoboUBcR:
        case RoboUBcAl:
        case RoboUBcEn:
        case RoboUBcAn:
        case RoboUBcLre:
        case RoboUBcLro:
        case RoboUBcRle:
        case RoboUBcRlo:
        case RoboUBcPdf:
        case RoboUBcLri:
        case RoboUBcRli:
        case RoboUBcFsi:
        case RoboUBcPdi:
            return 1;
        default:
            return 0;
    }
}

int RoboTextBidiScalarNeedsAlgorithm(RoboTextCodePoint codePoint)
{
    /*
     * The ASCII block, answered from the block rather than from the table — the same bargain the script
     * and direction stages make. No ASCII scalar is `R`, `AL`, `AN` or a formatting control (every one of
     * those lies above U+007F in the UCD), so the only ASCII scalars that need the algorithm are the ten
     * digits, which are `EN`. `text.bidi.ascii_fast_path` walks the block and checks this against the
     * table, so the shortcut is proved rather than assumed.
     */
    if (codePoint < 0x80u) {
        return (codePoint >= (RoboTextCodePoint)'0' && codePoint <= (RoboTextCodePoint)'9') ? 1 : 0;
    }
    return BidiScalarNeedsAlgorithmTable(codePoint);
}

/**
 * Non-zero when @p codePoint is one of the nine explicit formatting controls.
 *
 * Five of them (`LRE`/`RLE`/`LRO`/`RLO`/`PDF`) X9 *removes*, and the other four
 * (`LRI`/`RLI`/`FSI`/`PDI`) it does **not**: UAX #9 §5.2 keeps the isolate formatting characters in the
 * text, because they are what determines an isolate's paragraph level, and gives them levels of their own.
 * What both halves share is that neither is *drawn* — and that is the fact a painter needs, which is why
 * this predicate exists beside `RoboTextBidiParagraphScalarIsRemoved` rather than folded into it.
 */
int RoboTextBidiScalarIsFormattingControl(RoboTextCodePoint codePoint)
{
    switch (RoboTextUnicodeBidiClass(codePoint)) {
        case RoboUBcLre:
        case RoboUBcLro:
        case RoboUBcRle:
        case RoboUBcRlo:
        case RoboUBcPdf:
        case RoboUBcLri:
        case RoboUBcRli:
        case RoboUBcFsi:
        case RoboUBcPdi:
            return 1;
        default:
            return 0;
    }
}

int RoboTextBidiMirrorOf(RoboTextCodePoint codePoint, RoboTextCodePoint *outMirror)
{
    Uchar32 mirror = (Uchar32)codePoint;

    if (codePoint > RoboTextCodePointMax) {
        if (outMirror) {
            *outMirror = codePoint;
        }
        return 0;
    }
    if (BidiMirrorLookup((Uchar32)codePoint, &mirror)) {
        if (outMirror) {
            *outMirror = (RoboTextCodePoint)mirror;
        }
        return 1;
    }
    if (outMirror) {
        *outMirror = codePoint;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The paragraph                                                       */
/* ------------------------------------------------------------------ */

struct RoboTextBidiParagraph {
    /** The text the run table's byte column names. Borrowed; may have no bytes. */
    RoboTextView view;
    /** An **owned copy** of the scalars, so the mirroring door and the dump need nothing else. */
    Uchar32 *scalars;
    size_t scalarCount;
    /** Levels exactly as the algorithm left them: `RoboTextBidiLevelRemoved` is possible. */
    RoboTextBidiLevel *levels;
    /** Levels with §5.2's retention applied: never removed, one per scalar. */
    RoboTextBidiLevel *resolved;
    /** Logical runs, in source order. */
    RoboTextBidiRun *runs;
    size_t runCount;
    /** L2 over the runs: `visualRuns[v]` is the logical index of the v-th run to paint. */
    size_t *visualRuns;
    /** L2 over the visible scalars, as the algorithm produced it: logical index per visual slot. */
    size_t *visualScalars;
    size_t visibleCount;
    /** The inverse of @p visualScalars; `RoboTextBidiVisualNone` for a removed scalar. */
    size_t *visualOfScalar;
    RoboTextDirectionContext context;
    RoboTextDirection baseDirection;
    RoboTextBidiLevel baseLevel;
    RoboTextBidiBaseSource baseSource;
    size_t    removedScalars;
    size_t formattingControls;
    size_t rtlScalars;
    size_t rtlRuns;
    size_t mirroredScalars;
    size_t maxLevel;
};

/** Does any scalar in @p scalars need the algorithm? Used only for the left-to-right fast path. */
static int BidiTextNeedsAlgorithm(const Uchar32 *scalars, size_t count)
{
    for (size_t index = 0u; index < count; index++) {
        if (RoboTextBidiScalarNeedsAlgorithm((RoboTextCodePoint)scalars[index])) {
            return 1;
        }
    }
    return 0;
}

/**
 * The paragraph base, and where it came from.
 *
 * A declaration wins; then the text's own first strong scalar (P2/P3, through 12.7's door, whose
 * `Unknown` answer is deliberately not a direction); then the context's declared fallback; then P3's
 * level-0 default. The last case is the one this stage adds to 12.7: a *resolution* has to have a level,
 * and with no strong scalar UAX #9's own answer is level 0 — so the answer is `Ltr` and the paragraph
 * records that nothing in the text asked for it.
 */
static RoboTextDirection BidiResolveBase(const Uchar32 *scalars,
                                         size_t count,
                                         const RoboTextDirectionContext *context,
                                         RoboTextBidiBaseSource *outSource)
{
    RoboTextDirection declared = RoboTextDirectionUnknown;
    RoboTextDirection strong;

    switch (context->mode) {
        case RoboTextDirectionModeExplicitLtr:
            declared = RoboTextDirectionLtr;
            break;
        case RoboTextDirectionModeExplicitRtl:
            declared = RoboTextDirectionRtl;
            break;
        case RoboTextDirectionModeAuto:
        default:
            break;
    }
    if (declared != RoboTextDirectionUnknown) {
        *outSource = RoboTextBidiBaseDeclared;
        return declared;
    }
    strong = RoboTextUnicodeBaseDirection(scalars, count, RoboTextDirectionUnknown);
    if (strong != RoboTextDirectionUnknown) {
        *outSource = RoboTextBidiBaseStrong;
        return strong;
    }
    if (context->fallbackDirection != RoboTextDirectionUnknown) {
        *outSource = RoboTextBidiBaseFallback;
        return context->fallbackDirection;
    }
    *outSource = RoboTextBidiBaseDefault;
    return RoboTextDirectionLtr;
}

/** §5.2's retention, in place: a removed scalar takes the level of the scalar before it, or the base. */
static void BidiRetainLevels(RoboTextBidiLevel *levels,
                             RoboTextBidiLevel *resolved,
                             size_t count,
                             RoboTextBidiLevel baseLevel)
{
    RoboTextBidiLevel carry = baseLevel;

    for (size_t index = 0u; index < count; index++) {
        if (RoboTextBidiLevelIsResolved(levels[index])) {
            carry = levels[index];
        }
        resolved[index] = carry;
    }
}

/** Coalesce @p resolved into runs, and count what the stats need on the way through. */
static void BidiBuildRuns(struct RoboTextBidiParagraph *paragraph)
{
    size_t index = 0u;
    size_t runCount = 0u;

    while (index < paragraph->scalarCount) {
        RoboTextBidiLevel level = paragraph->resolved[index];
        size_t end = index + 1u;

        while (end < paragraph->scalarCount && paragraph->resolved[end] == level) {
            end++;
        }
        paragraph->runs[runCount].range = RoboTextRangeMake((RoboTextScalarIndex)index,
                                                           (RoboTextScalarIndex)end);
        paragraph->runs[runCount].level = level;
        paragraph->runs[runCount].direction = RoboTextBidiLevelDirection(level);
        paragraph->runs[runCount].logicalIndex = runCount;
        paragraph->runs[runCount].visualIndex = 0u;
        if (RoboTextBidiLevelIsRightToLeft(level)) {
            paragraph->rtlRuns++;
        }
        if ((size_t)level > paragraph->maxLevel) {
            paragraph->maxLevel = (size_t)level;
        }
        runCount++;
        index = end;
    }
    paragraph->runCount = runCount;
}

/**
 * Non-zero when scalar @p index is one a painter draws: not X9-removed, and not a formatting control.
 *
 * The isolate formatting characters are the reason this is a predicate of its own rather than "the level is
 * resolved": UAX #9 §5.2 keeps them in the text with real levels, and they still must not paint. A glyph
 * for U+2066 would be a box, and a box is not what an isolate is.
 */
static int BidiScalarIsDrawable(const struct RoboTextBidiParagraph *paragraph, size_t index)
{
    if (!RoboTextBidiLevelIsResolved(paragraph->levels[index])) {
        return 0;
    }
    return RoboTextBidiScalarIsFormattingControl((RoboTextCodePoint)paragraph->scalars[index]) ? 0 : 1;
}

/** Reverse @p order between @p from and @p to, inclusive of @p from and exclusive of @p to. */
static void BidiReverseRange(size_t *order, size_t from, size_t to)
{
    while (from + 1u < to) {
        size_t swap = order[from];

        order[from] = order[to - 1u];
        order[to - 1u] = swap;
        from++;
        to--;
    }
}

/**
 * UAX #9 L2 over the runs: reverse every maximal run whose level is at least @p level, from the highest
 * level down to the lowest odd one.
 *
 * This is the same rule the algorithm applies to the scalars, applied one level up, which is why it is
 * fifteen lines rather than a copy of Phase 4's reorder: what a *painter* needs is the order of the
 * shaped runs, and what a hit test needs is the order of the scalars, and the two are published side by
 * side rather than one derived from the other at every call site.
 */
static void BidiOrderRuns(struct RoboTextBidiParagraph *paragraph)
{
    size_t lowestOdd = 0u;
    int hasOdd = 0;

    for (size_t index = 0u; index < paragraph->runCount; index++) {
        size_t level = (size_t)paragraph->runs[index].level;

        paragraph->visualRuns[index] = index;
        if ((level % 2u) == 1u && (!hasOdd || level < lowestOdd)) {
            lowestOdd = level;
            hasOdd = 1;
        }
    }
    if (!hasOdd) {
        /* An all-left-to-right paragraph is the identity in both orders — recorded, not merely left at
         * whatever the structure was initialised to, because a caller reads these fields. */
        for (size_t index = 0u; index < paragraph->runCount; index++) {
            paragraph->runs[index].visualIndex = index;
        }
        return;
    }
    /*
     * Down from the highest level to the lowest **odd** one, inclusive. The loop is written as a
     * descend-and-test rather than as `for (level = max; level >= lowestOdd; level--)` because the counter
     * is unsigned and the decrement of a `size_t` past zero wraps; `lowestOdd` is odd and therefore at
     * least 1, so the exit is always taken before that could happen.
     */
    {
        size_t level = paragraph->maxLevel;

        for (;;) {
            size_t index = 0u;

            while (index < paragraph->runCount) {
                if ((size_t)paragraph->runs[index].level < level) {
                    index++;
                    continue;
                }
                {
                    size_t end = index + 1u;

                    while (end < paragraph->runCount && (size_t)paragraph->runs[end].level >= level) {
                        end++;
                    }
                    BidiReverseRange(paragraph->visualRuns, index, end);
                    index = end;
                }
            }
            if (level <= lowestOdd) {
                break;
            }
            level--;
        }
    }
    /* Write each run's visual position back, so one record answers both readings. */
    for (size_t visual = 0u; visual < paragraph->runCount; visual++) {
        paragraph->runs[paragraph->visualRuns[visual]].visualIndex = visual;
    }
}

void RoboTextBidiParagraphDestroy(RoboTextBidiParagraph *paragraph)
{
    if (!paragraph) {
        return;
    }
    RoboFree(paragraph->scalars);
    RoboFree(paragraph->levels);
    RoboFree(paragraph->resolved);
    RoboFree(paragraph->runs);
    RoboFree(paragraph->visualRuns);
    RoboFree(paragraph->visualScalars);
    RoboFree(paragraph->visualOfScalar);
    RoboFree(paragraph);
}

Result RoboTextBidiParagraphResolve(const Uchar32 *scalars,
                                    size_t count,
                                    const RoboTextDirectionContext *context,
                                    RoboTextBidiParagraph **out,
                                    RoboTextDiag *diag)
{
    RoboTextDirectionContext defaults;
    RoboTextBidiParagraph *paragraph;
    RoboTextBidiBaseSource source = RoboTextBidiBaseDeclared;
    Result status;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboInvalidArgument,
                               "an out pointer is required");
    }
    *out = NULL;
    if (!scalars && count != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboInvalidArgument,
                               "scalars are required for a non-empty text");
    }
    if (!context) {
        RoboTextDirectionContextInit(&defaults);
        context = &defaults;
    }
    if (!RoboTextDirectionContextIsValid(context)) {
        return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboInvalidArgument,
                               "the direction context is not usable");
    }

    paragraph = (RoboTextBidiParagraph *)RoboCalloc(1u, sizeof(*paragraph));
    if (!paragraph) {
        return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboOutOfMemory,
                               "the bidi paragraph could not be allocated");
    }
    paragraph->context = *context;
    paragraph->scalarCount = count;
    paragraph->baseDirection = BidiResolveBase(scalars, count, context, &source);
    paragraph->baseSource = source;
    paragraph->baseLevel = RoboTextBidiLevelForDirection(paragraph->baseDirection);

    if (count != 0u) {
        paragraph->scalars = (Uchar32 *)RoboCalloc(count, sizeof(*paragraph->scalars));
        paragraph->levels = (RoboTextBidiLevel *)RoboCalloc(count, sizeof(*paragraph->levels));
        paragraph->resolved = (RoboTextBidiLevel *)RoboCalloc(count, sizeof(*paragraph->resolved));
        paragraph->runs = (RoboTextBidiRun *)RoboCalloc(count, sizeof(*paragraph->runs));
        paragraph->visualRuns = (size_t *)RoboCalloc(count, sizeof(*paragraph->visualRuns));
        paragraph->visualScalars = (size_t *)RoboCalloc(count, sizeof(*paragraph->visualScalars));
        paragraph->visualOfScalar = (size_t *)RoboCalloc(count, sizeof(*paragraph->visualOfScalar));
        if (!paragraph->scalars || !paragraph->levels || !paragraph->resolved || !paragraph->runs ||
            !paragraph->visualRuns || !paragraph->visualScalars || !paragraph->visualOfScalar) {
            RoboTextBidiParagraphDestroy(paragraph);
            return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboOutOfMemory,
                                   "the paragraph arrays could not be allocated");
        }
        memcpy(paragraph->scalars, scalars, count * sizeof(*paragraph->scalars));
    }

    /*
     * The levels. The fast path is taken only for a left-to-right paragraph in which no scalar can move
     * a level, and it is the *whole* algorithm's answer for such a text (see the file comment) — but it
     * still has to mark X9's removals, so a `BN` keeps its "no level" state here too.
     */
    if (count != 0u && paragraph->baseLevel == 0u && !BidiTextNeedsAlgorithm(scalars, count)) {
        for (size_t index = 0u; index < count; index++) {
            paragraph->levels[index] =
                (RoboTextUnicodeBidiClass((RoboTextCodePoint)scalars[index]) == RoboUBcBn)
                    ? RoboTextBidiLevelRemoved
                    : (RoboTextBidiLevel)0u;
        }
    } else if (count != 0u) {
        uint8_t paragraphLevel = 0u;

        status = UnicodeBidiResolveLevels(scalars, count,
                                          (paragraph->baseLevel == 1u) ? RoboBidiRtl : RoboBidiLtr,
                                          paragraph->levels, &paragraphLevel);
        if (status != RoboOk) {
            RoboTextBidiParagraphDestroy(paragraph);
            return RoboTextDiagMsg(diag, RoboTextDomainBidi, status,
                                   "the bidirectional algorithm refused the paragraph");
        }
        /* The algorithm's own answer for the base level, which P2/P3 above should have agreed with; an
         * explicit context is the one case where a caller's declaration is what he asked for. */
        paragraph->baseLevel = (RoboTextBidiLevel)paragraphLevel;
        paragraph->baseDirection = RoboTextBidiLevelDirection(paragraph->baseLevel);
    }

    BidiRetainLevels(paragraph->levels, paragraph->resolved, count, paragraph->baseLevel);
    BidiBuildRuns(paragraph);
    BidiOrderRuns(paragraph);

    for (size_t index = 0u; index < count; index++) {
        RoboTextCodePoint codePoint = (RoboTextCodePoint)paragraph->scalars[index];
        RoboTextCodePoint mirror = codePoint;

        if (!RoboTextBidiLevelIsResolved(paragraph->levels[index])) {
            paragraph->removedScalars++;
        }
        if (RoboTextBidiScalarIsFormattingControl(codePoint)) {
            paragraph->formattingControls++;
        }
        if (RoboTextBidiLevelIsRightToLeft(paragraph->resolved[index])) {
            paragraph->rtlScalars++;
            if (RoboTextBidiMirrorOf(codePoint, &mirror) && mirror != codePoint) {
                paragraph->mirroredScalars++;
            }
        }
        if (BidiScalarIsDrawable(paragraph, index)) {
            paragraph->visibleCount++;
        }
    }

    /*
     * The scalar-level visual order, which is the algorithm's own L2 — with two adjustments that are this
     * stage's rather than the algorithm's, and both of them about **what is drawn**:
     *
     *   - with every scalar at the same level L2 is the identity, so the order is built directly and no
     *     call is made (the fast path);
     *   - either way, the order is over the scalars a painter draws. UAX #9's own reorder output includes
     *     the isolate formatting characters (§5.2 keeps them, because they determine an isolate's
     *     paragraph level), and they are filtered out here. Filtering preserves the relative order of what
     *     remains, because L2 is a permutation.
     *
     * `visibleCount` was counted above, so `visualScalars` and it cannot disagree. `visualOfScalar` is the
     * inverse, which is the mapping a hit test reads.
     */
    if (count != 0u) {
        size_t visual = 0u;

        for (size_t index = 0u; index < count; index++) {
            paragraph->visualOfScalar[index] = RoboTextBidiVisualNone;
        }
        if (paragraph->maxLevel == 0u) {
            for (size_t index = 0u; index < count; index++) {
                if (BidiScalarIsDrawable(paragraph, index)) {
                    paragraph->visualScalars[visual] = index;
                    paragraph->visualOfScalar[index] = visual;
                    visual++;
                }
            }
        } else {
            size_t *order = paragraph->visualScalars;
            size_t ordered = 0u;

            status = UnicodeBidiReorder(paragraph->levels, count, order, &ordered);
            if (status != RoboOk) {
                RoboTextBidiParagraphDestroy(paragraph);
                return RoboTextDiagMsg(diag, RoboTextDomainBidi, status,
                                       "the reordering pass refused the paragraph");
            }
            for (size_t slot = 0u; slot < ordered; slot++) {
                size_t logical = order[slot];

                if (logical >= count) {
                    continue;
                }
                if (!BidiScalarIsDrawable(paragraph, logical)) {
                    continue;
                }
                /* Compacted in place: a drawable entry is never written before an earlier slot has been
                 * read, because `visual <= slot` holds at every step. */
                paragraph->visualScalars[visual] = logical;
                paragraph->visualOfScalar[logical] = visual;
                visual++;
            }
        }
        if (visual != paragraph->visibleCount) {
            /* The count and the order are one answer; a disagreement would be a bug in this file rather
             * than in the input, so the paragraph is refused rather than published inconsistent. */
            RoboTextBidiParagraphDestroy(paragraph);
            return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboError,
                                   "the visual order and the drawable count disagree");
        }
    }

    *out = paragraph;
    return RoboOk;
}

Result RoboTextBidiParagraphResolveView(RoboTextView view,
                                        const RoboTextDirectionContext *context,
                                        RoboTextBidiParagraph **out,
                                        RoboTextDiag *diag)
{
    Uchar32 *scalars = NULL;
    RoboTextScalarIndex count = 0u;
    Result status;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboInvalidArgument,
                               "an out pointer is required");
    }
    *out = NULL;
    status = RoboTextUnicodeDecodeAll(view, &scalars, &count, diag);
    if (status != RoboOk) {
        return status;
    }
    status = RoboTextBidiParagraphResolve(scalars, (size_t)count, context, out, diag);
    RoboTextUnicodeScalarsFree(scalars);
    return status;
}

/* ------------------------------------------------------------------ */
/* Reading                                                             */
/* ------------------------------------------------------------------ */

RoboTextScalarIndex RoboTextBidiParagraphScalarCount(const RoboTextBidiParagraph *paragraph)
{
    return paragraph ? (RoboTextScalarIndex)paragraph->scalarCount : 0u;
}

RoboTextDirection RoboTextBidiParagraphBaseDirection(const RoboTextBidiParagraph *paragraph)
{
    return paragraph ? paragraph->baseDirection : RoboTextDirectionUnknown;
}

RoboTextBidiLevel RoboTextBidiParagraphBaseLevel(const RoboTextBidiParagraph *paragraph)
{
    return paragraph ? paragraph->baseLevel : (RoboTextBidiLevel)0u;
}

int RoboTextBidiParagraphBaseWasDefaulted(const RoboTextBidiParagraph *paragraph)
{
    return (paragraph && paragraph->baseSource == RoboTextBidiBaseDefault) ? 1 : 0;
}

RoboTextBidiBaseSource RoboTextBidiParagraphBaseSource(const RoboTextBidiParagraph *paragraph)
{
    return paragraph ? paragraph->baseSource : RoboTextBidiBaseDefault;
}

size_t RoboTextBidiParagraphRunCount(const RoboTextBidiParagraph *paragraph)
{
    return paragraph ? paragraph->runCount : 0u;
}

const RoboTextBidiRun *RoboTextBidiParagraphRun(const RoboTextBidiParagraph *paragraph, size_t index)
{
    if (!paragraph || index >= paragraph->runCount) {
        return NULL;
    }
    return &paragraph->runs[index];
}

const RoboTextBidiRun *RoboTextBidiParagraphRunAtScalar(const RoboTextBidiParagraph *paragraph,
                                                        RoboTextScalarIndex scalar)
{
    size_t low;
    size_t high;

    if (!paragraph || paragraph->runCount == 0u || (size_t)scalar >= paragraph->scalarCount) {
        return NULL;
    }
    low = 0u;
    high = paragraph->runCount - 1u;
    while (low <= high) {
        size_t mid = low + (high - low) / 2u;

        if (RoboTextRangeContains(paragraph->runs[mid].range, scalar)) {
            return &paragraph->runs[mid];
        }
        if ((size_t)scalar < paragraph->runs[mid].range.start) {
            if (mid == 0u) {
                break;
            }
            high = mid - 1u;
        } else {
            low = mid + 1u;
        }
    }
    return NULL;
}

Result RoboTextBidiParagraphVisualRun(const RoboTextBidiParagraph *paragraph,
                                      size_t visual,
                                      size_t *outLogicalIndex,
                                      RoboTextDiag *diag)
{
    if (!paragraph || !outLogicalIndex) {
        return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboInvalidArgument,
                               "a paragraph and an out index are required");
    }
    if (visual >= paragraph->runCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboInvalidArgument,
                               "the visual index is past the last run");
    }
    *outLogicalIndex = paragraph->visualRuns[visual];
    return RoboOk;
}

RoboTextBidiLevel RoboTextBidiParagraphLevel(const RoboTextBidiParagraph *paragraph,
                                             RoboTextScalarIndex scalar)
{
    if (!paragraph || (size_t)scalar >= paragraph->scalarCount) {
        return RoboTextBidiLevelRemoved;
    }
    return paragraph->levels[scalar];
}

RoboTextBidiLevel RoboTextBidiParagraphResolvedLevel(const RoboTextBidiParagraph *paragraph,
                                                     RoboTextScalarIndex scalar)
{
    if (!paragraph || paragraph->scalarCount == 0u || (size_t)scalar >= paragraph->scalarCount) {
        return paragraph ? paragraph->baseLevel : (RoboTextBidiLevel)0u;
    }
    return paragraph->resolved[scalar];
}

int RoboTextBidiParagraphScalarIsRemoved(const RoboTextBidiParagraph *paragraph,
                                         RoboTextScalarIndex scalar)
{
    if (!paragraph || (size_t)scalar >= paragraph->scalarCount) {
        return 0;
    }
    return RoboTextBidiLevelIsResolved(paragraph->levels[scalar]) ? 0 : 1;
}

int RoboTextBidiParagraphScalarIsDrawable(const RoboTextBidiParagraph *paragraph,
                                          RoboTextScalarIndex scalar)
{
    if (!paragraph || (size_t)scalar >= paragraph->scalarCount) {
        return 0;
    }
    return BidiScalarIsDrawable(paragraph, (size_t)scalar);
}

RoboTextCodePoint RoboTextBidiParagraphScalar(const RoboTextBidiParagraph *paragraph,
                                              RoboTextScalarIndex index)
{
    if (!paragraph || (size_t)index >= paragraph->scalarCount) {
        return RoboTextCodePointReplacement;
    }
    return (RoboTextCodePoint)paragraph->scalars[index];
}

size_t RoboTextBidiParagraphVisualScalarCount(const RoboTextBidiParagraph *paragraph)
{
    return paragraph ? paragraph->visibleCount : 0u;
}

Result RoboTextBidiParagraphVisualScalar(const RoboTextBidiParagraph *paragraph,
                                         size_t visual,
                                         RoboTextScalarIndex *outLogical,
                                         RoboTextDiag *diag)
{
    if (!paragraph || !outLogical) {
        return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboInvalidArgument,
                               "a paragraph and an out index are required");
    }
    if (visual >= paragraph->visibleCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboInvalidArgument,
                               "the visual index is past the last visible scalar");
    }
    *outLogical = (RoboTextScalarIndex)paragraph->visualScalars[visual];
    return RoboOk;
}

Result RoboTextBidiParagraphVisualOfScalar(const RoboTextBidiParagraph *paragraph,
                                           RoboTextScalarIndex logical,
                                           size_t *outVisual,
                                           RoboTextDiag *diag)
{
    if (!paragraph || !outVisual) {
        return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboInvalidArgument,
                               "a paragraph and an out index are required");
    }
    if ((size_t)logical >= paragraph->scalarCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboInvalidArgument,
                               "the scalar index is past the last scalar");
    }
    *outVisual = paragraph->visualOfScalar[logical];
    return RoboOk;
}

int RoboTextBidiParagraphMirrors(const RoboTextBidiParagraph *paragraph,
                                 RoboTextScalarIndex scalar,
                                 RoboTextCodePoint *outDraw)
{
    RoboTextCodePoint codePoint;
    RoboTextCodePoint mirror;

    if (!paragraph) {
        return 0;
    }
    codePoint = RoboTextBidiParagraphScalar(paragraph, scalar);
    mirror = codePoint;
    if ((size_t)scalar >= paragraph->scalarCount) {
        if (outDraw) {
            *outDraw = codePoint;
        }
        return 0;
    }
    /* L4 applies to a scalar whose *resolved* level is odd — a removed control has no level and no
     * visible glyph, so it can never be mirrored. */
    if (!RoboTextBidiLevelIsRightToLeft(paragraph->resolved[scalar])) {
        if (outDraw) {
            *outDraw = codePoint;
        }
        return 0;
    }
    if (RoboTextBidiMirrorOf(codePoint, &mirror) && mirror != codePoint) {
        if (outDraw) {
            *outDraw = mirror;
        }
        return 1;
    }
    if (outDraw) {
        *outDraw = codePoint;
    }
    return 0;
}

Result RoboTextBidiParagraphGetStats(const RoboTextBidiParagraph *paragraph, RoboTextBidiStats *out)
{
    if (!paragraph || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->runs = paragraph->runCount;
    out->scalars = paragraph->scalarCount;
    out->visibleScalars = paragraph->visibleCount;
    out->removedScalars = paragraph->removedScalars;
    out->formattingControls = paragraph->formattingControls;
    out->drawableScalars = paragraph->visibleCount;
    out->rtlScalars = paragraph->rtlScalars;
    out->rtlRuns = paragraph->rtlRuns;
    out->mirroredScalars = paragraph->mirroredScalars;
    out->maxLevel = paragraph->maxLevel;
    out->baseLevel = paragraph->baseLevel;
    out->baseDirection = paragraph->baseDirection;
    out->baseDefaulted = RoboTextBidiParagraphBaseWasDefaulted(paragraph);
    out->baseSource = paragraph->baseSource;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The base source's vocabulary                                        */
/* ------------------------------------------------------------------ */

int RoboTextBidiBaseSourceIsKnown(RoboTextBidiBaseSource source)
{
    return (source >= RoboTextBidiBaseDeclared && source <= RoboTextBidiBaseDefault) ? 1 : 0;
}

const char *RoboTextBidiBaseSourceName(RoboTextBidiBaseSource source)
{
    switch (source) {
        case RoboTextBidiBaseDeclared:
            return "declared";
        case RoboTextBidiBaseStrong:
            return "strong";
        case RoboTextBidiBaseFallback:
            return "fallback";
        case RoboTextBidiBaseDefault:
            return "default";
        default:
            break;
    }
    return "unknown";
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/** The UCD short name of a bidi class, for the dump's class column. Never NULL. */
static const char *BidiClassName(UnicodeBidiClass bidiClass)
{
    switch (bidiClass) {
        case RoboUBcL:
            return "L";
        case RoboUBcR:
            return "R";
        case RoboUBcAl:
            return "AL";
        case RoboUBcEn:
            return "EN";
        case RoboUBcEs:
            return "ES";
        case RoboUBcEt:
            return "ET";
        case RoboUBcAn:
            return "AN";
        case RoboUBcCs:
            return "CS";
        case RoboUBcNsm:
            return "NSM";
        case RoboUBcBn:
            return "BN";
        case RoboUBcB:
            return "B";
        case RoboUBcS:
            return "S";
        case RoboUBcWs:
            return "WS";
        case RoboUBcOn:
            return "ON";
        case RoboUBcLre:
            return "LRE";
        case RoboUBcLro:
            return "LRO";
        case RoboUBcRle:
            return "RLE";
        case RoboUBcRlo:
            return "RLO";
        case RoboUBcPdf:
            return "PDF";
        case RoboUBcLri:
            return "LRI";
        case RoboUBcRli:
            return "RLI";
        case RoboUBcFsi:
            return "FSI";
        case RoboUBcPdi:
            return "PDI";
        default:
            break;
    }
    return "?";
}

/** `U+0048`, uppercase and padded to four digits. Returns the new length or RoboTextWriterOverflow. */
static size_t BidiWriteCodePoint(char *line, size_t capacity, size_t length, RoboTextCodePoint codePoint)
{
    static const char hex[] = "0123456789ABCDEF";
    char digits[6];
    size_t count = 0u;
    uint32_t value = codePoint;

    length = RoboTextWriterText(line, capacity, length, "U+");
    do {
        digits[count++] = hex[value & 0xFu];
        value >>= 4u;
    } while (value != 0u);
    while (count < 4u) {
        digits[count++] = '0';
    }
    if (length == RoboTextWriterOverflow || length + count + 1u > capacity) {
        return RoboTextWriterOverflow;
    }
    for (size_t index = 0u; index < count; index++) {
        line[length + index] = digits[count - 1u - index];
    }
    length += count;
    line[length] = '\0';
    return length;
}

Result RoboTextBidiRunWrite(const RoboTextBidiRun *run,
                            char *buffer,
                            size_t capacity,
                            size_t *outLength)
{
    char line[96];
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!run || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';

    length = RoboTextWriterText(line, sizeof(line), length, "[");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, run->range.start);
    length = RoboTextWriterText(line, sizeof(line), length, "..");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, run->range.end);
    length = RoboTextWriterText(line, sizeof(line), length, ") lvl ");
    length = RoboTextWriterUnsigned(line, sizeof(line), length, run->level);
    length = RoboTextWriterText(line, sizeof(line), length, " ");
    length = RoboTextWriterText(line, sizeof(line), length, RoboTextDirectionName(run->direction));
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    if (length + 1u > capacity) {
        if (outLength) {
            *outLength = length;
        }
        return RoboNotSupported;
    }
    memcpy(buffer, line, length + 1u);
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}

/** A sink that measures a dump in one pass and writes it in the next, or refuses it whole. */
typedef struct BidiDumpSink {
    char *buffer;
    size_t capacity;
    size_t length;
    int failed;
} BidiDumpSink;

static void BidiDumpSinkLine(BidiDumpSink *sink, const char *line, size_t lineLength)
{
    if (sink->failed) {
        return;
    }
    if (lineLength == RoboTextWriterOverflow) {
        sink->failed = 1;
        return;
    }
    if (sink->buffer) {
        /* Room for the line, its `\n`, and the terminating `\0` — the last of which the final check in
         * the write door reserves. Being exact here is what keeps "complete or absent" true at the last
         * byte rather than one byte early. */
        if (sink->length + lineLength + 2u > sink->capacity + 1u) {
            sink->failed = 1;
            return;
        }
        memcpy(sink->buffer + sink->length, line, lineLength);
        sink->length += lineLength;
        sink->buffer[sink->length++] = '\n';
        sink->buffer[sink->length] = '\0';
    } else {
        sink->length += lineLength + 1u;
    }
}

Result RoboTextBidiParagraphWrite(const RoboTextBidiParagraph *paragraph,
                                  RoboTextView view,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength)
{
    RoboTextByteIndex *offsets = NULL;
    BidiDumpSink measure;
    BidiDumpSink write;
    size_t printed = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!paragraph || !buffer || capacity == 0u) {
        return RoboInvalidArgument;
    }
    buffer[0] = '\0';
    if (view.bytes != NULL && paragraph->scalarCount != 0u) {
        Result status = RoboTextWriterByteOffsets(view, paragraph->scalarCount, &offsets, NULL);

        if (status != RoboOk) {
            return status;
        }
    }

    memset(&measure, 0, sizeof(measure));
    memset(&write, 0, sizeof(write));
    write.buffer = buffer;
    write.capacity = capacity;

    for (int pass = 0; pass < 2; pass++) {
        BidiDumpSink *sink = (pass == 0) ? &measure : &write;
        char line[192];
        size_t length;

        BidiDumpSinkLine(sink, "Bidi Analysis", 13u);

        /* The paragraph's own facts. */
        length = 0u;
        length = RoboTextWriterText(line, sizeof(line), length, "base: ");
        length = RoboTextWriterText(line, sizeof(line), length,
                                    RoboTextDirectionName(paragraph->baseDirection));
        length = RoboTextWriterText(line, sizeof(line), length, " level ");
        length = RoboTextWriterUnsigned(line, sizeof(line), length, paragraph->baseLevel);
        length = RoboTextWriterText(line, sizeof(line), length, " (");
        length = RoboTextWriterText(line, sizeof(line), length,
                                    RoboTextBidiBaseSourceName(paragraph->baseSource));
        length = RoboTextWriterText(line, sizeof(line), length, ")  scalars ");
        length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)paragraph->scalarCount);
        length = RoboTextWriterText(line, sizeof(line), length, "  runs ");
        length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)paragraph->runCount);
        length = RoboTextWriterText(line, sizeof(line), length, "  visible ");
        length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)paragraph->visibleCount);
        BidiDumpSinkLine(sink, line, length);

        /* The logical runs, in both index spaces. */
        for (size_t index = 0u; index < paragraph->runCount; index++) {
            const RoboTextBidiRun *run = &paragraph->runs[index];
            char label[32];
            size_t labelLength = 0u;

            labelLength = RoboTextWriterText(label, sizeof(label), labelLength, "lvl ");
            labelLength = RoboTextWriterUnsigned(label, sizeof(label), labelLength, run->level);
            labelLength = RoboTextWriterText(label, sizeof(label), labelLength, "  ");
            labelLength = RoboTextWriterText(label, sizeof(label), labelLength,
                                             RoboTextDirectionName(run->direction));
            if (labelLength == RoboTextWriterOverflow) {
                labelLength = 0u;
                label[0] = '\0';
            }
            length = RoboTextWriterRunRow(line, sizeof(line), run->range, label,
                                          offsets != NULL,
                                          offsets ? offsets[run->range.start] : 0u,
                                          offsets ? offsets[run->range.end] : 0u);
            BidiDumpSinkLine(sink, line, length);
        }

        /* L2's answer, as logical indices. */
        length = 0u;
        length = RoboTextWriterText(line, sizeof(line), length, "visual:");
        for (size_t visual = 0u; visual < paragraph->runCount; visual++) {
            length = RoboTextWriterText(line, sizeof(line), length, " ");
            length = RoboTextWriterUnsigned(line, sizeof(line), length,
                                            (uint32_t)paragraph->visualRuns[visual]);
        }
        BidiDumpSinkLine(sink, line, length);

        BidiDumpSinkLine(sink, "scalars:", 8u);
        /* The cap, and it has to be the *bound* rather than a counter checked inside the loop: a counter
         * that is only assigned after the loop prints every scalar, which is the bug this replaces (a
         * 509-scalar paragraph overflowed every buffer the caller had). */
        for (size_t index = 0u;
             index < paragraph->scalarCount && index < RoboTextBidiDumpScalarLimit; index++) {
            length = 0u;
            length = RoboTextWriterText(line, sizeof(line), length, "  #");
            length = RoboTextWriterUnsigned(line, sizeof(line), length, (uint32_t)index);
            length = RoboTextWriterText(line, sizeof(line), length, "  ");
            length = BidiWriteCodePoint(line, sizeof(line), length,
                                        (RoboTextCodePoint)paragraph->scalars[index]);
            length = RoboTextWriterText(line, sizeof(line), length, "  ");
            length = RoboTextWriterText(line, sizeof(line), length,
                                        BidiClassName(RoboTextUnicodeBidiClass(
                                            (RoboTextCodePoint)paragraph->scalars[index])));
            length = RoboTextWriterText(line, sizeof(line), length, "    lvl ");
            if (RoboTextBidiLevelIsResolved(paragraph->levels[index])) {
                length = RoboTextWriterUnsigned(line, sizeof(line), length, paragraph->levels[index]);
            } else {
                length = RoboTextWriterText(line, sizeof(line), length, "x");
            }
            BidiDumpSinkLine(sink, line, length);
        }
        printed = (paragraph->scalarCount < RoboTextBidiDumpScalarLimit)
                      ? paragraph->scalarCount
                      : RoboTextBidiDumpScalarLimit;
        if (paragraph->scalarCount > printed) {
            length = 0u;
            length = RoboTextWriterUnsigned(line, sizeof(line), length,
                                            (uint32_t)(paragraph->scalarCount - printed));
            length = RoboTextWriterText(line, sizeof(line), length, " more scalars not printed");
            BidiDumpSinkLine(sink, line, length);
        }
        if (sink->failed) {
            RoboFree(offsets);
            return RoboNotSupported;
        }
        /*
         * The measure pass is authoritative, and it is checked **before** the write pass rather than
         * after it: a dump that does not fit is complete-or-absent, so the caller's buffer must be left
         * empty (it already is — `buffer[0]` was cleared on entry) rather than holding a prefix of the
         * table. Writing a prefix and then reporting the overflow would be the worst of both answers.
         */
        if (measure.length + 1u > capacity) {
            if (outLength) {
                *outLength = measure.length;
            }
            RoboFree(offsets);
            return RoboNotSupported;
        }
    }
    if (outLength) {
        *outLength = write.length;
    }
    RoboFree(offsets);
    return RoboOk;
}
