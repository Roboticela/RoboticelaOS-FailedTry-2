/*
 * RoboLayout context (Phase 10.1) — see RoboLayoutContext.h.
 *
 * A context is a struct, a walk, and a classification. The walk is deliberately
 * the dumbest thing that can be written -- creation order, one memset per box,
 * one classification per box -- for the reason Phase 10.1 exists: a walk that did
 * anything clever here would be a layout pass with no geometry algorithm behind
 * it, which is the one outcome those phases must not produce.
 *
 * The classification (Phase 10.2) rides the same walk because it is the same
 * traversal. It needs three things per box -- the box's own display, its
 * children's displays, and its parent's classification -- and the walk already
 * has all three in hand. A second walk would be a second pass, and the module has
 * exactly one.
 */

#include "RoboLayout/RoboLayoutContext.h"

/* The pass is the one component that *writes* during a layout pass -- it marks
 * every box's result UNRESOLVED -- and the only door that hands out a mutable
 * box is the module-internal slot accessor. Every public door in
 * RoboLayoutObject.h validates the id itself and is deliberately read-only, so
 * the non-const walk a pass needs lives here and nowhere else. */
#include "RoboLayout/RoboLayoutAbsolute.h"
#include "RoboLayout/RoboLayoutPosition.h"

#include "RoboLayoutInternal.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct LayoutContext {
    LayoutConstraints constraints;
    int hasViewport;
    LayoutSize viewport;
    /*
     * Who measures text (10.5). Borrowed, and the flag is what keeps "nobody installed
     * a source" a *different* thing from "a source that measures nothing" -- the same
     * distinction `hasViewport` draws one line up, and for the same reason: a zeroed
     * record is not a statement.
     */
    LayoutTextMetrics textMetrics;
    int hasTextMetrics;
    LayoutStats stats;
    /*
     * Whether an **input** has changed since the last recomputing pass (10.14).
     *
     * The one piece of state the incremental door needs that the tree cannot answer:
     * a recorded viewport is not part of the tree, so a caller that moves the window
     * leaves every box's geometry stale while the invalidation model -- correctly --
     * reports nothing dirty. Rather than compare viewports and metric records (which
     * would mean comparing function pointers and a struct with padding in it), the
     * setters below *say* when they change something, and a recomputing pass clears
     * the flag. A caller that re-records the same viewport therefore pays for one
     * unnecessary pass, which is the conservative direction and the honest one.
     */
    int inputsDirty;
    LayoutPassOutcome outcome;
    /* Borrowed, and const *as read back*: the pass wrote into the boxes, not into
     * the tree, so a caller that only wants to ask "what did I run on?" has no
     * business editing the structure through this handle. */
    const LayoutTree *tree;
    void *userData;
};

/* ------------------------------------------------------------------ */
/* Classification (Phase 10.2)                                        */
/* ------------------------------------------------------------------ */

/**
 * Whether @p object has any element child whose computed display is block-level.
 *
 * The one input the IFC rule needs that is not the box's own display. A text node
 * is not a box (10.1), so a container holding only text answers 0 here -- which is
 * what makes `<p>one</p>` establish an inline context, exactly as it should.
 */
static int LayoutObjectHasBlockLevelChild(LayoutTree *tree,
                                               const LayoutObject *object)
{
    LayoutNodeId child = object->firstChild;

    while (child != RoboLayoutNodeIdNone) {
        const LayoutObject *slot = LayoutObjectSlotConst(tree, child);

        if (!slot) {
            /* Unreachable while the tree is add-only and this walk is the only
             * writer. Answering "no" rather than asserting keeps a future
             * removal bug from becoming a crash *inside a classification*, where
             * the stack trace would say nothing about the removal. */
            break;
        }
        if (CssDisplayIsBlockLevel(slot->display)) {
            return 1;
        }
        child = slot->NextSibling;
    }
    return 0;
}

/**
 * Classify one box: resolve what it participates in, and establish its own
 * context when the rules say it does.
 *
 * @p outKind receives the kind it established, or NONE.
 */
static Result LayoutClassifyObject(LayoutTree *tree,
                                            LayoutObject *object,
                                            LayoutFcKind *outKind,
                                            LayoutDiag *diag)
{
    LayoutFcKind kind;
    LayoutFcId owned = RoboLayoutFcIdNone;
    Result result;

    if (outKind) {
        *outKind = RoboLayoutFcNone;
    }

    if (object->parent == RoboLayoutNodeIdNone) {
        /* The root box establishes the initial formatting context, and
         * *participates* in it, because there is nothing above it to participate
         * in instead. That is what makes "every live box has a context once the
         * tree is classified" an invariant rather than a caveat. */
        result = LayoutTreeContextAdd(tree, RoboLayoutFcRoot, object->id,
                                             RoboLayoutFcIdNone, 0u, &owned, diag);
        if (result != RoboOk) {
            return result;
        }
        object->context = owned;
        object->contextOwned = owned;
        LayoutTreeContextAddParticipant(tree, owned);
        if (outKind) {
            *outKind = RoboLayoutFcRoot;
        }
        return RoboOk;
    }

    {
        const LayoutObject *parent =
            LayoutObjectSlotConst(tree, object->parent);
        LayoutFormattingContext parentContext;

        if (!parent) {
            return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                    RoboError,
                                    "a box's parent link names no box in this tree");
        }

        /*
         * A child participates in whatever its parent establishes -- or, when the
         * parent is transparent, in whatever the parent participates in. That is
         * the whole of the module's notion of transparency, in one expression,
         * and it is why the two ids on a box have to be two ids.
         */
        object->context = (parent->contextOwned != RoboLayoutFcIdNone)
                              ? parent->contextOwned
                              : parent->context;
        if (object->context == RoboLayoutFcIdNone) {
            /* Unreachable, and reported rather than assumed: the root box
             * establishes a context, so every descendant reaches one through the
             * parent chain. If this ever fires, the parent links are wrong and
             * saying so beats producing a box that belongs nowhere. */
            return RoboLayoutDiag(diag, RoboLayoutDomainFormattingContext,
                                    RoboError,
                                    "a box could not reach any formatting context");
        }

        {
            /* Phase 14.0: whether this box is out of flow is a fact about its *position*, read here
             * once rather than inside the classifier, which is a pure function of the facts it
             * is handed (`RoboLayoutFormattingContext.h`). Phases 19.3 and 19.4: whether it is a
             * flex item or a grid item is a fact about the context it just joined, which is why it
             * is read here too -- and the two are read as two facts rather than one "is an item"
             * flag because the *kind* of the context it joined is what the answer is a function of. */
            LayoutPosition declaredPosition = LayoutPositionOf(object->style);
            LayoutFormattingContext participating;
            int isFlexItem = 0;
            int isGridItem = 0;

            if (LayoutTreeContext(tree, object->context, &participating, NULL) ==
                    RoboOk) {
                isFlexItem = participating.kind == RoboLayoutFcFlex;
                isGridItem = participating.kind == RoboLayoutFcGrid;
            }
            kind = LayoutFcEstablishes(
                object->display, LayoutObjectHasBlockLevelChild(tree, object), 0,
                LayoutAbsoluteIsOutOfFlow(&declaredPosition), isFlexItem,
                isGridItem);
        }
        if (kind != RoboLayoutFcNone) {
            /* The new context's parent is the one the *owner* participates in, so
             * contexts nest the way the boxes that establish them do. */
            if (LayoutTreeContext(tree, object->context, &parentContext, diag) !=
                RoboOk) {
                return diag ? diag->code : RoboError;
            }
            result = LayoutTreeContextAdd(tree, kind, object->id, object->context,
                                                  parentContext.depth + 1u, &owned, diag);
            if (result != RoboOk) {
                return result;
            }
            object->contextOwned = owned;
            if (outKind) {
                *outKind = kind;
            }
        }

        LayoutTreeContextAddParticipant(tree, object->context);
    }
    return RoboOk;
}

int LayoutPassOutcomeIsKnown(LayoutPassOutcome outcome)
{
    return outcome >= RoboLayoutPassNone &&
           outcome < (LayoutPassOutcome)RoboLayoutPassOutcomeCount;
}

const char *LayoutPassOutcomeName(LayoutPassOutcome outcome)
{
    switch (outcome) {
    case RoboLayoutPassNone:
        return "none";
    case RoboLayoutPassWalked:
        return "walked";
    case RoboLayoutPassClean:
        return "clean";
    case RoboLayoutPassFailed:
        return "failed";
    default:
        return "unknown";
    }
}

Result LayoutContextCreate(LayoutContext **outContext,
                                         LayoutDiag *diag)
{
    LayoutContext *context;

    if (!outContext) {
        return RoboLayoutDiag(diag, RoboLayoutDomainContext, RoboInvalidArgument,
                                "out_context is NULL");
    }
    *outContext = NULL;
    context = (LayoutContext *)RoboCalloc(1u, sizeof(*context));
    if (!context) {
        return RoboLayoutDiag(diag, RoboLayoutDomainContext, RoboOutOfMemory,
                                "could not allocate the layout context");
    }
    /* calloc already gives the empty state: constraints cleared, no viewport, no
     * outcome, no tree. The explicit init is here so that the definition of
     * "empty" lives in one place rather than in the allocator's behaviour. */
    LayoutConstraintsInit(&context->constraints);
    context->outcome = RoboLayoutPassNone;
    /* Nothing has been recorded, so nothing an input describes can be stale -- but a
     * freshly created context has also run no pass, which the incremental door asks
     * about separately (`outcome`). Leaving this 0 states "no *input* has changed". */
    context->inputsDirty = 0;
    *outContext = context;
    return RoboOk;
}

void LayoutContextDestroy(LayoutContext *context)
{
    /* Everything else in the context is borrowed, so there is nothing to release
     * but the struct itself. */
    RoboFree(context);
}

void LayoutContextReset(LayoutContext *context)
{
    if (!context) {
        return;
    }
    LayoutConstraintsInit(&context->constraints);
    context->hasViewport = 0;
    context->viewport.width = 0;
    context->viewport.height = 0;
    /* The metrics source is dropped by a reset for the same reason the viewport is:
     * both are *inputs to a pass*, and a reset restores "this context has run
     * nothing", which includes "nothing has been installed". The userdata they name
     * is the caller's and is untouched -- see the note below. */
    LayoutTextMetricsInit(&context->textMetrics);
    context->hasTextMetrics = 0;
    context->inputsDirty = 0;
    memset(&context->stats, 0, sizeof(context->stats));
    context->outcome = RoboLayoutPassNone;
    context->tree = NULL;
    /* userData is deliberately left alone: it belongs to the caller, and a reset
     * that dropped it would break a caller that installs it once and resets per
     * pass -- which is the normal way to use this. */
}

Result LayoutContextSetViewport(LayoutContext *context,
                                               LayoutSize size,
                                               LayoutDiag *diag)
{
    if (!context) {
        return RoboLayoutDiag(diag, RoboLayoutDomainContext, RoboInvalidArgument,
                                "context is NULL");
    }
    if (size.width < 0 || size.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainContext, RoboInvalidArgument,
                                "viewport size is negative");
    }
    context->viewport = size;
    context->hasViewport = 1;
    /* A new containing block makes every conclusion beneath the root stale, and the
     * incremental door is told so here rather than left to compare two sizes (10.14). */
    context->inputsDirty = 1;
    /* Recorded as the available space and nothing more -- see the header. */
    return LayoutConstraintsSetAvailable(&context->constraints, size, diag);
}

Result LayoutContextViewport(const LayoutContext *context,
                                           LayoutSize *out)
{
    if (!context || !out) {
        return RoboInvalidArgument;
    }
    if (!context->hasViewport) {
        return RoboNotFound;
    }
    *out = context->viewport;
    return RoboOk;
}

Result LayoutContextSetTextMetrics(LayoutContext *context,
                                                   const LayoutTextMetrics *metrics)
{
    if (!context) {
        return RoboInvalidArgument;
    }
    context->inputsDirty = 1;
    if (!metrics) {
        LayoutTextMetricsInit(&context->textMetrics);
        context->hasTextMetrics = 0;
        return RoboOk;
    }
    /*
     * The record is copied; the *userdata inside it* stays the caller's. That is the
     * whole ownership story, and it is why clearing with NULL is a supported call
     * rather than an afterthought: a caller whose font is about to go away clears the
     * source first, and a context that kept the pointer past that would be measuring
     * through a dangling face.
     */
    context->textMetrics = *metrics;
    context->hasTextMetrics = 1;
    return RoboOk;
}

Result LayoutContextTextMetrics(const LayoutContext *context,
                                               LayoutTextMetrics *out)
{
    if (!context || !out) {
        return RoboInvalidArgument;
    }
    /* A cleared record is the answer for "nothing installed", and it is the record
     * whose own predicate (`LayoutTextMetricsIsUsable`) says so -- so this
     * door never has to report a miss to mean "nothing measures here". */
    if (context->hasTextMetrics) {
        *out = context->textMetrics;
        return RoboOk;
    }
    LayoutTextMetricsInit(out);
    return RoboOk;
}

Result LayoutContextSetConstraints(LayoutContext *context,
                                                  const LayoutConstraints *constraints,
                                                  LayoutDiag *diag)
{
    if (!context || !constraints) {
        return RoboLayoutDiag(diag, RoboLayoutDomainContext, RoboInvalidArgument,
                                "context or constraints is NULL");
    }
    context->constraints = *constraints;
    context->inputsDirty = 1;
    /* A caller that installs constraints has decided what the available space is,
     * so a previously recorded viewport no longer describes them. Keeping the two
     * in step matters because the viewport is read back by later goals as "the
     * initial containing block" and a stale one would be a wrong answer about the
     * root box's parent. */
    if (LayoutConstraintsHasAvailable(constraints)) {
        (void)LayoutConstraintsAvailable(constraints, &context->viewport);
        context->hasViewport = 1;
    } else {
        context->hasViewport = 0;
    }
    return RoboOk;
}

Result LayoutContextConstraints(const LayoutContext *context,
                                              LayoutConstraints *out)
{
    if (!context || !out) {
        return RoboInvalidArgument;
    }
    *out = context->constraints;
    return RoboOk;
}

void LayoutContextSetUserData(LayoutContext *context, void *userData)
{
    if (context) {
        context->userData = userData;
    }
}

void *LayoutContextUserData(const LayoutContext *context)
{
    return context ? context->userData : NULL;
}

Result LayoutContextRun(LayoutContext *context,
                                      LayoutTree *tree,
                                      LayoutDiag *diag)
{
    size_t index;
    size_t count;
    Result outcome;

    if (!context || !tree) {
        if (context) {
            context->outcome = RoboLayoutPassFailed;
        }
        return RoboLayoutDiag(diag, RoboLayoutDomainContext, RoboInvalidArgument,
                                "context or tree is NULL");
    }

    /*
     * Per-pass fields are reset; `passes` is cumulative and is not. A caller
     * reading stats after a second pass wants that pass's numbers, and a caller
     * asking how many passes have run wants the total -- so the count is carried
     * across the reset rather than cleared with everything else. Zeroing the whole
     * record and then incrementing makes `passes` permanently 1, which reads as
     * "this context ran once" for a context that ran five times.
     */
    {
        size_t completed = context->stats.passes;

        memset(&context->stats, 0, sizeof(context->stats));
        context->stats.passes = completed;
    }
    context->stats.objects = LayoutTreeObjectCount(tree);
    /*
     * What was stale when this pass began (10.14), read **before** the classification
     * release below -- that release marks the whole tree, and a snapshot taken after it
     * would report a document nobody had invalidated as a fully dirty one. The four
     * numbers are the phase's evidence: `invalidated` is what a partial pass would have
     * had to redo, `root` is where it would have started, `reason` is why, and
     * `recomputed` is the honest statement of what this build does with them.
     */
    context->stats.invalidation.invalidated = LayoutTreeInvalidationCount(tree);
    context->stats.invalidation.styleOnly = LayoutInvalidationStyleOnlyCount(tree);
    context->stats.invalidation.root = LayoutTreeInvalidationRoot(tree);
    context->stats.invalidation.reason =
        LayoutTreeInvalidationReason(tree, context->stats.invalidation.root);
    context->stats.invalidation.recomputed = 0;
    context->tree = tree;
    context->outcome = RoboLayoutPassWalked;

    /*
     * The previous classification is released before the walk, not after it. A
     * pass *replaces* the contexts rather than refining them -- a participant
     * count is a statement about a walk that is about to be redone -- so keeping
     * the old arena alive while the new one is built would leave two generations
     * of contexts for one tree, which is the one thing a reader could not tell
     * apart from a correct answer.
     */
    LayoutTreeInvalidateContexts(tree);

    count = context->stats.objects;
    for (index = 0u; index < count; index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;
        LayoutFcKind established = RoboLayoutFcNone;
        LayoutObject *object;

        if (LayoutObjectIdAt(tree, index, &id) != RoboOk) {
            /* Unreachable while the tree is add-only and parallel to this walk --
             * which is exactly why it is reported rather than assumed: a future
             * goal that introduces removal has to decide what a pass sees. */
            context->outcome = RoboLayoutPassFailed;
            return RoboLayoutDiag(diag, RoboLayoutDomainContext, RoboError,
                                    "the tree changed under the pass");
        }
        object = LayoutObjectSlot(tree, id);
        if (!object) {
            context->outcome = RoboLayoutPassFailed;
            return RoboLayoutDiag(diag, RoboLayoutDomainContext, RoboError,
                                    "a box vanished under the pass");
        }

        /* The one thing this pass writes: the box exists and nobody has computed
         * its geometry. The memset inside is what makes a repeated pass
         * idempotent rather than cumulative. */
        LayoutResultSetUnresolved(&object->result);

        context->stats.visited++;
        context->stats.boxesUnresolved++;
        if ((object->flags & RoboLayoutObjectFlagStyleMissing) != 0u) {
            context->stats.withoutStyle++;
        }
        if (object->depth > context->stats.depthMax) {
            context->stats.depthMax = object->depth;
        }

        /*
         * The classification, on the same walk and in the same order. Creation
         * order is document order, so a box's parent has already been classified
         * by the time the box is reached -- which is what makes "which context do
         * I participate in?" a read of the parent rather than a search up the
         * tree.
         */
        outcome = LayoutClassifyObject(tree, object, &established, diag);
        if (outcome != RoboOk) {
            context->outcome = RoboLayoutPassFailed;
            return diag ? diag->code : outcome;
        }
        if (established != RoboLayoutFcNone) {
            context->stats.contexts++;
            if (established < (LayoutFcKind)RoboLayoutFcKindCount) {
                context->stats.contextsByKind[established]++;
            }
            context->stats.boxesEstablishing++;
        } else {
            context->stats.boxesParticipatingOnly++;
        }
        if (object->contextOwned != RoboLayoutFcIdNone) {
            LayoutFormattingContext ownedContext;

            if (LayoutTreeContext(tree, object->contextOwned, &ownedContext,
                                        NULL) == RoboOk &&
                ownedContext.depth > context->stats.contextDepthMax) {
                context->stats.contextDepthMax = ownedContext.depth;
            }
        }
    }

    /*
     * Marked only after every box has been classified, so "this tree is
     * classified" is never true of a tree half of whose boxes hold a NONE id. An
     * empty tree is classified too -- it has no boxes and no contexts, which is an
     * answer rather than an absence.
     */
    LayoutTreeMarkClassified(tree);

    /*
     * Block layout (10.3), and the condition on it is the interesting part.
     *
     * The root box's containing block is the initial containing block, which is the
     * viewport -- and RoboLayout owns no window to ask. So the viewport is an
     * *input*: recorded by the caller, and absent when the caller did not record
     * one. Without it there is no width for a block box to fill and nothing to
     * stack into, so the pass stops here with every box UNRESOLVED and a block
     * stats record of zeros.
     *
     * That is deliberately not an error. A caller that only wants the classification
     * -- a suite proving which context a box is in, a diagnostic asking what kind of
     * box an element is -- should not have to invent a viewport to get it, and a
     * build that refused would make the phase that *classifies* depend on the phase
     * that *sizes*. It is also not silent: the result states and
     * `stats.block.blocksPlaced` both say nothing was placed, and the header states
     * the precondition.
     */
    if (context->hasViewport && LayoutTreeRoot(tree) != RoboLayoutNodeIdNone) {
        /*
         * One call, two records: the block direction and, inside it, the inline
         * direction -- because an inline formatting context's height is part of the
         * block stack (RoboLayoutBlock.h). The context owns both records, so it passes
         * both and the pass fills both.
         */
        Result laidOut = LayoutBlockLayout(
            tree, context->viewport,
            context->hasTextMetrics ? &context->textMetrics : NULL,
            &context->stats.block, &context->stats.inlineFlow, &context->stats.text, diag);

        if (laidOut != RoboOk) {
            context->outcome = RoboLayoutPassFailed;
            return laidOut;
        }
        /*
         * Counted here rather than inside the pass, because the fact is about *this*
         * pass's boxes and not about either direction's arithmetic: a box is COMPUTED
         * exactly when one of the two directions placed it, and each record already
         * carries its own count. Adding the two keeps one source of truth for the
         * number instead of a third counter that must agree with both -- and it is
         * what makes `boxesComputed` and the two placement counts impossible to
         * disagree by construction.
         *
         * No box can be in both: a box is placed either by the block pass (block-level,
         * in a block-direction context) or by the inline pass (inline-level, in an
         * inline one), and an atomic inline -- which is *both* a participant of a line
         * and the owner of a block context -- is counted only where it was placed, by
         * the inline pass, while the boxes inside it are counted by the block pass.
         */
        context->stats.boxesComputed = context->stats.block.blocksPlaced +
                                        context->stats.inlineFlow.participantsPlaced;
        if (context->stats.boxesComputed > count) {
            context->stats.boxesComputed = count;
        }
        context->stats.boxesUnresolved = count > context->stats.boxesComputed
                                              ? count - context->stats.boxesComputed
                                              : 0u;
    }
    /*
     * A tree with no root box is not taken to the block pass at all, and the
     * condition above says so rather than letting a RoboNotFound travel back and
     * be reinterpreted: a document whose root element generates no box has no block
     * layout to do, `objects` is already 0, and every counter is already right.
     */

    /*
     * The pass recomputed every conclusion this module reaches, so the layout bits it
     * began with are satisfied and no longer claim anything (10.14). Two conditions,
     * and the second is the one that would be easy to get wrong:
     *
     *   a pass that laid geometry out has recomputed it -- so the natural statement is
     *   `recomputed = 1` whenever the block stage ran;
     *
     *   a pass with **no viewport** classified the tree and computed no coordinate, so
     *   clearing the bits there would leave the next pass believing work had been done
     *   that never was. Requirement 11 forbids exactly that stale-clean state, which is
     *   why the clearing is inside this condition rather than beside `passes`.
     *
     * STYLE is deliberately left alone: a repaint is not something a layout pass
     * performed, so a box holding only STYLE stays dirty *and* stops needing layout --
     * the pair of answers the incremental door below is built on.
     */
    if (context->hasViewport && LayoutTreeRoot(tree) != RoboLayoutNodeIdNone) {
        context->stats.invalidation.recomputed = 1;
        (void)LayoutTreeMarkRecomputed(tree);
        context->inputsDirty = 0;
    }

    context->stats.passes++;
    return RoboOk;
}

Result LayoutContextRunIncremental(LayoutContext *context,
                                                 LayoutTree *tree,
                                                 LayoutDiag *diag)
{
    if (!context || !tree) {
        if (context) {
            context->outcome = RoboLayoutPassFailed;
        }
        return RoboLayoutDiag(diag, RoboLayoutDomainContext, RoboInvalidArgument,
                                "context or tree is NULL");
    }

    /*
     * The decision, and every term of it is a question the model above can answer:
     *
     *   nothing this pass ran on before -- a context that has completed no pass has no
     *   basis for saying anything is up to date;
     *
     *   a different tree -- results are per tree, so a tree this context has not seen
     *   is entirely uncomputed;
     *
     *   an input changed -- the viewport or the metrics, neither of which is part of
     *   the tree and both of which every box's geometry depends on;
     *
     *   a box needs layout -- the model's own answer, and the one that makes
     *   requirement 13 true: a document whose only change was a colour is *not clean*
     *   and *does not need layout*, so the pass is skipped and its `styleOnly` count
     *   says why.
     *
     * Any of the four and the pass runs, because correctness beats the saving; none of
     * them and the tree is skipped, which is the only optimization this phase claims.
     */
    if (context->outcome != RoboLayoutPassNone && context->tree == tree &&
        !context->inputsDirty && !LayoutTreeNeedsLayout(tree)) {
        size_t completed = context->stats.passes;

        memset(&context->stats, 0, sizeof(context->stats));
        context->stats.passes = completed + 1u;
        context->stats.objects = LayoutTreeObjectCount(tree);
        /* `visited` stays 0: a pass that skipped the walk reached nothing, and the
         * number a reader needs in order to tell "skipped an empty tree" from "skipped a
         * full one" is `objects`, one field up. `outcome` says which happened. */
        context->stats.invalidation.styleOnly = LayoutInvalidationStyleOnlyCount(tree);
        context->stats.invalidation.recomputed = 0;
        context->tree = tree;
        context->outcome = RoboLayoutPassClean;
        return RoboOk;
    }

    return LayoutContextRun(context, tree, diag);
}

void LayoutContextStats(const LayoutContext *context,
                               LayoutStats *out)
{
    if (!out) {
        return;
    }
    if (!context) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = context->stats;
}

LayoutPassOutcome LayoutContextOutcome(const LayoutContext *context)
{
    return context ? context->outcome : RoboLayoutPassNone;
}

const LayoutTree *LayoutContextTree(const LayoutContext *context)
{
    return context ? context->tree : NULL;
}
