#include "exec/elf/elf_validate.h"
#include "mm/vmm/paging.h"
#include "mm/page.h"

int robo_user_va_ok(uint64_t va, uint64_t len)
{
    uint64_t end;
    if (!len)
        return robo_virtual_address_is_canonical((virt_addr_t)va) &&
               va >= ROBO_USER_MIN && va < ROBO_USER_MAX;
    if (!robo_u64_add_ok(va, len, &end))
        return 0;
    if (!robo_virtual_address_is_canonical((virt_addr_t)va) ||
        !robo_virtual_address_is_canonical((virt_addr_t)(end - 1ull)))
        return 0;
    if (va < ROBO_USER_MIN || end > ROBO_USER_MAX)
        return 0;
    return 1;
}

static int segs_overlap(const Elf64_Phdr *a, const Elf64_Phdr *b)
{
    uint64_t a1, b1;
    if (!robo_u64_add_ok(a->p_vaddr, a->p_memsz, &a1))
        return 1;
    if (!robo_u64_add_ok(b->p_vaddr, b->p_memsz, &b1))
        return 1;
    if (a->p_vaddr >= b1 || b->p_vaddr >= a1)
        return 0;
    return 1;
}

robo_exec_status_t robo_elf_validate(const RoboElfImage *img, size_t file_size)
{
    unsigned i, j;
    uint64_t end;

    if (!img)
        return ROBO_ELF_INVALID_HEADER;
    if (img->eh.e_type != ET_EXEC)
        return ROBO_ELF_UNSUPPORTED_TYPE;
    if (img->nload < 1)
        return ROBO_ELF_NO_LOAD;

    for (i = 0; i < img->phnum; i++) {
        const Elf64_Phdr *ph = &img->ph[i];
        if (ph->p_type != PT_LOAD)
            continue;
        if (ph->p_memsz == 0)
            continue;
        if (ph->p_filesz > ph->p_memsz)
            return ROBO_ELF_INVALID_SEGMENT;
        if (!robo_u64_add_ok(ph->p_offset, ph->p_filesz, &end))
            return ROBO_ELF_ADDRESS_OVERFLOW;
        if (end > (uint64_t)file_size)
            return ROBO_ELF_INVALID_SEGMENT;
        if (!robo_u64_add_ok(ph->p_vaddr, ph->p_memsz, &end))
            return ROBO_ELF_ADDRESS_OVERFLOW;
        if (!robo_user_va_ok(ph->p_vaddr, ph->p_memsz))
            return ROBO_ELF_ADDRESS_INVALID;
        if ((ph->p_flags & PF_W) && (ph->p_flags & PF_X))
            return ROBO_ELF_WX;
        if (!(ph->p_flags & (PF_R | PF_W | PF_X)))
            return ROBO_ELF_INVALID_SEGMENT;
        if (ph->p_align && (ph->p_align & (ph->p_align - 1ull)))
            return ROBO_ELF_INVALID_SEGMENT;
        if (ph->p_align >= ROBO_PAGE_SIZE) {
            if ((ph->p_vaddr & (ph->p_align - 1ull)) !=
                (ph->p_offset & (ph->p_align - 1ull)))
                return ROBO_ELF_INVALID_SEGMENT;
        }
        for (j = 0; j < i; j++) {
            if (img->ph[j].p_type != PT_LOAD || img->ph[j].p_memsz == 0)
                continue;
            if (segs_overlap(ph, &img->ph[j]))
                return ROBO_ELF_OVERLAP;
        }
    }
    if (!robo_user_va_ok(img->eh.e_entry, 1))
        return ROBO_ELF_ENTRY_INVALID;
    return ROBO_EXEC_OK;
}
