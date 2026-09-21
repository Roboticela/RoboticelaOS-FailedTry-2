# RoboPaint

Paint subsystem for Roboticela Runtime (**through Phase 11.9, opacity**).

**Version:** 0.8.0 — `ROBO_PAINT_API_FROZEN=0` (the surface grows again as Phase 11
continues)

`ROBO_PAINT_DRAWING_IMPLEMENTED=1` since 11.4 — this build fills a display command's
rectangle with its colour into a RoboGraphics surface, through the CPU raster, and strokes a
text run's glyphs into the same surface. The claim is **narrow**, and the fields beside it say
how narrow: a box's **background** (11.4), each of its **border bands** (11.5) and a run's
**glyphs** (11.6), while `raster` stays **0**, because the structural commands are not
drawings and a pass that consumed a whole list does not exist.
`robo_paint_drawing_is_implemented()` agrees with the constant, and
`background_painting`/`border_painting`/`text_painting`/`clipping`/`opacity` say *which* parts of
the pipeline exist — a caller deciding whether to look for a surface reads the function; one
deciding whether to *make* one reads the fields.

RoboPaint is the fifth module in the layout stack: it sits directly above RoboLayout and
directly below RoboRender (`DEPENDENCIES.md`). RoboLayout answers **where every box is**;
RoboPaint turns those answers into a **sequence of work** a rasteriser executes, 11.3 turns
that sequence into the **artifact** a rasteriser consumes, 11.4–11.6 put the first colours —
and the first **characters** — on a surface, 11.8 makes the **boundary** those drawings are
bounded by an explicit, nested scope in that artifact, and 11.9 gives every drawing a
**strength** that accumulates down the tree and is applied in the same pass.

```text
HTML / DOM → RoboCSS → RoboLayout → RoboPaint → operations → display list → push-clip / push-opacity / draw-background / draw-border / draw-text / pop-opacity / pop-clip → RoboGraphics raster → surface
```

This build stops after a box's **decorations, its text, the scopes that bound them and the
strength they are drawn at**: images and a rasteriser of the whole list are each a later goal, and
each is named in §Boundaries where it would otherwise be assumed.

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

## The tenth structural decision: the colours reach a surface, and the fence around them is explicit

`robo_paint_decoration_paint` walks a **display list** — not the records, because the display
list is the artifact whose geometry is resolved — and fills each **drawing** command into a
`robo_gfx_surface_t` through the RoboGraphics CPU raster: a background's border box (11.4) and,
since 11.5, each border side's **band**. The three **structural** commands are **skipped and
counted**, because they draw nothing by definition — which is why `raster` stays 0 while
`drawing` flips to 1. "Backgrounds and border bands are painted; nothing else is" is a number a
reader sees (`robo_paint_decoration_stats_t.skipped`) rather than a silence.

**The two kinds are one pass, because they are one order.** The list interleaves them the way the
traversal emits them, and CSS's paint order is that same order: a descendant's background is
painted *after* its ancestor's border, so a child that spills over its parent draws over the
parent's border rather than under it. Filling all backgrounds and then all borders would paint
every background in the document before every border in it — a different and visibly wrong
picture — so the walk is one walk and its order is the list's. This is why the background-only
door of 11.4 was **replaced** rather than joined: the fence it drew (`background_painting` 1,
`raster` 0) is the same fence, one command kind along.

A **band's shape** rides with the band: 11.3 resolved a side's rectangle and collapsed the four
side kinds into one command, which would have discarded the style — the one fact a rasteriser
cannot recover from a rectangle. So the `border-style` now travels record → command, and a
command is render-ready in the sense this module keeps using: its consumer can produce the drawn
shape from the command alone. What this build draws for the seven *pattern* styles
(`dotted`, `dashed`, `double`, `groove`, `ridge`, `inset`, `outset`) is their band, in the side's
colour — CSS's `solid` shape — and that simplification is counted (`unshaped`) rather than
silent.

The colour crosses into RoboGraphics through **one door**, `robo_paint_color_to_rgba8`, and the
whole of it is a field-for-field copy between two 8-bit sRGB records: no arithmetic, no colour
space, nothing to lose or invent, and alpha survives rather than being flattened. Its four kinds
are **decided rather than flattened**, because three of them are not colours a surface can take —
`currentColor` is *refused* (`currentColor` can only be resolved by the layer that owns an
element's `color`, and that is the paint pass, not this bridge), `none` means no colour was
declared, and `invalid` means it was meant to be a colour and was not.

The boundary is **read, not implemented**: the command already carries it as two axes from 11.1,
so the painter resets the raster's clip to the surface bounds, replaces the axis a boundary
hides, and lets `robo_gfx_raster_set_clip` intersect — the graphics module's arithmetic rather
than a second copy of it. It bounds a band exactly as it bounds a background, because a band is a
fill like any other. The surface belongs to the caller: the painter retains nothing, destroys
nothing it did not create, and holds no reference after returning.

## The eleventh structural decision: a run's characters live in a pool, named by a span

A text run is the first thing this module carries that is **not a number**, and the whole of
11.6 is what that costs. A fixed record cannot hold variable-length data, so the characters
live in a **pool** — one contiguous array, owned by exactly one list — and a record carries a
**span** into it: two integers, no pointers. That is the one shape that keeps every property
the earlier releases established. A text operation is still a value record with no pointers in
it; a list is still safe to copy, to discard and to rebuild; "no dangling pointer" is still
true *by construction*.

The pool is deliberately a type of its own, because it has **one implementation and two
owners**: the record list owns one, the display list owns one, and the builder copies each span
across — which is why clearing the records does not change a character the display list draws.
A span is validated **against the pool it names** rather than trusted, so a consumer either gets
the run's characters or is told they are not there.

A span counts **code points**, not bytes and not glyph ids. Code points are the unit RoboLayout
measures in, so the run is measured and painted in the same unit; and glyph ids are deliberately
not stored, because a glyph id is a fact about a *font* — a list that held them would be a list
only that font could draw, and "does the painter's face agree with the traversal's face?" would
be a question nothing could answer. Characters keep the display list a statement about the
**document**, and put glyph resolution at the drawing step, where RoboGraphics' own bridge says
it belongs. That leaves the Phase 14 seam already the right shape:

```text
this build    text run → the font's cmap, per code point, in logical order → paint
Phase 14      text run → RoboText shaping → a glyph run → paint
```

Text is also the third **drawing** kind in the one pass, and the pass prepares the caller's
face, because a `robo_font_face_t` is created without a character map and a lookup on one that
has not been parsed misses. A face whose map will not parse is still not a refusal: every glyph
becomes `.notdef` and `glyphs_missing` names the fact per code point.

## The twelfth structural decision: the boundary in force is a *scope*, not a field

Every release since 11.1 has carried a clip on each record and each command. That answers *what
bounds this drawing* and cannot answer *where does this boundary begin and end* — a flat list said
the same two intervals on every command and nowhere said that the box under command 12 is what
narrowed them. 11.8 adds that statement: a box whose used `overflow` **changes** the boundary in
force emits a `push-clip` after its own content boundary and a matching `pop-clip` before its
`end-box`, and the pass reads a flat sequence against one **stack** (`robo_paint_clip_stack_t`).

Three consequences, each a decision:

- **A scope appears only when the boundary changed.** A `visible` box narrows nothing; nor does a
  `hidden` box inside an ancestor that already bounded the same region. Both are two commands a
  consumer could only skip, and neither is emitted — `stats.clipped - stats.clip_scopes` is exactly
  the count of boundaries that were already in force.
- **The box's own background and border are outside its scope.** CSS clips a box's overflowing
  *contents*, not its own decoration, so the push stands after `content`. A flat list makes that the
  one arrangement a reader gets backwards, so it is asserted by command index **and** by pixel.
- **An excluded box is a valid empty interval, not an error.** An intersection that comes out
  disjoint is canonicalized to `[max(start), max(start)]`, so "nothing can be drawn here" is a
  record the type can hold — and a scope whose clip is empty suppresses drawing without poisoning
  the stack.

The boundary is applied in **one place** for every drawing kind — the pass's stack, reset into the
raster per drawing — so no painter carries a second copy of the rule, and the builder refuses any
list whose scopes do not pair up, narrow, or agree with the commands inside them.

## The thirteenth structural decision: a drawing has a *strength*, and the box owns it

A colour's alpha and an element's `opacity` are two facts, and every release before 11.9 had only
the first. `rgba(255, 0, 0, 0.5)` makes a *colour* half transparent; `opacity: 0.5` makes a **box**
half transparent — its background, its border, its text and every descendant it contains all
composite at half strength, and the colours they were declared with are untouched. 11.9 adds the
second factor: the traversal multiplies each box's declared value into the strength in force and
carries it on every record, and the pass applies it through `robo_gfx_raster_set_opacity` — the
graphics module's *global* multiplier, which is multiplied into every blend it performs.

Three consequences, each a decision:

- **The scope opens at the box's marker, not at its content boundary** — the one place 11.9 and
  11.8 deliberately differ, and not a style choice. CSS paints an element's own background and
  borders *at the element's own opacity*, so the scope has to be open before the first decoration;
  the same CSS paints that box's own background under its **parent's** clip, so the clip cannot be.
  The two pairs therefore nest differently around one box, and one rule holds both:
  **every record carries the state in force at its own position.**
- **A scope appears only when the declaration changed the strength**, exactly as a clip scope
  appears only when the boundary changed. `stats.opacity_declared - stats.opacity_scopes` is the
  count of declarations that established nothing.
- **The strength is on the record *and* in the scope.** A consumer that ignores the two new command
  kinds still draws every fading box at the strength on its own command; the scope is how a
  consumer that walks with state gets the nesting right. It is not the only place the strength
  lives.

Two things are worth stating because they are easy to get backwards. **A cleared record is fully
opaque**, not transparent: zeroing a record that meant "invisible" would make every element of
every document that never mentions `opacity` disappear, silently, with correct geometry and a
blank page. And **a text run never reads its own declaration**: the run's box is the element that
owns it, and that element has already opened a scope around it — so the strength is *passed in*,
and a run that read its own style would apply the same declaration twice and square it.

What this build does **not** do the way CSS says is composite the subtree as a group: the strength
is applied **per drawing**, so two overlapping descendant fills differ from a browser's flattened
result. That gap is asserted rather than hidden (`paint.opacity.group_deviation` computes both
answers through the graphics module's own blend), and closing it needs an offscreen layer, which
is a compositor.

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
| `RoboPaintTypes.h` | `robo_paint_op_kind_t` (14, `NONE` included), `robo_paint_diagnostic_domain_t` (10), their predicates and name doors |
| `RoboPaintDiagnostics.h` | `robo_paint_diag_t` and the `ROBO_PAINT_DIAG` macro |
| `RoboPaintGeometry.h` | `robo_paint_boxes_t`, `robo_paint_clip_t`, `robo_paint_boxes_of`, `robo_paint_clip_intersect` |
| `RoboPaintClip.h` | `robo_paint_clip_stack_t` — the LIFO of clip records a flat sequence is read against: push, pop, current, depth, reset (11.8) |
| `RoboPaintOpacity.h` | `robo_paint_opacity_t` — the strength a drawing is drawn at, its arithmetic and its defaults — and `robo_paint_opacity_stack_t`, the same LIFO one field along (11.9) |
| `RoboPaintOperation.h` | `robo_paint_operation_t` and its accessors, validity, side band and one-line writer |
| `RoboPaintList.h` | `robo_paint_list_t` — create/clear/append/at/operations/equal |
| `RoboPaintContext.h` | `robo_paint_context_t`, `robo_paint_stats_t`, `robo_paint_pass_outcome_t`, `robo_paint_context_run` |
| `RoboPaintDump.h` | `robo_paint_list_write` — the developer's text view |
| `RoboPaintDisplayCommand.h` | `robo_paint_display_command_t`, its 11-kind vocabulary, validity and writer (11.3, 11.6, 11.8, 11.9) |
| `RoboPaintDisplayList.h` | `robo_paint_display_list_t`, `robo_paint_display_list_stats_t` — the render-ready artifact (11.3) |
| `RoboPaintDisplayListBuilder.h` | `robo_paint_display_list_builder_t` — the conversion and structural check (11.3) |
| `RoboPaintDisplayDump.h` | `robo_paint_display_list_write` — the developer's view of the display list (11.3) |
| `RoboPaintColor.h` | `robo_paint_color_is_drawable`, `robo_paint_color_to_rgba8` — the one-way colour bridge to RoboGraphics (11.4) |
| `RoboPaintText.h` | `robo_paint_text_run_t`, `robo_paint_text_span_t`, `robo_paint_text_pool_t` — the code-point payload and the one container that owns it (11.6) |
| `RoboPaintDecoration.h` | `robo_paint_decoration_paint`, `robo_paint_decoration_stats_t` — the one pass that fills a box's backgrounds and border bands, strokes a run's glyphs and applies the strength they are drawn at, into a surface (11.4–11.6, 11.9) |

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
| `paint/docs/Decoration.md` | The colour bridge, the one decoration pass, its geometry, clipping, counters, ownership, safety and deferrals (11.4–11.5) |
| `paint/docs/Border.md` | The four bands, the `border-style` vocabulary and what travels where, corner ownership, and the seven pattern styles drawn as bands (11.5) |
| `paint/docs/Text.md` | The code-point pool and its span, the text record and its baseline, where a run sits in the sequence, glyph resolution and who prepares the face, the counters, and what is deferred to 11.7 and to Phase 14 (11.6) |
| `paint/docs/Clipping.md` | The boundary as a scope: the clip stack, the canonical empty interval, where a scope is emitted and why the box's own decoration is outside it, the builder's eleven refusals, the pass's one application point, and what is deferred (11.8) |
| `paint/docs/Opacity.md` | The strength: the record and its two invalid directions, where a scope is emitted and why that is *not* where the clip's is, the builder's eight refusals, the pass's one application point, the counters, and the group-compositing gap stated with the case that measures it (11.9) |
| `paint/docs/Tests.md` | Suite map (ten suites, 162 cases), fixtures, what the suites assert, running, sanitizers, what 11.1–11.9 found |

## Building and testing

```bash
# from runtime/
pwsh -NoProfile -File Tools/Build.ps1 -Configuration Debug -Verify -Test
./Out/RoboTest.host.exe --filter paint.
```

**162** of the cases the filter reports are this module's (**ten** suites); the filter is a
substring match, so the six `demo.paint.*` cases of the visual demo come with them
(`paint/docs/Tests.md` §1).

## Boundaries

RoboPaint must not depend on RoboWindow, RoboDisplay or RoboRender. It reads RoboLayout
through const doors, and uses RoboGraphics for **geometry and — since 11.4 — the CPU raster it
is handed a surface for**, plus — since 11.6 — the glyph draw, which it asks for by **glyph id
and outline**: RoboGraphics supplies the bridge to the font and never a shaper of its own. A
surface and a face are always the *caller's* arguments and never a query.

- **A box's background, its four border bands and a run's glyphs are painted; nothing else is.**
  `DRAWING` is 1, `background_painting` is 1, `border_painting` is 1 and `text_painting` is 1.
  Images, rounded corners, gradients, transforms and antialiasing are all absent, and every one of
  them is counted by the pass rather than silently dropped. The seven **pattern border styles** are
  drawn as their band, in the side's colour, and counted (`unshaped`) — `border_painting` claims a
  border's place and colour, not every border's look.
- **No rasteriser of the whole list.** The display list exists as a render-ready artifact
  (`display_lists` is 1) and one command kind of it is consumed; `raster` is 0.
- No framebuffer presentation, no compositor, no GPU.
- No **image** operations. Images are the next drawing kind and the second non-numeric payload,
  and they are a later goal's question — a pool of encoded bytes, or a reference to a decoded
  surface.
- **A strength per element, applied per drawing — not a group.** An element's `opacity` is
  accumulated along the path that reaches it and applied to every drawing that path produces
  (11.9). What is absent is CSS's *group* semantics: the subtree is not composited into an
  offscreen layer and faded as one, so two overlapping descendant fills differ from a browser's
  result. The gap is asserted (`paint.opacity.group_deviation`) rather than hidden, and closing it
  needs a compositor. Also absent: transforms, `border-radius`, gradients, antialiasing, blend
  modes, filters and masks.
- **Rectangular clipping of a box's contents, and nothing else.** A box whose used `overflow`
  *changes* the boundary in force opens a scope: `push-clip`, its contents, `pop-clip`
  (11.8). What is not here is rounding, `clip-path`, masks and filters; clip-aware hit testing
  (11.10); scroll clipping (nothing scrolls); and compositor- or tile-level clipping. The
  boundary is enforced in one place — the pass's own stack — for every drawing kind, so no
  painter carries a second copy of the rule.
- **No scrolling**, and no scroll offsets — 10.13 recorded the overflow; nothing scrolls it.
- No **paint invalidation** (11.11) and no **hit testing** (11.10) — the final positioned
  geometry is preserved for both, but neither is built here.
- No **`background-clip`** or **`background-origin`**: the painting area is the border box,
  which is CSS's own initial value rather than a simplification of it.
- No **`z-index`** or stacking context. The traversal is document order; a flat list with
  depths is what a later stacking pass will reorder.
