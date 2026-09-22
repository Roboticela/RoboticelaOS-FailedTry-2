#include "exec/elf/elf_loader.h"
#include "exec/elf/elf_parser.h"
#include "exec/elf/elf_validate.h"
#include "mm/vmm/vmm.h"
#include "mm/pmm.h"
#include "mm/page.h"
#include "kio.h"

static int gElfQuiet = 1;

void ElfSetQuiet(int quiet)
{
    gElfQuiet = quiet ? 1 : 0;
}

static void CopyBytes(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
}

static void ZeroBytes(void *dst, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--)
        *d++ = 0;
}

static uint64_t ElfVmFlags(uint32_t pf, int writable)
{
    uint64_t f = RoboVmUser | RoboVmRead;
    if (writable || (pf & PF_W))
        f |= RoboVmWrite;
    if ((pf & PF_X) && !writable)
        f |= RoboVmExecute;
    return f;
}

static int MapPage(RoboAddressSpace *as, VirtAddr va, PhysAddr *pa, uint64_t flags)
{
    PhysAddr page = PageAlloc();
    if (page == RoboInvalidPhysAddr)
        return 0;
    ZeroBytes((void *)(uintptr_t)page, RoboPageSize);
    if (!VmmAspaceMap(as, va, page, flags)) {
        PageFree(page);
        return 0;
    }
    if (pa)
        *pa = page;
    return 1;
}

int ElfEntryExecutable(RoboAddressSpace *as, uint64_t entry)
{
    PhysAddr pa;
    Pte pte;
    VirtAddr page;
    if (!as || !UserVaOk(entry, 1))
        return 0;
    page = entry & ~RoboPageMask;
    if (!VmmAspaceInspect(as, page, &pa, &pte))
        return 0;
    if (!(pte & RoboPteP) || !(pte & RoboPteUs))
        return 0;
    if (pte & RoboPteNx)
        return 0;
    (void)pa;
    return 1;
}

static ExecStatus LoadSegment(RoboAddressSpace *as, const uint8_t *file,
                                       size_t fileSize, const Elf64_Phdr *ph)
{
    uint64_t va0, va1, page, off, CopyN, ZeroN, fileLeft, bssLeft;
    uint64_t flagsTmp, flagsFin;
    const uint8_t *src;
    uint8_t *dst;

    (void)fileSize;
    va0 = ph->pVaddr & ~RoboPageMask;
    if (!U64AddOk(ph->pVaddr, ph->pMemsz, &va1))
        return RoboElfAddressOverflow;
    va1 = (va1 + RoboPageMask) & ~RoboPageMask;
    flagsTmp = RoboVmUser | RoboVmRead | RoboVmWrite;
    flagsFin = ElfVmFlags(ph->pFlags, 0);
    off = ph->pVaddr - va0;
    src = file + ph->pOffset;
    fileLeft = ph->pFilesz;
    bssLeft = ph->pMemsz - ph->pFilesz;

    for (page = va0; page < va1; page += RoboPageSize) {
        PhysAddr pa;
        uint64_t skip = (page == va0) ? off : 0;
        if (!MapPage(as, (VirtAddr)page, &pa, flagsTmp))
            return RoboElfMemoryFailure;
        dst = (uint8_t *)(uintptr_t)pa;
        CopyN = RoboPageSize - skip;
        if (CopyN > fileLeft)
            CopyN = fileLeft;
        if (CopyN)
            CopyBytes(dst + skip, src, (size_t)CopyN);
        src += CopyN;
        fileLeft -= CopyN;
        ZeroN = RoboPageSize - skip - CopyN;
        if (ZeroN > bssLeft)
            ZeroN = bssLeft;
        /* remainder of the page after file bytes is BSS or unused padding */
        if (RoboPageSize - skip - CopyN)
            ZeroBytes(dst + skip + CopyN, (size_t)(RoboPageSize - skip - CopyN));
        if (ZeroN)
            bssLeft -= ZeroN;
        if (!VmmAspaceProtect(as, (VirtAddr)page, flagsFin))
            return RoboElfMemoryFailure;
    }
    return RoboExecOk;
}

static ExecStatus MapStack(RoboAddressSpace *as, RoboExecImage *out)
{
    uint64_t top = RoboUserStackTop;
    uint64_t size = RoboUserStackSize;
    uint64_t base, page;
    uint64_t flags = RoboVmUser | RoboVmRead | RoboVmWrite;

    if (top < size)
        return RoboElfAddressInvalid;
    base = top - size;
    if (!UserVaOk(base, size))
        return RoboElfAddressInvalid;
    for (page = base; page < top; page += RoboPageSize) {
        if (!MapPage(as, (VirtAddr)page, 0, flags))
            return RoboElfMemoryFailure;
    }
    out->stackBase = base;
    out->StackTop = top;
    return RoboExecOk;
}

static unsigned StrLen(const char *s)
{
    unsigned n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

static int PlantUserArgv(RoboAddressSpace *as, RoboExecImage *out,
                           const char *argv0, const RoboExecArgv *argv)
{
    uint64_t top = RoboUserStackTop;
    uint64_t page = top - RoboPageSize;
    uint64_t zero = 0;
    uint64_t argc;
    uint64_t ptrs[RoboExecArgvMax];
    uint64_t rsp;
    uint64_t words;
    uint8_t *mem;
    PhysAddr pa;
    unsigned i, n, off, used;
    const char *s;

    if (!VmmAspaceTranslate(as, (VirtAddr)page, &pa))
        return 0;
    mem = (uint8_t *)(uintptr_t)pa;

    n = 0;
    if (argv && argv->argc) {
        n = argv->argc;
        if (n > RoboExecArgvMax)
            n = RoboExecArgvMax;
    } else {
        n = 1;
    }

    used = 0;
    for (i = 0; i < n; i++) {
        unsigned len;
        s = (argv && argv->argc) ? argv->args[i] : argv0;
        if (!s || !s[0])
            s = "user";
        len = StrLen(s);
        if (len > RoboExecArgMax)
            len = RoboExecArgMax;
        used += len + 1u;
    }
    used = (used + 15u) & ~15u;
    argc = n;
    words = 1ull + n + 1ull + 1ull + 2ull;
    if (words & 1ull)
        words++;
    if (used + (unsigned)(words * 8ull) + 16u > RoboPageSize)
        return 0;
    off = (unsigned)RoboPageSize - used;
    {
        unsigned cur = off;
        for (i = 0; i < n; i++) {
            unsigned len;
            s = (argv && argv->argc) ? argv->args[i] : argv0;
            if (!s || !s[0])
                s = "user";
            len = StrLen(s);
            if (len > RoboExecArgMax)
                len = RoboExecArgMax;
            CopyBytes(mem + cur, s, len);
            mem[cur + len] = 0;
            ptrs[i] = page + cur;
            cur += len + 1u;
        }
    }
    rsp = (page + off - words * 8ull) & ~15ull;
    if (rsp < page)
        return 0;
    off = (unsigned)(rsp - page);
    CopyBytes(mem + off, &argc, 8);
    off += 8;
    for (i = 0; i < n; i++) {
        CopyBytes(mem + off, &ptrs[i], 8);
        off += 8;
    }
    CopyBytes(mem + off, &zero, 8);
    off += 8;
    CopyBytes(mem + off, &zero, 8);
    off += 8;
    CopyBytes(mem + off, &zero, 8);
    off += 8;
    CopyBytes(mem + off, &zero, 8);
    out->StackTop = rsp;
    return 1;
}

ExecStatus ElfLoadImage(RoboAddressSpace *as,
                                       const void *bytes, size_t size,
                                       RoboExecImage *out,
                                       const char *argv0,
                                       const RoboExecArgv *argv)
{
    RoboElfImage img;
    ExecStatus st;
    unsigned i;

    if (!as || !bytes || !out)
        return RoboElfInvalidHeader;
    ZeroBytes(out, sizeof(*out));
    st = ElfParse(bytes, size, &img);
    if (st != RoboExecOk)
        return st;
    st = ElfValidate(&img, size);
    if (st != RoboExecOk)
        return st;

    if (!gElfQuiet) {
        kprintf("Class       : ELF64\n");
        kprintf("Machine     : x86_64\n");
        kprintf("Type        : EXEC\n");
        kprintf("Entry       : 0x%llx\n", (unsigned long long)img.eh.eEntry);
        kprintf("PHNum       : %u\n\n", (unsigned)img.phnum);
    }
    for (i = 0; i < img.phnum; i++) {
        const Elf64_Phdr *ph = &img.ph[i];
        if (ph->pType != PT_LOAD || ph->pMemsz == 0)
            continue;
        if (!gElfQuiet) {
            kprintf("Segment %u\n", i);
            kprintf("---------\n");
            kprintf("Type        : LOAD\n");
            kprintf("Offset      : 0x%llx\n", (unsigned long long)ph->pOffset);
            kprintf("VAddr       : 0x%llx\n", (unsigned long long)ph->pVaddr);
            kprintf("File Size   : 0x%llx\n", (unsigned long long)ph->pFilesz);
            kprintf("Mem Size    : 0x%llx\n", (unsigned long long)ph->pMemsz);
            kprintf("Flags       : %s%s%s\n\n",
                    (ph->pFlags & PF_R) ? "R" : "-",
                    (ph->pFlags & PF_W) ? "W" : "-",
                    (ph->pFlags & PF_X) ? "X" : "-");
        }
        st = LoadSegment(as, (const uint8_t *)bytes, size, ph);
        if (st != RoboExecOk) {
            if (!gElfQuiet) {
                kprintf("ELF LOAD FAILED\n\n");
                kprintf("Segment : %u\n", i);
                kprintf("Virtual : 0x%llx\n", (unsigned long long)ph->pVaddr);
                kprintf("Size    : 0x%llx\n\n", (unsigned long long)ph->pMemsz);
                kprintf("Reason:\n%s\n", ExecStatusName(st));
            }
            return st;
        }
        if (!out->textVa && (ph->pFlags & PF_X))
            out->textVa = ph->pVaddr;
    }
    st = MapStack(as, out);
    if (st != RoboExecOk)
        return st;

    /* SysV process entry: RSP % 16 == 0.
     *   argc, argv[], NULL, envp NULL, AT_NULL, 0 */
    if (!PlantUserArgv(as, out, argv0 ? argv0 : "user", argv))
        return RoboElfMemoryFailure;
    out->entry = img.eh.eEntry;
    out->loadBias = 0;
    if (!ElfEntryExecutable(as, out->entry)) {
        if (!gElfQuiet) {
            kprintf("ELF LOAD FAILED\n\n");
            kprintf("Entry : 0x%llx\n", (unsigned long long)out->entry);
            kprintf("Reason:\nEntry point is not inside an executable user mapping.\n");
        }
        return RoboElfEntryInvalid;
    }
    return RoboExecOk;
}
