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
#include "RoboLayoutInternal.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_layout_context {
    robo_layout_constraints_t constraints;
    int has_viewport;
    robo_layout_size_t viewport;
    robo_layout_stats_t stats;
    robo_layout_pass_outcome_t outcome;
    /* Borrowed, and const *as read back*: the pass wrote into the boxes, not into
     * the tree, so a caller that only wants to ask "what did I run on?" has no
     * business editing the structure through this handle. */
    const robo_layout_tree_t *tree;
    void *user_data;
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
static int layout_object_has_block_level_child(robo_layout_tree_t *tree,
                                               const robo_layout_object_t *object)
{
    robo_layout_node_id_t child = object->first_child;

    while (child != ROBO_LAYOUT_NODE_ID_NONE) {
        const robo_layout_object_t *slot = robo_layout_object_slot_const(tree, child);

        if (!slot) {
            /* Unreachable while the tree is add-only and this walk is the only
             * writer. Answering "no" rather than asserting keeps a future
             * removal bug from becoming a crash *inside a classification*, where
             * the stack trace would say nothing about the removal. */
            break;
        }
        if (robo_css_display_is_block_level(slot->display)) {
            return 1;
        }
        child = slot->next_sibling;
    }
    return 0;
}

/**
 * Classify one box: resolve what it participates in, and establish its own
 * context when the rules say it does.
 *
 * @p out_kind receives the kind it established, or NONE.
 */
static robo_result_t layout_classify_object(robo_layout_tree_t *tree,
                                            robo_layout_object_t *object,
                                            robo_layout_fc_kind_t *out_kind,
                                            robo_layout_diag_t *diag)
{
    robo_layout_fc_kind_t kind;
    robo_layout_fc_id_t owned = ROBO_LAYOUT_FC_ID_NONE;
    robo_result_t result;

    if (out_kind) {
        *out_kind = ROBO_LAYOUT_FC_NONE;
    }

    if (object->parent == ROBO_LAYOUT_NODE_ID_NONE) {
        /* The root box establishes the initial formatting context, and
         * *participates* in it, because there is nothing above it to participate
         * in instead. That is what makes "every live box has a context once the
         * tree is classified" an invariant rather than a caveat. */
        result = robo_layout_tree_context_add(tree, ROBO_LAYOUT_FC_ROOT, object->id,
                                             ROBO_LAYOUT_FC_ID_NONE, 0u, &owned, diag);
        if (result != ROBO_OK) {
            return result;
        }
        object->context = owned;
        object->context_owned = owned;
        robo_layout_tree_context_add_participant(tree, owned);
        if (out_kind) {
            *out_kind = ROBO_LAYOUT_FC_ROOT;
        }
        return ROBO_OK;
    }

    {
        const robo_layout_object_t *parent =
            robo_layout_object_slot_const(tree, object->parent);
        robo_layout_formatting_context_t parent_context;

        if (!parent) {
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                    ROBO_ERROR,
                                    "a box's parent link names no box in this tree");
        }

        /*
         * A child participates in whatever its parent establishes -- or, when the
         * parent is transparent, in whatever the parent participates in. That is
         * the whole of the module's notion of transparency, in one expression,
         * and it is why the two ids on a box have to be two ids.
         */
        object->context = (parent->context_owned != ROBO_LAYOUT_FC_ID_NONE)
                              ? parent->context_owned
                              : parent->context;
        if (object->context == ROBO_LAYOUT_FC_ID_NONE) {
            /* Unreachable, and reported rather than assumed: the root box
             * establishes a context, so every descendant reaches one through the
             * parent chain. If this ever fires, the parent links are wrong and
             * saying so beats producing a box that belongs nowhere. */
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                                    ROBO_ERROR,
                                    "a box could not reach any formatting context");
        }

        kind = robo_layout_fc_establishes(
            object->display, layout_object_has_block_level_child(tree, object), 0);
        if (kind != ROBO_LAYOUT_FC_NONE) {
            /* The new context's parent is the one the *owner* participates in, so
             * contexts nest the way the boxes that establish them do. */
            if (robo_layout_tree_context(tree, object->context, &parent_context, diag) !=
                ROBO_OK) {
                return diag ? diag->code : ROBO_ERROR;
            }
            result = robo_layout_tree_context_add(tree, kind, object->id, object->context,
                                                  parent_context.depth + 1u, &owned, diag);
            if (result != ROBO_OK) {
                return result;
            }
            object->context_owned = owned;
            if (out_kind) {
                *out_kind = kind;
            }
        }

        robo_layout_tree_context_add_participant(tree, object->context);
    }
    return ROBO_OK;
}

int robo_layout_pass_outcome_is_known(robo_layout_pass_outcome_t outcome)
{
    return outcome >= ROBO_LAYOUT_PASS_NONE &&
           outcome < (robo_layout_pass_outcome_t)ROBO_LAYOUT_PASS_OUTCOME_COUNT;
}

const char *robo_layout_pass_outcome_name(robo_layout_pass_outcome_t outcome)
{
    switch (outcome) {
    case ROBO_LAYOUT_PASS_NONE:
        return "none";
    case ROBO_LAYOUT_PASS_WALKED:
        return "walked";
    case ROBO_LAYOUT_PASS_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

robo_result_t robo_layout_context_create(robo_layout_context_t **out_context,
                                         robo_layout_diag_t *diag)
{
    robo_layout_context_t *context;

    if (!out_context) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONTEXT, ROBO_INVALID_ARGUMENT,
                                "out_context is NULL");
    }
    *out_context = NULL;
    context = (robo_layout_context_t *)robo_calloc(1u, sizeof(*context));
    if (!context) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONTEXT, ROBO_OUT_OF_MEMORY,
                                "could not allocate the layout context");
    }
    /* calloc already gives the empty state: constraints cleared, no viewport, no
     * outcome, no tree. The explicit init is here so that the definition of
     * "empty" lives in one place rather than in the allocator's behaviour. */
    robo_layout_constraints_init(&context->constraints);
    context->outcome = ROBO_LAYOUT_PASS_NONE;
    *out_context = context;
    return ROBO_OK;
}

void robo_layout_context_destroy(robo_layout_context_t *context)
{
    /* Everything else in the context is borrowed, so there is nothing to release
     * but the struct itself. */
    robo_free(context);
}

void robo_layout_context_reset(robo_layout_context_t *context)
{
    if (!context) {
        return;
    }
    robo_layout_constraints_init(&context->constraints);
    context->has_viewport = 0;
    context->viewport.width = 0;
    context->viewport.height = 0;
    memset(&context->stats, 0, sizeof(context->stats));
    context->outcome = ROBO_LAYOUT_PASS_NONE;
    context->tree = NULL;
    /* user_data is deliberately left alone: it belongs to the caller, and a reset
     * that dropped it would break a caller that installs it once and resets per
     * pass -- which is the normal way to use this. */
}

robo_result_t robo_layout_context_set_viewport(robo_layout_context_t *context,
                                               robo_layout_size_t size,
                                               robo_layout_diag_t *diag)
{
    if (!context) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONTEXT, ROBO_INVALID_ARGUMENT,
                                "context is NULL");
    }
    if (size.width < 0 || size.height < 0) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONTEXT, ROBO_INVALID_ARGUMENT,
                                "viewport size is negative");
    }
    context->viewport = size;
    context->has_viewport = 1;
    /* Recorded as the available space and nothing more -- see the header. */
    return robo_layout_constraints_set_available(&context->constraints, size, diag);
}

robo_result_t robo_layout_context_viewport(const robo_layout_context_t *context,
                                           robo_layout_size_t *out)
{
    if (!context || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!context->has_viewport) {
        return ROBO_NOT_FOUND;
    }
    *out = context->viewport;
    return ROBO_OK;
}

robo_result_t robo_layout_context_set_constraints(robo_layout_context_t *context,
                                                  const robo_layout_constraints_t *constraints,
                                                  robo_layout_diag_t *diag)
{
    if (!context || !constraints) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONTEXT, ROBO_INVALID_ARGUMENT,
                                "context or constraints is NULL");
    }
    context->constraints = *constraints;
    /* A caller that installs constraints has decided what the available space is,
     * so a previously recorded viewport no longer describes them. Keeping the two
     * in step matters because the viewport is read back by later goals as "the
     * initial containing block" and a stale one would be a wrong answer about the
     * root box's parent. */
    if (robo_layout_constraints_has_available(constraints)) {
        (void)robo_layout_constraints_available(constraints, &context->viewport);
        context->has_viewport = 1;
    } else {
        context->has_viewport = 0;
    }
    return ROBO_OK;
}

robo_result_t robo_layout_context_constraints(const robo_layout_context_t *context,
                                              robo_layout_constraints_t *out)
{
    if (!context || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = context->constraints;
    return ROBO_OK;
}

void robo_layout_context_set_user_data(robo_layout_context_t *context, void *user_data)
{
    if (context) {
        context->user_data = user_data;
    }
}

void *robo_layout_context_user_data(const robo_layout_context_t *context)
{
    return context ? context->user_data : NULL;
}

robo_result_t robo_layout_context_run(robo_layout_context_t *context,
                                      robo_layout_tree_t *tree,
                                      robo_layout_diag_t *diag)
{
    size_t index;
    size_t count;
    robo_result_t outcome;

    if (!context || !tree) {
        if (context) {
            context->outcome = ROBO_LAYOUT_PASS_FAILED;
        }
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONTEXT, ROBO_INVALID_ARGUMENT,
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
    context->stats.objects = robo_layout_tree_object_count(tree);
    context->tree = tree;
    context->outcome = ROBO_LAYOUT_PASS_WALKED;

    /*
     * The previous classification is released before the walk, not after it. A
     * pass *replaces* the contexts rather than refining them -- a participant
     * count is a statement about a walk that is about to be redone -- so keeping
     * the old arena alive while the new one is built would leave two generations
     * of contexts for one tree, which is the one thing a reader could not tell
     * apart from a correct answer.
     */
    robo_layout_tree_invalidate_contexts(tree);

    count = context->stats.objects;
    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_fc_kind_t established = ROBO_LAYOUT_FC_NONE;
        robo_layout_object_t *object;

        if (robo_layout_object_id_at(tree, index, &id) != ROBO_OK) {
            /* Unreachable while the tree is add-only and parallel to this walk --
             * which is exactly why it is reported rather than assumed: a future
             * goal that introduces removal has to decide what a pass sees. */
            context->outcome = ROBO_LAYOUT_PASS_FAILED;
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONTEXT, ROBO_ERROR,
                                    "the tree changed under the pass");
        }
        object = robo_layout_object_slot(tree, id);
        if (!object) {
            context->outcome = ROBO_LAYOUT_PASS_FAILED;
            return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONTEXT, ROBO_ERROR,
                                    "a box vanished under the pass");
        }

        /* The one thing this pass writes: the box exists and nobody has computed
         * its geometry. The memset inside is what makes a repeated pass
         * idempotent rather than cumulative. */
        robo_layout_result_set_unresolved(&object->result);

        context->stats.visited++;
        context->stats.boxes_unresolved++;
        if ((object->flags & ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING) != 0u) {
            context->stats.without_style++;
        }
        if (object->depth > context->stats.depth_max) {
            context->stats.depth_max = object->depth;
        }

        /*
         * The classification, on the same walk and in the same order. Creation
         * order is document order, so a box's parent has already been classified
         * by the time the box is reached -- which is what makes "which context do
         * I participate in?" a read of the parent rather than a search up the
         * tree.
         */
        outcome = layout_classify_object(tree, object, &established, diag);
        if (outcome != ROBO_OK) {
            context->outcome = ROBO_LAYOUT_PASS_FAILED;
            return diag ? diag->code : outcome;
        }
        if (established != ROBO_LAYOUT_FC_NONE) {
            context->stats.contexts++;
            if (established < (robo_layout_fc_kind_t)ROBO_LAYOUT_FC_KIND_COUNT) {
                context->stats.contexts_by_kind[established]++;
            }
            context->stats.boxes_establishing++;
        } else {
            context->stats.boxes_participating_only++;
        }
        if (object->context_owned != ROBO_LAYOUT_FC_ID_NONE) {
            robo_layout_formatting_context_t owned_context;

            if (robo_layout_tree_context(tree, object->context_owned, &owned_context,
                                        NULL) == ROBO_OK &&
                owned_context.depth > context->stats.context_depth_max) {
                context->stats.context_depth_max = owned_context.depth;
            }
        }
    }

    /*
     * Marked only after every box has been classified, so "this tree is
     * classified" is never true of a tree half of whose boxes hold a NONE id. An
     * empty tree is classified too -- it has no boxes and no contexts, which is an
     * answer rather than an absence.
     */
    robo_layout_tree_mark_classified(tree);

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
     * `stats.block.blocks_placed` both say nothing was placed, and the header states
     * the precondition.
     */
    if (context->has_viewport && robo_layout_tree_root(tree) != ROBO_LAYOUT_NODE_ID_NONE) {
        robo_result_t laid_out = robo_layout_block_layout(tree, context->viewport,
                                                          &context->stats.block, diag);

        if (laid_out != ROBO_OK) {
            context->outcome = ROBO_LAYOUT_PASS_FAILED;
            return laid_out;
        }
        /*
         * Counted here rather than inside the block pass, because the fact is about
         * *this* pass's boxes and not about the block pass's arithmetic: a box is
         * COMPUTED exactly when the block pass placed it, and the block stats already
         * carry that count. Deriving it keeps one source of truth for the number
         * instead of two counters that must agree -- and it is what makes
         * `boxes_computed` and `blocks_placed` impossible to disagree by construction.
         */
        context->stats.boxes_computed = context->stats.block.blocks_placed;
        context->stats.boxes_unresolved = count > context->stats.boxes_computed
                                              ? count - context->stats.boxes_computed
                                              : 0u;
    }
    /*
     * A tree with no root box is not taken to the block pass at all, and the
     * condition above says so rather than letting a ROBO_NOT_FOUND travel back and
     * be reinterpreted: a document whose root element generates no box has no block
     * layout to do, `objects` is already 0, and every counter is already right.
     */

    context->stats.passes++;
    return ROBO_OK;
}

void robo_layout_context_stats(const robo_layout_context_t *context,
                               robo_layout_stats_t *out)
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

robo_layout_pass_outcome_t robo_layout_context_outcome(const robo_layout_context_t *context)
{
    return context ? context->outcome : ROBO_LAYOUT_PASS_NONE;
}

const robo_layout_tree_t *robo_layout_context_tree(const robo_layout_context_t *context)
{
    return context ? context->tree : NULL;
}
