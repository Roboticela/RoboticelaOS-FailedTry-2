# Latin & European shaping conformance (Phase 12.18)

The shaping pipeline already existed before this phase:

```text
Unicode → script/bidi → font selection → cmap → GSUB → GPOS → GlyphRun → measurement → paint
```

What 12.18 adds is the thing that makes a change to any of those stages *judgeable*: a deterministic corpus
with an expectation at every layer, runnable on its own, whose failures name the layer that broke. It adds no
engine work, and it claims none.

Authority: this document. Owner: `Tests/Text/` (the fixtures and the suites), because the corpus is a
statement about the text layer's contract and not about any consumer of it.

---

## 1. Why not the shipped pack

The suites run over the shipped `System/Fonts/` pack in every other phase, and deliberately **not** here.

A production face is a moving target: Inter carries `dlig` and **no** `liga`, so a case that expected `fi` to
ligate over the shipped pack would be asserting a fact about Inter rather than about the engine, and would
break the day the pack changed. Conformance needs a font whose bytes state the answer, so
`Tests/Text/RoboTextConformanceFont.h` **builds** one: the repo's corpus policy prefers a synthetic builder in
C over a checked-in blob, and this is the case that policy exists for.

The pack is still exercised — by `text.gsub.shipped_pack`, `text.gpos.shipped_pack`, `text.kern.*` and
`text.ligature.shipped_pack`, which is where "does a real face behave" belongs. The split is: *the engine's
contract* is proved over the synthetic font; *a real font* is proved over the pack.

## 2. The fixture

`RoboTextConformanceFont.h` builds a TrueType face from a spec:

| Table | What the spec states |
|---|---|
| `cmap` | format 12, derived from **one** canonical code point list (ASCII letters and digits, European punctuation, Latin-1 and Latin Extended, Greek upper and lower, Cyrillic upper and lower, five combining marks). Glyph ids follow the list, so `ConfGlyphOf(cp)` and the built table cannot disagree. |
| `hmtx` | `400 + glyphId` units for a canonical glyph, a distinct number for a ligature, **zero for a combining mark** — so "a mark added width" is the engine's defect and not the font's. |
| `GSUB` | one ligature lookup per feature tag: `liga` (`ff`, `fi`, `ffi`, `ffl`), `dlig` (`ct`, off by default), `rlig` (`QQ`, a pair no European word contains so a required ligature never fires by accident). |
| `GPOS` | a pair adjustment under `kern` (`A V`, −80 units) and single adjustments under `mark` (each mark at `x +40, y +120`). |
| `GDEF` | glyph class 3 for the five marks, which is what the engines' `IgnoreMarks` participation reads. |

`ConfSpecEuropean`, `ConfSpecFallbackPrimary` (printable ASCII only), `ConfSpecFallbackSecondary` (Greek and
Cyrillic only) and the `omit*` flags (a face with no layout tables at all) are the four configurations the
suites need. `ConfSpecProbe` is what `ConfFaceOpen` validates the built face against, and it is derived from
the spec rather than fixed at `A`, because the fallback secondary deliberately maps no Latin.

### The one honest deviation

Mark placement is a **single adjustment** (GPOS lookup type 1) under `mark`, not a mark-to-base lookup
(type 4). That is not a shortcut in the fixture: 12.11 executes types 1, 2 and 9 and deliberately skips 3–8,
so a type-4 table would be counted `lookupsUnsupported` and would place nothing. What the suite can prove is
"a mark draws at the offset the font states and advances nothing"; what it must *say* it cannot prove is mark
attachment, and `text.shaping.marks.attachment_not_claimed` asserts `RoboTextCapabilities.complexScripts == 0`
so the limitation has to be flipped in the same change that removes it.

## 3. The case model

`RoboTextConformanceSupport.h` is a fixture, not a second harness (`Tests/Framework/RoboTest.h` is still the
only one). It answers the phase's §2 at every layer:

| Layer | Assertion |
|---|---|
| glyph count | `ConfExpectGlyphCount` — a count, distinct from the scalar count |
| glyph sequence | `ConfExpectGlyphs` — **ids**, never characters, so a substitution is really tested |
| clusters | `ConfExpectClusters` — `(sourceStart, sourceEnd, glyphCount)` triples |
| advances | `ConfExpectAdvance`, `ConfExpectFloatClose` |
| ligature provenance | `ConfExpectLigature` — `ligatureComponents >= 2` and the union source |
| run structure | `ConfExpectRunCount`, `ConfExpectRunClassification` (script **and** direction) |
| record validity | `ConfExpectValid` — `RoboTextGlyphRunValidate` problem by problem |
| determinism | `ConfResultsEqual` — glyph by glyph, offsets and clusters included |

A field a case leaves unset is a claim it does not make: `-1` and `NULL` mean "not checked", which is how a
glyph-only case and a position-only case live in one vocabulary without one of them asserting the other's
silence as agreement.

**Tolerances.** One, named: `ConfPositionTolerance` (0.25 device px) for anything that has been through
`font units → device pixels`, `ConfAdvanceTolerance` (0.05 px) for a total width. Integer comparisons — ids,
counts, source bounds — are exact, because that is the right question for them.

**Failure text.** `ConfReport` prints the case, the input, what was compared and both answers:

```text
Shaping conformance failure: text.shaping.ligature.ffi
  text:     "office"
  glyph ids
    expected: 48 257 50 52
    actual:   48 48 50 50 50 52
```

That is the difference the phase's §42 asks for: a reader can tell *which layer* moved without a debugger.

## 4. The suites

| Suite | Prefix | What it is about |
|---|---|---|
| `SuiteRoboTextShapingLatin.c` | `text.shaping.latin.*` | the basic sets **one character at a time** (so the fixture's own ligature rules cannot turn an alphabet into a feature test), punctuation, whitespace (including what the shaping layer honestly cannot answer about tab and newline), Latin Extended, clusters, classification, determinism, the run dump |
| `SuiteRoboTextShapingScripts.c` | `text.shaping.scripts.*` | every Greek and Cyrillic letter the `cmap` carries, per character; representative words; a Greek diacritic; **three runs for three scripts**, tiling the text in logical order; the script hint |
| `SuiteRoboTextShapingFeatures.c` | `text.shaping.ligature.*`, `text.shaping.feature.*` | `ffi` and the cluster it folds, `"liga" off`, longest-match (`fffi` → `ff` + `fi`), `dlig` default-off and both ways on, `rlig` surviving `font-variant-ligatures: none`, the specific beating the general, the run's own feature record, a range boundary cutting a pair, and `calt` stated as an absence |
| `SuiteRoboTextShapingKerning.c` | `text.shaping.kerning.*` | the `AV` pair moving by exactly the font's value, "not a pair" not moving, `"kern" off`, **positioning separable from substitution** on one run, measurement agreement, a range override, and the pair after a ligature |
| `SuiteRoboTextShapingMarks.c` | `text.shaping.marks.*` | a mark in its base's cluster, the base's width alone, precomposed ≠ decomposed (stated, not assumed), the font's stated placement, two marks on one base, the mark's script, and the attachment absence |
| `SuiteRoboTextShapingFallback.c` | `text.shaping.fallback.*` | the primary answers Latin and the chain answers Greek and Cyrillic; each selected range shapes with **no miss**; a range no candidate covers completely is answered by its base's face with `complete = 0` (never split per scalar); chain order and the no-face answer; one face with no chain |
| `SuiteRoboTextShapingIntegration.c` | `text.shaping.integration.*` | measurement agrees with the shaped advances and the counters, clusters tile and survive a second measurement, determinism over the corpus and a rebuilt face, a font with **no layout tables**, a font whose first lookup is **malformed**, the pen's prefix sum, mixed LTR/RTL structure, L4 mirroring where it applies, the generated reference dump, and the measured baseline |

The bridge assertion (§37/§38) lives with the bridge, not here: `Tests/Paint/SuiteRoboPaintText.c`'s
`paint.glyphrun.agrees_with_shaping` shapes a string with RoboText itself, requires the record and the display
list's copy to carry **that run's own numbers** — id, cluster, source range, advance and offset, exactly — and
requires the pen's prefix sum to be both the run's advance and the measured width. `Tests/Text/` cannot draw
anything (the module links no rasterizer), and `Tests/Paint/` is the layer that owns the record, so that is
where the two sides have to agree.

### Why the fallback cases use the selector and not `RoboTextShape`

`RoboTextShape` takes **one** context, so a text shaped with it is a text with one font — which is the
configuration §12.17 preserves and which the fallback section of that phase's docs describes. Fallback is
12.5's `RoboTextFontSelector`, which decides **for a coherent range** and reports *which* candidate answered.
The cases therefore select, then shape each range with the face the selector named, and require no miss: the
selection has to be *usable*, not merely returned.

## 5. Reference data and the visual half

- `Build/Text-ShapingConformance.txt` — the corpus, its per-case counters, width, and every run's script,
  direction, level and glyphs (id, source, advance, offset, ligature) plus the clusters. Generated by
  `RoboTextShape` itself, so it is a regression *diff* rather than a second opinion.
- `Build/Typography-Shaping.png` — the phase's §33 specimen rendered through the real path
  (`desktop.typography.gallery.shaping`). `RoboText` links no rasterizer by its own boundary
  (`Architecture.md` §10), so the rendered half is the shell's gallery; what it asserts is the four things a
  screenshot test honestly can — the chrome shaped, the page ran runs, the page has ink, and two renders are
  one frame.

### The guest specimen (§44)

The same page is rendered **inside the OS**: the guest program's checkpoint builds the specimen document
through the real chain (HTML → CSS → layout → records → display list → `RoboGraphics`, with the *pack's* face
and the per-run family walk of 12.17) and executes the list off-screen, reporting
`ROBO_QEMU_SHAPING_{RUNS,GLYPHS,MISSING,DRAWN,PIXELS,FAMILIES,UNRESOLVED}`. The values the display test pins
are 8 runs, 268 glyphs, 268 drawn, 11 880 ink pixels, 8 families resolved and **0** unresolved.

`MISSING` is pinned at **9** on purpose. The shipped pack is one family (Inter), so the specimen's Hebrew and
Arabic line has no glyphs; the number states the fallback boundary instead of hiding it, and a run that
dropped those characters would report 0 and fail. Getting this evidence required no change to the shared
checkpoint scene: the specimen is a *second*, off-screen render, so the scene's own frame numbers — pinned
for both launchers — are untouched.

## 6. Running it

```powershell
Scripts\Test\TestRoboTextConformance.ps1 -Target Host   # 55 cases; text.shaping.* only
Scripts\Test\TestRoboText.ps1 -Target Host              # 245 cases; everything under text.*
Scripts\Test\TestDesktop.ps1 -Target Qemu               # the shell and the §44 specimen in the OS
```

A shaping change is not done until `TestRoboText.ps1` and every boundary it touched pass
(`TestLayout`, `TestPaint`, `TestGraphics`, `TestFont`, `TestDesktop`, `-Target Host`), plus
`-Target Qemu` when the guest image is involved — which for this phase it is, because the guest renders the
specimen and its markers are pinned.

## 7. What this layer does not claim

- **Complex-script mark attachment** (GPOS types 3–8). `text.shaping.marks.attachment_not_claimed` pins
  `complexScripts == 0`. Phase 12.19's.
- **Contextual `calt` behaviour**: the fixture has no contextual rule, so
  `text.shaping.feature.contextual_absent` asserts that `calt` on and off produce identical runs. The TODO is
  in the *font*, and a case that pretended otherwise would pass for the wrong reason.
- **Canonical equivalence**: `é` and `e` + U+0301 are not required to produce the same glyphs; the case asserts
  what the font does (one glyph versus two) and that both are valid.
- **Line breaking, caret behaviour, justification, accessibility**: later phases. A newline is an ordinary
  scalar here and is *not* silently treated as drawable — `text.shaping.latin.whitespace_newline` asserts the
  counted miss.
- **Vertical text**: 12.20's work, not this corpus's. `advanceY` is zero for every text *here* because every case
  is shaped with the default horizontal writing mode; the vertical cases that read it non-zero are
  `text.vertical.*` in `SuiteRoboTextVertical.c` over the built vertical fixture. The Latin claims in this file
  hold in either mode, which is the point of the writing mode being data.
