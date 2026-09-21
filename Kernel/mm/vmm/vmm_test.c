#include "mm/vmm/vmm.h"
#include "mm/pmm.h"

#ifdef VMM_HOST_TEST
#include <stdio.h>
#define kprintf printf
#else
#include "kio.h"
#endif

static int ExpectMap(VirtAddr va, PhysAddr pa, uint64_t flags, const char *tag)
{
    if (!VmmMap(va, pa, flags)) {
        kprintf("VMM selftest: map failed %s\n", tag);
        return 0;
    }
    return 1;
}

static int ExpectTrans(VirtAddr va, PhysAddr expect, const char *tag)
{
    PhysAddr got;
    if (!VmmTranslate(va, &got)) {
        kprintf("VMM selftest: translate failed %s\n", tag);
        return 0;
    }
    if (got != expect) {
        kprintf("VMM selftest: translate mismatch %s got 0x%llx expect 0x%llx\n",
                tag, (unsigned long long)got, (unsigned long long)expect);
        return 0;
    }
    return 1;
}

int VmmSelftest(void)
{
    PhysAddr a, b, c, got, offPhys;
    VirtAddr va, vb, vc, voff;
    size_t free0, free1, tables;
    Pte pte;
    volatile uint64_t *wp;
    const uint64_t pattern = 0x1122334455667788ull;

    if (!VmmReady() || !PmmReady()) {
        kprintf("VMM selftest: not ready\n");
        return 0;
    }

    va = RoboVmmTestBase;
    vb = RoboVmmTestBase + RoboPageSize;
    vc = RoboVmmTestBase + 2ull * RoboPageSize;
    voff = RoboVmmTestBase + 0x678ull;

    if (VmmTranslate(va, &got)) {
        kprintf("VMM selftest: test window already mapped\n");
        return 0;
    }

    free0 = FreePageCount();
    a = PageAlloc();
    b = PageAlloc();
    c = PageAlloc();
    if (a == RoboInvalidPhysAddr || b == RoboInvalidPhysAddr || c == RoboInvalidPhysAddr) {
        kprintf("VMM selftest: page alloc failed\n");
        return 0;
    }
    if (a == b || b == c || a == c) {
        kprintf("VMM selftest: duplicate physical pages\n");
        return 0;
    }

    if (!ExpectMap(va, a, RoboVmRead | RoboVmWrite | RoboVmKernel, "A"))
        return 0;
    if (VmmMap(va, a, RoboVmRead | RoboVmWrite | RoboVmKernel)) {
        kprintf("VMM selftest: silent remap\n");
        return 0;
    }
    if (!ExpectTrans(va, a, "A") || !ExpectTrans(voff, a + 0x678ull, "A+off"))
        return 0;

    wp = (volatile uint64_t *)(uintptr_t)va;
    *wp = pattern;
    if (*wp != pattern) {
        kprintf("VMM selftest: readback mismatch\n");
        return 0;
    }

    if (!VmmProtect(va, RoboVmRead | RoboVmKernel)) {
        kprintf("VMM selftest: protect RO failed\n");
        return 0;
    }
    if (!VmmInspect(va, &got, &pte) || got != a || !(pte & RoboPteP) || (pte & RoboPteRw)) {
        kprintf("VMM selftest: RO flags\n");
        return 0;
    }
    if (*wp != pattern) {
        kprintf("VMM selftest: RO readback failed\n");
        return 0;
    }
    if (!VmmProtect(va, RoboVmRead | RoboVmWrite | RoboVmKernel)) {
        kprintf("VMM selftest: restore RW failed\n");
        return 0;
    }

    if (!ExpectMap(vb, b, RoboVmRead | RoboVmExecute | RoboVmKernel, "B"))
        return 0;
    if (!ExpectMap(vc, c, RoboVmRead | RoboVmWrite | RoboVmUser, "C"))
        return 0;
    if (!ExpectTrans(vb, b, "B") || !ExpectTrans(vc, c, "C"))
        return 0;
    if (!VmmInspect(vb, 0, &pte))
        return 0;
    if (VmmNxEnabled() && (pte & RoboPteNx)) {
        kprintf("VMM selftest: NX set on executable page\n");
        return 0;
    }
    if (!VmmInspect(vc, 0, &pte) || !(pte & RoboPteUs)) {
        kprintf("VMM selftest: USER bit missing\n");
        return 0;
    }
    {
        PhysAddr kphys = 0x401000;
        Pte kpte;
        if (VmmInspect((VirtAddr)kphys, 0, &kpte) && (kpte & RoboPteUs)) {
            kprintf("VMM selftest: kernel page is USER\n");
            return 0;
        }
    }

    free1 = FreePageCount();
    tables = (free0 - 3) - free1;

    if (!VmmUnmap(va, &got) || got != a) {
        kprintf("VMM selftest: unmap A\n");
        return 0;
    }
    if (VmmTranslate(va, &offPhys)) {
        kprintf("VMM selftest: A still translated\n");
        return 0;
    }
    if (!VmmUnmap(vb, &got) || got != b) {
        kprintf("VMM selftest: unmap B\n");
        return 0;
    }
    if (!VmmUnmap(vc, &got) || got != c) {
        kprintf("VMM selftest: unmap C\n");
        return 0;
    }
    if (VmmUnmap(va, 0)) {
        kprintf("VMM selftest: double unmap succeeded\n");
        return 0;
    }

    PageFree(a);
    PageFree(b);
    PageFree(c);
    if (FreePageCount() != free0 - tables) {
        kprintf("VMM selftest: free count %llu expect %llu\n",
                (unsigned long long)FreePageCount(),
                (unsigned long long)(free0 - tables));
        return 0;
    }

    if (VmmMap(1, a, RoboVmRead | RoboVmWrite) ||
        VmmMap(va, 1, RoboVmRead | RoboVmWrite) ||
        VmmTranslate(0x0000800000000000ull, &got)) {
        kprintf("VMM selftest: invalid address accepted\n");
        return 0;
    }

    kprintf("VMM selftest passed\n");
    return 1;
}
