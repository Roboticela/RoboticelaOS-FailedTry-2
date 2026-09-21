#include "server.h"
#include <robosys/syscall.h>
#include <robosys/display.h>
#include <robocompositor/protocol.h>
#include <robocompositor/output.h>

static int gListen;
static int gClient;
static uint64_t gClientPid;
static int gStop;
static int gServed;
static RoboOutput gOut;

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

static void DropClient(void)
{
    if (!gClient)
        return;
    CsDrop();
    Ipc((int)RoboIpcOpClose, gClient, 0, 0, 0);
    gClient = 0;
    gClientPid = 0;
}

static void AcceptOne(void)
{
    RoboIpcAccept acc;
    Long r;
    if (gClient)
        return;
    r = Ipc((int)RoboIpcOpRecv, gListen, &acc, sizeof(acc),
                 (int)RoboIpcNowait);
    if (IsErr(r))
        return;
    if (r < (Long)sizeof(acc) || acc.magic != RoboIpcAcceptMagic)
        return;
    gClient = acc.handle;
    gClientPid = acc.peerPid;
    gServed = 1;
    puts1("[COMP] window-server connected\n");
}

static void ServiceClient(void)
{
    RoboCompMsg m;
    Long r;
    int rc;
    if (!gClient)
        return;
    r = Ipc((int)RoboIpcOpRecv, gClient, &m, sizeof(m),
                 (int)RoboIpcNowait);
    if (IsErr(r)) {
        if (errcode(r) == RoboEagain)
            return;
        DropClient();
        return;
    }
    rc = CsHandle(gClientPid, &m);
    if (rc == 2) {
        Ipc((int)RoboIpcOpSend, gClient, &m, sizeof(m), 0);
        gStop = 1;
        return;
    }
    if (rc == 1)
        Ipc((int)RoboIpcOpSend, gClient, &m, sizeof(m), 0);
}

int main(int argc, char **argv)
{
    Long r;
    int headless = 0;
    unsigned idle = 0;
    int testMode = 0;
    static const char name[] = RoboIpcNameCompositor;
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

    puts1("[COMP] RoboCompositor starting\n");
    r = Ipc((int)RoboIpcOpListen, 0, (void *)name, 0, 0);
    if (IsErr(r) || r <= 0) {
        puts1("[COMP] listen failed\n");
        return 1;
    }
    gListen = (int)r;

    if (!OutputOpenDisplay(&gOut) || !gOut.ready) {
        puts1("[COMP] Display:\nUnavailable\nStatus:\nHEADLESS\n");
        headless = 1;
    } else {
        puts1("[COMP] Display: READY\n");
    }
    if (!CsInit(&gOut, headless)) {
        puts1("[COMP] init failed\n");
        return 1;
    }
    CsLogStatus();

    for (;;) {
        AcceptOne();
        ServiceClient();
        if (CsDirty())
            CsComposePresent();
        if (testMode) {
            if (gStop)
                break;
            if (gServed && !gClient) {
                idle++;
                if (idle > 50u)
                    break;
            } else {
                idle = 0;
            }
        }
        RoboSleep(1);
    }
    puts1("[COMP] RoboCompositor exit\n");
    return 0;
}
