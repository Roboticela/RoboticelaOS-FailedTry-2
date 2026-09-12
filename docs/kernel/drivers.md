# Driver system (Phases 14–16)

Roboticela OS keeps the base ISO small. The kernel ships only **Category A**
baseline drivers. Vendor GPUs, Wi-Fi, NVMe, AHCI, and USB stacks are not
compiled in.

```text
PCI / ACPI / platform
        ↓
  Device model
        ↓
  Driver registry + matching
        ↓
  Builtin driver (probe / start)
        ↓
  Subsystem API (block, graphics, input)
```

## Layout

| Path | Role |
|------|------|
| `/System/Drivers/Builtin/` | Compiled-in baseline (conceptual; v1 is linked into the kernel) |
| `/System/Drivers/Generic/` | Future optional standard packages (`.rdrv`) |
| `/System/Drivers/Installed/` | Future signed vendor packages |
| `/System/Drivers/Cache/` | Future download cache |

v1 does **not** load relocatable kernel modules. A `.rdrv` package is
verified and recorded. Executable driver code is not mapped from a
package until a future loader exists.

## Driver ABI

Major must match `ROBO_DRIVER_ABI_MAJOR` (1). Minor must be `<=`
`ROBO_DRIVER_ABI_MINOR` (0). A major mismatch is `ROBO_EBADABI`. The
kernel never reinterprets an incompatible ABI.

Lifecycle: `DISCOVERED → MATCHED → VERIFIED → LOADED → INITIALIZED →
RUNNING`. Failure sets `FAILED`. Removal: `STOPPING → UNLOADED`.

## Matching

PCI scores, highest wins:

1. vendor + device (+ subsystem)
2. class/subclass
3. no match → device stays `Unsupported` (kernel continues)

Platform devices match `ident` (`framebuffer`, `ps2-keyboard`,
`ps2-mouse`). PCI enumeration never calls a vendor driver directly.

## Signature system

Packages use magic `RDRV`, SHA-256 over (header with hash/sig zeroed)
combined with the payload hash, then **Ed25519** over the 32-byte
package hash.

Production mode rejects:

* unsigned packages
* hash mismatch
* invalid signature
* unknown key id
* ABI mismatch
* non-x86_64 `arch`

Test mode is explicit (`driver-test` on the kernel command line, or
`robo_rdrv_verify(..., test_mode=1)`). Unsigned packages also need
`ROBO_RDRV_FLAG_TEST`. There is no hidden bypass.

The kernel image contains **public** trust slots only. Private signing
keys are not in the repository, ISO, or kernel. The production key
ceremony is **not implemented**. Host tests generate ephemeral keys.

Revocation lists, key rotation, and Secure Boot integration are designed
for later and are **not implemented**.

## DMA / MMIO / IRQ

* MMIO: `robo_mmio_map` identity-maps physical BARs with `NOCACHE`.
* IRQ: `robo_irq_register` (PIC). IRQ0 remains the timer.
* DMA: virtio uses `robo_page_alloc` (VA = PA in the identity map).
  **IOMMU is not implemented.** Devices can DMA any identity-mapped RAM.
  This is a documented security limitation.

## Category A builtins (v1)

* PCI enumeration (ECAM via ACPI MCFG when present, CF8/CFC fallback)
* Minimal ACPI (RSDP checksum + MCFG only)
* UEFI framebuffer → RoboGraphics (CPU renderer)
* PS/2 keyboard and mouse → Input Manager (generic key/mouse events)
* VirtIO block (modern PCI) → existing `RoboBlockDevice` → RoboFS

VirtIO claims only disks with `capacity <= 32768` 512-byte sectors so a
firmware ESP presented as virtio-blk is not formatted.

## Not in this phase

NVMe, AHCI, USB, xHCI, GPU acceleration, Vulkan/OpenGL, networking,
full ACPI, user-space drivers, automatic repository download.

## Diagnostics

Boot prints `ROBOTICELA DRIVER SYSTEM` with live counts, then
`[PASS] DRIVER SYSTEM` / `[PASS] STORAGE DRIVER` when self-tests succeed.

User: `/bin/robo` calls `SYS_DRIVER_QUERY` (24).
