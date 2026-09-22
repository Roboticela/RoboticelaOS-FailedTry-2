# System/Fonts — the RoboticelaOS system typeface pack

This directory is the **single source of truth** for the faces RoboticelaOS ships.
Nothing else in the tree vendors a system font, and no build step reaches into a
host font directory (`C:\Windows\Fonts`, `/usr/share/fonts`, `/Library/Fonts`, …):
the guest image must be reproducible from this directory alone.

```text
System/Fonts/
├── system-fonts.manifest      the pack manifest (roles, weights, axes, licences)
├── UI/
│   └── Inter.ttf              the packaged face (Inter Variable, OFL 1.1)
└── licenses/
    └── Inter-OFL.txt          the licence text shipped with the face
```

The image build packs this directory under the runtime root **`/System/Fonts/`**
(`scripts/roboboot-build-common.ps1`) and `RoboFontSystemPack` discovers, loads
and selects from there.

## Rules

* One authoritative root. Do not copy faces elsewhere in the tree.
* Every redistributed face needs its licence text under `licenses/` and an
  entry in `system-fonts.manifest`.
* Do not rename a third-party face to imply it is another (never label Inter as
  "SF Pro" or similar). Expose Roboticela semantic roles (`display`, `text`,
  `monospace`) instead.
* A build must fail loudly when a face the manifest names is missing. It must not
  silently ship without its system font.

See [`Documentation/Development/SystemTypefacePack.md`](../../Documentation/Development/SystemTypefacePack.md)
for the full contract, discovery order, fallback and cache behaviour.
