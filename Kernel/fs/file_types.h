#ifndef ROBO_FILE_TYPES_H
#define ROBO_FILE_TYPES_H

#include <stdint.h>

typedef int32_t Fd;

#define RoboFdStdin   0
#define RoboFdStdout  1
#define RoboFdStderr  2
#define RoboFdInvalid ((Fd)-1)

#define RoboMaxFds    16u
#define RoboFdMax     RoboMaxFds

#define RoboFflagRead     (1u << 0)
#define RoboFflagWrite    (1u << 1)
#define RoboFflagAppend   (1u << 2)
#define RoboFflagNonblock (1u << 3)
#define RoboFflagCloexec  (1u << 4)
#define RoboFflagTrunc    (1u << 5)

#define RoboFdentCloexec  (1u << 0)

#define RoboSeekSet 0
#define RoboSeekCur 1
#define RoboSeekEnd 2

/*
 * ioctl request layout for future device/terminal use:
 *   bits [63:32] = namespace, [31:0] = command.
 * No commands are assigned in this phase.
 */
#define RoboIoctlNsShift 32
#define RoboIoctlNsNone  0ull

typedef enum {
    RoboFileRegular = 1,
    RoboFileDirectory,
    RoboFilePipe,
    RoboFileSocket,
    RoboFileDevice,
    RoboFileConsole
} RoboFileType;

#endif
