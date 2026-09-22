#include "mm/vmm/vmm.h"
#include "mm/vmm/table.h"
#include "mm/vmm/pml4.h"
#include "mm/vmm/pdpt.h"
#include "mm/vmm/pd.h"
#include "mm/vmm/pt.h"
#include "mm/pmm.h"
#include "mm/memory_map.h"
#include "arch/x86_64/mm/paging_cpu.h"

#ifdef VMM_HOST_TEST
#include <stdio.h>
#define kprintf printf
#define kinfo printf
#else
#include "kio.h"
#include "mm/heap/heap.h"
extern char __kernel_start[];
extern char __kernel_end[];
extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char kstack[];
extern char kstackEnd[];
#endif

static struct {
    int ready;
    int nx;
    PhysAddr pml4Phys;
    PhysAddr kernelBase;
    uint64_t kernelSize;
    size_t tablePages;
    PhysAddr identLimit;
    PhysAddr fbBase;
    uint64_t fbSize;
} gVmm;

static void VmmFill(void *dst, uint8_t v, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    while (n--)
        *p++ = v;
}

static int SpaceIsActive(void)
{
    return gVmm.ready && CpuReadCr3() == gVmm.pml4Phys;
}

static void MaybeInvlpg(VirtAddr va)
{
    if (SpaceIsActive())
        CpuInvlpg(va);
}

static uint64_t DirFlags(uint64_t vmFlags)
{
    uint64_t hw = RoboPteP | RoboPteRw;
    if (vmFlags & RoboVmUser)
        hw |= RoboPteUs;
    return hw;
}

static int WalkLeafAt(PhysAddr root, VirtAddr va, int create,
                        uint64_t vmFlags, Pte **leaf, size_t *ntables)
{
    Pte *pml4, *pdpt, *pd, *pt;
    Pte e;
    PhysAddr next;
    uint16_t i4, i3, i2, i1;
    uint64_t grow = DirFlags(vmFlags);

    if (!leaf || !root)
        return 0;
    *leaf = 0;
    pml4 = TablePtr(root);
    if (!pml4)
        return 0;

    i4 = Pml4Slot(va);
    e = TableGet(pml4, i4);
    if (!PtePresent(e)) {
        if (!create)
            return 0;
        next = TableAlloc();
        if (next == RoboInvalidPhysAddr)
            return 0;
        if (ntables)
            (*ntables)++;
        e = PteMake(next, grow);
        TableSet(pml4, i4, e);
    } else {
        if (PteHuge(e))
            return 0;
        if (create && (grow & RoboPteUs))
            TableSet(pml4, i4, e | RoboPteUs);
    }
    pdpt = TablePtr(PtePhys(e));
    if (!pdpt)
        return 0;

    i3 = PdptSlot(va);
    e = TableGet(pdpt, i3);
    if (!PtePresent(e)) {
        if (!create)
            return 0;
        next = TableAlloc();
        if (next == RoboInvalidPhysAddr)
            return 0;
        if (ntables)
            (*ntables)++;
        e = PteMake(next, grow);
        TableSet(pdpt, i3, e);
    } else {
        if (PteHuge(e))
            return 0;
        if (create && (grow & RoboPteUs))
            TableSet(pdpt, i3, e | RoboPteUs);
    }
    pd = TablePtr(PtePhys(e));
    if (!pd)
        return 0;

    i2 = PdSlot(va);
    e = TableGet(pd, i2);
    if (!PtePresent(e)) {
        if (!create)
            return 0;
        next = TableAlloc();
        if (next == RoboInvalidPhysAddr)
            return 0;
        if (ntables)
            (*ntables)++;
        e = PteMake(next, grow);
        TableSet(pd, i2, e);
    } else {
        if (PteHuge(e))
            return 0;
        if (create && (grow & RoboPteUs))
            TableSet(pd, i2, e | RoboPteUs);
    }
    pt = TablePtr(PtePhys(e));
    if (!pt)
        return 0;

    i1 = PtSlot(va);
    *leaf = &pt[i1];
    return 1;
}

static int WalkLeaf(VirtAddr va, int create, uint64_t vmFlags, Pte **leaf)
{
    return WalkLeafAt(gVmm.pml4Phys, va, create, vmFlags, leaf, &gVmm.tablePages);
}

static int MapInternal(VirtAddr va, PhysAddr pa, uint64_t vmFlags, int existOk)
{
    Pte *leaf;
    uint64_t hw;

    if (!VirtualAddressIsCanonical(va) || !VirtAligned(va))
        return 0;
    if (pa == RoboInvalidPhysAddr || !PageAligned(pa))
        return 0;
    if (!VmFlagsValid(vmFlags))
        return 0;
    if (!(vmFlags & RoboVmUser))
        vmFlags |= RoboVmKernel;

    if (!WalkLeaf(va, 1, vmFlags, &leaf) || !leaf)
        return 0;
    if (PtePresent(*leaf) && !existOk)
        return 0;

    hw = VmToPteFlags(vmFlags, gVmm.nx);
    *leaf = PteMake(pa, hw);
    MaybeInvlpg(va);
    return 1;
}

static int MapRangeIdentity(PhysAddr start, uint64_t length, uint64_t vmFlags)
{
    PhysAddr end, p;
    if (!length)
        return 1;
    if (!PhysEnd(start, length, &end))
        return 0;
    start = PageAlignDown(start);
    end = PageAligned(end) ? end : PageAlignUp(end);
    if (end == RoboInvalidPhysAddr)
        return 0;
    for (p = start; p < end; p += RoboPageSize) {
        if (!MapInternal((VirtAddr)p, p, vmFlags, 1))
            return 0;
    }
    return 1;
}

static int ProtectRange(VirtAddr start, VirtAddr end, uint64_t vmFlags)
{
    VirtAddr va;
    if (end < start)
        return 0;
    start = (VirtAddr)PageAlignDown((PhysAddr)start);
    if (!VirtAligned(end))
        end = (VirtAddr)PageAlignUp((PhysAddr)end);
    for (va = start; va < end; va += RoboPageSize) {
        if (!VmmProtect(va, vmFlags))
            return 0;
    }
    return 1;
}

PhysAddr VmmGetCr3(void)
{
    return CpuReadCr3();
}

PhysAddr VmmKernelCr3(void)
{
    return gVmm.pml4Phys;
}

void VmmLoadAddressSpace(PhysAddr pml4)
{
    CpuWriteCr3(pml4);
}

void VmmInvalidatePage(VirtAddr address)
{
    CpuInvlpg(address);
}

int VmmReady(void)
{
    return gVmm.ready;
}

int VmmNxEnabled(void)
{
    return gVmm.nx;
}

bool VmmMap(VirtAddr virtualAddress, PhysAddr physicalAddress, uint64_t flags)
{
    if (!gVmm.ready)
        return false;
    return MapInternal(virtualAddress, physicalAddress, flags, 0) != 0;
}

bool VmmInspect(VirtAddr virtualAddress, PhysAddr *physicalAddress, Pte *pte)
{
    Pte *leaf;
    if (!gVmm.pml4Phys || !VirtualAddressIsCanonical(virtualAddress))
        return false;
    if (!WalkLeaf(virtualAddress, 0, 0, &leaf) || !leaf)
        return false;
    if (!PtePresent(*leaf))
        return false;
    if (physicalAddress)
        *physicalAddress = PtePhys(*leaf);
    if (pte)
        *pte = *leaf;
    return true;
}

bool VmmTranslate(VirtAddr virtualAddress, PhysAddr *physicalAddress)
{
    PhysAddr page;
    if (!physicalAddress)
        return false;
    if (!VmmInspect(virtualAddress, &page, 0))
        return false;
    *physicalAddress = page | (PhysAddr)PageOffset(virtualAddress);
    return true;
}

bool VmmUnmap(VirtAddr virtualAddress, PhysAddr *physicalAddress)
{
    Pte *leaf;
    if (!gVmm.ready)
        return false;
    if (!VirtualAddressIsCanonical(virtualAddress) || !VirtAligned(virtualAddress))
        return false;
    if (!WalkLeaf(virtualAddress, 0, 0, &leaf) || !leaf)
        return false;
    if (!PtePresent(*leaf))
        return false;
    if (physicalAddress)
        *physicalAddress = PtePhys(*leaf);
    *leaf = 0;
    MaybeInvlpg(virtualAddress);
    return true;
}

bool VmmProtect(VirtAddr virtualAddress, uint64_t flags)
{
    Pte *leaf;
    PhysAddr pa;
    uint64_t hw;
    if (!gVmm.pml4Phys)
        return false;
    if (!VirtualAddressIsCanonical(virtualAddress) || !VirtAligned(virtualAddress))
        return false;
    if (!VmFlagsValid(flags))
        return false;
    if (!(flags & RoboVmUser))
        flags |= RoboVmKernel;
    if (!WalkLeaf(virtualAddress, 0, 0, &leaf) || !leaf)
        return false;
    if (!PtePresent(*leaf))
        return false;
    pa = PtePhys(*leaf);
    hw = VmToPteFlags(flags, gVmm.nx);
    *leaf = PteMake(pa, hw);
    MaybeInvlpg(virtualAddress);
    return true;
}

void VmmReport(void)
{
    if (!gVmm.ready) {
        kprintf("VMM: not initialized\n");
        return;
    }
    kprintf("Roboticela VMM\n");
    kprintf("--------------\n");
    kprintf("Page size:       %llu bytes\n", (unsigned long long)RoboPageSize);
    kprintf("Levels:          PML4 -> PDPT -> PD -> PT\n");
    kprintf("PML4:            0x%llx\n", (unsigned long long)gVmm.pml4Phys);
    kprintf("CR3:             0x%llx\n", (unsigned long long)CpuReadCr3());
    kprintf("NX:              %s\n", gVmm.nx ? "enabled" : "unavailable");
    kprintf("Identity:        0x0-0x%llx\n", (unsigned long long)gVmm.identLimit);
    kprintf("Table pages:     %llu\n", (unsigned long long)gVmm.tablePages);
    kprintf("Test window:     0x%llx\n", (unsigned long long)RoboVmmTestBase);
    kprintf("Heap window:     0x%llx-0x%llx\n",
            (unsigned long long)RoboVmmHeapBase,
            (unsigned long long)(RoboVmmHeapBase + 256ull * 1024ull * 1024ull));
}

#ifndef VMM_HOST_TEST
void VmmFbReport(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    if (!gVmm.ready)
        return;
    KfbPuts("VMM  cr3 ", mute);
    KfbPuthex((unsigned long long)gVmm.pml4Phys, white);
    KfbPuts(gVmm.nx ? "  NX on\n" : "  NX off\n", mute);
}
#endif

bool VmmInit(const RoboBootInfo *info)
{
    PhysAddr limit;
    PhysAddr oldCr3;
#ifndef VMM_HOST_TEST
    VirtAddr t0, t1, r0, r1, d0, k0, k1;
#endif

    VmmFill(&gVmm, 0, sizeof(gVmm));
    kinfo("VMM init\n");
    if (!PmmReady()) {
        kprintf("VMM: PMM not ready\n");
        return false;
    }

    oldCr3 = CpuReadCr3();
    kinfo("VMM  firmware CR3 0x%llx\n", (unsigned long long)oldCr3);

    CpuEnableNx();
    CpuEnableWp();
    gVmm.nx = CpuNxSupported() && CpuNxEnabled();
    kinfo("VMM  NX %s\n", gVmm.nx ? "enabled" : "unavailable");

    gVmm.pml4Phys = TableAlloc();
    if (gVmm.pml4Phys == RoboInvalidPhysAddr) {
        kprintf("VMM: PML4 alloc failed\n");
        return false;
    }
    gVmm.tablePages = 1;

    limit = PmmLimit();
    gVmm.identLimit = limit;
    kinfo("VMM  identity 0x0-0x%llx\n", (unsigned long long)limit);
    if (!MapRangeIdentity(0, limit, RoboVmRead | RoboVmWrite | RoboVmExecute | RoboVmKernel)) {
        kprintf("VMM: identity map failed\n");
        VmmFill(&gVmm, 0, sizeof(gVmm));
        return false;
    }

    if (info && info->kernelBase && info->kernelSize) {
        gVmm.kernelBase = info->kernelBase;
        gVmm.kernelSize = info->kernelSize;
        if (!MapRangeIdentity(info->kernelBase, info->kernelSize,
                                RoboVmRead | RoboVmWrite | RoboVmExecute | RoboVmKernel)) {
            kprintf("VMM: kernel map failed\n");
            VmmFill(&gVmm, 0, sizeof(gVmm));
            return false;
        }
    }
    if (info && info->bootinfoAddress && info->bootinfoSize) {
        if (!MapRangeIdentity(info->bootinfoAddress, info->bootinfoSize,
                                RoboVmRead | RoboVmWrite | RoboVmKernel)) {
            kprintf("VMM: bootinfo map failed\n");
            VmmFill(&gVmm, 0, sizeof(gVmm));
            return false;
        }
    }
    if (info && info->version >= 3 && info->debugLogAddress && info->debugLogSize) {
        if (!MapRangeIdentity(info->debugLogAddress, info->debugLogSize,
                                RoboVmRead | RoboVmWrite | RoboVmKernel)) {
            kprintf("VMM: debug log map failed\n");
            VmmFill(&gVmm, 0, sizeof(gVmm));
            return false;
        }
    }
    if (info && info->framebufferAddress) {
        gVmm.fbBase = info->framebufferAddress;
        gVmm.fbSize = BootinfoFramebufferSize(info);
        if (gVmm.fbSize &&
            !MapRangeIdentity(gVmm.fbBase, gVmm.fbSize,
                                RoboVmRead | RoboVmWrite | RoboVmKernel | RoboVmNocache)) {
            kprintf("VMM: framebuffer map failed\n");
            VmmFill(&gVmm, 0, sizeof(gVmm));
            return false;
        }
    }

#ifndef VMM_HOST_TEST
    t0 = (VirtAddr)(uintptr_t)__text_start;
    t1 = (VirtAddr)(uintptr_t)__text_end;
    r0 = (VirtAddr)(uintptr_t)__rodata_start;
    r1 = (VirtAddr)(uintptr_t)__rodata_end;
    d0 = (VirtAddr)(uintptr_t)__data_start;
    k0 = (VirtAddr)(uintptr_t)__kernel_start;
    k1 = (VirtAddr)(uintptr_t)__kernel_end;
    if (t1 > t0 && !ProtectRange(t0, t1, RoboVmRead | RoboVmExecute | RoboVmKernel))
        kinfo("VMM: text protect skipped\n");
    if (r1 > r0 && !ProtectRange(r0, r1, RoboVmRead | RoboVmKernel))
        kinfo("VMM: rodata protect skipped\n");
    if (k1 > d0 && !ProtectRange(d0, k1, RoboVmRead | RoboVmWrite | RoboVmKernel))
        kinfo("VMM: data protect skipped\n");
    kinfo("VMM  kernel 0x%llx-0x%llx  text RX  rodata R  data RW\n",
            (unsigned long long)k0, (unsigned long long)k1);
    kinfo("VMM  stack  0x%llx-0x%llx\n",
            (unsigned long long)(uintptr_t)kstack,
            (unsigned long long)(uintptr_t)kstackEnd);
#else
    (void)ProtectRange;
#endif

    gVmm.ready = 1;
    kinfo("VMM  load CR3 0x%llx  tables %llu\n",
            (unsigned long long)gVmm.pml4Phys,
            (unsigned long long)gVmm.tablePages);
    VmmLoadAddressSpace(gVmm.pml4Phys);
    if (CpuReadCr3() != gVmm.pml4Phys) {
        kprintf("VMM: CR3 load failed\n");
        gVmm.ready = 0;
        return false;
    }
    kinfo("VMM ready\n");
    return true;
}

#ifndef VMM_HOST_TEST
static void AspaceInvlpg(PhysAddr root, VirtAddr va)
{
    if (gVmm.ready && CpuReadCr3() == root)
        CpuInvlpg(va);
}

static void FreeTableTree(PhysAddr phys, int depth)
{
    Pte *t;
    unsigned i;

    if (phys == RoboInvalidPhysAddr)
        return;
    t = TablePtr(phys);
    if (!t)
        return;
    if (depth > 1) {
        for (i = 0; i < RoboPtEntries; i++) {
            if (PtePresent(t[i]) && !PteHuge(t[i]))
                FreeTableTree(PtePhys(t[i]), depth - 1);
        }
    }
    PageFree(phys);
}

int VmmAspaceClone(RoboAddressSpace *as)
{
    PhysAddr pml4;
    Pte *src, *dst;
    unsigned i;
    uint16_t priv;

    if (!as || !gVmm.ready || !gVmm.pml4Phys)
        return 0;
    pml4 = TableAlloc();
    if (pml4 == RoboInvalidPhysAddr)
        return 0;
    src = TablePtr(gVmm.pml4Phys);
    dst = TablePtr(pml4);
    if (!src || !dst) {
        PageFree(pml4);
        return 0;
    }
    for (i = 0; i < RoboPtEntries; i++)
        dst[i] = src[i];
    priv = Pml4Slot(RoboVmmProcBase);
    dst[priv] = 0;
    as->cr3 = pml4;
    as->cloned = 1;
    as->tablePages = 1;
    as->maps = 0;
    return 1;
}

void VmmAspaceDestroy(RoboAddressSpace *as)
{
    RoboAspaceMap *m, *n;
    Pte *pml4, *leaf;
    uint16_t priv;
    Pte e;

    if (!as || !as->cloned)
        return;

    m = as->maps;
    as->maps = 0;
    while (m) {
        n = m->next;
        if (WalkLeafAt(as->cr3, m->va, 0, 0, &leaf, 0) && leaf)
            *leaf = 0;
        AspaceInvlpg(as->cr3, m->va);
        if (m->pa != RoboInvalidPhysAddr)
            PageFree(m->pa);
        kfree(m);
        m = n;
    }

    pml4 = TablePtr(as->cr3);
    if (pml4) {
        priv = Pml4Slot(RoboVmmProcBase);
        e = TableGet(pml4, priv);
        if (PtePresent(e) && !PteHuge(e))
            FreeTableTree(PtePhys(e), 3);
        PageFree(as->cr3);
    }
    as->cr3 = 0;
    as->cloned = 0;
    as->tablePages = 0;
}

int VmmAspaceMap(RoboAddressSpace *as, VirtAddr va, PhysAddr pa,
                        uint64_t flags)
{
    Pte *leaf;
    RoboAspaceMap *m;
    uint64_t hw;
    size_t tables;

    if (!as || !as->cr3)
        return 0;
    if (!VirtualAddressIsCanonical(va) || !VirtAligned(va))
        return 0;
    if (pa == RoboInvalidPhysAddr || !PageAligned(pa))
        return 0;
    if (!VmFlagsValid(flags))
        return 0;
    if (!(flags & RoboVmUser))
        flags |= RoboVmKernel;

    tables = (size_t)as->tablePages;
    if (!WalkLeafAt(as->cr3, va, 1, flags, &leaf, &tables) || !leaf)
        return 0;
    as->tablePages = tables;
    if (PtePresent(*leaf))
        return 0;

    m = (RoboAspaceMap *)kmalloc(sizeof(*m));
    if (!m)
        return 0;
    m->va = va;
    m->pa = pa;
    m->next = as->maps;
    as->maps = m;

    hw = VmToPteFlags(flags, gVmm.nx);
    *leaf = PteMake(pa, hw);
    AspaceInvlpg(as->cr3, va);
    return 1;
}

int VmmAspaceUnmap(RoboAddressSpace *as, VirtAddr va, PhysAddr *pa)
{
    Pte *leaf;
    RoboAspaceMap **slot, *m;

    if (!as || !as->cr3)
        return 0;
    if (!WalkLeafAt(as->cr3, va, 0, 0, &leaf, 0) || !leaf)
        return 0;
    if (!PtePresent(*leaf))
        return 0;
    if (pa)
        *pa = PtePhys(*leaf);
    *leaf = 0;
    AspaceInvlpg(as->cr3, va);

    slot = &as->maps;
    while (*slot) {
        if ((*slot)->va == va) {
            m = *slot;
            *slot = m->next;
            /* caller of destroy frees pa; unmap alone does not */
            kfree(m);
            break;
        }
        slot = &(*slot)->next;
    }
    return 1;
}

int VmmAspaceTranslate(RoboAddressSpace *as, VirtAddr va,
                              PhysAddr *pa)
{
    Pte *leaf;

    if (!as || !as->cr3)
        return 0;
    if (!WalkLeafAt(as->cr3, va, 0, 0, &leaf, 0) || !leaf)
        return 0;
    if (!PtePresent(*leaf))
        return 0;
    if (pa)
        *pa = PtePhys(*leaf);
    return 1;
}

int VmmAspaceProtect(RoboAddressSpace *as, VirtAddr va, uint64_t flags)
{
    Pte *leaf;
    PhysAddr pa;
    uint64_t hw;

    if (!as || !as->cr3)
        return 0;
    if (!VirtualAddressIsCanonical(va) || !VirtAligned(va))
        return 0;
    if (!VmFlagsValid(flags))
        return 0;
    if (!(flags & RoboVmUser))
        flags |= RoboVmKernel;
    if (!WalkLeafAt(as->cr3, va, 0, 0, &leaf, 0) || !leaf)
        return 0;
    if (!PtePresent(*leaf))
        return 0;
    pa = PtePhys(*leaf);
    hw = VmToPteFlags(flags, gVmm.nx);
    *leaf = PteMake(pa, hw);
    AspaceInvlpg(as->cr3, va);
    return 1;
}

int VmmAspaceInspect(RoboAddressSpace *as, VirtAddr va,
                            PhysAddr *pa, Pte *pte)
{
    Pte *leaf;

    if (!as || !as->cr3)
        return 0;
    if (!WalkLeafAt(as->cr3, va, 0, 0, &leaf, 0) || !leaf)
        return 0;
    if (!PtePresent(*leaf))
        return 0;
    if (pa)
        *pa = PtePhys(*leaf);
    if (pte)
        *pte = *leaf;
    return 1;
}
#endif
