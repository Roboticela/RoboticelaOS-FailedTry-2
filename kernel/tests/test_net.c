#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "net/core/checksum.h"
#include "net/core/net_util.h"
#include "net/ipv4/ipv4.h"
#include "net/ethernet/ethernet.h"
#include "net/tcp/tcp.h"

static int g_fail;

static void expect(int cond, const char *name)
{
    if (cond)
        printf("[PASS] %s\n", name);
    else {
        printf("[FAIL] %s\n", name);
        g_fail = 1;
    }
}

static void test_checksum(void)
{
    static const uint8_t ip[] = {
        0x45, 0x00, 0x00, 0x3c, 0x1c, 0x46, 0x40, 0x00,
        0x40, 0x06, 0xb1, 0xe6, 0xac, 0x10, 0x0a, 0x63,
        0xac, 0x10, 0x0a, 0x0c
    };
    uint8_t tmp[20];
    uint16_t c;
    memcpy(tmp, ip, 20);
    tmp[10] = 0;
    tmp[11] = 0;
    c = robo_checksum(tmp, 20);
    expect(c == 0xb1e6, "checksum ipv4 vector");
    expect(robo_checksum(ip, 20) == 0, "checksum ipv4 verify");
}

static void test_ipv4(void)
{
    static uint8_t good[] = {
        0x45, 0x00, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00,
        0x40, 0x01, 0x00, 0x00, 0x0a, 0x00, 0x02, 0x02,
        0x0a, 0x00, 0x02, 0x0f
    };
    RoboIpv4Info info;
    uint8_t bad[20];
    uint16_t c;
    memcpy(bad, good, 20);
    c = robo_checksum(good, 20);
    good[10] = (uint8_t)(c >> 8);
    good[11] = (uint8_t)c;
    expect(robo_ipv4_parse(good, 20, &info) == 0, "ipv4 valid");
    expect(info.proto == 1, "ipv4 proto");
    memcpy(bad, good, 20);
    bad[0] = 0x55;
    expect(robo_ipv4_parse(bad, 20, &info) != 0, "ipv4 bad version");
    memcpy(bad, good, 20);
    bad[0] = 0x44;
    expect(robo_ipv4_parse(bad, 20, &info) != 0, "ipv4 bad ihl");
    memcpy(bad, good, 20);
    bad[2] = 0x00;
    bad[3] = 0x10;
    expect(robo_ipv4_parse(bad, 20, &info) != 0, "ipv4 bad total");
    memcpy(bad, good, 20);
    bad[10] ^= 0xFF;
    expect(robo_ipv4_parse(bad, 20, &info) != 0, "ipv4 bad checksum");
    memcpy(bad, good, 20);
    bad[6] = 0x20;
    expect(robo_ipv4_parse(bad, 20, &info) != 0, "ipv4 fragment rejected");
    expect(robo_ipv4_parse(good, 10, &info) != 0, "ipv4 short");
}

static void test_eth(void)
{
    uint8_t f[64];
    uint8_t dst[6], src[6];
    uint16_t type;
    unsigned off, plen;
    memset(f, 0, sizeof(f));
    f[12] = 0x08;
    f[13] = 0x00;
    expect(robo_eth_parse(f, 14, dst, src, &type, &off, &plen) == 0, "eth min ipv4");
    expect(type == 0x0800, "eth type ipv4");
    f[13] = 0x06;
    expect(robo_eth_parse(f, 14, dst, src, &type, &off, &plen) == 0, "eth arp");
    expect(robo_eth_parse(f, 10, dst, src, &type, &off, &plen) != 0, "eth short");
    f[12] = 0x00;
    f[13] = 0x01;
    expect(robo_eth_parse(f, 20, dst, src, &type, &off, &plen) != 0, "eth 802.3 reject");
}

static void test_tcp_sm(void)
{
    uint8_t st;
    st = ROBO_TCP_CLOSED;
    expect(robo_tcp_sm_apply(&st, ROBO_TCP_EV_PASSIVE_OPEN) == 0 &&
           st == ROBO_TCP_LISTEN, "CLOSED -> LISTEN");
    st = ROBO_TCP_CLOSED;
    expect(robo_tcp_sm_apply(&st, ROBO_TCP_EV_ACTIVE_OPEN) == 0 &&
           st == ROBO_TCP_SYN_SENT, "CLOSED -> SYN-SENT");
    st = ROBO_TCP_SYN_SENT;
    expect(robo_tcp_sm_apply(&st, ROBO_TCP_EV_SYNACK) == 0 &&
           st == ROBO_TCP_ESTABLISHED, "SYN-SENT -> ESTABLISHED");
    st = ROBO_TCP_SYN_RECEIVED;
    expect(robo_tcp_sm_apply(&st, ROBO_TCP_EV_ACK) == 0 &&
           st == ROBO_TCP_ESTABLISHED, "SYN-RECEIVED -> ESTABLISHED");
    st = ROBO_TCP_ESTABLISHED;
    expect(robo_tcp_sm_apply(&st, ROBO_TCP_EV_CLOSE) == 0 &&
           st == ROBO_TCP_FIN_WAIT_1, "ESTABLISHED -> FIN-WAIT-1");
    st = ROBO_TCP_ESTABLISHED;
    expect(robo_tcp_sm_apply(&st, ROBO_TCP_EV_FIN) == 0 &&
           st == ROBO_TCP_CLOSE_WAIT, "ESTABLISHED -> CLOSE-WAIT");
    st = ROBO_TCP_ESTABLISHED;
    expect(robo_tcp_sm_apply(&st, ROBO_TCP_EV_RST) == 0 &&
           st == ROBO_TCP_CLOSED, "RST -> CLOSED");
    st = ROBO_TCP_LISTEN;
    expect(robo_tcp_sm_apply(&st, ROBO_TCP_EV_ACK) != 0 &&
           st == ROBO_TCP_LISTEN, "LISTEN ignores ACK");
}

int main(void)
{
    test_checksum();
    test_ipv4();
    test_eth();
    test_tcp_sm();
    if (g_fail)
        return 1;
    printf("[PASS] NET HOST\n");
    return 0;
}
