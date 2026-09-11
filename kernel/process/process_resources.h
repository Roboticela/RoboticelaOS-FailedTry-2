#ifndef ROBO_PROCESS_RESOURCES_H
#define ROBO_PROCESS_RESOURCES_H

#include <stdint.h>

#define ROBO_FD_MAX     16u
#define ROBO_FD_STDIN   0
#define ROBO_FD_STDOUT  1
#define ROBO_FD_STDERR  2
#define ROBO_ENV_MAX    8u

typedef uint32_t robo_fd_t;

typedef struct RoboSpinlock {
    volatile unsigned locked;
} RoboSpinlock;

typedef struct RoboFileTable {
    void *slot[ROBO_FD_MAX];
    unsigned in_use[ROBO_FD_MAX];
    unsigned open_count;
} RoboFileTable;

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

RoboFileTable *robo_files_create(void);
void robo_files_destroy(RoboFileTable *files);
int robo_files_alloc(RoboFileTable *files, void *file);
int robo_files_close(RoboFileTable *files, int fd);
void *robo_files_get(RoboFileTable *files, int fd);

RoboIPCResources *robo_ipc_create(void);
void robo_ipc_destroy(RoboIPCResources *ipc);

RoboEnvironment *robo_env_create(void);
void robo_env_destroy(RoboEnvironment *env);

RoboCredentials *robo_creds_create(void);
void robo_creds_destroy(RoboCredentials *creds);

#endif
