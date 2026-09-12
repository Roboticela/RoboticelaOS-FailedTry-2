#include "server.h"
#include "ws_compositor.h"
#include <robosys/syscall.h>
#include <robosys/display.h>
#include <robosys/input.h>
#include <robowindow/protocol.h>

#define WS_CLIENTS 8

struct ws_client {
    int used;
    int handle;
    uint64_t pid;
};

static struct ws_client g_cl[WS_CLIENTS];
static int g_listen;
static int g_stop;
static int g_served;

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

static void add_client(int handle, uint64_t pid)
{
    unsigned i;
    for (i = 0; i < WS_CLIENTS; i++) {
        if (!g_cl[i].used) {
            g_cl[i].used = 1;
            g_cl[i].handle = handle;
            g_cl[i].pid = pid;
            ws_client_add(handle, pid);
            g_served = 1;
            return;
        }
    }
    robo_ipc((int)ROBO_IPC_OP_CLOSE, handle, 0, 0, 0);
}

static void drop_client(unsigned i)
{
    if (!g_cl[i].used)
        return;
    ws_client_drop(g_cl[i].handle);
    robo_ipc((int)ROBO_IPC_OP_CLOSE, g_cl[i].handle, 0, 0, 0);
    g_cl[i].used = 0;
    g_cl[i].handle = 0;
    g_cl[i].pid = 0;
}

static void pump_input(void)
{
    RoboInputEvent ev;
    robo_long r;
    for (;;) {
        r = robo_display((int)ROBO_DISP_INPUT, 0, &ev, sizeof(ev),
                         (int)ROBO_DISP_NOWAIT);
        if (is_err(r))
            break;
        ws_route_input(&ev);
    }
}

static void accept_one(void)
{
    RoboIpcAccept acc;
    robo_long r;
    r = robo_ipc((int)ROBO_IPC_OP_RECV, g_listen, &acc, sizeof(acc),
                 (int)ROBO_IPC_NOWAIT);
    if (is_err(r))
        return;
    if (r < (robo_long)sizeof(acc) || acc.magic != ROBO_IPC_ACCEPT_MAGIC)
        return;
    add_client(acc.handle, acc.peer_pid);
}

static void service_clients(void)
{
    unsigned i;
    for (i = 0; i < WS_CLIENTS; i++) {
        RoboWinMsg m;
        robo_long r;
        int rc;
        if (!g_cl[i].used)
            continue;
        r = robo_ipc((int)ROBO_IPC_OP_RECV, g_cl[i].handle, &m, sizeof(m),
                     (int)ROBO_IPC_NOWAIT);
        if (is_err(r)) {
            int e = errcode(r);
            if (e == ROBO_EAGAIN)
                continue;
            drop_client(i);
            continue;
        }
        rc = ws_handle(g_cl[i].handle, g_cl[i].pid, &m);
        if (rc == 2) {
            robo_ipc((int)ROBO_IPC_OP_SEND, g_cl[i].handle, &m, sizeof(m), 0);
            g_stop = 1;
            return;
        }
        if (rc == 1)
            robo_ipc((int)ROBO_IPC_OP_SEND, g_cl[i].handle, &m, sizeof(m), 0);
    }
}

int main(int argc, char **argv)
{
    robo_long r;
    int headless = 0;
    unsigned idle = 0;
    int test_mode = 0;
    static const char name[] = ROBO_IPC_NAME_WINDOW_SERVER;
    int ai;

    for (ai = 1; ai < argc; ai++) {
        const char *a = argv ? argv[ai] : 0;
        unsigned n = 0;
        const char *k = "selftest";
        if (!a)
            continue;
        while (k[n] && a[n] && k[n] == a[n])
            n++;
        if (!k[n] && !a[n])
            test_mode = 1;
    }

    puts1("[WINDOW] RoboWindowServer starting\n");
    r = robo_ipc((int)ROBO_IPC_OP_LISTEN, 0, (void *)name, 0, 0);
    if (is_err(r) || r <= 0) {
        puts1("[WINDOW] listen failed\n");
        return 1;
    }
    g_listen = (int)r;

    r = robo_display((int)ROBO_DISP_CLAIM, 0, 0, 0, (int)ROBO_DISP_ROLE_INPUT);
    if (is_err(r)) {
        puts1("[WINDOW] Display:\nUnavailable\nStatus:\nHEADLESS\n");
        headless = 1;
    }
    if (ws_compositor_connect() != 0) {
        puts1("[WINDOW] Compositor: unavailable\n");
        headless = 1;
        ws_init(1, 0);
    } else {
        puts1("[WINDOW] Compositor: READY\n");
        if (!headless)
            puts1("[WINDOW] Display: INPUT\n");
        ws_init(headless, ws_compositor_pixfmt());
    }

    for (;;) {
        unsigned n = 0;
        unsigned i;
        accept_one();
        service_clients();
        pump_input();
        ws_present_if_dirty();
        if (test_mode) {
            if (g_stop)
                break;
            for (i = 0; i < WS_CLIENTS; i++) {
                if (g_cl[i].used)
                    n++;
            }
            if (g_served && n == 0) {
                idle++;
                if (idle > 50u)
                    break;
            } else {
                idle = 0;
            }
        }
        robo_sleep(1);
    }
    puts1("[WINDOW] RoboWindowServer exit\n");
    return 0;
}
