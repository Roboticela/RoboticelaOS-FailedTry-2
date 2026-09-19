/*
 * RoboLayout version pin — Phase 10.6 line boxes.
 *
 * 0.6.0 is 0.5.0 plus the thing the preliminary line was always going to become: a
 * **line box**. A line is now a record the tree owns -- an id, a finalized rectangle,
 * the context it belongs to, its ordinal in that context, the width it had to fit into,
 * and the objects it placed -- with the construction state (a cursor, a top, a running
 * height and a member chain) staying on the pass's stack where it belongs.
 *
 * What 10.6 adds, in one line: **a line stops being a number and becomes a thing.**
 * 10.4's header called its line *preliminary* and named identity, alignment, a baseline
 * and a font-derived height as this phase's; identity and geometry are here, and the
 * baseline and the font-derived height are 10.7's, which is why the line's height is
 * still "the tallest member" and the header says so where the field is declared.
 *
 * The one idea this phase turns on, and getting it backwards would have made every
 * line a fragment of the wrong thing: **a line's members are the objects it placed,
 * not the participants of its context.** A transparent inline box with contents is a
 * participant -- 10.4 made its rectangle the union of what it enclosed -- and a union
 * can span several lines, so it belongs to no single line. Counting it as a member
 * would either state a rectangle the box does not have or count its children's width
 * twice; the difference between the two counts (`participants_placed` and
 * `line_members`) is exactly that case, which is why both exist.
 *
 * The other decision worth naming: the id is handed out when a line is *finalized*,
 * so an object's back-link to its line cannot be written at placement. That is not a
 * wrinkle to be smoothed over -- it is a consequence of laying out an atomic inline,
 * whose block pass finalizes lines of its own between the moment the parent line
 * opened and the moment it closes. The back-link is therefore filled in one walk at
 * finalization, which is the only moment the id and the membership are known together.
 *
 * 0.5.0 is 0.4.0 plus the participant that is neither box nor line: a **text run**.
 * A text node now generates a layout object of its own, it is measured through a
 * callback seam (RoboLayoutText.h), and its advance width is what a run of text in a
 * `<span>` finally has -- where 10.4 measured it as zero and said so.
 *
 * What 10.5 adds, in one line: **text is measured, and measurement is a seam.** A
 * text node generates a TEXT object; the object borrows its parent element's computed
 * style, because every property a run needs is inherited; the run is measured through
 * `robo_layout_text_metrics_t`, whose real supplier is a RoboFont face
 * (RoboLayoutTextFont.h); and a pass with no supplier measures every run as zero and
 * *counts* it, so "nobody measured" and "measured to nothing" are different facts.
 *
 * The one idea this phase turns on, and getting it backwards would have made every
 * existing layout suite a font test: **the pass must be testable without the
 * dependency.** Measurement is a callback for the same reason computed styles are one
 * (10.1), and the build with no source is byte-for-byte 10.4's behaviour with a
 * counter attached -- which is what let this phase land without moving a single 10.4
 * expectation about geometry.
 *
 * The other decision worth naming: a range is **code points**, not bytes, and the
 * conversion goes through RoboUnicode. A byte offset stops being a character
 * position the moment a document contains "é", "م" or "👋", and the whole point of
 * having built RoboUnicode in Phase 4 is that a later phase does not rediscover it.
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
 *   the records    every direction reports its own counters, and `boxes_computed`
 *                  is the two placement counts added together — one pass, three
 *                  records, because an inline context's height is part of the block
 *                  stack and the two cannot be separate passes, and because "how much
 *                  of the text anything measured" is not a question either of the
 *                  other two can answer
 *
 * and, from 10.6:
 *
 *   line boxes    an arena of finalized lines owned by the tree, each naming its
 *                  formatting context, its owner, its ordinal, its rectangle, the
 *                  width it had to fit into and the objects it placed -- with the
 *                  member chain in a second arena, because the placements of two
 *                  lines interleave
 *
 *   the lifecycle  an open line is construction state on the pass's stack and an
 *                  arena record is a conclusion: finalized at a wrap or at the end of
 *                  its context, never mutated, and released wholesale by the next pass
 *
 *   the back-link `robo_layout_object_line`: the line an object was placed on, written
 *                  once at finalization, refused for an object on no line -- which
 *                  includes a transparent inline box with contents, whose rectangle is
 *                  a union rather than a line's
 *
 * and, from 10.5:
 *
 *   the text model a **range** in code points, the **direction** a run was declared
 *                  in, the **style** a measurement needs resolved from a computed
 *                  style, the **request** and the **run** — with the run's width,
 *                  line extent, ascent, descent, code-point count and missing-glyph
 *                  count
 *
 *   the seam       `robo_layout_text_metrics_t`: who measures, asked through one
 *                  driver that turns "no source", "source with no callback" and "a
 *                  source that failed" into three different answers rather than
 *                  three call sites
 *
 *   the font half  `robo_layout_text_font_t` — the real supplier: a borrowed RoboFont
 *                  face, its cmap and its layout metrics, summing glyph advances in
 *                  **font units** and converting once, because a per-glyph rounding
 *                  accumulates
 *
 *   the TEXT kind  a layout object that is not a box: an element-less, childless
 *                  participant whose `display` is derived as `inline`
 *
 * `robo_layout_context_run` walks the tree, counts what it saw, classifies every
 * box, and — when the caller has recorded a viewport — lays both directions out. A
 * box neither direction placed is left *UNRESOLVED* rather than given a zeroed
 * rectangle: a caller that reads a result and finds UNRESOLVED has been told the
 * truth, and a build that returns (0, 0) instead has been told a lie. That is why
 * the result *state* is still the per-box authority even though `geometry` is 1.
 *
 * The capability fields say what is and is not in this build, and the five that
 * matter here are deliberately five: `geometry` is 1 because coordinates are
 * computed, `block_layout` is 1 because the block direction is one of them,
 * `inline_layout` is 1 because the inline direction is the other,
 * `block_and_inline_layout` is their conjunction, and `line_boxes` is 1 because the
 * lines those directions place are now records a caller can read.
 *
 * Still absent, and each a named later goal: baselines and line-height (10.7),
 * whitespace collapsing and Unicode line breaking, ``line splitting`` and alignment
 * (10.6's remaining neighbours and 10.7's), text
 * **shaping** — GlyphSubstitution, GlyphPositioning, ligatures, kerning — and the
 * bidi algorithm, which are Phase 14's, font *fallback* (10.8), a declared `width` in
 * the block direction, `min-width`, `max-width` and shrink-to-fit (10.8), margins
 * (10.9), padding (10.10), borders (10.11), positioning (10.12), overflow (10.13), the
 * anonymous block box a mixed container needs, and paint (Phase 11).
 *
 * The measurer is **unshaped by design**, and saying so is the difference between a
 * foundation and a claim: it sums the advances the font's own `hmtx` gives, in logical
 * order, which is exactly what CSS calls the advance width of a run *before* layout
 * reorders it. A later phase that adds shaping changes this measurer and nothing
 * else.
 *
 * The surface will keep growing for the whole of Phase 10, so API freeze is NOT
 * claimed here (ROBO_LAYOUT_API_FROZEN=0). RoboLayout depends on RoboHTML and
 * RoboCSS and on neither the other way round; it is the first module in the tree
 * that is allowed to see both, per DEPENDENCIES.md.
 */

#ifndef ROBO_LAYOUT_VERSION_H
#define ROBO_LAYOUT_VERSION_H

#define ROBO_LAYOUT_VERSION_MAJOR 0
#define ROBO_LAYOUT_VERSION_MINOR 7
#define ROBO_LAYOUT_VERSION_PATCH 0
#define ROBO_LAYOUT_VERSION_STRING "0.7.0"

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

/**
 * 1 once this build can ask — and be answered — how wide a run of text is.
 *
 * A constant rather than a runtime decision for the same reason the other three are:
 * it is read by `robo_layout_get_capabilities().text_layout` and by the suite that
 * asserts the flag against the code, so the two cannot drift.
 *
 * What it claims, exactly: a text node becomes a layout object, and a measurement can
 * be asked of a `robo_layout_text_metrics_t` and answered by a real font
 * (RoboLayoutTextFont.h). What it does **not** claim: that any particular pass
 * measured anything. A pass with no source installed measures every run as zero and
 * counts it (`robo_layout_text_stats_t.unmeasured`), and that is a legitimate build
 * configuration rather than a degraded one — which is why the *per-pass* answer is in
 * the stats record and this flag is only about the build.
 */
#define ROBO_LAYOUT_TEXT_LAYOUT_IMPLEMENTED 1

/**
 * 1 once this build produces **line boxes** a caller can read.
 *
 * A constant rather than a runtime decision for the same reason the other four are:
 * it is read by `robo_layout_get_capabilities().line_boxes` and by the suite that
 * asserts the flag against the code. What it claims, exactly: the inline pass
 * finalizes lines into the tree's arena, so after a pass there are line records with
 * geometry, membership and a back-link from each member. What it does **not** claim: a
 * baseline, a `line-height`, segmentation of a line into fragments, alignment, or a
 * font-derived line extent -- all of which are 10.7's or later, and the flag would be
 * lying if it meant them.
 */
#define ROBO_LAYOUT_LINE_BOXES_IMPLEMENTED 1

/**
 * 1 once this build places participants **on a baseline**.
 *
 * A constant rather than a runtime decision for the same reason the other five are:
 * it is read by `robo_layout_get_capabilities().baselines` and by the suite that
 * asserts the flag against the code. What it claims, exactly:
 *
 *   every line a pass stores has a usable baseline (ascent, descent and the kind
 *   they came from), its height is their sum, and every member of it holds a position
 *   derived from that baseline rather than from the line's top edge;
 *
 *   every object the pass places carries the baseline metrics it was placed with, a
 *   text run's from its font with the line gap split as half-leading, and everything
 *   else's from the documented bottom-edge fallback.
 *
 * What it does **not** claim: CSS `vertical-align` in any of its forms, `line-height`
 * as a resolved used value, an inline-block's baseline from its last in-flow line box
 * rather than its bottom edge, a replaced element's baseline, or anything glyph-level.
 * Each is a later phase's, and the header of `RoboLayoutBaseline.h` names the first
 * three where they would otherwise be assumed.
 */
#define ROBO_LAYOUT_BASELINES_IMPLEMENTED 1

#endif /* ROBO_LAYOUT_VERSION_H */
