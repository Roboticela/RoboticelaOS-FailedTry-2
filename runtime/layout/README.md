# RoboLayout

Layout subsystem for Roboticela Runtime (**through Phase 10.1, architecture**).

**Version:** 0.1.0 — `ROBO_LAYOUT_API_FROZEN=0` (the surface grows for the whole
of Phase 10)

`ROBO_LAYOUT_GEOMETRY_IMPLEMENTED=0` — this build computes no coordinate.

Public RoboLayout never depends on Chromium, and it does not depend on paint,
render, compositing, JavaScript or the shell. It is the **first module in the tree
allowed to see RoboHTML and RoboCSS together**, which `DEPENDENCIES.md` permits
precisely here and nowhere below: the two are peers and must not know each other,
so whoever needs both sits above both.

## Scope

**10.1 (layout architecture).** The whole of this build, and deliberately the
whole of it: the phase exists to build the *shape* of a layout pass and to state
the boundaries the following fourteen goals fill in.

| Layer | What 10.1 delivers | What it does **not** do |
|---|---|---|
| tree | An owned arena of boxes, ids that are dense and tree-scoped, parent/child/sibling links by id, a reverse lookup from element to box | Edit a box, re-parent, reorder, or build incrementally |
| object | One box per element that generates one: its element handle, its **borrowed** computed style, its structural links, its depth, the computed `display` it was created under, and its result | Say what *kind* of box it is beyond "a box" |
| constraints | Available space, minimum and maximum, each either **known or unset** and each with an explicit flag; a validity predicate; and `clamp` | Resolve a percentage, fill an `auto`, size anything intrinsically, or apply `box-sizing` |
| result | Position and size, plus an explicit **state**: `NONE`, `UNRESOLVED`, `COMPUTED` | Compute a position or a size |
| context | One pass: the constraints it ran with, the viewport, the counters, the outcome, and the user-data seam a later goal hangs scratch state on | Lay anything out |
| builder | DOM tree in, layout tree out, with `display: none` elements generating no box and every other element generating one | Handle text, anonymous boxes, or fragments |
| bridge | The RoboHTML + RoboCSS adapter: RoboCSS's four element-host callbacks, one computed style per element, and the style source the builder reads through | Own a style context, a stylesheet, or a parser |

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

## What "the pass ran" is allowed to mean

`robo_layout_context_run` walks the tree in creation order, marks every box
`UNRESOLVED`, and counts what it saw. It computes no coordinate.

That is a statement about this build rather than a placeholder for one. A caller
that reads a result and finds `UNRESOLVED` has been told the truth; a phase that
returned `(0, 0)` instead would have been told a lie, and every consumer above —
paint, compositing, hit testing — has a decision to make about a box whose
geometry is not known. `robo_layout_geometry_is_implemented()` answers `0`, and
`robo_layout_get_capabilities().geometry` reports `0` for the same reason.

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
| `docs/Tests.md` | The seven suites, what each asks, and what the phase found |

## Building and testing

```sh
# from runtime/
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Test
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Release -Test
./Out/RoboTest.host.exe --filter layout.       # 71 cases (+2 demo cases)
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
  read through 9.12's own accessor.
- No `position`, `margin`, `padding` or `border` arithmetic, and no containing
  block. Those values are readable from the borrowed style; resolving them is
  10.3 onwards.
- No paint, no rendering, no windowing, no display.
- No formatting contexts. `ROBO_LAYOUT_OBJECT_BOX` is the only object kind this
  build can produce, because it is the only distinction the tree can make today.
