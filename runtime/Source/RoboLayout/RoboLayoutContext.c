/*
 * RoboLayout context (Phase 10.1) — see RoboLayoutContext.h.
 *
 * A context is a struct with five fields and a walk. The walk is deliberately the
 * dumbest thing that can be written -- creation order, one memset per box -- for
 * the reason the phase exists: a walk that did anything clever here would be a
 * layout pass with no geometry algorithm behind it, which is the one outcome this
 * phase must not produce.
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

    count = context->stats.objects;
    for (index = 0u; index < count; index++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
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
    }

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
