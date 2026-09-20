# RoboPaint — background painting (Phase 11.4)

The first place in this module where a colour reaches a **surface**. Everything before it
produced *decisions*: RoboLayout concluded where every box is, the paint pass ordered the
operations, the display list resolved the box model into one rectangle per command. This phase
fills one kind of those commands, and the pipeline it completes is the one the earlier phases
drew with an empty last arrow:

```text
HTML / DOM → RoboCSS → RoboLayout → RoboPaint → records → display list
    → draw-background → RoboGraphics raster fill → the caller's surface
```

Two entry points, both under `Include/RoboPaint/`:

| Door | What it is |
|---|---|
| `robo_paint_color_is_drawable`, `robo_paint_color_to_rgba8` (`RoboPaintColor.h`) | the colour bridge: RoboCSS's colour into RoboGraphics' |
| `robo_paint_background_paint`, `robo_paint_background_stats_t` (`RoboPaintBackground.h`) | the pass: walk a display list, fill its `draw-background` commands |

`ROBO_PAINT_DRAWING_IMPLEMENTED` is **1** since this release, and the condition the version
header wrote down for the flip is exactly what happened: *a build in which an operation's
rectangle and colour reach a RoboGraphics surface*. The fence that keeps the claim honest is
`raster`, which stays **0** — see §8.

---

## 1. What is painted, and what is not

`robo_paint_background_paint(list, target, out_stats, diag)` walks the display list **once, in
order**, and for each command:

| Command | What the pass does |
|---|---|
| `draw-background` | resolve the colour, apply the command's clip, fill the command's rectangle |
| `begin-node`, `content`, `end-node` | **skipped** and counted — they draw nothing |
| `draw-border` | **skipped** and counted — the band is resolved, the rasterisation is a later goal's |

So the pass is deliberately a **background-only painter**, and "the backgrounds are painted and
the borders are not" is a number a reader sees (`skipped`) rather than a silence. The display
list is the input because it is the artifact whose geometry is *resolved*: a command carries the
one rectangle to fill, so the painter never reads the box model, a computed style or the tree.
The only conversion left is the colour.

## 2. The supported background model

Only what RoboCSS already represents, which is `background-color` — a single colour, resolved by
the paint pass before the command was built:

- a resolved colour fills its rectangle;
- `background-color: transparent` (the property's initial value, and the same record as an
  undeclared background) **emits no command at all**, so a plain `<div>` has no background
  command rather than an invisible one;
- `background: <colour>` is the shorthand RoboCSS already expands, and this layer reuses that
  expansion rather than parsing anything;
- `currentColor` is resolved by `RoboPaintContext.c` against the element's own `color`, because
  that layer has the element. One that stays contextual — `background-color: currentColor` on an
  element with no usable `color` — is a legitimate record, and the painter **counts and skips**
  it.

Not supported, each because the CSS layer does not represent it yet: background images,
gradients, multiple backgrounds, `background-repeat`, `background-position`, `background-size`,
`background-clip` and `background-origin`. The painting area is the **border box**, which is
CSS's own initial `background-clip` rather than a simplification of it.

## 3. Geometry

The rectangle is the command's own, in **paint coordinates** (one page space, ancestors
accumulated) — the same numbers RoboLayout wrote, with no origin to flip. The painter does not
recompute it, re-clip it against the box model or translate it:

- a background acts on the **border box**;
- a zero-area rectangle is a **valid record and a no-op fill**, counted (`empty`): "I would fill
  this rectangle" and "there is no rectangle" are different facts and the counters keep them
  apart;
- an unclipped fill's **centre** is where the command says it is, which is the form the suite
  asserts — and where fills overlap, the pixel is the **last** command's colour, because the
  fills happen in the list's order.

## 4. The colour bridge

`robo_paint_color_to_rgba8` is the whole of the conversion, and it is a **field-for-field copy**
between two 8-bit sRGB records: RoboCSS's `robo_css_color_t` and RoboGraphics'
`robo_gfx_color_rgba8_t`. There is no arithmetic, no colour space and therefore nothing to lose
or invent — including the alpha channel, which survives rather than being flattened.

The four CSS colour kinds are **decided rather than flattened**, because three of them are not
colours a surface can take:

| Kind | `is_drawable` | `to_rgba8` | Why |
|---|---|---|---|
| `RGBA` with `alpha != 0` | 1 | `ROBO_OK` | the channels copy straight across |
| `RGBA` with `alpha == 0` | 0 | `ROBO_OK` | a colour that occupies no pixel — an answer, not a failure |
| `currentColor` | 0 | `ROBO_NOT_SUPPORTED` | only the layer that owns an element's `color` can resolve it; this bridge has no element, and the pass already resolved it |
| `none` | 0 | `ROBO_NOT_FOUND` | nothing was declared |
| `invalid` | 0 | `ROBO_INVALID_ARGUMENT` | it was meant to be a colour and was not — a mistake rather than an absence |

`robo_paint_color_is_drawable` is exactly *a resolved colour with a non-zero alpha*, which is the
predicate the paint pass used to decide whether to emit the record in the first place — one rule,
stated once, read by both.

## 5. Paint order

There is no sorting and no batching: the pass fills in the list's order, and the list's order is
the tree's. For a document, that means a box's own background is filled **before** its
descendants', so a child's fill covers its parent's where they overlap — the classic proof that
the sequence is depth-first rather than a set of fills. A later goal may reorder for stacking
contexts; this phase does not.

## 6. Clipping is read, not implemented

A display command carries the boundary in force from the paint pass (Phase 11.1) as **two axes**,
because `overflow-x: hidden` beside `overflow-y: visible` is a legal declaration and a single
rectangle cannot express it. The painter uses it, and uses it *through the graphics layer*:

1. reset the raster's clip to the surface bounds — so nothing from the previous command leaks;
2. if neither axis is bounded, stop: the fill is bounded by the surface, exactly as it was in
   11.1;
3. for an axis that *is* bounded, replace that axis's interval with the boundary's; an axis the
   boundary leaves open keeps the surface's own interval;
4. `robo_gfx_raster_set_clip` intersects, so "the boundary bounds the fill" is the graphics
   module's arithmetic rather than a second copy of it.

No raster clipping architecture is added, no scroll clipping and no compositor clipping. What
this buys is the correct behaviour for a box's own paint: an `overflow: hidden` ancestor bounds a
descendant's background, which the suite demonstrates against a control document identical to the
first but for that one declaration.

## 7. What the pass reports

`robo_paint_background_stats_t` is a value record owned by nobody, and its fields are the
**pairs**, because the interesting facts are differences:

| Field | Means |
|---|---|
| `commands` | display commands visited |
| `backgrounds` | `draw-background` commands seen |
| `filled` | backgrounds that put a colour on the surface |
| `skipped` | commands that are not backgrounds: structural records and border bands |
| `contextual` | backgrounds whose colour stayed `currentColor` |
| `transparent` | backgrounds whose colour is fully transparent |
| `empty` | backgrounds with a zero-area rectangle |
| `clipped` | backgrounds drawn under a boundary the command carried |

Two identities hold by construction and are asserted, so no counter is a remainder nobody
checked:

```text
commands = backgrounds + skipped
backgrounds = filled + contextual + transparent + empty
```

`transparent` can only be reached by a hand-built command, since the pass emits nothing for a
transparent colour — and it is kept apart from `contextual` because "the author asked for
nothing" and "the colour could not be resolved here" are different facts about a document.

## 8. Why `raster` is still 0, and what `drawing` claims

`drawing` — and `ROBO_PAINT_DRAWING_IMPLEMENTED`, and `robo_paint_drawing_is_implemented()` — say
**1**, because a `draw-background` command's rectangle and colour reach a RoboGraphics surface.
That is a narrow claim and reading it wider is the mistake it is shaped to prevent:

- it does **not** claim borders, text, images, rounded corners, gradients, transforms,
  antialiasing or a compositor;
- it does **not** claim a rasteriser of the whole list — that is `raster`, which is **0**,
  because this painter consumes exactly one command kind and skips the rest;
- `background_painting` is **1** and says *which* drawing exists, so a caller deciding whether to
  make a surface has a field that answers it exactly.

## 9. Ownership and determinism

- The **surface is the caller's**: the painter retains nothing, destroys nothing and creates
  nothing it does not also destroy. It binds a `robo_gfx_raster_t` over the target for the
  duration of the pass and releases it before returning, so the clip and the source-over blend
  are RoboGraphics' rules rather than a second compositor's.
- The **list is read through `const`** and never mutated; the painter holds no reference to it
  after returning, and the records and the tree are untouched.
- The pass is **deterministic**: the order is the list's, every number it writes is a command's
  own, and painting the same list twice produces identical pixels and an identical record. An
  opaque fill is idempotent, which is what makes a rebuilt display list safe to repaint in place.
- Since a display list is a **snapshot** (11.3), clearing the record list and laying the tree out
  again changes not one pixel the existing list paints.

## 10. Safety

| Input | Answer |
|---|---|
| `NULL` list or `NULL` target | `ROBO_INVALID_ARGUMENT`, and the diagnostic says which |
| an **empty** list | `ROBO_OK`, every counter nought — a legitimate pass |
| a list of structural commands alone | `ROBO_OK`, every command skipped and counted |
| a **read-only** or **unimplemented-format** target | `ROBO_NOT_SUPPORTED` — the graphics layer's own refusal, mapped once |
| out of memory binding the raster | `ROBO_OUT_OF_MEMORY` — not folded into the line above, because that is this build failing rather than the caller's surface being wrong |
| a fill or clip that the graphics layer refuses | `ROBO_ERROR` with a diagnostic naming the step |

`robo_paint_background_stats_init(NULL)` is safe, and `out_stats` may be `NULL`.

## 11. Deferred, and to whom

| Not here | Where it belongs |
|---|---|
| border band rasterisation | a later 11.x goal; the band rectangle already exists |
| text and image painting | later 11.x goals — they need shaping and glyph positions |
| display-list-wide rasterisation | `raster` stays 0 until a consumer handles *every* kind |
| real clipping, scroll clipping, compositor clipping | later phases |
| opacity, transforms, filters, shadows, gradients, `border-radius` | later phases |
| `z-index` and stacking contexts | a later stacking pass reorders the list; this pass reads it as it stands |
| paint invalidation (11.11) and hit testing (11.10) | this module keeps no cache and has no dirty rectangles |
| framebuffer presentation, compositor, GPU | RoboRender and above |

## 12. What the suite pins

Fourteen cases under `paint.background.*` (`paint/docs/Tests.md` §1.9): the colour bridge's five
kinds; a document's colour landing in the rectangle the display list named and nowhere else; the
fill order proving depth-first traversal; the clip A/B against a `visible` control plus the
axis-split mechanism read from a hand-built command; the three "nothing to paint" documents; the
zero-area record; a relative offset moving the fill and not growing the box; the counter
identities; the four counters and the clip on a hand-built list; determinism across two surfaces
and a repeat pass in place; snapshot isolation across a re-layout; and the empty and refusing
cases. Every one of them reads the surface back, because that is the strongest form of "the
colour reached RoboGraphics" available without a screenshot.
