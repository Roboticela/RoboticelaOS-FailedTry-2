/*
 * RoboLayout results (Phase 10.1) — see RoboLayoutResult.h.
 */

#include "RoboLayout/RoboLayoutResult.h"

#include <string.h>

int LayoutResultStateIsKnown(LayoutResultState state)
{
    return state >= RoboLayoutResultNone &&
           state < (LayoutResultState)RoboLayoutResultStateCount;
}

const char *LayoutResultStateName(LayoutResultState state)
{
    switch (state) {
    case RoboLayoutResultNone:
        return "none";
    case RoboLayoutResultUnresolved:
        return "unresolved";
    case RoboLayoutResultComputed:
        return "computed";
    default:
        return "unknown";
    }
}

void LayoutResultInit(LayoutResult *result)
{
    if (!result) {
        return;
    }
    memset(result, 0, sizeof(*result));
}

void LayoutResultSetUnresolved(LayoutResult *result)
{
    if (!result) {
        return;
    }
    /* Zeroed first: a second pass over the same box must not inherit the numbers
     * the first pass wrote, and the flags go with them -- an overflow bit left
     * over from a size nobody has recomputed would be a claim about a box that no
     * longer exists. */
    memset(result, 0, sizeof(*result));
    result->state = RoboLayoutResultUnresolved;
}

LayoutResultState MakeLayoutResultState(const LayoutResult *result)
{
    return result ? result->state : RoboLayoutResultNone;
}

int LayoutResultIsComputed(const LayoutResult *result)
{
    return result && result->state == RoboLayoutResultComputed;
}

Result LayoutResultSetGeometry(LayoutResult *result,
                                              LayoutPoint position,
                                              LayoutSize size,
                                              LayoutDiag *diag)
{
    if (!result) {
        return RoboLayoutDiag(diag, RoboLayoutDomainResult, RoboInvalidArgument,
                                "result is NULL");
    }
    if (size.width < 0 || size.height < 0) {
        return RoboLayoutDiag(diag, RoboLayoutDomainResult, RoboInvalidArgument,
                                "box size is negative");
    }
    result->position = position;
    result->size = size;
    result->state = RoboLayoutResultComputed;
    return RoboOk;
}

Result LayoutResultPosition(const LayoutResult *result,
                                          LayoutPoint *out)
{
    if (!result || !out) {
        return RoboInvalidArgument;
    }
    if (result->state != RoboLayoutResultComputed) {
        return RoboNotFound;
    }
    *out = result->position;
    return RoboOk;
}

Result LayoutResultSize(const LayoutResult *result,
                                      LayoutSize *out)
{
    if (!result || !out) {
        return RoboInvalidArgument;
    }
    if (result->state != RoboLayoutResultComputed) {
        return RoboNotFound;
    }
    *out = result->size;
    return RoboOk;
}

Result LayoutResultRect(const LayoutResult *result,
                                      LayoutRect *out,
                                      LayoutDiag *diag)
{
    if (!result || !out) {
        return RoboLayoutDiag(diag, RoboLayoutDomainResult, RoboInvalidArgument,
                                "result or out is NULL");
    }
    if (result->state != RoboLayoutResultComputed) {
        return RoboLayoutDiag(diag, RoboLayoutDomainResult, RoboNotFound,
                                "the box's geometry has not been computed");
    }
    return GfxRectFromPointSize(result->position, result->size, out);
}

Result LayoutResultSetFlags(LayoutResult *result,
                                           uint32_t flags,
                                           LayoutDiag *diag)
{
    if (!result) {
        return RoboLayoutDiag(diag, RoboLayoutDomainResult, RoboInvalidArgument,
                                "result is NULL");
    }
    if ((flags & ~(uint32_t)RoboLayoutResultFlagAll) != 0u) {
        return RoboLayoutDiag(diag, RoboLayoutDomainResult, RoboInvalidArgument,
                                "result flags carry an undefined bit");
    }
    result->flags = flags;
    return RoboOk;
}

uint32_t LayoutResultFlags(const LayoutResult *result)
{
    return result ? result->flags : 0u;
}

int LayoutResultHasFlag(const LayoutResult *result, uint32_t flag)
{
    return result && flag != 0u && (result->flags & flag) == flag;
}

int LayoutResultEqual(const LayoutResult *a,
                             const LayoutResult *b)
{
    if (!a || !b) {
        return a == b;
    }
    if (a->state != b->state || a->flags != b->flags) {
        return 0;
    }
    if (a->state != RoboLayoutResultComputed) {
        /* An unresolved result is *equal* to another unresolved one whatever the
         * bytes in its geometry fields are -- there is no geometry to compare, and
         * comparing the scratch values would report two identical results as
         * different. */
        return 1;
    }
    return GfxPointEqual(a->position, b->position) &&
           GfxSizeEqual(a->size, b->size);
}
