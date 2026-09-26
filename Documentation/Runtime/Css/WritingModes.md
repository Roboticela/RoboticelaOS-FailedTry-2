# RoboCSS and the Writing-mode Family (Phase 18.13)

This document is the authority for the writing-mode family Phase 18 adds to
RoboCSS: the three closed sets that together decide which physical edge is
**inline-start**, which edge is **block-start**, and how a character stands inside
a vertical line.

The module is `Runtime/Source/RoboCSS/RoboCSSWriting.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSWriting.h`.

## 1. What the phase is

Three properties, each a closed set and each a row per value:

```text
direction         ltr | rtl
  └── right-to-left   rtl
writing-mode      horizontal-tb | vertical-rl | vertical-lr | sideways-rl | sideways-lr
  └── horizontal      horizontal-tb
  └── vertical        the four non-horizontal values
  └── sideways        sideways-rl, sideways-lr
text-orientation  mixed | upright | sideways
  └── sideways        sideways
```

`direction` says which way the inline axis runs. `writing-mode` says which physical
axis is the block axis and, for two of its values, whether the text is turned.
`text-orientation` says how a character stands when the mode is vertical; it is
carried and **inert** in `horizontal-tb`, exactly as `background-attachment: fixed`
is a value with no scrollport.

All three properties are **inherited**. The writing mode and the orientation decide
the physical meaning of every logical property, and a child that did not inherit them
would be laid out on axes its parent was not on.

## 2. `vertical` and `sideways` are two questions

The one interesting decision in this family is that `writing-mode`'s vertical values
are **not** a vertical/horizontal pair. `vertical-rl` and `vertical-lr` are vertical
and keep the characters mixed; `sideways-rl` and `sideways-lr` are vertical *and*
turn the text. So the row carries two classification columns rather than deriving one
from the other:

| value | horizontal | vertical | sideways |
|---|---|---|---|
| `horizontal-tb` | ✓ | | |
| `vertical-rl` | | ✓ | |
| `vertical-lr` | | ✓ | |
| `sideways-rl` | | ✓ | ✓ |
| `sideways-lr` | | ✓ | ✓ |

`CssWritingModeIsVertical` is therefore **not** the negation of
`CssWritingModeIsSideways`, and the two doors are asked separately. A reader that
derived one column from the other would answer that `vertical-rl` is sideways, which
is the defect the two columns exist to make impossible.

The two properties are exported as the two questions a consumer has:
`CssDirectionIsRightToLeft` is the fact bidi and logical properties read, and
`CssTextOrientationIsSideways` is the fact a vertical run reads. Each is asked of the
row, never of a spelling.

## 3. `direction` is upgraded in place, not added

`direction` has been a registered property since 9.7, and it had the bare **KEYWORD**
shape — which accepted *any* identifier, so `direction: banana` computed to a keyword
carrying the letters `banana`. Phase 18.13 changes its `computation` to
`RoboCssPropertyComputationDirection` and its predicate to the closed set. Its name,
its inheritance and its `ltr` initial value are unchanged, and `ltr` is the zero of
the enumerator, so a cleared record is still already right.

This is the same "shape change, not a new property" rule the border family recorded
for `border-width` and 9.13 for `position`. The conformance suite's row for
`direction` did not change, because a closed vocabulary of keywords still computes to
a **keyword** — the kind is the value's, and the refusal is the only thing that moved.

## 4. Non-goals

- **No vertical layout and no vertical shaper.** Nothing in RoboCSS derives a run, a
  line, a caret or a fragment from a writing mode; a value says *which axis is which*,
  and the module that has a box reads it. RoboText's own `RoboTextWritingMode` is the
  engine's answer, and it is a different type in a different module.
- **The `sideways-*` values are carried, not drawn.** They are values the build's text
  engine does not yet implement. They are kept because declining them would lose the
  two logical-property readers beside them, not because anything lays them out.
- **No logical-property expansion here.** `margin-inline-start` and its family are
  18.12's; this phase supplies the fact they are resolved against.
- **No bidi algorithm.** `direction` is the paragraph base direction an author
  declares; reordering the characters is RoboText's (Phase 12.8).

## 5. Capabilities and version

Phase 18.13 adds one flag to `CssCapabilities` — `writingModes` — and one diagnostic
domain (`RoboCssDomainWriting`), and takes the module to **0.25.0**. The flag claims
the three vocabularies and their classifications; it does not claim vertical layout.

Two registry rows join the table, both inherited: `writing-mode` (initial
`horizontal-tb`) and `text-orientation` (initial `mixed`). Each initial is the zero of
its enumerator, so the count of inherited properties moves from 20 to 22 and the
registry from 113 to 115.

## 6. Tests

`Tests/Css/SuiteRoboCssWriting.c` (`css.writing.*`, 7 cases) is the contract:

- the three vocabularies walked in enumerator order, with every keyword read back, the
  case-insensitive match, the absent/unknown `FromText` answers and the `SliceIsKnown`
  door;
- the classifications: `rtl` is right-to-left, horizontal and vertical are the axis
  question, and **sideways is a separate answer** the vertical pair is not;
- the safe answer for an unknown enumerator on every classification;
- the three shapes through the real grammar, including the refusal of `banana` for
  `direction` where the old KEYWORD shape would have accepted it;
- the three registry rows, their shapes, their inheritance and their initials;
- the capability flag, the version, the computation names and the diagnostic domain;
- determinism.
