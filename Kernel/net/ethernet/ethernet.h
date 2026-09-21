#ifndef ROBO_NET_ETHERNET_H
#define ROBO_NET_ETHERNET_H

#include <stdint.h>
#include "net/core/interface.h"
#include "net/core/packet.h"

int EthParse(const uint8_t *data, unsigned len,
                   uint8_t dst[6], uint8_t src[6], uint16_t *type,
                   unsigned *payloadOff, unsigned *payloadLen);
int EthRx(RoboNetIf *ifp, const void *data, unsigned len);
int EthSend(RoboNetIf *ifp, const uint8_t dst[6], uint16_t type,
                  RoboPacket *pkt);

#endif
