#include "server.h"
#include <robocompositor/compose.h>
#include <robocompositor/backend.h>
#include <robosys/syscall.h>
#include <robosys/syscall_errors.h>

#define CS_HEADLESS_W 64u
#define CS_HEADLESS_H 48u

struct cs_map {
    uint32_t window;
    uint64_t shm_id;
    uint32_t *pixels;
    uint32_t bytes;
};

static RoboComposeScene g_sc;
static RoboOutput *g_out;
static struct cs_map g_map[ROBO_COMPOSE_MAX_LAYERS];
static uint32_t *g_back;
static uint32_t *g_front;
static uint64_t g_bytes;
static int g_headless;
static int g_ready;
static uint64_t g_ws_pid;
static unsigned g_log_mod;

static int is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static void puts1(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    if (n)
        robo_write(1, s, n);
}

static void put_u(unsigned long long v)
{
    char buf[24];
    unsigned n = 0, i;
    if (!v) {
        robo_write(1, "0", 1);
        return;
    }
    while (v && n < 24) {
        buf[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    for (i = n; i > 0; i--)
        robo_write(1, &buf[i - 1], 1);
}

static void zclear(void *p, unsigned n)
{
    unsigned char *c = (unsigned char *)p;
    while (n--)
        *c++ = 0;
}

static int cpu_initialize(void *ctx)
{
    (void)ctx;
    return g_ready;
}

static int cpu_compose(void *ctx)
{
    (void)ctx;
    return robo_compose_frame(&g_sc);
}

static int cpu_present(void *ctx)
{
    RoboDamageRect d;
    uint32_t *buf;
    (void)ctx;
    if (g_headless || !g_out || !g_out->ready)
        return 1;
    d = g_sc.damage;
    robo_damage_clip(&d, g_sc.width, g_sc.height);
    if (robo_damage_is_empty(&d))
        return 1;
    buf = g_sc.front ? g_sc.front : g_sc.back;
    return robo_output_present(g_out, buf + (uint32_t)d.y * g_sc.stride + (uint32_t)d.x,
                               d.w, d.h, g_sc.stride, d.x, d.y);
}

static void cpu_shutdown(void *ctx)
{
    (void)ctx;
}

static const RoboCompositionBackend g_cpu = {
    "CPU",
    cpu_initialize,
    cpu_compose,
    cpu_present,
    cpu_shutdown
};

static struct cs_map *map_find(uint32_t id)
{
    unsigned i;
    for (i = 0; i < ROBO_COMPOSE_MAX_LAYERS; i++) {
        if (g_map[i].window == id)
            return &g_map[i];
    }
    return 0;
}

static struct cs_map *map_alloc(uint32_t id)
{
    unsigned i;
    struct cs_map *m = map_find(id);
    if (m)
        return m;
    for (i = 0; i < ROBO_COMPOSE_MAX_LAYERS; i++) {
        if (!g_map[i].window) {
            g_map[i].window = id;
            return &g_map[i];
        }
    }
    return 0;
}

static void map_unmap(struct cs_map *m)
{
    if (!m)
        return;
    if (m->pixels)
        robo_ipc((int)ROBO_IPC_OP_SHM_UNMAP, 0, m->pixels, 0, 0);
    m->pixels = 0;
    m->shm_id = 0;
    m->bytes = 0;
}

static uint32_t *map_surface(uint32_t id, uint64_t shm_id, uint32_t bytes)
{
    struct cs_map *m;
    robo_long va;
    if (!id || !shm_id)
        return 0;
    m = map_alloc(id);
    if (!m)
        return 0;
    if (m->shm_id == shm_id && m->pixels)
        return m->pixels;
    map_unmap(m);
    va = robo_ipc((int)ROBO_IPC_OP_SHM_MAP, (int)shm_id, 0, 0, 0);
    if (is_err(va))
        return 0;
    m->window = id;
    m->shm_id = shm_id;
    m->pixels = (uint32_t *)(uintptr_t)va;
    m->bytes = bytes;
    return m->pixels;
}

static int privileged(uint64_t pid)
{
    if (!g_ws_pid) {
        g_ws_pid = pid;
        return 1;
    }
    return g_ws_pid == pid;
}

int cs_init(RoboOutput *out, int headless)
{
    uint32_t w, h, stride, pixfmt;
    uint64_t bytes;
    robo_long va, va2;
    unsigned i;
    g_out = out;
    g_headless = headless;
    g_ready = 0;
    g_ws_pid = 0;
    g_log_mod = 0;
    for (i = 0; i < ROBO_COMPOSE_MAX_LAYERS; i++)
        zclear(&g_map[i], (unsigned)sizeof(g_map[i]));
    if (out && out->ready) {
        w = out->width;
        h = out->height;
        stride = out->stride ? out->stride : out->width;
        pixfmt = out->pixfmt;
    } else {
        w = CS_HEADLESS_W;
        h = CS_HEADLESS_H;
        stride = w;
        pixfmt = 0;
        g_headless = 1;
    }
    if (w > ROBO_COMPOSE_MAX_W)
        w = ROBO_COMPOSE_MAX_W;
    if (h > ROBO_COMPOSE_MAX_H)
        h = ROBO_COMPOSE_MAX_H;
    bytes = (uint64_t)stride * (uint64_t)h * 4ull;
    bytes = (bytes + 4095ull) & ~4095ull;
    g_bytes = bytes;
    va = robo_mmap(0, (robo_ulong)bytes,
                   (int)(ROBO_PROT_READ | ROBO_PROT_WRITE),
                   (int)(ROBO_MAP_PRIVATE | ROBO_MAP_ANON), -1, 0);
    va2 = robo_mmap(0, (robo_ulong)bytes,
                    (int)(ROBO_PROT_READ | ROBO_PROT_WRITE),
                    (int)(ROBO_MAP_PRIVATE | ROBO_MAP_ANON), -1, 0);
    if (is_err(va) || is_err(va2))
        return 0;
    g_back = (uint32_t *)(uintptr_t)va;
    g_front = (uint32_t *)(uintptr_t)va2;
    if (!robo_compose_init(&g_sc, g_back, g_front, w, h, stride, pixfmt))
        return 0;
    g_ready = 1;
    g_cpu.initialize(0);
    g_cpu.compose(0);
    g_cpu.present(0);
    robo_damage_clear(&g_sc.damage);
    return 1;
}

void cs_drop(void)
{
    unsigned i;
    for (i = 0; i < ROBO_COMPOSE_MAX_LAYERS; i++) {
        if (g_sc.layers[i].used)
            robo_compose_remove(&g_sc, g_sc.layers[i].window_id);
        map_unmap(&g_map[i]);
        g_map[i].window = 0;
    }
    g_ws_pid = 0;
    if (g_ready) {
        robo_damage_add(&g_sc.damage, 0, 0, g_sc.width, g_sc.height);
        cs_compose_present();
    }
}

int cs_dirty(void)
{
    return g_ready && !robo_damage_is_empty(&g_sc.damage);
}

int cs_compose_present(void)
{
    if (!g_ready)
        return 0;
    if (robo_damage_is_empty(&g_sc.damage))
        return 1;
    if (!g_cpu.compose(0))
        return 0;
    if (!g_cpu.present(0))
        return 0;
    g_log_mod++;
    if (g_log_mod == 1u || (g_log_mod % 32u) == 0u)
        cs_log_status();
    robo_damage_clear(&g_sc.damage);
    return 1;
}

void cs_log_status(void)
{
    unsigned vis = 0, i;
    for (i = 0; i < g_sc.nlayers; i++) {
        RoboComposeLayer *l = robo_compose_find(&g_sc, g_sc.zorder[i]);
        if (l && l->visible)
            vis++;
    }
    puts1("RoboCompositor\n--------------\n");
    puts1("Backend       : CPU\nOutput        : ");
    put_u(g_sc.width);
    puts1("x");
    put_u(g_sc.height);
    puts1("\nPixel Format  : ");
    puts1(g_sc.pixfmt == 1u ? "RGBX8888\n" : "BGRX8888\n");
    puts1("Windows       : ");
    put_u(g_sc.nlayers);
    puts1("\nVisible       : ");
    put_u(vis);
    puts1("\nLayers        : ");
    put_u(g_sc.nlayers);
    puts1("\nFrame         : ");
    put_u(g_sc.frame);
    puts1("\n");
}

static void reply_hello(RoboCompMsg *m)
{
    m->status = 0;
    m->width = g_sc.width;
    m->height = g_sc.height;
    m->pixfmt = g_sc.pixfmt;
    m->extra = g_headless ? 1u : 0u;
}

int cs_handle(uint64_t pid, RoboCompMsg *m)
{
    RoboComposeLayer *l;
    uint32_t *pix;
    if (!m)
        return 1;
    if (!privileged(pid)) {
        m->status = -ROBO_EPERM;
        return 1;
    }
    if (m->op == ROBO_COMP_HELLO) {
        reply_hello(m);
        return 1;
    }
    if (m->op == ROBO_COMP_SHUTDOWN) {
        m->status = 0;
        return 2;
    }
    if (m->op == ROBO_COMP_STATS) {
        m->status = 0;
        m->extra = g_sc.nlayers;
        m->window = g_sc.frame;
        reply_hello(m);
        return 1;
    }
    if (m->op == ROBO_COMP_SAMPLE) {
        m->status = 0;
        m->sample_px = robo_compose_sample(&g_sc, m->x, m->y);
        return 1;
    }
    if (m->op == ROBO_COMP_FRAME) {
        m->status = cs_compose_present() ? 0 : -ROBO_EIO;
        return 1;
    }
    if (m->op == ROBO_COMP_DAMAGE) {
        robo_damage_add(&g_sc.damage, m->dx, m->dy, m->dw, m->dh);
        m->status = 0;
        return 1;
    }
    if (m->op == ROBO_COMP_REGISTER || m->op == ROBO_COMP_UPDATE) {
        if (!m->window || !m->width || !m->height) {
            m->status = -ROBO_EINVAL;
            return 1;
        }
        pix = 0;
        if (m->shm_id)
            pix = map_surface(m->window, m->shm_id, m->shm_bytes);
        if (m->shm_id && !pix) {
            m->status = -ROBO_ENOMEM;
            return 1;
        }
        l = robo_compose_find(&g_sc, m->window);
        if (!l) {
            if (robo_compose_insert(&g_sc, m->window, pid, m->x, m->y,
                                    m->width, m->height, m->stride, pix,
                                    m->opacity > 255u ? 255u : m->opacity) != 0) {
                m->status = -ROBO_ENOMEM;
                return 1;
            }
            l = robo_compose_find(&g_sc, m->window);
            puts1("[COMP] layer ");
            put_u(m->window);
            puts1("\n");
        } else {
            robo_damage_add(&g_sc.damage, l->x, l->y, l->w, l->h);
            l->x = m->x;
            l->y = m->y;
            l->w = m->width;
            l->h = m->height;
            l->stride = m->stride ? m->stride : m->width;
            if (pix)
                l->pixels = pix;
            l->opacity = (uint8_t)(m->opacity > 255u ? 255u : m->opacity);
            l->visible = m->visible ? 1 : 0;
            if (l->visible)
                robo_damage_add(&g_sc.damage, l->x, l->y, l->w, l->h);
        }
        if (l)
            l->visible = m->visible ? 1 : 0;
        m->status = 0;
        return 1;
    }
    if (m->op == ROBO_COMP_UNREGISTER) {
        struct cs_map *mm = map_find(m->window);
        map_unmap(mm);
        if (mm)
            mm->window = 0;
        if (robo_compose_remove(&g_sc, m->window) != 0)
            m->status = -ROBO_ENOENT;
        else
            m->status = 0;
        return 1;
    }
    l = robo_compose_find(&g_sc, m->window);
    if (!l) {
        m->status = -ROBO_ENOENT;
        return 1;
    }
    if (m->op == ROBO_COMP_RAISE) {
        robo_compose_raise(&g_sc, m->window);
        m->status = 0;
        return 1;
    }
    if (m->op == ROBO_COMP_LOWER) {
        robo_compose_lower(&g_sc, m->window);
        m->status = 0;
        return 1;
    }
    if (m->op == ROBO_COMP_SET_Z) {
        robo_compose_set_z(&g_sc, m->window, m->z_index);
        m->status = 0;
        return 1;
    }
    m->status = -ROBO_EINVAL;
    return 1;
}
