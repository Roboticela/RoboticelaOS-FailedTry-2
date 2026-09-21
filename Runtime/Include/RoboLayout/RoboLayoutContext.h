/*
 * RoboLayout context (Phase 10.1).
 *
 * One layout pass: the tree it ran on, the constraints it ran with, what it saw,
 * and where a later goal hangs the temporary data it needs.
 *
 * A context exists as an object rather than as arguments to a call because a pass
 * *accumulates*: constraints are refined as the walk descends, a formatting
 * context needs scratch space that lives for the whole pass, and the counters are
 * read after it ends. Passing all of that down a call stack would be passing a
 * struct through every function anyway, and the struct would have to be allocated
 * somewhere -- so it is allocated once, here, and owned by the caller.
 *
 * It owns **nothing borrowed**: the tree, the constraints (a record by value,
 * copied in), and the caller's temporary data are all references. Destroying a
 * context frees the context and nothing else, which is what makes it safe to
 * destroy in any order relative to the tree it ran on.
 *
 * What a pass does in 10.1, stated plainly because the honesty of this phase
 * depends on it:
 *
 *   walks the tree, in creation order -- which is document order, because the
 *   builder adds parents before children and children left to right;
 *
 *   marks every box UNRESOLVED, so that a consumer reading a result is told
 *   nobody has computed it rather than handed a zeroed rectangle;
 *
 *   counts what it saw into `LayoutStats`, including how many boxes have
 *   no computed style at all;
 *
 *   and computes no coordinate. `LayoutStats.boxesComputed` is 0 after a
 *   10.1 pass and `LayoutGeometryIsImplemented()` answers 0, so a caller
 *   cannot mistake "the pass ran" for "the boxes were laid out".
 *
 * Phase 10.2 adds a second job to the same walk: **classify every box** into a
 * formatting context (RoboLayoutFormattingContext.h), storing the result on the
 * tree. It is the same loop because it is the same traversal -- the
 * classification needs each box's children and its parent's classification, both
 * of which the walk already has in hand, and a second walk of the same tree would
 * be the second pass this module refuses to have.
 *
 * The order is load-bearing: boxes are visited in *creation* order, which is
 * document order, which is parents before children. So when a box is reached, its
 * parent has already been classified, and "which context do I participate in?" is
 * a read rather than a search up the tree.
 *
 * Phase 10.3 adds a third job, and it is a different *kind* of job rather than
 * another counter: **block layout** (RoboLayoutBlock.h), which computes real
 * coordinates. It runs *after* the walk rather than inside it, because it is a
 * recursion over the tree rather than a scan of the arena -- a box's position
 * depends on its parent's content origin, which the parent knows and the arena
 * does not -- and because it needs the classification to be complete before it can
 * ask which boxes it owns.
 *
 * It also runs *conditionally*, and the condition is the one honest thing this
 * document has to say about it: **a pass lays out either direction only when it
 * has been given a viewport.** The root box's containing block is the initial
 * containing block, and RoboLayout owns no window to ask (README.md) -- so without
 * a viewport there is no width for anything to fill, no line to lay out, and every
 * result stays UNRESOLVED. That is a documented precondition and not a silent zero:
 * `LayoutStats.block` and `.inlineFlow` report all zeros and the caller can
 * see that `boxesComputed` is 0 with `objects` above it.
 *
 * Phase 10.4 adds a fourth job to that same third job rather than a fifth stage:
 * where a box's contents are an **inline formatting context**, its children are
 * placed along a line (RoboLayoutInline.h) *while* the block recursion descends,
 * and the extent the lines reached is the extent the block pass gives the box. The
 * two directions are one stage because an inline context's height is part of the
 * block stack; splitting them would mean re-stacking every ancestor afterwards.
 */

#ifndef ROBO_LAYOUT_CONTEXT_H
#define ROBO_LAYOUT_CONTEXT_H

#include "RoboBase/RoboError.h"

#include "RoboLayout/RoboLayoutBlock.h"
#include "RoboLayout/RoboLayoutConstraints.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutFormattingContext.h"
#include "RoboLayout/RoboLayoutGeometry.h"
#include "RoboLayout/RoboLayoutInline.h"
#include "RoboLayout/RoboLayoutInvalidation.h"
#include "RoboLayout/RoboLayoutText.h"
#include "RoboLayout/RoboLayoutTree.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What the last pass did. */
typedef enum LayoutPassOutcome {
    /** No pass has run in this context yet. */
    RoboLayoutPassNone = 0,
    /** The tree was walked; every result was left UNRESOLVED. */
    RoboLayoutPassWalked = 1,
    /** The pass could not complete (a NULL or unusable tree). */
    RoboLayoutPassFailed = 2,
    /**
     * `LayoutContextRunIncremental` found nothing to do (10.14) and skipped
     * the walk entirely.
     *
     * A fourth outcome rather than a flag on WALKED, because a caller that cannot tell
     * "nothing was stale" from "everything was recomputed" is a caller that cannot use
     * the answer: the first means its previous results are still current, and the second
     * means they were replaced. It is also the outcome that makes requirement 13
     * observable rather than merely claimed -- a repaint-only change produces this one.
     */
    RoboLayoutPassClean = 3
} LayoutPassOutcome;

/** Number of defined pass outcomes. */
#define RoboLayoutPassOutcomeCount 4u

/** Non-zero if @p outcome is a defined outcome. */
int LayoutPassOutcomeIsKnown(LayoutPassOutcome outcome);

/** Human-readable outcome name; never NULL; "unknown" when undefined. */
const char *LayoutPassOutcomeName(LayoutPassOutcome outcome);

/** What one pass saw. A record by value, owned by nobody. */
typedef struct LayoutStats {
    /** Boxes in the tree. */
    size_t objects;
    /** Boxes the pass reached (every box, in 10.1). */
    size_t visited;
    /** Boxes whose geometry is final. Always 0 in 10.1. */
    size_t boxesComputed;
    /** Boxes the pass deliberately left without geometry. */
    size_t boxesUnresolved;
    /** Boxes carrying STYLE_MISSING: a fact about the build, not a failure. */
    size_t withoutStyle;
    /** Deepest box seen. */
    uint32_t depthMax;
    /** Passes this context has completed, including this one. */
    size_t passes;

    /*
     * What the classification produced (Phase 10.2). Counters rather than a
     * single total because the interesting fact is the *mix*: a document whose
     * every box establishes a context and one that establishes a single root
     * context have the same box count and completely different layout shapes, and
     * a suite that could not tell them apart would not be testing the
     * classification.
     */
    /** Formatting contexts established, of every kind. */
    size_t contexts;
    /** Contexts of each kind, indexed by LayoutFcKind. Entry NONE is
     *  always 0 -- a context has a kind or it is not a context. */
    size_t contextsByKind[RoboLayoutFcKindCount];
    /** Boxes that established a context of their own. */
    size_t boxesEstablishing;
    /** Boxes that established none: transparent containers and inline boxes,
     *  whose contents an ancestor's context formats. */
    size_t boxesParticipatingOnly;
    /** Deepest context nesting seen: 0 for a tree whose only context is ROOT. */
    uint32_t contextDepthMax;

    /**
     * What block layout did (Phase 10.3).
     *
     * All zeros when the pass had no viewport to lay out into -- which is why
     * `boxesComputed` and `block.blocksPlaced` are both reported rather than one
     * being derived from the other: the first is "how many boxes have geometry",
     * the second is "how many this pass placed", and a reader who saw only the
     * first would have no way to tell a viewport-free pass from an empty document.
     */
    LayoutBlockStats block;

    /**
     * What inline layout did (Phase 10.4).
     *
     * `boxesComputed` above is `block.blocksPlaced` plus
     * `inlineFlow.participantsPlaced`, and both records are reported rather than
     * the sum alone because the sum cannot answer "why has this box no geometry?"
     * -- which is the question a reader of `boxesUnresolved` is asking, and which
     * has five different answers across the two records (an inline box in a block
     * context, a block box in an inline one, a box inside an unplaced box, and the
     * two ways a box can be placed).
     *
     * All zeros when the pass had no viewport, for the same reason `block` is: an
     * inline formatting context has no width to lay a line into without one.
     *
     * The field is named `inlineFlow` rather than `inline` because `inline` is a
     * keyword in the language this module is written in -- which is worth writing
     * down once, so that the one asymmetry between the two names in this struct
     * does not read as an accident.
     */
    LayoutInlineStats inlineFlow;

    /**
     * What text layout did (Phase 10.5).
     *
     * The record that answers a question the other two cannot, and the reason it is a
     * third record rather than two more fields on `inlineFlow`: the placement counts
     * say where the participants went, and this says **how much of the text anything
     * could measure**. A document laid out with no metrics source and one laid out
     * with a real font produce identical box counts, identical line counts and -- for
     * the unmeasured case -- identical geometry, so a reader who saw only the first
     * two records could not tell a correct build from one that never measured a
     * glyph. `measured` and `unmeasured` are the pair that distinguishes them
     * (RoboLayoutText.h).
     *
     * All zeros when the pass had no viewport, for the same reason the other two are:
     * text is placed along a line, and there is no line without a width.
     */
    LayoutTextStats text;

    /**
     * What was stale when this pass began, and whether it recomputed (Phase 10.14).
     *
     * A fourth record and not three more fields on one of the others, for the reason
     * `text` is a record: it answers a question the other three cannot. The placement
     * counts say where the boxes went; this says **how much of that had to be worked
     * out again**, which is the only number that can tell a pass that recomputed a
     * whole document from one that recomputed a document nobody had changed -- two runs
     * with identical placement counts and completely different costs.
     */
    LayoutInvalidationStats invalidation;
} LayoutStats;

/** An opaque, caller-owned pass. */
typedef struct LayoutContext LayoutContext;

/** Create an empty context: no constraints, no viewport, no tree, no pass.
 *  @return RoboOk; RoboInvalidArgument for NULL output; RoboOutOfMemory. */
Result LayoutContextCreate(LayoutContext **outContext,
                                         LayoutDiag *diag);

/** Destroy @p context. The tree, the styles and the caller's user data are not
 *  touched. Safe on NULL. */
void LayoutContextDestroy(LayoutContext *context);

/** Return @p context to its just-created state: constraints cleared, viewport
 *  forgotten, counters zeroed, outcome NONE, last tree forgotten. The pass count
 *  goes to 0 with it -- "this context has run nothing" is the state it restores,
 *  not "this context has run something else". Safe on NULL. */
void LayoutContextReset(LayoutContext *context);

/**
 * Record the viewport -- the initial containing block -- in CSS pixels.
 *
 * It is recorded as the *available* space and as nothing else. Whether a box
 * fills it is a formatting-context decision (10.2), so a context that also
 * treated it as a maximum would be answering a question that belongs to a later
 * goal. The window and display layers already know this number (RoboDisplay /
 * RoboWindow); RoboLayout takes it as an argument and hardcodes nothing.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL context or a negative size.
 */
Result LayoutContextSetViewport(LayoutContext *context,
                                               LayoutSize size,
                                               LayoutDiag *diag);

/** The recorded viewport.
 *  @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound
 *          when no viewport has been recorded -- the caller is told rather than
 *          handed an invented screen size. */
Result LayoutContextViewport(const LayoutContext *context,
                                           LayoutSize *out);

/** Replace the constraints wholesale (a record, copied in). Setting the viewport
 *  and setting the constraints are independent: a pass may be run with bounds
 *  that come from somewhere other than a viewport. */
Result LayoutContextSetConstraints(LayoutContext *context,
                                                  const LayoutConstraints *constraints,
                                                  LayoutDiag *diag);

/** The constraints, by value. Answers the cleared record when none were set, so
 *  a caller can tell "nothing is known" from a failure. */
Result LayoutContextConstraints(const LayoutContext *context,
                                              LayoutConstraints *out);

/**
 * Record who measures text (Phase 10.5), or clear it with NULL.
 *
 * The record is copied in by value, and the **userdata it names is borrowed for as
 * long as it stays installed** -- the same lifetime rule the style source states, and
 * for the same reason: RoboLayout cannot extend the life of something it does not
 * own, so the caller keeps whatever the callback needs alive until it clears the
 * metrics or destroys the context.
 *
 * It lives on the context rather than on the tree because it is an *input to a pass*
 * and not a property of a document: a tree is derived state that outlives many
 * passes, and a measurer is the same shape of thing the viewport is -- recorded here,
 * handed to the pass, never stored where it would outlive its owner by accident.
 *
 * NULL is a legitimate setting and means "nothing can measure": every text run
 * measures to zero and is counted as *unmeasured*, so the document lays out exactly
 * as 10.4 left it. That is the documented degradation rather than an error, and
 * `LayoutTextStats` is where a caller sees that it happened.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p context.
 */
Result LayoutContextSetTextMetrics(LayoutContext *context,
                                                   const LayoutTextMetrics *metrics);

/**
 * The installed metrics source, by value.
 *
 * @return RoboOk and the record; RoboInvalidArgument for NULL arguments. A context
 *         with no source installed answers the cleared record -- "measures nothing" --
 *         rather than failing, because that is a state a caller can legitimately be
 *         in and
 *         `LayoutTextMetricsIsUsable` already answers the question they meant
 *         to ask.
 */
Result LayoutContextTextMetrics(const LayoutContext *context,
                                               LayoutTextMetrics *out);

/**
 * Install the caller's temporary pass data.
 *
 * The extension point a later goal uses instead of growing this struct: 10.3's
 * block pass, 10.4's inline pass and 10.5's text pass each need their own scratch
 * state, and the alternative -- a field per formatting context -- would make this
 * header depend on every goal that follows it. RoboLayout never dereferences it,
 * so what it points at is entirely the caller's business.
 */
void LayoutContextSetUserData(LayoutContext *context, void *userData);

/** The installed user data, or NULL. */
void *LayoutContextUserData(const LayoutContext *context);

/**
 * Run a pass over @p tree.
 *
 * Walks every box, resets every result to UNRESOLVED, classifies every box, and
 * -- when a viewport has been recorded -- lays both directions out, which is where
 * results become COMPUTED: the block direction first and, wherever a box's contents
 * are an inline formatting context, the inline direction inside it. Every counter
 * is filled either way.
 *
 * What the caller gets therefore depends on what the caller gave it, and the
 * dependency is stated rather than inferred: no viewport means no block layout, so
 * every result comes back UNRESOLVED and `stats.block.blocksPlaced` is 0. That is
 * the same honest answer as 10.1's, now conditional on an input instead of being
 * the build's whole behaviour.
 *
 * A box the pass did not place is left UNRESOLVED rather than given a zeroed
 * rectangle, so a caller can always tell "nobody computed this" from "this is at
 * the origin with no size" -- which is the distinction the result *state* exists
 * for, and one this pass would destroy by writing a provisional geometry.
 *
 * **@p tree is taken mutable, and that is deliberate.** A pass *writes*: it is
 * the one component in the module that does, because a box's result is where a
 * pass puts what it concluded. Taking a `const` tree and casting the qualifier
 * away inside would compile, would read as "this changes nothing", and would be
 * undefined behaviour the first time a caller passed an object that really was
 * const. The structure is not editable -- there is no add, remove or reorder here
 * -- but the results are, and the signature says so.
 *
 * An empty tree is a legitimate pass (a document whose root element is
 * `display: none` builds no boxes) and reports zero objects rather than an error.
 * A tree with boxes but no viewport is equally legitimate: it is classified, and
 * laid out as far as the input allows, which is not at all.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL context or tree. A failure of
 *         the *block* stage -- a box link that names no box, a classification
 *         released under the pass -- fails the whole pass with
 *         RoboLayoutPassFailed and the diagnostic carrying the reason, because a
 *         pass that laid out half a document and reported success would be handing
 *         back a shape nobody asked for.
 */
Result LayoutContextRun(LayoutContext *context,
                                      LayoutTree *tree,
                                      LayoutDiag *diag);

/**
 * Run a pass only when something needs one (Phase 10.14).
 *
 * The one use this build makes of the invalidation model, and it is deliberately one
 * decision rather than a partial recursion -- the brief this phase answers forbids
 * incremental layout heuristics, and a build that guessed a dirty region would be
 * faster and wrong.
 *
 * The pass is skipped, and the outcome is `RoboLayoutPassClean`, exactly when **all
 * four** of these hold:
 *
 *   this context has already completed a pass;
 *   it ran that pass on **this** tree;
 *   no input it was given has changed since -- the viewport or the metrics source, the
 *   two things that are not part of the tree and that every box's geometry depends on;
 *   and no box carries a **layout** bit, which is `LayoutTreeNeedsLayout` and
 *   not `!is_clean`: a document whose only change was a colour is not clean and does
 *   not need geometry.
 *
 * Otherwise it is `LayoutContextRun`, with everything that door does -- including
 * the fresh classification, the recomputation, and clearing the layout bits it
 * satisfied. A caller may therefore use this door everywhere and never see a
 * difference except in the four cases above, which is the property that makes it safe
 * to adopt.
 *
 * The skipped pass still updates the records a caller reads: `passes` advances (a pass
 * that concluded "nothing to do" is a pass), `objects` is the tree's count, `visited`
 * is 0 because nothing was walked, and `invalidation.styleOnly` reports how many boxes
 * are waiting for a repaint. `recomputed` is 0, which is the whole point.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL context or tree. Failures are the
 *         ones `LayoutContextRun` reports, unchanged.
 */
Result LayoutContextRunIncremental(LayoutContext *context,
                                                 LayoutTree *tree,
                                                 LayoutDiag *diag);

/** What the last pass saw, by value. Zeros for a NULL context. */
void LayoutContextStats(const LayoutContext *context,
                               LayoutStats *out);

/** The last pass's outcome; RoboLayoutPassNone for a NULL context. */
LayoutPassOutcome LayoutContextOutcome(const LayoutContext *context);

/** Borrowed tree of the last pass, or NULL when none has run. */
const LayoutTree *LayoutContextTree(const LayoutContext *context);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_CONTEXT_H */
