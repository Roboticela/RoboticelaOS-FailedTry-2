/*
 * RoboLayout version pin — Phase 10.4 inline layout.
 *
 * 0.4.0 is 0.3.0 plus the second direction. 10.3 taught the module to compute a
 * coordinate in the one direction block layout owns -- down -- and 10.4 computes
 * the other: the boxes that participate in an inline formatting context are placed
 * one after another along a line, and when the next one does not fit, the line
 * moves down. The surface stays *shaped* the way 9.1 shaped RoboCSS and 10.1 shaped
 * this module: every model the later layout goals will extend exists, every door
 * that would compute geometry is either implemented or absent rather than faked, and
 * `robo_layout_get_capabilities` says so field by field.
 *
 * What 10.2 added, in one line: a box carries *two* formatting-context ids, because
 * it participates in one and may establish a different one for its contents, and a
 * pass fills both for every box.
 *
 * What 10.3 added, in one line: block-level boxes get geometry — a block box fills
 * its containing block's content width, its block-level children stack below one
 * another from its content origin, and its height is the declared one where this
 * build can resolve it and the stacked extent of its children otherwise.
 *
 * What 10.4 adds, in one line: the contents of a box that establishes an **inline**
 * formatting context become lines — participants placed along a cursor, wrapping
 * when one does not fit — and an inline box's own rectangle is the union of what its
 * contents reached.
 *
 * The one idea 10.3 turns on, because getting it backwards would cost 10.9 and 10.12
 * a rewrite: a box's **containing block** is its **parent box**, and its **formatting
 * context** decides the *direction* its contents are stacked in. 10.2's "transparent"
 * container is transparent to *context*, not to *containment* — it is still the box
 * its children are laid out inside.
 *
 * The one idea 10.4 turns on, for the same kind of reason: an inline-level box is
 * **atomic** when it establishes a formatting context of its own (`inline-block`,
 * whose inner display 10.2 records as `flow-root`) and is **non-atomic** when it
 * establishes nothing (`inline`). The atomic one is a unit on the line — it is the
 * only participant whose width is known before it is placed, and therefore the only
 * one the wrap rule can act on. The non-atomic one is transparent to the line: its
 * children are participants of the *same* context, placed at the same cursor, and
 * its own rectangle is the union of theirs. Reading that the wrong way round would
 * place an unmeasurable box as if it were opaque and drop everything inside it.
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
 * and, from 10.4:
 *
 *   inline layout  the same recursion's other branch: an inline formatting context's
 *                  participants are placed along a preliminary line, wrapping when
 *                  one does not fit, with a transparent inline box written as the
 *                  union of its contents and an atomic one's contents laid out by
 *                  the block pass inside it
 *
 *   the records    both directions report their own counters, and `boxes_computed`
 *                  is the two placement counts added together — one pass, two
 *                  records, because an inline context's height is part of the block
 *                  stack and the two cannot be separate passes
 *
 * `robo_layout_context_run` walks the tree, counts what it saw, classifies every
 * box, and — when the caller has recorded a viewport — lays both directions out. A
 * box neither direction placed is left *UNRESOLVED* rather than given a zeroed
 * rectangle: a caller that reads a result and finds UNRESOLVED has been told the
 * truth, and a build that returns (0, 0) instead has been told a lie. That is why
 * the result *state* is still the per-box authority even though `geometry` is 1.
 *
 * The capability fields say what is and is not in this build, and the four that
 * matter here are deliberately four: `geometry` is 1 because coordinates are
 * computed, `block_layout` is 1 because the block direction is one of them,
 * `inline_layout` is 1 because the inline direction is the other, and
 * `block_and_inline_layout` is their conjunction — the wider question a caller asks
 * when it wants to know whether layout is done, answered by the same derivation it
 * always was rather than retired now that both halves exist.
 *
 * Still absent, and each a named later goal: text measurement and line boxes (10.5,
 * 10.6), baselines (10.7), a declared `width` in the block direction, `min-width`,
 * `max-width` and shrink-to-fit (10.8), margins (10.9), padding (10.10), borders
 * (10.11), positioning (10.12), overflow (10.13), the anonymous block box a mixed
 * container needs and the inner inline context of an atomic inline, and paint
 * (Phase 11).
 *
 * The surface will keep growing for the whole of Phase 10, so API freeze is NOT
 * claimed here (ROBO_LAYOUT_API_FROZEN=0). RoboLayout depends on RoboHTML and
 * RoboCSS and on neither the other way round; it is the first module in the tree
 * that is allowed to see both, per DEPENDENCIES.md.
 */

#ifndef ROBO_LAYOUT_VERSION_H
#define ROBO_LAYOUT_VERSION_H

#define ROBO_LAYOUT_VERSION_MAJOR 0
#define ROBO_LAYOUT_VERSION_MINOR 4
#define ROBO_LAYOUT_VERSION_PATCH 0
#define ROBO_LAYOUT_VERSION_STRING "0.4.0"

/** 1 once the Phase 10 surface is frozen (not yet — see header comment). */
#define ROBO_LAYOUT_API_FROZEN 0

/**
 * 1 once this build computes box geometry.
 *
 * A compiler constant rather than a runtime decision, because it is read by two
 * doors that must not disagree -- `robo_layout_get_capabilities().geometry` and
 * `robo_layout_geometry_is_implemented()` -- and by the suite that asserts both.
 * Ships 1 since 10.3: the pass writes positions and sizes, so a box *can* have
 * geometry.
 *
 * "Can" is the whole of the claim, and the per-box answer is elsewhere: a box
 * neither direction placed is UNRESOLVED, and a pass with no viewport placed
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
 * Paired with `geometry` as a constant for the same reason: the block direction is
 * one of the two things that computes a coordinate, so a build that flipped the
 * wider flag without it would have a capability struct that disagrees with its own
 * behaviour. The conformance suite walks all of them together.
 */
#define ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED 1

/**
 * 1 once this build lays the **inline** direction out.
 *
 * Ships 1 since 10.4, and this constant is the one 10.3's version of this header
 * promised would arrive with the feature rather than before it: it argued that a
 * constant for a direction the build does not lay out would be a name with no code
 * behind it, and that `inline_layout` was at that point a field and not a promise.
 * The code exists now -- `robo_layout_inline_walk_contents`, reached through the
 * block pass -- so the name has something behind it.
 *
 * What it does **not** claim: line *boxes*. This build places participants along a
 * preliminary line and reports the line count; a line box has identity, alignment,
 * a baseline and a font-derived height, and that is 10.6's. The distinction is the
 * same one 10.3 drew between "this build computes coordinates" and "this box has
 * one", and it is drawn here rather than left to a reader.
 */
#define ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED 1

#endif /* ROBO_LAYOUT_VERSION_H */
