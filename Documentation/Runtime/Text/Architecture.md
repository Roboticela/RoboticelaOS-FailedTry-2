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
RunSetCreate → GlyphRunGenerate (once per text run) → the measurement, summed across runs
```

`RoboTextShape` does all three in one call, because a caller that wanted the runs *and* the measurement
would otherwise run two of them twice. The result **owns** the run set and the glyph runs and borrows only
the text and the face.

`RoboTextMeasurement` is the layout seam, and it is deliberately **not** a shaped measurement: kerning,
mark advance adjustment and per-glyph positioning all move a run's width and none of them exist yet. What
it has is the property that matters now:

- `width` is the sum of the glyphs' own device advances, accumulated in `double`, rounded once;
- `ascent`/`descent`/`lineHeight` come from the face through its own metrics door, and the line height
  honours a declared leading when the context declares one (`RoboTextLineHeightFromFontMetrics` = CSS's
  `normal` = the face's own extent);
- `missingGlyphs` counts what the face cannot draw, which is the one number that keeps a plausible width
  for undrawable text from passing as a measurement.

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

| Absent | Goal |
|---|---|
| variable-axis application (`wght`/`opsz` beyond face selection) | unowned until RoboFont interpolates outlines |
| GSUB / substitution | 12.10 |
| GPOS / positioning | 12.11 |
| ligatures | 12.12 |
| kerning | 12.13 |
| the Unicode bidirectional algorithm | 12.8 |
| complex scripts | 12.19 |
| vertical text | 12.20 |

Also deliberately absent: grapheme clustering, normalization, case mapping, selection, caret rendering,
editing, shaping, `@font-face`, `font-feature-settings`, `font-variation-settings`, `font-stretch`
matching and full generic-family semantics. The four weights and the `opsz` class **bind but do not
instance**: a style records them and nothing applies an axis.

---

## 10. Build and tests

| | |
|---|---|
| Build | `Scripts/Build/BuildRoboText.ps1` (module + the shipped pack's presence check) |
| Test | `Scripts/Test/TestRoboText.ps1 -Target Host` — `text.*`, 39 cases |
| Suites | `Tests/Text/SuiteRoboText{Buffer,Runs,Glyphs,Shape,Conformance,Gallery}.c` |
| Fixture | `Tests/Text/RoboTextTestSupport.h` — opens the **shipped** pack through RoboFont alone |
| Dev surface | `Build/Text-GlyphTable.txt`, written by `text.gallery.glyph_table` |

The fixture is deliberately built on RoboFont and not on the typography layer: the module's declared
dependencies are `RoboBase`/`RoboMem`/`RoboUnicode`/`RoboFont`, and a fixture that linked the layer above
would let a text change that broke the boundary compile and pass.

A text change is not done until these pass:

| Change | Run |
|---|---|
| anything in RoboText | `TestRoboText.ps1 -Target Host` |
| a boundary RoboLayout crosses (measurement, units) | `TestLayout.ps1 -Target Host` (344 cases) |
| a boundary RoboPaint crosses (glyphs, the pen, the raster) | `TestPaint.ps1 -Target Host` (233) |
| the glyph-generation seam | `TestGraphics.ps1 -Target Host` (177) and `TestRasterizer.ps1 -Target Host` (31) |
| the shipped face or the pack | `TestFont.ps1 -Target Host` (189) |
| anything the shell draws | `TestDesktop.ps1 -Target Host` (54) |
| the guest image | `TestDesktop.ps1 -Target Qemu` |

### The visual test surface, and where the pixels live

The phase asks for a surface showing the raw text, its runs, the glyph ids, the advances and a rendered
glyph run. Those split across two parts, and the split is the module boundary rather than a convenience:
**RoboText links no rasterizer** (the boundary scan forbids `RoboGraphics` and `RoboRender`), so nothing
under `Tests/Text/` can draw a glyph.

| Half | Where | What it shows |
|---|---|---|
| the numbers | `Build/Text-GlyphTable.txt`, written by `Tests/Text/SuiteRoboTextGallery.c` | every named string: raw text, byte/scalar counts, run rows (`script direction [first,last)`), glyph rows (`id`, source span, cluster, device advance, `hmtx` units, offset, `mapped`, metrics status), and the measurement — for the shipped face at the UI Text role |
| the pixels | `Build/Typography-Desktop.png`, `-Chrome.png`, `-Document.png`, written by `Tests/Desktop/SuiteRoboDesktopTypographyGallery.c` | the shell, the window chrome and a document specimen, each drawn through the resolver *and* RoboText |

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
6. **Per-run faces are selected but the shell/document path still paints one face per surface.** A
   document paints through a single face, so `font-weight: 700` reaches layout as a number rather than a
   bolder face, and the analysis layer's multi-face runs are not yet what the desktop walks. Items 5 and 6
   are the same missing thing: a shaped run that can carry an axis value and a face.
7. **No letter spacing applied.** `letterSpacingPx` is carried and not read.
8. **`text.*` has no QEMU-only cases.** The module computes on the development machine; the guest-visible
   half of the phase is the desktop's rendering check above.

---

## 12. Where each phase lands next

```
12.2  System Typeface Pack            ✓
12.3  Desktop + document face binding ✓
12.4  RoboText architecture           ✓
12.5  Unicode / font integration       ✓  (UnicodeIntegration / FontIntegration / FontFallback)
12.6  Script runs                      ✓  (ScriptRuns)
12.7  Direction runs
12.8  Bidirectional algorithm          → fills the direction runs this phase classifies
12.10 Glyph substitution (GSUB)        → replaces RoboTextGlyphRunGenerate's body
12.11 Glyph positioning (GPOS)         → fills RoboTextGlyph.offsetX/offsetY
12.13 Kerning                          → moves advances inside the same records
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
`ligatureProvenance` `1`, and `positioning`/`kerning`/`complexScripts`/`verticalText` stay `0`.
