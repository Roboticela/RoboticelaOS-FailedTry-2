#include "efi.h"
#include "roboboot.h"
#include "roboticela/bootinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void CmdlineAddToken(char *buf, unsigned cap, const char *tok)
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
            Strcpy(buf, cap, tok);
        return;
    }
    if (n + 1 + tlen + 1 > cap)
        return;
    buf[n] = ' ';
    Strcpy(buf + n + 1, cap - n - 1, tok);
}

static EFI_GUID kAcpi20 = {0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
static EFI_GUID kAcpi10 = {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};

static int GuidEq(const EFI_GUID *a, const EFI_GUID *b)
{
    return memcmp(a, b, sizeof(EFI_GUID)) == 0;
}

static uint64_t FindRsdp(void)
{
    UINTN i;
    uint64_t acpi10 = 0;
    if (!gST || !gST->ConfigurationTable) return 0;
    for (i = 0; i < gST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *t = &gST->ConfigurationTable[i];
        if (GuidEq(&t->VendorGuid, &kAcpi20))
            return (uint64_t)(uintptr_t)t->VendorTable;
        if (GuidEq(&t->VendorGuid, &kAcpi10))
            acpi10 = (uint64_t)(uintptr_t)t->VendorTable;
    }
    return acpi10;
}

static int AllocPagesAt(uint64_t addr, uint64_t bytes)
{
    EFI_PHYSICAL_ADDRESS p = addr;
    UINTN pages = (UINTN)((bytes + 4095ull) / 4096ull);
    if (!pages) pages = 1;
    if (!gBS || !gBS->AllocatePages) return 0;
    return gBS->AllocatePages(AllocateAddress, EfiLoaderData, pages, &p) == EFI_SUCCESS;
}

static void *AllocPagesAny(uint64_t bytes, uint64_t *phys)
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

static uint32_t MapType(uint32_t efiType)
{
    switch (efiType) {
    case EfiConventionalMemory:
        return RoboMemAvailable;
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
        return RoboMemLoader;
    case EfiACPIReclaimMemory:
        return RoboMemAcpi;
    case EfiACPIMemoryNVS:
        return RoboMemNvs;
    case EfiMemoryMappedIO:
    case EfiMemoryMappedIOPortSpace:
        return RoboMemMmio;
    default:
        return RoboMemReserved;
    }
}

static void JumpKernel(uint64_t entry, RoboBootInfo *info)
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

int EfiStartKernel(const Target *t, const Handoff *h, int debug)
{
    Phdr ph[RbMaxPhdrs];
    int nph, i;
    uint64_t kmin = ~0ull, kmax = 0, kbase = 0, ksize = 0;
    uint64_t initPhys = 0, initSize = 0;
    uint64_t mapPhys = 0;
    uint64_t logPhys = 0;
    void *logBuf = 0;
    uint32_t logSize = 128u * 1024u;
    uint32_t logUsed = 0, logCount = 0;
    RoboBootInfo *info;
    RoboMemoryRegion *regions;
    UINTN mapSize = 0, MapKey = 0, descSize = 0;
    uint32_t descVer = 0;
    EFI_MEMORY_DESCRIPTOR *efiMap = 0;
    UINTN extra;
    uint32_t fw, fh, pitch, bpp, pixfmt;
    uint64_t fb;
    uint64_t entry;

    (void)t;
    logBuf = AllocPagesAny(logSize, &logPhys);
    if (logBuf)
        BootlogHandoffBegin(logBuf, logSize);
    Bootlog(NULL, "Kexec  start");
    if (!h || !h->kernel.valid || !h->kernel.bytes) {
        Bootlog(NULL, "Kexec  no kernel bytes");
        return RbErr;
    }
    nph = Elf64Phdrs(h->kernel.bytes, h->kernel.size, ph, RbMaxPhdrs);
    if (nph < 1) {
        Bootlog(NULL, "Kexec  no PT_LOAD");
        return RbErrFormat;
    }
    entry = h->kernel.entry;
    Bootlogf(NULL, "Kexec  %d PT_LOAD  entry 0x%llx", nph, (unsigned long long)entry);

    for (i = 0; i < nph; i++) {
        uint64_t start = ph[i].vaddr;
        uint64_t end = ph[i].vaddr + ph[i].memsz;
        if (start < kmin) kmin = start;
        if (end > kmax) kmax = end;
        Bootlogf(NULL, "Kexec  range  0x%llx-0x%llx",
                    (unsigned long long)start, (unsigned long long)end);
    }
    kbase = kmin;
    ksize = kmax - kmin;
    if (ksize == 0) return RbErrFormat;
    Bootlogf(NULL, "Kexec  AllocateAddress  0x%llx  %llu bytes",
                (unsigned long long)kbase, (unsigned long long)ksize);
    if (!AllocPagesAt(kbase, ksize)) {
        Bootlogf(NULL, "Kexec  map failed  0x%llx", (unsigned long long)kbase);
        return RbErrNomem;
    }
    Bootlog(NULL, "Kexec  zero pages");
    memset((void *)(uintptr_t)kbase, 0, (size_t)ksize);
    for (i = 0; i < nph; i++) {
        if (ph[i].filesz) {
            if (ph[i].offset + ph[i].filesz > h->kernel.size) {
                Bootlog(NULL, "Kexec  PHDR file overflow");
                return RbErrFormat;
            }
            Bootlogf(NULL, "Kexec  copy  0x%llx  %llu bytes",
                        (unsigned long long)ph[i].vaddr, (unsigned long long)ph[i].filesz);
            memcpy((void *)(uintptr_t)ph[i].vaddr,
                   h->kernel.bytes + ph[i].offset, (size_t)ph[i].filesz);
        } else
            Bootlogf(NULL, "Kexec  bss  0x%llx  %llu",
                        (unsigned long long)ph[i].vaddr, (unsigned long long)ph[i].memsz);
    }
    if (debug)
        DebugFlush(NULL);

    if (h->ramdisk.valid && h->ramdisk.size) {
        void *p = AllocPagesAny(h->ramdisk.size, &initPhys);
        if (!p) {
            Bootlog(NULL, "Kexec  InitFS alloc failed");
            return RbErrNomem;
        }
        memcpy(p, h->ramdisk.bytes, h->ramdisk.size);
        initSize = h->ramdisk.size;
        Bootlogf(NULL, "Kexec  InitFS  0x%llx  %llu bytes",
                    (unsigned long long)initPhys, (unsigned long long)initSize);
    } else
        Bootlog(NULL, "Kexec  no InitFS");

    info = (RoboBootInfo *)AllocPagesAny(8192, &mapPhys);
    if (!info) {
        Bootlog(NULL, "Kexec  bootinfo alloc failed");
        return RbErrNomem;
    }
    memset(info, 0, 8192);
    regions = (RoboMemoryRegion *)(info + 1);
    Bootlogf(NULL, "Kexec  bootinfo  0x%llx", (unsigned long long)mapPhys);

    EfiGraphicsInfo(&fb, &fw, &fh, &pitch, &bpp, &pixfmt);
    info->magic = RoboBootinfoMagic;
    info->version = RoboBootinfoVersion;
    info->flags = debug ? RoboBootDebug : 0;
    info->framebufferAddress = fb;
    info->framebufferWidth = fw;
    info->framebufferHeight = fh;
    info->framebufferPitch = pitch;
    info->framebufferBpp = bpp;
    info->framebufferPixfmt = pixfmt;
    info->rsdpAddress = FindRsdp();
    info->kernelBase = kbase;
    info->kernelSize = ksize;
    info->initrdAddress = initPhys;
    info->initrdSize = initSize;
    info->bootinfoAddress = mapPhys;
    info->bootinfoSize = 8192;
    info->debugTermX = (uint32_t)(h->debugTermX > 0 ? h->debugTermX : 0);
    info->debugTermY = (uint32_t)(h->debugTermY > 0 ? h->debugTermY : 0);
    info->debugTermW = (uint32_t)(h->debugTermW > 0 ? h->debugTermW : 0);
    info->debugTermH = (uint32_t)(h->debugTermH > 0 ? h->debugTermH : 0);
    info->splashBarX = (uint32_t)(h->splashBarX > 0 ? h->splashBarX : 0);
    info->splashBarY = (uint32_t)(h->splashBarY > 0 ? h->splashBarY : 0);
    info->splashBarW = (uint32_t)(h->splashBarW > 0 ? h->splashBarW : 0);
    info->splashBarH = (uint32_t)(h->splashBarH > 0 ? h->splashBarH : 0);
    info->splashProgress = h->splashProgress;
    if (info->splashProgress > RoboBootProgressMax)
        info->splashProgress = RoboBootProgressMax;
    info->splashHandoff = RoboBootProgressHandoff;
    if (logBuf && logPhys) {
        BootlogHandoffInfo(&logUsed, &logCount);
        info->debugLogAddress = logPhys;
        info->debugLogSize = logSize;
        info->debugLogCount = logCount;
    }
    Strcpy(info->cmdline, sizeof(info->cmdline), h->cmdline);
    Bootlogf(NULL, "Bootinfo  magic ROBO  flags 0x%x", (unsigned)info->flags);
    Bootlogf(NULL, "Bootinfo  fb 0x%llx  %ux%u  pitch %u  fmt %u",
                (unsigned long long)fb, (unsigned)fw, (unsigned)fh, (unsigned)pitch, (unsigned)pixfmt);
    Bootlogf(NULL, "Bootinfo  RSDP 0x%llx", (unsigned long long)info->rsdpAddress);
    Bootlogf(NULL, "Bootinfo  kernel 0x%llx  %llu",
                (unsigned long long)kbase, (unsigned long long)ksize);
    Bootlogf(NULL, "Bootinfo  block 0x%llx  %llu",
                (unsigned long long)info->bootinfoAddress,
                (unsigned long long)info->bootinfoSize);
    if (info->debugLogAddress)
        Bootlogf(NULL, "Bootinfo  logs 0x%llx  %u lines  %u bytes",
                    (unsigned long long)info->debugLogAddress,
                    (unsigned)info->debugLogCount,
                    (unsigned)info->debugLogSize);
    if (info->splashBarW)
        Bootlogf(NULL, "Bootinfo  splash  %ux%u at %u,%u  %u percent",
                    (unsigned)info->splashBarW, (unsigned)info->splashBarH,
                    (unsigned)info->splashBarX, (unsigned)info->splashBarY,
                    (unsigned)(info->splashProgress / 100u));
    if (h->cmdline[0])
        Bootlogf(NULL, "Bootinfo  cmdline  %s", h->cmdline);
    if (debug)
        DebugFlush(NULL);
    if (debug)
        CmdlineAddToken(info->cmdline, sizeof(info->cmdline), "debug");
    if (EfiFlagKtest())
        CmdlineAddToken(info->cmdline, sizeof(info->cmdline), "selftest");

    mapSize = 0;
    if (!gBS || !gBS->GetMemoryMap || !gBS->ExitBootServices) {
        Bootlog(NULL, "Kexec  memory map API missing");
        return RbErr;
    }
    Bootlog(NULL, "Kexec  GetMemoryMap size");
    gBS->GetMemoryMap(&mapSize, 0, &MapKey, &descSize, &descVer);
    extra = descSize * 8;
    mapSize += extra;
    Bootlogf(NULL, "Kexec  map buffer  %u  desc %u  ver %u",
                (unsigned)mapSize, (unsigned)descSize, (unsigned)descVer);
    efiMap = (EFI_MEMORY_DESCRIPTOR *)malloc(mapSize);
    if (!efiMap) return RbErrNomem;

    for (i = 0; i < 8; i++) {
        EFI_STATUS st;
        UINTN size = mapSize;
        st = gBS->GetMemoryMap(&size, efiMap, &MapKey, &descSize, &descVer);
        if (st != EFI_SUCCESS) {
            free(efiMap);
            return RbErr;
        }
        {
            UINTN off, count = 0;
            uint8_t *raw = (uint8_t *)efiMap;
            UINTN maxn = (8192 - sizeof(RoboBootInfo)) / sizeof(RoboMemoryRegion);
            for (off = 0; off + descSize <= size && count < maxn; off += descSize) {
                EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)(raw + off);
                regions[count].address = d->PhysicalStart;
                regions[count].length = d->NumberOfPages * 4096ull;
                regions[count].type = MapType(d->Type);
                regions[count].reserved = 0;
                count++;
            }
            info->memoryMap = (uint64_t)(uintptr_t)regions;
            info->memoryMapCount = count;
        }
        Bootlogf(NULL, "Kexec  memory map  %u regions  try %d",
                    (unsigned)info->memoryMapCount, i + 1);
        if (debug && i == 0) {
            Bootlog(NULL, "Kexec  ExitBootServices");
            DebugFlush(NULL);
        }
        BootlogFreeze();
        st = gBS->ExitBootServices(gImage, MapKey);
        if (st == EFI_SUCCESS) {
            Bootlogf(NULL, "Kexec  jump  0x%llx", (unsigned long long)entry);
            BootlogHandoffInfo(&logUsed, &logCount);
            if (info->debugLogAddress)
                info->debugLogCount = logCount;
            JumpKernel(entry, info);
            return RbErrFail;
        }
        Bootlogf(NULL, "Kexec  ExitBootServices retry  status 0x%llx",
                    (unsigned long long)st);
        mapSize = size + extra;
        free(efiMap);
        efiMap = (EFI_MEMORY_DESCRIPTOR *)malloc(mapSize);
        if (!efiMap) return RbErrNomem;
    }
    return RbErrFail;
}
