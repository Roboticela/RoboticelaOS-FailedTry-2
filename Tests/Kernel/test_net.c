#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "net/core/checksum.h"
#include "net/core/net_util.h"
#include "net/ipv4/ipv4.h"
#include "net/ethernet/ethernet.h"
#include "net/tcp/tcp.h"

static int gFail;

static void expect(int cond, const char *name)
{
    if (cond)
        printf("[PASS] %s\n", name);
    else {
        printf("[FAIL] %s\n", name);
        gFail = 1;
    }
}

static void TestChecksum(void)
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
    c = Checksum(tmp, 20);
    expect(c == 0xb1e6, "checksum ipv4 vector");
    expect(Checksum(ip, 20) == 0, "checksum ipv4 verify");
}

static void TestIpv4(void)
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
    c = Checksum(good, 20);
    good[10] = (uint8_t)(c >> 8);
    good[11] = (uint8_t)c;
    expect(Ipv4Parse(good, 20, &info) == 0, "ipv4 valid");
    expect(info.proto == 1, "ipv4 proto");
    memcpy(bad, good, 20);
    bad[0] = 0x55;
    expect(Ipv4Parse(bad, 20, &info) != 0, "ipv4 bad version");
    memcpy(bad, good, 20);
    bad[0] = 0x44;
    expect(Ipv4Parse(bad, 20, &info) != 0, "ipv4 bad ihl");
    memcpy(bad, good, 20);
    bad[2] = 0x00;
    bad[3] = 0x10;
    expect(Ipv4Parse(bad, 20, &info) != 0, "ipv4 bad total");
    memcpy(bad, good, 20);
    bad[10] ^= 0xFF;
    expect(Ipv4Parse(bad, 20, &info) != 0, "ipv4 bad checksum");
    memcpy(bad, good, 20);
    bad[6] = 0x20;
    expect(Ipv4Parse(bad, 20, &info) != 0, "ipv4 fragment rejected");
    expect(Ipv4Parse(good, 10, &info) != 0, "ipv4 short");
}

static void TestEth(void)
{
    uint8_t f[64];
    uint8_t dst[6], src[6];
    uint16_t type;
    unsigned off, plen;
    memset(f, 0, sizeof(f));
    f[12] = 0x08;
    f[13] = 0x00;
    expect(EthParse(f, 14, dst, src, &type, &off, &plen) == 0, "eth min ipv4");
    expect(type == 0x0800, "eth type ipv4");
    f[13] = 0x06;
    expect(EthParse(f, 14, dst, src, &type, &off, &plen) == 0, "eth arp");
    expect(EthParse(f, 10, dst, src, &type, &off, &plen) != 0, "eth short");
    f[12] = 0x00;
    f[13] = 0x01;
    expect(EthParse(f, 20, dst, src, &type, &off, &plen) != 0, "eth 802.3 reject");
}

static void TestTcpSm(void)
{
    uint8_t st;
    st = RoboTcpClosed;
    expect(TcpSmApply(&st, RoboTcpEvPassiveOpen) == 0 &&
           st == RoboTcpListen, "CLOSED -> LISTEN");
    st = RoboTcpClosed;
    expect(TcpSmApply(&st, RoboTcpEvActiveOpen) == 0 &&
           st == RoboTcpSynSent, "CLOSED -> SYN-SENT");
    st = RoboTcpSynSent;
    expect(TcpSmApply(&st, RoboTcpEvSynack) == 0 &&
           st == RoboTcpEstablished, "SYN-SENT -> ESTABLISHED");
    st = RoboTcpSynReceived;
    expect(TcpSmApply(&st, RoboTcpEvAck) == 0 &&
           st == RoboTcpEstablished, "SYN-RECEIVED -> ESTABLISHED");
    st = RoboTcpEstablished;
    expect(TcpSmApply(&st, RoboTcpEvClose) == 0 &&
           st == RoboTcpFinWait1, "ESTABLISHED -> FIN-WAIT-1");
    st = RoboTcpEstablished;
    expect(TcpSmApply(&st, RoboTcpEvFin) == 0 &&
           st == RoboTcpCloseWait, "ESTABLISHED -> CLOSE-WAIT");
    st = RoboTcpEstablished;
    expect(TcpSmApply(&st, RoboTcpEvRst) == 0 &&
           st == RoboTcpClosed, "RST -> CLOSED");
    st = RoboTcpListen;
    expect(TcpSmApply(&st, RoboTcpEvAck) != 0 &&
           st == RoboTcpListen, "LISTEN ignores ACK");
}

int main(void)
{
    TestChecksum();
    TestIpv4();
    TestEth();
    TestTcpSm();
    if (gFail)
        return 1;
    printf("[PASS] NET HOST\n");
    return 0;
}
