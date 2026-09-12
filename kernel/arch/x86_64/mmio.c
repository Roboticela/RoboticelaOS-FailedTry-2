#include "arch/x86_64/mmio.h"
#include "mm/vmm/vmm.h"
#include "mm/page.h"

void *robo_mmio_map(phys_addr_t pa, uint64_t len)
{
    phys_addr_t start, end, p;
    uint64_t flags;

    if (!len || pa + len < pa)
        return 0;
    start = robo_page_align_down(pa);
    end = robo_page_align_up(pa + len);
    flags = ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_KERNEL | ROBO_VM_NOCACHE;
    for (p = start; p < end; p += ROBO_PAGE_SIZE) {
        phys_addr_t got = ROBO_INVALID_PHYS_ADDR;
        if (robo_vmm_translate((virt_addr_t)p, &got) && got == p)
            continue;
        if (!robo_vmm_map((virt_addr_t)p, p, flags))
            return 0;
    }
    return (void *)(uintptr_t)pa;
}

uint8_t robo_mmio_r8(const volatile void *p)
{
    return *(const volatile uint8_t *)p;
}

uint16_t robo_mmio_r16(const volatile void *p)
{
    return *(const volatile uint16_t *)p;
}

uint32_t robo_mmio_r32(const volatile void *p)
{
    return *(const volatile uint32_t *)p;
}

uint64_t robo_mmio_r64(const volatile void *p)
{
    return *(const volatile uint64_t *)p;
}

void robo_mmio_w8(volatile void *p, uint8_t v)
{
    *(volatile uint8_t *)p = v;
}

void robo_mmio_w16(volatile void *p, uint16_t v)
{
    *(volatile uint16_t *)p = v;
}

void robo_mmio_w32(volatile void *p, uint32_t v)
{
    *(volatile uint32_t *)p = v;
}

void robo_mmio_w64(volatile void *p, uint64_t v)
{
    *(volatile uint64_t *)p = v;
}
