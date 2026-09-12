#include "net/arp/arp.h"
#include "net/core/net_util.h"
#include "net/ethernet/ethernet.h"
#include "net/core/network.h"
#include "thread/thread.h"
#include "time/time.h"
#include "kio.h"

#define ARP_HTYPE 1u
#define ARP_PTYPE 0x0800u
#define ARP_HLEN  6u
#define ARP_PLEN  4u
#define ARP_REQ   1u
#define ARP_REP   2u
#define ARP_TTL_NS (120ull * 1000000000ull)

static RoboArpEntry g_arp[ROBO_NET_MAX_ARP];
static struct RoboThread *g_arp_waiters[ROBO_NET_MAX_ARP];
static int g_arp_posted[ROBO_NET_MAX_ARP];

static RoboArpEntry *find_ip(uint32_t ip)
{
    unsigned i;
    for (i = 0; i < ROBO_NET_MAX_ARP; i++) {
        if (g_arp[i].used && g_arp[i].ip == ip)
            return &g_arp[i];
    }
    return 0;
}

static RoboArpEntry *alloc_ent(void)
{
    unsigned i;
    uint64_t oldest = ~0ull;
    int oldi = -1;
    for (i = 0; i < ROBO_NET_MAX_ARP; i++) {
        if (!g_arp[i].used)
            return &g_arp[i];
        if (g_arp[i].expire_ns < oldest) {
            oldest = g_arp[i].expire_ns;
            oldi = (int)i;
        }
    }
    if (oldi < 0)
        return 0;
    if (g_arp[oldi].queued) {
        robo_pkt_free(g_arp[oldi].queued);
        g_arp[oldi].queued = 0;
    }
    return &g_arp[oldi];
}

int robo_arp_init(void)
{
    unsigned i;
    for (i = 0; i < ROBO_NET_MAX_ARP; i++) {
        robo_net_memset(&g_arp[i], 0, (unsigned)sizeof(g_arp[i]));
        g_arp_waiters[i] = 0;
        g_arp_posted[i] = 0;
    }
    return 1;
}

int robo_arp_insert(RoboNetIf *ifp, uint32_t ip, const uint8_t mac[6],
                    uint8_t state)
{
    RoboArpEntry *e;
    unsigned i;
    if (!mac || !ip)
        return -1;
    e = find_ip(ip);
    if (!e)
        e = alloc_ent();
    if (!e)
        return -1;
    e->used = 1;
    e->ip = ip;
    e->ifp = ifp;
    e->state = state;
    for (i = 0; i < 6; i++)
        e->mac[i] = mac[i];
    e->expire_ns = robo_time_now() + ARP_TTL_NS;
    i = (unsigned)(e - g_arp);
    robo_net_signal(&g_arp_waiters[i], &g_arp_posted[i]);
    if (e->queued && state == ROBO_ARP_REACHABLE && ifp) {
        robo_eth_send(ifp, e->mac, ROBO_ETH_IP, e->queued);
        robo_pkt_free(e->queued);
        e->queued = 0;
    }
    if (robo_net_debug()) {
        char ips[16], macs[18];
        robo_ipv4_fmt(ip, ips);
        robo_mac_fmt(e->mac, macs);
        kprintf("[INFO] [NET] ARP %s -> %s state %u\n",
                ips, macs, (unsigned)state);
    }
    return 0;
}

int robo_arp_lookup(RoboNetIf *ifp, uint32_t ip, uint8_t mac[6])
{
    RoboArpEntry *e = find_ip(ip);
    unsigned i;
    (void)ifp;
    if (!e || e->state == ROBO_ARP_INCOMPLETE)
        return -1;
    if (mac) {
        for (i = 0; i < 6; i++)
            mac[i] = e->mac[i];
    }
    return 0;
}

int robo_arp_remove(uint32_t ip)
{
    RoboArpEntry *e = find_ip(ip);
    if (!e)
        return -1;
    if (e->queued) {
        robo_pkt_free(e->queued);
        e->queued = 0;
    }
    e->used = 0;
    return 0;
}

void robo_arp_expire(void)
{
    unsigned i;
    uint64_t now = robo_time_now();
    for (i = 0; i < ROBO_NET_MAX_ARP; i++) {
        if (!g_arp[i].used)
            continue;
        if (now < g_arp[i].expire_ns)
            continue;
        if (g_arp[i].state == ROBO_ARP_REACHABLE)
            g_arp[i].state = ROBO_ARP_STALE;
        else {
            if (g_arp[i].queued) {
                robo_pkt_free(g_arp[i].queued);
                g_arp[i].queued = 0;
            }
            g_arp[i].used = 0;
            robo_net_signal(&g_arp_waiters[i], &g_arp_posted[i]);
        }
    }
}

static int send_arp(RoboNetIf *ifp, uint16_t op, uint32_t sip, uint32_t tip,
                    const uint8_t *tha)
{
    uint8_t p[28];
    RoboPacket *pkt;
    uint8_t bcast[6];
    unsigned i;
    if (!ifp)
        return -1;
    robo_net_memset(p, 0, 28);
    robo_wr16(p, ARP_HTYPE);
    robo_wr16(p + 2, ARP_PTYPE);
    p[4] = ARP_HLEN;
    p[5] = ARP_PLEN;
    robo_wr16(p + 6, op);
    for (i = 0; i < 6; i++)
        p[8 + i] = ifp->mac[i];
    robo_wr32(p + 14, sip);
    if (tha) {
        for (i = 0; i < 6; i++)
            p[18 + i] = tha[i];
    }
    robo_wr32(p + 24, tip);
    pkt = robo_pkt_alloc();
    if (!pkt)
        return -1;
    if (robo_pkt_copyin(pkt, p, 28) != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    for (i = 0; i < 6; i++)
        bcast[i] = (op == ARP_REQ) ? 0xFFu : (tha ? tha[i] : 0xFFu);
    i = (unsigned)robo_eth_send(ifp, bcast, ROBO_ETH_ARP, pkt);
    robo_pkt_free(pkt);
    return (int)i;
}

int robo_arp_request(RoboNetIf *ifp, uint32_t tip)
{
    RoboArpEntry *e;
    if (!ifp)
        return -1;
    e = find_ip(tip);
    if (!e) {
        e = alloc_ent();
        if (!e)
            return -1;
        e->used = 1;
        e->ip = tip;
        e->ifp = ifp;
        e->state = ROBO_ARP_INCOMPLETE;
        e->expire_ns = robo_time_now() + 3000000000ull;
        e->queued = 0;
        robo_net_memset(e->mac, 0, 6);
    }
    return send_arp(ifp, ARP_REQ, ifp->ipv4, tip, 0);
}

int robo_arp_resolve(RoboNetIf *ifp, uint32_t ip, uint8_t mac[6], int can_block)
{
    RoboArpEntry *e;
    unsigned idx, spins;
    if (robo_arp_lookup(ifp, ip, mac) == 0)
        return 0;
    if (robo_arp_request(ifp, ip) != 0)
        return -1;
    if (!can_block)
        return -1;
    e = find_ip(ip);
    if (!e)
        return -1;
    idx = (unsigned)(e - g_arp);
    for (spins = 0; spins < 40u; spins++) {
        if (robo_arp_lookup(ifp, ip, mac) == 0)
            return 0;
        robo_net_wait(&g_arp_waiters[idx], &g_arp_posted[idx]);
        robo_sleep(50);
    }
    return -1;
}

int robo_arp_rx(RoboNetIf *ifp, RoboPacket *pkt)
{
    const uint8_t *p;
    uint16_t htype, ptype, op;
    uint32_t sip, tip;
    uint8_t sha[6];
    unsigned i;

    if (!pkt || pkt->length < 28u) {
        robo_pkt_free(pkt);
        return -1;
    }
    p = robo_pkt_cdata(pkt);
    htype = robo_rd16(p);
    ptype = robo_rd16(p + 2);
    op = robo_rd16(p + 6);
    if (htype != ARP_HTYPE || ptype != ARP_PTYPE || p[4] != ARP_HLEN ||
        p[5] != ARP_PLEN) {
        robo_pkt_free(pkt);
        return -1;
    }
    if (op != ARP_REQ && op != ARP_REP) {
        robo_pkt_free(pkt);
        return -1;
    }
    for (i = 0; i < 6; i++)
        sha[i] = p[8 + i];
    sip = robo_rd32(p + 14);
    tip = robo_rd32(p + 24);
    if (sip)
        robo_arp_insert(ifp, sip, sha, ROBO_ARP_REACHABLE);
    if (op == ARP_REQ && ifp && tip == ifp->ipv4)
        send_arp(ifp, ARP_REP, ifp->ipv4, sip, sha);
    {
        char li[16], ti[16], tm[18];
        robo_ipv4_fmt(ifp ? ifp->ipv4 : 0, li);
        robo_ipv4_fmt(sip, ti);
        robo_mac_fmt(sha, tm);
        kprintf("ARP\n---\nInterface : %s\nLocal IP  : %s\nTarget IP : %s\nTarget MAC: %s\nState     : REACHABLE\n",
                ifp ? ifp->name : "?", li, ti, tm);
    }
    robo_pkt_free(pkt);
    return 0;
}

unsigned robo_arp_count(void)
{
    unsigned i, n = 0;
    for (i = 0; i < ROBO_NET_MAX_ARP; i++) {
        if (g_arp[i].used)
            n++;
    }
    return n;
}

RoboArpEntry *robo_arp_at(unsigned i)
{
    unsigned n = 0, k;
    for (k = 0; k < ROBO_NET_MAX_ARP; k++) {
        if (!g_arp[k].used)
            continue;
        if (n == i)
            return &g_arp[k];
        n++;
    }
    return 0;
}

void robo_arp_report(void)
{
    unsigned i;
    kprintf("ARP cache\n---------\n");
    for (i = 0; i < ROBO_NET_MAX_ARP; i++) {
        char ip[16], mac[18];
        if (!g_arp[i].used)
            continue;
        robo_ipv4_fmt(g_arp[i].ip, ip);
        robo_mac_fmt(g_arp[i].mac, mac);
        kprintf("%s %s state %u\n", ip, mac, (unsigned)g_arp[i].state);
    }
}
