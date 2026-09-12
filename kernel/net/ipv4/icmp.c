#include "net/ipv4/icmp.h"
#include "net/ipv4/ipv4.h"
#include "net/core/checksum.h"
#include "net/core/net_util.h"
#include "net/core/network.h"
#include "thread/thread.h"
#include "time/time.h"
#include "kio.h"

static uint16_t g_echo_id = 1;
static uint16_t g_wait_id;
static uint16_t g_wait_seq;
static int g_echo_ok;
static struct RoboThread *g_ping_waiter;
static int g_ping_posted;

int robo_icmp_rx(RoboNetIf *ifp, RoboPacket *pkt, uint32_t src, uint32_t dst)
{
    const uint8_t *p;
    uint8_t type, code;
    uint16_t csum, id, seq;
    RoboPacket *rep;
    unsigned n;

    (void)dst;
    if (!pkt || pkt->length < 8u) {
        robo_pkt_free(pkt);
        return -1;
    }
    p = robo_pkt_cdata(pkt);
    type = p[0];
    code = p[1];
    csum = robo_checksum(p, pkt->length);
    if (csum != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    if (type == 0 && code == 0) {
        id = robo_rd16(p + 4);
        seq = robo_rd16(p + 6);
        if (id == g_wait_id && seq == g_wait_seq) {
            g_echo_ok = 1;
            robo_net_signal(&g_ping_waiter, &g_ping_posted);
        }
        robo_pkt_free(pkt);
        return 0;
    }
    if (type != 8 || code != 0) {
        robo_pkt_free(pkt);
        return 0;
    }
    n = pkt->length;
    rep = robo_pkt_alloc();
    if (!rep) {
        robo_pkt_free(pkt);
        return -1;
    }
    if (robo_pkt_copyin(rep, p, n) != 0) {
        robo_pkt_free(rep);
        robo_pkt_free(pkt);
        return -1;
    }
    robo_pkt_data(rep)[0] = 0;
    robo_pkt_data(rep)[2] = 0;
    robo_pkt_data(rep)[3] = 0;
    csum = robo_checksum(robo_pkt_data(rep), n);
    robo_wr16(robo_pkt_data(rep) + 2, csum);
    robo_ipv4_send(ifp, src, ROBO_IPPROTO_ICMP, rep, 0);
    robo_pkt_free(rep);
    robo_pkt_free(pkt);
    return 0;
}

int robo_icmp_ping(uint32_t dst, uint32_t timeout_ms)
{
    RoboPacket *pkt;
    uint8_t buf[16];
    uint16_t csum;
    uint64_t deadline;
    unsigned spins;
    RoboNetIf *ifp = robo_netif_default();

    if (!ifp)
        return -1;
    g_echo_ok = 0;
    g_wait_id = g_echo_id++;
    g_wait_seq = 1;
    robo_net_memset(buf, 0, 16);
    buf[0] = 8;
    robo_wr16(buf + 4, g_wait_id);
    robo_wr16(buf + 6, g_wait_seq);
    buf[8] = 'R';
    buf[9] = 'O';
    buf[10] = 'B';
    buf[11] = 'O';
    csum = robo_checksum(buf, 16);
    robo_wr16(buf + 2, csum);
    pkt = robo_pkt_alloc();
    if (!pkt)
        return -1;
    if (robo_pkt_copyin(pkt, buf, 16) != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    if (robo_ipv4_send(ifp, dst, ROBO_IPPROTO_ICMP, pkt, 1) != 0) {
        robo_pkt_free(pkt);
        return -1;
    }
    robo_pkt_free(pkt);
    if (!timeout_ms)
        timeout_ms = 2000;
    deadline = robo_time_now() + (uint64_t)timeout_ms * 1000000ull;
    for (spins = 0; spins < 80u; spins++) {
        if (g_echo_ok)
            return 0;
        if (robo_time_now() >= deadline)
            break;
        robo_net_wait(&g_ping_waiter, &g_ping_posted);
        robo_sleep(25);
    }
    return g_echo_ok ? 0 : -1;
}
