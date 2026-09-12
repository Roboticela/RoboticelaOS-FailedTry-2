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

int robo_route_init(void);
int robo_route_add(uint32_t dest, uint32_t netmask, uint32_t gateway,
                   RoboNetIf *ifp, uint32_t metric);
int robo_route_lookup(uint32_t dst, RoboNetIf **ifp, uint32_t *nexthop);
void robo_route_report(void);
unsigned robo_route_count(void);
RoboRoute *robo_route_at(unsigned i);

#endif
