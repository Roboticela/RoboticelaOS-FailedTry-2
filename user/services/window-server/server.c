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

struct ws_win {
    int used;
    uint32_t id;
    uint64_t owner;
    int ch;
    int32_t x, y;
    uint32_t w, h, flags, state;
    int visible;
    char title[32];
    uint64_t shm_id;
    uint32_t shm_bytes, stride, pixfmt, opacity;
    uint32_t *pixels;
    RoboWindowEvent eq[WS_EQ];
    unsigned eh, et, en, drops;
    int wait_ch;
};

static struct ws_win g_w[WS_WIN];
static uint32_t g_z[WS_WIN];
static unsigned g_zn;
static uint32_t g_next_id = 1001;
static uint32_t g_focus;
static int32_t g_mx, g_my;
static uint32_t g_pixfmt;
static int g_headless;
static int g_dirty;

static int is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static void zclear(void *p, unsigned n)
{
    unsigned char *c = (unsigned char *)p;
    while (n--)
        *c++ = 0;
}

static void log_u(const char *s, unsigned long long v)
{
    char buf[24];
    unsigned n = 0, i;
    while (s && s[n])
        n++;
    if (n)
        robo_write(1, s, n);
    if (!v) {
        robo_write(1, "0\n", 2);
        return;
    }
    n = 0;
    while (v && n < 24) {
        buf[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    for (i = n; i > 0; i--)
        robo_write(1, &buf[i - 1], 1);
    robo_write(1, "\n", 1);
}

static struct ws_win *by_id(uint32_t id)
{
    unsigned i;
    if (!id)
        return 0;
    for (i = 0; i < WS_WIN; i++) {
        if (g_w[i].used && g_w[i].id == id)
            return &g_w[i];
    }
    return 0;
}

static unsigned pid_count(uint64_t pid)
{
    unsigned i, n = 0;
    for (i = 0; i < WS_WIN; i++) {
        if (g_w[i].used && g_w[i].owner == pid)
            n++;
    }
    return n;
}

static int overflow_wh(uint32_t w, uint32_t h, uint64_t *bytes)
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

static void z_remove(uint32_t id)
{
    unsigned i, j;
    for (i = 0; i < g_zn; i++) {
        if (g_z[i] == id) {
            for (j = i; j + 1 < g_zn; j++)
                g_z[j] = g_z[j + 1];
            g_zn--;
            return;
        }
    }
}

static void z_raise(uint32_t id)
{
    z_remove(id);
    if (g_zn < WS_WIN)
        g_z[g_zn++] = id;
}

static void fill_ev(RoboWinMsg *m, const RoboWindowEvent *e)
{
    m->ev_type = e->type;
    m->ev_code = e->code;
    m->ev_value = e->value;
    m->ev_x = e->x;
    m->ev_y = e->y;
    m->ev_dx = e->dx;
    m->ev_dy = e->dy;
    m->ev_mods = e->modifiers;
    m->ev_buttons = e->buttons;
    m->window = e->window;
}

static void reply_wait(struct ws_win *w, const RoboWindowEvent *e)
{
    RoboWinMsg m;
    if (!w || !w->wait_ch)
        return;
    zclear(&m, (unsigned)sizeof(m));
    m.op = ROBO_WIN_WAIT_EVENT;
    m.status = 0;
    fill_ev(&m, e);
    robo_ipc((int)ROBO_IPC_OP_SEND, w->wait_ch, &m, sizeof(m), 0);
    w->wait_ch = 0;
}

static void enqueue(struct ws_win *w, const RoboWindowEvent *e)
{
    if (!w || !e)
        return;
    if (w->wait_ch) {
        reply_wait(w, e);
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

static int dequeue(struct ws_win *w, RoboWindowEvent *e)
{
    if (!w || !w->en)
        return 0;
    *e = w->eq[w->et];
    w->et = (w->et + 1u) % WS_EQ;
    w->en--;
    return 1;
}

static void post(struct ws_win *w, uint32_t type)
{
    RoboWindowEvent e;
    zclear(&e, (unsigned)sizeof(e));
    e.type = type;
    e.window = w->id;
    e.x = w->x;
    e.y = w->y;
    enqueue(w, &e);
}

static void sync_comp(struct ws_win *w)
{
    if (!w)
        return;
    ws_compositor_sync(w->id, w->shm_id, w->shm_bytes, w->x, w->y, w->w, w->h,
                       w->stride, w->pixfmt, w->visible, w->opacity);
    g_dirty = 1;
}

static int alloc_surface(struct ws_win *w, uint32_t width, uint32_t height)
{
    uint64_t bytes;
    robo_long id, va;
    if (overflow_wh(width, height, &bytes))
        return -ROBO_EOVERFLOW;
    id = robo_ipc((int)ROBO_IPC_OP_SHM_CREATE, 0, 0, (robo_ulong)bytes, 0);
    if (is_err(id))
        return -ROBO_ENOMEM;
    va = robo_ipc((int)ROBO_IPC_OP_SHM_MAP, (int)id, 0, 0, 0);
    if (is_err(va))
        return -ROBO_ENOMEM;
    w->shm_id = (uint64_t)id;
    w->shm_bytes = (uint32_t)bytes;
    w->stride = width;
    w->pixfmt = g_pixfmt;
    w->pixels = (uint32_t *)(uintptr_t)va;
    w->w = width;
    w->h = height;
    return 0;
}

static void free_surface(struct ws_win *w)
{
    if (!w)
        return;
    if (w->pixels) {
        robo_ipc((int)ROBO_IPC_OP_SHM_UNMAP, 0, w->pixels, 0, 0);
        w->pixels = 0;
    }
    w->shm_id = 0;
}

static void destroy_win(struct ws_win *w)
{
    if (!w || !w->used)
        return;
    log_u("[WINDOW] Destroyed window ", w->id);
    post(w, ROBO_WIN_EV_DESTROYED);
    if (g_focus == w->id)
        g_focus = 0;
    z_remove(w->id);
    ws_compositor_remove(w->id);
    free_surface(w);
    w->used = 0;
    w->wait_ch = 0;
    g_dirty = 1;
}

static void set_focus(struct ws_win *nw)
{
    struct ws_win *old;
    if (nw && !(nw->flags & ROBO_WINDOW_FOCUSABLE))
        return;
    if (nw && g_focus == nw->id)
        return;
    old = by_id(g_focus);
    if (old) {
        old->state = old->visible ? ROBO_WIN_VISIBLE : ROBO_WIN_HIDDEN;
        post(old, ROBO_WIN_EV_FOCUS_LOST);
    }
    g_focus = nw ? nw->id : 0;
    if (nw) {
        z_raise(nw->id);
        post(nw, ROBO_WIN_EV_FOCUS_GAINED);
        log_u("[WINDOW] Focus -> ", nw->id);
        ws_compositor_raise(nw->id);
        sync_comp(nw);
    }
}

int ws_init(int headless, uint32_t pixfmt)
{
    unsigned i;
    g_headless = headless;
    g_pixfmt = pixfmt;
    g_next_id = 1001;
    g_zn = 0;
    g_focus = 0;
    g_mx = g_my = 0;
    g_dirty = 0;
    for (i = 0; i < WS_WIN; i++)
        zclear(&g_w[i], (unsigned)sizeof(g_w[i]));
    return 1;
}

void ws_client_add(int handle, uint64_t pid)
{
    log_u("[WINDOW] Client pid ", pid);
    (void)handle;
}

void ws_client_drop(int handle)
{
    unsigned i;
    for (i = 0; i < WS_WIN; i++) {
        if (g_w[i].used && g_w[i].ch == handle)
            destroy_win(&g_w[i]);
    }
}

int ws_live_windows(void)
{
    unsigned i, n = 0;
    for (i = 0; i < WS_WIN; i++) {
        if (g_w[i].used)
            n++;
    }
    return (int)n;
}

int ws_has_waiters(void)
{
    unsigned i;
    for (i = 0; i < WS_WIN; i++) {
        if (g_w[i].used && g_w[i].wait_ch)
            return 1;
    }
    return 0;
}

void ws_present_if_dirty(void)
{
    if (!g_dirty)
        return;
    ws_compositor_frame();
    g_dirty = 0;
}

static RoboWindowID hit(int32_t x, int32_t y)
{
    unsigned i;
    if (!g_zn)
        return 0;
    i = g_zn;
    while (i) {
        struct ws_win *w;
        i--;
        w = by_id(g_z[i]);
        if (!w || !w->visible)
            continue;
        if (x >= w->x && y >= w->y &&
            x < w->x + (int32_t)w->w && y < w->y + (int32_t)w->h)
            return w->id;
    }
    return 0;
}

void ws_route_input(const RoboInputEvent *ev)
{
    struct ws_win *w;
    RoboWindowEvent e;
    RoboWindowID id;
    if (!ev)
        return;
    zclear(&e, (unsigned)sizeof(e));
    e.code = ev->code;
    e.value = ev->value;
    e.modifiers = ev->modifiers;
    e.buttons = ev->buttons;
    if (ev->type == ROBO_MOUSE_MOVE || ev->type == ROBO_MOUSE_BUTTON ||
        ev->type == ROBO_MOUSE_SCROLL) {
        if (ev->type == ROBO_MOUSE_MOVE) {
            g_mx += ev->dx;
            g_my += ev->dy;
            if (ev->x || ev->y) {
                g_mx = ev->x;
                g_my = ev->y;
            }
        } else if (ev->x || ev->y) {
            g_mx = ev->x;
            g_my = ev->y;
        }
        e.x = g_mx;
        e.y = g_my;
        e.dx = ev->dx;
        e.dy = ev->dy;
        id = hit(g_mx, g_my);
        w = by_id(id);
        if (ev->type == ROBO_MOUSE_BUTTON && ev->value && w)
            set_focus(w);
        if (!w)
            return;
        e.window = w->id;
        if (ev->type == ROBO_MOUSE_MOVE)
            e.type = ROBO_WIN_EV_MOUSE_MOVE;
        else if (ev->type == ROBO_MOUSE_BUTTON)
            e.type = ROBO_WIN_EV_MOUSE_BUTTON;
        else
            e.type = ROBO_WIN_EV_MOUSE_SCROLL;
        enqueue(w, &e);
        return;
    }
    w = by_id(g_focus);
    if (!w)
        return;
    e.window = w->id;
    e.x = g_mx;
    e.y = g_my;
    e.type = (ev->type == ROBO_KEY_UP) ? ROBO_WIN_EV_KEY_UP : ROBO_WIN_EV_KEY_DOWN;
    enqueue(w, &e);
}

static struct ws_win *own(uint32_t id, uint64_t pid, int *err)
{
    struct ws_win *w = by_id(id);
    if (!w) {
        *err = -ROBO_ENOENT;
        return 0;
    }
    if (w->owner != pid) {
        *err = -ROBO_EPERM;
        return 0;
    }
    return w;
}

static void reply_ok(RoboWinMsg *m, struct ws_win *w)
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
        m->shm_id = w->shm_id;
        m->shm_bytes = w->shm_bytes;
        m->pixfmt = w->pixfmt;
        m->stride = w->stride;
    }
}

int ws_handle(int handle, uint64_t pid, RoboWinMsg *m)
{
    struct ws_win *w;
    int err = 0;
    unsigned i;
    RoboWindowEvent ev;

    if (!m)
        return 1;
    if (m->op == ROBO_WIN_CREATE) {
        uint64_t bytes;
        if (overflow_wh(m->width, m->height, &bytes)) {
            m->status = -ROBO_EOVERFLOW;
            if (!m->width || !m->height)
                m->status = -ROBO_EINVAL;
            return 1;
        }
        if (pid_count(pid) >= WS_PER) {
            m->status = -ROBO_ENOMEM;
            return 1;
        }
        w = 0;
        for (i = 0; i < WS_WIN; i++) {
            if (!g_w[i].used) {
                w = &g_w[i];
                break;
            }
        }
        if (!w) {
            m->status = -ROBO_ENOMEM;
            return 1;
        }
        zclear(w, (unsigned)sizeof(*w));
        if (alloc_surface(w, m->width, m->height) != 0) {
            m->status = -ROBO_ENOMEM;
            return 1;
        }
        w->used = 1;
        w->id = g_next_id++;
        w->owner = pid;
        w->ch = handle;
        w->x = 40;
        w->y = 40;
        w->flags = m->flags ? m->flags : (ROBO_WINDOW_FOCUSABLE |
                                          ROBO_WINDOW_MOVABLE |
                                          ROBO_WINDOW_RESIZABLE);
        w->state = ROBO_WIN_CREATED;
        w->visible = 0;
        w->opacity = 255;
        z_raise(w->id);
        log_u("[WINDOW] Created window ", w->id);
        log_u("[WINDOW] Surface ", w->w);
        post(w, ROBO_WIN_EV_CREATED);
        reply_ok(m, w);
        if (w->flags & ROBO_WINDOW_VISIBLE) {
            w->visible = 1;
            w->state = ROBO_WIN_VISIBLE;
            post(w, ROBO_WIN_EV_SHOWN);
            set_focus(w);
            sync_comp(w);
        }
        return 1;
    }
    if (m->op == ROBO_WIN_STATS) {
        m->status = 0;
        m->extra = (uint32_t)ws_live_windows();
        m->window = g_focus;
        return 1;
    }
    if (m->op == ROBO_WIN_HIT_TEST) {
        m->status = 0;
        m->window = hit(m->x, m->y);
        return 1;
    }
    if (m->op == ROBO_WIN_GET_FOCUS) {
        m->status = 0;
        m->window = g_focus;
        return 1;
    }
    if (m->op == ROBO_WIN_SAMPLE_PIXEL) {
        uint32_t px = 0;
        if (ws_compositor_sample(m->x, m->y, &px) != 0) {
            m->status = -ROBO_EIO;
            return 1;
        }
        m->status = 0;
        m->extra = px;
        return 1;
    }
    if (m->op == ROBO_WIN_SHUTDOWN) {
        m->status = 0;
        return 2;
    }
    if (m->op == ROBO_WIN_TEST_INJECT) {
        RoboInputEvent in;
        zclear(&in, (unsigned)sizeof(in));
        if (m->ev_type == ROBO_WIN_EV_KEY_DOWN)
            in.type = ROBO_KEY_DOWN;
        else if (m->ev_type == ROBO_WIN_EV_KEY_UP)
            in.type = ROBO_KEY_UP;
        else if (m->ev_type == ROBO_WIN_EV_MOUSE_MOVE)
            in.type = ROBO_MOUSE_MOVE;
        else if (m->ev_type == ROBO_WIN_EV_MOUSE_BUTTON)
            in.type = ROBO_MOUSE_BUTTON;
        else if (m->ev_type == ROBO_WIN_EV_MOUSE_SCROLL)
            in.type = ROBO_MOUSE_SCROLL;
        else
            in.type = m->ev_type;
        in.code = m->ev_code;
        in.value = m->ev_value;
        in.x = m->ev_x;
        in.y = m->ev_y;
        in.dx = m->ev_dx;
        in.dy = m->ev_dy;
        in.modifiers = m->ev_mods;
        in.buttons = m->ev_buttons;
        robo_display((int)ROBO_DISP_INJECT, 0, &in, sizeof(in), 0);
        m->status = 0;
        return 1;
    }
    w = own(m->window, pid, &err);
    if (!w) {
        m->status = err;
        return 1;
    }
    if (m->op == ROBO_WIN_DESTROY) {
        destroy_win(w);
        m->status = 0;
        return 1;
    }
    if (m->op == ROBO_WIN_SHOW) {
        w->visible = 1;
        w->state = ROBO_WIN_VISIBLE;
        post(w, ROBO_WIN_EV_SHOWN);
        log_u("[WINDOW] Window shown ", w->id);
        set_focus(w);
        sync_comp(w);
        reply_ok(m, w);
        return 1;
    }
    if (m->op == ROBO_WIN_HIDE) {
        w->visible = 0;
        w->state = ROBO_WIN_HIDDEN;
        post(w, ROBO_WIN_EV_HIDDEN);
        if (g_focus == w->id)
            set_focus(0);
        sync_comp(w);
        reply_ok(m, w);
        return 1;
    }
    if (m->op == ROBO_WIN_MOVE) {
        w->x = m->x;
        w->y = m->y;
        post(w, ROBO_WIN_EV_MOVED);
        sync_comp(w);
        reply_ok(m, w);
        return 1;
    }
    if (m->op == ROBO_WIN_RESIZE) {
        uint32_t *oldp = w->pixels;
        uint64_t oldid = w->shm_id;
        int e;
        e = alloc_surface(w, m->width, m->height);
        if (e) {
            m->status = e;
            return 1;
        }
        if (oldp)
            robo_ipc((int)ROBO_IPC_OP_SHM_UNMAP, 0, oldp, 0, 0);
        (void)oldid;
        post(w, ROBO_WIN_EV_RESIZED);
        sync_comp(w);
        reply_ok(m, w);
        return 1;
    }
    if (m->op == ROBO_WIN_SET_TITLE) {
        unsigned t;
        for (t = 0; t < 31; t++) {
            w->title[t] = m->title[t];
            if (!w->title[t])
                break;
        }
        w->title[31] = 0;
        reply_ok(m, w);
        return 1;
    }
    if (m->op == ROBO_WIN_GET_SURFACE) {
        reply_ok(m, w);
        return 1;
    }
    if (m->op == ROBO_WIN_REQUEST_FOCUS) {
        set_focus(w);
        reply_ok(m, w);
        return 1;
    }
    if (m->op == ROBO_WIN_SUBMIT) {
        sync_comp(w);
        reply_ok(m, w);
        return 1;
    }
    if (m->op == ROBO_WIN_SET_OPACITY) {
        w->opacity = m->extra > 255u ? 255u : m->extra;
        sync_comp(w);
        reply_ok(m, w);
        return 1;
    }
    if (m->op == ROBO_WIN_POLL_EVENT) {
        if (!dequeue(w, &ev)) {
            m->status = -ROBO_EAGAIN;
            return 1;
        }
        m->status = 0;
        fill_ev(m, &ev);
        return 1;
    }
    if (m->op == ROBO_WIN_WAIT_EVENT) {
        if (dequeue(w, &ev)) {
            m->status = 0;
            fill_ev(m, &ev);
            return 1;
        }
        w->wait_ch = handle;
        return 0;
    }
    m->status = -ROBO_EINVAL;
    return 1;
}
