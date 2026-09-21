#include "server.h"
#include "ws_compositor.h"
#include <robosys/syscall.h>
#include <robosys/display.h>
#include <robosys/input.h>
#include <robowindow/protocol.h>

#define WS_CLIENTS 8

struct wsClient {
    int used;
    int handle;
    uint64_t pid;
};

static struct wsClient gCl[WS_CLIENTS];
static int gListen;
static int gStop;
static int gServed;

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int errcode(Long r)
{
    return (int)(-(int64_t)r);
}

static void puts1(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    if (n)
        Write(1, s, n);
}

static void AddClient(int handle, uint64_t pid)
{
    unsigned i;
    for (i = 0; i < WS_CLIENTS; i++) {
        if (!gCl[i].used) {
            gCl[i].used = 1;
            gCl[i].handle = handle;
            gCl[i].pid = pid;
            WsClientAdd(handle, pid);
            gServed = 1;
            return;
        }
    }
    Ipc((int)RoboIpcOpClose, handle, 0, 0, 0);
}

static void DropClient(unsigned i)
{
    if (!gCl[i].used)
        return;
    WsClientDrop(gCl[i].handle);
    Ipc((int)RoboIpcOpClose, gCl[i].handle, 0, 0, 0);
    gCl[i].used = 0;
    gCl[i].handle = 0;
    gCl[i].pid = 0;
}

static void PumpInput(void)
{
    RoboInputEvent ev;
    Long r;
    for (;;) {
        r = Display((int)RoboDispInput, 0, &ev, sizeof(ev),
                         (int)RoboDispNowait);
        if (IsErr(r))
            break;
        WsRouteInput(&ev);
    }
}

static void AcceptOne(void)
{
    RoboIpcAccept acc;
    Long r;
    r = Ipc((int)RoboIpcOpRecv, gListen, &acc, sizeof(acc),
                 (int)RoboIpcNowait);
    if (IsErr(r))
        return;
    if (r < (Long)sizeof(acc) || acc.magic != RoboIpcAcceptMagic)
        return;
    AddClient(acc.handle, acc.peerPid);
}

static void ServiceClients(void)
{
    unsigned i;
    for (i = 0; i < WS_CLIENTS; i++) {
        RoboWinMsg m;
        Long r;
        int rc;
        if (!gCl[i].used)
            continue;
        r = Ipc((int)RoboIpcOpRecv, gCl[i].handle, &m, sizeof(m),
                     (int)RoboIpcNowait);
        if (IsErr(r)) {
            int e = errcode(r);
            if (e == RoboEagain)
                continue;
            DropClient(i);
            continue;
        }
        rc = WsHandle(gCl[i].handle, gCl[i].pid, &m);
        if (rc == 2) {
            Ipc((int)RoboIpcOpSend, gCl[i].handle, &m, sizeof(m), 0);
            gStop = 1;
            return;
        }
        if (rc == 1)
            Ipc((int)RoboIpcOpSend, gCl[i].handle, &m, sizeof(m), 0);
    }
}

int main(int argc, char **argv)
{
    Long r;
    int headless = 0;
    unsigned idle = 0;
    int testMode = 0;
    static const char name[] = RoboIpcNameWindowServer;
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
            testMode = 1;
    }

    puts1("[WINDOW] RoboWindowServer starting\n");
    r = Ipc((int)RoboIpcOpListen, 0, (void *)name, 0, 0);
    if (IsErr(r) || r <= 0) {
        puts1("[WINDOW] listen failed\n");
        return 1;
    }
    gListen = (int)r;

    r = Display((int)RoboDispClaim, 0, 0, 0, (int)RoboDispRoleInput);
    if (IsErr(r)) {
        puts1("[WINDOW] Display:\nUnavailable\nStatus:\nHEADLESS\n");
        headless = 1;
    }
    if (WsCompositorConnect() != 0) {
        puts1("[WINDOW] Compositor: unavailable\n");
        headless = 1;
        WsInit(1, 0);
    } else {
        puts1("[WINDOW] Compositor: READY\n");
        if (!headless)
            puts1("[WINDOW] Display: INPUT\n");
        WsInit(headless, WsCompositorPixfmt());
    }

    for (;;) {
        unsigned n = 0;
        unsigned i;
        AcceptOne();
        ServiceClients();
        PumpInput();
        WsPresentIfDirty();
        if (testMode) {
            if (gStop)
                break;
            for (i = 0; i < WS_CLIENTS; i++) {
                if (gCl[i].used)
                    n++;
            }
            if (gServed && n == 0) {
                idle++;
                if (idle > 50u)
                    break;
            } else {
                idle = 0;
            }
        }
        RoboSleep(1);
    }
    puts1("[WINDOW] RoboWindowServer exit\n");
    return 0;
}
