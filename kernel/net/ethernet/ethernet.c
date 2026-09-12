#include "net/ethernet/ethernet.h"
#include "net/core/net_util.h"
#ifndef NET_HOST_TEST
#include "net/core/packet.h"
#include "net/arp/arp.h"
#include "net/ipv4/ipv4.h"
#include "net/core/network.h"
#include "kio.h"
#endif

int robo_eth_parse(const uint8_t *data, unsigned len,
                   uint8_t dst[6], uint8_t src[6], uint16_t *type,
                   unsigned *payload_off, unsigned *payload_len)
{
    uint16_t et;
    unsigned i, plen;
    if (!data || len < ROBO_NET_ETH_HDR)
        return -1;
    if (len > ROBO_NET_ETH_MAX)
        return -1;
    et = robo_rd16(data + 12);
    if (et < 0x0600u)
        return -1;
    plen = len - ROBO_NET_ETH_HDR;
    if (et != ROBO_ETH_IP && et != ROBO_ETH_ARP)
        return -1;
    if (dst) {
        for (i = 0; i < 6; i++)
            dst[i] = data[i];
    }
    if (src) {
        for (i = 0; i < 6; i++)
            src[i] = data[6 + i];
    }
    if (type)
        *type = et;
    if (payload_off)
        *payload_off = ROBO_NET_ETH_HDR;
    if (payload_len)
        *payload_len = plen;
    return 0;
}

#ifndef NET_HOST_TEST
int robo_eth_rx(RoboNetIf *ifp, const void *data, unsigned len)
{
    const uint8_t *d = (const uint8_t *)data;
    uint8_t dst[6], src[6];
    uint16_t type;
    unsigned off, plen;
    RoboPacket *pkt;
    unsigned i, mine, bcast;

    if (robo_eth_parse(d, len, dst, src, &type, &off, &plen) != 0) {
        if (ifp)
            ifp->rx_drop++;
        return -1;
    }
    mine = 1;
    bcast = 1;
    if (ifp) {
        mine = 1;
        for (i = 0; i < 6; i++) {
            if (dst[i] != ifp->mac[i])
                mine = 0;
            if (dst[i] != 0xFFu)
                bcast = 0;
        }
        if (!mine && !bcast)
            return 0;
    }
    pkt = robo_pkt_alloc();
    if (!pkt) {
        if (ifp)
            ifp->rx_drop++;
        return -1;
    }
    pkt->ifp = ifp;
    if (robo_pkt_copyin(pkt, d + off, plen) != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    if (type == ROBO_ETH_ARP)
        return robo_arp_rx(ifp, pkt);
    if (type == ROBO_ETH_IP)
        return robo_ipv4_rx(ifp, pkt);
    robo_pkt_free(pkt);
    return 0;
}

int robo_eth_send(RoboNetIf *ifp, const uint8_t dst[6], uint16_t type,
                  RoboPacket *pkt)
{
    uint8_t frame[ROBO_NET_ETH_MAX];
    unsigned i, n, payload;
    if (!ifp || !dst || !pkt)
        return -1;
    payload = pkt->length;
    if (payload > ifp->mtu)
        return -1;
    n = ROBO_NET_ETH_HDR + payload;
    if (n > ROBO_NET_ETH_MAX)
        return -1;
    for (i = 0; i < 6; i++) {
        frame[i] = dst[i];
        frame[6 + i] = ifp->mac[i];
    }
    robo_wr16(frame + 12, type);
    if (payload)
        robo_net_memcpy(frame + 14, robo_pkt_cdata(pkt), payload);
    while (n < ROBO_NET_ETH_MIN)
        frame[n++] = 0;
    return robo_netif_send(ifp, frame, n);
}
#endif
