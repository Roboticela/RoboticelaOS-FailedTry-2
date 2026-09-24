/*
 * Complex-script shaping in RoboText (Phase 12.19) — the strategy, the plan, the clusters.
 *
 * ## The one rule
 *
 * **A complex script's behaviour is a *plan*, not a branch.** Scripts whose shaping depends on context —
 * Arabic's joining, the Indic scripts' syllable structure and reordering — need work that Latin does not:
 * per-position forms, an order of feature *stages* the font's lookup list alone cannot express, and glyphs
 * moved relative to one another. All three are described here as **data** (a strategy, a list of stages with
 * masks, and two reorder steps) rather than as a chain of `if (script == …)` tests, because a second script
 * must be a new *row*, not a new *path*, and because a renderer or a font must never be asked to know which
 * script it is shaping (12.19 §38, §39).
 *
 * The division of labour is the phase's §26 and it is deliberately narrow:
 *
 *   the **plan**     says which features run, in which stage, at which positions, and whether the buffer is
 *                    reordered between stages. `RoboTextComplex.c` owns the strategy table; the two script
 *                    modules own their own stages (`RoboTextArabic.h`, `RoboTextIndic.h`).
 *   **GSUB**         substitutes. Nothing here writes a glyph id, and no substitution is implemented outside
 *                    the engine 12.10 wrote.
 *   **GPOS**         positions. Marks, cursive attachment and kerning are the engine 12.11 built.
 *   **bidi**         resolves direction and order. Nothing here reverses the text, computes a level or
 *                    reorders a *visual* run (12.19 §15).
 *   **clusters**     describe which scalars shape as one unit and where splitting them would be unsafe.
 *
 * ## Where the scalars come from
 *
 * The strategies read the same `RoboTextView` everything else does, decoded through
 * `RoboTextUnicodeDecodeAll` — the module's one decoder (12.5). No strategy walks UTF-8, and none keeps a
 * property table: joining types come from `RoboUnicode/RoboUnicodeJoining.h`, categories from
 * `RoboUnicodeProps.h`, and the cluster vocabulary is this module's own.
 *
 * ## What this build does and does not claim
 *
 * Named rather than implied, because a plan that silently degraded would render wrong text with correct
 * counts: Arabic joining and the Arabic feature stages are implemented end to end; Devanagari's clusters,
 * conjuncts, reph and pre-base matra reordering are implemented; and **the remaining Indic scripts, the
 * Southeast Asian group, Arabic's joining groups (Syriac/N'Ko/Mongolian special cases) and canonically
 * reordered input are not** — `RoboTextComplexPlanForScript` answers the strategy they share, and
 * `RoboTextCapabilities` reports what is real (`complexScripts`, `complexScriptDiagnostics`, …).
 * `Documentation/Runtime/Text/ComplexScripts.md` is the phase's authority and states each of those limits
 * in the same words.
 */
#ifndef ROBO_TEXT_COMPLEX_H
#define ROBO_TEXT_COMPLEX_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextGsub.h"
#include "RoboText/RoboTextOtLayout.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The strategy                                                        */
/* ------------------------------------------------------------------ */

/**
 * What kind of complex shaping a script needs — the coarsest fact, and the only one a caller reads.
 *
 * A *strategy*, not a script: the phase's §2 asks that script identity stay in Unicode analysis (12.6's
 * script runs) and that shaping behaviour live here, so this enumerator names **families of behaviour** and
 * the script → family mapping is one table (`RoboTextComplexPlanForScript`). Two scripts that shape alike
 * share a strategy, which is what makes "add Kannada" a data change rather than a new code path.
 */
typedef enum RoboTextComplexStrategy {
    /** No context-dependent work: `cmap` → GSUB → GPOS, exactly as Phases 12.10–12.13 left it. */
    RoboTextComplexStrategyNone = 0,
    /**
     * Joining: a letter's form depends on the letters beside it, which the font expresses as the
     * `isol`/`fina`/`medi`/`init` features applied **per position** rather than globally (Arabic, Syriac,
     * N'Ko, Mandaic, Mongolian — of which this build implements Arabic, see the header's last section).
     */
    RoboTextComplexStrategyArabic = 1,
    /**
     * Syllable structure and reordering: consonants, viramas, matras and marks group into a **syllable**
     * that shapes as one unit, the base is found inside it, some elements are reordered to shaping order
     * before GSUB and back to visual order after it (Devanagari and the other Indic scripts).
     */
    RoboTextComplexStrategyIndic = 2,
    /**
     * Marks and clusters without reordering or joining: the consonant/vowel/mark structure matters for
     * clusters and for per-syllable features, but nothing moves (Thai, Khmer, Myanmar).
     *
     * This build **does not implement** a shaper for it: a script that resolves to this strategy gets no
     * stages, which is the honest answer while the architecture is in place and the behaviour is not. The
     * strategy exists so the gap is a *named* value rather than a script that silently looks like Latin.
     */
    RoboTextComplexStrategySoutheastAsian = 3
} RoboTextComplexStrategy;

/** `"none"`, `"arabic"`, `"indic"`, `"southeast-asian"`. Never NULL. */
const char *RoboTextComplexStrategyName(RoboTextComplexStrategy strategy);

/** The strategy @p script shapes with. Total: every script answers, and `None` is the common case. */
RoboTextComplexStrategy RoboTextComplexStrategyForScript(RoboTextScript script);

/**
 * One script's shaping plan: its strategy, its OpenType script tag and the properties of its script that a
 * reorder step needs.
 *
 * A row of the module's table, borrowed, and **never NULL** — a script with no row answers the `None` row
 * rather than nothing, so a caller never has to ask twice. The flags are the script's own facts (whether it
 * has a reph, whether it writes half forms, …) and they are what the phase's §17 asks to be *data*: a
 * canonical `IndicScriptPlan` with no glyph id in it and no rule that only one script's tests would reveal.
 */
typedef struct RoboTextComplexScriptPlan {
    /** The script this row describes. */
    RoboTextScript script;
    /** Its strategy. */
    RoboTextComplexStrategy strategy;
    /** The four-byte OpenType script tag (`RoboTextOtScriptTag`), or 0 when the script has none. */
    uint32_t scriptTag;
    /** Non-zero when the script's shaping needs its clusters resolved before any feature runs. */
    int requiresClusterAnalysis;
    /** Non-zero when a letter's form depends on its neighbours (the Arabic strategy). */
    int requiresJoining;
    /** Non-zero when the shaping buffer is reordered — before GSUB, after it, or both. */
    int requiresReordering;
    /** Non-zero when the script writes a **reph**: a syllable-initial `ra` + virama that moves after the base. */
    int hasReph;
    /** Non-zero when the script writes half forms (a consonant + virama's reduced form). */
    int hasHalfForms;
    /** Non-zero when the script has pre-base vowel signs, which are reordered before the base. */
    int hasPreBaseForms;
    /** Non-zero when the script has post-base vowel signs. */
    int hasPostBaseForms;
    /** Non-zero when the script has below-base forms (a consonant + virama drawn under the base). */
    int hasBelowBaseForms;
    /** Non-zero when a nukta (`nukt`) or an akhand ligature (`akhn`) can apply to it. */
    int hasNukta;
    /** Non-zero when the script writes a virama (a vowel-killer that joins consonants). */
    int hasVirama;
} RoboTextComplexScriptPlan;

/** The plan for @p script. Never NULL; a script with no row answers the `None` row. */
const RoboTextComplexScriptPlan *RoboTextComplexPlanForScript(RoboTextScript script);

/* ------------------------------------------------------------------ */
/* The per-run plan: stages and masks                                  */
/* ------------------------------------------------------------------ */

/** Feature tags one stage may name. The Indic stages are the widest real ones. */
#define RoboTextComplexMaxStageFeatures 20u

/** Stages one plan may hold. Two are used today (Arabic's forms, Indic's two halves). */
#define RoboTextComplexMaxStages 6u

/** No mask: the feature applies at every position, which is what every stage of a Latin run is. */
#define RoboTextComplexMaskAll 0u

/**
 * One feature inside one stage, and **where it applies**.
 *
 * `mask` is 0 (everywhere) or a bit the strategy's own mask word uses: Arabic puts a different bit on each
 * contextual form, Devanagari puts one on the reph, one on the half-form positions and so on. The engine
 * applies the feature at a position only when `mask == 0` or the position's own mask word ANDs with it —
 * which is how "the font asks for `init` here and `medi` there" becomes a fact about *positions* rather
 * than about a second substitution pass.
 */
typedef struct RoboTextComplexStageFeature {
    /** The OpenType feature tag, four bytes and a terminator. */
    char tag[RoboTextFeatureTagBytes];
    /** 0, or the strategy's own bit for the positions this feature applies at. */
    uint32_t mask;
} RoboTextComplexStageFeature;

/** One stage: the features it turns on, and what happens to the buffer when it is done. */
typedef struct RoboTextComplexStage {
    /** `"arabic.forms"`, `"indic.substitute"` — the name a diagnostic prints. Never NULL. */
    const char *name;
    /** The features this stage resolves and applies, in the order the strategy lists them. */
    RoboTextComplexStageFeature features[RoboTextComplexMaxStageFeatures];
    size_t featureCount;
    /**
     * Which reorder step runs **before** this stage, and `RoboTextComplexReorderNone` for none.
     *
     * The Indic strategy puts its **initial** reorder here, on its first stage, and the reason is a property
     * of the *buffer* rather than of the script: before any substitution has run the buffer holds exactly one
     * glyph per scalar, so a move can be stated in scalar indices. After `ccmp` that is no longer true, and a
     * reorder expressed in scalars would move the wrong glyphs.
     */
    int reorderBefore;
    /**
     * Which reorder step runs **after** this stage, and `RoboTextComplexReorderNone` for none.
     *
     * After, not before, because the order is the point: Devanagari reorders its reph *after* the substitution
     * stage (until `rphf` has run there is no reph glyph to move) and before the presentation stage, whose
     * features shape glyphs that are already in visual order.
     */
    int reorderAfter;
} RoboTextComplexStage;

/** The reorder steps a `RoboTextComplexStage::reorderAfter` may name. */
typedef enum RoboTextComplexReorder {
    /** Nothing moves after this stage. */
    RoboTextComplexReorderNone = 0,
    /** The strategy's **initial** reorder: shaping order, before the substitution stages. */
    RoboTextComplexReorderInitial = 1,
    /** The strategy's **final** reorder: visual order, after every substitution stage. */
    RoboTextComplexReorderFinal = 2
} RoboTextComplexReorder;

/**
 * A run's complex-shaping plan: the stages, in order, with the caller's own settings already honoured.
 *
 * A **value**, and small (a few hundred bytes), for the same reason `RoboTextFeatureSet` is one: a run
 * carries one, and a plan that owned a pointer would need a lifetime rule. It is resolved once per run, from
 * the script and the caller's features, and never carries a font.
 */
typedef struct RoboTextComplexPlan {
    /** The script this plan was resolved for, and (through it) the row it came from. */
    RoboTextScript script;
    RoboTextComplexStrategy strategy;
    /** The script's own row, borrowed from the module's table. Never NULL. */
    const RoboTextComplexScriptPlan *scriptPlan;
    /** The stages, in application order. Empty for `None`. */
    RoboTextComplexStage stages[RoboTextComplexMaxStages];
    size_t stageCount;
    /** The stages this plan would have had with no caller settings at all, so a dump can say what was turned off. */
    size_t stageCountUnfiltered;
} RoboTextComplexPlan;

/** Clear @p plan to "no strategy, no stages". Safe on NULL. */
void RoboTextComplexPlanInit(RoboTextComplexPlan *plan);

/**
 * Resolve @p script's plan with @p features as the caller's settings.
 *
 * The stages come from the strategy's own module (`RoboTextArabicStages`, `RoboTextIndicStages`), and the
 * caller's settings are applied **as an override**: a tag the caller turned *off* is removed from its stage,
 * and a tag the caller did not name keeps the strategy's default. That is the same precedence
 * `RoboTextLigaturePolicyApplyIfUnset` gives a caller for a ligature, one layer down, and it is what makes
 * `font-feature-settings: "init" 0` mean something.
 *
 * @param features may be NULL ("the caller named nothing").
 * @return RoboOk; RoboInvalidArgument for a NULL @p out.
 */
Result RoboTextComplexPlanResolve(RoboTextScript script,
                                  const RoboTextFeatureSet *features,
                                  RoboTextComplexPlan *out,
                                  RoboTextDiag *diag);

/** Non-zero when @p plan has any stage at all: the question "does this text need a strategy". */
int RoboTextComplexPlanHasStages(const RoboTextComplexPlan *plan);

/** The stage @p index, borrowed, or NULL when out of range. */
const RoboTextComplexStage *RoboTextComplexPlanStage(const RoboTextComplexPlan *plan, size_t index);

/** The strategy's own script tag for @p plan (`"arab"`, `"deva"`), or 0. */
uint32_t RoboTextComplexPlanScriptTag(const RoboTextComplexPlan *plan);

/**
 * The feature set one stage resolves with, as a `RoboTextFeatureSet`.
 *
 * The bridge between a strategy's stage and the shared plan resolver: every stage feature becomes an * explicit `On` (or, for an alternate feature, `On` with the caller's value), so calling
 * `RoboTextOtPlanResolve` with that set and **no script defaults** applies exactly this stage's features and
 * nothing else. Handing the *script's* defaults in instead would let a feature the stage does
 * not own run in every stage, which is how an Indic conjunct comes to be half-formed twice.
 */
Result RoboTextComplexStageFeatureSet(const RoboTextComplexStage *stage,
                                      RoboTextFeatureSet *out,
                                      RoboTextDiag *diag);

/**
 * Load the stage's mask bits into @p plan so the shared engine honours them.
 *
 * Walks the resolved `RoboTextOtPlan`'s features and ORs each one's stage mask into its lookups'
 * `lookupMasks`, which is what makes the engine's positional test possible. A tag the stage names that the
 * font's LangSys does not list is simply absent from the resolved plan, so a font that ships no `medi` runs
 * the other five and no error is raised (12.19 §24).
 *
 * @return RoboOk (including when nothing matched); RoboInvalidArgument for a NULL argument.
 */
Result RoboTextComplexStageApplyMasks(const RoboTextComplexPlan *plan,
                                      const RoboTextComplexStage *stage,
                                      RoboTextOtPlan *otPlan);

/* ------------------------------------------------------------------ */
/* Clusters                                                            */
/* ------------------------------------------------------------------ */

/** What one cluster is, in the terms a line breaker and a caret need. */
typedef enum RoboTextComplexClusterKind {
    /** One scalar that shapes alone: a Latin letter, a space, a digit. */
    RoboTextComplexClusterSimple = 0,
    /** A consonant syllable: a base with its viramas, matras and marks (Indic). */
    RoboTextComplexClusterSyllable = 1,
    /** A joining sequence of Arabic letters: one connected stretch of the text. */
    RoboTextComplexClusterJoining = 2,
    /** A mark sequence with no base of its own (a stray combining mark). */
    RoboTextComplexClusterMarks = 3,
    /** A scalar that shapes nothing: a bidi control, a default-ignorable. */
    RoboTextComplexClusterControl = 4
} RoboTextComplexClusterKind;

/**
 * One shaping cluster: the smallest stretch of source that must be shaped (and broken) as a unit.
 *
 * `source` is a range in the text's **scalar** index space, in logical order, and it is the source of truth
 * for selection and for a later line breaker: a cluster is never rewritten to reflect glyph order, because
 * the text's order is not the shaper's to change (12.19 §7).
 */
typedef struct RoboTextComplexCluster {
    /** The scalars this cluster covers: half-open, never empty. */
    RoboTextRange source;
    /** Which of the four shapes it is. */
    RoboTextComplexClusterKind kind;
    /** The cluster's strategy, so a dump can be read without the run. */
    RoboTextComplexStrategy strategy;
    /**
     * The index of the cluster's **base** among its own scalars (0 for a simple cluster).
     *
     * Relative to `source.start`, so a cluster carries no absolute answer a caller could use with the wrong
     * text: the base of Devanagari `क` `्` `ष` is the `ष`, and the base of a simple cluster is its one scalar.
     */
    size_t baseIndex;
    /** Non-zero when the cluster carries a reph (a syllable-initial `ra` + virama). */
    int hasReph;
    /** Non-zero when the cluster has a pre-base element (a matra that is written before its base). */
    int hasPreBase;
    /** Non-zero when the cluster has a post-base element. */
    int hasPostBase;
    /** Non-zero when the cluster has a below-base element. */
    int hasBelowBase;
    /** Non-zero when a virama joins the cluster's consonants. */
    int hasVirama;
    /** Non-zero when a nukta appears in the cluster. */
    int hasNukta;
    /**
     * Non-zero when a split **between this cluster and the next** is unsafe.
     *
     * The forward half of the boundary rule, and the one that is a property of a cluster rather than of its
     * neighbour: a half-formed consonant whose virama attaches to what follows, a joining letter whose
     * connection a line break would cut, a stray mark cluster with no base of its own. A split *inside* a
     * cluster is unsafe by construction (see @ref RoboTextComplexIsSafeBoundary) and needs no flag.
     */
    int unsafeToBreak;
} RoboTextComplexCluster;

/**
 * The clusters of @p range, in logical order.
 *
 * The strategy decides what a cluster is: Arabic groups a joining sequence (letters that connect, with their
 * marks) and Devanagari groups a syllable. Both boundaries are the same question asked of different rules,
 * and both are answered here rather than by a caller.
 *
 * @param clusters the caller's array; may be NULL when @p capacity is 0, in which case only @p outCount is
 *                 written (the "how many would this be" question).
 * @param outCount receives the count. A count greater than @p capacity means @p clusters holds its prefix.
 * @return RoboOk; RoboInvalidArgument for a NULL @p outCount or a range outside @p view.
 */
Result RoboTextComplexClustersResolve(RoboTextView view,
                                      RoboTextRange range,
                                      RoboTextComplexCluster *clusters,
                                      size_t capacity,
                                      size_t *outCount,
                                      RoboTextDiag *diag);

/** `"syllable"`, `"joining"`, … — the kind's name, for a diagnostic. Never NULL. */
const char *RoboTextComplexClusterKindName(RoboTextComplexClusterKind kind);

/**
 * Non-zero when a boundary **at** @p at (a scalar index) is a safe place to split the text.
 *
 * The phase's §28: a line breaker, a caret and an incremental shaper all need to know where not to cut, and
 * the answer is a property of the clusters rather than of any one of them. @p at is a boundary exactly when
 * it is a cluster's own start — every index inside a cluster is refused — **and** the cluster before it did
 * not claim the two must stay together (`unsafeToBreak`), which is how "a virama attaches forward" and "a
 * joining letter connects onwards" are stated once instead of at each consumer.
 *
 * This is groundwork, not a line breaker: it answers a question and implements nothing (12.19 §28).
 *
 * @param clusters as returned by @ref RoboTextComplexClustersResolve, in logical order.
 * @return 1 when the split is safe, 0 when it is not (including at an index the clusters do not start).
 */
int RoboTextComplexIsSafeBoundary(const RoboTextComplexCluster *clusters,
                                  size_t count,
                                  RoboTextScalarIndex at);

/**
 * Run @p plan's reorder step @p step over @p buffer — the shaping-order surgery a complex script needs.
 *
 * Called **between** GSUB stages by the glyph generator, never by a caller that only wants glyphs: what moves
 * is the shaping buffer (ids with their source ranges), never the text, and never the finished run (12.19
 * §7). The step is the plan's own (`RoboTextComplexStage::reorderAfter`), so which one runs *where* is the
 * strategy's decision and this door is the dispatcher for it.
 *
 * @param step a @ref RoboTextComplexReorder value; `None` is a no-op rather than an error, because a plan a
 *             caller assembled by hand may legitimately ask for nothing.
 * @param view the text the buffer was built from, borrowed and only read.
 * @param range the scalar range the buffer covers, in @p view's index space.
 * @return RoboOk; RoboInvalidArgument for a NULL plan or buffer; RoboOutOfMemory when the buffer could not
 *         grow (it is left holding what it had).
 */
Result RoboTextComplexReorderApply(const RoboTextComplexPlan *plan,
                              int step,
                              RoboTextShapingBuffer *buffer,
                              RoboTextView view,
                              RoboTextRange range,
                              RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/** One line per stage with its features and masks (`"arabic.forms  isol@1 fina@2 …  reorder=final"`). */
Result RoboTextComplexPlanWrite(const RoboTextComplexPlan *plan,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength);

/** One line per cluster: range, kind, base and the flags that are set. Complete-or-absent. */
Result RoboTextComplexClustersWrite(const RoboTextComplexCluster *clusters,
                                    size_t count,
                                    char *buffer,
                                    size_t capacity,
                                    size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_COMPLEX_H */
