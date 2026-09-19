/*
 * RoboLayout version pin — Phase 10.3 block layout.
 *
 * 0.3.0 is 0.2.0 plus the first thing that computes a **coordinate**. The surface
 * stays deliberately *shaped* the way 9.1 was for RoboCSS — every model the later
 * layout goals will extend exists, every door that would compute geometry is
 * either implemented or absent rather than faked, and `robo_layout_get_capabilities`
 * says so field by field.
 *
 * What 10.2 added, in one line: a box carries *two* formatting-context ids, because
 * it participates in one and may establish a different one for its contents, and a
 * pass fills both for every box.
 *
 * What 10.3 adds, in one line: block-level boxes get geometry — a block box fills
 * its containing block's content width, its block-level children stack below one
 * another from its content origin, and its height is the declared one where this
 * build can resolve it and the stacked extent of its children otherwise.
 *
 * The one idea that phase turns on, because getting it backwards would cost 10.9
 * and 10.12 a rewrite: a box's **containing block** is its **parent box**, and its
 * **formatting context** decides the *direction* its contents are stacked in. 10.2's
 * "transparent" container is transparent to *context*, not to *containment* — it is
 * still the box its children are laid out inside.
 *
 * What exists here:
 *
 *   the tree       a layout tree distinct from the DOM, whose objects reference
 *                  DOM nodes and computed styles rather than copying either
 *   the object     one box per element that generates one, with parent/child/
 *                  sibling links, its depth, and the display it was built under
 *   the constraints available/minimum/maximum width and height, each either
 *                  *known* or *unset* — a negative size is not "unbounded", it
 *                  is a bug, so the unbounded case is a flag and not a sentinel
 *   the result     position and size, with an explicit state rather than only
 *                  numbers: UNRESOLVED is something a consumer can see, and a
 *                  zeroed rectangle is something it cannot
 *   the context    one pass: the tree it ran on, the constraints it ran with,
 *                  the counters it produced, and the doors a later goal adds
 *                  temporary layout data through
 *   the builder    DOM tree in, layout tree out, with `display: none` elements
 *                  generating no box and every other element generating one
 *   the bridge     the DOM+CSS adapter: RoboCSS's element host, its inline-style
 *                  callback, and one computed style per element, borrowed by the
 *                  tree for as long as the bridge lives
 *
 * and, from 10.2:
 *
 *   the contexts   an arena of formatting contexts owned by the tree, built by
 *                  the pass on the same walk that marks every box UNRESOLVED:
 *                  ROOT, BLOCK and INLINE, each naming the box whose contents it
 *                  formats, the context the owner participates in, how many boxes
 *                  it coordinates and how deeply it nests
 *
 *   the classifier one pure function of a computed display, one boolean and one
 *                  flag, so the rules can be asserted as a table rather than
 *                  inferred from a built document
 *
 * and, from 10.3:
 *
 *   block layout   one recursion over the tree, gated on the formatting context and
 *                  descending the *parent* link, that writes a position and a size
 *                  into each placed box's result
 *
 * `robo_layout_context_run` walks the tree, counts what it saw, classifies every
 * box, and -- when the caller has recorded a viewport -- lays the block direction
 * out. A box it did not place is left *UNRESOLVED* rather than given a zeroed
 * rectangle: a caller that reads a result and finds UNRESOLVED has been told the
 * truth, and a build that returns (0, 0) instead has been told a lie. That is why
 * the result *state* is still the per-box authority even though `geometry` is now
 * 1.
 *
 * The capability fields say what is and is not in this build, and the four that
 * matter here are deliberately four rather than two: `geometry` is 1 because
 * coordinates are computed, `block_layout` is 1 because the block direction is the
 * one that is, `inline_layout` is 0 because line boxes are 10.4's, and
 * `block_and_inline_layout` is 0 because it names both. A phase that implemented
 * one direction and flipped the wider flag would have advertised a feature it does
 * not have.
 *
 * Still absent, and each a named later goal: margins (10.9), padding (10.10),
 * borders (10.11), a declared `width` (10.8), positioning (10.12), overflow
 * (10.13), line boxes (10.4-10.6), and paint (Phase 11).
 *
 * The surface will keep growing for the whole of Phase 10, so API freeze is NOT
 * claimed here (ROBO_LAYOUT_API_FROZEN=0). RoboLayout depends on RoboHTML and
 * RoboCSS and on neither the other way round; it is the first module in the tree
 * that is allowed to see both, per DEPENDENCIES.md.
 */

#ifndef ROBO_LAYOUT_VERSION_H
#define ROBO_LAYOUT_VERSION_H

#define ROBO_LAYOUT_VERSION_MAJOR 0
#define ROBO_LAYOUT_VERSION_MINOR 3
#define ROBO_LAYOUT_VERSION_PATCH 0
#define ROBO_LAYOUT_VERSION_STRING "0.3.0"

/** 1 once the Phase 10 surface is frozen (not yet — see header comment). */
#define ROBO_LAYOUT_API_FROZEN 0

/**
 * 1 once this build computes box geometry.
 *
 * A compiler constant rather than a runtime decision, because it is read by two
 * doors that must not disagree -- `robo_layout_get_capabilities().geometry` and
 * `robo_layout_geometry_is_implemented()` -- and by the suite that asserts both.
 * Ships 1 since 10.3: `robo_layout_block_layout` writes positions and sizes, so a
 * box *can* have geometry.
 *
 * "Can" is the whole of the claim, and the per-box answer is elsewhere: a box the
 * block pass did not place is UNRESOLVED, and a pass with no viewport placed
 * nothing. `robo_layout_result_is_computed` answers about a box.
 *
 * It is deliberately *not* paired with a constant for formatting contexts, and
 * that asymmetry is the point: 10.2's classification is a finished feature rather
 * than a flag waiting to be flipped, so it is a capability field and nothing
 * else. A second compiler constant there would invite a caller to test whether the
 * *build* classifies boxes, when the question they mean to ask is whether a
 * particular tree has been classified -- which
 * `robo_layout_tree_is_classified` answers.
 */
#define ROBO_LAYOUT_GEOMETRY_IMPLEMENTED 1

/**
 * 1 once this build lays the **block** direction out.
 *
 * Paired with `geometry` as a constant for the same reason: the block pass is the
 * only thing that computes a coordinate, so the two are true together and a build
 * that flipped one without the other would have a capability struct that disagrees
 * with its own behaviour. `robo_layout_get_capabilities().block_layout` and
 * `robo_layout_capabilities_t.inline_layout` are its two neighbours, and the
 * conformance suite walks all three together.
 *
 * There is deliberately no constant for the *inline* direction. A constant for a
 * feature this build does not have would be a name with no code behind it, and
 * `inline_layout` is already a field a caller can read.
 */
#define ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED 1

#endif /* ROBO_LAYOUT_VERSION_H */
