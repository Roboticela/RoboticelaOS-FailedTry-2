# Percentage resolution (Phase 19.2)

A `<percentage>` is the one CSS size that is meaningless on its own. `width: 50%` is
half of *what*, `margin: 5%` is five percent of *which* number, and `left: 25%` is a
quarter of an extent the declaration does not name. Before this phase every one of
those declarations was read, recorded `UNRESOLVED` and used as **zero** — 10.8's rule,
inherited unchanged by 10.9, 10.10, 10.11 and 10.12, each of which wrote in its own
header "no goal resolves a percentage yet".

This is the goal that resolves them. It adds **no new pass** and reads **no new
property**: what it adds is a fifth kind to four vocabularies, a magnitude field
beside each existing length, and the **containing-block base** threaded to the one
place each layer already resolved a number.

Files: `RoboLayoutSizing.h/.c` (the dimension), `RoboLayoutMargin.h/.c`,
`RoboLayoutPadding.h/.c` and `RoboLayoutPosition.h/.c` (the three other
vocabularies); `RoboLayoutInternal.h` (`LAYOUT_STYLE_VALUE_PERCENT`, the seam's fifth
answer); `RoboLayoutBlock.c` and `RoboLayoutInline.c` (the bases).
Suite: `Tests/Layout/SuiteRoboLayoutSizing.c` (`layout.percentage.*`, six cases) and
`Tests/Layout/SuiteRoboLayoutPosition.c` (`layout.positioning.percentage`).
Version: `0.19.0`; capability: `LayoutCapabilities.percentageResolution`.

## 1. What a percentage is, and why the base is the whole phase

A percentage is a *ratio to the containing block*, and which dimension of the
containing block it is a ratio of is **not uniform**. CSS states two rules, and the
phase is the two of them plus the plumbing that makes them askable:

```
   width / height        →  the containing block's content width / height
   margin / padding      →  the containing block's content WIDTH, for every side
   left / right          →  the containing block's width
   top / bottom          →  the containing block's HEIGHT
```

The margin/padding asymmetry is CSS's and is deliberately kept: `margin-top: 10%`
is ten percent of the containing block's **width**, not its height. A build that
resolved a vertical margin against the height would look right in every document
whose containing block happened to be square and wrong in most others, which is
exactly why the rule is stated here and asserted in the suite.

Two consequences follow, and they are the design:

- **A percentage keeps its magnitude.** It is not folded into a number at the seam,
  because at the seam there is no base. `LayoutMarginPercentage(5.0)` carries `5.0`,
  and the policy that is handed the containing block does the arithmetic. This is
  why the phase added a *kind* to each vocabulary rather than resolving eagerly: the
  same resolved number can be the answer to a different question depending on who
  asks, and the difference survives so a diagnostic can tell "the author named a
  length" from "the author named a ratio".
- **A percentage with no base degrades to `UNRESOLVED`.** There is no invented
  default. A block box's height is its contents', so a `height: 50%` inside one has
  nothing to resolve against — and it stays `UNRESOLVED`, used as zero and counted,
  exactly as it did before the phase. The degradation is *stated*, not silent.

## 2. Four vocabularies, one shape

Each layer's record gained a `PERCENTAGE` kind and a `double percentage` field
beside its existing length, and each gained the same three doors:

| Layer | Kind | Field | Doors |
|---|---|---|---|
| Dimension | `RoboLayoutDimensionPercentage = 4` | `LayoutDimension.percentage` | `LayoutDimensionPercentage`, `IsPercentage`, `PercentageOf` |
| Margin | `RoboLayoutMarginPercentage = 4` | `LayoutMargin.percentage` | `LayoutMarginPercentage`, `IsPercentage`, `PercentageOf` |
| Padding | `RoboLayoutPaddingPercentage = 3` | `LayoutPadding.percentage` | `LayoutPaddingPercentage`, `IsPercentage`, `PercentageOf` |
| Inset | `RoboLayoutInsetPercentage = 4` | `LayoutInset.percentage` | `LayoutInsetPercentage`, `IsPercentage`, `PercentageOf` |

The kind numbers are appended after the ones 10.8–10.12 defined, so no earlier
enumerator moved and every count grew by one
(`LayoutDimensionKindCount` 4→5, `RoboLayoutDimensionSourceCount` 5→6,
`RoboLayoutMarginKindCount` 4→5, `RoboLayoutPaddingKindCount` 3→4,
`RoboLayoutInsetKindCount` 4→5).

A percentage is **signed where the length is signed** and **non-negative where it is
not**, which is the grammar's own rule kept rather than a new one: a negative
`margin`, `inset` or dimension percentage is legal CSS and pulls, a negative
`padding` percentage is not and reads as `UNRESOLVED` — the same answer a negative
padding length gets. Each `IsValid` bounds the magnitude by
`LAYOUT_STYLE_PERCENT_MAX` with a pair of comparisons rather than an `isfinite`,
because this is a freestanding build and `<math.h>` is not available.

## 3. The seam: a fifth answer, not a second reader

`LayoutStyleClassify` — the one place a computed style becomes a layer's vocabulary —
gained one answer:

```
LAYOUT_STYLE_VALUE_PERCENT = 4
```

It is filled when the computed value is a `RoboCssComputedValuePercentage` that is
not flagged unresolved, and its magnitude is the value's number. Every reader below
it (`LayoutSizingReadDimension`, `LayoutMarginsOf`, `LayoutPaddingsOf`,
`LayoutInsetOfProperty`) gained one `case` that turns that answer into its own
`…Percentage(number)` record — a **fifth answer** rather than a re-interpretation of
`UNRESOLVED`, because "a percentage this build can resolve with a base" and "a value
it cannot use at all" are two different facts and only the first has a base worth
asking for.

The dimension reader's `width`/`height` case is the one asymmetry: a **negative**
percentage dimension is not a size, so it reads as `UNRESOLVED`, the same answer a
negative px length gets and for the same reason (`LayoutDimensionIsValid` refuses
it).

## 4. Resolution: where the base arrives

Resolution is per layer, and each layer got the base the way its existing code could
take it.

**Dimensions** — `LayoutSizing` gained
`percentageBaseWidth`/`percentageBaseHeight` (each with a `has…` flag, because an
indefinite base is a real state and a zero base is a real measurement). The
resolution branches:

```
PERCENTAGE and hasPercentageBaseWidth   LayoutPxRound(pct / 100 * base), source PERCENTAGE
PERCENTAGE and no base                  the AUTO branch (degrade)
```

A *flag beside the number* rather than a sentinel, for the reason 10.8's records give
everywhere: a zeroed base is a legitimate measurement whose zero must not be read as
an absence.

**Margins and padding** — the policy is handed the containing block's content
**width** on every side (`LayoutMarginsUseNonAuto` gained a `containingWidth`
argument and `LayoutPaddingResolve` one too), and a percentage resolves as
`LayoutPxRound(pct / 100 * width)`, counted as a resolved side. A margin keeps its
sign through the arithmetic, so `margin: -5%` pulls.

**Insets** — `LayoutPositionResolveAgainst` is a new door beside
`LayoutPositionResolve`, carrying the containing block's width **and** height with a
presence flag each. `left`/`right` resolve against the width, `top`/`bottom` against
the height, and a block-axis percentage with no height base degrades to `UNRESOLVED`
while the inline one, which *has* its base, still resolves. `LayoutPositionResolve`
is kept and delegates with both flags clear, so every caller that had no base — and
every pre-19.2 expectation — behaves exactly as before.

## 5. Threading the base: one ordering subtlety

The bases come from the pass, and the pass already computed them:

- **Block**: `LayoutBlockBox` hands `availableWidth` — the containing block's
  *content* width, the number its own margins and width already resolve against — as
  the width base, and `percentHeightBase`/`hasPercentHeightBase` as the height base.
  A child's percentage-height base is the parent's **declared** (or
  resolved-percentage) height, computed **before** the children are laid out: a
  `height: 50%` child must resolve against the height its parent *asked for*, not
  against the height the parent grew to once that child was placed. The ordering is
  forced rather than tidy, and the phase's first draft got it wrong in the direction
  that has no symptom until a nested percentage is written.
- **Inline**: an atomic inline resolves a percentage inset against the line's
  available width and has no height base, so a block-axis inset percentage degrades —
  the same degradation a text run has no way to escape.
- **Root and out-of-flow**: the root box's height base is the viewport; an
  out-of-flow box's is the containing block its placement was built against.

## 6. What is deliberately absent

Each a named later row, stated at the capability's own comment so a reader meets it
where they would assume the feature:

- **Out-of-flow inset percentages.** 14.0's `RoboLayoutAbsolute*` has its own stated
  contract — a percentage there is read, recorded `UNRESOLVED` and used as zero — and
  that is left **untouched**. A percentage inset on an `absolute`/`fixed` box stays
  its own later change rather than a silent widening of a completed phase; the kind
  this phase added is one 14.0's reader already classifies as unusable, so nothing
  there moved.
- **Grid tracks.** Percentages in a grid track are 19.4's arithmetic, not this
  layer's. A percentage `flex-basis` was 19.3's and is now resolved by the flex pass
  against the container's main size when that is definite (`FlexLayout.md`); a
  percentage basis against an indefinite main size degrades to the content size.
- **Percentage `min-width`/`max-width` bounds.** The bounds layer records a min/max
  as a length or an intrinsic; a percentage bound is the constraints goal's.
- **`calc()` and viewport units.** A declared `calc(50% + 10px)` and a `vh`/`vw`
  arrive as an unusable value and stay `UNRESOLVED`.
- **A percentage on a transparent inline box.** A transparent box takes none of the
  three insets by 10.9–10.11's own rule, so there is nothing here to resolve.

`percentage-resolution` would be claiming those if it were named for "percentages are
finished". It is not.

## 7. What the suite asserts

**`layout.percentage.record`** — the dimension's new kind, the magnitude kept, the
doors, and a magnitude outside `LAYOUT_STYLE_PERCENT_MAX` refused while a negative
one stays valid.

**`width_nested`** — `50%` of `50%` of the viewport is a quarter of it, so the base
is read from the *resolved* parent, not from a second copy of the declaration.

**`height_root`**, **`height_child`**, **`height_indefinite`**, **`height_nested`** —
a percentage height against the viewport, against a parent's declared height, the
degradation when the containing height is indefinite, and two nested percentage
heights. Every expectation is read from the base the fixture built, never from a
literal the fixture happened to produce.

**`layout.positioning.percentage`** — the inset's two bases, one case per fact:
`left: 10%` of a 200px containing block is 20, `top: 25%` of a 400px one is 100 (the
*height* base, the half a swapped-bases build gets wrong), `right: 50%` displaces
leftward, a negative percentage pulls, no base counts `insetsUnresolved`, an
indefinite height degrades the block axis while the inline one still resolves,
precedence and the counters are unchanged, and a non-relative box *declines* a
percentage rather than resolving it.

Plus `layout.sizing.block_percentage_width` — the renamed seam case, now asserting
`width: 50%` of a 300px block is **150** where before it asserted the declaration was
`UNRESOLVED`.

## 8. What the phase changed in the suites that came before it

The module's self-description moved, and so did the vocabulary counts:

| Assertion | Was | Is |
|---|---|---|
| `layout.module.version` + `layout.conformance.statement` | `0.18.0` | **`0.19.0`** |
| the capability table (two suites) | 25 rows, `painting` last | **26 rows**, `percentage_resolution` before `painting` |
| `layout.conformance.capabilities` implemented count | 24 of 25 | **25 of 26** |
| `RoboLayoutDimensionKindCount` / `…SourceCount` | 4 / 5 | **5 / 6** |
| `RoboLayoutMarginKindCount` / `…PaddingKindCount` / `…InsetKindCount` | 4 / 3 / 4 | **5 / 4 / 5** |
| `layout.sizing.block_unresolved_width` | a percentage is `UNRESOLVED` | renamed **`block_percentage_width`**: it resolves to 150 |

Plus the margin, padding and position seams, each of which used to assert "a
percentage is `UNRESOLVED`" and now asserts the kind and the magnitude. The margin
seam's declared fixture had to be corrected to the value its own comment named —
one case, and it is recorded because a fixture that disagrees with its comment is
how a case reports the wrong thing as a pass.

## 9. Boundaries

The dependency direction is unchanged, and the phase *reduced* what a later layer
has to guess rather than adding a new edge. The bases are numbers the pass already
had; the arithmetic is in the layer that owns the property; and `RoboLayoutPosition`
still resolves a mode and four insets without knowing what a containing block *is*.
The module's `-Verify` scan is unchanged: a percentage is resolved from records this
module already reads, and `<math.h>`, RoboFont and RoboText are as absent here as
they were before the phase.
