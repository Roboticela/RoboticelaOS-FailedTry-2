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
    ROBO_PAINT_OP_END_BOX      = 7    /* leave a box: draws nothing */
} robo_paint_op_kind_t;
```

The order of the enumerators **is** the paint order: a box is entered, its decorations are
emitted in the order CSS paints them (background first, then the border), its children are
emitted inside, and it is left. A consumer that walked the list and ignored every `kind`
would still see each box's operations contiguous and in paint order.

The vocabulary is **seven wide** — six drawing-or-marker kinds plus `NONE` — and deliberately
has no `TEXT`, `IMAGE`, `CLIP`, `SAVE`, `RESTORE` or `OPACITY` enumerator. A name with no
code behind it is a promise, not a vocabulary; adding one later is one enumerator, one name
row and one case in the validity rule.

### 1.1 Four border kinds, not one kind plus a side

A `side` field would need a "not a border" value for the other kinds, and a field that means
nothing for most of its records is the "shape that reads as an answer" this tree refuses. With
a kind per side there is no such value, and `robo_paint_op_kind_side` is a **total function**
from the four border kinds to the side each draws on and a refusal for everything else — so no
operation can carry a side that disagrees with what it draws.

### 1.2 Markers

`BEGIN_BOX` and `END_BOX` draw nothing and carry no colour: their `color` is
`robo_css_color_none()`, and `robo_paint_operation_is_valid` requires exactly that, so a
marker carrying a colour is a refused record rather than one a consumer might draw. Their
purpose is the boundary: a rasteriser that will one day push a clip, save a state or blend a
group needs a marker to hang that on, and the depth the marker carries is what makes the
tree's shape recoverable from the flat list.

---

## 2. The traversal contract

`robo_paint_context_run` walks the tree in **document order** — parents before children,
children left to right, following the layout tree's own `first_child`/`next_sibling` order —
and for each box emits:

```text
BEGIN_BOX        the box's three rectangles in paint coordinates, the accumulated
                 origin, the clip in force, the box's depth
BACKGROUND       iff the declared background draws
BORDER_TOP       iff the top side's used width is above zero and its colour draws
BORDER_RIGHT     iff ...
BORDER_BOTTOM    iff ...
BORDER_LEFT      iff ...
<the box's children, in order, under the boundary this box's own overflow put in force>
END_BOX          the same rectangles, origin, clip and depth as BEGIN_BOX
```

The invariants a consumer may rely on, and where the suite checks them:

| Invariant | Case |
|---|---|
| Each box's operations are contiguous, beginning with `BEGIN_BOX` and ending with `END_BOX`. | `paint.traversal.single_box`, `.empty_element` |
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
and the colour rule (`none` for a marker, a drawable colour for a drawing kind).

`robo_paint_list_append` enforces it. The traversal is one caller; a rule enforced in the
container is a rule no future caller can forget, and it is the "geometry safety" requirement
satisfied once rather than at every call site. A list a consumer reads is therefore a list of
records by construction.

---

## 6. What is deliberately not here

- No **text** or **image** operation (a run is counted, not painted).
- No **clip**, **save**, **restore** or **opacity** operation: the clip is *carried* on every
  operation, and a rasteriser applies it from the record rather than from a separate op. The
  vocabulary stays small until a consumer exists for a wider one.
- No **display-list** artifact: this is the operation sequence one will be built from.
- No **z-index** or stacking context: document order, with depths a later stacking pass
  reorders.
