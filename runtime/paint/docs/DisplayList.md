# RoboPaint display list (Phase 11.3)

This document states the display list: what a **display command** is, how a paint record
resolves to one, why the conversion is not duplication, and what a consumer may rely on.
The operation model it is built from is in `Operations.md`; the architecture it sits inside
is in `Architecture.md`.

---

## 1. Purpose

A `robo_paint_operation_t` is a **record of a decision**: it carries the box's three
rectangles and a kind out of a seven-wide vocabulary, and it leaves one derivation to the
consumer -- the band a border side occupies is `robo_paint_operation_side_band`, computed on
demand because the operation already carries the two rectangles the band is the difference
of. That is the right shape for a record.

A `robo_paint_display_list_t` is the **artifact a rasteriser consumes**: an ordered sequence
of `robo_paint_display_command_t`, each carrying the **one rectangle it acts on, already
resolved**, so a rasteriser never consults the box model, never reads a CSS box side and
never has to know that "background" means "the border box".

```text
HTML / DOM → RoboCSS → RoboLayout → RoboPaint
                                       │
                        robo_paint_context_run
                                       ▼
                              robo_paint_list_t        (records: boxes + kind)
                                       │
                     robo_paint_display_list_builder_build
                                       ▼
                            robo_paint_display_list_t    (commands: one rectangle)
```

The list is deliberately **flat**, for the operation list's reason: a rasteriser walks a
sequence once and keeps a clip stack, and the tree's shape is recoverable from the `depth`
each command carries. It is deliberately **not a scene graph** and **not reordered** --
batching, merging and stacking-context reordering are later goals', and doing any of them
here would be an optimization with no consumer.

---

## 2. The command vocabulary

```c
typedef enum robo_paint_display_command_kind {
    ROBO_PAINT_DISPLAY_CMD_NONE            = 0,  /* a cleared record; never appended */
    ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE      = 1,  /* enter a node: draws nothing */
    ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND = 2,  /* fill a rectangle with a colour */
    ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER     = 3,  /* fill one border side's band */
    ROBO_PAINT_DISPLAY_CMD_CONTENT         = 4,  /* the node's own drawing ends */
    ROBO_PAINT_DISPLAY_CMD_END_NODE        = 5   /* leave a node: draws nothing */
} robo_paint_display_command_kind_t;
```

**Five kinds, one narrower than the paint record's seven**, and the difference is the point:
the **four** border kinds collapse into **one**, because the fact that distinguished them --
which side -- is consumed into the band rectangle. A command that also carried a side would
be carrying a fact its own geometry already states.

The vocabulary splits the operation vocabulary's way: **structural** commands
(`BEGIN_NODE`, `CONTENT`, `END_NODE`) are emitted for **every** node and carry no colour;
**drawing** commands (`DRAW_BACKGROUND`, `DRAW_BORDER`) are emitted **only when they draw**
and carry a colour a rasteriser can use. `robo_paint_display_command_kind_draws` and
`robo_paint_display_command_kind_is_structural` are the partition, and the validity rule
splits on *drawing* rather than on *marker* so `CONTENT` is held to the structural side.

---

## 3. The record-to-command mapping

Every paint operation resolves to **exactly one** display command, by a total function over
the record vocabulary:

| Paint operation | Display command | `rect` |
|---|---|---|
| `BEGIN_BOX` | `BEGIN_NODE` | the **border box** |
| `BACKGROUND` | `DRAW_BACKGROUND` | the **border box** |
| `BORDER_TOP` / `BORDER_RIGHT` / `BORDER_BOTTOM` / `BORDER_LEFT` | `DRAW_BORDER` | the side's **band** |
| `CONTENT` | `CONTENT` | the **content box** |
| `END_BOX` | `END_NODE` | the **border box** |

`node`, `depth`, `clip` and `color` carry through **unchanged**. Because the mapping is
one-to-one, a display list has the same length as the record list it was built from, and the
two can be paired positionally -- which is how `paint.display.*` checks it.

**Why this is not duplication.** The paint list says *which rectangles a decision was made
from*; the display list says *which rectangle to fill*. The padding box, the box side and
the background's "the border box" convention are all *inputs* that the build consumes, and
once they are consumed nothing above needs them. What survives is exactly the drawable
geometry, with the coordinate space unchanged: a command's `rect` is in **paint
coordinates**, one page space, exactly as the operation's boxes were.

---

## 4. Ordering and the structural invariant

Commands preserve the record order exactly, which is the traversal's document order: per
node, `BEGIN_NODE`, its drawing commands, `CONTENT`, its subtree, `END_NODE`. No command is
reordered and no command is merged.

More than that, a display list is **regular**, and this is a *guarantee* rather than a hope.
A paint list is a container that validates each operation but not their *sequence*; the
builder is the door that turns the traversal's convention into an invariant. It walks the
record list with a stack of the currently open boxes and **refuses** a list where:

- a `BEGIN_BOX` does not nest directly inside the currently open boxes (its `depth` is not
  the open count);
- a `CONTENT`, `END_BOX` or drawing operation does not belong to the innermost open box
  (its `depth` or its `node` id is not that box's);
- a box passes its content boundary twice, closes before it, or emits a decoration *after*
  it;
- the sequence does not close every box it opens.

On a refusal `out_list` is left **empty** and the diagnostic names the reason, so a caller
never holds a partially converted sequence -- the paint pass's rule at the same boundary.

A consumer may therefore group a box's commands as `[BEGIN_NODE .. CONTENT]` for its own
drawing and `(CONTENT .. END_NODE)` for its subtree, by `depth` and the boundaries alone,
without consulting the tree.

---

## 5. Ownership and lifetime

| Object | Owns | Lifetime rule |
|---|---|---|
| `robo_paint_display_list_t` | one growable array of value commands | safe to discard and regenerate, in any order relative to the records and the tree |
| `robo_paint_display_list_builder_t` | its reusable open-box stack | safe to destroy in any order; `reset` keeps the stack's capacity |
| `robo_paint_display_command_t` | nothing | a value: copy it, embed it, put it on the stack |

A command carries **no pointers**, so a display list cannot contain a dangling pointer and
cannot mutate layout geometry. The list borrows nothing: it has no reference to the record
list it was built from or to the tree, so 11.2's snapshot property becomes the display
list's too -- `paint.display.mutation_isolation` clears the record list and re-lays-out the
tree, and the display list does not move.

The builder is a **stateful object** because the structural check needs the open-box stack,
and it is reused across builds (`reset` keeps the capacity) exactly as a paint context is
reused across passes. It owns nothing borrowed.

---

## 6. Geometry safety, and validity

`robo_paint_display_command_is_valid` requires a known kind and never `NONE`; a live node
id; a rectangle with a **non-negative** extent (empty is fine -- a zero-height box is a
statement about the document); a valid clip; and the colour rule. `robo_paint_display_list_append`
enforces it, so every list a consumer reads is a list of commands -- the "geometry safety"
requirement satisfied once in the container rather than at every call site.

The builder inherits its inputs' guarantees: every operation in a paint list is already a
record, and the resolved rectangle is a difference of two rectangles the operation carried,
computed with the module's saturating helpers. Nothing here can produce NaN, infinity or a
negative extent.

---

## 7. The dump

`robo_paint_display_list_write` renders the list as one line per command, indented by the
command's own `depth`, in list order -- a text door into a buffer and nothing more, the paint
dump's shape one stage up:

```text
#0000 BEGIN_NODE        node=1 depth=0 rect=0,0,100,40 clip=visible/visible
#0001   DRAW_BACKGROUND node=1 depth=0 rect=0,0,100,40 clip=visible/visible color=#ff0000ff
#0002   DRAW_BORDER     node=1 depth=0 rect=0,0,100,1  clip=visible/visible color=#000000ff
#0003   CONTENT         node=1 depth=0 rect=1,1,98,38  clip=visible/visible
#0004 END_NODE          node=1 depth=0 rect=0,0,100,40 clip=visible/visible
```

`ROBO_PAINT_DISPLAY_DUMP_LINE_MAX` is a **derived** bound, and `paint.display.dump` measures
the widest line this build can actually produce rather than trusting the derivation.

---

## 8. What is deliberately not here

- **No rasterisation of the whole list, no clipping architecture, no presentation.** The display
  list is data and `raster` is 0: since 11.4 exactly one command kind is consumed -- a
  `draw-background` is filled into a caller's surface by `robo_paint_background_paint`
  (`paint/docs/Background.md`), and a `draw-border` band, the structural commands and everything
  else are still data. A background fill *is* bounded by the clip the command carries, but no
  clipping op is emitted and no clipping system exists.
- **No batching, merging or command reordering.** The order is the traversal's; an optimizer
  is a later goal's and would need a consumer to justify it.
- **No transforms, opacity, save/restore or clip *operations*.** A command carries the clip
  it draws under; a rasteriser applies it from the command. A wider vocabulary waits for a
  consumer.
- **No text or image commands.** A text run is counted, not painted (11.1); when text
  painting arrives it is a new kind with its own snapshot fields, which is the shape the
  record was designed for.
- **No scroll, damage, tile or compositor concepts**, and no paint invalidation (11.11) or
  hit testing (11.10).

---

## 9. What 11.3 may hand a rasteriser

1. `robo_paint_display_list_builder_build` converts a paint list into a display list
   deterministically; the two lists pair positionally, one command per record.
2. A command carries the **one rectangle it acts on**, resolved, in paint coordinates.
3. A display list is **regular**: the structural skeleton is an invariant the builder
   enforced, not a convention.
4. A command is a value with no pointers; a list owns one array of them and is safe to
   discard and regenerate.
5. The clip is carried per command, two axes, exactly as the operation carried it.
6. An out-of-order record list is refused with an empty output list and a diagnostic.
