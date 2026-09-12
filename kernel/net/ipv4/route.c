#include "net/ipv4/route.h"
#include "net/core/net_util.h"
#include "kio.h"

static RoboRoute g_rt[ROBO_NET_MAX_ROUTE];
static unsigned g_nrt;

int robo_route_init(void)
{
    unsigned i;
    for (i = 0; i < ROBO_NET_MAX_ROUTE; i++)
        robo_net_memset(&g_rt[i], 0, (unsigned)sizeof(g_rt[i]));
    g_nrt = 0;
    return 1;
}

int robo_route_add(uint32_t dest, uint32_t netmask, uint32_t gateway,
                   RoboNetIf *ifp, uint32_t metric)
{
    RoboRoute *r;
    if (!ifp || g_nrt >= ROBO_NET_MAX_ROUTE)
        return -1;
    r = &g_rt[g_nrt++];
    r->dest = dest;
    r->netmask = netmask;
    r->gateway = gateway;
    r->ifp = ifp;
    r->metric = metric;
    r->used = 1;
    return 0;
}

int robo_route_lookup(uint32_t dst, RoboNetIf **ifp, uint32_t *nexthop)
{
    unsigned i;
    RoboRoute *best = 0;
    uint32_t best_mask = 0;
    uint32_t best_metric = 0xFFFFFFFFu;
    for (i = 0; i < g_nrt; i++) {
        RoboRoute *r = &g_rt[i];
        if (!r->used || !r->ifp)
            continue;
        if ((dst & r->netmask) != (r->dest & r->netmask))
            continue;
        if (r->netmask > best_mask ||
            (r->netmask == best_mask && r->metric < best_metric)) {
            best = r;
            best_mask = r->netmask;
            best_metric = r->metric;
        }
    }
    if (!best)
        return -1;
    if (ifp)
        *ifp = best->ifp;
    if (nexthop)
        *nexthop = best->gateway ? best->gateway : dst;
    return 0;
}

unsigned robo_route_count(void)
{
    return g_nrt;
}

RoboRoute *robo_route_at(unsigned i)
{
    return i < g_nrt ? &g_rt[i] : 0;
}

void robo_route_report(void)
{
    unsigned i;
    kprintf("Routes\n------\n");
    for (i = 0; i < g_nrt; i++) {
        char d[16], m[16], g[16];
        RoboRoute *r = &g_rt[i];
        if (!r->used)
            continue;
        robo_ipv4_fmt(r->dest, d);
        robo_ipv4_fmt(r->netmask, m);
        robo_ipv4_fmt(r->gateway, g);
        kprintf("%s/%s via %s dev %s metric %u\n",
                d, m, g, r->ifp ? r->ifp->name : "?", (unsigned)r->metric);
    }
}
