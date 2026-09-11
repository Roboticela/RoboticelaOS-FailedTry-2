#include "mm/vmm/vmm.h"
#include "mm/pmm.h"

#ifdef VMM_HOST_TEST
#include <stdio.h>
#define kprintf printf
#else
#include "kio.h"
#endif

static int expect_map(virt_addr_t va, phys_addr_t pa, uint64_t flags, const char *tag)
{
    if (!robo_vmm_map(va, pa, flags)) {
        kprintf("VMM selftest: map failed %s\n", tag);
        return 0;
    }
    return 1;
}

static int expect_trans(virt_addr_t va, phys_addr_t expect, const char *tag)
{
    phys_addr_t got;
    if (!robo_vmm_translate(va, &got)) {
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

int robo_vmm_selftest(void)
{
    phys_addr_t a, b, c, got, off_phys;
    virt_addr_t va, vb, vc, voff;
    size_t free0, free1, tables;
    robo_pte_t pte;
    volatile uint64_t *wp;
    const uint64_t pattern = 0x1122334455667788ull;

    if (!robo_vmm_ready() || !robo_pmm_ready()) {
        kprintf("VMM selftest: not ready\n");
        return 0;
    }

    va = ROBO_VMM_TEST_BASE;
    vb = ROBO_VMM_TEST_BASE + ROBO_PAGE_SIZE;
    vc = ROBO_VMM_TEST_BASE + 2ull * ROBO_PAGE_SIZE;
    voff = ROBO_VMM_TEST_BASE + 0x678ull;

    if (robo_vmm_translate(va, &got)) {
        kprintf("VMM selftest: test window already mapped\n");
        return 0;
    }

    free0 = robo_free_page_count();
    a = robo_page_alloc();
    b = robo_page_alloc();
    c = robo_page_alloc();
    if (a == ROBO_INVALID_PHYS_ADDR || b == ROBO_INVALID_PHYS_ADDR || c == ROBO_INVALID_PHYS_ADDR) {
        kprintf("VMM selftest: page alloc failed\n");
        return 0;
    }
    if (a == b || b == c || a == c) {
        kprintf("VMM selftest: duplicate physical pages\n");
        return 0;
    }

    if (!expect_map(va, a, ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_KERNEL, "A"))
        return 0;
    if (robo_vmm_map(va, a, ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_KERNEL)) {
        kprintf("VMM selftest: silent remap\n");
        return 0;
    }
    if (!expect_trans(va, a, "A") || !expect_trans(voff, a + 0x678ull, "A+off"))
        return 0;

    wp = (volatile uint64_t *)(uintptr_t)va;
    *wp = pattern;
    if (*wp != pattern) {
        kprintf("VMM selftest: readback mismatch\n");
        return 0;
    }

    if (!robo_vmm_protect(va, ROBO_VM_READ | ROBO_VM_KERNEL)) {
        kprintf("VMM selftest: protect RO failed\n");
        return 0;
    }
    if (!robo_vmm_inspect(va, &got, &pte) || got != a || !(pte & ROBO_PTE_P) || (pte & ROBO_PTE_RW)) {
        kprintf("VMM selftest: RO flags\n");
        return 0;
    }
    if (*wp != pattern) {
        kprintf("VMM selftest: RO readback failed\n");
        return 0;
    }
    if (!robo_vmm_protect(va, ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_KERNEL)) {
        kprintf("VMM selftest: restore RW failed\n");
        return 0;
    }

    if (!expect_map(vb, b, ROBO_VM_READ | ROBO_VM_EXECUTE | ROBO_VM_KERNEL, "B"))
        return 0;
    if (!expect_map(vc, c, ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_USER, "C"))
        return 0;
    if (!expect_trans(vb, b, "B") || !expect_trans(vc, c, "C"))
        return 0;
    if (!robo_vmm_inspect(vb, 0, &pte))
        return 0;
    if (robo_vmm_nx_enabled() && (pte & ROBO_PTE_NX)) {
        kprintf("VMM selftest: NX set on executable page\n");
        return 0;
    }
    if (!robo_vmm_inspect(vc, 0, &pte) || !(pte & ROBO_PTE_US)) {
        kprintf("VMM selftest: USER bit missing\n");
        return 0;
    }
    {
        phys_addr_t kphys = 0x401000;
        robo_pte_t kpte;
        if (robo_vmm_inspect((virt_addr_t)kphys, 0, &kpte) && (kpte & ROBO_PTE_US)) {
            kprintf("VMM selftest: kernel page is USER\n");
            return 0;
        }
    }

    free1 = robo_free_page_count();
    tables = (free0 - 3) - free1;

    if (!robo_vmm_unmap(va, &got) || got != a) {
        kprintf("VMM selftest: unmap A\n");
        return 0;
    }
    if (robo_vmm_translate(va, &off_phys)) {
        kprintf("VMM selftest: A still translated\n");
        return 0;
    }
    if (!robo_vmm_unmap(vb, &got) || got != b) {
        kprintf("VMM selftest: unmap B\n");
        return 0;
    }
    if (!robo_vmm_unmap(vc, &got) || got != c) {
        kprintf("VMM selftest: unmap C\n");
        return 0;
    }
    if (robo_vmm_unmap(va, 0)) {
        kprintf("VMM selftest: double unmap succeeded\n");
        return 0;
    }

    robo_page_free(a);
    robo_page_free(b);
    robo_page_free(c);
    if (robo_free_page_count() != free0 - tables) {
        kprintf("VMM selftest: free count %llu expect %llu\n",
                (unsigned long long)robo_free_page_count(),
                (unsigned long long)(free0 - tables));
        return 0;
    }

    if (robo_vmm_map(1, a, ROBO_VM_READ | ROBO_VM_WRITE) ||
        robo_vmm_map(va, 1, ROBO_VM_READ | ROBO_VM_WRITE) ||
        robo_vmm_translate(0x0000800000000000ull, &got)) {
        kprintf("VMM selftest: invalid address accepted\n");
        return 0;
    }

    kprintf("VMM selftest passed\n");
    return 1;
}
