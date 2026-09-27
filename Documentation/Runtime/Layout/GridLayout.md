# Grid layout (Phase 19.4)

A grid container is a box whose **inner** display is `grid`, and a grid item is a box that
participates in the context such a container establishes. Before this phase `display: grid`
and `display: inline-grid` were refused by 9.12's vocabulary, and 18.4 had landed the *values*
— the flow, the template, the line placements and the gutters as computed values — with
nothing in the tree reading them. This is the goal that makes the pair real display values,
gives a grid container a context of its own (`RoboLayoutFcGrid`), and places its in-flow
children into a two-dimensional set of **tracks** instead of stacking them or placing them on
a line.

It adds **no engine** to the module above this one: the items are ordinary boxes with results,
margins, padding, borders, overflow and children of their own, and the block pass reaches the
grid pass exactly as it reaches the inline and flex ones. What the phase adds is a fourth
formatting context, one algorithm over the boxes already in the tree, and the counters the
pass reports it with.

Files: `RoboLayoutGrid.h/.c` (the pass and the counters record), the constructor of
`RoboLayout/RoboLayoutTypes.h` (the diagnostic domain), `RoboCSS/RoboCSSDisplay.h/.c` (the two
display values and their inner), `RoboLayoutFormattingContext.h/.c` (the context kind and the
classifier's sixth input), `RoboLayoutInternal.h` (the walk entry point), `RoboLayoutBlock.c`
(the dispatch and the identity) and `RoboLayoutContext.c` (the per-box fact that a box is a
grid item).
Suite: `Tests/Layout/SuiteRoboLayoutGrid.c` (24 cases, `layout.grid.*`).
Version: `0.21.0`; capability: `LayoutCapabilities.gridLayout`.
Boundary scan: `Runtime/Tools/Build.ps1` compiles `RoboLayoutGrid.c` and the suite in
`-Verify` like every other source.

## 1. The two display values, and the inner column

`grid` and `inline-grid` are two more rows of the table 9.12 shaped and 19.3 already extended:

| Value | outer | inner |
|---|---|---|
| `grid` | `BLOCK` | `GRID` |
| `inline-grid` | `INLINE` | `GRID` |

`RoboCssDisplayGrid = 6` and `RoboCssDisplayInlineGrid = 7` are appended enumerators and
`RoboCssDisplayCount` is 8; `RoboCssDisplayInnerGrid = 4` is the new inner kind and
`RoboCssDisplayInnerCount` is 5. Nothing in `RoboCssDisplay.c` changed to add them: the
vocabulary is a table, the outer/inner columns are columns, and the identifier parser, the
serializer and `CssDisplayIsInlineLevel` all read those columns rather than testing for a
spelling. The reason the *inner* column matters is the one 19.3 gave: what a layout pass needs
to know is not "was this written as grid" but "what lays these contents out", and that is a
column.

## 2. The context kind

`RoboLayoutFcGrid = 5` is the sixth context kind and `RoboLayoutFcKindCount` is 6.
`LayoutFcEstablishes` gained a **sixth** input, `isGridItem`, beside 19.3's `isFlexItem`, and
the two are asked for the reason the flex one was: a box inside a grid is not laid out by
*its own* inner display alone. The pass reaches the grid direction through
`LayoutBlockWalkContents`, which dispatches on the context the box **owns**:

    owned.kind == RoboLayoutFcFlex  → LayoutFlexWalkContents
    owned.kind == RoboLayoutFcGrid  → LayoutGridWalkContents

and `LayoutBlockWalkChildren` is the one door into an item's own contents, so a grid item's
children are laid out by the same machinery as any other box's.

The classifier's ordering is the algorithm's ordering, and the grid branch sits where the flex
branch does — after `flow-root`, before the out-of-flow and transparency questions, because
those are about a box's **children** while the inner value is about what the box does with
them.

## 3. The algorithm

The pass runs in nine steps, and the first two are the ones that make it a grid rather than a
flex box:

**1. The container's declarations.** The two track lists (`grid-template-columns`,
`grid-template-rows`), the two implicit lists (`grid-auto-columns`, `grid-auto-rows`), the
flow, the two gutters, `justify-content`/`align-content`/`align-items` and `direction`. Every
one of them is read from the **computed style** through 18.4's own reader — a track list is a
value record (`RoboCSSGridTemplate.h`) and is never re-parsed here — and a declaration this
build cannot use degrades to the initial value rather than failing, which is the rule every
other reader in this module follows.

**2. The items and their placements.** An in-flow child that is a box is an item; an
out-of-flow child is not (14.0 places it), and a text run is **counted** rather than placed,
because CSS wraps an inline run in an anonymous grid item and this build makes none. Each
item's four line longhands are resolved by `LayoutGridResolveAxis` into `[start, end)` plus
"definite on this axis?", with the nine combinations CSS defines collapsing to two answers: a
definite line on either side (with a `span` on the other giving the extent) pins the axis, and
two `auto`s leave it to the cursor. An over-constrained pair, a line past the bound and a named
line all degrade to `auto`, which is the same degradation the track lists get.

**3. Auto-placement**, in three sweeps: an item definite on **both** axes is placed where it
says; then an item definite on the axis the flow advances *across* is placed in that band; and
then everything else follows the cursor in document order. The implicit grid's extent on each
axis before auto-placement is the explicit tracks extended by any placement that named a line
past them, and the axis the flow **advances along** is bounded by that extent while the other
axis grows. That bound is the rule, not a search limit: a third item in a two-column
`grid-auto-flow: row` grid goes into a second **row**, and a search that let the advancing axis
grow would place it in an implicit third column and lay the document out as one long line. The
cursor's sparse/dense distinction is a difference in **order** and in nothing else: a sparse
cursor starts where the previous placement ended and never looks back, a dense one starts at
the origin for every item and therefore backfills a hole.

**4. The columns.** Each track is sized from its declared breadth — a length, a percentage
against the container's content width, a `minmax()` pair, or a fraction — with `auto`,
`min-content` and `max-content` taking a content floor and ceiling from the items' measured
**min-content** and **max-content** contributions (19.1's doors), distributed across a
spanning item's tracks. Then `fr` shares the space the other tracks left, `justify-content`
places or grows the result, and the gutters come out before either.

**5. Each item's inline size, and its contents.** A grid item's containing block *is its
area*, so a percentage width resolves against the inline size the columns gave it, `auto`
stretches across the area less the item's own insets (CSS's initial `justify-items: normal`),
and a declared width is placed at the area's inline start. The item's contents are then laid
out at that width — **inside** the track sizing, not at the end, because a row's size is a
function of what the items came to when they were laid out at the width the columns decided.
That ordering is the algorithm, and it is the one phase the flex pass does not have.

**6. The rows**, by the same four steps, from the items' block contributions, with `align-content`
distributing them where the container's height is definite. An indefinite container has no
free space to distribute, so an `fr` row is read as `auto` and the rows *are* the extent.

**6b. The baselines, and the rows they raise.** A `baseline`-aligned row is as tall as its
deepest baseline plus the deepest descent below it, exactly as a baseline-aligned flex line is,
and every baseline-aligned item in the row is placed so its own baseline lands on the row's.

**7–8. The offsets and each item's area**, with `align-items`/`align-self` placing the item on
the block axis (`stretch`/`normal` growing an `auto` height, `baseline` using 6b, everything
else placing it in the area), an `auto` margin absorbing the free space in its own axis
**ahead of** the alignment property, and `direction: rtl` mirroring the columns.

**9. Each item is written** through the same doors every other direction uses:
`LayoutResultSetGeometry`, `LayoutOverflowMeasure`, the margin/padding/border records, the
baseline it was placed with, and the same counters the block pass already keeps.

### Counting what the placement did

`implicitTracks` is the **grid's** own answer — the tracks the final grid has beyond the two
templates, counted once per track — rather than a per-item sum, because three items that reach
one implicit track created one track. `denseItems` counts the items a dense flow actually
**backfilled** (placed earlier in the flow order than the sparse cursor had reached) rather than
every item a dense flow placed, so the number answers "did looking back matter?".

## 4. The counters, and the block pass's accounting identity

`LayoutBlockStats.grid` is a `LayoutGridCounters` by value, beside the flex record and nested
the same way. `blocksPlaced` still counts every placed box — the container's items are its
contribution to it — and `itemsPlaced + itemsSkipped` is every in-flow child the pass looked
at, which is the identity `RoboLayoutBlock.h` states for every direction.

| Counter | What it answers |
|---|---|
| `containers` | grid containers whose contents the grid pass laid out |
| `itemsPlaced` | grid items that received geometry |
| `itemsSkipped` | in-flow children that are not items this build can place (a text run, a box past the ceiling) |
| `columns`, `rows` | tracks the pass sized, explicit and implicit together |
| `implicitTracks` | tracks the final grid has beyond the two templates |
| `placedItems` | items whose placement came from the four longhands on **both** axes |
| `autoPlaced` | items the auto-placement algorithm placed, bands included |
| `denseItems` | items a dense flow backfilled |
| `spanningItems` | items covering more than one track on either axis |
| `frTracks` | `<flex>` tracks the pass resolved |
| `contentTracks` | tracks whose size came from their contents |
| `stretched` | items that grew into their area from `stretch`/`normal` |
| `autoMargins` | items an `auto` margin absorbed free space for |
| `baselineItems` | items whose resolved block-axis alignment was `baseline` |
| `reversed` | containers whose column order was reversed (`direction: rtl`) |
| `overflowingItems` | items a declared inline size made wider than their area |

`LayoutGridCountersEqual` is field-by-field, and `SuiteRoboLayoutGrid.c`'s capability case
pins that one field moved apart is a difference.

## 5. The box-model policy this phase adds

Two additions, each a rule rather than a special case:

**A grid item's containing block is its area.** A percentage width or height resolves against
the area's own inline or block extent, not against the container's box. That is CSS's rule and
the one an author writing `width: 50%` in a two-column grid means, and it is why step 5 runs
before step 6: the columns must exist before an item's own percentage can resolve.

**An `auto` margin absorbs free space ahead of the alignment property.** The four physical
sides are mapped once to flow-relative flags with `direction` deciding only *which* side is the
inline axis's before-side, exactly as the flex pass does it and for the same reason, and the
absorbed space grows the item's **margin box** rather than moving it. `justify-content` and
`align-items` are then not consulted for that axis, which is CSS's rule.

## 6. What is deliberately absent

Each absence is named in `RoboLayoutGrid.h` and in the `grid_layout` flag, so a reader does not
have to infer it from behaviour:

- `justify-items`, `justify-self` and the `place-*` shorthands, which 18.4 deferred and this
  phase still does not add — an item's inline alignment is therefore always the *initial*
  behaviour (stretch an `auto` width, else the area's inline start).
- named lines, `[name]`, `grid-template-areas` and `subgrid`: a placement this build reads is a
  number, a `span` or `auto`, and an area template places nothing.
- `repeat(auto-fill)`/`repeat(auto-fit)`, whose count is the container's used size.
- shorthand expansion: `grid`, `grid-template`, `grid-row`, `grid-column`, `grid-area` and
  `gap` are read as carried declarations, the same boundary flex draws at `flex`/`flex-flow`.
- masonry, `order`-modified placement order, `align-self: baseline` on the inline axis, and a
  grid **root** box (the tree root establishes the initial block context whatever it computes
  to).
- replaced elements and `aspect-ratio`, and vertical writing modes.

## 7. What the suite asserts

`Tests/Layout/SuiteRoboLayoutGrid.c` (24 cases) fabricates a **typed** computed style per tag,
including the track lists and the line placements as the preserved token trees a real cascade
carries, because the point of the phase is that the pass reads those *values* — a case built
from a stylesheet could only prove the parser. Geometry is read back through the public result
doors.

| Case | Asks |
|---|---|
| `capability` | the flag, and that the counters record has a defined zero |
| `formatting_context` | `display: grid` and `display: inline-grid` each own a `RoboLayoutFcGrid` context |
| `fixed_columns` | a declared track is `minmax(x, x)`, and the third item wraps into a second **row** |
| `fraction_columns` | `1fr 2fr` shares the content width |
| `percentage_columns` | a percentage track resolves against the container's content box |
| `gap` | `column-gap`/`row-gap` come out before the fractions are shared |
| `content_column` | an `auto` track takes its items' measured content |
| `auto_stretch` | `normal` grows the `auto` tracks, and `justify-content` places them when nothing is `auto` |
| `implicit_rows` | no template: one implicit column, one row per item |
| `auto_flow_column` | `grid-auto-flow: column` advances along the other axis |
| `line_placement` | `grid-column-start` pins one item and the cursor places the rest |
| `both_axes_placed` | four longhands place an item before any cursor moves |
| `spanning` | a `span 2` item covers two tracks and the next item still adds no column |
| `dense` | the same three items under `row` and `row dense` |
| `align_items` | `stretch` grows an `auto` height, `align-self: flex-end` places a declared one |
| `align_content` | the rows of an indefinite container *are* the extent |
| `baseline` | two items' bottom-edge fallbacks line up and the row grows |
| `auto_margins` | two `auto` margins split the free space |
| `overflow` | a declared width wider than its area is *reported*, not clamped |
| `rtl` | `direction: rtl` reverses the column order |
| `nested` | a grid inside a grid item |
| `counters_balance` | every in-flow child is placed or skipped, and the grid's items are the container's contribution to `blocksPlaced` |
| `allocator_balance` | a pass allocates nothing it does not release |

## 8. What the phase changed in the suites that came before it

Four, and each is a re-pin rather than a new claim:

- `SuiteRoboLayout.c` — the version (`0.21.0`), the capability table (a `grid_layout` row), the
  domain count (26) and the domain ordering (`grid` is the newest, `flex` the one before it).
- `SuiteRoboLayoutOverflow.c`, `SuiteRoboLayoutPosition.c` — the domain count.
- `SuiteRoboLayoutConformance.c` — the capability table, the "implemented" count (27 of 28,
  `painting` the only 0) and the version statement.
- `SuiteRoboLayoutFormattingContext.c` — the sixth kind and the sixth classifier input.

## 9. Boundaries

RoboLayout may see RoboCSS and RoboHTML and nothing above them; this phase reads computed
values and adds no RoboCSS, RoboPaint, RoboRender or RoboWindow dependency.
`Runtime/Tools/Build.ps1`'s boundary scan compiles the module and its suites in `-Verify` like
every other source, and `RoboLayoutGrid.c` includes only `RoboLayoutInternal.h`,
`RoboLayoutGrid.h`, the sizing/text/margin/padding/border/position/overflow/baseline/intrinsic
headers it reads through, and RoboCSS's own value headers.

## 10. Running the suite

    pwsh -NoProfile -File Scripts/Test/TestLayout.ps1 -Target Host

`TestLayout.ps1` is the gate (453 cases with this suite). A grid change is not done until
`TestCss.ps1`, `TestPaint.ps1`, `TestGraphics.ps1`, `TestFont.ps1` and `TestDesktop.ps1`
(`-Target Host`) also pass, and `TestDesktop.ps1 -Target Qemu` where the guest image is
involved.
