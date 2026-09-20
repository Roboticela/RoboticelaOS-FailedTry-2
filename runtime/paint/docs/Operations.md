# RoboPaint operations and the traversal contract

This document states the two things `RoboPaintTypes.h` names as the traversal's contract:
**what an operation is** and **the order a pass emits them in**. The architecture they sit
inside is in `Architecture.md`; this is the precise shape.

---

## 1. The operation vocabulary

```c
typedef enum robo_paint_op_kind {
    ROBO_PAINT_OP_NONE         = 0,   /* a cleared record; never appended */
    ROBO_PAINT_OP_BEGIN_BOX    = 1,   /* enter a box: draws nothing */
    ROBO_PAINT_OP_BACKGROUND   = 2,   /* fill the border box */
    ROBO_PAINT_OP_BORDER_TOP   = 3,
    ROBO_PAINT_OP_BORDER_RIGHT = 4,
    ROBO_PAINT_OP_BORDER_BOTTOM= 5,
    ROBO_PAINT_OP_BORDER_LEFT  = 6,   /* the band between the two boxes */
    ROBO_PAINT_OP_CONTENT      = 7,   /* the box's own records end, its contents begin */
    ROBO_PAINT_OP_END_BOX      = 8    /* leave a box: draws nothing */
} robo_paint_op_kind_t;
```

The order of the enumerators **is** the paint order: a box is entered, its decorations are
emitted in the order CSS paints them (background first, then the border), its own records
end at its **content boundary**, its children are emitted inside, and it is left. A consumer
that walked the list and ignored every `kind` would still see each box's operations
contiguous and in paint order.

The drawing vocabulary is deliberately small — one background and four border sides — and
has no `TEXT`, `IMAGE`, `CLIP`, `SAVE`, `RESTORE` or `OPACITY` enumerator. A name with no
code behind it is a promise, not a vocabulary; adding one later is one enumerator, one name
row and one case in the validity rule. `CONTENT` (added by 11.2) is the one kind since 11.1.

### 1.0 Structural records versus drawing records

The vocabulary splits in two, and the split is the rule a builder obeys:

| Half | Kinds | Emitted | Colour |
|---|---|---|---|
| **Structural** | `BEGIN_BOX`, `CONTENT`, `END_BOX` | for **every** placed box | `none` |
| **Drawing** | `BACKGROUND`, the four `BORDER_*` | **only when it draws** | a rasteriser-usable colour |

The two predicates are `robo_paint_op_kind_is_structural` and `robo_paint_op_kind_draws`, and
they partition the vocabulary above `NONE`. Structural records are what give the sequence a
regular skeleton — one triple per box — while drawing records make the list's length a
statement about the document. Structural rather than "marker" is the right split because
`CONTENT` is neither a drawing nor a box marker.

### 1.1 Four border kinds, not one kind plus a side

A `side` field would need a "not a border" value for the other kinds, and a field that means
nothing for most of its records is the "shape that reads as an answer" this tree refuses. With
a kind per side there is no such value, and `robo_paint_op_kind_side` is a **total function**
from the four border kinds to the side each draws on and a refusal for everything else — so no
operation can carry a side that disagrees with what it draws.

### 1.1a The one field whose meaning varies by kind, and the second one (11.5)

A record is nearly uniform: a node, a depth, three boxes, a clip. Two fields vary by kind, and
both are **required** to be the right thing rather than merely permitted:

| Field | A structural record | A background | A border side |
|---|---|---|---|
| `color` | `none` | the resolved `background-color` | the resolved `border-*-color` |
| `style` (11.5) | `none` | `none` | the side's `border-style`, which must **generate a border** |

The `style` is carried because it is not derivable one stage up: the display list resolves a
side into the band it occupies, and the band's rectangle states *where* the border is but not
*what shape* fills it. `robo_paint_operation_style` is its accessor, and the rule is
`robo_css_border_style_generates_border` rather than "not `none`" — a `hidden` border draws
nothing, which is why RoboLayout gives such a side a used width of zero and no record is emitted
at all.

### 1.2 Markers, and the content boundary

`BEGIN_BOX` and `END_BOX` draw nothing and carry no colour: their `color` is
`robo_css_color_none()`, and `robo_paint_operation_is_valid` requires exactly that, so a
marker carrying a colour is a refused record rather than one a consumer might draw. Their
purpose is the boundary: a rasteriser that will one day push a clip, save a state or blend a
group needs a marker to hang that on, and the depth the marker carries is what makes the
tree's shape recoverable from the flat list.

`CONTENT` is the third structural record and is **not** a marker
(`robo_paint_operation_is_marker` excludes it). It is emitted for every placed box, whether
or not the box declared a background, drew a border or has children, because what it states
is a fact about the *sequence*: this box's own records end here and its subtree's begin. It
acts on the **content box**, and it is what makes the boundary explicit rather than inferred.
Without it a consumer would look at the next child's depth to find where one box's own
records end, and a childless box would have no boundary at all.

---

## 2. The traversal contract

`robo_paint_context_run` walks the tree in **document order** — parents before children,
children left to right, following the layout tree's own `first_child`/`next_sibling` order —
and for each box emits:

```text
BEGIN_BOX        the box's three rectangles in paint coordinates, the accumulated
                 origin, the clip in force, the box's depth       (structural, always)
BACKGROUND       iff the declared background draws
BORDER_TOP       iff the top side's used width is above zero and its colour draws
BORDER_RIGHT     iff ...
BORDER_BOTTOM    iff ...
BORDER_LEFT      iff ...
CONTENT          the content box, the origin, the clip and the depth   (structural, always)
<the box's children, in order, under the boundary this box's own overflow put in force>
END_BOX          the same rectangles, origin, clip and depth as BEGIN_BOX (structural)
```

The invariants a consumer may rely on, and where the suite checks them:

| Invariant | Case |
|---|---|
| Each box's operations are contiguous, beginning with `BEGIN_BOX` and ending with `END_BOX`. | `paint.traversal.single_box`, `.empty_element` |
| Exactly one `CONTENT` per placed box, between its opening marker and its closing marker. | `paint.records.content_sequence`, `paint.records.fields` |
| A box's `CONTENT` precedes every one of its children's opening markers. | `paint.records.nested_order`, `paint.records.siblings_order` |
| A structural record carries `none` and a drawing record a usable colour. | `paint.records.fields`, `paint.conformance.document_list` |
| A box's decorations carry the **same depth** as its markers. | `paint.traversal.background`, `.border_sides` |
| Siblings appear in DOM order and interleave with their own subtrees, never flattened across. | `paint.traversal.siblings_in_order` |
| An operation is emitted **only when it draws**: no declaration, no operation; a zero used width, no border side. | `paint.traversal.background`, `.border_sides` |
| A `BACKGROUND` paints over the **border box** (CSS's initial `background-clip`). | `paint.traversal.background` |
| A box's children are emitted between its markers, under the boundary **its** overflow put in force. | `paint.traversal.clip_hidden`, `.clip_axis_split` |
| A text run is visited and counted and emits **no** operation. | `paint.traversal.text_counted_not_painted` |
| Two passes over the same tree produce identical lists. | `paint.traversal.counts_and_determinism` |

The traversal's contract is deliberately about **order and membership**, not about drawing:
nothing here says what a rectangle looks like on a surface, only which operations exist, in
what sequence, carrying what.

---

## 3. The two coordinate spaces

Every operation of a placed box carries:

- `boxes` — the border, padding and content rectangles in **paint coordinates**: one page
  space, where the accumulated origins of every ancestor have already been added;
- `origin` — the containing block's content origin, in paint coordinates.

Layout coordinates are `boxes - origin`, and `robo_paint_operation_local_boxes` is the door
that computes it. The relationship is not decoration: it must equal what
`robo_paint_boxes_of` reads straight out of layout, for every operation of every box, and
`paint.traversal.box_model_geometry` asserts exactly that. Carrying both spaces is what keeps
a consumer from re-walking the tree to rediscover what layout concluded — the "recalculate in
paint" this module exists to prevent — and the assertion is what keeps the two halves from
drifting.

The accumulation step is `origin_child = boxes.content` (the containing block's content
origin, in paint coordinates), because layout wrote each child's geometry relative to that
point. It is applied with `robo_paint_boxes_translate`, in 64 bits and saturated.

---

## 4. The clip in force

The clip is **two axes**, each a mode plus a half-open interval, and it is accumulated **per
axis** as the walk descends:

- a box whose own used overflow modes hide **neither** axis leaves the clip unchanged — the
  case every box in an ordinary document produces;
- a box whose modes hide an axis intersects that axis with the box's **padding box** (CSS
  clips a box's overflowing contents at its padding edge) and carries the other axis through
  untouched;
- an intersection that comes out empty is success with an empty interval, not a failure: a box
  its clipping ancestor already excluded draws nothing.

The boundary a box puts in force applies to its **children**, not to itself — a box's own
operations carry the clip it was entered under. `robo_paint_clip_rect` answers a single
rectangle only when both axes are bounded and **refuses** otherwise, because a `visible` axis
has no bound to report.

---

## 5. Validity, and where it is enforced

`robo_paint_operation_is_valid` requires a known kind and never `NONE`; a live node id; three
valid boxes whose border box contains the padding box contains the content box; a valid clip;
the colour rule (`none` for a **structural** record, a drawable colour for a **drawing** kind —
see §1.0); and, since 11.5, the **style rule** (a border side's `style` generates a border,
every other kind's is `none` — see §1.1a). The colour split is `robo_paint_op_kind_draws`, not
`is_marker`, so a `CONTENT` record is held to the structural side.

`robo_paint_list_append` enforces it. The traversal is one caller; a rule enforced in the
container is a rule no future caller can forget, and it is the "geometry safety" requirement
satisfied once rather than at every call site. A list a consumer reads is therefore a list of
records by construction.

---

## 6. Snapshot semantics

A record is a **value**: it carries no pointer into the layout tree, the style tree or the
DOM, so a list built from a document cannot move when that document is laid out again, and
cannot dangle when the tree is destroyed. The two coordinate spaces it carries (`boxes` and
`origin`, §3) are copied numbers, not references. This is the property
`paint.records.snapshot_isolation` pins: it lays a tree out, paints a list, lays the *same*
tree out at a different viewport, paints again, and shows the first list's records did not
move while the second list did.

The one thing a record deliberately does *not* snapshot is the texture of a text run: text is
counted, not painted (§6 below), so there is no run payload to carry. When text painting
arrives it will be a new kind with its own snapshot fields, which is the shape the record was
designed for.

## 7. What is deliberately not here

- No **text** or **image** operation (a run is counted, not painted).
- No **clip**, **save**, **restore** or **opacity** operation: the clip is *carried* on every
  operation, and a rasteriser applies it from the record rather than from a separate op. The
  vocabulary stays small until a consumer exists for a wider one.
- No **display-list** artifact: this is the operation sequence one will be built from.
- No **z-index** or stacking context: document order, with depths a later stacking pass
  reorders.
