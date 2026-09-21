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

static RoboArpEntry gArp[RoboNetMaxArp];
static struct RoboThread *gArpWaiters[RoboNetMaxArp];
static int gArpPosted[RoboNetMaxArp];

static RoboArpEntry *FindIp(uint32_t ip)
{
    unsigned i;
    for (i = 0; i < RoboNetMaxArp; i++) {
        if (gArp[i].used && gArp[i].ip == ip)
            return &gArp[i];
    }
    return 0;
}

static RoboArpEntry *AllocEnt(void)
{
    unsigned i;
    uint64_t oldest = ~0ull;
    int oldi = -1;
    for (i = 0; i < RoboNetMaxArp; i++) {
        if (!gArp[i].used)
            return &gArp[i];
        if (gArp[i].expireNs < oldest) {
            oldest = gArp[i].expireNs;
            oldi = (int)i;
        }
    }
    if (oldi < 0)
        return 0;
    if (gArp[oldi].queued) {
        PktFree(gArp[oldi].queued);
        gArp[oldi].queued = 0;
    }
    return &gArp[oldi];
}

int ArpInit(void)
{
    unsigned i;
    for (i = 0; i < RoboNetMaxArp; i++) {
        NetMemset(&gArp[i], 0, (unsigned)sizeof(gArp[i]));
        gArpWaiters[i] = 0;
        gArpPosted[i] = 0;
    }
    return 1;
}

int ArpInsert(RoboNetIf *ifp, uint32_t ip, const uint8_t mac[6],
                    uint8_t state)
{
    RoboArpEntry *e;
    unsigned i;
    if (!mac || !ip)
        return -1;
    e = FindIp(ip);
    if (!e)
        e = AllocEnt();
    if (!e)
        return -1;
    e->used = 1;
    e->ip = ip;
    e->ifp = ifp;
    e->state = state;
    for (i = 0; i < 6; i++)
        e->mac[i] = mac[i];
    e->expireNs = TimeNow() + ARP_TTL_NS;
    i = (unsigned)(e - gArp);
    NetSignal(&gArpWaiters[i], &gArpPosted[i]);
    if (e->queued && state == RoboArpReachable && ifp) {
        EthSend(ifp, e->mac, RoboEthIp, e->queued);
        PktFree(e->queued);
        e->queued = 0;
    }
    if (NetDebug()) {
        char ips[16], macs[18];
        Ipv4Fmt(ip, ips);
        MacFmt(e->mac, macs);
        kprintf("[INFO] [NET] ARP %s -> %s state %u\n",
                ips, macs, (unsigned)state);
    }
    return 0;
}

int ArpLookup(RoboNetIf *ifp, uint32_t ip, uint8_t mac[6])
{
    RoboArpEntry *e = FindIp(ip);
    unsigned i;
    (void)ifp;
    if (!e || e->state == RoboArpIncomplete)
        return -1;
    if (mac) {
        for (i = 0; i < 6; i++)
            mac[i] = e->mac[i];
    }
    return 0;
}

int ArpRemove(uint32_t ip)
{
    RoboArpEntry *e = FindIp(ip);
    if (!e)
        return -1;
    if (e->queued) {
        PktFree(e->queued);
        e->queued = 0;
    }
    e->used = 0;
    return 0;
}

void ArpExpire(void)
{
    unsigned i;
    uint64_t now = TimeNow();
    for (i = 0; i < RoboNetMaxArp; i++) {
        if (!gArp[i].used)
            continue;
        if (now < gArp[i].expireNs)
            continue;
        if (gArp[i].state == RoboArpReachable)
            gArp[i].state = RoboArpStale;
        else {
            if (gArp[i].queued) {
                PktFree(gArp[i].queued);
                gArp[i].queued = 0;
            }
            gArp[i].used = 0;
            NetSignal(&gArpWaiters[i], &gArpPosted[i]);
        }
    }
}

static int SendArp(RoboNetIf *ifp, uint16_t op, uint32_t sip, uint32_t tip,
                    const uint8_t *tha)
{
    uint8_t p[28];
    RoboPacket *pkt;
    uint8_t bcast[6];
    unsigned i;
    if (!ifp)
        return -1;
    NetMemset(p, 0, 28);
    Wr16(p, ARP_HTYPE);
    Wr16(p + 2, ARP_PTYPE);
    p[4] = ARP_HLEN;
    p[5] = ARP_PLEN;
    Wr16(p + 6, op);
    for (i = 0; i < 6; i++)
        p[8 + i] = ifp->mac[i];
    Wr32(p + 14, sip);
    if (tha) {
        for (i = 0; i < 6; i++)
            p[18 + i] = tha[i];
    }
    Wr32(p + 24, tip);
    pkt = PktAlloc();
    if (!pkt)
        return -1;
    if (PktCopyin(pkt, p, 28) != 0) {
        PktFree(pkt);
        return -1;
    }
    for (i = 0; i < 6; i++)
        bcast[i] = (op == ARP_REQ) ? 0xFFu : (tha ? tha[i] : 0xFFu);
    i = (unsigned)EthSend(ifp, bcast, RoboEthArp, pkt);
    PktFree(pkt);
    return (int)i;
}

int ArpRequest(RoboNetIf *ifp, uint32_t tip)
{
    RoboArpEntry *e;
    if (!ifp)
        return -1;
    e = FindIp(tip);
    if (!e) {
        e = AllocEnt();
        if (!e)
            return -1;
        e->used = 1;
        e->ip = tip;
        e->ifp = ifp;
        e->state = RoboArpIncomplete;
        e->expireNs = TimeNow() + 3000000000ull;
        e->queued = 0;
        NetMemset(e->mac, 0, 6);
    }
    return SendArp(ifp, ARP_REQ, ifp->ipv4, tip, 0);
}

int ArpResolve(RoboNetIf *ifp, uint32_t ip, uint8_t mac[6], int canBlock)
{
    RoboArpEntry *e;
    unsigned idx, spins;
    if (ArpLookup(ifp, ip, mac) == 0)
        return 0;
    if (ArpRequest(ifp, ip) != 0)
        return -1;
    if (!canBlock)
        return -1;
    e = FindIp(ip);
    if (!e)
        return -1;
    idx = (unsigned)(e - gArp);
    for (spins = 0; spins < 40u; spins++) {
        if (ArpLookup(ifp, ip, mac) == 0)
            return 0;
        NetWait(&gArpWaiters[idx], &gArpPosted[idx]);
        RoboSleep(50);
    }
    return -1;
}

int ArpRx(RoboNetIf *ifp, RoboPacket *pkt)
{
    const uint8_t *p;
    uint16_t htype, ptype, op;
    uint32_t sip, tip;
    uint8_t sha[6];
    unsigned i;

    if (!pkt || pkt->length < 28u) {
        PktFree(pkt);
        return -1;
    }
    p = PktCdata(pkt);
    htype = Rd16(p);
    ptype = Rd16(p + 2);
    op = Rd16(p + 6);
    if (htype != ARP_HTYPE || ptype != ARP_PTYPE || p[4] != ARP_HLEN ||
        p[5] != ARP_PLEN) {
        PktFree(pkt);
        return -1;
    }
    if (op != ARP_REQ && op != ARP_REP) {
        PktFree(pkt);
        return -1;
    }
    for (i = 0; i < 6; i++)
        sha[i] = p[8 + i];
    sip = Rd32(p + 14);
    tip = Rd32(p + 24);
    if (sip)
        ArpInsert(ifp, sip, sha, RoboArpReachable);
    if (op == ARP_REQ && ifp && tip == ifp->ipv4)
        SendArp(ifp, ARP_REP, ifp->ipv4, sip, sha);
    {
        char li[16], ti[16], tm[18];
        Ipv4Fmt(ifp ? ifp->ipv4 : 0, li);
        Ipv4Fmt(sip, ti);
        MacFmt(sha, tm);
        kprintf("ARP\n---\nInterface : %s\nLocal IP  : %s\nTarget IP : %s\nTarget MAC: %s\nState     : REACHABLE\n",
                ifp ? ifp->name : "?", li, ti, tm);
    }
    PktFree(pkt);
    return 0;
}

unsigned ArpCount(void)
{
    unsigned i, n = 0;
    for (i = 0; i < RoboNetMaxArp; i++) {
        if (gArp[i].used)
            n++;
    }
    return n;
}

RoboArpEntry *ArpAt(unsigned i)
{
    unsigned n = 0, k;
    for (k = 0; k < RoboNetMaxArp; k++) {
        if (!gArp[k].used)
            continue;
        if (n == i)
            return &gArp[k];
        n++;
    }
    return 0;
}

void ArpReport(void)
{
    unsigned i;
    kprintf("ARP cache\n---------\n");
    for (i = 0; i < RoboNetMaxArp; i++) {
        char ip[16], mac[18];
        if (!gArp[i].used)
            continue;
        Ipv4Fmt(gArp[i].ip, ip);
        MacFmt(gArp[i].mac, mac);
        kprintf("%s %s state %u\n", ip, mac, (unsigned)gArp[i].state);
    }
}
