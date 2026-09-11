#ifndef ROBOSYS_TYPES_H
#define ROBOSYS_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef int64_t  robo_long;
typedef uint64_t robo_ulong;
typedef uint64_t robo_pid;
typedef uint64_t robo_tid;

typedef struct RoboAbiInfo {
    uint32_t major;
    uint32_t minor;
    uint32_t features;
    uint32_t reserved;
} RoboAbiInfo;

#define ROBO_ABI_FEAT_WRITE   (1u << 0)
#define ROBO_ABI_FEAT_EXIT    (1u << 1)
#define ROBO_ABI_FEAT_TIME    (1u << 2)
#define ROBO_ABI_FEAT_SLEEP   (1u << 3)
#define ROBO_ABI_FEAT_MMAP    (1u << 4)
#define ROBO_ABI_FEAT_FS      (1u << 5)
#define ROBO_ABI_FEAT_THREAD  (1u << 6)
#define ROBO_ABI_FEAT_PROCESS (1u << 7)
#define ROBO_ABI_FEAT_WAIT    (1u << 8)

typedef struct RoboStat {
    uint64_t size;
    uint64_t mode;
    uint64_t inode;
    uint64_t uid;
    uint64_t gid;
    uint64_t created;
    uint64_t modified;
    uint64_t accessed;
    uint32_t type;
    uint32_t nlink;
} RoboStat;

#endif
