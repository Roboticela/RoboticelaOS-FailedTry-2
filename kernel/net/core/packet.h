#ifndef ROBO_NET_PACKET_H
#define ROBO_NET_PACKET_H

#include <stdint.h>

struct RoboNetIf;

typedef struct RoboPacket {
    uint8_t *buf;
    unsigned cap;
    unsigned offset;
    unsigned length;
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t proto;
    uint8_t pad[3];
    struct RoboNetIf *ifp;
} RoboPacket;

int robo_pkt_pool_init(void);
RoboPacket *robo_pkt_alloc(void);
void robo_pkt_free(RoboPacket *p);
void robo_pkt_reset(RoboPacket *p);
uint8_t *robo_pkt_data(RoboPacket *p);
const uint8_t *robo_pkt_cdata(const RoboPacket *p);
int robo_pkt_push(RoboPacket *p, unsigned n);
int robo_pkt_pull(RoboPacket *p, unsigned n);
int robo_pkt_append(RoboPacket *p, const void *src, unsigned n);
int robo_pkt_trim(RoboPacket *p, unsigned n);
int robo_pkt_copyin(RoboPacket *p, const void *src, unsigned n);

#endif
