#ifndef ROBO_PROCESS_RESOURCES_H
#define ROBO_PROCESS_RESOURCES_H

#include <stdint.h>

#define ROBO_ENV_MAX    8u

typedef struct RoboSpinlock {
    volatile unsigned locked;
} RoboSpinlock;

typedef struct RoboIPCResources {
    unsigned object_count;
} RoboIPCResources;

typedef struct RoboEnvironment {
    unsigned count;
    char key[ROBO_ENV_MAX][32];
    char val[ROBO_ENV_MAX][64];
} RoboEnvironment;

typedef struct RoboCredentials {
    uint64_t uid;
    uint64_t gid;
    uint64_t effective_uid;
    uint64_t effective_gid;
    uint64_t capabilities;
} RoboCredentials;

RoboIPCResources *robo_ipc_create(void);
void robo_ipc_destroy(RoboIPCResources *ipc);

RoboEnvironment *robo_env_create(void);
void robo_env_destroy(RoboEnvironment *env);

RoboCredentials *robo_creds_create(void);
void robo_creds_destroy(RoboCredentials *creds);

#endif
