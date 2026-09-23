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

## How to run

```powershell
Scripts\Test\TestRoboText.ps1 -Target Host            # 245 cases; also writes Build/Text-GlyphTable.txt
Scripts\Test\TestRoboTextConformance.ps1 -Target Host # 55 cases; just text.shaping.*
Scripts\Build\BuildRoboText.ps1                       # the module, its boundary scan and its suites
```

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
