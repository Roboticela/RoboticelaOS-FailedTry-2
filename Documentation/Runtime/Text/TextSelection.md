# Text-selection mapping (Phase 12.21)

> **Selection is expressed in source-text coordinates; glyph geometry is derived from shaped visual
> output.**

This is the phase's contract. It is the *bridge* document: the pipeline on one side knows where every glyph
came from, the paint pass on the other knows where every glyph went, and nothing before this phase knew both
at once — which is why a click could not become a caret and a caret could not become a rectangle.

The mapping, both ways, and nothing else:

```text
Logical text ──▶ RoboTextPosition ──▶ cluster ──▶ glyph boundary ──▶ point
Point ──▶ visual run ──▶ inline distance ──▶ cluster ──▶ RoboTextPosition
```

One implementation, one file: `Runtime/Source/RoboText/RoboTextSelection.c` with the public contract
`Runtime/Include/RoboText/RoboTextSelection.h`. It **shapes nothing**: no query in it decodes UTF-8, selects a
font, resolves a script or a level, runs GSUB or GPOS, or measures. Every number it answers with is read off a
`RoboTextGlyphRun` that was shaped before it was asked, plus the one thing a run cannot know — **where the
caller put it**.

---

## 1. Where the code lives

| Piece | File |
|---|---|
| The whole layer: positions, affinity, selections, placements, the map, hit testing, carets, rectangles, the dump and the fuzz entry | `Runtime/Source/RoboText/RoboTextSelection.c`, public `Runtime/Include/RoboText/RoboTextSelection.h` |
| The clusters it maps through | `Runtime/Source/RoboText/RoboTextGlyphs.c` — `RoboTextGlyphRunClusterForScalar`, 12.14's derived record |
| The axes it projects onto | `Runtime/Source/RoboText/RoboTextVertical.c` — `RoboTextWritingAxesInline`/`…Block` (12.20) |
| The runs and the list | `RoboTextRunList.h`, `RoboTextGlyphs.h` (12.14) |
| The capability flags | `RoboTextVersion.h`/`.c` — the `selection*` block, plus `caretGeometry` |

Nothing in this module allocates per query, opens a file, or walks text it was not asked about. There is no
`RoboMem` allocation in it at all: the map owns two arrays, built once by `RoboTextSelectionMapCreate`.

---

## 2. Positions are the source's, never a glyph's

| Vocabulary | Meaning |
|---|---|
| `RoboTextPosition` | one `RoboTextScalarIndex` offset into the **source text** |
| `RoboTextSelection` | `anchor` + `focus`, both positions — direction preserved, never discarded |
| `RoboTextRange` | the normalized half-open range, over the same offsets |
| `RoboTextSelectionDirection` | `Forward` / `Backward` / `None`, from the anchor/focus pair |
| `RoboTextAffinity` | `Upstream` / `Downstream`, which side of a two-location boundary was meant |

`RoboTextPosition` exists as a struct around one integer for the reason the index types are types: a caret
position and a length are the same integer and different ideas, and a door that takes a `RoboTextPosition`
cannot be handed a glyph index by accident. The **end of the text is a legitimate position**
(`offset == scalarCount`), because `RoboTextRange` is half-open and a caret at the end of a line lives there.

The central rule, said as a defect rather than a preference: **a caller holding glyph indices has already lost
the ligature, the mark and the reordered Indic cluster**, because those are exactly the places where glyph
index and source offset stopped agreeing. So no door here takes one.

### 2.1 Affinity

A source offset does not have one screen position. At the boundary two visual runs share — a bidi boundary, a
fallback font change, the end of a wrapped line — there are **two** legitimate carets, and a source offset
*inside* a multi-scalar cluster has **none** that a caller may ask for. Hence:

- `Upstream` hugs the text before the position, `Downstream` the text after it;
- a hit inside a ligature or a conjunct names the cluster's start or its end, chosen by which half was
  clicked, and reports `RoboTextSelectionHit::snapped` so the snap is a **fact** rather than a silence;
- `RoboTextSelectionMapCaret` at a shared boundary answers the run that ends there for `Upstream` and the run
  that begins there for `Downstream`, and the two answers are legitimately different places.

---

## 3. The one thing a caller must supply

A placement is where a run was put:

```c
typedef struct RoboTextSelectionPlacement {
    size_t runIndex;        /* the run's logical index in the list */
    size_t lineIndex;       /* the caller's: this module never groups lines by itself */
    RoboTextSelectionPoint origin;   /* the run's pen origin: its first glyph's baseline point */
    float blockStart, blockEnd;      /* the line band, signed along the **block** axis */
    RoboTextWritingMode writingMode; /* overwritten from the run at map creation */
    uint32_t flags;                  /* RoboTextSelectionPlacementReversed */
} RoboTextSelectionPlacement;
```

Three decisions are worth stating, because each is the result of a real alternative being rejected:

- **`lineIndex` is the caller's.** What a line *is* belongs to the layer that broke it, so two placements with
  one `lineIndex` are one line and a line's placements are ordered the way the caller laid them out — which
  for a bidi line is not their logical order, and is exactly why the two are separate questions here.
- **The band is a pair of signed block-axis distances, not two y coordinates.** `…PlacementForRun` computes it
  as `-ascent` to `+descent` through `RoboTextWritingAxesBlock`, so a vertical run's band is its line's cross
  extent and a horizontal run's is the familiar above/below-baseline pair. A caller that wants a line box's
  leading writes its own two numbers and nothing else changes.
- **`Reversed` is a bit on the placement, not an assumption inside the module.** This build's painter walks
  every run's span in array order, RTL included (`RoboPaintDecoration.c`, counted as `PaintDecorationStats::rtl`),
  so the flag defaults off. The day 12.8's visual order reaches the glyph array, one line of a caller changes
  and every query here is already correct.

A placement for a caller that has a run in hand is one call; the map copies the array, so moving a line does
not require telling the map.

---

## 4. The map

`RoboTextSelectionMapCreate(list, placements, count, options, &map, diag)` borrows the **list** (its runs are
the glyphs, the clusters and the source the whole thing maps between, and a map over a destroyed list maps over
freed memory) and copies the **placements**. It refuses, rather than repairing:

| Problem | Refusal |
|---|---|
| `RoboTextSelectionProblemRun` | a placement names no run of the list |
| `RoboTextSelectionProblemPlacement` | a run of the list is not a valid run |
| `RoboTextSelectionProblemDuplicate` | two placements name one run |
| `RoboTextSelectionProblemOrder` | the placements are not in ascending run order |
| `RoboTextSelectionProblemUnplaced` | a run is not placed at all |
| `RoboTextSelectionProblemBand` | a band that is empty and not a caret's width |

A **subset is refused**: a caller with runs it does not want selectable builds a list without them, which is
the rule the list's own constructor already follows for overlapping runs. `RoboTextSelectionMapValidate`
re-checks the same list, because what a constructor accepts and what a record *is* must not be two rules.

### 4.1 Axes, and why no call site swaps x and y

A run's glyphs lie along its **inline** axis and which physical axis that is depends on its writing mode
(12.20). So this module never writes `point.x - origin.x`: `RoboTextSelectionPlacementProject` is the one place
an x/y pair becomes an (inline, block) pair, through `RoboTextWritingAxesInline`/`…Block`, and everything
downstream — the caret's cross-axis extent, the hit test's inline distance, a rectangle's two corners — is
expressed in that pair and converted back at the end. A vertical column and a horizontal line therefore take
the same path, and `vertical-rl` differs from `vertical-lr` in the block vector and in nothing else.

### 4.2 The run's own direction decides which end is first

The run's **classified direction** decides which way its inline axis is read: an `Rtl` run's glyph array is in
logical order and its pen starts at its *right* edge, so its first cluster sits at the run's largest inline
offset. It is the run's own direction and not the paragraph's, which is why a run of digits inside an Arabic
paragraph is not mirrored — the fact 12.7 already established and this layer consumes.

---

## 5. The three queries

| Query | Direction | Answer |
|---|---|---|
| `RoboTextSelectionMapHitTest` | point → logical position | a `RoboTextSelectionHit`: position, affinity, run, line, cluster start, `snapped`, signed inline distance |
| `RoboTextSelectionMapCaret` | position + affinity → geometry | a `RoboTextCaretGeometry` across the block axis |
| `RoboTextSelectionMapGeometry` | selection → rectangles | a `RoboTextSelectionGeometry`, one rect per (line, run) segment |

`distance` is **signed along the inline axis**: negative before the boundary the position names, positive
after it, so a caller can tell "the point was past the end of this line" from "before its start" without
knowing which axis the line ran on.

An unplaceable position is **not an error**: `RoboTextCaretGeometry::valid` is 0 with the spans zero, so a
caller has one thing to check rather than five. A NULL argument, on the other hand, is `RoboInvalidArgument`.

### 5.1 Caret orientation comes from the mode, not the script

A caret is drawn **across** the block axis, so a horizontal line's caret is vertical and a vertical column's is
horizontal. `RoboTextCaretGeometryIsHorizontal` reads the **segment** (`SpanX > SpanY`) rather than the mode's
name, which is the point of the door: a caller that assumed "horizontal mode, horizontal caret" would draw a
one-thousandth-pixel line across the text instead of through it. The mode itself is on the record, and it is
the answering run's own — a Latin letter inside a Japanese column is sideways and the caret beside it is
horizontal while the ideograph's is vertical, because the mode is data.

### 5.2 Rectangles, not a box

A logical range that crosses a bidi boundary is **several** places on the page, and a single bounding box
around them would cover text the selection does not contain. So the answer is a list:

- one rectangle per (line, run) segment the range covers, in **visual** order — by line, then by the order the
  caller laid that line's placements out;
- within one run the covered clusters coalesce into **one** segment, which is what keeps zero-advance marks
  from producing a rectangle each: a base and its mark are one place on the page;
- a selection of nothing but zero-advance clusters is widened to the caret's thickness rather than emitted as
  an invisible rectangle — a selection a reader cannot see is the one failure this layer must not produce;
- the rectangle is *(inline × block)*, so its width is the inline extent in a horizontal line and its **height**
  is the inline extent in a column;
- `bounds` exists as a convenience for a caller that wants a rough box to scroll into view, and is explicitly
  never the answer to "what is selected".

`RoboTextMaxSelectionRects` (64) caps the list, and the cap is **stated and reported**: the geometry's
`truncated` says when a caller is looking at a partial answer, and the call answers `RoboNotSupported` with the
rectangles that fit.

### 5.3 Lines, whitespace and the options

`RoboTextSelectionOptions` resolves what the inputs leave open: the caret's thickness (which is also the width
a zero-extent selection is given), whether a point past a line's end may name its trailing whitespace, and
whether a point off every line's band resolves to the nearest one. The whitespace rule is implemented on the
**scalars**, through `RoboTextUnicodeIsWhitespace`, and never on advances: a space's width is the font's
business, and a tab's logical position is independent of whatever width a layout gave it.

---

## 6. What the layer rests on

- **Clusters** (12.14) are authoritative for the mapping. A ligature, an Indic conjunct, an Arabic joining
  cluster and a base with its marks are each a source range over a glyph range, and a hit resolves to a cluster
  boundary rather than to a place inside one — so the layer **cannot** invent a glyph-space selection that the
  source text cannot express. `RoboTextSelectionHit::clusterStart` and `…::snapped` are how a caller sees it.
- **A font boundary is not a logical boundary.** A range spanning a fallback change produces one rectangle per
  run, and every position under it is still a position in the one text: `text.selection.fallback`.
- **No reshaping.** The map is built once from the finalized list; a query is a walk over that list's clusters.
  Nothing is decoded, selected, shaped or measured at query time — Phase 12.21.43's rule, and the reason a hit
  test over a paragraph costs no shaping.
- **Incremental updates are prepared for, not built.** The map is per-list, so a document's granularity is its
  runs: an edit invalidates the lists it touched. Nothing here caches across lists or holds a global index.

---

## 7. What this build does not claim

Named rather than implied, because each is a row of this phase list:

- **Editing** — insertion, deletion, replacement, undo. 12.22's hooks; this layer is the mapping they need.
- **Caret navigation policy** — word, grapheme and line movement, shift-extension, `Home`/`End`.
- **IME, clipboard, mouse and gesture handling**, and CSS `user-select`.
- **Full caret subdivision inside a ligature.** A ligature is one geometry region for the whole range that
  selected it; a sub-ligature caret would need the font to state how the glyph divides, which 12.21.9 puts
  later rather than guessing.
- **Selection painting in the shell.** `RoboTextSelectionGeometry` is what a painter consumes; the desktop's
  own selection overlay and its design tokens are the shell's row, and the spec's `SelectionPaintCommand` is
  deliberately **not** in this module — the correct order (selection background, then glyph runs) belongs to
  the pass that owns the display list, and this layer changes no glyph colour. The one place the geometry *is*
  filled is the guest checkpoint (§9.1), which is how the rectangles are proved paintable rather than
  plausible.
- **A `SelectionMap()` accessor on RoboLayout's line boxes.** Every case supplies its own placements, which is
  exactly the contract: the one thing this module cannot read off a run is where the caller put it. Binding it
  to a real line box is the layout row that consumes these results.
- **Grapheme segmentation** (UAX #29). Cluster boundaries are 12.14's rule — a combining mark continues the
  cluster before it — which is correct for the scripts this build shapes and is named as the approximation it
  is.
- **Per-phase build scripts.** The repo's convention since 12.19 is one `BuildRoboText.ps1`/`TestRoboText.ps1`
  per module, not one per phase; the spec's `BuildRoboTextSelection.ps1` would be a second way to build the
  same module.

---

## 8. Diagnostics

Complete-or-absent, like every dump in this module: a line that does not fit leaves the buffer empty and
reports the length a complete one needs either way.

| Door | Says |
|---|---|
| `RoboTextPositionWrite` | `"12"`, `"0 (start)"`, `"8 (end)"` |
| `RoboTextSelectionWrite` | `"3..9 forward"` |
| `RoboTextCaretGeometryWrite` | `"vertical (100.00, 200.00)-(100.00, 216.00) w=1.00 horizontal-tb valid"` |
| `RoboTextSelectionHitWrite` | `"position 7 downstream (run 1, line 0, cluster 6, snapped, +2.50)"` |
| `RoboTextSelectionMapWrite` | a header, a line per placement, then each run's clusters as `source → glyphs` |

`RoboTextSelectionMapWrite` is Phase 12.21.34's debug overlay in its honest form, and its last section is the
one that matters: a reader can see `source → glyphs` per cluster, which is the mapping the whole phase is
about, and can see *which* of a line's three runs is the right-to-left one.

`RoboTextSelectionFuzz` throws arbitrary bytes at every query with one invariant: no crash, no out-of-range
access, no invalid position, no infinite loop. A zero-length call is valid; a NULL pointer with a non-zero size
is `RoboInvalidArgument`, exactly as `RoboTextFuzzGlyphs` reports it.

---

## 9. Tests

`Tests/Text/SuiteRoboTextSelection.c` — thirteen cases, all `text.selection.*`, over the **12.18 conformance
fixture** (`Tests/Text/RoboTextConformanceFont.h`): Latin with `liga`, zero-advance combining marks, an `AV`
kern pair, the complex fixture's Arabic, and `ConfSpecVertical`'s column with `vmtx` advances.

| Case | What it decides |
|---|---|
| `positions` | the vocabulary: anchor/focus survives normalization, clamping, affinity's involution, the caret's segment reading, the dumps |
| `map` | creation's refusals, the accessors, the empty map, the options as data |
| `hit` | point → position, the signed distance, the nearest-line rule, the trailing-whitespace option |
| `caret` | the band from the run's own metrics, the text's two ends, an unplaceable position, the dump |
| `ligature` | a hit inside `ffi` snaps to the cluster, both halves of it, and the range selects the whole glyph — never a partial one |
| `marks` | a base and its zero-advance mark are **one** rectangle, and a selection of the mark alone is caret-wide rather than invisible |
| `rtl` | an Arabic run's first cluster is at its **largest** inline offset; a Latin run's is at zero |
| `bidi` | two runs sharing a boundary answer two carets, one line apart, and the same text wrapped gives one rectangle per line |
| `vertical` | the same three queries on the other axis: the inline extent is `advanceY`, the block extent is the band, and the horizontal run states the swap |
| `fallback` | a font boundary is not a logical boundary — two faces, one continuous selection, positions still in the one text |
| `roundtrip` | position → caret → hit → position, and range → geometry → boundary hits → range, on safe cluster boundaries |
| `diagnostics` | the dump's shape, complete-or-absent |
| `fuzz` | every query from arbitrary bytes |

Run with `TestRoboText.ps1 -Target Host`. A selection change is not done until the boundary suites that
consume shaped text pass with it: `TestLayout`, `TestPaint`, `TestGraphics`, `TestFont`, `TestDesktop` and
`TestRoboTextConformance` — all Host.

### 9.1 The guest half

The document path carries no selection at all — there is no mouse, no focus and no editor — so the guest
checkpoint is a **second off-screen render**, like 12.18's and 12.20's: `User/robo-runtime/main.c`'s
`GuestSelectionCheckpoint` shapes `"ABC"` with RoboText directly, builds a map over it, asks the three
questions, and **fills the rectangles it got back** into a guest surface. Pinned as `ROBO_QEMU_SELECTION_*` in
`Scripts/pipeline/commands.test.ps1` (`robo test-qemu-display`):

```text
HIT_POSITION=1  HIT_AFFINITY=0 (upstream)        ← the nearer boundary, never a glyph index
CARET_COLUMN_HORIZONTAL=1  CARET_LINE_HORIZONTAL=0   ← the same position, both modes
CARET_SPAN=0x0000BD0E  BLOCK_ONE=0x0000BD0E     ← a caret is as long as the band it sits in
BLOCK_ONE=0x0000BD0E   BLOCK_ALL=0x0000BD0E     ← one glyph and three cover the same band
INLINE_ALL=0x0001D4C0 (120.000 px)               ← three cells of the run's own advanceY
EQUAL_BLOCKS=1                                   ← the whole statement as one flag
INK=0x000016F8  INK_BOX=0x00310078 (49×120)      ← the rectangles, as pixels
```

`CARET_SPAN == BLOCK_ONE` and `BLOCK_ONE == BLOCK_ALL` are the two claims a host PNG cannot make: the caret's
length is the line's band rather than a constant, and a selection's cross extent is the band rather than a
function of how many glyphs it covers. A rectangle built from a glyph count fails `EQUAL_BLOCKS`; a caret
drawn from a fixed length fails `CARET_SPAN`.

---

## 10. Capabilities

12.21 sets, field by field, in `RoboTextVersion.h`/`.c` and read one by one in
`Tests/Text/SuiteRoboTextConformance.c`: `selectionMapping`, `selectionPositions`, `selectionClusters`,
`selectionAffinity`, `selectionHitTesting`, `caretGeometry`, `selectionGeometry`, `selectionWritingAxes`,
`selectionValidation`, `selectionDiagnostics`, `selectionFuzzing`. The module moves to **`1.12.0`**.

There is deliberately **no** `selectionPainting` and no `selectionShaping` flag: the first is the shell's row
and the second is a claim this module's whole design denies — it consumes shaped output and produces nothing
that any later stage has to shape.
