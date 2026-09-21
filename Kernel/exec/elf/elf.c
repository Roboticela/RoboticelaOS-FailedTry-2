#include "exec/elf/elf.h"

int U64AddOk(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a + b < a)
        return 0;
    if (out)
        *out = a + b;
    return 1;
}

int ElfIdentOk(const unsigned char *ident, size_t n, ExecStatus *err)
{
    ExecStatus e = RoboElfInvalidMagic;
    if (!ident || n < EI_NIDENT) {
        if (err) *err = e;
        return 0;
    }
    if (ident[0] != ELFMAG0 || ident[1] != ELFMAG1 ||
        ident[2] != ELFMAG2 || ident[3] != ELFMAG3) {
        if (err) *err = RoboElfInvalidMagic;
        return 0;
    }
    if (ident[EI_CLASS] != ELFCLASS64) {
        if (err) *err = RoboElfInvalidClass;
        return 0;
    }
    if (ident[EI_DATA] != ELFDATA2LSB) {
        if (err) *err = RoboElfInvalidEndian;
        return 0;
    }
    if (ident[EI_VERSION] != EV_CURRENT) {
        if (err) *err = RoboElfInvalidVersion;
        return 0;
    }
    if (err) *err = RoboExecOk;
    return 1;
}
