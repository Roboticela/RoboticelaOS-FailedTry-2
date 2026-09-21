#ifndef ROBO_EXEC_H
#define ROBO_EXEC_H

#include "exec/exec_error.h"
#include "exec/elf/elf_loader.h"
#include "process/process.h"
#include "roboticela/bootinfo.h"

ExecStatus Exec(RoboProcess *process, const char *path);
ExecStatus ExecEx(RoboProcess *process, const char *path,
                                const RoboExecArgv *argv);
ExecStatus ExecLastStatus(void);
int ExecImagePresent(const char *path);
RoboProcess *ProcessCreateFromElf(const char *path);
RoboProcess *ProcessCreateFromElfArgv(const char *path,
                                               const RoboExecArgv *argv);
RoboProcess *ProcessCreateInitElf(const char *path,
                                          const RoboExecArgv *argv);
void ExecSetQuiet(int quiet);
int ExecSelftest(const RoboBootInfo *info);

#endif
