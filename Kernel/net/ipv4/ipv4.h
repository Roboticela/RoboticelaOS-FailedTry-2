#ifndef ROBO_NET_IPV4_H
#define ROBO_NET_IPV4_H

#include <stdint.h>
#include "net/core/packet.h"
#include "net/core/interface.h"

#define RoboIpv4MinIhl 5u

typedef struct RoboIpv4Info {
    uint8_t ihl;
    uint8_t tos;
    uint16_t total;
    uint16_t id;
    uint16_t flagsOff;
    uint8_t ttl;
    uint8_t proto;
    uint16_t csum;
    uint32_t src;
    uint32_t dst;
} RoboIpv4Info;

int Ipv4Parse(const uint8_t *data, unsigned len, RoboIpv4Info *out);
int Ipv4Rx(RoboNetIf *ifp, RoboPacket *pkt);
int Ipv4Send(RoboNetIf *ifp, uint32_t dst, uint8_t proto,
                   RoboPacket *pkt, int canBlock);
int Ipv4Local(uint32_t ip);

#endif
