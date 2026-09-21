# RoboPaint display list (Phase 11.3, extended by 11.6)

This document states the display list: what a **display command** is, how a paint record
resolves to one, why the conversion is not duplication, and what a consumer may rely on.
The operation model it is built from is in `Operations.md`; the architecture it sits inside
is in `Architecture.md`.

---

## 1. Purpose

A `robo_paint_operation_t` is a **record of a decision**: it carries the box's three
rectangles and a kind out of a twelve-wide vocabulary, and it leaves one derivation to the
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
    ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT       = 5,  /* draw one text run's glyphs (11.6) */
    ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP       = 6,  /* boundary from here on (11.8) */
    ROBO_PAINT_DISPLAY_CMD_POP_CLIP        = 7,  /* the enclosing boundary again (11.8) */
    ROBO_PAINT_DISPLAY_CMD_END_NODE        = 8,  /* leave a node: draws nothing */
    ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY    = 9,  /* strength from here on (11.9) */
    ROBO_PAINT_DISPLAY_CMD_POP_OPACITY     = 10  /* the enclosing strength again (11.9) */
} robo_paint_display_command_kind_t;
```

**Eleven kinds including `NONE` -- ten above it, three fewer than the record vocabulary's
thirteen** -- and the three are exactly the **four border kinds collapsing into one**, because the
fact that distinguished them (which side) is consumed into the band rectangle. A command that also
carried a side would be carrying a fact its own geometry already states. Nothing else collapses:
11.6's `TEXT` record keeps a kind of its own, because a run is not a box and its command is not a
fill; and each scope pair keeps **two** kinds, for the reason `paint/docs/Operations.md` §1 gives --
a flag field is what a consumer gets wrong where two kinds are two entries in a switch it writes
anyway.

Two facts survive those collapses and had to be carried deliberately. 11.5's is the
**`border-style`**: the side is consumed into the rectangle, which states *where* a band is, but
the style is the shape *inside* it and is not derivable from the rectangle -- so `draw-border`
has a `style` field, it is required to be one that generates a border, and it is the reason this
artifact is render-ready rather than merely resolved. 11.8's and 11.9's is the **state each
scope establishes**: both halves of a clip pair carry the same `clip` and both halves of an
opacity pair the same `opacity` (`Opacity.md` §3), so a scope is self-identifying and a consumer
that restores its own stack never reads a second value. 11.6's is the **run** (`draw-text`), which
is the one field on this model that names another container: a span into **this list's own**
code-point pool, a baseline offset and the font inputs (`paint/docs/Text.md`). The pool is the
list's own rather than the record list's -- the builder copies each span across -- which is what
keeps 11.2's snapshot property true of the artifact as well as of the sequence.

The vocabulary splits the operation vocabulary's way: **structural** commands
(`BEGIN_NODE`, `CONTENT`, `PUSH_CLIP`, `POP_CLIP`, `PUSH_OPACITY`, `POP_OPACITY`, `END_NODE`) are
emitted for **every** node — plus one pair per node whose boundary or strength changed — and carry
no colour; **drawing** commands (`DRAW_BACKGROUND`, `DRAW_BORDER`, `DRAW_TEXT`) are emitted **only
when they draw** and carry a colour a rasteriser can use. `robo_paint_display_command_kind_draws` and
`robo_paint_display_command_kind_is_structural` are the partition, and the validity rule
splits on *drawing* rather than on *marker* so `CONTENT` is held to the structural side — the same
reason the two clip kinds land there: they draw nothing (`Clipping.md` §4).

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
| `PUSH_CLIP` / `POP_CLIP` (11.8) | `PUSH_CLIP` / `POP_CLIP` | the box's **padding box** |
| `TEXT` (11.6) | `DRAW_TEXT` | the run's **advance box** |
| `PUSH_OPACITY` / `POP_OPACITY` (11.9) | `PUSH_OPACITY` / `POP_OPACITY` | the **border box** |
| `END_BOX` | `END_NODE` | the **border box** |

A clip command's rectangle is the box's **padding box** — the boundary the box's own `overflow`
resolved to — and deliberately *not* the effective clip's rectangle, which does not always exist:
`overflow-x: hidden` beside `overflow-y: visible` is a legal document, so `robo_paint_clip_rect`
refuses exactly the case where a reader would most like a rectangle, and a command that answered
anyway would be inventing one. The boundary itself travels in the `clip` field, where the axis
split survives — and both commands of a scope carry the **same** clip, so a consumer restores the
enclosing boundary by popping its own stack rather than by reading a second value.

An **opacity** command's rectangle is the **border box**, which is where an opacity acts: the
strength applies to the box as a whole — its own decoration and its contents — so the box's own
outer rectangle is the region the pair is about, and the value it establishes travels in the
`opacity` field rather than in the geometry.

`node`, `depth`, `clip`, `opacity`, `color`, -- for a band -- `style` and, for a run, everything in
`text` except the span itself carry through **unchanged**. The span is the one field that is not
copied verbatim: it is re-numbered into the output's own pool.
Because the mapping is one-to-one, a display list has the same length as the record list it was
built from, and the two can be paired positionally -- which is how `paint.display.*` checks it.

**Why this is not duplication.** The paint list says *which rectangles a decision was made
from*; the display list says *which rectangle to fill*. The padding box, the box side and
the background's "the border box" convention are all *inputs* that the build consumes, and
once they are consumed nothing above needs them. What survives is exactly the drawable
geometry, with the coordinate space unchanged: a command's `rect` is in **paint
coordinates**, one page space, exactly as the operation's boxes were.

---

## 4. Ordering and the structural invariant

Commands preserve the record order exactly, which is the traversal's document order: per
node, `BEGIN_NODE`, its drawing commands, `CONTENT`, its subtree, `END_NODE`. A node that clips
its contents adds a pair around its subtree — `CONTENT`, `PUSH_CLIP`, the subtree, `POP_CLIP`,
`END_NODE` — so a box's own decoration stays *outside* the scope it establishes, which is CSS's
arrangement (`Clipping.md` §4). A node that **fades** its contents adds its pair in the other
place: `BEGIN_NODE`, `PUSH_OPACITY`, the decorations, `CONTENT`, the subtree, `POP_OPACITY`,
`END_NODE` — *inside*, so the box's own decoration is drawn at the box's own strength, which is
CSS's arrangement too (`Opacity.md` §3). The two pairs therefore nest differently around one node,
and both are correct for the same reason: each opens where the state it establishes begins. No
command is reordered and no command is merged.

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
- the sequence does not close every box it opens;
- a `PUSH_CLIP`/`POP_CLIP` pair does not hold together — a push before its box's `CONTENT`, two
  pushes for one box, a push that establishes nothing new, a scope **wider** than the boundary it
  is inside, a drawing inside a scope whose own clip disagrees with it, a pop with no push, a pop
  naming another box, a pop carrying another boundary, or a scope never closed (**eleven** separate
  messages, each its own refusal — `Clipping.md` §5);
- a `PUSH_OPACITY`/`POP_OPACITY` pair does not hold together — the same list one field along, plus
  the one refusal that distinguishes the two scopes: a push that comes **after the box has begun
  drawing** (after a decoration or after its content boundary), because an opacity belongs to the
  box itself where a clip belongs to its contents (**eight** separate messages, each its own
  refusal — `Opacity.md` §4).

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
statement about the document); a valid clip; the **colour rule** (`none` for a structural
command, a colour a rasteriser can use for a drawing one); and, since 11.5, the **style rule** --
a `draw-border` carries a style that generates a border, and every other kind carries `none`.
`robo_paint_display_list_append` enforces it, so every list a consumer reads is a list of
commands -- the "geometry safety" requirement satisfied once in the container rather than at
every call site. Since 11.9 the rule has a third clause beside the colour and style rules: the
**strength rule** -- every command must carry a **valid** opacity record, and both halves of a pair
the value the pair establishes. A command with a `NaN` strength is not a command this build may
hand to a rasteriser.

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
#0000 begin-node       node=1 depth=0 clip=visible/visible opacity=1.00 rect=0,0,100,40
#0001 draw-background  node=1 depth=0 clip=visible/visible opacity=0.50 rect=0,0,100,40 color=#ff0000ff
#0002 draw-border      node=1 depth=0 clip=visible/visible opacity=0.50 rect=0,0,100,1 color=#000000ff style=dashed
#0003 content          node=1 depth=0 clip=visible/visible opacity=0.50 rect=1,1,98,38
#0004   draw-text        node=2 depth=1 clip=visible/visible opacity=0.25 rect=1,1,16,12 color=#ff0000ff text="Hi"
#0005 end-node         node=1 depth=0 clip=visible/visible opacity=0.50 rect=0,0,100,40
```

The `opacity=` field appears on every command exactly as `clip=` does, and the two scopes' lines
carry the same value as the push and the pop that bound them -- the self-identifying rule, visible
in the format (`Opacity.md` §8).

The names are the vocabulary's own, lower case, exactly as `robo_paint_display_command_kind_name`
spells them, padded to `ROBO_PAINT_DISPLAY_NAME_COLUMN` (16) so the fields after them line up. The
ordinal is written by the dump rather than by the command's writer, which is why it is five
characters wide (`#` plus a zero-padded four-digit number) and followed by the depth's own indent.

The `style=` field appears on a `draw-border` and on nothing else, exactly as the field itself
does: a band that could not say which of the ten styles it draws would be the one thing a reader
of the dump could not tell apart from a `solid` one. The `text=` field appears on a `draw-text`
and on nothing else, for the same reason one stage along: a span is two integers, and a reader of
a dump is a reader of *characters*, so the door that has the pool appends the quoted run --
bounded and escaped, because a line is a fixed-size buffer and a run has no bound at all.

`ROBO_PAINT_DISPLAY_DUMP_LINE_MAX` is a **derived** bound, and `paint.display.dump` measures
the widest line this build can actually produce rather than trusting the derivation.

---

## 8. What is deliberately not here

- **No rasterisation of the whole list, and no presentation.** The display list is data and
  `raster` is 0: since 11.4–11.8 exactly the three **drawing** kinds are consumed — a
  `draw-background` and a `draw-border` are filled into a caller's surface and a `draw-text` has
  its glyphs stroked into it, all by `robo_paint_decoration_paint` (`paint/docs/Decoration.md`,
  `paint/docs/Border.md`, `paint/docs/Text.md`), in the list's order — while the seven structural
  commands, **both scope pairs included**, are still data. A drawing is bounded by the clip in
  force (the scope's, or its own outside any scope) and drawn at the strength in force (the
  scope's, or its own), and every drawing kind does both; what is absent is rounding, `clip-path`,
  masks, and clipping that a compositor or a scroll container owns.
- **No batching, merging or command reordering.** The order is the traversal's; an optimizer
  is a later goal's and would need a consumer to justify it.
- **No transforms or save/restore.** A command carries the clip and the strength it draws under,
  and a `push-clip`/`pop-clip` pair states the **boundary scope** it draws inside while a
  `push-opacity`/`pop-opacity` pair states the **strength scope** (11.9); a rasteriser applies both
  from the stream. A wider vocabulary waits for a consumer — which is why each pair is a pair
  rather than a general state stack: the two states that change are the clip and the strength, and
  each has its own two kinds.
- **No image commands**, and nothing behind the one text kind that does exist: no shaping, no
  bidi, no line breaking, no fallback. What a `draw-text` states is the runs RoboLayout measured,
  in logical order, resolvable against a font the *caller* supplies.
- **No scroll, damage, tile or compositor concepts.** Paint invalidation (11.11) and hit testing
  (11.10) have since been built, and both read this artifact rather than changing it: a diff over two
  display lists compares command pairs position by position and reports a region per position whose
  drawing differs, and the two lists are still rebuilt whole (`raster` is 0), so there is nothing here
  that patches a list or tracks damage between frames.

---

## 9. What 11.3 may hand a rasteriser

1. `robo_paint_display_list_builder_build` converts a paint list into a display list
   deterministically; the two lists pair positionally, one command per record.
2. A command carries the **one rectangle it acts on**, resolved, in paint coordinates.
3. A display list is **regular**: the structural skeleton is an invariant the builder
   enforced, not a convention.
4. A command is a value with no pointers; a list owns one array of them and is safe to
   discard and regenerate.
5. The clip is carried per command, two axes, exactly as the operation carried it — and a
   scope that *changes* it is stated in the stream (`push-clip` … `pop-clip`), so a consumer
   replaying the list knows the boundary without re-deriving an intersection (11.8).
6. The strength is carried per command too, and a scope that *changes* it is stated in the same
   stream (`push-opacity` … `pop-opacity`), placed where CSS places it — around the node's own
   decoration as well as its contents, so a consumer that ignores the pair still draws every
   fading command at the strength on its own record (11.9).
7. An out-of-order record list is refused with an empty output list and a diagnostic.
