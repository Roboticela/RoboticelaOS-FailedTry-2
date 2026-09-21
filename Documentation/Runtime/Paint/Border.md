# RoboPaint — border painting (Phase 11.5)

The second drawing this module does, and the first one with a **shape**. 11.4 put a box's
background on a surface; this phase fills each of its **border sides**, and in doing so fixes one
thing that had been dropped a stage too early — the `border-style`.

Read `paint/docs/Decoration.md` for the pass itself: one walk, the colour bridge, the clip, the
counters, ownership and safety are all there and are shared. What follows is the border half.

```text
HTML / DOM → RoboCSS → RoboLayout → RoboPaint → records → display list
    → draw-border (one command, resolved) → RoboGraphics raster fill → the caller's surface
```

---

## 1. Two things the phase had to decide, and only one of them is about a rectangle

A border is **not** just a coloured rectangle, and the difference is what this phase exists to
state:

- **Where a side is** is the box model's conclusion, and it had already been reached. RoboLayout's
  used border record carries a per-side width, style and colour; the paint pass emits one record
  per side whose used width is above zero; the display list resolves each into the band it
  occupies. None of that is new here.
- **How a side looks** is the style, and 11.3 had discarded it. The band a side occupies is the
  difference between the border box and the padding box, and the four side kinds were collapsed
  into one command *because the side is consumed into that rectangle* — which is right, and which
  would also have consumed the style had it been left as an input to the derivation. A `dashed`
  band and a `solid` band with the same rectangle and colour are different pictures, and a
  rasteriser cannot recover which one it is looking at. So the style now travels
  **record → command** and is a required field of a `draw-border`, refused if absent.

That is the whole architectural content of the phase: one field, on both models, for a fact that
is not derivable.

## 2. The band

A band is the region of the **border box** outside the **padding box** on one edge, and the two
axes are answered differently on purpose:

| Side | Rectangle, for a border box `(x, y, w, h)` and insets top `t`, right `r`, bottom `b`, left `l` |
|---|---|
| top | `(x, y, w, t)` |
| right | `(x + w − r, y, r, h)` |
| bottom | `(x, y + h − b, w, b)` |
| left | `(x, y, l, h)` |

Both faces of an axis contribute to the *other* axis's extent, which is why a top band spans the
box's full width: a box with a left and a right border has two bands, each as tall as the box.

The arithmetic lives in `PaintOperationSideBand` (11.1) and is performed **once**, at
build time — the display command carries the one rectangle to fill and nothing above it ever
needs the padding box again. Every extent is floored at zero, so a box whose insets were saturated
produces an empty band rather than a negative one.

## 3. Paint order, and which side owns a corner

The four bands of a box are emitted in the order the sides are numbered — **top, right, bottom,
left** — and each spans one axis's full extent, so two bands always overlap at a corner and the
band drawn **last** owns it. For a box with four band colours the corners are therefore: top-left
`left`, top-right `right`, bottom-right `bottom`, bottom-left `left`.

This is a statement about **this build's simplification**, not about CSS: a real renderer mitres
the corners, so the four bands *union* to the border ring only because each covers the corner for
its side. It is pinned where it is observable — the suite reads the corner pixels — because a
later goal that mitres them must change that assertion deliberately.

Between boxes the order is the traversal's, and this is where the phase's one-pass decision earns
its keep: a descendant's background is filled **after** its ancestor's bands, so a child that
spills over its parent (a relative offset, an overflowing box) draws over the parent's border.
Filling every background and then every border would get exactly this wrong.

## 4. The style vocabulary

All ten of CSS's styles are represented on the command; two of them can never appear on one.

| Style | On a command? | What this build fills |
|---|---|---|
| `solid` | yes | the band — the style's shape *is* the band |
| `dotted`, `dashed`, `double`, `groove`, `ridge`, `inset`, `outset` | yes | the band, in the side's colour (**CSS's `solid` shape**), counted in `unshaped` |
| `none` | **no** | nothing: RoboLayout gives such a side a used width of zero, so no record is emitted |
| `hidden` | **no** | nothing, for the same reason — which is why the command's rule is `generates_border`, not "not `none`" |

The seven pattern styles are the phase's one deliberate simplification, and it is a **number**
rather than a silence. A border drawn as a solid band when the author asked for dots is exactly
the kind of quiet wrongness a rendering module should count instead of shipping, so
`PaintDecorationStats.unshaped` counts every band filled whose style is not `solid`, and
`border_painting == 1` is a claim about a border's **place and colour** rather than about every
border's look.

## 5. Colours

Per-side colours come from RoboLayout's used border record, exactly as the background's do, and
cross into RoboGraphics through the same one door (`PaintColorToRgba8`). There is no
second colour representation and no per-side conversion: `border-color: #ff0000 #00ff00 #0000ff
#ffff00` reaches the four commands in top, right, bottom, left order.

`currentColor` is resolved by the paint pass against the element's own `color` — the
`border-*-color` half of the "paint-visible" property class 10.14 named. One that stays
contextual is a legitimate record, and the pass counts it (`contextual`) rather than drawing a
guess.

## 6. What a band is not

- **Not a margin, a padding or a border *width* problem.** The band's geometry is derived from
  layout's own boxes; this layer introduces no arithmetic of its own beyond the saturation every
  accumulation in the module uses.
- **Not a fifth box layer.** The command carries one rectangle; there is no border-box-minus-
  padding-box record in the list, because the difference is the derivation and nothing above it
  needs both terms.
- **Not a clipping boundary.** A band is *bounded* by an ancestor's `overflow`, exactly as a
  background is — a band under a hidden ancestor is clipped by the same mechanism and counted in
  the same `clipped` — but a band does not itself clip anything.
- **Not shaped.** No mitring, no dashes, no rounded corners, no `border-radius`, no antialiasing,
  no border images or gradients.

## 7. What the suite pins

Fourteen cases under `paint.border.*` (`paint/docs/Tests.md` §1.9):

- `style_reaches_the_command` — for `solid`, `dashed`, `dotted` and `double`, the **record's**
  accessor and the **command's** field agree on all four sides, and every other kind carries
  `none`;
- `four_bands` — a 208×108 border box from `width: 200px; height: 100px; border: 4px solid`, with
  the four bands computed from that box rather than from absolutes, the content box four pixels
  inside it, and every band filled in its colour;
- `per_side_widths` — `border-width: 1px 2px 3px 4px` giving four bands of four different facing
  extents, each spanning its axis's full extent;
- `absent_sides` — `none`, zero width, `hidden`, a single-side shorthand and a per-side override:
  six documents one declaration apart, each asserting how many bands exist;
- `colors_and_corners` — the four shorthand colours, and the four corner pixels that say which
  side was drawn last;
- `with_background` — background plus border in one box: the background is the whole border box,
  emitted before the bands, with the ring, the interior and the outside each probed;
- `child_over_parent_border` — the decisive ordering case: a relatively offset child spilling over
  its parent's border, whose colour is what is over the border;
- `clipping` — a hidden ancestor against an identical `visible` control, plus the axis-split
  mechanism read from a hand-built band;
- `pattern_styles` — the seven pattern styles, each asserting the command's style *and* that the
  band was still filled, for the number `unshaped`;
- `hand_built_kinds` — a band with no style is refused, `hidden` is refused, and the three counter
  identities hold on a hand-built list;
- `determinism`, `mutation_isolation` — two passes over one list, an independently rebuilt list
  compared command for command, and a list that paints the same pixels after its sources were
  cleared and re-laid-out;
- `empty_cases` — an empty list, structural commands alone, `NULL` arguments, and a read-only
  surface mapped to `RoboNotSupported`;
- `end_to_end` — the full document (box model, nesting, a relative offset, a pattern border, hidden
  overflow), the pass's counters against the display list's own, the clipped-drawing count, and
  four probes inside one box.

Every pixel assertion reads the surface back, because that is the strongest form of "the colour
reached RoboGraphics" available without a screenshot.
