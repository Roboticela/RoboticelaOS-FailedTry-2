/*
 * RoboCSS logical properties (Phase 18.12).
 *
 * A logical property names an edge by its *role* — block-start, inline-start — and
 * lets the writing mode and direction decide which physical edge that is. This
 * module owns the role vocabulary and the one resolver that turns a role into a
 * physical edge:
 *
 *   logical side   block-start | block-end | inline-start | inline-end
 *   logical axis   block | inline
 *
 * `CssLogicalResolveSide` maps a role to a `CssBoxSide` (top/right/bottom/left),
 * reading the **writing-mode** module's `CssWritingMode` and `CssDirection` (§18.13).
 * That is the whole of the mapping, and it is stated once here rather than at every
 * call site:
 *
 *   horizontal-tb   block: top → bottom;  inline: left → right (rtl reverses)
 *   vertical-rl     block: right → left;  inline: top → bottom (rtl reverses)
 *   vertical-lr     block: left → right;  inline: top → bottom (rtl reverses)
 *
 * The two `sideways-*` modes share their vertical counterpart's axes — sideways turns
 * the *text*, not the axes — so `sideways-rl` resolves exactly as `vertical-rl` does.
 *
 * The properties this phase registers are the logical longhands and shorthands of the
 * box model: `margin-*`, `padding-*` and the insets, plus `block-size`/`inline-size`.
 * A logical property is carried as the same shape its physical counterpart is — the
 * mapping to the physical property is this resolver's job, not a second value model.
 *
 * What this module is not
 *
 * Not a registry: nothing here decides inheritance or initial values
 * (RoboCSSPropertyRegistry.h). Not a layout: nothing here maps an element's computed
 * style to physical values — that is the phase that has an element (19.10's RTL
 * layout) — and nothing here owns a writing mode (`RoboText`'s `RoboTextWritingMode`
 * is the engine's, and this module reads the *CSS property* vocabulary).
 */

#ifndef ROBO_CSS_LOGICAL_H
#define ROBO_CSS_LOGICAL_H

#include "RoboBase/RoboError.h"
#include "RoboCSS/RoboCSSBox.h"
#include "RoboCSS/RoboCSSDiagnostics.h"
#include "RoboCSS/RoboCSSTypes.h"
#include "RoboCSS/RoboCSSValue.h"
#include "RoboCSS/RoboCSSWriting.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* logical sides and axes                                              */
/* ------------------------------------------------------------------ */

/** A logical edge. BLOCK_START is 0. */
typedef enum CssLogicalSide {
    /** `block-start`: the edge content starts from in the block axis. */
    RoboCssLogicalSideBlockStart = 0,
    /** `block-end`: the edge it ends at. */
    RoboCssLogicalSideBlockEnd,
    /** `inline-start`: the edge content starts from in the inline axis. */
    RoboCssLogicalSideInlineStart,
    /** `inline-end`: the edge it ends at. */
    RoboCssLogicalSideInlineEnd
} CssLogicalSide;

/** Number of defined logical sides. */
#define RoboCssLogicalSideCount 4u

/** A logical axis. BLOCK is 0. */
typedef enum CssLogicalAxis {
    /** The block axis. */
    RoboCssLogicalAxisBlock = 0,
    /** The inline axis. */
    RoboCssLogicalAxisInline
} CssLogicalAxis;

/** Number of defined logical axes. */
#define RoboCssLogicalAxisCount 2u

int CssLogicalSideIsKnown(CssLogicalSide side);
const char *CssLogicalSideName(CssLogicalSide side);
size_t CssLogicalSideCount(void);
CssLogicalSide CssLogicalSideFromSlice(CssSlice keyword);
CssLogicalSide CssLogicalSideFromText(const char *keyword);
int CssLogicalSideSliceIsKnown(CssSlice keyword);

/** Which axis @p side belongs to. Unknown answers BLOCK. */
CssLogicalAxis CssLogicalSideAxis(CssLogicalSide side);

/**
 * Whether @p side is the *start* edge of its axis (`block-start`, `inline-start`).
 *
 * The `-start`/`-end` half of the role, asked separately from the axis, because a
 * consumer resolving a shorthand like `margin-inline` reads both. Unknown answers 0.
 */
int CssLogicalSideIsStart(CssLogicalSide side);

Result CssLogicalSideWrite(CssLogicalSide side,
                           char *buffer,
                           size_t capacity,
                           size_t *outLength,
                           CssDiag *diag);

/** Capacity @ref CssLogicalSideWrite needs ("inline-start" plus a NUL). */
#define RoboCssLogicalSideTextMax 16u

int CssLogicalAxisIsKnown(CssLogicalAxis axis);
const char *CssLogicalAxisName(CssLogicalAxis axis);
size_t CssLogicalAxisCount(void);
CssLogicalAxis CssLogicalAxisFromSlice(CssSlice keyword);
CssLogicalAxis CssLogicalAxisFromText(const char *keyword);
int CssLogicalAxisSliceIsKnown(CssSlice keyword);

/* ------------------------------------------------------------------ */
/* resolution                                                          */
/* ------------------------------------------------------------------ */

/**
 * The physical edge @p side resolves to under @p mode and @p direction.
 *
 * `horizontal-tb` reads `direction` on the inline axis; the four vertical modes read
 * it there too (a vertical `rtl` runs its inline progression bottom-to-top), while the
 * block axis follows the mode. An unknown writing mode is read as `horizontal-tb`, the
 * same safe answer `CssWritingModeAxes` gives.
 */
CssBoxSide CssLogicalResolveSide(CssLogicalSide side,
                                 CssWritingMode mode,
                                 CssDirection direction);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_CSS_LOGICAL_H */
