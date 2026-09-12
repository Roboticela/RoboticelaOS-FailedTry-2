#include "exec/elf/elf_loader.h"
#include "exec/elf/elf_parser.h"
#include "exec/elf/elf_validate.h"
#include "mm/vmm/vmm.h"
#include "mm/pmm.h"
#include "mm/page.h"
#include "kio.h"

static int g_elf_quiet;

void robo_elf_set_quiet(int quiet)
{
    g_elf_quiet = quiet ? 1 : 0;
}

static void copy_bytes(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
}

static void zero_bytes(void *dst, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--)
        *d++ = 0;
}

static uint64_t elf_vm_flags(uint32_t pf, int writable)
{
    uint64_t f = ROBO_VM_USER | ROBO_VM_READ;
    if (writable || (pf & PF_W))
        f |= ROBO_VM_WRITE;
    if ((pf & PF_X) && !writable)
        f |= ROBO_VM_EXECUTE;
    return f;
}

static int map_page(RoboAddressSpace *as, virt_addr_t va, phys_addr_t *pa, uint64_t flags)
{
    phys_addr_t page = robo_page_alloc();
    if (page == ROBO_INVALID_PHYS_ADDR)
        return 0;
    zero_bytes((void *)(uintptr_t)page, ROBO_PAGE_SIZE);
    if (!robo_vmm_aspace_map(as, va, page, flags)) {
        robo_page_free(page);
        return 0;
    }
    if (pa)
        *pa = page;
    return 1;
}

int robo_elf_entry_executable(RoboAddressSpace *as, uint64_t entry)
{
    phys_addr_t pa;
    robo_pte_t pte;
    virt_addr_t page;
    if (!as || !robo_user_va_ok(entry, 1))
        return 0;
    page = entry & ~ROBO_PAGE_MASK;
    if (!robo_vmm_aspace_inspect(as, page, &pa, &pte))
        return 0;
    if (!(pte & ROBO_PTE_P) || !(pte & ROBO_PTE_US))
        return 0;
    if (pte & ROBO_PTE_NX)
        return 0;
    (void)pa;
    return 1;
}

static robo_exec_status_t load_segment(RoboAddressSpace *as, const uint8_t *file,
                                       size_t file_size, const Elf64_Phdr *ph)
{
    uint64_t va0, va1, page, off, copy_n, zero_n, file_left, bss_left;
    uint64_t flags_tmp, flags_fin;
    const uint8_t *src;
    uint8_t *dst;

    (void)file_size;
    va0 = ph->p_vaddr & ~ROBO_PAGE_MASK;
    if (!robo_u64_add_ok(ph->p_vaddr, ph->p_memsz, &va1))
        return ROBO_ELF_ADDRESS_OVERFLOW;
    va1 = (va1 + ROBO_PAGE_MASK) & ~ROBO_PAGE_MASK;
    flags_tmp = ROBO_VM_USER | ROBO_VM_READ | ROBO_VM_WRITE;
    flags_fin = elf_vm_flags(ph->p_flags, 0);
    off = ph->p_vaddr - va0;
    src = file + ph->p_offset;
    file_left = ph->p_filesz;
    bss_left = ph->p_memsz - ph->p_filesz;

    for (page = va0; page < va1; page += ROBO_PAGE_SIZE) {
        phys_addr_t pa;
        uint64_t skip = (page == va0) ? off : 0;
        if (!map_page(as, (virt_addr_t)page, &pa, flags_tmp))
            return ROBO_ELF_MEMORY_FAILURE;
        dst = (uint8_t *)(uintptr_t)pa;
        copy_n = ROBO_PAGE_SIZE - skip;
        if (copy_n > file_left)
            copy_n = file_left;
        if (copy_n)
            copy_bytes(dst + skip, src, (size_t)copy_n);
        src += copy_n;
        file_left -= copy_n;
        zero_n = ROBO_PAGE_SIZE - skip - copy_n;
        if (zero_n > bss_left)
            zero_n = bss_left;
        /* remainder of the page after file bytes is BSS or unused padding */
        if (ROBO_PAGE_SIZE - skip - copy_n)
            zero_bytes(dst + skip + copy_n, (size_t)(ROBO_PAGE_SIZE - skip - copy_n));
        if (zero_n)
            bss_left -= zero_n;
        if (!robo_vmm_aspace_protect(as, (virt_addr_t)page, flags_fin))
            return ROBO_ELF_MEMORY_FAILURE;
    }
    return ROBO_EXEC_OK;
}

static robo_exec_status_t map_stack(RoboAddressSpace *as, RoboExecImage *out)
{
    uint64_t top = ROBO_USER_STACK_TOP;
    uint64_t size = ROBO_USER_STACK_SIZE;
    uint64_t base, page;
    uint64_t flags = ROBO_VM_USER | ROBO_VM_READ | ROBO_VM_WRITE;

    if (top < size)
        return ROBO_ELF_ADDRESS_INVALID;
    base = top - size;
    if (!robo_user_va_ok(base, size))
        return ROBO_ELF_ADDRESS_INVALID;
    for (page = base; page < top; page += ROBO_PAGE_SIZE) {
        if (!map_page(as, (virt_addr_t)page, 0, flags))
            return ROBO_ELF_MEMORY_FAILURE;
    }
    out->stack_base = base;
    out->stack_top = top;
    return ROBO_EXEC_OK;
}

static unsigned str_len(const char *s)
{
    unsigned n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

static int plant_user_argv(RoboAddressSpace *as, RoboExecImage *out,
                           const char *argv0, const RoboExecArgv *argv)
{
    uint64_t top = ROBO_USER_STACK_TOP;
    uint64_t page = top - ROBO_PAGE_SIZE;
    uint64_t zero = 0;
    uint64_t argc;
    uint64_t ptrs[ROBO_EXEC_ARGV_MAX];
    uint64_t rsp;
    uint64_t words;
    uint8_t *mem;
    phys_addr_t pa;
    unsigned i, n, off, used;
    const char *s;

    if (!robo_vmm_aspace_translate(as, (virt_addr_t)page, &pa))
        return 0;
    mem = (uint8_t *)(uintptr_t)pa;

    n = 0;
    if (argv && argv->argc) {
        n = argv->argc;
        if (n > ROBO_EXEC_ARGV_MAX)
            n = ROBO_EXEC_ARGV_MAX;
    } else {
        n = 1;
    }

    used = 0;
    for (i = 0; i < n; i++) {
        unsigned len;
        s = (argv && argv->argc) ? argv->args[i] : argv0;
        if (!s || !s[0])
            s = "user";
        len = str_len(s);
        if (len > ROBO_EXEC_ARG_MAX)
            len = ROBO_EXEC_ARG_MAX;
        used += len + 1u;
    }
    used = (used + 15u) & ~15u;
    argc = n;
    words = 1ull + n + 1ull + 1ull + 2ull;
    if (words & 1ull)
        words++;
    if (used + (unsigned)(words * 8ull) + 16u > ROBO_PAGE_SIZE)
        return 0;
    off = (unsigned)ROBO_PAGE_SIZE - used;
    {
        unsigned cur = off;
        for (i = 0; i < n; i++) {
            unsigned len;
            s = (argv && argv->argc) ? argv->args[i] : argv0;
            if (!s || !s[0])
                s = "user";
            len = str_len(s);
            if (len > ROBO_EXEC_ARG_MAX)
                len = ROBO_EXEC_ARG_MAX;
            copy_bytes(mem + cur, s, len);
            mem[cur + len] = 0;
            ptrs[i] = page + cur;
            cur += len + 1u;
        }
    }
    rsp = (page + off - words * 8ull) & ~15ull;
    if (rsp < page)
        return 0;
    off = (unsigned)(rsp - page);
    copy_bytes(mem + off, &argc, 8);
    off += 8;
    for (i = 0; i < n; i++) {
        copy_bytes(mem + off, &ptrs[i], 8);
        off += 8;
    }
    copy_bytes(mem + off, &zero, 8);
    off += 8;
    copy_bytes(mem + off, &zero, 8);
    off += 8;
    copy_bytes(mem + off, &zero, 8);
    off += 8;
    copy_bytes(mem + off, &zero, 8);
    out->stack_top = rsp;
    return 1;
}

robo_exec_status_t robo_elf_load_image(RoboAddressSpace *as,
                                       const void *bytes, size_t size,
                                       RoboExecImage *out,
                                       const char *argv0,
                                       const RoboExecArgv *argv)
{
    RoboElfImage img;
    robo_exec_status_t st;
    unsigned i;

    if (!as || !bytes || !out)
        return ROBO_ELF_INVALID_HEADER;
    zero_bytes(out, sizeof(*out));
    st = robo_elf_parse(bytes, size, &img);
    if (st != ROBO_EXEC_OK)
        return st;
    st = robo_elf_validate(&img, size);
    if (st != ROBO_EXEC_OK)
        return st;

    if (!g_elf_quiet) {
        kprintf("Class       : ELF64\n");
        kprintf("Machine     : x86_64\n");
        kprintf("Type        : EXEC\n");
        kprintf("Entry       : 0x%llx\n", (unsigned long long)img.eh.e_entry);
        kprintf("PHNum       : %u\n\n", (unsigned)img.phnum);
    }
    for (i = 0; i < img.phnum; i++) {
        const Elf64_Phdr *ph = &img.ph[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
            continue;
        if (!g_elf_quiet) {
            kprintf("Segment %u\n", i);
            kprintf("---------\n");
            kprintf("Type        : LOAD\n");
            kprintf("Offset      : 0x%llx\n", (unsigned long long)ph->p_offset);
            kprintf("VAddr       : 0x%llx\n", (unsigned long long)ph->p_vaddr);
            kprintf("File Size   : 0x%llx\n", (unsigned long long)ph->p_filesz);
            kprintf("Mem Size    : 0x%llx\n", (unsigned long long)ph->p_memsz);
            kprintf("Flags       : %s%s%s\n\n",
                    (ph->p_flags & PF_R) ? "R" : "-",
                    (ph->p_flags & PF_W) ? "W" : "-",
                    (ph->p_flags & PF_X) ? "X" : "-");
        }
        st = load_segment(as, (const uint8_t *)bytes, size, ph);
        if (st != ROBO_EXEC_OK) {
            if (!g_elf_quiet) {
                kprintf("ELF LOAD FAILED\n\n");
                kprintf("Segment : %u\n", i);
                kprintf("Virtual : 0x%llx\n", (unsigned long long)ph->p_vaddr);
                kprintf("Size    : 0x%llx\n\n", (unsigned long long)ph->p_memsz);
                kprintf("Reason:\n%s\n", robo_exec_status_name(st));
            }
            return st;
        }
        if (!out->text_va && (ph->p_flags & PF_X))
            out->text_va = ph->p_vaddr;
    }
    st = map_stack(as, out);
    if (st != ROBO_EXEC_OK)
        return st;

    /* SysV process entry: RSP % 16 == 0.
     *   argc, argv[], NULL, envp NULL, AT_NULL, 0 */
    if (!plant_user_argv(as, out, argv0 ? argv0 : "user", argv))
        return ROBO_ELF_MEMORY_FAILURE;
    out->entry = img.eh.e_entry;
    out->load_bias = 0;
    if (!robo_elf_entry_executable(as, out->entry)) {
        if (!g_elf_quiet) {
            kprintf("ELF LOAD FAILED\n\n");
            kprintf("Entry : 0x%llx\n", (unsigned long long)out->entry);
            kprintf("Reason:\nEntry point is not inside an executable user mapping.\n");
        }
        return ROBO_ELF_ENTRY_INVALID;
    }
    return ROBO_EXEC_OK;
}
