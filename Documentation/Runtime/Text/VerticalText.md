# Vertical writing (Phase 12.20)

> **RoboText owns writing-mode-aware shaping and glyph positioning; the renderer only paints the resulting
> `GlyphRun`.**

This is the phase's contract. It is short because the phase adds **no engine**: a vertical run is the same
`RoboTextGlyphRun` a horizontal run is, positioned on the other axis, with a per-glyph orientation the shaper
resolved. Everything below is the *decision* half of the rule above — which axis, which orientation, which
feature — and the paint half is three lines of accumulation.

The vertical pipeline:

```text
UTF-8
  ↓
Unicode
  ↓
Script
  ↓
Bidi                     (12.8 — unchanged, and still the only visual ordering)
  ↓
Font selection           (12.17 — unchanged)
  ↓
Writing mode             ← this phase
  ↓
Text orientation         ← this phase
  ↓
cmap → vert / vrt2       (12.10's GSUB, asked for by the writing mode)
  ↓
GPOS                     (12.11's engine, unchanged; a vertical run adds its own advances on top)
  ↓
GlyphRun                 (12.14 — the same record, with advanceY and an orientation)
  ↓
Measurement              (12.15 — the same engine, over the same list)
  ↓
TextLayout
  ↓
DrawGlyphRun             (12.16's pass, whose pen accumulates both axes)
  ↓
Rasterization
```

---

## 1. Where the code lives

| Piece | File |
|---|---|
| The writing mode, the axes, the orientation tables, the `vert`/`vrt2` hook, the dump | `Runtime/Source/RoboText/RoboTextVertical.c`, public `Runtime/Include/RoboText/RoboTextVertical.h` |
| The vocabulary: `RoboTextWritingMode`, `RoboTextTextOrientation`, `RoboTextGlyphOrientation`, and their `IsKnown`/`Name` doors | `Runtime/Include/RoboText/RoboTextTypes.h`, `Runtime/Source/RoboText/RoboTextTypes.c` |
| The fields on the shape options, the text run and the glyph run | the same `RoboTextTypes.h`, `RoboTextRuns.h`, `RoboTextGlyphs.h` |
| The vertical font metrics door (`vhea`/`vmtx`, the origin) | `Runtime/Source/RoboText/RoboTextFont.c` — public `RoboTextFont.h` |
| The **placement**: the vertical twin of the `hmtx` line | `Runtime/Source/RoboText/RoboTextGlyphs.c`, `GlyphApplyVerticalMetrics` |
| The cut: orientation is a run boundary | `Runtime/Source/RoboText/RoboTextRuns.c` |
| The key: writing mode and orientation make two runs different | `Runtime/Source/RoboText/RoboTextRunList.c`, `RoboTextGlyphs.h`'s `RoboTextGlyphRunKeyField` |
| The measurement's `advanceY` | `Runtime/Source/RoboText/RoboTextMeasurement.c` (12.15's engine, reading 12.14's totals) |
| The pen | `Runtime/Source/RoboPaint/RoboPaintDecoration.c` |

Nothing here parses a font: `vhea`/`vmtx` are RoboFont's (`FontVheaParse`, `FontMetricsInfoGlyphVertical`),
and this module only *consumes* the normalised metrics. Nothing here parses CSS: `writing-mode` and
`text-orientation` are §8's prepared styles, not a stylesheet reader.

---

## 2. Writing mode is data, never inferred

`RoboTextWritingMode` is a field on `RoboTextShapeOptions` and on every run:

```text
horizontal-tb        inline (+1, 0)   block  (0, +1)
vertical-rl          inline (0, +1)   block (-1, 0)
vertical-lr          inline (0, +1)   block (+1, 0)
```

The four combinations the phase names — Japanese horizontal, Japanese vertical, Latin vertical, Arabic
vertical — are four `RoboTextShapeOptions`, and each is reachable. **A script never selects a mode.** A text
layer that looked at the characters to decide could not express the other setting of the same text, which is
exactly the failure this field exists to prevent.

### 2.1 Direction and writing mode are different questions

| | question | values | resolved by |
|---|---|---|---|
| `RoboTextDirection` | the logical **inline** direction | `ltr` / `rtl` | 12.8's bidi algorithm |
| `RoboTextWritingMode` | the physical **axis** | horizontal / vertical-rl / vertical-lr | the caller |

They compose rather than replace each other. A vertical run still carries its bidi level and its resolved
direction, and 12.8's `visualOrder` is still the only visual ordering. **Nothing in this phase reorders
anything** — §5 says how that is proved.

### 2.2 Axes are explicit

`RoboTextWritingAxes` is the inline/block pair as two unit vectors, and two doors convert a scalar into a
physical vector:

```c
RoboTextWritingAxes RoboTextWritingModeAxes(RoboTextWritingMode mode);
Result RoboTextWritingAxesInline(const RoboTextWritingAxes *axes, float value, float *outX, float *outY);
Result RoboTextWritingAxesBlock (const RoboTextWritingAxes *axes, float value, float *outX, float *outY);
```

A layout pass that wrote `if (vertical) { swap x and y }` gets `vertical-rl` right and `vertical-lr` wrong,
because the two differ in their **block** axis and in nothing else. Naming both axes makes the two vertical
modes one table row each. An unknown mode answers the **horizontal** pair rather than a zero vector: a zero
axis would collapse every coordinate a layout produced, which reads as a layout bug rather than as a bad mode.

---

## 3. Orientation — upright or sideways

`RoboTextGlyphOrientation` is the per-glyph answer: a glyph stands **upright** (drawn at its own vertical
form, used by CJK) or **sideways** (rotated a quarter turn, used by Latin inside a Japanese column).
`RoboTextGlyphOrientationRotation` is the angle — `0` for upright, `π/2` clockwise for sideways — and it is
the *whole* of what a painter needs, because a painter that also chose the angle would be deciding
orientation, which is this layer's job.

### 3.1 Three tables, in one order

`RoboTextVerticalOrientationOf(codePoint, script, requested)` answers `Mixed` by asking **three** tables, in
this order:

1. **`RoboTextVerticalCodePointIsRotated`** — U+FF61–U+FFDC (halfwidth Katakana) and U+FFE8–U+FFEE. Asked
   **first**, because their *script* is Katakana or Hangul, which would otherwise stand them up. UAX #50
   rotates them, and a Japanese column of `ｶ` must be a rotated `ｶ`.
2. **`RoboTextVerticalScriptIsUpright`** — one row per script: Han, Hiragana, Katakana, Bopomofo, Hangul and
   Yi stand up; every other script lies on its side. `Common` and `Inherited` are **false** here, because they
   name no script of their own.
3. **`RoboTextVerticalCodePointIsUpright`** — the code-point ranges for the characters whose script is
   `Common`: CJK Symbols and Punctuation (U+3000–U+303F), Bopomofo, CJK Strokes, the enclosed and compatibility
   blocks, Hangul Syllables, CJK Compatibility Ideographs, the Vertical Forms block (U+FE10–U+FE1F), CJK
   Compatibility Forms, fullwidth forms, Enclosed Ideographic Supplement and the CJK ideograph extensions.

A **forced** orientation (`upright` or `sideways` in `RoboTextShapeOptions::textOrientation`) overrides all
three, in both directions.

### 3.2 What this is not

The classification is **this module's own table plus RoboUnicode's script property**, and it is an
approximation of UAX #50's `Vertical_Orientation`, not that property: RoboUnicode does not carry
`Vertical_Orientation` yet, and inventing a second Unicode table inside RoboText would violate the boundary
Phase 12.5 set. When RoboUnicode gains the property, `RoboTextVerticalScriptIsUpright` and the two code-point
tables become a read of it and the doors here do not change.

### 3.3 Whitespace takes its neighbour

`RoboTextVerticalOrientationInContext(codePoint, script, preceding, requested)` is the door segmentation
uses, and it differs from the per-character one in exactly one case: **whitespace takes the orientation of the
scalars before it**. A space between two ideographs therefore stays upright rather than opening a one-scalar
sideways run — which is what CSS Writing Modes specifies for `text-orientation: mixed`, and what every reader
of a Japanese column expects. At the start of a paragraph the preceding orientation is upright. A *forced*
orientation still wins: a caller that asked for `sideways` gets a sideways space, because the space is not
what the request was about.

---

## 4. The cut — orientation is a run boundary

The run set cuts a text run where **script**, **resolved direction**, the effective **features**, or the
**resolved orientation** changes (12.20 adds the fourth). So

```text
一 A B 丁
```

is three runs — Han upright, Latin sideways, Han upright — and a painter reads **one orientation per run**
rather than inspecting code points. The resolved orientation travels on the run
(`RoboTextRun::orientation`) and through to the glyph run
(`RoboTextGlyphRunOrientation`), where it is also part of `RoboTextGlyphRunKey`: two runs that agree on
everything else and disagree on orientation are **different runs** and are never coalesced.

A horizontal run's resolved orientation is `Upright` — which is what its glyphs are, not a claim that
anything asked. The per-scalar orientation array is allocated only for a vertical mode, because cutting a
horizontal text on an axis nothing reads would change every run set this build has produced.

---

## 5. OpenType — `vert` and `vrt2`

Vertical OpenType integration is **two tags**:

```c
Result RoboTextWritingModeApplyFeatures(RoboTextWritingMode mode, RoboTextFeatureSet *features);
```

A vertical mode names `vert` and `vrt2` **On** in the run set's feature set; a horizontal mode writes nothing.
That is the entire integration: the tags reach 12.10's GSUB through the one door every feature uses, and no
engine learns that vertical text exists. There is no vertical substitution table, no vertical shaping pass and
no second GSUB.

**Both tags**, because they are two spellings of one job: `vert` is the original and `vrt2` its modern
replacement, faces carry one or the other, and enabling both lets the font's own FeatureList decide which
exists. A feature a face does not carry resolves to nothing, so naming both is not "enabling two features" in
any way a caller can observe. A tag the **caller** already named is left alone — the same "specific beats
default" rule the ligature and kerning policies follow — so `"vrt2" off` means what it says.

Because the request comes from the writing mode and not from the script, a `Common` character gets its
vertical form too: a CJK comma in a vertical column reaches the fixture's `vert` rule exactly as an ideograph
does.

---

## 6. Metrics — the advance, the origin, and the synthesised case

### 6.1 The advance

```text
horizontal:  glyph → hmtx xAdvance
vertical:    glyph → vmtx advanceHeight
```

`RoboTextFontGlyphVerticalMetrics` reads `vhea`/`vmtx` through RoboFont (which parses them once, lazily, like
every other table) and reports `advanceHeightPx`, `originXPx`, `originYPx`, `topSideBearing`, `fromFace` and
`advanceSynthesised`. A vertical run's glyph is then

```c
glyph->advanceX = 0;
glyph->advanceY = vertical.advanceHeightPx + shaping->advanceAdjustY;
glyph->offsetX  = -vertical.originXPx + shaping->offsetAdjustX;
glyph->offsetY  = -vertical.originYPx + shaping->offsetAdjustY;
```

one helper, `GlyphApplyVerticalMetrics`, beside the `hmtx` line in `RoboTextGlyphs.c`, used by both the map
pass and the substitution rebuild — so the face's vertical answer is applied once, in one function, and
12.11's GPOS is added on top through exactly the two lines it already adds for `hmtx`.

**The advance is the resulting glyph's.** A `vert` substitution changes the id, and the rebuild asks the face
for the *substitute's* `vmtx` entry, the same rule 12.10 established for ligatures. A vertical advance is never
assumed to be the font size: it is the `vmtx` entry or the specification's own fallback.

### 6.2 The origin

`vertOriginX` centres the glyph on the column (`horiBearingX + horiAdvance / 2` in units, which is what the
OpenType specification states); `vertOriginY` is measured **upward from the horizontal baseline**, and a `glyf`
face states it as `yMax + topSideBearing`. Drawing the glyph at the pen therefore means translating by the
**negation** of both:

```text
offsetX = -vertOriginX     offsetY = -vertOriginY
```

**Two negations, and they are the whole of "vertical origin".** Folding them into the placement offset here —
once, in one function — is what the phase's "do not allow the renderer to independently reinterpret vertical
origins" asks for: a painter applies a glyph's offset and knows nothing about `vmtx`, `vhea` or a vertical
origin at all.

A face with no outline box (a CFF face, and every fixture built without `glyf`) takes the `vmtx` top side
bearing as its origin with `originFromOutline` **clear**, which is stated rather than passed off. There is
**no `VORG` reader**; the specification's own derivation covers the faces this build ships and tests.

### 6.4 What an attachment is placed against in a vertical run

A mark or a cursive chain is positioned relative to another glyph's **pen**, and the pen's step between the two
is the one quantity GPOS does not carry. So the engine asks the face — and in a vertical run it asks `vmtx`,
not `hmtx`: the step is the run's **inline advance**, which `RoboTextWritingAxesInline` maps to `+y` for the two
vertical modes and to `+x` horizontally.

```text
distance  = (writing mode vertical ? vmtx advanceHeightPx + YAdvance : hmtx + XAdvance)
placement = baseAnchor − (inline(distance) + markAnchor) + base's own placement
```

Both anchors stay in the glyph's own space (`x` right, `y` up) — the space the painter composes a run in — so
only the *distance* is turned through the axes and the cross-axis difference is a plain subtraction. Written
that way the horizontal arithmetic is bit-for-bit what it was before the mode existed, and a vertical mark
compensates the vertical pen instead of subtracting an `hmtx` number the vertical pen never moved on.
`RoboTextGposOptions::writingMode` is how the mode reaches the engine; it is the run's own field, so no call
site passes an axis.

`text.vertical.mark` is the proof, and it is written to be decisive: the fixture's one `mark` rule anchors a
voiced sound mark on its Hiragana letter, and the case reads the same attachment in **both** modes against one
face — 1006 font units of vertical step against 406 horizontal, two different placements from one set of
anchors. Forcing the horizontal reader in a vertical run moves the mark by exactly the difference (≈9.6 px at
16 px), which is how the case was checked rather than assumed.

### 6.3 A face with no vertical metrics is shaped, and counted

A face with no `vmtx` is **not** refused. The advance is synthesised — the em box, or `vhea`'s own
ascent + descent + lineGap when the face carries a `vhea` — and `RoboTextGlyphRunVerticalSynthesised` counts
the glyphs that took that path. Zero for a horizontal run and for a vertical run over a face with vertical
metrics; non-zero is not a defect (it is the specification's own answer for such a face) but it is the one
number that tells a caller whether the advances it is holding were **measured or derived**.

---

## 7. Measurement

12.15's engine is unchanged: `RoboTextMeasureGlyphRunList` sums the runs' own totals, and
`run->advanceTotalY` was already a field. So

```text
measurement.width     = Σ run advanceX        (0 for a pure vertical run)
measurement.advanceY  = Σ run advanceY        (0 for a horizontal run)
```

Both are published, both are the runs' own numbers, and neither is a fake: a vertical measurement is **not** a
rotated horizontal rectangle, and `width != advanceY` for a vertical run is a fact the record states.
`RoboTextMeasureValidate`'s invariants are unchanged, so a vertical measurement validates by the same door.

---

## 8. The paint half

`RoboPaintDecoration.c`'s text pass receives each glyph's `advanceX`/`advanceY`/`offsetX`/`offsetY` on the
display list (12.16's `PaintGlyph`, copied by the recorder). Its pen is a **2D** accumulation:

```c
draw.origin = MakeGfxPointF(penX + glyph->offsetX, baselineY + penY + glyph->offsetY);
penX += glyph->advanceX;
penY += glyph->advanceY;
```

There is **no branch on a writing mode**, and that is the point: a run's `advanceX`/`advanceY` pair *is* the
writing mode, already resolved by RoboText. For horizontal text `advanceY` is zero, so `penY` never moves and
the arithmetic is bit-for-bit what the single-axis pen did before; for vertical text `advanceX` is zero, so
`penX` never moves and the column descends as the shaper's advances say it does. The pass cannot learn a script,
a writing mode or a vertical origin from those numbers.

### 8.1 Sideways is a mark on the glyph, not a mode of the pass

A column of Latin is *turned*, and that is the one thing the advances cannot say — they are the shaper's answer
about the **pen**, and the turn is its answer about the **ink**. So the answer travels as a bit:

```text
PaintGlyphFlagSideways      the recorder sets it from RoboTextGlyphRunOrientation(run)
draw.rotation = Clockwise90 the pen composes it for the rasterizer, and nothing else changes
```

- It is a **glyph** flag rather than a run field, and that is a correction the first cut of this file's own
  reasoning needed: one paint record can hold several shaped runs (a line of `一 A B 丁` is three, with two
  orientations), so a record-level answer would be wrong for one of them. The characters live in the record's
  span and the presentation lives on the glyph — the same shape 12.16 already gave the cluster and the source.
- The recorder writes it from `RoboTextGlyphRunOrientation`, so the decision stays in the text layer; the pen
  reads the bit and passes an angle. No code point is inspected anywhere in the drawing pass.
- `PaintGlyphFlagSideways` is part of the record the display-list builder **copies**, so a replay draws a
turned glyph from the list alone — the list still needs no shape result, no face and no text layer.

The angle reaches the rasterizer as `GfxGlyphRotation` on `GfxGlyphRasterParams` (0–3 quarter turns clockwise,
`GfxGlyphDrawInit` defaulting to none), and a quarter turn is **exact** rather than a resampled bitmap: the
transform is applied where a font-unit point becomes a device point, so an outline's control points are turned
*before* flattening and the mask that comes out is the turned glyph, with the rectangle to match. Zero quarter
turns is bit-for-bit the pre-12.20 path, which is what keeps every horizontal glyph in this build
byte-identical.

The turn is proved in three places, each where it can be: `graphics.glyph.rotation` on the rasterizer's own
lattice, `graphics.typography.vertical_specimen` on the shipped face with a PNG a person can look at, and —
the whole chain inside the OS — `User/robo-runtime/main.c`'s vertical checkpoint, whose two ink counts for one
glyph upright and turned are **equal** (314 pixels) in boxes of 26×30 and 30×26.

### 8.2 What is deliberately not built here

`ComputedWritingStyle` (§34) is not added: nothing in the tree would consume it, and a struct with no reader is a
claim rather than preparation. `writing-mode` / `text-orientation` parsing is a stylesheet phase's work. Vertical
**block progression** — a line box that descends, a second column beside the first, vertical line breaking and
vertical justification — is on the phase's own non-goals list, so the DesktopShell records horizontal runs and
the pen is ready for a vertical one the moment a layout pass hands it one.

---

## 9. Diagnostics

`RoboTextWritingModeWrite(mode, orientation, buffer, capacity, outLength)` is a one-line dump —
`"vertical-rl mixed"` — **complete or absent** like every dump in this module: a setting that does not fit
leaves the buffer as the empty string and reports the length a complete line needs either way. The vocabulary
it prints comes from the shared `RoboTextWritingModeName` / `RoboTextTextOrientationName` /
`RoboTextGlyphOrientationName` doors, so a run's mode, its requested orientation and its resolved orientation
are one set of spellings everywhere they appear.

---

## 10. What this build does not claim

Each of these is a **named absence**, not an oversight:

| Not claimed | Why, and what it would take |
|---|---|
| CSS `writing-mode` / `text-orientation` parsing | A stylesheet phase's work; the value types and the shape-option fields are here for it. |
| `ComputedWritingStyle` (§34) | Nothing consumes it yet; a struct with no reader is a claim, not preparation. |
| Vertical block progression, vertical line breaking, vertical justification | The phase's own non-goals. The pen is ready; the line box is not. |
| Ruby layout | A later goal. |
| Glyph rasterization at an angle in **this** module | The angle is a number here (`RoboTextGlyphOrientationRotation`) and a transform one layer down (`GfxGlyphRotation`, §8.1). RoboText links no rasterizer by its own boundary on purpose: the turn is proved in `graphics.glyph.rotation` and `graphics.typography.vertical_specimen`, which is where the pixels are. |
| `Vertical_Orientation` from RoboUnicode | §3.2: the tables here approximate UAX #50 until RoboUnicode carries the property. |
| `VORG` | §6.2: the specification's derivation covers the faces this build ships. |
| Cluster-grained or per-column font fallback | 12.19's named gap; a vertical run inherits whatever face selection gave it. |
| A **vertical cursive chain** | §6.4 states the distance on the inline axis and no cursive vertical face exists to prove it (Mongolian is not in the pack or the fixtures). A horizontal chain is `text.arabic.cursive`. |
| `vrt2` as a separate reader | The fixture carries `vert` alone; both tags are named and the font's FeatureList decides, which §5 states. A face that distinguishes them would be a fixture. |
| A **guest document** in a vertical writing mode | The shell and the document path carry no writing mode at all (the row above), so there is no CSS a guest page could state it with. What the guest *can* show, it shows: `User/robo-runtime/main.c`'s `GuestVerticalCheckpoint` is a second off-screen render that shapes `"ABC"` with RoboText directly — `vertical-rl` + `sideways`, eight markers, pinned in `Scripts/pipeline/commands.test.ps1` — and rasterises the run through the same `GfxGlyphDraw` bridge the paint pass calls. Its two verdicts are the pen's (`advanceY` 120.000 px of three em boxes, `advanceX` **0**) and the turn's (the same glyph upright and turned ink **314 pixels each** — exactly equal, because the outline is turned in the 1/256 px lattice — in boxes of 26×30 and 30×26). |
| A guest page with **upright** glyphs in a column | The system pack ships one family (Inter), which carries neither CJK ideographs nor `vert`; a guest page could only render `.notdef`, so a "visual regression" would compare two empty columns. The upright half is proved over the conformance fixture's own face on the host (`text.vertical.*`, `ConfSpecVertical`), and the sideways half — the one the pack *can* express — is the guest render above and `graphics.typography.vertical_specimen`'s six UI sizes → `Build/Typography-Vertical.png`. |

The **capabilities** record what is true: `verticalText`, `writingAxes`, `verticalOrientation`,
`verticalMetrics`, `verticalFeatures` and `verticalDiagnostics` are `1`, and the module is at **`1.11.0`**.
There is deliberately **no `verticalShaper`** flag — the phase adds no engine, and a flag claiming one would be
the over-claim that record exists to avoid.

---

## 11. Tests

`Tests/Text/SuiteRoboTextVertical.c` — 14 cases, `text.vertical.*`, over the built vertical fixture
(`ConfSpecVertical` in `Tests/Text/RoboTextConformanceFont.h`), via `TestRoboText.ps1 -Target Host`:

| Case | What it pins |
|---|---|
| `text.vertical.writing_axes` | The three modes' inline/block pairs, both conversion doors, and the unknown mode's horizontal default. |
| `text.vertical.orientation` | The script table, both code-point tables, the **rotated table beating the script**, forced orientations, and the two rotation values. |
| `text.vertical.orientation_context` | Whitespace taking its neighbour; a space between ideographs staying upright; a forced orientation beating the rule. |
| `text.vertical.run_split` | Orientation is a cut: Han / Latin / Han as three runs with three orientations, forced `sideways` leaving three script runs, and halfwidth Katakana sideways. |
| `text.vertical.vert_feature` | `vert` substituting through 12.10's engine in a vertical mode and **not** in a horizontal one; a `Common` character substituted; a character with no rule left alone. |
| `text.vertical.advance` | `advanceX == 0`, `advanceY` the **substitute's** `vmtx` advance, the run total the sum of the glyphs', and the horizontal case unchanged. |
| `text.vertical.origin` | `offsetY` the negated origin, `offsetX` the centring, and a horizontal glyph carrying neither. |
| `text.vertical.synthesised` | A face with no `vmtx`: every glyph counted, the em-box advance, and the substitutions still running. |
| `text.vertical.mark` | The `mark` attachment in **both** modes: the anchor difference minus the *inline* step, the mark advancing nothing on either axis, the base unmoved, and the two distances one face states. |
| `text.vertical.fallback` | A face chosen by the **chain** answers a Han run whole (`runs == scriptRuns`, `fallbackRuns == 1`, the run naming that face) and is shaped with *its* `vmtx` advance and *its* `vert` product, because the writing mode is a field of the shape rather than of the face. |
| `text.vertical.measurement` | `width == Σ run advanceX`, `advanceY == Σ run advanceY`, `width == 0` for a vertical run, and validation. |
| `text.vertical.bidi` | An Arabic run in a vertical column still classified RTL with a level; a mixed Latin/Arabic/Latin paragraph stays three runs in logical order. |
| `text.vertical.diagnostics` | The dump, its complete-or-absent rule, and every name. |
| `text.vertical.determinism` | Two shapings of one text byte-for-byte equal, both axes and the orientation included. |

**The fixture states both advances.** Every glyph's horizontal advance is `400 + id` and its vertical one is
`1000 + id`, and that difference is what makes the advance cases mean anything: a build that quietly reused
`hmtx` for `advanceY` would agree with a fixture that derived one from the other and disagree with this one.
The fixture also carries a `vhea`/`vmtx` pair, a `vert` feature substituting two upright forms, a `mark`
attachment whose two anchors put a voiced sound mark on its Hiragana letter, and a `cmap` spanning Han,
Hiragana, Latin, a digit, **Arabic**, CJK punctuation and halfwidth Katakana — five scripts in one small face,
with every glyph id **derived from the run array** rather than written out, so adding a run cannot silently
move a suite's expectations.

**And one exception to `400 + id`/`1000 + id`:** the combining mark is zero on *both* axes. That is what makes
the mark case a statement about the bytes rather than about rounding — a column with a mark in it is exactly as
long as its letter alone — and a mark that inherited `1000 + id` would have made the case vacuous. The GPOS
table exists because of that one rule, which is also why the plain `vert_feature` case now reads the
generation mode `Gpos`: the furthest stage that ran is positioning, over substitution's product.

The two halves of the **seam** are proved outside this module, in the suites that own the pixels:

| Case | What it pins |
|---|---|
| `graphics.glyph.rotation` | The rasterizer's quarter turn, exactly: the mask rectangle for 0/90/180/270°, a covered area that does not change under a turn, the coverage at `(x, y)` equal to the unrotated coverage at the position the turn maps it from, and an enumerator outside 0..3 defaulting to **no** turn rather than wrapping. |
| `graphics.typography.vertical_specimen` | The shipped face sideways at six UI sizes through the bridge: `L` at 64 px measures `29×47` upright and `47×29` turned, six columns ink the page down its length, the same call twice is byte-identical, and `Build/Typography-Vertical.png` is written for a person. |
| `paint.text.sideways` | The pen: a record built by hand with `PaintGlyphFlagSideways` inks **below** the baseline where the same record upright inks above it (the same hundred pixels either way), and a two-glyph `advanceY` column with each glyph turned inks two bands a pen step apart with nothing between them. |
| `ROBO_QEMU_VERTICAL_*` | The same chain **inside the OS**: the guest shapes the specimen with RoboText, reads the mode/orientation off the run, steps the pen by the run's own advances and turns the outline in a guest surface. Pinned in `Scripts/pipeline/commands.test.ps1` with the rest of the display checkpoint, and run by `robo test-qemu-display`. |

A vertical change is not done until `TestRoboText.ps1 -Target Host` and then
`TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` pass — the last four because the run and glyph
records grew fields the layout and paint bridges read, and the two before them because the turn lands on a
surface there. `TestDesktop.ps1 -Target Qemu` comes with them: the guest's own vertical specimen is pinned,
and a build that lost the rotation, the writing mode or the guest's parse of the face's metrics fails there
rather than on the host.
