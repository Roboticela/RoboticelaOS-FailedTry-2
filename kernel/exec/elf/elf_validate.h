#ifndef ROBO_ELF_VALIDATE_H
#define ROBO_ELF_VALIDATE_H

#include "exec/elf/elf.h"

robo_exec_status_t robo_elf_validate(const RoboElfImage *img, size_t file_size);
int robo_user_va_ok(uint64_t va, uint64_t len);

#endif
