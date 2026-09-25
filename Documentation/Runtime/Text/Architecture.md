# RoboText architecture (Phase 12.4)

RoboText is the runtime's **text layer**. It sits between the font layer (RoboFont) and the two layers
that consume text (RoboLayout measures it, RoboPaint draws it), and it exists to answer one question once
for everybody:

> **what is a piece of text, as an object?**

Before this phase the tree answered that question in four places. `RoboLayoutTextFont.c` walked code
points, summed `hmtx` in font units and scaled once. `RoboPaintDecoration.c` walked the *same* code
points, looked each one up in the cmap, drew the glyph and accumulated a `float` pen. `RoboDesktop` and
the document bridge resolved faces by role. Nothing was wrong, and the arrangement had a stated,
documented limit — two implementations of "advance" over one font, allowed to differ by a fraction of a
pixel at the end of a run, because each was a careful restatement of the same three lines.

RoboText removes the restatement. This document is the phase's contract: what the layer owns, what it
does not, and where each boundary's tests live.

---

## 1. The shape

```
                       HTML / DOM
                           │
                           ▼
                         CSS
                           │
                           ▼
                      Typography            (Phase 12.3 — roles → faces)
                           │
                           ▼
                       RoboText             ← this phase
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
   TextBuffer          TextRuns          FontContext
        │                  │                  │
        │          ┌───────┴───────┐          │
        │          │               │          │
        │        Script        Direction      │
        │          │               │          │
        └──────────┴───────┬───────┴──────────┘
                           │
                           ▼
                       GlyphRun
                           │
                ┌──────────┴──────────┐
                ▼                     ▼
           RoboLayout              RoboPaint
                │                     │
                └──────────┬──────────┘
                           ▼
                      RoboGraphics
                           │
                           ▼
                       Framebuffer
```

The milestone, stated the way the phase states it:

> After 12.4, text is no longer something the renderer discovers while painting. Text is a first-class
> runtime object with explicit runs, fonts, glyphs, metrics and source mapping.

---

## 2. Ownership

| Thing | Owner | Where |
|---|---|---|
| Bytes, scalar index, ranges | RoboText | `RoboTextBuffer.c`, `RoboTextTypes.c` |
| Script / direction / text runs | RoboText | `RoboTextRuns.c` |
| Glyph record, glyph runs, source mapping | RoboText | `RoboTextGlyphs.c` |
| The pipeline and the measurement | RoboText | `RoboTextShape.c` |
| Font parsing, tables, cmap, `hmtx`, fallback, selection | **RoboFont** | unchanged (Phase 5) |
| Unicode properties, scripts, bidi classes, encoding | **RoboUnicode** | unchanged (Phase 4) |
| Line boxes, geometry, where a run goes | **RoboLayout** | unchanged (Phase 10) |
| Coverage antialiasing, the raster | **RoboGraphics** | unchanged (Phase 12.1) |
| Faces by semantic role | **RoboTypography** | unchanged (Phase 12.3) |

RoboText parses no font and keeps no Unicode table. That is not a courtesy: a text layer with its own
cmap reader would be the second font parser in the tree, and a text layer with its own script tables
would be the second Unicode database.

### What may and may not be included

RoboText may include `RoboBase`, `RoboMem`, `RoboUnicode` and `RoboFont`. It may **not** include
`RoboHTML`, `RoboCSS`, `RoboLayout`, `RoboPaint`, `RoboRender`, `RoboWindow`, `RoboDisplay`,
`RoboGraphics` or `RoboTypography` — every one of those is a *consumer* of text, and an arrow that
pointed back at one of them would let the text layer ask a caller what the answer is.
`Assert-RoboTextBoundaries` in `Runtime/Tools/Build.ps1` enforces the list, and it is deliberately wide
on the consumer side because the temptation is real: the document bridge lives one layer up.

---

## 3. Units, ranges and the buffer

### Four index spaces, four types

| Type | Counts | Produced by |
|---|---|---|
| `RoboTextByteIndex` | bytes into UTF-8 | the DOM, `string.h`, a file offset |
| `RoboTextScalarIndex` | Unicode **scalar values** | runs, glyph source spans, selections |
| `RoboTextGlyphIndex` | glyphs in a glyph run | a painter |
| `RoboTextClusterIndex` | user-perceived characters | **nothing yet** (grapheme segmentation is a later goal) |

`héllo` is 5 scalars and 6 bytes. `🙂` is 1 scalar and 4 bytes. A layer that used one integer for both
would pass every ASCII test and fail on the first accent, which is why the two conversions are
**functions with a `Result`** rather than arithmetic at a call site:

- `RoboTextBufferScalarToByte` is **total** for every in-range index, including the one past the last
  scalar — so a caller can never fabricate a byte offset that is not on a scalar boundary, and the
  end-of-text caret position has an offset;
- `RoboTextBufferByteToScalar` **refuses** a byte that falls inside a multi-byte scalar rather than
  rounding to the nearest boundary. A selection range that cut a character in half would be a silent
  corruption; a refusal is a detour.

The buffer owns its UTF-8 and keeps a **scalar index** — one byte offset per scalar, built on first use
and invalidated by any mutation. `RoboTextBuffer.c` argues why: random access by scalar index is what the
rest of the layer needs and UTF-8 only gives it by walking, so the index turns `ScalarAt` from a walk
into one decode at a known offset while making `ScalarToByte` total. A `RoboTextView` is the borrowed
half — the same two conversions with no cache — and it exists so that measuring a DOM node does not copy
its bytes.

Decoding is `RoboUnicodeReplace` throughout. A document that reached the DOM already went through the
HTML tokenizer's own decoding, so a lone surrogate or a truncated sequence is *already* a fact of the
text; a buffer that refused it would be refusing a document the DOM accepted.

### Ranges

`RoboTextRange` is half-open `[start, end)` in scalars, so `start == end` is a **position** (a caret, a
glyph's source start) rather than a degenerate span. The algebra — `Length`, `IsEmpty`, `IsInverted`,
`IsWithin`, `Contains`, `ContainsRange`, `Intersects`, `Union`, `Slice`, `Shift` — is defined once, in
`RoboTextTypes.h`, because a layer that re-derives containment at each use site eventually disagrees with
itself about whether the end is included. Every operation **clamps** rather than wrapping: a range is two
`uint32_t`s, so subtraction can underflow and addition can overflow, and a range that read as *inside* a
small text because its start wrapped is how a selection highlights the wrong bytes.

---

## 4. Runs

Three run types, because three different questions are asked and they change independently.

| Type | Question | Answered from |
|---|---|---|
| `RoboTextScriptRun` | which script is this stretch in? | RoboUnicode's script tables |
| `RoboTextDirectionRun` | which way does this stretch run? | RoboUnicode's `Bidi_Class`, classified |
| `RoboTextRun` | the intersection, plus the font and language | the two above |

### Script: the neutrality rule

`Common` and `Inherited` are not scripts of their own. A neutral scalar takes the **previous** resolved
script, and a scalar before the first resolved script takes the **next** one — one forward pass, stable,
and what makes `"abc 123 def"` **one** Latin run rather than `[Latn][Zyyy][Latn]`. A text with no
script-bearing scalar at all stays `Common`: nothing resolved, so nothing is claimed.

### Direction: classification, not resolution

This is the phase's sharpest limit and it is stated precisely rather than gestured at:

| | |
|---|---|
| **12.4 does** | classify each scalar from its own `Bidi_Class`: `R`/`AL` right-to-left, `L` left-to-right, everything else takes the paragraph direction |
| **12.8 will do** | embedding levels, the explicit formatting characters, the weak and neutral rules (W1–W7, N0–N2), and reordering (L1/L2) |

The paragraph direction is the caller's declared base, or else the text's first strong scalar, or else
`RoboTextDirectionUnknown`. The third case is a real state and not an error: `UnicodeBidiParagraphDirection`
documents "0 if none", and 0 is `RoboLtr`, so taking it at face value would make `"123 456"` a
left-to-right paragraph — a claim UAX #9's P2/P3 deliberately does not make.

The consequence worth knowing: **declaring a base changes the neutral classification**. `"Hello שלום"`
is two text runs left-to-right and three with `baseDirection = RTL`, because the space between the two
scripts no longer belongs to the Latin run. That is the rule made visible, and `text.runs.declared_base`
pins it.

### Language

A BCP 47 tag, carried and **not interpreted**. Shaping needs it — a `locl` feature is language-dependent
— and shaping is a later goal. What exists now is the field, its validation (ASCII alphanumerics and
hyphens) and the copy, so that the run a later goal shapes already carries the tag rather than acquiring
one when somebody remembers. An empty tag means "none declared", which is a different statement from
`"und"` and is written as emptiness rather than as a sentinel.

### Shape options

`RoboTextShapeOptions` carries the declared base direction, a script hint, a language and an OpenType
feature list. The features are **validated and unread**: the tags are stored so the field exists in the
request a later goal extends, and nothing consults them. A feature array that silently did nothing would
be worse than no array, so `features`/`featureCount` are echoed into every run's options where a test can
see them.

---

## 5. Glyphs

```
glyphId    what to draw      an index into the face's glyph space; after substitution the count
                             will not equal the number of scalars
source     where it came     a RANGE of scalars, which is what makes a ligature (two scalars, one
           from              glyph) and a mark (one scalar, no glyph of its own) expressible in the
                             same type as the one-to-one case
advance    how far the pen   already in device pixels
           moves after it
offset     where it sits     in device pixels; zero for every glyph this build produces
```

`cluster` is the scalar index of the run's first scalar for this glyph's cluster. With no clustering
algorithm it is the glyph's own source start — the identity mapping: correct, useless for selection, and
**stated** rather than implied.

### The advance is a device quantity, and that is the change that matters

Before: the measurer summed `hmtx` values in font units and scaled once, while the painter accumulated
`float` device advances glyph by glyph. Two arithmetic chains, one font, and a comment admitting a run's
last glyph could sit a fraction of a pixel from the width its line was justified to.

Now: a glyph carries its device advance, and the **measurement is the sum of those advances**, accumulated
in `double` and rounded once at the boundary. One rounding per run survives (it was never about font units
— it was about not rounding per glyph), and the pen the painter accumulates and the width the measurer
reports are sums of the *same numbers*.

`RoboTextFontUnitsToDevice` is the module's **one** scaling door: `(value * sizePx) / upem` accumulated in
`double`, returned as `float` — the same expression the graphics layer's own device-metrics bridge uses,
so a glyph's advance here and one layer down are the same number rather than two roundings of one. A face
with no usable `upem` is refused rather than divided by zero, because an infinity in a pen position is a
bug three layers away from its cause.

### Missing glyphs

A scalar the cmap does not map becomes `.notdef` (glyph 0) and **still moves the pen**, by glyph 0's own
advance. The alternative — a zero advance — would silently reflow a line around a character the reader can
*see* (the box is drawn), and would make the measurement of undrawable text narrower than its ink.
`missingGlyphs` counts one per glyph, from RoboText's own count: a glyph whose id is `.notdef` **or** whose
metrics would not parse. A face whose metrics table will not parse at all answers `Unavailable` per glyph —
an *answer*, not a refusal, because what is true is that its glyphs do not advance.

---

## 6. The pipeline and measurement

```
RunSetCreate → GlyphRunGenerate (once per text run) → GlyphRunFinalize → the run list → RoboTextMeasureGlyphRunList
```

`RoboTextShape` does all of them in one call, because a caller that wanted the runs *and* the measurement
would otherwise run two of them twice. The result **owns** the run set and the glyph runs and borrows only
the text and the face.

Since Phase 12.15 the last step is a **door over the run list** (`RoboTextMeasurement.c`) rather than a walk
inside the shape call, which is what makes a result's own measurement and a caller's second measurement of the
same result one implementation instead of two that have to agree. The engine's default request computes no
bounds, so a caller that wants a width never pays for outlines.

Since Phase 12.11 the generator is a **shaping** pass and not a lookup: GSUB decides which glyphs there are,
GPOS decides where they sit, and the run is rebuilt once at the end. The pipeline a caller should hold in
mind:

```text
UTF-8 → scalars → scripts → direction → bidi → font selection → features → cmap
      → GSUB (substitution) → GPOS (positioning / kerning) → glyph run → finalize → run list
      → measurement → layout → paint
```

`RoboTextMeasurement` is the layout seam. That is why positioning came *before* kerning as a separate goal
even though kerning is GPOS: a kerning layer over an engine that does not exist would have had to invent one.

- `width` is the sum of the runs' own final device advances — their own plus the font's adjustments —
  accumulated in `double`, rounded once;
- `ascent`/`descent`/`lineGap` are the **maximum** over the participating runs (12.15 §5), and `lineHeight`
  comes from the face through its own metrics door, honouring a declared leading when the context declares one
  (`RoboTextLineHeightFromFontMetrics` = CSS's `normal` = the face's own extent);
- `missingGlyphs` counts what the face cannot draw, which is the one number that keeps a plausible width
  for undrawable text from passing as a measurement;
- `logicalBounds` and `inkBounds` are two geometries rather than one, and `hasInkBounds` says whether the
  second was computed at all.

12.15's contract is [TextMeasurement.md](TextMeasurement.md); the numbers above are that engine's outputs, not
a second walk's.

### Refusals versus answers

A text layer that refuses too much pushes a decision into a caller with no better answer; a layer that
answers everything reports confident nonsense. RoboText refuses exactly two absences:

| RoboTextNotSupported | the context names no face |
| RoboError | the face's cmap will not parse |

Everything else is answered: an unmapped scalar is `.notdef` plus a counted miss, text with no strong
direction gets `Unknown` runs, and an empty text produces an empty run set rather than a failure — no text
is a fact about a document, not an error in it.

---

## 7. The transitional cmap path, and where it went

`RoboPaintDecoration.c` used to be the tree's transitional text path, and its own header said so:

```
11.6 (then)   text run -> the font's cmap, per code point, in logical order -> paint
12.4 (now)    text run -> RoboText -> a glyph run -> paint
```

The loop that remains is four lines and owns **no font knowledge at all**: it does not know what a cmap
is, it does not resolve an id, it does not scale an advance, and it cannot disagree with the measurer
because the measurer runs the same glyph generation. What is left to RoboPaint is the two facts a text
layer has no business knowing: where the run's origin is on *this* surface, and that UI text is drawn
coverage-antialiased (Phase 12.1's `RoboGfxGlyphModeAlpha`, selected by the paint pass because it is the
only thing in the tree that knows it is drawing UI text rather than auditing an outline).

One consequence a reader should know: the paint pass now **prepares both of a face's tables** (cmap and
metrics) at the top of a pass. The old loop inherited the metrics parse as a side effect of the graphics
layer's advance door; the new one does not call that door, so the preparation has to be *said*.

`RoboTextGlyphGenerationCmapPerScalar` is the placeholder's name, and it is **isolated in
`RoboTextGlyphs.c`** on purpose: Phase 12.10's substitution pass replaces that one function's body — one
glyph from two scalars, a mark with no advance — and nothing above it changes.

---

## 8. Determinism

For identical text, face, size, direction, script and configuration, RoboText produces identical results,
bit for bit, on every run. This is what the screenshot and layout-regression tests stand on, and it is
enforced by shape rather than by discipline: the module has no global mutable state, no cache, no
randomness, no time and no host text API. Two text runs of one text measure the same because the
arithmetic is a function of the inputs — not because anything remembered. `text.conformance.repetition`
measures one string eight times and compares every field.

Nothing here loads a font from a path and nothing reads a host font directory. Faces arrive as borrowed
`FontFace *` in a `RoboTextFontContext`; who owns them — the pack, the typography resolver, a test's
fixture — is not this module's business, which is what keeps the arrow from the text layer to the
typography layer from existing at all.

---

## 9. Capabilities, and what is not here

`RoboTextGetCapabilities` is the honest record, and `text.conformance.capabilities` asserts it field by
field: the twelve model flags are present, and every algorithm flag is **absent**.

| Present | Since |
|---|---|
| the Unicode bidirectional algorithm | 12.8 |
| GSUB / substitution | 12.10 |
| ligatures (with the policy and the provenance) | 12.12 |
| GPOS / positioning (lookup types 1–6, 9 as of 12.19) | 12.11 |
| kerning (with the policy and the report) | 12.13 |
| complex scripts (Arabic joining, Indic syllables, positional masks) | 12.19 |
| vertical writing (the mode, the orientation, `vmtx` advances, `vert`/`vrt2`) | 12.20 |
| selection mapping (positions, clusters, affinity, hit tests, carets, rectangles) | 12.21 |

| Absent | Goal |
|---|---|
| variable-axis application (`wght`/`opsz` beyond face selection) | unowned until RoboFont interpolates outlines |
| GPOS lookup types 7–8 (the two contextual positioning forms) | no goal yet; recognised and skipped — Positioning.md §10 |
| the legacy `kern` table | Phase 5 owns the parser |
| cluster-grained font fallback | named in ComplexScripts.md as its own row |
| selection painting, caret rendering, editing and navigation | 12.22 onward |

Also deliberately absent: grapheme clustering, normalization, case mapping, editing, `@font-face`,
`font-feature-settings`, `font-variation-settings`, `font-stretch` matching and full generic-family
semantics. The four weights and the `opsz` class **bind but do not instance**: a style records them and
nothing applies an axis.

---

## 10. Build and tests

| | |
|---|---|
| Build | `Scripts/Build/BuildRoboText.ps1` (module + the shipped pack's presence check) |
| Test | `Scripts/Test/TestRoboText.ps1 -Target Host` — `text.*`, 322 cases |
| Conformance | `Scripts/Test/TestRoboTextConformance.ps1 -Target Host` — `text.shaping.*` only, 55 cases (12.18) |
| Suites | `Tests/Text/SuiteRoboText{Buffer,Runs,Glyphs,Shape,Script,Direction,Bidi,Unicode,FontSelection,Analysis,Gsub,Ligature,Gpos,Kerning,Conformance,Gallery,Selection,Vertical,Shaping{Latin,Scripts,Features,Kerning,Marks,Fallback,Integration}}.c` |
| Fixture | `Tests/Text/RoboTextTestSupport.h` — opens the **shipped** pack through RoboFont alone |
| Fixture (12.18) | `Tests/Text/RoboTextConformanceFont.h` + `RoboTextConformanceSupport.h` — **builds** a face from a spec, so a substitution is asserted because the bytes say so |
| Dev surface | `Build/Text-GlyphTable.txt`, written by `text.gallery.glyph_table` |
| Dev surface (12.18) | `Build/Text-ShapingConformance.txt`, written by `text.shaping.integration.dump` — the corpus, its counters, and every run's glyphs, offsets and clusters |

The fixture is deliberately built on RoboFont and not on the typography layer: the module's declared
dependencies are `RoboBase`/`RoboMem`/`RoboUnicode`/`RoboFont`, and a fixture that linked the layer above
would let a text change that broke the boundary compile and pass.

The 12.18 corpus is the one place a *built* face is used, and the reason is stated in
[ShapingConformance.md](ShapingConformance.md) §1: a production face's substitution set is its own opinion,
so the engine's contract is proved over bytes this repository states while the shipped pack keeps its own
cases (`text.gsub.shipped_pack`, `text.gpos.shipped_pack`, `text.kern.*`).

A text change is not done until these pass:

| Change | Run |
|---|---|
| anything in RoboText | `TestRoboText.ps1 -Target Host` |
| a shaping expectation (ligature, pair, mark, fallback range) | `TestRoboTextConformance.ps1 -Target Host` |
| a selection, caret or hit-test expectation (12.21) | `TestRoboText.ps1 -Target Host` (`text.selection.*`) |
| a boundary RoboLayout crosses (measurement, units) | `TestLayout.ps1 -Target Host` (359 cases) |
| a boundary RoboPaint crosses (glyphs, the pen, the raster) | `TestPaint.ps1 -Target Host` (241) |
| the glyph-generation seam | `TestGraphics.ps1 -Target Host` (177) and `TestRasterizer.ps1 -Target Host` (31) |
| the shipped face or the pack | `TestFont.ps1 -Target Host` (189) |
| anything the shell draws | `TestDesktop.ps1 -Target Host` (56) |
| the guest image | `TestDesktop.ps1 -Target Qemu` (`robo test-qemu-display` for the three guest checkpoints: `ROBO_QEMU_TYPOGRAPHY_*`, `..._SHAPING_*`, `..._VERTICAL_*`, `..._SELECTION_*`) |

### The visual test surface, and where the pixels live

The phase asks for a surface showing the raw text, its runs, the glyph ids, the advances and a rendered
glyph run. Those split across two parts, and the split is the module boundary rather than a convenience:
**RoboText links no rasterizer** (the boundary scan forbids `RoboGraphics` and `RoboRender`), so nothing
under `Tests/Text/` can draw a glyph.

| Half | Where | What it shows |
|---|---|---|
| the numbers | `Build/Text-GlyphTable.txt`, written by `Tests/Text/SuiteRoboTextGallery.c` | every named string: raw text, byte/scalar counts, run rows (`script direction [first,last)`), glyph rows (`id`, source span, cluster, device advance, `hmtx` units, offset, `mapped`, metrics status), and the measurement — for the shipped face at the UI Text role |
| the pixels | `Build/Typography-Desktop.png`, `-Chrome.png`, `-Document.png`, `-Shaping.png`, written by `Tests/Desktop/SuiteRoboDesktopTypographyGallery.c` | the shell, the window chrome, a document specimen and 12.18's shaping specimen, each drawn through the resolver *and* RoboText |

The table is asserted as it is written, because a dump proves nothing by itself: the glyph rows' advances
must sum to `measurement.width` (so a second arithmetic path inside the pipeline shows up as a mismatch),
the row count must equal `measurement.glyphCount`, every glyph's source range must be non-empty and inside
the text, and the whole file must be **byte-identical on a second build** — which is what makes it usable
as a reference the way a screenshot is.

The last two rows are the ones a reader should look at first. `unicode` (`é Å Ω 中 🙂`) is `Latn`/`Grek`/`Hani`
because the spaces between them are neutral and take the previous resolved script; and `noncharacter`
(`U+FDD0`) is one glyph, `id=0`, `mapped=0`, advancing by glyph 0's own 10.50 px — `.notdef`, with the pen
still moving, which is the rule §5 argues for made visible in a row.

### QEMU validation (Phase 12.4)

`TestDesktop.ps1 -Target Qemu` builds the image and boots it, and the guest's runtime module list
(`scripts/roboboot-build-common.ps1`) names `RoboText` — a guest built without it links no text at all.
The run reports the desktop's own counters over the serial line:

```
ROBO_QEMU_DESKTOP_WINDOWS=0x00000001
ROBO_QEMU_DESKTOP_CONTENT_RUNS=0x00000001
ROBO_QEMU_DESKTOP_LABELS=0x00000004
ROBO_QEMU_DESKTOP_FILLS=0x0000000C
ROBO_QEMU_DESKTOP_PIXELS=0x0009F670
```

`PIXELS` is a checksum of the presented frame, and it is **identical to the value the same run reported
before this migration**. That is the phase's strongest single piece of evidence: the shell now measures
and paints every glyph through RoboText, and the framebuffer did not move a pixel.

---

## 11. Current limitations

1. **No shaping.** One cmap lookup per scalar, in logical order. Correct for the text it handles, and
   named as a placeholder rather than presented as an implementation.
2. **No bidi.** Direction is *classified*, never resolved; an RTL run is drawn left to right from its
   origin and the paint pass counts how many runs that happened to.
3. **Fallback exists as of 12.5, and the single-context pipeline still binds one face.** The run set and
   `RoboTextShape` carry one `RoboTextFontContext`, so their glyph generation still turns an uncovered
   scalar into `.notdef` and a counted miss. *Multi-face* selection lives one door over:
   `RoboTextAnalysisCreate` produces runs with per-run faces, and `RoboTextShapeRequestToRun` feeds them
   back into the same generator. See [FontIntegration](FontIntegration.md).
4. **No grapheme clusters.** `cluster` is the identity mapping.
5. **Weights and optical size bind but do not instance.** A style records them; no axis is applied.
6. **A run carries one face, selected from its `font-family` as of 12.17 — but not a face per script.**
   The request reaches layout and the provider answers it, so a document's run is measured and drawn with
   the face its declaration resolves to, and `font-weight: 700` selects (never synthesizes) among the faces
   the database has. What is still absent is a *mixed-font run*: the analysis layer's per-script faces are
   not yet what the desktop walks, so a run that spans faces falls back to one of them. Items 5 and 6 are
   the same missing thing: a shaped run that can carry an axis value, and a run set the shell walks
   run by run.
7. **No letter spacing applied.** `letterSpacingPx` is carried and not read.
8. **`text.*` has no QEMU-only cases.** The module computes on the development machine; the guest-visible
   half of every phase is the desktop's rendering check above, plus 12.18's and 12.20's own off-screen
   checkpoints.
9. **Selection is a map, not an interaction.** The logical ↔ visual bridge exists (12.21), but nothing
   *acts* on it yet: no editing, no caret navigation, no IME, no clipboard and no selection painting, and
   no `SelectionMap()` accessor on RoboLayout's line boxes — every case supplies its own placements.

---

## 12. Where each phase lands next

```
12.2  System Typeface Pack            ✓
12.3  Desktop + document face binding ✓
12.4  RoboText architecture           ✓
12.5  Unicode / font integration       ✓  (UnicodeIntegration / FontIntegration / FontFallback)
12.6  Script runs                      ✓  (ScriptRuns)
12.7  Direction runs                   ✓  (DirectionRuns)
12.8  Bidirectional algorithm          ✓  (Bidi)
12.10 Glyph substitution (GSUB)        ✓  (GlyphSubstitution)  → replaces RoboTextGlyphRunGenerate's body
12.11 Glyph positioning (GPOS)         ✓  (Positioning)        → fills RoboTextGlyph.offsetX/offsetY
12.12 Ligatures                        ✓  (Ligatures)
12.13 Kerning                          ✓  (Kerning)            → the policy and the report over 12.11
12.14 Glyph runs                       ✓  (GlyphRuns)          → finalizes the run and lists them
12.15 Text measurement                  ✓  (TextMeasurement)   → the engine over the run list
12.16 Paint / layout bridge             ✓  (PaintBridge)        → the display list carries the shaped glyphs
12.17 CSS font request                  ✓  (FontRequest)        → the request and the face it resolves to
12.18 Latin & European conformance      ✓  (ShapingConformance) → the corpus that asserts every layer
12.19 Complex scripts                   ✓  (ComplexScripts)     → Arabic and Indic as a plan, not a branch
12.20 Vertical writing                  ✓  (VerticalText)       → writing mode as data, no second engine
12.21 Selection mapping                 ✓  (TextSelection)      → the logical ↔ visual bridge
```

Every one of those consumes this layer rather than replacing it. That is the milestone: the seams
(`RoboTextRun`, `RoboTextGlyph`, `RoboTextMeasurement`) are the ones the later goals were planned
against, and no caller has to change when a generator does.

---

## 13. Phase 12.5 — Unicode / font integration

12.5 connects this architecture to the real Unicode and font systems without changing the 12.4 seams. The
new pieces and their contracts:

| Area | Public header | Implementation | Contract |
|---|---|---|---|
| Code points, decoder, properties, script/direction classification | `RoboText/RoboTextUnicode.h` | `RoboTextUnicode.c`, `RoboTextScriptResolver.c` | [UnicodeIntegration.md](UnicodeIntegration.md) |
| Coverage, selection, fallback chain, font identity | `RoboText/RoboTextFontSelection.h` | `RoboTextFontSelection.c` | [FontIntegration.md](FontIntegration.md), [FontFallback.md](FontFallback.md) |
| Analysis runs and the shape request | `RoboText/RoboTextAnalysis.h` | `RoboTextAnalysis.c` | [FontIntegration.md](FontIntegration.md) §5 |

Three properties the phase adds and that a reader should check against the code:

1. **One decoder, one resolver, one classifier.** `RoboTextRuns.c` no longer decodes or classifies anything
   itself; it calls `RoboTextUnicodeDecodeAll`, `RoboTextScriptRunsResolve` and
   `RoboTextUnicodeClassifyDirection`. The run set and the analysis layer therefore cannot disagree about a
   boundary or a script.
2. **Coverage-gated, deterministic, data-driven selection.** The fallback order *is* the chain array, so a
   fallback change is a data change. `RoboTextGetCapabilities` reports `fontSelection`, `fontCoverage`,
   `coverageCache`, `analysisRuns`, `shapeRequest`, `unicodeIntegration`, `codePoints`, `combiningMarks`,
   `scriptResolver` all `1`, and still reports `bidiAlgorithm`, `substitution`, `positioning`, `kerning`,
   `ligatures`, `complexScripts`, `verticalText` all `0`.
3. **`RoboText` still links no rasterizer.** The new headers include RoboFont and RoboUnicode and nothing
   above; `Assert-RoboTextBoundaries` enforces it. A rendered multi-script run is the shell's gallery.

The version moved to `1.1.0`: the run and glyph record shapes are unchanged, and the new API surface is
additional.

---

## 14. Phase 12.6 — script runs

12.6 turns the script *classification* 12.5 wired up into a segmentation stage with a contract of its own.
It adds no header and no record: the type is still `RoboTextScriptRun { range, script; }`, because a script
run is an analysis result and not a rendering one, and nothing glyph-shaped or font-shaped belongs in it.

| Area | Where | Contract |
|---|---|---|
| The rule, the run accumulation and the diagnostics | `RoboTextScriptResolver.c` | [ScriptRuns.md](ScriptRuns.md) |
| `Script_Extensions`, the ASCII block answers | `RoboTextUnicode.c` | [UnicodeIntegration.md](UnicodeIntegration.md) §4 |
| Run-level font selection | `RoboTextAnalysis.c` | [FontIntegration.md](FontIntegration.md) §5 |

Three properties the phase adds, and what they replaced:

1. **A neutral is resolved against *both* neighbours, and consulted against `Script_Extensions`.** 12.5's
   single carry could not tell a space between two stretches of Latin (which should join them) from a
   character listed by both sides (which should stay neutral), and could not read the second property at
   all. The order of the checks — and the mark rule's precedence over the extension table — is in
   `RoboTextScriptRunsResolve`'s doc comment and in ScriptRuns.md §2, and the two must agree.
2. **The font is selected once per script run, not once per scalar.** This subsumes 12.5's "a combining mark
   takes the font of the scalar before it": the mark resolves into its base's *run* and the run is selected
   as a unit, so a base and its accent cannot be two faces by construction. The analysis runs are therefore
   the script runs cut again by classified direction, and there is no third cut for the font.
3. **The classification reads no direction and no visual order.** The rule is identical for RTL text, which
   is what lets a selection, a caret and a hit test use the same segmentation a future bidi pass will.

The version moved to `1.2.0`, and `RoboTextGetCapabilities` reports `scriptExtensions`,
`contextualScriptResolution` and `scriptRunDiagnostics` `1` — each exercised by a `text.script.*` case —
while every algorithm flag the phase plan owns stays `0`.

---

## 15. Phase 12.7 — direction runs

12.7 turns the direction *classification* 12.4 declared into a stage with a contract of its own. It adds
one header and one source file, and no record shape: `RoboTextDirectionRun` is still
`{ range, direction; }`, moved to `RoboTextTypes.h` so that the resolver's header can name it, and
`RoboTextDirectionUnknown` is still what a paragraph with no strong character and no declaration answers.

| Area | Where | Contract |
|---|---|---|
| The class projection, the paragraph direction, the resolver, the diagnostics | `RoboTextDirection.c`, `RoboTextDirection.h` | [DirectionRuns.md](DirectionRuns.md) |
| The vocabulary: classes, modes, the context | `RoboTextTypes.h` | DirectionRuns.md §2, §4 |
| The shared P2/P3 scan | `RoboTextUnicodeBaseDirection` | [UnicodeIntegration.md](UnicodeIntegration.md) §6 |
| The direction runs' consumers | `RoboTextRuns.c`, `RoboTextAnalysis.c` | DirectionRuns.md §1, §8 |

Three properties the phase adds, and what they replaced:

1. **Direction is a classification with a vocabulary, not a per-scalar fallback.** 12.5 classified a scalar
   as strong or "takes the base", which made neutrals and numbers the same kind of thing. 12.7 classifies
   four kinds — strong, number, attaching mark, neutral — and resolves a *block* of them against both
   sides, so `"Hello, world!"` is one run and `"ABC אבג 123 DEF"` is three.
2. **A number is not a left-to-right scalar.** Its class is a weak one, and the direction it resolves to
   comes from the paragraph (I1/I2) while what it *lends* its neighbours comes from W7. That distinction
   is what stops an RTL paragraph from claiming a number and stops an LTR paragraph from putting a
   right-to-left hole between two of them.
3. **One resolver, three readers.** `RoboTextDirectionRunsResolve` fills the runs and the per-scalar array
   in one pass; the run set, the analysis runs and the analysis's own run array all read that call, so the
   boundaries they report cannot differ.

The version moved to `1.3.0`. `RoboTextGetCapabilities` reports `paragraphDirection`, `directionContext`,
`directionClasses` and `directionRunDiagnostics` `1` — each exercised by a `text.direction.*` case — and
still reports `bidiAlgorithm`, `substitution`, `positioning`, `kerning`, `ligatures`, `complexScripts`,
`verticalText` and `variableAxisApplication` `0`.

---

## 16. Phase 12.10 — glyph substitution

12.10 is the first phase where a font's own tables decide what a glyph *is*: the cmap's one-id-per-scalar
sequence becomes the font's sequence, and the record the rest of the pipeline reads gains a flag saying a
lookup chose this id. The table is Phase 5's (`FontFaceGsub`, parsed once per face and immutable); this phase
is the plan and the readers.

| Area | Where | Contract |
|---|---|---|
| The feature plan, the scan, the counters, the trace | `RoboTextGsub.c` | [GlyphSubstitution.md](GlyphSubstitution.md) |
| One reader per lookup type (all eight) | `RoboTextGsubSubst.c` | GlyphSubstitution.md §8 |
| The mutable shaping sequence and its calls | `RoboTextGsubInternal.h`, `RoboTextGsub.h` | GlyphSubstitution.md §4 |
| The GSUB step in the generator | `RoboTextGlyphs.c` | GlyphSubstitution.md §9 |

The three properties the phase adds:

1. **The font's lookup order is the answer, and features are a filter over it.** The plan walks the face's
   ScriptList → LangSys → FeatureList, keeps the tags the caller's feature set leaves on, and collects their
   lookup indices sorted ascending — never in feature order, never alphabetical.
2. **Substitution rebuilds the run rather than editing it in place.** The buffer is the mutable working set
   and the run is rebuilt once, so an advance is asked of the face for the *resulting* id: a ligature's
   advance is its own, which is the number a reader would notice if it were a sum.
3. **A malformed font is a skip and a counter, never a failure.** Every reader answers "no match", the
   lookup is abandoned, and the glyphs produced so far are the caller's; four limits bound what a hostile
   font can spend. `RoboTextGsubApply` fails only for a programming error.

The version moved to `1.6.0` (with 12.12). `RoboTextGetCapabilities` reports `substitution` `1`,
`glyphGenerationIsPlaceholder` `0` — the placeholder was replaced — and every other algorithm flag the
phase plan owns still `0`.

---

## 17. Phase 12.12 — ligatures

12.12 is a *policy and provenance* phase: 12.10 already substitutes, so what is added is which five tags a
caller wants, how a stylesheet names them, and what a product remembers about the characters it swallowed.
It adds no engine, no second table and no `fi` list.

| Area | Where | Contract |
|---|---|---|
| The policy, the variants, the feature-set application, the report | `RoboTextLigature.c`, `RoboTextLigature.h` | [Ligatures.md](Ligatures.md) |
| The policy's value types, beside the feature set they become | `RoboTextTypes.h` | Ligatures.md §2 |
| Where the policy is applied (explicit settings win) | `RoboTextRuns.c` | Ligatures.md §2 |
| A product's source range and component count | `RoboTextGlyphs.h`, `RoboTextGlyphs.c` | Ligatures.md §6 |

The two properties the phase adds:

1. **Required ligatures are not decoration.** `liga`, `clig`, `rlig`, `dlig` and `hlig` are five independent
   tags, `RoboTextLigatureVariantNone` switches the optional four off and leaves `rlig` alone, and a policy
   cannot fold one kind's state into another's — the failure it prevents is wrong Arabic letters, not a
   plainer `fi`.
2. **A ligature's provenance survives it.** One glyph, several scalars: the source range is the union of its
   components' and `ligatureComponents` says how many produced it, so `RoboTextGlyphRunGlyphsForScalar`
   answers the ligature glyph for every scalar inside it. The data a caret needs exists; the caret is later.

The version moved to `1.6.0` with 12.10. `RoboTextGetCapabilities` reports `ligatures`, `ligaturePolicy` and
`ligatureProvenance` `1`.

---

## 18. Phase 12.11 — glyph positioning

12.11 is the second half of shaping: GSUB answers *which glyphs*, GPOS answers *where they sit*. It is one
engine in two files over the **same shared layout core** GSUB uses, which is the point — not two OpenType
implementations that happen to agree.

| Area | Where | Contract |
|---|---|---|
| The plan, the apply loop, the counters, the trace, the writers | `RoboTextGpos.c`, `RoboTextGpos.h` | [Positioning.md](Positioning.md) |
| One reader per lookup type this build executes | `RoboTextGposSubst.c` | Positioning.md §4 |
| The GPOS step in the generator, after GSUB | `RoboTextGlyphs.c` | Positioning.md §1 |
| The font's own part of a glyph's vectors | `RoboTextGlyphs.h`, `RoboTextGlyphs.c` | Positioning.md §2 |

The three properties the phase adds:

1. **Positioning writes an adjustment, never a total.** `advanceAdjustX`/`offsetAdjustX` sit beside the totals
   the run build computes once, so a diagnostic can say "12.40px, of which -1.25 is the font's kerning".
2. **The order GSUB → GPOS is not interchangeable, and the suite proves it.** `text.gsub.ligature_positioning`
   shapes a face that carries both tables, where the GPOS pair is stated against the *ligature's* glyph id: the
   pair can only apply if substitution ran first, and the ligature's advance is its own plus the adjustment
   rather than the sum of its components'.
3. **A font's contents never fail a run of text.** A malformed subtable skips and is counted, a subtable offset
   that lands inside its own lookup's header is refused once per lookup, and the probe budget is recorded in the
   engine that spent it.

The version moved to `1.7.0` (with 12.13). `RoboTextGetCapabilities` reports `positioning` `1`.

---

## 19. Phase 12.13 — kerning

12.13 is a *policy and report* phase, exactly as 12.12 was for ligatures: 12.11 already reads a pair
adjustment, so what is added is the caller's decision as data, the resolution of which source may answer, and the
per-glyph record of what the font did. There is no second kerning engine and no hard-coded pair.

| Area | Where | Contract |
|---|---|---|
| The policy, the resolution, the report | `RoboTextKerning.c`, `RoboTextKerning.h` | [Kerning.md](Kerning.md) |
| The policy's value types, beside the feature set they become | `RoboTextTypes.h` | Kerning.md §2 |
| Where the policy is applied (explicit settings win) | `RoboTextRuns.c` | Kerning.md §3 |
| Per-range feature overrides, cut into runs | `RoboTextTypes.h`, `RoboTextRuns.c` | Kerning.md §9 |
| The per-glyph adjustment facts the report reads | `RoboTextGlyphs.h` | Kerning.md §12 |

The three properties the phase adds:

1. **`kern` is a feature, and the policy is data.** `RoboTextKerningPolicy` becomes entries in a
   `RoboTextFeatureSet` and reaches the positioning plan through the same door every other tag uses, so
   `font-kerning: none` cannot become a special case in a paint layer.
2. **One source answers, and the fallback is never an addition.** A face may carry kerning in GPOS *and* in a
   legacy `kern` table; the rule is a choice (`RoboTextKerningLegacy`), and the resolution reports the table's
   *presence* separately from which source *answered*, because this build does not read the legacy table.
3. **Range overrides are a shaping boundary.** `RoboTextShapeOptionsSetFeatureRange` cuts the text into runs
   where the effective settings change — before any plan is resolved — and the consequence (a pair straddling
   the seam is not a pair) is asserted by `text.kern.range_override` rather than hidden.

The version moved to `1.7.0` with 12.11. `RoboTextGetCapabilities` reports `kerning`, `kerningPolicy` and
`kerningReport` `1`, and `complexScripts`/`verticalText`/`variableAxisApplication` stay `0`.

---

## 20. Phase 12.15 — text measurement

12.15 turns the measurement from a walk inside the shape call into the module's **one engine over a finalized
run list**. It adds no shaping, decodes nothing and selects nothing: the shaped result is already final, and the
only question left is how much space it takes.

| Area | Where | Contract |
|---|---|---|
| The engine, the record, the rect, the request, the tolerance | `RoboTextMeasurement.c`, `RoboTextMeasurement.h` | [TextMeasurement.md](TextMeasurement.md) |
| The result's own measurement, and the re-measure door | `RoboTextShape.c`, `RoboTextShape.h` | TextMeasurement.md §2 |
| The run list the engine reads | `RoboTextRunList.h` | GlyphRuns.md |
| The glyph metrics door the ink box asks | `RoboTextFont.h`, `RoboFontMetrics.h` | TextMeasurement.md §7 |

The four properties the phase adds:

1. **Measurement consumes shaped output and never shapes.** `RoboTextMeasureGlyphRunList` reads the runs'
   advances, metrics and glyphs. A width is therefore the sum of the same numbers the painter's pen accumulates,
   which is what makes `measurement == layout advance == glyph-run advance` true by construction rather than by
   two implementations agreeing.
2. **Line metrics over a mixed-font line are the maximum over the runs.** A line of `"Hello مرحبا 中文"` is as
   tall as its tallest run, and the aggregation is one function (`MeasureLineMetricsOf`) that both the engine's
   fields and `RoboTextMeasureGlyphRunListLineMetrics` publish, so the record and the fields cannot disagree.
3. **Advance, logical bounds and ink bounds are three geometries.** Advance is what the pen does (a space has
   one and no ink), logical bounds is the box layout places, and ink is what the outlines occupy (a mark has no
   advance and ink). Folding one into another is a defect, exactly as it is one phase down for coverage, alpha
   and opacity.
4. **An answer is either complete or absent.** Ink bounds are reported only when every glyph the face could
   answer for contributed a box (`hasInkBounds`), and a glyph whose face carries none is skipped rather than
   approximated with the advance box. Rendering an absent box as a guess would clip to something that paints
   nothing.

The version moved to `1.9.0`. `RoboTextGetCapabilities` reports `measurementEngine`, `lineMetricsPolicy`,
`inkBounds`, `measurementBounds`, `measurementValidation`, `measurementDiagnostics` and `measurementTolerance`
`1`; `verticalText` stays `0`, and `advanceY` is `0` for every text this build produces — published so that
12.20 changes a value rather than a record.

---

## 21. Phase 12.16 — the paint bridge

12.16 changes nothing inside this module and everything about *when* it runs. A text record used to carry the
characters only, and the paint pass re-shaped them through `RoboTextGlyphRunGenerate` on **every frame** it
replayed a display list. Now the recording pass shapes them **once**, stores the glyphs and the face, and a
replay draws the stored ids.

| Area | Where | Contract |
|---|---|---|
| The glyph record, span and pool | `RoboPaint/RoboPaintText.h` (`PaintGlyph`, `PaintGlyphSpan`, `PaintGlyphPool`) | [PaintBridge.md](PaintBridge.md) |
| The face tokens | `RoboPaint/RoboPaintText.h`, `RoboPaint/RoboPaintFontTable.h` | PaintBridge.md §4 |
| The recorder that shapes | `RoboPaint/RoboPaintContext.c` (`PaintContextSetTextFace`) | PaintBridge.md §5 |
| The pen that draws the stored glyphs | `RoboPaint/RoboPaintDecoration.c` | PaintBridge.md §6 |

The three properties the phase turns on, each of which a reader should check against the code:

1. **The record keeps both halves.** The characters stay because they are what a selection, a hit test and a
   copy read (12.8's `visualIndex`, 12.14's clusters and 12.5's source ranges are the run-list half of the same
   fact); the glyphs are stored because a replay must not shape. `glyphs` empty **exactly when** the token is
   `None` is the one-fact invariant between them.
2. **A glyph id is meaningless without its face**, so the face travels as a token into a table the display list
   **copies** — the same "a list owns its snapshot" rule the text pool follows, extended to faces because a
   glyph id drawn with the wrong face is a different letter rather than a wrong colour.
3. **RoboText still supplies the ids and the placement and nothing more.** The paint pass reads
   `glyphId`, `offsetX/Y`, `advanceX/Y`, `cluster` and the source range off `PaintGlyph` and never asks RoboText
   a question at draw time — it does not decode, select, segment, substitute, position or kern, which is exactly
   the boundary `RoboGraphicsText.h` wrote down when it said *"RoboText supplies glyph IDs + placement later."*

## 22. Phase 12.20 — vertical writing

A **writing-mode-neutral** text model: a vertical run is the same `RoboTextGlyphRun` on the other axis, not a
second engine. The writing mode is data on `RoboTextShapeOptions` and on every run (`RoboTextWritingMode`); the
axes are an explicit inline/block pair (`RoboTextWritingAxes`); the per-glyph answer is
`RoboTextGlyphOrientation` with the rotation a painter composes
(`RoboTextGlyphOrientationRotation`); and the OpenType integration is two feature tags (`vert`/`vrt2`) that reach
12.10's GSUB through the door every other feature uses.

| Area | Where | Contract |
|---|---|---|
| The writing mode, the axes, the orientation tables, the feature hook, the dump | `RoboTextVertical.c`, `RoboTextVertical.h` | [VerticalText.md](VerticalText.md) |
| The vocabulary (`RoboTextWritingMode`, `RoboTextTextOrientation`, `RoboTextGlyphOrientation`) | `RoboTextTypes.h`, `RoboTextTypes.c` | VerticalText.md §2, §3 |
| The `vhea`/`vmtx` door and the vertical origin | `RoboTextFont.c`, `RoboTextFont.h` (`RoboTextFontVerticalMetrics`) | VerticalText.md §6 |
| The placement, beside the `hmtx` line | `RoboTextGlyphs.c` (`GlyphApplyVerticalMetrics`) | VerticalText.md §6.1 |
| The attachment's distance, on the inline axis | `RoboTextGposSubst.c` (`GposPenDistance`, the mark and cursive readers), `RoboTextGposOptions::writingMode` | VerticalText.md §6.4 |
| Orientation as a run boundary | `RoboTextRuns.c` | VerticalText.md §4 |
| Writing mode and orientation in the run key | `RoboTextRunList.c`, `RoboTextGlyphs.h` (`RoboTextGlyphRunKeyField`) | VerticalText.md §4 |
| The measurement's `advanceY` | `RoboTextMeasurement.c` | VerticalText.md §7 |
| The pen that accumulates both advances, and the sideways mark it carries | `RoboPaint/RoboPaintDecoration.c`, `RoboPaintContext.c` (the recorder), `RoboPaint/RoboPaintText.h` (`PaintGlyphFlagSideways`) | VerticalText.md §8 |
| The quarter turn the rasterizer applies to the outline | `RoboGraphics/RoboGraphicsGlyph.c` (`GfxGlyphRasterParams::rotation`), `RoboGraphicsText.c` (`GfxGlyphDraw`) | TextRasterQuality.md, "Rotation" |
| The guest's own vertical specimen (the chain inside the OS) | `User/robo-runtime/main.c` (`GuestVerticalCheckpoint`), pinned in `Scripts/pipeline/commands.test.ps1` | VerticalText.md §8.1, §11 |

The three properties a reader should check against the code:

1. **The mode is never inferred.** Nothing in the module derives a writing mode from a script or from a code
   point, and the run set cuts on the **resolved orientation** so a painter reads one answer per run rather
   than inspecting characters. Japanese horizontal and Japanese vertical are two `RoboTextShapeOptions`.
2. **There is no vertical engine.** The substitution is 12.10's GSUB (`vert`/`vrt2` are tags), the positioning
   is 12.11's GPOS plus the face's own `vmtx` advance and origin, and the measurement is 12.15's engine over the
   same list. The one new mechanism is the **axes value**, which exists so no call site writes the x/y swap —
   the GPOS attachments included: an attachment's pen distance is the run's **inline** advance, so the same
   reader asks `vmtx` in a vertical mode and `hmtx` in a horizontal one (VerticalText.md §6.4).
3. **The paint half branches on nothing.** `RoboPaintDecoration.c` adds each glyph's `advanceX` and
   `advanceY`; horizontal text has a zero second term, so the arithmetic that was there before is unchanged and
   the pass still cannot learn a script, a writing mode or a vertical origin. The sideways half is **carried,
   not re-derived**: one `PaintGlyphFlagSideways` bit set per glyph by the recorder from the run RoboText
   produced (a paint record can span several runs, so the record is the wrong grain), handed by the pen to
   `GfxGlyphDraw` as a quarter turn, and applied by the rasterizer **to the outline** in the one device
   transform — so a painter never tests a code point and no layer below RoboText decides an orientation.

The version moved to `1.11.0`. `RoboTextGetCapabilities` reports `verticalText`, `writingAxes`,
`verticalOrientation`, `verticalMetrics`, `verticalFeatures` and `verticalDiagnostics` `1`, each read field by
field in `Tests/Text/SuiteRoboTextConformance.c` — and there is deliberately **no `verticalShaper`** flag,
because the phase adds no engine: `verticalText` claims a writing *model*, and `advanceY` is a real number for
a vertical run rather than the published zero 12.15 left so that this phase would change a value.

---

## 23. Phase 12.21 — selection mapping

12.21 makes shaped text **addressable**. It adds no shaping and no measurement: it is the bridge between a
source position and a place on the page, built over the finalized run list of 12.14 and the clusters that run
already carries.

| Area | Where | Contract |
|---|---|---|
| The whole layer: positions, affinity, selections, placements, the map, the three queries, the rectangles, the dumps, the fuzz entry | `RoboTextSelection.c`, public `RoboTextSelection.h` | [TextSelection.md](TextSelection.md) |
| The clusters the mapping is over | `RoboTextGlyphs.c` — `RoboTextGlyphRunClusterForScalar` | GlyphRuns.md |
| The inline/block axes it projects onto | `RoboTextVertical.c` — `RoboTextWritingAxesInline`/`…Block` | VerticalText.md §2.2 |
| The run, the list, and the metrics a placement's band comes from | `RoboTextRunList.h`, `RoboTextGlyphs.h` | GlyphRuns.md |
| The capability flags | `RoboTextVersion.h`/`.c` | TextSelection.md §10 |

The five properties the phase adds:

1. **A position is a source offset; geometry is derived.** `RoboTextPosition` is one `RoboTextScalarIndex`
   offset into the original text, a selection keeps anchor and focus, and no door in the layer takes a glyph
   index — because a caller holding one has already lost the ligature, the mark and the reordered Indic
   cluster. `RoboTextSelectionMapHitTest` answers a position and `…MapCaret` answers geometry, and neither can
   be mistaken for the other.
2. **The one input is the placement.** A run knows its glyphs but not where the caller put them, so
   `RoboTextSelectionPlacement` carries the pen origin, the caller's line index and the line's band as two
   signed distances along the **block** axis. The map copies placements and borrows the list, and it refuses a
   subset, a duplicate, an out-of-order array and an unplaced run rather than repairing any of them.
3. **No call site swaps x and y.** Every query projects onto the placement's own axes through 12.20's doors and
   converts back at the end, so a column's caret, hit test and rectangles are correct by construction and
   `vertical-rl` differs from `vertical-lr` in the block vector and in nothing else.
4. **A source offset may have two visual locations, and never a fraction of one.** Affinity says which of two
   a boundary meant; a hit inside a ligature or a conjunct snaps to the cluster and **reports that it
   snapped**; a selection returns one rectangle per (line, run) segment in visual order rather than one box,
   and a base with its zero-advance marks is one of those rectangles rather than one each.
5. **A query shapes nothing.** Nothing in the layer decodes UTF-8, selects a font, resolves a script or a
   level, runs GSUB/GPOS or measures — a hit test over a paragraph costs no shaping, and the layer allocates
   nothing per query.

The version moved to `1.12.0`, and the move is the smallest any phase has asked for: no existing record
changed at all. `RoboTextGetCapabilities` reports `selectionMapping`, `selectionPositions`,
`selectionClusters`, `selectionAffinity`, `selectionHitTesting`, `caretGeometry`, `selectionGeometry`,
`selectionWritingAxes`, `selectionValidation`, `selectionDiagnostics` and `selectionFuzzing` `1`, each read
field by field in `Tests/Text/SuiteRoboTextConformance.c` — with deliberately **no `selectionPainting`** (the
shell's row) and no `selectionShaping` (a claim the layer's whole design denies).

---

## 24. Phase 12.22 — text editing

12.22 makes shaped text **editable**, and it is the first phase in the block whose consumer sits *above*
RoboText: an editor changes source scalars and everything downstream is a consequence, exactly as it is for a
document a caller typed for the first time.

| Area | Where | Contract |
|---|---|---|
| The whole layer: the editor, the operation record, the invalidation, the boundaries, composition, transactions, transformation, the dumps, the fuzz entry | `RoboTextEditing.c`, public `RoboTextEditing.h` | [TextEditing.md](TextEditing.md) |
| The one mutation | `RoboTextBuffer.c` — `RoboTextBufferRemove` then `RoboTextBufferInsert` | Buffer.md |
| Positions, ranges, affinity, selections | `RoboTextSelection.h`, reused rather than re-declared | TextSelection.md |
| The cluster boundaries an edit may land on | `RoboTextGlyphRunCluster*` through `RoboTextEditBoundariesFromRuns` | GlyphRuns.md, TextEditing.md §6 |
| The character rule's property read | `RoboTextUnicode.c` — `RoboTextUnicodeGeneralCategory` | UnicodeIntegration.md |
| The safety of a complex-script boundary | `RoboTextComplex.c` — `RoboTextComplexIsSafeBoundary` | ComplexScripts.md §28 |
| The capability flags | `RoboTextVersion.h`/`.c` | TextEditing.md §16 |

The six properties the phase adds:

1. **Editing is one mutation.** `RoboTextEditorReplace` is the only door that changes the text; insert,
   replace-selection, backspace, forward delete and every command are that door with a computed range. So no
   two ways to change the text exist, and a transaction's cancel is a *replace* rather than a replay.
2. **The text stays logical.** The editor owns a `RoboTextBuffer`, a 12.21 selection and a caret that is a
   position, an affinity and two flags — **no geometry**, because a caret's form is a layout's answer and this
   layer has no layout. Shaping and layout are downstream consequences of the new text, never of an edit that
   describes glyphs.
3. **A boundary is a rule, and the rules it lacks are refused by name.** A scalar step that can never split a
   UTF-8 sequence, extended over the combining marks that hang off a base, and — with a resolver a caller
   installs — the **shaped** clusters, so a ligature, an Arabic joining sequence or a Devanagari conjunct is
   one editing unit. Word and line boundaries are `RoboNotSupported` rather than `position ± 1`.
4. **One editor for every writing mode and every direction.** There is no vertical editor and no RTL editor:
   the writing mode is data on the shape options and on every run (12.20), the editor never sees it, and a
   column's clusters are in source order whatever the block axis does.
5. **Invalidation is paragraph-widened and says which passes are needed.** Five ranges and seven flags, two of
   them discriminating: whether the level runs can have moved (a right-to-left scalar, or a *number*), and
   whether a newly inserted scalar may be covered by no face the previous selection used. A one-scalar edit
   can change its neighbours' shaping, and a paragraph is the smallest range that is provably sufficient
   without a shaping context this layer does not have — a later, finer contract is a change of *range*.
6. **Composition is text with a kept slice.** The composing text lives in the buffer so the caret and every
   query work during it; what makes it distinguishable from committed text is `textBefore`, the slice it
   replaced — so committing publishes the whole composition as one operation and asks only for a repaint,
   while cancelling restores the text **and** the selection and caret from before it began.

Every edit carries the record an undo history needs — the range, both texts, both selections, both carets —
and the history itself is deliberately not built: `Undo`/`Redo` are refused by name and `undoHistory` is `0`.
The version moved to `1.13.0`, a minor move because nothing a caller already used changed shape: the editor's
state is 12.4's buffer plus 12.21's selection and caret, and the records it adds are new types in a new header.
`RoboTextGetCapabilities` reports `textEditing`, `editOperations`, `editingBoundaries`, `clusterBoundaries`,
`editInvalidation`, `editTransactions`, `compositionState`, `editTransformations`, `editingDiagnostics` and
`editingFuzzing` `1`, each read field by field in `Tests/Text/SuiteRoboTextConformance.c` — with
`undoHistory`, `wordBoundaries` and `lineBoundaries` `0`, each a named absence rather than an oversight.

## 25. Phase 12.23 — the whole-pipeline conformance corpus

12.23 adds no engine work: it is the **executable contract** over everything the block built, at the grain the
layer suites cannot see. `SuiteRoboTextShapingCorpus.c` (`text.shaping.corpus.*`, 13 cases) takes one sample
per category from UTF-8 to a measurement and asserts the **joins** between the layers, with each step checked
against the previous step rather than against a constant, so a failure names the stage that moved.

| Area | Where | Contract |
|---|---|---|
| The corpus, the malformed matrix, the determinism sweep, the report and the timing record | `Tests/Text/SuiteRoboTextShapingCorpus.c` | [ShapingConformance.md](ShapingConformance.md) §8 |
| The fixtures it uses, built byte by byte | `RoboTextConformanceFont.h`, `RoboTextComplexFont.h` | ShapingConformance.md §2 |
| The measurement it ties out | `RoboTextMeasureGlyphRunList` — a door **over the list** | Measurement.md |
| The selection and editing it drives | 12.21's map, 12.22's editor | TextSelection.md, TextEditing.md |

The four properties the phase adds:

1. **The measurement agrees with the runs, by construction.** The corpus calls the measurement door with the
   run list and requires its totals to equal the list's own — two implementations of the same fact meeting,
   not a third sum written in a test.
2. **A gap is a value, not a silence.** Nine Indic scripts whose positional rules this build lacks are asserted
   through their own table rows (their virama, and every reorder array empty); the four Southeast Asian scripts
   answer a strategy with no stages; Tibetan and Han answer `None`. A script that borrowed Devanagari's rules
   would fail here rather than pass quietly.
3. **Malformed input has a stated answer.** Eleven single-corruption variants, each shaped twice: a
   `GSUB`/`GPOS`/`GDEF` whose contents are out of range or truncated is refused (`RoboParseError`), a `cmap`
   that maps nothing and a `maxp` that claims one glyph are refused whole, a zero `unitsPerEm` still shapes —
   and the one variant that refuses the **face** is the directory entry that points outside the source.
4. **The same request answers the same way, and the answer is diffable.** Eight samples × 100 repetitions,
   compared field by field and byte for byte on the run-list block, plus `Build/Text-ConformanceCorpus.txt` —
   one line per sample — which two generations must produce identically.

No capability flag accompanies this phase: the corpus is a statement about the engine, not a thing the engine
does, and `RoboTextGetCapabilities` still reports `1.13.0`'s set. What the phase did change is the **dumps**:
a glyph's line is built in a fixed local buffer, and 12.20's fields had pushed a vertical glyph past it, which
made the whole run list's block unobtainable at every destination size — the defect the determinism case found,
now sized for every field plus the longest flags word. That is the phase's own argument for existing: the
contract it enforces is the one that caught a defect the layer suites had no door for.

Regression: `TestRoboText.ps1 -Target Host` (335 cases) and
`TestRoboTextConformance.ps1 -Target Host` (68) contain the corpus; a shaping change is not done until
`TestLayout`, `TestPaint`, `TestGraphics`, `TestRasterizer`, `TestFont` and `TestDesktop` (Host) pass with it.

---

## 26. Phase 12.24 — final integration and API freeze

The phase that **stopped adding capability and made the subsystem one thing**. Three changes, and the first is
the only one that removed code.

### 26.1 One place where a glyph becomes ink

The tree had two text implementations. The **document** path shaped while it recorded (`RoboPaintContext.c`,
12.16) and drew from the display list (`RoboPaintDecoration.c`); the **shell** had its own — a private UTF-8
decoder (`DesktopChromeUtf8Next`), one `cmap` lookup per code point, its own advance sum for a label's width,
and its own pen into `GfxRasterDrawGlyph`. Two implementations of "what does this string look like" is exactly
what this module exists to prevent, and the third copy was about to be written.

The seam that made the removal a *reuse* rather than a move is
`Runtime/Source/RoboPaint/RoboPaintRun.c` + public `Runtime/Include/RoboPaint/RoboPaintRun.h`:

| Door | What it is |
|---|---|
| `PaintRunShape` / `PaintRunShapeScalars` | one shape call and one `RoboTextGlyph` → `PaintGlyph` conversion, closed over the four facts only shaping knows (mapped, source range, ligature components, the run's own orientation) |
| `PaintRunMeasure` | a width with no glyph array kept — the number a label is centred on |
| `PaintRunDraw` | the 2D pen: skip a control, place at `(x + offsetX, baselineY + y + offsetY)`, advance by `(advanceX, advanceY)`, turn a sideways glyph one quarter turn |

The two existing call sites were rewired onto them rather than rewritten, so their records are bit-for-bit what
they were (`TestPaint.ps1` 242/242 is the evidence). The shell now calls `PaintRunMeasure`/`PaintRunShape`/
`PaintRunDraw`, and `RoboDesktop.c` prepares the face's cmap and metrics **where the face enters the shell**,
because a `FontFace` is created unparsed and every door that needs a table answers `RoboNotSupported` until it
is — the same sentence `RoboPaintDecoration.c` has always said for the document path.

### 26.2 The public surface is frozen as an umbrella, with two tiers

`Runtime/Include/RoboText/RoboText.h` names every header in the module, and `Tests/Text/SuiteRoboTextApi.c`
(`text.api.*`, 9 cases) includes **only** it — so a header left out of the umbrella is a compile error — and
then reads the umbrella and matches its list against the directory, so a public header added and forgotten
fails on the day it lands. The tiers are the **seam** (buffer, shape, measurement, runs, selection, editing,
version) and the **engines** behind it (`Gsub`, `Gpos`, `OtLayout`, the complex-script plans, glyphs,
ligatures, kerning) — public because the conformance suites call them, and not what a consumer wants.

The rest of the suite is the part a conformance case cannot state: the documented initial value of every public
`...Init`; the refusals (a `NULL`, a scalar past the end, a byte inside a multi-byte scalar, a context with no
face answering `RoboNotSupported`); stable borrowing (two reads of one result are one pointer, and the run
list's own advance *is* the measurement's width, exactly); and the stress ladder — nothing, one scalar, 4 096
scalars across three scripts with a glyph-per-scalar ceiling, a script the pack does not cover (retained in
full, with a missing count), and twenty-five identical measurements.

### 26.3 The rules are a build step

Four of the phase's ten integration rules are mechanically checkable, and are checked by
`Assert-RoboTextIntegrationRules` in `Runtime/Tools/Build.ps1` (`-Verify`): **one glyph selector** (no
`cmap` lookup in a layout/paint/render/shell/window/display/typography module), **no shaping engine header from
a consumer**, **one ink path** (`GfxRasterDrawGlyph` only in RoboGraphics and the one pen), and **a rasterizer
that knows no text** (`RoboGraphics` may not include `RoboText/` at all). Every allowance is a named file or
directory, so a new call site fails; the patterns were proved able to fail before they were trusted.

Running the scan under `-Verify` also surfaced a boundary that had been failing since 12.17 without anyone
noticing: `Assert-RoboDesktopBoundaries` forbade `RoboLayout` includes in the shell outside three named files,
and `RoboDesktopFontProvider.c/.h` — the layout seam's *concrete* provider, which legitimately includes
`RoboLayoutFontResolver.h` — was the fourth. It is a named exemption with its reason now, rather than a widened
pattern.

Ownership, lifetime, threading, cache boundaries, invalidation, the error-handling table and the
stable/advanced/internal/experimental/deprecated split are written down in `APIStability.md`; the pipeline, the
layer table and the ten rules in `Overview.md`. **No capability flag and no version move accompanies the
phase**, because a freeze adds no engine behaviour — `RoboTextGetCapabilities` still reports `1.13.0`.

Regression: `TestRoboText.ps1 -Target Host` (344 cases, the API suite among them) and
`TestRoboTextConformance.ps1 -Target Host` (68), then `TestLayout`, `TestPaint`, `TestGraphics`,
`TestRasterizer`, `TestFont` and `TestDesktop` (Host) with `TestDesktop.ps1 -Target Qemu` when the guest image
is involved, and `Runtime/Tools/Build.ps1 -Verify` for the scans.
