#include "mm/vmm/paging.h"

#include <stdio.h>
#include <string.h>

static int g_pass;
static int g_fails;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

int main(void)
{
    CHECK(robo_pml4_index(0) == 0, "pml4 0");
    CHECK(robo_pdpt_index(0) == 0, "pdpt 0");
    CHECK(robo_pd_index(0) == 0, "pd 0");
    CHECK(robo_pt_index(0) == 0, "pt 0");
    CHECK(robo_page_offset(0x1234) == 0x234, "offset");
    CHECK(robo_pt_index(0x12345678ull) == 0x145, "pt sample");
    CHECK(robo_pd_index(0x12345678ull) == 0x91, "pd sample");
    CHECK(robo_pml4_index(ROBO_VMM_TEST_BASE) == 256, "test pml4");
    CHECK(robo_virtual_address_is_canonical(0), "canon 0");
    CHECK(robo_virtual_address_is_canonical(ROBO_VMM_TEST_BASE), "canon hh");
    CHECK(!robo_virtual_address_is_canonical(0x0000800000000000ull), "noncanon");
    CHECK(robo_virt_aligned(0x1000), "aligned");
    CHECK(!robo_virt_aligned(1), "unaligned");
    CHECK(robo_vm_flags_valid(ROBO_VM_READ | ROBO_VM_KERNEL), "flags rw");
    CHECK(!robo_vm_flags_valid(ROBO_VM_USER | ROBO_VM_KERNEL), "user+kernel");
    CHECK(!robo_vm_flags_valid(0), "empty flags");
    {
        uint64_t hw = robo_vm_to_pte_flags(ROBO_VM_READ | ROBO_VM_WRITE, 1);
        CHECK((hw & ROBO_PTE_P) && (hw & ROBO_PTE_RW) && (hw & ROBO_PTE_NX), "rw nx");
        hw = robo_vm_to_pte_flags(ROBO_VM_READ | ROBO_VM_EXECUTE, 1);
        CHECK((hw & ROBO_PTE_P) && !(hw & ROBO_PTE_RW) && !(hw & ROBO_PTE_NX), "rx");
        hw = robo_vm_to_pte_flags(ROBO_VM_READ | ROBO_VM_USER, 1);
        CHECK((hw & ROBO_PTE_US) && !(hw & ROBO_PTE_RW), "user ro");
    }
    CHECK((robo_pte_make(0x401000, ROBO_PTE_P) & ROBO_PTE_ADDR) == 0x401000, "pte addr");
    printf("VMM host tests  %d passed  %d failed\n", g_pass, g_fails);
    return g_fails ? 1 : 0;
}
