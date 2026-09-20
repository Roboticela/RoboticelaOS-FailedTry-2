# RoboLayout Overflow — Phase 10.13

Module version at this record: **0.13.0**. Phase **10.13 — Overflow**.

---

## 1. What the phase is

Every phase up to 10.12 assumed the contents **fit**. This one admits they may not,
and its whole shape follows from a single decision about what an overflow *is*:

```text
an overflow is NOT a size.

the box is the size its declarations and its contents asked for, and it stays
that size; what this phase adds is a second rectangle -- the region the contents
reached -- recorded BESIDE the box rather than folded into it.
```

That is CSS's own model (`overflow` does not make a box grow; it decides whether what
does not fit is drawn or clipped), and it is why the record is an **extent per side**
rather than a second size. A box that overflows on the left and a box that overflows
on the right have the same width and two different overflow regions, and a size cannot
tell them apart.

```text
┌───────────────────────────┐
│ element box (padding box) │
│  ┌─────────────────────┐  │
│  │ content             │  │
│  │       ┌─────────────┼──┼──▶ contents reach past the right edge
│  │       │             │  │
│  └───────┴─────────────┘  │
└───────────────────────────┘
        the element's own rectangle is unchanged
        the right extent records that a thing got out
```

The box model was completed by 10.9–10.11 (margins outside, a border, a padding,
content inside) and 10.12 gave a box a second coordinate. 10.13 gives it a
**second rectangle**.

## 2. The RoboCSS side, first

RoboLayout consumes computed values and parses nothing, so the phase starts with a
vocabulary (`RoboCSS/include/RoboCSSOverflow.h`).

`overflow` is the first property in this build whose value is not about the box at all
but about the **room the box has**, and two facts about it decided the module's shape:

**The value is per axis, and the axis belongs to the *property*.** `overflow-x` and
`overflow-y` are two declarations and a box can clip horizontally and not vertically.
So the vocabulary is one *value* — `visible` or `hidden` — and `overflow-x: hidden` and
`overflow-y: hidden` are two typed values of that one value set. Nothing downstream
ever sees a single collapsed `overflow`.

**The shorthand is read, not expanded.** No phase in this runtime expands a shorthand:
`margin`, `padding`, `border` and `background` are registered as shorthands and *carry
their tokens* while their longhands are what anything reads. `overflow` could have
taken the same road and deliberately does not — `overflow: hidden` is written on nearly
every box an author wants clipped, and dropping it because the expansion machinery does
not exist would make a common document lay out as if nobody had written anything. So
the shorthand's *own name* is registered on this vocabulary too, and a consumer that
finds no longhand reads it as speaking for **both** axes, which is what the shorthand
means one step earlier than CSS computes it.

What that costs is stated where it is paid: `overflow: hidden visible` is legal CSS
this build does not read, and a declaration written that way is an **invalid value**
rather than a half-understood one.

```text
overflow                        shorthand -- read as both axes
overflow-x / overflow-y         the longhands -- read one axis at a time, first
two values                      visible, hidden
named and not added             scroll, auto, clip
```

The set is closed, like `display`'s, `position`'s and `border-style`'s, for the same
reason: a layout pass *dispatches* on this value, so `overflow: banana` has to be a
mistake rather than a keyword carrying text that a consumer silently fails to
interpret. `scroll` and `auto` are *scrolling*, which needs a scrollport, scrollbars and
a scroll offset that no phase here has; `clip` is a clipping rule this build's single
boundary does not distinguish from `hidden`. Each is named so the phase that adds it
knows where it goes.

## 3. The layout vocabulary

### The axis

Two, named for the **direction** rather than for a letter, because every door takes
one: a mode is per axis, and a build that indexed the pair by `0` and `1` would make
`clips(1)` a question about the document's writing conventions.

```c
ROBO_LAYOUT_OVERFLOW_AXIS_HORIZONTAL = 0   /* overflow-x */
ROBO_LAYOUT_OVERFLOW_AXIS_VERTICAL   = 1   /* overflow-y */
```

### The declared axis, in three kinds

| Kind | Meaning |
|---|---|
| `ROBO_LAYOUT_OVERFLOW_ABSENT` | nobody declared this axis; CSS's initial value applies because it applies |
| `ROBO_LAYOUT_OVERFLOW_RESOLVED` | a mode this build has (carried in the record) |
| `ROBO_LAYOUT_OVERFLOW_UNRESOLVED` | declared, and not a value this build can use; used as `visible` |

`ABSENT` is **0**, so a cleared record holds the state in which nobody declared
anything — the same decision 10.9's margin and 10.12's inset vocabularies made.

The third kind is the one worth stating: a declaration this build could not use is a
*declaration*, not an absence. `overflow-x: var(--o)` and `overflow-x: scroll` both
arrive as unusable (a carried reference and a refused identifier are indistinguishable
by the time a computed style holds them) and both are **counted** rather than dropped,
because a document whose clipping depends on one should be visible as such.

### The used overflow — two kinds of fact in one record

```c
typedef struct robo_layout_overflow_edges {
    robo_css_overflow_t horizontal, vertical;   /* the used MODE, per axis */
    robo_gfx_pixel_x_t  left, right;            /* how far contents reached past ... */
    robo_gfx_pixel_y_t  top, bottom;            /* ... the padding box, each >= 0  */
    size_t axes_resolved;                       /* declared modes that reached geometry */
    size_t axes_unresolved;                     /* declared axes this build cannot use */
} robo_layout_overflow_edges_t;
```

A consumer needs both halves together: the **modes** tell a paint pass *whether* there
is a boundary without re-reading a style and re-deciding which declarations this build
could use, and the **extents** tell it *what* it would be clipping and tell a parent
what its child contributes.

The three counters partition the two axes:

```text
axes_resolved      an axis whose declared mode reached geometry. Counted even when
                   the mode is `visible`, because `overflow-x: visible` is a
                   declaration an author wrote and is not the same statement as
                   nobody having written anything.

axes_unresolved    an axis declared with a value this build could not use. Its used
                   mode is `visible`.

the remainder      the axes nobody declared, in which CSS's initial value applies.

axes_resolved + axes_unresolved <= 2
```

## 4. Four numbers, and they are measured from the **padding box**

```text
left / top / right / bottom    how far the contents reached BEYOND that edge,
                              each one non-negative, each one zero when nothing
                              reached past it
```

The reference is the padding box because that is where CSS puts the clipping boundary
(`overflow: hidden` clips to the padding edge, not to the border edge and not to the
content edge) and because it is the one rectangle a box's own declarations cannot move:
a margin is outside it, a border is outside it, and a padding is *inside* it. So a box
with `padding: 20px` and a child that sticks 5px out of the padding box has an overflow
of **5**, whatever its border does.

A box whose contents fit has four zeroes, which is the common case and is not an error
— it is a box with **no overflow**, and the difference between it and a box nothing
measured is the object's *result state*, exactly as everywhere else in this module.

Every extent is an integer pixel count, every subtraction is done in **64 bits and
saturated**, and no extent can come out negative: a box whose contents reached *inside*
it has an overflow of zero, which is a different statement from a negative one and the
reason the extents are clamped at the boundary rather than allowed through zero.

## 5. The one policy

```c
robo_layout_overflow_resolve(&declared, &edges, &diag);
```

One door rather than two, and the contrast with 10.12 is the point: a *displacement*
is the same arithmetic for every kind of box, and so is an overflow — an atomic inline
and a block box read their declarations by the same rules — so there is nothing for a
second door to say.

The rules, in the order the function asks them:

```text
1. an axis the style declared a USABLE mode for takes that mode and is counted
   `axes_resolved` -- `visible` included;

2. an axis declared with a value this build could not use is used as `visible` and
   is counted `axes_unresolved`;

3. an axis nobody declared is `visible`, CSS's initial value, and is counted
   NOWHERE -- "the author wrote nothing" and "the author wrote `visible`" are two
   declarations and the counters are where the difference lives;

4. the extents start at zero -- this answers WHAT WAS DECLARED, and the pass that
   measures the contents fills them in.
```

The declared axis is read longhand-first: `overflow-x` / `overflow-y` each from its own
declaration, then the `overflow` shorthand for either axis a longhand said nothing
about, then `visible`.

This function cannot fail on a document: every axis has a stated answer.

## 6. The measurement

`robo_layout_overflow_measure` is the one place a box's own geometry and its
declarations meet. It runs at the point in a pass where the box's **padding box** is
known — the border box less its borders, floored at zero for the reason every
subtraction here is — and it does three things:

```text
1. read the declarations into modes      (robo_layout_overflow_of + _resolve)

2. place the padding box in the box's OWN space, corner at the origin, so the
   extents computed are measured against the box itself and no ancestor
   coordinate is involved

3. fold each PLACED CHILD's region into the record
```

### The accumulation rule, and why it is a maximum

```c
robo_layout_overflow_edges_include(&edges, padding_box, region, &diag);
```

The one accumulation rule, and it is a **per-side maximum** of how far `region`
reaches past `padding_box` — not a union, because the record is measured *from* the
padding box rather than being a rectangle of its own, and the two are equivalent only
when both operands share their reference. `padding_box` is the caller's box and does
not change; `region` is the thing being folded in; both are in the padding box's space.

It is saturating, so a box at the seam's maximum reached by a child at the seam's
maximum gets a maximum rather than a wrapped negative, and a `region` wholly inside
`padding_box` leaves every extent where it was.

## 7. Two rectangles, and the difference is the phase

```text
region        the padding box grown by the four extents -- what the contents
              occupy in the end, and what a parent includes when it measures its
              own overflow. On an axis whose mode CLIPS, the region is the
              padding box itself: a box that hides an axis keeps what it hides.

clip rect     the padding box, when at least one axis clips. It is the boundary a
              paint pass will clip to, and this phase stops at EXPOSING it --
              nothing here rasterises anything.
```

```c
robo_layout_overflow_edges_region(edges, padding_box, &out, &diag);
robo_layout_overflow_edges_clip_rect(edges, padding_box, &out, &diag);
```

`clip_rect` **refuses** with `ROBO_NOT_FOUND` when neither axis clips rather than
answering the padding box anyway: a box with no boundary has no clip rectangle, which
is a different statement from an empty one.

### The recursion, and the one identity that makes it work

A child contributes its **region** to its parent, so a child that clips contributes
only what it kept — while its own record still says how far its contents reached.

```text
child's record     says how far ITS contents reached
child's region     says what it KEEPS, after its own clipping
parent folds in    the region, not the record
```

Two consumers, two answers, one record. The alternative — fold the child's *contents*
into the parent and forget the child clipped — is how a clipped subtree comes to leak
into an ancestor's scrollable area, and `layout.overflow.pass_recursion_clipped`
asserts the difference.

## 8. Where it is measured

**Block pass.** Every block-level box that is placed is measured, and the pass:

- writes the record onto the object (`object->overflow`);
- sets the result's two flags, **per axis**, from a measured extent;
- counts `overflow_clipped`, `overflowing` and `overflow_unresolved`.

The last two counters are the interesting pair: a document can declare
`overflow: hidden` on every box and have nothing stick out of any of them, so "how many
boxes clip" and "how many boxes overflow" are two facts and neither implies the other.

**Inline pass.** Only an **atomic participant** is measured. A **text run** is not a box
at all, and a **transparent inline box**'s rectangle *is* the union of its contents — so
an overflow relative to it would be a statement about a rectangle that was already
derived from the things being measured. Both are declined, and the reason they are
declined is the reason their extents are still folded into their parent's: the parent's
measurement walks every placed child, and these two contribute through
`robo_layout_overflow_object_region`.

The participant's flags are set **before** its geometry is written, and that ordering is
the one place in the inline pass where it matters: `robo_layout_result_set_geometry`
does not touch the flag word, so the bits set here survive placement. A build that
reset them at placement would report no overflow for every atomic inline and would look
correct in every document whose boxes fit.

## 9. What the box does NOT do

**It does not grow.** `width: 100px; overflow: visible` with 200px of contents is
100px wide with an overflow of 100 on one side. The sizing layer is untouched, and the
suite asserts the *pair*, because a build that grew the box would look right in every
single-box test and be wrong in every layout that depends on the number.

**It does not collapse an axis into the pair.** `clips(axis)` is asked per axis and the
result's two flags are per axis, because a clipped axis and an unclipped one are
different boxes.

**It does not move anything.** A box that hides an axis keeps the geometry it was
given; what changes is which rectangle a paint pass would clip to, and this phase does
not paint.

**It does not resize a parent because a child overflowed.** The parent's own size is
what its declarations and its *in-flow* contents asked for; the child's region is folded
into the parent's overflow record and nowhere else.

### Deliberately absent

| Absent | Why |
|---|---|
| **Scrolling** — scroll offset, scrollport, scrollbar, `overflow: auto` | those are the next goals; `hidden` here is *clipping*, which is why a clip rect is exposed and no scrollable area is |
| **Axis promotion** | CSS promotes `overflow-x: visible` to `auto` when the other axis scrolls, because a box cannot scroll one axis and paint outside itself on the other. That rule exists *for scrolling*, and this build has none, so `visible` beside `hidden` stays `visible`. Asserted by `layout.overflow.pass_axis_split` so the phase that adds scrolling has a case to change |
| `text-overflow`, ellipsis, `overflow-clip-margin`, `contain` | a later phase's or a later value's |
| Raster clipping, display-list clipping, compositor clipping, hit testing | later phases; this one exposes the rectangles and stops |
| Float arithmetic | every number here is an integer pixel count, every subtraction is 64-bit and saturated |

## 10. Where it sits against the phases before it

```text
margin                  external spacing                (10.9)
  ↓
border                  part of the element's own box   (10.11)
  ↓
padding                 inside the border               (10.10)
  ↓
content                 what the box was given          (10.3-10.8)
  ↓
overflow                what the contents reached       (10.13)  <- recorded BESIDE the box
```

- **against margins**: a margin is external spacing and moves the box; an overflow is
  contents reaching outside a box that did not move. Adding the two never happens —
  `layout.overflow.pass_box_model` asserts the box-model numbers and the overflow
  record together.
- **against padding and borders**: both move the *boundary* the extents are measured
  from (the padding box), so a wider border shrinks the padding box and can turn a
  fitting child into an overflowing one without changing the child.
- **against positioning (10.12)**: a relatively positioned box is *displaced* but still
  contributes its **region** — which is its final position plus its extents — so a
  displaced child that leaves its parent's box contributes to the parent's overflow.
  `layout.overflow.pass_relative_child` is that case, and it is the phase boundary in
  one test: 10.12 says where a box is, 10.13 says what that does to the box around it.

## 11. What the suites prove

26 cases in `layout/tests/SuiteRoboLayoutOverflow.c` (`layout.overflow.*`) and 7 in
`css/tests/SuiteRoboCssOverflow.c` (`css.overflow.*`).

| Group | Cases | Asks |
|---|---|---|
| vocabulary | `kind_vocabulary`, `axis_vocabulary`, `axis_record`, `declared_record`, `edges_record`, `seam_declarations` | is every kind/axis walked, is a cleared record the zero of the vocabulary, and does the seam read a computed style the way §3 says? |
| policy | `resolve_modes`, `include_extents`, `include_saturates`, `region_per_axis`, `clip_rect_refusals` | do the four resolve rules hold, is the accumulation a per-side maximum, and does an unclipped box refuse a clip rect? |
| the pass | `pass_fitting`, `pass_child_right`, `pass_child_below`, `pass_size_unchanged`, `pass_hidden_clips`, `pass_axis_split`, `pass_recursion_clipped`, `pass_relative_child`, `pass_stats_and_unresolved`, `pass_box_model`, `pass_extreme_width` | does a real tree measure the way §6–§8 say, without growing anything? |
| the door | `object_door` | does the object door refuse an unplaced box, a text run and a transparent inline box, and answer for a placed one? |
| lifecycle | `relayout`, `determinism`, `allocator_balance` | does a second pass update the record, is the pass a pure function, and does the suite leak nothing? |

The four findings the suite produced are in `layout/docs/Tests.md` §18.

## 12. What is deferred, and to whom

| Deferred | Goal |
|---|---|
| scrolling: scrollport, scrollbars, scroll offset, `overflow: auto`/`scroll` | the next layout goals |
| axis promotion (`visible` → `auto` beside a scrolling axis) | the same goal, and `layout.overflow.pass_axis_split` is the case to change |
| raster clipping, display lists, clipping in a compositor | Phase 11 (RoboPaint) and the compositor goals |
| hit testing against an overflow region | the hit-testing goals |
| `text-overflow`, ellipsis | the text/CSS goals that own them |
| absolute/fixed/sticky positioning, and their containing blocks | 18.5 — `position` is read and declined there today |
| `box-sizing`, intrinsic sizing, percentage insets | 10.8's stated boundaries, untouched here |
