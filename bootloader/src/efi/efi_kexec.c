#include "efi.h"
#include "roboboot.h"
#include "roboticela/bootinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static EFI_GUID k_acpi20 = {0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
static EFI_GUID k_acpi10 = {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};

static int guid_eq(const EFI_GUID *a, const EFI_GUID *b)
{
    return memcmp(a, b, sizeof(EFI_GUID)) == 0;
}

static uint64_t find_rsdp(void)
{
    UINTN i;
    uint64_t acpi10 = 0;
    if (!gST || !gST->ConfigurationTable) return 0;
    for (i = 0; i < gST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *t = &gST->ConfigurationTable[i];
        if (guid_eq(&t->VendorGuid, &k_acpi20))
            return (uint64_t)(uintptr_t)t->VendorTable;
        if (guid_eq(&t->VendorGuid, &k_acpi10))
            acpi10 = (uint64_t)(uintptr_t)t->VendorTable;
    }
    return acpi10;
}

static int alloc_pages_at(uint64_t addr, uint64_t bytes)
{
    EFI_PHYSICAL_ADDRESS p = addr;
    UINTN pages = (UINTN)((bytes + 4095ull) / 4096ull);
    if (!pages) pages = 1;
    if (!gBS || !gBS->AllocatePages) return 0;
    return gBS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &p) == EFI_SUCCESS;
}

static void *alloc_pages_any(uint64_t bytes, uint64_t *phys)
{
    EFI_PHYSICAL_ADDRESS p = 0;
    UINTN pages = (UINTN)((bytes + 4095ull) / 4096ull);
    if (!pages) pages = 1;
    if (!gBS || !gBS->AllocatePages) return 0;
    if (gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &p) != EFI_SUCCESS)
        return 0;
    if (phys) *phys = (uint64_t)p;
    return (void *)(uintptr_t)p;
}

static uint32_t map_type(uint32_t efi_type)
{
    switch (efi_type) {
    case EfiConventionalMemory:
        return ROBO_MEM_AVAILABLE;
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
        return ROBO_MEM_LOADER;
    case EfiACPIReclaimMemory:
        return ROBO_MEM_ACPI;
    case EfiACPIMemoryNVS:
        return ROBO_MEM_NVS;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
        return ROBO_MEM_MMIO;
    default:
        return ROBO_MEM_RESERVED;
    }
}

static void jump_kernel(uint64_t entry, RoboBootInfo *info)
{
    __asm__ __volatile__(
        "cli\n\t"
        "mov %[info], %%rcx\n\t"
        "mov %[info], %%rdi\n\t"
        "jmp *%[entry]\n\t"
        :
        : [entry] "r"(entry), [info] "r"(info)
        : "rcx", "rdi", "memory");
}

int efi_start_kernel(const rb_target *t, const rb_handoff *h, int debug)
{
    rb_phdr ph[RB_MAX_PHDRS];
    int nph, i;
    uint64_t kmin = ~0ull, kmax = 0, kbase = 0, ksize = 0;
    uint64_t init_phys = 0, init_size = 0;
    uint64_t map_phys = 0;
    RoboBootInfo *info;
    RoboMemoryRegion *regions;
    UINTN map_size = 0, map_key = 0, desc_size = 0;
    uint32_t desc_ver = 0;
    EFI_MEMORY_DESCRIPTOR *efi_map = 0;
    UINTN extra;
    uint32_t fw, fh, pitch, bpp, pixfmt;
    uint64_t fb;
    uint64_t entry;

    (void)t;
    if (!h || !h->kernel.valid || !h->kernel.bytes) return RB_ERR;
    nph = rb_elf64_phdrs(h->kernel.bytes, h->kernel.size, ph, RB_MAX_PHDRS);
    if (nph < 1) return RB_ERR_FORMAT;
    entry = h->kernel.entry;

    for (i = 0; i < nph; i++) {
        uint64_t start = ph[i].vaddr;
        uint64_t end = ph[i].vaddr + ph[i].memsz;
        if (start < kmin) kmin = start;
        if (end > kmax) kmax = end;
    }
    kbase = kmin;
    ksize = kmax - kmin;
    if (ksize == 0) return RB_ERR_FORMAT;
    if (!alloc_pages_at(kbase, ksize)) {
        rb_logf("kernel AllocateAddress 0x%llx failed", (unsigned long long)kbase);
        return RB_ERR_NOMEM;
    }
    memset((void *)(uintptr_t)kbase, 0, (size_t)ksize);
    for (i = 0; i < nph; i++) {
        if (ph[i].filesz) {
            if (ph[i].offset + ph[i].filesz > h->kernel.size) return RB_ERR_FORMAT;
            memcpy((void *)(uintptr_t)ph[i].vaddr,
                   h->kernel.bytes + ph[i].offset, (size_t)ph[i].filesz);
        }
    }

    if (h->ramdisk.valid && h->ramdisk.size) {
        void *p = alloc_pages_any(h->ramdisk.size, &init_phys);
        if (!p) return RB_ERR_NOMEM;
        memcpy(p, h->ramdisk.bytes, h->ramdisk.size);
        init_size = h->ramdisk.size;
    }

    info = (RoboBootInfo *)alloc_pages_any(8192, &map_phys);
    if (!info) return RB_ERR_NOMEM;
    memset(info, 0, 8192);
    regions = (RoboMemoryRegion *)(info + 1);

    efi_graphics_info(&fb, &fw, &fh, &pitch, &bpp, &pixfmt);
    info->magic = ROBO_BOOTINFO_MAGIC;
    info->version = ROBO_BOOTINFO_VERSION;
    info->flags = debug ? ROBO_BOOT_DEBUG : 0;
    info->framebuffer_address = fb;
    info->framebuffer_width = fw;
    info->framebuffer_height = fh;
    info->framebuffer_pitch = pitch;
    info->framebuffer_bpp = bpp;
    info->framebuffer_pixfmt = pixfmt;
    info->rsdp_address = find_rsdp();
    info->kernel_base = kbase;
    info->kernel_size = ksize;
    info->initrd_address = init_phys;
    info->initrd_size = init_size;
    info->debug_term_x = (uint32_t)(h->debug_term_x > 0 ? h->debug_term_x : 0);
    info->debug_term_y = (uint32_t)(h->debug_term_y > 0 ? h->debug_term_y : 0);
    info->debug_term_w = (uint32_t)(h->debug_term_w > 0 ? h->debug_term_w : 0);
    info->debug_term_h = (uint32_t)(h->debug_term_h > 0 ? h->debug_term_h : 0);
    rb_strcpy(info->cmdline, sizeof(info->cmdline), h->cmdline);
    if (debug) {
        const char *p = info->cmdline;
        int has = 0;
        while (*p) {
            if (p[0] == 'd' && p[1] == 'e' && p[2] == 'b' && p[3] == 'u' && p[4] == 'g') { has = 1; break; }
            p++;
        }
        if (!has) {
            size_t n = strlen(info->cmdline);
            if (n && n + 7 < sizeof(info->cmdline)) {
                info->cmdline[n] = ' ';
                rb_strcpy(info->cmdline + n + 1, sizeof(info->cmdline) - n - 1, "debug");
            } else if (!n) {
                rb_strcpy(info->cmdline, sizeof(info->cmdline), "debug");
            }
        }
    }

    map_size = 0;
    if (!gBS || !gBS->GetMemoryMap || !gBS->ExitBootServices) return RB_ERR;
    rb_logf("starting kernel at 0x%llx", (unsigned long long)entry);
    if (debug)
        rb_bootlogf(NULL, "Starting kernel at 0x%llx", (unsigned long long)entry);
    gBS->GetMemoryMap(&map_size, 0, &map_key, &desc_size, &desc_ver);
    extra = desc_size * 8;
    map_size += extra;
    efi_map = (EFI_MEMORY_DESCRIPTOR *)malloc(map_size);
    if (!efi_map) return RB_ERR_NOMEM;

    for (i = 0; i < 8; i++) {
        EFI_STATUS st;
        UINTN size = map_size;
        st = gBS->GetMemoryMap(&size, efi_map, &map_key, &desc_size, &desc_ver);
        if (st != EFI_SUCCESS) {
            free(efi_map);
            return RB_ERR;
        }
        {
            UINTN off, count = 0;
            uint8_t *raw = (uint8_t *)efi_map;
            UINTN maxn = (8192 - sizeof(RoboBootInfo)) / sizeof(RoboMemoryRegion);
            for (off = 0; off + desc_size <= size && count < maxn; off += desc_size) {
                EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)(raw + off);
                regions[count].address = d->PhysicalStart;
                regions[count].length = d->NumberOfPages * 4096ull;
                regions[count].type = map_type(d->Type);
                regions[count].reserved = 0;
                count++;
            }
            info->memory_map = (uint64_t)(uintptr_t)regions;
            info->memory_map_count = count;
        }
        if (debug && i == 0) {
            rb_bootlogf(NULL, "Kernel mapped  0x%llx  %llu bytes",
                        (unsigned long long)kbase, (unsigned long long)ksize);
            rb_bootlogf(NULL, "Memory map  %u regions", (unsigned)info->memory_map_count);
            rb_bootlog(NULL, "ExitBootServices");
            rb_debug_flush(NULL);
        }
        st = gBS->ExitBootServices(gImage, map_key);
        if (st == EFI_SUCCESS) {
            jump_kernel(entry, info);
            return RB_ERR_FAIL;
        }
        map_size = size + extra;
        free(efi_map);
        efi_map = (EFI_MEMORY_DESCRIPTOR *)malloc(map_size);
        if (!efi_map) return RB_ERR_NOMEM;
    }
    return RB_ERR_FAIL;
}
