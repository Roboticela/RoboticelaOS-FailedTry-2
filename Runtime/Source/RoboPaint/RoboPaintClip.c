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
#define RoboPaintClipStackFirst 8u

struct PaintClipStack {
    PaintClip *records;
    size_t count;
    size_t capacity;
};

/** Grow @p stack to hold @p capacity records; a no-op when it already can. */
static Result ClipStackReserve(PaintClipStack *stack,
                                        size_t capacity,
                                        PaintDiag *diag)
{
    PaintClip *grown;

    if (capacity <= stack->capacity) {
        return RoboOk;
    }
    grown = (PaintClip *)Realloc(stack->records, capacity * sizeof(*grown));
    if (!grown) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboOutOfMemory,
                               "could not grow the clip stack");
    }
    /* Zeroed from the old end onward, so a record past the count is a cleared one rather
     * than an uninitialised interval a future reader might trust. */
    memset(grown + stack->capacity, 0, (capacity - stack->capacity) * sizeof(*grown));
    stack->records = grown;
    stack->capacity = capacity;
    return RoboOk;
}

Result PaintClipStackCreate(PaintClipStack **outStack,
                                           PaintDiag *diag)
{
    PaintClipStack *stack;

    if (!outStack) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboInvalidArgument,
                               "out_stack is NULL");
    }
    *outStack = NULL;
    stack = (PaintClipStack *)RoboCalloc(1u, sizeof(*stack));
    if (!stack) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboOutOfMemory,
                               "could not allocate the clip stack");
    }
    /* No first block: an empty stack is the ordinary state and most stacks never allocate
     * one -- the same "a container that holds nothing costs nothing" rule the builder's
     * open-box stack follows. */
    *outStack = stack;
    return RoboOk;
}

void PaintClipStackDestroy(PaintClipStack *stack)
{
    if (!stack) {
        return;
    }
    RoboFree(stack->records);
    RoboFree(stack);
}

void PaintClipStackReset(PaintClipStack *stack)
{
    if (!stack) {
        return;
    }
    /* The capacity survives: the callers that reset do it per pass. */
    stack->count = 0u;
}

Result PaintClipStackPush(PaintClipStack *stack,
                                         const PaintClip *clip,
                                         PaintDiag *diag)
{
    Result result;

    if (!stack || !clip) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboInvalidArgument,
                               "the stack and the clip are required");
    }
    if (!PaintClipIsValid(clip)) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboError,
                               "the clip is not a record this build produced");
    }
    if (stack->count == stack->capacity) {
        size_t wanted = stack->capacity < 1u
                                ? RoboPaintClipStackFirst
                                : stack->capacity * 2u;

        result = ClipStackReserve(stack, wanted, diag);
        if (result != RoboOk) {
            return result;
        }
    }
    stack->records[stack->count] = *clip;
    stack->count++;
    return RoboOk;
}

Result PaintClipStackPop(PaintClipStack *stack,
                                        PaintDiag *diag)
{
    if (!stack) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboInvalidArgument,
                               "stack is NULL");
    }
    if (stack->count == 0u) {
        return RoboPaintDiag(diag, RoboPaintDomainClip, RoboNotFound,
                               "there is no clip scope to close");
    }
    stack->count--;
    return RoboOk;
}

void PaintClipStackCurrent(const PaintClipStack *stack,
                                   PaintClip *out)
{
    if (!out) {
        return;
    }
    if (!stack || stack->count == 0u) {
        /* Nothing open is not a miss: it is the cleared record, and the caller may draw
         * under it. */
        PaintClipInit(out);
        return;
    }
    *out = stack->records[stack->count - 1u];
}

size_t PaintClipStackDepth(const PaintClipStack *stack)
{
    return stack ? stack->count : 0u;
}

size_t PaintClipStackCapacity(const PaintClipStack *stack)
{
    return stack ? stack->capacity : 0u;
}

int PaintClipStackIsEmpty(const PaintClipStack *stack)
{
    return PaintClipStackDepth(stack) == 0u;
}
