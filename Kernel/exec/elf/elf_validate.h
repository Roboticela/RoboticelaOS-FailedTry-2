#ifndef ROBO_ELF_VALIDATE_H
#define ROBO_ELF_VALIDATE_H

#include "exec/elf/elf.h"

ExecStatus ElfValidate(const RoboElfImage *img, size_t fileSize);
int UserVaOk(uint64_t va, uint64_t len);

#endif
