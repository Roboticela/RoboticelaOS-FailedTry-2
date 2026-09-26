# RoboCSS and the Grid Family (Phase 18.4)

This document is the authority for the fourth thing Phase 18 adds to RoboCSS: the
**grid family**. It is deliberately half a family, and saying which half is the point
of the phase — a grid container places its items into *tracks* it computes from a
template, and a track list is a grammar of its own, so this phase types the two
properties whose values are simple enough to read today and *carries* the rest.

The module is `Runtime/Source/RoboCSS/RoboCSSGrid.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSGrid.h`. It owns one closed set, its classifications
and the reader that is the reason the set is not a plain table:

```text
grid-auto-flow        row | column | row dense | column dense
                      (read from the component value, because three of the four
                       spellings are two words)
row-gap / column-gap  normal | <length-percentage>      -- the shared length grammar
gap                   shorthand                       ---- carried, not expanded
grid-template-columns ┐
grid-template-rows    │  track templates
grid-template-areas   │
grid-auto-columns     │
grid-auto-rows        ┘
grid-row-start/end    ┐
grid-column-start/end │  line placements
grid-row / grid-column│
grid-area             ┘
grid / grid-template  shorthands                      ---- carried, not expanded
```

## 1. `grid-auto-flow` is not one identifier

Every closed vocabulary before this one had rows that are a single identifier —
`row`, `no-repeat`, `inline-block` — and so its reader could take a *slice* and match
it against the table. `grid-auto-flow` breaks that: CSS writes it as
`[ row | column ] || dense`, so

- `row dense`, `dense row` and `dense` are **one** value (`row dense`), and
- `column dense`, `dense column` are one value (`column dense`).

A single-identifier table could not express that. It would either accept only
`row`/`column` and lose the density — the one thing density changes, since a dense
container may place a later item *before* an earlier one — or it would add a fifth row
for a spelling (`dense`) that is not a distinct value. So the reader works on the
whole component value, the way `background-position` reads its pair:

```c
CssGridParse CssGridAutoFlowFromValue(const CssValue *specified,
                                      CssGridAutoFlow *out);
```

It collects the significant components (two is the most a `grid-auto-flow` can hold),
classifies each as an axis or a density, refuses a repeated half (`row column`), and
reports the value through `CssGridAutoFlowFromParts(column, dense)` — so the two
*facts* a layout pass dispatches on are what the reader read, not a rebuilt string.

The vocabulary's rows carry those two facts as columns, and the doors are
`CssGridAutoFlowIsColumn` (auto-placement follows columns) and
`CssGridAutoFlowIsDense` (holes are backfilled). An unknown value answers 0 to both,
which is the safe direction: a value this build cannot read must not claim
auto-placement follows columns.

The single-identifier door still exists — `CssGridAutoFlowSliceIsKnown` — for the
one-word spellings `row`, `column` and `dense`, and `CssGridAutoFlowFromSlice`
answers `row dense` for `dense` and the initial `row` for anything it does not know.

## 2. The gutters are a length family, not a shape

`row-gap` and `column-gap` (and their legacy `grid-*-gap` aliases) take
`normal | <length-percentage>`. That is not a shape of its own: a gutter *is* a
length, so they join the one length grammar `flex-basis` and the box edges already
share, with `normal` as their own keyword. Reusing the body is what keeps
`row-gap: 1in` and `margin-top: 1in` normalised by one implementation.

`normal` is a keyword and **not zero**. For a grid container it is the user agent's
ordinary gutter, which is a different statement from a gutter of no pixels; a build
that resolved it to `0px` would lay every authored grid out with no gaps at all.
It is carried as the keyword it is, exactly as `medium` is for a border width.

## 3. What is carried, and why

Everything else in the family takes a grammar this phase does not read:

- the **track templates** (`grid-template-columns`, `grid-template-rows`,
  `grid-auto-columns`, `grid-auto-rows`, `grid-template-areas`), whose value is a
  track list — a size, a `minmax()`, a repeat count, a line name;
- the **line placements** (`grid-row-start`/`-end`, `grid-column-start`/`-end`),
  whose value is a number, a `span`, or a custom ident;
- the **shorthands** `grid`, `grid-template`, `grid-row`, `grid-column`, `grid-area`
  and `gap`.

All of them are registered as **carried declarations** (`PRESERVE`) with their real
initial values (`none`, `auto`, `normal`). A `PRESERVE` row is not a placeholder: it
is the author's declaration, structurally intact, with the registry supplying its
inheritance and its initial value, so the cascade and the computed style stay correct
until the track grammar lands. That is the same boundary `flex-flow` and `flex` are
drawn at, and — as there — **no shorthand is expanded by this build**.

## 4. Non-goals

- **No grid layout.** No track is measured, no item is placed, no line number is
  resolved, and no implicit track is created. A value here says *what rule* a layout
  pass follows; where the tracks and items land belongs to the phase that has a box.
- **No track grammar.** `minmax()`, `repeat()`, `subgrid`, named lines, `span` and
  the `fr` unit are carried as tokens, not read. That is the half of the family this
  phase names rather than half-implements.
- **No grid alignment properties of their own.** `align-items`, `align-self`,
  `align-content` and `justify-content` already exist from 18.3 and are legal on a
  grid container unchanged; `justify-items`, `justify-self` and the `place-*`
  shorthands are **not added** here, and will be a row of their own rather than a
  second alignment vocabulary.
- **No subgrid, no masonry, no grid-auto-flow interaction with placement order.**
  Flow and density are values with classifications; what they do to a placed item is
  layout.

## 5. Capabilities and version

Phase 18.4 adds one flag to `CssCapabilities` — `grid` — and one diagnostic domain
(`RoboCssDomainGrid`), and takes the module to **0.22.0**. The flag claims the flow
vocabulary and the gutter length family and states that the remaining grid
properties are carried; it does not claim the track grammar or grid layout.

Twenty registry rows join the table, none of them inherited: a grid container's flow,
its template, its line placements and its gutters are the box's own, and a child that
inherited a template would place itself into its parent's grid.

## 6. Tests

`Tests/Css/SuiteRoboCssGrid.c` (`css.grid.*`, 12 cases) is the contract:

- the flow vocabulary walked in enumerator order, with every keyword read back, the
  `dense` → `row dense` canonicalisation, and the identifier door telling `row` from
  `banana`;
- the reader over real declaration text: `row`, `column`, `dense`, both orders of the
  two-word spellings, a deferred `var()`, and the refusals — a repeated half, an
  unknown word, a number, three components;
- serialization and the buffer discipline — a too-small buffer is `RoboNotSupported`
  with the length it needed and the buffer left empty;
- the two shape columns (`IsColumn`, `IsDense`) per row and the safe answer for an
  unknown value;
- the computed values over real declaration text — `grid-auto-flow` to its canonical
  spelling, the gutter family over the shared length grammar, and the carried
  declarations as `PRESERVED` rather than `INVALID`;
- the twenty registry rows (shape, non-inheritance, initial value, the six
  shorthands flagged) read from `CssPropertyRegistryCreateStandard`;
- the capability, the version, the kind name and the diagnostic domain; and
- determinism, by structural equality of two computations of one declaration.

`TestCss.ps1 -Target Host` is the gate (676 cases with this suite), and a grid change
is not done until `TestLayout`, `TestPaint`, `TestHtml` and `TestDesktop`
(`-Target Host`) also pass — the layout side will read these values, so a change here
is a change to what it reads even though nothing in this phase lays a grid out.
