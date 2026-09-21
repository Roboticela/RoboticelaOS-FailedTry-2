#ifndef ROBO_FD_H
#define ROBO_FD_H

#include "fs/fd_table.h"

struct RoboProcess;

Fd FdAlloc(struct RoboProcess *process, RoboFile *file,
                        uint32_t flags);
Fd FdDup(struct RoboProcess *process, Fd oldfd);
int FdClose(struct RoboProcess *process, Fd fd);
RoboFile *FdGet(struct RoboProcess *process, Fd fd);
int FdInUse(struct RoboProcess *process, Fd fd);

#ifndef PROCESS_HOST_TEST
void FdTableDump(const struct RoboProcess *process);
int FdSelftest(void);
#ifndef FD_HOST_TEST
void FdFbReport(void);
#endif
#endif

#endif
