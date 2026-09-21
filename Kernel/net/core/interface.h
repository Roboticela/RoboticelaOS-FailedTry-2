#ifndef ROBO_NET_INTERFACE_H
#define ROBO_NET_INTERFACE_H

#include <stdint.h>
#include "net/core/net_util.h"

struct RoboPacket;

typedef struct RoboNetIf {
    uint32_t id;
    char name[RoboNetNameMax];
    uint8_t mac[6];
    uint8_t pad[2];
    uint32_t ipv4;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint16_t mtu;
    uint8_t linkUp;
    uint8_t used;
    int (*SendFrame)(struct RoboNetIf *ifp, const void *data, unsigned len);
    void *drv;
    uint64_t txFrames;
    uint64_t rxFrames;
    uint64_t rxDrop;
} RoboNetIf;

int NetifInit(void);
RoboNetIf *NetifRegister(const char *name, const uint8_t mac[6],
                               int (*SendFrame)(RoboNetIf *, const void *, unsigned),
                               void *drv);
RoboNetIf *NetifDefault(void);
RoboNetIf *NetifById(uint32_t id);
unsigned NetifCount(void);
RoboNetIf *NetifAt(unsigned i);
void NetifSetIpv4(RoboNetIf *ifp, uint32_t ip, uint32_t mask,
                         uint32_t gw, uint32_t dns);
void NetifSetLink(RoboNetIf *ifp, int up);
int NetifSend(RoboNetIf *ifp, const void *data, unsigned len);
int NetifRx(RoboNetIf *ifp, const void *data, unsigned len);

#endif
