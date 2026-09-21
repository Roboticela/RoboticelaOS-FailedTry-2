#include "net/ipv4/route.h"
#include "net/core/net_util.h"
#include "kio.h"

static RoboRoute gRt[RoboNetMaxRoute];
static unsigned gNrt;

int RouteInit(void)
{
    unsigned i;
    for (i = 0; i < RoboNetMaxRoute; i++)
        NetMemset(&gRt[i], 0, (unsigned)sizeof(gRt[i]));
    gNrt = 0;
    return 1;
}

int RouteAdd(uint32_t dest, uint32_t netmask, uint32_t gateway,
                   RoboNetIf *ifp, uint32_t metric)
{
    RoboRoute *r;
    if (!ifp || gNrt >= RoboNetMaxRoute)
        return -1;
    r = &gRt[gNrt++];
    r->dest = dest;
    r->netmask = netmask;
    r->gateway = gateway;
    r->ifp = ifp;
    r->metric = metric;
    r->used = 1;
    return 0;
}

int RouteLookup(uint32_t dst, RoboNetIf **ifp, uint32_t *nexthop)
{
    unsigned i;
    RoboRoute *best = 0;
    uint32_t bestMask = 0;
    uint32_t bestMetric = 0xFFFFFFFFu;
    for (i = 0; i < gNrt; i++) {
        RoboRoute *r = &gRt[i];
        if (!r->used || !r->ifp)
            continue;
        if ((dst & r->netmask) != (r->dest & r->netmask))
            continue;
        if (r->netmask > bestMask ||
            (r->netmask == bestMask && r->metric < bestMetric)) {
            best = r;
            bestMask = r->netmask;
            bestMetric = r->metric;
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

unsigned RouteCount(void)
{
    return gNrt;
}

RoboRoute *RouteAt(unsigned i)
{
    return i < gNrt ? &gRt[i] : 0;
}

void RouteReport(void)
{
    unsigned i;
    kprintf("Routes\n------\n");
    for (i = 0; i < gNrt; i++) {
        char d[16], m[16], g[16];
        RoboRoute *r = &gRt[i];
        if (!r->used)
            continue;
        Ipv4Fmt(r->dest, d);
        Ipv4Fmt(r->netmask, m);
        Ipv4Fmt(r->gateway, g);
        kprintf("%s/%s via %s dev %s metric %u\n",
                d, m, g, r->ifp ? r->ifp->name : "?", (unsigned)r->metric);
    }
}
