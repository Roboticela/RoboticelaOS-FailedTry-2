/*
 * RoboLayout results (Phase 10.1) — see RoboLayoutResult.h.
 */

#include "RoboLayout/RoboLayoutResult.h"

#include <string.h>

int robo_layout_result_state_is_known(robo_layout_result_state_t state)
{
    return state >= ROBO_LAYOUT_RESULT_NONE &&
           state < (robo_layout_result_state_t)ROBO_LAYOUT_RESULT_STATE_COUNT;
}

const char *robo_layout_result_state_name(robo_layout_result_state_t state)
{
    switch (state) {
    case ROBO_LAYOUT_RESULT_NONE:
        return "none";
    case ROBO_LAYOUT_RESULT_UNRESOLVED:
        return "unresolved";
    case ROBO_LAYOUT_RESULT_COMPUTED:
        return "computed";
    default:
        return "unknown";
    }
}

void robo_layout_result_init(robo_layout_result_t *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
}

void robo_layout_result_set_unresolved(robo_layout_result_t *result)
{
    if (!result) {
        return;
    }
    /* Zeroed first: a second pass over the same box must not inherit the numbers
     * the first pass wrote, and the flags go with them -- an overflow bit left
     * over from a size nobody has recomputed would be a claim about a box that no
     * longer exists. */
    memset(result, 0, sizeof(*result));
    result->state = ROBO_LAYOUT_RESULT_UNRESOLVED;
}

robo_layout_result_state_t robo_layout_result_state(const robo_layout_result_t *result)
{
    return result ? result->state : ROBO_LAYOUT_RESULT_NONE;
}

int robo_layout_result_is_computed(const robo_layout_result_t *result)
{
    return result && result->state == ROBO_LAYOUT_RESULT_COMPUTED;
}

robo_result_t robo_layout_result_set_geometry(robo_layout_result_t *result,
                                              robo_layout_point_t position,
                                              robo_layout_size_t size,
                                              robo_layout_diag_t *diag)
{
    if (!result) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_RESULT, ROBO_INVALID_ARGUMENT,
                                "result is NULL");
    }
    if (size.width < 0 || size.height < 0) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_RESULT, ROBO_INVALID_ARGUMENT,
                                "box size is negative");
    }
    result->position = position;
    result->size = size;
    result->state = ROBO_LAYOUT_RESULT_COMPUTED;
    return ROBO_OK;
}

robo_result_t robo_layout_result_position(const robo_layout_result_t *result,
                                          robo_layout_point_t *out)
{
    if (!result || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (result->state != ROBO_LAYOUT_RESULT_COMPUTED) {
        return ROBO_NOT_FOUND;
    }
    *out = result->position;
    return ROBO_OK;
}

robo_result_t robo_layout_result_size(const robo_layout_result_t *result,
                                      robo_layout_size_t *out)
{
    if (!result || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (result->state != ROBO_LAYOUT_RESULT_COMPUTED) {
        return ROBO_NOT_FOUND;
    }
    *out = result->size;
    return ROBO_OK;
}

robo_result_t robo_layout_result_rect(const robo_layout_result_t *result,
                                      robo_layout_rect_t *out,
                                      robo_layout_diag_t *diag)
{
    if (!result || !out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_RESULT, ROBO_INVALID_ARGUMENT,
                                "result or out is NULL");
    }
    if (result->state != ROBO_LAYOUT_RESULT_COMPUTED) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_RESULT, ROBO_NOT_FOUND,
                                "the box's geometry has not been computed");
    }
    return robo_gfx_rect_from_point_size(result->position, result->size, out);
}

robo_result_t robo_layout_result_set_flags(robo_layout_result_t *result,
                                           uint32_t flags,
                                           robo_layout_diag_t *diag)
{
    if (!result) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_RESULT, ROBO_INVALID_ARGUMENT,
                                "result is NULL");
    }
    if ((flags & ~(uint32_t)ROBO_LAYOUT_RESULT_FLAG_ALL) != 0u) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_RESULT, ROBO_INVALID_ARGUMENT,
                                "result flags carry an undefined bit");
    }
    result->flags = flags;
    return ROBO_OK;
}

uint32_t robo_layout_result_flags(const robo_layout_result_t *result)
{
    return result ? result->flags : 0u;
}

int robo_layout_result_has_flag(const robo_layout_result_t *result, uint32_t flag)
{
    return result && flag != 0u && (result->flags & flag) == flag;
}

int robo_layout_result_equal(const robo_layout_result_t *a,
                             const robo_layout_result_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->state != b->state || a->flags != b->flags) {
        return 0;
    }
    if (a->state != ROBO_LAYOUT_RESULT_COMPUTED) {
        /* An unresolved result is *equal* to another unresolved one whatever the
         * bytes in its geometry fields are -- there is no geometry to compare, and
         * comparing the scratch values would report two identical results as
         * different. */
        return 1;
    }
    return robo_gfx_point_equal(a->position, b->position) &&
           robo_gfx_size_equal(a->size, b->size);
}
