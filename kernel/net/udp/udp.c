#include "net/udp/udp.h"
#include "net/ipv4/ipv4.h"
#include "net/core/checksum.h"
#include "net/core/net_util.h"
#include "net/core/network.h"
#include "net/socket/socket.h"
#include "syscall/syscall_errors.h"
#include "thread/thread.h"
#include "time/time.h"

int robo_udp_rx(RoboPacket *pkt, uint32_t src, uint32_t dst)
{
    const uint8_t *p;
    uint16_t sport, dport, ulen, csum;
    RoboSocket *s;
    RoboPacket *q;
    unsigned i;

    if (!pkt || pkt->length < 8u) {
        robo_pkt_free(pkt);
        return -1;
    }
    p = robo_pkt_cdata(pkt);
    sport = robo_rd16(p);
    dport = robo_rd16(p + 2);
    ulen = robo_rd16(p + 4);
    csum = robo_rd16(p + 6);
    if (ulen < 8u || ulen > pkt->length) {
        robo_pkt_free(pkt);
        return -1;
    }
    if (csum) {
        if (robo_checksum_pseudo(src, dst, ROBO_IPPROTO_UDP, p, ulen) != 0) {
            robo_pkt_free(pkt);
            return -1;
        }
    }
    if (robo_pkt_trim(pkt, pkt->length - ulen) != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    if (robo_pkt_pull(pkt, 8) != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    pkt->src_ip = src;
    pkt->dst_ip = dst;
    pkt->src_port = sport;
    pkt->dst_port = dport;
    s = robo_sock_find_udp(dst, dport);
    if (!s)
        s = robo_sock_find_udp(0, dport);
    if (!s) {
        robo_pkt_free(pkt);
        return 0;
    }
    if (s->udp_qn >= ROBO_NET_UDP_Q) {
        robo_pkt_free(pkt);
        return 0;
    }
    q = robo_pkt_alloc();
    if (!q) {
        robo_pkt_free(pkt);
        return -1;
    }
    if (robo_pkt_copyin(q, robo_pkt_cdata(pkt), pkt->length) != 0) {
        robo_pkt_free(q);
        robo_pkt_free(pkt);
        return -1;
    }
    q->src_ip = src;
    q->dst_ip = dst;
    q->src_port = sport;
    q->dst_port = dport;
    s->udp_q[s->udp_qn++] = q;
    robo_net_signal(&s->rx_waiter, &s->rx_posted);
    robo_pkt_free(pkt);
    (void)i;
    return 0;
}

int robo_udp_send(RoboSocket *s, uint32_t dst_ip, uint16_t dst_port,
                  const void *buf, unsigned len)
{
    RoboPacket *pkt;
    uint8_t hdr[8];
    uint16_t ulen, csum;
    RoboNetIf *ifp;
    uint32_t src;

    if (!s || (!buf && len))
        return -ROBO_EINVAL;
    if (len > ROBO_NET_MTU - 28u)
        return -ROBO_EOVERFLOW;
    if (!s->lport) {
        if (robo_sock_bind(s, s->lip, 0) != 0)
            return -ROBO_EAGAIN;
    }
    ifp = robo_netif_default();
    if (!ifp)
        return -ROBO_ENODEV;
    src = s->lip ? s->lip : ifp->ipv4;
    ulen = (uint16_t)(8u + len);
    pkt = robo_pkt_alloc();
    if (!pkt)
        return -ROBO_ENOMEM;
    robo_net_memset(hdr, 0, 8);
    robo_wr16(hdr, s->lport);
    robo_wr16(hdr + 2, dst_port);
    robo_wr16(hdr + 4, ulen);
    if (robo_pkt_copyin(pkt, hdr, 8) != 0) {
        robo_pkt_free(pkt);
        return -ROBO_ENOMEM;
    }
    if (len && robo_pkt_append(pkt, buf, len) != 0) {
        robo_pkt_free(pkt);
        return -ROBO_ENOMEM;
    }
    csum = robo_checksum_pseudo(src, dst_ip, ROBO_IPPROTO_UDP,
                                robo_pkt_cdata(pkt), pkt->length);
    if (!csum)
        csum = 0xFFFFu;
    robo_wr16(robo_pkt_data(pkt) + 6, csum);
    if (robo_ipv4_send(ifp, dst_ip, ROBO_IPPROTO_UDP, pkt, 1) != 0) {
        robo_pkt_free(pkt);
        return -ROBO_EIO;
    }
    robo_pkt_free(pkt);
    return (int)len;
}

int robo_udp_recv(RoboSocket *s, void *buf, unsigned len,
                  uint32_t *src_ip, uint16_t *src_port, int nonblock)
{
    RoboPacket *q;
    unsigned n, i;
    uint64_t deadline;
    if (!s || !buf)
        return -ROBO_EINVAL;
    deadline = robo_time_now() + 5000000000ull;
    for (;;) {
        if (s->err)
            return -s->err;
        if (s->udp_qn) {
            q = s->udp_q[0];
            for (i = 1; i < s->udp_qn; i++)
                s->udp_q[i - 1] = s->udp_q[i];
            s->udp_qn--;
            n = q->length;
            if (n > len)
                n = len;
            if (n)
                robo_net_memcpy(buf, robo_pkt_cdata(q), n);
            if (src_ip)
                *src_ip = q->src_ip;
            if (src_port)
                *src_port = q->src_port;
            robo_pkt_free(q);
            return (int)n;
        }
        if (nonblock)
            return -ROBO_EAGAIN;
        if (robo_time_now() >= deadline)
            return -ROBO_EIO;
        robo_net_wait(&s->rx_waiter, &s->rx_posted);
        robo_sleep(1);
    }
}
