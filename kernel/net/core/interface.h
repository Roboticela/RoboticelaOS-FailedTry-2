#ifndef ROBO_NET_INTERFACE_H
#define ROBO_NET_INTERFACE_H

#include <stdint.h>
#include "net/core/net_util.h"

struct RoboPacket;

typedef struct RoboNetIf {
    uint32_t id;
    char name[ROBO_NET_NAME_MAX];
    uint8_t mac[6];
    uint8_t pad[2];
    uint32_t ipv4;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint16_t mtu;
    uint8_t link_up;
    uint8_t used;
    int (*send_frame)(struct RoboNetIf *ifp, const void *data, unsigned len);
    void *drv;
    uint64_t tx_frames;
    uint64_t rx_frames;
    uint64_t rx_drop;
} RoboNetIf;

int robo_netif_init(void);
RoboNetIf *robo_netif_register(const char *name, const uint8_t mac[6],
                               int (*send_frame)(RoboNetIf *, const void *, unsigned),
                               void *drv);
RoboNetIf *robo_netif_default(void);
RoboNetIf *robo_netif_by_id(uint32_t id);
unsigned robo_netif_count(void);
RoboNetIf *robo_netif_at(unsigned i);
void robo_netif_set_ipv4(RoboNetIf *ifp, uint32_t ip, uint32_t mask,
                         uint32_t gw, uint32_t dns);
void robo_netif_set_link(RoboNetIf *ifp, int up);
int robo_netif_send(RoboNetIf *ifp, const void *data, unsigned len);
int robo_netif_rx(RoboNetIf *ifp, const void *data, unsigned len);

#endif
