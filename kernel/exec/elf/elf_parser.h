#ifndef ROBO_ELF_PARSER_H
#define ROBO_ELF_PARSER_H

#include "exec/elf/elf.h"

robo_exec_status_t robo_elf_parse(const void *bytes, size_t size, RoboElfImage *out);

#endif
