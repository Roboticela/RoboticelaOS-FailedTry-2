#include "exec/elf/elf_parser.h"
#include "exec/elf/elf_validate.h"
#include "mm/vmm/paging.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int gPass;
static int gFails;

#define CHECK(cond, msg) do { \
    if (cond) { gPass++; } \
    else { gFails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

static void wr16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void wr32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void wr64(unsigned char *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

static void ident(unsigned char *b)
{
    memset(b, 0, 16);
    b[0] = ELFMAG0;
    b[1] = ELFMAG1;
    b[2] = ELFMAG2;
    b[3] = ELFMAG3;
    b[EI_CLASS] = ELFCLASS64;
    b[EI_DATA] = ELFDATA2LSB;
    b[EI_VERSION] = EV_CURRENT;
}

static void EhCommon(unsigned char *b, uint16_t type, uint16_t phnum,
                      uint64_t entry, uint64_t phoff)
{
    ident(b);
    wr16(b + 16, type);
    wr16(b + 18, EM_X86_64);
    wr32(b + 20, EV_CURRENT);
    wr64(b + 24, entry);
    wr64(b + 32, phoff);
    wr16(b + 52, 64);
    wr16(b + 54, 56);
    wr16(b + 56, phnum);
}

static void PhLoad(unsigned char *p, uint32_t flags, uint64_t off,
                    uint64_t va, uint64_t filesz, uint64_t memsz, uint64_t align)
{
    wr32(p + 0, PT_LOAD);
    wr32(p + 4, flags);
    wr64(p + 8, off);
    wr64(p + 16, va);
    wr64(p + 24, va);
    wr64(p + 32, filesz);
    wr64(p + 40, memsz);
    wr64(p + 48, align);
}

int main(void)
{
    unsigned char file[0x1200];
    RoboElfImage img;
    uint64_t va = RoboUserTextBase;
    ExecStatus st;

    CHECK(sizeof(Elf64_Ehdr) == 64, "ehdr packed 64");
    CHECK(sizeof(Elf64_Phdr) == 56, "phdr packed 56");
    memset(file, 0, sizeof(file));
    EhCommon(file, ET_EXEC, 1, va, 64);
    PhLoad(file + 64, PF_R | PF_X, 0x1000, va, 16, 16, 0x1000);
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboExecOk, "parse valid");
    st = ElfValidate(&img, sizeof(file));
    CHECK(st == RoboExecOk, "validate valid ELF64 EXEC");

    file[0] = 0;
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboElfInvalidMagic, "bad magic");
    ident(file);

    file[EI_CLASS] = 1;
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboElfInvalidClass, "ELF32 rejected");
    file[EI_CLASS] = ELFCLASS64;

    file[EI_DATA] = 2;
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboElfInvalidEndian, "big endian rejected");
    file[EI_DATA] = ELFDATA2LSB;

    wr16(file + 18, 3);
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboElfInvalidMachine, "wrong machine");
    wr16(file + 18, EM_X86_64);

    wr16(file + 52, 32);
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboElfInvalidHeader, "bad ehsize");
    wr16(file + 52, 64);

    wr16(file + 54, 32);
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboElfInvalidProgramHeader, "bad phentsize");
    wr16(file + 54, 56);

    wr16(file + 56, 17);
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboElfInvalidProgramHeader, "huge phnum");
    wr16(file + 56, 1);

    PhLoad(file + 64, PF_R | PF_X, 0x1000, va, 32, 16, 0x1000);
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboExecOk, "parse filesz>memsz");
    st = ElfValidate(&img, sizeof(file));
    CHECK(st == RoboElfInvalidSegment, "filesz > memsz");
    PhLoad(file + 64, PF_R | PF_X, 0x1000, va, 16, 16, 0x1000);

    PhLoad(file + 64, PF_R | PF_W | PF_X, 0x1000, va, 16, 16, 0x1000);
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboExecOk, "parse WX");
    st = ElfValidate(&img, sizeof(file));
    CHECK(st == RoboElfWx, "W^X");
    PhLoad(file + 64, PF_R | PF_X, 0x1000, va, 16, 16, 0x1000);

    PhLoad(file + 64, PF_R | PF_X, 0x1000, 0x401000ull, 16, 16, 0x1000);
    wr64(file + 24, 0x401000ull);
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboExecOk, "parse kernel va");
    st = ElfValidate(&img, sizeof(file));
    CHECK(st == RoboElfAddressInvalid, "kernel VA rejected");
    wr64(file + 24, va);
    PhLoad(file + 64, PF_R | PF_X, 0x1000, va, 16, 16, 0x1000);

    wr16(file + 16, ET_DYN);
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboExecOk, "parse ET_DYN");
    st = ElfValidate(&img, sizeof(file));
    CHECK(st == RoboElfUnsupportedType, "ET_DYN rejected");
    wr16(file + 16, ET_EXEC);

    wr16(file + 56, 2);
    PhLoad(file + 64, PF_R | PF_X, 0x40, va, 16, 0x1000, 0);
    PhLoad(file + 64 + 56, PF_R | PF_W, 0x80, va + 0x800, 16, 0x1000, 0);
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboExecOk, "parse overlap");
    st = ElfValidate(&img, sizeof(file));
    CHECK(st == RoboElfOverlap, "overlap PT_LOAD");

    wr64(file + 24, va + 0x200000ull);
    wr16(file + 56, 1);
    PhLoad(file + 64, PF_R | PF_X, 0x1000, va, 16, 16, 0x1000);
    st = ElfParse(file, sizeof(file), &img);
    CHECK(st == RoboExecOk, "parse entry in user window");
    st = ElfValidate(&img, sizeof(file));
    CHECK(st == RoboExecOk, "validate does not require segment membership");

    PhLoad(file + 64, PF_R | PF_X, 0x1000, va, 16, 16, 0x1000);
    wr64(file + 8 + 64, ~0ull - 8ull); /* pOffset overflow with filesz */
    wr64(file + 32 + 64, 32);
    wr64(file + 40 + 64, 32);
    st = ElfParse(file, sizeof(file), &img);
    if (st == RoboExecOk)
        st = ElfValidate(&img, sizeof(file));
    CHECK(st == RoboElfAddressOverflow || st == RoboElfInvalidSegment,
          "offset overflow");

    printf("ELF host tests  %d passed  %d failed\n", gPass, gFails);
    return gFails ? 1 : 0;
}
