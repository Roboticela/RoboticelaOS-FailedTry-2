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
 * Why this is not "just a rect": a `GfxRect` cannot express *unresolved*,
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
typedef enum LayoutResultState {
    /** No result at all. */
    RoboLayoutResultNone = 0,
    /** A box exists; its geometry has not been computed. */
    RoboLayoutResultUnresolved = 1,
    /** Position and size are final for the pass that produced them. */
    RoboLayoutResultComputed = 2
} LayoutResultState;

/** Number of defined result states. */
#define RoboLayoutResultStateCount 3u

/** Non-zero if @p state is a defined result state. */
int LayoutResultStateIsKnown(LayoutResultState state);

/** Human-readable state name; never NULL; "unknown" when undefined. */
const char *LayoutResultStateName(LayoutResultState state);

/*
 * Result flags. Two of them, because the fact a consumer needs about a box that
 * does not fit is *which* direction it overflowed: a scrollable axis is a
 * different box from a clipped one. Both stay 0 in 10.1 -- nothing measures
 * anything -- and their existence is what 10.13 fills in rather than a new field
 * that changes the struct's layout.
 */
#define RoboLayoutResultFlagOverflowX (1u << 0)
#define RoboLayoutResultFlagOverflowY (1u << 1)
/** Every defined result flag. */
#define RoboLayoutResultFlagAll \
    (RoboLayoutResultFlagOverflowX | RoboLayoutResultFlagOverflowY)

/**
 * One box's geometry.
 *
 * A record by value and owned by nobody: it holds no allocation, so it is safe
 * to copy, to embed, and to put on the stack.
 */
typedef struct LayoutResult {
    LayoutResultState state;
    /** Top-left corner, in the containing block's space. Meaningful only when
     *  @c state is COMPUTED. */
    LayoutPoint position;
    /** Border-box size. Meaningful only when @c state is COMPUTED. */
    LayoutSize size;
    /** ROBO_LAYOUT_RESULT_FLAG_* bits. */
    uint32_t flags;
} LayoutResult;

/** Clear @p result to NONE with zeroed geometry. Safe on NULL. */
void LayoutResultInit(LayoutResult *result);

/** Mark @p result as "a box exists, nobody has computed it". Clears geometry
 *  and flags, so a second pass cannot inherit the first pass's numbers. Safe on
 *  NULL. */
void LayoutResultSetUnresolved(LayoutResult *result);

/** The state. RoboLayoutResultNone for NULL. */
LayoutResultState MakeLayoutResultState(const LayoutResult *result);

/** Non-zero when position and size are final for the producing pass. */
int LayoutResultIsComputed(const LayoutResult *result);

/**
 * Record the geometry and move the result to COMPUTED.
 *
 * The door a later goal computes through. It validates rather than trusts: a
 * negative width or height is refused, because a box cannot enclose a negative
 * area and a result that carried one would be a wrong answer discovered much
 * later, in paint.
 *
 * @return RoboOk; RoboInvalidArgument for NULL arguments or a negative size
 *         (@p result is left untouched).
 */
Result LayoutResultSetGeometry(LayoutResult *result,
                                              LayoutPoint position,
                                              LayoutSize size,
                                              LayoutDiag *diag);

/**
 * Read the position.
 * @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound when
 *         the result is not COMPUTED -- the caller is told the geometry is not
 *         known rather than handed a zero. Use LayoutResultState to
 *         distinguish "unresolved" from "none".
 */
Result LayoutResultPosition(const LayoutResult *result,
                                          LayoutPoint *out);

/** Read the size. Same contract as LayoutResultPosition. */
Result LayoutResultSize(const LayoutResult *result,
                                      LayoutSize *out);

/**
 * Read the box as a half-open rectangle (position + size).
 * @return RoboOk; RoboInvalidArgument for NULL arguments; RoboNotFound when
 *         the result is not COMPUTED; RoboError when the size does not fit the
 *         rectangle type. The rectangle is in the containing block's space, in
 *         the RoboGraphics convention (see RoboLayoutGeometry.h).
 */
Result LayoutResultRect(const LayoutResult *result,
                                      LayoutRect *out,
                                      LayoutDiag *diag);

/**
 * Set the flag bits, keeping the geometry.
 * @return RoboOk; RoboInvalidArgument for NULL @p result or a bit outside
 *         RoboLayoutResultFlagAll. An unknown bit is refused rather than
 *         stored, so a typo cannot travel through the pipeline looking like a
 *         laid-out fact.
 */
Result LayoutResultSetFlags(LayoutResult *result,
                                           uint32_t flags,
                                           LayoutDiag *diag);

/** The flag bits (0 for NULL). */
uint32_t LayoutResultFlags(const LayoutResult *result);

/** Non-zero when @p result carries @p flag. */
int LayoutResultHasFlag(const LayoutResult *result, uint32_t flag);

/** Field-by-field equality of state, geometry and flags. Safe on NULL. */
int LayoutResultEqual(const LayoutResult *a,
                             const LayoutResult *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_RESULT_H */
