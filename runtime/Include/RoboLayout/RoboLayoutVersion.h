/*
 * RoboLayout version pin — Phase 10.1 layout architecture.
 *
 * 0.1.0 is the architecture-only surface, and it is deliberately *shaped* like
 * 9.1 was for RoboCSS: every model the later layout goals will extend exists,
 * every door that would compute geometry is absent rather than faked, and
 * `robo_layout_get_capabilities` says so field by field.
 *
 * What exists here is the *shape* of a layout pass and nothing that resolves
 * anything:
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
 * 10.1 computes no coordinate, resolves no percentage, collapses no margin and
 * paints nothing. `robo_layout_context_run` walks the tree, counts what it saw
 * and leaves every result *UNRESOLVED* -- which is a statement about this build
 * rather than a placeholder for one: a caller that reads a result and finds
 * UNRESOLVED has been told the truth, and a phase that returns (0, 0) instead
 * has been told a lie.
 *
 * The surface will keep growing for the whole of Phase 10, so API freeze is NOT
 * claimed here (ROBO_LAYOUT_API_FROZEN=0). RoboLayout depends on RoboHTML and
 * RoboCSS and on neither the other way round; it is the first module in the tree
 * that is allowed to see both, per DEPENDENCIES.md.
 */

#ifndef ROBO_LAYOUT_VERSION_H
#define ROBO_LAYOUT_VERSION_H

#define ROBO_LAYOUT_VERSION_MAJOR 0
#define ROBO_LAYOUT_VERSION_MINOR 1
#define ROBO_LAYOUT_VERSION_PATCH 0
#define ROBO_LAYOUT_VERSION_STRING "0.1.0"

/** 1 once the Phase 10 surface is frozen (not yet — see header comment). */
#define ROBO_LAYOUT_API_FROZEN 0

/**
 * 1 once this build computes box geometry.
 *
 * A compiler constant rather than a runtime decision, because it is read by two
 * doors that must not disagree -- `robo_layout_get_capabilities().geometry` and
 * `robo_layout_geometry_is_implemented()` -- and by the suite that asserts both.
 * Phase 10.1 ships 0: no coordinate exists, so no caller may read one.
 */
#define ROBO_LAYOUT_GEOMETRY_IMPLEMENTED 0

#endif /* ROBO_LAYOUT_VERSION_H */
