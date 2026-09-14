---
alwaysApply: true
---

## Roboticela Complete Color Palette

### Brand / Primary (Blue)

| Token | Light Mode | Dark Mode | Use |
|---|---|---|---|
| `--brand-primary` | `#0E7AFF` | `#3B9EFF` | Primary buttons, active nav, links |
| `--brand-primary-hover` | `#0A6AE8` | `#5CADFF` | Button/link hover |
| `--brand-primary-active` | `#0757C7` | `#2E8CE6` | Pressed/mousedown state |
| `--brand-primary-disabled` | `#B8D9FF` | `#1E3A5C` | Disabled primary buttons |
| `--brand-primary-subtle` | `rgba(14,122,255,0.10)` | `rgba(59,158,255,0.15)` | Selected states, badge backgrounds |
| `--brand-primary-focus-ring` | `rgba(14,122,255,0.25)` | `rgba(59,158,255,0.30)` | Input focus rings |

### Accent (Teal — sparing use only)

| Token | Light Mode | Dark Mode | Use |
|---|---|---|---|
| `--accent-teal` | `#14B8A6` | `#2DD4C4` | Success states, "Active"/"Verified" badges |
| `--accent-teal-subtle` | `rgba(20,184,166,0.10)` | `rgba(45,212,196,0.15)` | Success badge backgrounds |

### Semantic (Status Colors)

| Token | Light Mode | Dark Mode | Use |
|---|---|---|---|
| `--success` | `#30D158` | `#30D158` | Success text/icons |
| `--success-subtle` | `rgba(48,209,88,0.10)` | `rgba(48,209,88,0.15)` | Success badge bg |
| `--warning` | `#FF9F0A` | `#FF9F0A` | Warnings, unverified states |
| `--warning-subtle` | `rgba(255,159,10,0.10)` | `rgba(255,159,10,0.15)` | Warning badge bg |
| `--danger` | `#FF3B30` | `#FF453A` | Errors, destructive actions |
| `--danger-hover` | `#E0342A` | `#FF5A50` | Destructive button hover |
| `--danger-subtle` | `rgba(255,59,48,0.10)` | `rgba(255,69,58,0.10)` | Error banner/badge bg |

### Neutrals — Text

| Token | Light Mode | Dark Mode | Use |
|---|---|---|---|
| `--text-primary` | `#1D1D1F` | `#FFFFFF` | Headlines, primary body text |
| `--text-secondary` | `#6E6E73` | `#A1A1A6` | Body copy, descriptions |
| `--text-tertiary` | `#86868B` | `#98989D` | Captions, muted labels |
| `--text-disabled` | `#C7C7CC` | `#48484A` | Disabled field text |
| `--text-on-brand` | `#FFFFFF` | `#FFFFFF` | Text on filled blue buttons |
| `--text-link` | `#0E7AFF` | `#3B9EFF` | Inline links |

### Neutrals — Backgrounds

| Token | Light Mode | Dark Mode | Use |
|---|---|---|---|
| `--bg-primary` | `#FFFFFF` | `#000000` | Page background |
| `--bg-secondary` | `#F5F5F7` | `#1C1C1E` | Section alternation, cards, sidebars |
| `--bg-tertiary` | `#EDEDEF` | `#2C2C2E` | Nested cards, input backgrounds |
| `--bg-elevated` | `#FFFFFF` | `#2C2C2E` | Modals, popovers (raised above tertiary) |
| `--bg-hover` | `#F5F5F7` | `#2C2C2E` | Row/list item hover |
| `--bg-active` | `#EDEDEF` | `#3A3A3C` | Row/list item pressed |
| `--bg-overlay` | `rgba(0,0,0,0.5)` | `rgba(0,0,0,0.7)` | Modal backdrop |

### Borders / Dividers

| Token | Light Mode | Dark Mode | Use |
|---|---|---|---|
| `--border-default` | `#D2D2D7` | `#38383A` | Card borders, input borders, dividers |
| `--border-subtle` | `#E5E5E7` | `#2C2C2E` | Faint separators (e.g. table rows) |
| `--border-strong` | `#B8B8BD` | `#48484A` | Emphasized borders (e.g. danger zone) |
| `--border-focus` | `#0E7AFF` | `#3B9EFF` | Focused input border |
| `--border-danger` | `rgba(255,59,48,0.3)` | `rgba(255,69,58,0.3)` | Danger zone card border |

### Buttons — Secondary / Ghost

| Token | Light Mode | Dark Mode | Use |
|---|---|---|---|
| `--btn-secondary-bg` | `transparent` | `transparent` | Secondary button rest |
| `--btn-secondary-border` | `#D2D2D7` | `#38383A` | Secondary button border |
| `--btn-secondary-hover-bg` | `#F5F5F7` | `#2C2C2E` | Secondary button hover |
| `--btn-secondary-text` | `#1D1D1F` | `#FFFFFF` | Secondary button label |
| `--btn-ghost-hover-bg` | `#F5F5F7` | `rgba(255,255,255,0.08)` | Icon-only/ghost button hover |

### Shadows (paired with the palette, not standalone hex)

| Token | Light Mode | Dark Mode |
|---|---|---|
| `--shadow-sm` | `0 1px 3px rgba(0,0,0,0.08)` | `0 1px 3px rgba(0,0,0,0.3)` |
| `--shadow-md` | `0 4px 24px rgba(0,0,0,0.08)` | `0 4px 24px rgba(0,0,0,0.4)` |
| `--shadow-lg` | `0 12px 32px rgba(0,0,0,0.12)` | `0 12px 40px rgba(0,0,0,0.5)` |
| `--shadow-modal` | `0 20px 60px rgba(0,0,0,0.2)` | `0 20px 60px rgba(0,0,0,0.6)` |

### Rules for Applying This Palette

- **One accent rule holds:** `--brand-primary` is the only color driving interactive elements (buttons, links, toggles-on, focus rings) across the whole product. `--accent-teal` appears only for "success/verified/active" semantic badges — never as a second competing CTA color.
- **Never mix light-mode and dark-mode hex values in the same view** — always resolve through the token, not the literal hex, so a component dropped into either theme stays correct automatically.
- **`--bg-secondary` vs `--bg-tertiary`**: secondary is for large surfaces (sidebar, section backgrounds), tertiary is for small nested elements sitting *inside* a secondary surface (an input field inside a settings card) — this is what keeps the depth layering correct instead of everything reading as the same flat gray.


---
alwaysApply: true
---

Whenever is required update the debug logs to add new logs which it will show.

---
alwaysApply: true
---

# System Prompt: Apple-Style Design Generator

You are a senior visual/product designer who has spent over a decade designing for Apple's ecosystem (apple.com marketing pages, iOS/macOS interfaces, and keynote-style product reveals). You default to Apple's design language on every page, component, and layout you produce — precise, restrained, premium, and effortless. You do not explain that you're imitating Apple; you simply design this way natively.

## Core Design Philosophy

1. **Clarity over decoration.** Every element must justify its existence. If it doesn't communicate, remove it. Whitespace is a design tool, not empty leftover space — use generous negative space to let content breathe.
2. **Deference.** The UI never competes with the content. Chrome, borders, and ornamentation stay quiet so the product (image, headline, feature) is the star.
3. **Depth with restraint.** Use subtle layering — soft shadows, translucency (frosted-glass/blur effects), and gentle elevation — to imply hierarchy, never for flash.
4. **One hero idea per screen.** Each section makes a single, confident statement. Don't stack three competing ideas in one viewport.

## Typography — Exact Values

- Font stack: `font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display", "SF Pro Text", "Helvetica Neue", Arial, sans-serif;`
- Type scale (exact px, weight, letter-spacing, line-height):
  | Role | Size | Weight | Letter-spacing | Line-height |
  |---|---|---|---|---|
  | Display (hero) | 80px | 600 (Semibold) | -0.015em | 1.05 |
  | H1 (section headline) | 56px | 600 | -0.015em | 1.07 |
  | H2 (subsection) | 40px | 600 | -0.01em | 1.1 |
  | H3 (card title) | 28px | 600 | -0.005em | 1.14 |
  | Subheadline | 24px | 400 (Regular) | 0em | 1.33 |
  | Body | 19px | 400 | 0em | 1.42 |
  | Body small | 17px | 400 | 0em | 1.47 |
  | Caption | 12px | 400 | 0em | 1.33 |
  | Button label | 17px | 500 (Medium) | 0em | 1 |
- Mobile scale (viewport ≤ 734px): Display 40px, H1 32px, H2 28px, H3 21px, Subheadline 19px, Body 17px.

## Color — Exact Hex Values

| Token | Hex | Usage |
|---|---|---|
| `--white` | `#FFFFFF` | Light section background |
| `--black` | `#000000` | Dark section background (full black, not off-black) |
| `--gray-950` | `#1D1D1F` | Primary text on white; near-black surfaces |
| `--gray-100` | `#F5F5F7` | Light gray section background |
| `--gray-300` | `#D2D2D7` | Hairline borders/dividers |
| `--gray-500` | `#86868B` | Secondary/muted text on white |
| `--gray-600` | `#6E6E73` | Body text on white (slightly darker than gray-500) |
| `--gray-on-dark` | `#A1A1A6` | Secondary text on black sections |
| `--blue` | `#0071E3` | Primary CTA, links |
| `--blue-hover` | `#0077ED` | Button hover state |
| `--blue-active` | `#006EDB` | Button pressed state |
| `--red` | `#FF3B30` | Errors/destructive actions only |

Exactly one accent color per page beyond blue (e.g. product gold `#F5D061`, pink `#FF6482`, or midnight `#1E2749` — pick one, do not mix).

## Layout & Spacing — Exact Values

- Max content width: `1024px` (wide marketing sections), `980px` (text-focused sections), `692px` (single-column copy blocks).
- Horizontal edge padding: `22px` on mobile (≤ 734px), `40px` on tablet (735–1068px), `min(5vw, 80px)` on desktop.
- Section vertical padding: `140px` top/bottom on desktop, `88px` on mobile.
- Spacing scale (use only these values): `4px, 8px, 12px, 16px, 24px, 32px, 48px, 64px, 96px, 128px`.
- Border radius: `18px` on large cards/images, `12px` on small cards, `980px` on pill buttons, `8px` on input fields.
- Grid: 12-column, `24px` gutters on desktop, `16px` gutters on mobile.

## Imagery & Motion — Exact Values

- Hero product image max width: `900px`, centered, `48px` top margin from headline block.
- Box shadow (soft elevation): `0 4px 24px rgba(0, 0, 0, 0.08)` on light cards; `0 8px 40px rgba(0, 0, 0, 0.35)` on dark sections.
- Backdrop blur (nav bar): `backdrop-filter: blur(20px) saturate(180%); background-color: rgba(255,255,255,0.72);` (light mode) or `rgba(0,0,0,0.72)` (dark mode).
- Easing curve: `cubic-bezier(0.28, 0.11, 0.32, 1)` for all transitions and reveals (Apple's signature ease-out curve). Duration `600ms` for section reveals, `300ms` for hover/press states, `150ms` for button press.
- Hover scale on cards: `transform: scale(1.02);` transition `300ms` with the easing curve above.
- Scroll reveal: elements start `opacity: 0; transform: translateY(24px);`, animate to `opacity: 1; transform: translateY(0);` over `600ms`.

## Components — Exact Values

- **Primary button:** height `44px` desktop / `50px` mobile, padding `0 22px`, background `--blue`, text `#FFFFFF` at `17px`/500 weight, border-radius `980px`, hover → `--blue-hover`, active → `--blue-active`, transition `150ms` ease.
- **Secondary button:** same dimensions, `transparent` background, `1px solid --gray-300` border, text `--blue`.
- **Nav bar:** height `44px`, sticky, blur per above, logo `20px`, nav items `12px` font/400 weight, `32px` gap between items.
- **Cards:** border-radius `18px`, padding `32px`, background `--gray-100` (light theme) or `#111113` (dark theme), no border, shadow per above.
- **Footer:** background `--gray-100`, text `12px` `--gray-500`, 4–6 columns, `1px solid --gray-300` divider above legal text, `48px` vertical padding.

## Voice & Copywriting

- Confident, benefit-first, minimal adjectives. State what the product *does*, not marketing fluff.
- Short headline → one supporting line → a simple call to action ("Learn more", "Buy", "Try it now").
- Never use exclamation points or hype language. Calm confidence, not excitement.

## Output Instructions

When generating a page or component:
1. Default to a light section immediately followed by a dark "reveal" section, alternating as needed.
2. Always specify exact hex values, font stacks, spacing, and radius values in code — never leave styling vague.
3. Every hero section needs: a large centered headline, a short subhead, a primary CTA, and a product image/visual with breathing room around it.
4. Keep total color count per page small (2–3 neutrals + 1 accent).
5. Prioritize responsive, mobile-first layouts that collapse gracefully — Apple's pages always work flawlessly on small screens.

Design as if this will ship on apple.com tomorrow — nothing generic, nothing busy, nothing loud.


# Roboticela OS — Complete Build Plan

## From your current RoboBootloader/kernel to a Chromium-powered operating system

Yes. Let's start properly and make this the **master architecture plan for Roboticela OS**.

I checked the current Chromium architecture before laying this out. Chromium itself has explicit platform/build abstractions, including OS build flags, Mojo IPC, and the Ozone display platform. Chromium's current source even documents how an external platform can add an Ozone implementation. ([Chromium Git Repositories][1])

That gives us an important architectural direction:

> **Do not make Roboticela OS imitate Linux. Build Roboticela OS's own kernel and APIs, then create a Chromium platform port/compatibility layer on top of them.**

---

# 0. The final goal

The finished system should look approximately like this:

```text
                         ROBOTICELA OS
┌───────────────────────────────────────────────────────────────┐
│                                                               │
│                    Roboticela Applications                    │
│                                                               │
│  File Manager   Settings   Terminal   Store   Browser   Apps │
│                                                               │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│                    HTML / CSS / JavaScript                    │
│                                                               │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│                       RoboRuntime                              │
│                                                               │
│             Roboticela Web Application Runtime                │
│                                                               │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│                         Chromium                              │
│                                                               │
│       Blink       V8       Skia       Mojo       Network       │
│                                                               │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│              Roboticela Chromium Platform Layer               │
│                                                               │
│   Process │ Thread │ File │ Network │ Graphics │ Sandbox      │
│                                                               │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│                    Roboticela User Space                      │
│                                                               │
│  libc │ runtime │ services │ drivers │ shell │ applications  │
│                                                               │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│                    Roboticela Kernel                           │
│                                                               │
│  VM │ PMM │ Scheduler │ IPC │ Syscalls │ VFS │ Security      │
│                                                               │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│                       RoboFS                                  │
│                                                               │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│                     Hardware Drivers                           │
│                                                               │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│                    Physical Hardware                           │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

And underneath:

```text
UEFI
 ↓
RoboBootloader
 ↓
Roboticela Kernel
```

Your current QEMU booting work is therefore **the beginning of Phase 1**, not something we throw away.

---

# 1. Critical architectural decision

There are three things we should **not** do.

### Don't do this

```text
Roboticela OS
    ↓
Linux kernel
    ↓
Chromium
```

You have already decided you don't want Linux as the kernel foundation.

### Don't do this either

```text
Roboticela Kernel
    ↓
fake Linux kernel APIs
    ↓
Chromium
```

That creates an enormous compatibility project and makes Roboticela OS effectively dependent on Linux's architecture.

### Do this

```text
Roboticela Kernel
        ↓
Roboticela OS APIs
        ↓
Chromium Platform Adapter
        ↓
Chromium
```

This lets **Roboticela OS remain Roboticela OS**.

---

# 2. Repository architecture

I recommend eventually having one main repository:

```text
RoboticelaOS/
```

with:

```text
RoboticelaOS/
│
├── bootloader/
│   ├── include/
│   ├── src/
│   ├── assets/
│   ├── linker/
│   ├── config/
│   ├── tests/
│   └── Makefile
│
├── kernel/
│   ├── arch/
│   │   └── x86_64/
│   │       ├── boot/
│   │       ├── cpu/
│   │       ├── interrupts/
│   │       ├── paging/
│   │       ├── context/
│   │       └── smp/
│   │
│   ├── mm/
│   ├── process/
│   ├── scheduler/
│   ├── syscall/
│   ├── ipc/
│   ├── vfs/
│   ├── security/
│   ├── drivers/
│   ├── time/
│   ├── acpi/
│   ├── pci/
│   ├── graphics/
│   └── kernel.c
│
├── fs/
│   └── robofs/
│
├── libc/
│
├── libsystem/
│
├── userspace/
│   ├── init/
│   ├── shell/
│   ├── services/
│   ├── drivers/
│   ├── daemons/
│   └── utilities/
│
├── runtime/
│   ├── roboruntime/
│   ├── web/
│   └── javascript/
│
├── graphics/
│   ├── framebuffer/
│   ├── compositor/
│   ├── windows/
│   ├── fonts/
│   └── input/
│
├── network/
│   ├── ethernet/
│   ├── arp/
│   ├── ipv4/
│   ├── ipv6/
│   ├── tcp/
│   ├── udp/
│   ├── dns/
│   ├── sockets/
│   ├── tls/
│   └── http/
│
├── drivers/
│   ├── pci/
│   ├── storage/
│   ├── usb/
│   ├── network/
│   ├── gpu/
│   ├── audio/
│   ├── input/
│   └── display/
│
├── sdk/
│   ├── include/
│   ├── libraries/
│   ├── tools/
│   └── templates/
│
├── tools/
│   ├── image-builder/
│   ├── filesystem/
│   ├── package/
│   ├── emulator/
│   └── debugger/
│
├── apps/
│   ├── file-manager/
│   ├── terminal/
│   ├── settings/
│   └── store/
│
├── chromium/
│   ├── patches/
│   ├── platform/
│   ├── ozone/
│   ├── sandbox/
│   ├── build/
│   └── integration/
│
├── tests/
│   ├── kernel/
│   ├── fs/
│   ├── drivers/
│   ├── runtime/
│   └── chromium/
│
├── scripts/
├── docs/
├── configs/
└── Makefile
```

**Important:** don't create all of these directories today.

We create them when their subsystem begins.

---

# 3. Development rule: every subsystem must be independently testable

This is extremely important for your project because you previously had the problem of changing everything and then not knowing what broke.

Every subsystem should have:

```text
implementation
tests
documentation
demo
integration test
```

For example:

```text
kernel/mm/
```

should eventually contain:

```text
physical.c
physical.h
virtual.c
virtual.h
heap.c
heap.h
tests/
```

and a test such as:

```text
allocate 100 pages
 ↓
write to them
 ↓
free them
 ↓
allocate again
 ↓
verify reuse
```

Only when that works do we integrate it into the larger kernel.

---

# 4. The development environments

You actually need **three environments**.

## Environment A — Host development

Your current Ubuntu machine.

```text
Ubuntu
 ↓
compiler
 ↓
build tools
 ↓
Roboticela OS image
```

This is where you develop.

---

## Environment B — QEMU

Primary testing environment:

```text
Roboticela OS
 ↓
QEMU
 ↓
virtual CPU
virtual RAM
virtual disk
virtual GPU
virtual network
virtual keyboard
virtual mouse
```

QEMU should remain your primary automated test target for a long time.

---

## Environment C — Real hardware

Only after the QEMU version is stable:

```text
Toshiba / test PC
 ↓
UEFI
 ↓
RoboBootloader
 ↓
Roboticela Kernel
```

Do **not** use your main laptop as your primary experimental hardware target.

---

# 5. Phase 0 — Engineering infrastructure

Before adding more kernel features, establish the development infrastructure.

## 5.1 Git repository

Create:

```text
RoboticelaOS
```

with branches:

```text
main
develop
feature/*
```

Don't work directly on `main`.

---

## 5.2 Build output separation

You already use an out-of-tree build.

Keep that.

For example:

```text
RoboticelaOS/
```

source:

```text
/home/muhammad/Desktop/Muhammad/RoboticelaOS
```

and build:

```text
/home/muhammad/Desktop/Muhammad/build/roboticela-os
```

Never put generated files into the source tree unless deliberately required.

---

# 6. Define the target architecture

For the first version:

```text
Architecture: x86_64
Boot: UEFI
Firmware: UEFI
Virtualization: QEMU
Kernel language: C
Assembly: only where required
```

Potential later architectures:

```text
x86_64
ARM64
RISC-V64
```

But **not now**.

Chromium itself supports multiple processor architectures, but adding a new OS is already a large task; don't simultaneously create a multi-architecture kernel. ([Chromium Git Repositories][1])

---

# 7. Define the kernel/user boundary

This needs to be decided **before** writing hundreds of APIs.

Your system should be:

```text
Ring 0
───────
Kernel

Ring 3
───────
User applications
User services
User drivers where appropriate
Chromium
```

Never let normal applications directly access:

```text
physical memory
PCI registers
GPU registers
disk controller
interrupt controller
```

They communicate through kernel/system interfaces.

---

# 8. Phase 1 — Boot information contract

Your RoboBootloader already gives the kernel information.

Now formalize it.

Create:

```text
bootloader/include/roboticela/bootinfo.h
```

Conceptually:

```c
typedef struct {
    uint64_t address;
    uint64_t length;
    uint32_t type;
} RoboMemoryRegion;
```

Then:

```c
typedef struct {
    RoboMemoryRegion *memory_map;
    uint64_t memory_map_count;

    uint64_t framebuffer_address;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;

    uint64_t rsdp_address;

    uint64_t kernel_base;
    uint64_t kernel_size;

    uint64_t initrd_address;
    uint64_t initrd_size;
} RoboBootInfo;
```

Don't copy this blindly; this is the **contract we need to design**.

---

# 9. Bootloader acceptance test

When the kernel starts, it should be able to print:

```text
Roboticela Kernel
-----------------

Boot protocol: Roboticela
CPU: x86_64

Memory:
  Available: XXX MB
  Reserved:  XXX MB

Framebuffer:
  1280x800
  32 BPP

ACPI:
  RSDP found

Kernel:
  address: ...
  size: ...

InitFS:
  address: ...
  size: ...
```

Once this works:

**BOOT-001 PASSED**

---

# 10. Phase 2 — Physical Memory Manager

This is your first major kernel subsystem.

Create:

```text
kernel/mm/
```

with:

```text
pmm.c
pmm.h
memory_map.c
memory_map.h
page.c
page.h
```

---

## 10.1 Input

The PMM receives:

```text
UEFI memory map
```

It must identify:

```text
usable memory
reserved memory
ACPI reclaimable
ACPI NVS
bootloader data
kernel memory
framebuffer
```

Never allocate from reserved regions.

---

## 10.2 First allocator

Start with:

```text
bitmap allocator
```

Example:

```text
1 bit = 1 physical 4 KiB page
```

If:

```text
bit = 0
```

page is available.

If:

```text
bit = 1
```

page is occupied.

---

## 10.3 API

Eventually:

```c
phys_addr_t robo_page_alloc(void);
void robo_page_free(phys_addr_t page);

size_t robo_page_count(void);
size_t robo_free_page_count(void);
```

---

## 10.4 Tests

Test:

```text
allocate one page
allocate 100 pages
allocate all available pages
free pages
allocate again
```

Verify:

```text
no overlapping physical pages
no allocation from reserved memory
no double allocation
no double free
```

---

# 11. Phase 3 — Virtual Memory Manager

Directory:

```text
kernel/mm/vmm/
```

Implement:

```text
PML4
PDPT
PD
PT
```

for x86_64.

You need:

```text
map
unmap
translate
protect
```

---

## 11.1 Kernel address space

Initially:

```text
kernel virtual address
        ↓
physical memory
```

Make kernel mappings stable.

---

## 11.2 Page permissions

Pages should eventually support:

```text
READ
WRITE
EXECUTE
USER
KERNEL
```

This becomes important later for:

* executable files
* shared libraries
* JIT
* Chromium sandbox
* V8
* security

---

## 11.3 Acceptance test

Create a kernel test:

```text
allocate physical page
 ↓
map virtual address
 ↓
write data
 ↓
read data
 ↓
unmap
 ↓
verify access fails
```

---

# 12. Phase 4 — Kernel heap

Build:

```text
kernel/mm/heap/
```

Start with:

```c
kmalloc()
kfree()
kcalloc()
krealloc()
```

Don't immediately build a sophisticated allocator.

Start with something correct.

Later:

```text
slab allocator
small object allocator
per-CPU caches
```

---

# 13. Phase 5 — CPU initialization

Now formalize:

```text
kernel/arch/x86_64/
```

Implement:

```text
GDT
TSS
IDT
exceptions
CPU initialization
```

You need exception handlers for at least:

```text
divide error
debug
breakpoint
invalid opcode
page fault
general protection fault
double fault
```

---

# 14. Page fault handler

This is especially important.

When a page fault happens, report:

```text
CR2 = fault address
RIP = instruction address
error code
present/not present
read/write
user/kernel
```

Example:

```text
KERNEL PANIC

Page Fault

Address : 0x...
RIP     : 0x...
Access  : WRITE
Mode    : USER
Present : NO
```

This will save you enormous debugging time later.

---

# 15. Phase 6 — Interrupt controller

Initially support the QEMU hardware you need.

Architecture:

```text
Hardware
 ↓
interrupt controller
 ↓
IDT
 ↓
kernel interrupt handler
```

Eventually:

```text
timer
keyboard
mouse
storage
network
GPU
USB
```

---

# 16. Phase 7 — Timer

You need a reliable monotonic timer.

Expose:

```c
robo_time_now();
robo_sleep();
```

Kernel needs it for:

```text
scheduler
timeouts
networking
animations
process timing
Chromium timers
```

---

# 17. Phase 8 — Scheduler

Now create:

```text
kernel/scheduler/
```

First implementation:

```text
preemptive round-robin
```

Example:

```text
Task A
 ↓
Task B
 ↓
Task C
 ↓
Task A
```

Timer interrupt triggers scheduling.

---

# 18. Thread object

Define a kernel thread structure.

Conceptually:

```c
typedef struct RoboThread {
    uint64_t id;
    uint64_t state;
    uint64_t stack;
    uint64_t kernel_stack;
    uint64_t instruction_pointer;
    uint64_t address_space;
} RoboThread;
```

States:

```text
NEW
READY
RUNNING
BLOCKED
SLEEPING
TERMINATED
```

---

# 19. Context switching

Implement:

```text
save registers
 ↓
save stack
 ↓
load next thread
 ↓
restore registers
 ↓
resume
```

Acceptance test:

```text
Thread A increments counter
Thread B increments counter

Expected:
A = 100000
B = 100000
```

---

# 20. Phase 9 — Processes

Now separate:

```text
thread
```

from:

```text
process
```

A process owns:

```text
address space
file descriptors
threads
environment
credentials
IPC resources
```

Example:

```text
Process
├── Address Space
├── Thread 1
├── Thread 2
├── Files
├── IPC
└── Environment
```

---

# 21. ELF loader

This is essential.

Your kernel should eventually load:

```text
ELF executable
```

from:

```text
RoboFS
```

Architecture:

```text
ELF file
 ↓
ELF parser
 ↓
segments
 ↓
virtual memory
 ↓
stack
 ↓
entry point
 ↓
user process
```

Acceptance test:

```text
hello
```

prints:

```text
Hello from user space.
```

---

# 22. Phase 10 — System calls

Create a carefully versioned ABI.

For example:

```text
SYS_EXIT
SYS_READ
SYS_WRITE
SYS_OPEN
SYS_CLOSE
SYS_MMAP
SYS_MUNMAP
SYS_PROCESS_CREATE
SYS_THREAD_CREATE
SYS_SLEEP
SYS_TIME
SYS_IPC
```

Do **not** expose random internal kernel functions.

Create a stable syscall boundary.

---

# 23. File descriptors

You'll need:

```text
stdin
stdout
stderr
files
pipes
sockets
devices
```

Everything can eventually be represented by a handle/file descriptor abstraction.

Example:

```text
fd 0 = stdin
fd 1 = stdout
fd 2 = stderr
fd 3 = file
fd 4 = socket
```

This makes porting software significantly easier.

---

# 24. Phase 11 — VFS

Before RoboFS itself, create a generic VFS layer:

```text
Application
     ↓
VFS
     ↓
RoboFS
```

Later:

```text
VFS
├── RoboFS
├── FAT
├── ISO9660
└── virtual filesystems
```

This separation is extremely useful.

---

# 25. Phase 12 — RoboFS

Now implement your actual filesystem.

Start with:

```text
superblock
block allocator
inode/object table
directories
files
metadata
```

Minimum operations:

```text
create
delete
open
close
read
write
seek
stat
mkdir
rmdir
rename
```

---

# 26. RoboFS first version

Don't attempt:

```text
journaling
compression
snapshots
encryption
deduplication
```

on version 1.

First make:

```text
create file
write file
read file
rename
delete
directories
```

100% reliable.

---

# 27. Phase 13 — Init process

After kernel initialization:

```text
kernel
 ↓
/System/init
```

`init` becomes the first user-space process.

It starts:

```text
system services
drivers
network service
display service
window server
```

Eventually:

```text
init
 ├── device manager
 ├── network manager
 ├── window server
 ├── compositor
 ├── filesystem services
 └── login/session manager
```

---

# 28. Phase 14 — PCI

Implement PCI enumeration.

For each device:

```text
bus
device
function
vendor ID
device ID
class
subclass
BARs
interrupt information
```

Output:

```text
PCI devices:

00:01.0 VGA
00:02.0 Ethernet
00:03.0 SATA
...
```

This becomes the basis for your driver manager.

---

# 29. Driver architecture

Don't hardcode:

```text
if Intel
if AMD
if NVIDIA
```

into random kernel code.

Create:

```text
Driver Manager
      ↓
Driver Registry
      ↓
Device Matching
      ↓
Driver
      ↓
Hardware
```

Example:

```text
PCI device
 ↓
vendor=8086
device=xxxx
 ↓
Intel graphics driver
```

Later:

```text
robo driver list
robo driver install
robo driver update
robo driver remove
```

---

# 30. First storage driver

For QEMU, choose **one virtual storage interface first**.

Don't begin with:

```text
NVMe
AHCI
USB
SCSI
SATA
```

all at once.

Get one working:

```text
block_read()
block_write()
```

Then:

```text
block device
 ↓
RoboFS
```

---

# 31. Phase 15 — Framebuffer graphics

You already receive framebuffer information from your bootloader.

Turn that into:

```text
RoboGraphics
```

API:

```c
robo_display_get_info();

robo_surface_create();

robo_surface_destroy();

robo_surface_present();
```

Initially:

```text
CPU rendering
```

is completely acceptable.

---

# 32. Drawing primitives

Implement:

```text
pixel
line
rectangle
rounded rectangle
image
bitmap
text
alpha blending
clipping
```

Then:

```text
double buffering
```

to prevent tearing.

---

# 33. Font rendering

Eventually:

```text
font file
 ↓
font parser
 ↓
glyph rasterizer
 ↓
surface
```

You will eventually need this for:

```text
UI
Chromium
browser text
system UI
```

---

# 34. Phase 16 — Keyboard

First QEMU keyboard.

Architecture:

```text
Keyboard hardware
 ↓
driver
 ↓
input event
 ↓
input manager
 ↓
application
```

Event:

```c
ROBO_KEY_DOWN
ROBO_KEY_UP
```

Don't make applications understand hardware scancodes.

---

# 35. Mouse

Same architecture:

```text
mouse driver
 ↓
input manager
 ↓
ROBO_MOUSE_MOVE
ROBO_MOUSE_BUTTON
ROBO_MOUSE_SCROLL
```

---

# 36. Input manager

Create one unified input API:

```text
keyboard
mouse
touchpad
touchscreen
gamepad
```

Later.

Chromium should see:

```text
generic input events
```

rather than PS/2/USB implementation details.

---

# 37. Phase 17 — Window server

Now:

```text
RoboWindowServer
```

An application asks:

```text
create window
```

and gets:

```text
window ID
surface
event queue
```

Window structure:

```text
Window
├── ID
├── position
├── size
├── surface
├── visibility
├── focus
└── event queue
```

---

# 38. Phase 18 — RoboCompositor

Now create:

```text
RoboCompositor
```

It receives:

```text
Window A surface
Window B surface
Window C surface
```

and produces:

```text
final screen
```

Initially:

```text
CPU compositor
```

Later:

```text
GPU compositor
```

---

# 39. Phase 19 — Networking

This must happen **before serious Chromium work**.

Build:

```text
NIC
 ↓
Ethernet
 ↓
ARP
 ↓
IPv4
 ↓
TCP/UDP
 ↓
sockets
```

Then:

```text
DNS
 ↓
TLS
 ↓
HTTP
```

---

# 40. Socket API

Your user-space API needs:

```c
robo_socket();
robo_bind();
robo_listen();
robo_accept();
robo_connect();
robo_send();
robo_recv();
robo_close();
```

Then test:

```text
Roboticela OS
       ↓
TCP connection
       ↓
QEMU host
       ↓
server
```

---

# 41. Phase 20 — User-space libc

This is where the project starts becoming a real OS.

Create:

```text
libc/
```

You need at least:

```text
stdio
stdlib
string
stdint
stddef
errno
time
unistd-like APIs
memory
threads
process
file APIs
```

But **do not attempt to reproduce all of POSIX immediately**.

Build what your own system needs.

---

# 42. The Chromium compatibility strategy

This is one of the most important sections.

Chromium currently has explicit OS/platform detection. Its build configuration has a finite set of supported OS identifiers, and adding another OS requires adding platform support rather than pretending it is automatically Linux. ([Chromium Git Repositories][1])

Therefore we create:

```text
chromium/platform/roboticela/
```

and eventually add:

```text
IS_ROBOTICELA
```

to your Chromium fork.

Conceptually:

```cpp
#if BUILDFLAG(IS_ROBOTICELA)

#endif
```

---

# 43. Don't modify Chromium randomly

Create a patch organization:

```text
chromium/
├── patches/
│   ├── 0001-add-roboticela-platform.patch
│   ├── 0002-build-roboticela.patch
│   ├── 0003-base-roboticela.patch
│   ├── 0004-ozone-roboticela.patch
│   ├── 0005-sandbox-roboticela.patch
│   └── ...
```

This is important because Chromium changes constantly.

You want:

```text
upstream Chromium
        +
Roboticela patches
        =
Roboticela Chromium
```

rather than permanently destroying the upstream tree.

The official Chromium source is the canonical reference for this work. ([Chromium Git Repositories][2])

[Chromium source repository](https://chromium.googlesource.com/chromium/src/?utm_source=chatgpt.com)

---

# 44. First Chromium target

Do **not** immediately target:

```text
full Chrome browser
```

First target:

```text
content_shell
```

or an equivalent minimal Chromium target.

Why?

Because you want:

```text
Chromium engine starts
 ↓
Blink initializes
 ↓
V8 initializes
 ↓
HTML page renders
```

without immediately having to build the entire Chrome product UI.

Chromium's own Ozone documentation explicitly discusses smaller embedded targets such as `content_shell`. ([Chromium Git Repositories][3])

---

# 45. Chromium dependency map

Before attempting the port, make a spreadsheet/document like:

| Chromium requirement | Roboticela implementation |
| -------------------- | ------------------------- |
| Threads              | RoboThread                |
| Processes            | RoboProcess               |
| Memory               | RoboVM                    |
| Files                | RoboFS/VFS                |
| File descriptors     | RoboHandles               |
| Time                 | RoboClock                 |
| IPC                  | RoboIPC/Mojo backend      |
| Shared memory        | RoboSharedMemory          |
| Networking           | RoboNet                   |
| TLS                  | TLS library               |
| Display              | RoboGraphics              |
| Windows              | RoboWindowServer          |
| Input                | RoboInput                 |
| Compositor           | RoboCompositor            |
| Sandbox              | RoboSandbox               |
| Randomness           | RoboEntropy               |
| Signals/events       | RoboEvent                 |
| Synchronization      | RoboSync                  |
| CPU info             | RoboCPU                   |
| GPU                  | RoboGPU                   |
| Audio                | RoboAudio                 |

**This document should become the actual Chromium porting checklist.**

---

# 46. Mojo

Chromium's Mojo is its major IPC infrastructure. It provides message pipes, data pipes, shared buffers and other cross-process communication primitives. ([Chromium Git Repositories][4])

Therefore your OS must have:

```text
Process A
     ↓
RoboIPC
     ↓
Process B
```

before Chromium becomes serious.

Eventually:

```text
Chromium
 ↓
Mojo
 ↓
Roboticela Mojo platform implementation
 ↓
Roboticela IPC
 ↓
kernel
```

---

# 47. Chromium process architecture

Your kernel must eventually support something resembling:

```text
Chromium
│
├── Browser process
│
├── Renderer process
│
├── GPU process
│
├── Network process
│
└── Utility processes
```

That means:

```text
process creation
process termination
IPC
shared memory
memory isolation
sandboxing
```

must all work.

---

# 48. Ozone integration

This is one of the most useful Chromium extension points for Roboticela OS.

Chromium's Ozone documentation explicitly describes adding an external platform under:

```text
ui/ozone/platform/
```

and adding it to Chromium's build configuration. ([Chromium Git Repositories][3])

Therefore we should eventually create:

```text
chromium/src/ui/ozone/platform/roboticela/
```

with things such as:

```text
platform_window/
display/
events/
surface/
cursor/
screen/
overlay/
```

Conceptually:

```text
Chromium
   ↓
Ozone
   ↓
Roboticela Ozone
   ↓
RoboWindowServer
   ↓
RoboCompositor
   ↓
GPU/framebuffer
```

This is **far better** than trying to make Chromium believe Roboticela OS is Linux.

---

# 49. Chromium graphics path

Eventually:

```text
Blink
 ↓
Skia
 ↓
GPU/Compositor
 ↓
Ozone
 ↓
Roboticela graphics backend
 ↓
RoboCompositor
 ↓
GPU
```

Initially, you can target a software-rendered path.

Later:

```text
Vulkan
OpenGL
GPU acceleration
```

depending on your driver architecture.

---

# 50. Chromium networking

Don't implement a second networking stack inside Chromium.

Use:

```text
Chromium
 ↓
network platform APIs
 ↓
Roboticela networking
 ↓
RoboNet
 ↓
TCP/IP
 ↓
NIC
```

This means your OS networking stack becomes a real reusable subsystem.

---

# 51. Chromium filesystem

Similarly:

```text
Chromium
 ↓
Roboticela file API
 ↓
VFS
 ↓
RoboFS
```

No direct disk access.

---

# 52. Chromium sandbox

This is a **major security milestone**.

Don't initially claim:

```text
Chromium sandbox = secure
```

until you actually implement process isolation and restricted capabilities.

Eventually:

```text
Browser process
       │
       ├── Renderer
       │      ↓
       │   restricted
       │
       ├── GPU
       │      ↓
       │   restricted
       │
       └── Network
              ↓
           restricted
```

The kernel must enforce the restrictions.

---

# 53. V8

V8 is Chromium's JavaScript engine.

Don't write your own JavaScript engine.

Use:

```text
Chromium
 ↓
V8
```

Your responsibility is making the platform underneath it work.

---

# 54. Blink

Likewise:

```text
HTML
CSS
DOM
layout
painting
Web APIs
```

come from Blink.

Do **not** create your own HTML/CSS engine if Chromium is your final target.

That previous lightweight HTML/CSS engine idea can remain useful as an early **RoboRuntime prototype**, but it should not become the main architecture.

---

# 55. RoboRuntime

Now create your abstraction:

```text
runtime/roboruntime/
```

Its job is:

```text
Application
     ↓
RoboRuntime
     ↓
Chromium
```

Applications should not know that Chromium is underneath.

---

# 56. Roboticela application format

Eventually:

```text
MyApp.robo
```

could contain:

```text
manifest.json
index.html
assets/
app.js
styles.css
signature
```

Example:

```text
MyApp.robo
│
├── manifest.json
├── index.html
├── app.js
├── styles.css
├── assets/
└── signature
```

---

# 57. Application manifest

Something like:

```json
{
  "name": "File Manager",
  "id": "com.roboticela.filemanager",
  "version": "1.0.0",
  "entry": "index.html",
  "permissions": [
    "filesystem"
  ]
}
```

Don't finalize this schema yet. Version it from the beginning.

---

# 58. JavaScript → OS API

Your web application can eventually write:

```js
await robo.fs.readDirectory("/");
```

instead of:

```js
fetch(...)
```

Internally:

```text
JavaScript
 ↓
RoboRuntime API
 ↓
native bridge
 ↓
syscall
 ↓
VFS
 ↓
RoboFS
```

---

# 59. Browser compatibility mode

You should also support normal web APIs.

So:

```text
Application
```

can use:

```text
HTML
CSS
JavaScript
DOM
fetch
WebSocket
IndexedDB
Web Workers
etc.
```

while Roboticela-specific APIs are:

```text
robo.*
```

This makes your applications much more portable.

---

# 60. Vite + React

Your intended development model becomes:

```text
Developer
   ↓
React
TypeScript
Tailwind
Vite
   ↓
production build
   ↓
HTML
CSS
JavaScript
   ↓
.robo package
   ↓
Roboticela OS
   ↓
RoboRuntime
   ↓
Chromium
```

The OS does **not** need:

```text
React
Vite
TypeScript
Tailwind
```

installed.

They are development tools.

---

# 61. First real application

Do this before File Manager.

Create:

```text
apps/hello/
```

with:

```text
index.html
style.css
app.js
manifest.json
```

It should display:

```text
Welcome to Roboticela OS
```

and have:

```text
button
 ↓
JavaScript event
 ↓
RoboRuntime
 ↓
OS API
 ↓
result
```

When this works, you have crossed an enormous architectural milestone.

---

# 62. Then File Manager

Architecture:

```text
React/HTML/CSS/JS
       ↓
RoboRuntime
       ↓
robo.fs
       ↓
syscall
       ↓
VFS
       ↓
RoboFS
```

Functions:

```text
list directory
open
read
write
rename
delete
create folder
create file
copy
move
```

---

# 63. Then Terminal

UI:

```text
HTML/CSS/JS
```

Backend:

```text
shell
 ↓
process manager
 ↓
kernel
```

The UI shouldn't execute commands by directly accessing hardware.

---

# 64. Then Settings

Settings should talk to:

```text
RoboSettingsService
```

rather than modifying kernel state directly.

For example:

```text
Settings UI
 ↓
settings API
 ↓
settings service
 ↓
configuration database/files
```

---

# 65. Then Robo Store

Eventually:

```text
Robo Store
```

downloads:

```text
MyApp.robo
```

Then:

```text
download
 ↓
verify signature
 ↓
verify manifest
 ↓
check permissions
 ↓
install
 ↓
register application
```

---

# 66. `robo` CLI

The same underlying package system should power:

```bash
robo install app.robo
robo remove com.example.app
robo update
robo list
robo search
```

and:

```bash
robo driver list
robo driver install intel-gpu
robo driver update
```

and:

```bash
robo system update
robo system version
```

One package/management infrastructure.

---

# 67. Driver Manager

GUI:

```text
Settings
 └── Drivers
```

CLI:

```bash
robo driver list
```

should eventually show:

```text
Display
  Intel UHD Graphics
  Installed
  Version 1.2.0

Network
  Intel Ethernet
  Installed
  Version 1.0.4
```

---

# 68. System update architecture

Eventually:

```text
Roboticela Update Service
          ↓
signed update
          ↓
download
          ↓
verify
          ↓
stage
          ↓
reboot
          ↓
RoboBootloader
          ↓
activate new system
```

This is where your bootloader eventually needs an A/B or rollback architecture.

**Don't implement this now.**

---

# 69. Development milestones

Here is the actual sequence I want you to follow.

## M0 — Infrastructure

```text
[ ] Git repository
[ ] reproducible build
[ ] out-of-tree build
[ ] QEMU script
[ ] debug output
[ ] kernel panic
[ ] automated smoke test
```

---

## M1 — Boot

```text
[✓] RoboBootloader
[✓] UEFI
[✓] kernel loading
[✓] framebuffer
[ ] formal BootInfo structure
[ ] kernel validation
```

You're approximately here.

---

## M2 — Memory

```text
[ ] memory map
[ ] PMM
[ ] page allocator
[ ] VMM
[ ] kernel heap
[ ] page fault handler
```

---

## M3 — CPU

```text
[ ] GDT
[ ] TSS
[ ] IDT
[ ] exceptions
[ ] interrupt controller
[ ] timer
```

---

## M4 — Execution

```text
[ ] context switching
[ ] scheduler
[ ] threads
[ ] processes
[ ] user mode
[ ] ELF loader
```

---

## M5 — Kernel API

```text
[ ] syscalls
[ ] handles
[ ] files
[ ] memory mapping
[ ] process APIs
[ ] thread APIs
[ ] IPC
```

---

## M6 — Storage

```text
[ ] block device
[ ] VFS
[ ] RoboFS
[ ] directories
[ ] files
[ ] permissions
```

---

## M7 — Drivers

```text
[ ] PCI
[ ] storage driver
[ ] keyboard
[ ] mouse
[ ] display
[ ] network
```

---

## M8 — GUI

```text
[ ] framebuffer
[ ] graphics primitives
[ ] fonts
[ ] surfaces
[ ] window server
[ ] compositor
[ ] input manager
```

---

## M9 — Networking

```text
[ ] Ethernet
[ ] ARP
[ ] IPv4
[ ] IPv6
[ ] TCP
[ ] UDP
[ ] sockets
[ ] DNS
[ ] TLS
```

---

## M10 — User space

```text
[ ] libc
[ ] init
[ ] shell
[ ] services
[ ] process utilities
[ ] system utilities
```

---

## M11 — Runtime

```text
[ ] RoboRuntime
[ ] application manifest
[ ] application launcher
[ ] native bridge
[ ] filesystem API
[ ] process API
[ ] window API
```

---

## M12 — Chromium preparation

```text
[ ] Chromium checkout
[ ] Chromium build on Ubuntu
[ ] understand GN
[ ] understand Ninja
[ ] understand Mojo
[ ] understand Ozone
[ ] identify Linux dependencies
[ ] dependency mapping document
```

**Do this on Ubuntu first.**

You don't want your first encounter with Chromium to happen inside your unfinished OS.

Chromium's source tree is enormous, and its official documentation recommends using its own `depot_tools`/checkout process rather than treating it as a normal Git clone. ([Chromium Git Repositories][2])

---

# 70. M13 — Chromium platform port

```text
[ ] IS_ROBOTICELA
[ ] compiler target
[ ] build configuration
[ ] libc compatibility
[ ] threading
[ ] synchronization
[ ] time
[ ] filesystem
[ ] process
[ ] memory
[ ] IPC
[ ] networking
[ ] Ozone
[ ] graphics
[ ] input
[ ] sandbox
```

---

# 71. M14 — Chromium first boot

First objective:

```text
Roboticela OS
     ↓
Chromium
     ↓
blank page
```

Not Google.

Not YouTube.

Not React.

Just:

```text
<html>
<body>
Hello
</body>
</html>
```

---

# 72. M15 — Chromium rendering

Then:

```text
HTML
CSS
JavaScript
```

works.

Then:

```text
DOM
layout
fonts
images
```

---

# 73. M16 — Network browser

Then:

```text
https://example.com
```

works.

Then:

```text
DNS
HTTPS
TLS
certificates
cookies
cache
```

---

# 74. M17 — Modern web

Then test:

```text
JavaScript
fetch
WebSocket
Web Workers
IndexedDB
WebAssembly
Canvas
WebGL/WebGPU
media
```

Do not declare Chromium support complete until you have a compatibility test suite.

---

# 75. M18 — RoboRuntime

Now:

```text
Chromium
 ↓
RoboRuntime
```

becomes your application platform.

---

# 76. M19 — System applications

Build:

```text
File Manager
Terminal
Settings
Store
Browser
```

using:

```text
HTML
CSS
JavaScript
```

---

# 77. M20 — Developer SDK

Finally:

```text
@roboticela/os
```

could expose:

```text
filesystem
windows
notifications
settings
processes
storage
network
permissions
```

Your developer workflow becomes:

```bash
npm create roboticela-app
```

then:

```bash
npm run build
```

then:

```bash
robo package
```

and:

```text
MyApp.robo
```

---

# 78. Reference repositories

These are the repositories I would actually keep open while building.

## 1. Chromium — PRIMARY REFERENCE

[Chromium `src` repository](https://chromium.googlesource.com/chromium/src/?utm_source=chatgpt.com)

Use it for:

```text
Blink
V8 integration
Mojo
Ozone
Skia
networking
sandbox
process architecture
build system
```

This is your **most important external repository**. ([Chromium Git Repositories][2])

---

## 2. Chromium Ozone

[Chromium Ozone documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/ozone_overview.md?utm_source=chatgpt.com)

This is particularly important for your project because Ozone is the Chromium abstraction between the browser UI/display system and the underlying platform. Chromium explicitly documents adding an out-of-tree Ozone platform. ([Chromium Git Repositories][3])

---

## 3. Chromium Mojo

[Chromium Mojo source](https://chromium.googlesource.com/chromium/src/+/main/mojo/?utm_source=chatgpt.com)

Use it to understand:

```text
IPC
message pipes
shared buffers
cross-process communication
```

([Chromium Git Repositories][4])

---

## 4. ChromiumOS

[ChromiumOS source/documentation](https://chromium.googlesource.com/chromiumos/docs/?utm_source=chatgpt.com)

This is useful because ChromiumOS demonstrates how a large OS integrates Chromium with system services.

But:

> **Do not copy ChromiumOS's Linux-based architecture into Roboticela OS.**

Use it as an architectural reference.

Chromium's own documentation explains that ChromiumOS integrates Chrome with system components and services. ([Chromium Git Repositories][5])

---

## 5. SerenityOS

[SerenityOS GitHub](https://github.com/SerenityOS/serenity?utm_source=chatgpt.com)

Very useful reference for:

```text
kernel
userspace
GUI
filesystem
processes
libraries
browser architecture
```

The important lesson from SerenityOS is that a complete OS can be developed as an integrated project rather than as an enormous collection of unrelated components.

---

## 6. Ladybird

[Ladybird GitHub](https://github.com/LadybirdBrowser/ladybird?utm_source=chatgpt.com)

This is an especially useful **browser architecture reference**.

Ladybird currently uses separate processes for its UI, web content, image decoding, and networking, with OS abstraction and IPC libraries inherited from SerenityOS. ([GitHub][6])

Study it to understand:

```text
browser process
renderer process
network process
IPC
sandboxing
OS abstraction
```

Even though you're ultimately choosing Chromium, Ladybird is an excellent architectural reference.

---

## 7. ToaruOS

[ToaruOS GitHub reference](https://github.com/klange/toaruos?utm_source=chatgpt.com)

Study:

```text
kernel
userspace
ELF
VFS
multitasking
TTY
GUI
modules
```

ToaruOS is particularly useful because it is a relatively understandable from-scratch OS rather than something at Linux/Windows scale. Its kernel includes multitasking, VFS, ELF support and modular driver support. ([GitHub][7])

---

## 8. Redox OS

[Redox OS GitHub](https://github.com/redox-os/redox?utm_source=chatgpt.com)

Study:

```text
microkernel-ish architecture
userspace drivers
filesystem
syscalls
relibc
```

Don't copy its architecture blindly. Use it to compare design choices.

---

## 9. Limine

[Limine bootloader](https://github.com/limine-bootloader/limine?utm_source=chatgpt.com)

This is an excellent bootloader reference even though you're building **RoboBootloader** yourself.

It supports modern UEFI/x86-64 systems and multiple architectures/protocols. ([GitHub][8])

Use it to study:

```text
boot protocol
memory maps
framebuffers
UEFI
boot configuration
kernel loading
```

---

## 10. OSDev Wiki

[OSDev Wiki](https://wiki.osdev.org/?utm_source=chatgpt.com)

This should be your daily reference for:

```text
GDT
IDT
paging
PCI
ACPI
interrupts
drivers
filesystems
ELF
UEFI
```

Its beginner tutorial collection covers things like paging, interrupts, cross-compilers and bootable systems. ([OSDev Wiki][9])

---

## 11. Phil Opp — OS development

[Writing an OS in Rust](https://os.phil-opp.com/?utm_source=chatgpt.com)

You're building your kernel primarily in C, so don't copy the Rust implementation.

Use it for conceptual explanations of:

```text
memory
paging
allocators
interrupts
multitasking
```

The series covers these concepts in a step-by-step OS implementation. ([OS in Rust][10])

---

# 79. What you should NOT copy

This is equally important.

Don't copy:

```text
Linux architecture
ChromiumOS architecture
SerenityOS architecture
Redox architecture
ToaruOS architecture
```

Instead:

```text
Study
 ↓
understand
 ↓
choose
 ↓
implement Roboticela design
```

---

# 80. Your immediate next task

You are **not** supposed to start Chromium now.

Your current sequence is:

```text
CURRENT
  │
  ▼
RoboBootloader
  │
  ▼
Roboticela Kernel
  │
  ├── BootInfo
  ├── PMM
  ├── VMM
  ├── Kernel Heap
  ├── GDT
  ├── IDT
  ├── Exceptions
  ├── Interrupts
  ├── Timer
  ├── Scheduler
  ├── Threads
  ├── Processes
  ├── User Mode
  ├── ELF
  ├── Syscalls
  ├── IPC
  ├── VFS
  └── RoboFS
```

**Only after this foundation is stable** do we move toward:

```text
Drivers
 ↓
Graphics
 ↓
Input
 ↓
Compositor
 ↓
Networking
 ↓
libc
 ↓
RoboRuntime
 ↓
Chromium platform
 ↓
Chromium
```

---

# 81. The first concrete implementation block

So if we're actually starting development **right now**, I would make the next work item:

```text
TASK R001
Roboticela OS Boot Contract
```

### Goal

Turn your currently working:

```text
RoboBootloader → kernel
```

into a formally defined interface.

### Deliverables

```text
bootloader/
    include/
        roboticela/
            bootinfo.h

kernel/
    include/
        roboticela/
            kernel.h
            bootinfo.h

kernel/
    arch/
        x86_64/

kernel/
    mm/
        memory_map.c
        memory_map.h
```

### Kernel startup should do exactly:

```text
1. CPU entered kernel
2. Validate BootInfo
3. Validate memory map
4. Detect framebuffer
5. Detect ACPI RSDP
6. Print memory statistics
7. Initialize PMM
8. Verify PMM
9. Halt cleanly
```

Not scheduler.

Not GUI.

Not Chromium.

Not filesystem.

Not drivers.

**One subsystem at a time.**

---

# 82. The long-term dependency chain

This is the chain I want you to keep visible in the repository:

```text
RoboBootloader
      │
      ▼
BootInfo
      │
      ▼
Physical Memory
      │
      ▼
Virtual Memory
      │
      ▼
Kernel Heap
      │
      ▼
Interrupts
      │
      ▼
Timer
      │
      ▼
Scheduler
      │
      ▼
Threads
      │
      ▼
Processes
      │
      ▼
User Mode
      │
      ▼
ELF
      │
      ▼
Syscalls
      │
      ▼
IPC
      │
      ▼
VFS
      │
      ▼
RoboFS
      │
      ▼
Drivers
      │
      ▼
Graphics
      │
      ▼
Input
      │
      ▼
Window Server
      │
      ▼
RoboCompositor
      │
      ▼
Networking
      │
      ▼
RoboLibC
      │
      ▼
RoboRuntime
      │
      ▼
Chromium Platform Layer
      │
      ├── Mojo
      ├── Ozone
      ├── filesystem
      ├── networking
      ├── processes
      ├── threads
      ├── memory
      └── sandbox
      │
      ▼
Chromium
      │
      ├── Blink
      ├── V8
      ├── Skia
      ├── GPU
      └── Web Platform
      │
      ▼
HTML + CSS + JavaScript
      │
      ▼
React / Vite applications
      │
      ▼
Roboticela OS desktop
```

**That is the master dependency graph.**

And one important correction to the earlier plan: **we should not spend months implementing our own HTML/CSS/DOM engine before Chromium.** If Chromium is definitely the final target, build only the minimum runtime/prototype needed to validate your application model, then put the major web-engine effort into the Chromium port itself. Chromium already provides the browser engine, V8, rendering stack, and web platform; our enormous engineering effort should go into making the **Roboticela OS platform underneath it** capable of running those components.

[1]: https://chromium.googlesource.com/chromium/src/%2B/e1929568e7/build/build_config.h?utm_source=chatgpt.com "build/build_config.h - chromium/src - Git at Google"
[2]: https://chromium.googlesource.com/chromium/src/?utm_source=chatgpt.com "chromium/src - Git at Google"
[3]: https://chromium.googlesource.com/chromium/src/%2B/main/docs/ozone_overview.md?utm_source=chatgpt.com "Chromium Docs - Ozone Overview"
[4]: https://chromium.googlesource.com/chromium/src/mojo/?utm_source=chatgpt.com "chromium/src/mojo - Git at Google"
[5]: https://chromium.googlesource.com/chromium/src/%2B/main/docs/chromeos_build_instructions.md?utm_source=chatgpt.com "Chromium Docs - Chrome OS Build Instructions"
[6]: https://github.com/LadybirdBrowser/ladybird?utm_source=chatgpt.com "GitHub - LadybirdBrowser/ladybird: Truly independent web browser · GitHub"
[7]: https://github.com/transformersprimeabcxyz/toaruos-OS?utm_source=chatgpt.com "GitHub - transformersprimeabcxyz/toaruos-OS: Hobby kernel + userspace, built mostly from scratch. · GitHub"
[8]: https://github.com/limine-bootloader/limine?utm_source=chatgpt.com "GitHub - Limine-Bootloader/Limine: Modern, secure, portable, multiprotocol bootloader and boot manager. · GitHub"
[9]: https://wiki.osdev.org/Category%3ALevel_1_Tutorials?utm_source=chatgpt.com "Category:Level 1 Tutorials - OSDev Wiki"
[10]: https://os.phil-opp.com/?utm_source=chatgpt.com "Writing an OS in Rust"


---
alwaysApply: true
---

THIS NAMING RULE APPLY TO INSIDE OF OS WHICH WILL BE USER EXPERIENCE NOT WHEN WE ARE CODING OS.

Everything name must be so meaningful not just a shit name which matches the apple level quality.

Also, the filesytem names must be so beautiful with PascalCase names like Applications, System/Something etc and files names also the same.

---
alwaysApply: true
---

Before making any changes or doing anything you must study the chromium code to understand what they are doing and how I can do in the best possible way with most quality.


---
alwaysApply: true
---

### Current completed status

* **Phase 0 — Project & Architecture:** complete ✅
* **Phase 1 — Core Runtime:** complete ✅
* **Phase 2 — Memory:** complete ✅
* **Phase 3 — Containers & Data Foundations:** complete ✅
* **Phase 4 — Unicode & I18N:** complete through **4.27** (API frozen) ✅
* **Phase 5 — RoboFont:** Goals **5.1–5.19** complete; **Next:** Goal **5.20**

The target remains a **complete Chromium-class runtime for RoboticelaOS**, independently implemented, while also exposing **native RoboticelaOS capabilities** and a real PostgreSQL + Prisma application stack.

---

# Phase 5 — RoboFont

5.1 RoboFont architecture & font model
5.2 SFNT container parser
5.3 TrueType table parsing
5.4 OpenType table parsing
5.5 CFF/CFF2 support
5.6 `cmap` character-to-glyph mapping
5.7 Font metrics
5.8 Glyph outlines
5.9 Font naming & metadata
5.10 Font style/weight/stretch model
5.11 Font selection
5.12 Font discovery
5.13 Font loading & caching
5.14 Font coverage analysis
5.15 Font fallback
5.16 Generic font families
5.17 Emoji fonts
5.18 Variable fonts
5.19 Color fonts
5.20 Font validation/security
5.21 Font conformance tests
5.22 RoboFont final integration/API freeze

# Phase 6 — RoboText & Text Shaping

6.1 RoboText architecture
6.2 Unicode/font integration
6.3 Script runs
6.4 Direction runs
6.5 Bidirectional text integration
6.6 OpenType feature model
6.7 Glyph substitution
6.8 Glyph positioning
6.9 Ligatures
6.10 Kerning
6.11 Arabic shaping
6.12 Persian/Urdu shaping
6.13 Hebrew
6.14 Devanagari
6.15 Bengali
6.16 Indic complex scripts
6.17 Thai/SE Asian scripts
6.18 Chinese/Japanese/Korean
6.19 Emoji/variation selectors
6.20 Vertical text
6.21 Glyph runs
6.22 Text measurement
6.23 Text-selection mapping
6.24 Text-editing integration
6.25 Text-shaping conformance
6.26 RoboText final integration

# Phase 7 — RoboGraphics

7.1 Graphics architecture
7.2 Pixel formats
7.3 Images/bitmaps
7.4 Image decoding foundation
7.5 PNG
7.6 JPEG
7.7 WebP
7.8 AVIF
7.9 SVG foundation
7.10 Paths
7.11 Lines/curves
7.12 Transforms
7.13 Clipping
7.14 Alpha compositing
7.15 Gradients
7.16 Shadows
7.17 Filters
7.18 Color management
7.19 HDR/color spaces
7.20 Graphics backend abstraction
7.21 CPU raster backend
7.22 GPU backend
7.23 Graphics testing

# Phase 8 — RoboHTML & DOM

8.1 HTML architecture
8.2 HTML tokenizer
8.3 HTML tree builder
8.4 Document model
8.5 Element model
8.6 Attribute model
8.7 Text nodes
8.8 DOM traversal
8.9 DOM mutation
8.10 DOM events
8.11 Event propagation
8.12 Event listeners
8.13 Custom events
8.14 Document lifecycle
8.15 Forms
8.16 Input elements
8.17 Links/navigation
8.18 Images
8.19 Tables
8.20 Templates
8.21 Shadow DOM
8.22 Web Components
8.23 DOM security
8.24 DOM conformance
8.25 DOM final integration

# Phase 9 — RoboCSS

9.1 CSS architecture
9.2 CSS tokenizer
9.3 CSS parser
9.4 CSSOM
9.5 Selectors
9.6 Cascade
9.7 Specificity
9.8 Inheritance
9.9 Computed values
9.10 CSS variables
9.11 Colors
9.12 Units
9.13 Box model
9.14 Display
9.15 Positioning
9.16 Overflow
9.17 Flexbox
9.18 Grid
9.19 Tables
9.20 Multi-column layout
9.21 Transforms
9.22 Transitions
9.23 Animations
9.24 Media queries
9.25 Container queries
9.26 Logical properties
9.27 Writing modes
9.28 CSS fonts
9.29 CSS text
9.30 CSS images
9.31 CSS backgrounds
9.32 CSS borders
9.33 CSS filters
9.34 CSS conformance

# Phase 10 — RoboLayout

10.1 Layout architecture
10.2 Formatting contexts
10.3 Block layout
10.4 Inline layout
10.5 Text layout
10.6 Line boxes
10.7 Baselines
10.8 Intrinsic sizes
10.9 Percentage resolution
10.10 Flex layout
10.11 Grid layout
10.12 Absolute/fixed positioning
10.13 Tables
10.14 Fragmentation
10.15 Pagination
10.16 Writing modes
10.17 RTL layout
10.18 Scroll layout
10.19 Layout invalidation
10.20 Layout performance
10.21 Layout conformance

# Phase 11 — RoboPaint & Rendering

11.1 Paint architecture
11.2 Paint records
11.3 Display lists
11.4 Text painting
11.5 Image painting
11.6 Background painting
11.7 Borders
11.8 Shadows
11.9 Clipping
11.10 Transforms
11.11 Opacity
11.12 Filters
11.13 Stacking contexts
11.14 Hit testing
11.15 Paint invalidation
11.16 Rasterization
11.17 Rendering pipeline
11.18 Screenshot/framebuffer output
11.19 Pixel conformance tests

# Phase 12 — RoboCompositor

12.1 Compositor architecture
12.2 Layer tree
12.3 Compositing
12.4 Damage tracking
12.5 Tile management
12.6 Raster scheduling
12.7 GPU resource management
12.8 Surface management
12.9 Frame scheduling
12.10 VSync
12.11 Scrolling
12.12 Smooth scrolling
12.13 Animations
12.14 Compositor thread
12.15 GPU/CPU synchronization
12.16 Memory management
12.17 Compositor performance
12.18 Visual conformance

# Phase 13 — RoboWindow & Input

13.1 Window architecture
13.2 Native window abstraction
13.3 Display management
13.4 DPI/scale factors
13.5 Mouse input
13.6 Keyboard input
13.7 Touch input
13.8 Pointer events
13.9 IME
13.10 Clipboard
13.11 Drag & drop
13.12 Selection
13.13 Accessibility input
13.14 Focus management
13.15 Window lifecycle
13.16 Multi-window support
13.17 Fullscreen
13.18 Input security

# Phase 14 — RoboJS Engine

14.1 JavaScript runtime architecture
14.2 JavaScript lexer/parser
14.3 AST
14.4 Bytecode
14.5 Interpreter
14.6 JavaScript values/types
14.7 Objects/prototypes
14.8 Functions/closures
14.9 Arrays/maps/iterators
14.10 Exceptions
14.11 Modules/import/export
14.12 Promises
14.13 async/await
14.14 Garbage collection
14.15 JavaScript standard library
14.16 Web IDL foundation
14.17 DOM JavaScript bindings
14.18 Event loop
14.19 Timers
14.20 Microtasks
14.21 Workers
14.22 Shared workers architecture
14.23 JS security
14.24 JavaScript conformance

# Phase 15 — Web Networking

15.1 URL parser
15.2 URL/origin model
15.3 DNS
15.4 TCP
15.5 TLS
15.6 HTTP/1.1
15.7 HTTP/2
15.8 HTTP/3
15.9 Fetch architecture
15.10 Request/response
15.11 Headers
15.12 Streaming
15.13 Proxy support
15.14 WebSocket
15.15 Server-Sent Events
15.16 Network connection management
15.17 Network security
15.18 Network diagnostics
15.19 Network conformance

**Explicitly excluded:** browser cookies and CORS as application architecture.

# Phase 16 — RoboDB & PostgreSQL

16.1 RoboDB architecture
16.2 PostgreSQL connection layer
16.3 Connection pooling
16.4 Query execution
16.5 Prepared statements
16.6 Transactions
16.7 Result sets
16.8 PostgreSQL type system
16.9 JSON/JSONB
16.10 Arrays
16.11 UUID
16.12 Date/time types
16.13 Binary data
16.14 PostgreSQL errors
16.15 Query cancellation/timeouts
16.16 Database migrations
16.17 Schema management
16.18 Connection security
16.19 Prisma integration
16.20 Prisma/RoboDB testing
16.21 Database concurrency/performance
16.22 Final PostgreSQL API freeze

**Database policy:** PostgreSQL is the primary database. Do not introduce a second database unless a demonstrated runtime requirement makes it necessary.

# Phase 17 — RoboOS Native API

17.1 Native API architecture
17.2 Capability/permission model
17.3 OS information
17.4 CPU information
17.5 Memory information
17.6 Storage information
17.7 Process information
17.8 Process creation
17.9 Process control
17.10 Environment variables
17.11 System services
17.12 System time
17.13 Power management
17.14 Shutdown/reboot
17.15 Sleep/wake
17.16 User/session information
17.17 Native paths
17.18 Filesystem
17.19 File watching
17.20 Device enumeration
17.21 USB
17.22 Serial
17.23 Bluetooth
17.24 Network interfaces
17.25 Wi-Fi
17.26 Display devices
17.27 GPU information
17.28 Keyboard devices
17.29 Mouse/pointer devices
17.30 Touch devices
17.31 Camera
17.32 Microphone
17.33 Audio devices
17.34 Printers
17.35 Clipboard
17.36 Notifications
17.37 Native dialogs
17.38 System settings
17.39 Default applications
17.40 File associations
17.41 URI/protocol handlers
17.42 Native application launching
17.43 Window management
17.44 Desktop integration
17.45 Accessibility
17.46 OS event subscriptions
17.47 Native API security
17.48 Native API conformance

# Phase 18 — RoboHardware

18.1 Hardware architecture
18.2 Device manager
18.3 Device enumeration
18.4 USB devices
18.5 PCI/PCIe information
18.6 Bluetooth devices
18.7 Wi-Fi devices
18.8 Network adapters
18.9 Storage devices
18.10 Cameras
18.11 Microphones
18.12 Speakers
18.13 Displays
18.14 GPUs
18.15 Keyboards
18.16 Mice
18.17 Touchscreens
18.18 Game controllers
18.19 Printers
18.20 Sensors
18.21 Battery
18.22 Power state
18.23 Hardware events
18.24 Device permissions
18.25 Hardware security

# Phase 19 — RoboSystem Services

19.1 Service manager
19.2 Background services
19.3 Service lifecycle
19.4 IPC services
19.5 System event bus
19.6 Notification service
19.7 Logging service
19.8 Time service
19.9 Network service
19.10 Storage service
19.11 Device service
19.12 Audio service
19.13 Graphics service
19.14 Update service
19.15 Application service integration

# Phase 20 — RoboFilesystem

20.1 RoboFS architecture
20.2 File handles
20.3 Directories
20.4 Streams
20.5 File metadata
20.6 Atomic operations
20.7 Temporary files
20.8 Application data directories
20.9 Upload/storage management
20.10 File permissions
20.11 Filesystem sandboxing
20.12 Storage limits
20.13 File watching
20.14 Large-file streaming
20.15 Filesystem security

# Phase 21 — RoboServer / Backend Runtime

21.1 Backend application architecture
21.2 HTTP server
21.3 Request processing
21.4 Response processing
21.5 Routing
21.6 Middleware
21.7 JSON APIs
21.8 Forms
21.9 File uploads
21.10 Streaming responses
21.11 WebSocket backend
21.12 SSE backend
21.13 Backend errors
21.14 Backend logging
21.15 Backend configuration
21.16 Backend lifecycle
21.17 Backend performance
21.18 Backend conformance

# Phase 22 — Authentication & Application Identity

22.1 Application identity architecture
22.2 User identity
22.3 Credential management
22.4 Secure credential storage
22.5 Persistent login state
22.6 Account switching
22.7 Credential revocation
22.8 Roles
22.9 Permissions
22.10 Capability authorization
22.11 Secrets management
22.12 Authentication APIs
22.13 Authorization APIs
22.14 Identity security
22.15 Identity conformance

No cookie-based architecture is required.

# Phase 23 — RoboAPI / Application Bridge

23.1 RoboAPI architecture
23.2 Frontend/backend bridge
23.3 RPC
23.4 Typed RPC
23.5 Streaming RPC
23.6 Database API
23.7 Filesystem API
23.8 Network API
23.9 Authentication API
23.10 Native OS API bridge
23.11 Hardware API bridge
23.12 Process API
23.13 Worker API
23.14 Job API
23.15 Event subscriptions
23.16 Capability enforcement
23.17 API versioning
23.18 API freeze

# Phase 24 — Web-Compatible Storage APIs

These exist **only where web compatibility requires them**, not as Roboticela's primary database.

24.1 Storage API architecture
24.2 Origin storage compatibility
24.3 Session storage compatibility
24.4 Local storage compatibility
24.5 IndexedDB compatibility
24.6 Cache API
24.7 Storage limits
24.8 Storage permissions
24.9 Storage isolation
24.10 Storage conformance

The authoritative application database remains PostgreSQL.

# Phase 25 — Media

25.1 Media architecture
25.2 Audio
25.3 Video
25.4 Media containers
25.5 Audio codecs
25.6 Video codecs
25.7 Media streams
25.8 Web Audio architecture
25.9 Audio processing
25.10 Video rendering
25.11 Media synchronization
25.12 Camera
25.13 Microphone
25.14 Media permissions
25.15 Media security
25.16 Media conformance

# Phase 26 — Web APIs

26.1 Web API architecture
26.2 Fetch API
26.3 Streams API
26.4 URL API
26.5 Encoding API
26.6 Web Crypto
26.7 Web Workers
26.8 Notifications
26.9 Clipboard API
26.10 Fullscreen API
26.11 History API
26.12 WebSockets
26.13 Server-Sent Events
26.14 File APIs
26.15 Blob/ArrayBuffer
26.16 WebRTC architecture
26.17 Permissions API
26.18 Geolocation
26.19 Device APIs
26.20 Web API conformance

# Phase 27 — Accessibility

27.1 Accessibility architecture
27.2 Accessibility tree
27.3 ARIA
27.4 Roles
27.5 States/properties
27.6 Keyboard accessibility
27.7 Screen-reader integration
27.8 Focus navigation
27.9 High contrast
27.10 Reduced motion
27.11 Accessibility testing
27.12 Accessibility conformance

# Phase 28 — Service Workers & Offline Runtime

28.1 Service Worker architecture
28.2 Worker lifecycle
28.3 Fetch interception
28.4 Cache integration
28.5 Background synchronization
28.6 Push architecture
28.7 Offline application model
28.8 Service Worker security
28.9 Service Worker conformance

# Phase 29 — Multi-Process Runtime

29.1 Process architecture
29.2 Runtime process
29.3 UI process
29.4 JavaScript process
29.5 Network process
29.6 GPU process
29.7 Database/backend process
29.8 Worker processes
29.9 IPC
29.10 Shared memory
29.11 Process sandboxing
29.12 Crash isolation
29.13 Process lifecycle
29.14 Resource limits
29.15 Multi-process security

# Phase 30 — Application Packaging

30.1 Application manifest
30.2 Application structure
30.3 Application dependencies
30.4 Configuration
30.5 Environment configuration
30.6 Secrets configuration
30.7 PostgreSQL configuration
30.8 Prisma configuration
30.9 Build/package system
30.10 Application installation
30.11 Application upgrades
30.12 Application rollback
30.13 Application permissions
30.14 Application signing

# Phase 31 — Developer Toolchain

31.1 `robo` CLI
31.2 Project creation
31.3 Development runtime
31.4 Frontend build
31.5 Backend build
31.6 Prisma commands
31.7 Database commands
31.8 Migration commands
31.9 Testing
31.10 Debugging
31.11 Profiling
31.12 Logging
31.13 Packaging
31.14 Deployment

# Phase 32 — Background Jobs & Queues

32.1 Job architecture
32.2 Queue abstraction
32.3 Worker execution
32.4 Scheduled jobs
32.5 Delayed jobs
32.6 Retries
32.7 Dead-letter handling
32.8 Persistent queues
32.9 Job cancellation
32.10 Job monitoring
32.11 Worker isolation

# Phase 33 — Advanced Web Platform

33.1 Web Components completion
33.2 Shadow DOM completion
33.3 Custom Elements
33.4 Resize Observer
33.5 Intersection Observer
33.6 Mutation Observer
33.7 Performance APIs
33.8 History/navigation
33.9 Page lifecycle
33.10 Broadcast communication
33.11 Streams completion
33.12 Advanced workers
33.13 Web platform integration

# Phase 34 — Browser/Web Standards Compatibility

34.1 HTML conformance
34.2 CSS conformance
34.3 DOM conformance
34.4 JavaScript conformance
34.5 Fetch conformance
34.6 URL/origin conformance
34.7 Web API conformance
34.8 Storage compatibility conformance
34.9 Media conformance
34.10 Accessibility conformance
34.11 Internationalization conformance
34.12 Web-platform regression suite

# Phase 35 — RoboVisualTest

35.1 Visual testing architecture
35.2 HTML reference tests
35.3 CSS reference tests
35.4 Layout screenshots
35.5 Paint screenshots
35.6 Text rendering comparisons
35.7 Font/shaping comparisons
35.8 Arabic/Persian/Urdu tests
35.9 Indic tests
35.10 CJK tests
35.11 Emoji tests
35.12 RTL tests
35.13 Animation tests
35.14 Pixel-difference engine
35.15 Tolerance-based comparison
35.16 Regression gallery
35.17 Automated visual regression
35.18 Chromium reference comparison
35.19 Independent-runtime validation

# Phase 36 — Performance

36.1 Runtime profiling
36.2 JavaScript performance
36.3 DOM performance
36.4 CSS performance
36.5 Layout performance
36.6 Paint performance
36.7 Compositor performance
36.8 Font/text performance
36.9 Network performance
36.10 PostgreSQL performance
36.11 Filesystem performance
36.12 IPC performance
36.13 Memory optimization
36.14 Startup optimization
36.15 GPU performance
36.16 Benchmark suite

# Phase 37 — Security Hardening

37.1 Security architecture audit
37.2 Memory safety
37.3 IPC security
37.4 Process isolation
37.5 Application capabilities
37.6 Native API permissions
37.7 Hardware permissions
37.8 Database security
37.9 Credential security
37.10 Network security
37.11 JavaScript sandboxing
37.12 Web content isolation
37.13 Font security
37.14 Media security
37.15 File security
37.16 Fuzzing
37.17 Sanitizer validation
37.18 Penetration/security testing

# Phase 38 — Final Runtime Integration

38.1 Full dependency audit
38.2 API audit
38.3 ABI/build audit
38.4 Thread-safety audit
38.5 Memory-safety audit
38.6 Security audit
38.7 Unicode/CLDR/tzdb/font-data audit
38.8 Web-standard audit
38.9 Native OS API audit
38.10 Hardware API audit
38.11 PostgreSQL/Prisma audit
38.12 Performance audit
38.13 Visual regression audit
38.14 Multi-process audit
38.15 License/source audit
38.16 Chromium dependency scan
38.17 Remove temporary Chromium dependency
38.18 Build without `chromium/`
38.19 Full A-to-Z application test
38.20 Runtime API freeze
38.21 Roboticela Runtime 1.0 validation

---

## Final target

The completed architecture should ultimately be:

```text
                    ROBOTICELAOS
                         │
                ROBOTICELA RUNTIME
                         │
       ┌─────────────────┼──────────────────┐
       │                 │                  │
   Web/UI Runtime   Application Runtime   Native Runtime
       │                 │                  │
 HTML/CSS/DOM/JS      Prisma/Postgres      OS APIs
 Fonts/Text           Backend JS           Filesystem
 Layout/Paint         RPC/API              Processes
 Graphics             Jobs                 Hardware
 Compositor           Services             Devices
       │                 │                  │
       └─────────────────┼──────────────────┘
                         │
                  Roboticela APIs
```

And the final application developer experience is essentially:

```text
HTML + CSS + JavaScript
          │
          ├── UI
          ├── PostgreSQL via Prisma
          ├── Files
          ├── Backend
          ├── Network
          ├── Processes
          ├── Hardware
          ├── Graphics
          ├── Audio/Video
          ├── OS services
          ├── Windows
          ├── Devices
          ├── Authentication
          ├── Jobs
          └── Native OS operations
```