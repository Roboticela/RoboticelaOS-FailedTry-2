/*
 * RoboLayout results (Phase 10.1).
 *
 * Where a box ended up and how big it is -- plus, and this is the part that
 * matters in an architecture phase, an explicit statement of whether anybody has
 * worked that out yet:
 *
 *   NONE         this object has no result at all: the object is not in a tree
 *                that has been through a pass, or it is a box that the pass
 *                deliberately did not reach (a `display: none` element never
 *                becomes an object, so NONE here means something else went on)
 *   UNRESOLVED   the box exists and nobody has computed its geometry. This is
 *                the state every box is in after a 10.1 pass, and it is the
 *                whole point of the phase: a caller reading a result is told the
 *                truth instead of being handed (0, 0)
 *   COMPUTED     position and size are final for the pass that produced them
 *
 * Why this is not "just a rect": a `robo_gfx_rect_t` cannot express *unresolved*,
 * and every consumer in the tree above -- paint (11), compositing (15), hit
 * testing (12) -- has a decision to make about a box whose geometry is not known.
 * A zero rectangle forces that decision to be "paint a degenerate box", which is
 * a silent wrong answer, where UNRESOLVED forces it to be "skip this box and
 * count it".
 *
 * The coordinate space is stated rather than implied, because every later goal
 * has to agree on it:
 *
 *   `position` is the box's top-left corner, in its **containing block's**
 *   coordinate space. It is not a document-space coordinate and not a
 *   viewport-space one: those are derived by walking the chain of containing
 *   blocks, and each step of that walk is a later goal's job. What 10.1 fixes is
 *   that a result *can* say where a box is, so that 10.2 onwards has one place to
 *   put the answer.
 *
 * `size` is the border-box size -- the thing a box's edges enclose -- because
 * that is the only size paint can draw without re-deriving padding and border.
 * 10.1 does not compute it; it states what the number will mean.
 */

#ifndef ROBO_LAYOUT_RESULT_H
#define ROBO_LAYOUT_RESULT_H

#include "RoboBase/RoboError.h"
#include "RoboLayout/RoboLayoutDiagnostics.h"
#include "RoboLayout/RoboLayoutGeometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** How much of a box's geometry is known. */
typedef enum robo_layout_result_state {
    /** No result at all. */
    ROBO_LAYOUT_RESULT_NONE = 0,
    /** A box exists; its geometry has not been computed. */
    ROBO_LAYOUT_RESULT_UNRESOLVED = 1,
    /** Position and size are final for the pass that produced them. */
    ROBO_LAYOUT_RESULT_COMPUTED = 2
} robo_layout_result_state_t;

/** Number of defined result states. */
#define ROBO_LAYOUT_RESULT_STATE_COUNT 3u

/** Non-zero if @p state is a defined result state. */
int robo_layout_result_state_is_known(robo_layout_result_state_t state);

/** Human-readable state name; never NULL; "unknown" when undefined. */
const char *robo_layout_result_state_name(robo_layout_result_state_t state);

/*
 * Result flags. Two of them, because the fact a consumer needs about a box that
 * does not fit is *which* direction it overflowed: a scrollable axis is a
 * different box from a clipped one. Both stay 0 in 10.1 -- nothing measures
 * anything -- and their existence is what 10.13 fills in rather than a new field
 * that changes the struct's layout.
 */
#define ROBO_LAYOUT_RESULT_FLAG_OVERFLOW_X (1u << 0)
#define ROBO_LAYOUT_RESULT_FLAG_OVERFLOW_Y (1u << 1)
/** Every defined result flag. */
#define ROBO_LAYOUT_RESULT_FLAG_ALL \
    (ROBO_LAYOUT_RESULT_FLAG_OVERFLOW_X | ROBO_LAYOUT_RESULT_FLAG_OVERFLOW_Y)

/**
 * One box's geometry.
 *
 * A record by value and owned by nobody: it holds no allocation, so it is safe
 * to copy, to embed, and to put on the stack.
 */
typedef struct robo_layout_result {
    robo_layout_result_state_t state;
    /** Top-left corner, in the containing block's space. Meaningful only when
     *  @c state is COMPUTED. */
    robo_layout_point_t position;
    /** Border-box size. Meaningful only when @c state is COMPUTED. */
    robo_layout_size_t size;
    /** ROBO_LAYOUT_RESULT_FLAG_* bits. */
    uint32_t flags;
} robo_layout_result_t;

/** Clear @p result to NONE with zeroed geometry. Safe on NULL. */
void robo_layout_result_init(robo_layout_result_t *result);

/** Mark @p result as "a box exists, nobody has computed it". Clears geometry
 *  and flags, so a second pass cannot inherit the first pass's numbers. Safe on
 *  NULL. */
void robo_layout_result_set_unresolved(robo_layout_result_t *result);

/** The state. ROBO_LAYOUT_RESULT_NONE for NULL. */
robo_layout_result_state_t robo_layout_result_state(const robo_layout_result_t *result);

/** Non-zero when position and size are final for the producing pass. */
int robo_layout_result_is_computed(const robo_layout_result_t *result);

/**
 * Record the geometry and move the result to COMPUTED.
 *
 * The door a later goal computes through. It validates rather than trusts: a
 * negative width or height is refused, because a box cannot enclose a negative
 * area and a result that carried one would be a wrong answer discovered much
 * later, in paint.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments or a negative size
 *         (@p result is left untouched).
 */
robo_result_t robo_layout_result_set_geometry(robo_layout_result_t *result,
                                              robo_layout_point_t position,
                                              robo_layout_size_t size,
                                              robo_layout_diag_t *diag);

/**
 * Read the position.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND when
 *         the result is not COMPUTED -- the caller is told the geometry is not
 *         known rather than handed a zero. Use robo_layout_result_state to
 *         distinguish "unresolved" from "none".
 */
robo_result_t robo_layout_result_position(const robo_layout_result_t *result,
                                          robo_layout_point_t *out);

/** Read the size. Same contract as robo_layout_result_position. */
robo_result_t robo_layout_result_size(const robo_layout_result_t *result,
                                      robo_layout_size_t *out);

/**
 * Read the box as a half-open rectangle (position + size).
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL arguments; ROBO_NOT_FOUND when
 *         the result is not COMPUTED; ROBO_ERROR when the size does not fit the
 *         rectangle type. The rectangle is in the containing block's space, in
 *         the RoboGraphics convention (see RoboLayoutGeometry.h).
 */
robo_result_t robo_layout_result_rect(const robo_layout_result_t *result,
                                      robo_layout_rect_t *out,
                                      robo_layout_diag_t *diag);

/**
 * Set the flag bits, keeping the geometry.
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p result or a bit outside
 *         ROBO_LAYOUT_RESULT_FLAG_ALL. An unknown bit is refused rather than
 *         stored, so a typo cannot travel through the pipeline looking like a
 *         laid-out fact.
 */
robo_result_t robo_layout_result_set_flags(robo_layout_result_t *result,
                                           uint32_t flags,
                                           robo_layout_diag_t *diag);

/** The flag bits (0 for NULL). */
uint32_t robo_layout_result_flags(const robo_layout_result_t *result);

/** Non-zero when @p result carries @p flag. */
int robo_layout_result_has_flag(const robo_layout_result_t *result, uint32_t flag);

/** Field-by-field equality of state, geometry and flags. Safe on NULL. */
int robo_layout_result_equal(const robo_layout_result_t *a,
                             const robo_layout_result_t *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_RESULT_H */
