#include "net/ethernet/ethernet.h"
#include "net/core/net_util.h"
#ifndef NET_HOST_TEST
#include "net/core/packet.h"
#include "net/arp/arp.h"
#include "net/ipv4/ipv4.h"
#include "net/core/network.h"
#include "kio.h"
#endif

int EthParse(const uint8_t *data, unsigned len,
                   uint8_t dst[6], uint8_t src[6], uint16_t *type,
                   unsigned *payloadOff, unsigned *payloadLen)
{
    uint16_t et;
    unsigned i, plen;
    if (!data || len < RoboNetEthHdr)
        return -1;
    if (len > RoboNetEthMax)
        return -1;
    et = Rd16(data + 12);
    if (et < 0x0600u)
        return -1;
    plen = len - RoboNetEthHdr;
    if (et != RoboEthIp && et != RoboEthArp)
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
    if (payloadOff)
        *payloadOff = RoboNetEthHdr;
    if (payloadLen)
        *payloadLen = plen;
    return 0;
}

#ifndef NET_HOST_TEST
int EthRx(RoboNetIf *ifp, const void *data, unsigned len)
{
    const uint8_t *d = (const uint8_t *)data;
    uint8_t dst[6], src[6];
    uint16_t type;
    unsigned off, plen;
    RoboPacket *pkt;
    unsigned i, mine, bcast;

    if (EthParse(d, len, dst, src, &type, &off, &plen) != 0) {
        if (ifp)
            ifp->rxDrop++;
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
    pkt = PktAlloc();
    if (!pkt) {
        if (ifp)
            ifp->rxDrop++;
        return -1;
    }
    pkt->ifp = ifp;
    if (PktCopyin(pkt, d + off, plen) != 0) {
        PktFree(pkt);
        return -1;
    }
    if (type == RoboEthArp)
        return ArpRx(ifp, pkt);
    if (type == RoboEthIp)
        return Ipv4Rx(ifp, pkt);
    PktFree(pkt);
    return 0;
}

int EthSend(RoboNetIf *ifp, const uint8_t dst[6], uint16_t type,
                  RoboPacket *pkt)
{
    uint8_t frame[RoboNetEthMax];
    unsigned i, n, payload;
    if (!ifp || !dst || !pkt)
        return -1;
    payload = pkt->length;
    if (payload > ifp->mtu)
        return -1;
    n = RoboNetEthHdr + payload;
    if (n > RoboNetEthMax)
        return -1;
    for (i = 0; i < 6; i++) {
        frame[i] = dst[i];
        frame[6 + i] = ifp->mac[i];
    }
    Wr16(frame + 12, type);
    if (payload)
        NetMemcpy(frame + 14, PktCdata(pkt), payload);
    while (n < RoboNetEthMin)
        frame[n++] = 0;
    return NetifSend(ifp, frame, n);
}
#endif
