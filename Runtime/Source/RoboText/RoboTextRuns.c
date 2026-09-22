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
 * Phase 12.5 moved the two classifications out of this file and into `RoboTextUnicode.c`, where they are
 * the module's one implementation:
 *
 *   the decoder             `RoboTextUnicodeDecodeAll` — one UTF-8 policy, one place that reads `next`
 *   the script resolver     `RoboTextScriptRunsResolve` — the neutrality rule, so the run set and the
 *                           analysis layer cannot disagree about where a script run begins
 *   the direction classifier `RoboTextUnicodeBaseDirection` / `RoboTextUnicodeClassifyDirection`
 *
 * What is left here is the *intersection* — the part that is genuinely about a run set rather than about
 * Unicode: a text run exists where two boundary sets agree, and the walk below is the only place that
 * knows it.
 *
 * Direction is classified, not resolved (see the header): `R`/`AL` are right-to-left, `L` is
 * left-to-right, and everything else takes the paragraph direction — the caller's declared base, or the
 * text's own first strong scalar, or `Unknown` when the text has none of either. The `Unknown` case is a
 * real state (`"123 456"` with nothing declared) rather than an error, so the classifier answers it
 * rather than inventing an LTR claim the bidirectional algorithm has not made yet.
 */

#include "RoboText/RoboTextRuns.h"

#include "RoboMem/RoboMem.h"
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
    RoboTextDirection baseDirection;
    RoboTextScript scriptHint;
    RoboTextLanguage language;
    /** The setting every text run carries. Copied, so the set keeps nothing borrowed but the text. */
    RoboTextFontContext font;
};

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

    status = RoboTextUnicodeDecodeAll(view, &scalars, &count, diag);
    if (status != RoboOk) {
        RoboFree(set);
        return status;
    }
    set->scalarCount = count;
    if (count == 0u) {
        /* No text is a fact about a document. An empty set answers every door below without a run. */
        set->baseDirection = (options->baseDirection == RoboTextDirectionUnknown)
                                 ? RoboTextDirectionUnknown
                                 : options->baseDirection;
        *out = set;
        return RoboOk;
    }

    directions = (RoboTextDirection *)RoboCalloc(count, sizeof(*directions));
    set->scriptRuns = (RoboTextScriptRun *)RoboCalloc(count, sizeof(*set->scriptRuns));
    set->directionRuns = (RoboTextDirectionRun *)RoboCalloc(count, sizeof(*set->directionRuns));
    set->textRuns = (RoboTextRun *)RoboCalloc(count, sizeof(*set->textRuns));
    if (!directions || !set->scriptRuns || !set->directionRuns || !set->textRuns) {
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
            RoboFree(directions);
            RoboFree(scalars);
            RoboTextRunSetDestroy(set);
            return status;
        }
        set->baseDirection = RoboTextUnicodeBaseDirection(scalars, (size_t)count, options->baseDirection);
        for (RoboTextScalarIndex index = 0u; index < count; index++) {
            directions[index] = RoboTextUnicodeClassifyDirection((RoboTextCodePoint)scalars[index],
                                                                 set->baseDirection);
        }

        for (RoboTextScalarIndex index = 0u; index < count;) {
            RoboTextScalarIndex end = index + 1u;

            while (end < count && directions[end] == directions[index]) {
                end++;
            }
            set->directionRuns[set->directionRunCount].range = RoboTextRangeMake(index, end);
            set->directionRuns[set->directionRunCount].direction = directions[index];
            set->directionRunCount++;
            index = end;
        }
        /*
         * The text runs are the *intersection* of the two walks, which is why this loop tests both arrays
         * rather than walking either: a boundary in either segmentation is a boundary here, and the result
         * is contiguous and ascending by construction — the property `RunAtScalar`'s binary search relies
         * on.
         */
        for (RoboTextScalarIndex index = 0u; index < count;) {
            RoboTextScalarIndex end = index + 1u;

            while (end < count && scripts[end] == scripts[index] &&
                   directions[end] == directions[index]) {
                end++;
            }
            set->textRuns[set->textRunCount].range = RoboTextRangeMake(index, end);
            set->textRuns[set->textRunCount].script = scripts[index];
            set->textRuns[set->textRunCount].direction = directions[index];
            set->textRuns[set->textRunCount].baseDirection = set->baseDirection;
            set->textRuns[set->textRunCount].font = set->font;
            set->textRuns[set->textRunCount].language = set->language;
            set->textRunCount++;
            index = end;
        }
        RoboFree(scripts);
    }

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
    for (size_t index = 0u; index < set->textRunCount; index++) {
        const RoboTextRun *run = &set->textRuns[index];
        int seen = 0;

        if (run->direction == RoboTextDirectionRtl) {
            out->rtlRuns++;
        } else if (run->direction == RoboTextDirectionUnknown) {
            out->unknownDirectionRuns++;
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
