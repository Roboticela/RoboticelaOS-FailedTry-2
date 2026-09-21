#include "net/core/packet.h"
#include "net/core/net_util.h"
#include "mm/heap/heap.h"

static RoboPacket gPkts[RoboNetMaxPkt];
static uint8_t *gBufs[RoboNetMaxPkt];
static uint8_t gUsed[RoboNetMaxPkt];
static int gPoolOk;

int PktPoolInit(void)
{
    unsigned i;
    if (gPoolOk)
        return 1;
    for (i = 0; i < RoboNetMaxPkt; i++) {
        gBufs[i] = (uint8_t *)kmalloc(RoboNetPktCap);
        if (!gBufs[i])
            return 0;
        NetMemset(gBufs[i], 0, RoboNetPktCap);
        gPkts[i].buf = gBufs[i];
        gPkts[i].cap = RoboNetPktCap;
        gUsed[i] = 0;
    }
    gPoolOk = 1;
    return 1;
}

RoboPacket *PktAlloc(void)
{
    unsigned i;
    if (!gPoolOk && !PktPoolInit())
        return 0;
    for (i = 0; i < RoboNetMaxPkt; i++) {
        if (!gUsed[i]) {
            gUsed[i] = 1;
            PktReset(&gPkts[i]);
            return &gPkts[i];
        }
    }
    return 0;
}

void PktFree(RoboPacket *p)
{
    unsigned i;
    if (!p)
        return;
    for (i = 0; i < RoboNetMaxPkt; i++) {
        if (p == &gPkts[i]) {
            gUsed[i] = 0;
            return;
        }
    }
}

void PktReset(RoboPacket *p)
{
    if (!p)
        return;
    p->offset = RoboNetHeadroom;
    p->length = 0;
    p->srcIp = 0;
    p->dstIp = 0;
    p->srcPort = 0;
    p->dstPort = 0;
    p->proto = 0;
    p->ifp = 0;
}

uint8_t *PktData(RoboPacket *p)
{
    return p ? p->buf + p->offset : 0;
}

const uint8_t *PktCdata(const RoboPacket *p)
{
    return p ? p->buf + p->offset : 0;
}

int PktPush(RoboPacket *p, unsigned n)
{
    if (!p || n > p->offset)
        return -1;
    p->offset -= n;
    p->length += n;
    return 0;
}

int PktPull(RoboPacket *p, unsigned n)
{
    if (!p || n > p->length)
        return -1;
    p->offset += n;
    p->length -= n;
    return 0;
}

int PktAppend(RoboPacket *p, const void *src, unsigned n)
{
    unsigned end;
    if (!p)
        return -1;
    end = p->offset + p->length;
    if (n > p->cap - end)
        return -1;
    if (src && n)
        NetMemcpy(p->buf + end, src, n);
    p->length += n;
    return 0;
}

int PktTrim(RoboPacket *p, unsigned n)
{
    if (!p || n > p->length)
        return -1;
    p->length -= n;
    return 0;
}

int PktCopyin(RoboPacket *p, const void *src, unsigned n)
{
    if (!p || n > p->cap - p->offset)
        return -1;
    p->length = 0;
    return PktAppend(p, src, n);
}
