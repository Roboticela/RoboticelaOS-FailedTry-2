#include "net/udp/udp.h"
#include "net/ipv4/ipv4.h"
#include "net/core/checksum.h"
#include "net/core/net_util.h"
#include "net/core/network.h"
#include "net/socket/socket.h"
#include "syscall/syscall_errors.h"
#include "thread/thread.h"
#include "time/time.h"

int UdpRx(RoboPacket *pkt, uint32_t src, uint32_t dst)
{
    const uint8_t *p;
    uint16_t sport, dport, ulen, csum;
    RoboSocket *s;
    RoboPacket *q;
    unsigned i;

    if (!pkt || pkt->length < 8u) {
        PktFree(pkt);
        return -1;
    }
    p = PktCdata(pkt);
    sport = Rd16(p);
    dport = Rd16(p + 2);
    ulen = Rd16(p + 4);
    csum = Rd16(p + 6);
    if (ulen < 8u || ulen > pkt->length) {
        PktFree(pkt);
        return -1;
    }
    if (csum) {
        if (ChecksumPseudo(src, dst, RoboIpprotoUdp, p, ulen) != 0) {
            PktFree(pkt);
            return -1;
        }
    }
    if (PktTrim(pkt, pkt->length - ulen) != 0) {
        PktFree(pkt);
        return -1;
    }
    if (PktPull(pkt, 8) != 0) {
        PktFree(pkt);
        return -1;
    }
    pkt->srcIp = src;
    pkt->dstIp = dst;
    pkt->srcPort = sport;
    pkt->dstPort = dport;
    s = SockFindUdp(dst, dport);
    if (!s)
        s = SockFindUdp(0, dport);
    if (!s) {
        PktFree(pkt);
        return 0;
    }
    if (s->udpQn >= RoboNetUdpQ) {
        PktFree(pkt);
        return 0;
    }
    q = PktAlloc();
    if (!q) {
        PktFree(pkt);
        return -1;
    }
    if (PktCopyin(q, PktCdata(pkt), pkt->length) != 0) {
        PktFree(q);
        PktFree(pkt);
        return -1;
    }
    q->srcIp = src;
    q->dstIp = dst;
    q->srcPort = sport;
    q->dstPort = dport;
    s->udpQ[s->udpQn++] = q;
    NetSignal(&s->rxWaiter, &s->rxPosted);
    PktFree(pkt);
    (void)i;
    return 0;
}

int UdpSend(RoboSocket *s, uint32_t dstIp, uint16_t dstPort,
                  const void *buf, unsigned len)
{
    RoboPacket *pkt;
    uint8_t hdr[8];
    uint16_t ulen, csum;
    RoboNetIf *ifp;
    uint32_t src;

    if (!s || (!buf && len))
        return -RoboEinval;
    if (len > RoboNetMtu - 28u)
        return -RoboEoverflow;
    if (!s->lport) {
        if (SockBind(s, s->lip, 0) != 0)
            return -RoboEagain;
    }
    ifp = NetifDefault();
    if (!ifp)
        return -RoboEnodev;
    src = s->lip ? s->lip : ifp->ipv4;
    ulen = (uint16_t)(8u + len);
    pkt = PktAlloc();
    if (!pkt)
        return -RoboEnomem;
    NetMemset(hdr, 0, 8);
    Wr16(hdr, s->lport);
    Wr16(hdr + 2, dstPort);
    Wr16(hdr + 4, ulen);
    if (PktCopyin(pkt, hdr, 8) != 0) {
        PktFree(pkt);
        return -RoboEnomem;
    }
    if (len && PktAppend(pkt, buf, len) != 0) {
        PktFree(pkt);
        return -RoboEnomem;
    }
    csum = ChecksumPseudo(src, dstIp, RoboIpprotoUdp,
                                PktCdata(pkt), pkt->length);
    if (!csum)
        csum = 0xFFFFu;
    Wr16(PktData(pkt) + 6, csum);
    if (Ipv4Send(ifp, dstIp, RoboIpprotoUdp, pkt, 1) != 0) {
        PktFree(pkt);
        return -RoboEio;
    }
    PktFree(pkt);
    return (int)len;
}

int UdpRecv(RoboSocket *s, void *buf, unsigned len,
                  uint32_t *srcIp, uint16_t *srcPort, int nonblock)
{
    RoboPacket *q;
    unsigned n, i;
    uint64_t deadline;
    if (!s || !buf)
        return -RoboEinval;
    deadline = TimeNow() + 5000000000ull;
    for (;;) {
        if (s->err)
            return -s->err;
        if (s->udpQn) {
            q = s->udpQ[0];
            for (i = 1; i < s->udpQn; i++)
                s->udpQ[i - 1] = s->udpQ[i];
            s->udpQn--;
            n = q->length;
            if (n > len)
                n = len;
            if (n)
                NetMemcpy(buf, PktCdata(q), n);
            if (srcIp)
                *srcIp = q->srcIp;
            if (srcPort)
                *srcPort = q->srcPort;
            PktFree(q);
            return (int)n;
        }
        if (nonblock)
            return -RoboEagain;
        if (TimeNow() >= deadline)
            return -RoboEio;
        NetWait(&s->rxWaiter, &s->rxPosted);
        RoboSleep(1);
    }
}
