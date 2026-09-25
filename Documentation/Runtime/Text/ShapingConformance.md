# Shaping conformance (Phases 12.18 and 12.23)

The shaping pipeline already existed before 12.18:

```text
Unicode → script/bidi → font selection → cmap → GSUB → GPOS → GlyphRun → measurement → paint
```

What 12.18 adds is the thing that makes a change to any of those stages *judgeable*: a deterministic corpus
with an expectation at every layer, runnable on its own, whose failures name the layer that broke. It adds no
engine work, and it claims none.

12.23 adds the same claim at the other grain — **one sample per category, end to end**, from the decoder to a
measurement, plus bidi, fallback, vertical text, selection, editing, a malformed-font matrix, a hundred
repetitions per sample and a diffable table (§8). It is where the layers meet that the layer suites cannot
see, and it is where the defect §8 records was found.

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
| `SuiteRoboTextShapingCorpus.c` | `text.shaping.corpus.*` | the **whole pipeline** for one sample per category — decode → script → plan → runs → measurement — plus bidi, fallback, vertical, selection, editing, the malformed matrix, a hundred repetitions per sample, the diffable table and the timing record. See §8 |

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

A second off-screen render sits beside it: 12.20's `GuestVerticalCheckpoint` shapes `"ABC"` with RoboText
directly (the document path carries no writing mode), steps the pen by the run's own `advanceY` and turns the
glyph through the rasterizer, reporting `ROBO_QEMU_VERTICAL_*`. Its ink counts for one glyph upright and turned
are **equal**, which is the vertical phase's own strongest claim and no horizontal case can make it.

A **second** guest specimen joined it in 12.20 and follows the same rule: `GuestVerticalCheckpoint` shapes
`"ABC"` with RoboText directly (`vertical-rl` + `sideways`) and rasterises the run through the same
`GfxGlyphDraw` bridge, reporting `ROBO_QEMU_VERTICAL_*`. Its verdicts are the pen's (`advanceY` 120.000 px over
three em boxes, `advanceX` 0) and the turn's (one glyph upright and turned ink **314 pixels each**, in boxes of
26×30 and 30×26). The document path above has no writing mode to render a vertical page with — the phase's own
non-goal — so this half of 12.20 is shaped rather than laid out, and it says so in
[VerticalText.md](VerticalText.md) §10.

## 6. Running it

```powershell
Scripts\Test\TestRoboTextConformance.ps1 -Target Host   # 68 cases; text.shaping.* only
Scripts\Test\TestRoboText.ps1 -Target Host              # 335 cases; everything under text.*
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

## 8. The whole-pipeline corpus (Phase 12.23)

The suites above each prove one *layer*. `SuiteRoboTextShapingCorpus.c` (`text.shaping.corpus.*`, 13 cases)
proves the **join between them** for one sample per category, from UTF-8 to a measurement, and adds no shaping
of its own: every number it asserts is read off a door that already existed, and the measurement door it uses
is 12.15's `RoboTextMeasureGlyphRunList` — a door **over the run list** — so "the measurement agrees with the
runs" is an equality between two implementations rather than a third sum written in a test.

### Which stage a failure names

§1 of the phase asks that a failure say which stage moved, so the chain is asked one door at a time and each
step is checked against the *previous* step rather than against a constant: a wrong glyph count with a right
scalar count is the shaper, a wrong scalar count is the decoder, a right glyph count with a wrong advance is
GPOS, and a missing run is the resolver. The cases:

| Case | What it proves |
|---|---|
| `.stages` | decode → script runs → complex plan → shape → runs tile → measurement, over Latin, a Devanagari conjunct and Arabic |
| `.scripts` | Greek, Cyrillic, and three scripts in one string, each run carrying its own script |
| `.latin_extended` | the Latin/European table (basic, punctuation, quotes, digits, whitespace, Latin-1, decomposed) with exact glyph and cluster counts |
| `.ligature_kerning` | a feature that moves **glyphs and the measurement**: `ffi` as one cluster with its provenance, `office` four glyphs against six with `liga` off, and `AV` narrower than its two letters |
| `.unsupported_scripts` | the gaps stated as gaps: nine Indic rows with their own virama and no reorder arrays, the four Southeast Asian scripts with no stages, Tibetan and Han answering `None` |
| `.bidi` | logical tiling, resolved levels per run, odd/even by direction, and the visual order as a **permutation** with both directions of the mapping |
| `.fallback` | the selector naming *which* candidate answered, per scalar and per coherent range, with `complete` reported rather than repaired |
| `.vertical` | a column on y with zero width, sideways Latin, and a mark that does not lengthen the column |
| `.measurement_selection_editing` | six measured categories, the selection map over a ligature (a snapped hit, a caret at the cluster start, a round trip), and an edit whose bytes shape **exactly** like a fresh layout of the text it produced |
| `.malformed` | the matrix below, plus the fixture's truncated-directory and GDEF-absent presets |
| `.determinism` | eight samples × 100 repetitions, compared field by field and byte for byte on the run-list block |
| `.report` | the table below, regenerated and compared against itself |
| `.performance` | 1 600 shapes over 13 800 scalars, recorded and printed |

### The malformed matrix (§28)

Eleven variants, each **one** stated corruption of an otherwise good font, each shaped twice. What is asserted
is the part that holds either way: the answer is a `Result`, it is the same on the second ask, and — when the
face *does* shape — the run list is valid and its runs still tile the text. Which way each variant went is part
of the record, printed as one line per variant:

| Corruption | Answer |
|---|---|
| `GSUB`/`GPOS` ScriptList, FeatureList or LookupList offset past the table; either table truncated | `RoboParseError` — a refusal, not a silent skip |
| `GDEF` MarkAttachClassDef offset past the table | `RoboParseError` |
| `cmap` with no subtable | `RoboError` — nothing can be mapped, so the shape refuses |
| `maxp` claiming one glyph | `RoboError` |
| `head` `unitsPerEm` zero | `RoboOk` — the face still shapes, and the case still checks the list |

A **directory entry** that points outside the source is the one case that refuses the whole face, and it is
asserted as a refusal rather than as an absence: a font whose own contents lie about their location is not one
to half-read. Two of the phase's §28 variants are named and **not built**, because a suite cannot assert "did
not hang" — an extension-lookup cycle and an impossible class definition are fixture-layout questions for the
`GSUB`/`GPOS` builders, not patches a corpus can write blind.

### The report, which is the checked-in expected data

`Build/Text-ConformanceCorpus.txt` is written by `.report` — one line per sample: label, scalars, runs,
glyphs, clusters, width, height, misses and the scripts the runs carry. Two generations must be identical, so
it can be diffed, and it is the artifact a reviewer reads first after a shaping change. It is generated by
`RoboTextShape` itself, so it is a regression diff and not a second opinion; no independent shaper produces it.

### What the corpus found

The vertical samples could not be dumped **at any buffer size**: a glyph's diagnostic line is built in a fixed
local buffer, and 12.20's added fields pushed a vertical glyph's line past it — at which point
`RoboTextGlyphWrite` returned `RoboNotSupported` (correctly: complete-or-absent), the run writer propagated it,
and the whole run list's block became absent. A dump that no caller can obtain is not a dump, so both local
buffers are now sized for every field plus the longest flags word rather than for the fields as they happened
to be when they were written. This is exactly the class of defect the phase exists to surface, and it was found
by the determinism case asking for a block nobody had ever asked a vertical shape for.

### What this corpus does not claim

The phase's §2 asks for a directory per script and a file per layer; this build is C with one fixture per font
and one suite per layer, so the **content** lives in the suites §4 lists and a folder per script would be a
second layout convention for the same cases. Also named rather than implied: no checked-in `.ttf` blob (the
fixtures are built byte by byte), no external reference shaper, and no QEMU page of its own — the guest half of
this stack is the shell's `ROBO_QEMU_{SHAPING,VERTICAL,SELECTION}_*` checkpoints, which §5 documents, and the
corpus adds no fourth render.
