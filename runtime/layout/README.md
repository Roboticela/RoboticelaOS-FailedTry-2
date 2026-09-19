# RoboLayout

Layout subsystem for Roboticela Runtime (**through Phase 10.4, inline layout**).

**Version:** 0.4.0 — `ROBO_LAYOUT_API_FROZEN=0` (the surface grows for the whole
of Phase 10)

`ROBO_LAYOUT_GEOMETRY_IMPLEMENTED=1` — this build computes coordinates, in both
directions.

`ROBO_LAYOUT_FORMATTING_CONTEXTS_IMPLEMENTED=1` — this build classifies every box
into the context it participates in and the context, if any, it establishes.

`ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED=1` — this build stacks block-level boxes
down the page and gives each one a position and a size.

`ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED=1` — this build places an inline formatting
context's participants along a line, wrapping when one does not fit.

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

| Layer | What it delivers | What it does **not** do |
|---|---|---|
| tree | An owned arena of boxes, ids that are dense and tree-scoped, parent/child/sibling links by id, a reverse lookup from element to box | Edit a box, re-parent, reorder, or build incrementally |
| object | One box per element that generates one: its element handle, its **borrowed** computed style, its structural links, its depth, the computed `display` it was created under, and its result | Say what *kind* of box it is beyond "a box" |
| constraints | Available space, minimum and maximum, each either **known or unset** and each with an explicit flag; a validity predicate; and `clamp` | Resolve a percentage, fill an `auto`, size anything intrinsically, or apply `box-sizing` |
| result | Position and size, plus an explicit **state**: `NONE`, `UNRESOLVED`, `COMPUTED` — the per-box authority on whether its numbers are real | Refine a previous pass's numbers rather than replacing them |
| context | One pass: the constraints it ran with, the viewport, the counters, the outcome, and the user-data seam a later goal hangs scratch state on | Lay out a direction whose context it did not record a viewport for |
| builder | DOM tree in, layout tree out, with `display: none` elements generating no box and every other element generating one | Handle text, anonymous boxes, or fragments |
| bridge | The RoboHTML + RoboCSS adapter: RoboCSS's four element-host callbacks, one computed style per element, and the style source the builder reads through | Own a style context, a stylesheet, or a parser |
| formatting context | The four kinds, the record, and the classification as a **pure function of a display value**, a child composition and a root flag; the per-tree arena of contexts; the two ids on each box | Lay anything out, measure anything, or create an anonymous box |
| block layout | A real position and size for every block-level box in a block-direction context: width filled from the containing block, children stacked from the content origin, height declared or content-derived, and a record of what was placed and what was not | Resolve a declared width, a percentage, a margin, a padding or a border; place an inline box, a line, or an unreached box; build an anonymous wrapper |
| inline layout | A real position and size for every participant of an inline formatting context: a cursor that advances, a line that moves down when a participant does not fit, a union for a transparent box, and an atomic box's contents laid out inside it by the block pass | Measure text, build a line box, collapse whitespace, break Unicode lines, or build the anonymous wrapper a block-level box in an inline context needs |

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
| `RoboLayoutResult.h` | Position, size, flags, and the state that says which are real |
| `RoboLayoutContext.h` | One pass, its stats and its outcome |
| `RoboLayoutFormattingContext.h` | The four kinds, the record, and the pure classifier |
| `RoboLayoutBlock.h` | The block pass, its preconditions, and the record it leaves |
| `RoboLayoutInline.h` | The inline direction: the record it leaves, and the doctrine of atomic versus transparent |
| `RoboLayoutStyle.h` | The style-source seam |
| `RoboLayoutBuilder.h` | DOM document in, layout tree out |
| `RoboLayoutDom.h` | The RoboHTML + RoboCSS bridge that supplies the styles |
| `RoboLayoutDiagnostics.h` | The caller-owned diagnostic record |
| `RoboLayoutVersion.h` | Version pin and the two capability constants |

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
       │                 ├── robo_layout_block_layout(tree, viewport, &block, &inline, diag)
       │                 │        └── robo_layout_inline_walk_contents(walk, owner, width, &extent)
       │                 │                 └── robo_layout_block_walk_children(walk, atomic, w, &ext)
       │                 │
       │                 ├── robo_layout_tree_root_context(tree, &id, diag)
       │                 └── robo_layout_tree_context(tree, id, &fc, diag)
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
| `docs/Tests.md` | The ten suites, what each asks, and what each phase found |

## Building and testing

```sh
# from runtime/
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Test
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Release -Test
./Out/RoboTest.host.exe --filter layout.       # 125 cases (+2 demo cases)
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
- No declared `width`. A block box fills its containing block, which is CSS's
  `width: auto`; resolving `200px`, a percentage, `min-width` or `box-sizing` is
  10.8.
- No `margin`, `padding` or `border` arithmetic, and no `box-sizing`. A box's
  content box *is* its border box until 10.9–10.11, which is why those declarations
  can be present in a document and change no number here.
- No percentage, relative-unit, `min-height` or `max-height` resolution. A declared
  height is honoured only where it is a computed `px` length; anything else takes
  the `auto` path rather than a guess.
- No paint, no rendering, no windowing, no display. The viewport is an argument.
- No **line box**, no text measurement, no font metrics and no baseline, so a
  `<span>` holding only text is zero-wide and a container whose only content is
  text produces a line *count* of zero. Both are counted (`empty_contexts`,
  `textless`) rather than patched over with a guessed width.
- No **anonymous box**, and no automatic inner inline context for an atomic inline.
  A block-level box inside an inline one is counted as `block_level_declined`
  rather than placed inside something with no geometry — that box is what the
  anonymous wrapper will be for.
- No positional, float, overflow or fragmentation behaviour. `position` is
  readable from the borrowed style and resolved by nothing; the result's two
  overflow flags stay 0 even when a declared height is smaller than its content.
- No `flex`, `grid`, `table`, `ruby` or `multicol` context, and no enumerator for
  any of them. `ROBO_LAYOUT_OBJECT_KIND_COUNT` is still two — `NONE` and `BOX` —
  and `ROBO_LAYOUT_FC_KIND_COUNT` is four.
