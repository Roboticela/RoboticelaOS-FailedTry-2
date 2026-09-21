#include <robowindow/window.h>
#include <robosys/syscall.h>

static int gCh;
static int gOk;

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int errcode(Long r)
{
    return (int)(-(int64_t)r);
}

static void zmsg(RoboWinMsg *m)
{
    unsigned char *p = (unsigned char *)m;
    unsigned i;
    for (i = 0; i < sizeof(*m); i++)
        p[i] = 0;
}

static void CpyTitle(char *dst, const char *src)
{
    unsigned i;
    for (i = 0; i < 31; i++) {
        dst[i] = src && src[i] ? src[i] : 0;
        if (!dst[i])
            break;
    }
    dst[31] = 0;
}

static int rpc(RoboWinMsg *m)
{
    Long r;
    if (!gOk)
        return -RoboEnoent;
    r = Ipc((int)RoboIpcOpSend, gCh, m, sizeof(*m), 0);
    if (IsErr(r))
        return -errcode(r);
    r = Ipc((int)RoboIpcOpRecv, gCh, m, sizeof(*m), 0);
    if (IsErr(r))
        return -errcode(r);
    if (m->status)
        return m->status;
    return 0;
}

int WindowConnect(void)
{
    Long r;
    unsigned i;
    static const char name[] = RoboIpcNameWindowServer;

    if (gOk)
        return 0;
    for (i = 0; i < 200u; i++) {
        r = Ipc((int)RoboIpcOpConnect, 0, (void *)name, 0, 0);
        if (!IsErr(r) && r > 0) {
            gCh = (int)r;
            gOk = 1;
            return 0;
        }
        RoboSleep(5);
    }
    return -RoboEnoent;
}

void WindowDisconnect(void)
{
    if (gOk)
        Ipc((int)RoboIpcOpClose, gCh, 0, 0, 0);
    gOk = 0;
    gCh = 0;
}

RoboWindowID WindowCreate(uint32_t width, uint32_t height, uint32_t flags)
{
    RoboWinMsg m;
    int e;
    zmsg(&m);
    m.op = RoboWinCreate;
    m.width = width;
    m.height = height;
    m.flags = flags;
    e = rpc(&m);
    if (e)
        return 0;
    return m.window;
}

static int simple(uint32_t op, RoboWindowID window)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = op;
    m.window = window;
    return rpc(&m);
}

int WindowDestroy(RoboWindowID window)
{
    return simple(RoboWinDestroy, window);
}

int WindowShow(RoboWindowID window)
{
    return simple(RoboWinShow, window);
}

int WindowHide(RoboWindowID window)
{
    return simple(RoboWinHide, window);
}

int WindowMove(RoboWindowID window, int32_t x, int32_t y)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = RoboWinMove;
    m.window = window;
    m.x = x;
    m.y = y;
    return rpc(&m);
}

int WindowResize(RoboWindowID window, uint32_t width, uint32_t height)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = RoboWinResize;
    m.window = window;
    m.width = width;
    m.height = height;
    return rpc(&m);
}

int WindowSetTitle(RoboWindowID window, const char *title)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = RoboWinSetTitle;
    m.window = window;
    CpyTitle(m.title, title);
    return rpc(&m);
}

int WindowGetSurface(RoboWindowID window, RoboWindowSurface *out)
{
    RoboWinMsg m;
    Long va;
    int e;
    if (!out)
        return -RoboEinval;
    zmsg(&m);
    m.op = RoboWinGetSurface;
    m.window = window;
    e = rpc(&m);
    if (e)
        return e;
    va = Ipc((int)RoboIpcOpShmMap, (int)m.shmId, 0, 0, 0);
    if (IsErr(va))
        return -errcode(va);
    out->width = m.width;
    out->height = m.height;
    out->stride = m.stride;
    out->pixfmt = m.pixfmt;
    out->pixels = (uint32_t *)(uintptr_t)va;
    out->shmId = m.shmId;
    out->shmBytes = m.shmBytes;
    return 0;
}

int WindowRequestFocus(RoboWindowID window)
{
    return simple(RoboWinRequestFocus, window);
}

static int TakeEvent(uint32_t op, RoboWindowID window, RoboWindowEvent *out)
{
    RoboWinMsg m;
    int e;
    zmsg(&m);
    m.op = op;
    m.window = window;
    e = rpc(&m);
    if (e)
        return e;
    if (!out)
        return 0;
    out->type = m.evType;
    out->window = m.window;
    out->code = m.evCode;
    out->value = m.evValue;
    out->x = m.evX;
    out->y = m.evY;
    out->dx = m.evDx;
    out->dy = m.evDy;
    out->modifiers = m.evMods;
    out->buttons = m.evButtons;
    return 0;
}

int WindowWaitEvent(RoboWindowID window, RoboWindowEvent *out)
{
    return TakeEvent(RoboWinWaitEvent, window, out);
}

int WindowPollEvent(RoboWindowID window, RoboWindowEvent *out)
{
    return TakeEvent(RoboWinPollEvent, window, out);
}

int WindowSubmit(RoboWindowID window)
{
    return simple(RoboWinSubmit, window);
}

int WindowStats(uint32_t *liveWindows)
{
    RoboWinMsg m;
    int e;
    zmsg(&m);
    m.op = RoboWinStats;
    e = rpc(&m);
    if (e)
        return e;
    if (liveWindows)
        *liveWindows = m.extra;
    return 0;
}

int WindowShutdown(void)
{
    RoboWinMsg m;
    int e;
    zmsg(&m);
    m.op = RoboWinShutdown;
    e = rpc(&m);
    return e;
}

int WindowTestInject(const RoboWindowEvent *ev)
{
    RoboWinMsg m;
    if (!ev)
        return -RoboEinval;
    zmsg(&m);
    m.op = RoboWinTestInject;
    m.evType = ev->type;
    m.evCode = ev->code;
    m.evValue = ev->value;
    m.evX = ev->x;
    m.evY = ev->y;
    m.evDx = ev->dx;
    m.evDy = ev->dy;
    m.evMods = ev->modifiers;
    m.evButtons = ev->buttons;
    m.window = ev->window;
    return rpc(&m);
}

RoboWindowID WindowHitTest(int32_t x, int32_t y)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = RoboWinHitTest;
    m.x = x;
    m.y = y;
    if (rpc(&m))
        return 0;
    return m.window;
}

RoboWindowID WindowGetFocus(void)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = RoboWinGetFocus;
    if (rpc(&m))
        return 0;
    return m.window;
}

int WindowSetOpacity(RoboWindowID window, uint32_t opacity)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = RoboWinSetOpacity;
    m.window = window;
    m.extra = opacity;
    return rpc(&m);
}

int WindowSamplePixel(int32_t x, int32_t y, uint32_t *px)
{
    RoboWinMsg m;
    int e;
    zmsg(&m);
    m.op = RoboWinSamplePixel;
    m.x = x;
    m.y = y;
    e = rpc(&m);
    if (e)
        return e;
    if (px)
        *px = m.extra;
    return 0;
}
