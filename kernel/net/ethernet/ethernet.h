#ifndef ROBO_NET_ETHERNET_H
#define ROBO_NET_ETHERNET_H

#include <stdint.h>
#include "net/core/interface.h"
#include "net/core/packet.h"

int robo_eth_parse(const uint8_t *data, unsigned len,
                   uint8_t dst[6], uint8_t src[6], uint16_t *type,
                   unsigned *payload_off, unsigned *payload_len);
int robo_eth_rx(RoboNetIf *ifp, const void *data, unsigned len);
int robo_eth_send(RoboNetIf *ifp, const uint8_t dst[6], uint16_t type,
                  RoboPacket *pkt);

#endif
