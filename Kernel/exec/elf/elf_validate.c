#include "exec/elf/elf_validate.h"
#include "mm/vmm/paging.h"
#include "mm/page.h"

int UserVaOk(uint64_t va, uint64_t len)
{
    uint64_t end;
    if (!len)
        return VirtualAddressIsCanonical((VirtAddr)va) &&
               va >= RoboUserMin && va < RoboUserMax;
    if (!U64AddOk(va, len, &end))
        return 0;
    if (!VirtualAddressIsCanonical((VirtAddr)va) ||
        !VirtualAddressIsCanonical((VirtAddr)(end - 1ull)))
        return 0;
    if (va < RoboUserMin || end > RoboUserMax)
        return 0;
    return 1;
}

static int SegsOverlap(const Elf64_Phdr *a, const Elf64_Phdr *b)
{
    uint64_t a1, b1;
    if (!U64AddOk(a->pVaddr, a->pMemsz, &a1))
        return 1;
    if (!U64AddOk(b->pVaddr, b->pMemsz, &b1))
        return 1;
    if (a->pVaddr >= b1 || b->pVaddr >= a1)
        return 0;
    return 1;
}

ExecStatus ElfValidate(const RoboElfImage *img, size_t fileSize)
{
    unsigned i, j;
    uint64_t end;

    if (!img)
        return RoboElfInvalidHeader;
    if (img->eh.eType != ET_EXEC)
        return RoboElfUnsupportedType;
    if (img->nload < 1)
        return RoboElfNoLoad;

    for (i = 0; i < img->phnum; i++) {
        const Elf64_Phdr *ph = &img->ph[i];
        if (ph->pType != PT_LOAD)
            continue;
        if (ph->pMemsz == 0)
            continue;
        if (ph->pFilesz > ph->pMemsz)
            return RoboElfInvalidSegment;
        if (!U64AddOk(ph->pOffset, ph->pFilesz, &end))
            return RoboElfAddressOverflow;
        if (end > (uint64_t)fileSize)
            return RoboElfInvalidSegment;
        if (!U64AddOk(ph->pVaddr, ph->pMemsz, &end))
            return RoboElfAddressOverflow;
        if (!UserVaOk(ph->pVaddr, ph->pMemsz))
            return RoboElfAddressInvalid;
        if ((ph->pFlags & PF_W) && (ph->pFlags & PF_X))
            return RoboElfWx;
        if (!(ph->pFlags & (PF_R | PF_W | PF_X)))
            return RoboElfInvalidSegment;
        if (ph->pAlign && (ph->pAlign & (ph->pAlign - 1ull)))
            return RoboElfInvalidSegment;
        if (ph->pAlign >= RoboPageSize) {
            if ((ph->pVaddr & (ph->pAlign - 1ull)) !=
                (ph->pOffset & (ph->pAlign - 1ull)))
                return RoboElfInvalidSegment;
        }
        for (j = 0; j < i; j++) {
            if (img->ph[j].pType != PT_LOAD || img->ph[j].pMemsz == 0)
                continue;
            if (SegsOverlap(ph, &img->ph[j]))
                return RoboElfOverlap;
        }
    }
    if (!UserVaOk(img->eh.eEntry, 1))
        return RoboElfEntryInvalid;
    return RoboExecOk;
}
