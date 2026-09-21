#ifndef ROBO_NET_UDP_H
#define ROBO_NET_UDP_H

#include <stdint.h>
#include "net/core/packet.h"
#include "net/socket/socket.h"

int UdpRx(RoboPacket *pkt, uint32_t src, uint32_t dst);
int UdpSend(struct RoboSocket *s, uint32_t dstIp, uint16_t dstPort,
                  const void *buf, unsigned len);
int UdpRecv(struct RoboSocket *s, void *buf, unsigned len,
                  uint32_t *srcIp, uint16_t *srcPort, int nonblock);

#endif
