/*
 * RoboPaint clip stack (Phase 11.8) — see RoboPaintGeometry.h for the record.
 *
 * 11.1 gave this module a clip *record* and one accumulation: a box's `overflow` intersected
 * with the boundary its ancestor put in force, carried down the traversal as a value. That
 * was enough to bound a drawing and not enough to say **which scope** a drawing is inside:
 * a flat display list carried the boundary on every command and nowhere said where one
 * boundary began and another ended.
 *
 * This header is that missing statement, and it is deliberately a *stack* rather than a
 * richer record. Three things follow, and each is the reason for a decision:
 *
 *   **the stack holds records, not derivations.** A pushed value is a whole
 *   `robo_paint_clip_t` -- both axes, both modes, both intervals -- so "the clip in force"
 *   is the top of the stack and needs no boundary, no mode and no layout tree to work out.
 *   A stack of *boundaries* would have to be re-intersected against the element geometry to
 *   answer the same question, which is the arithmetic `RoboPaintGeometry.h` already did
 *   once, at the box that owned it;
 *
 *   **nothing here decides whether to clip.** A push is a statement about the stream, not a
 *   policy: the traversal asks RoboLayout's overflow doors, intersects, and pushes the
 *   result. This type has no opinion about CSS;
 *
 *   **the caller owns it.** Like the paint list and the display list, a stack is created,
 *   used and destroyed by whoever needs it -- no global clip state, no hidden depth, and two
 *   stacks can be walked at once. The paint pass keeps one, and it is the only long-lived one
 *   in the build.
 *
 * **Where it is used, and where a value is used instead.** The traversal is recursive and
 * carries its clip as a *value* (`paint_walk`'s argument), because a recursive walk's
 * "what is in force here" is the call's own parameter and a shared mutable stack would be a
 * second mechanism for one fact. The stack is for the two consumers that read a **flat**
 * sequence: the display-list builder, which uses it to check that every command inside a
 * scope agrees with the scope, and the decoration pass, which uses it to know what bounds a
 * drawing without re-deriving anything. Two readers, one sequence, one stack each.
 *
 * A stack whose depth is zero is the state of a document that clips nothing: `current`
 * answers the cleared record, and a caller may draw under it without asking whether anything
 * is in force -- the same "a `visible` axis is carried rather than cleared" rule the record
 * itself states.
 *
 * An **empty** clip is a legal value to push and a meaningful one: a scope ancestor geometry
 * has excluded entirely. Drawing inside it draws nothing, which is a fact about the document
 * and not an error (`robo_paint_clip_is_empty` is how a consumer asks), and popping it
 * restores the enclosing boundary -- an empty scope poisons the drawing inside it and never
 * the stack.
 */
#ifndef ROBO_PAINT_CLIP_H
#define ROBO_PAINT_CLIP_H

#include "RoboBase/RoboError.h"

#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintGeometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A LIFO of clip records. Opaque; owned by whoever created it. */
typedef struct robo_paint_clip_stack robo_paint_clip_stack_t;

/**
 * Create an empty stack (nothing in force).
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p out_stack; ROBO_OUT_OF_MEMORY. The
 *         stack is left empty on failure and @p out_stack receives NULL.
 */
robo_result_t robo_paint_clip_stack_create(robo_paint_clip_stack_t **out_stack,
                                           robo_paint_diag_t *diag);

/** Destroy @p stack, freeing its storage. Safe on NULL. */
void robo_paint_clip_stack_destroy(robo_paint_clip_stack_t *stack);

/**
 * Empty @p stack, keeping its capacity.
 *
 * What a caller reusing a stack across passes calls first, and the only way to end up with
 * an *empty* stack other than popping it -- which is deliberately the same state, because
 * "a fresh pass" and "every scope closed" are the same fact about the clip.
 */
void robo_paint_clip_stack_reset(robo_paint_clip_stack_t *stack);

/**
 * Push @p clip: from here on it is the clip in force.
 *
 * @p clip is copied, so the caller's record may go out of scope immediately -- the property
 * every record in this module has. A `visible`/`visible` record is legal and pushes a scope
 * that bounds nothing; the *command* vocabulary refuses such a push, because a record that
 * establishes no boundary is a record whose fields disagree about what it is
 * (`RoboPaintOperation.h`), but the stack is a container of values and has no opinion.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a @p clip that is not a
 *         record this build produced; ROBO_OUT_OF_MEMORY when the stack cannot grow. On
 *         either failure nothing was pushed and the stack is exactly as it was.
 */
robo_result_t robo_paint_clip_stack_push(robo_paint_clip_stack_t *stack,
                                         const robo_paint_clip_t *clip,
                                         robo_paint_diag_t *diag);

/**
 * Pop the innermost scope.
 *
 * A pop with **nothing to pop is a refusal**, not a silent no-op: it is the one malformed
 * operation a stack can be handed, and answering OK would let a consumer that lost track of
 * its own pushes go on drawing under a boundary that is not in force. The refusal is also
 * safe -- the stack is untouched, so a caller may report it and carry on, exactly as the
 * builder reports a mismatched display command and leaves the caller's list empty rather
 * than half-built.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p stack; ROBO_NOT_FOUND when the stack
 *         is empty.
 */
robo_result_t robo_paint_clip_stack_pop(robo_paint_clip_stack_t *stack,
                                        robo_paint_diag_t *diag);

/**
 * The clip in force: the innermost scope's record, or the cleared record when the stack is
 * empty.
 *
 * @p out always receives a valid record -- an empty stack is not an error here, because
 * "nothing is in force" is the ordinary state of a document that declares no overflow. Safe
 * on NULL: a NULL stack answers the cleared record, so a caller may read a destructively
 * cleared pointer without a branch.
 */
void robo_paint_clip_stack_current(const robo_paint_clip_stack_t *stack,
                                   robo_paint_clip_t *out);

/** Scopes currently open: 0 for an empty stack, which is a legal answer (0 for NULL). */
size_t robo_paint_clip_stack_depth(const robo_paint_clip_stack_t *stack);

/** Records the stack can hold before it must grow (0 for NULL). */
size_t robo_paint_clip_stack_capacity(const robo_paint_clip_stack_t *stack);

/** Non-zero when no scope is open. Safe on NULL (1 -- nothing is in force). */
int robo_paint_clip_stack_is_empty(const robo_paint_clip_stack_t *stack);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_CLIP_H */
