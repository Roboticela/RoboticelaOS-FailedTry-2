#ifndef ROBO_NET_UDP_H
#define ROBO_NET_UDP_H

#include <stdint.h>
#include "net/core/packet.h"
#include "net/socket/socket.h"

int robo_udp_rx(RoboPacket *pkt, uint32_t src, uint32_t dst);
int robo_udp_send(struct RoboSocket *s, uint32_t dst_ip, uint16_t dst_port,
                  const void *buf, unsigned len);
int robo_udp_recv(struct RoboSocket *s, void *buf, unsigned len,
                  uint32_t *src_ip, uint16_t *src_port, int nonblock);

#endif
