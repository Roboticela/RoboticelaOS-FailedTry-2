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

static int g_net_ok;
static int g_net_dbg;
static RoboThread *g_worker;

int robo_net_debug(void)
{
    return g_net_dbg;
}

void robo_net_set_debug(int on)
{
    g_net_dbg = on ? 1 : 0;
}

void robo_net_wait(RoboThread **slot, int *posted)
{
    if (!slot || !posted)
        return;
    if (*posted) {
        *posted = 0;
        return;
    }
    *slot = robo_current_thread();
    if (*posted) {
        *posted = 0;
        *slot = 0;
        return;
    }
    robo_thread_block();
    *slot = 0;
}

void robo_net_signal(RoboThread **slot, int *posted)
{
    RoboThread *t;
    if (!slot || !posted)
        return;
    *posted = 1;
    t = *slot;
    if (t)
        robo_thread_wake(t);
}

void robo_net_wake_walk(void)
{
    unsigned i, n = robo_sock_count();
    for (i = 0; i < n; i++) {
        RoboSocket *s = robo_sock_at(i);
        if (!s || !s->used)
            continue;
        if (s->udp_qn || s->rcv_len || s->accept_n || s->err || s->fin_rcvd)
            robo_net_signal(&s->rx_waiter, &s->rx_posted);
        if (s->tcp_state == ROBO_TCP_ESTABLISHED)
            robo_net_signal(&s->conn_waiter, &s->conn_posted);
        if (s->snd_cap - s->snd_len)
            robo_net_signal(&s->tx_waiter, &s->tx_posted);
    }
}

static void net_tick_waiters(void)
{
    unsigned i, n = robo_sock_count();
    for (i = 0; i < n; i++) {
        RoboSocket *s = robo_sock_at(i);
        if (!s || !s->used)
            continue;
        if (s->rx_waiter)
            robo_thread_wake(s->rx_waiter);
        if (s->conn_waiter)
            robo_thread_wake(s->conn_waiter);
        if (s->tx_waiter)
            robo_thread_wake(s->tx_waiter);
    }
}

static void net_worker(void *arg)
{
    unsigned ticks = 0;
    (void)arg;
    for (;;) {
        robo_virtio_net_poll();
        robo_arp_expire();
        robo_tcp_timer();
        robo_net_wake_walk();
        if (++ticks >= 50u) {
            ticks = 0;
            net_tick_waiters();
        }
        robo_sleep(1);
    }
}

int robo_net_init(void)
{
    if (g_net_ok)
        return 1;
    if (!robo_pkt_pool_init())
        return 0;
    robo_netif_init();
    robo_arp_init();
    robo_route_init();
    robo_sock_init();
    g_net_ok = 1;
    return 1;
}

static void net_report_if(void)
{
    RoboNetIf *ifp = robo_netif_default();
    char mac[18], ip[16], mask[16], gw[16];
    kprintf("\nNetwork\n-------\n\n");
    if (!ifp) {
        kprintf("Interface : (none)\n");
        kprintf("Link      : DOWN\n\n");
        return;
    }
    robo_mac_fmt(ifp->mac, mac);
    robo_ipv4_fmt(ifp->ipv4, ip);
    robo_ipv4_fmt(ifp->netmask, mask);
    robo_ipv4_fmt(ifp->gateway, gw);
    kprintf("Interface : %s\n", ifp->name);
    kprintf("MAC       : %s\n", mac);
    kprintf("Link      : %s\n", ifp->link_up ? "UP" : "DOWN");
    kprintf("IP        : %s\n", ip);
    kprintf("Netmask   : %s\n", mask);
    kprintf("Gateway   : %s\n", gw);
    kprintf("MTU       : %u\n\n", (unsigned)ifp->mtu);
}

void robo_net_report(void)
{
    net_report_if();
    robo_route_report();
    robo_arp_report();
    robo_sock_report();
}

int robo_net_ready(void)
{
    return g_net_ok && robo_netif_default() != 0;
}

void robo_net_poll(void)
{
    robo_virtio_net_poll();
}

int robo_net_bringup(void)
{
    RoboNetIf *ifp;
    if (!g_net_ok && !robo_net_init())
        return 0;
    ifp = robo_netif_default();
    if (!ifp) {
        kprintf("[FAIL] NETWORK  no interface\n");
        net_report_if();
        return 0;
    }
    robo_netif_set_ipv4(ifp, QEMU_IP, QEMU_MASK, QEMU_GW, QEMU_DNS);
    robo_route_add(QEMU_IP & QEMU_MASK, QEMU_MASK, 0, ifp, 1);
    robo_route_add(0, 0, QEMU_GW, ifp, 10);
    if (!g_worker)
        g_worker = robo_thread_create("net", net_worker, 0);
    net_report_if();
    if (g_worker)
        kprintf("[INFO] [NET] worker started\n");
    kprintf("[PASS] NETWORK STACK\n");
    return 1;
}
