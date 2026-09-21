#include "syscall/syscall_validate.h"
#include "syscall/syscall_errors.h"
#include "exec/elf/elf.h"
#include "exec/elf/elf_validate.h"
#include "mm/vmm/vmm.h"
#include "mm/vmm/paging.h"
#include "mm/page.h"
#include "arch/x86_64/gdt.h"

int U64AddOkSys(uint64_t a, uint64_t b, uint64_t *out)
{
    return U64AddOk(a, b, out);
}

static int PageAccessOk(RoboAddressSpace *as, uint64_t page, uint64_t access)
{
    PhysAddr pa;
    Pte pte;

    if (!VmmAspaceInspect(as, (VirtAddr)page, &pa, &pte))
        return 0;
    if (!(pte & RoboPteP) || !(pte & RoboPteUs))
        return 0;
    if ((access & RoboUserAccessWrite) && !(pte & RoboPteRw))
        return 0;
    if ((access & RoboUserAccessExec) && (pte & RoboPteNx))
        return 0;
    (void)pa;
    return 1;
}

int UserRangeValid(RoboAddressSpace *as, uint64_t address,
                         uint64_t length, uint64_t access)
{
    uint64_t page, end;

    if (!as)
        return 0;
    if (!length)
        return UserVaOk(address, 1);
    if (!UserVaOk(address, length))
        return 0;
    if (!U64AddOk(address, length, &end))
        return 0;
    page = address & ~RoboPageMask;
    for (; page < end; page += RoboPageSize) {
        if (!PageAccessOk(as, page, access))
            return 0;
    }
    return 1;
}

int CopyFromUser(RoboAddressSpace *as, void *kernelDestination,
                        uint64_t userSource, size_t length)
{
    unsigned char *dst = (unsigned char *)kernelDestination;
    size_t got = 0;

    if (!length)
        return 0;
    if (!dst || !as)
        return -RoboEfault;
    if (!UserRangeValid(as, userSource, (uint64_t)length,
                               RoboUserAccessRead))
        return -RoboEfault;
    while (got < length) {
        uint64_t page = (userSource + got) & ~RoboPageMask;
        uint64_t off = (userSource + got) & RoboPageMask;
        uint64_t chunk = RoboPageSize - off;
        PhysAddr pa;
        const unsigned char *src;
        uint64_t i;

        if (chunk > length - got)
            chunk = length - got;
        if (!VmmAspaceTranslate(as, (VirtAddr)page, &pa))
            return -RoboEfault;
        src = (const unsigned char *)(uintptr_t)(pa + off);
        for (i = 0; i < chunk; i++)
            dst[got + i] = src[i];
        got += (size_t)chunk;
    }
    return 0;
}

int CopyToUser(RoboAddressSpace *as, uint64_t userDestination,
                      const void *kernelSource, size_t length)
{
    const unsigned char *src = (const unsigned char *)kernelSource;
    size_t got = 0;

    if (!length)
        return 0;
    if (!src || !as)
        return -RoboEfault;
    if (!UserRangeValid(as, userDestination, (uint64_t)length,
                               RoboUserAccessWrite))
        return -RoboEfault;
    while (got < length) {
        uint64_t page = (userDestination + got) & ~RoboPageMask;
        uint64_t off = (userDestination + got) & RoboPageMask;
        uint64_t chunk = RoboPageSize - off;
        PhysAddr pa;
        unsigned char *dst;
        uint64_t i;

        if (chunk > length - got)
            chunk = length - got;
        if (!VmmAspaceTranslate(as, (VirtAddr)page, &pa))
            return -RoboEfault;
        dst = (unsigned char *)(uintptr_t)(pa + off);
        for (i = 0; i < chunk; i++)
            dst[i] = src[got + i];
        got += (size_t)chunk;
    }
    return 0;
}

int UserStringValid(RoboAddressSpace *as, uint64_t address,
                           size_t maxLength, size_t *outLen)
{
    size_t n = 0;

    if (!as || !maxLength)
        return 0;
    while (n < maxLength) {
        unsigned char c;
        if (CopyFromUser(as, &c, address + n, 1) != 0)
            return 0;
        n++;
        if (c == 0) {
            if (outLen)
                *outLen = n - 1;
            return 1;
        }
    }
    return 0;
}

int CopyUserString(RoboAddressSpace *as, char *kernelDestination,
                          uint64_t userSource, size_t maxLength)
{
    size_t n = 0;

    if (!kernelDestination || !maxLength)
        return -RoboEinval;
    if (!UserStringValid(as, userSource, maxLength, &n))
        return -RoboEfault;
    if (n + 1 > maxLength)
        return -RoboEinval;
    if (CopyFromUser(as, kernelDestination, userSource, n + 1) != 0)
        return -RoboEfault;
    kernelDestination[n] = 0;
    return 0;
}

int UserReturnOk(const struct Iframe *frame)
{
    if (!frame)
        return 0;
    if ((frame->cs & 3ull) != 3ull)
        return 0;
    if (frame->cs != RoboGdtUserCodeRpl)
        return 0;
    if (frame->ss != RoboGdtUserDataRpl)
        return 0;
    if (!UserVaOk(frame->rip, 1))
        return 0;
    if (!UserVaOk(frame->rsp, 1))
        return 0;
    return 1;
}
