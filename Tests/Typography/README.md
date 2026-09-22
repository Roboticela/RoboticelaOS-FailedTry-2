# RoboTypography Tests

The typography binding (Phase 12.3): a semantic role in, a loaded system face and its numbers out.
Contract: [`../../Documentation/Runtime/Typography/TypeBinding.md`](../../Documentation/Runtime/Typography/TypeBinding.md).

## Suites

| Suite | Filter prefix | What it is about |
|---|---|---|
| `SuiteRoboTypographyRoles.c` | `typography.roles.*`, `typography.vocabulary.*`, `typography.scale.*`, `typography.conformance.*` | the role vocabulary and its groups, the optical-size classes, the weight vocabulary and the nearest-weight tie rule, the scale table's own validity and ordering, the element → role vocabulary, and the generated document sheet |
| `SuiteRoboTypographyResolver.c` | `typography.resolve.*` | role → face over the **shipped** pack: every role resolves, weights select and step down, the per-role cache, coverage fallback, a role whose family is missing, and the document sheet through the resolver |
| `SuiteRoboTypographyDocument.c` | `typography.document.*` | the document defaults: the element vocabulary, a caller's own scale, and the emitted CSS |

The shell's half of the binding lives with the shell: `Tests/Desktop/SuiteRoboDesktopTypography.c`
(`desktop.typography.*`) and `Tests/Desktop/SuiteRoboDesktopTypographyGallery.c`
(`desktop.typography.gallery.*`, which writes `Build/Typography-*.png`).

## The fixture

`RoboTypographyTestSupport.h` is the one fixture the three suites share, and it is **not** a framework —
the runtime has exactly one of those (`Tests/Runtime/Framework/RoboTest.h`).

- `TypographyFixtureOpen(root)` opens the **shipped** `System/Fonts/` pack through a host IO adapter and
  builds the resolver over it. Every case that needs a face uses this rather than building one by hand,
  because a suite that tested a substitute could pass while the real pack was broken. `root == NULL`
  means the repository's own `System/Fonts`.
- `TypographyVirtualPack(manifest)` is for the states the shipped pack cannot express — a manifest that
  omits a role, say — with the **real** face planted beside it, so the case exercises a genuine parseable
  font rather than a stub.
- Every helper asserts rather than returning a status, and every assertion message names the step that
  failed: an unopenable shipped pack is this phase's own failure and a reader should not have to work out
  from four checks in a row which one gave.

## How to run

```powershell
Scripts\Test\TestTypography.ps1 -Target Host
Scripts\Test\TestDesktop.ps1 -Target Host      # the binding as pixels, plus the galleries
Scripts\Build\BuildTypography.ps1              # the module, its boundary scan and its suites
```

There is no QEMU suite here: the module has no guest-only behaviour, and the guest-visible check is the
shell's own (`Scripts\Test\TestDesktop.ps1 -Target Qemu`, which boots the image and reads the guest's
`ROBO_QEMU_TYPOGRAPHY_*` markers).

## Adding a regression

1. Decide which layer owns it. A role's *meaning* (which pack role, which weight, which size) belongs in
   the scale and a `typography.scale.*` / `typography.roles.*` case; a role's *resolution* belongs in the
   resolver; a *binding* — a surface that stopped reading the scale — belongs in `Tests/Desktop`.
2. Prefer the shipped pack. If the failure needs a manifest the repository does not ship, reach for
   `TypographyVirtualPack` rather than adding a second system font root.
3. If the case is about a picture, add it to the gallery suite and assert something beside the PNG: two
   renders being one frame, or the scale being the authority (resize a role and the checksum must change).
4. Never assert a font size, weight or leading literal. Read the role's entry from the scale, so a
   restyle changes the number in one place.
