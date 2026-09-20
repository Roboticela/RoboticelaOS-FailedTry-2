# RoboPaint

Paint subsystem for Roboticela Runtime (**through Phase 11.3, display lists**).

**Version:** 0.3.0 — `ROBO_PAINT_API_FROZEN=0` (the surface grows again as Phase 11
continues)

`ROBO_PAINT_DRAWING_IMPLEMENTED=0` — this build produces an ordered list of operations,
and a render-ready display list above it, and puts no colour on a surface.
`robo_paint_drawing_is_implemented()` answers 0 with it, so a caller cannot mistake "the
pass ran" for "the pixels are there".

RoboPaint is the fifth module in the layout stack: it sits directly above RoboLayout and
directly below RoboRender (`DEPENDENCIES.md`). RoboLayout answers **where every box is**;
RoboPaint turns those answers into a **sequence of work** a rasteriser will one day
execute, and 11.3 turns that sequence into the **artifact** a rasteriser consumes.

```text
HTML / DOM → RoboCSS → RoboLayout → RoboPaint → operations → display list → RoboGraphics → Display
```

This phase stops at **RoboPaint → display list**; rasterisation is a later goal.

## The one structural decision

**A paint list is a record of what will be drawn, not a set of drawing instructions, and
it is produced by one deterministic traversal of the layout tree.** Three consequences
follow, and each is a decision rather than a detail:

- **The order comes from the tree, not from the list.** The list is flat and contiguous,
  with an explicit `BEGIN_BOX`/`END_BOX` pair per box, because a rasteriser walks once and
  keeps a clip stack. The shape of the tree is *recoverable* from the list by the `depth`
  each operation carries, so a nested structure would only have to be flattened before use.
- **An operation is emitted only when it draws.** A box with no background declaration
  emits no background operation; a side whose used width is zero emits no border
  operation. The list's length is a statement about the document rather than about the
  traversal, which is how "no speculative operation" stays true while the vocabulary stays
  small.
- **Paint derives nothing RoboLayout already concluded.** The border box, the two inset
  layers, the clip rectangle and the four used border widths all come from layout's own
  doors. The only arithmetic here is the two things layout cannot know: the accumulation
  that turns per-containing-block coordinates into one page space, and the intersection
  that turns a nested boundary into one clip.

## The second structural decision: two coordinate spaces, held together by an assertion

An operation carries the boxes in **paint coordinates** (one page space, ancestors
accumulated) *and* the containing block's content origin, so layout coordinates are
`boxes - origin` (`robo_paint_operation_local_boxes`). Carrying both is not redundancy to
be tidied away: a consumer with only the list cannot recover the other space without
re-walking the tree, and re-walking the tree to rediscover what layout concluded is exactly
the "recalculate in paint" this module exists to prevent. What makes the pair safe is that
the relationship is **checked** — `robo_paint_operation_local_boxes` must equal what
`robo_paint_boxes_of` reads straight out of layout, for every operation of every box, and
the suite asserts exactly that.

## The third structural decision: a clip is two intervals, not one rectangle

`overflow-x: hidden` beside `overflow-y: visible` is legal and means what it says: bounded
horizontally, free vertically. A single rectangle cannot express that — a consumer handing
its four numbers to a rasteriser would clip the axis the author left open. So
`robo_paint_clip_t` is **two axes**, each a mode plus a half-open interval, and the one case
where a rectangle is right (`robo_paint_clip_rect`) is a door that **refuses** rather than
answering otherwise.

## The fourth structural decision: a stale tree is refused, not painted

`robo_layout_tree_needs_layout` answers whether any box carries a layout bit, and a tree
that does is refused with `ROBO_PAINT_PASS_STALE` and an empty list. A list built from
half-computed geometry is a *wrong* list that draws boxes in the wrong places; a caller
that wanted a best-effort preview can run a layout pass first. This is the whole of the
phase's integration with 10.14.

## The fifth structural decision: the operation vocabulary splits in two

`NONE`, `BEGIN_BOX`, `BACKGROUND`, the four `BORDER_*` sides, `CONTENT`, `END_BOX`. There is no
`TEXT`, `IMAGE`, `CLIP`, `SAVE`, `RESTORE` or `OPACITY` enumerator, because a name with no code
behind it is a promise rather than a vocabulary. What makes them *expressible* is the record's
shape: an operation is a tagged record with a node, a depth, three rectangles and a clip, so
adding a kind is one enumerator, one name row and one case in the validity rule — never a
redesign of the list.

The vocabulary splits into **structural** records (`BEGIN_BOX`, `CONTENT`, `END_BOX`), emitted
for every placed box and carrying no colour, and **drawing** records (`BACKGROUND`, the four
`BORDER_*`), emitted only when they draw and carrying a colour a rasteriser can use. Structural
records give the sequence a regular skeleton; drawing records make the list's length a statement
about the document. `robo_paint_op_kind_is_structural` and `robo_paint_op_kind_draws` are the
partition.

## The sixth structural decision: the content boundary is explicit

A box's own records end at its **`CONTENT`** record, emitted for every placed box whether or not
it drew anything and whether or not it has children. It acts on the content box and states a
fact about the sequence rather than the document: this box's own painting is the contiguous group
`[BEGIN_BOX .. CONTENT]` and its subtree is `(CONTENT .. END_BOX)`, so a consumer reads the
boundary without inferring it from the next child's depth — and a childless box has one exactly
as a parent does. It is why the colour rule splits on *drawing* rather than on *marker*: `CONTENT`
is structural but is not a box marker.

## The seventh structural decision: invalidation stops at the build

10.14 asks a per-box question — *is this conclusion still true?* — and answers with a state
on the box. Paint invalidation asks a different question — *is this list still current?* —
and this phase's answer is not a mechanism: the list is **not a cache of conclusions**, it
is regenerated from the tree on every pass. What the phase hands a future system is its
*input*: `robo_paint_stats_t.style_only` counts the visited boxes whose only staleness is a
paint-visible change. There is no paint-invalidation constant, no dirty rectangle and no
per-list currency check.

## The eighth structural decision: the display list resolves the box model

`robo_paint_operation_t` says which rectangles a decision was made from -- the box model,
plus a kind -- and deliberately leaves one derivation to the consumer: the band a border
side occupies is `robo_paint_operation_side_band`, computed on demand. Phase 11.3 adds
`robo_display_command_t`, which says which **one rectangle to act on**, resolved:

| Paint operation | Display command | Rectangle |
|---|---|---|
| `BEGIN_BOX` | `begin-node` | border box |
| `BACKGROUND` | `draw-background` | border box |
| `BORDER_*` (four kinds) | `draw-border` (one kind) | the side's **band** |
| `CONTENT` | `content` | content box |
| `END_BOX` | `end-node` | border box |

Two things are worth stating so neither is mistaken for duplication. The **padding box
disappears** -- it was an input to the band derivation, and nothing above needs it once the
band is resolved. The **four border kinds collapse into one** -- the fact that
distinguished them is consumed into the band rectangle. So a rasteriser walks a display list
once and never consults the box model, and the coordinate space is unchanged (paint
coordinates, one page space).

## The ninth structural decision: the display list is *regular*, and the builder proves it

A paint list is a **container**: `robo_paint_list_append` validates each operation, so every
list is a list of records, but nothing sequences them -- a caller may append in any order.
`robo_paint_list_builder_build` turns the traversal's **convention** (one structural triple
per box, in nested order, with decorations before the content boundary) into an **invariant**
of the display list, and **refuses** a record list that does not obey it. A consumer of a
display list may therefore group commands by `depth` and by the `begin-node`/`content`/
`end-node` boundaries without re-deriving anything.

## Geometry is borrowed, not defined

There are no new coordinate types. `RoboGraphicsGeometry.h` defines the canonical pixel
geometry, RoboLayout re-exports it, and RoboPaint reads it — so the numbers a rasteriser
receives are the numbers RoboLayout wrote, with no conversion for a sign or an origin to
flip in.

## Public API

`Include/RoboPaint/RoboPaint.h` is the umbrella; the headers it includes are the surface:

| Header | Surface |
|---|---|
| `RoboPaintVersion.h` | Version macros, `ROBO_PAINT_API_FROZEN`, `ROBO_PAINT_DRAWING_IMPLEMENTED` |
| `RoboPaintTypes.h` | `robo_paint_op_kind_t` (8), `robo_paint_diagnostic_domain_t` (9), their predicates and name doors |
| `RoboPaintDiagnostics.h` | `robo_paint_diag_t` and the `ROBO_PAINT_DIAG` macro |
| `RoboPaintGeometry.h` | `robo_paint_boxes_t`, `robo_paint_clip_t`, `robo_paint_boxes_of`, `robo_paint_clip_intersect` |
| `RoboPaintOperation.h` | `robo_paint_operation_t` and its accessors, validity, side band and one-line writer |
| `RoboPaintList.h` | `robo_paint_list_t` — create/clear/append/at/operations/equal |
| `RoboPaintContext.h` | `robo_paint_context_t`, `robo_paint_stats_t`, `robo_paint_pass_outcome_t`, `robo_paint_context_run` |
| `RoboPaintDump.h` | `robo_paint_list_write` — the developer's text view |
| `RoboPaintDisplayCommand.h` | `robo_paint_display_command_t`, its 5-kind vocabulary, validity and writer (11.3) |
| `RoboPaintDisplayList.h` | `robo_paint_display_list_t`, `robo_paint_display_list_stats_t` — the render-ready artifact (11.3) |
| `RoboPaintDisplayListBuilder.h` | `robo_paint_display_list_builder_t` — the conversion and structural check (11.3) |
| `RoboPaintDisplayDump.h` | `robo_paint_display_list_write` — the developer's view of the display list (11.3) |

## Lifecycle

`robo_paint_init()` / `robo_paint_shutdown()` / `robo_paint_is_initialized()`, plus
`robo_paint_get_capabilities()`, `robo_paint_version_string()` and
`robo_paint_drawing_is_implemented()`. A context, a tree and a list each have their own
lifetime and **borrow nothing from each other**: a context owns no list, a list holds no
reference to the tree, and an operation has no pointers in it — so all three may be
destroyed in any order.

## One lifetime rule a compiler cannot check

`robo_paint_list_operations` borrows a contiguous array that moves when the list grows. It
is valid **until the next append**, which is a documented rule rather than a hazard,
because nothing in an operation points anywhere.

## Documentation

| Document | Contents |
|---|---|
| `paint/docs/Architecture.md` | The phase, the dependency position, the traversal, the coordinate model, the clip model, ownership, what 11.3 onwards may assume |
| `paint/docs/Operations.md` | The operation vocabulary, the structural/drawing split, the traversal contract, snapshot semantics — the precise order, the coordinate spaces and the clip accumulation |
| `paint/docs/DisplayList.md` | The display command, the resolved rectangle per kind, the builder's structural invariants, ordering, ownership and what is deferred (11.3) |
| `paint/docs/Tests.md` | Suite map (five suites, 65 cases), fixtures, what the suites assert, running, sanitizers, what 11.1–11.3 found |

## Building and testing

```bash
# from runtime/
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Verify -Test
./Out/RoboTest.host.exe --filter paint.
```

## Boundaries

RoboPaint must not depend on RoboWindow, RoboDisplay or RoboRender. It reads RoboLayout
through const doors and may use RoboGraphics geometry **only**.

- No rasterisation, no framebuffer presentation, no compositor, no GPU. `DRAWING` is 0.
- **No display-list *consumption*** — the display list exists as a render-ready artifact
  (`display_lists` is 1), and nothing rasters it, clips it or presents it yet. Rasterisation
  is a later goal; `raster` is 0.
- No **text** or **image** operations. A text run is visited and counted
  (`robo_paint_stats_t.text_runs`) and **not painted**: a run's rectangle is a line fragment
  with no box layers, and painting one means shaping and glyph positions, which a later
  phase owns. The count exists so "this document has text and none of it was painted" is a
  number a reader sees rather than a silence.
- No **opacity**, no transforms, no `border-radius`, no gradients, no antialiasing.
- No **actual clipping**. The clip is computed and carried; no raster op is emitted. No
  scroll clipping and no compositor clipping.
- No **paint invalidation** (11.11) and no **hit testing** (11.10) — the final positioned
  geometry is preserved for both, but neither is built here.
- No **`background-clip`** or **`background-origin`**: the painting area is the border box,
  which is CSS's own initial value rather than a simplification of it.
- No **`z-index`** or stacking context. The traversal is document order; a flat list with
  depths is what a later stacking pass will reorder.
