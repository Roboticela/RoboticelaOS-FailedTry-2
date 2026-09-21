# QEMU Display Backend (Goal 7.12)

## Mechanism used by this repository

QEMU is launched with **`-vga std`** (see `scripts/roboboot-qemu-common.ps1`).
That exposes a classic VGA device; firmware/kernel use **UEFI GOP** to obtain a
linear framebuffer. There is no virtio-gpu path in the current image.

Framebuffer metadata reaches userspace through the existing kernel display
syscall (`SYS_DISPLAY` / `robosys/display.h`):

| Op | Role |
|---|---|
| `RoboDispClaim` | Own output (+ input) role |
| `RoboDispInfo` | width, height, pitch (bytes), bpp, pixfmt, ready |
| `RoboDispPresent` | Copy userspace pixels into the GOP framebuffer |
| `RoboDispInput` / `INJECT` | Keyboard/mouse events (HID codes) |
| `RoboDispRelease` | Drop claim |

Pitch is **bytes per scanline**. Present `stride` is **pixels** (32 bpp).

## Runtime architecture

```
RoboGraphics surface
        ↓
RoboWindow present
        ↓
RoboWindow QEMU backend (platform/qemu)
        ↓
Guest: SYS_DISPLAY PRESENT  |  Host sim: QemuFbPresentSurface
        ↓
RoboDisplay QEMU backend (enumeration / scale=1)
        ↓
QEMU-visible GOP framebuffer
```

QEMU is a **backend**, not a second window/input stack.

## Pixel formats

BootInfo / SYS_DISPLAY pixfmt values (duplicated as `ROBO_QEMU_PIX_*`):

- `0` — BGRX8888 (`RoboGfxFormatBgra8` preferred)
- `1` — RGBX8888 (`RoboGfxFormatRgba8` preferred)

Only 32 bpp is supported. Guest present copies 32-bit pixels as-is; create
surfaces in the FB-native format (robo-runtime does this from `INFO`).

## Resolution and DPI

- Resolution comes from `RoboDispInfo` (typically the GOP mode chosen by
  firmware; commonly 1280×800 in this project’s boot path, but **not hard-coded**).
- Dynamic mode switching is **not** exposed; window resize requests are accepted
  but the actual FB size stays fixed.
- Physical DPI is unavailable → `scale = 1.0`, `dpi_x/dpi_y = 0` (Goal 7.8
  fallback: logical == physical pixels).

## Input

`RoboDispInput` events are translated in the QEMU window backend into
`WindowObserveKey` / `WindowObserveMouse` (existing Phase 7.10/7.11
APIs). No QEMU-specific key/mouse enums are exposed publicly.

## Host simulator

`QemuSimInit` / `WindowBackendUseQemuSim` allocate an in-memory
framebuffer for unit tests without QEMU or syscalls. Host production builds do
**not** set `RoboWindowHasQemu`.

## Smoke test

```powershell
scripts\robo test-qemu-display
```

Waits for serial markers from `/System/runtime/robo-runtime`:

- `ROBO_QEMU_DISPLAY_OK`
- `ROBO_QEMU_PRESENT_OK`
- `ROBO_QEMU_INPUT_OK`
- `ROBO_QEMU_DISPLAY_SHUTDOWN_OK`

Interactive visual check: `scripts\robo run-runtime-qemu` (GTK display), or
`scripts\robo run-qemu` to build everything first.

## Limitations

- Fixed GOP resolution for the life of the claim.
- No refresh-rate reporting.
- No damage-region present API beyond full-surface present.
- Guest color conversion is the caller’s responsibility (match FB pixfmt).
- Headless claim (`ready=0`) presents as a no-op success.
