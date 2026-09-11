#ifndef ROBO_FD_H
#define ROBO_FD_H

#include "fs/fd_table.h"

struct RoboProcess;

robo_fd_t robo_fd_alloc(struct RoboProcess *process, RoboFile *file,
                        uint32_t flags);
robo_fd_t robo_fd_dup(struct RoboProcess *process, robo_fd_t oldfd);
int robo_fd_close(struct RoboProcess *process, robo_fd_t fd);
RoboFile *robo_fd_get(struct RoboProcess *process, robo_fd_t fd);
int robo_fd_in_use(struct RoboProcess *process, robo_fd_t fd);

#ifndef PROCESS_HOST_TEST
void robo_fd_table_dump(const struct RoboProcess *process);
int robo_fd_selftest(void);
#ifndef FD_HOST_TEST
void robo_fd_fb_report(void);
#endif
#endif

#endif
