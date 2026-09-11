#include "exec/elf/elf.h"

int robo_u64_add_ok(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a + b < a)
        return 0;
    if (out)
        *out = a + b;
    return 1;
}

int robo_elf_ident_ok(const unsigned char *ident, size_t n, robo_exec_status_t *err)
{
    robo_exec_status_t e = ROBO_ELF_INVALID_MAGIC;
    if (!ident || n < EI_NIDENT) {
        if (err) *err = e;
        return 0;
    }
    if (ident[0] != ELFMAG0 || ident[1] != ELFMAG1 ||
        ident[2] != ELFMAG2 || ident[3] != ELFMAG3) {
        if (err) *err = ROBO_ELF_INVALID_MAGIC;
        return 0;
    }
    if (ident[EI_CLASS] != ELFCLASS64) {
        if (err) *err = ROBO_ELF_INVALID_CLASS;
        return 0;
    }
    if (ident[EI_DATA] != ELFDATA2LSB) {
        if (err) *err = ROBO_ELF_INVALID_ENDIAN;
        return 0;
    }
    if (ident[EI_VERSION] != EV_CURRENT) {
        if (err) *err = ROBO_ELF_INVALID_VERSION;
        return 0;
    }
    if (err) *err = ROBO_EXEC_OK;
    return 1;
}
