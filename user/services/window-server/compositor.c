#include "ws_compositor.h"
#include <robosys/syscall.h>
#include <robocompositor/protocol.h>

static int g_ch;
static int g_ok;
static uint32_t g_pixfmt;
static uint32_t g_w, g_h;

static int is_err(robo_long r)
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
    robo_long r;
    if (!g_ok)
        return -1;
    r = robo_ipc((int)ROBO_IPC_OP_SEND, g_ch, m, sizeof(*m), 0);
    if (is_err(r))
        return -1;
    r = robo_ipc((int)ROBO_IPC_OP_RECV, g_ch, m, sizeof(*m), 0);
    if (is_err(r))
        return -1;
    return m->status;
}

int ws_compositor_connect(void)
{
    robo_long r;
    unsigned i;
    RoboCompMsg m;
    static const char name[] = ROBO_IPC_NAME_COMPOSITOR;

    if (g_ok)
        return 0;
    for (i = 0; i < 200u; i++) {
        r = robo_ipc((int)ROBO_IPC_OP_CONNECT, 0, (void *)name, 0, 0);
        if (!is_err(r) && r > 0) {
            g_ch = (int)r;
            g_ok = 1;
            zmsg(&m);
            m.op = ROBO_COMP_HELLO;
            if (rpc(&m) != 0) {
                g_ok = 0;
                return -1;
            }
            g_w = m.width;
            g_h = m.height;
            g_pixfmt = m.pixfmt;
            return 0;
        }
        robo_sleep(5);
    }
    return -1;
}

int ws_compositor_ready(void)
{
    return g_ok;
}

uint32_t ws_compositor_pixfmt(void)
{
    return g_pixfmt;
}

uint32_t ws_compositor_width(void)
{
    return g_w;
}

uint32_t ws_compositor_height(void)
{
    return g_h;
}

void ws_compositor_sync(uint32_t id, uint64_t shm_id, uint32_t shm_bytes,
                        int32_t x, int32_t y, uint32_t w, uint32_t h,
                        uint32_t stride, uint32_t pixfmt, int visible,
                        uint32_t opacity)
{
    RoboCompMsg m;
    zmsg(&m);
    m.op = ROBO_COMP_UPDATE;
    m.window = id;
    m.shm_id = shm_id;
    m.shm_bytes = shm_bytes;
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

void ws_compositor_remove(uint32_t id)
{
    RoboCompMsg m;
    zmsg(&m);
    m.op = ROBO_COMP_UNREGISTER;
    m.window = id;
    rpc(&m);
}

void ws_compositor_raise(uint32_t id)
{
    RoboCompMsg m;
    zmsg(&m);
    m.op = ROBO_COMP_RAISE;
    m.window = id;
    rpc(&m);
}

void ws_compositor_damage(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    RoboCompMsg m;
    zmsg(&m);
    m.op = ROBO_COMP_DAMAGE;
    m.dx = x;
    m.dy = y;
    m.dw = w;
    m.dh = h;
    rpc(&m);
}

int ws_compositor_frame(void)
{
    RoboCompMsg m;
    zmsg(&m);
    m.op = ROBO_COMP_FRAME;
    return rpc(&m);
}

int ws_compositor_sample(int32_t x, int32_t y, uint32_t *px)
{
    RoboCompMsg m;
    zmsg(&m);
    m.op = ROBO_COMP_SAMPLE;
    m.x = x;
    m.y = y;
    if (rpc(&m) != 0)
        return -1;
    if (px)
        *px = m.sample_px;
    return 0;
}
