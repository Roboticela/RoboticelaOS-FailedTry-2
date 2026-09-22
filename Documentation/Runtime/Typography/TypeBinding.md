# Desktop + document face binding (Phase 12.3)

Phase **12.2** put the system typefaces in one place and gave them semantic *font* roles: `UI Display`,
`UI Text`, `Monospace`, resolved through the Phase 5 pipeline from `/System/Fonts/system-fonts.manifest`.

Phase **12.3** is the other half of that sentence. It answers the question a UI surface actually asks —
*what type is this label set in?* — with a **typography role**, and it makes every existing text surface
in the shell ask that question instead of holding a font file of its own.

```text
Desktop / Chrome / Document UI
            │   asks for a role, never a file
            ▼
     TypographyRole          RoboTypography/RoboTypographyTypes.h
            │
            ▼
     TypographyStyle         a loaded face + size + weight + leading + optical class
            │
            ▼
   System Typeface Pack      RoboFont/RoboFontSystemPack.h         (Phase 12.2)
            │
            ▼
        RoboFont             FontSelect → a registry-owned FontFace  (Phase 5)
            │
            ▼
   RoboLayout / RoboPaint → RoboGraphics → framebuffer
```

The rule the phase establishes, in one line: **a UI component names a role, and the role names the
face.** Nothing above the resolver knows `/System/Fonts/` exists, what the family is called, or which
file it came out of.

---

## 1. Where the pieces live

| Piece | Where | Owns |
|---|---|---|
| Roles, optical-size classes, weights, the resolved style record | `RoboTypography/RoboTypographyTypes.h/.c` | the vocabulary |
| The one table that says what each role is set at | `RoboTypography/RoboTypographyScale.h/.c` | the numbers |
| Role → face, the cache, coverage fallback, the document sheet | `RoboTypography/RoboTypographyResolver.h/.c` | the binding |
| The desktop's surfaces | `RoboDesktop/RoboDesktopChrome.c` | which role a band uses |
| The document bridge | `RoboDesktop/RoboDesktopDocument.c` | face + generated defaults |
| The host launcher's stack | `RoboDesktop/platform/host/RoboDesktopHost.c` | pack open, for the checkpoint |
| The guest's stack | `User/robo-runtime/main.c` | pack open, for the image |
| Suites | `Tests/Typography/`, `Tests/Desktop/SuiteRoboDesktopTypography.c`, `Tests/Desktop/SuiteRoboDesktopTypographyGallery.c` | what is asserted |

`RoboTypography` is a **new module** rather than a file inside RoboDesktop because the document bridge
and the shell both need it and neither owns it. It sits between `RoboFont` and `RoboDesktop` and may not
include `RoboHTML`, `RoboCSS`, `RoboLayout`, `RoboPaint`, `RoboRender`, `RoboWindow`, `RoboDisplay` or
`RoboGraphics` — the consumer-layer rule that `Assert-RoboTypographyBoundaries` in `Runtime/Tools/Build.ps1`
enforces, and the one that keeps it a binding rather than a renderer.

---

## 2. Roles

Twenty-one roles in four groups, in `RoboTypographyRole`. The groups are the surfaces the phase binds:

| Group | Roles |
|---|---|
| Desktop | `DesktopTitle`, `DesktopSubtitle`, `WindowTitle`, `WindowSubtitle` |
| Chrome | `ChromeLabel`, `ChromeButton`, `ChromeMenu`, `ChromeMenuItem`, `ChromeTab`, `ChromeAddress`, `ChromeStatus` |
| Document | `DocumentBody`, `DocumentHeading1`, `DocumentHeading2`, `DocumentHeading3`, `DocumentCaption`, `DocumentCode` |
| System | `SystemLabel`, `SystemSecondaryLabel`, `SystemEmphasis`, `SystemMonospace` |

`RoboTypographyRoleNone` is not a role; it is the value of a cleared record, and asking for it is an
argument error rather than a default.

**The groups are not decoration.** The `ChromeTab`, `ChromeAddress`, `ChromeMenu` and `ChromeMenuItem`
roles exist, resolve, and are set at deliberate numbers, but this build's shell has no tab strip, no
address bar and no menu bar — Phase 14 (and the browser chrome that follows it) is what will draw them.
They are in the vocabulary now because a role added later would be a role whose numbers were chosen
without the rest of the scale in view; a role present now costs one row in a table and one line in a
suite. Every one of them resolves against the shipped pack and is covered by `typography.resolve.roles`.

---

## 3. The scale

`TypographyDefaultScale` in `RoboTypographyScale.c` is **the file**. One array, one entry per role, and
each entry is `{role, fontRole, weight, sizePx, lineHeightPx, opticalSize}`.

| Step | Size | Roles set at it |
|---|---|---|
| Display | 32 | `DocumentHeading1` |
| Heading | 24 | `DocumentHeading2` |
| Title | 19 | `DocumentHeading3` |
| Body | 16 | `DocumentBody` |
| — | 14 | `DocumentCode` |
| Caption | 13 | `DesktopTitle`, `WindowTitle`, `DocumentCaption` |
| — | 12 | `ChromeLabel`, `ChromeButton`, `ChromeMenu`, `ChromeMenuItem`, `ChromeTab`, `ChromeAddress`, `SystemLabel`, `SystemMonospace` |
| Label | 11 | `DesktopSubtitle`, `WindowSubtitle`, `ChromeStatus`, `SystemSecondaryLabel`, `SystemEmphasis` |

Two things about those numbers are decisions rather than accidents.

**The UI roles cluster in 11–13 px because the shell's geometry is fixed and this phase may not redesign
it.** A 30 px panel and a 24 px title band were sized in 11.13 for an 11 px label; the phase's job is to
change *which face and which weight* those surfaces draw with, not to move them. So binding the chrome
changed its type without moving a rectangle — which is exactly what
`desktop.typography.roles_change_picture` asserts, by showing that the role-driven shell and the legacy
single-face shell draw *different* frames at the same label count.

**The document roles are free to be large**, and `DocumentHeading1` at 32 px is the one place in this
build where type is genuinely display-sized.

A caller may pass **its own scale** to `RoboTypographyResolverCreate`. That is what makes a restyle a
dataset change rather than a code change, and it is what the gallery suites use to prove that a size
comes from the scale: they build a second resolver over the same pack with one role resized and assert
the frame changes.

---

## 4. Optical size

`RoboTypographyOpticalSize` is a **class**, not a number: `Small`, `Body`, `UI`, `Display`, `LargeDisplay`.
Every role is set at one of them.

The shipped Inter face is variable and has a real `opsz` axis (14–14–32), and the manifest preserves it.
**This build does not apply it.** RoboFont parses `fvar` and exposes the axis as a selectable hint, but it
does not interpolate outlines (`FontOutlineVariationApplied` stays 0), so applying `opsz` would mean
instancing, which is not this phase.

The class is therefore an abstraction deliberately held one step short of the axis: a resolver that
eventually maps `Small → opsz 14` and `LargeDisplay → opsz 32` can replace this one without a single
caller changing, because callers ask for the class. `typography.vocabulary.optical_weight` and
`typography.roles.style_record` pin the vocabulary and the record; the policy is Phase 14's.

---

## 5. Weights, and what "no synthesis" means here

Four weights in the vocabulary: `RoboTypographyWeightRegular` (400), `Medium` (500), `SemiBold` (600),
`Bold` (700).

`RoboTypographyResolveWeight` asks the pack for a role at an explicit weight. The weight is first snapped
to the vocabulary by `RoboTypographyNearestWeight`, whose tie rule is stated rather than left to a
comparison's luck — a distance tie returns the **lighter** weight. The style records the weight that was
*actually selected*, so a step-down is visible to the caller rather than inferred, and
`RoboTypographyResolverStats.weightStepDowns` counts the cases.

**Nothing is synthesized.** There is no emboldening transform, no faux italic, no smeared outline. If the
packaged family cannot select a weight, the answer is the nearest one it has.

One consequence is worth stating plainly, because a reader looking at the gallery will notice it: **the
shipped family is Inter Variable, and the four weights now resolve to four distinct `weight` values on
*one* `FontFace` object.** `typography.resolve.weights` proves the weights resolve and differ; the pixels
do not differ yet, because applying the `wght` axis to an instance is the same not-this-phase problem as
`opsz`. The document gallery's Regular/Medium/Semibold/Bold lines exist in the picture so that the change
is visible in one place on the day the axis *is* applied.

---

## 6. Fallback

Two fallbacks, and they are different things:

**Weight step-down**, above. A *face choice* within the vocabulary, deterministic, never synthesized.

**Coverage fallback.** Text the system face cannot draw is a *coverage* question, and it is answered by
RoboFont's own fallback chain through `RoboTypographyResolveGlyphs`, which returns the primary selection
*and* the fallback decision via `FontFallbackResult`. A resolver created without a fallback resolver
reports `RoboNotSupported` rather than pretending the primary covers everything.

**A role with no family is not an error.** A role whose family cannot be selected resolves to `RoboOk`
with a face-less style (`face == NULL`, size/weight/leading still filled from the scale). The shell then
draws that label with `DesktopConfig.face`, which is exactly the pre-12.3 behaviour — so a pack that
declares only some roles produces a desktop that is *worse*, not one that is dead, and
`desktop.typography.missing_family_fallback` asserts it with a pack that has no display family at all.
`RoboNotFound` is reserved for a role with no scale entry, and `RoboInvalidArgument` for a bad argument.

---

## 7. Desktop integration

`DesktopConfig.typography` is a **borrowed** resolver; `DesktopCreate` may also be given a `face` and
will use it for every role the resolver cannot name. A desktop with neither still renders — a textless
shell is a supported state, not a crash — and reports it through `labelsUnshaped`.

`DesktopChromeTextStyle` is the single door from a roll to a `DesktopTextStyle`, and
`RoboDesktopChrome.c` resolves each surface's role **once per frame** rather than once per label:

| Surface | Role |
|---|---|
| The panel's identity | `DesktopTitle` |
| The applications label | `ChromeLabel` |
| The status label | `ChromeStatus` |
| A window's title band | `WindowTitle` |

Resolving once per frame is what lets the *measurement* of a label and the *drawing* of it provably read
one style: `DesktopChromeLabel` narrows the label's area with a width measured from the same style it
then draws with. A label resolved from a role is placed **typographically** — the role's line box is
centred in the band and then dropped to the baseline by the face's own ascent
(`RoboTypographyStyleApplyFontMetrics` → `ascentPx`) — rather than by the bounding-box rule the pre-12.3
path used. That is the phase's baseline-alignment requirement, and it is one rule shared by every band.

---

## 8. Document integration

`DesktopDocumentConfig.typography` does two things, and both are in `RoboDesktopDocument.c`:

**The face.** The document resolves `DocumentBody` once and uses *that* face for measurement and paint.
`DesktopDocumentFace` reports it, and `DesktopWindowSetContent` is handed the same object — so "measured
with X, painted through Y" is structurally impossible here rather than merely avoided. When no resolver
is configured, `config.face` is used, and when neither is set the document is textless and still a
document.

**The defaults.** `RoboTypographyResolverDocumentStyleSheet` emits the document roles as CSS, and the
bridge adds it at **`RoboCssOriginRuntime`** — *below* the author's sheet in the cascade. So a document
that declares its own `h1` wins, and a document that declares nothing starts from the system's table
rather than from the layout engine's initial 16 px.

The element vocabulary is deliberately small and is not a CSS font engine:

| Element | Role |
|---|---|
| `body` | `DocumentBody` |
| `h1` | `DocumentHeading1` |
| `h2` | `DocumentHeading2` |
| `h3`, `h4`, `h5`, `h6` | `DocumentHeading3` |
| `small`, `caption`, `figcaption` | `DocumentCaption` |
| `code`, `pre`, `kbd`, `samp`, `tt` | `DocumentCode` |
| everything else | inherits the body's setting (`RoboNotFound`) |

`h4`–`h6` taking heading 3 is a decision rather than an omission: the vocabulary has three heading roles,
and a fourth size invented here would be a type setting that exists nowhere else in the system. Elements
with no default of their own get **no rule at all**, so the generated sheet declares only where the
default differs from the body — which is why `p`, `li` and `td` are absent from it rather than repeating
16 px.

What the sheet deliberately does **not** carry: a `font-family` declaration. Face selection is the
pack's resolution, and CSS font matching, `@font-face`, `font-stretch` matching and
`font-feature-settings` are later phases. `RoboFontSystemPackApplyGenericConfig` is the pack's bridge for
the day `system-ui` needs one.

`line-height` *is* emitted. The desktop consumes the role's leading today (it places a baseline from it);
`RoboLayout` does not yet resolve `line-height` as a used value, so the declaration is this phase's half
of a contract a layout phase will honour.

---

## 9. Caching

Resolution is cached **per role**, keyed by the (weight, style) the role was last resolved at. A second
resolve of one role at the same weight is a lookup rather than a scan, which is what keeps a shell
painting many labels from re-running selection for each of them. The key includes the weight because an
override that returned the cached setting would be a *wrong* answer rather than a slow one.

The cache is bounded by construction — one entry per defined role — so there is no eviction policy to
get wrong, and no font data is duplicated by it: it holds faces the registry already owns.
`RoboTypographyResolverStats` reports hits, misses and inserts, and
`RoboTypographyResolverCacheClear` drops every entry without touching the statistics.

The cache is *per resolver*, not global, so a leaked resolver cannot poison a later one, and
`desktop.frame.determinism` plus `GalleryDeterministic` in the gallery suite pin the property that
matters to a picture: two resolves and two renders of one state produce one frame.

---

## 10. Building and testing

```powershell
Scripts/Build/BuildTypography.ps1            # the module, its boundary scan and its suites
Scripts/Test/TestTypography.ps1 -Target Host # typography.roles.*, typography.resolve.*, typography.document.*
Scripts/Test/TestDesktop.ps1 -Target Host    # desktop.typography.*, the binding as pixels
Scripts/Test/TestFont.ps1 -Target Host       # the pack the roles resolve against
Scripts/Test/TestGraphics.ps1 -Target Host   # the 12.1 raster the bound faces land on
Scripts/Test/TestPaint.ps1 -Target Host
Scripts/Test/TestDesktop.ps1 -Target Qemu    # the same shell in the image
```

A typography change does not rebuild unrelated subsystems: `BuildTypography.ps1` compiles
`RoboBase`, `RoboMem`, `RoboContainers`, `RoboUnicode`, `RoboI18N`, `RoboFont` and `RoboTypography`
and nothing above them.

### Suites

| Suite | Cases | What it pins |
|---|---|---|
| `Tests/Typography/SuiteRoboTypographyRoles.c` | `typography.roles.*`, `typography.vocabulary.*`, `typography.scale.*`, `typography.conformance.*` | the role vocabulary, the optical classes, the scale's validity and ordering, the CSS emitter's determinism |
| `Tests/Typography/SuiteRoboTypographyResolver.c` | `typography.resolve.*` | every role resolves against the **shipped** pack, weights select, cache behaviour, coverage fallback, missing family |
| `Tests/Typography/SuiteRoboTypographyDocument.c` | `typography.document.*` | the element vocabulary, the generated sheet, a caller's scale |
| `Tests/Desktop/SuiteRoboDesktopTypography.c` | `desktop.typography.*` | the shell speaks the roles; a document gets the system's defaults; measurement and paint share one face |
| `Tests/Desktop/SuiteRoboDesktopTypographyGallery.c` | `desktop.typography.gallery.*` | determinism, that the *scale* is the authority for the picture, and the type ladder's height |

The typography suites open the pack through a host IO adapter and the same discovery, registry, loader,
selection and pack code the guest runs. They do not build a face by hand, because a suite that tested a
substitute could pass while the shipped pack was broken.

### Visual evidence

The gallery suite writes three PNGs on every host run:

```text
Build/Typography-Desktop.png    the panel: DesktopTitle, ChromeLabel, ChromeStatus
Build/Typography-Chrome.png     three window title bands (WindowTitle) and the panel
Build/Typography-Document.png   the specimen: h1/h2/h3/body/caption/code, the alphabet, digits,
                                punctuation and the four weight names
```

They sit beside the 12.1 raster fixture's `Build/Typography-Coverage.png` and
`Build/Typography-Stroke.png`. A PNG is evidence for a person; the *verdict* comes from the assertions
beside it, because a suite whose verdict depended on the filesystem would fail on a read-only tree while
the pixels it wrote were perfectly good.

---

## 11. Current limitations

- **Weights and optical size bind but do not instance.** The `wght` and `opsz` axes are declared,
  preserved and resolved; no instance is created, so the four weights render alike today. Applying an
  axis is RoboText's problem (12.4+), where a shaped run has somewhere to carry it.
- **No run carries its own weight.** A document paints through **one** face (the body role's), so a
  `font-weight: 700` in an author sheet reaches layout and paint as a number and not as a bolder face.
  Per-element face selection needs the shaped-run model.
- **Chrome roles exist without chrome.** `ChromeTab`, `ChromeAddress`, `ChromeMenu` and `ChromeMenuItem`
  resolve and are set, but nothing draws a tab strip, an address bar or a menu bar yet.
- **No `font-family`, no `@font-face`, no matching.** The generated sheet sets type; it does not select a
  family from CSS.
- **No tracking.** `letter-spacing` has nowhere to be applied by the current run walk, so it is absent
  from the style record rather than carried and ignored.
- **`line-height` is declared, not resolved.** The document's generated sheet emits it and the desktop
  reads the role's leading; `RoboLayout` does not yet honour it as a used value.
- **No shaping, no bidi, no complex scripts.** Unchanged from 12.2 — this phase binds faces, it does not
  arrange glyphs.

---

## 12. What comes next

**12.4 — RoboText architecture** is the goal this one exists to make cheap. It introduces the shaped
run: a sequence of glyph ids, their advances and offsets, produced by shaping and consumed by the
painter. The seam is already cut — the rasterizer takes a `GlyphId` and a baseline origin, and the
resolver hands out a face plus its metrics — so 12.4 replaces the *chooser* of glyphs and adds the run
that carries them. When it does, the two things this phase could not finish become natural: a run carries
its own weight and optical size, so an author's `font-weight: 700` selects a face variant rather than a
number, and the four gallery weight lines finally differ.
