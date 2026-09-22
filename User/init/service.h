#ifndef ROBO_INIT_SERVICE_H
#define ROBO_INIT_SERVICE_H

#include <stdint.h>

enum {
    RoboSvcStopped = 0,
    RoboSvcStarting,
    RoboSvcRunning,
    RoboSvcReady,
    RoboSvcStopping,
    RoboSvcFailed,
    RoboSvcExited
};

enum {
    RoboRestartNever = 0,
    RoboRestartOnFailure,
    RoboRestartAlways
};

#define RoboSvcCritical (1u << 0)
#define RoboSvcEnabled  (1u << 1)
#define RoboSvcSelftest (1u << 2)

typedef struct RoboServiceConfig {
    const char *name;
    const char *path;
    uint32_t flags;
    uint32_t restartPolicy;
    uint32_t restartLimit;
} RoboServiceConfig;

typedef struct RoboService {
    const RoboServiceConfig *cfg;
    uint64_t pid;
    uint32_t state;
    uint32_t restartPolicy;
    uint32_t restartCount;
    uint32_t restartLimit;
    uint64_t lastStartTime;
    uint64_t restartAfter;
    int exitStatus;
    int ready;
} RoboService;

void ServiceTableInit(void);
unsigned ServiceCount(void);
RoboService *ServiceAt(unsigned i);
RoboService *ServiceFindPid(uint64_t pid);
int ServiceStart(RoboService *svc);
void ServiceOnExit(RoboService *svc, int status);
int ServiceStartDue(void);
uint64_t ServiceNextDelayMs(void);
int ServiceLive(void);
void ServiceSetSelftest(int on);
void ServiceSetCheckpoint(int on);

#endif
