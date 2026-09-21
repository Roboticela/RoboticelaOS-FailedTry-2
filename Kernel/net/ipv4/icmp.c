#include "net/ipv4/icmp.h"
#include "net/ipv4/ipv4.h"
#include "net/core/checksum.h"
#include "net/core/net_util.h"
#include "net/core/network.h"
#include "thread/thread.h"
#include "time/time.h"
#include "kio.h"

static uint16_t gEchoId = 1;
static uint16_t gWaitId;
static uint16_t gWaitSeq;
static int gEchoOk;
static struct RoboThread *gPingWaiter;
static int gPingPosted;

int IcmpRx(RoboNetIf *ifp, RoboPacket *pkt, uint32_t src, uint32_t dst)
{
    const uint8_t *p;
    uint8_t type, code;
    uint16_t csum, id, seq;
    RoboPacket *rep;
    unsigned n;

    (void)dst;
    if (!pkt || pkt->length < 8u) {
        PktFree(pkt);
        return -1;
    }
    p = PktCdata(pkt);
    type = p[0];
    code = p[1];
    csum = Checksum(p, pkt->length);
    if (csum != 0) {
        PktFree(pkt);
        return -1;
    }
    if (type == 0 && code == 0) {
        id = Rd16(p + 4);
        seq = Rd16(p + 6);
        if (id == gWaitId && seq == gWaitSeq) {
            gEchoOk = 1;
            NetSignal(&gPingWaiter, &gPingPosted);
        }
        PktFree(pkt);
        return 0;
    }
    if (type != 8 || code != 0) {
        PktFree(pkt);
        return 0;
    }
    n = pkt->length;
    rep = PktAlloc();
    if (!rep) {
        PktFree(pkt);
        return -1;
    }
    if (PktCopyin(rep, p, n) != 0) {
        PktFree(rep);
        PktFree(pkt);
        return -1;
    }
    PktData(rep)[0] = 0;
    PktData(rep)[2] = 0;
    PktData(rep)[3] = 0;
    csum = Checksum(PktData(rep), n);
    Wr16(PktData(rep) + 2, csum);
    Ipv4Send(ifp, src, RoboIpprotoIcmp, rep, 0);
    PktFree(rep);
    PktFree(pkt);
    return 0;
}

int IcmpPing(uint32_t dst, uint32_t timeoutMs)
{
    RoboPacket *pkt;
    uint8_t buf[16];
    uint16_t csum;
    uint64_t deadline;
    unsigned spins;
    RoboNetIf *ifp = NetifDefault();

    if (!ifp)
        return -1;
    gEchoOk = 0;
    gWaitId = gEchoId++;
    gWaitSeq = 1;
    NetMemset(buf, 0, 16);
    buf[0] = 8;
    Wr16(buf + 4, gWaitId);
    Wr16(buf + 6, gWaitSeq);
    buf[8] = 'R';
    buf[9] = 'O';
    buf[10] = 'B';
    buf[11] = 'O';
    csum = Checksum(buf, 16);
    Wr16(buf + 2, csum);
    pkt = PktAlloc();
    if (!pkt)
        return -1;
    if (PktCopyin(pkt, buf, 16) != 0) {
        PktFree(pkt);
        return -1;
    }
    if (Ipv4Send(ifp, dst, RoboIpprotoIcmp, pkt, 1) != 0) {
        PktFree(pkt);
        return -1;
    }
    PktFree(pkt);
    if (!timeoutMs)
        timeoutMs = 2000;
    deadline = TimeNow() + (uint64_t)timeoutMs * 1000000ull;
    for (spins = 0; spins < 80u; spins++) {
        if (gEchoOk)
            return 0;
        if (TimeNow() >= deadline)
            break;
        NetWait(&gPingWaiter, &gPingPosted);
        RoboSleep(25);
    }
    return gEchoOk ? 0 : -1;
}
