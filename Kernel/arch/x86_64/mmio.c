#include "arch/x86_64/mmio.h"
#include "mm/vmm/vmm.h"
#include "mm/page.h"

void *MmioMap(PhysAddr pa, uint64_t len)
{
    PhysAddr start, end, p;
    uint64_t flags;

    if (!len || pa + len < pa)
        return 0;
    start = PageAlignDown(pa);
    end = PageAlignUp(pa + len);
    flags = RoboVmRead | RoboVmWrite | RoboVmKernel | RoboVmNocache;
    for (p = start; p < end; p += RoboPageSize) {
        PhysAddr got = RoboInvalidPhysAddr;
        if (VmmTranslate((VirtAddr)p, &got) && got == p)
            continue;
        if (!VmmMap((VirtAddr)p, p, flags))
            return 0;
    }
    return (void *)(uintptr_t)pa;
}

uint8_t MmioR8(const volatile void *p)
{
    return *(const volatile uint8_t *)p;
}

uint16_t MmioR16(const volatile void *p)
{
    return *(const volatile uint16_t *)p;
}

uint32_t MmioR32(const volatile void *p)
{
    return *(const volatile uint32_t *)p;
}

uint64_t MmioR64(const volatile void *p)
{
    return *(const volatile uint64_t *)p;
}

void MmioW8(volatile void *p, uint8_t v)
{
    *(volatile uint8_t *)p = v;
}

void MmioW16(volatile void *p, uint16_t v)
{
    *(volatile uint16_t *)p = v;
}

void MmioW32(volatile void *p, uint32_t v)
{
    *(volatile uint32_t *)p = v;
}

void MmioW64(volatile void *p, uint64_t v)
{
    *(volatile uint64_t *)p = v;
}
