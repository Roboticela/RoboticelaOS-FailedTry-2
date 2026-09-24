/*
 * Complex-script strategies and plans (Phase 12.19) — see `RoboTextComplex.h` for the contract.
 *
 * Four pieces, in the order a shaping call meets them:
 *
 *   **the script table**   one row per script whose shaping is not the Latin path, with its strategy and the
 *                          script's own facts (a reph, half forms, a virama, …). It is *data*: adding a
 *                          script is adding a row, and a script with no row answers the `None` row so no
 *                          caller ever has to ask twice.
 *   **the plan**           the strategy's stages, filtered by the caller's own feature settings. The stages
 *                          themselves come from the strategy's module (`RoboTextArabic.c`,
 *                          `RoboTextIndic.c`), because a stage is script *behaviour* and this file is the
 *                          dispatcher.
 *   **the clusters**       the shaping units of a range: a joining sequence for Arabic, a syllable for
 *                          Devanagari, one scalar and its marks for everything else. The strategy decides
 *                          what a cluster is; the arithmetic of "where may the line break" is here.
 *   **the writers**        complete-or-absent, like every dump in this module.
 *
 * The two rules the file exists to keep: a script is never branched on outside the table below, and nothing
 * here writes a glyph id, reverses text or computes a bidi level.
 */
#include "RoboText/RoboTextComplex.h"

#include "RoboMem/RoboMem.h"

#include "RoboText/RoboTextArabic.h"
#include "RoboText/RoboTextIndic.h"
#include "RoboText/RoboTextUnicode.h"
#include "RoboTextInternal.h"

#include "RoboText/RoboTextOtLayout.h"
#include "RoboUnicode/RoboUnicodeJoining.h"
#include "RoboUnicode/RoboUnicodeProps.h"

#include <string.h>

/** The four-byte OpenType tag `RoboTextOtTagOf` would compute, as a constant expression. */
#define RoboComplexTag(a, b, c, d) \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | \
     ((uint32_t)(uint8_t)(c) << 8) | (uint32_t)(uint8_t)(d))

/* ------------------------------------------------------------------ */
/* The script table                                                    */
/* ------------------------------------------------------------------ */

/**
 * The strategies, one row per script.
 *
 * Ordered by strategy and then by script, so the Arabic family's rows and the Indic family's rows are each
 * one block a reader can scan. The tag is the registry's, written as a constant expression rather than
 * computed by `RoboTextOtScriptTag` because a table initialized at load time cannot call it — and
 * `text.complex.script_tag` pins every row's tag against that function, so the two cannot drift.
 */
static const RoboTextComplexScriptPlan RoboComplexScriptPlans[] = {
    /* --- The Arabic strategy: joining, forms per position --- */
    { RoboUScriptArabic, RoboTextComplexStrategyArabic, RoboComplexTag('a', 'r', 'a', 'b'),
      1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { RoboUScriptSyriac, RoboTextComplexStrategyArabic, RoboComplexTag('s', 'y', 'r', 'c'),
      1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { RoboUScriptNko, RoboTextComplexStrategyArabic, RoboComplexTag('n', 'k', 'o', ' '),
      1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { RoboUScriptMandaic, RoboTextComplexStrategyArabic, RoboComplexTag('m', 'a', 'n', 'd'),
      1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { RoboUScriptMongolian, RoboTextComplexStrategyArabic, RoboComplexTag('m', 'o', 'n', 'g'),
      1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { RoboUScriptManichaean, RoboTextComplexStrategyArabic, RoboComplexTag('m', 'a', 'n', 'i'),
      1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { RoboUScriptPsalterPahlavi, RoboTextComplexStrategyArabic, RoboComplexTag('p', 'h', 'l', 'p'),
      1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { RoboUScriptHanifiRohingya, RoboTextComplexStrategyArabic, RoboComplexTag('r', 'o', 'h', 'g'),
      1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { RoboUScriptSogdian, RoboTextComplexStrategyArabic, RoboComplexTag('s', 'o', 'g', 'd'),
      1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },
    { RoboUScriptOldUyghur, RoboTextComplexStrategyArabic, RoboComplexTag('o', 'u', 'g', 'r'),
      1, 1, 0, 0, 0, 0, 0, 0, 0, 0 },

    /* --- The Indic strategy: syllables, a base, reordering --- */
    { RoboUScriptDevanagari, RoboTextComplexStrategyIndic, RoboComplexTag('d', 'e', 'v', 'a'),
      1, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { RoboUScriptBengali, RoboTextComplexStrategyIndic, RoboComplexTag('b', 'e', 'n', 'g'),
      1, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { RoboUScriptGurmukhi, RoboTextComplexStrategyIndic, RoboComplexTag('g', 'u', 'r', 'u'),
      1, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { RoboUScriptGujarati, RoboTextComplexStrategyIndic, RoboComplexTag('g', 'u', 'j', 'r'),
      1, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { RoboUScriptOriya, RoboTextComplexStrategyIndic, RoboComplexTag('o', 'r', 'y', 'a'),
      1, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { RoboUScriptTamil, RoboTextComplexStrategyIndic, RoboComplexTag('t', 'a', 'm', 'l'),
      1, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { RoboUScriptTelugu, RoboTextComplexStrategyIndic, RoboComplexTag('t', 'e', 'l', 'u'),
      1, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { RoboUScriptKannada, RoboTextComplexStrategyIndic, RoboComplexTag('k', 'n', 'd', 'a'),
      1, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { RoboUScriptMalayalam, RoboTextComplexStrategyIndic, RoboComplexTag('m', 'l', 'y', 'm'),
      1, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { RoboUScriptSinhala, RoboTextComplexStrategyIndic, RoboComplexTag('s', 'i', 'n', 'h'),
      1, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
    { RoboUScriptKhmer, RoboTextComplexStrategySoutheastAsian, RoboComplexTag('k', 'h', 'm', 'r'),
      1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { RoboUScriptMyanmar, RoboTextComplexStrategySoutheastAsian, RoboComplexTag('m', 'y', 'm', 'r'),
      1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },

    /* --- The Southeast Asian strategy: clusters, no reordering --- */
    { RoboUScriptThai, RoboTextComplexStrategySoutheastAsian, RoboComplexTag('t', 'h', 'a', 'i'),
      1, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { RoboUScriptLao, RoboTextComplexStrategySoutheastAsian, RoboComplexTag('l', 'a', 'o', ' '),
      1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

/** The row everything else answers: no strategy, no stages, no work. */
static const RoboTextComplexScriptPlan RoboComplexNoPlan = {
    RoboUScriptCommon, RoboTextComplexStrategyNone, 0u,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const char *RoboTextComplexStrategyName(RoboTextComplexStrategy strategy)
{
    switch (strategy) {
    case RoboTextComplexStrategyArabic:
        return "arabic";
    case RoboTextComplexStrategyIndic:
        return "indic";
    case RoboTextComplexStrategySoutheastAsian:
        return "southeast-asian";
    case RoboTextComplexStrategyNone:
    default:
        return "none";
    }
}

const RoboTextComplexScriptPlan *RoboTextComplexPlanForScript(RoboTextScript script)
{
    for (size_t index = 0u; index < sizeof(RoboComplexScriptPlans) / sizeof(RoboComplexScriptPlans[0]);
         index++) {
        if (RoboComplexScriptPlans[index].script == script) {
            return &RoboComplexScriptPlans[index];
        }
    }
    return &RoboComplexNoPlan;
}

RoboTextComplexStrategy RoboTextComplexStrategyForScript(RoboTextScript script)
{
    return RoboTextComplexPlanForScript(script)->strategy;
}

/* ------------------------------------------------------------------ */
/* The plan                                                            */
/* ------------------------------------------------------------------ */

void RoboTextComplexPlanInit(RoboTextComplexPlan *plan)
{
    if (!plan) {
        return;
    }
    memset(plan, 0, sizeof(*plan));
    plan->script = RoboUScriptCommon;
    plan->strategy = RoboTextComplexStrategyNone;
    plan->scriptPlan = &RoboComplexNoPlan;
}

/** Non-zero when @p stage still names a feature after the caller's settings were applied. */
static int StageHasFeatures(const RoboTextComplexStage *stage)
{
    return stage->featureCount != 0u;
}

/**
 * Apply @p features to @p stage — the caller's settings as an **override** over the strategy's default.
 *
 * A tag the caller turned `Off` is *removed* rather than marked off: the stage becomes a set of features to
 * resolve, and a feature that must not run should not be in it. A tag the caller turned `On` is kept (the
 * strategy already asked for it), and a tag the caller did not name keeps the strategy's answer. That is the
 * precedence 12.12 established for ligatures — a caller's explicit answer wins, silence does not — applied
 * one layer down where the answer is a stage rather than a tag.
 */
static void StageApplyCallerSettings(const RoboTextFeatureSet *features, RoboTextComplexStage *stage)
{
    size_t out = 0u;

    if (!features) {
        return;
    }
    for (size_t index = 0u; index < stage->featureCount; index++) {
        const RoboTextFeatureSetting *setting =
            RoboTextFeatureSetFind(features, stage->features[index].tag);

        if (setting && setting->state == RoboTextFeatureOff) {
            continue;
        }
        stage->features[out] = stage->features[index];
        if (setting && setting->state == RoboTextFeatureOn) {
            /* The caller may name a value the strategy did not; the caller's is the answer. */
            stage->features[out].mask = stage->features[index].mask;
        }
        out++;
    }
    stage->featureCount = out;
}

Result RoboTextComplexPlanResolve(RoboTextScript script,
                                  const RoboTextFeatureSet *features,
                                  RoboTextComplexPlan *out,
                                  RoboTextDiag *diag)
{
    size_t count = 0u;
    size_t kept = 0u;

    if (!out) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a plan output is required");
    }
    RoboTextComplexPlanInit(out);
    out->script = script;
    out->scriptPlan = RoboTextComplexPlanForScript(script);
    out->strategy = out->scriptPlan->strategy;

    switch (out->strategy) {
    case RoboTextComplexStrategyArabic:
        if (RoboTextArabicStages(out, out->stages, RoboTextComplexMaxStages, &count, diag) != RoboOk) {
            return RoboNotSupported;
        }
        break;
    case RoboTextComplexStrategyIndic:
        if (RoboTextIndicStages(out, out->stages, RoboTextComplexMaxStages, &count, diag) != RoboOk) {
            return RoboNotSupported;
        }
        break;
    case RoboTextComplexStrategySoutheastAsian:
    case RoboTextComplexStrategyNone:
    default:
        /*
         * Nothing to stage. The Southeast Asian strategy is named and *empty*: the architecture is here, the
         * behaviour is not, and an empty plan is the honest representation of that (12.19 §23 asks for
         * exactly this rather than for a script that silently shapes like Latin without saying so).
         */
        count = 0u;
        break;
    }

    out->stageCountUnfiltered = count;
    for (size_t index = 0u; index < count; index++) {
        StageApplyCallerSettings(features, &out->stages[index]);
        if (!StageHasFeatures(&out->stages[index])) {
            continue;
        }
        if (kept != index) {
            out->stages[kept] = out->stages[index];
        }
        kept++;
    }
    out->stageCount = kept;
    return RoboOk;
}

int RoboTextComplexPlanHasStages(const RoboTextComplexPlan *plan)
{
    return plan && plan->stageCount != 0u;
}

const RoboTextComplexStage *RoboTextComplexPlanStage(const RoboTextComplexPlan *plan, size_t index)
{
    if (!plan || index >= plan->stageCount) {
        return NULL;
    }
    return &plan->stages[index];
}

uint32_t RoboTextComplexPlanScriptTag(const RoboTextComplexPlan *plan)
{
    if (!plan || !plan->scriptPlan) {
        return 0u;
    }
    return plan->scriptPlan->scriptTag;
}

Result RoboTextComplexStageFeatureSet(const RoboTextComplexStage *stage,
                                      RoboTextFeatureSet *out,
                                      RoboTextDiag *diag)
{
    if (!stage || !out) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a stage and a feature set are required");
    }
    RoboTextFeatureSetInit(out);
    for (size_t index = 0u; index < stage->featureCount; index++) {
        Result status = RoboTextFeatureSetSet(out, stage->features[index].tag, RoboTextFeatureOn, 0u);

        /*
         * A stage that names more features than a set holds is a *bug in the strategy*, not a font or caller
         * error: the sets are sized for the widest real stage, and the report says which one overflowed
         * rather than silently dropping the tail (which would shape text with half a plan).
         */
        if (status != RoboOk) {
            return RoboTextDiagMsg(diag, RoboTextDomainRun, status,
                                   "a complex stage names more features than a set can hold");
        }
    }
    return RoboOk;
}

Result RoboTextComplexStageApplyMasks(const RoboTextComplexPlan *plan,
                                      const RoboTextComplexStage *stage,
                                      RoboTextOtPlan *otPlan)
{
    if (!stage || !otPlan) {
        return RoboInvalidArgument;
    }
    (void)plan;
    for (size_t feature = 0u; feature < otPlan->featureCount; feature++) {
        const RoboTextOtFeature *enabled = &otPlan->features[feature];
        uint32_t mask = RoboTextComplexMaskAll;

        for (size_t index = 0u; index < stage->featureCount; index++) {
            if (strncmp(stage->features[index].tag, enabled->tag, RoboTextFeatureTagBytes) == 0) {
                mask = stage->features[index].mask;
                break;
            }
        }
        for (uint16_t lookup = 0u; lookup < enabled->lookupIndexCount; lookup++) {
            uint16_t lookupIndex = enabled->lookupIndices[lookup];

            if (lookupIndex >= RoboFontOtMaxLookups) {
                continue;
            }
            /*
             * Indexed by the **Lookup List** index, which is the index the engine reads it with: the plan
             * needs no search here and the engine needs none at apply time. `lookupOrder` holds the same
             * numbers in execution order, so the two are the same set said two ways — and neither is a
             * position in the other, because a plan that enables lookups 0, 4, 5, 6 and 7 would otherwise
             * write a mask for lookup 4 into slot 1 and the engine would read a zero for lookup 4.
             *
             * OR, not assign: two features of one stage may name the same lookup (a font sharing a lookup
             * between `medi` and `init`, which real fonts do), and the lookup applies where *either*
             * feature's positions ask for it.
             */
            otPlan->lookupMasks[lookupIndex] |= (uint16_t)mask;
        }
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Clusters                                                            */
/* ------------------------------------------------------------------ */

const char *RoboTextComplexClusterKindName(RoboTextComplexClusterKind kind)
{
    switch (kind) {
    case RoboTextComplexClusterSyllable:
        return "syllable";
    case RoboTextComplexClusterJoining:
        return "joining";
    case RoboTextComplexClusterMarks:
        return "marks";
    case RoboTextComplexClusterControl:
        return "control";
    case RoboTextComplexClusterSimple:
    default:
        return "simple";
    }
}

/**
 * Decode @p range's scalars into @p out.
 *
 * The module's one decoder, asked for a *range*: `RoboTextUnicodeDecodeAll` takes a view, so the range's
 * scalars are decoded by walking the view from the range's first scalar's byte offset — the same idiom the
 * glyph generator uses (`RoboTextViewScalarToByte` + `UnicodeUtf8Decode`), which is what keeps "the shaper and
 * the generator read the same characters" true by construction.
 *
 * @param out caller's array; may be NULL when @p capacity is 0.
 */
Result RoboTextComplexDecodeRange(RoboTextView view,
                                  RoboTextRange range,
                                  RoboTextCodePoint *out,
                                  size_t capacity,
                                  size_t *outCount,
                                  RoboTextDiag *diag)
{
    RoboTextByteIndex startByte = 0u;
    size_t offset;
    size_t count = RoboTextRangeLength(range);

    if (!outCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a scalar count output is required");
    }
    *outCount = 0u;
    if (RoboTextRangeIsInverted(range)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a range that ends before it starts has no scalars");
    }
    if (count == 0u) {
        return RoboOk;
    }
    if (out && capacity < count) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboNotSupported,
                               "the caller's array cannot hold the range's scalars");
    }
    if (RoboTextViewScalarToByte(view, range.start, &startByte, diag) != RoboOk) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "the range does not lie in the text");
    }
    offset = (size_t)startByte;
    for (size_t index = 0u; index < count; index++) {
        Uchar32 scalar = 0u;
        size_t next = offset;

        if (UnicodeUtf8Decode(view.bytes, view.length, offset, RoboUnicodeReplace, &scalar, &next) !=
                RoboUnicodeOk ||
            next <= offset) {
            return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboParseError,
                                   "the range's text could not be decoded");
        }
        offset = next;
        if (out) {
            out[index] = (RoboTextCodePoint)scalar;
        }
    }
    *outCount = count;
    return RoboOk;
}

int RoboTextComplexScalarIsMark(RoboTextCodePoint cp)
{
    UnicodeGeneralCategory category = UnicodeCategory((Uchar32)cp);

    return category == RoboUGcMn || category == RoboUGcMe || category == RoboUGcMc;
}

/** The script of the one scalar at index @p at, or `Common` when it cannot be decoded. */
static RoboTextScript ComplexScriptAt(RoboTextView view, RoboTextScalarIndex at, RoboTextDiag *diag)
{
    RoboTextCodePoint scalar = 0u;
    size_t count = 0u;

    if (RoboTextComplexDecodeRange(view, RoboTextRangeMake(at, at + 1u), &scalar, 1u, &count, diag) !=
            RoboOk ||
        count != 1u) {
        return RoboTextScriptCommon;
    }
    return (RoboTextScript)RoboTextUnicodeScript(scalar);
}

/**
 * The **no-strategy** cluster pass: one cluster per scalar, with a mark continuing the one before it.
 *
 * This is the pass Latin, Greek, Cyrillic and every other simple script gets, and it is deliberately the same
 * rule 12.14's glyph clusters use ("a combining mark continues the cluster of the glyph before it"), so a
 * caller reading complex clusters and a caller reading glyph clusters see one answer for `e` + U+0301. A
 * default-ignorable scalar is its own cluster and marked a **control**, which is what a line breaker must not
 * count as ink.
 */
static Result ComplexClustersSimple(RoboTextView view,
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

    status = RoboTextComplexDecodeRange(view, range, NULL, 0u, &count, diag);
    if (status != RoboOk || count == 0u) {
        *outCount = 0u;
        return status;
    }
    scalars = (RoboTextCodePoint *)RoboCalloc(count, sizeof(*scalars));
    if (!scalars) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboOutOfMemory,
                               "the cluster pass could not decode the range");
    }
    status = RoboTextComplexDecodeRange(view, range, scalars, count, &count, diag);

    for (size_t index = 0u; status == RoboOk && index < count; index++) {
        int joinsPrevious = RoboTextComplexScalarIsMark(scalars[index]) && produced != 0u &&
                            clusters[produced - 1u].kind != RoboTextComplexClusterControl;

        if (joinsPrevious) {
            clusters[produced - 1u].source.end = range.start + index + 1u;
            continue;
        }
        if (produced < capacity) {
            RoboTextComplexCluster *cluster = &clusters[produced];

            memset(cluster, 0, sizeof(*cluster));
            cluster->source = RoboTextRangeMake(range.start + index, range.start + index + 1u);
            cluster->strategy = RoboTextComplexStrategyNone;
            cluster->kind = UnicodeIsDefaultIgnorable((Uchar32)scalars[index])
                                ? RoboTextComplexClusterControl
                                : RoboTextComplexClusterSimple;
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

Result RoboTextComplexClustersResolve(RoboTextView view,
                                      RoboTextRange range,
                                      RoboTextComplexCluster *clusters,
                                      size_t capacity,
                                      size_t *outCount,
                                      RoboTextDiag *diag)
{
    RoboTextComplexStrategy strategy;

    if (!outCount) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a cluster count output is required");
    }
    *outCount = 0u;
    if (RoboTextRangeIsInverted(range)) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a range that ends before it starts has no clusters");
    }
    if (RoboTextRangeLength(range) == 0u) {
        return RoboOk;
    }
    if (!clusters && capacity != 0u) {
        return RoboTextDiagMsg(diag, RoboTextDomainRun, RoboInvalidArgument,
                               "a capacity without an array is not a request");
    }

    /* The strategy is the text's own: the first scalar of the range decides, exactly as a run's does. */
    strategy = RoboTextComplexStrategyForScript(ComplexScriptAt(view, range.start, diag));
    switch (strategy) {
    case RoboTextComplexStrategyArabic:
        return RoboTextArabicClusters(view, range, clusters, capacity, outCount, diag);
    case RoboTextComplexStrategyIndic:
        return RoboTextIndicClusters(view, range, clusters, capacity, outCount, diag);
    case RoboTextComplexStrategySoutheastAsian:
    case RoboTextComplexStrategyNone:
    default:
        return ComplexClustersSimple(view, range, clusters, capacity, outCount, diag);
    }
}

int RoboTextComplexIsSafeBoundary(const RoboTextComplexCluster *clusters,
                                  size_t count,
                                  RoboTextScalarIndex at)
{
    if (!clusters || count == 0u) {
        return 0;
    }
    for (size_t index = 0u; index < count; index++) {
        if (clusters[index].source.start != at) {
            continue;
        }
        /*
         * A cluster's own start is a boundary — unless the cluster before it says the two may not be
         * separated: a half-formed consonant whose virama attaches forward, a joining letter whose
         * connection would be cut, a stray mark with no base.
         */
        if (index == 0u) {
            return 1;
        }
        return clusters[index - 1u].unsafeToBreak ? 0 : 1;
    }
    /* Inside a cluster, or past the end: not a boundary at all. */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Reordering                                                          */
/* ------------------------------------------------------------------ */

Result RoboTextComplexReorderApply(const RoboTextComplexPlan *plan,
                              int step,
                              RoboTextShapingBuffer *buffer,
                              RoboTextView view,
                              RoboTextRange range,
                              RoboTextDiag *diag)
{
    if (!plan || !buffer) {
        return RoboTextDiagMsg(diag, RoboTextDomainGlyph, RoboInvalidArgument,
                               "a plan and a shaping buffer are required");
    }
    if (step == RoboTextComplexReorderNone) {
        return RoboOk;
    }
    /*
     * The dispatch is by **strategy**, not by script: a reorder step is behaviour, and the module that owns
     * the behaviour owns the rule. Only the Indic strategy moves anything today; a future strategy with a
     * reorder step adds its own arm here rather than a case at the call site.
     */
    if (plan->strategy == RoboTextComplexStrategyIndic) {
        return step == RoboTextComplexReorderInitial
                   ? RoboTextIndicReorderInitial(buffer, view, range, diag)
                   : RoboTextIndicReorderFinal(buffer, view, range, diag);
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

Result RoboTextComplexPlanWrite(const RoboTextComplexPlan *plan,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength)
{
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!plan) {
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboInvalidArgument,
                               "a plan is required");
    }
    if (!buffer || capacity == 0u) {
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboInvalidArgument,
                               "a plan dump needs a buffer");
    }
    buffer[0] = '\0';
    length = RoboTextWriterText(buffer, capacity, length, "script ");
    length = RoboTextWriterText(buffer, capacity, length, RoboTextScriptName(plan->script));
    length = RoboTextWriterText(buffer, capacity, length, "  strategy ");
    length = RoboTextWriterText(buffer, capacity, length, RoboTextComplexStrategyName(plan->strategy));
    length = RoboTextWriterText(buffer, capacity, length, "  stages ");
    length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)plan->stageCount);
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    for (size_t index = 0u; index < plan->stageCount; index++) {
        const RoboTextComplexStage *stage = &plan->stages[index];

        length = RoboTextWriterText(buffer, capacity, length, "|\n  ");
        length = RoboTextWriterText(buffer, capacity, length, stage->name);
        for (size_t feature = 0u; feature < stage->featureCount; feature++) {
            length = RoboTextWriterText(buffer, capacity, length, " ");
            length = RoboTextWriterText(buffer, capacity, length, stage->features[feature].tag);
            if (stage->features[feature].mask != RoboTextComplexMaskAll) {
                length = RoboTextWriterText(buffer, capacity, length, "@");
                length = RoboTextWriterUnsigned(buffer, capacity, length,
                                                stage->features[feature].mask);
            }
        }
        length = RoboTextWriterText(buffer, capacity, length, "  reorder=");
        length = RoboTextWriterText(buffer, capacity, length,
                                    stage->reorderAfter == RoboTextComplexReorderInitial
                                        ? "initial"
                                        : stage->reorderAfter == RoboTextComplexReorderFinal ? "final"
                                                                                            : "none");
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

Result RoboTextComplexClustersWrite(const RoboTextComplexCluster *clusters,
                                    size_t count,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength)
{
    size_t length = 0u;

    if (outLength) {
        *outLength = 0u;
    }
    if (!buffer || capacity == 0u) {
        return RoboTextDiagMsg(NULL, RoboTextDomainRun, RoboInvalidArgument,
                               "a cluster dump needs a buffer");
    }
    buffer[0] = '\0';
    length = RoboTextWriterText(buffer, capacity, length, "clusters ");
    length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)count);
    if (length == RoboTextWriterOverflow) {
        return RoboNotSupported;
    }
    for (size_t index = 0u; index < count; index++) {
        const RoboTextComplexCluster *cluster = &clusters[index];

        length = RoboTextWriterText(buffer, capacity, length, "|\n  [");
        length = RoboTextWriterUnsigned(buffer, capacity, length, cluster->source.start);
        length = RoboTextWriterText(buffer, capacity, length, ",");
        length = RoboTextWriterUnsigned(buffer, capacity, length, cluster->source.end);
        length = RoboTextWriterText(buffer, capacity, length, ") ");
        length = RoboTextWriterText(buffer, capacity, length,
                                    RoboTextComplexClusterKindName(cluster->kind));
        length = RoboTextWriterText(buffer, capacity, length, " base=");
        length = RoboTextWriterUnsigned(buffer, capacity, length, (uint32_t)cluster->baseIndex);
        if (cluster->hasReph) {
            length = RoboTextWriterText(buffer, capacity, length, " reph");
        }
        if (cluster->hasPreBase) {
            length = RoboTextWriterText(buffer, capacity, length, " pre");
        }
        if (cluster->hasPostBase) {
            length = RoboTextWriterText(buffer, capacity, length, " post");
        }
        if (cluster->hasBelowBase) {
            length = RoboTextWriterText(buffer, capacity, length, " below");
        }
        if (cluster->hasVirama) {
            length = RoboTextWriterText(buffer, capacity, length, " virama");
        }
        if (cluster->hasNukta) {
            length = RoboTextWriterText(buffer, capacity, length, " nukta");
        }
        if (cluster->unsafeToBreak) {
            length = RoboTextWriterText(buffer, capacity, length, " unsafe");
        }
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
