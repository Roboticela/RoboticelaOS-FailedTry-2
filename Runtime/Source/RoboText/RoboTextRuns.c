/*
 * RoboText's segmentation (Phase 12.4) — see RoboTextRuns.h.
 *
 * The file is one pass over the text's scalars that answers three questions per scalar (its script, its
 * classified direction, and nothing else), followed by three run walks that read those two arrays. The
 * order matters and is worth stating because it is what keeps the three segmentations consistent:
 * classification happens **once**, into per-scalar arrays, and every run type is then a *maximal span of
 * equal values* in one or two of those arrays. A design that segmented each run type independently would
 * have three decoders and three chances to disagree about what the text is.
 *
 * The neutrality rule is the one place a reader should slow down. A scalar whose script is Common or
 * Inherited does not get a run of its own: it takes the **previous** resolved script, and a scalar before
 * the first resolved script takes the **next** one. That is a single forward pass, it is stable (the
 * answer never depends on where the reader is standing), and it is what makes `"abc 123 def"` one script
 * run rather than three — which is the shape every consumer above wants, and the reason
 * `RoboTextScriptIsNeutral` exists at all.
 *
 * Direction is classified, not resolved (see the header): `R`/`AL` are right-to-left, `L` is
 * left-to-right, and everything else takes the paragraph direction — the caller's declared base, or the
 * text's own first strong scalar, or `Unknown` when the text has none of either. The `Unknown` case is a
 * real state (`"123 456"` with nothing declared) rather than an error, so the classifier answers it
 * rather than inventing an LTR claim the bidirectional algorithm has not made yet.
 */

#include "RoboText/RoboTextRuns.h"

#include "RoboMem/RoboMem.h"
#include "RoboUnicode/RoboUnicode.h"

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
/* Classification                                                      */
/* ------------------------------------------------------------------ */

/**
 * Decode the whole view into a scalar array, in one pass.
 *
 * Two walks rather than one growing array: the first counts (so the second can allocate exactly), and
 * neither walk allocates. A `DecodeRange`-shaped loop over `ScalarAt` would be O(n²) for a document's
 * text, which is the one input size this module must not be quadratic in.
 */
static Result RunsDecodeAll(RoboTextView view,
                            Uchar32 **outScalars,
                            RoboTextScalarIndex *outCount,
                            RoboTextDiag *diag)
{
    size_t offset = 0u;
    size_t count = 0u;
    Uchar32 *scalars;

    *outScalars = NULL;
    *outCount = 0u;
    if (view.length != 0u && !view.bytes) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a view with a length needs bytes");
    }
    if (view.length == 0u) {
        return RoboOk;
    }
    while (offset < view.length) {
        size_t next = offset;

        if (UnicodeUtf8Decode(view.bytes, view.length, offset, RoboUnicodeReplace, NULL, &next) !=
                RoboUnicodeOk ||
            next <= offset) {
            return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboParseError,
                                   "the text could not be decoded");
        }
        offset = next;
        count++;
    }
    if (count > (size_t)RoboTextIndexMax) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "the text is longer than this build can index");
    }
    scalars = (Uchar32 *)RoboCalloc(count, sizeof(*scalars));
    if (!scalars) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the decoded text could not be allocated");
    }
    offset = 0u;
    for (size_t index = 0u; index < count; index++) {
        size_t next = offset;

        if (UnicodeUtf8Decode(view.bytes, view.length, offset, RoboUnicodeReplace, &scalars[index],
                              &next) != RoboUnicodeOk ||
            next <= offset) {
            RoboFree(scalars);
            return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboParseError,
                                   "the text could not be decoded");
        }
        offset = next;
    }
    *outScalars = scalars;
    *outCount = (RoboTextScalarIndex)count;
    return RoboOk;
}

/** The script a scalar belongs to, honouring a caller's hint. */
static RoboTextScript RunsRawScript(Uchar32 scalar, RoboTextScript hint)
{
    if (hint != RoboTextScriptCommon) {
        return hint;
    }
    return MakeUnicodeScript(scalar);
}

/** The direction a scalar's own `Bidi_Class` classifies as, before neutrals are resolved. */
static RoboTextDirection RunsStrongDirection(Uchar32 scalar)
{
    switch (MakeUnicodeBidiClass(scalar)) {
        case RoboUBcL:
            return RoboTextDirectionLtr;
        case RoboUBcR:
        case RoboUBcAl:
            return RoboTextDirectionRtl;
        default:
            return RoboTextDirectionUnknown;
    }
}

/**
 * Fill `script[]` from the raw classifications, resolving Common/Inherited by the neutrality rule.
 *
 * One forward pass. `carry` starts unset, so a scalar before the first resolved script is provisionally
 * Common; the moment a resolved script is seen, everything provisional before it is rewritten to that
 * script — which is exactly "the next resolved script", arrived at without a backward pass.
 */
static void RunsResolveScripts(const Uchar32 *scalars,
                               RoboTextScalarIndex count,
                               RoboTextScript hint,
                               RoboTextScript *script)
{
    RoboTextScript carry = RoboTextScriptCommon;
    int haveCarry = 0;

    for (RoboTextScalarIndex index = 0u; index < count; index++) {
        RoboTextScript raw = RunsRawScript(scalars[index], hint);

        if (!RoboTextScriptIsNeutral(raw)) {
            if (!haveCarry) {
                for (RoboTextScalarIndex back = 0u; back < index; back++) {
                    script[back] = raw;
                }
                haveCarry = 1;
            }
            carry = raw;
            script[index] = raw;
        } else {
            script[index] = haveCarry ? carry : RoboTextScriptCommon;
        }
    }
}

/**
 * The paragraph direction the text is classified against.
 *
 * The declared base wins; otherwise the text's own first strong scalar; otherwise `Unknown`. The third
 * case is why `UnicodeBidiParagraphDirection` alone is not the answer: it documents "0 if none", and 0 is
 * `RoboBidiLtr` — so taking it at face value would make `"123 456"` a left-to-right paragraph, which is a
 * claim UAX #9's P2/P3 deliberately does not make.
 */
static RoboTextDirection RunsBaseDirection(const Uchar32 *scalars,
                                           RoboTextScalarIndex count,
                                           RoboTextDirection declared)
{
    int hasStrong = 0;

    if (declared != RoboTextDirectionUnknown) {
        return declared;
    }
    for (RoboTextScalarIndex index = 0u; index < count; index++) {
        if (RunsStrongDirection(scalars[index]) != RoboTextDirectionUnknown) {
            hasStrong = 1;
            break;
        }
    }
    if (!hasStrong) {
        return RoboTextDirectionUnknown;
    }
    return (UnicodeBidiParagraphDirection(scalars, (size_t)count) == RoboBidiRtl)
               ? RoboTextDirectionRtl
               : RoboTextDirectionLtr;
}

/** Classify every scalar's direction: strong classes from the scalar, everything else from the base. */
static void RunsClassifyDirections(const Uchar32 *scalars,
                                  RoboTextScalarIndex count,
                                  RoboTextDirection base,
                                  RoboTextDirection *direction)
{
    for (RoboTextScalarIndex index = 0u; index < count; index++) {
        RoboTextDirection strong = RunsStrongDirection(scalars[index]);

        direction[index] = (strong == RoboTextDirectionUnknown) ? base : strong;
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
    RoboTextScript *scripts = NULL;
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

    status = RunsDecodeAll(view, &scalars, &count, diag);
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

    scripts = (RoboTextScript *)RoboCalloc(count, sizeof(*scripts));
    directions = (RoboTextDirection *)RoboCalloc(count, sizeof(*directions));
    set->scriptRuns = (RoboTextScriptRun *)RoboCalloc(count, sizeof(*set->scriptRuns));
    set->directionRuns = (RoboTextDirectionRun *)RoboCalloc(count, sizeof(*set->directionRuns));
    set->textRuns = (RoboTextRun *)RoboCalloc(count, sizeof(*set->textRuns));
    if (!scripts || !directions || !set->scriptRuns || !set->directionRuns || !set->textRuns) {
        RoboFree(scripts);
        RoboFree(directions);
        RoboFree(scalars);
        RoboTextRunSetDestroy(set);
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the run arrays could not be allocated");
    }

    RunsResolveScripts(scalars, count, options->scriptHint, scripts);
    set->baseDirection = RunsBaseDirection(scalars, count, options->baseDirection);
    RunsClassifyDirections(scalars, count, set->baseDirection, directions);

    for (RoboTextScalarIndex index = 0u; index < count;) {
        RoboTextScalarIndex end = index + 1u;

        while (end < count && scripts[end] == scripts[index]) {
            end++;
        }
        set->scriptRuns[set->scriptRunCount].range = RoboTextRangeMake(index, end);
        set->scriptRuns[set->scriptRunCount].script = scripts[index];
        set->scriptRunCount++;
        index = end;
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
     * rather than walking either: a boundary in either segmentation is a boundary here, and the result is
     * contiguous and ascending by construction — the property `RunAtScalar`'s binary search relies on.
     */
    for (RoboTextScalarIndex index = 0u; index < count;) {
        RoboTextScalarIndex end = index + 1u;

        while (end < count && scripts[end] == scripts[index] && directions[end] == directions[index]) {
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
