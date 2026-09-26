# RoboCSS and the Multi-column Family (Phase 18.6)

This document is the authority for the sixth thing Phase 18 adds to RoboCSS: the
**multi-column family**. A multi-column container is a *fragmentation* container —
a block that splits its content into a sequence of columns — and every later phase
that fragments content for it reads these values.

The module is `Runtime/Source/RoboCSS/RoboCSSColumn.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSColumn.h`.

## 1. What the phase is

Two properties say how a column box *behaves* and live in the module as closed sets:

```text
column-span   none | all
  └── spans all   all (the box leaves the column flow)
column-fill   auto | balance
  └── balances    balance
```

`column-span: all` is the fact that takes a box *out* of the column flow; it is a
different kind of participant, not a wider one. `column-fill` says whether the
columns are filled to a balanced height or one after another — a different picture.
`column-fill`'s initial value is `balance`, which is **not** the zero of its
enumerator; the shape carries the keyword, so the registry writes `balance` down and
there is no cleared-record hazard at all.

## 2. The family is assembled from shapes that already exist

The interesting fact about the phase is how little of it is new:

| Property | Shape |
|---|---|
| `column-count` | `auto`, or a **positive** whole number — a shape of its own |
| `column-width` | the shared length grammar with the keyword `auto` |
| `column-span` | a new closed set (`none` \| `all`) |
| `column-fill` | a new closed set (`auto` \| `balance`) |
| `column-rule-width` | the **border** family's line width (`thin` \| `medium` \| `thick` \| a length) |
| `column-rule-style` | the **border** family's border style |
| `column-rule-color` | the **border** family's colour shape (`currentColor` included) |
| `column-rule`, `columns` | carried declarations (shorthands) |
| `column-gap` | already a row — it is the same gutter `row-gap` is |

So a column rule is **not** a second border family: `column-rule-style: banana` is
refused by the border style table because it **is** the border style table, and
`column-rule-color: currentColor` resolves the same way a border colour does.
`column-width: 1in` and `width: 1in` are normalised by one body. Only
`column-count` adds arithmetic — its bound (at least one) is why it is a shape of
its own: zero columns is not a smaller count, it is not a count.

## 3. Non-goals

- **No fragmentation layout.** No content is split into columns, no column is filled
  to its height, no height is balanced, and no number of columns is derived from a
  width and a container. A value here says *what rule* a fragmentation pass follows;
  where the columns land belongs to the phase that has a box.
- **No `break-*` / `orphans` / `widows`.** Those are fragmentation *controls* that
  belong to the same later layout phase, and they are not registered here.
- **No new border family.** §2: the rule decoration reuses the border shapes rather
  than growing a second copy of a style, a width or a colour.
- **No expanded shorthand.** `column-rule` and `columns` are carried, as every
  shorthand in this build is.

## 4. Capabilities and version

Phase 18.6 adds one flag to `CssCapabilities` — `multiColumn` — and one diagnostic
domain (`RoboCssDomainColumn`), and takes the module to **0.24.0**. The flag claims
the count, width, span and fill values and the rule decoration reused from the border
family; it does not claim fragmentation layout.

Nine registry rows join the table, none inherited: a container's column count, width,
span, fill and rule decoration are the box's own, and a child that inherited a column
count would split *itself* into that many columns.

## 5. Tests

`Tests/Css/SuiteRoboCssColumn.c` (`css.column.*`, 6 cases) is the contract:

- the two closed vocabularies walked in enumerator order, with every keyword read
  back, the case-insensitive match, the absent/unknown `FromText` answers and the
  `SliceIsKnown` door;
- the classifications (`spans all`, `balances`) and the safe answer for an unknown
  enumerator;
- the count shape: `auto`, a positive integer, and the three refusals (zero, a
  negative and a fraction, and a word that is not `auto`);
- the width shape through the shared length grammar, with `auto` as its keyword;
- the two vocabularies through the real grammar, and the border style reuse: a value
  the border family refuses is refused here;
- the registry rows, their shapes, their non-inheritance and the two shorthands;
- the capability flag, the version, the computation names and the diagnostic domain;
- determinism.
