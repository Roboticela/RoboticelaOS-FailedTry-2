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
    ROBO_PAINT_OP_PUSH_CLIP    = 8,   /* from here on the box's boundary is in force (11.8) */
    ROBO_PAINT_OP_TEXT         = 9,   /* draw one text run's glyphs (11.6) */
    ROBO_PAINT_OP_POP_CLIP     = 10,  /* the enclosing boundary is in force again (11.8) */
    ROBO_PAINT_OP_END_BOX      = 11,  /* leave a box: draws nothing */
    ROBO_PAINT_OP_PUSH_OPACITY = 12,  /* from here on the box's strength is in force (11.9) */
    ROBO_PAINT_OP_POP_OPACITY  = 13   /* the enclosing strength is in force again (11.9) */
} robo_paint_op_kind_t;
```

The order of the enumerators **is** the paint order: a box is entered, its decorations are
emitted in the order CSS paints them (background first, then the border), its own records
end at its **content boundary**, its children are emitted inside, and it is left. A consumer
that walked the list and ignored every `kind` would still see each box's operations
contiguous and in paint order.

The **drawing** vocabulary is deliberately small — one background, four border sides and, since
11.6, one text run — and has no `IMAGE`, `SAVE`, `RESTORE`, `TRANSFORM` or bare `OPACITY`
enumerator. A name with no code behind it is a promise, not a vocabulary; adding one later is one
enumerator, one name row and one case in the validity rule. Six kinds have been added since 11.1,
and each arrived with the consumer that needed it: `CONTENT` (11.2), the explicit boundary between
a box's own records and its descendants'; `TEXT` (11.6), the first drawing that is not a rectangle
filled with a colour; the `PUSH_CLIP`/`POP_CLIP` pair (11.8), which turn "the boundary in force"
from a field repeated on every record into a **scope** (`paint/docs/Clipping.md`); and the
`PUSH_OPACITY`/`POP_OPACITY` pair (11.9), the same shape one field along, whose placement differs
for a reason CSS states (`paint/docs/Opacity.md`).

**A scope pair is two kinds rather than one kind plus a flag**, and both releases made that choice.
The flag would be a field a consumer could get wrong where two kinds are two entries in a switch it
already has to write — and the two scopes sit at *different points* in a box's life, so a single
`SCOPE` kind would need a second field to say which of the two it was opening.

### 1.0 Structural records versus drawing records

The vocabulary splits in two, and the split is the rule a builder obeys:

| Half | Kinds | Emitted | Colour |
|---|---|---|---|
| **Structural** | `BEGIN_BOX`, `CONTENT`, `PUSH_CLIP`, `POP_CLIP`, `PUSH_OPACITY`, `POP_OPACITY`, `END_BOX` | for **every** placed box, plus one pair per box whose boundary or strength **changed** | `none` |
| **Drawing** | `BACKGROUND`, the four `BORDER_*`, `TEXT` | **only when it draws** | a rasteriser-usable colour |

The two predicates are `robo_paint_op_kind_is_structural` and `robo_paint_op_kind_draws`, and
they partition the vocabulary above `NONE`. Structural records are what give the sequence a
regular skeleton — one triple per box, plus the pair of a box that clips its contents or fades
them — while drawing records make the list's length a statement about the document. Structural rather
than "marker" is the right split because `CONTENT` is neither a drawing nor a box marker, and
`robo_paint_op_kind_is_marker` is the narrower predicate that picks out the two kinds that
*are* box markers (`Clipping.md` §4).

### 1.1 Four border kinds, not one kind plus a side

A `side` field would need a "not a border" value for the other kinds, and a field that means
nothing for most of its records is the "shape that reads as an answer" this tree refuses. With
a kind per side there is no such value, and `robo_paint_op_kind_side` is a **total function**
from the four border kinds to the side each draws on and a refusal for everything else — so no
operation can carry a side that disagrees with what it draws.

### 1.1a The fields whose meaning varies by kind (11.5, 11.6)

A record is nearly uniform: a node, a depth, three boxes, a clip. Three fields vary by kind, and
each is **required** to be the right thing rather than merely permitted:

| Field | A structural record | A background | A border side | A text run |
|---|---|---|---|---|
| `color` | `none` | the resolved `background-color` | the resolved `border-*-color` | the run's inherited `color` |
| `style` (11.5) | `none` | `none` | the side's `border-style`, which must **generate a border** | `none` |
| `text` (11.6) | a cleared run | a cleared run | a cleared run | the run: a non-empty span, a non-negative baseline offset and font inputs that are a record |
| `clip` (11.1, 11.8) | the clip in force | the clip in force | the clip in force | the clip in force |

The `clip` field is on **every** record and carries the boundary in force at that point in the
traversal — which is what lets a consumer that never looks at a `PUSH_CLIP`/`POP_CLIP` still
bound every drawing. The two clip kinds carry the **same** value (the boundary they establish),
so the pair is self-identifying: a builder matches a pop to the innermost open scope by comparing
the two records rather than by trusting position.

The `style` is carried because it is not derivable one stage up: the display list resolves a
side into the band it occupies, and the band's rectangle states *where* the border is but not
*what shape* fills it. `robo_paint_operation_style` is its accessor, and the rule is
`robo_css_border_style_generates_border` rather than "not `none`" — a `hidden` border draws
nothing, which is why RoboLayout gives such a side a used width of zero and no record is emitted
at all.

The `text` member is a **sub-record** rather than six more fields on the operation, for the
reason `boxes` is one: the facts a text drawing has and a fill does not arrive together, so a
record cannot carry half of them. A background's cleared run and a run's `style` of `none` are
the same statement from opposite sides. `robo_paint_operation_text` is its accessor, and the
rule is `robo_paint_text_run_is_valid` (Text.md §3).

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
                 origin, the clip in force, the strength in force, the depth  (structural)
PUSH_OPACITY     iff the box's own declaration CHANGED the strength        (structural, 11.9)
BACKGROUND       iff the declared background draws
BORDER_TOP       iff the top side's used width is above zero and its colour draws
BORDER_RIGHT     iff ...
BORDER_BOTTOM    iff ...
BORDER_LEFT      iff ...
CONTENT          the content box, the origin, the clip and the depth   (structural, always)
PUSH_CLIP        iff this box's own overflow CHANGED the boundary          (structural, 11.8)
<the box's children, in order -- a child that is a text run is one TEXT record rather than a
 structural triple, because a run is not a box>
POP_CLIP         carrying the boundary its push carried                     (structural, 11.8)
POP_OPACITY      carrying the strength its push carried                    (structural, 11.9)
END_BOX          the same rectangles, origin, clip and depth as BEGIN_BOX (structural)
```

Note where the two scope pairs sit relative to one another, because it is the phase's one
structural decision and it is CSS's: the **opacity** push stands *before* the box's first
decoration (a box's own background and border are painted at the box's own opacity) while the
**clip** push stands *after* the content boundary (a box's own background and border are painted
under its **parent's** boundary). `paint/docs/Opacity.md` §3 has the argument.

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
| A box whose strength **changed** emits exactly one `PUSH_OPACITY`/`POP_OPACITY` pair, and both halves carry the value the pair establishes. | `paint.opacity.scope_placement`, `.declared`, `.nested` |
| A `PUSH_OPACITY` precedes the box's first decoration; a `PUSH_CLIP` follows its content boundary. | `paint.opacity.scope_placement`, `paint.clip.scope_placement` |
| Every record of a box's own painting carries the box's strength; its markers carry the enclosing one. | `paint.opacity.scope_placement`, `.text` |
| A sibling never inherits another sibling's strength. | `paint.opacity.sibling_isolation` |
| A text run is emitted **after** its parent box's `CONTENT`, at one depth past that box, naming itself rather than the box. | `paint.traversal.text_painted`, `paint.text.order_and_depth` |
| A run wider than its line wraps **whole** rather than fragmenting, so two line fragments are two runs. | `paint.text.wrapped_lines` |
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

## 4.1 The strength in force (11.9)

One number, accumulated by **multiplication** rather than by intersection, and carried on every
record for the same reason the clip is: a consumer with only the list must not have to re-walk the
tree to discover it.

- the value a box puts in force is `multiply(in_force, declared)`, where `declared` is what its own
  computed style says (`1.0` when it says nothing);
- it applies to the box **itself** and to everything under it — its decorations, its content
  boundary and its subtree — which is why its push stands before its first decoration;
- a box whose product **equals** what was already in force emits no pair at all, so an `opacity: 1`
  box and a document that declares nothing are byte-identical to the 11.8 output;
- `robo_paint_opacity_value` answers `1.0` for a `NULL`, so "nothing is in force" and "a
  full-strength scope" read alike: both are states in which nothing inside changes.

A **text run does not read its own declaration.** The run's box is the element that owns it, and
that element already opened the scope around it — so the strength is *passed in* to the text
emitter, and a run that read its own style would apply the same declaration twice and square it.
That is `paint.opacity.text`'s claim and `paint.opacity.end_to_end`'s three-drawing count.

---

## 5. Validity, and where it is enforced

`robo_paint_operation_is_valid` requires a known kind and never `NONE`; a live node id; three
valid boxes whose border box contains the padding box contains the content box; a valid clip;
the colour rule (`none` for a **structural** record, a drawable colour for a **drawing** kind —
see §1.0); since 11.5, the **style rule** (a border side's `style` generates a border, every
other kind's is `none` — see §1.1a); and, since 11.6, the **text rule** (a `TEXT` record carries
a valid run, every other kind a cleared one — see §1.1a), which is the style rule's shape a
third time. The colour split is `robo_paint_op_kind_draws`, not `is_marker`, so a `CONTENT`
record is held to the structural side.

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

The one payload a record cannot hold by value is a text run's **characters**, because they are
variable-length. Since 11.6 they live in the **list's own pool** and the record carries a
**span** — two integers — so a record is still a value with no pointers in it and the snapshot
property still holds by construction. A span is meaningless apart from its pool, which is why
the doors that consume one take the pool too and **refuse** a span that runs past the end
(`RoboPaintText.h`). The display list owns a pool of its own and the builder copies each span
across, so clearing the records does not change a character the display list draws.

## 7. What is deliberately not here

- No **image** kind: images are the next drawing kind and the second non-numeric payload, and
  they belong to a later goal (`Clipping.md` §12). Nor a `text-decoration`/`text-shadow` field on
  the one text kind that does exist.
- No **`SAVE`** or **`RESTORE`** operation, and no **`TRANSFORM`**: the two states that change in a
  sequence are the clip and the strength, each restored by the pop that closes the scope its push
  opened rather than by a general state stack. The vocabulary stays small until a consumer exists
  for a wider one — and "a `PUSH_OPACITY` is not an `OPACITY`" is the distinction that keeps the
  pair two **structural** kinds rather than a drawing one.
- No **z-index** or stacking context: document order, with depths a later stacking pass
  reorders.
