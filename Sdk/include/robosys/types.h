#ifndef ROBOSYS_TYPES_H
#define ROBOSYS_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef int64_t  Long;
typedef uint64_t Ulong;
typedef uint64_t Pid;
typedef uint64_t Tid;

typedef struct RoboAbiInfo {
    uint32_t major;
    uint32_t minor;
    uint32_t features;
    uint32_t reserved;
} RoboAbiInfo;

#define RoboAbiFeatWrite   (1u << 0)
#define RoboAbiFeatExit    (1u << 1)
#define RoboAbiFeatTime    (1u << 2)
#define RoboAbiFeatSleep   (1u << 3)
#define RoboAbiFeatMmap    (1u << 4)
#define RoboAbiFeatFs      (1u << 5)
#define RoboAbiFeatThread  (1u << 6)
#define RoboAbiFeatProcess (1u << 7)
#define RoboAbiFeatWait    (1u << 8)
#define RoboAbiFeatDriver  (1u << 9)
#define RoboAbiFeatIpc     (1u << 10)
#define RoboAbiFeatDisplay (1u << 11)
#define RoboAbiFeatNet     (1u << 12)
#define RoboAbiFeatTls     (1u << 13)

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

typedef struct RoboIpcAccept {
    uint32_t magic;
    int32_t handle;
    uint64_t peerPid;
} RoboIpcAccept;

typedef struct RoboDriverStatus {
    uint32_t pciCount;
    uint32_t driversRegistered;
    uint32_t driversLoaded;
    uint32_t driversFailed;
    uint32_t signedOk;
    uint32_t rejected;
    uint32_t storageReady;
    uint32_t gfxWidth;
    uint32_t gfxHeight;
    uint32_t keyboard;
    uint32_t mouse;
    uint32_t testMode;
} RoboDriverStatus;

#endif
