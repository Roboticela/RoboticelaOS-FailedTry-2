#include "arch/x86_64/exception.h"
#include "mm/vmm/vmm.h"
#include "mm/page.h"
#include "kio.h"

static int PfFail(const char *tag)
{
    kprintf("PF selftest: %s\n", tag);
    return 0;
}

int PfSelftest(void)
{
    VirtAddr va;
    PhysAddr phys;
    uint64_t cr2, rip, err, expectRip;

    va = (VirtAddr)(RoboVmmTestBase + 16ull * RoboPageSize);
    expectRip = PfProbeRip();

    kprintf("PF  probe VA 0x%llx  RIP 0x%llx\n",
            (unsigned long long)va, (unsigned long long)expectRip);

    if (VmmReady() && VmmTranslate(va, &phys))
        return PfFail("probe VA still mapped");

    if (!PfCatchRead((uint64_t)va, &cr2, &rip, &err))
        return PfFail("no fault");

    if (cr2 != (uint64_t)va) {
        kprintf("PF selftest: CR2 0x%llx expect 0x%llx\n",
                (unsigned long long)cr2, (unsigned long long)va);
        return 0;
    }
    if (rip != expectRip) {
        kprintf("PF selftest: RIP 0x%llx expect 0x%llx\n",
                (unsigned long long)rip, (unsigned long long)expectRip);
        return 0;
    }
    if (err & RoboPfP)
        return PfFail("present");
    if (err & RoboPfWr)
        return PfFail("write");
    if (err & RoboPfUs)
        return PfFail("user");
    if (err & RoboPfId)
        return PfFail("execute");

    kprintf("PF  CR2 matches probe VA\n");
    kprintf("PF  RIP matches probe instruction\n");
    kprintf("PF selftest passed\n");
    return 1;
}
