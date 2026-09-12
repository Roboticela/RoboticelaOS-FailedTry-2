#include <robowindow/window.h>
#include <robosys/syscall.h>

static int g_ch;
static int g_ok;

static int is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int errcode(robo_long r)
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

static void cpy_title(char *dst, const char *src)
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
    robo_long r;
    if (!g_ok)
        return -ROBO_ENOENT;
    r = robo_ipc((int)ROBO_IPC_OP_SEND, g_ch, m, sizeof(*m), 0);
    if (is_err(r))
        return -errcode(r);
    r = robo_ipc((int)ROBO_IPC_OP_RECV, g_ch, m, sizeof(*m), 0);
    if (is_err(r))
        return -errcode(r);
    if (m->status)
        return m->status;
    return 0;
}

int robo_window_connect(void)
{
    robo_long r;
    unsigned i;
    static const char name[] = ROBO_IPC_NAME_WINDOW_SERVER;

    if (g_ok)
        return 0;
    for (i = 0; i < 200u; i++) {
        r = robo_ipc((int)ROBO_IPC_OP_CONNECT, 0, (void *)name, 0, 0);
        if (!is_err(r) && r > 0) {
            g_ch = (int)r;
            g_ok = 1;
            return 0;
        }
        robo_sleep(5);
    }
    return -ROBO_ENOENT;
}

void robo_window_disconnect(void)
{
    if (g_ok)
        robo_ipc((int)ROBO_IPC_OP_CLOSE, g_ch, 0, 0, 0);
    g_ok = 0;
    g_ch = 0;
}

RoboWindowID robo_window_create(uint32_t width, uint32_t height, uint32_t flags)
{
    RoboWinMsg m;
    int e;
    zmsg(&m);
    m.op = ROBO_WIN_CREATE;
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

int robo_window_destroy(RoboWindowID window)
{
    return simple(ROBO_WIN_DESTROY, window);
}

int robo_window_show(RoboWindowID window)
{
    return simple(ROBO_WIN_SHOW, window);
}

int robo_window_hide(RoboWindowID window)
{
    return simple(ROBO_WIN_HIDE, window);
}

int robo_window_move(RoboWindowID window, int32_t x, int32_t y)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = ROBO_WIN_MOVE;
    m.window = window;
    m.x = x;
    m.y = y;
    return rpc(&m);
}

int robo_window_resize(RoboWindowID window, uint32_t width, uint32_t height)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = ROBO_WIN_RESIZE;
    m.window = window;
    m.width = width;
    m.height = height;
    return rpc(&m);
}

int robo_window_set_title(RoboWindowID window, const char *title)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = ROBO_WIN_SET_TITLE;
    m.window = window;
    cpy_title(m.title, title);
    return rpc(&m);
}

int robo_window_get_surface(RoboWindowID window, RoboWindowSurface *out)
{
    RoboWinMsg m;
    robo_long va;
    int e;
    if (!out)
        return -ROBO_EINVAL;
    zmsg(&m);
    m.op = ROBO_WIN_GET_SURFACE;
    m.window = window;
    e = rpc(&m);
    if (e)
        return e;
    va = robo_ipc((int)ROBO_IPC_OP_SHM_MAP, (int)m.shm_id, 0, 0, 0);
    if (is_err(va))
        return -errcode(va);
    out->width = m.width;
    out->height = m.height;
    out->stride = m.stride;
    out->pixfmt = m.pixfmt;
    out->pixels = (uint32_t *)(uintptr_t)va;
    out->shm_id = m.shm_id;
    out->shm_bytes = m.shm_bytes;
    return 0;
}

int robo_window_request_focus(RoboWindowID window)
{
    return simple(ROBO_WIN_REQUEST_FOCUS, window);
}

static int take_event(uint32_t op, RoboWindowID window, RoboWindowEvent *out)
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
    out->type = m.ev_type;
    out->window = m.window;
    out->code = m.ev_code;
    out->value = m.ev_value;
    out->x = m.ev_x;
    out->y = m.ev_y;
    out->dx = m.ev_dx;
    out->dy = m.ev_dy;
    out->modifiers = m.ev_mods;
    out->buttons = m.ev_buttons;
    return 0;
}

int robo_window_wait_event(RoboWindowID window, RoboWindowEvent *out)
{
    return take_event(ROBO_WIN_WAIT_EVENT, window, out);
}

int robo_window_poll_event(RoboWindowID window, RoboWindowEvent *out)
{
    return take_event(ROBO_WIN_POLL_EVENT, window, out);
}

int robo_window_submit(RoboWindowID window)
{
    return simple(ROBO_WIN_SUBMIT, window);
}

int robo_window_stats(uint32_t *live_windows)
{
    RoboWinMsg m;
    int e;
    zmsg(&m);
    m.op = ROBO_WIN_STATS;
    e = rpc(&m);
    if (e)
        return e;
    if (live_windows)
        *live_windows = m.extra;
    return 0;
}

int robo_window_shutdown(void)
{
    RoboWinMsg m;
    int e;
    zmsg(&m);
    m.op = ROBO_WIN_SHUTDOWN;
    e = rpc(&m);
    return e;
}

int robo_window_test_inject(const RoboWindowEvent *ev)
{
    RoboWinMsg m;
    if (!ev)
        return -ROBO_EINVAL;
    zmsg(&m);
    m.op = ROBO_WIN_TEST_INJECT;
    m.ev_type = ev->type;
    m.ev_code = ev->code;
    m.ev_value = ev->value;
    m.ev_x = ev->x;
    m.ev_y = ev->y;
    m.ev_dx = ev->dx;
    m.ev_dy = ev->dy;
    m.ev_mods = ev->modifiers;
    m.ev_buttons = ev->buttons;
    m.window = ev->window;
    return rpc(&m);
}

RoboWindowID robo_window_hit_test(int32_t x, int32_t y)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = ROBO_WIN_HIT_TEST;
    m.x = x;
    m.y = y;
    if (rpc(&m))
        return 0;
    return m.window;
}

RoboWindowID robo_window_get_focus(void)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = ROBO_WIN_GET_FOCUS;
    if (rpc(&m))
        return 0;
    return m.window;
}

int robo_window_set_opacity(RoboWindowID window, uint32_t opacity)
{
    RoboWinMsg m;
    zmsg(&m);
    m.op = ROBO_WIN_SET_OPACITY;
    m.window = window;
    m.extra = opacity;
    return rpc(&m);
}

int robo_window_sample_pixel(int32_t x, int32_t y, uint32_t *px)
{
    RoboWinMsg m;
    int e;
    zmsg(&m);
    m.op = ROBO_WIN_SAMPLE_PIXEL;
    m.x = x;
    m.y = y;
    e = rpc(&m);
    if (e)
        return e;
    if (px)
        *px = m.extra;
    return 0;
}
