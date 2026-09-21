#ifndef ROBO_FS_PIPE_H
#define ROBO_FS_PIPE_H

#include "fs/file.h"

/*
 * Pipe backend interface. No pipe buffers or SYS_PIPE in this phase.
 * Creating a pipe file returns a RoboFile whose operations yield ENOTSUP.
 */
RoboFile *PipeFileCreate(uint32_t flags);

#endif
