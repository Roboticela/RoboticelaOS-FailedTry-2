#include "arch/x86_64/exception.h"
#include "mm/vmm/vmm.h"
#include "mm/page.h"
#include "kio.h"

static int pf_fail(const char *tag)
{
    kprintf("PF selftest: %s\n", tag);
    return 0;
}

int robo_pf_selftest(void)
{
    virt_addr_t va;
    phys_addr_t phys;
    uint64_t cr2, rip, err, expect_rip;

    va = (virt_addr_t)(ROBO_VMM_TEST_BASE + 16ull * ROBO_PAGE_SIZE);
    expect_rip = robo_pf_probe_rip();

    kprintf("PF  probe VA 0x%llx  RIP 0x%llx\n",
            (unsigned long long)va, (unsigned long long)expect_rip);

    if (robo_vmm_ready() && robo_vmm_translate(va, &phys))
        return pf_fail("probe VA still mapped");

    if (!robo_pf_catch_read((uint64_t)va, &cr2, &rip, &err))
        return pf_fail("no fault");

    if (cr2 != (uint64_t)va) {
        kprintf("PF selftest: CR2 0x%llx expect 0x%llx\n",
                (unsigned long long)cr2, (unsigned long long)va);
        return 0;
    }
    if (rip != expect_rip) {
        kprintf("PF selftest: RIP 0x%llx expect 0x%llx\n",
                (unsigned long long)rip, (unsigned long long)expect_rip);
        return 0;
    }
    if (err & ROBO_PF_P)
        return pf_fail("present");
    if (err & ROBO_PF_WR)
        return pf_fail("write");
    if (err & ROBO_PF_US)
        return pf_fail("user");
    if (err & ROBO_PF_ID)
        return pf_fail("execute");

    kprintf("PF  CR2 matches probe VA\n");
    kprintf("PF  RIP matches probe instruction\n");
    kprintf("PF selftest passed\n");
    return 1;
}
