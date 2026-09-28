# RoboCSS and the Table Family (Phase 18.5)

This document is the authority for the fifth thing Phase 18 adds to RoboCSS: the
**table family**. It is a *property* family rather than a box model — every later
phase that lays a table out reads these values — and it is deliberately the first
family here whose members are mostly **inherited**.

The module is `Runtime/Source/RoboCSS/RoboCSSTable.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSTable.h`.

## 1. What the phase is, and the one fact worth reading twice

CSS gives a table box five properties that describe it before any column is
measured: how its widths are derived (`table-layout`), whether adjoining cell
borders are one or two (`border-collapse`), how far apart separate cells sit
(`border-spacing`), which edge the caption sits on (`caption-side`), and whether a
cell with no visible content still draws (`empty-cells`). `visibility` joins them
because table rows are what `visibility: collapse` is for.

The fact the phase turns on is inheritance, and it is the opposite of every other
layout family in this build:

| Property | Inherited |
|---|---|
| `table-layout` | **no** |
| `border-collapse` | yes |
| `border-spacing` | yes |
| `caption-side` | yes |
| `empty-cells` | yes |
| `visibility` | yes |

A table's cells *take* the collapse, the spacing, the caption side and the
empty-cell rule from the table box rather than deciding them for themselves. A
build that made one of these non-inherited would lay every cell out by the rule an
ancestor wrote, which is a whole-table mistake, so the inheritance of every row is
asserted in `css.table.registry`.

## 2. What the phase types

Five closed vocabularies, each a row per value, read through the same shared
closed-vocabulary body the display, position, background-repeat and border-style
shapes use:

```text
table-layout      auto | fixed
  └── content-driven   auto (widths come from the contents) | fixed (they do not)
border-collapse   separate | collapse
  └── borders adjoin   collapse
caption-side      top | bottom | block-start | block-end | inline-start | inline-end
  └── block axis       the two block-* rows
  └── inline axis      the two inline-* rows
empty-cells       show | hide
  └── draws empty      show
visibility        visible | hidden | collapse
  └── takes space      visible, hidden
  └── paints           visible
```

`caption-side` is the first vocabulary in this build whose members are **logical**:
`block-start` names the block-start edge of whatever writing mode is in force, so
the two axis predicates report the axis and the phase that has a writing mode
resolves the physical edge. The physical rows (`top`, `bottom`) are neither axis —
a different fact from "block", which is why the two questions are asked separately
rather than as one negation.

`visibility`'s two classifications are the pair that is easy to get wrong:
`hidden` **takes space** and does not paint, while `collapse` removes the box from
the layout for a table row. They are read as facts about the value, never as the
word an author typed.

## 3. `border-spacing` — carried, and why

`border-spacing` is deliberately **not** a vocabulary. Its grammar is
`<length>{1,2}` — one length for both axes or two for each — which is the shared
length grammar with a *pair* above it rather than a closed set of spellings, so the
declaration is registered as a carried declaration (`PRESERVED`) exactly as the
grid track templates are. That is a named boundary, not a gap: the phase that reads
it is table layout (19.6), which reads the preserved components directly rather than
asking this module to type a pair (§7). Until it landed the cascade and the computed
style were still correct — the author's declaration survives intact with its
inheritance and its initial value.

## 4. Non-goals

- **No table layout.** No column is measured, no width is distributed, no border
  is collapsed into another, and no `visibility: collapse` row is removed from the
  layout. A value here says *what rule* a table layout pass follows; where the
  cells land belongs to the phase that has a box, and that phase is 19.6 (§7).
- **No internal table display types.** `display: table`, `table-row`, `table-cell`
  and their relatives are **not** rows in this phase's display vocabulary. An
  internal table box is a layout fact of its own, and introducing it was table
  layout's job: 19.6 added the ten values, the third `CssDisplayOuter` kind and the
  `table` inner (§7). This phase is named for the properties and not for the boxes
  they describe.
- **No `border-spacing` pair grammar**, as §3.
- **No `vertical-align`.** A cell's vertical alignment is the shared alignment
  property every box has, and it belongs to the CSS-text goal (18.15) rather than
  to the table vocabulary.

## 5. Capabilities and version

Phase 18.5 adds one flag to `CssCapabilities` — `tables` — and one diagnostic
domain (`RoboCssDomainTable`), and takes the module to **0.23.0**. The flag claims
the five vocabularies and the carried `border-spacing`; it does not claim table
layout, the internal-box model or the spacing pair grammar.

Six registry rows join the table, and the flag is the module's own statement of
what is and is not here — the internal display types are the absence it names by
name rather than leaves to be discovered.

## 6. Tests

`Tests/Css/SuiteRoboCssTable.c` (`css.table.*`, 9 cases) is the contract:

- each of the five vocabularies walked in enumerator order, with every keyword read
  back, the case-insensitive match, the absent/unknown `FromText` answers and the
  `SliceIsKnown` door that tells a genuine value apart from the default;
- the classifications per row — content-driven, borders-adjoin, block/inline axis,
  draws-empty, takes-space, paints — and the safe answer for an unknown enumerator;
- the serialization discipline: a too-small buffer is `RoboNotSupported` with the
  length it needed and the buffer left empty;
- the five computed shapes through the real grammar, including a deferred `var()`
  and the two refusal sentences, and the carried `border-spacing`;
- the registry rows, their shapes and — the family's point — their inheritance;
- the capability flag, the version, the computation names and the diagnostic
  domain;
- determinism: the same declaration computes to the same value twice, and two
  logical caption sides are different values.

## 7. The layout half (Phase 19.6)

Nothing above changes to lay a table out, and the properties this phase typed are
read exactly where they were said to be read. 19.6 (`Documentation/Runtime/Layout/
TableLayout.md`) added the **ten display values** — `table`, `inline-table`,
`table-row-group`, `table-header-group`, `table-footer-group`, `table-row`,
`table-cell`, `table-column`, `table-column-group` and `table-caption` — as rows of
9.12's vocabulary, a **third outer kind** (`RoboCssDisplayOuterInternal`) because an
internal table box is placed by neither block nor inline flow, and a **sixth inner**
(`RoboCssDisplayInnerTable`) for the pair that establishes the context. It reads
`table-layout`, `border-collapse` and `caption-side` as the computed values 18.5
types, and reads `border-spacing` as the preserved pair §3 describes — one length for
both gutters or two, in CSS's own order — so this module types no pair and the
carried declaration arrives byte for byte.

The one thing that moved rather than joined is `RoboCssDisplayTextMax`: 16 was the
longest keyword's length plus a NUL, and `table-column-group` is nineteen characters,
so the bound is **24** and the display suite checks it against every row.
