#ifndef ROBO_ELF_H
#define ROBO_ELF_H

#include <stdint.h>
#include <stddef.h>
#include "exec/exec_error.h"

#define ELFMAG0 0x7Fu
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS64 2u
#define ELFDATA2LSB 1u
#define EV_CURRENT 1u
#define EM_X86_64 62u
#define ET_EXEC 2u
#define ET_DYN 3u

#define PT_NULL 0u
#define PT_LOAD 1u
#define PT_DYNAMIC 2u
#define PT_INTERP 3u
#define PT_NOTE 4u
#define PT_PHDR 6u

#define PF_X 1u
#define PF_W 2u
#define PF_R 4u

#define EI_NIDENT 16u
#define EI_CLASS 4u
#define EI_DATA 5u
#define EI_VERSION 6u
#define EI_OSABI 7u

#define ROBO_ELF_MAX_PHDRS 16u

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;

typedef struct {
    unsigned char e_ident[16];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff;
    Elf64_Off e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Off p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} __attribute__((packed)) Elf64_Phdr;

typedef struct {
    Elf64_Ehdr eh;
    Elf64_Phdr ph[ROBO_ELF_MAX_PHDRS];
    unsigned phnum;
    unsigned nload;
} RoboElfImage;

int robo_elf_ident_ok(const unsigned char *ident, size_t n, robo_exec_status_t *err);
int robo_u64_add_ok(uint64_t a, uint64_t b, uint64_t *out);

#endif
