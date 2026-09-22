#include "net/core/network.h"
#include "net/core/packet.h"
#include "net/core/interface.h"
#include "net/core/checksum.h"
#include "net/arp/arp.h"
#include "net/ipv4/route.h"
#include "net/ipv4/ipv4.h"
#include "net/socket/socket.h"
#include "net/tcp/tcp.h"
#include "drivers/virtio_net.h"
#include "thread/thread.h"
#include "scheduler/scheduler.h"
#include "time/time.h"
#include "kio.h"

#define QEMU_IP  0x0A00020Fu
#define QEMU_MASK 0xFFFFFF00u
#define QEMU_GW  0x0A000202u
#define QEMU_DNS 0x0A000203u

static int gNetOk;
static int gNetDbg;
static RoboThread *gWorker;

int NetDebug(void)
{
    return gNetDbg;
}

void NetSetDebug(int on)
{
    gNetDbg = on ? 1 : 0;
}

void NetWait(RoboThread **slot, int *posted)
{
    if (!slot || !posted)
        return;
    if (*posted) {
        *posted = 0;
        return;
    }
    *slot = CurrentThread();
    if (*posted) {
        *posted = 0;
        *slot = 0;
        return;
    }
    ThreadBlock();
    *slot = 0;
}

void NetSignal(RoboThread **slot, int *posted)
{
    RoboThread *t;
    if (!slot || !posted)
        return;
    *posted = 1;
    t = *slot;
    if (t)
        ThreadWake(t);
}

void NetWakeWalk(void)
{
    unsigned i, n = SockCount();
    for (i = 0; i < n; i++) {
        RoboSocket *s = SockAt(i);
        if (!s || !s->used)
            continue;
        if (s->udpQn || s->rcvLen || s->acceptN || s->err || s->finRcvd)
            NetSignal(&s->rxWaiter, &s->rxPosted);
        if (s->tcpState == RoboTcpEstablished)
            NetSignal(&s->connWaiter, &s->connPosted);
        if (s->sndCap - s->sndLen)
            NetSignal(&s->txWaiter, &s->txPosted);
    }
}

static void NetTickWaiters(void)
{
    unsigned i, n = SockCount();
    for (i = 0; i < n; i++) {
        RoboSocket *s = SockAt(i);
        if (!s || !s->used)
            continue;
        if (s->rxWaiter)
            ThreadWake(s->rxWaiter);
        if (s->connWaiter)
            ThreadWake(s->connWaiter);
        if (s->txWaiter)
            ThreadWake(s->txWaiter);
    }
}

static void NetWorker(void *arg)
{
    unsigned ticks = 0;
    (void)arg;
    for (;;) {
        VirtioNetPoll();
        ArpExpire();
        TcpTimer();
        NetWakeWalk();
        if (++ticks >= 50u) {
            ticks = 0;
            NetTickWaiters();
        }
        RoboSleep(1);
    }
}

int NetInit(void)
{
    if (gNetOk)
        return 1;
    if (!PktPoolInit())
        return 0;
    NetifInit();
    ArpInit();
    RouteInit();
    SockInit();
    gNetOk = 1;
    return 1;
}

static void NetReportIf(void)
{
    RoboNetIf *ifp = NetifDefault();
    char mac[18], ip[16], mask[16], gw[16];
    kinfo("\nNetwork\n-------\n\n");
    if (!ifp) {
        kinfo("Interface : (none)\n");
        kinfo("Link      : DOWN\n\n");
        return;
    }
    MacFmt(ifp->mac, mac);
    Ipv4Fmt(ifp->ipv4, ip);
    Ipv4Fmt(ifp->netmask, mask);
    Ipv4Fmt(ifp->gateway, gw);
    kinfo("Interface : %s\n", ifp->name);
    kinfo("MAC       : %s\n", mac);
    kinfo("Link      : %s\n", ifp->linkUp ? "UP" : "DOWN");
    kinfo("IP        : %s\n", ip);
    kinfo("Netmask   : %s\n", mask);
    kinfo("Gateway   : %s\n", gw);
    kinfo("MTU       : %u\n\n", (unsigned)ifp->mtu);
}

void NetReport(void)
{
    NetReportIf();
    RouteReport();
    ArpReport();
    SockReport();
}

int NetReady(void)
{
    return gNetOk && NetifDefault() != 0;
}

void NetPoll(void)
{
    VirtioNetPoll();
}

int NetBringup(void)
{
    RoboNetIf *ifp;
    if (!gNetOk && !NetInit())
        return 0;
    ifp = NetifDefault();
    if (!ifp) {
        kprintf("[FAIL] NETWORK  no interface\n");
        NetReportIf();
        return 0;
    }
    NetifSetIpv4(ifp, QEMU_IP, QEMU_MASK, QEMU_GW, QEMU_DNS);
    RouteAdd(QEMU_IP & QEMU_MASK, QEMU_MASK, 0, ifp, 1);
    RouteAdd(0, 0, QEMU_GW, ifp, 10);
    if (!gWorker)
        gWorker = ThreadCreate("net", NetWorker, 0);
    NetReportIf();
    if (gWorker)
        kinfo("[INFO] [NET] worker started\n");
    kinfo("[PASS] NETWORK STACK\n");
    return 1;
}
