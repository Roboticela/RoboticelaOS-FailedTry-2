/*
 * RoboLayout version pin — Phase 10.15 layout tests, and the last goal of Phase 10.
 *
 * 0.17.0 is 0.16.0 plus **out-of-flow placement** (Phase 14.0): a box with
 * `position: absolute` or `position: fixed` is taken out of flow and placed against its
 * **containing block** -- the nearest positioned ancestor's padding box, or the initial
 * containing block (the viewport) when there is none, and always for `fixed`. The phase adds
 * a header, one policy module, `LayoutCapabilities.outOfFlow` and a diagnostic domain,
 * and it is the behaviour 10.12 named and deferred: `right`/`bottom` anchor an edge, two
 * opposing insets size an axis between them when the declared dimension is `auto`, and a
 * declared dimension beside two insets is over-constrained and counted.
 *
 * It was pulled forward from the CSS-completion goal for one reason, and the reason is worth
 * keeping where a reader meets the version: Phase 14's desktop shell cannot be *composed*
 * without it. A wallpaper that fills the viewport, a dock on the bottom edge, a menu floating
 * over a window and a window that is a rectangle at a place are four out-of-flow boxes, and a
 * build that stacks them in normal flow can express none of the four -- which is exactly what
 * the shell authored in `Desktop/` discovered when its first real layout was attempted.
 *
 * What 0.17.0 does **not** claim is at the flag: no `z-index`, no stacking context, no paint
 * order (document order still decides), no `sticky`, no scrollport, no percentage inset, no
 * `shrink-to-fit`, no `box-sizing: border-box`, and no out-of-flow **inline-level** box (an
 * inline element with `position: absolute` is still placed on its line, because the inline
 * pass owns those participants).
 *
 * 0.16.0 is 0.15.0 plus **the CSS font request and the face it resolves to** (Phase 12.17): a
 * `font-family` declaration becomes an ordered, classified family list, the scalar half is delegated
 * to the measurer's own resolver so a request and a `LayoutTextStyle` cannot disagree, a caller-supplied
 * `LayoutFontProvider` answers each entry in author order, and the face that answered is recorded **on
 * the run** so its width and its glyphs come from one selection. The version moves with the capability
 * because this phase does add surface: two headers, five doors and `LayoutCapabilities.fontRequest`.
 *
 * What 0.16.0 does **not** claim is named at the flag and in `RoboLayoutFontResolver.h`: layout selects
 * a face, it does not *own* a font database -- the resolution is a callback the caller installs, and a
 * build with no provider measures every run with the single attached face exactly as it did before.
 *
 * 0.15.0 is 0.14.0 plus the layer that asks whether the module is *right*: a
 * twenty-first suite (`layout/tests/SuiteRoboLayoutIntegration.c`) that stops testing
 * one layer at a time and lays out whole trees -- a card inside a column inside a
 * shell, with a declared width, a margin, a padding, a border, a relative offset and
 * a clipping child all in the same pass -- so that the fourteen layers above it are
 * checked against each other rather than against their own docs. The suite brings
 * nothing to the module's surface: no door, no enumerator, no capability field, which
 * is why `ROBO_LAYOUT_*_IMPLEMENTED` gains nothing here and the version moves alone.
 *
 * 0.14.0's prose is worth leaving where it is, because 10.15 found the thing a
 * fourteen-layer build is most likely to be wrong about: **the pass's own arithmetic
 * was unbounded.** Every layer from 10.9 on states the rule for its own sums -- add
 * in 64 bits, saturate rather than refuse, "because there is no document an author can
 * write that some number cannot describe" -- and the pass that *calls* them did not:
 * its block cursor, a margin origin and a margin box each summed bounded numbers into
 * an `int32` and wrapped. The wrapped value is not merely a wrong coordinate, it is a
 * *negative* extent, and the sizing layer refuses one -- correctly, since a size is
 * never negative -- so a document that was merely very tall, or merely very deeply
 * indented, made the whole pass **fail** rather than saturate. Three sites, one
 * helper (`LayoutExtentClamp`), and the rule is now in one place so that a layer
 * and the pass cannot disagree about the bound.
 *
 * The other thing the phase writes down is the shape of a finding: the defect was not
 * found by a layer's own suite -- and could not have been, because four of them test
 * the geometry's maximum already, one layer at a time. It was found by an integration
 * case that stacks a `2000000000px` block above a `2147483647px` one, which is a
 * document only a *pass* can see. The consolidation is what found it, which is the
 * argument for the phase existing at all.
 *
 * 0.9.0 is 0.8.0 plus the layer that turns a declaration into **room**: four sides,
 * each of them `auto` or a *signed* px length or a value this build cannot resolve
 * (`RoboLayoutMargin.h`), and two policies that say what those three kinds mean to a
 * block box in normal flow and to an inline-level participant. 10.8's sizing layer
 * then resolves a width against what the margins left rather than against the whole
 * containing block.
 *
 * What 10.9 adds, in one line: **a margin never changes a box's size, and it changes
 * everything about where the box goes** -- its origin inside its containing block, the
 * width its own `auto` fills, and the extent its parent stacks by.
 *
 * The one idea this phase turns on, and getting it backwards would produce a plausible
 * document that is wrong everywhere: **the declarations are not the used values.** A
 * margin is the first value in this module whose *answer* depends on its context --
 * `auto` is nothing until a containing block and a declared width have been measured
 * against each other, and CSS gives `auto` two different rules depending on whether
 * the box is block-level. Hence `LayoutMarginEdges`, which records the four
 * used numbers plus how many declarations each of them came from, and two resolution
 * doors rather than one with a flag.
 *
 * The other decision worth naming: **there is no collapsing.** Adjacent vertical
 * margins add. That is stated in the header, asserted by the suite, and named as the
 * next thing a later goal changes (`layout/docs/Margins.md` §6) -- and the *shape* it
 * leaves behind is that collapsing is a rule over the four used values, not over the
 * declarations, which is why the used values are a record of numbers.
 *
 * 0.8.0 is 0.7.0 plus the layer that turns a declaration into a size: an `auto`
 * dimension, a declared px length and a value this build cannot resolve are three
 * *kinds* (`RoboLayoutSizing.h`), the containing block's availability is recorded
 * per direction rather than as one number, the bounds are applied through 10.1's own
 * clamp, and both directions resolve their width and height through the same two
 * doors instead of reading `LayoutStylePxLength` and hoping.
 *
 * What 10.8 adds, in one line: **a declared `width` finally reaches geometry -- and
 * so does the width a box's children are laid out into.**
 *
 * The one idea this phase turns on, and getting it backwards would have moved every
 * 10.3 expectation in the module: **`auto` width and a declared width resolve through
 * one rule, and for `auto` the rule's answer is the containing block's width.** That
 * is why the recursion's numbers are unchanged wherever nothing declares a width, and
 * why the phase could add real sizing without rewriting a single existing geometry
 * assertion -- the two cases agree exactly where the old build only knew one of them.
 *
 * The other decision worth naming: `UNRESOLVED` is a kind of its own. A percentage and
 * `auto` resolve the same way *today* -- fill the containing block -- and are kept
 * apart because that is the answer that will change: a percentage needs a containing
 * *dimension*, and the day one is known the branch that changes is the resolver's,
 * not the seam's that read the declaration.
 *
 * 0.7.0 is 0.6.0 plus the floor a line rests on: every line a pass stores has an
 * ascent, a descent and a kind, every member's position is derived from them, and a
 * text run's are its font's with the line gap split as half-leading while everything
 * else's are the documented bottom-edge fallback.
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
 * twice; the difference between the two counts (`participantsPlaced` and
 * `lineMembers`) is exactly that case, which is why both exist.
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
 * `LayoutTextMetrics`, whose real supplier is a RoboFont face
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
 * `LayoutGetCapabilities` says so field by field.
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
 *   the records    every direction reports its own counters, and `boxesComputed`
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
 *   the back-link `LayoutObjectLine`: the line an object was placed on, written
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
 *   the seam       `LayoutTextMetrics`: who measures, asked through one
 *                  driver that turns "no source", "source with no callback" and "a
 *                  source that failed" into three different answers rather than
 *                  three call sites
 *
 *   the font half  `LayoutTextFont` — the real supplier: a borrowed RoboFont
 *                  face, its cmap and its layout metrics, summing glyph advances in
 *                  **font units** and converting once, because a per-glyph rounding
 *                  accumulates
 *
 *   the TEXT kind  a layout object that is not a box: an element-less, childless
 *                  participant whose `display` is derived as `inline`
 *
 * `LayoutContextRun` walks the tree, counts what it saw, classifies every
 * box, and — when the caller has recorded a viewport — lays both directions out. A
 * box neither direction placed is left *UNRESOLVED* rather than given a zeroed
 * rectangle: a caller that reads a result and finds UNRESOLVED has been told the
 * truth, and a build that returns (0, 0) instead has been told a lie. That is why
 * the result *state* is still the per-box authority even though `geometry` is 1.
 *
 * The capability fields say what is and is not in this build, and the five that
 * matter here are deliberately five: `geometry` is 1 because coordinates are
 * computed, `blockLayout` is 1 because the block direction is one of them,
 * `inlineLayout` is 1 because the inline direction is the other,
 * `blockAndInlineLayout` is their conjunction, and `lineBoxes` is 1 because the
 * lines those directions place are now records a caller can read.
 *
 * Still absent, and each a named later goal: `line-height` as a resolved used value
 * and `vertical-align` (10.7's remainder), whitespace collapsing and Unicode line
 * breaking, line splitting and alignment (10.6's remaining neighbours), text
 * **shaping** — GlyphSubstitution, GlyphPositioning, ligatures, kerning — and the
 * bidi algorithm, which are Phase 14's, font *fallback*, percentage and intrinsic
 * sizing — `min-content`, `max-content`, `fit-content`, shrink-to-fit (10.8's
 * remainder) — margins (10.9), padding (10.10), borders (10.11), positioning (10.12),
 * overflow (10.13), the anonymous block box a mixed container needs, and paint
 * (Phase 11).
 *
 * The measurer is **unshaped by design**, and saying so is the difference between a
 * foundation and a claim: it sums the advances the font's own `hmtx` gives, in logical
 * order, which is exactly what CSS calls the advance width of a run *before* layout
 * reorders it. A later phase that adds shaping changes this measurer and nothing
 * else.
 *
 * The surface will keep growing for the whole of Phase 10, so API freeze is NOT
 * claimed here (RoboLayoutApiFrozen=0). RoboLayout depends on RoboHTML and
 * RoboCSS and on neither the other way round; it is the first module in the tree
 * that is allowed to see both, per DEPENDENCIES.md.
 */

#ifndef ROBO_LAYOUT_VERSION_H
#define ROBO_LAYOUT_VERSION_H

#define RoboLayoutVersionMajor 0
#define RoboLayoutVersionMinor 17
#define RoboLayoutVersionPatch 0
#define RoboLayoutVersionString "0.17.0"

/** 1 once the Phase 10 surface is frozen (not yet — see header comment). */
#define RoboLayoutApiFrozen 0

/**
 * 1 once this build computes box geometry.
 *
 * A compiler constant rather than a runtime decision, because it is read by two
 * doors that must not disagree -- `LayoutGetCapabilities().geometry` and
 * `LayoutGeometryIsImplemented()` -- and by the suite that asserts both.
 * Ships 1 since 10.3: the pass writes positions and sizes, so a box *can* have
 * geometry.
 *
 * "Can" is the whole of the claim, and the per-box answer is elsewhere: a box
 * neither direction placed is UNRESOLVED, and a pass with no viewport placed
 * nothing. `LayoutResultIsComputed` answers about a box.
 *
 * It is deliberately *not* paired with a constant for formatting contexts, and
 * that asymmetry is the point: 10.2's classification is a finished feature rather
 * than a flag waiting to be flipped, so it is a capability field and nothing
 * else. A second compiler constant there would invite a caller to test whether the
 * *build* classifies boxes, when the question they mean to ask is whether a
 * particular tree has been classified -- which
 * `LayoutTreeIsClassified` answers.
 */
#define RoboLayoutGeometryImplemented 1

/**
 * 1 once this build lays the **block** direction out.
 *
 * Paired with `geometry` as a constant for the same reason: the block direction is
 * one of the two things that computes a coordinate, so a build that flipped the
 * wider flag without it would have a capability struct that disagrees with its own
 * behaviour. The conformance suite walks all of them together.
 */
#define RoboLayoutBlockLayoutImplemented 1

/**
 * 1 once this build lays the **inline** direction out.
 *
 * Ships 1 since 10.4, and this constant is the one 10.3's version of this header
 * promised would arrive with the feature rather than before it: it argued that a
 * constant for a direction the build does not lay out would be a name with no code
 * behind it, and that `inlineLayout` was at that point a field and not a promise.
 * The code exists now -- `LayoutInlineWalkContents`, reached through the
 * block pass -- so the name has something behind it.
 *
 * What it does **not** claim: line *boxes*. This build places participants along a
 * preliminary line and reports the line count; a line box has identity, alignment,
 * a baseline and a font-derived height, and that is 10.6's. The distinction is the
 * same one 10.3 drew between "this build computes coordinates" and "this box has
 * one", and it is drawn here rather than left to a reader.
 */
#define RoboLayoutInlineLayoutImplemented 1

/**
 * 1 once this build can ask — and be answered — how wide a run of text is.
 *
 * A constant rather than a runtime decision for the same reason the other three are:
 * it is read by `LayoutGetCapabilities().textLayout` and by the suite that
 * asserts the flag against the code, so the two cannot drift.
 *
 * What it claims, exactly: a text node becomes a layout object, and a measurement can
 * be asked of a `LayoutTextMetrics` and answered by a real font
 * (RoboLayoutTextFont.h). What it does **not** claim: that any particular pass
 * measured anything. A pass with no source installed measures every run as zero and
 * counts it (`LayoutTextStats.unmeasured`), and that is a legitimate build
 * configuration rather than a degraded one — which is why the *per-pass* answer is in
 * the stats record and this flag is only about the build.
 */
#define RoboLayoutTextLayoutImplemented 1

/**
 * 1 once this build produces **line boxes** a caller can read.
 *
 * A constant rather than a runtime decision for the same reason the other four are:
 * it is read by `LayoutGetCapabilities().lineBoxes` and by the suite that
 * asserts the flag against the code. What it claims, exactly: the inline pass
 * finalizes lines into the tree's arena, so after a pass there are line records with
 * geometry, membership and a back-link from each member. What it does **not** claim: a
 * baseline, a `line-height`, segmentation of a line into fragments, alignment, or a
 * font-derived line extent -- all of which are 10.7's or later, and the flag would be
 * lying if it meant them.
 */
#define RoboLayoutLineBoxesImplemented 1

/**
 * 1 once this build places participants **on a baseline**.
 *
 * A constant rather than a runtime decision for the same reason the other five are:
 * it is read by `LayoutGetCapabilities().baselines` and by the suite that
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
#define RoboLayoutBaselinesImplemented 1

/**
 * 1 once this build resolves a box's **width and height** from its declarations.
 *
 * A constant rather than a runtime decision for the same reason the other six are:
 * it is read by `LayoutGetCapabilities().sizing` and by the suite that asserts
 * the flag against the code. What it claims, exactly:
 *
 *   a declared px `width` and `height` reach the geometry of a block box and of an
 *   atomic inline, and a box's resolved width is what its children are laid out into;
 *
 *   an `auto` dimension has a stated answer -- the containing block's width, or the
 *   contents' extent for a height -- and a value this build cannot resolve is
 *   *recorded* as `UNRESOLVED` and resolved by that same rule;
 *
 *   a minimum and a maximum can be recorded and are applied through
 *   `LayoutConstraintsClamp`, and a pair that contradicts itself is reported
 *   rather than resolved in either's favour.
 *
 * What it does **not** claim: percentage resolution against a containing *dimension*
 * (there is none to resolve against), `min-content`/`max-content`/`fit-content`,
 * shrink-to-fit, `box-sizing`, or any contribution from borders. Each
 * is a later phase's, and `RoboLayoutSizing.h` names them where they would otherwise
 * be assumed.
 *
 * One thing 10.9 and 10.10 between them made precise rather than changed: the number
 * this layer resolves is the box's **own box as its declarations determined it**, which
 * under this build's `box-sizing: content-box` is the **content** box -- and the border
 * box a result carries is that plus the padding, derived by the padding layer rather
 * than resolved here. Margins change neither, which is why neither is an input to this
 * layer.
 */
#define RoboLayoutSizingImplemented 1

/**
 * 1 once this build turns a box's **margins** into the space it occupies.
 *
 * A constant rather than a runtime decision for the same reason the other eight are:
 * it is read by `LayoutGetCapabilities().margins` and by the suite that asserts
 * the flag against the code. What it claims, exactly:
 *
 *   four sides, each of them an `auto`, a **signed** px length or a declaration this
 *   build cannot resolve, read from a computed style and never parsed here;
 *
 *   a **used** value per side, resolved by one of two stated policies -- distribution
 *   of the leftover space for a block-level box in normal flow, and a used value of 0
 *   for an inline-level participant -- with the containing block's contents left over
 *   after the two horizontal margins available to the box's own `auto` width;
 *
 *   the box placed at its margin origin while its `size` stays its content box, and
 *   the extent a parent stacks by being the box's **margin box**;
 *
 *   an atomic inline's margins in its **advance** along the line and in the baseline
 *   it is placed on, which is its bottom margin edge.
 *
 * What it does **not** claim: margin collapsing of any kind -- adjacent margins add --
 * percentage margins (read, recorded unresolved and used as zero, because no goal
 * resolves a percentage yet), margins on a **non-replaced inline box** (CSS applies
 * none, and `LayoutObjectMargins` refuses the box rather than reporting zeros),
 * a margin on a replaced element or an out-of-flow box, or `box-sizing`. Each is a
 * later phase's or later positioning's, and `RoboLayoutMargin.h` names them where they
 * would otherwise be assumed.
 */
#define RoboLayoutMarginsImplemented 1

/**
 * 1 once this build turns a box's **padding** into the size it occupies and the room
 * its contents get.
 *
 * A constant rather than a runtime decision for the same reason the other nine are: it
 * is read by `LayoutGetCapabilities().padding` and by the suite that asserts the
 * flag against the code. What it claims, exactly:
 *
 *   four sides, each of them a **non-negative** px length or a declaration this build
 *   cannot resolve, read from a computed style and never parsed here -- and no `auto`,
 *   because the CSS grammar has none for this property;
 *
 *   a **used** value per side by one stated policy, which is the whole of it: padding
 *   neither distributes like a margin nor resolves against a containing block, so the
 *   same four declarations mean the same thing to every kind of box;
 *
 *   the box's **padding box** -- derived as the content box plus the padding, which is
 *   what a result's `size` carried until 10.11 and what the border box is now built
 *   from -- and the box's **content box** inset from its padding-box edges by
 *   `padding-left`/`padding-top`, under this build's `box-sizing: content-box` model,
 *   which is CSS's initial value;
 *
 *   an `auto` width filling what the margins **and** the padding left, because CSS's
 *   constraint equation has both terms, and an atomic inline's advance including its
 *   padding on the line.
 *
 * What it does **not** claim: `box-sizing: border-box`, which is the branch a later goal
 * adds at one place; percentage and relative-unit padding (read, recorded unresolved and
 * used as zero, because no goal resolves a percentage yet); and padding on a **transparent
 * inline box**, whose rectangle is the union of its contents and which this build does
 * not inflate. Each is a later phase's, and `RoboLayoutPadding.h` names them where they
 * would otherwise be assumed.
 *
 * One door moved in 10.11 and this note is where a reader finds out: the function that
 * computes the padding box was named `..._border_box` until a border existed, because it
 * was named for the box the module *hoped* to complete rather than for the box it
 * computes. It is `LayoutPaddingEdgesPaddingBox` now, and the arithmetic is
 * unchanged.
 */
#define RoboLayoutPaddingImplemented 1

/**
 * 1 once this build turns a box's **border** into the size it occupies, the room its
 * contents get, and the record a paint pass draws an edge from.
 *
 * A constant rather than a runtime decision for the same reason the other ten are: it is
 * read by `LayoutGetCapabilities().border` and by the suite that asserts the flag
 * against the code. What it claims, exactly:
 *
 *   twelve longhands, read from a computed style and never parsed here -- four widths
 *   (a px length, one of the three width keywords, or a declaration this build cannot
 *   use), four **styles** (9.15's typed vocabulary, all ten of them) and four
 *   **colours** (9.9's own record, carried unaltered, `currentColor` included);
 *
 *   a **used** width per side by one stated policy, and the used value is *not* the
 *   declared one: a `border-style` of `none` or `hidden` gives every side a used width of
 *   zero however `border-width` was written, which is CSS's own rule and the reason
 *   `CssBorderStyleGeneratesBorder` exists;
 *
 *   the **border box** -- what `LayoutResult::size` carries -- derived as the
 *   padding box plus the border, and the **padding box** width resolved by subtracting
 *   the borders from the containing block, so an `auto` width fills what the margins, the
 *   border *and* the padding left;
 *
 *   an atomic inline's advance including its borders, and the whole record -- used width,
 *   style and colour per side -- readable from the object, so a paint pass never re-reads
 *   a declaration to decide whether this build could use it.
 *
 * What it does **not** claim: `border-radius` or any corner geometry; `hidden`'s
 * border-collapsing meaning, which is a table rule this build has no tables for; logical
 * border properties; and any border on a **transparent inline box**, which takes none for
 * the reason it takes no margin and no padding. Percentage border widths are absent too,
 * and absent for a different reason than a percentage padding: the CSS grammar has no
 * percentage for `border-width` at all, so an `UNRESOLVED` width here is a relative unit,
 * a `calc()`, a negative length or the forbidden `auto` -- never a percentage this build
 * declined to resolve. Each absence is named in `RoboLayoutBorder.h` where it would
 * otherwise be assumed.
 */
#define RoboLayoutBorderImplemented 1

/**
 * 1 once this build turns a box's **`position`** into a displacement: where a box ends
 * up relative to where flow put it, without changing flow.
 *
 * A constant rather than a runtime decision for the same reason the other eleven are: it
 * is read by `LayoutGetCapabilities().positioning` and by the suite that asserts
 * the flag against the code. What it claims, exactly:
 *
 *   the **mode** read from a computed style (9.13's own vocabulary, carried rather than
 *   copied) and the four **insets** (`top`, `right`, `bottom`, `left`) read from the
 *   box model's offset shape -- each a signed px length, the `auto` keyword, or a
 *   declaration this build cannot use, with the signedness a margin has rather than the
 *   non-negativity a padding and a border have;
 *
 *   the **used displacement** by one stated policy: `left` wins over `right`, `top` wins
 *   over `bottom`, and an `auto` side says nothing -- deterministic, documented at the
 *   rule, and enough for every document that writes one side of a pair;
 *
 *   a result whose `position` is the **final** position (the flow position plus the
 *   displacement) while the object carries the displacement itself, so the flow position
 *   is `final - offset` and neither is guessed; a box's **size** and its **flow
 *   contribution** are both untouched, so the following sibling starts where it always
 *   would have.
 *
 * What it does **not** claim: `absolute` and `fixed` **behaviour** -- that is 14.0's, and the
 * flag for it is `RoboLayoutOutOfFlowImplemented`, which is where the containing block, the two
 * axes and the far-side anchor are described -- `sticky` (in flow, taking no offset, because
 * there is no scrollport to constrain against), no `z-index`, no stacking context, no paint
 * order, and no percentage inset (read, recorded unresolved and used as zero). This phase
 * displaces a box that is in flow; whether the box is in flow at all is the other one. A
 * displacement applies to a box the passes give a rectangle to; a **transparent inline
 * box** (whose rectangle is a union) and a **text run** (which is not a box) take none.
 * Each is named in `RoboLayoutPosition.h` where it would otherwise be assumed.
 */
#define RoboLayoutPositioningImplemented 1

/**
 * 1 once this build places a box **out of flow** against a containing block (14.0).
 *
 * A compiler constant rather than a runtime decision for the same reason the other fourteen
 * are: it is read by `LayoutGetCapabilities().outOfFlow` and by the suite that asserts both.
 *
 * What it claims, exactly:
 *
 *   the mode is read from a computed style (9.13's own vocabulary) and the two facts that
 *   separate the three in-flow modes from the two out-of-flow ones are asked of it rather
 *   than re-derived (`LayoutAbsoluteIsOutOfFlow`, `...IsViewportAnchored`);
 *
 *   the **containing block** is the nearest positioned ancestor's padding box, or the
 *   initial containing block for `fixed` and for an `absolute` box with no positioned
 *   ancestor -- decided by the pass's walk, which descends through static boxes because a
 *   static wrapper is not a containing block;
 *
 *   both axes are resolved by one policy: the near side (`left`/`top`) wins where it is
 *   declared, the far side (`right`/`bottom`) **positions the axis** where the near side says
 *   nothing -- a capability 10.12 did not have, and the reason the origin is a second call
 *   that knows the box's own size -- and two opposing insets with no definite declared
 *   dimension **size** the axis between them, which is `left: 0; right: 0`;
 *
 *   a declared dimension beside two present insets is reported as **over-constrained** rather
 *   than resolved in the far side's favour, and every declaration is counted (used, `auto`,
 *   or unusable) so that a document whose box is positioned with `top: 10%` says so.
 *
 * What it does **not** claim: `z-index`, stacking contexts and paint order (nothing is
 * reordered -- document order still decides, which is why the phase is named for *placement*
 * and not for stacking); `sticky` (in flow, taking no offset, as in 10.12, because there is
 * still no scrollport); `shrink-to-fit` (a box with `width: auto` and fewer than two insets
 * fills what is left, because this build has no intrinsic width to shrink to); percentages
 * (read, recorded unresolved and used as zero); `box-sizing: border-box`; an out-of-flow
 * **inline-level** box (the inline pass owns those participants and still places them on
 * their line); and the static-position rule CSS applies when neither side of an axis is
 * declared (this build states "the containing block's origin" instead).
 */
#define RoboLayoutOutOfFlowImplemented 1

/**
 * 1 once this build measures what happens when a box's contents do **not** fit.
 *
 * A constant rather than a runtime decision for the same reason the other twelve are: it is
 * read by `LayoutGetCapabilities().overflow` and by the suite that asserts the flag
 * against the code. What it claims, exactly:
 *
 *   the two **axes** read from a computed style -- `overflow-x`/`overflow-y` first, then
 *   CSS's `overflow` shorthand for an axis the longhand said nothing about, which is read
 *   rather than expanded because no phase in this build expands a shorthand -- each an axis
 *   whose declaration is a mode this build has, a declaration it cannot use, or nothing at
 *   all, with `visible` as the answer for the last two and the counters telling them apart;
 *
 *   a **used record** per measured box: two modes and four **extents**, each saying how far
 *   the contents reached *beyond* the padding box -- non-negative, measured from the box's
 *   own padding edge rather than the border's, and saturated rather than wrapped;
 *
 *   the phase's **recursion**: every placed child contributes its **region** -- its padding
 *   box grown by its own extents and clipped back on an axis it hides -- and its region
 *   already contains everything below it, so a measurement is O(children) and a child that
 *   clips cannot leak the contents it kept into an ancestor;
 *
 *   a **region** and a **clip rectangle** exposed for the phase that paints: the region is
 *   what the contents occupy, the clip rectangle is CSS's padding edge and is *refused*
 *   rather than answered when no axis clips;
 *
 *   and two **result flags**, one bit per axis, so a consumer that only needs to know
 *   whether a box's contents fit reads a bit rather than four numbers.
 *
 * What it does **not** claim: **scrolling** -- no scroll offset, no scrollport, no
 * scrollbar, no `overflow: auto` or `scroll` behaviour; **clipping** -- the boundary is
 * exposed and nothing rasterises anything, and display-list or compositor clipping is a
 * later phase's; CSS's **axis promotion** (`overflow-x: visible` beside a scrolling
 * `overflow-y` becomes `auto`), which exists for scrolling and is therefore absent here;
 * and `text-overflow`, ellipsis, `overflow-clip-margin` and `contain`, each a later value's.
 * Every absence is named in `RoboLayoutOverflow.h` where it would otherwise be assumed.
 */
#define RoboLayoutOverflowImplemented 1

/**
 * 1 once this build tracks which of its conclusions are still true.
 *
 * A constant rather than a runtime decision for the same reason the other thirteen are: it
 * is read by `LayoutGetCapabilities().invalidation` and by the suite that asserts the
 * flag against the code. What it claims, exactly:
 *
 *   a **state** per box -- four bits saying *what* is stale: the computed style changed in a
 *   way layout does not read, this box's own geometry, its descendants' layout, or its
 *   overflow extents -- with STYLE deliberately apart from the other three so that "needs a
 *   repaint" and "needs a relayout" are two questions with two answers;
 *
 *   a **reason** per box, closed and short, for a reader rather than for the algorithm: was
 *   this a geometry change, a displacement, a content change, the containing block, or
 *   "something below you";
 *
 *   a **classifier** over property *names*: `LayoutStyleEffectForProperty` answers
 *   "layout does not read it", "paint-visible only" or "feeds geometry", written out by
 *   hand rather than derived from RoboCSS's registry, because the question is what **layout**
 *   reads and only this module can answer it;
 *
 *   three **propagation policies**, each with its own shape: a box-model change marks the box
 *   with all three layout bits, its ancestors with SUBTREE and OVERFLOW, and the *following
 *   flow* after it -- transitively, because a later sibling moves when an earlier one reflows
 *   and the one after *it* moves when that one did; a **displacement** change marks the box
 *   and its ancestors' OVERFLOW and leaves the following flow clean, which is 10.12's contract
 *   that a relatively positioned box keeps its flow space; an **overflow-mode** change marks
 *   OVERFLOW alone and no geometry at all;
 *
 *   a **dirty root** -- the lowest common ancestor of the boxes whose own geometry is stale,
 *   which is the smallest subtree a relayout could start from -- and the **counts** a pass
 *   reports, so that "what was stale" and "what was done" are both readable;
 *
 *   the **pass contract**: a recomputation clears the layout bits and **leaves STYLE**, and a
 *   pass with no viewport clears nothing at all, because it computed no coordinate;
 *
 *   and one **decision**: `LayoutContextRunIncremental` skips the pass when nothing
 *   was invalidated, when no input changed and when the tree is the one it last ran on.
 *
 * What it does **not** claim: **partial layout**. The dirty root is computed and reported and
 * nothing starts from it -- this pass recomputes from the tree root exactly as it did in
 * 10.1, because the brief that asks for invalidation also forbids the incremental recursion
 * that would make it faster, and a build that guessed a dirty region would be quick and wrong.
 * There are no dependency **graphs**, no per-CPU or per-thread state, no layout *dirtiness* as
 * a performance heuristic, and no paint invalidation beyond the STYLE bit that a paint phase
 * would consume. Every absence is named in `RoboLayoutInvalidation.h` where it would
 * otherwise be assumed.
 */
#define RoboLayoutInvalidationImplemented 1

/**
 * 1 once this build resolves a CSS `font-family` to a face (12.17).
 *
 * A compiler constant rather than a runtime decision, read by the two doors that must not disagree --
 * `LayoutGetCapabilities().fontRequest` and the suite that asserts both -- exactly as the other
 * `RoboLayout*Implemented` macros are.
 *
 * What it claims, in one sentence: a computed style's font properties become a **value** record -- an
 * ordered, classified family list plus size, weight, stretch, slope and direction -- and that record
 * is offered, entry by entry and in author order, to a `LayoutFontProvider` the caller installs, whose
 * answer is recorded on the measured run (`LayoutTextRun.face`, `…fontFallback`).
 *
 * What it does **not** claim, and each of these is a feature a later phase owns: there is no font
 * database here (the provider is a callback; RoboLayout neither loads a face, reads a `cmap`, consults
 * coverage nor picks a fallback chain of its own), no per-scalar or per-script run splitting (one run,
 * one face -- the request carries the family list a later splitter would cut on), no metric synthesis
 * for a weight or a slope the face does not have, no `@font-face`, no web font and no device-scale
 * figure. `font-request` would be claiming those if it were named for "fonts are finished".
 */
#define RoboLayoutFontRequestImplemented 1

#endif /* ROBO_LAYOUT_VERSION_H */
