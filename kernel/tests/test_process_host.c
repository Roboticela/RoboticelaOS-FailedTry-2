#include "process/process.h"
#include "process/process_table.h"
#include "fs/backends/console.h"
#include "fs/file.h"

#include <stdio.h>
#include <string.h>

static int g_pass;
static int g_fails;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

static void zero_thread(RoboThread *t)
{
    memset(t, 0, sizeof(*t));
}

int main(void)
{
    RoboProcess *k, *a, *b, *c, *d;
    RoboThread t1, t2, t3;
    int fd;
    robo_process_id_t gone;

    CHECK(robo_process_subsystem_init(), "subsystem init");
    k = robo_kernel_process();
    CHECK(k != 0, "kernel process");
    CHECK(robo_process_id(k) == ROBO_PROCESS_ID_KERNEL, "kernel pid 1");
    CHECK(robo_process_id(k) != 0, "pid not zero");
    CHECK(robo_process_lookup(k->id) == k, "kernel in table");
    CHECK(k->fd_table && k->ipc && k->environment && k->credentials, "kernel resources");
    CHECK(k->address_space != 0, "kernel aspace");

    a = robo_process_create_named("a");
    b = robo_process_create_named("b");
    c = robo_process_create_named("c");
    d = robo_process_create_named("d");
    CHECK(a && b && c && d, "create four");
    CHECK(a->id && b->id && c->id && d->id, "nonzero pids");
    CHECK(a->id != b->id && b->id != c->id && c->id != d->id && a->id != d->id,
          "unique pids");
    CHECK(robo_process_lookup(a->id) == a, "lookup a");
    CHECK(robo_process_lookup(b->id) == b, "lookup b");
    CHECK(a->parent == k && b->parent == k, "parent kernel");
    CHECK(a->fd_table != b->fd_table && a->ipc != b->ipc, "independent files/ipc");
    CHECK(a->environment != b->environment && a->credentials != b->credentials,
          "independent env/creds");
    CHECK(a->address_space != b->address_space, "independent aspace");
    CHECK(a->address_space->cr3 != b->address_space->cr3, "independent cr3 token");

    {
        RoboFile *tf = robo_console_open(ROBO_FFLAG_WRITE);
        RoboFile *got;
        CHECK(tf != 0, "fd file");
        fd = robo_process_fd_alloc(a, tf);
        CHECK(fd >= 3, "fd alloc");
        got = robo_process_fd_get(a, fd);
        CHECK(got == tf, "fd get");
        if (got)
            robo_file_put(got);
        CHECK(!robo_process_fd_in_use(b, fd), "fd not shared");
        CHECK(robo_process_fd_close(a, fd), "fd close");
        CHECK(!robo_process_fd_in_use(a, fd), "fd closed");
        if (tf)
            robo_file_put(tf);
    }

    zero_thread(&t1);
    zero_thread(&t2);
    zero_thread(&t3);
    t1.id = 10;
    t2.id = 11;
    t3.id = 12;
    CHECK(robo_process_add_thread(a, &t1), "add t1");
    CHECK(robo_process_add_thread(a, &t2), "add t2");
    CHECK(robo_process_add_thread(a, &t3), "add t3");
    CHECK(a->thread_count == 3, "count 3");
    CHECK(t1.process == a && t2.process == a && t3.process == a, "owners");
    CHECK(robo_thread_process(&t1) == a, "thread_process");
    CHECK(!robo_process_add_thread(b, &t1), "no steal");
    CHECK(robo_process_remove_thread(a, &t2), "remove t2");
    CHECK(a->thread_count == 2, "count 2");
    CHECK(robo_process_remove_thread(a, &t1), "remove t1");
    CHECK(robo_process_remove_thread(a, &t3), "remove t3");
    CHECK(a->thread_count == 0, "count 0");

    gone = b->id;
    CHECK(robo_process_destroy(b), "destroy b");
    CHECK(robo_process_lookup(gone) == 0, "stale pid");
    CHECK(robo_process_destroy(c), "destroy c");
    CHECK(robo_process_destroy(d), "destroy d");
    CHECK(robo_process_destroy(a), "destroy a");
    CHECK(!robo_process_destroy(k), "kernel immortal");
    CHECK(robo_process_lookup(k->id) == k, "kernel remains");
    CHECK(robo_process_stat_live() == 1, "only kernel live");
    CHECK(robo_process_stat_created() >= 5, "created count");
    CHECK(robo_process_stat_destroyed() >= 4, "destroyed count");
    CHECK(robo_process_stat_aspace_created() == robo_process_stat_aspace_destroyed(),
          "aspace balanced");

    printf("Process host tests  %d passed  %d failed\n", g_pass, g_fails);
    return g_fails ? 1 : 0;
}
