#ifndef ROBO_ELF_PARSER_H
#define ROBO_ELF_PARSER_H

#include "exec/elf/elf.h"

ExecStatus ElfParse(const void *bytes, size_t size, RoboElfImage *out);

#endif
