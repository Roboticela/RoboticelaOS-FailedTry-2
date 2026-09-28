# Desktop UI

The OS desktop **window documents** are authored in `Desktop/` as a Vite + React + TypeScript app.

## Two surfaces

| Surface | What it is |
|---|---|
| **Vite lab** (`npm run dev`) | Interactive browser preview. Routes under `/` — click, type, drag, open menus. Tailwind + TypeScript. |
| **Guest export** (`npm run build` → `DesktopPage.h`) | Static HTML + RoboCSS sheet + page script. SSR from `GuestOverviewApp` / `GuestInputLabApp`; RoboJS evaluates `kDesktopPageScript` in the guest. |

Native chrome (panel + window frames) owns the desk. Exported markup is **content only** — no nested wallpaper, menu bar, or dock inside the document. The scene opens one native window per seeded app (System Overview + Input Lab).

## Routes (Vite lab)

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
3. SSR-renders the guest apps and writes `Runtime/Source/RoboDesktop/Generated/DesktopPage.h`

Guest CSS may use `display: flex` and flex longhands (layout 19.3). Silent no-ops (`border-radius`, `box-shadow`, `transform`, `transition`, `gap`) stay banned by the export validator until paint/materials support them.
