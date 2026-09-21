#include "net/tcp/tcp.h"
#ifndef NET_HOST_TEST
#include "net/socket/socket.h"
#include "net/ipv4/ipv4.h"
#include "net/core/checksum.h"
#include "net/core/net_util.h"
#include "net/core/network.h"
#include "net/core/packet.h"
#include "syscall/syscall_errors.h"
#include "thread/thread.h"
#include "time/time.h"
#include "mm/heap/heap.h"
#include "kio.h"
#endif

int TcpSmApply(uint8_t *state, int event)
{
    uint8_t s;
    if (!state)
        return -1;
    s = *state;
    if (event == RoboTcpEvRst) {
        *state = RoboTcpClosed;
        return 0;
    }
    switch (s) {
    case RoboTcpClosed:
        if (event == RoboTcpEvPassiveOpen) {
            *state = RoboTcpListen;
            return 0;
        }
        if (event == RoboTcpEvActiveOpen) {
            *state = RoboTcpSynSent;
            return 0;
        }
        break;
    case RoboTcpListen:
        if (event == RoboTcpEvSyn) {
            *state = RoboTcpSynReceived;
            return 0;
        }
        if (event == RoboTcpEvClose) {
            *state = RoboTcpClosed;
            return 0;
        }
        break;
    case RoboTcpSynSent:
        if (event == RoboTcpEvSynack) {
            *state = RoboTcpEstablished;
            return 0;
        }
        if (event == RoboTcpEvSyn) {
            *state = RoboTcpSynReceived;
            return 0;
        }
        if (event == RoboTcpEvClose || event == RoboTcpEvTimeout) {
            *state = RoboTcpClosed;
            return 0;
        }
        break;
    case RoboTcpSynReceived:
        if (event == RoboTcpEvAck) {
            *state = RoboTcpEstablished;
            return 0;
        }
        if (event == RoboTcpEvClose) {
            *state = RoboTcpFinWait1;
            return 0;
        }
        if (event == RoboTcpEvTimeout) {
            *state = RoboTcpClosed;
            return 0;
        }
        break;
    case RoboTcpEstablished:
        if (event == RoboTcpEvClose) {
            *state = RoboTcpFinWait1;
            return 0;
        }
        if (event == RoboTcpEvFin) {
            *state = RoboTcpCloseWait;
            return 0;
        }
        break;
    case RoboTcpFinWait1:
        if (event == RoboTcpEvAck) {
            *state = RoboTcpFinWait2;
            return 0;
        }
        if (event == RoboTcpEvFin) {
            *state = RoboTcpClosing;
            return 0;
        }
        break;
    case RoboTcpFinWait2:
        if (event == RoboTcpEvFin) {
            *state = RoboTcpTimeWait;
            return 0;
        }
        break;
    case RoboTcpCloseWait:
        if (event == RoboTcpEvClose) {
            *state = RoboTcpLastAck;
            return 0;
        }
        break;
    case RoboTcpClosing:
        if (event == RoboTcpEvAck) {
            *state = RoboTcpTimeWait;
            return 0;
        }
        break;
    case RoboTcpLastAck:
        if (event == RoboTcpEvAck) {
            *state = RoboTcpClosed;
            return 0;
        }
        break;
    case RoboTcpTimeWait:
        if (event == RoboTcpEvTimeout) {
            *state = RoboTcpClosed;
            return 0;
        }
        break;
    default:
        break;
    }
    return -1;
}

const char *TcpStateName(uint8_t st)
{
    switch (st) {
    case RoboTcpClosed: return "CLOSED";
    case RoboTcpListen: return "LISTEN";
    case RoboTcpSynSent: return "SYN-SENT";
    case RoboTcpSynReceived: return "SYN-RECEIVED";
    case RoboTcpEstablished: return "ESTABLISHED";
    case RoboTcpFinWait1: return "FIN-WAIT-1";
    case RoboTcpFinWait2: return "FIN-WAIT-2";
    case RoboTcpCloseWait: return "CLOSE-WAIT";
    case RoboTcpClosing: return "CLOSING";
    case RoboTcpLastAck: return "LAST-ACK";
    case RoboTcpTimeWait: return "TIME-WAIT";
    default: return "?";
    }
}

#ifndef NET_HOST_TEST
#define TCP_MSS 1360u
#define TCP_RTO_NS 1000000000ull
#define TCP_SYN_RTO_NS 1000000000ull
#define TCP_TW_NS 2000000000ull
#define TCP_MAX_RETRY 5u

static int SeqLeq(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) <= 0;
}

static int SeqLt(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

static unsigned TcpUsable(const RoboSocket *s)
{
    uint32_t inflight = s->sndNxt - s->sndUna;
    uint32_t wnd = s->sndWnd;
    if (inflight >= wnd)
        return 0;
    return (unsigned)(wnd - inflight);
}

static int TcpXmit(RoboSocket *s, uint8_t flags, uint32_t seq,
                    const uint8_t *payload, unsigned plen)
{
    RoboPacket *pkt;
    uint8_t hdr[20];
    uint16_t tlen, csum;
    RoboNetIf *ifp;
    uint32_t src;

    ifp = NetifDefault();
    if (!ifp || !s)
        return -1;
    src = s->lip ? s->lip : ifp->ipv4;
    tlen = (uint16_t)(20u + plen);
    NetMemset(hdr, 0, 20);
    Wr16(hdr, s->lport);
    Wr16(hdr + 2, s->rport);
    Wr32(hdr + 4, seq);
    Wr32(hdr + 8, s->rcvNxt);
    hdr[12] = (5u << 4);
    hdr[13] = flags;
    s->rcvWnd = s->rcvCap - s->rcvLen;
    Wr16(hdr + 14, (uint16_t)s->rcvWnd);
    pkt = PktAlloc();
    if (!pkt)
        return -1;
    if (PktCopyin(pkt, hdr, 20) != 0) {
        PktFree(pkt);
        return -1;
    }
    if (plen && PktAppend(pkt, payload, plen) != 0) {
        PktFree(pkt);
        return -1;
    }
    csum = ChecksumPseudo(src, s->rip, RoboIpprotoTcp,
                                PktCdata(pkt), pkt->length);
    Wr16(PktData(pkt) + 16, csum);
    if (Ipv4Send(ifp, s->rip, RoboIpprotoTcp, pkt, 1) != 0) {
        PktFree(pkt);
        return -1;
    }
    PktFree(pkt);
    (void)tlen;
    if ((flags & (RoboTcpfSyn | RoboTcpfFin | RoboTcpfRst)) || plen) {
        s->rtoDeadline = TimeNow() + TCP_RTO_NS;
    }
    return 0;
}

static void TcpOutput(RoboSocket *s)
{
    unsigned off, n, cap;
    if (!s || s->tcpState != RoboTcpEstablished)
        return;
    off = (unsigned)(s->sndNxt - s->sndUna);
    if (off > s->sndLen)
        off = s->sndLen;
    cap = TcpUsable(s);
    if (!cap)
        return;
    n = s->sndLen - off;
    if (n > cap)
        n = cap;
    if (n > TCP_MSS)
        n = TCP_MSS;
    if (!n)
        return;
    if (TcpXmit(s, (uint8_t)(RoboTcpfAck | RoboTcpfPsh),
                 s->sndNxt, s->sndBuf + off, n) == 0)
        s->sndNxt += n;
}

static void TcpAckData(RoboSocket *s, uint32_t ack)
{
    uint32_t adv;
    if (SeqLt(ack, s->sndUna) || SeqLt(s->sndNxt + 1u, ack))
        return;
    adv = ack - s->sndUna;
    if (adv && adv <= s->sndLen) {
        NetMemcpy(s->sndBuf, s->sndBuf + adv, s->sndLen - adv);
        s->sndLen -= adv;
        s->sndUna = ack;
        NetSignal(&s->txWaiter, &s->txPosted);
    } else if (ack == s->sndUna) {
        /* dup ack */
    } else if (SeqLeq(s->sndUna, ack) && SeqLeq(ack, s->sndNxt)) {
        s->sndUna = ack;
    }
}

static RoboSocket *TcpNewChild(RoboSocket *parent, uint32_t src, uint16_t sport,
                                 uint32_t irs)
{
    RoboSocket *c = SockAlloc(1, (int)RoboIpprotoTcp, parent->owner);
    if (!c)
        return 0;
    c->lip = parent->lip ? parent->lip :
             (NetifDefault() ? NetifDefault()->ipv4 : 0);
    c->lport = parent->lport;
    c->rip = src;
    c->rport = sport;
    c->tcpState = RoboTcpSynReceived;
    c->sstate = RoboSockConnecting;
    c->irs = irs;
    c->rcvNxt = irs + 1u;
    c->iss = (uint32_t)TimeNow() ^ 0xC0FFEEu;
    c->sndUna = c->iss;
    c->sndNxt = c->iss + 1u;
    c->sndWnd = 4096;
    c->rcvWnd = c->rcvCap;
    c->listenParent = parent;
    c->retries = 0;
    return c;
}

static void TcpQueueAccept(RoboSocket *parent, RoboSocket *c)
{
    if (!parent || !c)
        return;
    if (parent->acceptN >= RoboNetAcceptQ) {
        TcpXmit(c, RoboTcpfRst | RoboTcpfAck, c->sndNxt, 0, 0);
        SockFree(c);
        return;
    }
    parent->acceptQ[parent->acceptN++] = c;
    c->tcpState = RoboTcpEstablished;
    c->sstate = RoboSockConnected;
    NetSignal(&parent->rxWaiter, &parent->rxPosted);
}

int TcpRx(RoboPacket *pkt, uint32_t src, uint32_t dst)
{
    const uint8_t *p;
    unsigned doff, hlen, plen;
    uint16_t sport, dport, win;
    uint32_t seq, ack;
    uint8_t flags;
    RoboSocket *s;
    uint16_t tlen;

    if (!pkt || pkt->length < 20u) {
        PktFree(pkt);
        return -1;
    }
    p = PktCdata(pkt);
    tlen = (uint16_t)pkt->length;
    if (ChecksumPseudo(src, dst, RoboIpprotoTcp, p, tlen) != 0) {
        PktFree(pkt);
        return -1;
    }
    doff = p[12] >> 4;
    if (doff < 5u) {
        PktFree(pkt);
        return -1;
    }
    hlen = doff * 4u;
    if (hlen > pkt->length) {
        PktFree(pkt);
        return -1;
    }
    sport = Rd16(p);
    dport = Rd16(p + 2);
    seq = Rd32(p + 4);
    ack = Rd32(p + 8);
    flags = p[13];
    win = Rd16(p + 14);
    plen = pkt->length - hlen;
    if (flags & RoboTcpfRst) {
        s = SockFindTcp(dst, dport, src, sport);
        if (s) {
            s->err = RoboEio;
            s->tcpState = RoboTcpClosed;
            s->sstate = RoboSockClosed;
            NetSignal(&s->rxWaiter, &s->rxPosted);
            NetSignal(&s->connWaiter, &s->connPosted);
        }
        PktFree(pkt);
        return 0;
    }
    s = SockFindTcp(dst, dport, src, sport);
    if (!s && (flags & RoboTcpfSyn) && !(flags & RoboTcpfAck)) {
        RoboSocket *ls = SockFindListen(dst, dport);
        if (!ls)
            ls = SockFindListen(0, dport);
        if (ls && ls->acceptN < (unsigned)ls->backlog) {
            s = TcpNewChild(ls, src, sport, seq);
            if (s) {
                TcpXmit(s, (uint8_t)(RoboTcpfSyn | RoboTcpfAck),
                         s->iss, 0, 0);
            }
        }
        PktFree(pkt);
        return 0;
    }
    if (!s) {
        PktFree(pkt);
        return 0;
    }
    s->sndWnd = win ? win : s->sndWnd;
    if (s->tcpState == RoboTcpSynSent) {
        if ((flags & (RoboTcpfSyn | RoboTcpfAck)) ==
            (RoboTcpfSyn | RoboTcpfAck) && ack == s->iss + 1u) {
            s->irs = seq;
            s->rcvNxt = seq + 1u;
            s->sndUna = ack;
            s->sndNxt = ack;
            TcpSmApply(&s->tcpState, RoboTcpEvSynack);
            s->sstate = RoboSockConnected;
            TcpXmit(s, RoboTcpfAck, s->sndNxt, 0, 0);
            NetSignal(&s->connWaiter, &s->connPosted);
        }
        PktFree(pkt);
        return 0;
    }
    if (s->tcpState == RoboTcpSynReceived) {
        if ((flags & RoboTcpfAck) && ack == s->iss + 1u) {
            s->sndUna = ack;
            s->sndNxt = ack;
            if (s->listenParent)
                TcpQueueAccept(s->listenParent, s);
            else {
                TcpSmApply(&s->tcpState, RoboTcpEvAck);
                s->sstate = RoboSockConnected;
                NetSignal(&s->connWaiter, &s->connPosted);
            }
        }
        PktFree(pkt);
        return 0;
    }
    if (flags & RoboTcpfAck)
        TcpAckData(s, ack);
    if (plen && seq == s->rcvNxt &&
        (s->tcpState == RoboTcpEstablished ||
         s->tcpState == RoboTcpFinWait1 ||
         s->tcpState == RoboTcpFinWait2)) {
        unsigned room = s->rcvCap - s->rcvLen;
        unsigned n = plen;
        if (n > room)
            n = room;
        if (n) {
            NetMemcpy(s->rcvBuf + s->rcvLen, p + hlen, n);
            s->rcvLen += n;
            s->rcvNxt += n;
            NetSignal(&s->rxWaiter, &s->rxPosted);
        }
        TcpXmit(s, RoboTcpfAck, s->sndNxt, 0, 0);
    } else if (plen && seq != s->rcvNxt) {
        TcpXmit(s, RoboTcpfAck, s->sndNxt, 0, 0);
    }
    if ((flags & RoboTcpfFin) && seq + plen == s->rcvNxt) {
        s->rcvNxt += 1u;
        s->finRcvd = 1;
        if (s->tcpState == RoboTcpEstablished)
            TcpSmApply(&s->tcpState, RoboTcpEvFin);
        else if (s->tcpState == RoboTcpFinWait1)
            TcpSmApply(&s->tcpState, RoboTcpEvFin);
        else if (s->tcpState == RoboTcpFinWait2) {
            TcpSmApply(&s->tcpState, RoboTcpEvFin);
            s->twDeadline = TimeNow() + TCP_TW_NS;
        }
        TcpXmit(s, RoboTcpfAck, s->sndNxt, 0, 0);
        NetSignal(&s->rxWaiter, &s->rxPosted);
    }
    if ((flags & RoboTcpfAck) && s->finSent) {
        if (s->tcpState == RoboTcpFinWait1 && ack == s->sndUna)
            TcpSmApply(&s->tcpState, RoboTcpEvAck);
        if (s->tcpState == RoboTcpClosing && ack == s->sndUna) {
            TcpSmApply(&s->tcpState, RoboTcpEvAck);
            s->twDeadline = TimeNow() + TCP_TW_NS;
        }
        if (s->tcpState == RoboTcpLastAck && ack == s->sndUna) {
            TcpSmApply(&s->tcpState, RoboTcpEvAck);
            s->sstate = RoboSockClosed;
        }
    }
    TcpOutput(s);
    PktFree(pkt);
    return 0;
}

int TcpConnect(RoboSocket *s, uint32_t dst, uint16_t port)
{
    unsigned i;
    if (!s)
        return -RoboEinval;
    if (!s->lport) {
        if (SockBind(s, s->lip, 0) != 0)
            return -RoboEagain;
    }
    s->rip = dst;
    s->rport = port;
    s->iss = (uint32_t)TimeNow() ^ 0x51EDC0DEu;
    s->sndUna = s->iss;
    s->sndNxt = s->iss + 1u;
    s->sndWnd = 4096;
    s->rcvWnd = s->rcvCap;
    TcpSmApply(&s->tcpState, RoboTcpEvActiveOpen);
    s->sstate = RoboSockConnecting;
    s->retries = 0;
    if (TcpXmit(s, RoboTcpfSyn, s->iss, 0, 0) != 0)
        return -RoboEio;
    for (i = 0; i < 80u; i++) {
        if (s->tcpState == RoboTcpEstablished)
            return 0;
        if (s->err)
            return -s->err;
        if (s->tcpState == RoboTcpClosed)
            return -RoboEio;
        NetWait(&s->connWaiter, &s->connPosted);
        RoboSleep(50);
    }
    return -RoboEio;
}

int TcpListen(RoboSocket *s, int backlog)
{
    if (!s || !s->lport)
        return -RoboEinval;
    if (backlog < 1)
        backlog = 1;
    if (backlog > (int)RoboNetAcceptQ)
        backlog = (int)RoboNetAcceptQ;
    s->backlog = backlog;
    TcpSmApply(&s->tcpState, RoboTcpEvPassiveOpen);
    s->sstate = RoboSockListening;
    return 0;
}

int TcpAccept(RoboSocket *s, RoboSocket **out)
{
    unsigned i;
    if (!s || !out)
        return -RoboEinval;
    for (;;) {
        if (s->acceptN) {
            *out = s->acceptQ[0];
            for (i = 1; i < s->acceptN; i++)
                s->acceptQ[i - 1] = s->acceptQ[i];
            s->acceptN--;
            (*out)->listenParent = 0;
            return 0;
        }
        if (s->nonblock)
            return -RoboEagain;
        NetWait(&s->rxWaiter, &s->rxPosted);
        RoboSleep(1);
    }
}

int TcpSend(RoboSocket *s, const void *buf, unsigned len)
{
    unsigned n, room;
    if (!s || (!buf && len))
        return -RoboEinval;
    if (s->err)
        return -s->err;
    if (s->shutdownTx)
        return -RoboEio;
    if (s->tcpState != RoboTcpEstablished &&
        s->tcpState != RoboTcpCloseWait)
        return -RoboEio;
    for (;;) {
        room = s->sndCap - s->sndLen;
        if (room) {
            n = len;
            if (n > room)
                n = room;
            if (n)
                NetMemcpy(s->sndBuf + s->sndLen, buf, n);
            s->sndLen += n;
            TcpOutput(s);
            return (int)n;
        }
        if (s->nonblock)
            return -RoboEagain;
        NetWait(&s->txWaiter, &s->txPosted);
        RoboSleep(1);
    }
}

int TcpRecv(RoboSocket *s, void *buf, unsigned len, int nonblock)
{
    unsigned n;
    uint64_t deadline;
    if (!s || !buf)
        return -RoboEinval;
    deadline = TimeNow() + 8000000000ull;
    for (;;) {
        if (s->err)
            return -s->err;
        if (s->rcvLen) {
            n = s->rcvLen;
            if (n > len)
                n = len;
            NetMemcpy(buf, s->rcvBuf, n);
            NetMemcpy(s->rcvBuf, s->rcvBuf + n, s->rcvLen - n);
            s->rcvLen -= n;
            return (int)n;
        }
        if (s->finRcvd || s->shutdownRx ||
            s->tcpState == RoboTcpCloseWait ||
            s->tcpState == RoboTcpLastAck ||
            s->tcpState == RoboTcpClosing ||
            s->tcpState == RoboTcpTimeWait ||
            s->tcpState == RoboTcpClosed)
            return 0;
        if (nonblock)
            return -RoboEagain;
        if (TimeNow() >= deadline)
            return -RoboEtimedout;
        NetWait(&s->rxWaiter, &s->rxPosted);
        RoboSleep(1);
    }
}

int TcpClose(RoboSocket *s)
{
    if (!s)
        return 0;
    if (s->tcpState == RoboTcpEstablished ||
        s->tcpState == RoboTcpSynReceived) {
        TcpSmApply(&s->tcpState, RoboTcpEvClose);
        TcpOutput(s);
        TcpXmit(s, (uint8_t)(RoboTcpfFin | RoboTcpfAck), s->sndNxt, 0, 0);
        s->sndNxt += 1u;
        s->finSent = 1;
        return 0;
    }
    if (s->tcpState == RoboTcpCloseWait) {
        TcpSmApply(&s->tcpState, RoboTcpEvClose);
        TcpXmit(s, (uint8_t)(RoboTcpfFin | RoboTcpfAck), s->sndNxt, 0, 0);
        s->sndNxt += 1u;
        s->finSent = 1;
        return 0;
    }
    s->tcpState = RoboTcpClosed;
    s->sstate = RoboSockClosed;
    return 0;
}

int TcpShutdown(RoboSocket *s, int how)
{
    if (!s)
        return -RoboEinval;
    if (how == 0 || how == 2)
        s->shutdownRx = 1;
    if (how == 1 || how == 2) {
        s->shutdownTx = 1;
        if (s->tcpState == RoboTcpEstablished)
            TcpClose(s);
    }
    return 0;
}

void TcpTimer(void)
{
    unsigned i, n = SockCount();
    uint64_t now = TimeNow();
    for (i = 0; i < n; i++) {
        RoboSocket *s = SockAt(i);
        if (!s || s->kind != 1)
            continue;
        if (s->tcpState == RoboTcpTimeWait && s->twDeadline &&
            now >= s->twDeadline) {
            TcpSmApply(&s->tcpState, RoboTcpEvTimeout);
            s->sstate = RoboSockClosed;
            continue;
        }
        if (!s->rtoDeadline || now < s->rtoDeadline)
            continue;
        if (s->tcpState == RoboTcpSynSent) {
            if (s->retries++ >= TCP_MAX_RETRY) {
                s->err = RoboEio;
                s->tcpState = RoboTcpClosed;
                NetSignal(&s->connWaiter, &s->connPosted);
                continue;
            }
            TcpXmit(s, RoboTcpfSyn, s->iss, 0, 0);
            continue;
        }
        if (s->tcpState == RoboTcpSynReceived) {
            if (s->retries++ >= TCP_MAX_RETRY) {
                s->tcpState = RoboTcpClosed;
                continue;
            }
            TcpXmit(s, (uint8_t)(RoboTcpfSyn | RoboTcpfAck), s->iss, 0, 0);
            continue;
        }
        if (s->sndUna != s->sndNxt && s->sndLen) {
            unsigned nsend = s->sndNxt - s->sndUna;
            if (nsend > s->sndLen)
                nsend = s->sndLen;
            if (nsend > TCP_MSS)
                nsend = TCP_MSS;
            if (s->retries++ >= TCP_MAX_RETRY) {
                s->err = RoboEio;
                s->tcpState = RoboTcpClosed;
                NetSignal(&s->rxWaiter, &s->rxPosted);
                continue;
            }
            TcpXmit(s, (uint8_t)(RoboTcpfAck | RoboTcpfPsh),
                     s->sndUna, s->sndBuf, nsend);
        }
        if (s->finSent && s->tcpState != RoboTcpClosed &&
            s->retries < TCP_MAX_RETRY) {
            TcpXmit(s, (uint8_t)(RoboTcpfFin | RoboTcpfAck),
                     s->sndUna + s->sndLen, 0, 0);
        }
    }
}
#endif
