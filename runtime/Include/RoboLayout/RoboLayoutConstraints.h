/*
 * RoboLayout constraints (Phase 10.1).
 *
 * What a box is being laid out *into*: how much room there is, and what the
 * bounds on the result are. There are three, and each is either known or unset:
 *
 *   available   how much space the containing block offers -- what a percentage
 *               is a percentage *of*, and what `auto` would fill
 *   minimum     the least the box may be (CSS `min-width` / `min-height`)
 *   maximum     the most the box may be (CSS `max-width` / `max-height`)
 *
 * Two decisions make this small header worth arguing about.
 *
 * **Unset is a flag, not a sentinel.** The obvious alternative is to write
 * "unbounded" as a huge number, and it is wrong for a reason that shows up
 * immediately: an unbounded height and a height nobody has worked out yet produce
 * the same arithmetic and are different answers. A caller that has not been told
 * the available width must be able to say so, because there is no containing
 * block in 10.1 to ask and inventing one is precisely the failure this phase
 * exists to avoid.
 *
 * **A negative size is refused, never reinterpreted.** `available = -1` is not
 * "unbounded" and not "zero"; it is a caller bug, and the call says so. That is
 * also why nothing here *clamps an input into range silently*.
 *
 * What is deliberately absent: any CSS sizing rule. `robo_layout_constraints_clamp`
 * applies minimum and maximum to a size somebody else decided, and holds no
 * opinion about what that size should have been -- no percentage resolution, no
 * `auto` filling, no intrinsic sizing, no `box-sizing`. Those are 10.8, and if
 * this header grew them every later goal would have two places to look.
 *
 * `available` is deliberately *not* an implicit maximum in `clamp`. Filling the
 * available space is a formatting-context decision (a block box does, an inline
 * box does not), so a constraint object that treated it as a cap would answer a
 * question that belongs to Phase 10.3.
 */

#ifndef ROBO_LAYOUT_CONSTRAINTS_H
#define ROBO_LAYOUT_CONSTRAINTS_H

#include "RoboBase/RoboError.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutGeometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The space one box is laid out into.
 *
 * A record by value and owned by nobody, like every other model record in the
 * runtime: it holds no allocation and is safe to copy and to put on the stack.
 * The cleared state (ROBO_LAYOUT_CLEARED) is "nothing is known", which is the
 * honest starting point for a pass.
 */
typedef struct robo_layout_constraints {
    robo_layout_size_t available;
    robo_layout_size_t minimum;
    robo_layout_size_t maximum;
    /** Non-zero when the matching field has been set. */
    int has_available;
    int has_minimum;
    int has_maximum;
} robo_layout_constraints_t;

/** Clear @p constraints to "nothing is known". Safe on NULL. */
void robo_layout_constraints_init(robo_layout_constraints_t *constraints);

/** Same as init: reset every field, keeping no value. Safe on NULL. */
void robo_layout_constraints_clear(robo_layout_constraints_t *constraints);

/**
 * Record the space the containing block offers.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p constraints or a negative
 *         width/height. A zero size is a *known* zero, which is why it is
 *         accepted while a negative one is not.
 */
robo_result_t robo_layout_constraints_set_available(robo_layout_constraints_t *constraints,
                                                    robo_layout_size_t size,
                                                    robo_layout_diag_t *diag);

/** Record the least the box may be. Same contract as set_available. */
robo_result_t robo_layout_constraints_set_minimum(robo_layout_constraints_t *constraints,
                                                  robo_layout_size_t size,
                                                  robo_layout_diag_t *diag);

/** Record the most the box may be. Same contract as set_available. */
robo_result_t robo_layout_constraints_set_maximum(robo_layout_constraints_t *constraints,
                                                  robo_layout_size_t size,
                                                  robo_layout_diag_t *diag);

/** Non-zero when the available space has been recorded. 0 for NULL. */
int robo_layout_constraints_has_available(const robo_layout_constraints_t *constraints);
/** Non-zero when a minimum has been recorded. 0 for NULL. */
int robo_layout_constraints_has_minimum(const robo_layout_constraints_t *constraints);
/** Non-zero when a maximum has been recorded. 0 for NULL. */
int robo_layout_constraints_has_maximum(const robo_layout_constraints_t *constraints);

/**
 * Read the recorded value.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND when
 *         that field was never set -- which is a different answer from a
 *         recorded zero and is why these do not simply return a size.
 */
robo_result_t robo_layout_constraints_available(const robo_layout_constraints_t *constraints,
                                                robo_layout_size_t *out);
robo_result_t robo_layout_constraints_minimum(const robo_layout_constraints_t *constraints,
                                              robo_layout_size_t *out);
robo_result_t robo_layout_constraints_maximum(const robo_layout_constraints_t *constraints,
                                              robo_layout_size_t *out);

/**
 * Non-zero when the record is internally consistent: a minimum and a maximum
 * that are both known must not contradict each other.
 *
 * A minimum above the maximum is not repaired and not reported as an error at
 * set time, because the two may legitimately be set in either order and the
 * combination only becomes a problem once both exist. CSS resolves the conflict
 * in the minimum's favour (10.8); this predicate is what lets that goal *see*
 * the conflict, and what lets a caller refuse a nonsensical pair up front.
 */
int robo_layout_constraints_is_valid(const robo_layout_constraints_t *constraints);

/**
 * Apply the recorded minimum and maximum to @p desired.
 *
 * Not a sizing algorithm: it has no opinion about what @p desired should have
 * been and consults neither `available` nor any property. It is the bounds
 * arithmetic every later goal would otherwise write for itself, in one place, so
 * that "min wins over max" is decided once.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a negative
 *         @p desired; ROBO_NOT_FOUND when the bounds contradict each other
 *         (@p out is untouched) -- the caller is told rather than handed a size
 *         no one can defend. When neither bound is known @p desired is copied
 *         through unchanged.
 */
robo_result_t robo_layout_constraints_clamp(const robo_layout_constraints_t *constraints,
                                            robo_layout_size_t desired,
                                            robo_layout_size_t *out,
                                            robo_layout_diag_t *diag);

/** Field-by-field equality, including which fields are set. Safe on NULL. */
int robo_layout_constraints_equal(const robo_layout_constraints_t *a,
                                  const robo_layout_constraints_t *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_CONSTRAINTS_H */
