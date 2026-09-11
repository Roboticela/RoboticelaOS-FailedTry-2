#include "exec/elf/elf_parser.h"
#include "exec/elf/elf_validate.h"
#include "mm/vmm/paging.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_pass;
static int g_fails;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
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

static void eh_common(unsigned char *b, uint16_t type, uint16_t phnum,
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

static void ph_load(unsigned char *p, uint32_t flags, uint64_t off,
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
    uint64_t va = ROBO_USER_TEXT_BASE;
    robo_exec_status_t st;

    CHECK(sizeof(Elf64_Ehdr) == 64, "ehdr packed 64");
    CHECK(sizeof(Elf64_Phdr) == 56, "phdr packed 56");
    memset(file, 0, sizeof(file));
    eh_common(file, ET_EXEC, 1, va, 64);
    ph_load(file + 64, PF_R | PF_X, 0x1000, va, 16, 16, 0x1000);
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_EXEC_OK, "parse valid");
    st = robo_elf_validate(&img, sizeof(file));
    CHECK(st == ROBO_EXEC_OK, "validate valid ELF64 EXEC");

    file[0] = 0;
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_ELF_INVALID_MAGIC, "bad magic");
    ident(file);

    file[EI_CLASS] = 1;
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_ELF_INVALID_CLASS, "ELF32 rejected");
    file[EI_CLASS] = ELFCLASS64;

    file[EI_DATA] = 2;
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_ELF_INVALID_ENDIAN, "big endian rejected");
    file[EI_DATA] = ELFDATA2LSB;

    wr16(file + 18, 3);
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_ELF_INVALID_MACHINE, "wrong machine");
    wr16(file + 18, EM_X86_64);

    wr16(file + 52, 32);
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_ELF_INVALID_HEADER, "bad ehsize");
    wr16(file + 52, 64);

    wr16(file + 54, 32);
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_ELF_INVALID_PROGRAM_HEADER, "bad phentsize");
    wr16(file + 54, 56);

    wr16(file + 56, 17);
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_ELF_INVALID_PROGRAM_HEADER, "huge phnum");
    wr16(file + 56, 1);

    ph_load(file + 64, PF_R | PF_X, 0x1000, va, 32, 16, 0x1000);
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_EXEC_OK, "parse filesz>memsz");
    st = robo_elf_validate(&img, sizeof(file));
    CHECK(st == ROBO_ELF_INVALID_SEGMENT, "filesz > memsz");
    ph_load(file + 64, PF_R | PF_X, 0x1000, va, 16, 16, 0x1000);

    ph_load(file + 64, PF_R | PF_W | PF_X, 0x1000, va, 16, 16, 0x1000);
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_EXEC_OK, "parse WX");
    st = robo_elf_validate(&img, sizeof(file));
    CHECK(st == ROBO_ELF_WX, "W^X");
    ph_load(file + 64, PF_R | PF_X, 0x1000, va, 16, 16, 0x1000);

    ph_load(file + 64, PF_R | PF_X, 0x1000, 0x401000ull, 16, 16, 0x1000);
    wr64(file + 24, 0x401000ull);
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_EXEC_OK, "parse kernel va");
    st = robo_elf_validate(&img, sizeof(file));
    CHECK(st == ROBO_ELF_ADDRESS_INVALID, "kernel VA rejected");
    wr64(file + 24, va);
    ph_load(file + 64, PF_R | PF_X, 0x1000, va, 16, 16, 0x1000);

    wr16(file + 16, ET_DYN);
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_EXEC_OK, "parse ET_DYN");
    st = robo_elf_validate(&img, sizeof(file));
    CHECK(st == ROBO_ELF_UNSUPPORTED_TYPE, "ET_DYN rejected");
    wr16(file + 16, ET_EXEC);

    wr16(file + 56, 2);
    ph_load(file + 64, PF_R | PF_X, 0x40, va, 16, 0x1000, 0);
    ph_load(file + 64 + 56, PF_R | PF_W, 0x80, va + 0x800, 16, 0x1000, 0);
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_EXEC_OK, "parse overlap");
    st = robo_elf_validate(&img, sizeof(file));
    CHECK(st == ROBO_ELF_OVERLAP, "overlap PT_LOAD");

    wr64(file + 24, va + 0x200000ull);
    wr16(file + 56, 1);
    ph_load(file + 64, PF_R | PF_X, 0x1000, va, 16, 16, 0x1000);
    st = robo_elf_parse(file, sizeof(file), &img);
    CHECK(st == ROBO_EXEC_OK, "parse entry in user window");
    st = robo_elf_validate(&img, sizeof(file));
    CHECK(st == ROBO_EXEC_OK, "validate does not require segment membership");

    ph_load(file + 64, PF_R | PF_X, 0x1000, va, 16, 16, 0x1000);
    wr64(file + 8 + 64, ~0ull - 8ull); /* p_offset overflow with filesz */
    wr64(file + 32 + 64, 32);
    wr64(file + 40 + 64, 32);
    st = robo_elf_parse(file, sizeof(file), &img);
    if (st == ROBO_EXEC_OK)
        st = robo_elf_validate(&img, sizeof(file));
    CHECK(st == ROBO_ELF_ADDRESS_OVERFLOW || st == ROBO_ELF_INVALID_SEGMENT,
          "offset overflow");

    printf("ELF host tests  %d passed  %d failed\n", g_pass, g_fails);
    return g_fails ? 1 : 0;
}
