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

#define RoboElfMaxPhdrs 16u

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;

typedef struct {
    unsigned char eIdent[16];
    Elf64_Half eType;
    Elf64_Half eMachine;
    Elf64_Word eVersion;
    Elf64_Addr eEntry;
    Elf64_Off ePhoff;
    Elf64_Off eShoff;
    Elf64_Word eFlags;
    Elf64_Half eEhsize;
    Elf64_Half ePhentsize;
    Elf64_Half ePhnum;
    Elf64_Half eShentsize;
    Elf64_Half eShnum;
    Elf64_Half eShstrndx;
} __attribute__((packed)) Elf64_Ehdr;

typedef struct {
    Elf64_Word pType;
    Elf64_Word pFlags;
    Elf64_Off pOffset;
    Elf64_Addr pVaddr;
    Elf64_Addr pPaddr;
    Elf64_Xword pFilesz;
    Elf64_Xword pMemsz;
    Elf64_Xword pAlign;
} __attribute__((packed)) Elf64_Phdr;

typedef struct {
    Elf64_Ehdr eh;
    Elf64_Phdr ph[RoboElfMaxPhdrs];
    unsigned phnum;
    unsigned nload;
} RoboElfImage;

int ElfIdentOk(const unsigned char *ident, size_t n, ExecStatus *err);
int U64AddOk(uint64_t a, uint64_t b, uint64_t *out);

#endif
