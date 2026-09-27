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
 * direction each runs in comes from `flex-direction` (RoboCSSFlex.h) and from the
 * `direction` the box inherits -- never from the items, never from a character, and
 * never from a writing mode this build has no vertical flow for. This build lays
 * flex out in the horizontal writing mode only:
 *
 *   `row` / `row-reverse`        main = inline (x), cross = block (y)
 *   `column` / `column-reverse`  main = block (y), cross = inline (x)
 *
 * `direction: rtl` puts the inline axis's start edge on the right, so it reverses a
 * **`row`'s main axis**; a `*-reverse` `flex-direction` reverses its own axis the
 * same way, and the two compose by exclusive-or -- which is what makes `row-reverse`
 * inside an RTL container read left-to-right. A `column`'s cross axis is the inline
 * one, and reversing it is 19.10's (see the non-claims below).
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
 *     `auto` cross size to its line, and `baseline` lining first baselines up), and
 *     `align-content` distributes the lines;
 *   - an item whose `min-width`/`min-height` is `auto` gets **the automatic minimum
 *     size** -- a content floor from its measured min-content size, clamped by its
 *     maximum -- so an item does not shrink past its own content by accident;
 *   - an `auto` margin **absorbs** free space on the axis it is on, ahead of
 *     `justify-content` and `align-items`, exactly as CSS's first alignment step
 *     says, and both auto margins share it;
 *   - `direction: rtl` reverses a `row`'s main axis and composes with `*-reverse`.
 *
 * What the phase deliberately does **not** claim
 *
 *   - **A flex root box.** `LayoutFcEstablishes` answers ROOT for the tree root
 *     whatever it computes to, so `display: flex` on the root element lays its
 *     children out as blocks. A later goal can give the initial context an inner
 *     display; this one does not.
 *   - **Baseline alignment on a `column`.** A `row`'s cross axis is the block
 *     (vertical) one, so `baseline` is a first-baseline alignment there and is
 *     implemented; a `column`'s cross axis is the inline one, where this build has
 *     no baseline to place, so `baseline` resolves as `flex-start` and is counted.
 *   - **A reversed inline axis on a `column`.** `direction: rtl` reverses a `row`'s
 *     main axis (above); a `column`'s cross axis is the inline one and reversing it
 *     is 19.10's, so a column's cross placement is always left-to-right here.
 *   - **The automatic minimum's `overflow` exception.** CSS withdraws the content
 *     floor when the item is a scroll container; this build has no scroll state at
 *     layout time, so the floor is applied whatever the item's `overflow`.
 *   - **`gap` / `row-gap` / `column-gap`** and the `flex` / `flex-flow` shorthands:
 *     nothing in this build expands a shorthand, so only the longhands are read.
 *   - **A baseline for anything but a text run.** The item's baseline is its first
 *     baseline-bearing **direct** child's; an item with none (an empty box, a box
 *     whose text is in a grandchild) takes the documented bottom-edge fallback.
 *   - **Percentage `flex-basis` against an indefinite main size** (which degrades to
 *     the content size), replaced elements and `aspect-ratio`, and **vertical
 *     writing modes**.
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
     *  alignment was `stretch` (or `normal`), their cross size was `auto`, and no
     *  `auto` cross margin had already claimed the space. */
    size_t stretched;
    /** Items whose `order` moved them from document order. */
    size_t reordered;
    /** Containers whose main axis's start and end edges were swapped -- a
     *  `*-reverse` spelling, a `row` in an RTL container, or both. */
    size_t reversed;
    /** Items whose used **minimum** main size came from the automatic minimum (a
     *  content floor) rather than a declared `min-width`/`min-height`, **and was
     *  above zero**. The counter that says whether the floor actually bit. */
    size_t contentMinItems;
    /** Items with an `auto` margin that **absorbed** free space on an axis, so the
     *  item's margin box grew rather than `justify-content`/`align-items` placing
     *  it. */
    size_t autoMargins;
    /** Items whose resolved alignment was `baseline`, so their first baseline was
     *  aligned with their line's. */
    size_t baselineItems;
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
