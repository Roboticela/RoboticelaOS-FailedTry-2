# Driver system (Phases 14–16 + Generic Phase 1)

Roboticela OS keeps the base ISO small and boots without vendor packages by
shipping **Category A** standards-based drivers. Vendor GPUs, chipset Wi-Fi,
and vendor NICs are **Category B** (signed `.rdrv` packages later).

```text
PCI / ACPI / USB / platform
        ↓
  Device model
        ↓
  Driver registry + matching
        ↓
  Builtin driver (probe / start)
        ↓
  Subsystem API (block, graphics, input, USB, net, power)
```

## Layout

| Path | Role |
|---|---|
| `/System/Drivers/Builtin/` | Compiled-in baseline (linked into the kernel) |
| `/System/Drivers/Generic/` | Future optional standard packages (`.rdrv`) |
| `/System/Drivers/Installed/` | Future signed vendor packages |
| `/System/Drivers/Cache/` | Future download cache |

v1 does **not** load relocatable kernel modules. A `.rdrv` package is
verified and recorded. Executable driver code is not mapped from a
package until a future loader exists.

## Driver ABI

Major must match `RoboDriverAbiMajor` (1). Minor must be `<=`
`RoboDriverAbiMinor` (0). A major mismatch is `RoboEbadabi`.

Lifecycle: `DISCOVERED → MATCHED → VERIFIED → LOADED → INITIALIZED →
RUNNING`. Failure sets `FAILED`. Removal: `STOPPING → UNLOADED`.

## Matching

PCI scores, highest wins:

1. vendor + device (+ subsystem)
2. class/subclass (+ progif when non-zero on the id)
3. no match → device stays `Unsupported` (kernel continues; production boot stays quiet)

Platform devices match `ident` (`framebuffer`, `ps2-keyboard`,
`ps2-mouse`).

## Category A builtins (generic Phase 1)

| Area | Driver / layer | Standard |
|---|---|---|
| PCI | Enumeration | ECAM (ACPI MCFG) / CF8–CFC |
| ACPI | Tables | RSDP → XSDT → MCFG, FADT, MADT |
| Display | UEFI GOP + EDID synth | Firmware framebuffer |
| Input | PS/2 | Legacy keyboard/mouse |
| Input | USB HID class | Boot protocol (with xHCI transfers later) |
| Storage | VirtIO block | VirtIO PCI |
| Storage | AHCI | SATA AHCI (PCI `01:06:01`) |
| Storage | NVMe | NVM Express (PCI `01:08:02`) |
| Storage | Block table + GPT/MBR | Partition layer over `RoboBlockDevice` |
| USB | xHCI host | USB 3.x host (PCI `0C:03:30`) |
| USB | MSC BOT class | Mass storage class attach |
| Network | VirtIO-net + TCP/IP | VirtIO + Ethernet/IPv4 stack |
| Power | Battery API | Stub until AML (`_BIF`/`_BST`) |

### Block architecture

```text
NVMe / AHCI / VirtIO / USB-MSC
        ↓
  RoboBlockDevice
        ↓
  BdevTable (disk0, disk0p1, …)
        ↓
  GPT / MBR PartitionProbe
        ↓
  VFS / filesystem
```

### Display path (no vendor GPU)

```text
UEFI GOP → framebuffer → RoboGraphics (CPU) → desktop
EDID preferred mode synthesized from GOP until a raw EDID blob is handed off
```

## Signature system

Packages use magic `RDRV`, SHA-256 over (header with hash/sig zeroed)
combined with the payload hash, then **Ed25519** over the 32-byte
package hash. Production rejects unsigned / mismatched / unknown-key
packages. Test mode is explicit (`driver-test` cmdline).

## DMA / MMIO / IRQ

* MMIO: `MmioMap` identity-maps physical BARs with `NOCACHE`.
* IRQ: `IrqRegister` (PIC). MSI/MSI-X routing is not yet a full driver.
* DMA: drivers use `PageAlloc` (VA = PA). **IOMMU is not implemented.**

## Category B (not in this phase)

* Intel / AMD / NVIDIA GPU acceleration
* Vendor Ethernet / Wi-Fi chipset drivers
* Full AML interpreter, sleep/hibernate
* HDA codec graphs, I²C/SPI device drivers, TPM 2.0
* Relocatable `.rdrv` loading

## Roadmap (dependency order)

```text
Done:   UEFI → PCI → interrupts/DMA → VirtIO → GOP → PS/2 → AHCI/NVMe → GPT → xHCI host
Next:   xHCI transfer rings → HID/MSC I/O → HDA → VirtIO-GPU → Wi-Fi MAC glue → AML battery
Later:  Vendor GPU / Wi-Fi / NIC packages via Driver Manager
```

## Diagnostics

Self-test (`ktest`) prints `ROBOTICELA DRIVER SYSTEM` with live counts and
`[PASS] DRIVER SYSTEM` / `[PASS] STORAGE DRIVER` when suites succeed.
Production boots keep unsupported-device dumps silent.
