#include "net/core/packet.h"
#include "net/core/net_util.h"
#include "mm/heap/heap.h"

static RoboPacket g_pkts[ROBO_NET_MAX_PKT];
static uint8_t *g_bufs[ROBO_NET_MAX_PKT];
static uint8_t g_used[ROBO_NET_MAX_PKT];
static int g_pool_ok;

int robo_pkt_pool_init(void)
{
    unsigned i;
    if (g_pool_ok)
        return 1;
    for (i = 0; i < ROBO_NET_MAX_PKT; i++) {
        g_bufs[i] = (uint8_t *)kmalloc(ROBO_NET_PKT_CAP);
        if (!g_bufs[i])
            return 0;
        robo_net_memset(g_bufs[i], 0, ROBO_NET_PKT_CAP);
        g_pkts[i].buf = g_bufs[i];
        g_pkts[i].cap = ROBO_NET_PKT_CAP;
        g_used[i] = 0;
    }
    g_pool_ok = 1;
    return 1;
}

RoboPacket *robo_pkt_alloc(void)
{
    unsigned i;
    if (!g_pool_ok && !robo_pkt_pool_init())
        return 0;
    for (i = 0; i < ROBO_NET_MAX_PKT; i++) {
        if (!g_used[i]) {
            g_used[i] = 1;
            robo_pkt_reset(&g_pkts[i]);
            return &g_pkts[i];
        }
    }
    return 0;
}

void robo_pkt_free(RoboPacket *p)
{
    unsigned i;
    if (!p)
        return;
    for (i = 0; i < ROBO_NET_MAX_PKT; i++) {
        if (p == &g_pkts[i]) {
            g_used[i] = 0;
            return;
        }
    }
}

void robo_pkt_reset(RoboPacket *p)
{
    if (!p)
        return;
    p->offset = ROBO_NET_HEADROOM;
    p->length = 0;
    p->src_ip = 0;
    p->dst_ip = 0;
    p->src_port = 0;
    p->dst_port = 0;
    p->proto = 0;
    p->ifp = 0;
}

uint8_t *robo_pkt_data(RoboPacket *p)
{
    return p ? p->buf + p->offset : 0;
}

const uint8_t *robo_pkt_cdata(const RoboPacket *p)
{
    return p ? p->buf + p->offset : 0;
}

int robo_pkt_push(RoboPacket *p, unsigned n)
{
    if (!p || n > p->offset)
        return -1;
    p->offset -= n;
    p->length += n;
    return 0;
}

int robo_pkt_pull(RoboPacket *p, unsigned n)
{
    if (!p || n > p->length)
        return -1;
    p->offset += n;
    p->length -= n;
    return 0;
}

int robo_pkt_append(RoboPacket *p, const void *src, unsigned n)
{
    unsigned end;
    if (!p)
        return -1;
    end = p->offset + p->length;
    if (n > p->cap - end)
        return -1;
    if (src && n)
        robo_net_memcpy(p->buf + end, src, n);
    p->length += n;
    return 0;
}

int robo_pkt_trim(RoboPacket *p, unsigned n)
{
    if (!p || n > p->length)
        return -1;
    p->length -= n;
    return 0;
}

int robo_pkt_copyin(RoboPacket *p, const void *src, unsigned n)
{
    if (!p || n > p->cap - p->offset)
        return -1;
    p->length = 0;
    return robo_pkt_append(p, src, n);
}
