# RoboPaint — architecture (through Phase 11.2)

RoboPaint is the fifth module in the layout stack. It sits directly above RoboLayout and
directly below RoboRender (`DEPENDENCIES.md`), and it turns the completed geometry RoboLayout
produced into a **deterministic sequence of operations** a rasteriser will one day execute.
This release is the boundary between those two and nothing above it: it establishes the
context, the operation vocabulary, the list and its ownership, the deterministic traversal,
the geometry a rasteriser reads, and the clip state. It does not draw.

---

## 1. What the phase is

Phase 10 ended with every box carrying a position, a size, four inset layers, a final
coordinate, an overflow record and a validity state. Phase 11.1 asks the one question that
model makes askable — *what will be drawn, in what order, under what boundary?* — and answers
it as **data** rather than as drawing. The pipeline this phase establishes, and stops at the
third arrow:

```text
HTML / DOM → RoboCSS → RoboLayout → RoboPaint → operations → (display list) → RoboGraphics
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

Eight headers under `Include/RoboPaint/`, one umbrella:

| Header | What it holds |
|---|---|
| `RoboPaintTypes.h` | `robo_paint_op_kind_t` (8 values), `robo_paint_diagnostic_domain_t` (9), their predicates and name doors |
| `RoboPaintDiagnostics.h` | `robo_paint_diag_t` and `ROBO_PAINT_DIAG` |
| `RoboPaintGeometry.h` | `robo_paint_boxes_t`, `robo_paint_clip_t`, `robo_paint_boxes_of`, `robo_paint_clip_intersect` |
| `RoboPaintOperation.h` | `robo_paint_operation_t`, its accessors, validity, side band, one-line writer |
| `RoboPaintList.h` | `robo_paint_list_t` — create/clear/append/at/operations/equal |
| `RoboPaintContext.h` | `robo_paint_context_t`, `robo_paint_stats_t`, `robo_paint_pass_outcome_t`, `robo_paint_context_run` |
| `RoboPaintDump.h` | `robo_paint_list_write` |
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
    robo_paint_op_kind_t kind;      /* the seven, plus NONE */
    robo_layout_node_id_t node;     /* never NONE for a live operation */
    size_t depth;                   /* 0 for the root: makes the tree recoverable */
    robo_paint_boxes_t boxes;       /* the three rectangles, in paint coordinates */
    robo_layout_point_t origin;     /* the containing block's content origin */
    robo_paint_clip_t clip;         /* the boundary in force */
    robo_css_color_t color;         /* none for a marker */
} robo_paint_operation_t;
```

### 4.1 The vocabulary: structural and drawing

`NONE`, `BEGIN_BOX`, `BACKGROUND`, the four `BORDER_*` kinds, **`CONTENT`**, `END_BOX`. There
is no `TEXT`, `IMAGE`, `CLIP`, `SAVE`, `RESTORE` or `OPACITY` enumerator, even though the
phase's brief lists all six as things the representation must *eventually* express. The
reason is the one RoboLayout 10.2 gives for not adding a constant for a feature it does not
have: a name with no code behind it is a promise, not a vocabulary. What makes them
expressible is the record's shape — adding a kind is one enumerator, one name row and one
case in the validity rule, never a redesign of the list.

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
- **Text.** A text run is visited and counted (`text_runs`) and **not painted**. A run's
  rectangle is a line fragment with no box layers, and painting one means shaping and glyph
  positions, which a later phase owns; a painter that summed advances in logical order would
  be claiming a text renderer it does not have. The count exists so "this document has text
  and none of it was painted" is a number a reader sees rather than a silence.
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

The clip edge for a box's contents is its **padding box**: CSS clips a box's overflowing
contents at its padding edge, and RoboLayout's own clip rectangle reads the same box one layer
down. Nothing in RoboPaint decides whether to clip beyond re-reading the box's own used modes.

---

## 8. The pass, and the staleness refusal

`robo_paint_context_run(context, tree, list, diag)` walks the tree in document order — parents
before children, children in DOM/layout order — and, for every box nothing has to be asked
about:

1. emits `BEGIN_BOX` with the box's three rectangles in paint coordinates, the accumulated
   origin, the clip in force and the box's depth;
2. emits `BACKGROUND` when the box's background declaration draws;
3. emits one `BORDER_*` per side whose used width is above zero and whose colour draws;
4. recurses into the box's children, in order, under the boundary this box's own overflow put
   in force;
5. emits `END_BOX`.

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
list of names, so a field added without an assertion is a visible omission. Eight of its
eleven fields are **1** (`context`, `operation_model`, `paint_list`, `traversal`,
`box_geometry`, `clip_state`, `decoration_ops`) and three are **0**:

| Field | Value | Why |
|---|---|---|
| `drawing` | 0 | no colour reaches a surface; the field a caller about to look for a framebuffer should read |
| `display_lists` | 0 | a display list as a consumable artifact is a later goal |
| `raster` | 0 | rasterising a list into a RoboGraphics surface is a later goal |
| `paint_invalidation` | 0 | 11.11; what exists instead is its *input*, `stats.style_only` |

`decoration_ops` is **1** and is named for what it is rather than what a reader might hope:
this build decides *that* a background will be filled and *which* colour, and records it. It
does not fill it. Keeping `decoration_ops` and `drawing` apart is what stops "a background
operation exists" from being read as "a background was painted".

`ROBO_PAINT_DRAWING_IMPLEMENTED` is 0 and is read by two doors that must not disagree —
`robo_paint_get_capabilities().drawing` and `robo_paint_drawing_is_implemented()` — and by the
conformance suite, which asserts all three together. What the constant will claim when it
flips is stated now so the flip is a decision rather than a discovery: a build in which an
operation's rectangle and colour reach a RoboGraphics surface. It will **not** claim
antialiasing, rounded corners, gradients, opacity beyond the colour's own alpha, or a
compositor.

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

What 11.1 does **not** do, each named where it would otherwise be assumed:

- **No drawing.** `DRAWING` is 0. No raster, framebuffer, compositor or GPU.
- **No display lists as the final system** — only the operation sequence one will be built
  from.
- **No text or image operations.** Text is counted, not painted.
- **No opacity, transforms, `border-radius`, gradients or antialiasing.**
- **No actual clipping** — the clip is computed and carried; no raster op is emitted. No scroll
  clipping, no compositor clipping.
- **No `background-clip`/`background-origin`** — the border box is CSS's initial value.
- **No `z-index` or stacking context.** Document order; a later stacking pass reorders.
- **No paint invalidation (11.11) and no hit testing (11.10).** The final positioned geometry
  is preserved for both; neither is built.
- **No scrolling** and no scroll offsets — 10.13 recorded the overflow; nothing scrolls it.

---

## 14. What 11.1 and 11.2 found

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

---

## 15. What Phase 11.3 onwards may assume

1. `robo_paint_context_run` produces the full operation sequence for a laid-out tree, in
   document order, deterministically, and refuses a stale tree rather than painting it.
2. `robo_paint_operation_local_boxes` equals `robo_paint_boxes_of` for every operation of
   every box — the two coordinate spaces are consistent by assertion.
3. The boxes nest (border ⊇ padding ⊇ content), are finite, and are saturated rather than
   wrapped.
4. The clip is two axes and accumulates per axis; `robo_paint_clip_rect` answers only when both
   axes are bounded.
5. An operation is a value with no pointers, and a list owns exactly one array of them; the
   list is safe to discard and regenerate.
6. `robo_paint_list_append` refuses any operation `robo_paint_operation_is_valid` rejects, so
   every list is a list of records.
7. Text runs are visited and counted; painting them is a later phase's.
8. `stats.style_only` is the input a paint-invalidation system will consume; there is no
   paint-invalidation mechanism yet and no per-list currency check.
9. There are no new coordinate types: geometry is RoboGraphics', re-exported by layout, read
   here.
10. **The sequence is regular**: exactly one structural triple (`BEGIN_BOX`, `CONTENT`,
    `END_BOX`) per placed box, in that order, with the box's own drawing records between the
    opening marker and the content boundary and its subtree between the boundary and the
    closing marker. A consumer can group a box's records by `depth` and by that boundary
    alone.
11. **A record is a snapshot that does not move.** Laying the same tree out again, or
    destroying it, leaves an existing list's records byte-identical — `paint.records.snapshot_
    isolation` pins it. A display list built from these records therefore does not need to
    copy defensively.
12. `robo_paint_op_kind_is_structural` / `robo_paint_op_kind_draws` partition the vocabulary
    above `NONE`, so a new kind is added by choosing a side rather than by extending a
    two-member special case.
