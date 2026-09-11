#ifndef ROBO_VFS_TYPES_H
#define ROBO_VFS_TYPES_H

#include <stdint.h>
#include <robosys/types.h>
#include "fs/file_types.h"
#include "syscall/syscall_errors.h"

#define ROBO_PATH_MAX  111u
#define ROBO_NAME_MAX  55u

#define ROBO_VFS_MAX_FS     8u
#define ROBO_VFS_MAX_MOUNTS 8u

#define ROBO_VFS_LOOKUP_PARENT (1u << 0)

typedef enum {
    ROBO_VNODE_FILE = 1,
    ROBO_VNODE_DIRECTORY,
    ROBO_VNODE_SYMLINK,
    ROBO_VNODE_DEVICE,
    ROBO_VNODE_PIPE,
    ROBO_VNODE_SOCKET
} RoboVnodeType;

typedef struct RoboDirEntry {
    uint64_t inode;
    uint32_t type;
    char name[ROBO_NAME_MAX + 1];
} RoboDirEntry;

struct RoboVnode;
struct RoboFile;
struct RoboMount;
struct RoboFilesystem;
struct RoboBlockDevice;

#endif
