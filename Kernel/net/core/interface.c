#include "net/core/interface.h"
#include "net/ethernet/ethernet.h"
#include "net/core/net_util.h"
#include "kio.h"

static RoboNetIf gIf[RoboNetMaxIf];
static unsigned gNif;
static uint32_t gNextId = 1;

int NetifInit(void)
{
    unsigned i;
    for (i = 0; i < RoboNetMaxIf; i++)
        NetMemset(&gIf[i], 0, (unsigned)sizeof(gIf[i]));
    gNif = 0;
    gNextId = 1;
    return 1;
}

RoboNetIf *NetifRegister(const char *name, const uint8_t mac[6],
                               int (*SendFrame)(RoboNetIf *, const void *, unsigned),
                               void *drv)
{
    RoboNetIf *ifp;
    unsigned i, n;
    if (gNif >= RoboNetMaxIf || !mac || !SendFrame)
        return 0;
    ifp = &gIf[gNif];
    NetMemset(ifp, 0, (unsigned)sizeof(*ifp));
    ifp->id = gNextId++;
    n = 0;
    if (name) {
        while (name[n] && n + 1u < RoboNetNameMax) {
            ifp->name[n] = name[n];
            n++;
        }
    }
    ifp->name[n] = 0;
    for (i = 0; i < 6; i++)
        ifp->mac[i] = mac[i];
    ifp->mtu = RoboNetMtu;
    ifp->SendFrame = SendFrame;
    ifp->drv = drv;
    ifp->used = 1;
    ifp->linkUp = 1;
    gNif++;
    {
        char macs[18];
        MacFmt(ifp->mac, macs);
        kinfo("[INFO] [NET] interface %s mac %s mtu %u\n",
                ifp->name, macs, (unsigned)ifp->mtu);
    }
    return ifp;
}

RoboNetIf *NetifDefault(void)
{
    return gNif ? &gIf[0] : 0;
}

RoboNetIf *NetifById(uint32_t id)
{
    unsigned i;
    for (i = 0; i < gNif; i++) {
        if (gIf[i].used && gIf[i].id == id)
            return &gIf[i];
    }
    return 0;
}

unsigned NetifCount(void)
{
    return gNif;
}

RoboNetIf *NetifAt(unsigned i)
{
    return i < gNif ? &gIf[i] : 0;
}

void NetifSetIpv4(RoboNetIf *ifp, uint32_t ip, uint32_t mask,
                         uint32_t gw, uint32_t dns)
{
    if (!ifp)
        return;
    ifp->ipv4 = ip;
    ifp->netmask = mask;
    ifp->gateway = gw;
    ifp->dns = dns;
}

void NetifSetLink(RoboNetIf *ifp, int up)
{
    if (ifp)
        ifp->linkUp = up ? 1 : 0;
}

int NetifSend(RoboNetIf *ifp, const void *data, unsigned len)
{
    int r;
    if (!ifp || !ifp->SendFrame || !ifp->linkUp)
        return -1;
    r = ifp->SendFrame(ifp, data, len);
    if (r == 0)
        ifp->txFrames++;
    return r;
}

int NetifRx(RoboNetIf *ifp, const void *data, unsigned len)
{
    if (!ifp)
        return -1;
    ifp->rxFrames++;
    return EthRx(ifp, data, len);
}
