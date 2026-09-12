#include "net/core/interface.h"
#include "net/ethernet/ethernet.h"
#include "net/core/net_util.h"
#include "kio.h"

static RoboNetIf g_if[ROBO_NET_MAX_IF];
static unsigned g_nif;
static uint32_t g_next_id = 1;

int robo_netif_init(void)
{
    unsigned i;
    for (i = 0; i < ROBO_NET_MAX_IF; i++)
        robo_net_memset(&g_if[i], 0, (unsigned)sizeof(g_if[i]));
    g_nif = 0;
    g_next_id = 1;
    return 1;
}

RoboNetIf *robo_netif_register(const char *name, const uint8_t mac[6],
                               int (*send_frame)(RoboNetIf *, const void *, unsigned),
                               void *drv)
{
    RoboNetIf *ifp;
    unsigned i, n;
    if (g_nif >= ROBO_NET_MAX_IF || !mac || !send_frame)
        return 0;
    ifp = &g_if[g_nif];
    robo_net_memset(ifp, 0, (unsigned)sizeof(*ifp));
    ifp->id = g_next_id++;
    n = 0;
    if (name) {
        while (name[n] && n + 1u < ROBO_NET_NAME_MAX) {
            ifp->name[n] = name[n];
            n++;
        }
    }
    ifp->name[n] = 0;
    for (i = 0; i < 6; i++)
        ifp->mac[i] = mac[i];
    ifp->mtu = ROBO_NET_MTU;
    ifp->send_frame = send_frame;
    ifp->drv = drv;
    ifp->used = 1;
    ifp->link_up = 1;
    g_nif++;
    {
        char macs[18];
        robo_mac_fmt(ifp->mac, macs);
        kprintf("[INFO] [NET] interface %s mac %s mtu %u\n",
                ifp->name, macs, (unsigned)ifp->mtu);
    }
    return ifp;
}

RoboNetIf *robo_netif_default(void)
{
    return g_nif ? &g_if[0] : 0;
}

RoboNetIf *robo_netif_by_id(uint32_t id)
{
    unsigned i;
    for (i = 0; i < g_nif; i++) {
        if (g_if[i].used && g_if[i].id == id)
            return &g_if[i];
    }
    return 0;
}

unsigned robo_netif_count(void)
{
    return g_nif;
}

RoboNetIf *robo_netif_at(unsigned i)
{
    return i < g_nif ? &g_if[i] : 0;
}

void robo_netif_set_ipv4(RoboNetIf *ifp, uint32_t ip, uint32_t mask,
                         uint32_t gw, uint32_t dns)
{
    if (!ifp)
        return;
    ifp->ipv4 = ip;
    ifp->netmask = mask;
    ifp->gateway = gw;
    ifp->dns = dns;
}

void robo_netif_set_link(RoboNetIf *ifp, int up)
{
    if (ifp)
        ifp->link_up = up ? 1 : 0;
}

int robo_netif_send(RoboNetIf *ifp, const void *data, unsigned len)
{
    int r;
    if (!ifp || !ifp->send_frame || !ifp->link_up)
        return -1;
    r = ifp->send_frame(ifp, data, len);
    if (r == 0)
        ifp->tx_frames++;
    return r;
}

int robo_netif_rx(RoboNetIf *ifp, const void *data, unsigned len)
{
    if (!ifp)
        return -1;
    ifp->rx_frames++;
    return robo_eth_rx(ifp, data, len);
}
