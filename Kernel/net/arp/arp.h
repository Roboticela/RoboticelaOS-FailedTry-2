#ifndef ROBO_NET_ARP_H
#define ROBO_NET_ARP_H

#include <stdint.h>
#include "net/core/interface.h"
#include "net/core/packet.h"

#define RoboArpIncomplete 1u
#define RoboArpReachable  2u
#define RoboArpStale      3u

typedef struct RoboArpEntry {
    uint32_t ip;
    uint8_t mac[6];
    uint8_t state;
    uint8_t used;
    uint64_t expireNs;
    RoboPacket *queued;
    struct RoboNetIf *ifp;
} RoboArpEntry;

int ArpInit(void);
int ArpLookup(RoboNetIf *ifp, uint32_t ip, uint8_t mac[6]);
int ArpInsert(RoboNetIf *ifp, uint32_t ip, const uint8_t mac[6],
                    uint8_t state);
int ArpRemove(uint32_t ip);
void ArpExpire(void);
int ArpRx(RoboNetIf *ifp, RoboPacket *pkt);
int ArpRequest(RoboNetIf *ifp, uint32_t tip);
int ArpResolve(RoboNetIf *ifp, uint32_t ip, uint8_t mac[6], int canBlock);
void ArpReport(void);
unsigned ArpCount(void);
RoboArpEntry *ArpAt(unsigned i);

#endif
