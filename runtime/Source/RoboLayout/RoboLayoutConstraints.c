/*
 * RoboLayout constraints (Phase 10.1) — see RoboLayoutConstraints.h.
 */

#include "RoboLayout/RoboLayoutConstraints.h"

#include <string.h>

/**
 * Is this a size layout can hold?
 *
 * A negative width or height is refused everywhere for the same reason: the type
 * can represent it, nothing in layout can mean anything by it, and the two places
 * a negative number is *deliberately* meaningful (an empty size, and an empty
 * rectangle) are predicates in RoboGraphics, not inputs here.
 */
static int layout_size_is_known(robo_layout_size_t size)
{
    return size.width >= 0 && size.height >= 0;
}

void robo_layout_constraints_init(robo_layout_constraints_t *constraints)
{
    if (!constraints) {
        return;
    }
    memset(constraints, 0, sizeof(*constraints));
}

void robo_layout_constraints_clear(robo_layout_constraints_t *constraints)
{
    robo_layout_constraints_init(constraints);
}

robo_result_t robo_layout_constraints_set_available(robo_layout_constraints_t *constraints,
                                                    robo_layout_size_t size,
                                                    robo_layout_diag_t *diag)
{
    if (!constraints) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONSTRAINTS, ROBO_INVALID_ARGUMENT,
                                "constraints is NULL");
    }
    if (!layout_size_is_known(size)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONSTRAINTS, ROBO_INVALID_ARGUMENT,
                                "available size is negative");
    }
    constraints->available = size;
    constraints->has_available = 1;
    return ROBO_OK;
}

robo_result_t robo_layout_constraints_set_minimum(robo_layout_constraints_t *constraints,
                                                  robo_layout_size_t size,
                                                  robo_layout_diag_t *diag)
{
    if (!constraints) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONSTRAINTS, ROBO_INVALID_ARGUMENT,
                                "constraints is NULL");
    }
    if (!layout_size_is_known(size)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONSTRAINTS, ROBO_INVALID_ARGUMENT,
                                "minimum size is negative");
    }
    constraints->minimum = size;
    constraints->has_minimum = 1;
    return ROBO_OK;
}

robo_result_t robo_layout_constraints_set_maximum(robo_layout_constraints_t *constraints,
                                                  robo_layout_size_t size,
                                                  robo_layout_diag_t *diag)
{
    if (!constraints) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONSTRAINTS, ROBO_INVALID_ARGUMENT,
                                "constraints is NULL");
    }
    if (!layout_size_is_known(size)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONSTRAINTS, ROBO_INVALID_ARGUMENT,
                                "maximum size is negative");
    }
    constraints->maximum = size;
    constraints->has_maximum = 1;
    return ROBO_OK;
}

int robo_layout_constraints_has_available(const robo_layout_constraints_t *constraints)
{
    return constraints && constraints->has_available;
}

int robo_layout_constraints_has_minimum(const robo_layout_constraints_t *constraints)
{
    return constraints && constraints->has_minimum;
}

int robo_layout_constraints_has_maximum(const robo_layout_constraints_t *constraints)
{
    return constraints && constraints->has_maximum;
}

robo_result_t robo_layout_constraints_available(const robo_layout_constraints_t *constraints,
                                                robo_layout_size_t *out)
{
    if (!constraints || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!constraints->has_available) {
        return ROBO_NOT_FOUND;
    }
    *out = constraints->available;
    return ROBO_OK;
}

robo_result_t robo_layout_constraints_minimum(const robo_layout_constraints_t *constraints,
                                              robo_layout_size_t *out)
{
    if (!constraints || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!constraints->has_minimum) {
        return ROBO_NOT_FOUND;
    }
    *out = constraints->minimum;
    return ROBO_OK;
}

robo_result_t robo_layout_constraints_maximum(const robo_layout_constraints_t *constraints,
                                              robo_layout_size_t *out)
{
    if (!constraints || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (!constraints->has_maximum) {
        return ROBO_NOT_FOUND;
    }
    *out = constraints->maximum;
    return ROBO_OK;
}

int robo_layout_constraints_is_valid(const robo_layout_constraints_t *constraints)
{
    if (!constraints) {
        return 0;
    }
    /* One bound is always consistent with itself, and "nothing is known" is a
     * legal state rather than an invalid one. Only a pair can contradict. */
    if (constraints->has_minimum && constraints->has_maximum) {
        if (constraints->minimum.width > constraints->maximum.width) {
            return 0;
        }
        if (constraints->minimum.height > constraints->maximum.height) {
            return 0;
        }
    }
    return 1;
}

robo_result_t robo_layout_constraints_clamp(const robo_layout_constraints_t *constraints,
                                            robo_layout_size_t desired,
                                            robo_layout_size_t *out,
                                            robo_layout_diag_t *diag)
{
    robo_layout_size_t size;

    if (!constraints || !out) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONSTRAINTS, ROBO_INVALID_ARGUMENT,
                                "constraints or out is NULL");
    }
    if (!layout_size_is_known(desired)) {
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONSTRAINTS, ROBO_INVALID_ARGUMENT,
                                "desired size is negative");
    }
    if (!robo_layout_constraints_is_valid(constraints)) {
        /* Not clamped into something defensible: a minimum above a maximum has no
         * answer, and picking one here would hide a bug that a caller can fix. */
        return ROBO_LAYOUT_DIAG(diag, ROBO_LAYOUT_DOMAIN_CONSTRAINTS, ROBO_NOT_FOUND,
                                "minimum and maximum contradict each other");
    }

    size = desired;
    if (constraints->has_maximum) {
        if (size.width > constraints->maximum.width) {
            size.width = constraints->maximum.width;
        }
        if (size.height > constraints->maximum.height) {
            size.height = constraints->maximum.height;
        }
    }
    /* Applied second, so the minimum wins a tie -- CSS's own resolution (10.8)
     * and the one that keeps a box visible. */
    if (constraints->has_minimum) {
        if (size.width < constraints->minimum.width) {
            size.width = constraints->minimum.width;
        }
        if (size.height < constraints->minimum.height) {
            size.height = constraints->minimum.height;
        }
    }
    *out = size;
    return ROBO_OK;
}

int robo_layout_constraints_equal(const robo_layout_constraints_t *a,
                                  const robo_layout_constraints_t *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->has_available != b->has_available || a->has_minimum != b->has_minimum ||
        a->has_maximum != b->has_maximum) {
        return 0;
    }
    /* Only the fields that are set are compared: an unset field's bytes are
     * whatever the caller's stack held, and comparing them would make two equal
     * records unequal. */
    if (a->has_available &&
        !robo_gfx_size_equal(a->available, b->available)) {
        return 0;
    }
    if (a->has_minimum && !robo_gfx_size_equal(a->minimum, b->minimum)) {
        return 0;
    }
    if (a->has_maximum && !robo_gfx_size_equal(a->maximum, b->maximum)) {
        return 0;
    }
    return 1;
}
