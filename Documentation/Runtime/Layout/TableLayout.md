# Table layout (Phase 19.6)

A table container is a box whose **inner** display is `table`, and the boxes inside it are the
table's rows, cells and caption. Before this phase the ten table display values were refused by
9.12's vocabulary, and 18.5 had landed the *properties* — `table-layout`, `border-collapse`,
`border-spacing`, `caption-side`, `empty-cells` — as computed values with nothing in the tree
reading them. This is the goal that makes the family real display values, gives a table
container a context of its own (`RoboLayoutFcTable`), and places its contents into **rows and
columns** instead of stacking them or placing them on a line.

It is the fifth formatting context this build lays out, beside the block, inline, flex and grid
directions, and it adds **no engine** to the module above this one: a row, a cell and a caption
are ordinary boxes with results, margins, padding, borders, overflow and children of their own,
and the block pass reaches the table pass exactly as it reaches the other four.

Files: `RoboLayoutTable.h/.c` (the pass and the counters record), the constructor of
`RoboLayout/RoboLayoutTypes.h` (the diagnostic domain), `RoboCSS/RoboCSSDisplay.h/.c` (the ten
display values, the third outer kind and the `table` inner), `RoboLayoutFormattingContext.h/.c`
(the context kind and the classifier's rules for an internal box), `RoboLayoutInternal.h` (the
walk entry point), `RoboLayoutBlock.c` (the dispatch, the fourth verdict and the identity),
`RoboLayoutBlock.h` (the counters record), `RoboLayout.h` and `RoboLayoutVersion.h` (the
capability).
Suite: `Tests/Layout/SuiteRoboLayoutTable.c` (18 cases, `layout.table.*`).
Version: `0.22.0`; capability: `LayoutCapabilities.tableLayout`.
Boundary scan: `Runtime/Tools/Build.ps1` compiles `RoboLayoutTable.c` and the suite in
`-Verify` like every other source.

## 1. The ten display values, and the third outer kind

`table` and `inline-table` are the fifth pair of the shape 9.12 shaped and 19.3/19.4 already
extended twice — a block-level value and an atomic inline one sharing one **inner**. The eight
internal values are the first rows in this vocabulary that are **neither** block- nor
inline-level, and that is what made the phase add a third outer kind rather than two more rows:

| Value | outer | inner | what it is |
|---|---|---|---|
| `table` | `BLOCK` | `TABLE` | the container |
| `inline-table` | `INLINE` | `TABLE` | the same container, atomic on a line |
| `table-row-group` | `INTERNAL` | `FLOW` | a group of rows |
| `table-header-group` | `INTERNAL` | `FLOW` | a header group (placed in document order here) |
| `table-footer-group` | `INTERNAL` | `FLOW` | a footer group |
| `table-row` | `INTERNAL` | `FLOW` | a run of cells |
| `table-cell` | `INTERNAL` | `FLOW-ROOT` | one slot's contents, a context of its own |
| `table-column` | `INTERNAL` | `FLOW` | a declared width and no geometry |
| `table-column-group` | `INTERNAL` | `FLOW` | a group of declared widths |
| `table-caption` | `INTERNAL` | `FLOW-ROOT` | the table's caption, a context of its own |

So `RoboCssDisplayCount` is **18**, `RoboCssDisplayOuterCount` is **4** (with
`RoboCssDisplayOuterInternal = 3`) and `RoboCssDisplayInnerCount` is **6** (with
`RoboCssDisplayInnerTable = 5`). `RoboCssDisplayTextMax` moved from 16 to **24**, because
`table-column-group` is nineteen characters and a value that cannot be written into the
serializer's buffer is a value a caller cannot print; the suite checks the bound against every
row rather than leaving it to the comment.

The reason the outer column had to grow is the question every pass asks first: **which flow
places this box?** An internal table box's answer is neither block nor inline — the table it
belongs to places it — and a build that called a cell block-level would place a table's cells
down the page as a column of blocks, which is the whole of what a table is not. Nothing else in
`RoboCssDisplay.c` changed to add any of the ten rows: the vocabulary is a table, and the
identifier parser, the serializer and every `CssDisplayIs*` predicate read its columns.

The new predicates are the ones the modules above needed, each a *rule* rather than a list:

| Predicate | Answers |
|---|---|
| `CssDisplayIsAtomicInline` | inline-level with an inner that is not plain flow — the generalization `CssDisplayIsInlineBlock` always pointed at (`inline-block`, `inline-flex`, `inline-grid`, `inline-table`) |
| `CssDisplayIsTableRoot` | the inner is `table`: `table` and `inline-table`, the two that establish the context |
| `CssDisplayIsTableInternal` | the outer is `INTERNAL`: the eight boxes a table places |
| `CssDisplayIsTableRowLike` | a row or a row group (either group kind) — the table's block axis |
| `CssDisplayIsTableColumnLike` | a column or a column group — a declared width and no geometry |
| `CssDisplayIsTableCell` / `…Caption` | exactly those two |
| `CssDisplayIsTableBoundary` | a cell or a caption — the two internal boxes CSS 2.1 §9.4.1 lists as establishing a block formatting context |

## 2. The context kind

`RoboLayoutFcTable = 6` is the seventh context kind (counting `NONE`) and
`RoboLayoutFcKindCount` is **7**. `LayoutFcEstablishes` gained one branch, in the same place and
for the same reason the flex and grid branches sit there:

    inner display == flow-root → BLOCK        (a cell and a caption arrive here)
    inner display == flex      → FLEX         (19.3)
    inner display == grid      → GRID         (19.4)
    inner display == table     → TABLE        (19.6)
    an internal table box      → NONE         (a row, a group, a column: the table places it)

The two orders are load-bearing. The `table` branch is asked **before** the out-of-flow and
transparency questions because those are about a box's *children* while the inner value is about
what the box does with them. The internal branch is asked **after** the `flow-root` branch, so a
**cell** and a **caption** never reach it: their inner is `flow-root`, they take the first
branch, and they establish a block formatting context of their own — which is CSS 2.1 §9.4.1's
own list and the reason a cell's contents are laid out *inside* the cell at the width the
columns gave it.

`LayoutFcDisplayIsBlockContainer` gained the same distinction: a **cell** and a **caption** are
block containers (asked of `CssDisplayIsTableBoundary`, not of the outer column), and a row, a
row group, a column and a column group are not. `LayoutFcDisplayCanEstablish` gained the
`table` inner, so a `display: table` box answers 1 to "could this ever establish a context"
rather than 0 — a one-line consequence of the new inner value that the formatting-context suite
now asserts row by row.

The pass reaches the table direction through `LayoutBlockWalkContents`, which dispatches on the
context the box **owns**:

    owned.kind == RoboLayoutFcFlex   → LayoutFlexWalkContents
    owned.kind == RoboLayoutFcGrid   → LayoutGridWalkContents
    owned.kind == RoboLayoutFcTable  → LayoutTableWalkContents

and it is the **only** caller: a table's own pass reads its children itself, so
`LayoutBlockVerdictFor`'s `LAYOUT_BLOCK_TABLE_INTERNAL` verdict is about a box whose table is
*missing* from the tree, not about one whose table is there.

## 3. The algorithm

The pass runs CSS 2.1 §17's steps in §17's order, and the order is the algorithm:

**1. The container's declarations.** `table-layout`, `border-collapse`, `border-spacing` and
`caption-side`, read from the computed style through 18.5's own readers. `border-spacing` is
**carried** rather than typed — two lengths is not one of 18.5's closed vocabularies — so it is
read as preserved components, one length for both gutters or two for the column and the row
gutter in CSS's own order. A collapsing table's spacing is not read at all, because CSS says
`border-spacing` does not apply to one.

**2. The row and cell model, with the fixup.** A caption, the `table-column` boxes and the rows,
with a cell that is a direct child of the table placed as an **anonymous row** of its own. Every
path into the model goes through one function, so `firstCell`/`cellCount` are exact for every
row however it was reached. What the fixup *reads* rather than generates:

| Child | What happens |
|---|---|
| `table-caption` | the first is the table's caption; a second is counted as skipped (CSS has one) |
| `table-row` | a row, and its `table-cell` children are its cells |
| a row group (either kind) | placed as a box, and its rows are the model's rows in document order; a cell directly inside it is an anonymous row |
| `table-column` / `table-column-group` | a **declared width** for the next column slot, and **no geometry** — CSS has no column box on the page |
| `table-cell` | an anonymous row of one (the fixup's real work, and the one it counts) |
| a text run, or a box that is not a table-internal one | **counted**, not wrapped in the anonymous cell CSS would generate |
| an out-of-flow child | not a table box at all: 14.0's pass places it |

**3. The caption next**, because its extent is what the rows start below (or end above): the
caption's contents are laid out at the container's content width through the block pass, and the
extent they reached is the room the caption takes on the block axis.

**4. The columns**, by the used `table-layout` algorithm. `fixed` takes the width the **first
row's cells** (after the column boxes have had their say) and the column boxes declared, and
shares what is left equally among the columns that declared none — the one place a *cell*
decides a column rather than the other way round. `auto` takes a **floor** from the widest
cell's min-content contribution and a **ceiling** from its max-content one (19.1's measurement,
read per column), then distributes the container's width over the columns that can still grow,
in proportion to the room each has left; a declared width is a floor as well as a hint, so a
column whose cell needs more than it declared is as wide as the cell.

A detail that removes a whole class of CSS's own cases: a table here **always has a definite
inline size.** The block pass resolves the container's content width before it asks this pass,
exactly as it does for a flex row or a grid, so `table-layout: fixed` is always *usable* (CSS
only honours it against a definite width, and shrink-to-fit is not this build's) and the `auto`
algorithm always has a width to distribute rather than a content-derived one to solve for.

**5. Each cell's contents, then each row's height.** A cell's own declared width wins over its
column's — and a cell wider than its column is **counted** (`overflowingCells`) rather than
clamped, because an overflowing cell is CSS's answer and hiding it would be this build's — then
the cell's contents are laid out *inside* it at that width through the block pass, and the row's
height is its tallest cell's. A row's own declared height raises it further where the author
asked for more room than its cells needed. This ordering is why the two axes are resolved in
this order and not the other way round: a row's height is a function of contents that do not
exist until the columns are sized.

**6. Every box is written.** The caption, each cell, then each row, then each group — through
the same doors every other direction uses: `LayoutResultSetGeometry`, `LayoutOverflowMeasure`,
the margin/padding/border records, and the same counters the block pass already keeps. A **row**
and a **row group** are written with **no insets of their own**, because CSS's table model
ignores a row's padding, border and margin for the geometry and a build that gave them a box
model of their own would place its cells relative to a number no other implementation agrees
about. A group's rectangle is the union of the rows it holds, which is what makes a
`table-row-group` something a paint pass can draw a background on.

## 4. The counters, and the block pass's accounting identity

`LayoutBlockStats.table` is a `LayoutTableCounters` by value, beside the flex and grid records
and nested the same way. `blocksPlaced` still counts every placed box — a table's cells, rows,
groups and caption are its contribution to it — and `LayoutBlockStatsEqual` compares the nested
record, which is the assertion that would have caught it being left out.

| Counter | What it answers |
|---|---|
| `containers` | table containers whose contents the table pass laid out |
| `rows` | row boxes placed (anonymous rows included) |
| `groups` | row groups placed (either kind, empty ones included) |
| `columns` | column slots sized, declared or discovered |
| `columnBoxes` | `table-column`/`table-column-group` boxes read for a declared width and given no geometry |
| `cellsPlaced` | cells that received geometry |
| `cellsEmpty` | cells with no in-flow **box** child — what `empty-cells` selects on |
| `captions` | captions placed on the table's block-start or block-end edge |
| `anonymousRows` | rows the fixup generated because a cell was a direct child of the table |
| `itemsSkipped` | in-flow children the pass could not place, for a stated reason |
| `contentColumns` | columns sized from their **contents** |
| `fixedColumns` | columns sized from a **declaration** (a column box, the first row, or the equal share) |
| `spacedTables` | tables whose cells are separated by a gutter |
| `collapsedTables` | tables whose borders adjoin, so their gutters are zero |
| `overflowingCells` | cells a declared width made wider than their column |

`contentColumns + fixedColumns == columns` always holds, and the pair is what says which
arithmetic sized a table. `LayoutTableCountersEqual` is field-by-field and the suite pins that
one field moved apart is a difference.

The block pass's identity `blocksPlaced + inlineLevelSkipped + participantsPlaced + unreached ==
objects` closes over this direction because the table pass counts **both** ways it can decline a
box: a child it cannot use (`table.itemsSkipped`) and a column box, both in
`inlineLevelSkipped` — the bucket the block record names for "an object with no geometry, for a
stated reason".

## 5. The box-model policy this phase adds

**The block axis is the table's, and `caption-side` names its edges.** A cell's position is its
column's offset and its containing block is the table's content box, so a percentage inside a
cell resolves against the cell's own content width (the column's width less the cell's insets)
rather than against the table's. A table in this build has one caption edge per side of its
block axis: `top`/`block-start` (the initial value) and `bottom`/`block-end`; the two inline
spellings are read and placed as the block-start edge, which is a named absence rather than a
silent fallback.

**`border-collapse: collapse` is read, counted, and made to zero the gutters.** CSS says
`border-spacing` does not apply to a collapsing table, so the same declaration produces 5px/10px
of space in a separate table and none here. The *border arithmetic* of a collapsing table — one
border where two adjoin, and the rules that pick which — is a later row's work and is named as
absent rather than half-done: each cell's border stays its own.

**`empty-cells` is not read; its subject is counted.** A cell with no in-flow box child is
counted (`cellsEmpty`) and placed like any other, because the property's only effect is on a
cell's border and background and this module paints nothing. `visibility` is not read either:
this build places every cell it is given, and a layout pass that acted on either would be
claiming a paint decision.

## 6. What is deliberately absent

Each absence is named in `RoboLayoutTable.h`, in the `table_layout` flag's own comment and
here, so a reader does not have to infer it from behaviour:

- **`colspan` and `rowspan`.** They are HTML *attributes* and not CSS properties, so no computed
  value carries them and this module reads no attribute: every cell occupies exactly one slot,
  and a document whose cells span must not expect the table to be right about it.
- **the border arithmetic of a collapsing table**, as above.
- **anonymous cell generation**: a child that is neither a table-internal box nor a caption is
  counted, exactly as the grid pass counts the anonymous grid item it does not build. The fixup
  *inside* a table is the one this phase does.
- **an internal table box outside a table**: CSS 2.1 §17.2.1 wraps it in the anonymous table,
  row group and row it would need, and this build makes no boxes — so it is counted and left
  `UNRESOLVED` (`LAYOUT_BLOCK_TABLE_INTERNAL`). A table-internal box is neither block- nor
  inline-level, so a container whose *only* child is one has no block-level child at all and
  establishes an **inline** context, where 10.4's pass declines it instead; both are stated, and
  both are counted.
- **`vertical-align` on a cell** (its contents sit at its block-start edge), `visibility:
  collapse`, `caption-side: inline-start`/`inline-end`, a table **root** box (the tree root
  establishes the initial block context whatever it computes to), replaced elements and
  `aspect-ratio`, `overflow` on a row group, `direction: rtl` column mirroring (19.10) and
  vertical writing modes (19.9).

## 7. What the suite asserts

`Tests/Layout/SuiteRoboLayoutTable.c` (18 cases) fabricates a **typed** computed style per tag
occurrence — including `table-layout`, `border-collapse`, `caption-side` and the carried
`border-spacing` — because the point of the phase is that the pass reads those *values* through
18.5's computed-value doors, and a case built from a stylesheet could only prove the parser.
The container is the `body` and every box is a `div` whose display the suite states, so a
fixture never has to ask the HTML parser what a `<tr>` is. Geometry is read back through the
public result doors.

| Case | Asks |
|---|---|
| `capability` | the flag, the counters record's defined zero, and that the nested record is part of `LayoutBlockStatsEqual` |
| `formatting_context` | `display: table` and `display: inline-table` each own a `RoboLayoutFcTable` context, and a row is neither block- nor inline-level, cannot establish, and is not a block container |
| `rows_and_cells` | `fixed` takes the first row's declared widths, the rows stack, and the container's height is the rows' band |
| `auto_columns` | `auto` shares the container's width over the columns that can still grow, in proportion to their content ceilings |
| `column_box` | a `table-column` box contributes a declared width, generates no geometry, and is counted as declined |
| `overflowing_cell` | a cell wider than its column is counted, not clamped |
| `row_height` | a row is its tallest cell's, and the row's own declared height raises it |
| `row_group` | a group is one rectangle over the rows it holds, its rows in document order |
| `caption_top` / `caption_bottom` | the caption on the block-start and block-end edges, and the table as tall as both together |
| `anonymous_row` | a cell that is a direct child of the table becomes a row of one — twice |
| `internal_outside_table` | a cell with no table is counted (not placed), and so is a child a table cannot use |
| `empty_cells` | `empty-cells`' subject: no in-flow box child, text and emptiness counted the same |
| `border_spacing` / `border_collapse` | the two gutters are geometry, and `collapse` zeroes them |
| `nested` | a table inside a cell, in the cell's own block context |
| `counters_balance` | the two column buckets, the four box kinds, and the block record's identity over every object |
| `allocator_balance` | a pass allocates nothing it does not release |

## 8. What the phase changed in the suites that came before it

Five, and each is a re-pin rather than a new claim:

- `SuiteRoboCssDisplay.c` — the vocabulary walk (18 values), the outer/inner counts (4/6), the
  per-value classification table (an internal box is neither block- nor inline-level) and the
  unknown-keyword list (`table` is a value now).
- `SuiteRoboLayout.c` — the version, the capability table (a `table_layout` row), the domain
  count and the domain ordering (`table` was the newest when this phase landed, `grid` the one
  before it).
- `SuiteRoboLayoutOverflow.c`, `SuiteRoboLayoutPosition.c` — the domain count.
- `SuiteRoboLayoutConformance.c` — the capability table, the "implemented" count, the version
  statement and the seventh context kind.

**The numbers in those three bullets are deliberately not written down here any more.** 19.6
recorded them as `0.22.0`, `27`, `28 of 29` and `table`; 19.7 then moved four of the five
(`0.23.0`, `28`, `29 of 30`, `fragment`), which is the ordinary cost of a phase and not a
defect — but it is a cost this file should stop paying one phase at a time. The suites pin the
numbers where they belong, and this document names *which* suite asserts what rather than
repeating the value that will move.
- `SuiteRoboLayoutFormattingContext.c` — the seventh kind, `LayoutFcKindIsKnown` one enumerator
  further, and ten rows in the can-establish/is-a-block-container table.

## 9. Boundaries

RoboLayout may see RoboCSS and RoboHTML and nothing above them; this phase reads computed values
and adds no RoboPaint, RoboRender or RoboWindow dependency, and `RoboLayoutTable.c` includes
only `RoboLayoutInternal.h`, `RoboLayoutTable.h`, the sizing/text/margin/padding/border/position/
overflow/intrinsic headers it reads through, and RoboCSS's own value headers.
`Runtime/Tools/Build.ps1`'s boundary scan compiles the module and its suites in `-Verify` like
every other source.

## 10. Running the suite

    pwsh -NoProfile -File Scripts/Test/TestLayout.ps1 -Target Host

`TestLayout.ps1` is the gate (471 cases with this suite). A table change is not done until
`TestCss.ps1`, `TestPaint.ps1`, `TestGraphics.ps1`, `TestFont.ps1` and `TestDesktop.ps1`
(`-Target Host`) also pass, and `TestDesktop.ps1 -Target Qemu` where the guest image is
involved.
