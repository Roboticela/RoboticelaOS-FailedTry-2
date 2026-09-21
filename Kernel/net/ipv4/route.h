#ifndef ROBO_NET_ROUTE_H
#define ROBO_NET_ROUTE_H

#include <stdint.h>
#include "net/core/interface.h"

typedef struct RoboRoute {
    uint32_t dest;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t metric;
    RoboNetIf *ifp;
    uint8_t used;
} RoboRoute;

int RouteInit(void);
int RouteAdd(uint32_t dest, uint32_t netmask, uint32_t gateway,
                   RoboNetIf *ifp, uint32_t metric);
int RouteLookup(uint32_t dst, RoboNetIf **ifp, uint32_t *nexthop);
void RouteReport(void);
unsigned RouteCount(void);
RoboRoute *RouteAt(unsigned i);

#endif
