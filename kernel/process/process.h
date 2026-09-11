#ifndef ROBO_PROCESS_H
#define ROBO_PROCESS_H

#include <stdint.h>
#include "process/process_state.h"
#include "process/process_resources.h"
#include "thread/thread.h"

#define ROBO_PROCESS_ID_KERNEL 1ull
#define ROBO_PROCESS_NAME_MAX  32

typedef uint64_t robo_process_id_t;

typedef struct RoboProcess {
    robo_process_id_t id;
    char name[ROBO_PROCESS_NAME_MAX];
    robo_process_state_t state;

    RoboAddressSpace *address_space;
    int owns_aspace;

    RoboThread *threads;
    uint64_t thread_count;

    RoboFileTable *files;
    RoboIPCResources *ipc;
    RoboEnvironment *environment;
    RoboCredentials *credentials;

    struct RoboProcess *parent;
    struct RoboProcess *first_child;
    struct RoboProcess *sibling_next;

    uint64_t created_at;
    uint64_t exit_time;
    int exit_status;

    RoboSpinlock lock;
    struct RoboProcess *next;
} RoboProcess;

int robo_process_subsystem_init(void);
RoboProcess *robo_kernel_process(void);

RoboProcess *robo_process_create(void);
RoboProcess *robo_process_create_named(const char *name);
int robo_process_destroy(RoboProcess *process);
void robo_process_exit(int status);

RoboProcess *robo_current_process(void);
robo_process_id_t robo_process_id(const RoboProcess *process);
RoboProcess *robo_process_lookup(robo_process_id_t pid);

int robo_process_add_thread(RoboProcess *process, RoboThread *thread);
int robo_process_remove_thread(RoboProcess *process, RoboThread *thread);

int robo_process_fd_alloc(RoboProcess *process, void *file);
int robo_process_fd_close(RoboProcess *process, int fd);
void *robo_process_fd_get(RoboProcess *process, int fd);

void robo_process_set_switch_log(int on);
void robo_arch_switch_address_space(RoboAddressSpace *address_space);

uint64_t robo_process_stat_created(void);
uint64_t robo_process_stat_destroyed(void);
uint64_t robo_process_stat_live(void);
uint64_t robo_process_stat_live_max(void);
uint64_t robo_process_stat_aspace_created(void);
uint64_t robo_process_stat_aspace_destroyed(void);
uint64_t robo_process_stat_files(void);
uint64_t robo_process_stat_ipc(void);
uint64_t robo_process_stat_env(void);
uint64_t robo_process_stat_creds(void);
uint64_t robo_aspace_stat_same(void);
uint64_t robo_aspace_stat_cross(void);
uint64_t robo_arch_cr3_reloads(void);
void robo_arch_prepare_thread_switch(RoboThread *old, RoboThread *next);

#ifdef PROCESS_HOST_TEST
void robo_host_set_current_thread(RoboThread *t);
#endif

void robo_process_debug_dump(const RoboProcess *process);
void robo_process_report(void);
#ifndef PROCESS_HOST_TEST
void robo_process_fb_report(void);
#endif
int robo_process_selftest(void);

void robo_process_panic_relation(const RoboThread *thread, const char *reason);
void robo_process_panic_aspace(const RoboProcess *process, const RoboThread *thread,
                               phys_addr_t expected, phys_addr_t actual);

#endif
