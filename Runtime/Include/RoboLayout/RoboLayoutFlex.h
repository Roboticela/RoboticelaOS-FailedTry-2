/*
 * RoboLayout flex layout (Phase 19.3).
 *
 * A flex container is a box whose inner display is `flex` (`display: flex` or
 * `display: inline-flex`), and this phase is the algorithm that lays its in-flow
 * children out as **flex items** instead of stacking them or placing them on a
 * line. It is the third formatting context this build lays out, beside the block
 * and inline directions, and it establishes the same shape they do: the box owns a
 * context (`RoboLayoutFcFlex`, RoboLayoutFormattingContext.h), the block pass
 * dispatches to it, and the items are ordinary boxes to everything downstream --
 * they have results, margins, padding, borders, overflow and content of their own.
 *
 * The two axes, and why the vocabulary is CSS's
 *
 * A flex container has a **main axis** and a **cross axis**, and which physical
 * direction each is comes from `flex-direction` (RoboCSSFlex.h) and from nothing
 * else -- never from the items, never from a character, and never from a writing
 * mode this build has no vertical flow for. This build lays flex out in the
 * horizontal writing mode only:
 *
 *   `row` / `row-reverse`     main = inline (x), cross = block (y)
 *   `column` / `column-reverse`  main = block (y), cross = inline (x)
 *
 * What the phase claims
 *
 *   - `display: flex` and `display: inline-flex` are real display values
 *     (RoboCSSDisplay.h) and each establishes a flex formatting context.
 *   - the items are the container's in-flow children, ordered by `order` and then
 *     by document order (a stable ordering);
 *   - each item's **flex base size** comes from `flex-basis` when it names one, or
 *     from the item's main-size property when that is definite, or from the item's
 *     measured **content** size (`LayoutIntrinsicMeasureObject`);
 *   - `flex-grow` and `flex-shrink` distribute free space with the min/max clamp,
 *     freezing an item that reaches a bound and redistributing among the rest;
 *   - `flex-wrap` breaks the items into lines when the container's main size is
 *     definite, and `wrap-reverse` reverses the line order across the cross axis;
 *   - `justify-content` positions the items along the main axis, `align-items` /
 *     `align-self` align each across it (with `stretch` growing an item with an
 *     `auto` cross size to its line), and `align-content` distributes the lines.
 *
 * What the phase deliberately does **not** claim
 *
 *   - **A flex root box.** `LayoutFcEstablishes` answers ROOT for the tree root
 *     whatever it computes to, so `display: flex` on the root element lays its
 *     children out as blocks. A later goal can give the initial context an inner
 *     display; this one does not.
 *   - **Baseline alignment.** `align-items: baseline` (and `align-self: baseline`)
 *     is treated as `flex-start`; the cross alignment is a position and no baseline
 *     is consulted.
 *   - **The automatic minimum size** (`min-width: auto` on a flex item). The used
 *     minimum is the declared `min-width`/`min-height` or zero, so an item with
 *     no declared minimum can shrink below its content.
 *   - **`gap` / `row-gap` / `column-gap`** and the `flex` / `flex-flow` shorthands:
 *     nothing in this build expands a shorthand, so only the longhands are read.
 *   - **`auto` margins that absorb free space.** An `auto` margin on a flex item is
 *     used as zero, exactly as an `auto` inset with no scrollport is.
 *   - **`min-width: auto`'s content floor, percentage `flex-basis` against an
 *     indefinite main size** (which degrades to the content size), replaced
 *     elements and `aspect-ratio`, and **vertical writing modes**.
 *
 * This header is the counters record the pass reports through `LayoutBlockStats`
 * and the two doors over it; the algorithm is RoboLayoutFlex.c and is reached only
 * through the block pass, exactly as the inline direction is.
 */

#ifndef ROBO_LAYOUT_FLEX_H
#define ROBO_LAYOUT_FLEX_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* What one flex container's layout came to                            */
/* ------------------------------------------------------------------ */

/**
 * Counters for the flex work one block pass did. A record by value, owned by
 * nobody, and nested in `LayoutBlockStats` beside its block, inline, margin,
 * padding, border, position and overflow counters.
 *
 * Counters rather than one total, for the reason every other record in this module
 * gives: the interesting fact is the *mix*. A `row` container of three equal items
 * and a `column` container of one growable item can place the same number of boxes,
 * and a record that could not tell them apart would not be testing the algorithm.
 */
typedef struct LayoutFlexCounters {
    /** Flex containers whose contents the flex pass laid out. */
    size_t containers;
    /** Flex **items** that received geometry. The flex counterpart of
     *  `LayoutBlockStats.blocksPlaced`. */
    size_t itemsPlaced;
    /** In-flow children that are **not** flex items this build can place -- a text
     *  run (whose anonymous flex item is not built) or an element the flex pass
     *  declined. Counted so `unreached` stays an honest difference (`RoboLayoutBlock.h`
     *  states the identity that closes over every direction). */
    size_t itemsSkipped;
    /** Flex **lines** produced across every container. One for every `nowrap`
     *  container, and more when a wrapped container's items did not fit. */
    size_t lines;
    /** Items whose `flex-grow` was above zero (whether or not there was free space
     *  to give them). The counter that tells a rigid row from a growing one. */
    size_t growItems;
    /** Items that **stretched** across the cross axis because their resolved
     *  alignment was `stretch` (or `normal`) and their cross size was `auto`. */
    size_t stretched;
    /** Items whose `order` moved them from document order. */
    size_t reordered;
    /** Containers whose direction was a `*-reverse` spelling, so the main axis's
     *  start and end edges were swapped. */
    size_t reversed;
} LayoutFlexCounters;

/** Clear @p counters to zero. Safe on NULL. */
void LayoutFlexCountersInit(LayoutFlexCounters *counters);

/** Field-by-field equality. Safe on NULL. */
int LayoutFlexCountersEqual(const LayoutFlexCounters *a,
                            const LayoutFlexCounters *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_FLEX_H */
