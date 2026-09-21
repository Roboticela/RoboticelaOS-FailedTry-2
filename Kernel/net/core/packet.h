#ifndef ROBO_NET_PACKET_H
#define ROBO_NET_PACKET_H

#include <stdint.h>

struct RoboNetIf;

typedef struct RoboPacket {
    uint8_t *buf;
    unsigned cap;
    unsigned offset;
    unsigned length;
    uint32_t srcIp;
    uint32_t dstIp;
    uint16_t srcPort;
    uint16_t dstPort;
    uint8_t proto;
    uint8_t pad[3];
    struct RoboNetIf *ifp;
} RoboPacket;

int PktPoolInit(void);
RoboPacket *PktAlloc(void);
void PktFree(RoboPacket *p);
void PktReset(RoboPacket *p);
uint8_t *PktData(RoboPacket *p);
const uint8_t *PktCdata(const RoboPacket *p);
int PktPush(RoboPacket *p, unsigned n);
int PktPull(RoboPacket *p, unsigned n);
int PktAppend(RoboPacket *p, const void *src, unsigned n);
int PktTrim(RoboPacket *p, unsigned n);
int PktCopyin(RoboPacket *p, const void *src, unsigned n);

#endif
