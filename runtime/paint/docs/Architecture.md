# RoboPaint — architecture (through Phase 11.10)

RoboPaint is the fifth module in the layout stack. It sits directly above RoboLayout and
directly below RoboRender (`DEPENDENCIES.md`), and it turns the completed geometry RoboLayout
produced into a **deterministic sequence of operations** a rasteriser executes -- and, since
11.3, into the **render-ready display list** it consumes. 11.1 established the context, the
operation vocabulary, the list and its ownership, the deterministic traversal, the geometry a
rasteriser reads and the clip state; 11.2 made the record sequence regular; 11.3 resolved the
box model into one rectangle per command; 11.4–11.7 put four kinds of drawing on a
**surface** -- a `draw-background` command's rectangle and colour, a `draw-border`'s band, a
`draw-text`'s glyphs and a `draw-image`'s resource, all through a RoboGraphics CPU raster, with the
picture added to the three after the goals that bound and fade them had landed. What is left is real
clipping, opacity and a rasteriser of the whole list, each a later goal's.

---

## 1. What the phase is

Phase 10 ended with every box carrying a position, a size, four inset layers, a final
coordinate, an overflow record and a validity state. Phase 11.1 asks the one question that
model makes askable — *what will be drawn, in what order, under what boundary?* — and answers
it as **data** rather than as drawing. The pipeline this phase establishes, and stops at the
third arrow:

```text
HTML / DOM → RoboCSS → RoboLayout → RoboPaint → operations → display list → draw-background → RoboGraphics raster
```

The single idea the phase turns on, and getting it backwards would produce a module that is
fast and wrong:

> **A paint list is a record of what will be drawn, not a set of drawing instructions, and
> it is produced by one deterministic traversal of the layout tree.**

Three consequences follow, and each is a decision:

| Consequence | Where it is argued |
|---|---|
| The order comes from the tree, not from the list — flat, contiguous, with an explicit `BEGIN_BOX`/`END_BOX` pair per box and a recoverable `depth`. | §4 |
| An operation is emitted **only when it draws** — no declaration, no operation. | §5 |
| Paint derives nothing RoboLayout already concluded; only the page accumulation and the clip intersection are computed here. | §6, §7 |

---

## 2. The dependency position

`DEPENDENCIES.md` places RoboPaint directly above RoboLayout. The rules that follow from
that position, and which the module honours as a signature rather than as a check:

- The pass takes the tree by **`const` pointer**. RoboPaint never writes a result, a line, a
  baseline or an overflow record; every read goes through RoboLayout's read doors, so "paint
  does not modify layout state" is the type rather than a rule a reviewer must verify.
- RoboPaint must not depend on RoboWindow, RoboDisplay or RoboRender. It may use
  RoboGraphics geometry **only** — there are no new coordinate types (§6).
- No Chromium/Blink paint class, no Skia abstraction, no Chromium build dependency. Chromium
  is external reference material and nothing here is a translation of it.

---

## 3. The module surface

Seventeen headers under `Include/RoboPaint/`, one umbrella:

| Header | What it holds |
|---|---|
| `RoboPaintTypes.h` | `robo_paint_op_kind_t` (14 values), `robo_paint_diagnostic_domain_t` (10), their predicates and name doors |
| `RoboPaintDiagnostics.h` | `robo_paint_diag_t` and `ROBO_PAINT_DIAG` |
| `RoboPaintGeometry.h` | `robo_paint_boxes_t`, `robo_paint_clip_t`, `robo_paint_boxes_of`, `robo_paint_clip_intersect` |
| `RoboPaintOperation.h` | `robo_paint_operation_t`, its accessors, validity, side band, one-line writer |
| `RoboPaintList.h` | `robo_paint_list_t` — create/clear/append/at/operations/equal |
| `RoboPaintContext.h` | `robo_paint_context_t`, `robo_paint_stats_t`, `robo_paint_pass_outcome_t`, `robo_paint_context_run` |
| `RoboPaintDump.h` | `robo_paint_list_write` |
| `RoboPaintDisplayCommand.h` | `robo_paint_display_command_t`, its vocabulary, validity and writer (11.3) |
| `RoboPaintDisplayList.h` | `robo_paint_display_list_t`, `robo_paint_display_list_stats_t` (11.3) |
| `RoboPaintDisplayListBuilder.h` | `robo_paint_display_list_builder_t` (11.3) |
| `RoboPaintDisplayDump.h` | `robo_paint_display_list_write` (11.3) |
| `RoboPaintColor.h` | The colour bridge: `robo_paint_color_is_drawable`, `robo_paint_color_to_rgba8` (11.4) |
| `RoboPaintText.h` | The code-point payload: `robo_paint_text_run_t`, `robo_paint_text_span_t`, `robo_paint_text_pool_t` (11.6) |
| `RoboPaintClip.h` | `robo_paint_clip_stack_t` — the clip scopes a flat sequence is read against (11.8) |
| `RoboPaintOpacity.h` | `robo_paint_opacity_t` and `robo_paint_opacity_stack_t` — the strength a drawing is drawn at, and the same LIFO one field along (11.9) |
| `RoboPaintDecoration.h` | `robo_paint_decoration_paint`, `robo_paint_decoration_stats_t` — the one pass that executes backgrounds, border bands and text runs (11.4–11.6) |
| `RoboPaintVersion.h` | Version macros, `ROBO_PAINT_API_FROZEN`, `ROBO_PAINT_DRAWING_IMPLEMENTED` |
| `RoboPaint.h` | Lifecycle, capabilities, version |

Two closed enumerations and one diagnostic domain set, in the shape every module uses: an
explicit `NONE`/`COUNT`, an `is_known` predicate and a `name` door that never returns NULL. A
suite that walks a vocabulary and checks the count against the names is how a phase that adds
an enumerator is *made* to add its name.

---

## 4. The operation, and the flat list

`robo_paint_operation_t` is a value record with no pointers in it:

```c
typedef struct robo_paint_operation {
    robo_paint_op_kind_t kind;      /* the thirteen, plus NONE */
    robo_layout_node_id_t node;     /* never NONE for a live operation */
    size_t depth;                   /* 0 for the root: makes the tree recoverable */
    robo_paint_boxes_t boxes;       /* the three rectangles, in paint coordinates */
    robo_layout_point_t origin;     /* the containing block's content origin */
    robo_paint_clip_t clip;         /* the boundary in force */
    robo_paint_opacity_t opacity;   /* the strength in force (11.9) */
    robo_css_color_t color;         /* none for a marker */
} robo_paint_operation_t;
```

Every record carries **both** states in force, for the same reason it carries both coordinate
spaces: a consumer with only the list must not have to re-walk the tree to discover what was in
force at a record's position. What it does *not* have to do is reconstruct the nesting — the two
scope pairs are in the sequence, and §4.5 states their placement.

### 4.1 The vocabulary: structural and drawing

`NONE`, `BEGIN_BOX`, `BACKGROUND`, the four `BORDER_*` kinds, **`CONTENT`**, **`TEXT`**,
`PUSH_CLIP`, **`POP_CLIP`** (11.8), **`PUSH_OPACITY`**, **`POP_OPACITY`** (11.9), `END_BOX`. There
is no `IMAGE`, `SAVE`, `RESTORE` or bare `OPACITY` enumerator, even though the phase's brief lists
some of them as things the representation must *eventually* express. The reason is the one
RoboLayout 10.2 gives for not adding a constant for a feature it does not have: a name with no code
behind it is a promise, not a vocabulary. What makes them expressible is the record's shape — and
the shape has now been tested four times, by `CONTENT` at 11.2, `TEXT` at 11.6 and the two scope
pairs at 11.8 and 11.9, each of which was one enumerator, one name row and one case in the validity
rule rather than a redesign of the list.

**A scope is two enumerators rather than one**, and both releases that added a scope made the same
choice. A single `CLIP`/`OPACITY` kind would need a flag to say which half it was, and the flag is a
field a consumer could get wrong where two kinds are two entries in a switch it must already write.
The pairs are also not drawings: `robo_paint_op_kind_draws` answers no for all four, which is what
keeps `raster` at 0 (`paint/docs/Operations.md` §1.0).

The vocabulary splits in two, and the split is the rule a builder obeys:

| Half | Kinds | Emitted | Colour |
|---|---|---|---|
| **Structural** | `BEGIN_BOX`, `CONTENT`, `END_BOX` | for **every** placed box | `none` |
| **Drawing** | `BACKGROUND`, the four `BORDER_*` | **only when it draws** | a usable colour |

`robo_paint_op_kind_is_structural` and `robo_paint_op_kind_draws` are the predicates, and
they partition the vocabulary above `NONE`. **11.2** added `CONTENT` — the record the
brief's sequence names that 11.1 did not emit — and it is the kind that makes "structural"
rather than "marker" the right word: `CONTENT` draws nothing but is not a box marker.

`CONTENT` is emitted for every placed box whether or not the box drew anything or has
children, because what it states is a fact about the *sequence*: this box's own records end
here and its subtree's begin. It acts on the **content box**, and it makes the boundary
explicit rather than inferred — a consumer reads a box's own painting as
`[BEGIN_BOX .. CONTENT]` and its subtree as `(CONTENT .. END_BOX)`, and a childless box has a
boundary exactly as a parent does.

### 4.2 Four border kinds, not one kind plus a side

A `side` field would have to carry a "not a border" value for the other kinds, and a field
that means nothing for most of its records is the "shape that reads as an answer" this tree
refuses. With a kind per side there is no such value to get wrong, and
`robo_paint_op_kind_side` is a **total function** from the four border kinds to the side each
draws on and a refusal for everything else — so no operation can carry a side that disagrees
with what it draws.

### 4.3 The list is flat

`robo_paint_list_t` is an ordered array of operations, and the **only** thing this module
allocates. The tree's shape is recoverable from the `depth` each operation carries, and a
nested structure would only have to be flattened before use — a rasteriser walks a sequence
once and keeps a clip stack. The order a consumer meets is, for each box: `BEGIN_BOX`, the
decorations in the order CSS paints them (background, then the four border sides), the box's
children, `END_BOX`. A consumer that ignored every `kind` would still see each box's
operations contiguous and in paint order.

### 4.4 Validity, and where it is enforced

`robo_paint_operation_is_valid` requires: a known kind and never `NONE`; a live node id; three
valid boxes whose border box contains the padding box contains the content box; a valid clip;
and the colour rule — `none` for a marker and a *drawable* colour for a drawing kind.
`none` on a drawing operation is refused because a fill or a border with no colour draws
nothing, and a builder that emitted one would be emitting an operation that is not a
statement about the document.

The rule is enforced at **`robo_paint_list_append`**, not in the traversal. The traversal is
one caller; a rule enforced in the container is a rule no future caller can forget — the
argument `RoboLayoutTree.h` makes for validating a parent link at the door rather than in the
builder. It is the "geometry safety" requirement satisfied once instead of at every call
site, and it is what makes every list a consumer reads a list of records.

---

## 5. What draws, and what does not

An operation is emitted only when it draws. Concretely:

- **Background.** `background-color` is `transparent` until an author says otherwise; a
  transparent background emits nothing. The painting area is the **border box**, which is
  CSS's own initial `background-clip` — the background extends under the border rather than
  stopping at the padding edge. `background-clip` and `background-origin` are not read.
- **Borders.** A side draws iff its **used width is above zero** and its colour draws. The
  used width is the whole of "does this side draw": a zero-width border, a `none` style and an
  undeclared side are one number, which is the conclusion RoboLayout already reached and
  stated in `RoboLayoutBorder.h`.
- **Colours.** `background-color` and `border-*-color` are the **paint-visible** half of
  10.14's property classification — the half layout deliberately ignores — so reading them is
  exactly this module's job. `currentColor` is resolved here (against the element's `color`),
  because `RoboLayoutBorder.h` says only the layer with an element can resolve it, and this is
  that layer. A colour that could not be resolved is *counted*
  (`robo_paint_stats_t.contextual_colors`) rather than silently dropped.
- **Text.** A text node generates a layout object of the one new kind the vocabulary has
  needed, and since 11.6 it generates a **`TEXT` record** rather than a count: one drawing whose
  payload is a **span** into the list's code-point pool. It is not a box, so it gets no
  `BEGIN_BOX`/`CONTENT`/`END_BOX` triple -- it is emitted where the run sits, as a child of the
  box whose element owns the text node. What it does **not** claim is stated in `Text.md` §7:
  no shaping, no bidi, no line breaking, no fallback -- the runs painted are the runs layout
  measured, in logical order, at the font's own advances. The counters still exist (`text_runs`
  against `texts`), so "this document has text and none of it was painted" is a number a reader
  sees rather than a silence.
- **Style-less boxes.** A box with no computed style emits a marker pair and no decoration,
  and is counted (`without_style`). A fact about the build, not a failure.

---

## 6. The coordinate model

RoboGraphics defines the canonical pixel geometry; RoboLayout re-exports it under names that
read as layout; RoboPaint reads it under layout's own names. **There are no new coordinate
types**, so the numbers a rasteriser receives are the numbers RoboLayout wrote, with no
conversion for a sign or an origin to flip in.

Two coordinate facts are named:

- **layout coordinates** — what RoboLayout wrote: a box's geometry relative to its containing
  block's content origin.
- **paint coordinates** — one page space, where the accumulated origins of every ancestor have
  already been added.

The traversal carries the accumulation: at a box, `child_origin = boxes.content` (in paint
coordinates), because layout wrote each child's geometry relative to that point. An operation
carries the boxes in paint coordinates **and** the containing block's origin, so layout
coordinates are `boxes - origin` (`robo_paint_operation_local_boxes`).

### 6.1 Why both, and why that is safe

The obvious "keep one and derive the other" is wrong in a specific way: a consumer that has
only the list cannot recover the *other* space without re-walking the tree, and re-walking the
tree to rediscover what layout concluded is exactly the "recalculate in paint" this phase
exists to prevent. What makes the pair safe rather than a second opinion is that the
relationship is **checkable and checked**: `robo_paint_operation_local_boxes` must equal what
`robo_paint_boxes_of` reads straight out of layout, for every operation of every box, and the
suite asserts exactly that. A redundancy with an assertion holding the two halves together is
a test; one without is a place for them to drift.

### 6.2 The three boxes

`robo_paint_boxes_t` is the box model's three rectangles — border box, padding box, content
box — derived in one place from layout's result and layout's two inset records. Three
rectangles rather than one because the difference between them **is** the thing painter and
hit-tester both want: the band between the border box and the padding box is a border, the
region inside the padding box is a background, and a consumer that re-derived either from a
computed style would be re-deciding which declarations the layout pass could use.

`robo_paint_boxes_from_layers` is the one derivation, and two facts about it are load-bearing:

- **every sum is taken in 64 bits and saturated**, because `border.x + width` is the sum of
  two bounded numbers and their sum is not bounded — the rule 10.15 found the hard way in the
  pass itself, restated here where this module's own accumulation is;
- **the padding box is floored at zero on each axis**, because a box whose borders are wider
  than the box itself leaves no room, and a negative width is not a width. The floor is
  applied to the result, which is what makes a saturated border box (`width: 2147483647px`
  with a `border: 5px solid`) produce a readable record rather than a refusal.

`insets_measured` is a flag, not a sentinel — the same rule `RoboLayoutConstraints.h` states
for unset bounds. A **text run** and a **transparent inline box with contents** are both
placements RoboLayout's inset doors decline; for those, all three rectangles are the one
placement geometry and the flag is clear, so "does this box have box layers?" is a question
the record answers rather than a distinction it blurs.

---

## 7. The clip in force

`robo_paint_clip_t` is **two axes, not one rectangle**, and this is the record worth reading
twice because one rectangle is the obvious shape and it is wrong. `overflow-x: hidden` beside
`overflow-y: visible` is a legal declaration this build resolves, and it means what it says:
bounded horizontally, free vertically. A single rectangle cannot express that — a consumer
handing its four numbers to a rasteriser would clip the axis the author left open, and the
document would look subtly wrong in a way no assertion about a rectangle would catch.

Each axis is a mode plus a **half-open interval**; `mode` is CSS's own record rather than a
boolean, because "this axis has a boundary" **is** "this axis resolved to a mode that hides".
A cleared record is `visible` on both axes, CSS's initial value. Three derived questions are
answered by doors rather than inferred:

- `robo_paint_clip_axis_is_active` — is this axis bounded?
- `robo_paint_clip_axis_is_empty` — bounded *and* empty: nothing inside can be drawn.
- `robo_paint_clip_rect` — the one case where a rectangle says everything, both axes bounded.
  It **refuses** otherwise, exactly as `robo_layout_overflow_edges_clip_rect` does, because a
  `visible` axis has no bound to report and answering one would hand a rasteriser a number
  that means "free" as if it meant "bounded".

`robo_paint_clip_intersect` is the accumulation, and its rule is per axis: an axis the
boundary hides is intersected with the boundary's interval; an axis it leaves open is carried
through untouched; a boundary that hides neither axis leaves the clip unchanged, which is why
the traversal can call it for every box rather than asking whether it needs to. An
intersection that comes out empty is success with an empty interval, not a failure — a box
whose clipping ancestor has already excluded it draws nothing, which is a statement about the
document.

An intersection that comes out **disjoint** — a box an ancestor's boundary entirely excluded — is
canonicalized to a zero-length interval at the larger of the two starts rather than left running
backwards, so "nothing can be drawn here" is a *record* the type can carry instead of the one case
that has to be special-cased. §19.3 states why, and the choice of which interval it lands on.

The clip edge for a box's contents is its **padding box**: CSS clips a box's overflowing
contents at its padding edge, and RoboLayout's own clip rectangle reads the same box one layer
down. Nothing in RoboPaint decides whether to clip beyond re-reading the box's own used modes —
and since 11.8 what that boundary *does* downstream is a **scope** in the artifact, not just a
field on each command (§19).

---

## 8. The pass, and the staleness refusal

`robo_paint_context_run(context, tree, list, diag)` walks the tree in document order — parents
before children, children in DOM/layout order — and, for every box nothing has to be asked
about:

1. emits `BEGIN_BOX` with the box's three rectangles in paint coordinates, the accumulated
   origin, the clip in force, the strength in force and the box's depth;
2. emits `PUSH_OPACITY` when the box's own declaration **changes** the strength (11.9) — at the
   marker, before anything the box draws, because CSS paints a box's own background and border
   at the box's own opacity;
3. emits `BACKGROUND` when the box's background declaration draws;
4. emits one `BORDER_*` per side whose used width is above zero and whose colour draws;
5. emits `CONTENT`, on the content box;
6. emits `PUSH_CLIP` when this box's own used `overflow` **changes** the boundary (11.8) — after
   the content boundary, because CSS clips a box's overflowing *contents* and not its own
   decoration;
7. recurses into the box's children, in order;
8. emits `POP_CLIP`, then `POP_OPACITY`, then `END_BOX` — each pop carrying the value its push
   carried, so a pair is self-identifying.

A scope opens **only when the value changed**, and that is one rule read twice: an `overflow:
visible` box narrows nothing and an `opacity: 1` box fades nothing, so neither emits the two
commands a consumer could only skip. `stats.clipped − stats.clip_scopes` and
`stats.opacity_declared − stats.opacity_scopes` are the two counts of declarations that
established nothing.

`list` is **cleared first**, before the staleness check, so a refused pass hands back an empty
list rather than the previous pass's.

### 8.1 The pass outcomes

`ROBO_PAINT_PASS_NONE`, `PAINTED`, `FAILED`, and `STALE`. `STALE` is a **fourth outcome, not
a fifth error**: at least one box carries a layout bit (`robo_layout_tree_needs_layout`), so
the walk refuses rather than painting geometry that is not final, and returns `ROBO_OK`,
because nothing went wrong that a caller cannot fix — the cure is to run a layout pass. A
caller that could not tell "your tree is stale" from "your tree is broken" would go looking
for a bug in the document.

Two further legitimate non-walks: an empty tree (no root box) is a pass that paints nothing
and reports zero objects, and a box reached under a box nothing placed is **counted**
(`unplaced`) and its subtree walked for the counts and emitted nowhere.

### 8.2 Determinism

Painting the same tree twice produces the same operation sequence. There is no dependence on
pointer addresses, hash order, unspecified child ordering, thread scheduling or uninitialised
memory: child order is the layout tree's own `first_child`/`next_sibling` order, and a
comparison is one call (`robo_paint_list_equal`) rather than a walk a suite would re-implement.

---

## 9. Ownership and lifetime

| Object | Owns | Lifetime rule |
|---|---|---|
| Context | nothing borrowed | safe to destroy in any order relative to the tree and the list; `user_data` is the caller's and `reset` leaves it alone |
| List | one growable array of value operations | safe to discard and regenerate, in any order relative to the tree; `clear` keeps the capacity |
| Operation | nothing | a value: copy it, embed it, put it on the stack |
| Tree | (layout's) | borrowed, never mutated — the pass takes it `const` |

Because an operation has no pointers, a list cannot contain a dangling pointer; because the
list holds no reference to the tree, it cannot mutate layout geometry. `clear`/`reserve`
exist so a per-frame renderer reuses storage rather than allocating per frame — and that is
the *only* concession this phase makes to performance, because the next pass recomputes every
operation from scratch: a partially updated list would be a cache of conclusions this module
has no way to validate.

The one lifetime rule a compiler cannot check: `robo_paint_list_operations` borrows a
contiguous array that **moves when the list grows**. It is valid until the next append, a
documented rule rather than a hazard, because nothing in an operation points anywhere.

---

## 10. The capability model

`robo_paint_capabilities_t` is read positionally by `paint.conformance.capabilities` against a
list of names, so a field added without an assertion is a visible omission. Thirteen of its
sixteen fields are **1** (`context`, `operation_model`, `paint_list`, `traversal`,
`box_geometry`, `clip_state`, `decoration_ops`, `display_lists` since 11.3, **`drawing`** and
**`background_painting`** since 11.4, **`border_painting`** since 11.5, **`text_painting`**
since 11.6, **`clipping`** since 11.8 and **`opacity`** since 11.9) and two are **0**:

| Field | Value | Why |
|---|---|---|
| `drawing` | **1** | a **drawing** command's rectangle and colour reach a RoboGraphics surface (11.4–11.6) — the field a caller about to look for a framebuffer should read |
| `display_lists` | **1** | a display list as a consumable artifact exists (11.3) |
| `background_painting` | **1** | *which* drawing exists: a background over its border box (11.4) |
| `border_painting` | **1** | the second drawing: each border side filled across its **band** (11.5). It claims a border's place and colour, not its look — the seven pattern styles are drawn as bands and counted (`unshaped`) |
| `text_painting` | **1** | the third drawing: a run's glyphs resolved through the caller's face and stroked into the surface (11.6). It claims a run's glyphs reach a surface, in logical order and unshaped — `rtl` counts the runs that were drawn that way |
| `clipping` | **1** | the boundary is a **scope**: a box whose overflow changes what is in force emits `push-clip`/`pop-clip`, and the pass bounds every drawing kind by the scope it stands in (11.8). Rectangular only — no rounding, no `clip-path`, no masks, no compositor or scroll clipping |
| `opacity` | **1** | a drawing has a **strength** on top of its colour's alpha: a box's `opacity` is multiplied down the tree, carried on every record and applied through `robo_gfx_raster_set_opacity` at one point in the pass (11.9). **Per drawing, not per group** — CSS's flattened-subtree semantics need an offscreen layer, and the gap is asserted (`paint.opacity.group_deviation`) rather than hidden |
| `raster` | 0 | a rasteriser that consumes **every** command is a later goal; the structural kinds are not drawings |
| `paint_invalidation` | 0 | 11.11; what exists instead is its *input*, `stats.style_only` |

`decoration_ops` is **1** and is named for what it is rather than what a reader might hope:
this build decides *that* a background will be filled and *which* colour, and records it.
Keeping `decoration_ops` and `drawing` apart is what stops "a background operation exists" from
being read as "a background was painted" — and 11.4 is the release where the second of those
became true, for one command kind, with 11.5 adding the second and 11.6 the third.

`ROBO_PAINT_DRAWING_IMPLEMENTED` is **1** and is read by two doors that must not disagree —
`robo_paint_get_capabilities().drawing` and `robo_paint_drawing_is_implemented()` — and by the
conformance suite, which asserts all three together. The condition it was written down for — *a
build in which an operation's rectangle and colour reach a RoboGraphics surface* — is what
happened, and the flip did **not** widen the claim: it does not claim the seven pattern border
styles, antialiasing, rounded corners, gradients, compositor-level or group opacity, a compositor,
or that the whole list rasterises.

---

## 11. Where paint invalidation stops, and why

10.14 asks a per-box question — *is this conclusion still true?* — and answers with a state on
the box. Paint invalidation asks a different question — *is this list still current?* — and
this phase's answer is not a mechanism at all: a paint list is **not a cache of conclusions**,
it is regenerated from the tree on every pass, so there is nothing for a dirty-rect system to
maintain and nothing for a second invalidation vocabulary to mean.

What the phase hands the future system is its **input**: `robo_paint_stats_t.style_only` counts
the visited boxes whose only staleness is a paint-visible change (10.14's `STYLE` bit with no
layout bit) — exactly the set a Phase 11.11 system would repaint. Counting it is not painting
anything, and building the system that consumes it is a later goal's, which is why this module
has no paint-invalidation constant, no dirty rectangle and no per-list currency check.

---

## 12. The developer's view

`robo_paint_list_write` renders the list as one line per operation, indented by the depth the
operation carries, in list order — a *text* door into a buffer and nothing more. It is not a
renderer (it prints what the record says; a dump that disagreed would be a second
implementation of the record), not a tree (the indentation is the operation's own `depth`, so
it is what a *rasteriser* would see — the view worth having when an operation is in the wrong
place), and not a console (the guest target has no `stdio`). The format is fixed so a suite can
assert it and a person can diff two documents.

`ROBO_PAINT_DUMP_LINE_MAX` is a **derived** bound rather than a chosen one — the widest line is
a border side, and its parts at their widest printed forms sum to less than the constant with
headroom for a line. `paint.dump.line_bound` measures the widest line this build can actually
produce rather than trusting the derivation, so a future field that pushes past it fails the
test rather than overflowing a caller's buffer.

---

## 13. Boundaries, and the deferral list

What this module does **not** do, each named where it would otherwise be assumed:

- **A box's decorations and its text draw.** `DRAWING` is 1, and
  `background_painting`/`border_painting`/`text_painting` say which three. The seven **pattern**
  border styles are drawn as their band and counted (`unshaped`); images and everything below are
  absent, and the pass counts what it skips.
- **Rectangular clipping of a box's contents.** `clipping` is 1: a box whose used `overflow`
  changes the boundary in force opens a scope, and every drawing kind stops at it. What is absent
  is rounded clipping, `clip-path`, masks and filters, clip-aware hit testing (11.10), scrolling,
  and any clip a compositor or a tile scheduler would own.
- **No framebuffer presentation, compositor or GPU.** A surface is an argument.
- **No rasteriser of the whole list** — the display list is the artifact and three of its drawing
  kinds are consumed; `raster` is 0.
- **No image operation**, and no shaping, bidi, line breaking or font fallback behind the text one
  that does exist (`Text.md` §7).
- **No opacity, transforms, `border-radius`, gradients or antialiasing.**
- **No clipping architecture** — the clip a command carries is applied to the raster's own clip
  (11.4–11.6); no clipping op is emitted. No scroll clipping, no compositor clipping.
- **No `background-clip`/`background-origin`** — the border box is CSS's initial value.
- **No `z-index` or stacking context.** Document order; a later stacking pass reorders.
- **No paint invalidation (11.11) and no hit testing (11.10).** The final positioned geometry
  is preserved for both; neither is built.
- **No scrolling** and no scroll offsets — 10.13 recorded the overflow; nothing scrolls it.

---

## 14. What 11.1–11.8 found

11.1 is an architecture release, so most of what it establishes is design; 11.2 is a record
release, and what it establishes was largely already right — an operation was already a value
snapshot with no pointers, the list already owned it, the traversal already ordered it
deterministically and the dump already read it back. What 11.2 adds is the one record the
sequence was missing and the tests that pin the guarantees the brief demands. The things the
two phases *discovered* rather than decided:

1. **A forward per-layer derivation can put the padding box outside its border box.** The
   obvious `padding = border - insets` computes each layer from the previous one, and a
   saturated inset at the extreme of the coordinate range makes the next layer's origin move
   the wrong way. The derivation now works inward from the outer rectangle with saturating
   arithmetic and a zero floor on each extent, so the three layers always nest — the property
   `robo_paint_boxes_is_valid` asserts and `paint.geometry.boxes_from_layers` pins.
2. **The two coordinate spaces have to be asserted, not just carried.** Nothing about writing
   `boxes` and `origin` into a record keeps them consistent; only `local == boxes - origin`
   checked against layout holds them together. The suite does, for every operation.
3. **A one-rectangle clip cannot carry `overflow-x: hidden` beside `overflow-y: visible`** —
   the axis split is a semantic requirement rather than a convenience.
4. **A "paint." test filter is a substring match.** The runtime test group added for this
   module (`paint.`) therefore also matches the demo layer's six `demo.paint.*` cases — the
   same superset the `layout` group carries, recorded in `scripts/pipeline/commands.test.ps1`
   so the count is a known grouping rather than a surprise.
5. **A dump line bound must be measured.** A guessed buffer size is a bug that only shows on
   the widest document; deriving the bound and asserting it against a real widest line keeps
   the constant and the code from drifting.

And 11.2's own:

6. **"Marker" was the wrong word for the colour rule.** 11.1 split the vocabulary on
   `is_marker`, which had exactly two members, so a third structural kind would have fallen
   through to the drawing half and been asked for a colour it has no use for. Adding `CONTENT`
   is what exposed it: the split is now `robo_paint_op_kind_draws`, and "structural" is the
   half that means "emitted for every box and carries no colour". A two-member predicate that
   happened to partition a two-member set is a rule that was not general enough to survive
   one more member.
7. **A "blank" viewport change is not always blank.** The snapshot-isolation test has to
   *prove* the mutation it isolates against, and a box with a declared width would have made
   the second layout pass identical to the first — the test would have passed for the wrong
   reason. A block with no declared width fills its containing block, so the viewport change
   is observable; the case asserts the two lists differ as well as that the first did not
   move.

And 11.4's, which are the ones a first *drawing* release finds:

8. **A refusal can arrive as a different code one level down than the layer above promised.**
   The graphics raster refuses a target that cannot take a write with
   `ROBO_INVALID_ARGUMENT` and a format it does not implement with `ROBO_NOT_SUPPORTED`, while
   the painter's own header said "read-only *or* unsupported" was one `ROBO_NOT_SUPPORTED`.
   The two disagreed for a read-only surface — and a read-only surface is exactly what a suite
   hands a painter to check it refuses. The mapping is now explicit in one place, and
   `ROBO_OUT_OF_MEMORY` is deliberately not folded into it: that is this build failing rather
   than the caller's surface being wrong, and a caller retrying after freeing something
   deserves to be told which it was.
9. **"Every fill's centre is its own colour" is false the moment two fills overlap.** A
   child's background covers its parent's, so the correct expectation at a point is the
   colour of the **last** drawable command whose rectangle contains it and whose clip admits
   it — which is what "the fills happen in the list's order" means, stated where a pixel can
   be read. The suite now asks the list for that expectation instead of assuming each fill is
   visible, and the same oracle is what makes a clipped document's pixels checkable at all.
10. **The host test link is the one command in this tree whose argument list is large enough
    to fail.** 498 arguments and 32,741 characters, with the sanitizer flags on top of a list
    that had been growing by five or six files a phase: Windows refuses to start a process
    whose command line passes ~32k, and the failure mode is *the compiler not starting*
    rather than a build error. `Tools/Build.ps1` now hands the list over in a response file —
    and the list must be written with **forward slashes**, because clang's response-file
    tokenizer treats `\` as an escape and silently eats every separator in a Windows path
    (`D:\Muhammad\...` arrives as `D:Muhammad...`). A build fix that looked like a toolchain
    bug, found because the sanitizer gate is run as part of every phase.

And 11.6's, which are the ones a release whose payload is not a number finds:

11. **A structural check ordered before the rule it exempts is unreachable.** The display
    builder's text branch was written *after* the generic "a record's depth is its innermost
    box's" check — a rule a run fails by construction, because a run is owed one level **down**
    from the box that owns it. Every document containing text was refused with that message and
    the text branch beneath it could never run. Nothing but a document with text could expose
    it, because the branch was never reached to be wrong on its own.
12. **A drawing whose rectangle is an advance box is not a fill.** A text command is exempt
    from the zero-area rule — a glyph's ink overhangs its advance box — and it is exempt from
    the "the rectangle's centre is its colour" expectation, because a rectangle can be valid and
    contain no ink (an unmapped code point draws `.notdef` and nothing else). Both exemptions are
    statements about text rather than special cases, and both are pinned.
13. **A face is created without a character map, and the layer that draws must prepare it.**
    `robo_font_face_lookup_glyph` answers `ROBO_NOT_SUPPORTED` on an unprepared face, so a
    painter that resolved glyphs without calling `robo_font_face_parse_cmap` would draw nothing
    and report every code point missing — a rendering-shaped failure with a font-shaped cause.
    The pass now prepares the map once, through the same door the layout measurer uses, which is
    also what keeps the two layers from disagreeing about the same face.

And 11.8's, which are the ones a release about the thing already carried everywhere finds:

14. **A containment test written backwards refuses exactly the documents it was written to
    accept.** The builder's "a scope is no wider than the boundary it is inside" check was
    spelled `narrow.start > outer.start || narrow.end < outer.end` — the opposite predicate.
    An inset child (`[20, 200]` inside `[0, 200]`) satisfies `20 > 0`, so every document with a
    **nested** scope was refused as "wider than the boundary it is inside", while a document
    with a single scope never reached the check and passed. It took a case whose expectation is
    recomputed from the tree — and a second scope — to see it.
15. **The one case a clip model most needs is the one a naive intersection refuses.** Two
    disjoint intervals intersect to `[200, 40]`, which `robo_paint_clip_is_valid` throws out —
    so "a box an ancestor excluded" had no representation. Canonicalizing to `[max(start),
    max(start)]` makes it a valid empty record, and the choice of *where* the empty interval
    lands is a decision (a function of the two inputs) rather than a clamp that hides one.
16. **Reading the artifact's scopes makes the stack load-bearing, and only a hand-built list
    can show it.** For every list this build produces, the command's own `clip` and the scope's
    are equal — the builder refuses a disagreement — so a test that used documents alone could
    not tell a pass that reads the stack from one that reads the field. The case that can is a
    list whose drawing carries **no** clip of its own inside an empty scope: it must draw
    nothing.

---

## 15. What Phase 11.3 onwards may assume

1. `robo_paint_context_run` produces the full operation sequence for a laid-out tree, in
   document order, deterministically, and refuses a stale tree rather than painting it.
2. `robo_paint_operation_local_boxes` equals `robo_paint_boxes_of` for every operation of
   every box — the two coordinate spaces are consistent by assertion.
3. The boxes nest (border ⊇ padding ⊇ content), are finite, and are saturated rather than
   wrapped.
4. The clip is two axes and accumulates per axis; `robo_paint_clip_rect` answers only when both
   axes are bounded, and a disjoint intersection is canonicalized to a valid **empty** interval
   rather than left running backwards (11.8, §19.3).
5. An operation is a value with no pointers, and a list owns exactly one array of them; the
   list is safe to discard and regenerate.
6. `robo_paint_list_append` refuses any operation `robo_paint_operation_is_valid` rejects, so
   every list is a list of records.
7. A text run produces one `TEXT` record rather than a structural triple, emitted as a child of
the box that owns the text node: one depth past that box, naming itself, and after that box's
`CONTENT`. A run wider than its line wraps whole, so two line fragments are two runs.
8. `stats.style_only` is the input a paint-invalidation system will consume; there is no
   paint-invalidation mechanism yet and no per-list currency check.
9. There are no new coordinate types: geometry is RoboGraphics', re-exported by layout, read
   here.
10. **The sequence is regular**: exactly one structural triple (`BEGIN_BOX`, `CONTENT`,
    `END_BOX`) per placed box, in that order, with the box's own drawing records between the
    opening marker and the content boundary and its subtree between the boundary and the
    closing marker — plus a `PUSH_CLIP`/`POP_CLIP` pair around that subtree for a box whose
    boundary **changed** (11.8, §19). A consumer can group a box's records by `depth` and by
    those boundaries alone, and one that ignores the pair entirely still sees a boundary on
    every drawing record it reads.
11. **A record is a snapshot that does not move.** Laying the same tree out again, or
    destroying it, leaves an existing list's records byte-identical — `paint.records.snapshot_
    isolation` pins it. A display list built from these records therefore does not need to
    copy defensively.
12. `robo_paint_op_kind_is_structural` / `robo_paint_op_kind_draws` partition the vocabulary
    above `NONE`, so a new kind is added by choosing a side rather than by extending a
    two-member special case.
13. **A display list exists** and is the render-ready artifact: `robo_paint_display_list_builder_build`
    converts a paint list into a `robo_paint_display_list_t`, one command per record, in the
    same order, with the box model resolved into a single rectangle per command (§16).
14. A display command is a value with no pointers; a display list owns one array of them and
    is safe to discard and regenerate, in any order relative to the records and the tree.
15. A display list is **regular** — the structural skeleton is an invariant the builder
    enforced and refuses to violate; an out-of-order record list produces an empty list and
    a `ROBO_PAINT_DOMAIN_DISPLAY_LIST` diagnostic.
16. **The drawings reach a surface** (11.4–11.6): `robo_paint_decoration_paint` executes every
    **drawing** command of a display list into a caller's `robo_gfx_surface_t` — a background's
    border box, each border side's band, and a text run's glyphs, in **one walk in list order**
    — and **skips** the structural commands, counting them. The colour crosses through one
    door, `robo_paint_color_to_rgba8`, which is a field-for-field copy between two 8-bit sRGB
    records (§17).
17. A drawing is bounded by the **clip in force**, per axis, applied through the raster's own
    clip at one point in the pass — the scope's clip when a `push-clip` is open, else the
    command's own (11.8, §19). The surface belongs to the caller: nothing is retained or
    destroyed by the pass, and the list is read through `const`.
18. **A border's style travels** (11.5): the `border-style` is a required field of a
    `draw-border` command and of the `BORDER_*` record, because it is the one fact a band's
    rectangle cannot state. The band is what this build fills; the seven pattern styles are
    drawn as that band and counted (`unshaped`).
19. **`raster` is still 0.** Nothing consumes a display command other than the four drawing
    kinds, and there is no mechanism that rasterises a list end to end.
20. **A run's characters live in a pool** (11.6): a `TEXT` record and a `draw-text` command carry
    a **span** — two integers — into a code-point pool their list owns, so the record stays a
    value with no pointers in it and the snapshot property survives the one payload that is not a
    number. The span counts **code points**, and the display list owns a pool of its own, so
    clearing the records cannot change a character the list draws (§18).

---

## 16. The display list (Phase 11.3)

`robo_display_command_t` is the layer above the record: it carries the **one rectangle a
command acts on, resolved**, so a rasteriser never reads the box model.

| Paint operation | Display command | `rect` |
|---|---|---|
| `BEGIN_BOX` | `BEGIN_NODE` | border box |
| `BACKGROUND` | `DRAW_BACKGROUND` | border box |
| `BORDER_*` (four) | `DRAW_BORDER` (one) | the side's **band** |
| `CONTENT` | `CONTENT` | content box |
| `PUSH_CLIP` / `POP_CLIP` (11.8) | `PUSH_CLIP` / `POP_CLIP` | the box's **padding box** |
| `TEXT` | `DRAW_TEXT` | the run's rectangle (its advance box) |
| `END_BOX` | `END_NODE` | border box |

The mapping is a **total function** over the record vocabulary, so the two lists are the same
length and pair positionally; `node`, `depth`, `clip` and `colour` carry through unchanged.
Two consequences are worth naming because they look like losses and are the point: the
**padding box stops being an input** (it was one to the band derivation, and since 11.8 it is the
rectangle the two clip commands carry instead, because a box's boundary is defined at its padding
edge), and the **four border kinds collapse into one** (the side is consumed into the band
rectangle). The coordinate space is unchanged — paint coordinates, one page space.

One field *does* travel with the collapse (11.5): the **`border-style`**. The side is consumed
into the band rectangle, which is right — the rectangle states where the side is — but the
style is the shape *inside* it and is not derivable from the rectangle, so a `draw-border`
carries it and a command without one is refused. A `draw-background`, which has no side, carries
`none`; the validity rule splits exactly that way (`generates_border` for a band, `none` for
every other kind).

The vocabulary is **nine** kinds (11.6 added `DRAW_TEXT`, 11.8 the scope pair), split
structural/drawing the record's way, and the validity rule splits on *drawing* rather than
*marker* so `CONTENT` and the two clip kinds are held to the structural side. A `draw-text` carries its run beside the rectangle — the span into this
list's pool, the baseline offset and the font inputs — which is the one field on the model that
points at another container rather than being a value.

The **builder** is the phase's second claim: a paint list is a container that validates each
operation but not their sequence, so the builder walks the records with a stack of the open
boxes and **refuses** a list that is not a traversable document (a box that never closes, a
boundary in the wrong place, a marker naming the wrong node, a decoration after the content
boundary). A refused build leaves the output list **empty**. The builder is a stateful object
because the check needs that stack, and it is reused across builds.

Ownership is the paint list's: a display list owns one array of value commands and reaches
nothing else, so 11.2's snapshot property is the display list's too — mutating the record
list or re-laying-out the tree leaves it byte-identical (`paint.display.mutation_isolation`).

See `paint/docs/DisplayList.md` for the precise contract.

---

## 17. Decoration painting (Phases 11.4–11.6)

`robo_paint_decoration_paint(list, target, face, out_stats, diag)` is the first door in this
module that takes a **surface**, and the shape of the whole phase is in its signature:

- the input is a **display list**, not the records, because the display list is the artifact
  whose geometry is resolved. A command carries the one rectangle to draw, the boundary in force
  and, for a band, the style that shapes it -- so the painter never reads the box model, a
  computed style or the tree, and the only conversions left are the colour and, for text, the
  characters;
- the **face is the caller's** too (11.6), and it is nullable: a text command with no face is
  counted (`no_font`) and skipped, so a caller that has no font still gets a painted page and is
  *told* which of its commands needed one;
- the output is a **caller's** surface. The painter retains nothing and destroys nothing it did
  not create: it binds a `robo_gfx_raster_t` over the target for the duration and releases it
  before returning, so the clip and the source-over blend are the graphics module's rules
  rather than a second compositor's;
- what it does *not* touch is as explicit as what it does. `begin-node`, `content` and
  `end-node` draw nothing by definition -- all three are **skipped and counted**, which is what
  makes "the drawings reached a surface and the structural commands did not" a number a reader
  sees (`skipped`) rather than a silence.

### 17.0 One walk, because there is one order

11.5's architectural decision, and the reason the background-only door of 11.4 was *replaced*
rather than joined: a box's background, its four bands and its text runs are all drawings, and the
list interleaves them the way the traversal emits them. CSS's paint order is that same order, so a
descendant's background is filled **after** an ancestor's border and a child that spills over its
parent draws over the parent's band -- the suite's decisive case. Three passes, one per kind, would
fill every background in the document before every border and draw every character after every
band: a different and visibly wrong picture. The fence 11.4 drew (`background_painting` 1,
`raster` 0) is unchanged; what moved is that one command kind became three, which is why
`border_painting` and `text_painting` now sit beside `background_painting`. 11.6 **extended the
door with one argument** rather than adding a fourth walk, because a second walk would have
re-introduced exactly the ordering problem 11.5 removed.

### 17.1 The colour bridge

`robo_paint_color_to_rgba8` is the whole of the conversion and it is a **field-for-field copy**
between RoboCSS's `robo_css_color_t` and RoboGraphics' `robo_gfx_color_rgba8_t`: both are 8-bit
sRGB with straight alpha, so there is no arithmetic, no colour space and nothing to lose or
invent -- including the alpha channel, which survives rather than being flattened.

The four CSS kinds are **decided rather than flattened**, because three of them are not colours
a surface can take: `currentColor` is refused (`ROBO_NOT_SUPPORTED`, because only the layer that
owns an element's `color` can resolve it -- that is the paint pass, not this bridge), `none`
answers `ROBO_NOT_FOUND` (nothing was declared), and `invalid` answers
`ROBO_INVALID_ARGUMENT` (it was meant to be a colour and was not). A fully transparent colour is a
*colour*: it converts and it does not draw, and `robo_paint_color_is_drawable` is exactly that
pair -- which is the same predicate the paint pass used to decide whether to emit the record.

### 17.2 The clip, read rather than implemented

A command carries the boundary in force from 11.1 as two axes, so the painter applies **the
boundary it was given** and invents no clipping architecture: reset the raster's clip to the
surface bounds (so nothing leaks from the previous command), replace the axis a boundary hides
while leaving the open axis at the surface's own interval, and let `robo_gfx_raster_set_clip`
intersect. "The boundary bounds the fill" is therefore the graphics module's arithmetic rather
than a second copy of it, and an `overflow: hidden` ancestor bounds a descendant's background
correctly without a raster clipping system existing yet. Since 11.8 the boundary the pass applies
is the **scope's** when a `push-clip` is open and the command's own otherwise (§19), which is one
application point for every drawing kind.

### 17.3 What the pass reports

`robo_paint_decoration_stats_t`'s fields are the **pairs**, because the interesting facts are
differences, and three identities hold by construction and are asserted:
`commands = draws + skipped`, `draws = backgrounds + borders + texts + images`, and
`draws = filled + contextual + transparent + empty + no_font + no_resource`. `transparent` and `contextual` are
kept apart because "the author asked for nothing" and "the colour could not be resolved here" are
different facts about a document, and `no_font` sits on the far side of `draws` from those three
because it is a fact about the **call** rather than the document. `unshaped` is a fourth kind of
fact rather than a fourth outcome -- it is a subset of `filled`, and it is the number that keeps
`border_painting` from being read as "every border looks the way it was asked to".

### 17.3b Glyphs, and a run that is not a rectangle

A `draw-text` is the one drawing whose rectangle is its **advance box** rather than a region to
fill, and two rules follow from that. It is **exempt from the zero-area rule** -- a glyph's ink can
overhang its advance box, so a rectangle with no area is not "nothing to draw" for a run the way it
is for a fill. And it is drawn by **resolving each code point through the caller's face**, at the
face's own advances, starting at the rectangle's left edge and the baseline at
`rect.y + baseline_offset`.

The pass **prepares the face's character map** once before the first lookup, because a
`robo_font_face_t` is created without one and a lookup on an unprepared face misses; that is the
same door the layout measurer calls before it resolves a glyph. A face whose map will not parse is
still not a refusal: every glyph becomes `.notdef` and `glyphs_missing` names the fact per code
point, so a broken font degrades to the shape of a font with no glyph for the text rather than to
a failed pass. A run is drawn in **logical order** and the RTL runs are counted (`rtl`) -- there is
no bidi in this build and a count is how it says so. `paint/docs/Text.md` has the payload, the
pool, the record and the deferrals.

### 17.3a The band, and the style that shapes it

A band is the region of the border box outside the padding box on one edge, computed once at
build time by `robo_paint_operation_side_band` and carried as the command's one rectangle. The
four bands of a box are emitted top, right, bottom, left, each spanning its axis's full extent,
so two bands overlap at every corner and the one drawn last owns it -- a statement about this
build's simplification (a real renderer mitres), pinned by the suite's corner colours rather than
left to be discovered. `paint/docs/Border.md` has the geometry, the style vocabulary and the
deferrals.

### 17.4 Safety

A `NULL` list or target is `ROBO_INVALID_ARGUMENT` with a diagnostic naming which; a `NULL`
**face** is legitimate and simply counts every text command `no_font`; an empty list is a
legitimate pass whose every counter is nought; a **read-only** or implementation-absent format is
`ROBO_NOT_SUPPORTED`, the graphics layer's own refusal mapped once (`ROBO_OUT_OF_MEMORY` is
deliberately *not* folded in, because that is this build failing rather than the caller's surface
being wrong); a drawing whose span names no run in the list's pool is **skipped** rather than
read, for the reason the geometry rule is enforced at the container; and a fill, glyph or clip the
graphics layer refuses is `ROBO_ERROR` with a diagnostic naming the step.

---

## 19. Clipping (Phase 11.8)

### 19.1 The boundary in force is a scope

11.1 gave the module a clip *record* and one accumulation: a box's `overflow`/`overflow-x`/
`overflow-y` resolved to a per-axis boundary, intersected with what an ancestor had already put in
force, carried down the traversal as a value. That answers **what bounds this drawing** and not
**where does this boundary begin and end** — a flat list carried the same two intervals on every
command and nowhere said that the box under command 12 is what narrowed them.

The artifact now states it. A box whose boundary for its **contents** differs from the boundary it
was **entered** under emits `PUSH_CLIP` after its own `CONTENT` boundary and a matching `POP_CLIP`
before its `END_BOX`, both carrying the same effective clip and the box's padding box as their
rectangle. Consumers read a flat sequence against a `robo_paint_clip_stack_t`:

| Reader | Stack | Why |
|---|---|---|
| the traversal | none — the clip is a **value** (a call parameter) | a recursive walk's "what is in force here" is the call's own argument |
| the display-list builder | one | to prove every command inside a scope agrees with it and that the scopes pair, narrow and close |
| the decoration pass | one | to know what bounds a drawing without re-deriving anything |

There is no global clip state anywhere in the build, and a stack is created and destroyed by its
reader like every other list here.

### 19.2 The condition, and why `clipped` and `clip_scopes` differ

A scope is opened on exactly one condition — the intersection **changed** the boundary — which is a
comparison of two records rather than a flag. A box with `overflow: visible` narrows nothing, and a
box with `overflow: hidden` inside an ancestor that already bounded the same region narrows nothing
either; neither is emitted, because a scope that establishes nothing is two commands a consumer
could only skip. `stats.clipped` counts boxes whose own modes say an axis hides; `stats.clip_scopes`
counts the boundaries that actually changed; the difference is the number of scopes the traversal
*declined* to open and is a property of the document rather than a rounding.

### 19.3 The canonical empty interval

An intersection can come out disjoint — a box entirely excluded by an ancestor's boundary gives
`start > end` — and such a record is not one this build may read. It is canonicalized to the
zero-length interval at the larger of the two starts, so "nothing can be drawn here" is a valid,
empty record that survives a push, a pop, a display command and a projection to a rectangle of zero
width. A scope whose clip is empty suppresses the drawings inside it and is popped normally: it
poisons the drawing, never the stack.

### 19.4 What the builder refuses

A display list's scopes are an invariant rather than a convention. A clip command must belong to
the innermost open box, come after that box's `CONTENT`, open at most one scope per box, and
establish something **new** and **no wider** than what is in force; a drawing inside a scope must
carry the same clip the scope does; a pop must match the innermost open scope in node, depth and
clip; and every scope must close. Each failure is its own message, so a refusal names the way the
list lies. Any refusal leaves the output list **empty**.

### 19.5 Where it is claimed, and what it does not claim

`clipping` is **1**: a box's overflowing contents are bounded by a rectangle the traversal derived
and the artifact states, for every drawing kind. It does not claim rounding, `clip-path`, masks or
filters; clip-aware hit testing (11.10); scrolling (nothing scrolls, and a clipped box is a
boundary rather than a container); a compositor's or a tile scheduler's clip; paint invalidation
(11.11); or a rasteriser that consumes the whole list (`raster` is still 0).
`paint/docs/Clipping.md` is the phase's own document — the stack's doors, the intersection rules,
the emission arrangement, the dump and the phase's findings.

See `paint/docs/Decoration.md` for the full contract, and `paint/docs/Border.md` for the border
half of it.

---

## 20. Opacity (Phase 11.9)

### 20.1 A colour's alpha and an element's opacity are two facts

`rgba(255, 0, 0, 0.5)` makes a *colour* half transparent. `opacity: 0.5` makes a **box** half
transparent: its background, its border, its text and every descendant it contains composite at
half strength, and the colours they were declared with are untouched. Every release from 11.1 to
11.8 had only the first — a `draw-background` command carried a colour and a boundary — and the
second is what this phase adds.

The property arrives from RoboCSS (`Include/RoboCSS/RoboCSSOpacity.h`): a real in `[0, 1]` whose
initial value is **1**, not inherited, and **clamped** rather than rejected, because CSS clamps and
an `opacity: 150%` that became an invalid declaration would make a document that renders correctly
elsewhere render as if the declaration were absent. A value that is not a number at all answers the
initial value — *visible* — because a record nothing can read must render the way an element
nothing declared does.

### 20.2 The scope opens at the marker, and the reason is CSS's

The two scope pairs nest differently around one box, and neither placement is a style choice:

| Pair | Opens | Because |
|---|---|---|
| `PUSH_CLIP` / `POP_CLIP` | after the box's `CONTENT` | a clip belongs to a box's **contents** — CSS paints that box's own background and border under its *parent's* boundary |
| `PUSH_OPACITY` / `POP_OPACITY` | after the box's `BEGIN_BOX` | an opacity belongs to the box **itself** — CSS paints that box's own background and border at the box's own opacity |

One rule holds both: **every record carries the state in force at its own position in the
sequence.** So `BEGIN_BOX` and `END_BOX` carry the *enclosing* strength while the decorations, the
content boundary, the subtree and the pop carry the box's — and both halves of a pair carry the
value the pair establishes, which is what makes a pair checkable against itself.

### 20.3 The arithmetic, and the two invalid directions

`robo_paint_opacity_multiply(outer, inner)` is the accumulation and the only place it happens: the
traversal computes `child = multiply(in_force, declared)` once per box and passes it down as a value,
exactly as the clip is. Both operands are records in `[0, 1]` so the clamp cannot fire for two
records this build made; it is applied anyway because the same door is the one a caller uses to
combine two values it assembled itself.

Three doors answer for a value they cannot read, and the directions are not uniform — which is
deliberate rather than an oversight:

| Door | Answers | Why |
|---|---|---|
| `robo_paint_opacity_from_value` (NaN/∞) | **opaque** (`1.0`) | **constructing** a record: the useful answer is what a document that declared nothing gets, so a caller's arithmetic bug renders as an unsurprising document rather than a blank one |
| `robo_paint_opacity_value`, `_is_opaque`, `_is_transparent` (NULL) | opaque / no-in-force | a predicate about the **state**: "no scope" and "a full-strength scope" both mean nothing inside changes |
| `robo_paint_opacity_to_byte` (invalid) | **`0`** — no ink | **consuming** a record, at the last gate before a pixel: a value nothing can read must not put ink on a surface, and a blank rectangle is visibly wrong where a fully drawn one is not. Unreachable from this build's own path, so it is the gate's own defence |

`_is_valid` is the one door that answers **no** for a `NULL`, and the asymmetry is the same one
`robo_paint_clip_is_valid` makes: the doors above answer *what is in force*, which a `NULL` has an
answer to, while this answers *is this a record*, which a `NULL` is not.

### 20.4 What the builder refuses, and the two findings

Eight refusals, each with its own message and an empty output list: a scope belonging to no open
box; one naming a box other than the innermost open one; two pushes from one box; a push **after
the box has begun drawing** (after a decoration or after its content boundary — the case a build
placing the scope at 11.8's point would fail); a push that establishes nothing; a push **stronger
than the strength it is inside**; a pop with nothing open; and a pop whose strength is not the one
its push carried. The strength ordering is what makes the in-scope rule rely on a checked
invariant rather than a hope.

Two findings, both from tests rather than from reasoning:

- **A run must not read its own declaration.** A text node's box is the element that owns it, and
  that element has already opened a scope around it — so a run reading its own style's `opacity`
  would apply the same declaration twice and square it. The strength is **passed in** to the text
  emitter. The case that would catch a regression counts three faded drawings for `0.5`/`0.5` with a
  run inside, the third at `0.25` rather than `0.125`.
- **A missing declaration must answer *opaque*.** `robo_css_computed_value_opacity` answers the
  initial value for a computed value that is not an opacity, and that door is the only reason a
  document with no `opacity` in it is not blank.

### 20.5 Where it is claimed, and what it does not claim

`opacity` is **1**: a drawing has a strength on top of its colour's alpha, accumulated down the path
that reaches it and applied through `robo_gfx_raster_set_opacity` at one point in the pass, for every
drawing kind. It does not claim **group** semantics — CSS composites an element's subtree into a
flattened layer and fades that, while this build multiplies per drawing, so two overlapping
descendant fills differ from a browser's result; `paint.opacity.group_deviation` is the document that
measures the gap, with both answers computed through the graphics module's own blend. It alsodoes not claim blend modes, filters, masks, transforms, a compositor, compositor layers or tile
compositing, `border-radius` clipping, scrolling or paint invalidation (11.11).
`paint/docs/Opacity.md` is the phase's own document — the record, the stack, the placement rule, the
refusals, the counters, the pixels and the phase's findings.

## 21. Hit testing (Phase 11.10)

### 21.1 The inverse query, answered by the same records

Everything §4 through §7 established is a statement about **drawing**: an operation is a record of
what will be drawn, the clip is the boundary it is drawn inside, the opacity is the strength it is
drawn at, and the traversal is the order the drawings come in. This phase asks the *inverse*
question of the same tree — which node is drawn at this point — and the answer is composed entirely
of those records rather than of a second model of any of them.

It lives in RoboPaint rather than in a module beside it, and §3 is the reason: a hit test is a second
consumer of `robo_paint_clip_t` and `robo_paint_opacity_t`, and a second consumer of **the walk**.
The painter accumulates a page origin, narrows a boundary and multiplies a strength while descending;
the hit test accumulates the same three things in the same order from the same declarations. That is
what makes "the two agree about where a box is" assertable, and it is why `hit_testing` joins
the capability model of §10 as its sixth field rather than as a new module's first.

### 21.2 The coordinate model, extended by nothing

§6's two spaces stand, and the phase adds a **name** rather than a space:

| space | what it is | who writes it |
|---|---|---|
| **layout** | relative to the containing block's content origin | RoboLayout |
| **paint** | one page space: the accumulated origins | the traversal |
| **window** | what an input layer reports a pointer in | RoboWindow |

`robo_paint_hit_test()` takes its point in **paint** coordinates, and **paint coordinates are window
coordinates in this build** — the traversal begins the root at `(0, 0)` and there is no scroll
offset, transform, zoom or device-pixel-ratio scale to undo. The two names are kept apart because the
phase that adds scrolling is the phase that makes them differ. The result carries the point in both
spaces (`point`, and `local = point - origin`), so a caller comparing against a layout rectangle uses
the space RoboLayout wrote and the round trip is checkable.

### 21.3 The four rules, and where each mirrors the painter

| Rule | Mirrors | Why it is the rule |
|---|---|---|
| **last drawn wins** — children before their parent, last child before first | §4.1's order: the box's own decorations, then its children left to right | the node that covers a point is the last one drawn there, so a document-order walk answers with the box underneath |
| **a box's region is its border box** | §6.2: the union of the three layers it could paint into | CSS hits a transparent `div` over its whole box, because painting nothing is not the same as being absent; an **empty** rectangle contains no point at all |
| **the boundary clips the contents, never the box** | §7 and 11.8's arrangement: the scope opens at the content boundary | a `overflow: hidden` box's own border band is drawn outside its own boundary and must still be hittable, while its child outside that boundary must not be |
| **a strength does not gate** | — | CSS governs hit testing with `pointer-events`, not `opacity`; this build has none, so the property's initial value applies: everything is hit-testable. The strength travels in the result so the phase that adds the property has the number |

The first three are what make the walk a *reverse* walk rather than a search: the subtree bound and
the boundary in force are the **early-outs**, and they are consulted in that order because the bound
is a question about geometry, which node could possibly be here, where the boundary is a question
about drawing.

### 21.4 The result is a value, and the trace is the walk's control flow

`robo_paint_hit_test_result_t` copies every fact in — the identity, both coordinate spaces, the three
rectangles, the boundary and the strength — for §9's reason: a caller holding a pointer into the tree
would hold a view the next mutation can move, and a click that changed meaning when a style was
recomputed is worse than no answer.

The **outcome** is a vocabulary rather than a boolean, and the third member is why: `STALE` says the
tree carries a layout bit, so no box's geometry is final and the query **refused**. That is §8's
argument restated — a hit test against half-computed geometry is not a less accurate answer, it is a
**wrong** one, because it would report whichever node happened to hold a stale rectangle. It is an
outcome and not an error, and `robo_paint_hit_test()` returns `ROBO_OK` for it, exactly as
`robo_paint_context_run()` returns `ROBO_OK` for `REFUSED`.

The dump is the walk's **control flow**, one line per object examined, with a five-member reason
vocabulary (`enter`, `unplaced`, `region`, `clip`, `hit`) rather than a per-decision log. There is no
reason for "tested and not the answer", deliberately: it would be on nearly every line and carry no
information, and the line that *should* have been the `hit` one is found by looking for it.

### 21.5 Where it is claimed, and what it does not claim

`hit_testing` is **1**: a point in paint coordinates resolves to the layout node drawn there, under
the boundary that node's own `overflow` puts in force, with the strength in force on it carried in
the answer. What it does **not** claim: event dispatch or bubbling (Phase 12's — the result names one
node and guesses at no ancestor), character- or caret-level text hit mapping (a run is one rectangle
and one node), `pointer-events`, `visibility` (registered in RoboCSS and consumed by no layer in this
build, so reading it here would refuse a point on something visibly drawn), `border-radius`,
`clip-path`, masks, filters, blend modes, transforms, alpha-mask or SVG geometry, scrolling, zoom,
device-pixel-ratio handling, or a compositor's hit testing. `paint/docs/HitTest.md` is the phase's own
document — the four rules, the counters and their three identities, the two shapes of broken tree it
refuses, the trace, and the screen-agreement case.
