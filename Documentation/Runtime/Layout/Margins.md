# Margins (Phase 10.9)

What an author asks for *around* a box, and the arithmetic that turns those
declarations into the space the box actually occupies.

10.8 answered how **big** a box is. This phase answers how much **room it takes**,
which is a different number in exactly one place: a margin is outside the box. So
nothing in this phase is a `LayoutDimension` — a dimension describes a box's
content box, and a margin is the gap beside it — and nothing here changes a `size`.

Files: `Include/RoboLayout/RoboLayoutMargin.h`, `Source/RoboLayout/RoboLayoutMargin.c`.
Suite: `layout/tests/SuiteRoboLayoutMargin.c` (25 cases).

## 1. The one structural decision: a margin moves a box and grows the space beside it, and never changes its size

Three numbers move, and they are the three things a margin does:

```text
containing block's content origin
        │
        ├── margin-left / margin-top ──▶ where the box is PLACED
        │
        ├── left + right ──────────────▶ what is LEFT for the box's contents
        │                                 (and so for an `auto` width to fill)
        │
        └── top + height + bottom ─────▶ what the PARENT STACKS the next box below
```

and one number does not:

```text
the box's own size            the content box, unchanged by all four declarations
```

That asymmetry is why the block pass writes `margin_origin` and `size` into the
result separately, and it is asserted as a pair wherever geometry is asserted: a
build that added the margins to the `size` would move *one* of the two numbers and
would fail a case whichever half it got wrong.

The order inside the pass is forced rather than tidy:

```text
read the declared dimensions
        │
        ▼
resolve the MARGINS          ← they need the declared width (is there anything to distribute?)
        │
        ▼
resolve the WIDTH            ← an `auto` width fills what the margins left
        │
        ▼
walk the children INTO that width
        │
        ▼
resolve the HEIGHT           ← and the extent the parent gets back is the MARGIN box
```

An `auto` width and a `margin: 0 auto` box cannot both be settled at once: the
keywords answer different questions about the same leftover, and the one the phase
reads is the *declared* dimension rather than the resolution. That is why
`LayoutMarginsResolveBlock` takes the `LayoutDimension` and not a
number — "this box's width is `auto`" and "this box's width is 0" are different
documents and only the first has nothing to distribute.

## 2. Four kinds of margin, and why `ABSENT` is not `RESOLVED(0)`

```text
RoboLayoutMarginAbsent      nobody declared this side
RoboLayoutMarginResolved    a px length, SIGNED, usable here
RoboLayoutMarginAuto        the keyword, which distributes
RoboLayoutMarginUnresolved  declared, and not a value this build can use
```

CSS's initial value for every margin is `0`, so an absent side and a declared
`margin: 0` are **one used value and two facts**. The first cut of this phase folded
them together — with an argument that still reads well — and the counters killed it:

> *a box nothing declared a margin for and a box declaring `margin: 0` have the same
> used value, and a fourth kind would be a distinction no geometry can observe.*

The counters observe it. `stats.block.margins_applied` is "how many margins did this
document have", asked once per placed box, and a record that counted an undeclared
side as a used length answers **four** for every box in every document — including
one whose stylesheet declares no margin at all. So `ABSENT` is the zero of the
vocabulary (a cleared record is the initial state, which is what
`LayoutMarginsInit` writes), it is used as zero in geometry, and it is counted
nowhere. 10.8 reached the same split from the other side: its
`RoboLayoutDimensionSourceNone` is likewise "nobody declared" and likewise not a
length.

`RESOLVED` is **signed**, and that is the one respect in which this vocabulary
differs from 10.8's: `margin: -10px` is legal CSS and a real technique, while a
negative `width` is neither. The classification function both readers are shaped from
grew one parameter for it (`LayoutStyleClassify(..., allow_negative, ...)`),
which is the whole of the difference between the two seams.

## 3. The two policies, and why this is not one function with a flag

The same four declarations mean different things to a block box and to an
inline-level participant, and CSS says so explicitly:

| | block box in normal flow | atomic inline |
|---|---|---|
| `auto` horizontal | **distributes** the leftover beside a declared width | **zero** |
| `auto` vertical | **zero** (`auto` only means something for an absolutely positioned box, 10.12) | **zero** |
| a length | used as written, negative included | used as written |
| a percentage | resolves against the containing width (19.2); zero and counted when there is none | resolves against the containing width; zero and counted when there is none |

Two doors rather than one with a flag, for the reason 10.8 kept two dimension
resolvers: the difference *is* the two CSS rules, and a flag lets a caller pass the
wrong one and get a plausible answer. Both share one helper
(`layout_margins_use_non_auto`), so the parts that are the same rule cannot drift.

The distribution itself is four lines and one condition:

```text
leftover = containing - declared width - (the margins that are not auto)
```

split over the auto sides when the width is **definite**, and left at zero
otherwise. Two autos split it, and an odd pixel goes to `right` — CSS says the two
used values are equal, integers cannot always do that, and a build has to pick. The
choice is invisible in every case that does not look for it, which is why
`layout.margins.block_auto_odd` is a case that looks.

An **over-constrained** box (margins and width already exceeding the containing
block) keeps its autos at zero and overflows to the right. CSS adjusts `margin-right`
and calls the layout over-constrained; this build does not, because the number it
would write is one the style never asked for, and the overflow is 10.13's to report.

## 4. The used values, and the one number that is derived

`LayoutMarginEdges` is what the four declarations *came to*:

```text
top, right, bottom, left          signed px, the used values
available_content_width           containing width - left - right, FLOORED at zero
resolved, autos_used, unresolved  the counters
```

`available_content_width` is the number this phase contributes to 10.8's sizing
layer: it is what a box's `auto` width fills and what its children are resolved
against, and it is **floored rather than refused** because the alternative is a
negative available width, and size arithmetic that can go negative is size arithmetic
that can produce a rectangle nobody can draw. A *negative margin* is still honoured
in the edges themselves — `left` may be negative and the box then overhangs its
containing block, which is what CSS does — so there is exactly **one clamp, in one
place, on the one number a later arithmetic reads**.

The record is a *conclusion* rather than a request, which is why
`LayoutMarginEdgesEqual` compares every field including the counters: two
resolutions that agreed about the four numbers and disagreed about how many
declarations they could not use would be two answers to one document.

There is deliberately **no separate cursor, no margin struct on the walk and no
state between calls**. A margin is a value on the stack that becomes a field on the
object and a number in the result, and `layout.margins.allocator_balance` walks three
rounds of placement to assert that resolving one allocates nothing.

## 5. Where the margins live when the pass is over

Two places, and they carry different things:

- `LayoutResult` carries the **box**: its origin is the margin origin and,
  since 10.10, its size is the **border** box — the content box plus the padding — so
  a caller that reads one result has everything a paint pass needs to draw a
  rectangle.

  *10.10's outcome row:* this section was written when a box's content box **was** its
  border box, and it says "the content box" where the result's `size` is meant. That
  is now the border box, and the content box is that inset by `padding-left` and
  `padding-top`, read from the object rather than re-derived from a style
  (`Padding.md` §6). The margin arithmetic here is untouched: a padding changes the
  size these four numbers sit around and never moves where the box is.

  *10.11's outcome row:* one more inset inside the same number. The result's `size`
  is the **border** box, `content + padding + border`, and the content box is that
  inset by **two** terms read from two records — the border's `top`/`left` through
  `LayoutObjectBorders` and then the padding's through
  `LayoutObjectPadding` (`Borders.md` §6). The margin arithmetic here is still
  untouched, and the one number this document defines is unchanged: what a parent
  stacks by is the child's margin box, which now encloses a two-inset border box
  rather than a one-inset one.
- `LayoutObject::margins` carries the **used margins**, read back through
  `LayoutObjectMargins`. The result cannot carry them: it holds the border
  box by definition, and adding the four numbers back is exactly the arithmetic this
  phase exists to stop a caller from re-deriving. The door refuses a **text run** and
  a **transparent inline box** with `RoboNotFound` — not zero — because CSS applies
  no margins to either, and a confident zero would tell a caller an author's margin
  had been honoured and come to nothing.

  *10.12's outcome row:* one thing beside the numbers, and it is the phase's own:
  **where the box sits is no longer only a margin question.** The margin origin is
  still where flow puts the box, and a `position: relative` box is *drawn* from
  there displaced by its resolved offset while its margins keep every duty they
  had. The margin box the parent stacks by is computed from the **flow** geometry,
  so a displaced box's margins still carve its hole in the parent exactly as if it
  had not moved (`Positioning.md` §5).

  *10.13's outcome row:* nothing here, and the phase is the one that finally *reports*
  what this document has been promising since 10.9 — "the overflow is 10.13's". An
  over-constrained box still keeps its autos at zero rather than adjusting
  `margin-right`, and what 10.13 adds is that the overhang is no longer a fact nothing
  writes down: the box's extent past its padding box is in its overflow record, and a
  **negative** margin's overhang lands there too, because an extent is measured from
  the padding box and a margin is outside it (`Overflow.md` §4). The four used values
  this document defines are untouched — a margin still moves a box and never resizes one
  — which is the pair `layout.overflow.pass_box_model` asserts.

The parent's side of it is one number: `layout_block_box` reports
`top + height + bottom` as the extent the cursor advances by. The containing block
never looks at where a child's content box ended; it only asks where the next one may
start.

## 6. The one place this knowingly differs from the specification: no collapsing

Adjacent vertical margins **add**. A `margin-bottom: 20px` above a
`margin-top: 30px` leaves 50px here where CSS leaves 30, and a parent's top margin
does not merge with its first child's, and an empty box's own pair does not collapse
into one either.

That is stated in the header, asserted as the *sum* by
`layout.margins.block_no_collapse`, and it is the assertion a later goal changes
rather than deletes — a build that had lost the margin arithmetic by collapsing early
would otherwise look correct in every document whose margins happened to be equal.

The *shape* the phase leaves for it is the point: collapsing is a rule over the four
**used values**, not over the declarations, which is why
`LayoutMarginEdges` is a record of numbers and not another vocabulary. A
goal that implements it changes the two places the extent is computed and adds no new
kind.

## 7. What the suite asserts

Twenty-five cases in four groups, and the boundaries between them are the phase:

- **the vocabulary** — four kinds, their names, the predicates, the miss a
  `LayoutMarginLength` on a non-`RESOLVED` side is, and the cleared record as
  `ABSENT` against `LayoutMarginZero()`'s declared zero.
- **the record** — the seam over a fabricated computed style (a negative length, an
  `auto`, a percentage, a relative unit, a flagged-`unresolved` length, an undeclared
  side, and the half-away-from-zero rounding a signed length is the first to reach),
  and the edges record's own validity rules.
- **the two policies**, driven directly, so that a wrong distribution fails at the
  arithmetic rather than three boxes later in a coordinate: basic, four sides,
  negative, centring, an odd leftover, an `auto` under an `auto` width,
  over-constrained, unresolved, a vertical `auto`, and the inline policy's silence.
- **the pass** — a box's origin and size, the width its children are laid out into,
  the extent a parent stacks by, three stacked siblings, no collapsing, an atomic
  inline's advance and its bottom-margin baseline, the object door's three refusals,
  re-layout under a changed declaration and a changed viewport, determinism, and the
  allocator balance.

Every fixture declares `html` and `body` as **block**, and that is a fact about the
fixture rather than about margins: an element nothing declared for computes to
`display: inline` — 9.12's initial value, which is the point of the suite's style
source answering NULL — so a document that declared only the subject would put the
subject's ancestors in the inline flow and never reach the subject at all. The first
cut of this suite made exactly that mistake in four cases and reported margins "not
reaching geometry" while actually measuring a document it had not written.

## 8. What the phase changed in the suites that came before it

| Case | Was | Is |
|---|---|---|
| `layout.block.pipeline` | card `margin: 10px 20px` → placed at (0, 0) | → **(20, 10)** |
| `layout.conformance.pipeline` | card → (0, 0) | → **(20, 10)** |
| `layout.conformance.pipeline` | `<body>`/`<html>` height → 100 | → **120** |

The third is the phase seen one level up: a build that placed the card at its margin
origin and then handed its parent the *content* box would pass the first two and
report 100. Each of the three carries a note in place saying which phase changed it
and why — the same convention 10.8 used for the three assertions it moved.

Six other assertions moved and none was weakened: the version pin, the capability
table in two suites (`margins` is a seventeenth field, sixteen of seventeen
implemented), `RoboLayoutDomainCount` (17, with `margins` newest), the
`layout.module.allocator_balance` version string, and the conformance statement.

## 9. Re-layout, and why it needed no machinery

`layout.margins.relayout` changes a declaration and re-runs the pass, and changes the
**viewport** and re-runs it — the second is the case that says a *declared* margin is
not a function of the containing block, which is the difference between it and a
distributed `auto`. (A distributed `auto` does move; `layout.margins.auto_center_geometry`
reads one at another width.) The invalidation itself is 10.3's — a rebuild releases
the classification and every result with it — and this case drives it rather than
re-implementing it.

There is no margin cache and no invalidation flag, for the reason 10.8 gave: the pass
is a pure function of the tree, and a margin resolution is a function of a declaration
and one number.

## 10. What is deliberately missing

Each a named later goal:

- **margin collapsing** — §6, and the only *specified* behaviour this phase does not
  implement.
- ~~**percentage resolution**~~ **done in 19.2** — `margin: 5%` resolves against the
  containing block's *width* in CSS, for every side, and since 19.2 this layer does
  exactly that: the kind `PERCENTAGE` carries the magnitude and `LayoutMarginsUseNonAuto`
  (and the inline policy) resolves it against the `containingWidth` argument the pass
  hands over, counted as a resolved side. A percentage with no containing width is still
  `UNRESOLVED` and counted. `available_content_width` can still be *right* while a
  side's used value is *missing*, which the two counters say separately
  (`PercentageResolution.md`).
- ~~**padding**~~ **done in 10.10** (`Padding.md`). A box's margin box is still its
  border box plus these four numbers, and what changed is what the border box *is*:
  no longer its content box, but that plus the padding — derived by the padding layer
  from the sizing layer's answer rather than by this one adding a third term.
- **borders** (10.11), still absent, and the reason a box's border box today is a
  content box plus padding and nothing else.
- ~~**positioning** (10.12)~~ **done in 10.12**, where a vertical `auto` and the
  `top`/`left` insets get their meaning (a vertical `auto` still resolves to zero:
  `auto` means something only for an absolutely positioned box, 18.5).
  ~~overflow (10.13), which owns an over-constrained box's overflow and a negative
  margin's overhang~~ **done in 10.13** — an over-constrained box's overflow and a
  negative margin's overhang are now *recorded* rather than merely noted, though
  neither is *acted on* until something scrolls.
- `box-sizing`, grid/table margin distribution, and `margin: inherit` arriving
  as a *keyword* rather than as four computed longhands (the classification reads a
  keyword other than `auto` as unusable, which is the honest answer until the
  computed layer resolves it). **Flex** margin distribution was 19.3's: the flex pass
  resolves each item's four margins with the container's content width as the
  percentage base, and an `auto` there **absorbs** the free space on its own axis,
  ahead of `justify-content` and `align-items` — which is a flex rule rather than a
  margin rule, and the reason those margins are resolved by that pass and not here
  (`FlexLayout.md` §5).
