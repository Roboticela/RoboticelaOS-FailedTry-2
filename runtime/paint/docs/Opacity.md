# RoboPaint — opacity (Phase 11.9)

The phase that gave a drawing a **strength**, and gave a subtree a way to state that all of it is
drawn weaker.

Every release since 11.1 has asked *"what bounds this drawing?"* and answered with a rectangle per
axis. None of them asked *"how strongly is it drawn?"*: a `draw-background` command carried a
colour and a clip, and a `rgba(255, 0, 0, 0.5)` fill was the only way to make anything translucent —
which is a fact about a **colour**, not about an element. CSS keeps those two apart on purpose. An
`opacity: 0.5` element is a half-transparent *box*: its background, its border, its text and every
descendant it contains all composite at half strength, and the colour they were declared with is
untouched.

This phase adds the second factor. It is deliberately **not** a compositor: the strength is
multiplied per drawing as each one is drawn, rather than applied to a flattened layer, and §9
states exactly where that differs from CSS and why the difference is the honest shape for this
build.

The record's own argument — why the initial value is a door rather than a literal, and why a
non-number answers *opaque* rather than *transparent* — is at `Include/RoboCSS/RoboCSSOpacity.h`
and `Include/RoboPaint/RoboPaintOpacity.h`. The traversal is `paint/docs/Architecture.md`; the pass
is `paint/docs/Decoration.md`; the command model is `paint/docs/DisplayList.md`; the record model is
`paint/docs/Operations.md`; the case list is `paint/docs/Tests.md` §1.12 and its findings §13.

```text
HTML / DOM
  → RoboCSS            `opacity` computed per box (a real in [0, 1]; not inherited; clamped)
  → RoboLayout         nothing — opacity is not a layout input
  → RoboPaint          the traversal multiplies it down the tree, per box
  → records            every record carries the strength in force; a *changed* one carries the pair
  → display list       PushOpacity(0.5) …decorations, content, subtree… PopOpacity(0.5)
  → the pass           one stack, one application point, `robo_gfx_raster_set_opacity`
  → RoboGraphics       the raster's global opacity, multiplied into every blend it performs
  → the caller's surface
```

---

## 1. The record, and the one direction that matters

```c
typedef struct robo_paint_opacity {
    double value;   /* [0, 1]; 1.0 is fully opaque */
} robo_paint_opacity_t;
```

One field, and the whole design of the record is in what a **zeroed** one means — because a record
that zeroes to *transparent* makes every element of every document that never mentions `opacity`
invisible, and the failure would be silent: correct geometry, correct layout, a blank page.

So a cleared record is **fully opaque**, `robo_paint_opacity_opaque()` is the door that states it,
and every accessor answers it for a record it cannot read:

| Door | Answers |
|---|---|
| `robo_paint_opacity_opaque` | the fully opaque record, by value — what an empty scope stack answers |
| `robo_paint_opacity_from_value` | `[0, 1]` from a real; **NaN/∞ → opaque**, not zero |
| `robo_paint_opacity_value` | the real; `1.0` for NULL |
| `robo_paint_opacity_is_valid` | finite and inside `[0, 1]` |
| `robo_paint_opacity_is_opaque` | exactly `1.0` — the state in which the declaration is a no-op |
| `robo_paint_opacity_equal` | field-by-field; two NULLs are equal |
| `robo_paint_opacity_multiply` | `outer × inner`, clamped — **the accumulation, in one place** |
| `robo_paint_opacity_to_byte` | the 8-bit strength the raster takes |

Two decisions are worth stating separately.

**The invalid direction is *visible*.** `robo_paint_opacity_from_value` answers opaque for a NaN or
an infinity rather than for zero. A NaN arriving here is a bug in someone's arithmetic upstream, and
a record nothing can read should render the way an element nothing declared does — which is
*visible*, and therefore a bug you can see rather than a page that quietly lost its contents. The
same choice the CSS layer makes one level up, for the same reason
(`Include/RoboCSS/RoboCSSOpacity.h`).

**A record is not a colour's alpha.** There is no door here that touches a colour, and the
combination is the product rather than a rewrite — it belongs to the raster, where
`robo_gfx_raster_set_opacity` is a *global* multiplier the graphics module keeps beside its blend,
so the pass that sets it once per drawing does not know or care how many colours that drawing
composites.

`robo_paint_opacity_to_byte` answers **0** — no ink — for a record that is not valid, which is the
*opposite* direction from `from_value`'s and is worth reading twice, because it is the one place in
this phase where two doors in the same header disagree about what to do with a value nothing can
read. It is not an inconsistency: they are on opposite sides of the same decision. `from_value` is
**constructing** a record out of input it did not choose, and the only useful answer there is the
one a document that declared nothing gets — visible. `to_byte` is **consuming** a record, at the
last gate before a pixel, and a value nothing can read must not put ink on a surface; a caller's
arithmetic bug that reaches this door should produce a blank rectangle, which is visibly wrong,
rather than a fully drawn one, which is not. In practice the branch is unreachable from this build's
own path — `from_value` cannot produce an invalid record, and `multiply` clamps — so it is the last
gate's own defence rather than an input a document can supply.

## 2. The stack

`robo_paint_opacity_stack_t` (opaque, `Include/RoboPaint/RoboPaintOpacity.h`) is a LIFO of whole
records, and it is the exact counterpart of 11.8's clip stack — same ownership, same five doors,
same growth policy:

| Door | Answers |
|---|---|
| `_create` / `_destroy` | ownership; the caller's, like every other list in this module |
| `_reset` | empty, **keeping capacity** — what a pass calls before reusing itself |
| `_push` | from here on, this is the strength in force (the record is *copied*) |
| `_pop` | the innermost scope is closed |
| `_current` | the strength in force; the cleared (opaque) record when empty |
| `_depth` / `_capacity` / `_is_empty` | 0 / grown-or-not / nothing in force |

The two stacks are **separate types rather than one**, and this is the second departure from 11.8
after §3's placement rule. A clip and a strength are different questions with different arithmetic —
one intersects intervals, the other multiplies reals — and a single "state" record carrying both
would have to answer which half an operation is about at every door. Two stacks, two vocabularies,
one traversal.

## 3. Where a scope is emitted, and why it is *not* where the clip's is

This is the phase's one structural decision, and it is not a style choice.

```text
11.8  pop-clip            after  a box's `content` boundary   → a clip belongs to a box's CONTENTS
11.9  push-opacity        after  a box's `begin-node`         → an opacity belongs to the box ITSELF
```

CSS paints an element's own `background-color` and borders **at the element's own opacity** — a
`0.5` red box is a half-transparent red box — so the scope must already be open when the box's first
decoration is emitted. The clip cannot be, because the same CSS paints that box's own background
under its **parent's** boundary: a child that spills over its parent draws over the parent's ring,
and the parent's `overflow: hidden` does not clip the parent's own background.

So the two pairs nest differently around the same box, and one rule holds them both together —
**every record carries the state in force at its own position in the sequence**:

```text
begin-node       ← carries the ENCLOSING strength
push-opacity 0.50   ← establishes the box's own
  background / border_*        ┐
  content                      │ all carry 0.50
  <subtree>                    ┘
pop-opacity 0.50
end-box          ← carries the ENCLOSING strength again
```

`BEGIN_BOX` stands *before* the push, so it carries the enclosing strength; the decorations, the
content boundary, the whole subtree and the pop all stand *inside* it and carry the box's; and
`END_BOX` stands after the pop, so it carries the enclosing strength again. That is the same rule
that makes `POP_CLIP` and `END_BOX` disagree about the clip — and it is what makes a pair
**self-identifying**: both halves of a scope carry the value the pair establishes, so a consumer
that only has the pair can check it against itself.

**A scope opens only when the box's declaration actually changes the strength.** The traversal
computes `child_opacity = multiply(in_force, declared)` and emits the pair only when that product
differs from what was already in force (11.8's rule one field along, and the same reason: an
`opacity: 1` box, or a document where nobody declares anything, is a document with **no scope at
all**). `stats.opacity_declared − stats.opacity_scopes` is therefore the count of declared strengths
that established nothing.

## 4. What the builder checks

Eight refusals, each with its own message, all producing an **empty** output list rather than a
half-built one:

| Refusal | What it catches |
|---|---|
| an opacity scope belongs to no open box | a scope outside every box |
| an opacity scope names a box other than the innermost open one | a scope attributed to the wrong box |
| a box opens two opacity scopes | a doubled push (a box declares once) |
| an opacity scope opens after the box it belongs to has begun drawing | a push after the box's first decoration **or** after its content boundary |
| an opacity scope establishes nothing new | a push equal to what is already in force |
| an opacity scope is stronger than the strength it is inside | a push **above** its enclosing strength — impossible from a product, and the reason the in-scope rule can be relied on afterwards |
| a box closes an opacity scope it did not open | a pop with nothing open |
| an opacity scope closes with a strength other than the one it opened | a pop that pairs with something, but not its own push |

The second row exists because a box's own depth is checkable: both halves name the box and carry its
depth, exactly as the box's `CONTENT` does, so a pair can be checked against the box it claims
rather than trusted. The fourth is the rule that a build placing the scope at the content boundary —
11.8's point — would break, and the case that pins it is
`paint.opacity.builder_refusals`'s *"a push after the box's first decoration"*: the same list as a
legal one with the marker's own painting moved ahead of the push.

The pair is **provably paired** rather than *probably* paired for the reason 11.8's is: a pop with
nothing open, a pop that closes a scope another box opened, and a pop whose strength is not the one
its push carried are three separate refusals rather than one that happens to catch all three.

## 5. The pass

One application point, `RoboPaintDecoration.c`:

```c
if (robo_paint_display_command_kind_is_opacity(command.kind)) {
    row = push ? robo_paint_opacity_stack_push(...)   /* and stats.opacity_scopes++ */
               :          robo_paint_opacity_stack_pop(...);
    ...
}
...
/* every drawing kind, at the one place a drawing reaches the raster */
effective = robo_paint_opacity_stack_is_empty(opacities)
                ? command.opacity            /* no scope: the record's own strength */
                : robo_paint_opacity_stack_current(opacities);
robo_gfx_raster_set_opacity(raster, robo_paint_opacity_to_byte(&effective));
```

The value applied is the **stack's** when a scope is open and the **record's own** when none is,
which is the same statement §1's default makes one level down: a document that declares no opacity
never has the raster's opacity touched for a non-255 value, so it is byte-identical to 11.8's
output. `stats.faded` counts exactly the drawings that performed that store with a byte other than
`255` — a `0.5` box whose fill is already fully transparent is *not* faded, because no pixel was
drawn weaker; the counter is a statement about the raster, not about the declaration.

Because the strength is on the **record** and the scope is a **command**, a consumer that ignores
the two new command kinds still draws every fading box at the strength on its own command. The
scope is how a consumer that walks with state gets the nesting right; it is not the only place the
strength lives.

## 6. Counters

| Where | Field | Means |
|---|---|---|
| traversal | `opacity_declared` | placed boxes whose **declared** opacity is not fully opaque |
| traversal | `opacity_scopes` | scopes actually opened (`declared − scopes` = declarations that established nothing) |
| display list | `opacity_scopes` | `push-opacity` commands; equal to the traversal's by construction |
| pass | `opacity_scopes` | scopes entered |
| pass | `opacity_depth_max` | high-water mark of the pass's own stack |
| pass | `faded` | drawings that reached the raster at a strength other than `255` |

`opacity_depth_max` is not `max(clip_depth_max)` and the two are not redundant: an opacity pair
opens at a box's marker and a clip pair at its content boundary, so a document can be one deeper in
one than the other, and `paint.opacity.end_to_end` is the document where both are stated.

## 7. Findings the phase recorded

Two, and both were found by a test rather than reasoned about in advance.

**The run must not read its own declaration.** A text node's box is the element that owns it, and
that element has already opened a scope around it — so a run that read its own style's `opacity`
would apply the same declaration a *second* time and square it. The traversal passes the strength
**in** to the text emitter (`paint_emit_text`'s `opacity` parameter) rather than letting it read
`computed`, and the case that would catch a regression is the three-drawing count in
`paint.opacity.end_to_end`: nested `0.5`/`0.5` with a run inside produces **three** faded drawings,
the third at `0.25` rather than `0.125`.

**A declaration that is not a number is not a strength of zero.** `robo_css_computed_value_opacity`
answers the initial value for a computed value that is not an opacity, which is the *only* reason a
document with no `opacity` in it is not blank. `paint.opacity.empty_and_malformed`'s last block — a
document with no computed styles at all, asserting every record is `1.0` — is the case that would
fail first if that door ever learned to answer zero.

## 8. Debug dump

The record and the command each grew the same field, written by each type's own `write` door, so the
two dumps agree on the spelling by construction:

```text
#0002  begin-box     node=2 depth=1 clip=visible/visible opacity=1.00 rect=0,0,40,40
#0003  push-opacity  node=2 depth=1 clip=visible/visible opacity=0.50 rect=0,0,40,40
#0004  background    node=2 depth=1 clip=visible/visible opacity=0.50 rect=0,0,40,40 color=#ff0000ff
#0005  content       node=2 depth=1 clip=visible/visible opacity=0.50 rect=0,0,40,40
#0006  pop-opacity   node=2 depth=1 clip=visible/visible opacity=0.50 rect=0,0,40,40
#0007  end-box       node=2 depth=1 clip=visible/visible opacity=1.00 rect=0,0,40,40
```

Read down the `opacity=` column: `1.00` before the push, `0.50` inside it, `1.00` again after the
pop. The pair's two lines carry the *same* value — §3's self-identifying rule, visible in the
format rather than only in the code.

## 9. What this build does **not** do the way CSS says

**CSS composites an element's whole subtree as a group and applies the opacity to the flattened
result. This build multiplies per drawing.**

The two agree whenever no two descendant drawings overlap; they disagree exactly when they do.
`paint.opacity.group_deviation` is the document where they part:

```html
<div style="opacity: 0.5; background-color: #ffffff">
    <div style="background-color: #ff0000"></div>   <!-- overlapping the parent's fill -->
</div>
```

A group implementation would composite white-then-red into one opaque result and halve *that*, so
the overlap is `50%` red over `50%` white. This build halves each fill separately and composites
them, so the overlap is the red at half over the white at half — a different pixel, and the case
computes **both** through `robo_gfx_composite_src_over_opacity`, the graphics module's own blend, so
the difference is a fact in the tests rather than a surprise in a dependency.

Group opacity needs an **offscreen layer** — a surface to composite into and a single blend of that
surface onto the target — which is a compositor, and out of this phase's scope by its own brief. The
record's shape is what a group implementation replaces: a `robo_paint_opacity_t` is the value a
layer would be composited at, and the scope pair is already the boundary such a layer would take.

## 10. Coordinate space

Unchanged from 11.1 and 11.8. An opacity is a scalar and has no coordinate; the pair's commands
carry the same two spaces every other command carries (the box's three rectangles in paint
coordinates, and the containing block's content origin), and they add no third. Nothing about the
strength is resolved against a position.

## 11. Visual smoke test

`paint.opacity.end_to_end` is the phase's document, and it is the brief's own:

```html
<div class="outer"><div class="inner">A</div></div>
```

with `.outer` half-opaque white and `.inner` half-opaque red at `font-size: 100px` — three drawing
kinds of this release inside one another, with a real face turning the run into a glyph. The pixels
are read out one by one: the outer's white survives at **alpha 128** (half over nothing), and the
inner's red is still red at its centre with **more of the white below it left than a plain half
would be** — the assertion that distinguishes "the child is a quarter of the way to white" from
"the child replaced its parent's strength".

The surface is then encoded as a **PNG through RoboGraphics' own encoder** — the one Phase 6
shipped, not a second one — whose signature and width are read back, which is what makes the blend
*inspectable* rather than merely asserted: a reader who wants to see the half-white with the
quarter-red inside it runs this case and opens the buffer.

Three narrower pixel cases pin the individual kinds, each against
`robo_gfx_composite_src_over_opacity` rather than against a number this suite computes:
`paint.opacity.background_pixels`, `paint.opacity.border_pixels` (a band, through the same pass
11.5 built) and `paint.opacity.text_pixels` (a glyph).

## 12. What is absent, on purpose

Blend modes and `mix-blend-mode`; filters and `backdrop-filter`; masks and `clip-path`;
`border-radius` clipping; transforms; animations and transitions; a compositor, compositor layers,
tile compositing and a raster scheduler; stacking contexts; scrolling; paint invalidation (11.11);
hit testing (11.10); and group opacity, as §9 states with the case that measures the gap. The
module's capability model names exactly what is here — `opacity` is **1** while `raster` stays **0**
— and version **0.8.0** is the release that made it so.

Nothing in this phase reads a Chromium header, links a Chromium library, or translates a Blink
class: the clip-and-strength model is two intervals and a real, and every door it is reached
through already existed.
