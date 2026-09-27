/*
 * RoboLayout grid layout (Phase 19.4).
 *
 * A grid container is a box whose inner display is `grid` (`display: grid` or
 * `display: inline-grid`), and this phase is the algorithm that places its in-flow
 * children as **grid items** into a two-dimensional set of **tracks** instead of
 * stacking them or placing them on a line. It is the fourth formatting context this
 * build lays out, beside the block, inline and flex directions, and it has the same
 * shape they do: the box owns a context (`RoboLayoutFcGrid`), the block pass
 * dispatches to it, and the items are ordinary boxes to everything downstream --
 * they have results, margins, padding, borders, overflow, baselines and content of
 * their own.
 *
 * The axis model, and how it differs from flex
 *
 * A flex container has one **main** axis and one cross axis, so an item's size is
 * resolved by one length distribution. A grid container has two axes that are
 * *both* distributions, and they are resolved **in order**: the columns first,
 * because a column's size is a function of the items' inline contributions and
 * nothing else, and then the rows, because a row's size is a function of the
 * items' *block* contributions -- which are not known until each item's contents
 * have been laid out at the inline size the columns decided. That order is the
 * algorithm, and it is why this pass has a phase the flex pass does not: lay the
 * items' contents out in the middle of the track sizing rather than at the end.
 *
 * This build lays a grid out in the horizontal writing mode only:
 *
 *   `grid-auto-flow: row`     auto-placement fills each row in turn
 *   `grid-auto-flow: column`  auto-placement fills each column in turn
 *
 * and `direction: rtl` puts the inline axis's start edge on the right, so it
 * reverses the **column** order exactly as it reverses a flex `row`'s main axis.
 *
 * What the phase claims
 *
 *   - `display: grid` and `display: inline-grid` are real display values
 *     (RoboCSSDisplay.h) and each establishes a grid formatting context;
 *   - the container's **explicit** tracks are its two track lists
 *     (RoboCSSGridTemplate.h), read from the computed style and never parsed here,
 *     with `fr` as a share of the free space, `auto`/`min-content`/`max-content` as
 *     content floors and ceilings, `minmax()` as the pair it is, and `repeat()`
 *     already expanded by the reader;
 *   - **implicit** tracks are created as placement needs them and are sized by
 *     `grid-auto-columns` / `grid-auto-rows`;
 *   - each item is placed by its `grid-row-start`/`-end` and
 *     `grid-column-start`/`-end` when those name a line or a span, and by the
 *     **auto-placement** algorithm otherwise -- in source order, following the
 *     container's `grid-auto-flow` axis, without overlapping a placed item, and
 *     backfilling earlier holes when the flow is `dense`. The axis the flow
 *     advances along is bounded by the tracks that axis already has (the explicit
 *     ones, extended by a placement that named a line past them) and the other
 *     axis grows instead: an automatic placement never adds a track to the axis
 *     it advances across, so a third item in a two-column grid goes into a second
 *     row rather than into a third column;
 *   - an item that **spans** several tracks contributes to the intrinsic tracks it
 *     spans, and its own size is the sum of the tracks it occupies plus the gaps
 *     between them;
 *   - the gutters (`row-gap` / `column-gap`, `normal` being zero for a grid
 *     container, which is CSS's own rule) separate the tracks on their axes;
 *   - `justify-content` distributes the columns and `align-content` the rows in a
 *     container whose size is definite, with `normal`/`stretch` growing the
 *     `auto`-sized tracks rather than leaving the free space at the end;
 *   - `align-items` / `align-self` align an item in the **block** axis of its area,
 *     `stretch` (and `normal`) growing an item with an `auto` height to its area,
 *     and `baseline` lining the first baselines of the items in a row up;
 *   - an `auto` width stretches across the item's area's inline axis, which is
 *     CSS's initial `justify-items: normal`, and a declared width is placed at the
 *     area's inline start;
 *   - an `auto` margin absorbs the free space in its own axis of the item's area
 *     ahead of the alignment properties, exactly as it does in a flex item;
 *   - `direction: rtl` reverses the column order and composes with the placement
 *     rules rather than replacing them.
 *
 * What the phase deliberately does **not** claim
 *
 *   - **`justify-items` / `justify-self` / the `place-*` shorthands**, which 18.4
 *     deferred and this phase still does not add: an item's inline alignment is
 *     therefore always the *initial* behaviour (stretch an `auto` width, else the
 *     area's inline start), and a document that wants another cannot ask for it.
 *   - **Named lines, `grid-template-areas` and `subgrid`.** A placement this build
 *     reads is a number, a `span` or `auto`; a `[name]` and an area template are
 *     carried in the declaration and not interpreted, and `grid-template-areas`
 *     places nothing.
 *   - **`repeat(auto-fill)` / `repeat(auto-fit)`**, whose count is the container's
 *     used size, and **the `grid`, `grid-template`, `grid-row`, `grid-column` and
 *     `grid-area` shorthands**: nothing in this build expands a shorthand, so only
 *     the longhands are read (the same boundary `flex` and `flex-flow` are drawn at).
 *   - **Masonry**, **grid's `order`-aware placement order beyond document order**
 *     (an item's `order` is not consulted: grid placement is in *order-modified
 *     document order*, and this build's document order is what it has),
 *     **`align-self: baseline` on the inline axis**, and a grid **root** box (the
 *     tree root establishes the initial block context whatever it computes to).
 *   - **Replaced elements / `aspect-ratio`**, and **vertical writing modes**.
 *
 * This header is the counters record the pass reports through `LayoutBlockStats`
 * and the two doors over it; the algorithm is RoboLayoutGrid.c and is reached only
 * through the block pass, exactly as the other three directions are.
 */

#ifndef ROBO_LAYOUT_GRID_H
#define ROBO_LAYOUT_GRID_H

#include <stddef.h>
#include <stdint.h>

#include "RoboBase/RoboError.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* What one grid container's layout came to                            */
/* ------------------------------------------------------------------ */

/**
 * Counters for the grid work one block pass did. A record by value, owned by
 * nobody, and nested in `LayoutBlockStats` beside the flex counters and the block,
 * inline, margin, padding, border, position and overflow ones.
 *
 * Counters rather than one total, for the reason every other record in this module
 * gives: the interesting fact is the *mix*. Two containers can place the same
 * number of items while one auto-placed them into an implicit track and the other
 * read every line number from a declaration, and a record that could not tell them
 * apart would not be testing the algorithm.
 */
typedef struct LayoutGridCounters {
    /** Grid containers whose contents the grid pass laid out. */
    size_t containers;
    /** Grid **items** that received geometry. The grid counterpart of
     *  `LayoutBlockStats.blocksPlaced`. */
    size_t itemsPlaced;
    /** In-flow children that are **not** grid items this build can place -- a text
     *  run (whose anonymous grid item is not built), an element the pass declined,
     *  or an item past the pass's own ceiling. Counted so `unreached` stays an
     *  honest difference (`RoboLayoutBlock.h` states the identity that closes over
     *  every direction). */
    size_t itemsSkipped;
    /** Column tracks the pass sized, explicit and implicit together. */
    size_t columns;
    /** Row tracks the pass sized, explicit and implicit together. */
    size_t rows;
    /** Tracks the final grid has **beyond the two templates**: the implicit extents on
     *  both axes, counted once per track rather than once per item that reached it. A
     *  placement that named a line past the last explicit one and an automatic placement
     *  that ran out of tracks both count here, because both create tracks the templates
     *  did not declare. The counter that says whether the implicit grid was real work or
     *  merely declared. */
    size_t implicitTracks;
    /** Items the placement resolved from their four longhands on **both** axes. An item
     *  definite on only one axis is placed by the cursor in that axis's band, so it is
     *  counted in `autoPlaced` instead -- the counter says which *mechanism* answered,
     *  and a banded search is the cursor's. */
    size_t placedItems;
    /** Items the **auto-placement** algorithm placed, including the ones it placed
     *  against a definite line on one axis and a cursor position on the other. */
    size_t autoPlaced;
    /** Items a dense flow actually **backfilled**: the ones placed earlier in the flow
     *  order than the sparse cursor had reached. `grid-auto-flow`'s density is the one
     *  thing it changes -- which *order* the items appear in -- so the counter answers
     *  "did looking back matter?" rather than "was the flow dense?", and it is
     *  deliberately not the same number as `autoPlaced` under a dense flow. */
    size_t denseItems;
    /** Items spanning more than one track on either axis. Their contribution to a
     *  track is the distributed one, which is a different arithmetic from a
     *  single-track item's. */
    size_t spanningItems;
    /** `<flex>` tracks the pass resolved -- a share of the free space rather than
     *  a declared or content size. */
    size_t frTracks;
    /** Tracks whose size came from their contents (a `min-content`, `max-content`
     *  or `auto` breadth) rather than from a declaration. */
    size_t contentTracks;
    /** Items that **stretched** into their area because their resolved block-axis
     *  alignment was `stretch` (or `normal`) and their height was `auto`. */
    size_t stretched;
    /** Items with an `auto` margin that **absorbed** free space in an axis, so the
     *  item's margin box grew rather than the alignment property placing it. */
    size_t autoMargins;
    /** Items whose resolved block-axis alignment was `baseline`, so their first
     *  baseline was aligned with their row's. */
    size_t baselineItems;
    /** Containers whose column order was reversed -- `direction: rtl`. */
    size_t reversed;
    /** Items whose declared inline size the columns could not honour, so the item
     *  is wider than the area it occupies. Reported rather than clamped: an
     *  overflowing item is CSS's answer, and hiding it would be this build's. */
    size_t overflowingItems;
} LayoutGridCounters;

/** Clear @p counters to zero. Safe on NULL. */
void LayoutGridCountersInit(LayoutGridCounters *counters);

/** Field-by-field equality. Safe on NULL. */
int LayoutGridCountersEqual(const LayoutGridCounters *a,
                            const LayoutGridCounters *b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_LAYOUT_GRID_H */
