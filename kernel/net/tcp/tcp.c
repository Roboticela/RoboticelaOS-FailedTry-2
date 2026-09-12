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

int robo_tcp_sm_apply(uint8_t *state, int event)
{
    uint8_t s;
    if (!state)
        return -1;
    s = *state;
    if (event == ROBO_TCP_EV_RST) {
        *state = ROBO_TCP_CLOSED;
        return 0;
    }
    switch (s) {
    case ROBO_TCP_CLOSED:
        if (event == ROBO_TCP_EV_PASSIVE_OPEN) {
            *state = ROBO_TCP_LISTEN;
            return 0;
        }
        if (event == ROBO_TCP_EV_ACTIVE_OPEN) {
            *state = ROBO_TCP_SYN_SENT;
            return 0;
        }
        break;
    case ROBO_TCP_LISTEN:
        if (event == ROBO_TCP_EV_SYN) {
            *state = ROBO_TCP_SYN_RECEIVED;
            return 0;
        }
        if (event == ROBO_TCP_EV_CLOSE) {
            *state = ROBO_TCP_CLOSED;
            return 0;
        }
        break;
    case ROBO_TCP_SYN_SENT:
        if (event == ROBO_TCP_EV_SYNACK) {
            *state = ROBO_TCP_ESTABLISHED;
            return 0;
        }
        if (event == ROBO_TCP_EV_SYN) {
            *state = ROBO_TCP_SYN_RECEIVED;
            return 0;
        }
        if (event == ROBO_TCP_EV_CLOSE || event == ROBO_TCP_EV_TIMEOUT) {
            *state = ROBO_TCP_CLOSED;
            return 0;
        }
        break;
    case ROBO_TCP_SYN_RECEIVED:
        if (event == ROBO_TCP_EV_ACK) {
            *state = ROBO_TCP_ESTABLISHED;
            return 0;
        }
        if (event == ROBO_TCP_EV_CLOSE) {
            *state = ROBO_TCP_FIN_WAIT_1;
            return 0;
        }
        if (event == ROBO_TCP_EV_TIMEOUT) {
            *state = ROBO_TCP_CLOSED;
            return 0;
        }
        break;
    case ROBO_TCP_ESTABLISHED:
        if (event == ROBO_TCP_EV_CLOSE) {
            *state = ROBO_TCP_FIN_WAIT_1;
            return 0;
        }
        if (event == ROBO_TCP_EV_FIN) {
            *state = ROBO_TCP_CLOSE_WAIT;
            return 0;
        }
        break;
    case ROBO_TCP_FIN_WAIT_1:
        if (event == ROBO_TCP_EV_ACK) {
            *state = ROBO_TCP_FIN_WAIT_2;
            return 0;
        }
        if (event == ROBO_TCP_EV_FIN) {
            *state = ROBO_TCP_CLOSING;
            return 0;
        }
        break;
    case ROBO_TCP_FIN_WAIT_2:
        if (event == ROBO_TCP_EV_FIN) {
            *state = ROBO_TCP_TIME_WAIT;
            return 0;
        }
        break;
    case ROBO_TCP_CLOSE_WAIT:
        if (event == ROBO_TCP_EV_CLOSE) {
            *state = ROBO_TCP_LAST_ACK;
            return 0;
        }
        break;
    case ROBO_TCP_CLOSING:
        if (event == ROBO_TCP_EV_ACK) {
            *state = ROBO_TCP_TIME_WAIT;
            return 0;
        }
        break;
    case ROBO_TCP_LAST_ACK:
        if (event == ROBO_TCP_EV_ACK) {
            *state = ROBO_TCP_CLOSED;
            return 0;
        }
        break;
    case ROBO_TCP_TIME_WAIT:
        if (event == ROBO_TCP_EV_TIMEOUT) {
            *state = ROBO_TCP_CLOSED;
            return 0;
        }
        break;
    default:
        break;
    }
    return -1;
}

const char *robo_tcp_state_name(uint8_t st)
{
    switch (st) {
    case ROBO_TCP_CLOSED: return "CLOSED";
    case ROBO_TCP_LISTEN: return "LISTEN";
    case ROBO_TCP_SYN_SENT: return "SYN-SENT";
    case ROBO_TCP_SYN_RECEIVED: return "SYN-RECEIVED";
    case ROBO_TCP_ESTABLISHED: return "ESTABLISHED";
    case ROBO_TCP_FIN_WAIT_1: return "FIN-WAIT-1";
    case ROBO_TCP_FIN_WAIT_2: return "FIN-WAIT-2";
    case ROBO_TCP_CLOSE_WAIT: return "CLOSE-WAIT";
    case ROBO_TCP_CLOSING: return "CLOSING";
    case ROBO_TCP_LAST_ACK: return "LAST-ACK";
    case ROBO_TCP_TIME_WAIT: return "TIME-WAIT";
    default: return "?";
    }
}

#ifndef NET_HOST_TEST
#define TCP_MSS 1360u
#define TCP_RTO_NS 1000000000ull
#define TCP_SYN_RTO_NS 1000000000ull
#define TCP_TW_NS 2000000000ull
#define TCP_MAX_RETRY 5u

static int seq_leq(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) <= 0;
}

static int seq_lt(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

static unsigned tcp_usable(const RoboSocket *s)
{
    uint32_t inflight = s->snd_nxt - s->snd_una;
    uint32_t wnd = s->snd_wnd;
    if (inflight >= wnd)
        return 0;
    return (unsigned)(wnd - inflight);
}

static int tcp_xmit(RoboSocket *s, uint8_t flags, uint32_t seq,
                    const uint8_t *payload, unsigned plen)
{
    RoboPacket *pkt;
    uint8_t hdr[20];
    uint16_t tlen, csum;
    RoboNetIf *ifp;
    uint32_t src;

    ifp = robo_netif_default();
    if (!ifp || !s)
        return -1;
    src = s->lip ? s->lip : ifp->ipv4;
    tlen = (uint16_t)(20u + plen);
    robo_net_memset(hdr, 0, 20);
    robo_wr16(hdr, s->lport);
    robo_wr16(hdr + 2, s->rport);
    robo_wr32(hdr + 4, seq);
    robo_wr32(hdr + 8, s->rcv_nxt);
    hdr[12] = (5u << 4);
    hdr[13] = flags;
    s->rcv_wnd = s->rcv_cap - s->rcv_len;
    robo_wr16(hdr + 14, (uint16_t)s->rcv_wnd);
    pkt = robo_pkt_alloc();
    if (!pkt)
        return -1;
    if (robo_pkt_copyin(pkt, hdr, 20) != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    if (plen && robo_pkt_append(pkt, payload, plen) != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    csum = robo_checksum_pseudo(src, s->rip, ROBO_IPPROTO_TCP,
                                robo_pkt_cdata(pkt), pkt->length);
    robo_wr16(robo_pkt_data(pkt) + 16, csum);
    if (robo_ipv4_send(ifp, s->rip, ROBO_IPPROTO_TCP, pkt, 1) != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    robo_pkt_free(pkt);
    (void)tlen;
    if ((flags & (ROBO_TCPF_SYN | ROBO_TCPF_FIN | ROBO_TCPF_RST)) || plen) {
        s->rto_deadline = robo_time_now() + TCP_RTO_NS;
    }
    return 0;
}

static void tcp_output(RoboSocket *s)
{
    unsigned off, n, cap;
    if (!s || s->tcp_state != ROBO_TCP_ESTABLISHED)
        return;
    off = (unsigned)(s->snd_nxt - s->snd_una);
    if (off > s->snd_len)
        off = s->snd_len;
    cap = tcp_usable(s);
    if (!cap)
        return;
    n = s->snd_len - off;
    if (n > cap)
        n = cap;
    if (n > TCP_MSS)
        n = TCP_MSS;
    if (!n)
        return;
    if (tcp_xmit(s, (uint8_t)(ROBO_TCPF_ACK | ROBO_TCPF_PSH),
                 s->snd_nxt, s->snd_buf + off, n) == 0)
        s->snd_nxt += n;
}

static void tcp_ack_data(RoboSocket *s, uint32_t ack)
{
    uint32_t adv;
    if (seq_lt(ack, s->snd_una) || seq_lt(s->snd_nxt + 1u, ack))
        return;
    adv = ack - s->snd_una;
    if (adv && adv <= s->snd_len) {
        robo_net_memcpy(s->snd_buf, s->snd_buf + adv, s->snd_len - adv);
        s->snd_len -= adv;
        s->snd_una = ack;
        robo_net_signal(&s->tx_waiter, &s->tx_posted);
    } else if (ack == s->snd_una) {
        /* dup ack */
    } else if (seq_leq(s->snd_una, ack) && seq_leq(ack, s->snd_nxt)) {
        s->snd_una = ack;
    }
}

static RoboSocket *tcp_new_child(RoboSocket *parent, uint32_t src, uint16_t sport,
                                 uint32_t irs)
{
    RoboSocket *c = robo_sock_alloc(1, (int)ROBO_IPPROTO_TCP, parent->owner);
    if (!c)
        return 0;
    c->lip = parent->lip ? parent->lip :
             (robo_netif_default() ? robo_netif_default()->ipv4 : 0);
    c->lport = parent->lport;
    c->rip = src;
    c->rport = sport;
    c->tcp_state = ROBO_TCP_SYN_RECEIVED;
    c->sstate = ROBO_SOCK_CONNECTING;
    c->irs = irs;
    c->rcv_nxt = irs + 1u;
    c->iss = (uint32_t)robo_time_now() ^ 0xC0FFEEu;
    c->snd_una = c->iss;
    c->snd_nxt = c->iss + 1u;
    c->snd_wnd = 4096;
    c->rcv_wnd = c->rcv_cap;
    c->listen_parent = parent;
    c->retries = 0;
    return c;
}

static void tcp_queue_accept(RoboSocket *parent, RoboSocket *c)
{
    if (!parent || !c)
        return;
    if (parent->accept_n >= ROBO_NET_ACCEPT_Q) {
        tcp_xmit(c, ROBO_TCPF_RST | ROBO_TCPF_ACK, c->snd_nxt, 0, 0);
        robo_sock_free(c);
        return;
    }
    parent->accept_q[parent->accept_n++] = c;
    c->tcp_state = ROBO_TCP_ESTABLISHED;
    c->sstate = ROBO_SOCK_CONNECTED;
    robo_net_signal(&parent->rx_waiter, &parent->rx_posted);
}

int robo_tcp_rx(RoboPacket *pkt, uint32_t src, uint32_t dst)
{
    const uint8_t *p;
    unsigned doff, hlen, plen;
    uint16_t sport, dport, win;
    uint32_t seq, ack;
    uint8_t flags;
    RoboSocket *s;
    uint16_t tlen;

    if (!pkt || pkt->length < 20u) {
        robo_pkt_free(pkt);
        return -1;
    }
    p = robo_pkt_cdata(pkt);
    tlen = (uint16_t)pkt->length;
    if (robo_checksum_pseudo(src, dst, ROBO_IPPROTO_TCP, p, tlen) != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    doff = p[12] >> 4;
    if (doff < 5u) {
        robo_pkt_free(pkt);
        return -1;
    }
    hlen = doff * 4u;
    if (hlen > pkt->length) {
        robo_pkt_free(pkt);
        return -1;
    }
    sport = robo_rd16(p);
    dport = robo_rd16(p + 2);
    seq = robo_rd32(p + 4);
    ack = robo_rd32(p + 8);
    flags = p[13];
    win = robo_rd16(p + 14);
    plen = pkt->length - hlen;
    if (flags & ROBO_TCPF_RST) {
        s = robo_sock_find_tcp(dst, dport, src, sport);
        if (s) {
            s->err = ROBO_EIO;
            s->tcp_state = ROBO_TCP_CLOSED;
            s->sstate = ROBO_SOCK_CLOSED;
            robo_net_signal(&s->rx_waiter, &s->rx_posted);
            robo_net_signal(&s->conn_waiter, &s->conn_posted);
        }
        robo_pkt_free(pkt);
        return 0;
    }
    s = robo_sock_find_tcp(dst, dport, src, sport);
    if (!s && (flags & ROBO_TCPF_SYN) && !(flags & ROBO_TCPF_ACK)) {
        RoboSocket *ls = robo_sock_find_listen(dst, dport);
        if (!ls)
            ls = robo_sock_find_listen(0, dport);
        if (ls && ls->accept_n < (unsigned)ls->backlog) {
            s = tcp_new_child(ls, src, sport, seq);
            if (s) {
                tcp_xmit(s, (uint8_t)(ROBO_TCPF_SYN | ROBO_TCPF_ACK),
                         s->iss, 0, 0);
            }
        }
        robo_pkt_free(pkt);
        return 0;
    }
    if (!s) {
        robo_pkt_free(pkt);
        return 0;
    }
    s->snd_wnd = win ? win : s->snd_wnd;
    if (s->tcp_state == ROBO_TCP_SYN_SENT) {
        if ((flags & (ROBO_TCPF_SYN | ROBO_TCPF_ACK)) ==
            (ROBO_TCPF_SYN | ROBO_TCPF_ACK) && ack == s->iss + 1u) {
            s->irs = seq;
            s->rcv_nxt = seq + 1u;
            s->snd_una = ack;
            s->snd_nxt = ack;
            robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_SYNACK);
            s->sstate = ROBO_SOCK_CONNECTED;
            tcp_xmit(s, ROBO_TCPF_ACK, s->snd_nxt, 0, 0);
            robo_net_signal(&s->conn_waiter, &s->conn_posted);
        }
        robo_pkt_free(pkt);
        return 0;
    }
    if (s->tcp_state == ROBO_TCP_SYN_RECEIVED) {
        if ((flags & ROBO_TCPF_ACK) && ack == s->iss + 1u) {
            s->snd_una = ack;
            s->snd_nxt = ack;
            if (s->listen_parent)
                tcp_queue_accept(s->listen_parent, s);
            else {
                robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_ACK);
                s->sstate = ROBO_SOCK_CONNECTED;
                robo_net_signal(&s->conn_waiter, &s->conn_posted);
            }
        }
        robo_pkt_free(pkt);
        return 0;
    }
    if (flags & ROBO_TCPF_ACK)
        tcp_ack_data(s, ack);
    if (plen && seq == s->rcv_nxt &&
        (s->tcp_state == ROBO_TCP_ESTABLISHED ||
         s->tcp_state == ROBO_TCP_FIN_WAIT_1 ||
         s->tcp_state == ROBO_TCP_FIN_WAIT_2)) {
        unsigned room = s->rcv_cap - s->rcv_len;
        unsigned n = plen;
        if (n > room)
            n = room;
        if (n) {
            robo_net_memcpy(s->rcv_buf + s->rcv_len, p + hlen, n);
            s->rcv_len += n;
            s->rcv_nxt += n;
            robo_net_signal(&s->rx_waiter, &s->rx_posted);
        }
        tcp_xmit(s, ROBO_TCPF_ACK, s->snd_nxt, 0, 0);
    } else if (plen && seq != s->rcv_nxt) {
        tcp_xmit(s, ROBO_TCPF_ACK, s->snd_nxt, 0, 0);
    }
    if ((flags & ROBO_TCPF_FIN) && seq + plen == s->rcv_nxt) {
        s->rcv_nxt += 1u;
        s->fin_rcvd = 1;
        if (s->tcp_state == ROBO_TCP_ESTABLISHED)
            robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_FIN);
        else if (s->tcp_state == ROBO_TCP_FIN_WAIT_1)
            robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_FIN);
        else if (s->tcp_state == ROBO_TCP_FIN_WAIT_2) {
            robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_FIN);
            s->tw_deadline = robo_time_now() + TCP_TW_NS;
        }
        tcp_xmit(s, ROBO_TCPF_ACK, s->snd_nxt, 0, 0);
        robo_net_signal(&s->rx_waiter, &s->rx_posted);
    }
    if ((flags & ROBO_TCPF_ACK) && s->fin_sent) {
        if (s->tcp_state == ROBO_TCP_FIN_WAIT_1 && ack == s->snd_una)
            robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_ACK);
        if (s->tcp_state == ROBO_TCP_CLOSING && ack == s->snd_una) {
            robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_ACK);
            s->tw_deadline = robo_time_now() + TCP_TW_NS;
        }
        if (s->tcp_state == ROBO_TCP_LAST_ACK && ack == s->snd_una) {
            robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_ACK);
            s->sstate = ROBO_SOCK_CLOSED;
        }
    }
    tcp_output(s);
    robo_pkt_free(pkt);
    return 0;
}

int robo_tcp_connect(RoboSocket *s, uint32_t dst, uint16_t port)
{
    unsigned i;
    if (!s)
        return -ROBO_EINVAL;
    if (!s->lport) {
        if (robo_sock_bind(s, s->lip, 0) != 0)
            return -ROBO_EAGAIN;
    }
    s->rip = dst;
    s->rport = port;
    s->iss = (uint32_t)robo_time_now() ^ 0x51EDC0DEu;
    s->snd_una = s->iss;
    s->snd_nxt = s->iss + 1u;
    s->snd_wnd = 4096;
    s->rcv_wnd = s->rcv_cap;
    robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_ACTIVE_OPEN);
    s->sstate = ROBO_SOCK_CONNECTING;
    s->retries = 0;
    if (tcp_xmit(s, ROBO_TCPF_SYN, s->iss, 0, 0) != 0)
        return -ROBO_EIO;
    for (i = 0; i < 80u; i++) {
        if (s->tcp_state == ROBO_TCP_ESTABLISHED)
            return 0;
        if (s->err)
            return -s->err;
        if (s->tcp_state == ROBO_TCP_CLOSED)
            return -ROBO_EIO;
        robo_net_wait(&s->conn_waiter, &s->conn_posted);
        robo_sleep(50);
    }
    return -ROBO_EIO;
}

int robo_tcp_listen(RoboSocket *s, int backlog)
{
    if (!s || !s->lport)
        return -ROBO_EINVAL;
    if (backlog < 1)
        backlog = 1;
    if (backlog > (int)ROBO_NET_ACCEPT_Q)
        backlog = (int)ROBO_NET_ACCEPT_Q;
    s->backlog = backlog;
    robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_PASSIVE_OPEN);
    s->sstate = ROBO_SOCK_LISTENING;
    return 0;
}

int robo_tcp_accept(RoboSocket *s, RoboSocket **out)
{
    unsigned i;
    if (!s || !out)
        return -ROBO_EINVAL;
    for (;;) {
        if (s->accept_n) {
            *out = s->accept_q[0];
            for (i = 1; i < s->accept_n; i++)
                s->accept_q[i - 1] = s->accept_q[i];
            s->accept_n--;
            (*out)->listen_parent = 0;
            return 0;
        }
        if (s->nonblock)
            return -ROBO_EAGAIN;
        robo_net_wait(&s->rx_waiter, &s->rx_posted);
        robo_sleep(1);
    }
}

int robo_tcp_send(RoboSocket *s, const void *buf, unsigned len)
{
    unsigned n, room;
    if (!s || (!buf && len))
        return -ROBO_EINVAL;
    if (s->err)
        return -s->err;
    if (s->shutdown_tx)
        return -ROBO_EIO;
    if (s->tcp_state != ROBO_TCP_ESTABLISHED &&
        s->tcp_state != ROBO_TCP_CLOSE_WAIT)
        return -ROBO_EIO;
    for (;;) {
        room = s->snd_cap - s->snd_len;
        if (room) {
            n = len;
            if (n > room)
                n = room;
            if (n)
                robo_net_memcpy(s->snd_buf + s->snd_len, buf, n);
            s->snd_len += n;
            tcp_output(s);
            return (int)n;
        }
        if (s->nonblock)
            return -ROBO_EAGAIN;
        robo_net_wait(&s->tx_waiter, &s->tx_posted);
        robo_sleep(1);
    }
}

int robo_tcp_recv(RoboSocket *s, void *buf, unsigned len, int nonblock)
{
    unsigned n;
    uint64_t deadline;
    if (!s || !buf)
        return -ROBO_EINVAL;
    deadline = robo_time_now() + 8000000000ull;
    for (;;) {
        if (s->err)
            return -s->err;
        if (s->rcv_len) {
            n = s->rcv_len;
            if (n > len)
                n = len;
            robo_net_memcpy(buf, s->rcv_buf, n);
            robo_net_memcpy(s->rcv_buf, s->rcv_buf + n, s->rcv_len - n);
            s->rcv_len -= n;
            return (int)n;
        }
        if (s->fin_rcvd || s->shutdown_rx ||
            s->tcp_state == ROBO_TCP_CLOSE_WAIT ||
            s->tcp_state == ROBO_TCP_LAST_ACK ||
            s->tcp_state == ROBO_TCP_CLOSING ||
            s->tcp_state == ROBO_TCP_TIME_WAIT ||
            s->tcp_state == ROBO_TCP_CLOSED)
            return 0;
        if (nonblock)
            return -ROBO_EAGAIN;
        if (robo_time_now() >= deadline)
            return -ROBO_ETIMEDOUT;
        robo_net_wait(&s->rx_waiter, &s->rx_posted);
        robo_sleep(1);
    }
}

int robo_tcp_close(RoboSocket *s)
{
    if (!s)
        return 0;
    if (s->tcp_state == ROBO_TCP_ESTABLISHED ||
        s->tcp_state == ROBO_TCP_SYN_RECEIVED) {
        robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_CLOSE);
        tcp_output(s);
        tcp_xmit(s, (uint8_t)(ROBO_TCPF_FIN | ROBO_TCPF_ACK), s->snd_nxt, 0, 0);
        s->snd_nxt += 1u;
        s->fin_sent = 1;
        return 0;
    }
    if (s->tcp_state == ROBO_TCP_CLOSE_WAIT) {
        robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_CLOSE);
        tcp_xmit(s, (uint8_t)(ROBO_TCPF_FIN | ROBO_TCPF_ACK), s->snd_nxt, 0, 0);
        s->snd_nxt += 1u;
        s->fin_sent = 1;
        return 0;
    }
    s->tcp_state = ROBO_TCP_CLOSED;
    s->sstate = ROBO_SOCK_CLOSED;
    return 0;
}

int robo_tcp_shutdown(RoboSocket *s, int how)
{
    if (!s)
        return -ROBO_EINVAL;
    if (how == 0 || how == 2)
        s->shutdown_rx = 1;
    if (how == 1 || how == 2) {
        s->shutdown_tx = 1;
        if (s->tcp_state == ROBO_TCP_ESTABLISHED)
            robo_tcp_close(s);
    }
    return 0;
}

void robo_tcp_timer(void)
{
    unsigned i, n = robo_sock_count();
    uint64_t now = robo_time_now();
    for (i = 0; i < n; i++) {
        RoboSocket *s = robo_sock_at(i);
        if (!s || s->kind != 1)
            continue;
        if (s->tcp_state == ROBO_TCP_TIME_WAIT && s->tw_deadline &&
            now >= s->tw_deadline) {
            robo_tcp_sm_apply(&s->tcp_state, ROBO_TCP_EV_TIMEOUT);
            s->sstate = ROBO_SOCK_CLOSED;
            continue;
        }
        if (!s->rto_deadline || now < s->rto_deadline)
            continue;
        if (s->tcp_state == ROBO_TCP_SYN_SENT) {
            if (s->retries++ >= TCP_MAX_RETRY) {
                s->err = ROBO_EIO;
                s->tcp_state = ROBO_TCP_CLOSED;
                robo_net_signal(&s->conn_waiter, &s->conn_posted);
                continue;
            }
            tcp_xmit(s, ROBO_TCPF_SYN, s->iss, 0, 0);
            continue;
        }
        if (s->tcp_state == ROBO_TCP_SYN_RECEIVED) {
            if (s->retries++ >= TCP_MAX_RETRY) {
                s->tcp_state = ROBO_TCP_CLOSED;
                continue;
            }
            tcp_xmit(s, (uint8_t)(ROBO_TCPF_SYN | ROBO_TCPF_ACK), s->iss, 0, 0);
            continue;
        }
        if (s->snd_una != s->snd_nxt && s->snd_len) {
            unsigned nsend = s->snd_nxt - s->snd_una;
            if (nsend > s->snd_len)
                nsend = s->snd_len;
            if (nsend > TCP_MSS)
                nsend = TCP_MSS;
            if (s->retries++ >= TCP_MAX_RETRY) {
                s->err = ROBO_EIO;
                s->tcp_state = ROBO_TCP_CLOSED;
                robo_net_signal(&s->rx_waiter, &s->rx_posted);
                continue;
            }
            tcp_xmit(s, (uint8_t)(ROBO_TCPF_ACK | ROBO_TCPF_PSH),
                     s->snd_una, s->snd_buf, nsend);
        }
        if (s->fin_sent && s->tcp_state != ROBO_TCP_CLOSED &&
            s->retries < TCP_MAX_RETRY) {
            tcp_xmit(s, (uint8_t)(ROBO_TCPF_FIN | ROBO_TCPF_ACK),
                     s->snd_una + s->snd_len, 0, 0);
        }
    }
}
#endif
