/*
 * Arabic shaping (Phase 12.19 §8–§15) — see `RoboTextArabic.h` for the model and the rules.
 *
 * The file is four pieces and each is small on purpose, because the *hard* part of Arabic shaping is not
 * any one of them: the joining types are Unicode's, the substitution is GSUB's, the mark and cursive
 * positioning is GPOS's, and the order of the text is bidi's. What is left is the connective tissue this
 * file is: which letter takes which form, where each form's feature applies, what shapes together, and which
 * features run in which stage.
 *
 * Nothing here walks UTF-8 (the range is decoded once, through the module's one decoder), nothing keeps a
 * range table of its own (joining types come from `RoboUnicode/RoboUnicodeJoining.h`), and nothing writes a
 * glyph id.
 */
#include "RoboText/RoboTextArabic.h"

#include "RoboMem/RoboMem.h"

#include "RoboText/RoboTextUnicode.h"
#include "RoboTextInternal.h"

#include "RoboUnicode/RoboUnicodeJoining.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Forms and masks                                                     */
/* ------------------------------------------------------------------ */

const char *RoboTextArabicFormName(RoboTextArabicForm form)
{
    switch (form) {
    case RoboTextArabicFormIsolated:
        return "isolated";
    case RoboTextArabicFormInitial:
        return "initial";
    case RoboTextArabicFormMedial:
        return "medial";
    case RoboTextArabicFormFinal:
        return "final";
    case RoboTextArabicFormNone:
    default:
        return "none";
    }
}

const char *RoboTextArabicFormFeature(RoboTextArabicForm form)
{
    switch (form) {
    case RoboTextArabicFormIsolated:
        return "isol";
    case RoboTextArabicFormInitial:
        return "init";
    case RoboTextArabicFormMedial:
        return "medi";
    case RoboTextArabicFormFinal:
        return "fina";
    case RoboTextArabicFormNone:
    default:
        return "";
    }
}

uint32_t RoboTextArabicFormMask(RoboTextArabicForm form)
{
    switch (form) {
    case RoboTextArabicFormIsolated:
        return (uint32_t)RoboTextArabicMaskIsolated;
    case RoboTextArabicFormInitial:
        return (uint32_t)RoboTextArabicMaskInitial;
    case RoboTextArabicFormMedial:
        return (uint32_t)RoboTextArabicMaskMedial;
    case RoboTextArabicFormFinal:
        return (uint32_t)RoboTextArabicMaskFinal;
    case RoboTextArabicFormNone:
    default:
        return RoboTextComplexMaskAll;
    }
}

int RoboTextArabicFormIsShaped(RoboTextArabicForm form)
{
    return form == RoboTextArabicFormIsolated || form == RoboTextArabicFormInitial ||
           form == RoboTextArabicFormMedial || form == RoboTextArabicFormFinal;
}

/* ------------------------------------------------------------------ */
/* Joining                                                             */
/* ------------------------------------------------------------------ */

/** Non-zero when @p cp is a letter (the three alphabetic categories) rather than a mark or a sign. */
static int ArabicIsLetter(RoboTextCodePoint cp)
{
    UnicodeGeneralCategory category = UnicodeCategory((Uchar32)cp);

    return category == RoboUGcLo || category == RoboUGcLm || category == RoboUGcLt;
}

/** Non-zero when @p cp belongs to a script this build shapes by joining. */
static int ArabicHasJoiningScript(RoboTextCodePoint cp)
{
    return RoboTextComplexStrategyForScript((RoboTextScript)RoboTextUnicodeScript(cp)) ==
           RoboTextComplexStrategyArabic;
}

/**
 * The neighbouring non-transparent scalar on one side of @p index, or a sentinel when there is none.
 *
 * The walk is the rule "a mark does not break a connection": a transparent scalar is stepped over, and the
 * answer is the first scalar that is not transparent — or a scalar the caller's own rule refuses. The bound
 * is the range rather than the text, because a run's forms are the run's business: shaping half a word and
 * looking outside it for a neighbour would make a run's answer depend on where it was cut.
 */
static int ArabicNeighbour(const RoboTextCodePoint *scalars,
                           size_t count,
                           size_t index,
                           int forward,
                           RoboTextCodePoint *out)
{
    size_t at = index;

    for (;;) {
        if (forward) {
            at++;
            if (at >= count) {
                return 0;
            }
        } else {
            if (at == 0u) {
                return 0;
            }
            at--;
        }
        if (UnicodeJoiningTypeOf((Uchar32)scalars[at]) != RoboUJtTransparent) {
            *out = scalars[at];
            return 1;
        }
    }
}

/**
 * One scalar's contextual form.
 *
 * The whole of the Arabic joining rule, and it is four questions in one order: what type is this letter,
 * what type is the nearest letter before it, what type is the nearest after it, and does this letter *accept*
 * a connection from that side. The last part is what makes `ALEF` final rather than medial — it accepts from
 * the left and gives nothing to the right — and it is asked of `UnicodeJoiningTypeJoinsRight`/`…JoinsLeft`
 * rather than re-derived here, because the asymmetry is a property of the type and not of this loop.
 */
static RoboTextArabicForm ArabicFormAt(const RoboTextCodePoint *scalars, size_t count, size_t index)
{
    UnicodeJoiningType type = UnicodeJoiningTypeOf((Uchar32)scalars[index]);
    RoboTextCodePoint before = 0u;
    RoboTextCodePoint after = 0u;
    int joinsBefore;
    int joinsAfter;
    int acceptsLeft;
    int acceptsRight;

    if (type == RoboUJtTransparent) {
        /* A mark has no connecting shape of its own; its base's connection is unaffected. */
        return RoboTextArabicFormNone;
    }
    if (type == RoboUJtNonJoining) {
        /*
         * A non-joining *letter* is shaped isolated — `HAMZA` is one, and the font's `isol` lookups are where
         * it gets its shape. A non-joining non-letter (a digit, a comma, a space) takes no form at all: asking
         * a font for `medi` on a comma would be asking it a question about a character it does not shape.
         */
        if (ArabicIsLetter(scalars[index]) && ArabicHasJoiningScript(scalars[index])) {
            return RoboTextArabicFormIsolated;
        }
        return RoboTextArabicFormNone;
    }

    acceptsLeft = UnicodeJoiningTypeJoinsRight(type);
    acceptsRight = UnicodeJoiningTypeJoinsLeft(type);
    joinsBefore = ArabicNeighbour(scalars, count, index, 0, &before) &&
                  UnicodeJoiningTypeJoinsLeft(UnicodeJoiningTypeOf((Uchar32)before));
    joinsAfter = ArabicNeighbour(scalars, count, index, 1, &after) &&
                 UnicodeJoiningTypeJoinsRight(UnicodeJoiningTypeOf((Uchar32)after));

    if (acceptsLeft && acceptsRight && joinsBefore && joinsAfter) {
        return RoboTextArabicFormMedial;
    }
    if (acceptsLeft && joinsBefore) {
        return RoboTextArabicFormFinal;
    }
    if (acceptsRight && joinsAfter) {
        return RoboTextArabicFormInitial;
    }
    return RoboTextArabicFormIsolated;
}

Result RoboTextArabicJoiningResolve(RoboTextView view,
                                    RoboTextRange range,
                                    RoboTextArabicForm *out,
                                    size_t capacity,
                                    size_t *outCount,
                                    RoboTextDiag *diag)
{
    RoboTextCodePoint *scalars = NULL;
    size_t count = 0u;
    Result status;

    if (!out || !outCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a form array and a count are required");
    }
    *outCount = 0u;
    if (RoboTextRangeIsInverted(range)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a range that ends before it starts has no forms");
    }
    if (RoboTextRangeLength(range) == 0u) {
        return RoboOk;
    }
    if (capacity < RoboTextRangeLength(range)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboNotSupported,
                               "the caller's array cannot hold the range's forms");
    }
    status = RoboTextComplexDecodeRange(view, range, NULL, 0u, &count, diag);
    if (status != RoboOk) {
        return status;
    }
    scalars = (RoboTextCodePoint *)RoboCalloc(count, sizeof(*scalars));
    if (!scalars) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the joining pass could not decode the range");
    }
    status = RoboTextComplexDecodeRange(view, range, scalars, count, &count, diag);
    if (status != RoboOk) {
        RoboFree(scalars);
        return status;
    }
    for (size_t index = 0u; index < count; index++) {
        out[index] = ArabicFormAt(scalars, count, index);
    }
    RoboFree(scalars);
    *outCount = count;
    return RoboOk;
}

Result RoboTextArabicMasksBuild(RoboTextView view,
                                RoboTextRange range,
                                uint32_t *masks,
                                size_t capacity,
                                size_t *outCount,
                                RoboTextDiag *diag)
{
    RoboTextArabicForm *forms = NULL;
    size_t count = 0u;
    Result status;

    if (!masks || !outCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a mask array and a count are required");
    }
    *outCount = 0u;
    if (RoboTextRangeLength(range) == 0u) {
        return RoboOk;
    }
    if (capacity < RoboTextRangeLength(range)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboNotSupported,
                               "the caller's array cannot hold the range's masks");
    }
    forms = (RoboTextArabicForm *)RoboCalloc(RoboTextRangeLength(range), sizeof(*forms));
    if (!forms) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the mask pass could not hold the range's forms");
    }
    status = RoboTextArabicJoiningResolve(view, range, forms, capacity, &count, diag);
    if (status == RoboOk) {
        for (size_t index = 0u; index < count; index++) {
            /* Zero for `None`: a scalar with no form matches no feature, which is the whole of the rule. */
            masks[index] = RoboTextArabicFormMask(forms[index]);
        }
        *outCount = count;
    }
    RoboFree(forms);
    return status;
}

/* ------------------------------------------------------------------ */
/* Clusters                                                            */
/* ------------------------------------------------------------------ */

/**
 * A joining sequence, one cluster.
 *
 * Two scalars belong to the same cluster when the second is a transparent mark (it belongs to what it follows)
 * or when the first letter can give a connection and the second can accept one — which is the *same* question
 * the form resolver asks, asked once here for boundaries rather than for shapes. A non-joining scalar starts a
 * new cluster, which is how `ZWNJ` does its job: it is non-joining, so it ends one sequence and the next
 * letter begins another, and a fallback or a line break may not cut either of them in half.
 */
Result RoboTextArabicClusters(RoboTextView view,
                              RoboTextRange range,
                              RoboTextComplexCluster *clusters,
                              size_t capacity,
                              size_t *outCount,
                              RoboTextDiag *diag)
{
    RoboTextCodePoint *scalars = NULL;
    size_t count = 0u;
    size_t produced = 0u;
    Result status;

    if (!outCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a cluster count output is required");
    }
    *outCount = 0u;
    if (RoboTextRangeLength(range) == 0u) {
        return RoboOk;
    }
    if (!clusters && capacity != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a capacity without an array is not a request");
    }
    status = RoboTextComplexDecodeRange(view, range, NULL, 0u, &count, diag);
    if (status != RoboOk) {
        return status;
    }
    scalars = (RoboTextCodePoint *)RoboCalloc(count, sizeof(*scalars));
    if (!scalars) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the cluster pass could not decode the range");
    }
    status = RoboTextComplexDecodeRange(view, range, scalars, count, &count, diag);

    for (size_t index = 0u; status == RoboOk && index < count; index++) {
        UnicodeJoiningType type = UnicodeJoiningTypeOf((Uchar32)scalars[index]);
        int continues = 0;

        if (produced != 0u) {
            RoboTextCodePoint previous = 0u;
            int hasPrevious = ArabicNeighbour(scalars, index + 1u, index, 0, &previous);

            if (type == RoboUJtTransparent) {
                /* A mark belongs to the cluster it follows, whatever that cluster is. */
                continues = 1;
            } else if (hasPrevious && UnicodeJoiningTypeJoinsLeft(type)) {
                continues = UnicodeJoiningTypeOf((Uchar32)previous) == RoboUJtJoinCausing ||
                            UnicodeJoiningTypeJoinsRight(UnicodeJoiningTypeOf((Uchar32)previous));
            }
            if (continues && clusters[produced - 1u].kind == RoboTextComplexClusterControl) {
                /* A control is never joined to: it is the boundary itself. */
                continues = 0;
            }
        }

        if (continues) {
            clusters[produced - 1u].source.end = range.start + index + 1u;
            if (type != RoboUJtTransparent) {
                /* Two connecting letters: the boundary between them is now unsafe, which is §25 and §28. */
                clusters[produced - 1u].unsafeToBreak = 1;
            }
            continue;
        }
        if (produced < capacity) {
            RoboTextComplexCluster *cluster = &clusters[produced];

            memset(cluster, 0, sizeof(*cluster));
            cluster->source = RoboTextRangeMake(range.start + index, range.start + index + 1u);
            cluster->strategy = RoboTextComplexStrategyArabic;
            if (UnicodeIsDefaultIgnorable((Uchar32)scalars[index])) {
                cluster->kind = RoboTextComplexClusterControl;
            } else {
                cluster->kind = UnicodeJoiningTypeJoins(type) ? RoboTextComplexClusterJoining
                                                             : RoboTextComplexClusterSimple;
            }
            cluster->baseIndex = 0u;
        }
        produced++;
    }

    RoboFree(scalars);
    if (status != RoboOk) {
        return status;
    }
    *outCount = produced;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The stages                                                          */
/* ------------------------------------------------------------------ */

/** A stage feature with a form's mask. */
static RoboTextComplexStageFeature ArabicFeature(const char *tag, uint32_t mask)
{
    RoboTextComplexStageFeature feature;

    memset(&feature, 0, sizeof(feature));
    for (size_t index = 0u; index < 4u && tag[index] != '\0'; index++) {
        feature.tag[index] = tag[index];
    }
    feature.mask = mask;
    return feature;
}

Result RoboTextArabicStages(const struct RoboTextComplexPlan *plan,
                            RoboTextComplexStage *stages,
                            size_t capacity,
                            size_t *outCount,
                            RoboTextDiag *diag)
{
    (void)plan;
    if (!stages || !outCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a stage array and a count are required");
    }
    *outCount = 0u;
    if (capacity < 3u) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboNotSupported,
                               "the Arabic strategy needs three stages");
    }

    /*
     * Stage 1 — composition and locale forms.
     *
     * First, because `ccmp` is what turns a base and its mark into the composed glyph a font wants to shape,
     * and every later stage must see that result: `locl` is beside it because both answer "what is this
     * character *here*", before any question about connection is asked.
     */
    memset(&stages[0], 0, sizeof(stages[0]));
    stages[0].name = "arabic.common";
    stages[0].features[0] = ArabicFeature("ccmp", RoboTextComplexMaskAll);
    stages[0].features[1] = ArabicFeature("locl", RoboTextComplexMaskAll);
    stages[0].featureCount = 2u;
    stages[0].reorderAfter = RoboTextComplexReorderNone;

    /*
     * Stage 2 — the contextual forms, **masked**.
     *
     * One stage rather than four passes, because the four features are alternatives *at a position* and not
     * stages of a pipeline: the mask says which positions each applies at, and the engine's positional test is
     * what keeps `init` off a medial letter. `fin2`/`fin3`/`med2` are the Syriac-valued alternates; a font that
     * lists them gets them, and a font that does not is unaffected, which is cheaper than a script check.
     */
    memset(&stages[1], 0, sizeof(stages[1]));
    stages[1].name = "arabic.forms";
    stages[1].features[0] = ArabicFeature("isol", RoboTextArabicMaskIsolated);
    stages[1].features[1] = ArabicFeature("init", RoboTextArabicMaskInitial);
    stages[1].features[2] = ArabicFeature("medi", RoboTextArabicMaskMedial);
    stages[1].features[3] = ArabicFeature("fina", RoboTextArabicMaskFinal);
    stages[1].features[4] = ArabicFeature("fin2", RoboTextArabicMaskFinal);
    stages[1].features[5] = ArabicFeature("fin3", RoboTextArabicMaskFinal);
    stages[1].features[6] = ArabicFeature("med2", RoboTextArabicMaskMedial);
    stages[1].featureCount = 7u;
    stages[1].reorderAfter = RoboTextComplexReorderNone;

    /*
     * Stage 3 — ligatures and contextual alternates, after the forms.
     *
     * After, because a `lam` + `alef` ligature is a ligature of two *shaped* letters: the font's `rlig` input
     * glyphs are the final/initial forms, not the nominal ones, and running it first would ask for a ligature
     * of glyphs that do not exist yet. `calt` rides with them for the same reason — a contextual alternate is
     * chosen from what the text *is*, which by now includes its forms.
     */
    memset(&stages[2], 0, sizeof(stages[2]));
    stages[2].name = "arabic.ligatures";
    stages[2].features[0] = ArabicFeature("rlig", RoboTextComplexMaskAll);
    stages[2].features[1] = ArabicFeature("calt", RoboTextComplexMaskAll);
    stages[2].features[2] = ArabicFeature("liga", RoboTextComplexMaskAll);
    stages[2].features[3] = ArabicFeature("clig", RoboTextComplexMaskAll);
    stages[2].features[4] = ArabicFeature("dlig", RoboTextComplexMaskAll);
    stages[2].featureCount = 5u;
    stages[2].reorderAfter = RoboTextComplexReorderNone;

    *outCount = 3u;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

Result RoboTextArabicJoiningWrite(RoboTextView view,
                                  RoboTextRange range,
                                  const RoboTextArabicForm *forms,
                                  size_t count,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength)
{
    size_t length = 0u;

    (void)view;
    (void)range;
    if (outLength) {
        *outLength = 0u;
    }
    if (!buffer || capacity == 0u || !forms) {
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboInvalidArgument,
                               "a joining dump needs a buffer and a form array");
    }
    buffer[0] = '\0';
    length = RoboTextWriterText(buffer, capacity, length, "forms ");
    length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)count);
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    for (size_t index = 0u; index < count; index++) {
        length = RoboTextWriterText(buffer, capacity, length, "|\n  #");
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)index);
        length = RoboTextWriterText(buffer, capacity, length, " form=");
        length = RoboTextWriterText(buffer, capacity, length, RoboTextArabicFormName(forms[index]));
        length = RoboTextWriterText(buffer, capacity, length, " mask=");
        length = RoboTextWriterUnsigned(buffer, capacity, length,
                                        RoboTextArabicFormMask(forms[index]));
        if (length == RoboTextWriterOverflow) {
            return RoboNotSupported;
        }
    }
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    if (outLength) {
        *outLength = length;
    }
    return RoboOk;
}
