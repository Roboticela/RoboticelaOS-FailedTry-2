#include "server.h"
#include <robosys/syscall.h>
#include <robosys/display.h>
#include <robocompositor/protocol.h>
#include <robocompositor/output.h>

static int g_listen;
static int g_client;
static uint64_t g_client_pid;
static int g_stop;
static int g_served;
static RoboOutput g_out;

static int is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int errcode(robo_long r)
{
    return (int)(-(int64_t)r);
}

static void puts1(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    if (n)
        robo_write(1, s, n);
}

static void drop_client(void)
{
    if (!g_client)
        return;
    cs_drop();
    robo_ipc((int)ROBO_IPC_OP_CLOSE, g_client, 0, 0, 0);
    g_client = 0;
    g_client_pid = 0;
}

static void accept_one(void)
{
    RoboIpcAccept acc;
    robo_long r;
    if (g_client)
        return;
    r = robo_ipc((int)ROBO_IPC_OP_RECV, g_listen, &acc, sizeof(acc),
                 (int)ROBO_IPC_NOWAIT);
    if (is_err(r))
        return;
    if (r < (robo_long)sizeof(acc) || acc.magic != ROBO_IPC_ACCEPT_MAGIC)
        return;
    g_client = acc.handle;
    g_client_pid = acc.peer_pid;
    g_served = 1;
    puts1("[COMP] window-server connected\n");
}

static void service_client(void)
{
    RoboCompMsg m;
    robo_long r;
    int rc;
    if (!g_client)
        return;
    r = robo_ipc((int)ROBO_IPC_OP_RECV, g_client, &m, sizeof(m),
                 (int)ROBO_IPC_NOWAIT);
    if (is_err(r)) {
        if (errcode(r) == ROBO_EAGAIN)
            return;
        drop_client();
        return;
    }
    rc = cs_handle(g_client_pid, &m);
    if (rc == 2) {
        robo_ipc((int)ROBO_IPC_OP_SEND, g_client, &m, sizeof(m), 0);
        g_stop = 1;
        return;
    }
    if (rc == 1)
        robo_ipc((int)ROBO_IPC_OP_SEND, g_client, &m, sizeof(m), 0);
}

int main(void)
{
    robo_long r;
    int headless = 0;
    unsigned idle = 0;
    static const char name[] = ROBO_IPC_NAME_COMPOSITOR;

    puts1("[COMP] RoboCompositor starting\n");
    r = robo_ipc((int)ROBO_IPC_OP_LISTEN, 0, (void *)name, 0, 0);
    if (is_err(r) || r <= 0) {
        puts1("[COMP] listen failed\n");
        return 1;
    }
    g_listen = (int)r;

    if (!robo_output_open_display(&g_out) || !g_out.ready) {
        puts1("[COMP] Display:\nUnavailable\nStatus:\nHEADLESS\n");
        headless = 1;
    } else {
        puts1("[COMP] Display: READY\n");
    }
    if (!cs_init(&g_out, headless)) {
        puts1("[COMP] init failed\n");
        return 1;
    }
    cs_log_status();

    for (;;) {
        accept_one();
        service_client();
        if (cs_dirty())
            cs_compose_present();
        if (g_stop)
            break;
        if (g_served && !g_client) {
            idle++;
            if (idle > 50u)
                break;
        } else {
            idle = 0;
        }
        robo_sleep(1);
    }
    puts1("[COMP] RoboCompositor exit\n");
    return 0;
}
