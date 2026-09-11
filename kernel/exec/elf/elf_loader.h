#ifndef ROBO_ELF_LOADER_H
#define ROBO_ELF_LOADER_H

#include "exec/elf/elf.h"
#include "mm/vmm/aspace.h"

typedef struct {
    uint64_t entry;
    uint64_t stack_top;
    uint64_t stack_base;
    uint64_t text_va;
    uint64_t load_bias;
} RoboExecImage;

robo_exec_status_t robo_elf_load_image(RoboAddressSpace *as,
                                       const void *bytes, size_t size,
                                       RoboExecImage *out);
int robo_elf_entry_executable(RoboAddressSpace *as, uint64_t entry);
void robo_elf_set_quiet(int quiet);

#endif
