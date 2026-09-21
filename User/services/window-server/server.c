#include "server.h"
#include "ws_compositor.h"
#include <robosys/syscall.h>
#include <robosys/display.h>

#define WS_WIN 64
#define WS_EQ  32
#define WS_PER 16
#define WS_MAX_W 2048u
#define WS_MAX_H 2048u
#define WS_MAX_BYTES (8u * 1024u * 1024u)

struct wsWin {
    int used;
    uint32_t id;
    uint64_t owner;
    int ch;
    int32_t x, y;
    uint32_t w, h, flags, state;
    int visible;
    char title[32];
    uint64_t shmId;
    uint32_t shmBytes, stride, pixfmt, opacity;
    uint32_t *pixels;
    RoboWindowEvent eq[WS_EQ];
    unsigned eh, et, en, drops;
    int waitCh;
};

static struct wsWin gW[WS_WIN];
static uint32_t gZ[WS_WIN];
static unsigned gZn;
static uint32_t gNextId = 1001;
static uint32_t gFocus;
static int32_t gMx, gMy;
static uint32_t gPixfmt;
static int gHeadless;
static int gDirty;

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static void zclear(void *p, unsigned n)
{
    unsigned char *c = (unsigned char *)p;
    while (n--)
        *c++ = 0;
}

static void LogU(const char *s, unsigned long long v)
{
    char buf[24];
    unsigned n = 0, i;
    while (s && s[n])
        n++;
    if (n)
        Write(1, s, n);
    if (!v) {
        Write(1, "0\n", 2);
        return;
    }
    n = 0;
    while (v && n < 24) {
        buf[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    for (i = n; i > 0; i--)
        Write(1, &buf[i - 1], 1);
    Write(1, "\n", 1);
}

static struct wsWin *ById(uint32_t id)
{
    unsigned i;
    if (!id)
        return 0;
    for (i = 0; i < WS_WIN; i++) {
        if (gW[i].used && gW[i].id == id)
            return &gW[i];
    }
    return 0;
}

static unsigned PidCount(uint64_t pid)
{
    unsigned i, n = 0;
    for (i = 0; i < WS_WIN; i++) {
        if (gW[i].used && gW[i].owner == pid)
            n++;
    }
    return n;
}

static int OverflowWh(uint32_t w, uint32_t h, uint64_t *bytes)
{
    uint64_t b;
    if (!w || !h)
        return 1;
    if (w > WS_MAX_W || h > WS_MAX_H)
        return 1;
    b = (uint64_t)w * (uint64_t)h;
    if (w && b / w != h)
        return 1;
    b *= 4ull;
    if (b / 4ull / w != h)
        return 1;
    if (b > WS_MAX_BYTES)
        return 1;
    if (bytes)
        *bytes = b;
    return 0;
}

static void ZRemove(uint32_t id)
{
    unsigned i, j;
    for (i = 0; i < gZn; i++) {
        if (gZ[i] == id) {
            for (j = i; j + 1 < gZn; j++)
                gZ[j] = gZ[j + 1];
            gZn--;
            return;
        }
    }
}

static void ZRaise(uint32_t id)
{
    ZRemove(id);
    if (gZn < WS_WIN)
        gZ[gZn++] = id;
}

static void FillEv(RoboWinMsg *m, const RoboWindowEvent *e)
{
    m->evType = e->type;
    m->evCode = e->code;
    m->evValue = e->value;
    m->evX = e->x;
    m->evY = e->y;
    m->evDx = e->dx;
    m->evDy = e->dy;
    m->evMods = e->modifiers;
    m->evButtons = e->buttons;
    m->window = e->window;
}

static void ReplyWait(struct wsWin *w, const RoboWindowEvent *e)
{
    RoboWinMsg m;
    if (!w || !w->waitCh)
        return;
    zclear(&m, (unsigned)sizeof(m));
    m.op = RoboWinWaitEvent;
    m.status = 0;
    FillEv(&m, e);
    Ipc((int)RoboIpcOpSend, w->waitCh, &m, sizeof(m), 0);
    w->waitCh = 0;
}

static void enqueue(struct wsWin *w, const RoboWindowEvent *e)
{
    if (!w || !e)
        return;
    if (w->waitCh) {
        ReplyWait(w, e);
        return;
    }
    if (w->en >= WS_EQ) {
        w->et = (w->et + 1u) % WS_EQ;
        w->en--;
        w->drops++;
    }
    w->eq[w->eh] = *e;
    w->eh = (w->eh + 1u) % WS_EQ;
    w->en++;
}

static int dequeue(struct wsWin *w, RoboWindowEvent *e)
{
    if (!w || !w->en)
        return 0;
    *e = w->eq[w->et];
    w->et = (w->et + 1u) % WS_EQ;
    w->en--;
    return 1;
}

static void post(struct wsWin *w, uint32_t type)
{
    RoboWindowEvent e;
    zclear(&e, (unsigned)sizeof(e));
    e.type = type;
    e.window = w->id;
    e.x = w->x;
    e.y = w->y;
    enqueue(w, &e);
}

static void SyncComp(struct wsWin *w)
{
    if (!w)
        return;
    WsCompositorSync(w->id, w->shmId, w->shmBytes, w->x, w->y, w->w, w->h,
                       w->stride, w->pixfmt, w->visible, w->opacity);
    gDirty = 1;
}

static int AllocSurface(struct wsWin *w, uint32_t width, uint32_t height)
{
    uint64_t bytes;
    Long id, va;
    if (OverflowWh(width, height, &bytes))
        return -RoboEoverflow;
    id = Ipc((int)RoboIpcOpShmCreate, 0, 0, (Ulong)bytes, 0);
    if (IsErr(id))
        return -RoboEnomem;
    va = Ipc((int)RoboIpcOpShmMap, (int)id, 0, 0, 0);
    if (IsErr(va))
        return -RoboEnomem;
    w->shmId = (uint64_t)id;
    w->shmBytes = (uint32_t)bytes;
    w->stride = width;
    w->pixfmt = gPixfmt;
    w->pixels = (uint32_t *)(uintptr_t)va;
    w->w = width;
    w->h = height;
    return 0;
}

static void FreeSurface(struct wsWin *w)
{
    if (!w)
        return;
    if (w->pixels) {
        Ipc((int)RoboIpcOpShmUnmap, 0, w->pixels, 0, 0);
        w->pixels = 0;
    }
    w->shmId = 0;
}

static void DestroyWin(struct wsWin *w)
{
    if (!w || !w->used)
        return;
    LogU("[WINDOW] Destroyed window ", w->id);
    post(w, RoboWinEvDestroyed);
    if (gFocus == w->id)
        gFocus = 0;
    ZRemove(w->id);
    WsCompositorRemove(w->id);
    FreeSurface(w);
    w->used = 0;
    w->waitCh = 0;
    gDirty = 1;
}

static void RoboSetFocus(struct wsWin *nw)
{
    struct wsWin *old;
    if (nw && !(nw->flags & RoboWindowFocusable))
        return;
    if (nw && gFocus == nw->id)
        return;
    old = ById(gFocus);
    if (old) {
        old->state = old->visible ? RoboWinVisible : RoboWinHidden;
        post(old, RoboWinEvFocusLost);
    }
    gFocus = nw ? nw->id : 0;
    if (nw) {
        ZRaise(nw->id);
        post(nw, RoboWinEvFocusGained);
        LogU("[WINDOW] Focus -> ", nw->id);
        WsCompositorRaise(nw->id);
        SyncComp(nw);
    }
}

int WsInit(int headless, uint32_t pixfmt)
{
    unsigned i;
    gHeadless = headless;
    gPixfmt = pixfmt;
    gNextId = 1001;
    gZn = 0;
    gFocus = 0;
    gMx = gMy = 0;
    gDirty = 0;
    for (i = 0; i < WS_WIN; i++)
        zclear(&gW[i], (unsigned)sizeof(gW[i]));
    return 1;
}

void WsClientAdd(int handle, uint64_t pid)
{
    LogU("[WINDOW] Client pid ", pid);
    (void)handle;
}

void WsClientDrop(int handle)
{
    unsigned i;
    for (i = 0; i < WS_WIN; i++) {
        if (gW[i].used && gW[i].ch == handle)
            DestroyWin(&gW[i]);
    }
}

int WsLiveWindows(void)
{
    unsigned i, n = 0;
    for (i = 0; i < WS_WIN; i++) {
        if (gW[i].used)
            n++;
    }
    return (int)n;
}

int WsHasWaiters(void)
{
    unsigned i;
    for (i = 0; i < WS_WIN; i++) {
        if (gW[i].used && gW[i].waitCh)
            return 1;
    }
    return 0;
}

void WsPresentIfDirty(void)
{
    if (!gDirty)
        return;
    WsCompositorFrame();
    gDirty = 0;
}

static RoboWindowID hit(int32_t x, int32_t y)
{
    unsigned i;
    if (!gZn)
        return 0;
    i = gZn;
    while (i) {
        struct wsWin *w;
        i--;
        w = ById(gZ[i]);
        if (!w || !w->visible)
            continue;
        if (x >= w->x && y >= w->y &&
            x < w->x + (int32_t)w->w && y < w->y + (int32_t)w->h)
            return w->id;
    }
    return 0;
}

void WsRouteInput(const RoboInputEvent *ev)
{
    struct wsWin *w;
    RoboWindowEvent e;
    RoboWindowID id;
    if (!ev)
        return;
    zclear(&e, (unsigned)sizeof(e));
    e.code = ev->code;
    e.value = ev->value;
    e.modifiers = ev->modifiers;
    e.buttons = ev->buttons;
    if (ev->type == RoboMouseMove || ev->type == RoboMouseButton ||
        ev->type == RoboMouseScroll) {
        if (ev->type == RoboMouseMove) {
            gMx += ev->dx;
            gMy += ev->dy;
            if (ev->x || ev->y) {
                gMx = ev->x;
                gMy = ev->y;
            }
        } else if (ev->x || ev->y) {
            gMx = ev->x;
            gMy = ev->y;
        }
        e.x = gMx;
        e.y = gMy;
        e.dx = ev->dx;
        e.dy = ev->dy;
        id = hit(gMx, gMy);
        w = ById(id);
        if (ev->type == RoboMouseButton && ev->value && w)
            RoboSetFocus(w);
        if (!w)
            return;
        e.window = w->id;
        if (ev->type == RoboMouseMove)
            e.type = RoboWinEvMouseMove;
        else if (ev->type == RoboMouseButton)
            e.type = RoboWinEvMouseButton;
        else
            e.type = RoboWinEvMouseScroll;
        enqueue(w, &e);
        return;
    }
    w = ById(gFocus);
    if (!w)
        return;
    e.window = w->id;
    e.x = gMx;
    e.y = gMy;
    e.type = (ev->type == RoboKeyUp) ? RoboWinEvKeyUp : RoboWinEvKeyDown;
    enqueue(w, &e);
}

static struct wsWin *own(uint32_t id, uint64_t pid, int *err)
{
    struct wsWin *w = ById(id);
    if (!w) {
        *err = -RoboEnoent;
        return 0;
    }
    if (w->owner != pid) {
        *err = -RoboEperm;
        return 0;
    }
    return w;
}

static void ReplyOk(RoboWinMsg *m, struct wsWin *w)
{
    m->status = 0;
    if (w) {
        m->window = w->id;
        m->x = w->x;
        m->y = w->y;
        m->width = w->w;
        m->height = w->h;
        m->flags = w->flags;
        m->state = w->state;
        m->shmId = w->shmId;
        m->shmBytes = w->shmBytes;
        m->pixfmt = w->pixfmt;
        m->stride = w->stride;
    }
}

int WsHandle(int handle, uint64_t pid, RoboWinMsg *m)
{
    struct wsWin *w;
    int err = 0;
    unsigned i;
    RoboWindowEvent ev;

    if (!m)
        return 1;
    if (m->op == RoboWinCreate) {
        uint64_t bytes;
        if (OverflowWh(m->width, m->height, &bytes)) {
            m->status = -RoboEoverflow;
            if (!m->width || !m->height)
                m->status = -RoboEinval;
            return 1;
        }
        if (PidCount(pid) >= WS_PER) {
            m->status = -RoboEnomem;
            return 1;
        }
        w = 0;
        for (i = 0; i < WS_WIN; i++) {
            if (!gW[i].used) {
                w = &gW[i];
                break;
            }
        }
        if (!w) {
            m->status = -RoboEnomem;
            return 1;
        }
        zclear(w, (unsigned)sizeof(*w));
        if (AllocSurface(w, m->width, m->height) != 0) {
            m->status = -RoboEnomem;
            return 1;
        }
        w->used = 1;
        w->id = gNextId++;
        w->owner = pid;
        w->ch = handle;
        w->x = 40;
        w->y = 40;
        w->flags = m->flags ? m->flags : (RoboWindowFocusable |
                                          RoboWindowMovable |
                                          RoboWindowResizable);
        w->state = RoboWinCreated;
        w->visible = 0;
        w->opacity = 255;
        ZRaise(w->id);
        LogU("[WINDOW] Created window ", w->id);
        LogU("[WINDOW] Surface ", w->w);
        post(w, RoboWinEvCreated);
        ReplyOk(m, w);
        if (w->flags & RoboWindowVisible) {
            w->visible = 1;
            w->state = RoboWinVisible;
            post(w, RoboWinEvShown);
            RoboSetFocus(w);
            SyncComp(w);
        }
        return 1;
    }
    if (m->op == RoboWinStats) {
        m->status = 0;
        m->extra = (uint32_t)WsLiveWindows();
        m->window = gFocus;
        return 1;
    }
    if (m->op == RoboWinHitTest) {
        m->status = 0;
        m->window = hit(m->x, m->y);
        return 1;
    }
    if (m->op == RoboWinGetFocus) {
        m->status = 0;
        m->window = gFocus;
        return 1;
    }
    if (m->op == RoboWinSamplePixel) {
        uint32_t px = 0;
        if (WsCompositorSample(m->x, m->y, &px) != 0) {
            m->status = -RoboEio;
            return 1;
        }
        m->status = 0;
        m->extra = px;
        return 1;
    }
    if (m->op == RoboWinShutdown) {
        m->status = 0;
        return 2;
    }
    if (m->op == RoboWinTestInject) {
        RoboInputEvent in;
        zclear(&in, (unsigned)sizeof(in));
        if (m->evType == RoboWinEvKeyDown)
            in.type = RoboKeyDown;
        else if (m->evType == RoboWinEvKeyUp)
            in.type = RoboKeyUp;
        else if (m->evType == RoboWinEvMouseMove)
            in.type = RoboMouseMove;
        else if (m->evType == RoboWinEvMouseButton)
            in.type = RoboMouseButton;
        else if (m->evType == RoboWinEvMouseScroll)
            in.type = RoboMouseScroll;
        else
            in.type = m->evType;
        in.code = m->evCode;
        in.value = m->evValue;
        in.x = m->evX;
        in.y = m->evY;
        in.dx = m->evDx;
        in.dy = m->evDy;
        in.modifiers = m->evMods;
        in.buttons = m->evButtons;
        Display((int)RoboDispInject, 0, &in, sizeof(in), 0);
        m->status = 0;
        return 1;
    }
    w = own(m->window, pid, &err);
    if (!w) {
        m->status = err;
        return 1;
    }
    if (m->op == RoboWinDestroy) {
        DestroyWin(w);
        m->status = 0;
        return 1;
    }
    if (m->op == RoboWinShow) {
        w->visible = 1;
        w->state = RoboWinVisible;
        post(w, RoboWinEvShown);
        LogU("[WINDOW] Window shown ", w->id);
        RoboSetFocus(w);
        SyncComp(w);
        ReplyOk(m, w);
        return 1;
    }
    if (m->op == RoboWinHide) {
        w->visible = 0;
        w->state = RoboWinHidden;
        post(w, RoboWinEvHidden);
        if (gFocus == w->id)
            RoboSetFocus(0);
        SyncComp(w);
        ReplyOk(m, w);
        return 1;
    }
    if (m->op == RoboWinMove) {
        w->x = m->x;
        w->y = m->y;
        post(w, RoboWinEvMoved);
        SyncComp(w);
        ReplyOk(m, w);
        return 1;
    }
    if (m->op == RoboWinResize) {
        uint32_t *oldp = w->pixels;
        uint64_t oldid = w->shmId;
        int e;
        e = AllocSurface(w, m->width, m->height);
        if (e) {
            m->status = e;
            return 1;
        }
        if (oldp)
            Ipc((int)RoboIpcOpShmUnmap, 0, oldp, 0, 0);
        (void)oldid;
        post(w, RoboWinEvResized);
        SyncComp(w);
        ReplyOk(m, w);
        return 1;
    }
    if (m->op == RoboWinSetTitle) {
        unsigned t;
        for (t = 0; t < 31; t++) {
            w->title[t] = m->title[t];
            if (!w->title[t])
                break;
        }
        w->title[31] = 0;
        ReplyOk(m, w);
        return 1;
    }
    if (m->op == RoboWinGetSurface) {
        ReplyOk(m, w);
        return 1;
    }
    if (m->op == RoboWinRequestFocus) {
        RoboSetFocus(w);
        ReplyOk(m, w);
        return 1;
    }
    if (m->op == RoboWinSubmit) {
        SyncComp(w);
        ReplyOk(m, w);
        return 1;
    }
    if (m->op == RoboWinSetOpacity) {
        w->opacity = m->extra > 255u ? 255u : m->extra;
        SyncComp(w);
        ReplyOk(m, w);
        return 1;
    }
    if (m->op == RoboWinPollEvent) {
        if (!dequeue(w, &ev)) {
            m->status = -RoboEagain;
            return 1;
        }
        m->status = 0;
        FillEv(m, &ev);
        return 1;
    }
    if (m->op == RoboWinWaitEvent) {
        if (dequeue(w, &ev)) {
            m->status = 0;
            FillEv(m, &ev);
            return 1;
        }
        w->waitCh = handle;
        return 0;
    }
    m->status = -RoboEinval;
    return 1;
}
