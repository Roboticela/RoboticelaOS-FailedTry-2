#ifndef ROBO_NET_IPV4_H
#define ROBO_NET_IPV4_H

#include <stdint.h>
#include "net/core/packet.h"
#include "net/core/interface.h"

#define ROBO_IPV4_MIN_IHL 5u

typedef struct RoboIpv4Info {
    uint8_t ihl;
    uint8_t tos;
    uint16_t total;
    uint16_t id;
    uint16_t flags_off;
    uint8_t ttl;
    uint8_t proto;
    uint16_t csum;
    uint32_t src;
    uint32_t dst;
} RoboIpv4Info;

int robo_ipv4_parse(const uint8_t *data, unsigned len, RoboIpv4Info *out);
int robo_ipv4_rx(RoboNetIf *ifp, RoboPacket *pkt);
int robo_ipv4_send(RoboNetIf *ifp, uint32_t dst, uint8_t proto,
                   RoboPacket *pkt, int can_block);
int robo_ipv4_local(uint32_t ip);

#endif
