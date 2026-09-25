# Desktop UI

The OS desktop **window document** is authored in `Desktop/` as a Vite + React + TypeScript app with Tailwind.

## Two surfaces

| Surface | What it is |
|---|---|
| **Vite lab** (`npm run dev`) | Interactive browser preview. Routes under `/` — click, type, drag, open menus. Tailwind + TypeScript. |
| **Guest export** (`npm run build` → `DesktopPage.h`) | Static HTML + RoboCSS-compatible sheet. No React runtime in the guest until Phase 16. |

The guest still cannot run handlers or Tailwind. The lab is how you test UX without booting QEMU every time; rebuild the image when you want the same chrome in the OS.

## Routes

| Path | Purpose |
|---|---|
| `/` | Index — links to every lab surface |
| `/desktop` | Full interactive desk |
| `/window` | Single window chrome |
| `/controls` | Fields, buttons, toggles, slider |
| `/dock` | Dock tiles and launch |
| `/menubar` | System bar + application menu |
| `/input-lab` | Keyboard / mouse telemetry |
| `/combined` | Full desk without the lab chrome frame |

## Build

`Scripts/Build/BuildDesktopUi.ps1` runs `npm run build`, which:

1. Typechecks with `tsc`
2. Builds the Vite SPA into `Build/Desktop/Ui/`
3. SSR-renders `GuestDesktopShell` and writes `Runtime/Source/RoboDesktop/Generated/DesktopPage.h`

Guest CSS must stay inside properties RoboCSS registers (`theme/stylesheet.ts`). The interactive lab uses Tailwind freely.
