# Out-of-flow placement (Phase 14.0)

`position: absolute` and `position: fixed` — the behaviour 10.12 named and deferred. The
authority is `Runtime/Include/RoboLayout/RoboLayoutAbsolute.h`, the policy is
`Runtime/Source/RoboLayout/RoboLayoutAbsolute.c`, and the recursion that calls it is
`RoboLayoutBlock.c`'s.

It was pulled forward from the CSS-completion goal for one reason, and it is worth writing down
where a reader meets it: Phase 14's desktop shell cannot be **composed** without it. A wallpaper
that fills the viewport, a dock on the bottom edge, a menu floating over a window and a window
that is a rectangle at a place are four out-of-flow boxes, and a build that stacks them in normal
flow can express none of the four.

## 1. The three questions, and why one module answers all three

An out-of-flow box needs three answers and no two of them can be given apart from the third:

| Question | Answer |
|---|---|
| what is it placed **against** | its **containing block**: the nearest positioned ancestor's padding box, or the initial containing block (the viewport) when there is none — and always for `fixed` |
| how **big** is it | the insets are part of the size: `left: 0; right: 0` with `width: auto` sizes the box between them |
| **where** does it go | the near side (`left`/`top`) where it is declared, else the **far** side (`right`/`bottom`) — which needs the box's own size, so it is a second call |

The split into two doors follows from the third row: `LayoutAbsolutePlacementBuild` resolves the
request (containing block + both axes) *before* the box is laid out, and
`LayoutAbsolutePlacementOrigin` resolves the coordinate *after*, because `right: 8px` means "my
right edge is 8px in from the containing block's right edge" and is not a number until the box has
a width. 10.12 could only ever have implemented the near side; that is exactly why it declined the
two modes.

## 2. The axis policy

One function per axis (`LayoutAbsoluteResolveAxis`), asked three questions in this order:

1. **the near side first.** A declared `left`/`top` positions the axis. A *definite* declared size
   beside it makes the axis **over-constrained**: the near inset and the size win, the far inset is
   ignored, and `declaredOverConstrained` says the document wrote one (CSS's left-to-right answer,
   stated rather than inferred);
2. **then the far side**, when the near side says nothing — absent *or* `auto`. The axis is then
   anchored by its far edge and the used extent stays the box's own (`marginBoxExtent` is `-1`,
   which is the value that means "the box decides" and is not a size);
3. **then the size**, and only when *both* insets are present with no definite declared dimension:
   the insets size the axis between them, clamped into the geometry's range and floored at zero.

Four kinds of declaration are counted per side, and the two that are easy to lose are `auto` (a
declaration that says nothing, and *not* the same statement as nobody having written one) and
`UNRESOLVED` (a percentage or a relative unit — used as zero and counted, so a box positioned with
`top: 10%` is visible in the record rather than silently at the origin).

## 3. The containing block, and the one sign that must be right

The containing block is the ancestor's **padding** box, and every coordinate the block pass writes
is relative to the box's **content** edge — `LayoutBlockChildren` hands a child a cursor that
starts at the content origin, so a child's stored position is content-relative however far its own
padding pushed it. The padding box **origin** in that space is therefore
`(-padding-left, -padding-top)`: the padding box begins exactly where the padding does. A border
appears in the number nowhere, and that falls out rather than being suppressed — the content origin
is already inside the border, so the two edges differ by the padding and by nothing else.

The padding box **size** is the content size plus the padding on each axis, which is the same
number as the border box less the borders.

The **initial** containing block is the viewport, recorded once at the top of the pass with its
origin at the root element's content edge. One rectangle for every `fixed` box rather than one
before the root's call and another after it, because two spellings of one rectangle is how a
`fixed` box's position comes to depend on which ancestor happened to place it.

## 4. The walk, and flow

* `LayoutBlockChildren` **skips** an out-of-flow child: it takes no room, moves no sibling and does
  not contribute to its parent's `auto` height. The check is before the display verdict, because
  those are two different questions.
* After a box's own geometry is final — height resolved, size known — if the box **is positioned**
  it places the out-of-flow descendants whose containing block it is (`LayoutBlockPlaceDescendants`).
  The walk descends through **static** children carrying the same containing block (a static wrapper
  is transparent to containment), and does not descend into a **positioned** child (it placed its own
  descendants, and it is a containing block of its own).
* A `fixed` box takes the **viewport** rectangle whichever ancestor found it — the one thing that
  separates it from `absolute`.
* The absolute box's `object->positionOffset` is the **cleared** record: a box that is out of flow
  has no flow position for a caller to recover, and `LayoutPosition.h`'s field exists so that "where
  flow put it" is never guessed.

## 5. Not implemented, and named rather than implied

* **`z-index`, stacking contexts and paint order.** Nothing is reordered — paint is still document
  order, which is why the phase is named for *placement* and not for stacking, and why the desktop
  shell's own CSS does not use `z-index` to say what is on top.
* **`sticky`.** In flow, taking no offset, exactly as 10.12 left it: there is no scrollport to
  constrain against.
* **`shrink-to-fit`.** A box with `width: auto` and fewer than two insets fills what is left,
  because this build has no intrinsic width to shrink to (`Sizing.md` names `min-content`,
  `max-content` and `fit-content` as later work).
* **The static position.** CSS places a box with neither side declared at its *static position*;
  this build states "the containing block's origin" instead, in one sentence and one function.
* **Percentages**, for either an inset or the containing block: read, recorded unresolved, used as
  zero.
* **`box-sizing: border-box`** anywhere in the build, so every size here is the margin box and the
  pass subtracts the margins, border and padding as it does in flow.
* **Out-of-flow *inline-level* boxes.** The inline pass owns inline participants and still places
  one on its line; the walk here is the block direction's.
* **Overflow.** An out-of-flow descendant's region is not folded into its containing block's 10.13
  record, which was measured before it was placed. That is the scrollable-overflow question a
  scrollport exists to answer.

## 6. Where it shows up

| Field / door | What it says |
|---|---|
| `LayoutGetCapabilities().outOfFlow` | 1 since 14.0, with the absences above named at the flag |
| `LayoutBlockStats.absolute` | the phase's own seven counters, kept in its own record rather than copied flat into the block stats |
| `LayoutBlockStats.offsetsDeclined` | no longer counts `absolute`/`fixed`: they are not laid out in flow at all, so no displacement is resolved for them |
| `RoboLayoutDomainAbsolute` | the twenty-third diagnostic domain |

## 7. Test map

Twenty-one cases in `Tests/Layout/SuiteRoboLayoutAbsolute.c`, domain `layout.absolute.*`:

| Case | Asks |
|---|---|
| `axis_vocabulary`, `side_record`, `containing_block_record` | two axes, four kinds over two sides, and the rectangle a box is placed against |
| `policy_near_wins` | the near side with a definite size (over-constrained) *and* the same two insets without one (stretched) |
| `policy_far_anchors` | `right: 8px` alone, and the same with `left: auto` — the far side positions, the `auto` is counted |
| `policy_size_from_insets` | 350 out of 400 with 20 and 30 declared; the definite-size refusal; the zero floor |
| `policy_unresolved` | a percentage (counted, zero) and a negative length (usable, honoured), plus the door's refusals |
| `axis_use_record` | the record's self-consistency rules |
| `placement_origin` / `placement_origin_clamps` | the far side needs the box's width; a coordinate before the containing block is answered; an extreme sum saturates rather than wraps |
| `content_height` | the margin box less the resolved insets, floored at zero, and the *miss* for an axis the insets did not size |
| `placement_build` | the whole request from one style, its round trip, and the refusal of an in-flow record |
| `classification` | the two doors over all five modes, and a NULL record as `static` |
| `block_positioned_wrapper` | **the phase's central case**: the padding box origin, the box's size, and the sibling after it unmoved |
| `containing_block_padding_edge` | the containing block is the *padding* edge, in a space where the content edge is zero |
| `block_viewport` | no positioned ancestor → the initial containing block, whatever the static ancestors do |
| `block_stretch` | the wallpaper: two insets on each axis fill the initial containing block |
| `block_fixed` | the dock: `fixed` ignores a positioned, offset ancestor — and a second pass at another viewport moves it |
| `static_wrapper_transparent` | a `static` wrapper with padding and a border is *not* the containing block |
| `nested_containing_blocks` | the **nearest** positioned ancestor wins |
| `inset_kinds` | a negative length, `right: auto` and `top: 25%` through real computed styles |
| `counters_determinism` | five counters that describe facts geometry alone cannot separate, plus a hundred-pass allocator balance |

## 8. What the suite found

Two findings, and both were in the implementation rather than in the expectations:

1. **The containing block had the padding on the wrong side of the origin.** The first pass used
   `finalOrigin + border + padding`, which is the *content* origin — so an absolutely positioned box
   inside a padded wrapper was placed one padding further in than CSS says, and the number looked
   plausible in every document with no padding. `layout.absolute.containing_block_padding_edge` is
   the case that pins it: the padding box begins where the padding does, so in content-relative space
   the insets start *before* zero.
2. **`left`/`right` with `width: auto` was read as "the near side wins" and nothing else.** The
   expectation in the policy case was written as if two insets without a declared dimension still
   declined the far side; the policy *is* CSS's — the insets size the axis — and the case was
   rewritten to assert both halves of the rule (over-constrained *with* a definite size, stretched
   *without* one), because those are the two questions a build gets wrong together.
