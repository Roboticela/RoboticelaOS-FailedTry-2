#ifndef ROBO_NET_ARP_H
#define ROBO_NET_ARP_H

#include <stdint.h>
#include "net/core/interface.h"
#include "net/core/packet.h"

#define ROBO_ARP_INCOMPLETE 1u
#define ROBO_ARP_REACHABLE  2u
#define ROBO_ARP_STALE      3u

typedef struct RoboArpEntry {
    uint32_t ip;
    uint8_t mac[6];
    uint8_t state;
    uint8_t used;
    uint64_t expire_ns;
    RoboPacket *queued;
    struct RoboNetIf *ifp;
} RoboArpEntry;

int robo_arp_init(void);
int robo_arp_lookup(RoboNetIf *ifp, uint32_t ip, uint8_t mac[6]);
int robo_arp_insert(RoboNetIf *ifp, uint32_t ip, const uint8_t mac[6],
                    uint8_t state);
int robo_arp_remove(uint32_t ip);
void robo_arp_expire(void);
int robo_arp_rx(RoboNetIf *ifp, RoboPacket *pkt);
int robo_arp_request(RoboNetIf *ifp, uint32_t tip);
int robo_arp_resolve(RoboNetIf *ifp, uint32_t ip, uint8_t mac[6], int can_block);
void robo_arp_report(void);
unsigned robo_arp_count(void);
RoboArpEntry *robo_arp_at(unsigned i);

#endif
