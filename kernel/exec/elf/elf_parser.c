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

static int range_in(size_t off, size_t n, size_t cap)
{
    uint64_t end;
    if (!robo_u64_add_ok((uint64_t)off, (uint64_t)n, &end))
        return 0;
    return end <= (uint64_t)cap;
}

robo_exec_status_t robo_elf_parse(const void *bytes, size_t size, RoboElfImage *out)
{
    const unsigned char *p;
    robo_exec_status_t err;
    unsigned i;
    uint64_t phoff, phsz;

    if (!bytes || !out || size < sizeof(Elf64_Ehdr))
        return ROBO_ELF_INVALID_HEADER;
    p = (const unsigned char *)bytes;
    if (!robo_elf_ident_ok(p, size, &err))
        return err;

    out->phnum = 0;
    out->nload = 0;
    out->eh.e_ident[0] = p[0];
    out->eh.e_ident[1] = p[1];
    out->eh.e_ident[2] = p[2];
    out->eh.e_ident[3] = p[3];
    {
        unsigned k;
        for (k = 4; k < 16; k++)
            out->eh.e_ident[k] = p[k];
    }
    out->eh.e_type = rd16(p + 16);
    out->eh.e_machine = rd16(p + 18);
    out->eh.e_version = rd32(p + 20);
    out->eh.e_entry = rd64(p + 24);
    out->eh.e_phoff = rd64(p + 32);
    out->eh.e_shoff = rd64(p + 40);
    out->eh.e_flags = rd32(p + 48);
    out->eh.e_ehsize = rd16(p + 52);
    out->eh.e_phentsize = rd16(p + 54);
    out->eh.e_phnum = rd16(p + 56);
    out->eh.e_shentsize = rd16(p + 58);
    out->eh.e_shnum = rd16(p + 60);
    out->eh.e_shstrndx = rd16(p + 62);

    if (out->eh.e_ehsize != sizeof(Elf64_Ehdr))
        return ROBO_ELF_INVALID_HEADER;
    if (out->eh.e_machine != EM_X86_64)
        return ROBO_ELF_INVALID_MACHINE;
    if (out->eh.e_version != EV_CURRENT)
        return ROBO_ELF_INVALID_VERSION;
    if (out->eh.e_phentsize != sizeof(Elf64_Phdr))
        return ROBO_ELF_INVALID_PROGRAM_HEADER;
    if (out->eh.e_phnum == 0 || out->eh.e_phnum > ROBO_ELF_MAX_PHDRS)
        return ROBO_ELF_INVALID_PROGRAM_HEADER;

    phoff = out->eh.e_phoff;
    phsz = (uint64_t)out->eh.e_phnum * (uint64_t)out->eh.e_phentsize;
    if (!range_in((size_t)phoff, (size_t)phsz, size))
        return ROBO_ELF_INVALID_PROGRAM_HEADER;

    out->phnum = out->eh.e_phnum;
    for (i = 0; i < out->phnum; i++) {
        const unsigned char *ph = p + phoff + (uint64_t)i * sizeof(Elf64_Phdr);
        Elf64_Phdr *d = &out->ph[i];
        d->p_type = rd32(ph + 0);
        d->p_flags = rd32(ph + 4);
        d->p_offset = rd64(ph + 8);
        d->p_vaddr = rd64(ph + 16);
        d->p_paddr = rd64(ph + 24);
        d->p_filesz = rd64(ph + 32);
        d->p_memsz = rd64(ph + 40);
        d->p_align = rd64(ph + 48);
        if (d->p_type == PT_LOAD && d->p_memsz)
            out->nload++;
    }
    return ROBO_EXEC_OK;
}
