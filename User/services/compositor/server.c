#include "server.h"
#include <robocompositor/compose.h>
#include <robocompositor/backend.h>
#include <robosys/syscall.h>
#include <robosys/syscallErrors.h>

#define CS_HEADLESS_W 64u
#define CS_HEADLESS_H 48u

struct csMap {
    uint32_t window;
    uint64_t shmId;
    uint32_t *pixels;
    uint32_t bytes;
};

static RoboComposeScene gSc;
static RoboOutput *gOut;
static struct csMap gMap[RoboComposeMaxLayers];
static uint32_t *gBack;
static uint32_t *gFront;
static uint64_t gBytes;
static int gHeadless;
static int gReady;
static uint64_t gWsPid;
static unsigned gLogMod;

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static void puts1(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    if (n)
        Write(1, s, n);
}

static void PutU(unsigned long long v)
{
    char buf[24];
    unsigned n = 0, i;
    if (!v) {
        Write(1, "0", 1);
        return;
    }
    while (v && n < 24) {
        buf[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    for (i = n; i > 0; i--)
        Write(1, &buf[i - 1], 1);
}

static void zclear(void *p, unsigned n)
{
    unsigned char *c = (unsigned char *)p;
    while (n--)
        *c++ = 0;
}

static int CpuInitialize(void *ctx)
{
    (void)ctx;
    return gReady;
}

static int CpuCompose(void *ctx)
{
    (void)ctx;
    return ComposeFrame(&gSc);
}

static int CpuPresent(void *ctx)
{
    RoboDamageRect d;
    uint32_t *buf;
    (void)ctx;
    if (gHeadless || !gOut || !gOut->ready)
        return 1;
    d = gSc.damage;
    DamageClip(&d, gSc.width, gSc.height);
    if (DamageIsEmpty(&d))
        return 1;
    buf = gSc.front ? gSc.front : gSc.back;
    return OutputPresent(gOut, buf + (uint32_t)d.y * gSc.stride + (uint32_t)d.x,
                               d.w, d.h, gSc.stride, d.x, d.y);
}

static void CpuShutdown(void *ctx)
{
    (void)ctx;
}

static const RoboCompositionBackend gCpu = {
    "CPU",
    CpuInitialize,
    CpuCompose,
    CpuPresent,
    CpuShutdown
};

static struct csMap *MapFind(uint32_t id)
{
    unsigned i;
    for (i = 0; i < RoboComposeMaxLayers; i++) {
        if (gMap[i].window == id)
            return &gMap[i];
    }
    return 0;
}

static struct csMap *MapAlloc(uint32_t id)
{
    unsigned i;
    struct csMap *m = MapFind(id);
    if (m)
        return m;
    for (i = 0; i < RoboComposeMaxLayers; i++) {
        if (!gMap[i].window) {
            gMap[i].window = id;
            return &gMap[i];
        }
    }
    return 0;
}

static void MapUnmap(struct csMap *m)
{
    if (!m)
        return;
    if (m->pixels)
        Ipc((int)RoboIpcOpShmUnmap, 0, m->pixels, 0, 0);
    m->pixels = 0;
    m->shmId = 0;
    m->bytes = 0;
}

static uint32_t *MapSurface(uint32_t id, uint64_t shmId, uint32_t bytes)
{
    struct csMap *m;
    Long va;
    if (!id || !shmId)
        return 0;
    m = MapAlloc(id);
    if (!m)
        return 0;
    if (m->shmId == shmId && m->pixels)
        return m->pixels;
    MapUnmap(m);
    va = Ipc((int)RoboIpcOpShmMap, (int)shmId, 0, 0, 0);
    if (IsErr(va))
        return 0;
    m->window = id;
    m->shmId = shmId;
    m->pixels = (uint32_t *)(uintptr_t)va;
    m->bytes = bytes;
    return m->pixels;
}

static int privileged(uint64_t pid)
{
    if (!gWsPid) {
        gWsPid = pid;
        return 1;
    }
    return gWsPid == pid;
}

int CsInit(RoboOutput *out, int headless)
{
    uint32_t w, h, stride, pixfmt;
    uint64_t bytes;
    Long va, va2;
    unsigned i;
    gOut = out;
    gHeadless = headless;
    gReady = 0;
    gWsPid = 0;
    gLogMod = 0;
    for (i = 0; i < RoboComposeMaxLayers; i++)
        zclear(&gMap[i], (unsigned)sizeof(gMap[i]));
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
        gHeadless = 1;
    }
    if (w > RoboComposeMaxW)
        w = RoboComposeMaxW;
    if (h > ROBO_COMPOSE_MAX_H)
        h = ROBO_COMPOSE_MAX_H;
    bytes = (uint64_t)stride * (uint64_t)h * 4ull;
    bytes = (bytes + 4095ull) & ~4095ull;
    gBytes = bytes;
    va = Mmap(0, (Ulong)bytes,
                   (int)(RoboProtRead | RoboProtWrite),
                   (int)(RoboMapPrivate | RoboMapAnon), -1, 0);
    va2 = Mmap(0, (Ulong)bytes,
                    (int)(RoboProtRead | RoboProtWrite),
                    (int)(RoboMapPrivate | RoboMapAnon), -1, 0);
    if (IsErr(va) || IsErr(va2))
        return 0;
    gBack = (uint32_t *)(uintptr_t)va;
    gFront = (uint32_t *)(uintptr_t)va2;
    if (!ComposeInit(&gSc, gBack, gFront, w, h, stride, pixfmt))
        return 0;
    gReady = 1;
    gCpu.initialize(0);
    gCpu.compose(0);
    gCpu.present(0);
    DamageClear(&gSc.damage);
    return 1;
}

void CsDrop(void)
{
    unsigned i;
    for (i = 0; i < RoboComposeMaxLayers; i++) {
        if (gSc.layers[i].used)
            ComposeRemove(&gSc, gSc.layers[i].windowId);
        MapUnmap(&gMap[i]);
        gMap[i].window = 0;
    }
    gWsPid = 0;
    if (gReady) {
        DamageAdd(&gSc.damage, 0, 0, gSc.width, gSc.height);
        CsComposePresent();
    }
}

int CsDirty(void)
{
    return gReady && !DamageIsEmpty(&gSc.damage);
}

int CsComposePresent(void)
{
    if (!gReady)
        return 0;
    if (DamageIsEmpty(&gSc.damage))
        return 1;
    if (!gCpu.compose(0))
        return 0;
    if (!gCpu.present(0))
        return 0;
    gLogMod++;
    if (gLogMod == 1u || (gLogMod % 32u) == 0u)
        CsLogStatus();
    DamageClear(&gSc.damage);
    return 1;
}

void CsLogStatus(void)
{
    unsigned vis = 0, i;
    for (i = 0; i < gSc.nlayers; i++) {
        RoboComposeLayer *l = ComposeFind(&gSc, gSc.zorder[i]);
        if (l && l->visible)
            vis++;
    }
    puts1("RoboCompositor\n--------------\n");
    puts1("Backend       : CPU\nOutput        : ");
    PutU(gSc.width);
    puts1("x");
    PutU(gSc.height);
    puts1("\nPixel Format  : ");
    puts1(gSc.pixfmt == 1u ? "RGBX8888\n" : "BGRX8888\n");
    puts1("Windows       : ");
    PutU(gSc.nlayers);
    puts1("\nVisible       : ");
    PutU(vis);
    puts1("\nLayers        : ");
    PutU(gSc.nlayers);
    puts1("\nFrame         : ");
    PutU(gSc.frame);
    puts1("\n");
}

static void ReplyHello(RoboCompMsg *m)
{
    m->status = 0;
    m->width = gSc.width;
    m->height = gSc.height;
    m->pixfmt = gSc.pixfmt;
    m->extra = gHeadless ? 1u : 0u;
}

int CsHandle(uint64_t pid, RoboCompMsg *m)
{
    RoboComposeLayer *l;
    uint32_t *pix;
    if (!m)
        return 1;
    if (!privileged(pid)) {
        m->status = -RoboEperm;
        return 1;
    }
    if (m->op == RoboCompHello) {
        ReplyHello(m);
        return 1;
    }
    if (m->op == RoboCompShutdown) {
        m->status = 0;
        return 2;
    }
    if (m->op == RoboCompStats) {
        m->status = 0;
        m->extra = gSc.nlayers;
        m->window = gSc.frame;
        ReplyHello(m);
        return 1;
    }
    if (m->op == RoboCompSample) {
        m->status = 0;
        m->samplePx = ComposeSample(&gSc, m->x, m->y);
        return 1;
    }
    if (m->op == RoboCompFrame) {
        m->status = CsComposePresent() ? 0 : -RoboEio;
        return 1;
    }
    if (m->op == RoboCompDamage) {
        DamageAdd(&gSc.damage, m->dx, m->dy, m->dw, m->dh);
        m->status = 0;
        return 1;
    }
    if (m->op == RoboCompRegister || m->op == RoboCompUpdate) {
        if (!m->window || !m->width || !m->height) {
            m->status = -RoboEinval;
            return 1;
        }
        pix = 0;
        if (m->shmId)
            pix = MapSurface(m->window, m->shmId, m->shmBytes);
        if (m->shmId && !pix) {
            m->status = -RoboEnomem;
            return 1;
        }
        l = ComposeFind(&gSc, m->window);
        if (!l) {
            if (ComposeInsert(&gSc, m->window, pid, m->x, m->y,
                                    m->width, m->height, m->stride, pix,
                                    m->opacity > 255u ? 255u : m->opacity) != 0) {
                m->status = -RoboEnomem;
                return 1;
            }
            l = ComposeFind(&gSc, m->window);
            puts1("[COMP] layer ");
            PutU(m->window);
            puts1("\n");
        } else {
            DamageAdd(&gSc.damage, l->x, l->y, l->w, l->h);
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
                DamageAdd(&gSc.damage, l->x, l->y, l->w, l->h);
        }
        if (l)
            l->visible = m->visible ? 1 : 0;
        m->status = 0;
        return 1;
    }
    if (m->op == RoboCompUnregister) {
        struct csMap *mm = MapFind(m->window);
        MapUnmap(mm);
        if (mm)
            mm->window = 0;
        if (ComposeRemove(&gSc, m->window) != 0)
            m->status = -RoboEnoent;
        else
            m->status = 0;
        return 1;
    }
    l = ComposeFind(&gSc, m->window);
    if (!l) {
        m->status = -RoboEnoent;
        return 1;
    }
    if (m->op == RoboCompRaise) {
        ComposeRaise(&gSc, m->window);
        m->status = 0;
        return 1;
    }
    if (m->op == RoboCompLower) {
        ComposeLower(&gSc, m->window);
        m->status = 0;
        return 1;
    }
    if (m->op == RoboCompSetZ) {
        ComposeSetZ(&gSc, m->window, m->zIndex);
        m->status = 0;
        return 1;
    }
    m->status = -RoboEinval;
    return 1;
}
