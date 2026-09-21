#include "exec/elf/elf_parser.h"

static uint16_t rd16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const unsigned char *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static int RangeIn(size_t off, size_t n, size_t cap)
{
    uint64_t end;
    if (!U64AddOk((uint64_t)off, (uint64_t)n, &end))
        return 0;
    return end <= (uint64_t)cap;
}

ExecStatus ElfParse(const void *bytes, size_t size, RoboElfImage *out)
{
    const unsigned char *p;
    ExecStatus err;
    unsigned i;
    uint64_t phoff, phsz;

    if (!bytes || !out || size < sizeof(Elf64_Ehdr))
        return RoboElfInvalidHeader;
    p = (const unsigned char *)bytes;
    if (!ElfIdentOk(p, size, &err))
        return err;

    out->phnum = 0;
    out->nload = 0;
    out->eh.eIdent[0] = p[0];
    out->eh.eIdent[1] = p[1];
    out->eh.eIdent[2] = p[2];
    out->eh.eIdent[3] = p[3];
    {
        unsigned k;
        for (k = 4; k < 16; k++)
            out->eh.eIdent[k] = p[k];
    }
    out->eh.eType = rd16(p + 16);
    out->eh.eMachine = rd16(p + 18);
    out->eh.eVersion = rd32(p + 20);
    out->eh.eEntry = rd64(p + 24);
    out->eh.ePhoff = rd64(p + 32);
    out->eh.eShoff = rd64(p + 40);
    out->eh.eFlags = rd32(p + 48);
    out->eh.eEhsize = rd16(p + 52);
    out->eh.ePhentsize = rd16(p + 54);
    out->eh.ePhnum = rd16(p + 56);
    out->eh.eShentsize = rd16(p + 58);
    out->eh.eShnum = rd16(p + 60);
    out->eh.eShstrndx = rd16(p + 62);

    if (out->eh.eEhsize != sizeof(Elf64_Ehdr))
        return RoboElfInvalidHeader;
    if (out->eh.eMachine != EM_X86_64)
        return RoboElfInvalidMachine;
    if (out->eh.eVersion != EV_CURRENT)
        return RoboElfInvalidVersion;
    if (out->eh.ePhentsize != sizeof(Elf64_Phdr))
        return RoboElfInvalidProgramHeader;
    if (out->eh.ePhnum == 0 || out->eh.ePhnum > RoboElfMaxPhdrs)
        return RoboElfInvalidProgramHeader;

    phoff = out->eh.ePhoff;
    phsz = (uint64_t)out->eh.ePhnum * (uint64_t)out->eh.ePhentsize;
    if (!RangeIn((size_t)phoff, (size_t)phsz, size))
        return RoboElfInvalidProgramHeader;

    out->phnum = out->eh.ePhnum;
    for (i = 0; i < out->phnum; i++) {
        const unsigned char *ph = p + phoff + (uint64_t)i * sizeof(Elf64_Phdr);
        Elf64_Phdr *d = &out->ph[i];
        d->pType = rd32(ph + 0);
        d->pFlags = rd32(ph + 4);
        d->pOffset = rd64(ph + 8);
        d->pVaddr = rd64(ph + 16);
        d->pPaddr = rd64(ph + 24);
        d->pFilesz = rd64(ph + 32);
        d->pMemsz = rd64(ph + 40);
        d->pAlign = rd64(ph + 48);
        if (d->pType == PT_LOAD && d->pMemsz)
            out->nload++;
    }
    return RoboExecOk;
}
