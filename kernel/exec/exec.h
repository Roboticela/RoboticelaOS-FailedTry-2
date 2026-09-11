#ifndef ROBO_EXEC_H
#define ROBO_EXEC_H

#include "exec/exec_error.h"
#include "process/process.h"
#include "roboticela/bootinfo.h"

robo_exec_status_t robo_exec(RoboProcess *process, const char *path);
robo_exec_status_t robo_exec_last_status(void);
int robo_exec_image_present(const char *path);
RoboProcess *robo_process_create_from_elf(const char *path);
RoboProcess *robo_process_create_init_elf(const char *path);
void robo_exec_set_quiet(int quiet);
int robo_exec_selftest(const RoboBootInfo *info);

#endif
