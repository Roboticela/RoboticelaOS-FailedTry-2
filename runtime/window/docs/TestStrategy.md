# Phase 7 Test Strategy (Goal 7.15)

Phase 7 verification covers RoboWindow + RoboDisplay + RoboGraphics presentation
and input. Tests reuse the existing **RoboTest** host runner — no second framework.

## Layout

| Area | Filter | Suite / script |
|---|---|---|
| Core window | `window.` (excl. p7) | `SuiteRoboWindow*.c` (Goals 7.1–7.14) |
| Display | `display.` | `SuiteRoboDisplay*.c` |
| Graphics foundation | `graphics.` | `graphics/tests/*` (Phase 6) |
| **Phase 7 acceptance** | `window.p7.` | `SuiteRoboWindowPhase7.c` |
| Host Win32 | `window.host.` / `display.host.` / `host.` | Host suites |
| QEMU sim (host unit) | `window.qemu.` / `window.p7.qemu_sim` | Sim FB + input |
| Real QEMU smoke | `robo test-qemu-display` | `scripts/pipeline/commands.test.ps1` |
| Host smoke | `robo test-display` | `scripts/pipeline/commands.test.ps1` |
| Full Phase 7 gate | `robo test-all` | `scripts/pipeline/commands.test.ps1` |

## How to run

```powershell
# Unit / integration (mock + null + qemu_sim + host when available)
.\runtime\Tools\Build.ps1 -Configuration Debug -Verify -Test -Filter "window.p7."

# Broader Phase 7 regression
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "window."
.\runtime\Tools\Build.ps1 -Configuration Debug -Test -Filter "display."

# Host + optional visible smoke
scripts\robo test-display
scripts\robo test-display --visible

# Real QEMU (skipped automatically if tooling missing — run when available)
scripts\robo test-qemu-display

# One-shot gate (unit + host; QEMU optional)
scripts\robo test-all
scripts\robo test-all --skip-qemu
```

Normal development builds do **not** require QEMU.

## Phase 7.15 acceptance (`window.p7.*`)

| Test | Covers |
|---|---|
| `version` | RoboWindow **0.15.0** |
| `lifecycle` | create/draw/present/resize/close/destroy cycles |
| `graphics` | RoboGraphics pattern + screenshot pixel checks |
| `scale` | logical vs physical (scale 2.0) |
| `resize` | before/after frame, dirty, surface replace |
| `stale` | purge pending events on destroy; no delivery to new window |
| `input` | keyboard + mouse through observe → queue |
| `multi` | three independent windows/surfaces |
| `failures` | create/present failure; shutdown with live windows |
| `shutdown` | idle / mid-frame / after input / after resize |
| `null_contract` | null backend create + frame |
| `qemu_sim` | shared visual fixture on QEMU sim backend |
| `loop_frame` | process_events + RESIZED + frame recreate |
| `perf` | light present/resize loop bound |
| `host` | real Win32 when `robo_window_backend_host_available()` |

## Pixel / visual validation

Deterministic fixture: background, border, four corner markers, rectangle,
line, alpha region, path triangle, center marker. Exact RGB checks on mock /
qemu_sim / host surfaces (tolerance 0–2 on background only).

Screenshot capture uses Phase 6.14 `robo_gfx_screenshot_*`. This is **not**
the future Phase 39 visual-regression system (no golden PNG corpus / gallery).

## Stale-window safety

On `robo_window_destroy`, pending queue events for that id are **purged**, then
`CLOSED` is emitted. `observe_key` / `observe_mouse` reject non-live ids.
New windows receive new ids — they never inherit purged events.

## Memory / sanitizers

Debug host builds enable `ROBO_MEM_DEBUG` via `Build.ps1 -Configuration Debug`.
Child double-free / overflow probes remain in the standard RoboTest harness.
No separate ASan CI gate is required for Goal 7.15; run Debug tests locally.

## Limitations

- Host DPI cannot be forced to arbitrary values in CI; scale **2.0** is covered
  via `config.scale_preference` + unit conversion tests (`display.scale.*`).
- Real QEMU needs the bootloader/QEMU toolchain; use `-SkipQemu` when absent.
- `window.p7.host` no-ops (pass) when the host backend is unavailable.
- Single-threaded render/event model — no compositor / VSync tests (Phase 15).

## Chromium

Phase 7 tests must not include Chromium headers or link Chromium. Verify scans
in `Build.ps1 -Verify` cover RoboWindow / RoboDisplay / RoboGraphics.
