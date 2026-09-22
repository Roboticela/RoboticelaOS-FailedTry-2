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
| `SuiteRoboTextGallery.c` | `text.gallery.*` | the development surface: writes `Build/Text-GlyphTable.txt` for every string the phase names — raw text, run rows, glyph rows with ids/units/advances/source spans, and the measurement — and asserts the table *is* the measurement, plus byte-identical determinism across two builds |

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

## How to run

```powershell
Scripts\Test\TestRoboText.ps1 -Target Host     # 39 cases; also writes Build/Text-GlyphTable.txt
Scripts\Build\BuildRoboText.ps1                # the module, its boundary scan and its suites
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
