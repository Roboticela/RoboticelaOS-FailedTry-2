# RoboCSS and Logical Properties (Phase 18.12)

This document is the authority for the logical-property support Phase 18 adds to
RoboCSS.

The module is `Runtime/Source/RoboCSS/RoboCSSLogical.c` with its public contract
`Runtime/Include/RoboCSS/RoboCSSLogical.h`.

## 1. What the phase is

A logical property names an edge by its *role* — `block-start`, `inline-start` — and
lets the writing mode and direction decide which physical edge that is:

```text
logical side   block-start | block-end | inline-start | inline-end
logical axis   block | inline
```

`CssLogicalResolveSide` maps a role to a `CssBoxSide` (top/right/bottom/left), reading
the **writing-mode** module's `CssWritingMode` and `CssDirection` (§18.13). The block
axis is the mode's; the inline axis is the mode's turned by `direction`:

| mode | block-start | block-end | inline-start (ltr) | inline-start (rtl) |
|---|---|---|---|---|
| `horizontal-tb` | top | bottom | left | right |
| `vertical-rl` | right | left | top | bottom |
| `vertical-lr` | left | right | top | bottom |
| `sideways-rl` | right | left | top | bottom |
| `sideways-lr` | left | right | top | bottom |

The two `sideways-*` modes resolve exactly as their vertical counterparts because
sideways turns the *text*, not the axes. An unknown writing mode is read as
`horizontal-tb`, the same safe answer `CssWritingModeAxes` gives in RoboText.

## 2. The logical properties are the same shapes as the physical ones

The phase registers the box model's logical longhands and shorthands — `margin-block-start`
and its three siblings, the four `padding-*` logical longhands, the four logical insets,
`block-size`/`inline-size`, and the four axis shorthands `margin-block`,
`margin-inline`, `padding-block` and `padding-inline`. A logical longhand is carried as
the **same shape** its physical counterpart is: `margin-block-start` is an `EdgeOffset`
like `margin-top`, `padding-inline-end` a `Length` like `padding-right`, `inline-size` a
`Length` `auto` like `width`. Turning a role into the physical edge it names is the
resolver's job, not a second value model.

## 3. Non-goals

- **No logical-value *use*.** Nothing here maps an element's *computed* style to
  physical values; that is the phase with an element (19.10's RTL layout). This module
  supplies the mapping a consumer reads.
- **The logical border longhands and logical `min-`/`max-` sizes** are named, not built.
  So are `border-start-start-radius` and the logical insets beyond the two axes.
- **No second writing-mode vocabulary.** The value comes from §18.13; this module reads
  it.

## 4. Capabilities and version

Phase 18.12 adds one flag to `CssCapabilities` — `logicalProperties` — and one diagnostic
domain (`RoboCssDomainLogical`), and takes the module to **0.31.0**. Eighteen registry
rows join the table, none inherited: the count moves from 143 to 161, non-inherited from
121 to 139, inherited stays 22. No new computation is added, because every logical row
reads a shape the table already has.

## 5. Tests

`Tests/Css/SuiteRoboCssLogical.c` (`css.logical.*`, 4 cases) is the contract:

- the side vocabulary with its axis and start/end classifications, and the axis
  vocabulary;
- the resolver over every writing mode and both directions, including that `rtl` flips
  only the inline axis, that a vertical `rtl` runs its inline axis bottom-to-top, that
  `sideways-*` matches its vertical counterpart, and that an unknown mode reads as
  `horizontal-tb`;
- the eighteen registry rows, their shapes and their non-inheritance, with the four
  shorthands marked;
- the capability flag, the version and the diagnostic domain.
