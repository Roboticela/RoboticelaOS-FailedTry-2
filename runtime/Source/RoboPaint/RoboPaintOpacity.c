/*
 * RoboPaint opacity (Phase 11.9) — see RoboPaintOpacity.h.
 *
 * Three parts, and each is small because the design put the decisions in the header rather
 * than in the arithmetic:
 *
 *   the record      one real, its range, and the *direction* of its cleared state;
 *   the product     the accumulation, which is the only thing this module computes;
 *   the stack       one growable array and three operations, the clip stack's shape exactly.
 *
 * **The one place this file is not a copy of `RoboPaintClip.c`** is the growth. The clip
 * stack zeroes the records past its count, and for a clip a zeroed record *is* the cleared
 * one -- `visible` on both axes with an interval that means nothing on an axis that bounds
 * nothing. A zeroed opacity record is `0.0`, which is fully **transparent**: a valid record,
 * a meaningful one, and the exact opposite of the state that means "nothing is in force". So
 * the new block is filled with the opaque record by a door rather than by `memset`, because
 * the invariant the clip comment states -- "a record past the count is a cleared one rather
 * than an uninitialised interval a future reader might trust" -- is not something zeroing can
 * express for this type.
 */

#include "RoboPaint/RoboPaintOpacity.h"

#include "RoboCSS/RoboCSSOpacity.h"

#include "RoboMem/RoboMem.h"

/* ------------------------------------------------------------------ */
/* The strength in force                                               */
/* ------------------------------------------------------------------ */

void robo_paint_opacity_init(robo_paint_opacity_t *opacity)
{
    if (!opacity) {
        return;
    }
    /* The value module's initial value rather than a literal here: `1.0` is a fact about CSS
     * and the layer that owns the property is the layer that states it, so a build that
     * changed the property's initial value would change one number rather than one number
     * and a copy of it. */
    opacity->value = robo_css_opacity_initial();
}

robo_paint_opacity_t robo_paint_opacity_opaque(void)
{
    robo_paint_opacity_t opacity;

    robo_paint_opacity_init(&opacity);
    return opacity;
}

robo_paint_opacity_t robo_paint_opacity_from_value(double value)
{
    robo_paint_opacity_t opacity;

    opacity.value = robo_css_opacity_clamp(value);
    return opacity;
}

double robo_paint_opacity_value(const robo_paint_opacity_t *opacity)
{
    if (!opacity) {
        /* Nothing is in force, which is full strength -- and it is the same answer the
         * stack gives for an empty one, so the two states a caller can be in (a cleared
         * pointer, an empty scope stack) read alike. */
        return robo_css_opacity_initial();
    }
    return opacity->value;
}

int robo_paint_opacity_is_valid(const robo_paint_opacity_t *opacity)
{
    /* NULL is *not* valid here, unlike in the value and predicate doors above, and the
     * asymmetry is deliberate: those answer "what is in force", which a NULL has an answer
     * to, while this answers "is this a record", which a NULL is not. It is the same split
     * `robo_paint_clip_is_valid` makes. */
    return opacity != NULL && robo_css_opacity_is_valid(opacity->value);
}

int robo_paint_opacity_is_opaque(const robo_paint_opacity_t *opacity)
{
    /* A NULL pointer has nothing in force, so it is answered *opaque* rather than refused:
     * this is a predicate about the state, and "no scope" and "a full-strength scope" are
     * the two states in which nothing inside changes. */
    return robo_css_opacity_is_opaque(robo_paint_opacity_value(opacity));
}

int robo_paint_opacity_is_transparent(const robo_paint_opacity_t *opacity)
{
    if (!opacity) {
        /* The opposite direction from the door above, and for the same reason read the
         * other way round: nothing in force is *not* a transparent scope, it is no scope at
         * all, so a drawing inside it draws at full strength. Answering 1 here would make a
         * caller skip a subtree a document asked it to draw. */
        return 0;
    }
    return robo_css_opacity_is_transparent(opacity->value);
}

int robo_paint_opacity_equal(const robo_paint_opacity_t *a,
                             const robo_paint_opacity_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    /* The number, exactly. Two opacities that differ by one part in a million are two
     * different pictures, and a tolerance here would turn a determinism failure into a
     * pass -- which is the one thing a comparison in this module must not do. */
    return a->value == b->value;
}

robo_paint_opacity_t robo_paint_opacity_multiply(const robo_paint_opacity_t *outer,
                                                 const robo_paint_opacity_t *inner)
{
    /*
     * The product, and the clamp beside it rather than instead of an argument.
     *
     * Both operands are records in `[0, 1]`, so their product is in `[0, 1]` and the clamp
     * cannot fire for two records this build produced. It is applied anyway because this
     * door is also the one a *caller* uses to combine two values it assembled itself, and
     * "the result is a record" is worth holding by construction rather than by an argument
     * that depends on where the operands came from. A NaN operand is answered with a record
     * rather than with a NaN, which is the property every consumer of this type is entitled
     * to: an opacity that reaches a rasteriser is one the rasteriser can apply.
     */
    return robo_paint_opacity_from_value(robo_paint_opacity_value(outer) *
                                         robo_paint_opacity_value(inner));
}

uint8_t robo_paint_opacity_to_byte(const robo_paint_opacity_t *opacity)
{
    double value;

    if (!robo_paint_opacity_is_valid(opacity)) {
        /* The safe direction for a converter: a value nothing can read must not put ink on a
         * surface, and answering 255 would turn a caller's arithmetic bug into a fully drawn
         * document. */
        return 0u;
    }
    value = opacity->value;
    if (value >= 1.0) {
        /* Exactly, rather than a value that rounds to it: `255` is the raster's own default,
         * so a fully opaque drawing is one a raster nothing has changed produces, and the
         * `1.0 * 255 + 0.5` sum below would be relying on truncation to land on it. */
        return 255u;
    }
    if (value <= 0.0) {
        return 0u;
    }
    /* Round to nearest: `+ 0.5` then truncate, so `0.5` becomes `128` rather than `127`.
     * Neither is exactly one half of 255, and the one that is nearer is this one -- the
     * raster then multiplies a source alpha by `128 / 255`, a hair above half. */
    return (uint8_t)(value * 255.0 + 0.5);
}

/* ------------------------------------------------------------------ */
/* The scopes in force                                                 */
/* ------------------------------------------------------------------ */

/** Records the first allocation holds. Small: most documents have no scope open at all. */
#define ROBO_PAINT_OPACITY_STACK_FIRST 8u

struct robo_paint_opacity_stack {
    robo_paint_opacity_t *records;
    size_t count;
    size_t capacity;
};

/** Grow @p stack to hold @p capacity records; a no-op when it already can. */
static robo_result_t opacity_stack_reserve(robo_paint_opacity_stack_t *stack,
                                           size_t capacity,
                                           robo_paint_diag_t *diag)
{
    robo_paint_opacity_t *grown;
    size_t index;

    if (capacity <= stack->capacity) {
        return ROBO_OK;
    }
    grown = (robo_paint_opacity_t *)robo_realloc(stack->records,
                                                 capacity * sizeof(*grown));
    if (!grown) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPACITY, ROBO_OUT_OF_MEMORY,
                               "could not grow the opacity stack");
    }
    /*
     * Filled with the **opaque** record from the old end onward, and by a door rather than by
     * `memset` -- see this file's header comment. A zeroed slot here would be a transparent
     * record, which is a legal value and therefore a *plausible* one: a future reader that
     * trusted a slot past the count would find the strongest possible scope where it should
     * have found none.
     */
    for (index = stack->capacity; index < capacity; index++) {
        robo_paint_opacity_init(&grown[index]);
    }
    stack->records = grown;
    stack->capacity = capacity;
    return ROBO_OK;
}

robo_result_t robo_paint_opacity_stack_create(robo_paint_opacity_stack_t **out_stack,
                                              robo_paint_diag_t *diag)
{
    robo_paint_opacity_stack_t *stack;

    if (!out_stack) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPACITY, ROBO_INVALID_ARGUMENT,
                               "out_stack is NULL");
    }
    *out_stack = NULL;
    stack = (robo_paint_opacity_stack_t *)robo_calloc(1u, sizeof(*stack));
    if (!stack) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPACITY, ROBO_OUT_OF_MEMORY,
                               "could not allocate the opacity stack");
    }
    /* No first block: an empty stack is the ordinary state and most stacks never allocate
     * one. */
    *out_stack = stack;
    return ROBO_OK;
}

void robo_paint_opacity_stack_destroy(robo_paint_opacity_stack_t *stack)
{
    if (!stack) {
        return;
    }
    robo_free(stack->records);
    robo_free(stack);
}

void robo_paint_opacity_stack_reset(robo_paint_opacity_stack_t *stack)
{
    if (!stack) {
        return;
    }
    /* The capacity survives: the callers that reset do it per pass. */
    stack->count = 0u;
}

robo_result_t robo_paint_opacity_stack_push(robo_paint_opacity_stack_t *stack,
                                            const robo_paint_opacity_t *opacity,
                                            robo_paint_diag_t *diag)
{
    robo_result_t result;

    if (!stack || !opacity) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPACITY, ROBO_INVALID_ARGUMENT,
                               "the stack and the opacity are required");
    }
    if (!robo_paint_opacity_is_valid(opacity)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPACITY, ROBO_ERROR,
                               "the opacity is not a record this build produced");
    }
    if (stack->count == stack->capacity) {
        size_t wanted = stack->capacity < 1u
                                ? ROBO_PAINT_OPACITY_STACK_FIRST
                                : stack->capacity * 2u;

        result = opacity_stack_reserve(stack, wanted, diag);
        if (result != ROBO_OK) {
            return result;
        }
    }
    stack->records[stack->count] = *opacity;
    stack->count++;
    return ROBO_OK;
}

robo_result_t robo_paint_opacity_stack_pop(robo_paint_opacity_stack_t *stack,
                                           robo_paint_diag_t *diag)
{
    if (!stack) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPACITY, ROBO_INVALID_ARGUMENT,
                               "stack is NULL");
    }
    if (stack->count == 0u) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPACITY, ROBO_NOT_FOUND,
                               "there is no opacity scope to close");
    }
    stack->count--;
    return ROBO_OK;
}

void robo_paint_opacity_stack_current(const robo_paint_opacity_stack_t *stack,
                                      robo_paint_opacity_t *out)
{
    if (!out) {
        return;
    }
    if (!stack || stack->count == 0u) {
        /* Nothing open is not a miss: it is the opaque record, and the caller may draw under
         * it at full strength. */
        robo_paint_opacity_init(out);
        return;
    }
    *out = stack->records[stack->count - 1u];
}

size_t robo_paint_opacity_stack_depth(const robo_paint_opacity_stack_t *stack)
{
    return stack ? stack->count : 0u;
}

size_t robo_paint_opacity_stack_capacity(const robo_paint_opacity_stack_t *stack)
{
    return stack ? stack->capacity : 0u;
}

int robo_paint_opacity_stack_is_empty(const robo_paint_opacity_stack_t *stack)
{
    return robo_paint_opacity_stack_depth(stack) == 0u;
}
