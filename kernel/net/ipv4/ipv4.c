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

int robo_ipv4_parse(const uint8_t *data, unsigned len, RoboIpv4Info *out)
{
    unsigned ihl, total, hlen;
    uint16_t csum, saved;
    uint8_t tmp[60];
    uint16_t flags_off;

    if (!data || !out || len < 20u)
        return -1;
    if ((data[0] >> 4) != 4u)
        return -1;
    ihl = data[0] & 0x0Fu;
    if (ihl < ROBO_IPV4_MIN_IHL)
        return -1;
    hlen = ihl * 4u;
    if (hlen > 60u || hlen > len)
        return -1;
    total = robo_rd16(data + 2);
    if (total < hlen || total > len)
        return -1;
    flags_off = robo_rd16(data + 6);
    if (flags_off & 0x3FFFu)
        return -1;
    robo_net_memcpy(tmp, data, hlen);
    saved = robo_rd16(tmp + 10);
    tmp[10] = 0;
    tmp[11] = 0;
    csum = robo_checksum(tmp, hlen);
    if (csum != saved)
        return -1;
    out->ihl = (uint8_t)ihl;
    out->tos = data[1];
    out->total = (uint16_t)total;
    out->id = robo_rd16(data + 4);
    out->flags_off = flags_off;
    out->ttl = data[8];
    out->proto = data[9];
    out->csum = saved;
    out->src = robo_rd32(data + 12);
    out->dst = robo_rd32(data + 16);
    (void)csum;
    return 0;
}

#ifndef NET_HOST_TEST
static uint16_t g_ip_id = 1;

int robo_ipv4_local(uint32_t ip)
{
    unsigned i, n;
    if (ip == 0xFFFFFFFFu || (ip & 0xF0000000u) == 0xE0000000u)
        return 1;
    n = robo_netif_count();
    for (i = 0; i < n; i++) {
        RoboNetIf *ifp = robo_netif_at(i);
        if (ifp && ifp->used && ifp->ipv4 == ip)
            return 1;
    }
    return 0;
}

int robo_ipv4_rx(RoboNetIf *ifp, RoboPacket *pkt)
{
    RoboIpv4Info info;
    unsigned hlen;
    const uint8_t *d;

    if (!pkt)
        return -1;
    d = robo_pkt_cdata(pkt);
    if (robo_ipv4_parse(d, pkt->length, &info) != 0) {
        if (ifp)
            ifp->rx_drop++;
        robo_pkt_free(pkt);
        return -1;
    }
    hlen = (unsigned)info.ihl * 4u;
    if (info.ttl == 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    if (!robo_ipv4_local(info.dst)) {
        robo_pkt_free(pkt);
        return 0;
    }
    if (robo_pkt_trim(pkt, pkt->length - info.total) != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    if (robo_pkt_pull(pkt, hlen) != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    pkt->src_ip = info.src;
    pkt->dst_ip = info.dst;
    pkt->proto = info.proto;
    pkt->ifp = ifp;
    if (info.proto == ROBO_IPPROTO_ICMP)
        return robo_icmp_rx(ifp, pkt, info.src, info.dst);
    if (info.proto == ROBO_IPPROTO_UDP)
        return robo_udp_rx(pkt, info.src, info.dst);
    if (info.proto == ROBO_IPPROTO_TCP)
        return robo_tcp_rx(pkt, info.src, info.dst);
    robo_pkt_free(pkt);
    return 0;
}

int robo_ipv4_send(RoboNetIf *ifp, uint32_t dst, uint8_t proto,
                   RoboPacket *pkt, int can_block)
{
    uint8_t hdr[20];
    uint16_t total, csum;
    uint32_t nexthop;
    uint8_t mac[6];
    RoboNetIf *out = ifp;
    unsigned i;

    if (!pkt)
        return -1;
    if (robo_route_lookup(dst, &out, &nexthop) != 0)
        return -1;
    if (!out)
        return -1;
    if (pkt->length > out->mtu - 20u)
        return -1;
    total = (uint16_t)(20u + pkt->length);
    robo_net_memset(hdr, 0, 20);
    hdr[0] = 0x45;
    robo_wr16(hdr + 2, total);
    robo_wr16(hdr + 4, g_ip_id++);
    hdr[8] = 64;
    hdr[9] = proto;
    robo_wr32(hdr + 12, out->ipv4);
    robo_wr32(hdr + 16, dst);
    csum = robo_checksum(hdr, 20);
    robo_wr16(hdr + 10, csum);
    if (robo_pkt_push(pkt, 20) != 0)
        return -1;
    robo_net_memcpy(robo_pkt_data(pkt), hdr, 20);
    if (robo_arp_resolve(out, nexthop, mac, can_block) != 0) {
        robo_pkt_pull(pkt, 20);
        return -1;
    }
    (void)i;
    return robo_eth_send(out, mac, ROBO_ETH_IP, pkt);
}
#endif
