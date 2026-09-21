#include "ws_compositor.h"
#include <robosys/syscall.h>
#include <robocompositor/protocol.h>

static int gCh;
static int gOk;
static uint32_t gPixfmt;
static uint32_t gW, gH;

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static void zmsg(RoboCompMsg *m)
{
    unsigned char *p = (unsigned char *)m;
    unsigned i;
    for (i = 0; i < sizeof(*m); i++)
        p[i] = 0;
}

static int rpc(RoboCompMsg *m)
{
    Long r;
    if (!gOk)
        return -1;
    r = Ipc((int)RoboIpcOpSend, gCh, m, sizeof(*m), 0);
    if (IsErr(r))
        return -1;
    r = Ipc((int)RoboIpcOpRecv, gCh, m, sizeof(*m), 0);
    if (IsErr(r))
        return -1;
    return m->status;
}

int WsCompositorConnect(void)
{
    Long r;
    unsigned i;
    RoboCompMsg m;
    static const char name[] = RoboIpcNameCompositor;

    if (gOk)
        return 0;
    for (i = 0; i < 200u; i++) {
        r = Ipc((int)RoboIpcOpConnect, 0, (void *)name, 0, 0);
        if (!IsErr(r) && r > 0) {
            gCh = (int)r;
            gOk = 1;
            zmsg(&m);
            m.op = RoboCompHello;
            if (rpc(&m) != 0) {
                gOk = 0;
                return -1;
            }
            gW = m.width;
            gH = m.height;
            gPixfmt = m.pixfmt;
            return 0;
        }
        RoboSleep(5);
    }
    return -1;
}

int WsCompositorReady(void)
{
    return gOk;
}

uint32_t WsCompositorPixfmt(void)
{
    return gPixfmt;
}

uint32_t WsCompositorWidth(void)
{
    return gW;
}

uint32_t WsCompositorHeight(void)
{
    return gH;
}

void WsCompositorSync(uint32_t id, uint64_t shmId, uint32_t shmBytes,
                        int32_t x, int32_t y, uint32_t w, uint32_t h,
                        uint32_t stride, uint32_t pixfmt, int visible,
                        uint32_t opacity)
{
    RoboCompMsg m;
    zmsg(&m);
    m.op = RoboCompUpdate;
    m.window = id;
    m.shmId = shmId;
    m.shmBytes = shmBytes;
    m.x = x;
    m.y = y;
    m.width = w;
    m.height = h;
    m.stride = stride;
    m.pixfmt = pixfmt;
    m.visible = visible ? 1u : 0u;
    m.opacity = opacity;
    rpc(&m);
}

void WsCompositorRemove(uint32_t id)
{
    RoboCompMsg m;
    zmsg(&m);
    m.op = RoboCompUnregister;
    m.window = id;
    rpc(&m);
}

void WsCompositorRaise(uint32_t id)
{
    RoboCompMsg m;
    zmsg(&m);
    m.op = RoboCompRaise;
    m.window = id;
    rpc(&m);
}

void WsCompositorDamage(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    RoboCompMsg m;
    zmsg(&m);
    m.op = RoboCompDamage;
    m.dx = x;
    m.dy = y;
    m.dw = w;
    m.dh = h;
    rpc(&m);
}

int WsCompositorFrame(void)
{
    RoboCompMsg m;
    zmsg(&m);
    m.op = RoboCompFrame;
    return rpc(&m);
}

int WsCompositorSample(int32_t x, int32_t y, uint32_t *px)
{
    RoboCompMsg m;
    zmsg(&m);
    m.op = RoboCompSample;
    m.x = x;
    m.y = y;
    if (rpc(&m) != 0)
        return -1;
    if (px)
        *px = m.samplePx;
    return 0;
}
