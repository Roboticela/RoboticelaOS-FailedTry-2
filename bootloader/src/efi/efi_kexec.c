#include "efi.h"
#include "roboboot.h"
#include "roboticela/bootinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cmdline_add_token(char *buf, unsigned cap, const char *tok)
{
    const char *p;
    size_t n, tlen;

    if (!buf || !tok || !tok[0] || cap < 2)
        return;
    p = buf;
    while (*p) {
        unsigned i;
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        for (i = 0; tok[i] && p[i] && p[i] == tok[i]; i++) { }
        if (!tok[i] && (p[i] == 0 || p[i] == ' '))
            return;
        while (*p && *p != ' ')
            p++;
    }
    n = strlen(buf);
    tlen = strlen(tok);
    if (!n) {
        if (tlen + 1 <= cap)
            rb_strcpy(buf, cap, tok);
        return;
    }
    if (n + 1 + tlen + 1 > cap)
        return;
    buf[n] = ' ';
    rb_strcpy(buf + n + 1, cap - n - 1, tok);
}

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
    uint64_t log_phys = 0;
    void *log_buf = 0;
    uint32_t log_size = 128u * 1024u;
    uint32_t log_used = 0, log_count = 0;
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
    log_buf = alloc_pages_any(log_size, &log_phys);
    if (log_buf)
        rb_bootlog_handoff_begin(log_buf, log_size);
    rb_bootlog(NULL, "Kexec  start");
    if (!h || !h->kernel.valid || !h->kernel.bytes) {
        rb_bootlog(NULL, "Kexec  no kernel bytes");
        return RB_ERR;
    }
    nph = rb_elf64_phdrs(h->kernel.bytes, h->kernel.size, ph, RB_MAX_PHDRS);
    if (nph < 1) {
        rb_bootlog(NULL, "Kexec  no PT_LOAD");
        return RB_ERR_FORMAT;
    }
    entry = h->kernel.entry;
    rb_bootlogf(NULL, "Kexec  %d PT_LOAD  entry 0x%llx", nph, (unsigned long long)entry);

    for (i = 0; i < nph; i++) {
        uint64_t start = ph[i].vaddr;
        uint64_t end = ph[i].vaddr + ph[i].memsz;
        if (start < kmin) kmin = start;
        if (end > kmax) kmax = end;
        rb_bootlogf(NULL, "Kexec  range  0x%llx-0x%llx",
                    (unsigned long long)start, (unsigned long long)end);
    }
    kbase = kmin;
    ksize = kmax - kmin;
    if (ksize == 0) return RB_ERR_FORMAT;
    rb_bootlogf(NULL, "Kexec  AllocateAddress  0x%llx  %llu bytes",
                (unsigned long long)kbase, (unsigned long long)ksize);
    if (!alloc_pages_at(kbase, ksize)) {
        rb_bootlogf(NULL, "Kexec  map failed  0x%llx", (unsigned long long)kbase);
        return RB_ERR_NOMEM;
    }
    rb_bootlog(NULL, "Kexec  zero pages");
    memset((void *)(uintptr_t)kbase, 0, (size_t)ksize);
    for (i = 0; i < nph; i++) {
        if (ph[i].filesz) {
            if (ph[i].offset + ph[i].filesz > h->kernel.size) {
                rb_bootlog(NULL, "Kexec  PHDR file overflow");
                return RB_ERR_FORMAT;
            }
            rb_bootlogf(NULL, "Kexec  copy  0x%llx  %llu bytes",
                        (unsigned long long)ph[i].vaddr, (unsigned long long)ph[i].filesz);
            memcpy((void *)(uintptr_t)ph[i].vaddr,
                   h->kernel.bytes + ph[i].offset, (size_t)ph[i].filesz);
        } else
            rb_bootlogf(NULL, "Kexec  bss  0x%llx  %llu",
                        (unsigned long long)ph[i].vaddr, (unsigned long long)ph[i].memsz);
    }
    if (debug)
        rb_debug_flush(NULL);

    if (h->ramdisk.valid && h->ramdisk.size) {
        void *p = alloc_pages_any(h->ramdisk.size, &init_phys);
        if (!p) {
            rb_bootlog(NULL, "Kexec  InitFS alloc failed");
            return RB_ERR_NOMEM;
        }
        memcpy(p, h->ramdisk.bytes, h->ramdisk.size);
        init_size = h->ramdisk.size;
        rb_bootlogf(NULL, "Kexec  InitFS  0x%llx  %llu bytes",
                    (unsigned long long)init_phys, (unsigned long long)init_size);
    } else
        rb_bootlog(NULL, "Kexec  no InitFS");

    info = (RoboBootInfo *)alloc_pages_any(8192, &map_phys);
    if (!info) {
        rb_bootlog(NULL, "Kexec  bootinfo alloc failed");
        return RB_ERR_NOMEM;
    }
    memset(info, 0, 8192);
    regions = (RoboMemoryRegion *)(info + 1);
    rb_bootlogf(NULL, "Kexec  bootinfo  0x%llx", (unsigned long long)map_phys);

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
    info->bootinfo_address = map_phys;
    info->bootinfo_size = 8192;
    info->debug_term_x = (uint32_t)(h->debug_term_x > 0 ? h->debug_term_x : 0);
    info->debug_term_y = (uint32_t)(h->debug_term_y > 0 ? h->debug_term_y : 0);
    info->debug_term_w = (uint32_t)(h->debug_term_w > 0 ? h->debug_term_w : 0);
    info->debug_term_h = (uint32_t)(h->debug_term_h > 0 ? h->debug_term_h : 0);
    info->splash_bar_x = (uint32_t)(h->splash_bar_x > 0 ? h->splash_bar_x : 0);
    info->splash_bar_y = (uint32_t)(h->splash_bar_y > 0 ? h->splash_bar_y : 0);
    info->splash_bar_w = (uint32_t)(h->splash_bar_w > 0 ? h->splash_bar_w : 0);
    info->splash_bar_h = (uint32_t)(h->splash_bar_h > 0 ? h->splash_bar_h : 0);
    info->splash_progress = h->splash_progress;
    if (info->splash_progress > ROBO_BOOT_PROGRESS_MAX)
        info->splash_progress = ROBO_BOOT_PROGRESS_MAX;
    info->splash_handoff = ROBO_BOOT_PROGRESS_HANDOFF;
    if (log_buf && log_phys) {
        rb_bootlog_handoff_info(&log_used, &log_count);
        info->debug_log_address = log_phys;
        info->debug_log_size = log_size;
        info->debug_log_count = log_count;
    }
    rb_strcpy(info->cmdline, sizeof(info->cmdline), h->cmdline);
    rb_bootlogf(NULL, "Bootinfo  magic ROBO  flags 0x%x", (unsigned)info->flags);
    rb_bootlogf(NULL, "Bootinfo  fb 0x%llx  %ux%u  pitch %u  fmt %u",
                (unsigned long long)fb, (unsigned)fw, (unsigned)fh, (unsigned)pitch, (unsigned)pixfmt);
    rb_bootlogf(NULL, "Bootinfo  RSDP 0x%llx", (unsigned long long)info->rsdp_address);
    rb_bootlogf(NULL, "Bootinfo  kernel 0x%llx  %llu",
                (unsigned long long)kbase, (unsigned long long)ksize);
    rb_bootlogf(NULL, "Bootinfo  block 0x%llx  %llu",
                (unsigned long long)info->bootinfo_address,
                (unsigned long long)info->bootinfo_size);
    if (info->debug_log_address)
        rb_bootlogf(NULL, "Bootinfo  logs 0x%llx  %u lines  %u bytes",
                    (unsigned long long)info->debug_log_address,
                    (unsigned)info->debug_log_count,
                    (unsigned)info->debug_log_size);
    if (info->splash_bar_w)
        rb_bootlogf(NULL, "Bootinfo  splash  %ux%u at %u,%u  %u percent",
                    (unsigned)info->splash_bar_w, (unsigned)info->splash_bar_h,
                    (unsigned)info->splash_bar_x, (unsigned)info->splash_bar_y,
                    (unsigned)(info->splash_progress / 100u));
    if (h->cmdline[0])
        rb_bootlogf(NULL, "Bootinfo  cmdline  %s", h->cmdline);
    if (debug)
        rb_debug_flush(NULL);
    if (debug)
        cmdline_add_token(info->cmdline, sizeof(info->cmdline), "debug");
    if (efi_flag_ktest())
        cmdline_add_token(info->cmdline, sizeof(info->cmdline), "selftest");

    map_size = 0;
    if (!gBS || !gBS->GetMemoryMap || !gBS->ExitBootServices) {
        rb_bootlog(NULL, "Kexec  memory map API missing");
        return RB_ERR;
    }
    rb_bootlog(NULL, "Kexec  GetMemoryMap size");
    gBS->GetMemoryMap(&map_size, 0, &map_key, &desc_size, &desc_ver);
    extra = desc_size * 8;
    map_size += extra;
    rb_bootlogf(NULL, "Kexec  map buffer  %u  desc %u  ver %u",
                (unsigned)map_size, (unsigned)desc_size, (unsigned)desc_ver);
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
        rb_bootlogf(NULL, "Kexec  memory map  %u regions  try %d",
                    (unsigned)info->memory_map_count, i + 1);
        if (debug && i == 0) {
            rb_bootlog(NULL, "Kexec  ExitBootServices");
            rb_debug_flush(NULL);
        }
        rb_bootlog_freeze();
        st = gBS->ExitBootServices(gImage, map_key);
        if (st == EFI_SUCCESS) {
            rb_bootlogf(NULL, "Kexec  jump  0x%llx", (unsigned long long)entry);
            rb_bootlog_handoff_info(&log_used, &log_count);
            if (info->debug_log_address)
                info->debug_log_count = log_count;
            jump_kernel(entry, info);
            return RB_ERR_FAIL;
        }
        rb_bootlogf(NULL, "Kexec  ExitBootServices retry  status 0x%llx",
                    (unsigned long long)st);
        map_size = size + extra;
        free(efi_map);
        efi_map = (EFI_MEMORY_DESCRIPTOR *)malloc(map_size);
        if (!efi_map) return RB_ERR_NOMEM;
    }
    return RB_ERR_FAIL;
}
