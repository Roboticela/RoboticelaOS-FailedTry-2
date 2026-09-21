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
static int LayoutSizeIsKnown(LayoutSize size)
{
    return size.width >= 0 && size.height >= 0;
}

void LayoutConstraintsInit(LayoutConstraints *constraints)
{
    if (!constraints) {
        return;
    }
    memset(constraints, 0, sizeof(*constraints));
}

void LayoutConstraintsClear(LayoutConstraints *constraints)
{
    LayoutConstraintsInit(constraints);
}

Result LayoutConstraintsSetAvailable(LayoutConstraints *constraints,
                                                    LayoutSize size,
                                                    LayoutDiag *diag)
{
    if (!constraints) {
        return RoboLayoutDiag(diag, RoboLayoutDomainConstraints, RoboInvalidArgument,
                                "constraints is NULL");
    }
    if (!LayoutSizeIsKnown(size)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainConstraints, RoboInvalidArgument,
                                "available size is negative");
    }
    constraints->available = size;
    constraints->hasAvailable = 1;
    return RoboOk;
}

Result LayoutConstraintsSetMinimum(LayoutConstraints *constraints,
                                                  LayoutSize size,
                                                  LayoutDiag *diag)
{
    if (!constraints) {
        return RoboLayoutDiag(diag, RoboLayoutDomainConstraints, RoboInvalidArgument,
                                "constraints is NULL");
    }
    if (!LayoutSizeIsKnown(size)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainConstraints, RoboInvalidArgument,
                                "minimum size is negative");
    }
    constraints->minimum = size;
    constraints->hasMinimum = 1;
    return RoboOk;
}

Result LayoutConstraintsSetMaximum(LayoutConstraints *constraints,
                                                  LayoutSize size,
                                                  LayoutDiag *diag)
{
    if (!constraints) {
        return RoboLayoutDiag(diag, RoboLayoutDomainConstraints, RoboInvalidArgument,
                                "constraints is NULL");
    }
    if (!LayoutSizeIsKnown(size)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainConstraints, RoboInvalidArgument,
                                "maximum size is negative");
    }
    constraints->maximum = size;
    constraints->hasMaximum = 1;
    return RoboOk;
}

int LayoutConstraintsHasAvailable(const LayoutConstraints *constraints)
{
    return constraints && constraints->hasAvailable;
}

int LayoutConstraintsHasMinimum(const LayoutConstraints *constraints)
{
    return constraints && constraints->hasMinimum;
}

int LayoutConstraintsHasMaximum(const LayoutConstraints *constraints)
{
    return constraints && constraints->hasMaximum;
}

Result LayoutConstraintsAvailable(const LayoutConstraints *constraints,
                                                LayoutSize *out)
{
    if (!constraints || !out) {
        return RoboInvalidArgument;
    }
    if (!constraints->hasAvailable) {
        return RoboNotFound;
    }
    *out = constraints->available;
    return RoboOk;
}

Result LayoutConstraintsMinimum(const LayoutConstraints *constraints,
                                              LayoutSize *out)
{
    if (!constraints || !out) {
        return RoboInvalidArgument;
    }
    if (!constraints->hasMinimum) {
        return RoboNotFound;
    }
    *out = constraints->minimum;
    return RoboOk;
}

Result LayoutConstraintsMaximum(const LayoutConstraints *constraints,
                                              LayoutSize *out)
{
    if (!constraints || !out) {
        return RoboInvalidArgument;
    }
    if (!constraints->hasMaximum) {
        return RoboNotFound;
    }
    *out = constraints->maximum;
    return RoboOk;
}

int LayoutConstraintsIsValid(const LayoutConstraints *constraints)
{
    if (!constraints) {
        return 0;
    }
    /* One bound is always consistent with itself, and "nothing is known" is a
     * legal state rather than an invalid one. Only a pair can contradict. */
    if (constraints->hasMinimum && constraints->hasMaximum) {
        if (constraints->minimum.width > constraints->maximum.width) {
            return 0;
        }
        if (constraints->minimum.height > constraints->maximum.height) {
            return 0;
        }
    }
    return 1;
}

Result LayoutConstraintsClamp(const LayoutConstraints *constraints,
                                            LayoutSize desired,
                                            LayoutSize *out,
                                            LayoutDiag *diag)
{
    LayoutSize size;

    if (!constraints || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainConstraints, RoboInvalidArgument,
                                "constraints or out is NULL");
    }
    if (!LayoutSizeIsKnown(desired)) {
        return RoboLayoutDiag(diag, RoboLayoutDomainConstraints, RoboInvalidArgument,
                                "desired size is negative");
    }
    if (!LayoutConstraintsIsValid(constraints)) {
        /* Not clamped into something defensible: a minimum above a maximum has no
         * answer, and picking one here would hide a bug that a caller can fix. */
        return RoboLayoutDiag(diag, RoboLayoutDomainConstraints, RoboNotFound,
                                "minimum and maximum contradict each other");
    }

    size = desired;
    if (constraints->hasMaximum) {
        if (size.width > constraints->maximum.width) {
            size.width = constraints->maximum.width;
        }
        if (size.height > constraints->maximum.height) {
            size.height = constraints->maximum.height;
        }
    }
    /* Applied second, so the minimum wins a tie -- CSS's own resolution (10.8)
     * and the one that keeps a box visible. */
    if (constraints->hasMinimum) {
        if (size.width < constraints->minimum.width) {
            size.width = constraints->minimum.width;
        }
        if (size.height < constraints->minimum.height) {
            size.height = constraints->minimum.height;
        }
    }
    *out = size;
    return RoboOk;
}

int LayoutConstraintsEqual(const LayoutConstraints *a,
                                  const LayoutConstraints *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->hasAvailable != b->hasAvailable || a->hasMinimum != b->hasMinimum ||
        a->hasMaximum != b->hasMaximum) {
        return 0;
    }
    /* Only the fields that are set are compared: an unset field's bytes are
     * whatever the caller's stack held, and comparing them would make two equal
     * records unequal. */
    if (a->hasAvailable &&
        !GfxSizeEqual(a->available, b->available)) {
        return 0;
    }
    if (a->hasMinimum && !GfxSizeEqual(a->minimum, b->minimum)) {
        return 0;
    }
    if (a->hasMaximum && !GfxSizeEqual(a->maximum, b->maximum)) {
        return 0;
    }
    return 1;
}
