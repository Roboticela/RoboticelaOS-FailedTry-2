# Building, testing, running and deploying RoboticelaOS

Everything goes through one command:

```
scripts/robo <command> [options]
```

There is no second build system. `build.ps1`, the `Makefile`s and
`bootloader/Makefile` are thin forwarders so existing habits keep working, and
they contain no build logic of their own.

---

## 1. Prerequisites

RoboticelaOS targets the **x86_64 / UEFI** platform and is developed on a
**Windows host** with PowerShell (5.1 or 7+). The pipeline says so explicitly
rather than half-working elsewhere.

| Requirement | Why | Override |
|---|---|---|
| LLVM 14+ (`clang`, `lld-link`, `ld.lld`, `llvm-ar`) | host preview, UEFI app, kernel, libc, userland, runtime | `ROBO_LLVM_BIN`, `ROBO_CLANG`, `ROBO_LD_LLD`, `ROBO_LLD_LINK`, `ROBO_LLVM_AR` |
| Python 3.8+ | asset baking, ESP/disk image, RoboFS tool, font and i18n generators | `ROBO_PYTHON` |
| Pillow, cairosvg | `bake_assets.py` (raster assets, `logo.svg`) | — |
| QEMU x86_64 + OVMF (`edk2-x86_64-code.fd`, `edk2-i386-vars.fd`) | `run-*` and `test-qemu-*` only | `ROBO_QEMU`, `ROBO_OVMF_CODE`, `ROBO_OVMF_VARS` |
| Git | revision stamping in the deployment manifest (optional) | — |

QEMU is **optional**: without it, `build-all` and the host test surfaces still
work, and the QEMU stages are reported as `SKIP`, never as passing.

Check the environment before anything else:

```
scripts/robo check
```

`check` prints `FOUND` / `MISSING` / `WRONG VERSION` / `OPTIONAL` for each
dependency, verifies every source tree the pipeline claims to build, verifies
the QEMU/UEFI firmware, and confirms the runtime is still Chromium-independent.
It exits non-zero if anything required is missing.

---

## 2. The commands

Run `scripts/robo help` for the live list.

### Inspect and maintain

| Command | Does |
|---|---|
| `check` | environment validation (see above) |
| `config` | print the resolved configuration (paths, QEMU, timeouts) |
| `version` | pipeline, runtime and bootloader versions |
| `clean` | remove all generated output; `--baked` also clears baked assets/fonts |
| `rebuild` | `clean` followed by a full `build-all` |

### Build

| Command | Produces |
|---|---|
| `build-all` | the whole system, then publishes the deployment artifacts |
| `build-assets` | splash images and bootloader fonts (`.rbim`, `.rbfn`) |
| `build-bootloader` | host preview executable + UEFI `BOOTX64.EFI` |
| `build-userland` | libc, user programs, guest runtime ELFs |
| `build-kernel` | `kernel.elf` (compiles libc and userland, packs `initrd`) |
| `build-runtime` | RoboticelaOS Runtime modules + host test binary |
| `build-image` | UEFI app, ESP, and the bootable GPT disk image |

`build-all` runs in dependency order and refuses to report success unless each
artifact exists and is non-empty:

```
assets ─┐
        ├─> bootloader EFI ─┐
runtime ┘                   ├─> kernel.elf ─> ESP/GPT image ─> deploy tree ─> manifest
libc ───> userland ─────────┘
```

### Test

| Command | Covers |
|---|---|
| `test-all` | every surface below (QEMU auto-skipped when unavailable) |
| `test-runtime` | the whole RoboticelaOS Runtime host suite |
| `test-bootloader` | bootloader unit + session tests |
| `test-kernel` | every kernel host unit test binary + libc host tests |
| `test-base` `test-memory` `test-containers` `test-unicode` `test-i18n` `test-font` `test-script` `test-graphics` `test-window` `test-display` `test-html` `test-demo` `test-infra` | one runtime subsystem each |
| `test-qemu-boot` | bootloader selftest under real UEFI in QEMU |
| `test-qemu-kernel` | kernel + OS core selftests under real UEFI in QEMU |
| `test-qemu-userspace` | the guest user-space selftests (window server, compositor, libc) |
| `test-qemu-display` | the visual runtime rendering through the QEMU display backend |
| `test-e2e` | clean build → boot → selftests → visual runtime → deployment validation |

### Run

| Command | Runs |
|---|---|
| `run-bootloader` | bootloader host preview window (no emulation) |
| `run-kernel` | the real kernel in QEMU |
| `run-runtime` | the visual runtime as a native host window |
| `run-runtime-qemu` | the OS in QEMU, booting to the visual runtime |
| `run-qemu` | full build, then boot the OS in QEMU |

Add `--headless` to any QEMU command to run without a window; it then verifies
by waiting for the markers the OS prints on the serial port.

### Deploy

| Command | Does |
|---|---|
| `deploy` | builds, then produces the USB tree, disk image and USB archive |
| `validate-deployment` | structurally validates the image and the tree |
| `flash` | writes the image to a physical device (needs `--device` and `--yes`) |
| `create-robofs-image` | standalone RoboFS image, formatted and checked |

### Options

`--verbose` `--debug` `--rebuild` `--skip-build` `--skip-qemu` `--headless`
`--sanitize` `--visible` `--filter <substr>` `--configuration Debug|Release`
`--baked` `--menu` `--hold-alt` `--fullscreen` `--firmware-menu` `--reset-nvram`
`--hold <ms>` `--shot <png>` `--width <px>` `--height <px>` `--timeout <s>`
`--device <n>` `--yes` `--force` `--esp-flags <text>` `--list`

---

## 3. Where the output goes

```
build/
  artifacts/      BOOTX64.EFI, kernel.elf, roboboot.exe, roboboot-test.exe
  images/         roboticelaos.img (bootable GPT disk), roboticelaos-usb.zip, robofs.img
  dist/           the flash-ready USB/SD tree (EFI/, config/, assets/, volumes/)
  deploy/         deployment.json (manifest: revision, artifacts, boot layout)
  logs/           the full output of every command that ran
  test-results/   <command>.json and <command>.junit.xml
  tmp/            OVMF variable store, QEMU serial log, scratch
```

Sub-projects keep their own object directories (`bootloader/build`,
`kernel/build`, `libc/build`, `runtime/Out`); `clean` removes those too. Nothing
authored by you is ever deleted.

Every long-running command writes its output to `build/logs/<name>.log` **as it
runs**, so a 20-minute build can be followed with
`tail -f build/logs/build-runtime.log`. A failed step prints the tail of that
log and names the file.

Test results are machine-readable: `build/test-results/<command>.json` and a
JUnit XML file for CI. A `skip` is recorded as `skip`, never as a pass.

---

## 4. Deploying to real hardware

Two shapes, both real.

### The disk image (recommended)

`build/images/roboticelaos.img` is a complete GPT disk: protective MBR, a GPT
with CRC-checked headers and partition array, and a FAT16 EFI System Partition
containing `EFI/BOOT/BOOTX64.EFI`, the kernel, the initrd, the boot
configuration and the assets. Write it verbatim to a USB stick or SD card:

```
scripts/robo deploy
scripts/robo validate-deployment
scripts/robo flash --device 3 --yes
```

`--device 3` means `\\.\PhysicalDrive3`. `flash` refuses to act without
`--yes`, refuses a target smaller than the image, refuses the system disk unless
`--force` (and then asks you to type `ERASE`), and **never elevates itself** —
if it needs administrator rights it tells you to re-run from an elevated shell.

### The file tree

Copy the **contents** of `build/dist/` onto a FAT32 drive. The stick root must
contain `EFI\BOOT\BOOTX64.EFI`; do not nest an extra folder.
`build/images/roboticelaos-usb.zip` is the same tree zipped for that workflow.

### What validation actually proves

`validate-deployment` parses the artifact the way firmware will, and fails if
any of it is wrong:

- protective MBR signature and GPT partition type
- GPT header signature, size, header CRC, and the backup header at the last LBA
- GPT partition array CRC, and an EFI System Partition with sane bounds
- FAT16 geometry (bytes/sector, cluster count) inside the FAT16 range
- `EFI/BOOT/BOOTX64.EFI` and `EFI/Roboticela/BOOTX64.EFI` are PE32+ x86-64
  `EFI application` images (machine `0x8664`, subsystem `10`)
- `volumes/sys/kernel.elf` is a 64-bit x86-64 ELF and is **byte-identical** to
  the kernel that was just built — so a stale image is caught, not shipped
- the deployment tree matches the image layout, and every required file is
  present and non-empty

---

## 5. Configuration and debug mode

`scripts/pipeline/config.ps1` is the single source of truth for paths, QEMU
settings and timeouts. Precedence is: command-line flag → environment override →
default.

```
ROBO_RUNTIME_CONFIGURATION=Release
ROBO_QEMU_MEMORY_MB=1024
ROBO_QEMU_ACCEL=whpx
ROBO_QEMU_DISPLAY=gtk
ROBO_TIMEOUT_KERNEL=600
ROBO_LLVM_BIN=C:\LLVM\bin
```

`--verbose` (`-v`) prints the resolved configuration and every command line the
pipeline runs. Secrets are never printed: any environment value whose name
matches `secret|password|token|credential|api_key|private_key` is redacted.

If a QEMU setting looks wrong, `scripts/robo config` shows exactly what will be
used before you run anything.

---

## 6. Known behaviour and limitations

- **`window.host.keyboard` and `window.host.mouse` need a foreground desktop
  session.** Both start by asking the window for foreground activation. Windows
  only grants `SetForegroundWindow` to a process that is already entitled to the
  foreground, so from a detached shell the call returns FALSE with
  `GetLastError() == 0` and `win32_focus` deliberately reports
  `ROBO_NOT_SUPPORTED` instead of faking success — the debug log line is
  `robo_window.win32: SetForegroundWindow declined err=0`. The two suites assert
  `ROBO_OK`, so they fail; `window.host.focus_display` exercises the same path
  and tolerates the refusal, so it passes either way. This is a property of the
  host desktop session, not of the pipeline: run `scripts/robo test-window` from
  a normal interactive desktop terminal for a green run. No other suite is
  affected — the mock and null backends cover the same API headlessly.
- **The guest's user-space selftests are not all green yet.**
  `test-qemu-userspace` boots with the `ktest` flag and gates on the verdicts
  the guest itself prints. It currently fails, with the reason quoted verbatim
  from the serial log:
  `[FAIL] Connected` / `[FAIL] Window Server` (there is no window-server
  service in `user/init/config.c` yet — the file's own comment says compositor
  and window server "return when that stack is ready") and `[FAIL] sockets` /
  `[FAIL] LIBC` (the libc socket test needs a server on the forwarded port).
  `[PASS] RoboCompositor` and `[PASS] INIT PROCESS` are listed as markers but
  cannot be reached in this boot: no compositor service is configured, and init
  prints its summary only after every service has exited while `robo-runtime`
  runs for the whole boot. The gate is deliberately red until those components
  land; it is never downgraded to a skip or a pass.
- **The QEMU disk attachment falls back automatically.** `test-qemu-*` and
  `run-kernel` try the configured disk mode (default `ide`) and, if the firmware
  finds nothing bootable, retry as `virtio-blk`. A failure lists exactly which
  markers never arrived.
- **QEMU acceleration defaults to `tcg`.** WHPX combined with the standard VGA
  GOP aborts QEMU on some Windows hosts. Set `ROBO_QEMU_ACCEL=whpx` if it works
  on yours.
- **The runtime build script runs its own suite as part of `-Test`.** That makes
  `build-runtime` a build and `test-runtime` build-plus-test; the granular
  `test-<component>` commands re-run individual suites against the same binary.
- **`chromium/` is reference material.** It is never compiled, never linked and
  never added to an include path. `robo check` verifies this on every run, and
  `-Verify` repeats the deeper per-subsystem scan during the runtime build.
- **The project is Windows-host only.** The toolchain, the UEFI image and the
  host preview all target `x86_64-pc-windows-msvc` host tooling.

---

## 7. Troubleshooting

| Symptom | Do this |
|---|---|
| `scripts/robo: command not found` on Windows | use `scripts\robo.cmd`, or `scripts\robo.ps1` from PowerShell |
| "PowerShell not found" from Git Bash | `powershell.exe` must be on `PATH` (it is by default on Windows) |
| `MISSING clang` / `lld-link` | install LLVM, or set `ROBO_LLVM_BIN` to the LLVM `bin` directory |
| `MISSING python: Pillow` | `pip install Pillow cairosvg` |
| QEMU stages are `SKIP` | install QEMU and the OVMF firmware, then re-run `robo check` |
| A build fails | read the named `build/logs/*.log`; the console only shows the tail |
| `the image is stale` from validation | re-run `scripts/robo build-image` |
| QEMU boots nothing | try `--esp-flags "splash_ms 0"`, or set `ROBO_QEMU_DISK=virtio` |
| `test-window` fails only in `window.host.keyboard` / `window.host.mouse` | the shell has no foreground desktop session; run from an interactive terminal (see §6) |
| `test-qemu-userspace` fails | expected today: the window-server and compositor services do not exist yet. Read the `[FAIL]` lines it quotes |

---

## 8. Layout of the pipeline itself

```
scripts/
  robo                     launcher (Git Bash / MSYS / WSL)
  robo.cmd                 launcher (cmd.exe)
  robo.ps1                 launcher (PowerShell)
  pipeline/
    robo.ps1               entry point: argument parsing, dispatch, exit codes
    config.ps1             all configuration, in one place
    common.ps1             output, live logging, process execution, results
    engine.ps1             adapters over the subsystem sub-builds
    commands.check.ps1     environment validation
    commands.build.ps1     build commands
    commands.test.ps1      test commands
    commands.run.ps1       run commands
    commands.deploy.ps1    deployment, validation, flashing
    validate_image.py      read-only GPT + FAT16 image validator
  roboboot-build-common.ps1  the bare-metal build engine (bootloader/kernel/ESP)
  roboboot-qemu-common.ps1   QEMU process management
  net-echo-server.py         loopback server the OS's network tests use
```

The engines are the only files that know how to invoke a specific compiler or
linker. `commands.*.ps1` talk only to `engine.ps1`, so there is exactly one
place to change when a subsystem's build invocation changes.
