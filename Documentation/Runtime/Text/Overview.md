# RoboText — Overview

The one place the whole text subsystem is described end to end. Every other document under
`Documentation/Runtime/Text/` is a *phase*: it argues one mechanism and its limitations. This one is the map
that says how the pieces fit, what a consumer may rely on, and where a new piece belongs. It was written in
**Phase 12.24**, which froze the subsystem; when a phase changes a mechanism, its own document changes and the
row it owns here changes with it.

> **RoboText is the only place text becomes an object.** A UTF-8 walk, a `cmap` lookup, a GSUB pass, a kerning
> adjustment, a bidi level or a glyph advance computed anywhere else is a defect, not a shortcut.

---

## 1. The pipeline

```text
UTF-8 source text
      ↓  RoboTextBuffer          (index spaces: byte ≠ scalar ≠ glyph ≠ cluster)
Unicode scalars
      ↓  RoboUnicode             (properties, scripts, Bidi_Class, Joining_Type — Phase 4)
script / direction / text runs
      ↓  RoboTextRuns, RoboTextScriptResolver, RoboTextDirection, RoboTextBidi
analysis + resolved direction + writing mode
      ↓  RoboTextFontContext     (the face from RoboTypography / RoboDesktopFontProvider, or a caller's)
font selection per run
      ↓  RoboTextGlyphs          (cmap → one glyph per scalar, metrics, source mapping)
      ↓  RoboTextGsub            (12.10 plan + 12.19 masked stages)
      ↓  RoboTextGpos            (12.11 positioning: pair adjustment, mark attachment, cursive)
      ↓  RoboTextGlyphRun        (12.14: glyphs, clusters, source ranges, advances, offsets, key)
GlyphRunList  ─┬─→  TextMeasurement   (12.15: the sum of the runs' own advances)
               ├─→  TextSelectionMap  (12.21: position ↔ cluster ↔ geometry, queries only)
               ├─→  TextEditor        (12.22: one mutation, expressed as one replaced source range)
               └─→  GlyphRun → Paint/DisplayList → Rasterizer   (12.16, 12.24)
```

**The arrows only point down.** RoboText may include `RoboBase`, `RoboMem`, `RoboUnicode` and `RoboFont`; it may
not include anything above it, and `Assert-RoboTextBoundaries` in `Runtime/Tools/Build.ps1` fails the build if it
tries. The reverse rule — what the tree above may do *to* text — is `Assert-RoboTextIntegrationRules`, in §4.

---

## 2. The public surface

`Runtime/Include/RoboText/` is the interface, and `Runtime/Include/RoboText/RoboText.h` is the aggregate: it
names every header in the directory, in two tiers (`text.api.header_hygiene` matches its list against the
directory, so a new header nobody added fails a test).

### The seam — what a document, a shell or an input layer should use

| Need | Door |
|---|---|
| a text value | `RoboTextBuffer.h`, `RoboTextTypes.h` (`RoboTextRange`, `RoboTextView`) |
| one call for runs *and* a measurement | `RoboTextShape.h` (`RoboTextShape`, `RoboTextShapeResult*`) |
| numbers only | `RoboTextMeasurement.h`, `RoboTextShapeResultMeasure`, `RoboTextMeasureText` |
| an ordered, visual-ordered run list | `RoboTextRunList.h` |
| the neutral fallback chain | `RoboTextFontSelection.h` |
| a position from a point, a caret, rectangles | `RoboTextSelection.h` |
| a text an input layer can edit | `RoboTextEditing.h` |
| what this build does and does not do | `RoboTextVersion.h` |

### The engines — public, and not what a consumer wants

`RoboTextGsub.h`, `RoboTextGpos.h`, `RoboTextOtLayout.h`, `RoboTextComplex.h`, `RoboTextArabic.h`,
`RoboTextIndic.h`, `RoboTextLigature.h`, `RoboTextKerning.h`, `RoboTextGlyphs.h`. They are public because the
conformance suites and RoboText's own sources are their callers. A module above RoboText that includes one is
failing the scan in §4 — the glyph run is what it wants instead.

---

## 3. What each layer owns, and what it may not grow

| Layer | Owns | Must never |
|---|---|---|
| RoboFont (5) | the font file, `cmap`, `hmtx`/`vmtx`, GPOS/GSUB **tables**, outlines | shape, segment text, or choose a face |
| RoboUnicode (4) | every Unicode property, script, bidi class, joining type, grapheme/word/line break | store a glyph or a font |
| **RoboText (12)** | the text model, runs, shaping, glyph runs, measurement, selection mapping, editing records | parse a font, keep a Unicode table, hold a layout tree or a paint record |
| RoboTypography (12.3) | role → face + size/weight/leading, bound and cached | shape, rasterize, or parse a font |
| RoboLayout (9) | line boxes, geometry, the *request* for a font (`LayoutFontRequest`) | walk UTF-8, look up a glyph, or keep a font database |
| RoboPaint (11) | display-list records, `PaintGlyph`, one pen (`RoboPaintRun.c`) | select a glyph, or measure a run itself |
| RoboRender / RoboGraphics | pixels, glyph masks, coverage antialiasing | learn a script, a language, a feature or a writing mode |
| RoboDesktop (11) | the shell, its chrome, and the **font provider** the layout seam asks | decode UTF-8, look up a cmap entry, or place a baseline of its own |

---

## 4. The ten rules, and the four a build enforces

Phase 12.24 §37 states ten rules. Four of them are mechanically checkable and are checked — by
`Assert-RoboTextIntegrationRules` in `Runtime/Tools/Build.ps1`, which runs under `-Verify`:

| Rule the scan enforces | Allowed callers |
|---|---|
| **one glyph selector** — no `FontFaceLookupGlyph`/`FontCmapLookup` in a layout, paint, render, shell, window, display or typography module | RoboFont, RoboText, and `RoboGraphics/RoboGraphicsText.c` (the named outline bridge) |
| **no shaping above the text layer** — no direct include of `RoboTextGsub.h`, `RoboTextGpos.h`, `RoboTextOtLayout.h`, the complex-script headers, `RoboTextGlyphs.h`, `RoboTextLigature.h`, `RoboTextKerning.h` from a consumer, in source or in a public header | RoboText's own sources and the test suites |
| **one ink path** — no `GfxRasterDrawGlyph`/`…Coverage` outside the graphics module | RoboGraphics, and `RoboPaint/RoboPaintRun.c` (the one pen) |
| **a rasterizer that knows no text** — `RoboGraphics` may not include `RoboText/…` at all | nothing |

The other six are properties rather than call sites, and are kept by review: *no subsystem paints text from
Unicode* (§3's table is the check), *no second font fallback* (one chain, `RoboTextFontSelection`), *no manual
RTL reversal* (visual order is `RoboTextGlyphRunListVisual*`), *no hand-reproduced GSUB/GPOS* (one plan, one
scan), *measurement consumes final shaped geometry* (`RoboTextGlyphRunListAdvance` *is* the width), *selection
and editing are source coordinates* (`RoboTextPosition`, `RoboTextRange`).

The scan was written in the phase that needed it most: `RoboDesktopChrome.c` had grown its own UTF-8 decoder,
its own per-code-point `cmap` loop, its own advance sum and its own pen — the second text implementation this
subsystem exists to prevent. It now shapes through `RoboTextShape` and draws through `PaintRunDraw`.

---

## 5. Consequences a consumer can rely on

- **A run's advances are the pen's.** `RoboPaintRun.c` moves a 2D pen by `(advanceX, advanceY)` and branches on
  nothing; the pair *is* the writing mode. `RoboPaintDecoration.c` adds the same pair. There is one pen.
- **A measurement is a record, not a recount.** `RoboTextMeasurement.width` is the sum of the runs' advances,
  computed once, and `RoboTextMeasureText` and `RoboTextShapeResultMeasure` answer the same number
  (`text.api.measurement_agreement`).
- **A selection is a source offset.** No door in `RoboTextSelection.h` takes a glyph index, a byte offset or a
  point. `RoboTextSelectionMapCaret` never fails for an unplaceable position: `valid` is 0 with zero spans.
- **An edit is one replacement.** Every command name reduces to `RoboTextEditorReplace`, so a transaction's
  cancel is a replace from a snapshot rather than a reverse replay, and an edit is atomic — the new text is built
  in a clone and installed only on success.
- **A missing glyph is a number.** Text is never silently dropped: the source range, cluster, run, advance and
  mapping survive, and `missingGlyphs` (or `PaintGlyphFlagMissing`) counts what the face could not draw.
- **Nothing has to hold a face alive on RoboText's behalf.** A `GlyphRun` records a *token* in paint and a
  borrowed face in RoboText, and the paint font table is what makes the id meaningful
  (`RoboPaintFontTable.h`); RoboText never retains a font it was not handed.

Details, including the ownership and lifetime statements and what is *not* frozen, are in
`APIStability.md`.

---

## 6. Where a change belongs

| A change | Where it goes |
|---|---|
| a new OpenType lookup type | `RoboTextGsub.c`/`RoboTextGsubSubst.c` or `RoboTextGpos.c`, plus a conformance case |
| a new script with a plan | a row in `RoboTextComplex.c`'s strategy table (plus a module if it has rules of its own), plus `SuiteRoboTextIndic.c`-style fixtures |
| a new CSS font property | the CSS adapter, mapped onto `LayoutFontRequest` (12.17) or the shape options — never onto a glyph |
| a glyph that must render differently | `RoboGraphics`' rasterizer or its bridge; RoboText keeps deciding *which* glyph and *where* |
| keyboard, IME, mouse | RoboInput → `RoboTextEditCommand` → `RoboTextEditing` (Phase 12.22 is the contract it will call) |
| a line box, a break, a justification | RoboLayout, reading `RoboTextMeasurement` and the run list |
| a new text metric | RoboText's measurement engine — never a second sum in a layout or a paint pass |

---

## 7. Visual evidence, and the structural evidence beside it

The rendered half is the shell's galleries, one PNG each, from real frames through the real pipeline:

| Artifact | What it shows |
|---|---|
| `Build/Typography-Desktop.png` | the panel, its roles, the shell's own labels |
| `Build/Typography-Chrome.png` | the chrome at its role sizes |
| `Build/Typography-Document.png` | a document page: Latin, accented Latin, Greek, Cyrillic, mixed-direction text, ligatures, kerning, fallback |
| `Build/Typography-Shaping.png` | the 12.18 shaping specimen |
| `Build/Typography-Vertical.png` | a vertical column at six UI sizes, with the turned glyph |
| `Build/Text-GlyphTable.txt` | the glyph table as text — the structural half of the same story |

The guest half is the QEMU checkpoints (`ROBO_QEMU_{SHAPING,VERTICAL,SELECTION}_*`), and the diffable structural
record is `Build/Text-ConformanceCorpus.txt` (12.23). A change to any mechanism above is not done until
`TestRoboText.ps1`, `TestRoboTextConformance.ps1`, and then the boundary suites the change touches
(`TestLayout`, `TestPaint`, `TestGraphics`, `TestRasterizer`, `TestFont`, `TestDesktop`, all `-Target Host`) pass
with `TestDesktop.ps1 -Target Qemu` when the guest image is involved.

---

## 8. Documents

`Architecture.md` (12.4 — the model and the index spaces) · `UnicodeIntegration.md`, `FontIntegration.md`,
`FontFallback.md` (12.5) · `ScriptRuns.md` (12.6) · `DirectionRuns.md` (12.7) · `GlyphSubstitution.md` (12.10) ·
`Positioning.md` (12.11) · `Ligatures.md` (12.12) · `Kerning.md` (12.13) · `GlyphRuns.md` (12.14) ·
`TextMeasurement.md` (12.15) · `PaintBridge.md` (12.16) · `ComplexScripts.md` (12.19) · `VerticalText.md`
(12.20) · `TextSelection.md` (12.21) · `TextEditing.md` (12.22) · `ShapingConformance.md` (12.18, 12.23) ·
`APIStability.md` (12.24 — what is frozen, and what a consumer may assume) · this file.
