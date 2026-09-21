#ifndef ROBO_ELF_LOADER_H
#define ROBO_ELF_LOADER_H

#include "exec/elf/elf.h"
#include "mm/vmm/aspace.h"

typedef struct {
    uint64_t entry;
    uint64_t StackTop;
    uint64_t stackBase;
    uint64_t textVa;
    uint64_t loadBias;
} RoboExecImage;

#define RoboExecArgvMax 8
#define RoboExecArgMax  127

typedef struct {
    unsigned argc;
    char args[RoboExecArgvMax][RoboExecArgMax + 1];
} RoboExecArgv;

ExecStatus ElfLoadImage(RoboAddressSpace *as,
                                       const void *bytes, size_t size,
                                       RoboExecImage *out,
                                       const char *argv0,
                                       const RoboExecArgv *argv);
int ElfEntryExecutable(RoboAddressSpace *as, uint64_t entry);
void ElfSetQuiet(int quiet);

#endif
