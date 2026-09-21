#ifndef ROBO_VFS_TYPES_H
#define ROBO_VFS_TYPES_H

#include <stdint.h>
#include <robosys/types.h>
#include "fs/file_types.h"
#include "syscall/syscall_errors.h"

#define RoboPathMax  111u
#define RoboNameMax  55u

#define RoboVfsMaxFs     8u
#define RoboVfsMaxMounts 8u

#define RoboVfsLookupParent (1u << 0)

typedef enum {
    RoboVnodeFile = 1,
    RoboVnodeDirectory,
    RoboVnodeSymlink,
    RoboVnodeDevice,
    RoboVnodePipe,
    RoboVnodeSocket
} RoboVnodeType;

typedef struct RoboDirEntry {
    uint64_t inode;
    uint32_t type;
    char name[RoboNameMax + 1];
} RoboDirEntry;

struct RoboVnode;
struct RoboFile;
struct RoboMount;
struct RoboFilesystem;
struct RoboBlockDevice;

#endif
