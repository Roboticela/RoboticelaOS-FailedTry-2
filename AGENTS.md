# RoboticelaOS — Agent and contributor rules

This is the primary development instruction file. Architecture detail lives in `Documentation/`. Color tokens live in `Documentation/Development/ColorPalette.md`.

There is **one** RoboticelaOS Runtime. Host vs QEMU is a **test execution target**, not a second runtime.

---

## Repository structure

| Folder | Owns |
|---|---|
| `Bootloader/` | UEFI bootloader (`Source/`, `Include/`, `Assets/`, `Config/`) |
| `Kernel/` | Kernel. Internal include paths (`mm/`, `fs/`, …) are the kernel's C include namespace. |
| `Runtime/` | Independent web/UI runtime. Public headers: `Runtime/Include/`. Implementation: `Runtime/Source/Robo*/`. |
| `System/` | Installed system data. `System/Fonts/` is the **system typeface pack**: the one place a system face, its manifest and its licence live. Installed into the image at `/System/Fonts/`. |
| `Desktop/` | Vite + React source for the OS desktop document. Compiled to plain HTML/CSS/JS by `BuildDesktopUi.ps1`. |
| `Libc/` | Freestanding C library for the guest. |
| `User/` | Guest userland programs and the linker script. |
| `Tests/` | All tests, grouped by subsystem. |
| `Documentation/` | All project documentation. |
| `Scripts/Build/` | Per-subsystem build entry points. Build always runs on the development machine. |
| `Scripts/Test/` | Per-subsystem test entry points. Pass `-Target Host` or `-Target Qemu`. |
| `Scripts/Qemu/` | QEMU run/test entry points. |
| `Scripts/pipeline/` | The existing compile/test/run engine (`robo.ps1`). |
| `Build/` | **Generated only.** Never edit. Host, QEMU, intermediate objects, artifacts. |
| `Tools/` | Shared repo tools. |
| `Sdk/` | Guest SDK headers. |

Empty stub trees (`apps/`, `graphics/`, `Drivers/`, `Network/`, `Userspace/`, `Filesystem/`, `Configs/`, `Libsystem/`) were removed. Do not recreate placeholder folders. Kernel drivers live in `Kernel/`; guest programs live in `User/`.

On Windows the filesystem is case-insensitive. Git's canonical names are PascalCase (`Runtime/`, `Scripts/`). `scripts/` and `Scripts/` are the same directory.

---

## Naming rules

| Kind | Convention | Example |
|---|---|---|
| Variables, fields, parameters | camelCase | `windowWidth`, `displayList` |
| Functions | PascalCase | `HtmlParse()`, `BuildDisplayList()` |
| Structs / classes / types / enums | PascalCase | `InputEvent`, `HtmlDocument`, `LayoutContext` |
| Enumerators and project macros | PascalCase with a `Robo` prefix when the name would otherwise collide | `RoboOk`, `RoboInputEventKeyDown` |
| Files | PascalCase | `DisplayList.c`, `HtmlParser.h` |
| Folders | PascalCase | `Runtime/Html/`, `Tests/Layout/` |

The public C API follows the same rules (`HtmlParse`, `LayoutContext`, `RoboOk`). Include guards stay `ROBO_*_H`.

Do not rename POSIX libc names, EFI/Win32/compiler symbols, syscall numbers (`SYS_*`), or third-party / generated identifiers. If a mapped PascalCase name collides with a platform API (`CreateWindow`, `DebugBreak`), prefix `Robo`.

Kernel `#include "mm/page.h"`-style paths stay until a dedicated include migration.

---

## Subsystem ownership

| Work | Location |
|---|---|
| HTML | `Runtime/Source/RoboHTML/` + `Runtime/Include/RoboHTML/` |
| CSS | `Runtime/Source/RoboCSS/` + `Runtime/Include/RoboCSS/` |
| Layout | `Runtime/Source/RoboLayout/` |
| Paint | `Runtime/Source/RoboPaint/` |
| Rasterizer (RoboRender) | `Runtime/Source/RoboRender/` |
| Graphics | `Runtime/Source/RoboGraphics/` |
| Window | `Runtime/Source/RoboWindow/` |
| Input | `Runtime/Source/RoboInput/` |
| Desktop shell | `Runtime/Source/RoboDesktop/` |
| Font | `Runtime/Source/RoboFont/` |
| System typeface pack | `System/Fonts/` (assets + `system-fonts.manifest` + licences), `Runtime/Source/RoboFont/RoboFontSystemPack.c` + public `Runtime/Include/RoboFont/RoboFontSystemPack.h`. Docs: `Documentation/Development/SystemTypefacePack.md`, legal: `Documentation/Legal/FontLicenses.md`. |
| Glyph rasterization (coverage AA) | `Runtime/Source/RoboGraphics/RoboGraphicsGlyph.c` + public `Runtime/Include/RoboGraphics/RoboGraphicsGlyph.h`. One glyph outline → grayscale coverage mask → existing source-over. Independent of shaping: no cmap, no font files, no runs belong here. Docs: `Documentation/Runtime/Typography/TextRasterQuality.md` |
| Typography roles / face binding | `Runtime/Source/RoboTypography/` + public `Runtime/Include/RoboTypography/`. Semantic role → the pack's face + size/weight/leading/optical class. It binds and caches; parsing stays in RoboFont, rasterizing in RoboGraphics. Docs: `Documentation/Runtime/Typography/TypeBinding.md` |
| Text architecture (RoboText) | `Runtime/Source/RoboText/` + public `Runtime/Include/RoboText/`. Text as a first-class object: index spaces, buffer, ranges, script/direction/text runs, glyphs and glyph runs, measurement. It parses no font (RoboFont) and keeps no Unicode table (Phase 4 Unicode). Docs: `Documentation/Runtime/Text/Architecture.md` |
| Direction runs (classification) | `Runtime/Source/RoboText/RoboTextDirection.c` + public `Runtime/Include/RoboText/RoboTextDirection.h`. A scalar's `Bidi_Class` → a direction class, a paragraph context, and one pass to direction runs. Not the bidi algorithm: no levels, no reordering (Phase 12.8). Docs: `Documentation/Runtime/Text/DirectionRuns.md` |
| Glyph substitution (GSUB) | `Runtime/Source/RoboText/RoboTextGsub.c` (the plan, the scan, the counters) + `RoboTextGsubSubst.c` (one reader per lookup type) + public `Runtime/Include/RoboText/RoboTextGsub.h`. The GSUB table itself is Phase 5's (`FontFaceGsub`); this interprets its offsets and writes no font parser of its own. Ligature policy is 12.12's. Docs: `Documentation/Runtime/Text/GlyphSubstitution.md` |
| Ligatures | `Runtime/Source/RoboText/RoboTextLigature.c` + public `Runtime/Include/RoboText/RoboTextLigature.h`; the policy types live beside `RoboTextFeatureSet` in `Runtime/Include/RoboText/RoboTextTypes.h`. Five tags (`liga`/`clig`/`rlig`/`dlig`/`hlig`) as caller-visible policy, the `font-variant-ligatures` vocabulary over it, and the glyph-run report. No second engine: it writes feature tags and GSUB substitutes. Docs: `Documentation/Runtime/Text/Ligatures.md` |
| Unicode / I18N | `Runtime/Source/RoboUnicode/`, `Runtime/Source/RoboI18N/` |
| Kernel | `Kernel/` |
| Boot | `Bootloader/` |
| Tests | `Tests/<Subsystem>/` |
| Documentation | `Documentation/` |
| Build script | `Scripts/Build/` |
| Test script | `Scripts/Test/` |
| QEMU tooling | `Scripts/Qemu/` |

Public interface = `Runtime/Include/<Module>/`. Other subsystems include those headers, never `Runtime/Source/.../Internal`.

---

## Dependency rules

Direction (runtime), downstream may depend on upstream, never the reverse:

```text
RoboBase → RoboMem → RoboContainers → RoboUnicode / RoboI18N
       ↘ RoboFont → RoboTypography → RoboDesktop
       ↘ RoboFont → RoboGraphics → RoboDisplay → RoboWindow
                                    ↘ RoboInput
RoboHTML  ┐
RoboCSS   ┴→ RoboLayout → RoboPaint → RoboRender → RoboDesktop
RoboUnicode / RoboFont → RoboText → RoboLayout / RoboPaint
```

- **Circular dependencies are prohibited.**
- RoboText is the only place text becomes an object: it may include `RoboBase`/`RoboMem`/`RoboUnicode`/`RoboFont` and nothing above them. RoboLayout and RoboPaint consume its results; neither walks UTF-8 or resolves a glyph id itself.
- RoboDesktop must not parse HTML/CSS/layout except through the named document-bridge files.
- RoboRender must not include document-layer headers in module sources.
- RoboInput may not include RoboPaint, RoboRender, or RoboDesktop.
- Do not add Chromium, Skia, V8, or Blink as a build or link dependency. `Chromium/` is reference material only.
- Kernel must not include Runtime headers. Runtime must not include Kernel headers.
- Bootloader must not include Runtime or Kernel implementation files.

Stable APIs: versioned headers under `Runtime/Include/`. New APIs belong in the owning module's public header, not a grab-bag.

### Glyph rasterization (Phase 12.1)

**Rasterizing a glyph outline and shaping text are separate jobs, and the seam between them is a
`GlyphId` plus a device-space baseline origin.** Glyph rasterization lives in RoboGraphics
(`Runtime/Source/RoboGraphics/RoboGraphicsGlyph.c`, public contract
`Runtime/Include/RoboGraphics/RoboGraphicsGlyph.h`); the bridge that turns a glyph id into an outline
stays in `RoboGraphicsText.c`. Nothing below that seam may learn about cmap, font files, CSS
`font-family`, shaping, runs or kerning, and no shaping work may reach around it.

- Public: `GfxGlyphMaskRasterizeOutline`, `GfxRasterDrawGlyphMask`, `GfxRasterDrawGlyphCoverage`,
  the mask accessors, `GfxGammaCoverageToAlpha` and the `GfxCoverageGamma` models.
- Internal: the scanline walk, the winding rule, the flattening tolerances, the 1/256 px lattice and
  the sRGB tables in `RoboGraphicsGlyph.c`. Do not widen them into the header to "share" them.
- Keep three numbers separate: **coverage**, **glyph colour alpha** and **raster opacity**. They meet
  only at `GfxRasterDrawGlyphMask`; folding one into another is a defect, not a simplification.
- Backgrounds, call sites and gamma: `Documentation/Runtime/Typography/TextRasterQuality.md` is the
  phase's contract; update it (and this section) when the model changes.
- Tests: `Tests/Graphics/SuiteRoboGraphicsGlyph.c` (`graphics.glyph.*`) for coverage/bounds/gamma/
  alpha/determinism and `Tests/Graphics/SuiteRoboGraphicsTypography.c` (`graphics.typography.*`) for
  the visual fixture. A glyph change is not done until `TestGraphics.ps1`, `TestPaint.ps1` and
  `TestDesktop.ps1` (Host) pass.

### System typeface pack (Phase 12.2)

**There is exactly one system font root — `/System/Fonts/`, sourced from `System/Fonts/` in the
repository — and the pack is described by its manifest, never by a hard-coded filename.** The pack
binds the existing Phase 5 pipeline to that root; it adds no font parser, no rasterizer and no
shaping.

- Assets: `System/Fonts/<role>/`, licences beside them under `System/Fonts/licenses/`, and the
  manifest `System/Fonts/system-fonts.manifest`. Do not copy a system face anywhere else in the tree,
  and never pack a host font directory (`C:\Windows\Fonts`, `/usr/share/fonts`, `/Library/Fonts`).
- Manifest: `RoboFontSystemManifestParse` is pure (no IO). Changing the grammar, the role vocabulary
  or the bound-family resolution rules means updating `Documentation/Development/SystemTypefacePack.md`
  (and this section) in the same change.
- Roles: callers ask for `RoboFontSystemRoleUIDisplay` / `…UIText` / `…Monospace`, never a filename.
  Role → family is a manifest binding; the selection layer is Phase 5's `FontSelect`.
- Licensing: every redistributed face needs a row in `Documentation/Legal/FontLicenses.md` and its
  licence text under `System/Fonts/licenses/`. Never rename a third-party face to imply it is
  another; the design target is SF-*class* typography, not Apple's SF.
- Build: `BuildFont.ps1`; the image build (`scripts/roboboot-build-common.ps1`) installs the pack and
  **fails** if the face, manifest or licence is missing. A release image must never ship without it.
- Tests: `Tests/Font/SuiteRoboFontSystemPack.c` (`font.systempack.*`) via `TestFont.ps1 -Target Host`,
  including an open of the **shipped** pack. Change the pack and `TestFont.ps1`, `TestGraphics.ps1`,
  `TestPaint.ps1` and `TestDesktop.ps1` (Host) must pass.

### Desktop + document face binding (Phase 12.3)

**A UI surface names a semantic `RoboTypographyRole`, and the role names the face. Nothing above the
resolver knows `/System/Fonts/` exists, what the family is called, or which file it came out of.**
RoboTypography is a module of its own (`Runtime/Source/RoboTypography/`,
`Runtime/Include/RoboTypography/`) because the shell and the document bridge both need it and neither
owns it. It binds and caches; it does not parse, rasterize or shape.

- What each role is set at is one table: `RoboTypographyDefaultScale` in
  `Runtime/Source/RoboTypography/RoboTypographyScale.c`. Never write a font size, weight or leading at
  a paint or measurement site — add or change a scale entry instead. A caller may pass its own scale to
  `RoboTypographyResolverCreate`; that is what makes a restyle a data change.
- Module boundary: RoboTypography may not include `RoboHTML`, `RoboCSS`, `RoboLayout`, `RoboPaint`,
  `RoboRender`, `RoboWindow`, `RoboDisplay` or `RoboGraphics`, and may not use Chromium/Skia. The
  generated document sheet is *text*; checking it is a test's job, not a link. `Assert-RoboTypographyBoundaries`
  in `Runtime/Tools/Build.ps1` enforces both rules — do not widen them.
- The desktop's bindings are the four `RoboTypographyRole` arguments in `RoboDesktopChrome.c`
  (`DesktopTitle`, `ChromeLabel`, `ChromeStatus`, `WindowTitle`), resolved once per frame so that a
  label's measurement and its drawing provably read one style. Baseline placement goes through
  `RoboTypographyStyleApplyFontMetrics`; do not add per-component pixel offsets.
- The document's bindings are `DesktopDocumentConfig.typography`: one face (the `DocumentBody` role's)
  for both measurement and paint, plus the generated default sheet added at `RoboCssOriginRuntime` so an
  author's own rule wins. Element → role is `RoboTypographyRoleForHtmlElement`; a `font-family`
  declaration is deliberately absent from the sheet.
- Weights and optical size **bind but do not instance**: the four weights resolve and are recorded in
  `RoboTypographyStyle.weight`, and no emboldening or slant is ever synthesized. Do not add one.
- Contract and limitations: `Documentation/Runtime/Typography/TypeBinding.md` is the phase's authority;
  update it (and this section) when a role, the scale or a binding changes.
- Build/tests: `BuildTypography.ps1` compiles the module and nothing above it; `TestTypography.ps1
  -Target Host` covers `typography.*`, and a binding change is not done until `TestDesktop.ps1 -Target
  Host` (which includes the three galleries and the boundary scan) passes. `TestDesktop.ps1 -Target
  Qemu` when the guest image is involved.

### RoboText architecture (Phase 12.4)

**Text is a first-class runtime object, not something the renderer discovers while painting. New text
abstractions belong in RoboText; a UTF-8 walk or a cmap lookup written anywhere else is a defect.**
RoboText (`Runtime/Source/RoboText/`, public `Runtime/Include/RoboText/`) is the layer between the font
layer and its two consumers. `Documentation/Runtime/Text/Architecture.md` is the goal's contract.

- Ownership: font parsing, cmap, `hmtx`, fallback and selection are **RoboFont**; Unicode properties,
  scripts, bidi classes and encoding are **Phase 4 Unicode**; line boxes and geometry are **RoboLayout**;
  coverage antialiasing is **RoboGraphics**; role → face is **RoboTypography**. RoboText owns only what a
  piece of text *is*. Never add a second font parser or a second Unicode table to reach around it.
- Boundary: RoboText may include `RoboBase`, `RoboMem`, `RoboUnicode`, `RoboFont`. It may not include
  `RoboHTML`, `RoboCSS`, `RoboLayout`, `RoboPaint`, `RoboRender`, `RoboWindow`, `RoboDisplay`,
  `RoboGraphics` or `RoboTypography` — every one is a text *consumer*, and an arrow back at one would let
  the text layer ask a caller what the answer is. `Assert-RoboTextBoundaries` in `Runtime/Tools/Build.ps1`
  enforces the list; do not widen it.
- Index spaces are distinct types and the conversions are functions with a `Result`:
  `RoboTextByteIndex` ≠ `RoboTextScalarIndex` ≠ `RoboTextGlyphIndex` ≠ `RoboTextClusterIndex`. Scalar→byte
  is **total** (so the end-of-text offset is a byte offset); byte→scalar **refuses** a byte inside a
  multi-byte scalar rather than rounding. Do not use one integer for two of them.
- A glyph's advance is a **device** quantity and `RoboTextFontUnitsToDevice` is the module's one scaling
  door. `RoboTextMeasurement.width` is the sum of the glyphs' own device advances — one rounding per run,
  never per glyph — which is what makes the measurer's answer and the painter's pen sums of the same
  numbers. A missing glyph still advances (glyph 0's advance) and is counted in `missingGlyphs`.
- Direction is **classified, never resolved**: 12.4 reads `Bidi_Class` per scalar and the Unicode
  bidirectional algorithm is 12.8's. Do not claim RTL correctness before then, and do not move a
  neutral's classification without saying so — declaring a paragraph base legitimately changes it.
- `RoboTextGetCapabilities` is the honest record: a later goal that adds an algorithm flips its flag in
  the same change. Add no capability the code does not have.
- The transitional path: `RoboPaintDecoration.c`'s text pass owns no font knowledge. If it ever needs a
  cmap, an id or a scale again, that work belongs in `RoboTextGlyphs.c` —
  `RoboTextGlyphGenerationCmapPerScalar` is the placeholder 12.10 replaces.
- Add no shaped-run behaviour (GSUB, GPOS, ligatures, kerning, complex scripts, bidi, vertical text,
  variable-axis application, `@font-face`, `font-feature-settings`) in this phase's name.
- Tests: `Tests/Text/SuiteRoboText{Buffer,Runs,Glyphs,Shape,Conformance,Gallery}.c` (`text.*`) via
  `TestRoboText.ps1 -Target Host`; the fixture is `Tests/Text/RoboTextTestSupport.h` and it opens the
  **shipped** pack through RoboFont alone. A text change is not done until `TestRoboText.ps1` and the
  suites for every boundary it touches pass — `TestLayout.ps1` (measurement), `TestPaint.ps1` (glyphs,
  the pen, the raster), `TestGraphics.ps1`/`TestRasterizer.ps1` (the glyph-generation seam),
  `TestFont.ps1` (the shipped face), `TestDesktop.ps1` (what the shell draws) — with
  `TestDesktop.ps1 -Target Qemu` when the guest image is involved. Visual evidence: the text half is
  `Build/Text-GlyphTable.txt` (`text.gallery.glyph_table`), and the rendered half is
  `Build/Typography-Desktop.png` / `-Chrome.png` / `-Document.png` — the module links no rasterizer by
  its own boundary, so a rendered glyph run is the shell's gallery, not this directory's.

### Unicode / font integration (Phase 12.5)

**RoboText consumes RoboUnicode and RoboFont; it keeps no Unicode table and parses no font.** New text code
that walks UTF-8, looks up a `cmap`, or reads a script/bidi range table is a defect, not a shortcut. The
public contract is `Documentation/Runtime/Text/{UnicodeIntegration,FontIntegration,FontFallback}.md`.

- Decoding: one decoder, `RoboTextUnicodeDecoder`/`RoboTextUnicodeDecodeAll` in `RoboTextUnicode.c`, over
  `UnicodeUtf8Decode` in `RoboUnicodeReplace` mode. Invalid UTF-8 policy: **one U+FFFD per ill-formed byte,
  advancing one byte**, identical in measurement, rendering, selection and editing. `RoboTextRuns.c` and
  `RoboTextAnalysis.c` call that door; neither decodes itself.
- Properties: every accessor (`RoboTextUnicode{GeneralCategory,Script,BidiClass,…}`) is one call into
  RoboUnicode. `RoboTextUnicodeDataVersion()` reports RoboUnicode's pin — never claim a version the tables
  do not have.
- Scripts: `RoboTextScriptRunsResolve` in `RoboTextScriptResolver.c` is the module's one implementation of
  the Common/Inherited neutrality rule, and the run set and the analysis layer both call it. Since 12.6 it
  resolves a neutral block against **both** neighbours and consults `Script_Extensions`; extend that file
  rather than adding a second segmenter.
- Coverage and selection: `RoboTextFontSelection.c`. Coverage is RoboFont's lazy, idempotent
  `FontFaceParseCoverage` (never re-parse a `cmap`); selection is a deterministic walk of an ordered
  **fallback chain the caller supplies** — no hard-coded family list, no language→font table, no randomness.
  The 256-slot coverage memo is a repeat-catcher, not a database.
- Analysis runs (`RoboTextAnalysis.c`) combine script + classified direction + selected font over one
  scalar range and contain **no glyphs**. Since 12.6 the font is selected **once per script run**, so a run's
  font is constant by construction and a base character and its accent cannot be two faces.
  `RoboTextShapeRequestToRun` feeds a run back into the existing generator — do not add a second one.
- Capabilities: a later goal flips its flag in `RoboTextVersion.h`/`.c` **and** in
  `Tests/Text/SuiteRoboTextConformance.c` in the same change, read field by field. 12.5 set
  `unicodeIntegration`/`codePoints`/`combiningMarks`/`scriptResolver`/`fontCoverage`/`fontSelection`/
  `coverageCache`/`analysisRuns`/`shapeRequest`.
- Tests: `Tests/Text/SuiteRoboText{Unicode,FontSelection,Analysis}.c` (`text.unicode.*`,
  `text.fontselection.*`, `text.analysis.*`) via `TestRoboText.ps1 -Target Host`. The pack ships one family
  (Inter), so fallback is exercised with a no-face primary plus a chain; a real multi-family chain is a
  **pack** change (assets + manifest + licence + image check), not a text-layer change. A 12.5 change is
  not done until `TestRoboText.ps1` and `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` pass.

### Script runs (Phase 12.6)

**Script segmentation describes the linguistic context of text and does not shape it.** The module's one
resolver (`RoboTextScriptResolver.c`) emits half-open `RoboTextScriptRun` ranges in the text's **scalar**
index space, and `Documentation/Runtime/Text/ScriptRuns.md` is the contract: the rule, its order, the
invariants and the ASCII path are stated there and in `RoboTextScriptRunsResolve`'s doc comment.

- One rule, in one order: `prev == next` wins first (no table lookup), then the mark rule, then the
  `Script_Extensions` pass, then carry-left, then carry-right, then `Common`. **A combining mark or
  `Inherited` scalar is resolved before the extension table is consulted** — do not reorder those two, and
  do not let a mark be claimed by a script its base is not.
- `Script` and `Script_Extensions` are different properties (UAX #24). Read both
  (`RoboTextUnicodeScriptInfoOf`, `…ScriptExtensionsInclude`); never derive one from the other, and never
  assume a primary script is a member of its own explicit set.
- `Common` and `Inherited` are **neutral**; a concrete script and `Unknown` are **anchors**. An unassigned
  scalar must stay its own run rather than being absorbed into a neighbour, and an unresolved neutral block
  is `Common` — never a guess. `RoboTextScriptIsAnchor`/`…IsNeutral` are the vocabulary; use them.
- Invariants, for every input: non-empty runs, contiguous with no gaps or overlaps, first starting at 0,
  last ending at the scalar count, no two adjacent runs carrying the same script (coalescing is by
  construction), and **no runs at all** for empty text.
- Cost: one forward pass, and **no allocation** — the only storage is the caller's run array. ASCII is
  answered from the block (`RoboTextUnicodeAsciiScript`, the U+0300 mark bound, the defaulted extension
  set); keep those three bounds proved by `text.script.ascii_fast_path` rather than assumed, and do not add
  mutable state or a global cache to the resolver — it is stateless so concurrent layout is safe.
- The resolver knows nothing about fonts and the selector knows nothing about the text walk. Ask the
  selector `RoboTextFontSelectorSelectRange` **per script run**, never per scalar. A segmentation helper
  that needs a face, a cmap or a glyph is in the wrong file.
- Add no shaping (GSUB, GPOS, ligatures, kerning, joining, Indic reordering), no bidi reordering and no
  visual ordering in this phase's name; direction stays classified and reordering is 12.8's.
- Diagnostics: `RoboTextScriptRunWrite` / `RoboTextScriptRunsWrite` are complete-or-absent. Keep the table
  printing **both** index spaces.
- Capabilities: 12.6 set `scriptExtensions`/`contextualScriptResolution`/`scriptRunDiagnostics` in
  `RoboTextVersion.h`/`.c` (`1.2.0`) and in `Tests/Text/SuiteRoboTextConformance.c`, field by field.
- Tests: `Tests/Text/SuiteRoboTextScript.c` (`text.script.*`) via `TestRoboText.ps1 -Target Host`, and a
  script change is not done until `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` pass. Measured
  cost is recorded by `text.script.performance` (bytes, runs, allocations, time).

### Direction runs (Phase 12.7)

**Direction is classified here and resolved in 12.8. A direction run says which logical context its scalars
belong to — never a level, an order or a visual position.** The module's one resolver is
`RoboTextDirectionRunsResolve` in `Runtime/Source/RoboText/RoboTextDirection.c`; its contract is
`Documentation/Runtime/Text/DirectionRuns.md`, and the public header is
`Runtime/Include/RoboText/RoboTextDirection.h`.

- One rule, in one order: a strong scalar keeps its own direction, a mark takes the character before it
  (W1), a digit-bearing block of number classes is a **number**, and everything else is one neutral block
  resolved against the nearest committed unit on each side (N1). The order, the `sor`/`eor` rule and the
  number's two facts are stated in `RoboTextDirection.h`, in the resolver's file comment and in
  DirectionRuns.md §2 — all three must agree.
- **A digit is not left-to-right.** `RoboTextDirectionClassOf` answers `Number` for `EN`/`AN`/`ES`/`ET`/`CS`
  and `RoboTextDirectionClassIsKnown` has no `Ltr` shorthand for them. A number's direction comes from the
  paragraph (even in either base — UAX #9 I1/I2) and the direction it *lends* the neutral beside it comes
  from W7. Folding either into "digits are LTR" is a defect, and `text.direction.numbers` is where it shows.
- The ASCII shortcut must stay **exact**: `RoboTextDirectionAsciiClassOf` spells out the weak classes the
  block contains (`ET` `#`/`$`/`%`, `ES` `+`/`-`, `CS` `,`/`.`/`/`/`:`). `text.direction.ascii_fast_path`
  proves it equals the table over all 128 scalars, so the resolver may take it first.
- `RoboTextDirectionClassOf` is the **table** with no shortcut, because that is what the shortcut is proved
  against. Do not put the fast path back inside it.
- The paragraph direction is one door, `RoboTextDirectionResolveParagraph`, over the 12.5 P2/P3 scan
  (`RoboTextUnicodeBaseDirection`, allocation-free except for the isolate case, which defers to RoboUnicode).
  `Auto` with no strong scalar answers the context's `fallbackDirection`, which is `Unknown` by default — a
  caller that wants P3's default declares `{ Auto, Ltr }`, which is what the shell does. Do not make the
  layer assume it.
- Consumers, in order: the run set, the analysis's own direction runs and the analysis runs cut against the
  per-scalar array. Ask the resolver, never re-derive a boundary at a call site; `RoboTextRuns.c` and
  `RoboTextAnalysis.c` each call it once and read its two outputs.
- Add no levels, no reordering, no isolate handling, no W2/W4/W5 refinement and no visual order in this
  phase's name — `RoboTextCapabilities.bidiAlgorithm` is `0` and 12.8 flips it.
- Diagnostics: `RoboTextDirectionRunWrite` / `RoboTextDirectionRunsWrite`, complete-or-absent, both index
  spaces, through the table writer they share with the script stage (`RoboTextWriterRunTable`).
- Capabilities: 12.7 set `paragraphDirection`/`directionContext`/`directionClasses`/`directionRunDiagnostics`
  in `RoboTextVersion.h`/`.c` (`1.3.0`) and in `Tests/Text/SuiteRoboTextConformance.c`, field by field.
- Tests: `Tests/Text/SuiteRoboTextDirection.c` (`text.direction.*`) via `TestRoboText.ps1 -Target Host`, and
  a direction change is not done until `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` pass.
  Measured cost is recorded by `text.direction.performance` (bytes, runs, allocations, time).

### Glyph substitution (Phase 12.10)

**GSUB changes glyph identity and sequence, never the Unicode text.** The table belongs to the font: Phase 5's
`FontFaceGsub` parses and validates it once per face, and `Runtime/Source/RoboText/RoboTextGsub.c` (the plan,
the scan and the counters) plus `RoboTextGsubSubst.c` (one reader per lookup type) interpret its offsets. A
second font parser or a second coverage parser here is a defect. `Documentation/Runtime/Text/GlyphSubstitution.md`
is the phase's contract.

- Feature selection is the font's, in the font's order: `RoboTextGsubPlanResolve` walks the ScriptList → the
  LangSys (the requested language's, else the script's default, else nothing) → the FeatureList filtered by
  the caller's settings, and emits the enabled features' lookup indices **deduplicated and sorted**. Sorting
  by feature tag or by mention order is a defect; `text.gsub.lookup_order` asserts the list's order, and a
  contextual record's nested lookup is applied through `RoboTextGsubApplyLookupAt` and counted.
- The **glyph buffer** is the mutable working set (`RoboTextShapingGlyph`: id, source range, cluster, level,
  `inputGlyphId`, `ligatureComponents`, flags); `RoboTextGlyphRun` is the finished result, rebuilt once after
  the last lookup so every advance is asked of the face for the *resulting* id. A ligature's advance is not
  the sum of its components'.
- **Ignored glyphs are stepped over, never removed**: matching walks `RoboTextGsubNextParticipating` /
  `…PrevParticipating`, which read the lookup flag word against GDEF's glyph classes. Moving backwards without
  decreasing is the bug that made every chaining backtrack compare the current glyph to its own pattern; the
  helper's comment says so.
- All eight lookup types run (`RoboTextGsubLookup*` in `RoboTextGsubInternal.h`), extension unwrapped to the
  real type (and the trace reports the **inner** type). `RoboTextGsubLookupAppliesToDirection` is the font's
  `RightToLeft` flag, not a heuristic on the script.
- Reads go through `FontReader`/`FontCheckedRange` and `FontOtResolveOffset16/32`, and a `Result` door is
  always compared (`GsubU16`/`GsubU32`/`GsubI16`): `!GsubReadU32(...)` inverts the question because `RoboOk` is
  zero, and that mistake hid single-substitution format 1, the extension wrapper and GDEF's mark set once.
- Malformed data **skips and continues** (`lookupsSkipped`), bounded by `RoboTextGsubLimits`
  (probes/glyphs/nesting/applications) with `limitReached` reported. A font's contents never fail a call.
- Capabilities: 12.10 set `substitution` and turned `glyphGenerationIsPlaceholder` off; version `1.6.0` with
  12.12. Tests: `Tests/Text/SuiteRoboTextGsub.c` (`text.gsub.*`) over an in-code synthetic GSUB fixture.

### Ligatures (Phase 12.12)

**A ligature is a many-to-one substitution the font asked for, and the characters are never touched.** The
layer is `Runtime/Source/RoboText/RoboTextLigature.c` + `Runtime/Include/RoboText/RoboTextLigature.h`, with the
value types (`RoboTextLigatureKind`, `…Variant`, `…Policy`) beside `RoboTextFeatureSet` in
`RoboTextTypes.h` because `RoboTextShapeOptions` carries one.
`Documentation/Runtime/Text/Ligatures.md` is the phase's authority; update it (and this section) when a kind,
its default or a variant's meaning changes.

- **Five independent tags, not a scale**: `liga`/`clig`/`rlig`/`dlig`/`hlig` as
  `RoboTextLigatureStandard`/`…Contextual`/`…Required`/`…Discretionary`/`…Historical`.
  `RoboTextLigatureKindTag` is the only tag literal in the module; `RoboTextLigatureVariantNone` turns the
  optional kinds off and **must not touch `rlig`** — required ligatures are not decoration, and a build that
  switched them off would produce wrong letters in Arabic rather than a plainer `fi`.
- The policy is a default under the caller, not over it: `RoboTextRunSetCreate` calls
  `RoboTextLigaturePolicyApplyIfUnset`, so a tag the caller named in `options.features` is left alone.
  `RoboTextLigaturePolicyApply` (overwriting) is for a layer that owns the whole answer.
- The CSS boundary is `RoboTextLigatureVariant` → `RoboTextLigaturePolicyFromVariant` and nothing else: no CSS
  parser, no tag written by a stylesheet layer, and two variants compose the way two declarations do.
- **Provenance is not optional**: a ligature glyph keeps the union of its components' source range and
  `RoboTextGlyph::ligatureComponents` (>= 2 means many-to-one), so selection, hit testing and later caret work
  can still reach the scalars it swallowed. `RoboTextLigatureReport`/`…ReportWrite` and
  `RoboTextGlyphRunLigatureAt` are the read doors; nothing here re-derives a component count from a glyph
  count a later lookup could have changed.
- Measurement and paint follow for free: the advance is the resulting glyph's own `hmtx` entry and the
  display list receives glyph runs, so **no code-point paint path may be added**. GPOS interaction (a mark
  after a ligature) is 12.11/12.13's and `positioning` is still `0`.
- Capabilities: 12.12 set `ligatures`/`ligaturePolicy`/`ligatureProvenance` (version `1.6.0`). Tests:
  `Tests/Text/SuiteRoboTextLigature.c` (`text.ligature.*`) plus the fixture's longest-match case in
  `text.gsub.ligature`. The shipped Inter carries `dlig` but **no `liga`**, so standard ligatures are proven
  against the synthetic fixture and the real-font evidence is `!?` → `‽` under `dlig`.

---

## Test rules

Host tests run `Build/Runtime/RoboTest.host.exe` on the development machine. QEMU tests boot the real OS.

| Change | Build (always on the development machine) | Test |
|---|---|---|
| HTML | `Scripts/Build/BuildHtml.ps1` | `Scripts/Test/TestHtml.ps1 -Target Host` |
| CSS | `BuildCss.ps1` | `TestCss.ps1 -Target Host` |
| Layout | `BuildLayout.ps1` | `TestLayout.ps1 -Target Host` |
| Paint | `BuildPaint.ps1` | `TestPaint.ps1 -Target Host` |
| Rasterizer | `BuildRasterizer.ps1` | `TestRasterizer.ps1 -Target Host` |
| Graphics | `BuildGraphics.ps1` | `TestGraphics.ps1 -Target Host` |
| Glyph rasterization / typography | `BuildGraphics.ps1` (and `BuildFont.ps1` if RoboFont changed) | `TestGraphics.ps1 -Target Host`, then `TestPaint.ps1 -Target Host` and `TestDesktop.ps1 -Target Host`; `-Target Qemu` when the guest image is involved. Visual evidence: `Build/Typography-Coverage.png` against `Build/Typography-Stroke.png` |
| System typeface pack | `BuildFont.ps1` (module + pack validation; the image build installs `System/Fonts/`) | `TestFont.ps1 -Target Host` (`font.systempack.*`); `-Target Qemu` when the installed image is involved |
| Typography roles / face binding | `BuildTypography.ps1` (module + boundary scan + its suites); the image builds the module into the guest runtime | `TestTypography.ps1 -Target Host` (`typography.*`), then `TestDesktop.ps1 -Target Host` (`desktop.typography.*` and the three galleries); `-Target Qemu` when the guest image is involved. Visual evidence: `Build/Typography-Desktop.png`, `Build/Typography-Chrome.png`, `Build/Typography-Document.png` |
| Text architecture (RoboText) | `BuildRoboText.ps1` (module + boundary scan + its suites); the image builds the module into the guest runtime | `TestRoboText.ps1 -Target Host` (`text.*`), then the suites for every boundary it touched: `TestLayout.ps1 -Target Host`, `TestPaint.ps1 -Target Host`, `TestGraphics.ps1 -Target Host`, `TestFont.ps1 -Target Host`, `TestDesktop.ps1 -Target Host`; `-Target Qemu` when the guest image is involved |
| Unicode / font integration (RoboText 12.5) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.unicode.*`, `text.fontselection.*`, `text.analysis.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` |
| Script runs (RoboText 12.6) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.script.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` |
| Direction runs (RoboText 12.7) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.direction.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host`. Evidence: the `dir[i]`/`base` rows of `Build/Text-GlyphTable.txt` |
| Glyph substitution (RoboText 12.10) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.gsub.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host`. Evidence: `Build/Text-GlyphTable.txt` (the glyph ids and advances the substitution produced) |
| Ligatures (RoboText 12.12) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.ligature.*` and `text.gsub.ligature`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` |
| Desktop UI | `BuildDesktopUi.ps1` | (compiled into the desktop scene) |
| Desktop | `BuildDesktop.ps1` | `TestDesktop.ps1 -Target Host` or `-Target Qemu` |
| Window | `BuildWindow.ps1` | `TestWindow.ps1 -Target Host` or `-Target Qemu` |
| Input | `BuildInput.ps1` | `TestInput.ps1 -Target Host` |
| Unicode / I18N | `BuildUnicode.ps1` | `TestUnicode.ps1` / `TestI18n.ps1 -Target Host` |
| Font | `BuildFont.ps1` | `TestFont.ps1 -Target Host` |
| Kernel | `BuildKernel.ps1` | `TestKernel.ps1 -Target Host` or `-Target Qemu` |
| Bootloader | `BuildBootloader.ps1` | `TestBootloader.ps1 -Target Host` or `-Target Qemu` |
| Public interface change | owning build + dependents | owning tests, then `TestIntegrationQemu.ps1` if guest-visible |
| Everything | `BuildEverything.ps1` (calls each `Build*.ps1`) | `TestEverything.ps1 -Target Host` or `-Target Qemu` |

Do not force unit tests through QEMU.

---

## Development rules

- Put source files in the owning subsystem. Do not create parallel implementations.
- Do not create a second build system. `Scripts/Build/*.ps1` call `Runtime/Tools/Build.ps1` or `Scripts/pipeline/robo.ps1`.
- Do not bypass subsystem APIs.
- Do not edit generated files under `Build/`.
- Do not introduce circular dependencies.
- Do not add Chromium dependencies.
- Do not scatter documentation outside `Documentation/` except a subsystem `README.md` pointer.
- Do not create a fake Host Runtime / FakeOS / BrowserRuntime.
- Do not make the entire project rebuild for a single-module change. Use the module script. Objects are incremental.
- Incremental objects are only correct if their inputs are tracked. The guest build
  (`scripts/roboboot-build-common.ps1`) tracks runtime headers with `Get-HeaderTreeDeps`, which walks
  `Runtime/Include` **and** `Runtime/Source` recursively, because a module's *internal* header is as much
  a compile input as its public one and a module struct that grew a field would otherwise leave objects
  compiled against the old layout linked in. Add a header root the same way rather than by listing files.
- Do not create unnecessary global state.
- Do not add logs to real OS, logs must be only in tests.

---

## New feature workflow

1. Identify the subsystem.
2. Open its `Runtime/Source/` (or `Kernel/`, `Bootloader/`) folder.
3. Change the public header only if the contract must change.
4. Implement inside the owning subsystem.
5. Add or update tests under `Tests/<Subsystem>/`.
6. Run the subsystem build script.
7. Run the subsystem tests: `Scripts/Test/Test<Part>.ps1 -Target Host` (or `-Target Qemu` when the guest image is involved).
8. Run QEMU tests if the guest image or boot path is affected.
9. Run integration tests if a public interface changed.
10. Update `Documentation/` if architecture or API changed.

---

## Color palette

Product UI color tokens: `Documentation/Development/ColorPalette.md`.
