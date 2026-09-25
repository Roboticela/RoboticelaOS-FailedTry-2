# RoboText Tests

The text layer (Phase 12.4): text in, runs and glyphs and a measurement out.
Contract: [`../../Documentation/Runtime/Text/Architecture.md`](../../Documentation/Runtime/Text/Architecture.md).

## Suites

| Suite | Filter prefix | What it is about |
|---|---|---|
| `SuiteRoboTextBuffer.c` | `text.buffer.*` | UTF-8 decoding, the scalar index, ASCII and multibyte scalars, the four index spaces, the scalar↔byte doors (total one way, refusing the other), views, slicing and mutation |
| `SuiteRoboTextRuns.c` | `text.runs.*` | the range algebra, script segmentation and neutral inheritance, direction classification against a declared base, run splitting, language validation, shape options |
| `SuiteRoboTextGlyphs.c` | `text.glyph.*` | code point → glyph id, the device advance at the unit boundary, missing-glyph advance and counting, source spans, cluster fields, glyph runs and their mapping |
| `SuiteRoboTextShape.c` | `text.shape.*` | the pipeline end to end: run sets, glyph runs, the measurement's width/ascent/descent/line-height, refusals, determinism, and the brief's required strings |
| `SuiteRoboTextConformance.c` | `text.conformance.*`, `text.vocabulary.*`, `text.font.*` | the module's own record: version, `RoboTextGetCapabilities` field by field, enumerator stability, font-context resolution and scaling, and bit-for-bit repetition |
| `SuiteRoboTextGallery.c` | `text.gallery.*` | the development surface:writes `Build/Text-GlyphTable.txt`
for every string the phase names — 12.4's Latin/ASCII set, 12.6's script samples and 12.7's direction samples — with raw text, run rows, direction-run rows, glyph rows with ids/units/advances/source spans, and the measurement; asserts the table *is* the measurement, plus byte-identical determinism across two builds |
| `SuiteRoboTextUnicode.c` | `text.unicode.*` | the Unicode integration (12.5): the code-point vocabulary, the property doors, the one decoder and its invalid-UTF-8 policy, `RoboTextUnicodeDecodeAll`, the shared script resolver's neutrality rule, and direction classification. Contract: [`UnicodeIntegration.md`](../../Documentation/Runtime/Text/UnicodeIntegration.md) |
| `SuiteRoboTextFontSelection.c` | `text.fontselection.*` | coverage through the face, the deterministic coverage-gated selector, the ordered fallback chain, the range door, the coverage memo's counters, identity equality/order and the dump. Contract: [`FontIntegration.md`](../../Documentation/Runtime/Text/FontIntegration.md), [`FontFallback.md`](../../Documentation/Runtime/Text/FontFallback.md) |
| `SuiteRoboTextAnalysis.c` | `text.analysis.*` | analysis runs: script + classified direction + selected font over one range, mixed-script splitting, combining marks kept with their base, the populated shape request and its bridge back into glyph generation, determinism and fuzz |
| `SuiteRoboTextDirection.c` | `text.direction.*` | the direction stage (12.7): the `Bidi_Class` → direction-class projection with the `EN`/`AN`/`ES`/`ET`/`CS` split kept visible, the paragraph context and its declared modes, run boundaries written as boundary lists for pure, mixed, neutral, number and mark text, explicit overrides, the invariants over every sample and every prefix, determinism, the diagnostic table in both index spaces, one-pass/one-allocation cost, and the integration with the run set, the analysis runs and the shape requests. Contract: [`DirectionRuns.md`](../../Documentation/Runtime/Text/DirectionRuns.md) |
| `SuiteRoboTextScript.c` | `text.script.*` | the script stage (12.6): script boundaries for every named script, mixed-script transitions, punctuation and digits handled contextually, `Script_Extensions` deciding a neutral the anchors disagree about, marks belonging to their base, symbols and emoji, `Unknown` kept apart, the ASCII block bounds proved against the tables, the diagnostic table, and one-pass/one-allocation cost. Contract: [`ScriptRuns.md`](../../Documentation/Runtime/Text/ScriptRuns.md) |

### The shaping conformance corpus (12.18)

Six suites over a **built** font, not the shipped pack, so that a ligature or a kern is asserted because the
font's bytes say so and not because Inter happens to behave that way. Contract:
[`ShapingConformance.md`](../../Documentation/Runtime/Text/ShapingConformance.md).

| Suite | Prefix | What it is about |
|---|---|---|
| `SuiteRoboTextShapingLatin.c` | `text.shaping.latin.*` | the basic sets one character at a time, punctuation, whitespace, Latin Extended, clusters, classification, determinism, the run dump |
| `SuiteRoboTextShapingScripts.c` | `text.shaping.scripts.*` | Greek and Cyrillic letter by letter, representative words, a Greek diacritic, three scripts in three runs, the script hint |
| `SuiteRoboTextShapingFeatures.c` | `text.shaping.ligature.*`, `text.shaping.feature.*` | `ffi` and its cluster, `liga` off, longest match, `dlig` both ways, `rlig` surviving a policy, the specific beating the general, a range boundary, and `calt` stated as an absence |
| `SuiteRoboTextShapingKerning.c` | `text.shaping.kerning.*` | the `AV` pair moving by the font's own value, non-pairs, `kern` off, positioning separable from substitution, measurement agreement, a range override |
| `SuiteRoboTextShapingMarks.c` | `text.shaping.marks.*` | a mark in its base's cluster, the base's width alone, precomposed ≠ decomposed, the font's placement, a second mark, and the attachment this build does not claim |
| `SuiteRoboTextShapingFallback.c` | `text.shaping.fallback.*` | the chain answering Greek and Cyrillic, every selected range shaping with no miss, a range no candidate covers, chain order, no chain |
| `SuiteRoboTextShapingIntegration.c` | `text.shaping.integration.*` | measurement against the shaped advances, clusters surviving, determinism, a face with no layout tables, a malformed lookup, the pen's prefix sum, mixed LTR/RTL, the reference dump, the baseline |

The fixture these share — `RoboTextConformanceFont.h` (the built face and its four specs) and
`RoboTextConformanceSupport.h` (the case model, the tolerances, the failure text) — is documented in
`ShapingConformance.md` §2–§3.

The rendered half of that surface lives with the shell, because `RoboText` links no rasterizer
(`Build/Typography-Desktop.png`, `-Chrome.png`, `-Document.png` from
`Tests/Desktop/SuiteRoboDesktopTypographyGallery.c`). See `Documentation/Runtime/Text/Architecture.md`
§10 for why the split is the module boundary and not a convenience.

The boundaries this module feeds live with their own modules: `text.*` says nothing about whether
`RoboLayout` measured with the right face or `RoboPaint` drew the right glyphs. Those are
`Tests/Layout/SuiteRoboLayoutText.c`, `Tests/Paint/SuiteRoboPaintText.c` and the desktop's own
`desktop.typography.*` galleries.

## The fixture

`RoboTextTestSupport.h` is the fixture every suite shares, and it is **not** a framework — the runtime has
exactly one of those (`Tests/Runtime/Framework/RoboTest.h`).

- `TextFixtureOpen(root)` opens the **shipped** `System/Fonts/` pack through **RoboFont alone** and builds
  a font context over it. Every case that needs a face uses this rather than a hand-built one, because a
  suite that tested a substitute could pass while the real pack was broken.
- The fixture deliberately does **not** link the typography layer. RoboText's declared dependencies are
  `RoboBase`/`RoboMem`/`RoboUnicode`/`RoboFont`; a fixture that reached one layer up would let a change
  that broke that boundary compile and pass. Role → face is `Tests/Typography`'s job.
- `root == NULL` means the repository's own `System/Fonts`.
- Every helper asserts rather than returning a status, and every message names the step that failed.

The shaping conformance suites share a **second** fixture, `RoboTextConformanceFont.h`, which builds a face
from a spec instead of opening one. It is deliberately not a way to avoid the shipped pack: what the pack
proves (`text.gsub.shipped_pack`, `text.gpos.shipped_pack`, `text.kern.*`) stays over the pack, and only the
*engine's own contract* is proved over bytes this repository states.

### Complex scripts (12.19)

Two suites over the **complex fixture** `RoboTextComplexFont.h`: one built face carrying both scripts, whose
bytes state every number a case asserts. Contract:
[`ComplexScripts.md`](../../Documentation/Runtime/Text/ComplexScripts.md).

| Suite | Prefix | What it is about |
|---|---|---|
| `SuiteRoboTextArabic.c` | `text.arabic.*` | joining (one form per scalar, a transparent mark invisible and a `ZWNJ` ending a connection), the masks, the four contextual forms arriving as four glyph ids, a caller's `init 0` winning, the `lam`+`alef` ligature with its source range and cluster, mark-to-base, mark-to-mark and cursive attachment read off the fixture's anchors, the joining clusters and their safe boundary, the plan and its dump, bidi's direction read rather than recomputed, a base-less mark answering for itself, a face with no layout tables, and a joining sequence answered by the fallback chain without a boundary inside it |
| `SuiteRoboTextIndic.c` | `text.indic.*` | syllables and their base, the conjunct as one glyph over three scalars, the half form, the reph written first and drawn last with sources unmoved, the pre-base matra shaped before its consonant, the post-base one, the per-scalar masks, the three stages and the script plan's own facts, the safe-boundary answers, the diagnostics, an orphan virama and a matra before any consonant, Latin + Arabic + Devanagari in one logical string, and a conjunct and a joining sequence answered by the fallback chain with the runs' clusters tiling them |

### Vertical writing (12.20)

One suite over the **vertical fixture** `ConfSpecVertical` in `RoboTextConformanceFont.h`: a `vhea`/`vmtx`
pair, a `vert` feature substituting two upright forms, and a `cmap` spanning Han, Hiragana, Latin, a digit,
Arabic, CJK punctuation and halfwidth Katakana. Every glyph's horizontal advance is `400 + id` and its
vertical one `1000 + id`, so a build that reused `hmtx` for `advanceY` fails rather than agrees, and every
glyph id is derived from the fixture's run array so adding a run cannot move an expectation. Contract:
[`VerticalText.md`](../../Documentation/Runtime/Text/VerticalText.md).

| Suite | Prefix | What it is about |
|---|---|---|
| `SuiteRoboTextVertical.c` | `text.vertical.*` | the writing mode's inline/block axes and the unknown mode's horizontal default, the three orientation tables with the **rotated** one beating the script, whitespace taking its neighbour, orientation as a run boundary with the forced modes, `vert` substituting through the ordinary GSUB engine vertically and not horizontally, `advanceX == 0` and `advanceY` the substitute's `vmtx` entry, the origin as a placement offset, a face with no `vmtx` shaped at its em box and counted, the `mark` attachment in both modes with the pen's step taken along the inline axis, a vertical run answered by the fallback chain keeping the mode and the `vmtx` advances, the measurement's `advanceY` with `width == 0`, an Arabic run still RTL in a column, the dump, and determinism |

## Selection (12.21)

One suite over the **12.18 conformance fixture** — Latin `ffi` under `liga`, zero-advance combining marks, the
complex fixture's Arabic and `ConfSpecVertical`'s `vmtx` column. Every expectation is read off the run being
queried (its own advances, clusters, metrics) or off the fixture's builder, never a literal pixel; each case
supplies its own **placements**, because where a caller put a run is the one thing a run cannot know.
Contract: [`TextSelection.md`](../../Documentation/Runtime/Text/TextSelection.md).

| Suite | Prefix | What it is about |
|---|---|---|
| `SuiteRoboTextSelection.c` | `text.selection.*` | the vocabulary (anchor/focus surviving normalization and clamping, affinity's involution, the caret's segment reading, the complete-or-absent dumps), the map's refusals and accessors, point → logical position with its signed inline distance and the trailing-whitespace option, the caret's band from a run's own metrics, a hit inside `ffi` snapping to the cluster with the whole glyph selected, a base and its zero-advance marks as one rectangle, an Arabic run's first cluster at its largest inline offset, one source position with two carets one line apart and one rectangle per line when wrapped, the same three queries on the vertical axis, a font boundary that is not a logical boundary, position → caret → hit → position round trips, the dump, and fuzzing |

## Text editing — `Tests/Text/SuiteRoboTextEditing.c` (Phase 12.22)

The layer an input system calls: one buffer, one selection and one caret, and every operation written as a
**single replacement of a source range**. Each case is a mutation, a unit (the boundary a delete uses) or a
consequence (the invalidation record), and the two directions meet in the middle — after an edit the editor's
own bytes are shaped and compared with the shape of the same text built directly (`ConfResultsEqual`), so
"the edit reshapes" is stated about the text rather than about a glyph anyone remembered. Contract:
[`TextEditing.md`](../../Documentation/Runtime/Text/TextEditing.md).

| Suite | Prefix | What it is about |
|---|---|---|
| `SuiteRoboTextEditing.c` | `text.editing.*` | the command and boundary vocabulary with the three refusals, one mutation reached four ways and the selection each leaves, a UTF-8 sequence never split (one, two, three and four-byte scalars) and an ill-formed insertion refused rather than repaired, a base **and its marks** as one unit, `ffi` as one cluster with a resolver and three characters without, `لا` as one shaped cluster over two scalars while the complex module still allows a break between the letters, a joining sequence and a conjunct reshaped by the edit's own text, the bidi flag on for Arabic and a number and off for Latin, the same editor doors over a vertical column, composition (start over a selection, update, cancel restoring the text *and* the selection, commit as one operation), transactions (members, the spanning commit, the restoring cancel), the four position rules and their inverse, the paragraph-widened invalidation and its block, and fuzzing |

## The whole-pipeline corpus — `Tests/Text/SuiteRoboTextShapingCorpus.c` (Phase 12.23)

The suites above each prove one *layer*. This one proves the **join between them**: one sample per category,
from UTF-8 to a measurement, with each step asked of the layer that owns it and checked against the *previous*
step rather than against a constant — so a failure names the stage that moved. It adds no engine work and no
capability flag; the measurement it ties out is the module's own door **over the run list**, so "the
measurement agrees with the runs" is two implementations of one fact meeting. The gaps are asserted as values
(the Indic scripts with no reorder rules, the Southeast Asian strategy with no stages, Tibetan and Han
answering `None`), malformed fonts have a stated answer (eleven single corruptions, each shaped twice), and
eight samples are repeated a hundred times and compared byte for byte. Contract:
[`ShapingConformance.md`](../../Documentation/Runtime/Text/ShapingConformance.md) §8.

| Suite | Prefix | What it is about |
|---|---|---|
| `SuiteRoboTextShapingCorpus.c` | `text.shaping.corpus.*` | the layer chain one door at a time over Latin, a Devanagari conjunct and Arabic; Greek, Cyrillic and three scripts in one string; the Latin/European table with exact glyph and cluster counts; a feature that moves glyphs **and** the measurement; the scripts this build names as gaps; bidi as resolved levels and a visual order proved a permutation; the selector naming which candidate answered; a vertical column, sideways Latin and a mark that does not lengthen it; the measurement, selection and editing round trip, where an edit's own bytes must shape exactly like a fresh layout of the text it produced; the malformed matrix; the determinism sweep; the diffable report; and the timing record |

## The API freeze — `Tests/Text/SuiteRoboTextApi.c` (Phase 12.24)

Every other suite here asks what the engine *computes*. This one asks what a **consumer may rely on**, which
fails differently: a door that starts handing back a pointer into freed memory, or an umbrella header that stops
naming a public header, is invisible until something builds against it. It includes **only**
`Runtime/Include/RoboText/RoboText.h` — so a header left out of the aggregate is a compile error — and then
reads that header and matches its list against the directory, so a *new* public header nobody added fails on the
day it lands. The rest is the initial value of every public `...Init`, the refusals a caller can provoke (a
`NULL`, a scalar past the end, a byte inside a multi-byte scalar, a context with no face), the borrowing
lifetime of a shape result's parts (two reads, one pointer; the run list's own advance *is* the measurement's
width), and the shapes a consumer really hands it — nothing, one scalar, 4 096 scalars across three scripts, a
script the pack does not cover, and twenty-five identical measurements. Contract:
[`APIStability.md`](../../Documentation/Runtime/Text/APIStability.md).

| Suite | Prefix | What it is about |
|---|---|---|
| `SuiteRoboTextApi.c` | `text.api.*` | the umbrella's completeness both ways, the version/capability record with its three named absences, the documented initial values, the refusals (including `RoboNotSupported` for a face-less context and an unplaceable position that is *not* an error), stable borrowing and a destroyed result, one measurement engine behind two doors, the index-space conversions with their two refusals, and the stress ladder |

## How to run

```powershell
Scripts\Test\TestRoboText.ps1 -Target Host            # 344 cases; also writes Build/Text-GlyphTable.txt
Scripts\Test\TestRoboTextConformance.ps1 -Target Host # 68 cases; just text.shaping.*
Scripts\Build\BuildRoboText.ps1                       # the module, its boundary scan and its suites
```

`TestRoboText.ps1` also writes `Build/Text-ConformanceCorpus.txt` — the corpus's one-line-per-sample table,
regenerated and compared against itself, so it can be diffed after a shaping change.

A text change is not done until the suites for every boundary it touched pass as well —
`TestLayout.ps1`, `TestPaint.ps1`, `TestGraphics.ps1`, `TestFont.ps1`, `TestDesktop.ps1` (all `-Target
Host`). There is no QEMU suite here: the module has no guest-only behaviour, and the guest-visible check is
the shell's own (`TestDesktop.ps1 -Target Qemu`, which boots the image and reads the guest's
`ROBO_QEMU_TYPOGRAPHY_*` and `ROBO_QEMU_DESKTOP_*` markers).

## Adding a regression

1. Decide which layer owns it. A *shape* of text — an index space, a range rule, a run split — belongs
   here; how a face's numbers are used belongs in a `text.font.*` case; what a caller did with the result
   belongs in that caller's suite.
2. Prefer the shipped pack. There is exactly one system font root and a second one would be a lie about
   the shipped image.
3. Assert numbers the *module* can derive — a total, a width sum, a count — rather than a literal that a
   font change would move. When a literal is the point (an upem boundary, an advance at a size), say so in
   the case name.
4. Any case about rejection should also state what is *answered* rather than refused: an unmapped scalar
   is `.notdef` plus a counted miss, and `text.shape.refusals` keeps the two apart.
5. If the regression is about what a *font* asked for — a substitution, a pair, a mark placement — it belongs
   in the conformance corpus over the built face, where the expectation is the font's own bytes rather than a
   production face's changing opinion. Name the layer that moved in the filter (`…ligature.…`,
   `…kerning.…`, `…marks.…`), because that is what a reader greps for.
