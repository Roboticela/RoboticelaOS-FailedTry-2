#ifndef ROBO_NET_ICMP_H
#define ROBO_NET_ICMP_H

#include <stdint.h>
#include "net/core/packet.h"
#include "net/core/interface.h"

int IcmpRx(RoboNetIf *ifp, RoboPacket *pkt, uint32_t src, uint32_t dst);
int IcmpPing(uint32_t dst, uint32_t timeoutMs);

#endif
