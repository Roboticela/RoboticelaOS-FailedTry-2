#include "net/ipv4/ipv4.h"
#include "net/core/checksum.h"
#include "net/core/net_util.h"
#ifndef NET_HOST_TEST
#include "net/ipv4/route.h"
#include "net/ipv4/icmp.h"
#include "net/ethernet/ethernet.h"
#include "net/arp/arp.h"
#include "net/udp/udp.h"
#include "net/tcp/tcp.h"
#include "net/core/network.h"
#include "kio.h"
#endif

int Ipv4Parse(const uint8_t *data, unsigned len, RoboIpv4Info *out)
{
    unsigned ihl, total, hlen;
    uint16_t csum, saved;
    uint8_t tmp[60];
    uint16_t flagsOff;

    if (!data || !out || len < 20u)
        return -1;
    if ((data[0] >> 4) != 4u)
        return -1;
    ihl = data[0] & 0x0Fu;
    if (ihl < RoboIpv4MinIhl)
        return -1;
    hlen = ihl * 4u;
    if (hlen > 60u || hlen > len)
        return -1;
    total = Rd16(data + 2);
    if (total < hlen || total > len)
        return -1;
    flagsOff = Rd16(data + 6);
    if (flagsOff & 0x3FFFu)
        return -1;
    NetMemcpy(tmp, data, hlen);
    saved = Rd16(tmp + 10);
    tmp[10] = 0;
    tmp[11] = 0;
    csum = Checksum(tmp, hlen);
    if (csum != saved)
        return -1;
    out->ihl = (uint8_t)ihl;
    out->tos = data[1];
    out->total = (uint16_t)total;
    out->id = Rd16(data + 4);
    out->flagsOff = flagsOff;
    out->ttl = data[8];
    out->proto = data[9];
    out->csum = saved;
    out->src = Rd32(data + 12);
    out->dst = Rd32(data + 16);
    (void)csum;
    return 0;
}

#ifndef NET_HOST_TEST
static uint16_t gIpId = 1;

int Ipv4Local(uint32_t ip)
{
    unsigned i, n;
    if (ip == 0xFFFFFFFFu || (ip & 0xF0000000u) == 0xE0000000u)
        return 1;
    n = NetifCount();
    for (i = 0; i < n; i++) {
        RoboNetIf *ifp = NetifAt(i);
        if (ifp && ifp->used && ifp->ipv4 == ip)
            return 1;
    }
    return 0;
}

int Ipv4Rx(RoboNetIf *ifp, RoboPacket *pkt)
{
    RoboIpv4Info info;
    unsigned hlen;
    const uint8_t *d;

    if (!pkt)
        return -1;
    d = PktCdata(pkt);
    if (Ipv4Parse(d, pkt->length, &info) != 0) {
        if (ifp)
            ifp->rxDrop++;
        PktFree(pkt);
        return -1;
    }
    hlen = (unsigned)info.ihl * 4u;
    if (info.ttl == 0) {
        PktFree(pkt);
        return -1;
    }
    if (!Ipv4Local(info.dst)) {
        PktFree(pkt);
        return 0;
    }
    if (PktTrim(pkt, pkt->length - info.total) != 0) {
        PktFree(pkt);
        return -1;
    }
    if (PktPull(pkt, hlen) != 0) {
        PktFree(pkt);
        return -1;
    }
    pkt->srcIp = info.src;
    pkt->dstIp = info.dst;
    pkt->proto = info.proto;
    pkt->ifp = ifp;
    if (info.proto == RoboIpprotoIcmp)
        return IcmpRx(ifp, pkt, info.src, info.dst);
    if (info.proto == RoboIpprotoUdp)
        return UdpRx(pkt, info.src, info.dst);
    if (info.proto == RoboIpprotoTcp)
        return TcpRx(pkt, info.src, info.dst);
    PktFree(pkt);
    return 0;
}

int Ipv4Send(RoboNetIf *ifp, uint32_t dst, uint8_t proto,
                   RoboPacket *pkt, int canBlock)
{
    uint8_t hdr[20];
    uint16_t total, csum;
    uint32_t nexthop;
    uint8_t mac[6];
    RoboNetIf *out = ifp;
    unsigned i;

    if (!pkt)
        return -1;
    if (RouteLookup(dst, &out, &nexthop) != 0)
        return -1;
    if (!out)
        return -1;
    if (pkt->length > out->mtu - 20u)
        return -1;
    total = (uint16_t)(20u + pkt->length);
    NetMemset(hdr, 0, 20);
    hdr[0] = 0x45;
    Wr16(hdr + 2, total);
    Wr16(hdr + 4, gIpId++);
    hdr[8] = 64;
    hdr[9] = proto;
    Wr32(hdr + 12, out->ipv4);
    Wr32(hdr + 16, dst);
    csum = Checksum(hdr, 20);
    Wr16(hdr + 10, csum);
    if (PktPush(pkt, 20) != 0)
        return -1;
    NetMemcpy(PktData(pkt), hdr, 20);
    if (ArpResolve(out, nexthop, mac, canBlock) != 0) {
        PktPull(pkt, 20);
        return -1;
    }
    (void)i;
    return EthSend(out, mac, RoboEthIp, pkt);
}
#endif
