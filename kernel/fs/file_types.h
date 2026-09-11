#ifndef ROBO_FILE_TYPES_H
#define ROBO_FILE_TYPES_H

#include <stdint.h>

typedef int32_t robo_fd_t;

#define ROBO_FD_STDIN   0
#define ROBO_FD_STDOUT  1
#define ROBO_FD_STDERR  2
#define ROBO_FD_INVALID ((robo_fd_t)-1)

#define ROBO_MAX_FDS    16u
#define ROBO_FD_MAX     ROBO_MAX_FDS

#define ROBO_FFLAG_READ     (1u << 0)
#define ROBO_FFLAG_WRITE    (1u << 1)
#define ROBO_FFLAG_APPEND   (1u << 2)
#define ROBO_FFLAG_NONBLOCK (1u << 3)
#define ROBO_FFLAG_CLOEXEC  (1u << 4)

#define ROBO_FDENT_CLOEXEC  (1u << 0)

#define ROBO_SEEK_SET 0
#define ROBO_SEEK_CUR 1
#define ROBO_SEEK_END 2

/*
 * ioctl request layout for future device/terminal use:
 *   bits [63:32] = namespace, [31:0] = command.
 * No commands are assigned in this phase.
 */
#define ROBO_IOCTL_NS_SHIFT 32
#define ROBO_IOCTL_NS_NONE  0ull

typedef enum {
    ROBO_FILE_REGULAR = 1,
    ROBO_FILE_DIRECTORY,
    ROBO_FILE_PIPE,
    ROBO_FILE_SOCKET,
    ROBO_FILE_DEVICE,
    ROBO_FILE_CONSOLE
} RoboFileType;

#endif
