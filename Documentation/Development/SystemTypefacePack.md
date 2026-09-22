# System Typeface Pack — Phase 12.2

The first real RoboticelaOS system typography package: one authoritative place
for the faces the OS ships, a deterministic manifest describing them, and a
semantic way for the runtime to ask for a face *by role* rather than by file.

Phase 12.1 gave the runtime a coverage-antialiased rasterizer. Phase 12.2
decides **which faces exist and where they live**. It does not touch the
rasterizer and it does not shape text — shaping is 12.4+.

## The face

| | |
|---|---|
| Family | **Inter** |
| File | `System/Fonts/UI/Inter.ttf` — installed at `/System/Fonts/UI/Inter.ttf` |
| Kind | variable font (`fvar`): `wght` 100–400–900, `opsz` 14–14–32 |
| Licence | SIL Open Font License 1.1 (`System/Fonts/licenses/Inter-OFL.txt`) |
| Source | The Inter Project Authors — <https://github.com/rsms/inter> |

The design target is **SF-class metrics, proportions and spacing**. Inter is an
independent, legally redistributable typeface. It is **never** presented as
Apple's SF, it is never renamed, and no proprietary Apple font file is copied or
implied.

Multiple weights are real rather than faked: Inter carries a `wght` axis, and
the pack exposes it. RoboFont parses `fvar` and the selector can satisfy a
weight request from the axis. Nothing synthesizes a bold — there is no
emboldening transform in this phase.

## Layout

```text
System/Fonts/                     (installed at /System/Fonts/)
├── system-fonts.manifest         deterministic role/weight/axis/licence description
├── UI/
│   └── Inter.ttf                 the packaged face
├── licenses/
│   └── Inter-OFL.txt             the licence text shipped with the face
└── metadata/                     reserved for future per-family sidecars
```

`System/Fonts/` in this repository is the **single source of truth**. No build
step reaches into a host font directory (`C:\Windows\Fonts`, `/usr/share/fonts`,
`/Library/Fonts`, …), and no second copy of the face lives in the runtime tree.

## Manifest

`system-fonts.manifest` is a small line-based text file — not a new
serialization format, and deliberately not binary, because it is a *description*
of installed data and should be readable next to the face.

```text
system-font-pack 1
root /System/Fonts

family Inter
  file UI/Inter.ttf
  role text weight 400
  role display weight 600
  role monospace weight 400
  weight 400
  stretch 100
  style normal
  variable
  axis wght 100 400 900
  axis opsz 14 14 32
  license OFL-1.1
  source The Inter Project Authors (https://github.com/rsms/inter)
  version 4.000
end
```

A `role` line is the semantic binding. Both `display` and `text` point at the
same physical family here — the two roles are a **semantic distinction, not two
font files** — and the weight on each is what the role resolves to when a caller
does not ask for a specific one.

The parser (`FontSystemManifestParse`) is pure: no IO, no paths. A malformed
directive, a duplicate family, an axis whose default sits outside its range, a
family with no `file`, an unterminated `family`, or an empty manifest is a
`RoboParseError`.

## Roles

```c
RoboFontSystemRoleUIDisplay   prominent titles and large labels   → Inter @ 600
RoboFontSystemRoleUIText      menus, controls, document body      → Inter @ 400
RoboFontSystemRoleMonospace   code, paths, tabular data            → Inter @ 400
```

`RoboFontSystemPackResolveRole(pack, role, weight, stretch, style, &selection)`
resolves a role through the *existing* Phase 5 selector. A caller names a role;
a filename is an installation detail it never sees.

When the bound family is variable and exposes a `wght` axis, an explicit weight
request is carried as a `FontAxisSetting` so the selector can prefer the
variable face over its nearest static neighbour. A weight outside the axis range
is clamped by the selector's normal distance rules — never by a synthetic
transform.

## Discovery, loading and cache

```text
/System/Fonts/system-fonts.manifest
      ↓ read (FontDiscoveryIo)  → parse
FontRegistry (5.12)             register + scan each declared file
      ↓
FontLoader (5.13)               load each registration → the existing font cache
      ↓
FontSelection (5.11)            role + weight + stretch + style → one face
```

The pack adds **no** new discovery, loading or selection code — it binds the
existing pipeline to one root. Discovery ordering is deterministic (manifest
order, then file order within a family). Discovery does not require a
host-installed font: the guest image is self-contained.

The cache is the Phase 5 loader cache, keyed on registration id / source /
face index, so a changed file is a changed registration and stale entries are
not possible across a refresh. `FontSystemPackLoadFailures` reports how many
registrations the loader refused while warming.

A missing manifest, a missing face file, or a family whose registered faces do
not resolve to the declared family name is a **hard error**. A pack never
degrades to a partial install: that is what makes "the image shipped its system
font" checkable.

## Fallback

`FontSystemPackResolveCodepoints` resolves a role, then runs the existing Phase
5 coverage-first fallback with the pack's family as the preferred family. It
only **chooses** a face — no shaping, no script runs. Complex-script shaping is
a later phase.

## Generic families (early CSS bridge)

`FontSystemPackApplyGenericConfig` points `system-ui`, `ui-sans-serif` and
`sans-serif` at the pack's UI Text family, and `ui-monospace` / `monospace` at
the packaged monospace family when one exists. That is the smallest CSS-facing
bridge this phase owns. Full CSS font matching, `font-feature-settings`,
`font-variation-settings` and `font-stretch` matching are Phase 18.

## Build

| Step | Where |
|---|---|
| Validate the pack, build the module | `Scripts/Build/BuildFont.ps1` |
| Test the whole pack path | `Scripts/Test/TestFont.ps1 -Target Host` (`font.systempack.*`) |
| Install into the image | `scripts/roboboot-build-common.ps1` packs `System/Fonts/` under `/System/Fonts/` |

The image build packs the face, its manifest and its licence, and **throws** if
any of them is missing. A release image can never ship without its system
typeface. Only the font pack changes when only the pack changes — the module
build is incremental.

## Tests

| Case | What it pins |
|---|---|
| `font.systempack.manifest_parse` | version, root, family, file, roles + weights, axes, licence, and that a bare parse invents no bound fields |
| `font.systempack.manifest_reject` | unknown directive, missing `end`, missing `file`, duplicate family, bad weight, bad axis order, empty/absent text, and the fuzz entry |
| `font.systempack.shipped_open` | the **shipped** pack opens; family `Inter`, one registered face, `wght` axis confirmed against the loaded font (100/400/900), licence `OFL-1.1`, cache warmed with zero load failures |
| `font.systempack.resolve_role` | text → 400, display → 600, an explicit 700 carried via the axis, and the family gate |
| `font.systempack.fallback_generic` | `'A'` resolves to the primary; `system-ui` resolves to the packed family |
| `font.systempack.missing` | a root with no manifest and a manifest naming an absent file are both hard `RoboNotFound`, leaving no registrations behind |

The suite opens the **real** `System/Fonts/` through a minimal host IO adapter,
so the same discovery/registry/loader/selection code the guest runs is what is
exercised — the host is a test target, not a second font implementation.

## QEMU validation

`Scripts/Test/TestDesktop.ps1 -Target Qemu` boots the image and checks that the
desktop's text reaches the picture (`ROBO_QEMU_DESKTOP_UNSHAPED` is 0,
`ROBO_QEMU_FACE_OK` is emitted) using the face installed at
`/System/Fonts/UI/Inter.ttf`. No host font is installed into the VM.

## Current limitations

* **One physical family.** UI Display and UI Text are two *roles* over one face;
  they are not two files. A distinct display optical size is a later decision.
* **No optical-size policy.** The `opsz` axis is preserved in the manifest and in
  RoboFont's model but is not driven automatically; selecting it is Phase 14
  desktop typography work.
* **No desktop role binding yet.** The shell loads the pack's face; binding its
  chrome and document text to the *roles* by name is **Phase 12.3** (the runtime
  plan's "Desktop + document face binding").
* **No shaping.** No kerning, ligatures, GSUB/GPOS, bidi or script runs. Phase 12.4+.
* **No mask cache.** Unchanged from 12.1.

## Attribution and licensing

See [`Documentation/Legal/FontLicenses.md`](../Legal/FontLicenses.md). The OFL
text ships beside the face at `System/Fonts/licenses/Inter-OFL.txt`; the licence
is not modified and attribution is not removed.
