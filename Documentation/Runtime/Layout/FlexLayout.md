# Flex layout (Phase 19.3)

A flex container is a box whose **inner** display is `flex`, and a flex item is a box
that participates in the context such a container establishes. Before this phase
`display: flex` and `display: inline-flex` were refused by 9.12's vocabulary, and
`RoboLayoutFcKind` had three laying-out kinds — `ROOT`, `BLOCK`, `INLINE`. This is the
goal that makes the flex pair real display values, gives a flex container a context of
its own (`RoboLayoutFcFlex`), and lays its in-flow children out along a **main axis**
with free space distributed by `flex-grow`/`flex-shrink` rather than stacked or placed
on a line.

It adds **no engine** to the module above this one: the items are ordinary boxes with
results, margins, padding, borders, overflow and children of their own, and the block
pass reaches the flex pass exactly as it reaches the inline one. What the phase adds is
a third formatting context, one algorithm over the boxes already in the tree, and the
counters the pass reports it with.

Files: `RoboLayoutFlex.h/.c` (the pass and the counters record), the constructor of
`RoboLayout/RoboLayoutTypes.h` (the diagnostic domain), `RoboCSS/RoboCSSDisplay.h/.c`
(the two display values and their inner), `RoboLayoutFormattingContext.h/.c` (the
context kind and the classifier), `RoboLayoutInternal.h` (the walk entry point),
`RoboLayoutBlock.c` (the dispatch and the identity) and `RoboLayoutContext.c` (the
per-box fact that a box is a flex item).
Suite: `Tests/Layout/SuiteRoboLayoutFlex.c` (24 cases, `layout.flex.*`).
Version: `0.20.0`; capability: `LayoutCapabilities.flexLayout`.
Boundary scan: `Runtime/Tools/Build.ps1` compiles `RoboLayoutFlex.c` and the suite in
`-Verify` like every other source.

## 1. The two display values, and the inner column

`flex` and `inline-flex` are the two rows 9.12's table was shaped for, and they land
as data rather than as a branch:

| Value | outer | inner |
|---|---|---|
| `flex` | `BLOCK` | `FLEX` |
| `inline-flex` | `INLINE` | `FLEX` |

`RoboCssDisplayInnerFlex = 3` is the new inner kind and `RoboCssDisplayInnerCount` is
4. Nothing in `RoboCssDisplay.c` changed to add them: the vocabulary is a table, the
parsers walk it, and `CssDisplayInnerName` gained one row. `RoboCssDisplayCount` moved
from 4 to 6 and both keywords parse case-insensitively like every other display value.

The distinction the inner column carries is exactly what 10.2's classifier needed and
did not have: a **flex container is not a block container.** `LayoutFcDisplayIsBlockContainer`
now answers 0 for a block-level box whose inner is `FLEX`, and `LayoutFcDisplayCanEstablish`
answers 1 for it — a flex formatting context is a new context the same way `flow-root`
is. That is the whole reason the phase reads *one* inner value rather than the two
spellings: `display: inline-flex` shares the inner with `display: flex`, and a build
that named the spellings would have to be edited again for the next value that shares
`FLEX`.

## 2. The context kind

`RoboLayoutFcKind` gained `RoboLayoutFcFlex = 4` and `RoboLayoutFcKindCount` moved from
4 to 5. `LayoutFcKindName` spells it `"flex"`, and `LayoutFcKindIsBlockContainer` is 0
for it — a flex context is neither the block direction nor the inline one, and naming
it as a block container would invite the block pass to stack its participants.

`LayoutFcEstablishes` gained a fifth fact and one new rule:

```c
LayoutFcKind LayoutFcEstablishes(CssDisplay display,
                                 int hasBlockLevelChild,
                                 int isRoot,
                                 int isOutOfFlow,
                                 int isFlexItem);
```

- an inner display of `FLEX` establishes `RoboLayoutFcFlex`, whatever the box's
  children are and whether or not it is positioned — asked *before* the out-of-flow
  and transparency questions, because the container owns its context by display and
  not by position;
- a **flex item** establishes `RoboLayoutFcBlock` when it has a block-level child,
  rather than being transparent. CSS says every flex item establishes an independent
  formatting context, and the reason it has to be stated here is specific: a
  transparent item's block-level child would reach the *flex* context through the
  parent chain, and a block-level box participating in a flex context is exactly the
  contradiction the block pass reports.

`isFlexItem` is a fact about the context the box just joined, so it is read once in
`LayoutClassifyObject` (RoboLayoutContext.c) from `object->context`, not inside the
classifier — the classifier stays a pure function of the facts it is handed.

## 3. The algorithm, and why it is a flag rather than two paths

`LayoutFlexWalkContents` (RoboLayoutFlex.c) is one pass over one container, in CSS
Flexible Box Layout's own order:

1. **collect** the in-flow children — a child is a flex item only if it is a box and
   its `position` is not out of flow, so an `absolute`/`fixed` child is placed by the
   containing block's own pass (14.0) exactly as it is in a block container;
2. **base size** per item, in three steps for the reason CSS states three:
   `flex-basis` when it names a size, the main-size property when that is definite,
   and the item's measured content size (`LayoutIntrinsicMeasureObject`) as the last
   word;
3. **order** the items by `order` and then by document order, with a stable insertion
   sort, before anything is measured;
4. **lines** when `flex-wrap` wraps *and* the container's main size is definite;
5. **flexible lengths** per line: distribute the free space by `flex-grow` (or
   `flex-shrink` weighted by the base size), clamping to `min`/`max`, and freeze an
   item that reached a bound and redistribute among the rest until no item violates.
   The **freeze** is load-bearing rather than bookkeeping: a clamped item leaves the
   redistribution, and a build that only counted the violation would recompute the same
   clamped target every pass. This phase found and fixed exactly that latent defect —
   the min/max case is the one that exercises it, and `layout.flex.min_max_clamp` is
   the regression that keeps it fixed;
6. **content** — each item's own children are laid out at the item's resolved size,
   which is what the item's **baseline** is then read from;
7. **line sizes** — including a baseline-aligned line's own baseline — `align-content`,
   and stretch (an item with an `auto` cross margin does not stretch: the margin has
   claimed the space);
8. **positions** — an `auto` margin absorbs the free space on its own axis first,
   then `justify-content` along the main axis and `align-items`/`align-self` (or the
   line's baseline) across it — and then one write per item.

The two axes are a **flag** (`rowMain`) rather than two code paths. Every position is
computed once in main/cross terms and swapped at the two points it becomes a
coordinate:

```
row / row-reverse            main = inline (x), cross = block (y)
column / column-reverse      main = block (y), cross = inline (x)
```

A `column` and a `row` therefore cannot disagree about `justify-content` or about
`flex-grow`. `CssFlexDirectionIsRow` and `CssFlexDirectionIsReversed` (18.3's
vocabulary) are the only two readers of the direction.

Which physical direction each axis runs in is a second pair of flags, and it is where
`direction` enters:

```
mainReversed  = CssFlexDirectionIsReversed(direction) XOR (row && direction: rtl)
crossReversed = CssFlexWrapIsReversed(flex-wrap)
```

The main axis's start is the **inline-start** edge, so `direction: rtl` reverses a
row's main axis exactly as `row-reverse` does and the two compose by exclusive-or —
which is what makes `row-reverse` inside an RTL container read left-to-right
(`layout.flex.rtl_compose`). A column's main axis is the block one, which `direction`
does not touch; a column's **cross** axis is the inline one and is 19.10's, so it stays
left-to-right here and the non-claim is stated in `RoboLayoutFlex.h`.

The flags are also why an `auto` margin cannot claim the wrong side: the collection
loop maps the four physical margin sides onto `autoMainBefore`/`…After` and
`autoCrossBefore`/`…After` **once**, every later step reads only the flow-relative
flags, and `LayoutFlexAbsorbMargin` maps back to the physical side when the space is
given out.

### The item's baseline

The item's baseline is its first baseline-bearing **direct child**'s, and that is the
item's first baseline at the depth this build places text: an item that establishes an
inline formatting context has its measured runs as direct children, and
`LayoutInlineFinalizeLine` writes each member's `y` so that `position.y + ascent` *is*
the line's baseline. An item with no such child takes the documented **bottom-edge
fallback** (`RoboLayoutBaseline.h`'s BOX rule). The offset is measured from the item's
cross-start **margin** edge, so "the baseline sits at `crossPos + baseline`" needs no
third term, and the metrics are written back to the object — a baseline is a
*placement* fact and the flex pass is what placed the item.

Three of the container's facts are read once:

- a **row**'s main size is always definite — the block pass resolved the container's
  width — which is why a row wraps and a column with an `auto` height does not;
- the **cross** size is the other axis: definite for a column, definite for a row only
  when the container has a declared (or percentage-resolved) height;
- the container's declared height is handed in by the block pass *before* the
  container's own height is resolved, for the same reason 19.2's percentage-height base
  is — it is the cross size a row stretches to and the main size a column distributes
  along.

## 4. The counters, and the block pass's accounting identity

`LayoutFlexCounters` is a record by value, nested in `LayoutBlockStats.flex` beside the
block, inline, margin, padding, border, position and overflow counters, for the reason
14.0's `absolute` record is nested: the counters belong to the phase that produced them
and are read together.

| Field | Means |
|---|---|
| `containers` | flex containers whose contents the flex pass laid out |
| `itemsPlaced` | flex items that received geometry |
| `itemsSkipped` | in-flow children that are **not** flex items this build can place |
| `lines` | flex lines produced across every container |
| `growItems` | items whose `flex-grow` was above zero |
| `stretched` | items that stretched across the cross axis |
| `reordered` | items whose `order` moved them from document order |
| `reversed` | containers whose main axis's start and end edges were swapped — a `*-reverse` spelling, a `row` in an RTL container, or both |
| `contentMinItems` | items whose used **minimum** main size came from the automatic minimum (a content floor) **and was above zero** — the counter that says whether the floor bit |
| `autoMargins` | items with an `auto` margin that **absorbed** free space on an axis |
| `baselineItems` | items whose resolved alignment was `baseline` |

`itemsSkipped` is the field that keeps the block pass's identity honest: CSS wraps an
inline run in an **anonymous flex item** and this build makes none, and a box beyond
the pass's fixed item cap is a child the pass declined — both are counted in `unreached`
rather than dropped (`RoboLayoutBlock.h` states the identity that closes over every
direction). `itemsPlaced` is incremented beside `blocksPlaced` so a consumer that
counted only one of them would see a document that does not add up.

The pass is bounded: `LAYOUT_FLEX_MAX_ITEMS` is 128 items in one container, and a child
past the cap is counted as skipped rather than silently placed at zero.

## 5. The box-model policy this phase adds

An item's margins, padding and borders are resolved by the module's existing layers
(`LayoutMarginsOf`, `LayoutPaddingResolve`, `LayoutBorderResolve`) with two policies
that are flex's and not 10.9–10.11's:

- an **`auto` margin absorbs free space.** It is a used value of zero until the
  alignment step has free space to hand out, and then it takes its share of it on the
  axis it is on — **ahead of** `justify-content` and `align-items`, which are not
  consulted at all once an `auto` margin has claimed the space (CSS Flexbox §8.1).
  Both auto margins on one axis split it. The absorbed amount **grows the item's
  margin box** — which is what an `auto` margin is — so it is added to the physical
  margin edge and to the item's outer size rather than moving a cursor past extra
  room.
- a **percentage** margin, padding or cross size resolves against the flex container's
  content width, which is 19.2's rule for a block box.

And one policy that is the item's *size* rather than its box: when `min-width` (a row)
or `min-height` (a column) is `auto`, the used minimum is **the automatic minimum
size** — the item's content-based minimum, which is its measured **min-content** size
clamped by the item's maximum (§4.5). The floor is a *shaped* measurement through
`LayoutIntrinsicMeasureObject`, so an item does not shrink past its longest unbreakable
piece by accident, and a document with no metrics measures it as zero exactly as every
other measurement here does. The one part of §4.5 this build cannot honour is the
`overflow: visible` exception, because there is no scroll state at layout time; the
capability comment and `RoboLayoutFlex.h` name it.

The four records are written to the object, so a later paint pass re-decides nothing.
An item's own children are laid out by `LayoutBlockWalkChildren` at the item's resolved
content width, so an item is a box like any other from the block pass's point of view.

## 6. What is deliberately absent

Each a named later row, stated in `RoboLayoutFlex.h` and at the capability's own
comment so a reader meets it where they would assume the feature:

- **A flex root box.** `LayoutFcEstablishes` answers `ROOT` for the tree root whatever
  it computes to, so `display: flex` on the root element lays its children out as
  blocks. Giving the initial context an inner display is a later goal's.
- **Baseline alignment on a `column`.** A row's cross axis is the block (vertical)
  one, so `baseline` is a first-baseline alignment there and is implemented; a column's
  cross axis is the inline one, where this build has no baseline to place, so `baseline`
  resolves as `flex-start` and is counted.
- **A reversed inline axis on a `column`.** `direction: rtl` reverses a `row`'s main
  axis (above); a `column`'s cross axis is the inline one and reversing it is 19.10's.
- **The automatic minimum's `overflow` exception** (above), and a **baseline for
  anything but a direct child's text** — the fallback is the item's bottom edge.
- **`gap` / `row-gap` / `column-gap`** and the **`flex` / `flex-flow` shorthands**:
  nothing in this build expands a shorthand, so only the longhands are read.
- **Percentage `flex-basis` against an indefinite main size** (it degrades to the
  content size), **replaced elements and `aspect-ratio`**, and **vertical writing
  modes**.

`flex-layout` would be claiming those if it were named for "flexbox is finished". It is
not — see `RoboLayoutFlexLayoutImplemented` in `RoboLayoutVersion.h`.

## 7. What the suite asserts

`Tests/Layout/SuiteRoboLayoutFlex.c` fabricates a **typed** computed style per tag —
including `flex-direction`, `flex-wrap`, `flex-grow` and the alignment values — because
the point of the phase is that the pass reads those *values*, and a case built from a
stylesheet could only prove the parser. Geometry is read back through the public result
doors, so every expectation is a number a consumer could read.

| Case | Subject |
|---|---|
| `capability` | `RoboLayoutFlexLayoutImplemented`, `LayoutGetCapabilities().flexLayout` and the macro agree |
| `formatting_context` | `display: flex` makes the box own a `RoboLayoutFcFlex` context |
| `row_grow` | `flex-basis: 0` with `grow: 1/2/1` in a 300px row gives 75/150/75 and puts them at 0/75/225 |
| `row_basis` | three `flex-basis` values land at 40/50/60 and the cursor advances by each |
| `row_shrink` | three 100px bases in a 150px row shrink to 50 each |
| `basis_percentage` | `flex-basis: 25%` of a 200px main size is 50 |
| `justify_center` | `justify-content: center` puts two 20px items at 30 and 50 |
| `align_stretch` | two items with no declared height stretch to the 200px cross size |
| `column` | a column's main axis is vertical and each item stretches to the 300px width |
| `wrap` | three 60px items in a 100px wrapped row take one line each, stacked 40 apart |
| `order` | `order: 1` on the second item places it first |
| `item_children` | a `<p>` inside an item is laid out at the item's content width and the item's `auto` cross size is the extent it reached |
| `counters` | `containers`, `itemsPlaced` and `lines` of one pass |
| `allocator_balance` | eight build→pass→destroy rounds leak nothing |
| `auto_minimum` | an item with `flex-basis: 0` and no declared minimum is floored at its content's min-content size (40px for a four-letter word at 10px a code point), and `contentMinItems` counts it |
| `text_intrinsic` | with neither `flex-basis` nor a main-size property the base size is the measured content, and the cross size is the content's extent |
| `min_max_clamp` | three growing items at basis 100 in a 600px row: the one whose `max-width: 60` is reached freezes and the other 340 redistributes to 270 each — the regression for the freeze defect |
| `min_width_clamp` | a declared `min-width: 100` is the used minimum and overrides the automatic floor |
| `auto_margins` | two `auto` margins split 120px of free space 60 each, and `justify-content` is not consulted |
| `auto_margins_cross` | an `auto` cross margin pushes an item to the line's cross end (y 170 of 200), and both auto margins centre it (y 85) |
| `baseline` | two items aligned on `baseline` put their bottom-edge fallbacks on one line, and the baseline each was placed with reads back off the object |
| `rtl` | `direction: rtl` packs a row from the right, and `reversed` counts the container |
| `rtl_compose` | `row-reverse` in an RTL container composes back to `row`, and `reversed` stays 0 |
| `nested` | a flex container inside a flex container lays its own items out inside *its* content box, and `containers` is 2 |

## 8. What the phase changed in the suites that came before it

The module's self-description moved, and so did the vocabulary counts:

| Assertion | Was | Is |
|---|---|---|
| `layout.module.version` + `layout.conformance.statement` | `0.19.0` | **`0.20.0`** |
| the capability table (two suites) | 26 rows, `painting` last | **27 rows**, `flex_layout` before `painting` |
| `layout.conformance.capabilities` implemented count | 25 of 26 | **26 of 27** |
| `RoboLayoutDomainCount` | 24 | **25** (`RoboLayoutDomainFlex` is the newest) |
| `RoboLayoutFcKindCount` | 4 | **5** |
| `RoboCssDisplayCount` / `…InnerCount` | 4 / 3 | **6 / 4** |
| `css.display.*` | `flex` and `inline-flex` are refused | they are values, and their outer/inner halves are asserted |
| `layout.fc.classify.*` | the 4-argument classifier | the 5-argument one, with the flex row |

The two suites that walk every display or every context kind — `CssSuiteRoboCssDisplay`
and `SuiteRoboLayoutFormattingContext` — walk the new rows rather than a representative,
for the reason those suites exist: a table whose rows and enumerators had drifted would
only ever show up as a value classified as another one's kind.

## 9. Boundaries

The dependency direction is unchanged. RoboLayoutFlex.c reads RoboCSS's **computed
values** (`RoboCSSComputedValue.h`), the three display doors and 18.3's flex vocabulary
(`RoboCSSFlex.h`), and it calls the module's own sizing, intrinsic, margin, padding,
border, position and overflow layers for each item — it invents no font system, no
second sizing path and no rasterizer. Nothing above RoboLayout knows a flex context
exists except through the geometry the pass wrote; `layout.flex.*` is the only suite
that names the counters, and the block pass's `flex` record is the one place they are
published.

## 10. Running the suite

```bash
pwsh Scripts/Test/TestLayout.ps1 -Target Host                        # the whole layout surface
./Build/Runtime/RoboTest.host.exe --filter layout.flex               # this phase (24 cases)
pwsh Scripts/Test/TestCss.ps1 -Target Host                          # the display vocabulary it extends
```
