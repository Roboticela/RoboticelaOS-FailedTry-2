#ifndef ROBO_INIT_SERVICE_H
#define ROBO_INIT_SERVICE_H

#include <stdint.h>

enum {
    ROBO_SVC_STOPPED = 0,
    ROBO_SVC_STARTING,
    ROBO_SVC_RUNNING,
    ROBO_SVC_READY,
    ROBO_SVC_STOPPING,
    ROBO_SVC_FAILED,
    ROBO_SVC_EXITED
};

enum {
    ROBO_RESTART_NEVER = 0,
    ROBO_RESTART_ON_FAILURE,
    ROBO_RESTART_ALWAYS
};

#define ROBO_SVC_CRITICAL (1u << 0)
#define ROBO_SVC_ENABLED  (1u << 1)
#define ROBO_SVC_SELFTEST (1u << 2)

typedef struct RoboServiceConfig {
    const char *name;
    const char *path;
    uint32_t flags;
    uint32_t restart_policy;
    uint32_t restart_limit;
} RoboServiceConfig;

typedef struct RoboService {
    const RoboServiceConfig *cfg;
    uint64_t pid;
    uint32_t state;
    uint32_t restart_policy;
    uint32_t restart_count;
    uint32_t restart_limit;
    uint64_t last_start_time;
    uint64_t restart_after;
    int exit_status;
    int ready;
} RoboService;

void robo_service_table_init(void);
unsigned robo_service_count(void);
RoboService *robo_service_at(unsigned i);
RoboService *robo_service_find_pid(uint64_t pid);
int robo_service_start(RoboService *svc);
void robo_service_on_exit(RoboService *svc, int status);
int robo_service_start_due(void);
uint64_t robo_service_next_delay_ms(void);
int robo_service_live(void);
void robo_service_set_selftest(int on);

#endif
