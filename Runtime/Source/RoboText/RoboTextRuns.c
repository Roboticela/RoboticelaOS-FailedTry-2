/*
 * RoboText's segmentation (Phase 12.4, re-based on the shared Unicode doors in 12.5) — see
 * RoboTextRuns.h.
 *
 * The file is one pass over the text's scalars that answers three questions per scalar (its script, its
 * classified direction, and nothing else), followed by three run walks that read those two arrays. The
 * order matters and is worth stating because it is what keeps the three segmentations consistent:
 * classification happens **once**, into per-scalar arrays, and every run type is then a *maximal span of
 * equal values* in one or two of those arrays. A design that segmented each run type independently would
 * have three decoders and three chances to disagree about what the text is.
 *
 * 12.5 moved the two classifications out of this file and into the module's own files, where each is the
 * one implementation:
 *
 *   the decoder             `RoboTextUnicodeDecodeAll` — one UTF-8 policy, one place that reads `next`
 *   the script resolver     `RoboTextScriptRunsResolve` — the neutrality rule, so the run set and the
 *                           analysis layer cannot disagree about where a script run begins
 *   the direction resolver  `RoboTextDirectionRunsResolve` (Phase 12.7) — the class projection, the
 *                           paragraph context and the neutral/number rules, for the same reason
 *
 * What is left here is the *intersection* — the part that is genuinely about a run set rather than about
 * Unicode: a text run exists where two boundary sets agree, and the walk below is the only place that
 * knows it.
 *
 * Direction is classified, not resolved (see the header), and 12.7 is what made the difference real: a run
 * here says which **context** its scalars belong to — a declared paragraph direction, the text's own first
 * strong scalar, a number's even level, or a neutral block's both-sides answer — and never a level or a
 * visual order. `RoboTextDirectionUnknown` remains a real state (`"123 456"` with nothing declared) rather
 * than an error, so the resolver answers it rather than inventing an LTR claim the algorithm has not made.
 *
 * 12.8 added a fourth thing to the same pass: the **resolution**. The direction runs are unchanged and stay
 * 12.7's; the text runs carry the direction the resolved level implies plus that level; and the paragraph
 * itself is published. Nothing is reordered, and the source text is never touched.
 */

#include "RoboText/RoboTextRuns.h"

#include "RoboMem/RoboMem.h"
#include "RoboText/RoboTextBidi.h"
#include "RoboText/RoboTextKerning.h"
#include "RoboText/RoboTextLigature.h"
#include "RoboText/RoboTextDirection.h"
#include "RoboText/RoboTextUnicode.h"

#include <string.h>

struct RoboTextRunSet {
    RoboTextScriptRun *scriptRuns;
    size_t scriptRunCount;
    RoboTextDirectionRun *directionRuns;
    size_t directionRunCount;
    RoboTextRun *textRuns;
    size_t textRunCount;
    RoboTextScalarIndex scalarCount;
    /** 12.7's classification of the paragraph; `Unknown` is a real state here (see the header). */
    RoboTextDirection baseDirection;
    /** 12.8's resolution of the same paragraph. Owned; freed with the set. */
    RoboTextBidiParagraph *paragraph;
    RoboTextScript scriptHint;
    RoboTextLanguage language;
    RoboTextFeatureSet features;
    /** The setting every text run carries. Copied, so the set keeps nothing borrowed but the text. */
    RoboTextFontContext font;
};

/* ------------------------------------------------------------------ */
/* Per-range feature overrides                                         */
/* ------------------------------------------------------------------ */

/** "No range covers this scalar", as an index no entry can have. */
#define RoboTextNoFeatureRange ((size_t)-1)

/** The index of the **first** range @p options states that covers @p scalar, or @ref RoboTextNoFeatureRange. */
static size_t FeatureRangeAt(const RoboTextShapeOptions *options, RoboTextScalarIndex scalar)
{
    for (size_t index = 0u; index < options->featureRangeCount; index++) {
        if (RoboTextRangeContains(options->featureRanges[index].range, scalar)) {
            return index;
        }
    }
    return RoboTextNoFeatureRange;
}

/**
 * @p base with @p range's settings merged over it.
 *
 * The range is the **more specific** statement, so its tags win and the ones it does not name keep the
 * whole-text answer — the rule a stylesheet's cascade applies, and the reason a range holds a whole set rather
 * than one tag. A set with no room left for a tag the range names keeps what it has: the cap on
 * `RoboTextMaxFeatures` is a fact about the request, not an error this layer can act on.
 */
static void FeatureRangeMerge(const RoboTextFeatureRange *range, RoboTextFeatureSet *base)
{
    for (size_t index = 0u; index < range->features.count; index++) {
        const RoboTextFeatureSetting *setting = &range->features.settings[index];

        (void)RoboTextFeatureSetSet(base, setting->tag, setting->state, setting->value);
    }
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

void RoboTextRunSetDestroy(RoboTextRunSet *set)
{
    if (!set) {
        return;
    }
    RoboFree(set->scriptRuns);
    RoboFree(set->directionRuns);
    RoboFree(set->textRuns);
    RoboTextBidiParagraphDestroy(set->paragraph);
    RoboFree(set);
}

Result RoboTextRunSetCreate(RoboTextView view,
                            const RoboTextFontContext *font,
                            const RoboTextShapeOptions *options,
                            RoboTextRunSet **out,
                            RoboTextDiag *diag)
{
    RoboTextShapeOptions defaults;
    RoboTextRunSet *set;
    Uchar32 *scalars = NULL;
    RoboTextScalarIndex count = 0u;
    RoboTextDirection *directions = NULL;
    /* The directions the *resolution* implies, per scalar: what a text run's `direction` reads, while
     * `directions` stays the classification the direction runs are built from. Two arrays, because the
     * two stages answer different questions about the same scalar (Phase 12.8). */
    RoboTextDirection *resolvedDirections = NULL;
    /* One entry per scalar: the range that covers it, or @ref RoboTextNoFeatureRange. Computed once, because
     * the text-run loop's third cut condition is "the effective features change" and asking the range list per
     * scalar inside that loop would compute the same answer `count x ranges` times. */
    size_t *featureRanges = NULL;
    Result status;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "an out pointer is required");
    }
    *out = NULL;
    if (!options) {
        RoboTextShapeOptionsInit(&defaults);
        options = &defaults;
    }
    if (!RoboTextShapeOptionsIsValid(options)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "the shape options are not usable");
    }
    if (view.length != 0u && !view.bytes) {
        /*
         * Named as the **text** rather than as the buffer the shared decoder would name: this is the door
         * a caller reaches when it hands in a view, so the message belongs to the caller's argument. The
         * decoder underneath reports the same condition under its own domain, which is right for a caller
         * that invoked it directly.
         */
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a view with a length needs bytes");
    }

    set = (RoboTextRunSet *)RoboCalloc(1u, sizeof(*set));
    if (!set) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the run set could not be allocated");
    }
    RoboTextFontContextInit(&set->font);
    if (font) {
        set->font = *font;
    }
    set->scriptHint = options->scriptHint;
    set->language = options->language;
    set->features = options->features;
    /* The ligature policy supplies the five ligature tags the caller did not state (Phase 12.12). Applied
     * here, once per run set, so the measurer and the painter read one resolved feature set — the same
     * property `RoboTypographyStyle` gives the shell, at the text layer's own seam. */
    RoboTextLigaturePolicyApplyIfUnset(&options->ligatures, &set->features);
    /* Kerning's policy lands in the same set, and after it: the two policies are about different tags, so the
     * order between them decides nothing — what matters is that both run *before* the engine's defaults are
     * consulted, which is where the run set hands the set to each run (Phase 12.13). */
    RoboTextKerningPolicyApplyIfUnset(&options->kerning, &set->features);

    status = RoboTextUnicodeDecodeAll(view, &scalars, &count, diag);
    if (status != RoboOk) {
        RoboFree(set);
        return status;
    }
    set->scalarCount = count;
    if (count == 0u) {
        /*
         * No text is a fact about a document. An empty set answers every door below without a run — and it
         * still gets a paragraph, because its base level is a fact too (declared, or P3's default) and a
         * caller reading `RoboTextRunSetBaseLevel` should not have to special-case emptiness.
         */
        RoboTextDirectionContext emptyContext = RoboTextDirectionContextForBase(options->baseDirection);

        set->baseDirection = (options->baseDirection == RoboTextDirectionUnknown)
                                 ? RoboTextDirectionUnknown
                                 : options->baseDirection;
        status = RoboTextBidiParagraphResolve(NULL, 0u, &emptyContext, &set->paragraph, diag);
        if (status != RoboOk) {
            RoboFree(set);
            return status;
        }
        *out = set;
        return RoboOk;
    }

    directions = (RoboTextDirection *)RoboCalloc(count, sizeof(*directions));
    resolvedDirections = (RoboTextDirection *)RoboCalloc(count, sizeof(*resolvedDirections));
    set->scriptRuns = (RoboTextScriptRun *)RoboCalloc(count, sizeof(*set->scriptRuns));
    set->directionRuns = (RoboTextDirectionRun *)RoboCalloc(count, sizeof(*set->directionRuns));
    set->textRuns = (RoboTextRun *)RoboCalloc(count, sizeof(*set->textRuns));
    featureRanges = (size_t *)RoboCalloc(count, sizeof(*featureRanges));
    if (!directions || !resolvedDirections || !set->scriptRuns || !set->directionRuns ||
        !set->textRuns || !featureRanges) {
        RoboFree(featureRanges);
        RoboFree(resolvedDirections);
        RoboFree(directions);
        RoboFree(scalars);
        RoboTextRunSetDestroy(set);
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the run arrays could not be allocated");
    }

    {
        /*
         * The per-scalar scripts and the script runs come out of **one** call, which is the 12.5 seam: the
         * neutrality rule is the resolver's, and this file only needs the two views of its answer — the
         * runs to publish and the per-scalar array to intersect with the direction classification. `count`
         * slots is the widest an answer can be, so the capacity can never be the reason a run is lost.
         */
        RoboTextScript *scripts = (RoboTextScript *)RoboCalloc(count, sizeof(*scripts));

        if (!scripts) {
            RoboFree(resolvedDirections);
            RoboFree(directions);
            RoboFree(scalars);
            RoboTextRunSetDestroy(set);
            return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                                   "the script array could not be allocated");
        }
        status = RoboTextScriptRunsResolve(scalars, (size_t)count, options->scriptHint, scripts,
                                           set->scriptRuns, (size_t)count, &set->scriptRunCount, diag);
        if (status != RoboOk) {
            RoboFree(scripts);
            RoboFree(featureRanges);
            RoboFree(resolvedDirections);
            RoboFree(directions);
            RoboFree(scalars);
            RoboTextRunSetDestroy(set);
            return status;
        }
        /*
         * The direction runs come the same way, from the phase's own resolver: one call fills the
         * per-scalar array the intersection below reads **and** the run array this set publishes, so the
         * direction boundaries in this file and the ones the analysis layer reports cannot differ. The
         * declared base maps onto a direction context — `Unknown` means "nothing declared", which is
         * `{ Auto, Unknown }` rather than an LTR default the caller did not ask for.
         */
        RoboTextDirectionContext directionContext = RoboTextDirectionContextForBase(options->baseDirection);

        status = RoboTextDirectionRunsResolve(scalars, (size_t)count, &directionContext, directions,
                                             set->directionRuns, (size_t)count,
                                             &set->directionRunCount, &set->baseDirection, diag);
        if (status != RoboOk) {
            RoboFree(scripts);
            RoboFree(featureRanges);
            RoboFree(resolvedDirections);
            RoboFree(directions);
            RoboFree(scalars);
            RoboTextRunSetDestroy(set);
            return status;
        }
        /*
         * And the **resolution**, through the phase's own one door (Phase 12.8). It takes the same context
         * the classification did, so the paragraph a caller declared and the paragraph that was classified
         * are the same paragraph, and it owns its answer: levels, logical runs and L2's visual order all
         * come from it, and this file only reads them.
         */
        status = RoboTextBidiParagraphResolve(scalars, (size_t)count, &directionContext,
                                             &set->paragraph, diag);
        if (status != RoboOk) {
            RoboFree(scripts);
            RoboFree(featureRanges);
            RoboFree(resolvedDirections);
            RoboFree(directions);
            RoboFree(scalars);
            RoboTextRunSetDestroy(set);
            return status;
        }
        for (RoboTextScalarIndex index = 0u; index < count; index++) {
            resolvedDirections[index] =
                RoboTextBidiLevelDirection(RoboTextBidiParagraphResolvedLevel(set->paragraph, index));
            featureRanges[index] = FeatureRangeAt(options, index);
        }
        /*
         * The text runs are the *intersection* of the two walks, which is why this loop tests both arrays
         * rather than walking either: a boundary in either the script segmentation or the resolved
         * direction is a boundary here, and the result is contiguous and ascending by construction — the
         * property `RunAtScalar`'s binary search relies on.
         *
         * The direction it cuts on is the **resolved** one, and the level travels on the run beside it.
         * Cutting on the level instead would split wherever a level moves without changing the parity —
         * two stretches a shaper does the same thing with. The algorithm's own boundaries are not lost:
         * they are `RoboTextRunSetBidiRun`'s.
         */
        for (RoboTextScalarIndex index = 0u; index < count;) {
            RoboTextScalarIndex end = index + 1u;

            /*
             * Three conditions, and the third is 12.9's feature ranges: a run carries **one** feature set, so
             * where the effective settings change the run must end. That is what makes a per-range override a
             * fact about the shaping pass rather than a note a paint layer has to remember, and it is why the
             * cut happens here — before any plan is resolved.
             */
            while (end < count && scripts[end] == scripts[index] &&
                   resolvedDirections[end] == resolvedDirections[index] &&
                   featureRanges[end] == featureRanges[index]) {
                end++;
            }
            set->textRuns[set->textRunCount].range = RoboTextRangeMake(index, end);
            set->textRuns[set->textRunCount].script = scripts[index];
            set->textRuns[set->textRunCount].direction = resolvedDirections[index];
            set->textRuns[set->textRunCount].bidiLevel =
                RoboTextBidiParagraphResolvedLevel(set->paragraph, index);
            set->textRuns[set->textRunCount].baseDirection = set->baseDirection;
            set->textRuns[set->textRunCount].font = set->font;
            set->textRuns[set->textRunCount].language = set->language;
            set->textRuns[set->textRunCount].features = set->features;
            if (featureRanges[index] != RoboTextNoFeatureRange) {
                FeatureRangeMerge(&options->featureRanges[featureRanges[index]],
                                  &set->textRuns[set->textRunCount].features);
            }
            set->textRunCount++;
            index = end;
        }
        RoboFree(scripts);
    }

    RoboFree(featureRanges);
    RoboFree(resolvedDirections);
    RoboFree(directions);
    RoboFree(scalars);
    *out = set;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Reading                                                             */
/* ------------------------------------------------------------------ */

size_t RoboTextRunSetScriptRunCount(const RoboTextRunSet *set)
{
    return set ? set->scriptRunCount : 0u;
}

const RoboTextScriptRun *RoboTextRunSetScriptRun(const RoboTextRunSet *set, size_t index)
{
    if (!set || index >= set->scriptRunCount) {
        return NULL;
    }
    return &set->scriptRuns[index];
}

size_t RoboTextRunSetDirectionRunCount(const RoboTextRunSet *set)
{
    return set ? set->directionRunCount : 0u;
}

const RoboTextDirectionRun *RoboTextRunSetDirectionRun(const RoboTextRunSet *set, size_t index)
{
    if (!set || index >= set->directionRunCount) {
        return NULL;
    }
    return &set->directionRuns[index];
}

size_t RoboTextRunSetTextRunCount(const RoboTextRunSet *set)
{
    return set ? set->textRunCount : 0u;
}

const RoboTextRun *RoboTextRunSetTextRun(const RoboTextRunSet *set, size_t index)
{
    if (!set || index >= set->textRunCount) {
        return NULL;
    }
    return &set->textRuns[index];
}

RoboTextScalarIndex RoboTextRunSetScalarCount(const RoboTextRunSet *set)
{
    return set ? set->scalarCount : 0u;
}

RoboTextDirection RoboTextRunSetBaseDirection(const RoboTextRunSet *set)
{
    return set ? set->baseDirection : RoboTextDirectionUnknown;
}

RoboTextBidiLevel RoboTextRunSetBaseLevel(const RoboTextRunSet *set)
{
    return RoboTextBidiParagraphBaseLevel(set ? set->paragraph : NULL);
}

RoboTextBidiBaseSource RoboTextRunSetBaseSource(const RoboTextRunSet *set)
{
    return RoboTextBidiParagraphBaseSource(set ? set->paragraph : NULL);
}

const RoboTextBidiParagraph *RoboTextRunSetBidiParagraph(const RoboTextRunSet *set)
{
    return set ? set->paragraph : NULL;
}

size_t RoboTextRunSetBidiRunCount(const RoboTextRunSet *set)
{
    return RoboTextBidiParagraphRunCount(set ? set->paragraph : NULL);
}

const RoboTextBidiRun *RoboTextRunSetBidiRun(const RoboTextRunSet *set, size_t index)
{
    return RoboTextBidiParagraphRun(set ? set->paragraph : NULL, index);
}

Result RoboTextRunSetVisualBidiRun(const RoboTextRunSet *set,
                                   size_t visual,
                                   size_t *outLogicalRunIndex,
                                   RoboTextDiag *diag)
{
    if (!set || !set->paragraph) {
        return RoboTextDiagMsg(diag, RoboTextDomainBidi, RoboInvalidArgument,
                               "a run set with a resolved paragraph is required");
    }
    return RoboTextBidiParagraphVisualRun(set->paragraph, visual, outLogicalRunIndex, diag);
}

RoboTextScript RoboTextRunSetScriptHint(const RoboTextRunSet *set)
{
    return set ? set->scriptHint : RoboTextScriptCommon;
}

const RoboTextLanguage *RoboTextRunSetLanguage(const RoboTextRunSet *set)
{
    static const RoboTextLanguage empty = {{0}};

    return set ? &set->language : &empty;
}

const RoboTextRun *RoboTextRunSetRunAtScalar(const RoboTextRunSet *set, RoboTextScalarIndex index)
{
    size_t low;
    size_t high;

    if (!set || set->textRunCount == 0u || index >= set->scalarCount) {
        return NULL;
    }
    low = 0u;
    high = set->textRunCount - 1u;
    while (low <= high) {
        size_t mid = low + (high - low) / 2u;

        if (RoboTextRangeContains(set->textRuns[mid].range, index)) {
            return &set->textRuns[mid];
        }
        if (index < set->textRuns[mid].range.start) {
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

Result RoboTextRunSetGetStats(const RoboTextRunSet *set, RoboTextRunStats *out)
{
    if (!set || !out) {
        return RoboInvalidArgument;
    }
    memset(out, 0, sizeof(*out));
    out->scriptRuns = set->scriptRunCount;
    out->directionRuns = set->directionRunCount;
    out->textRuns = set->textRunCount;
    out->scalars = set->scalarCount;
    out->bidiRuns = RoboTextBidiParagraphRunCount(set->paragraph);
    out->baseLevel = RoboTextBidiParagraphBaseLevel(set->paragraph);
    {
        RoboTextBidiStats bidiStats;

        if (RoboTextBidiParagraphGetStats(set->paragraph, &bidiStats) == RoboOk) {
            out->removedScalars = bidiStats.removedScalars;
            out->rtlScalars = bidiStats.rtlScalars;
        }
    }
    /* The direction runs' own classification, which is the state `unknownDirectionRuns` describes now
     * that the text runs are resolved: `"123 456"` with nothing declared has one unknown *direction* run
     * and no unknown text run. Counting both would double-count one fact under one name. */
    for (size_t index = 0u; index < set->directionRunCount; index++) {
        if (set->directionRuns[index].direction == RoboTextDirectionUnknown) {
            out->unknownDirectionRuns++;
        }
    }
    for (size_t index = 0u; index < set->textRunCount; index++) {
        const RoboTextRun *run = &set->textRuns[index];
        int seen = 0;

        if (run->direction == RoboTextDirectionRtl) {
            out->rtlRuns++;
        }
        /* Distinct scripts by a scan over the runs already counted: a run count is small (a handful even
         * for a mixed-script paragraph) and a hash set here would be more machinery than the answer is
         * worth. The comparison is against earlier runs, so the count does not depend on iteration order. */
        for (size_t back = 0u; back < index; back++) {
            if (set->textRuns[back].script == run->script) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            out->distinctScripts++;
        }
    }
    return RoboOk;
}

Result RoboTextFuzzSegment(const uint8_t *data, size_t size)
{
    RoboTextView view;
    RoboTextRunSet *set = NULL;
    RoboTextRunStats stats;

    if (!data && size != 0u) {
        return RoboInvalidArgument;
    }
    view = RoboTextViewMake((const char *)data, size);
    if (RoboTextRunSetCreate(view, NULL, NULL, &set, NULL) != RoboOk) {
        return RoboError;
    }
    (void)RoboTextRunSetGetStats(set, &stats);
    RoboTextRunSetDestroy(set);
    return RoboOk;
}
