# RoboCSS and the Flexbox Family (Phase 18.3)

This document is the authority for the third thing Phase 18 adds to RoboCSS: the
**flexbox family**. It is the first family in this build whose values are more than
one vocabulary, and the first whose properties only exist because a *layout* phase
will read them.

The module is `Runtime/Source/RoboCSS/RoboCSSFlex.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSFlex.h`. It owns three closed sets and the
classifications a layout pass dispatches on, and nothing else:

```text
flex-direction        row | row-reverse | column | column-reverse
flex-wrap             nowrap | wrap | wrap-reverse
justify-content   ┐
align-items       │   one alignment vocabulary, four value shapes
align-self        │
align-content     ┘
flex-grow / flex-shrink   <number>, and it must not be negative
flex-basis                <length-percentage> | auto | content | …
order                     <integer>, and it may be negative
flex-flow / flex          shorthands ---- carried, not expanded
```

## 1. The three vocabularies

Each is a table, and each row carries the facts a consumer asks rather than the
spelling it was written as — the same decision `display` (9.12), `position`
(9.13), `overflow` (10.13) and `border-style` (9.15) each record.

| Vocabulary | Values | Classifications |
|---|---|---|
| `CssFlexDirection` | `row`, `row-reverse`, `column`, `column-reverse` | `IsRow` / `IsColumn` (which physical axis is main), `IsReversed` (the start and end edges are swapped) |
| `CssFlexWrap` | `nowrap`, `wrap`, `wrap-reverse` | `IsWrapped`, `IsReversed` |
| `CssFlexAlign` | sixteen values (below) | `AllowedFor(role)`, `IsDistributed`, `IsBaselinePosition`, `IsAuto`, `IsNormal` |

A direction's two facts are independent columns and not an enumerator's name:
`row-reverse` shares `row`'s main axis and not its edges, so a comparison on either
fact alone would make two spellings one value. `wrap-reverse` does the same for the
cross axis.

As with every closed vocabulary here, ROW / NOWRAP / NORMAL are both a value and
the answer for "not one", so the *identifier* is what a caller asks when it needs
to tell `flex-direction: row` from `flex-direction: banana`:
`CssFlexDirectionSliceIsKnown` is that door, and `…FromSlice` alone is enough
once the identifier is known to be one of the rows.

## 2. The alignment vocabulary, and the four shapes over it

`CssFlexAlign` is **one** enumerator set — `normal`, `stretch`, `flex-start`,
`flex-end`, `center`, `space-between`, `space-around`, `space-evenly`, `baseline`,
`auto`, `start`, `end`, `self-start`, `self-end`, `left`, `right` — because CSS Box
Alignment defines `justify-content`, `align-items`, `align-self` and `align-content`
as *subsets of one set of values*. The subsets are not nested, and that is the whole
reason the shape is four shapes rather than one:

| value | justify | items | self | content |
|---|:---:|:---:|:---:|:---:|
| `normal`, `stretch`, `flex-start`, `flex-end`, `center`, `start`, `end` | ✓ | ✓ | ✓ | ✓ |
| `space-between`, `space-around`, `space-evenly` | ✓ | | | ✓ |
| `baseline` | | ✓ | ✓ | ✓ |
| `auto` | | | ✓ | |
| `self-start`, `self-end` | | ✓ | ✓ | |
| `left`, `right` | ✓ | | | |

So the registry has four computation shapes (`JustifyContent`, `AlignItems`,
`AlignSelf`, `AlignContent`) over one vocabulary, and each shape's *predicate* asks
`CssFlexAlignAllowedFor(value, role)` — the columns of the rows above, not a
comparison written at a call site. That is what makes `align-items: space-between`
a reported mistake while `justify-content: space-between` is a value, and
`justify-content: baseline` is the reverse. It is the computation layer's job
because the shape is the last place that knows which property was written.

The classifications beside the enumerator are for a layout pass: `IsDistributed`
(the three `space-*` values), `IsBaselinePosition`, `IsAuto` and `IsNormal`.

## 3. The numeric and length shapes

Three properties are not vocabularies at all:

- **`flex-grow`, `flex-shrink`** share one shape (`FlexFactor`): a `<number>` that
  must be **non-negative**. It is a shape of its own rather than the `NUMBER` family
  because of the bound — there is no upper limit, so a huge factor is legal CSS, but
  a negative one is a refusal rather than a clamp (unlike `opacity`, which CSS
  clamps). `CssFlexFactorIsValid` holds the rule in one place.
- **`order`** is an `ORDER` shape: a signed `<integer>`. A negative order is the
  property's whole point. `1.5` and `1.0` are both refused — `<integer>` is a
  grammar, not a value that happens to be whole — exactly as `z-index`'s shape does.
- **`flex-basis`** is a length family in the *shared* length grammar
  (`RoboCSSComputedValue.c`), not a shape of its own: a length, a percentage, or one
  of `auto`, `content`, `min-content`, `max-content`. Reusing the body is what keeps
  `flex-basis: 1in` and `width: 1in` normalised by one implementation. Its keyword
  list is its own, which is exactly the parameter the grammar carries.

A `calc()` or a `var()` in any of them is carried `PRESERVED` with the unresolved
flag, as everywhere else in this build: legal CSS this layer does not evaluate.

## 4. The two shorthands, and why they are carried

`flex-flow` is `<flex-direction> || <flex-wrap>` and `flex` is
`none | <flex-grow> <flex-shrink>? || <flex-basis>`. Each stands for two halves of
**different kinds**, so neither can take one closed vocabulary the way `overflow`
took its own in 10.13 — there is no single value a `flex-flow` could be typed as.

Both are therefore registered as **carried declarations** (`PRESERVE`), and one
boundary is stated rather than crossed:

- **No shorthand is expanded by this build.** `flex-flow: column wrap` reaches a
  consumer as the tokens the author wrote; a container's direction is read from
  `flex-direction` and its wrap from `flex-wrap`. A document that declares only the
  shorthand is, for this build, a declaration nothing downstream reads — which is the
  same boundary `margin`, `background` and `border` have had since their own phases,
  stated here because flexbox is the first family where the shorthand's two halves
  are *different kinds of value* rather than one.

Everything past that is deliberate non-goals rather than unfinished work:

- **No flex layout.** No line is measured, no free space is distributed, no
  main-axis or cross-axis position is computed, and no item is sized. A value here
  says *what rule* a layout pass follows; where the items land belongs to the phase
  that has a box — exactly as `position: sticky` is a value and not a scrolling
  behaviour.
- **No CSS Grid/Box-Alignment properties beyond the four.** `align-content`'s
  distribution has no `place-items`/`justify-items` sibling here.
- **No `wrap-reverse` behaviour and no baseline computation.** Both are values with
  classifications; what a reversed line order does and where a baseline is are layout
  questions.

## 5. Capabilities and version

Phase 18.3 adds one flag to `CssCapabilities` — `flexbox` — and one diagnostic
domain (`RoboCssDomainFlex`), and takes the module to **0.21.0**. The flag is
asserted field by field in `SuiteRoboCssConformance.c` and read again in
`SuiteRoboCssFlex.c`. The flag claims the *values* and their classifications; it
does not claim flex layout, which is not built.

The eleven registry rows are all non-inherited — a container's direction, a wrap
rule, an alignment, a factor, a basis and an order are the box's own, and a child
that inherited one would lay itself out by its parent's rule. Two initial values are
not the zero of their vocabulary: `align-self` starts at `auto` (not the `normal`
its siblings start at) and `flex-shrink` at `1` (not `flex-grow`'s `0`), so a
cleared record would hold the wrong answer for both — which is why the registry
writes them down.

## 6. Tests

`Tests/Css/SuiteRoboCssFlex.c` (`css.flex.*`, 14 cases) is the contract:

- the three vocabularies walked in enumerator order, with every keyword read back
  from the same row, the two classification columns asserted per row, and the
  unknown-value / unknown-role answers;
- parsing by identifier — case-insensitive, prefixes and near misses refused, and
  the "value vs. not-one" ambiguity (`row`, `nowrap`, `normal`) asked through the
  `SliceIsKnown` door;
- serialization and the buffer discipline — a too-small buffer is `RoboNotSupported`
  with the length it needed and the buffer left empty;
- the alignment role matrix: the seven values all four properties accept, the
  distribution / baseline / auto / self-position / physical-edge rows, and the
  unknown value and unknown role answering 0;
- the eight shapes over real declaration text — direction, wrap, the four alignment
  shapes with the *refusals* that differ between them, the factor (non-negative) and
  order (`<integer>`, signed), and `flex-basis` over the shared length grammar;
- the twelve registry rows (shape, non-inheritance, initial value, the two
  shorthands flagged) read from `CssPropertyRegistryCreateStandard`;
- the capability, the version, the three kind names and the diagnostic domain; and
- determinism, by structural equality of two computations of one declaration.

`TestCss.ps1 -Target Host` is the gate (664 cases with this suite), and a flex
change is not done until `TestLayout`, `TestPaint`, `TestHtml` and `TestDesktop`
(`-Target Host`) also pass — the layout side reads these values, so a change here is
a change to what it reads even though nothing in this phase lays a box out.
