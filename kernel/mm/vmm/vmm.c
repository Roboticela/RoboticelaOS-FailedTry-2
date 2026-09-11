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
extern char kstack_end[];
#endif

static struct {
    int ready;
    int nx;
    phys_addr_t pml4_phys;
    phys_addr_t kernel_base;
    uint64_t kernel_size;
    size_t table_pages;
    phys_addr_t ident_limit;
    phys_addr_t fb_base;
    uint64_t fb_size;
} g_vmm;

static void vmm_fill(void *dst, uint8_t v, size_t n)
{
    uint8_t *p = (uint8_t *)dst;
    while (n--)
        *p++ = v;
}

static int space_is_active(void)
{
    return g_vmm.ready && robo_cpu_read_cr3() == g_vmm.pml4_phys;
}

static void maybe_invlpg(virt_addr_t va)
{
    if (space_is_active())
        robo_cpu_invlpg(va);
}

static uint64_t dir_flags(uint64_t vm_flags)
{
    uint64_t hw = ROBO_PTE_P | ROBO_PTE_RW;
    if (vm_flags & ROBO_VM_USER)
        hw |= ROBO_PTE_US;
    return hw;
}

static int walk_leaf_at(phys_addr_t root, virt_addr_t va, int create,
                        uint64_t vm_flags, robo_pte_t **leaf, size_t *ntables)
{
    robo_pte_t *pml4, *pdpt, *pd, *pt;
    robo_pte_t e;
    phys_addr_t next;
    uint16_t i4, i3, i2, i1;
    uint64_t grow = dir_flags(vm_flags);

    if (!leaf || !root)
        return 0;
    *leaf = 0;
    pml4 = robo_table_ptr(root);
    if (!pml4)
        return 0;

    i4 = robo_pml4_slot(va);
    e = robo_table_get(pml4, i4);
    if (!robo_pte_present(e)) {
        if (!create)
            return 0;
        next = robo_table_alloc();
        if (next == ROBO_INVALID_PHYS_ADDR)
            return 0;
        if (ntables)
            (*ntables)++;
        e = robo_pte_make(next, grow);
        robo_table_set(pml4, i4, e);
    } else {
        if (robo_pte_huge(e))
            return 0;
        if (create && (grow & ROBO_PTE_US))
            robo_table_set(pml4, i4, e | ROBO_PTE_US);
    }
    pdpt = robo_table_ptr(robo_pte_phys(e));
    if (!pdpt)
        return 0;

    i3 = robo_pdpt_slot(va);
    e = robo_table_get(pdpt, i3);
    if (!robo_pte_present(e)) {
        if (!create)
            return 0;
        next = robo_table_alloc();
        if (next == ROBO_INVALID_PHYS_ADDR)
            return 0;
        if (ntables)
            (*ntables)++;
        e = robo_pte_make(next, grow);
        robo_table_set(pdpt, i3, e);
    } else {
        if (robo_pte_huge(e))
            return 0;
        if (create && (grow & ROBO_PTE_US))
            robo_table_set(pdpt, i3, e | ROBO_PTE_US);
    }
    pd = robo_table_ptr(robo_pte_phys(e));
    if (!pd)
        return 0;

    i2 = robo_pd_slot(va);
    e = robo_table_get(pd, i2);
    if (!robo_pte_present(e)) {
        if (!create)
            return 0;
        next = robo_table_alloc();
        if (next == ROBO_INVALID_PHYS_ADDR)
            return 0;
        if (ntables)
            (*ntables)++;
        e = robo_pte_make(next, grow);
        robo_table_set(pd, i2, e);
    } else {
        if (robo_pte_huge(e))
            return 0;
        if (create && (grow & ROBO_PTE_US))
            robo_table_set(pd, i2, e | ROBO_PTE_US);
    }
    pt = robo_table_ptr(robo_pte_phys(e));
    if (!pt)
        return 0;

    i1 = robo_pt_slot(va);
    *leaf = &pt[i1];
    return 1;
}

static int walk_leaf(virt_addr_t va, int create, uint64_t vm_flags, robo_pte_t **leaf)
{
    return walk_leaf_at(g_vmm.pml4_phys, va, create, vm_flags, leaf, &g_vmm.table_pages);
}

static int map_internal(virt_addr_t va, phys_addr_t pa, uint64_t vm_flags, int exist_ok)
{
    robo_pte_t *leaf;
    uint64_t hw;

    if (!robo_virtual_address_is_canonical(va) || !robo_virt_aligned(va))
        return 0;
    if (pa == ROBO_INVALID_PHYS_ADDR || !robo_page_aligned(pa))
        return 0;
    if (!robo_vm_flags_valid(vm_flags))
        return 0;
    if (!(vm_flags & ROBO_VM_USER))
        vm_flags |= ROBO_VM_KERNEL;

    if (!walk_leaf(va, 1, vm_flags, &leaf) || !leaf)
        return 0;
    if (robo_pte_present(*leaf) && !exist_ok)
        return 0;

    hw = robo_vm_to_pte_flags(vm_flags, g_vmm.nx);
    *leaf = robo_pte_make(pa, hw);
    maybe_invlpg(va);
    return 1;
}

static int map_range_identity(phys_addr_t start, uint64_t length, uint64_t vm_flags)
{
    phys_addr_t end, p;
    if (!length)
        return 1;
    if (!robo_phys_end(start, length, &end))
        return 0;
    start = robo_page_align_down(start);
    end = robo_page_aligned(end) ? end : robo_page_align_up(end);
    if (end == ROBO_INVALID_PHYS_ADDR)
        return 0;
    for (p = start; p < end; p += ROBO_PAGE_SIZE) {
        if (!map_internal((virt_addr_t)p, p, vm_flags, 1))
            return 0;
    }
    return 1;
}

static int protect_range(virt_addr_t start, virt_addr_t end, uint64_t vm_flags)
{
    virt_addr_t va;
    if (end < start)
        return 0;
    start = (virt_addr_t)robo_page_align_down((phys_addr_t)start);
    if (!robo_virt_aligned(end))
        end = (virt_addr_t)robo_page_align_up((phys_addr_t)end);
    for (va = start; va < end; va += ROBO_PAGE_SIZE) {
        if (!robo_vmm_protect(va, vm_flags))
            return 0;
    }
    return 1;
}

phys_addr_t robo_vmm_get_cr3(void)
{
    return robo_cpu_read_cr3();
}

phys_addr_t robo_vmm_kernel_cr3(void)
{
    return g_vmm.pml4_phys;
}

void robo_vmm_load_address_space(phys_addr_t pml4)
{
    robo_cpu_write_cr3(pml4);
}

void robo_vmm_invalidate_page(virt_addr_t address)
{
    robo_cpu_invlpg(address);
}

int robo_vmm_ready(void)
{
    return g_vmm.ready;
}

int robo_vmm_nx_enabled(void)
{
    return g_vmm.nx;
}

bool robo_vmm_map(virt_addr_t virtual_address, phys_addr_t physical_address, uint64_t flags)
{
    if (!g_vmm.ready)
        return false;
    return map_internal(virtual_address, physical_address, flags, 0) != 0;
}

bool robo_vmm_inspect(virt_addr_t virtual_address, phys_addr_t *physical_address, robo_pte_t *pte)
{
    robo_pte_t *leaf;
    if (!g_vmm.pml4_phys || !robo_virtual_address_is_canonical(virtual_address))
        return false;
    if (!walk_leaf(virtual_address, 0, 0, &leaf) || !leaf)
        return false;
    if (!robo_pte_present(*leaf))
        return false;
    if (physical_address)
        *physical_address = robo_pte_phys(*leaf);
    if (pte)
        *pte = *leaf;
    return true;
}

bool robo_vmm_translate(virt_addr_t virtual_address, phys_addr_t *physical_address)
{
    phys_addr_t page;
    if (!physical_address)
        return false;
    if (!robo_vmm_inspect(virtual_address, &page, 0))
        return false;
    *physical_address = page | (phys_addr_t)robo_page_offset(virtual_address);
    return true;
}

bool robo_vmm_unmap(virt_addr_t virtual_address, phys_addr_t *physical_address)
{
    robo_pte_t *leaf;
    if (!g_vmm.ready)
        return false;
    if (!robo_virtual_address_is_canonical(virtual_address) || !robo_virt_aligned(virtual_address))
        return false;
    if (!walk_leaf(virtual_address, 0, 0, &leaf) || !leaf)
        return false;
    if (!robo_pte_present(*leaf))
        return false;
    if (physical_address)
        *physical_address = robo_pte_phys(*leaf);
    *leaf = 0;
    maybe_invlpg(virtual_address);
    return true;
}

bool robo_vmm_protect(virt_addr_t virtual_address, uint64_t flags)
{
    robo_pte_t *leaf;
    phys_addr_t pa;
    uint64_t hw;
    if (!g_vmm.pml4_phys)
        return false;
    if (!robo_virtual_address_is_canonical(virtual_address) || !robo_virt_aligned(virtual_address))
        return false;
    if (!robo_vm_flags_valid(flags))
        return false;
    if (!(flags & ROBO_VM_USER))
        flags |= ROBO_VM_KERNEL;
    if (!walk_leaf(virtual_address, 0, 0, &leaf) || !leaf)
        return false;
    if (!robo_pte_present(*leaf))
        return false;
    pa = robo_pte_phys(*leaf);
    hw = robo_vm_to_pte_flags(flags, g_vmm.nx);
    *leaf = robo_pte_make(pa, hw);
    maybe_invlpg(virtual_address);
    return true;
}

void robo_vmm_report(void)
{
    if (!g_vmm.ready) {
        kprintf("VMM: not initialized\n");
        return;
    }
    kprintf("Roboticela VMM\n");
    kprintf("--------------\n");
    kprintf("Page size:       %llu bytes\n", (unsigned long long)ROBO_PAGE_SIZE);
    kprintf("Levels:          PML4 -> PDPT -> PD -> PT\n");
    kprintf("PML4:            0x%llx\n", (unsigned long long)g_vmm.pml4_phys);
    kprintf("CR3:             0x%llx\n", (unsigned long long)robo_cpu_read_cr3());
    kprintf("NX:              %s\n", g_vmm.nx ? "enabled" : "unavailable");
    kprintf("Identity:        0x0-0x%llx\n", (unsigned long long)g_vmm.ident_limit);
    kprintf("Table pages:     %llu\n", (unsigned long long)g_vmm.table_pages);
    kprintf("Test window:     0x%llx\n", (unsigned long long)ROBO_VMM_TEST_BASE);
    kprintf("Heap window:     0x%llx-0x%llx\n",
            (unsigned long long)ROBO_VMM_HEAP_BASE,
            (unsigned long long)(ROBO_VMM_HEAP_BASE + 256ull * 1024ull * 1024ull));
}

#ifndef VMM_HOST_TEST
void robo_vmm_fb_report(void)
{
    uint32_t mute = 0xA1A1A6u;
    uint32_t white = 0xFFFFFFu;
    if (!g_vmm.ready)
        return;
    kfb_puts("VMM  cr3 ", mute);
    kfb_puthex((unsigned long long)g_vmm.pml4_phys, white);
    kfb_puts(g_vmm.nx ? "  NX on\n" : "  NX off\n", mute);
}
#endif

bool robo_vmm_init(const RoboBootInfo *info)
{
    phys_addr_t limit;
    phys_addr_t old_cr3;
#ifndef VMM_HOST_TEST
    virt_addr_t t0, t1, r0, r1, d0, k0, k1;
#endif

    vmm_fill(&g_vmm, 0, sizeof(g_vmm));
    kprintf("VMM init\n");
    if (!robo_pmm_ready()) {
        kprintf("VMM: PMM not ready\n");
        return false;
    }

    old_cr3 = robo_cpu_read_cr3();
    kprintf("VMM  firmware CR3 0x%llx\n", (unsigned long long)old_cr3);

    robo_cpu_enable_nx();
    robo_cpu_enable_wp();
    g_vmm.nx = robo_cpu_nx_supported() && robo_cpu_nx_enabled();
    kprintf("VMM  NX %s\n", g_vmm.nx ? "enabled" : "unavailable");

    g_vmm.pml4_phys = robo_table_alloc();
    if (g_vmm.pml4_phys == ROBO_INVALID_PHYS_ADDR) {
        kprintf("VMM: PML4 alloc failed\n");
        return false;
    }
    g_vmm.table_pages = 1;

    limit = robo_pmm_limit();
    g_vmm.ident_limit = limit;
    kprintf("VMM  identity 0x0-0x%llx\n", (unsigned long long)limit);
    if (!map_range_identity(0, limit, ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_EXECUTE | ROBO_VM_KERNEL)) {
        kprintf("VMM: identity map failed\n");
        vmm_fill(&g_vmm, 0, sizeof(g_vmm));
        return false;
    }

    if (info && info->kernel_base && info->kernel_size) {
        g_vmm.kernel_base = info->kernel_base;
        g_vmm.kernel_size = info->kernel_size;
        if (!map_range_identity(info->kernel_base, info->kernel_size,
                                ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_EXECUTE | ROBO_VM_KERNEL)) {
            kprintf("VMM: kernel map failed\n");
            vmm_fill(&g_vmm, 0, sizeof(g_vmm));
            return false;
        }
    }
    if (info && info->bootinfo_address && info->bootinfo_size) {
        if (!map_range_identity(info->bootinfo_address, info->bootinfo_size,
                                ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_KERNEL)) {
            kprintf("VMM: bootinfo map failed\n");
            vmm_fill(&g_vmm, 0, sizeof(g_vmm));
            return false;
        }
    }
    if (info && info->version >= 3 && info->debug_log_address && info->debug_log_size) {
        if (!map_range_identity(info->debug_log_address, info->debug_log_size,
                                ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_KERNEL)) {
            kprintf("VMM: debug log map failed\n");
            vmm_fill(&g_vmm, 0, sizeof(g_vmm));
            return false;
        }
    }
    if (info && info->framebuffer_address) {
        g_vmm.fb_base = info->framebuffer_address;
        g_vmm.fb_size = robo_bootinfo_framebuffer_size(info);
        if (g_vmm.fb_size &&
            !map_range_identity(g_vmm.fb_base, g_vmm.fb_size,
                                ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_KERNEL | ROBO_VM_NOCACHE)) {
            kprintf("VMM: framebuffer map failed\n");
            vmm_fill(&g_vmm, 0, sizeof(g_vmm));
            return false;
        }
    }

#ifndef VMM_HOST_TEST
    t0 = (virt_addr_t)(uintptr_t)__text_start;
    t1 = (virt_addr_t)(uintptr_t)__text_end;
    r0 = (virt_addr_t)(uintptr_t)__rodata_start;
    r1 = (virt_addr_t)(uintptr_t)__rodata_end;
    d0 = (virt_addr_t)(uintptr_t)__data_start;
    k0 = (virt_addr_t)(uintptr_t)__kernel_start;
    k1 = (virt_addr_t)(uintptr_t)__kernel_end;
    if (t1 > t0 && !protect_range(t0, t1, ROBO_VM_READ | ROBO_VM_EXECUTE | ROBO_VM_KERNEL))
        kprintf("VMM: text protect skipped\n");
    if (r1 > r0 && !protect_range(r0, r1, ROBO_VM_READ | ROBO_VM_KERNEL))
        kprintf("VMM: rodata protect skipped\n");
    if (k1 > d0 && !protect_range(d0, k1, ROBO_VM_READ | ROBO_VM_WRITE | ROBO_VM_KERNEL))
        kprintf("VMM: data protect skipped\n");
    kprintf("VMM  kernel 0x%llx-0x%llx  text RX  rodata R  data RW\n",
            (unsigned long long)k0, (unsigned long long)k1);
    kprintf("VMM  stack  0x%llx-0x%llx\n",
            (unsigned long long)(uintptr_t)kstack,
            (unsigned long long)(uintptr_t)kstack_end);
#else
    (void)protect_range;
#endif

    g_vmm.ready = 1;
    kprintf("VMM  load CR3 0x%llx  tables %llu\n",
            (unsigned long long)g_vmm.pml4_phys,
            (unsigned long long)g_vmm.table_pages);
    robo_vmm_load_address_space(g_vmm.pml4_phys);
    if (robo_cpu_read_cr3() != g_vmm.pml4_phys) {
        kprintf("VMM: CR3 load failed\n");
        g_vmm.ready = 0;
        return false;
    }
    kprintf("VMM ready\n");
    return true;
}

#ifndef VMM_HOST_TEST
static void aspace_invlpg(phys_addr_t root, virt_addr_t va)
{
    if (g_vmm.ready && robo_cpu_read_cr3() == root)
        robo_cpu_invlpg(va);
}

static void free_table_tree(phys_addr_t phys, int depth)
{
    robo_pte_t *t;
    unsigned i;

    if (phys == ROBO_INVALID_PHYS_ADDR)
        return;
    t = robo_table_ptr(phys);
    if (!t)
        return;
    if (depth > 1) {
        for (i = 0; i < ROBO_PT_ENTRIES; i++) {
            if (robo_pte_present(t[i]) && !robo_pte_huge(t[i]))
                free_table_tree(robo_pte_phys(t[i]), depth - 1);
        }
    }
    robo_page_free(phys);
}

int robo_vmm_aspace_clone(RoboAddressSpace *as)
{
    phys_addr_t pml4;
    robo_pte_t *src, *dst;
    unsigned i;
    uint16_t priv;

    if (!as || !g_vmm.ready || !g_vmm.pml4_phys)
        return 0;
    pml4 = robo_table_alloc();
    if (pml4 == ROBO_INVALID_PHYS_ADDR)
        return 0;
    src = robo_table_ptr(g_vmm.pml4_phys);
    dst = robo_table_ptr(pml4);
    if (!src || !dst) {
        robo_page_free(pml4);
        return 0;
    }
    for (i = 0; i < ROBO_PT_ENTRIES; i++)
        dst[i] = src[i];
    priv = robo_pml4_slot(ROBO_VMM_PROC_BASE);
    dst[priv] = 0;
    as->cr3 = pml4;
    as->cloned = 1;
    as->table_pages = 1;
    as->maps = 0;
    return 1;
}

void robo_vmm_aspace_destroy(RoboAddressSpace *as)
{
    RoboAspaceMap *m, *n;
    robo_pte_t *pml4, *leaf;
    uint16_t priv;
    robo_pte_t e;

    if (!as || !as->cloned)
        return;

    m = as->maps;
    as->maps = 0;
    while (m) {
        n = m->next;
        if (walk_leaf_at(as->cr3, m->va, 0, 0, &leaf, 0) && leaf)
            *leaf = 0;
        aspace_invlpg(as->cr3, m->va);
        if (m->pa != ROBO_INVALID_PHYS_ADDR)
            robo_page_free(m->pa);
        kfree(m);
        m = n;
    }

    pml4 = robo_table_ptr(as->cr3);
    if (pml4) {
        priv = robo_pml4_slot(ROBO_VMM_PROC_BASE);
        e = robo_table_get(pml4, priv);
        if (robo_pte_present(e) && !robo_pte_huge(e))
            free_table_tree(robo_pte_phys(e), 3);
        robo_page_free(as->cr3);
    }
    as->cr3 = 0;
    as->cloned = 0;
    as->table_pages = 0;
}

int robo_vmm_aspace_map(RoboAddressSpace *as, virt_addr_t va, phys_addr_t pa,
                        uint64_t flags)
{
    robo_pte_t *leaf;
    RoboAspaceMap *m;
    uint64_t hw;
    size_t tables;

    if (!as || !as->cr3)
        return 0;
    if (!robo_virtual_address_is_canonical(va) || !robo_virt_aligned(va))
        return 0;
    if (pa == ROBO_INVALID_PHYS_ADDR || !robo_page_aligned(pa))
        return 0;
    if (!robo_vm_flags_valid(flags))
        return 0;
    if (!(flags & ROBO_VM_USER))
        flags |= ROBO_VM_KERNEL;

    tables = (size_t)as->table_pages;
    if (!walk_leaf_at(as->cr3, va, 1, flags, &leaf, &tables) || !leaf)
        return 0;
    as->table_pages = tables;
    if (robo_pte_present(*leaf))
        return 0;

    m = (RoboAspaceMap *)kmalloc(sizeof(*m));
    if (!m)
        return 0;
    m->va = va;
    m->pa = pa;
    m->next = as->maps;
    as->maps = m;

    hw = robo_vm_to_pte_flags(flags, g_vmm.nx);
    *leaf = robo_pte_make(pa, hw);
    aspace_invlpg(as->cr3, va);
    return 1;
}

int robo_vmm_aspace_unmap(RoboAddressSpace *as, virt_addr_t va, phys_addr_t *pa)
{
    robo_pte_t *leaf;
    RoboAspaceMap **slot, *m;

    if (!as || !as->cr3)
        return 0;
    if (!walk_leaf_at(as->cr3, va, 0, 0, &leaf, 0) || !leaf)
        return 0;
    if (!robo_pte_present(*leaf))
        return 0;
    if (pa)
        *pa = robo_pte_phys(*leaf);
    *leaf = 0;
    aspace_invlpg(as->cr3, va);

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

int robo_vmm_aspace_translate(RoboAddressSpace *as, virt_addr_t va,
                              phys_addr_t *pa)
{
    robo_pte_t *leaf;

    if (!as || !as->cr3)
        return 0;
    if (!walk_leaf_at(as->cr3, va, 0, 0, &leaf, 0) || !leaf)
        return 0;
    if (!robo_pte_present(*leaf))
        return 0;
    if (pa)
        *pa = robo_pte_phys(*leaf);
    return 1;
}

int robo_vmm_aspace_protect(RoboAddressSpace *as, virt_addr_t va, uint64_t flags)
{
    robo_pte_t *leaf;
    phys_addr_t pa;
    uint64_t hw;

    if (!as || !as->cr3)
        return 0;
    if (!robo_virtual_address_is_canonical(va) || !robo_virt_aligned(va))
        return 0;
    if (!robo_vm_flags_valid(flags))
        return 0;
    if (!(flags & ROBO_VM_USER))
        flags |= ROBO_VM_KERNEL;
    if (!walk_leaf_at(as->cr3, va, 0, 0, &leaf, 0) || !leaf)
        return 0;
    if (!robo_pte_present(*leaf))
        return 0;
    pa = robo_pte_phys(*leaf);
    hw = robo_vm_to_pte_flags(flags, g_vmm.nx);
    *leaf = robo_pte_make(pa, hw);
    aspace_invlpg(as->cr3, va);
    return 1;
}

int robo_vmm_aspace_inspect(RoboAddressSpace *as, virt_addr_t va,
                            phys_addr_t *pa, robo_pte_t *pte)
{
    robo_pte_t *leaf;

    if (!as || !as->cr3)
        return 0;
    if (!walk_leaf_at(as->cr3, va, 0, 0, &leaf, 0) || !leaf)
        return 0;
    if (!robo_pte_present(*leaf))
        return 0;
    if (pa)
        *pa = robo_pte_phys(*leaf);
    if (pte)
        *pte = *leaf;
    return 1;
}
#endif
