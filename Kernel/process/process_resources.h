#ifndef ROBO_PROCESS_RESOURCES_H
#define ROBO_PROCESS_RESOURCES_H

#include <stdint.h>

#define RoboEnvMax    8u

typedef struct RoboSpinlock {
    volatile unsigned locked;
} RoboSpinlock;

typedef struct RoboIPCResources {
    unsigned objectCount;
} RoboIPCResources;

typedef struct RoboEnvironment {
    unsigned count;
    char key[RoboEnvMax][32];
    char val[RoboEnvMax][64];
} RoboEnvironment;

typedef struct RoboCredentials {
    uint64_t uid;
    uint64_t gid;
    uint64_t effectiveUid;
    uint64_t effectiveGid;
    uint64_t capabilities;
} RoboCredentials;

RoboIPCResources *IpcCreate(void);
void IpcDestroy(RoboIPCResources *ipc);

RoboEnvironment *EnvCreate(void);
void EnvDestroy(RoboEnvironment *env);

RoboCredentials *CredsCreate(void);
void CredsDestroy(RoboCredentials *creds);

#endif
