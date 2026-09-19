# RoboLayout

Layout subsystem for Roboticela Runtime (**through Phase 10.8, width/height**).

**Version:** 0.8.0 — `ROBO_LAYOUT_API_FROZEN=0` (the surface grows for the whole
of Phase 10)

`ROBO_LAYOUT_GEOMETRY_IMPLEMENTED=1` — this build computes coordinates, in both
directions.

`ROBO_LAYOUT_FORMATTING_CONTEXTS_IMPLEMENTED=1` — this build classifies every box
into the context it participates in and the context, if any, it establishes.

`ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED=1` — this build stacks block-level boxes
down the page and gives each one a position and a size.

`ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED=1` — this build places an inline formatting
context's participants along a line, wrapping when one does not fit.

`ROBO_LAYOUT_TEXT_LAYOUT_IMPLEMENTED=1` — this build measures a run of text through
a font, and a text node is a participant of the line it was placed on.

`ROBO_LAYOUT_LINE_BOXES_IMPLEMENTED=1` — this build produces line boxes a caller
can read back: identity, finalized geometry, and which objects went on them.

`ROBO_LAYOUT_BASELINES_IMPLEMENTED=1` — this build gives every line a **floor** —
an ascent, a descent and the kind they were derived from — and places each of its
members on it rather than at the line's top edge.

`ROBO_LAYOUT_SIZING_IMPLEMENTED=1` — this build resolves a box's **width and
height** from its declarations, and a box's resolved width is what its children are
laid out into. A percentage is read and recorded as `UNRESOLVED` rather than
resolved against a containing dimension, and intrinsic sizing does not exist yet.

Public RoboLayout never depends on Chromium, and it does not depend on paint,
render, compositing, JavaScript or the shell. It is the **first module in the tree
allowed to see RoboHTML and RoboCSS together**, which `DEPENDENCIES.md` permits
precisely here and nowhere below: the two are peers and must not know each other,
so whoever needs both sits above both.

## Scope

**10.1 (layout architecture).** The phase exists to build the *shape* of a layout
pass and to state the boundaries the following fourteen goals fill in.

**10.2 (formatting contexts).** Which boxes lay each other out, and in which
direction. A box now carries two context ids — where it *participates* and what it
*establishes* — and the pass that walks the tree answers both, so that 10.3 and
10.4 each start with a question answered rather than a question to re-derive. A
context is a grouping, and no member's geometry came from it; that is 10.3's.

**10.3 (block layout).** Where each block-level box goes and how big it is. The
first phase that computes a coordinate, and its one structural claim is that
**containment is not context**: a box is placed inside its *parent*, and its
*formatting context* decides which pass stacks its contents. A block box fills its
containing block's content width, its block-level children stack below one another
from its content origin, and its height is a declared `px` length where this build
can resolve one and the stacked extent of its children otherwise.

**10.4 (inline layout).** The other direction, and it is a *branch* of the same
pass rather than a second one: a box that owns an **inline** formatting context
has its contents placed along a preliminary line, wrapping when one does not fit.
Its one structural claim is that an inline-level box is **atomic** or
**transparent** and never a degree of the same thing — an `inline-block` is
opaque to the line and sized before it is placed; an `inline` is transparent to it,
so its children are participants of the *same* context and its own rectangle is the
**union** of what they reached. The same pass therefore reports two records, and
`boxes_computed` is their two placement counts added together.

**10.5 (text layout).** The width a `<span>` holding only text produces. A text
node is not an element and has no `display`, so it is the one thing the object
vocabulary genuinely had to widen for (`ROBO_LAYOUT_OBJECT_TEXT`); it borrows its
parent element's computed style, derives its display as inline, and becomes an
**ordinary participant of the line** rather than a special case in the inline
pass. Where the width comes from is a **seam**: a callback a caller installs, with
a RoboFont-backed implementation supplied beside it. A pass with no seam measures
every run as zero and **counts it**, which is a legitimate way to run the module
and not a failure.

**10.6 (line boxes).** The preliminary line becomes a record. The structural claim
is that **the line being built is not a line box**: construction state stays on the
pass's stack and an immutable record is appended to the tree's line arena once, at
finalization — so "keep finalized results separate from temporary state" is the
shape of the types rather than a rule anyone has to remember. A line's **members
are the objects it placed**, which is not the same set as the context's
participants: a transparent inline box *with* contents encloses members without
being one, because its rectangle is a union and a union can span lines.

**10.7 (baselines).** Where the line gets a floor, and where members stop sitting
at its top edge. The structural claim is a **split of one fact across two
moments**: a participant's own metrics are recorded when it is *placed*, because
that is when the measurement is in hand, but its final y is written when the line
is *finalized*, because a line's ascent is a maximum that is not final until the
last member arrives — so the correction is one walk of the member chain at the
moment the line's geometry is written anyway. The line gap a font reports becomes
CSS half-leading here (the seam's one question stays "how wide is this run?"),
everything that is not a text run takes a documented **bottom-edge fallback**, and
the two coordinate spaces a baseline is read in — the line's own and its owner's —
are named by two doors rather than left to a reader to infer.

**10.8 (width/height).** Where a declaration becomes a size. The structural claim is
a **split of one number into two uses**: a box's resolved width is what its children
are laid out *into*, not merely what it is — so the width resolves first (it depends
on nothing), the children go in at it, and the height follows (it depends on them).
For an `auto` width the two numbers are the same, because `auto` *is* “fill the
containing block”, which is why every earlier expectation about a box with no
declared width still holds. A dimension is **three kinds** (`auto`, a definite px
length, and *declared but unresolvable* — a percentage or a relative unit, which
resolves like `auto` today and will not when a containing dimension is known), the
bounds are applied through 10.1's own clamp so “min wins over max” is still decided
in one place, and both directions ask `robo_layout_object_sizing` the same question.

| Layer | What it delivers | What it does **not** do |
|---|---|---|
| tree | An owned arena of boxes, ids that are dense and tree-scoped, parent/child/sibling links by id, a reverse lookup from element to box | Edit a box, re-parent, reorder, or build incrementally |
| object | One box per element that generates one: its element handle, its **borrowed** computed style, its structural links, its depth, the computed `display` it was created under, and its result | Say what *kind* of box it is beyond "a box" |
| constraints | Available space, minimum and maximum, each either **known or unset** and each with an explicit flag; a validity predicate; and `clamp` | Resolve a percentage, fill an `auto`, size anything intrinsically, or apply `box-sizing` |
| result | Position and size, plus an explicit **state**: `NONE`, `UNRESOLVED`, `COMPUTED` — the per-box authority on whether its numbers are real | Refine a previous pass's numbers rather than replacing them |
| context | One pass: the constraints it ran with, the viewport, the counters, the outcome, and the user-data seam a later goal hangs scratch state on | Lay out a direction whose context it did not record a viewport for |
| builder | DOM tree in, layout tree out, with `display: none` elements generating no box, every other element generating one, and a text node generating a TEXT object unless it is collapsible whitespace | Handle anonymous boxes or fragments |
| bridge | The RoboHTML + RoboCSS adapter: RoboCSS's four element-host callbacks, one computed style per element, and the style source the builder reads through | Own a style context, a stylesheet, or a parser |
| formatting context | The four kinds, the record, and the classification as a **pure function of a display value**, a child composition and a root flag; the per-tree arena of contexts; the two ids on each box | Lay anything out, measure anything, or create an anonymous box |
| block layout | A real position and size for every block-level box in a block-direction context: width **resolved through the sizing layer**, children stacked from the content origin **at that width**, height declared or content-derived, and a record of what was placed and what was not | Resolve a percentage against a containing dimension, a margin, a padding or a border; place an inline box, a line, or an unreached box; build an anonymous wrapper |
| inline layout | A real position and size for every participant of an inline formatting context: a cursor that advances, a line that moves down when a participant does not fit, a union for a transparent box, and an atomic box's contents laid out inside it by the block pass | Collapse whitespace, break Unicode lines, fragment a participant across two lines, or build the anonymous wrapper a block-level box in an inline context needs |
| text layout | A code-point range type, the style a font needs resolved from computed CSS, the **measurement seam**, a run record, and a RoboFont-backed measurer that sums advances in font units and scales once | Shape, kern, ligate, reorder for bidi, break lines, or rasterise a glyph |
| line box | The record of one line — identity, ordinal within its context, finalized rectangle in its owner's content-box space, the width it had to fit, the **baseline** it was given (ascent, descent, kind), and the objects it placed — read back through the tree's arena | Align, fragment, or paint a line; derive its height from a resolved `line-height` or from an inline-block's last in-flow line box |
| sizing | A box's dimensions as a question and an answer: the available extent per direction with whether it is known at all, the declared dimension in three kinds, the bounds, the contents' extent, and the resolved size with a **source** and a clamped flag per dimension — plus the one seam a computed style becomes a size through | Resolve a percentage against a containing *dimension*, size intrinsically (`min-content`, `max-content`, `fit-content`, shrink-to-fit), apply `box-sizing`, or add the contribution of margins, padding and borders |

## The one structural decision

RoboLayout does not use the DOM as its tree.

The two trees do not have the same members. An element with `display: none` is a
DOM node and generates no box; a text node is a DOM node and will generate
something in 10.5 that is not an element box; a future `::before` is a box with no
element behind it. A pass that walked the DOM would re-decide, at every step,
which nodes it was allowed to look at — and every later goal (line breaking,
anonymous blocks, fragments) would add another such decision in another place.

So the tree is built **once**, by the builder, and the pass walks boxes and only
boxes. The DOM stays the DOM; RoboLayout references it.

## The second structural decision: two context ids, not one flag

The tempting model is one fact per box — "this box *is* a block formatting
context" — and it is wrong in a way that costs a later phase a rewrite. A box has
two independent relationships to formatting contexts:

```text
box.panel   display: inline-block
   │
   ├── participates in   the root context        (where its own layout happens)
   └── establishes       a block context         (how its *contents* are laid out)

box.body    display: block, holds a block child
   │
   ├── participates in   the root context
   └── establishes       nothing — it is *transparent*
```

Talking about transparency takes three cases (establishes, transparent, inline),
and a single boolean on the box can hold two. So `robo_layout_object_t` carries
`context` and `context_owned`, and a child resolves its own participation with one
expression: *the parent's owned context if it has one, otherwise the parent's own*.

Transparency is the case worth getting right early. A plain block box whose
children are all block-level establishes nothing, which is what makes a margin
collapse *through* it and a float escape it. A build that made every block box a
barrier would pass every test this phase writes and make 10.9 and 10.13 impossible.

## The one rule the builder applies

```text
an element generates a box unless its computed display says otherwise
```

Everything else is structure, and the two lines that matter are:

- **children are elements, not nodes.** The DOM stores the newline between two
  elements as a text node, so a builder that walked *nodes* would produce a tree
  whose sibling order was half whitespace. Asking the DOM for element children is
  what makes `+` and `:first-child` mean the same thing here as they do in the
  selector matcher.
- **the root box is the document element**, not the document node. A document is
  not an element, generates nothing, and had a box of its own it would be a box
  with no style and no parent.

## The third structural decision: containment is not context

10.2 called a block container holding a block-level child "transparent", and the
word invites one dangerous reading — *not in the chain*. If a child were placed
inside the nearest box that *establishes* a context rather than inside its parent,
a grandchild's origin would be the grandparent's, and a document whose `<body>` is
transparent would look laid out while being wrong about every nested box.

So the two questions are answered from two different places, and 10.3's header
argues it at length:

```text
what am I laid out inside?      my parent box          the parent link, always
which direction stacks my
  contents?                     my formatting context  fc_kind_is_block_container()
```

`layout.block.context_gate` makes the second claim impossible to break over every
box in a tree: a block-level box is never in a non-block-direction context, and a
box in an inline context is never placed by the block pass.

## The fourth structural decision: atomic is not a degree of transparent

10.4's pair, and it is the same *shape* of decision as the one above — two
questions that look like one. An inline-level box comes in two kinds, and the
difference is not "how much does it participate" but *what its contents belong
to*:

```text
box.span   display: inline-block   established a context (flow-root)
   │
   └── opaque to the line: it occupies a run, and what is inside is laid out
       INSIDE it — which is why its width has to be known before it is placed,
       and why it is the one participant the wrapping rule can act on

box.em     display: inline        established nothing
   │
   └── transparent to the line: its children are participants of the SAME
       context, placed at the same cursor; its own rectangle is the UNION of
       what they reached
```

Reading that the wrong way round is the expensive mistake both times: treat a
`span` as opaque and it is placed with no width — nothing here measures text — so
everything inside is dropped; treat an `inline-block` as transparent and its
contents stack in the outer line, producing plausible numbers in the wrong
coordinate space. So atomicity is derived from the two facts the tree already
carries — the box's outer display, and whether it established a context — and there
is no "is this an inline-block" test in this module, because that question is
9.12's.

## The fifth structural decision: the line being built is not a line box

10.6's, and the counterpart one level down. Formatting contexts taught the module
that what a pass *concludes* belongs in an arena on the tree, released wholesale by
the next pass. A line box is a conclusion too, but the temptation is stronger here,
because a line is built incrementally and *feels* like a record being appended to:

```text
the tempting model
   open a line box → mutate it as participants are placed → close it

this build
   the open line is four numbers on the pass's stack;
   a record is written ONCE, at finalization, and appended to the tree's arena
```

The difference is not tidiness. In the tempting model there is a window during which
a line box exists and is wrong — addressable, half-built, with a cursor that will
change — and every later phase that reads a line has to know not to read it then.
Here there is no such window, and no door that mutates a line, because there is no
code that would need one. `robo_layout_line_box_t` has no cursor, no member head to
append to, and no "so far" field, so the requirement is met by the shape of the
types and `state` is a fact rather than a promise.

Members are a **chain in a shared arena** rather than a range, and that too is
forced by the recursion rather than chosen: laying out an atomic inline recurses
into the block pass for its contents, and those contents build lines and members of
their own before the atomic inline is placed on its parent's line. So the
placements of one line interleave with another's, each member names the next member
of *its* line, and reading is O(index) — which the door's own note says rather than
leaving a caller to infer an array.

## The sixth structural decision: one fact, two moments

10.7's, and it is the subtlest of the first six because both halves of it look like an
implementation detail for the same function. A member's **metrics** are recorded when
it is placed, because that is when the measurement is in hand. A member's **final y**
is written when the line is finalized, because the line's ascent is a *maximum* over
members and is not final until the last of them has arrived:

```text
the tempting model
   place a member at its final y
     -> wrong the moment a taller sibling follows it

the tempting fix
   place every member at the line's top, forever
     -> 10.6, and right only when the members share a font

this build
   place -> record the member's metrics and fold the line's maxima
         -> at finalization, one walk of the member chain writes each y
```

The third row is only available because of the fifth decision: a line is written
**once**, so there is exactly one moment at which its members' y can be corrected,
and that moment already exists. The walk that fills each member's back-link is the
same walk that writes its y, because "this member's baseline is the line's" and "this
member is on this line" are one event.

The same phase moved the **union** walk, and for the same reason: a transparent box's
rectangle bounds its contents' positions, so it cannot be computed until those
positions are final. The rule is unchanged — a box is the bounding box of what it
contains — and only *when* it is computed moved, from each box's close to a post-order
walk over the finished context, with the rebasing pass following it because the rebase
subtracts the origin the union just wrote.

## The seventh structural decision: one number, two uses

10.8's, and it is the only one of the seven that changes an answer the module was
already giving. 10.3 read a box's width once and used it twice — as the box's own
size and as the width its children were laid out into — and the two uses are the same
number only while nothing can size a box:

```text
the tempting model
   resolve the box's size -> then lay its children out at the containing block's width
     -> a 300px card whose child is 800 wide

this build
   resolve the WIDTH -> lay the children out AT IT -> resolve the HEIGHT (from them)
```

The split is the phase, and it is why the sizing layer has **two dimension
resolvers** beside the pair: an atomic inline's `auto` height is its own contents'
extent, so its width has to be settled before they are walked. A single door would
have made that caller either lie about contents it does not have yet or resolve
twice and discard one answer.

For an `auto` width the two uses agree — `auto` *is* "fill the containing block" —
which is why the seven structural decisions can share the recursion without any of
the earlier ones moving.

## What "the pass ran" is allowed to mean

There are two passes now, and they answer different questions about the same tree.

`robo_layout_context_run` classifies every box and then — if the context recorded a
viewport — runs layout, in both directions, as one pass. A box holds a
`robo_layout_result_t` whose **state** is the per-box authority:

| State | Means |
|---|---|
| `NONE` | no pass has run over this box |
| `UNRESOLVED` | a pass ran and did not give this box geometry |
| `COMPUTED` | a pass placed this box, in one direction or the other, and its numbers are real |

That a box is `UNRESOLVED` is a *statement*, not a placeholder for one. A caller
that reads a result and finds it unresolved has been told the truth; a phase that
returned `(0, 0)` instead would have been told a lie, and every consumer above —
paint, compositing, hit testing — has a decision to make about a box whose geometry
is not known.

The capability fields are deliberately the wider ones. `geometry` says **the build
computes coordinates**, not that a particular box has one: a box participating in a
block-direction context that is itself inside an unreached box has none, and a
`<span>` holding only text is zero-wide because nothing here measures text.
`layout.conformance.no_viewport` asserts the two claims side by side over one tree —
`geometry == 1` while nothing was placed — which is the conflation the per-box state
exists to make impossible.

## Geometry is borrowed, not defined

RoboLayout has no geometry types of its own. `robo_layout_point_t`,
`robo_layout_size_t`, `robo_layout_rect_t` and `robo_layout_rect_f_t` are aliases
of RoboGraphics' canonical pixel geometry, because a second point/size/rectangle
would be the worst kind of duplication: two types that agree today and differ
subtly in a year, with paint converting between them at the one boundary where a
mistake is a visible artifact.

The aliases exist so a layout signature reads as layout — `out_rect` cannot be
mistaken for a device rectangle — while an implementation passes one object to a
paint phase with no conversion.

## The seam that keeps the builder testable without CSS

The builder reads computed styles through a **callback**, not a
`robo_css_computed_style_t *` argument:

```c
typedef const robo_css_computed_style_t *(*robo_layout_style_fn)(
    void *userdata,
    const robo_html_node_handle_t *element);
```

A suite that wants to prove "a `display: none` element generates no box" or
"children are linked in document order" has no business standing up a style
context, a property table, a registry and a sheet to do it. What a caller does
*not* get to do is skip CSS and still claim the boxes are styled: a source that
answers nothing produces boxes with no style and the `STYLE_MISSING` flag, which
is a fact a later formatting context checks rather than an accident.

The real supplier is `RoboLayoutDom.h`, and it owns one computed style per element
so the tree can borrow them.

## Public API

| Header | Responsibility |
|---|---|
| `RoboLayout.h` | The umbrella: lifecycle, version, capabilities |
| `RoboLayoutTypes.h` | Object identity, the object-kind vocabulary, diagnostic domains |
| `RoboLayoutGeometry.h` | The aliases to RoboGraphics geometry |
| `RoboLayoutObject.h` | One box, and the read side of every field |
| `RoboLayoutTree.h` | The arena, the links, the lookup, `add_object` |
| `RoboLayoutConstraints.h` | Available/minimum/maximum, each known or unset; `clamp` |
| `RoboLayoutSizing.h` | One dimension in three kinds, the sizing record (available, declared, bounded, contents, resolved, with a source per dimension), the two resolvers and the pair, and the one seam a computed style becomes a size through |
| `RoboLayoutResult.h` | Position, size, flags, and the state that says which are real |
| `RoboLayoutContext.h` | One pass, its stats and its outcome |
| `RoboLayoutFormattingContext.h` | The four kinds, the record, and the pure classifier |
| `RoboLayoutBlock.h` | The block pass, its preconditions, and the record it leaves |
| `RoboLayoutInline.h` | The inline direction: the record it leaves, and the doctrine of atomic versus transparent |
| `RoboLayoutText.h` | Ranges in code points, direction, the resolved font inputs, the run record, the measurement seam, and the text counters |
| `RoboLayoutTextFont.h` | The RoboFont-backed measurer — cmap + hmtx, summed in font units and scaled once |
| `RoboLayoutLineBox.h` | One finalized line: the record, the state vocabulary, the baseline it carries, and the read side |
| `RoboLayoutBaseline.h` | The three baseline kinds, the metrics record, its arithmetic, and the two rules that produce it (a run's metrics; the bottom-edge fallback) |
| `RoboLayoutStyle.h` | The style-source seam |
| `RoboLayoutBuilder.h` | DOM document in, layout tree out |
| `RoboLayoutDom.h` | The RoboHTML + RoboCSS bridge that supplies the styles |
| `RoboLayoutDiagnostics.h` | The caller-owned diagnostic record |
| `RoboLayoutVersion.h` | Version pin and the capability constants |

## Lifecycle

```text
robo_layout_init()
       │
       ├── robo_layout_tree_create()
       │        │
       │        └── robo_layout_tree_build_from_document(tree, document,
       │                                                   options, report, diag)
       │                     ▲
       │                     │ options.styles
       │            robo_layout_dom_style_source(bridge, &source, NULL)
       │                     ▲
       │            robo_layout_dom_create(context, document, &bridge, NULL)
       │            robo_layout_dom_refresh(bridge, NULL)
       │
       ├── robo_layout_context_create()
       │        ├── robo_layout_context_set_viewport(size)
       │        └── robo_layout_context_run(context, tree, diag)
       │                 │  builds the context tree, classifies every box, and
       │                 │  lays both directions out
       │                 │
│                 ├── robo_layout_block_layout(tree, viewport, &block, &inline, &text, diag)
│                 │        └── robo_layout_inline_walk_contents(walk, owner, width, &extent)
│                 │                 ├── robo_layout_text_measure_node(node, style, metrics, &run)
│                 │                 │        └── robo_layout_text_font_measure(font, ...)   [the seam]
│                 │                 ├── layout_inline_finalize_line(walk, flow)
│                 │                 │        └── the line record + its member chain
│                 │                 └── robo_layout_block_walk_children(walk, atomic, w, &ext)
│                 │
│                 ├── robo_layout_tree_root_context(tree, &id, diag)
│                 ├── robo_layout_tree_context(tree, id, &fc, diag)
│                 ├── robo_layout_tree_line_count(tree)
│                 ├── robo_layout_tree_line(tree, id, &line, diag)
│                 ├── robo_layout_tree_line_member(tree, id, index, &node, diag)
│                 └── robo_layout_object_line(tree, node, &line_id, diag)
       │
       └── destroy in any order, except that the tree goes before the bridge
robo_layout_shutdown()
```

RoboLayout owns no global state, so `robo_layout_init` is a state transition with
nothing that can fail beyond being called twice. It is here because every module
in the runtime has the same lifecycle, and a caller should not have to know which
ones are special.

## One lifetime rule a compiler cannot check

**A tree built from a bridge's style source must be destroyed before the bridge.**
The tree borrows the bridge's computed styles, so destroying the bridge under it
leaves the tree pointing at freed memory. It is written down in `RoboLayoutDom.h`,
asserted by `layout.dom.lifecycle`, and it is the only ordering constraint in the
module.

## Documentation

| Document | Covers |
|---|---|
| `docs/Architecture.md` | Why the tree is separate, the builder's rule, the pass contract, the seams, and the ownership model |
| `docs/FormattingContexts.md` | The two-context model, the classification rules, ownership and depth, invalidation, and what 10.3–10.13 will consume |
| `docs/BlockLayout.md` | Containment versus context, the width and height rules, what the pass declines to place, determinism and invalidation, and what 10.4–10.13 will consume |
| `docs/InlineLayout.md` | Atomic versus transparent, where a participant's width comes from, the wrapping rule and why a preliminary line is not a line box, rebasing, and why there is no second entry point |
| `docs/TextLayout.md` | Text as an ordinary participant, ranges in code points, direction as metadata, the measurement seam and its three answers, the four facts a font needs, and the builder's second rule |
| `docs/LineBoxes.md` | Construction state versus the record, members as placements, the space a line is in, the lifecycle the pass runs, and what the block pass gets from the lines |
| `docs/Baselines.md` | Where the member's metrics are written versus where the line's floor is final, half-leading from the run's gap, the bottom-edge fallback, the two coordinate spaces, and why the union walk had to move |
| `docs/Sizing.md` | Why a resolved width is what the children are laid out into, the three dimension kinds, per-direction availability, where the bounds come from (and the both-dimensions rule), the two readers over one classification, and the three earlier assertions the phase updated |
| `docs/Tests.md` | The fourteen suites, what each asks, and what each phase found |

## Building and testing

```sh
# from runtime/
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Test
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Release -Test
./Out/RoboTest.host.exe --filter layout.       # 194 cases (+2 demo cases)
```

`Tools/Build.ps1 -Verify` additionally scans `Include/RoboLayout`,
`Source/RoboLayout` and `layout/` for a Chromium include and for a dependency on
any higher layer — paint, render, compositing, JS, the shell, or a window/display
module, which sits below layout in the graph but has no business in a layout pass.

## Boundaries

- No Chromium headers, sources, libraries or build outputs. Chromium's
  `LayoutObject` / `LayoutBox` / constraint models are temporary technical
  reference only.
- No CSS parsing, no property names, no stylesheet, no normalisation. Display is
  read through 9.12's own accessors, including the block-level predicate — there is
  deliberately no second one in this module.
- No **percentage resolved against a containing dimension**, no `min-content` /
  `max-content` / `fit-content`, no shrink-to-fit and no `box-sizing`. A declared
  `width` or `height` is honoured where it is a computed `px` length; a percentage and
  a relative unit are recorded as `UNRESOLVED` and take the `auto` path rather than a
  guess. The bounds path *is* live — it is applied through 10.1's clamp and tested
  end to end — but no property RoboCSS registers feeds it yet, so a document can
  declare `min-width` and change no number here.
- No `margin`, `padding` or `border` arithmetic. A box's content box *is* its border
  box until 10.9–10.11, which is why those declarations can be present in a document
  and change no number here.
- No paint, no rendering, no windowing, no display. The viewport is an argument.
- No **shaping**, no kerning, no ligature, no bidi reordering, no Unicode line
  breaking and no whitespace collapsing. A run's width is the sum of its glyphs'
  advances, which is what an unshaped run is. A participant too wide for its line
  **overflows** rather than fragmenting.
- No `line-height` **as a resolved used value**, and no `vertical-align` in any of
  its forms. 10.7 gave every line a floor and placed its members on it: the ascent is
  the members' maximum ascent, the descent their maximum descent, the height their
  sum, and a member's y is `origin.y + (line.ascent - member.ascent)`. What that is
  *not* is a `line-height` computation — a line's extent is the extent its metrics
  span — and the fallback for everything without font metrics is the documented
  **bottom edge**, not an inline-block's last in-flow line box, which is a later
  phase's rule and is named where it would otherwise be assumed.
- No **text measurement of its own**. The seam is a callback and the RoboFont-backed
  measurer is one implementation beside it; a pass with no seam measures every run
  as zero and counts it (`robo_layout_text_stats_t.unmeasured`), which is a
  legitimate way to run the module rather than a degraded one.
- No **anonymous box**, and no automatic inner inline context for an atomic inline.
  A block-level box inside an inline one is counted as `block_level_declined`
  rather than placed inside something with no geometry — that box is what the
  anonymous wrapper will be for.
- No positional, float, overflow or fragmentation behaviour. `position` is
  readable from the borrowed style and resolved by nothing; the result's two
  overflow flags stay 0 even when a declared height is smaller than its content.
- No `flex`, `grid`, `table`, `ruby` or `multicol` context, and no enumerator for
  any of them. `ROBO_LAYOUT_OBJECT_KIND_COUNT` is three — `NONE`, `BOX` and `TEXT`,
  and 10.5 is the phase that widened it, with the no-property-to-derive-from
  justification 10.1 wrote down — and `ROBO_LAYOUT_FC_KIND_COUNT` is four.
- No **line-box** alignment, `text-align` or painting. A line is a rectangle, a
  baseline and a membership list; `ROBO_LAYOUT_BASELINE_KIND_COUNT` is three —
  `NONE`, `TEXT` and `BOX` — and `NONE` is the one kind the validity predicate
  refuses, which is what keeps "not placed" from reading as "placed with no extent".
