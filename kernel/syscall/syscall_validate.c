#include "syscall/syscall_validate.h"
#include "syscall/syscall_errors.h"
#include "exec/elf/elf.h"
#include "exec/elf/elf_validate.h"
#include "mm/vmm/vmm.h"
#include "mm/vmm/paging.h"
#include "mm/page.h"
#include "arch/x86_64/gdt.h"

int robo_u64_add_ok_sys(uint64_t a, uint64_t b, uint64_t *out)
{
    return robo_u64_add_ok(a, b, out);
}

static int page_access_ok(RoboAddressSpace *as, uint64_t page, uint64_t access)
{
    phys_addr_t pa;
    robo_pte_t pte;

    if (!robo_vmm_aspace_inspect(as, (virt_addr_t)page, &pa, &pte))
        return 0;
    if (!(pte & ROBO_PTE_P) || !(pte & ROBO_PTE_US))
        return 0;
    if ((access & ROBO_USER_ACCESS_WRITE) && !(pte & ROBO_PTE_RW))
        return 0;
    if ((access & ROBO_USER_ACCESS_EXEC) && (pte & ROBO_PTE_NX))
        return 0;
    (void)pa;
    return 1;
}

int robo_user_range_valid(RoboAddressSpace *as, uint64_t address,
                         uint64_t length, uint64_t access)
{
    uint64_t page, end;

    if (!as)
        return 0;
    if (!length)
        return robo_user_va_ok(address, 1);
    if (!robo_user_va_ok(address, length))
        return 0;
    if (!robo_u64_add_ok(address, length, &end))
        return 0;
    page = address & ~ROBO_PAGE_MASK;
    for (; page < end; page += ROBO_PAGE_SIZE) {
        if (!page_access_ok(as, page, access))
            return 0;
    }
    return 1;
}

int robo_copy_from_user(RoboAddressSpace *as, void *kernel_destination,
                        uint64_t user_source, size_t length)
{
    unsigned char *dst = (unsigned char *)kernel_destination;
    size_t got = 0;

    if (!length)
        return 0;
    if (!dst || !as)
        return -ROBO_EFAULT;
    if (!robo_user_range_valid(as, user_source, (uint64_t)length,
                               ROBO_USER_ACCESS_READ))
        return -ROBO_EFAULT;
    while (got < length) {
        uint64_t page = (user_source + got) & ~ROBO_PAGE_MASK;
        uint64_t off = (user_source + got) & ROBO_PAGE_MASK;
        uint64_t chunk = ROBO_PAGE_SIZE - off;
        phys_addr_t pa;
        const unsigned char *src;
        uint64_t i;

        if (chunk > length - got)
            chunk = length - got;
        if (!robo_vmm_aspace_translate(as, (virt_addr_t)page, &pa))
            return -ROBO_EFAULT;
        src = (const unsigned char *)(uintptr_t)(pa + off);
        for (i = 0; i < chunk; i++)
            dst[got + i] = src[i];
        got += (size_t)chunk;
    }
    return 0;
}

int robo_copy_to_user(RoboAddressSpace *as, uint64_t user_destination,
                      const void *kernel_source, size_t length)
{
    const unsigned char *src = (const unsigned char *)kernel_source;
    size_t got = 0;

    if (!length)
        return 0;
    if (!src || !as)
        return -ROBO_EFAULT;
    if (!robo_user_range_valid(as, user_destination, (uint64_t)length,
                               ROBO_USER_ACCESS_WRITE))
        return -ROBO_EFAULT;
    while (got < length) {
        uint64_t page = (user_destination + got) & ~ROBO_PAGE_MASK;
        uint64_t off = (user_destination + got) & ROBO_PAGE_MASK;
        uint64_t chunk = ROBO_PAGE_SIZE - off;
        phys_addr_t pa;
        unsigned char *dst;
        uint64_t i;

        if (chunk > length - got)
            chunk = length - got;
        if (!robo_vmm_aspace_translate(as, (virt_addr_t)page, &pa))
            return -ROBO_EFAULT;
        dst = (unsigned char *)(uintptr_t)(pa + off);
        for (i = 0; i < chunk; i++)
            dst[i] = src[got + i];
        got += (size_t)chunk;
    }
    return 0;
}

int robo_user_string_valid(RoboAddressSpace *as, uint64_t address,
                           size_t max_length, size_t *out_len)
{
    size_t n = 0;

    if (!as || !max_length)
        return 0;
    while (n < max_length) {
        unsigned char c;
        if (robo_copy_from_user(as, &c, address + n, 1) != 0)
            return 0;
        n++;
        if (c == 0) {
            if (out_len)
                *out_len = n - 1;
            return 1;
        }
    }
    return 0;
}

int robo_copy_user_string(RoboAddressSpace *as, char *kernel_destination,
                          uint64_t user_source, size_t max_length)
{
    size_t n = 0;

    if (!kernel_destination || !max_length)
        return -ROBO_EINVAL;
    if (!robo_user_string_valid(as, user_source, max_length, &n))
        return -ROBO_EFAULT;
    if (n + 1 > max_length)
        return -ROBO_EINVAL;
    if (robo_copy_from_user(as, kernel_destination, user_source, n + 1) != 0)
        return -ROBO_EFAULT;
    kernel_destination[n] = 0;
    return 0;
}

int robo_user_return_ok(const struct robo_iframe *frame)
{
    if (!frame)
        return 0;
    if ((frame->cs & 3ull) != 3ull)
        return 0;
    if (frame->cs != ROBO_GDT_USER_CODE_RPL)
        return 0;
    if (frame->ss != ROBO_GDT_USER_DATA_RPL)
        return 0;
    if (!robo_user_va_ok(frame->rip, 1))
        return 0;
    if (!robo_user_va_ok(frame->rsp, 1))
        return 0;
    return 1;
}
