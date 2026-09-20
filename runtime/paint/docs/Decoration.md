# RoboPaint — decoration painting (Phases 11.4–11.8)

The first place in this module where a colour reaches a **surface**. Everything before it
produced *decisions*: RoboLayout concluded where every box is, the paint pass ordered the
operations, the display list resolved the box model into one rectangle per command. This pass
executes those commands, and the pipeline it completes is the one the earlier phases drew with an
empty last arrow:

```text
HTML / DOM → RoboCSS → RoboLayout → RoboPaint → records → display list
    → push-clip / draw-background / draw-border / draw-text / pop-clip → RoboGraphics raster
    → the caller's surface
```

Two entry points, both under `Include/RoboPaint/`:

| Door | What it is |
|---|---|
| `robo_paint_color_is_drawable`, `robo_paint_color_to_rgba8` (`RoboPaintColor.h`) | the colour bridge: RoboCSS's colour into RoboGraphics' |
| `robo_paint_decoration_paint`, `robo_paint_decoration_stats_t` (`RoboPaintDecoration.h`) | the pass: walk a display list, execute its **drawing** commands |

`ROBO_PAINT_DRAWING_IMPLEMENTED` is **1** since 11.4, and the condition the version header wrote
down for the flip is exactly what happened: *a build in which an operation's rectangle and colour
reach a RoboGraphics surface*. The fence that keeps the claim honest is `raster`, which stays
**0** — see §8.

**One pass for all three kinds, and the reason is the order.** A box's background, its four border
sides and its text runs are all *drawings*, and the list interleaves them the way the traversal
emits them: a box's background, then its bands, then the box's own contents — a run among them —
then its descendants'. CSS's paint order is exactly that, so a child that spills over its parent
draws **over** the parent's border. A background-only walk followed by a border-only walk and then
a text-only walk would fill every background in the document before every border and draw every
character after every band, which is a different and visibly wrong picture. 11.4's door was
background-only, 11.5 **replaced** it rather than adding beside it, and 11.6 **extended** it with
one argument — the face — rather than with a second pass over the same list; `paint/docs/Border.md`
documents the border half and `paint/docs/Text.md` the text half.

---

## 1. What is painted, and what is not

`robo_paint_decoration_paint(list, target, face, out_stats, diag)` walks the display list **once, in
order**, and for each command:

| Command | What the pass does |
|---|---|
| `push-clip` | the command's clip becomes the boundary in force, on the pass's own stack (11.8). Nothing is drawn and the raster is not touched |
| `draw-background` | resolve the colour, apply the clip in force, fill the **border box** |
| `draw-border` | resolve the colour, apply the clip in force, fill the side's **band** (11.5) |
| `draw-text` | resolve the colour, apply the clip in force, resolve each code point through the **face's cmap** and stroke its outline at the resolved pixel size (11.6) |
| `pop-clip` | the enclosing boundary is in force again; a pop on an empty stack is a reported error rather than a silent no-op (11.8) |
| `begin-node`, `content`, `end-node` | **skipped** and counted — they draw nothing by definition |

So "the drawings reach a surface and the structural commands do not" is a number a reader sees
(`skipped`) rather than a silence. The display list is the input because it is the artifact whose
geometry is *resolved*: a command carries the one rectangle to fill, the boundary in force and,
for a band, the style that shapes it — so the painter never reads the box model, a computed style
or the tree. The only conversion left is the colour.

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

## 6. Clipping: one stack, one application point

A display command carries the boundary in force from the paint pass (Phase 11.1) as **two axes**,
because `overflow-x: hidden` beside `overflow-y: visible` is a legal declaration and a single
rectangle cannot express it. Since 11.8 the pass also reads the **scope** the stream states, and
the whole of the clipping here is one stack and one application point:

```text
push-clip  → the command's clip becomes the boundary in force (the pass's own stack)
pop-clip   → the one outside it is in force again (a pop with nothing to pop is an error)
drawing    → the clip in force: the scope's if one is open, else the command's own
```

Reading the scope rather than the command is not observable for any list this build produced —
the builder refuses a command inside a scope that disagrees with it — which is exactly what makes
it safe, and it is what makes the stack **load-bearing** for a hand-built list: a drawing that
carries no clip of its own, inside a scope whose clip is empty, draws nothing
(`paint.clip.hand_built_scope`). Outside any scope the command's own clip is the only statement of
clipping a list without clip commands carries.

The application itself is unchanged and stays *through the graphics layer*:

1. reset the raster's clip to the surface bounds — so nothing from the previous command leaks;
2. if neither axis is bounded, stop: the fill is bounded by the surface, exactly as it was in
   11.1;
3. for an axis that *is* bounded, replace that axis's interval with the boundary's; an axis the
   boundary leaves open keeps the surface's own interval;
4. `robo_gfx_raster_set_clip` intersects, so "the boundary bounds the fill" is the graphics
   module's arithmetic rather than a second copy of it.

It is done in **one place for every drawing kind** — a background, a border band and a run are
bounded by the same four steps and no painter carries its own copy of them. No raster clipping
architecture is added, no scroll clipping and no compositor clipping. What this buys is the
correct behaviour for a box's own paint: an `overflow: hidden` ancestor bounds a descendant's
background, which the suite demonstrates against a control document identical to the first but for
that one declaration — and, since 11.8, also for text runs and border bands, with a nested scope
whose effective clip is recomputed from the tree rather than read off the list
(`paint/docs/Clipping.md`).

## 7. What the pass reports

`robo_paint_decoration_stats_t` is a value record owned by nobody, and its fields are the
**pairs**, because the interesting facts are differences:

| Field | Means |
|---|---|
| `commands` | display commands visited |
| `draws` | commands that draw something: backgrounds, border bands and text runs |
| `backgrounds` | `draw-background` commands seen |
| `borders` | `draw-border` commands seen — one per side whose **used width** is above zero |
| `texts` | `draw-text` commands seen (11.6) |
| `filled` | fills this pass **executed** — the raster's own no-ops (an empty clip, a rectangle with no surface pixels) are bounded there rather than here |
| `skipped` | commands the pass does not attempt to draw: the structural kinds (the markers, `content`, and since 11.8 the `push-clip`/`pop-clip` pair), and a drawing whose colour the record layer's own rule refuses |
| `contextual` | drawings whose colour stayed `currentColor` |
| `transparent` | drawings whose colour is fully transparent |
| `empty` | **fills** with a zero-area rectangle — a text run is exempt, because a run's rectangle is its *advance* box and a glyph's ink can overhang it (11.6) |
| `no_font` | text commands with no face to draw them — a fact about the **call**, not the document (11.6) |
| `unshaped` | borders filled whose style this build draws as a band rather than as its pattern (11.5) |
| `clipped` | drawings executed under a boundary — the scope's if one is open, the command's own otherwise (11.4, 11.8) |
| `clip_scopes` | clip scopes the pass walked: one per `push-clip`, asserted equal to the traversal's own count (11.8) |
| `clip_depth_max` | the deepest the pass's clip stack went — the nesting a document actually reached (11.8) |
| `glyphs` | glyph draws attempted (11.6) |
| `glyphs_missing` | code points that resolved to `.notdef`, or whose metrics would not parse (11.6) |
| `rtl` | runs drawn left to right from their origin (11.6) — the count is how a build with no bidi says so |

Three identities hold by construction and are asserted, so no counter is a remainder nobody
checked:

```text
commands = draws + skipped
draws    = backgrounds + borders + texts
draws    = filled + contextual + transparent + empty + no_font
```

`transparent` can only be reached by a hand-built command, since the pass emits nothing for a
transparent colour — and it is kept apart from `contextual` because "the author asked for
nothing" and "the colour could not be resolved here" are different facts about a document.
`unshaped` is a subset of `filled`, not a separate outcome: those bands **were** drawn, as their
band, and the number says how many were drawn as a simplification. `no_font` is the fourth
reason a *drawing* draws nothing, and it belongs on the far side of `draws` from the other three
because it is about the call rather than about the document — which is exactly what makes a
caller that forgot a face able to tell its blank frame apart from a textless page.

## 8. Why `raster` is still 0, and what `drawing` claims

`drawing` — and `ROBO_PAINT_DRAWING_IMPLEMENTED`, and `robo_paint_drawing_is_implemented()` — say
**1**, because a **drawing** command's rectangle and colour reach a RoboGraphics surface. That is
a narrow claim and reading it wider is the mistake it is shaped to prevent:

- it does **not** claim images, rounded corners, gradients, transforms, antialiasing or a
  compositor — and it claims a text run's glyphs only in the narrow sense `Text.md` §7 states: 11.6
  draws the runs RoboLayout measured, in **logical order**, at the advances the font's own `hmtx`
  gives, with no shaping and no bidi;
- it does **not** claim the seven pattern border styles — a band's shape for those is drawn as
  CSS's `solid`, and `unshaped` counts them — nor that a `draw-border` command's style has any
  consumer yet: the style travels for the rasteriser that lands later, and the band is what this
  build fills;
- it does **not** claim a rasteriser of the whole list — that is `raster`, which is **0**, because
  the structural command kinds are not drawings;
- `background_painting`, `border_painting`, `text_painting` and `clipping` are **1** and say *which*
  parts of the pipeline exist, so a caller deciding whether to make a surface has fields that
  answer it exactly. `clipping` claims rectangular clipping of a box's contents and nothing more:
  no rounding, no `clip-path`, no masks, and no clip the compositor or a scroll container owns.

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
| a `NULL` **face** | legitimate: every `draw-text` is counted (`no_font`) and skipped, so a textless document paints identically |
| an **empty** list | `ROBO_OK`, every counter nought — a legitimate pass |
| a list of structural commands alone | `ROBO_OK`, every command skipped and counted |
| a **read-only** or **unimplemented-format** target | `ROBO_NOT_SUPPORTED` — the graphics layer's own refusal, mapped once |
| out of memory binding the raster | `ROBO_OUT_OF_MEMORY` — not folded into the line above, because that is this build failing rather than the caller's surface being wrong |
| a fill or clip that the graphics layer refuses | `ROBO_ERROR` with a diagnostic naming the step |

`robo_paint_decoration_stats_init(NULL)` is safe, and `out_stats` may be `NULL`.

## 11. Deferred, and to whom

| Not here | Where it belongs |
|---|---|
| the **patterns** inside a dashed, dotted, double, groove, ridge, inset or outset band | a later 11.x goal; the style already travels on the command, and `unshaped` counts what is drawn as a band |
| shaping, bidi, line breaking, fallback fonts and every text effect | Phase 14 and RoboText; 11.6 draws the runs layout measured, in logical order (Text.md §7) |
| image painting | 11.7 — it needs a decoded surface or an encoded payload, which is the next kind's decision |
| display-list-wide rasterisation | `raster` stays 0 until a consumer handles *every* kind |
| real clipping, scroll clipping, compositor clipping | later phases |
| opacity, transforms, filters, shadows, gradients, `border-radius` | later phases |
| `z-index` and stacking contexts | a later stacking pass reorders the list; this pass reads it as it stands |
| paint invalidation (11.11) and hit testing (11.10) | this module keeps no cache and has no dirty rectangles |
| framebuffer presentation, compositor, GPU | RoboRender and above |

## 12. What the suite pins

Fourteen cases under `paint.background.*`, fourteen under `paint.border.*` and seventeen under
`paint.text.*` (`paint/docs/Tests.md` §1.8–§1.10). The text half is `paint/docs/Text.md`; it adds
the one thing the other two cannot: an expectation that is a **string**, read back through the
pool's own door, plus a synthetic face whose glyph expectations are arithmetic. The background
half: the colour bridge's five kinds; a
document's colour landing in the rectangle the display list named and nowhere else; the fill
order proving depth-first traversal; the clip A/B against a `visible` control plus the axis-split
mechanism read from a hand-built command; the three "nothing to paint" documents; the zero-area
record; a relative offset moving the fill and not growing the box; the counter identities; the
counters and the clip on a hand-built list; determinism across two surfaces and a repeat pass in
place; snapshot isolation across a re-layout; and the empty and refusing cases. The border half
is `paint/docs/Border.md` §7. Every one of them reads the surface back, because that is the
strongest form of "the colour reached RoboGraphics" available without a screenshot.
