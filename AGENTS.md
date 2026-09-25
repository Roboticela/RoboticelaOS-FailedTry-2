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
| Layout out-of-flow placement (14.0) | `Runtime/Source/RoboLayout/RoboLayoutAbsolute.c` + public `Runtime/Include/RoboLayout/RoboLayoutAbsolute.h` — the **one** answer to "what is this box placed against, how big is it, where does it go" for a box a `position: absolute`/`fixed` declaration takes **out of flow**. Docs: `Documentation/Runtime/Layout/OutOfFlow.md` |
| Paint | `Runtime/Source/RoboPaint/` — including the shared shaped-run seam `RoboPaintRun.c` + public `Runtime/Include/RoboPaint/RoboPaintRun.h`, the one shaping conversion and the one glyph pen every painter in the tree uses (Phase 12.24) |
| Rasterizer (RoboRender) | `Runtime/Source/RoboRender/` |
| Graphics | `Runtime/Source/RoboGraphics/` |
| Compositor (RoboCompositor) | `Runtime/Source/RoboCompositor/` + public `Runtime/Include/RoboCompositor/` — the **software** compositor: a layer tree (parent, surface, rectangle, offset, scale, opacity, clip, visibility), one composite pass into a destination **surface the caller owns**, a damage region list, a tile grid per surface, raster jobs under a per-frame budget with one caller-installed `CompositorRasterFn`, a surface pool with a memory budget, frames with reasons, an interval-clock vsync, scroll offsets, eased animation tracks, a frame **pump with a post queue**, present fences, counters and complete-or-absent dumps. `RoboCompositor.h` is its front door; GPU resources, a compositor thread, double buffering, rotation and colour management are `0` in its capability table **by name**. Docs: `Documentation/Runtime/Compositor/Architecture.md` |
| Window | `Runtime/Source/RoboWindow/` |
| Input | `Runtime/Source/RoboInput/` + public `Runtime/Include/RoboInput/` — the **one** normalization layer between hardware and UI: devices, the normalized event vocabulary, keyboard state and layout, pointer/touch/pen/gamepad, focus, hit testing, routing, gestures, shortcuts, the clipboard, drag/drop, the cursor, replay and synthesis. `RoboInput.h` is its front door. Docs: `Documentation/Runtime/Input/` |
| Keyboard layout + dead keys (13.9/13.10) | `Runtime/Source/RoboInput/RoboInputLayout.c` + public `Runtime/Include/RoboInput/RoboInputLayout.h`. A bounded `(key, level) → character` table, the four levels, the AltGr rule (two spellings, and `Ctrl`-without-`Alt` is **not** one) and the caps-lock rule stated once in `InputLayoutLevelFor`; dead accents are layout *data* — a spacing form, a combining form and a precomposed-pair table — not a text engine. Two tables ship (`US_ASCII`, `US_INTERNATIONAL`); another language is data a caller installs. Docs: `Documentation/Runtime/Input/KeyboardLayouts.md` |
| Text input pipeline (13.11) | `Runtime/Source/RoboInput/RoboInputText.c` + public `Runtime/Include/RoboInput/RoboInputText.h`. The one door from a key event to zero, one or two `TEXT_INPUT` events: layout → pending accent → text. It inserts nothing — the text reaches an editor only through 13.12/13.13's command layer. Docs: `Documentation/Runtime/Input/TextInput.md` |
| Desktop shell | `Runtime/Source/RoboDesktop/` |
| Font | `Runtime/Source/RoboFont/` |
| System typeface pack | `System/Fonts/` (assets + `system-fonts.manifest` + licences), `Runtime/Source/RoboFont/RoboFontSystemPack.c` + public `Runtime/Include/RoboFont/RoboFontSystemPack.h`. Docs: `Documentation/Development/SystemTypefacePack.md`, legal: `Documentation/Legal/FontLicenses.md`. |
| Glyph rasterization (coverage AA) | `Runtime/Source/RoboGraphics/RoboGraphicsGlyph.c` + public `Runtime/Include/RoboGraphics/RoboGraphicsGlyph.h`. One glyph outline → grayscale coverage mask → existing source-over. Independent of shaping: no cmap, no font files, no runs belong here. Docs: `Documentation/Runtime/Typography/TextRasterQuality.md` |
| Typography roles / face binding | `Runtime/Source/RoboTypography/` + public `Runtime/Include/RoboTypography/`. Semantic role → the pack's face + size/weight/leading/optical class. It binds and caches; parsing stays in RoboFont, rasterizing in RoboGraphics. Docs: `Documentation/Runtime/Typography/TypeBinding.md` |
| Text architecture (RoboText) | `Runtime/Source/RoboText/` + public `Runtime/Include/RoboText/`. Text as a first-class object: index spaces, buffer, ranges, script/direction/text runs, glyphs and glyph runs, measurement. It parses no font (RoboFont) and keeps no Unicode table (Phase 4 Unicode). Docs: `Documentation/Runtime/Text/Architecture.md` |
| Direction runs (classification) | `Runtime/Source/RoboText/RoboTextDirection.c` + public `Runtime/Include/RoboText/RoboTextDirection.h`. A scalar's `Bidi_Class` → a direction class, a paragraph context, and one pass to direction runs. Not the bidi algorithm: no levels, no reordering (Phase 12.8). Docs: `Documentation/Runtime/Text/DirectionRuns.md` |
| Glyph substitution (GSUB) | `Runtime/Source/RoboText/RoboTextGsub.c` (the plan, the scan, the counters) + `RoboTextGsubSubst.c` (one reader per lookup type) + public `Runtime/Include/RoboText/RoboTextGsub.h`. The GSUB table itself is Phase 5's (`FontFaceGsub`); this interprets its offsets and writes no font parser of its own. Ligature policy is 12.12's. Docs: `Documentation/Runtime/Text/GlyphSubstitution.md` |
| Ligatures | `Runtime/Source/RoboText/RoboTextLigature.c` + public `Runtime/Include/RoboText/RoboTextLigature.h`; the policy types live beside `RoboTextFeatureSet` in `Runtime/Include/RoboText/RoboTextTypes.h`. Five tags (`liga`/`clig`/`rlig`/`dlig`/`hlig`) as caller-visible policy, the `font-variant-ligatures` vocabulary over it, and the glyph-run report. No second engine: it writes feature tags and GSUB substitutes. Docs: `Documentation/Runtime/Text/Ligatures.md` |
| Complex scripts | `Runtime/Source/RoboText/RoboTextComplex.c` (the strategy table, the plan, the clusters), `RoboTextArabic.c` (joining, the four forms, the Arabic stages) and `RoboTextIndic.c` (the script rules, syllables, masks, the two reorders), public `Runtime/Include/RoboText/RoboTextComplex.h`, `…Arabic.h`, `…Indic.h`; the `Joining_Type` property is RoboUnicode's (`Runtime/Source/RoboUnicode/RoboUnicodeJoining.c`). A script's behaviour is a **plan** — strategy + stages of masked features + reorder steps — so a second script is a row, not an engine. Docs: `Documentation/Runtime/Text/ComplexScripts.md` |
| Vertical writing | `Runtime/Source/RoboText/RoboTextVertical.c` + public `Runtime/Include/RoboText/RoboTextVertical.h`; the vocabulary (`RoboTextWritingMode`, `…TextOrientation`, `…GlyphOrientation`) is in `RoboTextTypes.h`, the `vhea`/`vmtx` door in `RoboTextFont.c`, the placement in `RoboTextGlyphs.c` and the cut in `RoboTextRuns.c`. The writing mode is data on the options and on every run — never inferred from a script — and the OpenType integration is two feature tags (`vert`/`vrt2`) through 12.10's GSUB. No vertical shaping engine. Docs: `Documentation/Runtime/Text/VerticalText.md` |
| Selection mapping | `Runtime/Source/RoboText/RoboTextSelection.c` + public `Runtime/Include/RoboText/RoboTextSelection.h`. The logical ↔ visual bridge: a `RoboTextPosition` (a source scalar offset), anchor/focus selections, the placements a caller supplies, and the three queries — hit test (point → position), caret (position + affinity → geometry) and selection rectangles. It shapes nothing, measures nothing, takes no glyph index, and projects through `RoboTextWritingAxes` so no call site swaps x and y. Docs: `Documentation/Runtime/Text/TextSelection.md` |
| Shaping conformance corpus (12.23) | `Tests/Text/SuiteRoboTextShapingCorpus.c` (`text.shaping.corpus.*`) over the 12.18/12.19/12.20 fixtures. The whole pipeline for one sample per category — decode → script → plan → runs → measurement — plus bidi, fallback, vertical, selection, editing, the malformed matrix, a hundred repetitions per sample, the diffable `Build/Text-ConformanceCorpus.txt` and the timing record. It asserts the joins between layers and adds no engine work; docs: `Documentation/Runtime/Text/ShapingConformance.md` §8 |
| Text editing (RoboText) | `Runtime/Source/RoboText/RoboTextEditing.c` + public `Runtime/Include/RoboText/RoboTextEditing.h`. The editor an input layer calls: one `RoboTextBuffer`, one selection and one caret, and every operation — insert, replace-selection, backspace, forward delete, the command names, composition, transactions — expressed as **one replacement of a source range**. It owns the character and cluster boundaries a delete may land on, the paragraph-widened invalidation record a consumer redoes its work from, and the transformation of positions and ranges across an edit. It shapes nothing and has no caret geometry. Docs: `Documentation/Runtime/Text/TextEditing.md` |
| JavaScript runtime (RoboJS) | `Runtime/Source/RoboJS/` + public `Runtime/Include/RoboJS/` — the **one** JavaScript runtime, independently implemented: a lexer, a recursive-descent parser, an AST and its printer, the six value types, objects/prototypes/attributes, functions and closures, exceptions, a completion-based interpreter under step and depth budgets, a DOM bound through a `JsDomHost` vtable, listeners and events, timers on the caller's clock, one event loop and one microtask queue. `RoboJS.h` is its front door and names no `RoboHTML` type; `RoboJSDomHtml.c` is the **only** file in the module that includes one. Docs: `Documentation/RoboJS/Overview.md`, `Limitations.md`, `APIStability.md` |
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
                                    ↗ RoboInput
       ↘ RoboGraphics → RoboCompositor
RoboHTML  ┐
RoboCSS   ┴→ RoboLayout → RoboPaint → RoboRender → RoboDesktop
RoboUnicode / RoboFont → RoboText → RoboLayout / RoboPaint
RoboHTML → RoboJS → (the shell or document layer that runs a page's script)
```

- **Circular dependencies are prohibited.**
- RoboText is the only place text becomes an object: it may include `RoboBase`/`RoboMem`/`RoboUnicode`/`RoboFont` and nothing above them. RoboLayout and RoboPaint consume its results; neither walks UTF-8 or resolves a glyph id itself.
- **RoboJS is the only place a script is evaluated.** It sits above `RoboHTML` and below the layers that would run a page's script: it may include `RoboBase`/`RoboMem`, the C standard library and — in exactly one file, `RoboJSDomHtml.c` — `RoboHTML`, and nothing above that. `Assert-RoboJSBoundaries` in `Runtime/Tools/Build.ps1` (run by `-Verify`) is the rule as a scan: one runtime, one DOM seam, no second engine (no V8, QuickJS, Duktape, MuJS, JerryScript, Skia or Chromium). A shell that evaluates a script calls `JsRuntimeEvaluate` and reads the loop — a second front end, or a module computing `typeof` for itself, is a defect.
- RoboDesktop must not parse HTML/CSS/layout except through the named document-bridge files.
- RoboRender must not include document-layer headers in module sources.
- RoboInput may not include RoboPaint, RoboRender, or RoboDesktop.
- RoboCompositor may include `RoboGraphics` and **nothing above it** — not RoboRender, RoboWindow, RoboDisplay or RoboDesktop. `Assert-RoboCompositorBoundaries` enforces it for the module *and* its suites.
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
- A quarter turn is `GfxGlyphRasterParams::rotation` (0–3, clockwise) and it turns the **outline**, in the
  one device transform every verb walks — never a mask, never a compositor, never a rotated bitmap. A swap
  and a negation preserve the 1/256 px lattice, so `graphics.glyph.rotation` asserts equal bytes rather
  than a matching area, and a value outside 0–3 draws **unrotated** because `& 3` would turn a typo into a
  turn. `RoboGraphicsText.c` is the only caller that composes it, from the glyph's sideways mark (12.20);
  nothing here learns what a writing mode is.
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

### CSS font wiring — the resolved font request, and the face it resolves to (Phase 12.17)

**CSS chooses the requested typography; the font database chooses the actual font, and RoboLayout owns only
the request and the seam between them.** Two landed halves, one contract; `fontRequest` is set in
`LayoutCapabilities` and `Documentation/Runtime/Layout/FontRequest.md` is the phase's authority.

**The resolved font request** in `Runtime/Source/RoboLayout/RoboLayoutFontRequest.c` + public
`Runtime/Include/RoboLayout/RoboLayoutFontRequest.h` (`layout.fontrequest.*`). It is a **value** record — an
ordered, classified family list plus `sizePx`/`weight`/`stretch`/`italic`/`direction` — never a second font
system.

- `font-family` is kept **ordered and classified**: a name stays a name, a keyword (`sans-serif`, `system-ui`,
  `ui-monospace`, …) stays a `FontGenericFamily` via RoboFont's own `FontGenericFamilyFromName`. **Never
  hard-code `sans-serif → Inter` here** — a generic is resolved below this seam, by the font database.
- The scalar half is **delegated** to `LayoutTextStyleResolve`, so the request and the measurer's
  `LayoutTextStyle` cannot disagree about a size or a weight. Do not re-derive `font-size`/`font-weight`.
- The family list is **bounded** (`RoboLayoutFontMaxFamilies`); overflow drops the tail rather than failing.
  Keep it bounded and pointer-free: `LayoutTextStyle` is copied by value into a `PaintTextRun` (12.16), and a
  pointer in the request would break the paint record model.
- Initial values live in one place: the scalars in `RoboLayoutText.h`, the family as the single generic
  `serif` in the adapter (the registry's own spelled initial). `font-stretch` is read **best-effort** — the
  CSS property is not yet registered — and answers `normal` otherwise.
- RoboLayout's invalidation table already classifies every font property as layout-affecting; do not add a
  second invalidation path for a font change.

**The selection seam** in `Runtime/Source/RoboLayout/RoboLayoutFontResolver.c` + public
`Runtime/Include/RoboLayout/RoboLayoutFontResolver.h` (`layout.fontselect.*`). `LayoutFontRequestSelect` walks
the request's family entries **in author order** and asks a `LayoutFontProvider` — a two-field vtable
(`userdata` + `resolveFamily`), the same shape as the text metrics seam — for each. The first entry that
yields a face wins and the walk **stops**; `LayoutFontSelection.familyIndex`/`fallback` record which one
answered. A miss is `RoboOk` with a NULL face; a provider *failure* propagates and must never be read as "no
match".

- **RoboLayout must not grow a font database.** No file open, no `/System/Fonts/` scan, no `cmap`, no coverage
  walk, no registry, no fallback chain, no per-scalar or per-script splitting, no `@font-face`, no synthesized
  weight or slope. A generic is handed to the provider **as a generic** (`FontGenericFamily`), never as a name.
- The measurer resolves **once per run** and records the answer on the run (`LayoutTextRun.face`,
  `fontFallback`); paint draws with *that* face (`PaintContextRun`'s `run.face ? run.face : textFace`) rather
  than resolving again. A fact a later layer needs is carried, not recomputed — do not add a second
  resolution below layout.
- A build with **no provider** measures every run with the single attached face, exactly as before the phase,
  and that is a supported configuration rather than a degenerate one. `LayoutTextFontSetProvider` is the only
  thing that changes it.
- The one concrete provider is `RoboDesktopFontProvider` (`Runtime/Source/RoboDesktop/RoboDesktopFontProvider.c`),
  over the registry and the system pack: a generic through the pack's configured mapping (then the pack's role),
  the two role spellings `ui-text`/`ui-display` through `FontSystemPackResolveRole`, and a named family through
  `FontRegistryCollectFacesForFamily` + Phase 5's `FontSelect`. Its `resolved`/`unresolved` counters are
  published as `DesktopDocumentReport.fontFamiliesResolved`/`…Unresolved`.
- Diagnostics are two complete-or-absent writers, not a second log: `LayoutFontRequestWrite` (the ask) and
  `LayoutFontSelectionWrite` (the answer), beside the run's own `face`/`fontFallback`.
- Still another row's work, and named rather than implied: per-script fallback chains and mixed-font
  `GlyphRun`s (12.5's `RoboTextFontSelector`, driven from the family list this record preserves), `@font-face`,
  web fonts, and the QEMU visual evidence.

### Latin & European shaping conformance (Phase 12.18)

**A shaping change is judged by a deterministic corpus, not by a screenshot: over a font whose bytes this
repository states, every layer from Unicode to the pen gets an assertion, and a failure names the layer that
moved.** The corpus is `Tests/Text/RoboTextConformanceFont.h` (the built face and its specs) +
`RoboTextConformanceSupport.h` (the case model) + the six `SuiteRoboTextShaping*.c`; its contract is
`Documentation/Runtime/Text/ShapingConformance.md`.

- **The built face, not the shipped pack.** A production face is a moving target — Inter carries `dlig` and no
  `liga` — so a case over it would assert Inter's opinion rather than the engine's. The pack keeps its own
  cases (`text.gsub.shipped_pack`, `text.gpos.shipped_pack`, `text.kern.*`); do not move an engine-contract
  expectation onto the pack, and do not delete a pack case in favour of a fixture one.
- **The fixture states the answer in bytes**: one canonical code-point list drives `cmap` and `GlyphOf`,
  ligature substitutions under `liga`/`dlig`/`rlig`, a `kern` pair (`A V`, −80 units), `mark` adjustments and
  GDEF mark classes, and a zero advance for a mark. A case may not hard-code a number the builder derives.
- **Expectations are per layer and selective.** Glyph ids (never characters), counts, clusters as source
  ranges, advances, `ligatureComponents`, run script/direction, `RoboTextGlyphRunValidate` and determinism are
  each their own door; an unset field means the case makes no claim about it. Tolerances are two named
  constants (`ConfPositionTolerance`, `ConfAdvanceTolerance`) and integer comparisons are exact.
- **Two absences are asserted as absences** rather than passed over: `calt` (the fixture has no contextual
  rule) and, in 12.18's own fixture, complex-script mark attachment — a mark's placement there is proved with
  a type-1 adjustment for exactly that reason. 12.19 flipped the second claim in the same change as the
  engine: GPOS now runs types 3–6, `text.shaping.marks.attachment_claimed` reads it, and the type-1 case stays
  because a single adjustment is still a thing a font does. `calt` remains an absence and is still asserted as
  one — the conformance fixture states no contextual rule, so a case over it would be asserting nothing.
- Fallback is proved through 12.5's selector — which answers **for a coherent range** and names the candidate —
  not by shaping once with one context, because `RoboTextShape` takes one context by design.
- Build/test: `TestRoboTextConformance.ps1 -Target Host` (55 cases, `text.shaping.*`) and
  `TestRoboText.ps1 -Target Host` (the whole `text.*` surface), then every boundary the change touched. The
  reference dump is `Build/Text-ShapingConformance.txt` and the rendered half is the shell's
  `Build/Typography-Shaping.png` gallery — this module links no rasterizer.
- The guest half is `User/robo-runtime/main.c`'s `GuestShapingCheckpoint`: the specimen document built and
erendered **off-screen in the OS**, reported as `ROBO_QEMU_SHAPING_*` and pinned by
`Scripts/pipeline/commands.test.ps1`'s marker list (`MISSING` pinned at 9 — the pack covers no Hebrew or
Arabic, and stating that beats hiding it). It is a second render rather than an edit to the shared checkpoint
scene, whose frame numbers both launchers pin.

### Complex scripts (Phase 12.19)

**A script's shaping behaviour is a *plan*, not a branch: a strategy, stages of features with positional masks,
and reorder steps between them. A second complex script is a new row, not a new engine.** The modules are
`Runtime/Source/RoboText/RoboTextComplex.c` (the strategy table, the plan resolver, the cluster vocabulary and
`RoboTextComplexIsSafeBoundary`), `RoboTextArabic.c` (joining, the four contextual forms, the joining clusters,
the Arabic stages), `RoboTextIndic.c` (the script rules, syllables and their base, the position masks, the two
reorders, the Indic stages) and `Runtime/Source/RoboUnicode/RoboUnicodeJoining.c` (the `Joining_Type`
property). Public contracts: `Runtime/Include/RoboText/RoboTextComplex.h`, `…Arabic.h`, `…Indic.h`;
authority `Documentation/Runtime/Text/ComplexScripts.md`.

- **The plan is data and it is per run.** `RoboTextComplexPlanResolve` asks the strategy's own module for its
  stages, then applies the caller's `RoboTextShapeOptions::features` **as an override** — a tag turned `Off`
  is removed from its stage, so `font-feature-settings: "init" 0` means something. Do not add a
  `if (script == …)` anywhere above the table, and do not let a caller's silence be read as consent: silence
  keeps the strategy's default.
- **Positional masks are the only new mechanism.** One `uint32_t` per scalar is the *position's* answer
  (`RoboTextArabicMasksBuild`, `RoboTextIndicMasksBuild`) and one `uint16_t` per lookup is the *feature's*
  (`RoboTextComplexStageApplyMasks`), and GSUB and GPOS both ask
  `RoboTextOtMaskAllows` at the position a match starts. `RoboTextOtPlan::lookupMasks` is indexed by the
  **Lookup List** index the engine reads it with — never by a position in `lookupOrder`, whose numbers those
  are but whose order they are not. Mask 0 is "everywhere", which is every Latin, Greek and Cyrillic run.
- **Joining is a Unicode property, not a table here.** `RoboUnicodeJoining.h` is the one `Joining_Type` door
  (`UnicodeJoiningTypeOf`, `…JoinsRight`, `…JoinsLeft`, `UnicodeJoiningTypeJoins`), and Arabic's resolver
  reads it. A transparent scalar takes **no form** and does not break a connection; a right-joining letter
  (`ALEF`, `DAL`, `REH`, `WAW`) takes a final shape and never gives a connection onwards.
- **Indic rules are this module's data, and the file says which scripts have none.**
  `RoboTextIndicScriptRules` is a row per script; a script with no row is **refused** rather than shaped with
  Devanagari's virama. The positional classes come from `RoboTextIndicIsConsonant`/`…IsVirama`/the rule
  arrays — do not add a second script table and do not hard-code a `0x09…` scalar outside them.
- **The text is never touched.** Clusters are ranges in the text, masks are indexed by source scalar, and a
  reorder moves **glyphs** — each keeping the source range it came from. `RoboTextIndicReorderInitial` runs
  before the first stage (while the buffer is one glyph per scalar, so a move can be stated in scalars) and
  `…Final` after the substitution stage; nothing may move the characters or the finished run.
- **The reorders are a stage field** (`RoboTextComplexStage::reorderBefore`/`…After`), dispatched by
  `RoboTextComplexReorderApply` **by strategy**, not by script.
- **The engines gained abilities; the strategies gained no engines.** GPOS runs lookup types 3–6 (cursive,
  mark-to-base, mark-to-ligature, mark-to-mark) and needs the face's context for the pen distance, so a call
  with none counts them unsupported rather than attaching to a distance nobody knows. Do not add a second
  positioning engine, a second substitution pass, or a renderer-side script branch.
- **Two off-by-ones here were real defects and their fixes carry the reason with them:** a participating walk
  asked at the position itself answers that position, so a cursive chain must ask `index + 1u`
  (and backtracking must step **down** first); and a half form's mask must not carry an `at != base` guard,
  because a base a virama follows is the syllable the classifier *fell back* on.
- **A font boundary may not fall inside a cluster.** A complex cluster lives inside one script run and the
  selector's unit is the script run, so the face that draws a conjunct draws all of it — proved rather than
  asserted by `text.arabic.fallback_no_split` / `text.indic.fallback_no_split`, whose texts put the complex
  scripts on the **chain** so the fallback path is the one under test: `runs == scriptRuns` holds exactly when
  the font added no boundary, and each run's clusters must tile it. A **cluster-grained** selector — a different
  face per cluster where the primary covers only part of a run — is still not built, and the clusters already
  name what may be split (`RoboTextComplexIsSafeBoundary`, `unsafeToBreak`), so it is a row of its own.
- **Not implemented, and named rather than implied:** the Southeast Asian strategy (Thai/Khmer/Myanmar) has no
  stages; cluster-grained fallback; `blwf`'s interaction with `rkrf`/`vatu`; the `haln`/`cjct` "base becomes half form" refinement; the
  UCD's `Indic_Positional_Category` as the data source; the other Indic scripts' own reorder rules (their rows
  carry `hasReph` and the Devanagari rule set is applied, which is right for Devanagari and unproved for the
  rest); Arabic's joining groups (Syriac/N'Ko/Mongolian) and canonically reordered input. Each is a **change to
  a rule table**, not a new mechanism — say so in `RoboTextComplex.h` and `ComplexScripts.md` when it changes.
- Capabilities/version: 12.19 set `complexScripts`/`complexScriptStrategies`/`complexClusters`/
  `positionalFeatures`/`gposAttachments`/`reordering`/`complexDiagnostics` and took the module to `1.10.0`,
  each read field by field in `Tests/Text/SuiteRoboTextConformance.c`.
- Tests: `Tests/Text/SuiteRoboTextArabic.c` (`text.arabic.*`) and `SuiteRoboTextIndic.c` (`text.indic.*`) over
  the **complex fixture** `Tests/Text/RoboTextComplexFont.h` — one built face carrying both scripts, whose
  bytes state every contextual form's glyph id, the lam/alef ligature, the reph, the half form, the conjunct,
  fatha's and shadda's anchors and beh's cursive entry and exit. A complex change is not done until
  `TestRoboText.ps1 -Target Host` and then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` pass.
- The fixture's GPOS table exists whenever the spec has **any** rule kind: a face whose only positioning is an
  attachment (Arabic's, Devanagari's) must not ship no GPOS table, which is how a mark case comes to read
  zeroes and look like an engine defect.

### Vertical writing (Phase 12.20)

**The writing mode is data, the orientation is a class plus a rotation, and there is no vertical shaping
engine — a vertical run is the same `RoboTextGlyphRun` on the other axis.** The module is
`Runtime/Source/RoboText/RoboTextVertical.c` + public `Runtime/Include/RoboText/RoboTextVertical.h`; the
vocabulary lives in `RoboTextTypes.h` because `RoboTextShapeOptions` carries two of the three by value, the
`vhea`/`vmtx` door is `RoboTextFont.c`'s, the placement is one helper beside the `hmtx` line in
`RoboTextGlyphs.c`, the cut is `RoboTextRuns.c`'s and the key is `RoboTextRunList.c`'s; authority
`Documentation/Runtime/Text/VerticalText.md`.

- **Never infer the mode.** `RoboTextWritingMode` is a field on the shape options and on every run. Japanese
  horizontal and Japanese vertical are two settings of the same text; a layer that looked at the characters
  could not express the second. `RoboTextDirection` (the logical inline direction) and the writing mode (the
  physical axis) are different questions and compose rather than replace each other.
- **Axes are explicit.** `RoboTextWritingAxes` is the inline/block pair and
  `RoboTextWritingAxesInline`/`…Block` are the only doors that turn a scalar into a physical vector. Do not
  write `if (vertical) { swap x and y }` at a call site: `vertical-rl` and `vertical-lr` differ in their
  **block** axis and in nothing else, which is a table row each rather than a branch. An unknown mode answers
  the horizontal pair, never a zero vector.
- **Orientation asks three tables in one order**, and the order is the whole reason the third exists:
  `RoboTextVerticalCodePointIsRotated` **first** (halfwidth Katakana's script stands up and UAX #50 rotates the
  character), then `RoboTextVerticalScriptIsUpright` (one row per script, ideographic), then
  `RoboTextVerticalCodePointIsUpright` (the `Common` blocks). A forced `text-orientation` overrides all three.
  Whitespace takes the character before it, which is what `RoboTextVerticalOrientationInContext` exists for —
  the run set cuts on **that** door, so a space between two ideographs does not open a sideways run.
- **Orientation is a run boundary.** The run set cuts where script, resolved direction, the effective features
  or the resolved orientation changes, and writing mode and orientation are in `RoboTextGlyphRunKey` — two
  runs that disagree on orientation are different runs and are never coalesced. A painter reads one
  orientation per run and never inspects a code point to decide how to draw a glyph. The per-scalar orientation
  array is allocated **only** for a vertical mode.
- **`vert`/`vrt2` and nothing else.** `RoboTextWritingModeApplyFeatures` names both tags On for a vertical mode
  and writes nothing for a horizontal one; they reach 12.10's GSUB through the one door every feature uses. Do
  not add a vertical substitution pass, a vertical lookup type or a renderer-side script branch; a tag the
  caller named is left alone.
- **Vertical advances are the face's.** `advanceY = vmtx advanceHeight + the font's own GPOS`, applied by the
  one `GlyphApplyVerticalMetrics` helper in both the map pass and the substitution rebuild, so the answer
  belongs to the **resulting** glyph id (a `vert` substitute's entry, the rule 12.10 set for ligatures). Never
  assume the font size. The **origin** is folded into the placement offset (`offsetX = -vertOriginX`,
  `offsetY = -vertOriginY`) so a painter applies an offset and never learns what a vertical origin is. A face
  with no `vmtx` is shaped at its em box and **counted** (`RoboTextGlyphRunVerticalSynthesised`), which is the
  one number that says whether an advance was measured or derived.
- **Measurement is 12.15's engine, unchanged.** `width` is Σ run `advanceX` and `advanceY` is Σ run
  `advanceY`; a vertical measurement is not a rotated rectangle, and `width != advanceY` for a vertical run is
  a stated fact. Do not add a second measurement path.
- **An attachment is placed against the run's inline advance.** 12.11's readers ask the face for the pen's
  step; `RoboTextGposOptions::writingMode` (the run's own field) decides *which* advance that is — `hmtx` +
  `XAdvance` horizontally, `vmtx` + `YAdvance` vertically — and `RoboTextWritingAxesInline` puts it on the axis.
  Do not read `hmtx` in a vertical run: that is a distance the vertical pen never moved on, and it lands a mark
  a whole cell from its base. Value records are untouched by the mode — a record's x stays x and its y stays y.
  Proved in both modes against one face and one rule by `text.vertical.mark`.
- **The paint half is a 2D pen and one carried mark.** `RoboPaintDecoration.c` adds each glyph's `advanceX`
  **and** `advanceY` and branches on nothing: a run's advance pair *is* the writing mode, already resolved.
  `advanceY` is zero horizontally, so the arithmetic is bit-for-bit what the single-axis pen did. The sideways
  half rides as `PaintGlyph`'s one flag (`PaintGlyphIsSideways`, set per glyph by the recorder from the run
  RoboText produced, because one paint record can span several runs) and the pen hands it to
  `GfxGlyphDraw` as the rotation. **Do not add a writing-mode field to the paint record and do not let the
  painter test a code point**: the decision was made in RoboText and is carried, not re-derived.
- **The turn is rasterized, not composited.** `GfxGlyphRasterParams` gained `rotation` (12.20), applied in
  `GlyphDevice` — so curves are turned rather than resampled, the mask rectangle follows the ink and the
  caller's `GfxRasterDrawGlyphMask` is unchanged. `graphics.glyph.rotation` proves the turn is exact
  (equal coverage bytes at transposed positions) and `graphics.typography.vertical_specimen` proves it on the
  shipped face at six UI sizes (`L` at 64 px: `29×47` upright, `47×29` turned), written to
  `Build/Typography-Vertical.png`. A mask cache must key on the rotation — a sideways glyph is a different
  mask from the same glyph upright.
- **Not built, and named rather than implied:** vertical block progression and vertical line breaking (the
  phase's own non-goals — so the shell stays horizontal and `ComputedWritingStyle` is not added, because
  nothing would consume it), `Vertical_Orientation` from RoboUnicode (the tables here approximate UAX #50
  until RoboUnicode carries the property), `VORG`, a **guest page** in a vertical writing mode (the document
  path carries no writing mode at all) and a guest page with **upright** glyphs in a column (the pack is
  Inter, which carries neither CJK ideographs nor `vert`) — the sideways visual evidence is the rendered
  `Build/Typography-Vertical.png` through the real pen. Each is in
  `VerticalText.md` §10 and in `RoboTextVertical.h`'s own comment. The guest proves what the pack *can*
  express: `User/robo-runtime/main.c`'s vertical checkpoint shapes the specimen with RoboText in-guest, steps
  the pen by the run's own advances and turns the outline in a guest surface, pinned as `ROBO_QEMU_VERTICAL_*`
  in `Scripts/pipeline/commands.test.ps1` (`robo test-qemu-display`). Its two ink counts for one glyph upright
  and turned are **equal** because the outline is turned in the rasterizer's own lattice — a build that
  composited a rotated bitmap fails there.
- Capabilities/version: 12.20 set `verticalText`/`writingAxes`/`verticalOrientation`/`verticalMetrics`/
  `verticalFeatures`/`verticalDiagnostics` and took the module to **`1.11.0`**, each read field by field in
  `Tests/Text/SuiteRoboTextConformance.c`. There is deliberately **no `verticalShaper`** flag: the phase adds
  no engine.
- Tests: `Tests/Text/SuiteRoboTextVertical.c` (`text.vertical.*`, 14 cases) over the **vertical fixture**
  `ConfSpecVertical` in `Tests/Text/RoboTextConformanceFont.h` — a `vhea`/`vmtx` pair, a `vert` feature
  substituting two upright forms, a `mark` attachment whose two stated anchors put a voiced sound mark on its
  Hiragana letter, and a `cmap` spanning Han, Hiragana, Latin, a digit, Arabic, CJK punctuation and halfwidth
  Katakana. The fixture gives every glyph a horizontal advance of `400 + id` and a vertical one of `1000 + id`,
  so a build that reused `hmtx` for `advanceY` fails rather than agrees, and it derives every glyph id from its
  run array so adding a run cannot move a suite's expectation. The one exception is the combining mark, which
  is zero on **both** axes — which is what makes "a column with a mark in it is as long as its letter alone" a
  fact about the bytes. `text.vertical.mark` reads one rule in **both** modes (1006 units of vertical step
  against 406 horizontal), `text.vertical.fallback` proves a vertical run answered by the chain keeps the
  mode and the `vmtx` advances, and `text.vertical.run_split` the orientation cut. A vertical change is not
  done until `TestRoboText.ps1 -Target Host` and then
  `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` pass.

### Selection mapping (Phase 12.21)

**Selection is expressed in source-text coordinates; glyph geometry is derived from shaped visual output. A
selection is never a glyph index, and no query in this layer shapes anything.** The layer is
`Runtime/Source/RoboText/RoboTextSelection.c` + public `Runtime/Include/RoboText/RoboTextSelection.h`; it is a
**map over** 12.14's finalized run list plus the placements a caller supplies — the one thing a run cannot
know is where the caller put it — and its contract is `Documentation/Runtime/Text/TextSelection.md`.

- **A position is a source offset.** `RoboTextPosition` is one `RoboTextScalarIndex` offset into the original
  text; `RoboTextSelection` keeps anchor *and* focus, and `RoboTextSelectionRange` normalizes on demand. No
  door in the layer takes a glyph index, a byte offset or a screen coordinate: a caller holding a glyph index
  has already lost the ligature, the mark and the reordered Indic cluster, which is exactly what this phase is
  for.
- **The placement is the one input, and it is copied.** `RoboTextSelectionPlacement` carries the run's pen
  origin, the **caller's** line index and the line band as two signed distances along the **block** axis
  (`RoboTextSelectionPlacementForRun` derives them from the run's own metrics). `…MapCreate` borrows the list
  and copies placements, and it **refuses** a subset, a duplicate, an out-of-order array and an unplaced run
  rather than repairing any of them — the same rule the run list follows for overlapping runs.
- **No call site swaps x and y.** Every query projects through `RoboTextWritingAxesInline`/`…Block` (12.20) in
  `RoboTextSelectionPlacementProject` and converts back at the end, so a column's caret, hit test and
  rectangles are correct by construction and `vertical-rl` differs from `vertical-lr` in the block vector and
  in nothing else. The run's **own** classified direction decides which way its inline axis is read, so an
  `Rtl` run's first cluster is at its largest inline offset and a run of digits inside an Arabic paragraph is
  not mirrored.
- **Two visual locations, and never a fraction of one.** `RoboTextAffinityUpstream`/`Downstream` says which
  of a shared boundary's two carets was meant; a hit inside a ligature or a conjunct snaps to the cluster and
  **reports** it (`RoboTextSelectionHit::snapped`, `…::clusterStart`); a position inside a multi-scalar cluster
  is snapped to its start (downstream) or its end (upstream). `RoboTextSelectionMapCaret` never fails for an
  unplaceable position — `RoboTextCaretGeometry::valid` is 0 with the spans zero, so a caller has one thing to
  check — while a NULL argument is `RoboInvalidArgument`.
- **Geometry is rectangles, not a box.** `RoboTextSelectionMapGeometry` answers one rectangle per (line, run)
  segment in **visual** order, so a range across a bidi boundary or a fallback font boundary is several places
  on the page. Within one run the covered clusters coalesce into one segment, which is what keeps a base and
  its zero-advance marks to one rectangle; a selection of only zero-advance clusters is widened to the caret's
  thickness rather than emitted invisibly. The rectangle is *(inline × block)*, so its width carries the
  inline extent in a horizontal line and its **height** does in a column. `Geometry::bounds` is a convenience
  and never the answer to "what is selected". `RoboTextMaxSelectionRects`' cap is **reported** (`truncated`).
- **The caret's segment, not the mode's name.** `RoboTextCaretGeometryIsHorizontal` reads the segment
  (`SpanX > SpanY`): a horizontal line's caret is vertical, which is the opposite of the mode's own name and
  the reason the door exists. Orientation comes from the mode — never a script — so a Latin letter inside a
  Japanese column is sideways and the caret beside it is horizontal.
- **A query shapes nothing.** Nothing here decodes UTF-8, selects a font, resolves a script or a level, runs
  GSUB/GPOS or measures, and the layer allocates nothing per query. Do not add a cache, a second resolution or
  a "measure to be sure" path: a hit test over a paragraph must cost no shaping (12.21.43).
- **Not built, and named rather than implied:** editing, caret navigation policy, IME, clipboard, mouse and
  gesture handling, CSS `user-select`, grapheme segmentation (clusters remain 12.14's "a mark continues the
  cluster before it"), per-glyph caret subdivision inside a ligature, selection *painting* (the shell's row —
  the spec's `SelectionPaintCommand` is deliberately not in this module) and a `SelectionMap()` accessor on
  RoboLayout's line boxes. Also not added: per-phase build scripts — the convention since 12.19 is one
  `BuildRoboText.ps1`/`TestRoboText.ps1` per module.
- **Complete-or-absent dumps.** `RoboTextPositionWrite` / `RoboTextSelectionWrite` /
  `RoboTextCaretGeometryWrite` / `RoboTextSelectionHitWrite` / `RoboTextSelectionMapWrite` all report the
  length a complete line needs and leave the buffer empty when it does not fit; the map's block ends with each
  run's clusters as `source → glyphs`, which is the one thing a reader debugging a ligature needs.
  `RoboTextSelectionFuzz` drives every query from arbitrary bytes with one invariant: no crash, no
  out-of-range access, no invalid position, no infinite loop.
- Capabilities/version: 12.21 set `selectionMapping`/`selectionPositions`/`selectionClusters`/
  `selectionAffinity`/`selectionHitTesting`/`caretGeometry`/`selectionGeometry`/`selectionWritingAxes`/
  `selectionValidation`/`selectionDiagnostics`/`selectionFuzzing` and took the module to **`1.12.0`**, each
  read field by field in `Tests/Text/SuiteRoboTextConformance.c`. There is deliberately **no
  `selectionPainting`** and **no `selectionShaping`** flag.
- Tests: `Tests/Text/SuiteRoboTextSelection.c` (`text.selection.*`, 13 cases) over the **12.18 conformance
  fixture** — Latin `ffi` under `liga`, zero-advance combining marks, the complex fixture's Arabic, and
  `ConfSpecVertical`'s `vmtx` column — with every expectation read off the run being queried or off the
  fixture's builder, never a literal pixel. `text.selection.ligature` (both halves of `ffi` snap to the
  cluster, and the range selects the whole glyph), `text.selection.marks` (a base and its mark are one
  rectangle), `text.selection.bidi` (two carets at one boundary, one line apart) and `text.selection.vertical`
  (the same three queries on the other axis) are the decisive ones. A selection change is not done until
  `TestRoboText.ps1 -Target Host` and then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` pass.
- The guest half is `User/robo-runtime/main.c`'s `GuestSelectionCheckpoint` — a **second off-screen render**,
  because the document path carries no selection at all (no mouse, no focus, no editor) — pinned as
  `ROBO_QEMU_SELECTION_*` in `Scripts/pipeline/commands.test.ps1` (`robo test-qemu-display`): a point resolves
  to the **nearer boundary** rather than a glyph index; the column's caret is horizontal and the line's is not
  for the same position in the same text; `CARET_SPAN` equals `BLOCK_ONE` (a caret is as long as the band it
  sits in); `BLOCK_ONE` equals `BLOCK_ALL` while the inline extents differ (`EQUAL_BLOCKS=1`), which a
  rectangle built from a glyph count fails; and the rectangles are **filled into a guest surface** and counted
  (49×120 px of ink), so the geometry is proved paintable rather than merely printed.

### Text editing (Phase 12.22)

**Editing operates on logical source text; shaping and rendering are downstream consequences of text
mutations.** The layer is `Runtime/Source/RoboText/RoboTextEditing.c` + public
`Runtime/Include/RoboText/RoboTextEditing.h`, and `Documentation/Runtime/Text/TextEditing.md` is the phase's
authority.

- **One mutation.** `RoboTextEditorReplace` is the only door that changes the text; insert, replace-selection,
  both deletes and every command name are that door with a computed range. Do not add a second mutation path
  — a transaction's cancel is a *replace* from its snapshot rather than a reverse replay, and that is only
  true while there is one place the text changes. An edit is **atomic**: the new text is built in a clone and
  installed only on success, so a refusal changes nothing.
- **The text stays logical.** The editor owns one `RoboTextBuffer`, one `RoboTextSelection` and one
  `RoboTextCaret` — a position, an affinity and two flags — and **no geometry**: a caret's visual form is
  12.21's query against a selection map, which needs placements this layer does not have. Never store pixel
  coordinates as caret state, and never derive a position from a glyph width. `RoboTextEditorInsert`
  **validates** the bytes a caller passes (an ill-formed sequence is `RoboInvalidArgument`); a `RoboTextBuffer`
  *given* bytes keeps them, because silently rewriting a caller's text is the repair the other door refuses.
- **Boundaries are rules, and the ones this build lacks are refused by name.** `Scalar` (never splits a UTF-8
  sequence), `Character` (a base **and the combining marks that hang off it**, which is why one backspace over
  `e` + two accents takes all three scalars) and `Cluster` — answered by a resolver a caller installs from a
  run list, so a ligature, an Arabic joining sequence or a Devanagari conjunct is one editing unit. Without a
  resolver `Cluster` is `RoboNotSupported` rather than a quiet fallback to characters. `Word` and the two line
  boundaries are refused: never approximate them with `position ± 1`. The resolver **borrows** the list, and
  the text it was built over must be the editor's current text — the one contract this layer cannot check.
- **The shaped cluster is the editing unit, and the two layers answer two questions.** 12.19's complex
  clusters group a joining *sequence* (so `لا` is two one-scalar clusters and a line may break between them);
  the run's cluster is the ligature glyph the font states (one unit of shaped output). The editor reads the
  run's answer, because an edit that removed one letter of a ligature would cut it in half.
- **One editor for every writing mode and every direction.** There is no vertical editor and no RTL editor: the
  writing mode is data on the shape options and on every run (12.20), the editor never sees it, and a column's
  clusters are in **source (inline) order** whatever the block axis does.
- **Invalidation is paragraph-widened, and the flags are the discriminating half.** Five ranges (text, analysis,
  shaping, layout, paint) and seven flags; `requiresBidiReanalysis` is set only when the inserted **or removed**
  text carries a scalar whose level a paragraph does not inherit from its base (a right-to-left scalar, and a
  *number*, which UAX #9's I1/I2 give a level the base does not decide), and `requiresFontReselection` only
  when text changed at all. A no-op's record is the empty one. Correctness first: a one-scalar edit can change
  its neighbours' shaping, so a paragraph is the honest range, and a finer one later is a change of *range*.
- **Composition is text with a kept slice.** The composing text lives in the buffer so the caret and every
  query work during it; `textBefore` is what makes it distinguishable from committed text. Cancelling restores
  the slice **and** the selection and caret the composition began over — not the state as of the last
  keystroke, and not something reconstructed from glyphs.
- **Transactions appear as one mutation.** Members apply as they are added (every intermediate state is a real
  text), `Commit` publishes one spanning operation and the union invalidation, and `Cancel` restores from the
  snapshot in one replace. The members stay readable, which is what a per-keystroke undo of a paste needs.
- **Transformation is in logical source coordinates.** `RoboTextPositionTransformAfterEdit`: before the range
  unchanged, inside it (or at its start, or at its **end**) the end of the inserted text, after it shifted by
  `inserted − deleted`. Both rules are monotone, so a range never inverts and nothing is "repaired".
- **Not built, and named rather than implied:** `RoboInput` and keyboard scan codes, mouse and touch events, a
  clipboard backend, an undo **manager** (`Undo`/`Redo` are `RoboNotSupported` and `undoHistory` is `0` — the
  *record* is in every operation), word and line boundaries, grapheme segmentation, rich text, spell checking,
  autocorrect, DOM editing, `contenteditable`, accessibility editing, drag-and-drop, incremental reshaping
  (the invalidation ranges are the seam it would change), composition rendering, and per-phase build scripts.
  There is deliberately no guest half: the document path carries no editor.
- **Complete-or-absent dumps.** `RoboTextEditorWrite`, `RoboTextEditOperationWrite`,
  `RoboTextEditInvalidationWrite` and `RoboTextCompositionWrite` report the length a complete line needs and
  leave the buffer empty when it does not fit. `RoboTextEditingFuzz` drives an editor from arbitrary bytes
  with one invariant: the text stays valid UTF-8, every position stays inside it, the module itself never
  inverts the selection, no buffer leaks and no loop runs forever.
- Capabilities/version: 12.22 set `textEditing`/`editOperations`/`editingBoundaries`/`clusterBoundaries`/
  `editInvalidation`/`editTransactions`/`compositionState`/`editTransformations`/`editingDiagnostics`/
  `editingFuzzing` and took the module to **`1.13.0`**, each read field by field in
  `Tests/Text/SuiteRoboTextConformance.c`, with `undoHistory`/`wordBoundaries`/`lineBoundaries` asserted `0`.
- Tests: `Tests/Text/SuiteRoboTextEditing.c` (`text.editing.*`, 16 cases) over the 12.18 conformance fixture,
  the 12.19 complex fixture and the 12.20 vertical fixture. `text.editing.utf8` (one, two, three and four-byte
  scalars deleted one scalar at a time, and an ill-formed insertion refused), `text.editing.marks`,
  `text.editing.ligature` (`ffi` as one cluster with a resolver and three characters without — the typed path
  compared with the text built directly, `ConfResultsEqual`), `text.editing.arabic` and `text.editing.indic`
  (the edit's own bytes shaped and compared, so "the edit reshapes" is a fact about the text rather than about
  a remembered glyph), `text.editing.bidi` (the flag on for Arabic and a digit, off for Latin),
  `text.editing.vertical` (a column through the same doors) and `text.editing.composition` are the decisive
  ones. A 12.22 change is not done until `TestRoboText.ps1 -Target Host` and then
  `TestLayout/Paint/Graphics/Rasterizer/Font/Desktop.ps1 -Target Host` and `TestRoboTextConformance.ps1` pass.

### Whole-pipeline shaping conformance (Phase 12.23)

**The block is now backed by an executable contract at the grain the layer suites cannot see: one sample per
category from UTF-8 to a measurement, with each step checked against the previous step so a failure names the
stage that moved.** It is `Tests/Text/SuiteRoboTextShapingCorpus.c` (`text.shaping.corpus.*`, 13 cases) and its
contract is `Documentation/Runtime/Text/ShapingConformance.md` §8. It adds **no engine work** — every number it
asserts is read off a door that already existed, and the measurement door is 12.15's own
`RoboTextMeasureGlyphRunList` (a door **over the list**), so "the measurement agrees with the runs" is an
equality between two implementations and not a third sum written in a test.

- **No capability flag accompanies this phase**, and no new fixture: the corpus asserts the engine, not itself.
  `RoboTextGetCapabilities` still reports `1.13.0`'s set, and the fixtures are 12.18's, 12.19's and 12.20's,
  built byte by byte — never a checked-in `.ttf` blob and never an external shaper's opinion. Do not add a
  corpus flag, and do not move an engine-contract expectation onto the shipped pack.
- **A gap is a value, not a silence.** Nine Indic scripts whose positional rules this build lacks are asserted
  through their own table rows (their virama, and every reorder array empty); the four Southeast Asian scripts
  answer a strategy with no stages; Tibetan and Han answer `None`. A script that borrowed Devanagari's rules
  fails here rather than passing quietly, and a new row is a table entry plus this case's array.
- **Malformed input has a stated answer.** Eleven single-corruption variants, each shaped twice: an out-of-range
  or truncated `GSUB`/`GPOS`/`GDEF` is refused (`RoboParseError`), a `cmap` that maps nothing and a `maxp` that
  claims one glyph are refused whole, a `unitsPerEm` of zero still shapes, and the only variant that refuses
  the **face** is a directory entry pointing outside the source. Two of the phase's §28 variants are named and
  not built (an extension cycle and an impossible class definition): a suite cannot assert "did not hang".
- **Determinism is a hundred repetitions, and the record is diffable.** Eight samples × 100, compared field by
  field and byte for byte on the run-list block, plus `Build/Text-ConformanceCorpus.txt` — one line per sample,
  two generations identical — which is the artifact a reviewer diffs first.
- **The dumps are part of the contract, and this is where they broke.** A glyph's line is built in a fixed
  local buffer; 12.20's fields pushed a vertical glyph past it, so `RoboTextGlyphWrite` answered
  `RoboNotSupported` and **the whole run list's block became unobtainable at every destination size**. Both
  buffers are now sized for every field plus the longest flags word. A new field on a diagnostic line is a
  reason to check that buffer, and a corpus case that cannot dump a record it produced is a defect in one of
  the two, never a case to skip.
- Tests: `TestRoboText.ps1 -Target Host` (335 cases, the corpus among them) and
  `TestRoboTextConformance.ps1 -Target Host` (68), then `TestLayout`, `TestPaint`, `TestGraphics`,
  `TestRasterizer`, `TestFont` and `TestDesktop` (`-Target Host`). The guest half of this stack is the shell's
  `ROBO_QEMU_{SHAPING,VERTICAL,SELECTION}_*` checkpoints; the corpus adds no fourth render, and `TestDesktop.ps1
  -Target Qemu` is the phase's integration test.

### RoboText final integration and API freeze (Phase 12.24)

**RoboText is the one frozen text platform: one public API, one shaping pipeline, one font-selection path, one
measurement path, one glyph-run contract, and one place where a glyph becomes ink.** Later phases consume it;
they do not build competing text engines. `Documentation/Runtime/Text/Overview.md` is the map (the pipeline, the
layer table, the ten rules), `Documentation/Runtime/Text/APIStability.md` is the contract (stable / advanced /
internal / experimental / deprecated, ownership, lifetime, threading, invalidation, error handling).

- **The aggregate is the public surface.** `Runtime/Include/RoboText/RoboText.h` names every header in the
  module's directory, in two tiers: the *seam* a document, shell or input layer uses, and the *engines* behind
  it. Adding a public header means adding it there — `text.api.header_hygiene` reads the umbrella and matches
  it against the directory, and `Tests/Text/SuiteRoboTextApi.c` (`text.api.*`) includes **only** the umbrella,
  so a header left out breaks its build.
- **One pen, and it is `RoboPaintRun.c`.** `PaintRunShape`/`PaintRunShapeScalars`/`PaintRunMeasure` are the
  shaping conversion and `PaintRunDraw` is the two-axis pen; `RoboPaintContext.c` (the document's recorder) and
  `RoboDesktopChrome.c` (the shell's labels) both come through them, and `RoboPaintDecoration.c` draws a
  display-list command with the same pen. Do not add a third: the arithmetic, the control-glyph rule and the
  quarter turn for a sideways glyph live once, and `paint.text.*`/`desktop.*` are the cases that keep them
  there.
- **The shell has no text engine.** `RoboDesktopChrome.c` used to decode UTF-8 itself, resolve a glyph per code
  point, sum advances itself and stroke each glyph. It now calls `PaintRunMeasure`/`PaintRunShape`/`PaintRunDraw`
  — and `RoboDesktop.c` prepares the face's cmap and metrics where the face enters the shell, because a
  `FontFace` is created unparsed and every door that needs a table answers `RoboNotSupported` until it is.
- **The four checkable rules are a build step.** `Assert-RoboTextIntegrationRules` (in `Runtime/Tools/Build.ps1`,
  run by `-Verify`) fails on: a `cmap` lookup in a layout/paint/render/shell/window/display/typography module; a
  direct include of a shaping engine header from a consumer; a `GfxRasterDrawGlyph` outside RoboGraphics and the
  one pen; a `RoboText/…` include inside RoboGraphics. Each allowance is a named file or directory on purpose —
  a *new* call site must fail. The six remaining rules (no code-point paint, no second fallback, no manual RTL
  reversal, no hand-reproduced GSUB/GPOS, measurement from final geometry, source-coordinate selection/editing)
  are kept by review, and §3 of `Overview.md` is the table to check against.
- **The scan found real work, twice.** `Assert-RoboDesktopBoundaries` had been failing since 12.17 added
  `RoboDesktopFontProvider.c/.h` (the layout seam's concrete provider, which legitimately includes
  `RoboLayoutFontResolver.h`); it is now a named exemption with its reason. And the chrome was the second text
  implementation this subsystem exists to prevent — the removal is what the scan now keeps.
- Tests: `Tests/Text/SuiteRoboTextApi.c` (`text.api.*`, 9 cases) via `TestRoboText.ps1 -Target Host` (344 cases)
  and `TestRoboTextConformance.ps1` (68), then `TestLayout`, `TestPaint`, `TestGraphics`, `TestRasterizer`,
  `TestFont` and `TestDesktop` (`-Target Host`), with `TestDesktop.ps1 -Target Qemu` when the guest image is
  involved. `-Verify` must pass: it is what runs the integration-rule scan.

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
  display list receives glyph runs, so **no code-point paint path may be added**. GPOS sees the ligature and
  not its components (12.11), so a pair the font states against a ligature's glyph id applies to it:
  `text.gsub.ligature_positioning` is that order, executed.
- Capabilities: 12.12 set `ligatures`/`ligaturePolicy`/`ligatureProvenance` (version `1.6.0`). Tests:
  `Tests/Text/SuiteRoboTextLigature.c` (`text.ligature.*`) plus the fixture's longest-match case in
  `text.gsub.ligature`. The shipped Inter carries `dlig` but **no `liga`**, so standard ligatures are proven
  against the synthetic fixture and the real-font evidence is `!?` → `‽` under `dlig`.

### Glyph positioning (Phase 12.11)

**GPOS answers *where a glyph sits*, changes glyph positions and advances, never the ids, the order or the
Unicode text.** It is one engine in two files over the **same** layout core GSUB uses:
`Runtime/Source/RoboText/RoboTextGpos.c` (the plan, the apply loop, the counters, the trace) and
`RoboTextGposSubst.c` (one reader per lookup type this build executes), public contract
`Runtime/Include/RoboText/RoboTextGpos.h`, authority `Documentation/Runtime/Text/Positioning.md`.

- Feature selection is the shared resolver's (`RoboTextOtPlanResolve`) with **positioning's** defaults: `kern`,
  `mark`, `mkmk`, `curs`, `dist` and the Indic mark features. That list is the one place the two tables'
  defaults differ, and it is why the resolver takes the defaults as an argument. A caller's own setting always
  wins — there is no `if (script == Latin)` anywhere.
- Positioning writes **adjustments, not totals**: `RoboTextShapingGlyph::advanceAdjustX`/`offsetAdjustX` beside
  the face's own advance, added once by the run build. `RoboTextGlyph` keeps both (`advanceX` the total,
  `advanceAdjustX` the font's part) so a diagnostic can say what moved. `RoboTextShapingGlyphPositioned` is set
  even for a zero value record — "the font said 0" is not "the font said nothing".
- Values are font units scaled **once**, through `RoboTextFontUnitsToDevice`; the engine multiplies by
  `RoboTextGposOptions::deviceScale` and rounds nothing. A device-table delta is applied only when the caller
  supplied a device ppem; otherwise it is counted (`deviceUnevaluated`).
- Lookup types 1, 2 and 9 run; 3–8 are recognised and **skipped** (`lookupsUnsupported`), which leaves the
  glyph where its advance put it — not a wrong answer, but not mark attachment either. Do not add a second
  positioning engine or a per-script special case.
- Two reading rules that were real defects here: a resolved coverage offset is a `size_t` (**never**
  `uint16_t` — narrowing it silently loses every pair past 64 KiB, which is where the shipped face's kerning
  lives), and a subtable offset that lands inside its own lookup's header is refused **once per lookup** by
  `RoboTextOtSubtableInsideLookup`. `RoboTextOtSpendProbes` records the probe count in the **owning** engine's
  statistics.
- Capabilities/version: 12.11 set `positioning`; version `1.7.0` with 12.13. Tests:
  `Tests/Text/SuiteRoboTextGpos.c` (`text.gpos.*`, synthetic tables parsed directly plus the shipped pack),
  `text.gsub.ligature_positioning` for the GSUB→GPOS order, and `text.kern.*` for the policy over it.

### Kerning (Phase 12.13)

**Kerning is GPOS pair adjustment and nothing else; it changes advances, never glyphs or Unicode.** The layer
is `Runtime/Source/RoboText/RoboTextKerning.c` + `Runtime/Include/RoboText/RoboTextKerning.h`, with the value
types (`RoboTextKerningPolicy`, `RoboTextKerningLegacy`) beside `RoboTextShapeOptions` in `RoboTextTypes.h`
because the options carry one. `Documentation/Runtime/Text/Kerning.md` is the phase's authority; update it (and
this section) when a default, a source rule or a reported field changes.

- `kern` is a feature: the policy becomes entries in a `RoboTextFeatureSet` and reaches the plan through the
  same door every tag uses. `RoboTextKerningPolicyApplyIfUnset` in `RoboTextRunSetCreate` is a default **under**
  the caller's settings, so `"kern" off` wins. **No paint, layout or per-script check for kerning may be
  added** — there is nothing downstream of the feature set to special-case.
- Defaults: `RoboTextKerningPolicyDefault` asks for `kern` **on** and no legacy source; `RoboTextShapeOptionsInit`
  installs it. A face that carries no `kern` feature simply kerns nothing — the font's answer, not a policy.
- One source answers, never two: `RoboTextKerningLegacyNever` (default) or
  `RoboTextKerningLegacyIfNoGposKern`, which uses the legacy table **only** when GPOS has no `kern` feature.
  There is no "add them together" enumerator. **This build does not read the legacy `kern` table** (Phase 5
  does not parse it), so `RoboTextKerningResolution` reports the table's *presence*
  (`legacyTableAvailable`) separately from which source *answered* (`source`) — do not collapse those two
  fields into one, and do not set `source` to `LegacyKern` until a parser exists.
- `RoboTextKerningResolve` asks the face **twice** — once with the engine's defaults (what the face offers) and
  once with the caller's settings (what applies) — because a caller turning kerning off has not changed the
  face. Keep both answers.
- Range overrides are 12.13 §9's: `RoboTextShapeOptionsSetFeatureRange` stores a range and the **run set** cuts
  the text where the effective settings change, before any plan is resolved. A range boundary is a shaping
  boundary — a pair straddling it is not a pair — and `text.kern.range_override` asserts that rather than
  hiding it. Do not move the cut into an engine.
- Measurement follows the final advances: `painted width = measured width = GlyphRun advance`, and
  `advanceX == units→device(advanceUnits) + advanceAdjustX` per glyph. Nothing above the glyph run may
  re-derive kerning.
- Capabilities: 12.13 set `kerning`/`kerningPolicy`/`kerningReport` (version `1.7.0` with 12.11). Tests:
  `Tests/Text/SuiteRoboTextKerning.c` (`text.kern.*`) over the **shipped** pack, whose real kerning sits in a
  class matrix behind an extension lookup past 64 KiB — the face that found both reading defects — plus
  `text.gsub.ligature_positioning` for the ligature interaction.

### Glyph runs (Phase 12.14)

**A run is finalized once and is immutable; a shaped text is a `RoboTextGlyphRunList` with the logical order
as the list and the visual order in an array beside it.** `Runtime/Source/RoboText/RoboTextGlyphs.c` derives the
four records a glyph array implies — clusters, per-glyph flags, the face's metrics and the run's totals — in
`RoboTextGlyphRunFinalize`, the **only** writer of them; `Runtime/Source/RoboText/RoboTextRunList.c` owns the
list, its keys, its two orders and the merge. `Documentation/Runtime/Text/GlyphRuns.md` is the phase's contract.

- `RoboTextGlyphRun` is immutable and read-only through its accessors. Anything that needs a different shaped
  result builds a new run; nothing edits one in place. The struct is in `RoboTextGlyphsInternal.h` for the one
  reason an opaque type needs an internal header: the run list merges two runs, which is a concatenation of two
  arrays and a re-derivation of everything derived from them.
- Clusters are in **logical source order** and tile both the glyph array and the run's source. The grouping rule
  is stated in `GlyphContinuesCluster`: a source that starts inside the cluster continues it, a combining mark
  continues it, anything else opens a new one. **No grapheme segmentation** (UAX #29) is implemented, and the
  approximation is named in the header and the doc rather than implied. `RoboTextGlyph::cluster` is the cluster
  *index* — never re-derive it from a glyph count, and never reverse the array for an RTL run.
- `RoboTextGlyph::codePoint` is the character a glyph came from, or `0` when its source covers more than one
  scalar. It exists because a glyph's properties are the character's — `Mark` above all — and after substitution
  the text is out of scope. `source` is the scalar *index*; do not confuse the two, and do not ask a category
  table about an index.
- The flag word is six flags and **no more** (`RoboTextGlyphFlag`): the two cluster flags, `Mark`, `Ligature`,
  `UnsafeToBreak` and `UnsafeToConcat`. `control`/`mirrored`/`mapped`/`substituted`/`positioned` stay fields of
  their own — stating a fact twice is how two readings of one record come to disagree. `UnsafeToBreak` is
  information for Phase 19's line breaker, **not** a breaker: it claims nothing about the boundaries it leaves
  clear.
- Metrics come from `RoboTextFontContextMetrics`, the measurement's own door, so a line box built from a run
  and one built from the measurement agree by construction. `RoboTextGlyphRunListMetrics` answers with the
  **first** run's record — stated rather than hidden — while the measurement's line metrics (12.15) aggregate by
  the maximum rule. Both answers are published; do not collapse them into one, and read the door the question
  is actually about.
- The visual order is Phase 12.8's answer **read**, never recomputed: `RoboTextGlyphRunListOrderVisual` sorts by
  the `visualIndex` of the level run each glyph run's source starts in. A second L2 in this module would drift
  from 12.8's the first time a level resolution changed. A run straddling two level runs goes with the one its
  source starts in.
- `RoboTextGlyphRunKey` is what makes two runs different runs; `RoboTextGlyphRunKeyDifference` names the field.
  `RoboTextGlyphRunListCoalesce` merges only when the keys are equal **and** the sources are contiguous, and it
  merges by concatenating and re-finalizing — never by adding two totals and keeping one run's clusters. A
  **view** (`RoboTextGlyphRunListCreateView`) may not merge, because it does not own its runs.
- `RoboTextShapeResultGlyphRunList` is that view, built once during shaping — before the measurement, because
  since 12.15 the measurement **is** a door over it. Do not add a second list type, a second key, or a cache:
  `RoboTextCapabilities::shapingCache` is `0` on purpose (12.14.33), and a cache added later is an optimization
  over these keys and must never be required for correctness.
- Capabilities: 12.14 set `glyphRunFinalization`/`glyphClusters`/`glyphFlags`/`glyphRunMetrics`/`glyphRunList`/
  `glyphRunKeys`/`glyphRunCoalescing`/`glyphRunValidation`/`glyphRunDiagnostics` and left `shapingCache` **0**,
  each read field by field in `Tests/Text/SuiteRoboTextConformance.c`; version `1.8.0`. Tests:
  `Tests/Text/SuiteRoboTextGlyphRuns.c` (`text.glyphrun.*`) via `TestRoboText.ps1 -Target Host`, and a
  glyph-run change is not done until `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` pass (with
  `-Target Qemu` when the guest image is involved). The measured baseline is `text.glyphrun.performance`.

### Text measurement (Phase 12.15)

**Measurement is a door over a finalized run list, and it never shapes.**
`Runtime/Source/RoboText/RoboTextMeasurement.c` + public `Runtime/Include/RoboText/RoboTextMeasurement.h`
(`RoboTextMeasureGlyphRunList`, the rect, the request, the line-metrics record, the tolerance, the validation and
the diagnostic block). `Documentation/Runtime/Text/TextMeasurement.md` is the phase's contract.

- The engine reads the runs' own advances, metrics and glyphs; it does **not** decode, select a font, resolve a
  script or a level, or run GSUB/GPOS/kerning. `RoboTextShape.c` calls it over the result's own list, so a
  result's measurement and `RoboTextShapeResultMeasure` are one implementation. A new measurement path that
  re-shapes is a defect, not a shortcut.
- `width` is the sum of the runs' own totals, each of which is its glyphs' device advances accumulated in
  `double`, rounded **once** at the boundary. Never sum font units and scale afterwards, and never round per
  glyph: `measurement.width == Σ run advance == the pen the painter accumulates`, and `text.measure.advances`
  asserts both halves.
- **Three geometries stay three** (the same rule as coverage/alpha/opacity one phase down): *advance* is what the
  pen does, *logical bounds* is the box layout places, *ink bounds* is what the outlines occupy. A space has an
  advance and no ink; a combining mark has no advance and ink. `hasInkBounds`/`hasLogicalBounds` say whether each
  was computed — an all-zero rectangle and an uncomputed one are the same value.
- Line metrics are one function (`MeasureLineMetricsOf`) and one policy: `ascent`/`descent`/`lineGap` are the
  **maximum** over participating runs, `height` their sum, and `lineHeight` what a line box occupies (a declared
  leading when declared). Never take the first run's for the measurement, and never compute the record
  separately from the three fields.
- Ink bounds come from each glyph's **outline** box (`glyf` through `RoboTextFontGlyphMetrics`), scaled by the
  run's own context — the module's one scaling door — and translated by the pen position and the glyph's GPOS
  placement offset. The advance box is never used as the ink box. A glyph with no box is skipped and reported
  (`hasInkBounds` `0`); a partial box passed off as complete is the failure this flag exists to prevent.
- The baseline is `0` and y increases **upward**; `RoboTextMeasureValidate` refuses a record whose baseline is
  not zero or whose stored logical box disagrees with the width and metrics it derives from. Validation is a
  door a test calls, never a hot-path step, and the engine does not call it.
- The request's options are a cost boundary: the default computes no bounds, because ink bounds parse each
  glyph's outline. `RoboTextShape` measures with the default, so a caller that wants a width never pays for
  outlines. A vertical writing mode is `advanceY`, which is `0` today and stays a field rather than a second API.
- No caching (see the run-list bullet above), no line breaking, no tab expansion and no grapheme segmentation:
  a newline is an ordinary scalar, tab stops are layout's, and cluster-level ink is the union of its glyphs'.
- Capabilities: 12.15 set `measurementEngine`/`lineMetricsPolicy`/`inkBounds`/`measurementBounds`/
  `measurementValidation`/`measurementDiagnostics`/`measurementTolerance`, each read field by field in
  `Tests/Text/SuiteRoboTextConformance.c`; version `1.9.0`. Tests: `Tests/Text/SuiteRoboTextMeasurement.c`
  (`text.measure.*`) via `TestRoboText.ps1 -Target Host`, and a measurement change is not done until
  `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` pass (with `-Target Qemu` when the guest image is
  involved). The measured baselines are `text.measure.performance`'s two loops — advances, and ink bounds, which
  are reported separately because they cost differently.

### Paint / layout bridge (Phase 12.16)

**A display list is a snapshot of shaped text: the record carries the glyphs and the face they came from, and
shaping happens where the document is recorded, never on replay.** The layer is
`Runtime/Include/RoboPaint/RoboPaintText.h` (the `PaintGlyph` record, `PaintGlyphSpan`, `PaintGlyphPool`),
`Runtime/Include/RoboPaint/RoboPaintFontTable.h` (the pointer-free face tokens), and the recorder in
`RoboPaintContext.c` plus the pen in `RoboPaintDecoration.c`. `Documentation/Runtime/Text/PaintBridge.md` is the
phase's contract; the glyph run it snapshots is `Documentation/Runtime/Text/GlyphRuns.md`.

- Two payloads, one record: a text record keeps the **characters** (`PaintTextSpan`, what selection/hit test/copy
  read, Phase 12.21) **and** the **glyphs** (`PaintGlyphSpan`) the run shaped to, plus the face as a
  `PaintFontToken`. Dropping either half is a defect: glyphs alone make a list only one font can describe, and
  characters alone make a replay shape again.
- The invariant that keeps the pair honest: `glyphs` is empty **exactly when** `fontToken` is
  `PaintFontTokenNone`. `PaintTextRunIsValid` refuses glyphs without a face and a face without glyphs; the span's
  bounds are checked at the container that owns the pool (`PaintListAppend`, `PaintDisplayListAppend`).
- Tokens, never face pointers, so `RoboPaintOperation.h`'s "a record is a value with no pointers in it" stays
  true. `PaintFontTokenPrimary` is the drawing pass's own `face` argument (one document, one face — no table);
  an assigned token resolves in the list's own `PaintFontTable`; a token that does not resolve is a **counted
  miss** (`noFont`), never a fallback. Faces are added/resolved **by address**, and `PaintFontTableAdd` is
  **idempotent** so recording per run does not grow the table per run.
- The recording pass shapes **once** (`PaintContextSetTextFace` + `PaintContextSetFontTable`), storing the ids,
  placements, advances, clusters and source ranges a `RoboTextGlyphRun` produced. A replay performs **no**
  decoding, font selection, GSUB, GPOS, kerning or `cmap` lookup — that absence is the phase.
- The drawing pass is the pen and nothing else: set the pen from the run rectangle, the baseline from
  `baselineOffset` (layout's, never recomputed), draw each id at `pen + offset` through `GfxRasterDrawGlyph`, then
  advance by `advanceX`/`advanceY`. **Positioning is already in the offset**; the rasterizer must not reinterpret
  it. A `missing` glyph draws `.notdef` and is counted; a `control` glyph draws and advances nothing but stays in
  the array so later indices match the shaped run.
- Both pools (`PaintTextPool`, `PaintGlyphPool`) and the table (`PaintFontTable`) are the list's **own copies**
  (`…PoolCopy`, `PaintFontTableCopy`) — the builder renumbers every span and token into the output, so a display
  list draws the same ink after the record list that produced it is gone, with **no face argument at all**.
- Capabilities: 12.16 set `glyphRuns`/`shapedTextRecording` in `RoboPaint.h` and bumped the paint version to
  `0.13.0`, read field by field in `Tests/Paint/SuiteRoboPaintConformance.c`. Do not add a glyph cache, an atlas,
  batching, a multi-face recorder or a subpixel-phase cache in this phase's name.
- Tests: `Tests/Paint/SuiteRoboPaintText.c` (`paint.text.*`, `paint.glyphrun.*`) via `TestPaint.ps1 -Target Host`,
  then `TestRoboText/Font/Graphics/Layout/Rasterizer/Desktop.ps1 -Target Host` (with `-Target Qemu` when the guest
  image is involved). Evidence: `paint.glyphrun.recorded` (ids/advances/clusters on the record),
  `paint.glyphrun.snapshot` (the same bytes after the records are freed), `paint.glyphrun.span_rules` (the
  one-fact invariant) and `paint.glyphrun.without_a_face` (a counted `noFont`).

### Input architecture (Phase 13.1/13.2)

**RoboInput is the one input normalization layer, and nothing above it may read a device or invent an event.**
The module is `Runtime/Source/RoboInput/` + public `Runtime/Include/RoboInput/`, and
`Documentation/Runtime/Input/Overview.md` is the map. A hardware driver normalizes; RoboInput models and
routes (13.31); nothing may do another's job. The module is at **0.7.0** and is complete as a *model*: what is
missing is a **consumer** — nothing outside the module includes it yet, which `§7` of the map states rather
than leaves to be inferred.

- **One event is a value, and its payload is a union.** `InputEvent` is trivially copyable, names nothing it
does not own, and carries `type`/`source`/`device`/`timestampMs`/`modifiers`/`space`/`window`/`display`/
`target` plus the one payload its type owns. `InputEventIsValid` refuses a record whose fields disagree about
what it is — seven rules listed in full in `RoboInputEvent.h`, including that **the coordinate space is set
exactly when the event has a position**. Do not add a second payload field beside a type; do not widen a
payload to say what a second one is for.
- **The type vocabulary is appended to, never reordered.** The values are what a backend writes and a saved
recording replays; `input.conformance.vocabulary` asserts each one and the count positionally.
- **A device is a descriptor in a fixed table, and disconnect *removes*.** `RoboInputDevice.h`: seven device
kinds, eleven capability bits, three connection states, and a registry whose `Disconnect` hands the removed
descriptor back so no stale device can be looked up, asked for as a type's primary, or left holding a key.
Drivers produce normalized events; this module consumes them and never depends on a driver.
- **Keyboard state is derived, not accumulated.** `RoboInputKeyboardState.h` keeps one held table and reads
the modifiers and locks off it, so a device disconnect cannot strand a key or a modifier; auto-repeat is one
event per call from a deadline the state pushes forward, driven by a caller's clock (a test simulates time
rather than sleeping). Never keep a second copy of "which keys are down".
- **The physical/logical/character distinction is three types, and none may be used for another.**
`PhysicalKey` and `LogicalKey`/`KeyCode` are RoboWindow's (`RoboWindowKey.h`); a backend's `scanCode` is
carried and read by nothing. **RoboText editing must never see a scan code** (13.5): it receives text and
commands.
- **Timestamps are monotonic milliseconds** from an epoch the runtime chooses, never a wall clock; 0 means
"the platform does not say". Ordering comes from the queue's `(timestamp, insertion)` rule.
- **State of this phase, as capabilities.** `InputCapabilities` is read field by field by
`input.conformance.capabilities`, and the zeros are the important half. Exactly five are **0** —
`deviceReading`, `platformAdapters`, `textComposition`, `eventLoop`, `multiprocessInput` — and each names a
goal that has not landed. Set one to 1 only in the change that builds it, and add the case that reads it.
- Tests: `TestInput.ps1 -Target Host`; `Assert-RoboInputBoundaries` (in `Runtime/Tools/Build.ps1`, run by
`-Verify`) fails on a Chromium include, a consumer-layer include, or a document-layer include beyond the two
identity headers — each allowance a named file or directory, so a *new* call site fails. Do not widen it.

### Coalescing, latency, accessibility hooks and gamepad navigation (Phase 13.47/13.48/13.52/13.54)

**Four policy layers, four files, and each answers a question the event model does not.** They sit *around* the
queue and *above* the router rather than inside either, so none of them changes what an event is.

- **Coalescing merges three types and nothing else.** `RoboInputCoalescing.h`: `InputEventTypeMayCoalesce` is
  the only door that answers the question, and it answers `1` for `POINTER_MOVE`/`TOUCH_MOVE`/`PEN_MOVE` only —
  **every other type is a barrier**, including one a later phase appends, because merging a press or a key
  either drops one or invents one. `InputCoalescerPush` does not dispatch: it answers `EMIT`, `HOLD` or
  `FLUSH_FIRST`, and `FLUSH_FIRST` is the whole ordering rule (a press may not overtake the moves it happened
  after). The key is *(device, pointer, type)*, so two fingers are two slots; the merged record is the
  **newest** event whole with the window's movement in `deltaX`/`deltaY`. Nothing is dropped — a full table is
  a `FLUSH_FIRST` — and a flush against a full queue answers `RoboBusy` with the remainder **still held**.
  Do not add a second merge rule and do not let a barrier take a slot.
- **A stamp that goes backwards is refused, not saturated.** `RoboInputLatency.h`: five stamps (hardware,
  queue, dispatch, handler, present) and four differences. Saturating an out-of-order stamp to zero would
  report a *good* latency for a call site measuring from the wrong clock, which is the one failure mode this
  layer must not have; a `Begin` whose queue stamp precedes its hardware stamp is refused for the same reason.
  `Begin` takes a never-used slot, then the oldest **finished** record, and answers `RoboBusy` only when every
  record is still live. This is not Phase 41: no sampling policy, no histogram, no storage beyond the process.
- **Accessibility invokes a meaning, never a keystroke.** `RoboInputAccessibility.h`: the vocabulary is the
  request, `InputA11yCommandMap` turns it into this module's own `InputCommand`, and `InputA11yInvokeCommand`
  routes it through the same `InputCommandRouter` a shortcut uses — with `InputA11yCommandAllowed` as the
  boundary: the text, focus and view families plus cancellation, and **not** the window family, the debug
  overlay, the reset or quit, because a client that could close the surface it was describing could dismiss
  the thing it was reading. A refusal is `RoboNotSupported` **before** the router, so a barred command cannot
  reach a target by another name. `SET_VALUE` is `RoboNotSupported` (no command exists) and `TOGGLE` is an
  activation; do not add a synthetic key event to make either work. An accessibility *tree* is Phase 31.
- **Gamepad navigation is a mapping, not a driver.** `RoboInputGamepadNavigation.h`: the deadzone is a number
  (at or inside it is a rest, so the comparison is strict), the direction is the **dominant axis** with ties
  going vertical, the d-pad beats the stick, and a pressed A or B beats both so an activation is one action.
  A **held direction repeats** on the 13.8 cadence shape from the caller's clock; an `Activate` or a `Back` is
  an **edge and never repeats**. `InputGamepadNavigationActionCommand` is how a controller reaches the command
  layer; nothing here applies an action, and nothing polls a controller (`deviceReading` stays 0).
- Tests: `Tests/Input/SuiteRoboInputCoalescing.c` (`input.coalescing.*`), `SuiteRoboInputLatency.c`
  (`input.latency.*`), `SuiteRoboInputAccessibility.c` (`input.accessibility.*`) and
  `SuiteRoboInputGamepadNavigation.c` (`input.gamepadnav.*`) through `TestInput.ps1 -Target Host`.

### Keyboard layout, dead keys and the text pipeline (Phase 13.9/13.10/13.11)

**A layout turns a position into a character and stops; a key event says a key changed state while a text
event says text should be inserted; and `KeyDown → insert a character` is never the universal path.** The
layout is `Runtime/Source/RoboInput/RoboInputLayout.c` + public
`Runtime/Include/RoboInput/RoboInputLayout.h`; the pipeline is `RoboInputText.c` + public `RoboInputText.h`.

- **Levels, and the two rules that are easy to get wrong.** `InputLayoutLevelFor` is the whole semantics:
shift, AltGr (spelled `AltRight` **or** `Ctrl+Alt`), and caps lock inverting shift **only for a cased key**,
which is why a mapping carries `RoboInputLayoutMappingFlagCased` and why `Shift+1` is still `!` with caps lock
on. A bare left Alt is a menu accelerator, not a third level. `InputModifiersIsControlChord` is the other
half: `Ctrl`-without-`Alt`, or `Meta`, produces **no** text — a runtime that inserted a character would fire
`Ctrl+C` *and* type a `c`.
- **A dead key is layout data.** Five accents, each with a spacing form, a combining form and a
precomposed-pair table listing **both cases** rather than deriving one by an offset (`ÿ`/`Ÿ` is the exception
a rule would be wrong once about). One accent waits at a time; arming it produces **no event**; composing,
being pressed twice (which emits the spacing form — that is how `´` is typed), being replaced by a different
accent, and being unable to compose (which emits the spacing form **and then** the character) are the four
resolutions. Never drop one of the two characters, and never emit a combining mark where the user typed a
spacing character.
- **`RoboInputTextOrigin` is the chord's, one entry per call.** It is a *report* — nothing above branches on
it — and both events of a rejected accent share it. Do not make it one-per-event: a caller passing a scalar
would have the second write land beside it.
- **The pending accent is cancelled, never leaked.** `InputTextPipelineCancelDeadKey` is what focus loss,
`Escape`, a window deactivation and a device disconnect call: an accent that outlived its field would modify
the first character typed into the next one. A keystroke that means no character, and a **release**, must not
disturb it.
- **A layout is data this module can hold and does not ship for every language.** `US_ASCII` and
`US_INTERNATIONAL` ship; `InputLayoutSetMapping` is how another arrives, and the table is a stated bound
(`RoboInputLayoutMaxMappings`) that refuses rather than drops. The table is kept in `(physical, level)` order
so a dump and two runs are byte-for-byte equal.
- **The pipeline inserts nothing.** It hands `TEXT_INPUT` events to whatever owns the focused editor — which
is 13.12/13.13's command layer and RoboText's editor (12.22), not this file. Do not call an editor from here.
- Capabilities: 13.9/13.10/13.11 set `keyboardLayout`/`deadKeys`/`textInput` plus
`layoutCount`/`layoutLevelCount`/`deadKeyCount`/`textEventsPerChord` and took the module to **`0.3.0`**; a
dead key deliberately does **not** set `textComposition`, because that is an input *method*'s preedit and its
candidates (13.37).
- Tests: `Tests/Input/SuiteRoboInputLayout.c` (`input.layout.*`, 7 cases) and `SuiteRoboInputText.c`
(`input.text.*`, 11 cases) via `TestInput.ps1 -Target Host`, then the boundary scan. `input.text.reject`
(two events, in the order they were typed), `input.text.repeat`, `input.text.level` (caps lock, AltGr and the
control chord) and `input.text.deadkey` are the decisive ones.

### Out-of-flow placement (Phase 14.0)

**A box a declaration takes out of flow is placed against a *containing block* rather than by flow, and
the containing block, the size and the coordinate are one question asked in three parts.** The module is
`Runtime/Source/RoboLayout/RoboLayoutAbsolute.c` + public `Runtime/Include/RoboLayout/RoboLayoutAbsolute.h`;
the recursion that calls it is `RoboLayoutBlock.c`'s; the contract is
`Documentation/Runtime/Layout/OutOfFlow.md`. It is the behaviour Phase 10.12 named and deferred, pulled
forward because Phase 14's shell cannot be composed without it.

- **One policy per axis.** `LayoutAbsoluteResolveAxis`: the near side (`left`/`top`) wins where declared —
  and a *definite* declared size beside it is **over-constrained** and counted rather than resolved in the
  far side's favour; the far side positions the axis where the near side says nothing (absent *or* `auto`);
  and both insets present with no definite dimension **size** the axis between them. Do not add a second
  precedence rule anywhere, and do not read `left`/`right` as "the near side is the position" — those are
  two different questions and `layout.absolute.policy_near_wins` asserts both halves.
- **The origin is a second call, and that is the phase.** `LayoutAbsolutePlacementOrigin` takes the box's own
  margin box, because `right: 8px` means "my right edge is 8px in from the containing block's right edge"
  and is not a coordinate until the box has a width. A build that resolved the origin before sizing can only
  ever implement the near side.
- **The containing block is the ancestor's *padding* box, and every number the pass writes is
  content-relative.** The padding box origin in that space is therefore `(-padding-left, -padding-top)`; a
  border appears in the number nowhere, and `layout.absolute.containing_block_padding_edge` is the case that
  pins it. The **initial** containing block is the viewport, recorded once per pass with its origin at the
  root element's content edge — one rectangle for every `fixed` box, never one before the root's call and
  another after it.
- **Containment is a walk, not a parent link.** `LayoutBlockPlaceDescendants` descends through **static**
  children carrying the same containing block (a static wrapper is transparent to containment) and does not
  descend into a **positioned** child, which placed its own descendants and is a containing block of its own.
  A box is placed *after* its containing block's own flow is final; `LayoutBlockChildren` **skips** an
  out-of-flow child, so it takes no room and grows no parent.
- **`fixed` is the one mode that ignores its ancestors**, and `absolute` reaches the viewport only when no
  ancestor is positioned. Both are `LayoutAbsoluteIsOutOfFlow` / `...IsViewportAnchored`, asked of 9.13's own
  vocabulary rather than re-derived; a NULL record is `static`, which is CSS's initial value.
- **Not built, and named rather than implied:** `z-index`, stacking contexts and paint order (nothing is
  reordered — paint is still document order, so the shell's own CSS does not use `z-index` to say what is on
  top), `sticky`, a scrollport, `shrink-to-fit`, the static-position rule, percentage insets, `box-sizing:
  border-box`, out-of-flow **inline-level** boxes (the inline pass still places those on their line), and
  folding an out-of-flow descendant's region into its containing block's 10.13 overflow record.
- Reading it: `LayoutBlockStats.absolute` holds the phase's seven counters in their own record, and
  `offsetsDeclined` no longer counts `absolute`/`fixed` — no displacement is resolved for a box that is not
  in flow. `RoboLayoutDomainAbsolute` is the twenty-third diagnostic domain, and
  `LayoutGetCapabilities().outOfFlow` is the flag, with the absences above named at it.
- Tests: `Tests/Layout/SuiteRoboLayoutAbsolute.c` (`layout.absolute.*`, 21 cases) via
  `TestLayout.ps1 -Target Host`, then `TestPaint`, `TestGraphics`, `TestRasterizer`, `TestDesktop` and
  `TestFont` (`-Target Host`). `layout.absolute.block_positioned_wrapper` (the padding-box origin, the box's
  size and the sibling after it unmoved), `...block_stretch` (the wallpaper), `...block_fixed` (the dock
  ignoring a positioned ancestor, and moving when the viewport does), `...static_wrapper_transparent` and
  `...nested_containing_blocks` are the decisive ones.

### Compositor (Phase 15)

**RoboCompositor is a software compositor, and its one output is pixels in a surface the caller owns.** It
adds no rasterizer, no display backend and no frame thread, and it may include `RoboGraphics` and
**nothing above it** — `Assert-RoboCompositorBoundaries` in `Runtime/Tools/Build.ps1` refuses RoboRender,
RoboPaint, RoboLayout, RoboWindow, RoboDisplay, RoboDesktop and RoboInput by name, because "just walk the
display list here" is the one simplification that would make the module untestable without a renderer and
turn 15.6's seam into a formality. Authority: `Documentation/Runtime/Compositor/Architecture.md`.

- **The layer tree is the model and paint order is child order.** A layer carries an id, a parent, a
  surface, a rectangle, an offset, a uniform scale, an opacity, an axis-aligned clip and visibility.
  `CompositorLayerPaintOrder` walks it *back to front* and is the **only** place an order is decided:
  nothing re-sorts, and a reparent is a move in the child array rather than a second ordering. Geometry a
  consumer is handed (`CompositorLayerBoundsInCompositor`) is the accumulated transform in the
  compositor's own space, computed on demand and never stored twice.
- **Damage is a region list that merges, and over-merging is the right trade.** A false positive costs a
  present; a false negative is a stale pixel, so `CompositorDamageAdd` merges a rectangle that touches or
  overlaps, and a list past `maxDamageRects` becomes a **full-surface** present
  (`damageOverflowToFull`) rather than dropping regions. `CompositorDamagePixelCount` is the metric
  everything else is read against, and a present keeps its damage when it refuses.
- **Tiles exist so a *region* is schedulable, not to be a second surface.** One grid per surface with
  three states, and the dirty set is **derived from the damage region** (`CompositorTilesDirtyFromDamage`) —
  a tile list maintained by hand would be a second answer to "what is stale". A present marks the tiles it
  covered clean; a viewer that wants to re-render one tile asks for it.
- **Rasterizing is the caller's function.** `CompositorSetRasterFn` installs the one callback and
  `CompositorRunScheduled` runs jobs under a per-frame budget. A callback that refuses **keeps its job**
  rather than losing it (`compositor.job.refusal`), a job with no callback is counted rather than
  silently dropped (`compositor.job.nocallback`), and a job is deferred rather than dropped when the
  budget is spent. This module rasterizes no display list of its own.
- **Surfaces are refcounted and pooled, and the budget evicts by least-recent use.**
  `CompositorSurfaceAcquire`/`…Release`/`…Trim`/`…Destroy`; destroying a surface **detaches the layers
  that referenced it**, so no layer holds a surface id that no longer resolves (`compositor.surface.replace`)
  and `CompositorValidate` is the door that proves it. `CompositorResourceIsAvailable` refuses the two GPU
  kinds **by name** rather than pretending to allocate a texture.
- **The clock is the caller's, and a fence says the honest thing.** Nothing sleeps and nothing reads a
  wall clock: `CompositorAdvanceTime`/`CompositorTimeNow` are the only time doors, vsync is an *interval*
  on that clock (`CompositorSetVsync`, with a late frame **counted** rather than hidden), and a present
  fence reports that the destination **is** those pixels — there is no swap chain, so `doubleBuffering` is
  0 rather than a claim.
- **Device/other threads post; the frame runs on one thread.** `CompositorPostFrame` is the producer door
  and `CompositorPumpFrame`/`…DrainPosts` the single consumer, with coalescing counted (`compositor.frame.writers`);
  `compositorThread` is 0 because this build has one frame thread, the caller's. Do not add a frame thread
  here without changing 15.14's stated model.
- **Animations are tracks on a property**, five properties and three easings (`CompositorEasingApply`), with
  `reducedMotion` snapping to the end instead of animating. A track is a value: replacing or cancelling one
  is `CompositorAnimationCancel`/`…Replace`, never a leak and never a second interpolator. Smooth scrolling
  is the same track over a layer's scroll offset, not a second scroll path.
- **Not built, and named rather than implied:** GPU resources and GPU/CPU sync (`gpuResources`, `gpuSync`),
  a compositor thread, double buffering, rotation (`rotation` 0 — a quarter turn is the rasterizer's
  business, as 12.20 already established), subpixel sampling (a scaled layer is sampled nearest neighbour
  by integer arithmetic), colour management, layer blur and tone mapping, filters/backdrop effects,
  non-uniform scale, and presenting to a display (`presentToDisplay` 0: the destination is a surface, and a
  framebuffer is `RenderPresentFramebuffer`/RoboDisplay's door, one module over). Each is a row in the
  capability table's second block, and `Documentation/Runtime/Compositor/Architecture.md` §8 states what it
  would take.
- **No production consumer yet, and that is the phase's own stop line.** RoboCompositor is a leaf consumer
  of RoboGraphics: Phase 15 built the composition model and proved it against its own suites. Wiring a
  shell's frame loop to it — a layer per window, one surface per window, `CompositorPumpFrame` where the
  shell presents today — is the next phase's work and needs no change to this module.
- Capabilities/version: `CompositorGetCapabilities` reports **1.0.0**'s set in two blocks, the second being
  the absences above; the numeric fields are the bounds every fixed-size table is allocated from, so a
  test reads a limit rather than hard-coding one. `CompositorWriteCapabilities` and
  `CompositorWriteSummary` dump them.
- Tests: `Tests/Compositor/SuiteRoboCompositor.c` (`compositor.*`, the vocabulary, the capability table, the
  clock, counters, refusals, the dump and the fuzz invariant),
  `SuiteRoboCompositorVisual.c` (the tree, geometry, paint order, hit testing through clips, the composite
  pass, damage, the present and 15.18's determinism) and `SuiteRoboCompositorScheduling.c` (the pool and its
  budget, the resource policy, tiles, the scheduler, animations, smooth scrolling, the frame pump, vsync and
  fences) — 50 cases through `TestCompositor.ps1 -Target Host`, then `TestRuntime.ps1 -Target Host`. A
  compositor change is not done until both pass. The measured baseline is
  `compositor.performance.baseline`: it prints a frame's cost and asserts the cost **model** rather than a
  number — under region presents the pixels a frame writes are the damage and the layers over it, so
  doubling the damaged area doubles the work.

### Minimal RoboJS (Phase 16)

**There is one JavaScript runtime in RoboticelaOS, it is independently implemented — no V8, no Node, no QuickJS,
no Duktape — and every layer that wants a script evaluated talks to it or not at all.** The module is
`Runtime/Source/RoboJS/` + public `Runtime/Include/RoboJS/`, it sits above `RoboHTML` and below the shell, and it
is the *only* consumer that reads a document through a vtable of its own. Authority:
`Documentation/RoboJS/Overview.md` (the map), `Limitations.md` (every gap with the layer it belongs to) and
`APIStability.md` (the contract, `1.0.0`, `apiFrozen`).

- **The pipeline is one pass each way and no layer skips one.** lexer → parser → AST (+ a diffable printer) →
  interpreter → values/objects/environments → the host seams. There is **no bytecode, no IR, no JIT and no
  constant folding**; `js.runtime.dump` and the AST printer are the reason a change can be *diffed* rather than
  eyeballed, and `JsAstIsWellFormed` is a closure property (every node kind has an evaluator arm) so "parsed but
  not runnable" is impossible rather than unlikely.
- **One DOM seam.** `RoboJSDom.h` is a `JsDomHost` vtable and names no HTML type; `RoboJSDomHtml.c`/`.h` are the
  `RoboHTML` adapter, and `RoboJSDomHtml.h` is deliberately **not** in `RoboJS.h`. A host test implements the same
  vtable over a fixture — that is what makes the binding's behaviour testable without a parser, and it is why a
  second document implementation means a second adapter rather than a flag inside this one.
- **`RoboHTML`'s construction gate is asserted, not worked around.** A parsed document accepts no new nodes
  (Phase 8 pins that refusal), so a page's script reads everything, writes every attribute, rearranges the nodes
  the document already has, and **creates nodes only where construction is open** — a never-parsed document or one
  `HtmlDocumentReset` reopened. A `textContent` write that would create a node is refused **before anything is
  destroyed**; the adapter classifies the shape first (`js.integration.creation_refused` / `…creation_open`).
- **A limit is a limit, and the number lives in one place.** `RoboJsDefault*` in `RoboJSTypes.h` bounds steps
  (500 000), call depth (64), parse depth (64), properties per object (8 192), string length (1 MiB), timers
  (1 024), the task queue (4 096), timers per cycle (256), tasks per cycle (4 096), microtasks per cycle (4 096)
  and the heap chunk (64 KiB) — published through `JsGetCapabilities` and read in `js.limit.budgets` rather than
  repeated at a call site. Exhaustion is a located `RoboJsErrorLimit` and **the runtime stays usable afterwards**,
  because "the OS did not freeze" is the point of the limit.
- **The heap is a chunked bump allocator released as a whole.** No collector, no per-object free, no reference
  counting, no weak references — `JsHeapReset` is the door, a cycle can never leak, and memory grows until a
  reset. The cost is stated in `Memory.md` rather than hidden, and `JsRuntimeReset` is what a shell does between
  pages: timers cancelled, both queues cleared, listeners and wrappers dropped, builtins kept.
- **One boundary order, and the loop is not reentrant.** `JsLoopRunOne` drains the microtask queue to empty,
  collects due timers only when the task queue is empty, and runs **at most one** job — so a queued task beats a
  timer that became due while it waited, a job's own microtasks run at the next boundary, and
  `queueMicrotask(f); setTimeout(g, 0)` prints `f` then `g` as a fact about the loop rather than a race. A listener
  that calls a loop door gets `RoboInvalidArgument` and must **post** (`JsLoopPostTask`, `JsMicrotaskQueue`);
  `js.loop.reentrancy` asserts that from five places. **Producers post, the owner runs** — nothing here calls a
  JavaScript function from another thread's stack (`Threading.md`).
- **A timer is a due record on the caller's clock.** `JsTimerSet` records and returns an id; it calls nothing. With
  `deterministic = 1` the clock is a counter only `JsRuntimeAdvanceClock` moves, with no clock at all it is the
  runtime's own counter (deliberately **not** wall time), and a delay of 0 is due now but still runs at a loop
  boundary — which is why the statements after `setTimeout(f, 0)` run first.
- **The sandbox is the absence of host capability.** The global table is the whole list (`Limitations.md` §2):
  `Object`, `String`, `Number`, `Boolean`, `Math`, the five error kinds, `parseInt`, `console`, `RoboJS`,
  `globalThis`, the scheduling doors, the three literals, and `document`/`window` once a host installs them.
  `hostProcessAccess`, `hostFileSystem`, `hostNetwork`, `hostEnvironmentVariables` and `multiThreaded` are 0 in the
  capability table by name, and `js.security.boundary` reads the globals to prove no host door is reachable.
- **An absence is stated, not implied.** `JsCapabilities`' second block is the list a consumer would otherwise
  assume, and `js.runtime.capabilities` reads **every** field of all three blocks — so a flag that stops being true
  fails a test. Adding a capability means adding it to that case in the same change.
- **Refusals are named.** A `textContent` reshape, `queueMicrotask(nonFunction)`, a full job queue, a re-entrant
  loop door, an exhausted budget and a RoboHTML construction refusal each answer with a name and a reason; a
  failed mutation is never hidden as a stored property, and a host failure reason is never overwritten by a
  generic one.
- **Not built, and named rather than implied:** `class`, `async`/`await`, generators, modules, destructuring,
  rest/spread, template literals, optional chaining, `??`, `Symbol`, `BigInt`, typed arrays, regex literals,
  `Proxy`/`Reflect`, `Promise`, weak collections, a garbage collector, a JIT, a bytecode VM, multi-threading,
  `Date`, `JSON`, `Array` as a global constructor, most `String`/`Array`/`Math` methods, `innerHTML`,
  `MutationObserver`, capture-phase events, `requestAnimationFrame`, and any host capability access. Each is a row
  in `Limitations.md` and a `0` in the capability table.
- **Stated divergences from the specification**, each asserted by a case rather than left to drift: no temporal
  dead zone (a hoisted `let`/`const` reads `undefined`), no redeclaration error, a top-level `const` **is** a
  non-writable property of the global object, ASI is partial, and the shortest round-tripping number form is
  approximated. `Limitations.md` §11 is the table.
- **The shell has no text engine and no script engine of its own.** A consumer calls `JsRuntimeEvaluate` and the
  loop doors. Today `Assert-RoboDesktopBoundaries` names `RoboJS` among the things a RoboDesktop file may not
  reach for, so running a page's script is one bridge file **plus** that pattern in the same commit, and the
  RoboInput → desktop pointer path that would make a click a node-targeted event is Phase 13's open work
  (`Documentation/RoboJS/Integration.md` §4 is the gate table, `Events.md` §5 the one-line shape).
- **No `math.h`.** `Runtime/Source/RoboJS/RoboJSMath.h` is a header-only shim and the suites compute `NaN`/
  `Infinity` by division from variables — the guest libc has no math header, and it is part of why the module is
  guest-buildable. **There is no guest half yet**: no `ROBO_QEMU_JS_*` marker, no `test-qemu` command and no
  `RoboJS` entry in the guest program's `RuntimeModules`; `Documentation/RoboJS/QemuTesting.md` states the absence
  and the four things that would change together rather than implying a suite.
- Version/capabilities: 16.16 froze the public surface at **`1.0.0`** with `apiFrozen = 1`, in two tiers named by
  `RoboJS.h` (the seam a consumer uses, the engines behind it). `Documentation/RoboJS/APIStability.md` is the
  contract: ownership, lifetime, threading, error handling and what a version bump means.
- Tests: three suites and **45 cases** — `Tests/RoboJS/SuiteRoboJS.c` (the language, 23),
  `SuiteRoboJSHost.c` (the seams over a 32-node **fixture** DOM, 11) and `SuiteRoboJSIntegration.c` (the real
  `RoboHTML` adapter, 11) — through `Scripts/Test/TestRoboJS.ps1 -Target Host`. A DOM-adapter change is not done
  until `TestHtml.ps1` and `TestDesktop.ps1 -Target Host` pass too; a change to the module boundary, its link
  list or its suites needs `-Verify`, which is what runs `Assert-RoboJSBoundaries`.

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
| Compositor | `BuildCompositor.ps1` (`-Verify` runs the module's own boundary scan) | `TestCompositor.ps1 -Target Host` (`compositor.*`, 50 cases), then `TestRuntime.ps1 -Target Host` (the whole-suite gate). Evidence: `compositor.visual.conformance` (a clipped, scrolled, translucent and scaled scene rendered twice and compared byte for byte), `compositor.present.damage` (a region present writing only its damage, counted) and `compositor.frame.writers` (the post queue's coalescing), plus `Build/Compositor-Conformance.png` |
| Glyph rasterization / typography | `BuildGraphics.ps1` (and `BuildFont.ps1` if RoboFont changed) | `TestGraphics.ps1 -Target Host`, then `TestPaint.ps1 -Target Host` and `TestDesktop.ps1 -Target Host`; `-Target Qemu` when the guest image is involved. Visual evidence: `Build/Typography-Coverage.png` against `Build/Typography-Stroke.png` |
| System typeface pack | `BuildFont.ps1` (module + pack validation; the image build installs `System/Fonts/`) | `TestFont.ps1 -Target Host` (`font.systempack.*`); `-Target Qemu` when the installed image is involved |
| Typography roles / face binding | `BuildTypography.ps1` (module + boundary scan + its suites); the image builds the module into the guest runtime | `TestTypography.ps1 -Target Host` (`typography.*`), then `TestDesktop.ps1 -Target Host` (`desktop.typography.*` and the three galleries); `-Target Qemu` when the guest image is involved. Visual evidence: `Build/Typography-Desktop.png`, `Build/Typography-Chrome.png`, `Build/Typography-Document.png` |
| Text architecture (RoboText) | `BuildRoboText.ps1` (module + boundary scan + its suites); the image builds the module into the guest runtime | `TestRoboText.ps1 -Target Host` (`text.*`), then the suites for every boundary it touched: `TestLayout.ps1 -Target Host`, `TestPaint.ps1 -Target Host`, `TestGraphics.ps1 -Target Host`, `TestFont.ps1 -Target Host`, `TestDesktop.ps1 -Target Host`; `-Target Qemu` when the guest image is involved |
| Unicode / font integration (RoboText 12.5) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.unicode.*`, `text.fontselection.*`, `text.analysis.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` |
| Script runs (RoboText 12.6) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.script.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` |
| Direction runs (RoboText 12.7) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.direction.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host`. Evidence: the `dir[i]`/`base` rows of `Build/Text-GlyphTable.txt` |
| Glyph positioning (RoboText 12.11) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.gpos.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host`. Evidence: the `text.gpos.shipped_pack` advance difference and the `text.gpos.glyph_dump` geometry |
| Glyph substitution (RoboText 12.10) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.gsub.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host`. Evidence: `Build/Text-GlyphTable.txt` (the glyph ids and advances the substitution produced) |
| Ligatures (RoboText 12.12) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.ligature.*`, `text.gsub.ligature` and `text.gsub.ligature_positioning`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host` |
| Kerning (RoboText 12.13) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.kern.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host`. Evidence: `text.kern.pairs` (the shipped face's own kerning moving the advance) and `text.kern.range_override` (the per-range split) |
| Glyph runs (RoboText 12.14) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.glyphrun.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host`. Evidence: `text.glyphrun.clusters_ligature` (one glyph, one cluster, two scalars), `text.glyphrun.order_visual` (the Latin word painted between two Hebrew ones) and `text.glyphrun.performance` (the baseline) |
| Text measurement (RoboText 12.15) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.measure.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host`. Evidence: `text.measure.kern` and `text.measure.ligature` (the two things that move a width, on the shipped face), `text.measure.mark_ink` (an accent raising the ink while the width stays the base's) and `text.measure.performance` (the two baselines) |
| Paint / layout bridge (12.16) | `BuildPaint.ps1` | `TestPaint.ps1 -Target Host` (`paint.text.*`, `paint.glyphrun.*`), then `TestRoboText/Font/Graphics/Layout/Rasterizer/Desktop.ps1 -Target Host`; `-Target Qemu` when the guest image is involved. Evidence: `paint.glyphrun.recorded` (the glyphs on the record), `paint.glyphrun.snapshot` (a display list drawing the same bytes after its records are freed) and `paint.glyphrun.without_a_face` (a counted `noFont`) |
| CSS font wiring (12.17) | `BuildLayout.ps1` (+ `BuildDesktop.ps1` when the provider or the document changed) | `TestLayout.ps1 -Target Host` (`layout.fontrequest.*`, `layout.fontselect.*`), then `TestPaint.ps1 -Target Host` (the pen draws the run's own face) and `TestDesktop.ps1 -Target Host` (`desktop.document.font_families` resolves a shipped name, an unknown name and a generic over the real registry); `-Target Qemu` when the guest image is involved |
| Complex scripts (12.19) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.arabic.*`, `text.indic.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host`. Evidence: `text.arabic.forms` (the four contextual forms arrived as four glyph ids), `text.arabic.lam_alef_ligature` (one glyph, two scalars, both still the text), `text.indic.conjunct` (`क` `्` `ष` as one glyph covering three scalars), `text.indic.reph` (written first, drawn last, sources unmoved), `text.indic.pre_base_matra` (a vowel sign shaped before its consonant),  `text.indic.three_scripts` (three strategies in one string, three runs, each in logical order), `text.arabic.no_layout_tables` (a face with neither table still answers with its `cmap` and its direction) and `text.arabic.fallback_no_split` / `text.indic.fallback_no_split` (the chain answers the complex runs and no run boundary lands inside a cluster) |
| Vertical writing (12.20) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.vertical.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host`. Evidence: `text.vertical.mark` (one face, one `mark` rule, two modes: the anchor difference minus the **inline** step — `vmtx`'s 1006 units vertically against `hmtx`'s 406 horizontally — with the mark advancing nothing on either axis), `text.vertical.writing_axes` (the inline/block pairs, and `vertical-rl` vs `vertical-lr` differing only in the block axis), `text.vertical.orientation` (the **rotated** table beating the script: halfwidth `ｶ` sideways although its script stands up), `text.vertical.orientation_context` (a space between two ideographs staying upright), `text.vertical.run_split` (Han/Latin/Han as three runs with three resolved orientations, forced `sideways` leaving the three script runs), `text.vertical.vert_feature` (`vert` substituting through 12.10's GSUB vertically and **not** horizontally), `text.vertical.advance` (`advanceX == 0`, `advanceY` the **substitute's** `vmtx` entry, the horizontal case unchanged), `text.vertical.origin` (`offsetY` the negated `vmtx` origin), `text.vertical.synthesised` (a face with no `vmtx` shaped at its em box and every glyph counted), `text.vertical.measurement` (`advanceY == Σ run advanceY` and `width == 0` for a vertical run), `text.vertical.bidi` (an Arabic run still RTL with a level inside a vertical column) and `text.vertical.determinism` |
| Whole-pipeline conformance (12.23) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.shaping.corpus.*`, 335 cases) and `TestRoboTextConformance.ps1 -Target Host` (68), then `TestLayout/Paint/Graphics/Rasterizer/Font/Desktop.ps1 -Target Host`. Evidence: `text.shaping.corpus.stages` (the layer chain asked one door at a time), `.determinism` (8 samples × 100 repetitions, byte for byte), `.malformed` (eleven single corruptions, each shaped twice) and `Build/Text-ConformanceCorpus.txt` (the diffable table). No capability flag, no new fixture, no fourth guest render |
| Selection mapping (12.21) | `BuildRoboText.ps1` (+ `BuildUser.ps1` for the guest checkpoint) | `TestRoboText.ps1 -Target Host` (`text.selection.*`), then `TestLayout/Paint/Graphics/Font/Desktop.ps1 -Target Host`, then `TestDesktop.ps1 -Target Qemu` for the guest render. Evidence: `text.selection.ligature` (both halves of `ffi` snap to the cluster, and the range selects the whole glyph rather than part of it), `text.selection.marks` (a base and its zero-advance marks are **one** rectangle, and a mark alone is caret-wide rather than invisible), `text.selection.rtl` (an Arabic run's first cluster at its **largest** inline offset), `text.selection.bidi` (one source position, two carets, one line apart, and a wrapped selection giving one rectangle per line), `text.selection.vertical` (the same three queries on the other axis: the inline extent is `advanceY`, the block extent is the band, and the horizontal run states the swap), `text.selection.fallback` (a font boundary is not a logical boundary — two faces, one continuous selection) and `text.selection.roundtrip`. Guest evidence: the `ROBO_QEMU_SELECTION_*` markers, including the filled rectangle's own pixel count |
| Text editing (12.22) | `BuildRoboText.ps1` | `TestRoboText.ps1 -Target Host` (`text.editing.*`), then `TestLayout/Paint/Graphics/Rasterizer/Font/Desktop.ps1 -Target Host` and `TestRoboTextConformance.ps1 -Target Host`. Evidence: `text.editing.utf8` (a one, two, three and four-byte scalar each deleted as **one** scalar, and an ill-formed insertion refused rather than repaired), `text.editing.marks` (`e` + two accents as one character unit, plus the marks-only text), `text.editing.ligature` (`ffi` as one cluster with a resolver and three characters without, and the typed path shaping like the text it produced), `text.editing.arabic` (`لا` as two one-scalar complex clusters but **one** shaped cluster, and a joining sequence reshaped after a delete), `text.editing.indic` (the conjunct as one unit, the half form after one scalar, the conjunct reformed by two insertions), `text.editing.bidi` (the flag on when the inserted **or** removed scalar is Arabic or a digit, off for Latin), `text.editing.vertical` (the same editor doors over a column), `text.editing.composition` (cancel restoring the text **and** the selection the composition began over), `text.editing.transaction` (the spanning commit and the restoring cancel), `text.editing.transform`, `text.editing.invalidation` (the paragraph range ending at the terminator) and `text.editing.fuzz` |
| Latin/European shaping conformance (12.18) | `BuildRoboText.ps1` (and the image build, which compiles the guest specimen) | `TestRoboTextConformance.ps1 -Target Host` (`text.shaping.*`), then `TestRoboText.ps1 -Target Host`, `TestPaint.ps1 -Target Host` (`paint.glyphrun.agrees_with_shaping`) and `TestLayout/Graphics/Font/Desktop.ps1 -Target Host`; then `TestDesktop.ps1 -Target Qemu`, which runs the guest specimen. Evidence: `text.shaping.integration.dump` (`Build/Text-ShapingConformance.txt`), `desktop.typography.gallery.shaping` (`Build/Typography-Shaping.png`) and the `ROBO_QEMU_SHAPING_*` markers |
| Desktop UI | `BuildDesktopUi.ps1` | (compiled into the desktop scene) |
| Desktop | `BuildDesktop.ps1` | `TestDesktop.ps1 -Target Host` or `-Target Qemu` |
| Window | `BuildWindow.ps1` | `TestWindow.ps1 -Target Host` or `-Target Qemu` |
| Input | `BuildInput.ps1` | `TestInput.ps1 -Target Host`, then `-Verify` (the boundary scan and the four RoboText integration rules) |
| Input — device table, event vocabulary, keyboard state (13.2) | `BuildInput.ps1` | `TestInput.ps1 -Target Host` (`input.device.*`, `input.payload.*`, `input.keyboard.*`). Evidence: `input.device.primary` (the first of a type is its primary, a later flag takes it, a disconnect hands it to the next), `input.device.disconnect` (removal rather than marking, with the removed descriptor handed back) and `input.keyboard.repeat` (one event per call from a deadline the state pushes forward, and never a repeat after a release) |
| Input — keyboard layout, dead keys, the text pipeline (13.9/13.10/13.11) | `BuildInput.ps1` | `TestInput.ps1 -Target Host` (`input.layout.*`, `input.text.*`). Evidence: `input.layout.levels` (the AltGr spellings, and caps lock inverting shift **only** for a cased key), `input.layout.builtin` (`a`/`A`, `1`/`!`, an unmapped key as `NotFound`, and the international table's five dead accents), `input.text.levels` (caps lock, AltGr on a table with no third level, and a control chord producing nothing) and `input.text.reject` (a dead accent and an incompatible base as **two** events, in the order they were typed) |
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
