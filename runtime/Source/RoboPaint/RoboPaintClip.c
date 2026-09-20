/*
 * RoboPaint clip stack (Phase 11.8) — see RoboPaintClip.h.
 *
 * One growable array and three operations, and the whole of the type's behaviour is the
 * order it keeps: a push copies a record onto the end, a pop takes the last one off, and
 * "in force" reads the last one. There is no arithmetic here at all -- no intersection, no
 * mode resolution, no boundary -- because the record a caller pushes is already the answer
 * to that question, and a second derivation of it in the container is exactly how two
 * layers end up disagreeing about where a boundary is.
 *
 * The growth discipline is the paint list's and the display list's, so a caller has one
 * shape to learn: capacity doubles from a small first block, the array is reallocated, and a
 * failure leaves the stack untouched. `reset` keeps the capacity, because the callers that
 * matter (the builder and the pass) reset per pass and should not re-grow per pass.
 */
#include "RoboPaint/RoboPaintClip.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

/** Records the first allocation holds. Small: most documents have no scope open at all. */
#define ROBO_PAINT_CLIP_STACK_FIRST 8u

struct robo_paint_clip_stack {
    robo_paint_clip_t *records;
    size_t count;
    size_t capacity;
};

/** Grow @p stack to hold @p capacity records; a no-op when it already can. */
static robo_result_t clip_stack_reserve(robo_paint_clip_stack_t *stack,
                                        size_t capacity,
                                        robo_paint_diag_t *diag)
{
    robo_paint_clip_t *grown;

    if (capacity <= stack->capacity) {
        return ROBO_OK;
    }
    grown = (robo_paint_clip_t *)robo_realloc(stack->records, capacity * sizeof(*grown));
    if (!grown) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_OUT_OF_MEMORY,
                               "could not grow the clip stack");
    }
    /* Zeroed from the old end onward, so a record past the count is a cleared one rather
     * than an uninitialised interval a future reader might trust. */
    memset(grown + stack->capacity, 0, (capacity - stack->capacity) * sizeof(*grown));
    stack->records = grown;
    stack->capacity = capacity;
    return ROBO_OK;
}

robo_result_t robo_paint_clip_stack_create(robo_paint_clip_stack_t **out_stack,
                                           robo_paint_diag_t *diag)
{
    robo_paint_clip_stack_t *stack;

    if (!out_stack) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_INVALID_ARGUMENT,
                               "out_stack is NULL");
    }
    *out_stack = NULL;
    stack = (robo_paint_clip_stack_t *)robo_calloc(1u, sizeof(*stack));
    if (!stack) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_OUT_OF_MEMORY,
                               "could not allocate the clip stack");
    }
    /* No first block: an empty stack is the ordinary state and most stacks never allocate
     * one -- the same "a container that holds nothing costs nothing" rule the builder's
     * open-box stack follows. */
    *out_stack = stack;
    return ROBO_OK;
}

void robo_paint_clip_stack_destroy(robo_paint_clip_stack_t *stack)
{
    if (!stack) {
        return;
    }
    robo_free(stack->records);
    robo_free(stack);
}

void robo_paint_clip_stack_reset(robo_paint_clip_stack_t *stack)
{
    if (!stack) {
        return;
    }
    /* The capacity survives: the callers that reset do it per pass. */
    stack->count = 0u;
}

robo_result_t robo_paint_clip_stack_push(robo_paint_clip_stack_t *stack,
                                         const robo_paint_clip_t *clip,
                                         robo_paint_diag_t *diag)
{
    robo_result_t result;

    if (!stack || !clip) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_INVALID_ARGUMENT,
                               "the stack and the clip are required");
    }
    if (!robo_paint_clip_is_valid(clip)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_ERROR,
                               "the clip is not a record this build produced");
    }
    if (stack->count == stack->capacity) {
        size_t wanted = stack->capacity < 1u
                                ? ROBO_PAINT_CLIP_STACK_FIRST
                                : stack->capacity * 2u;

        result = clip_stack_reserve(stack, wanted, diag);
        if (result != ROBO_OK) {
            return result;
        }
    }
    stack->records[stack->count] = *clip;
    stack->count++;
    return ROBO_OK;
}

robo_result_t robo_paint_clip_stack_pop(robo_paint_clip_stack_t *stack,
                                        robo_paint_diag_t *diag)
{
    if (!stack) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_INVALID_ARGUMENT,
                               "stack is NULL");
    }
    if (stack->count == 0u) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP, ROBO_NOT_FOUND,
                               "there is no clip scope to close");
    }
    stack->count--;
    return ROBO_OK;
}

void robo_paint_clip_stack_current(const robo_paint_clip_stack_t *stack,
                                   robo_paint_clip_t *out)
{
    if (!out) {
        return;
    }
    if (!stack || stack->count == 0u) {
        /* Nothing open is not a miss: it is the cleared record, and the caller may draw
         * under it. */
        robo_paint_clip_init(out);
        return;
    }
    *out = stack->records[stack->count - 1u];
}

size_t robo_paint_clip_stack_depth(const robo_paint_clip_stack_t *stack)
{
    return stack ? stack->count : 0u;
}

size_t robo_paint_clip_stack_capacity(const robo_paint_clip_stack_t *stack)
{
    return stack ? stack->capacity : 0u;
}

int robo_paint_clip_stack_is_empty(const robo_paint_clip_stack_t *stack)
{
    return robo_paint_clip_stack_depth(stack) == 0u;
}
