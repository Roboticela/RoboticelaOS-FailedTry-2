#include "graphics/gfx.h"
#include "mm/heap/heap.h"
#include "kio.h"

extern const uint8_t kfont8x16[256][16];

static RoboDisplayInfo gDisp;
static RoboSurface *gBack;
static int gReady;

static uint32_t PackPx(uint32_t pixfmt, uint32_t argb)
{
    unsigned a = (argb >> 24) & 255u;
    unsigned r = (argb >> 16) & 255u;
    unsigned g = (argb >> 8) & 255u;
    unsigned b = argb & 255u;
    (void)a;
    if (pixfmt == RoboPixRgbx)
        return b | (g << 8) | (r << 16);
    return r | (g << 8) | (b << 16);
}

static uint32_t UnpackPx(uint32_t pixfmt, uint32_t p)
{
    unsigned r, g, b;
    if (pixfmt == RoboPixRgbx) {
        b = p & 255u;
        g = (p >> 8) & 255u;
        r = (p >> 16) & 255u;
    } else {
        r = p & 255u;
        g = (p >> 8) & 255u;
        b = (p >> 16) & 255u;
    }
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static int ClipXy(const RoboSurface *s, int x, int y)
{
    if (!s || !s->pixels)
        return 0;
    if (x < 0 || y < 0)
        return 0;
    if ((uint32_t)x >= s->width || (uint32_t)y >= s->height)
        return 0;
    return 1;
}

int GfxInit(const RoboBootInfo *info)
{
    gReady = 0;
    gBack = 0;
    gDisp.width = 0;
    gDisp.height = 0;
    gDisp.pitch = 0;
    gDisp.bpp = 0;
    gDisp.pixfmt = RoboPixBgrx;
    gDisp.framebuffer = 0;
    if (!info || !info->framebufferAddress || info->framebufferBpp != 32)
        return 0;
    if (!info->framebufferWidth || !info->framebufferHeight)
        return 0;
    gDisp.width = info->framebufferWidth;
    gDisp.height = info->framebufferHeight;
    gDisp.pitch = info->framebufferPitch;
    gDisp.bpp = info->framebufferBpp;
    gDisp.pixfmt = info->framebufferPixfmt;
    gDisp.framebuffer = (void *)(uintptr_t)info->framebufferAddress;
    gReady = 1;
    return 1;
}

int GfxReady(void)
{
    return gReady;
}

int DisplayGetInfo(RoboDisplayInfo *out)
{
    if (!out || !gReady)
        return 0;
    *out = gDisp;
    return 1;
}

RoboSurface *SurfaceWrap(uint32_t *pixels, uint32_t w, uint32_t h,
                               uint32_t stride, uint32_t pixfmt)
{
    RoboSurface *s;
    if (!pixels || !w || !h || !stride)
        return 0;
    if (stride < w)
        return 0;
    s = (RoboSurface *)kmalloc(sizeof(*s));
    if (!s)
        return 0;
    s->width = w;
    s->height = h;
    s->stride = stride;
    s->pixfmt = pixfmt;
    s->flags = 0;
    s->pixels = pixels;
    return s;
}

RoboSurface *SurfaceCreate(uint32_t w, uint32_t h)
{
    RoboSurface *s;
    uint64_t bytes;
    uint32_t *pix;
    if (!w || !h || w > 4096u || h > 4096u)
        return 0;
    bytes = (uint64_t)w * (uint64_t)h * 4ull;
    if (bytes > (8ull * 1024ull * 1024ull))
        return 0;
    pix = (uint32_t *)kmalloc((size_t)bytes);
    if (!pix)
        return 0;
    s = SurfaceWrap(pix, w, h, w, gReady ? gDisp.pixfmt : RoboPixBgrx);
    if (!s) {
        kfree(pix);
        return 0;
    }
    s->flags = RoboSurfOwned;
    return s;
}

void SurfaceDestroy(RoboSurface *s)
{
    if (!s)
        return;
    if ((s->flags & RoboSurfOwned) && s->pixels)
        kfree(s->pixels);
    kfree(s);
}

void SurfacePixel(RoboSurface *s, int x, int y, uint32_t argb)
{
    uint32_t *row;
    if (!ClipXy(s, x, y))
        return;
    row = s->pixels + (uint32_t)y * s->stride;
    row[x] = PackPx(s->pixfmt, argb);
}

void SurfaceBlend(RoboSurface *s, int x, int y, uint32_t argb)
{
    uint32_t *row, dst, out;
    unsigned a, ia, r, g, b, dr, dg, db;
    if (!ClipXy(s, x, y))
        return;
    a = (argb >> 24) & 255u;
    if (a == 255u) {
        SurfacePixel(s, x, y, argb);
        return;
    }
    if (a == 0)
        return;
    ia = 255u - a;
    row = s->pixels + (uint32_t)y * s->stride;
    dst = UnpackPx(s->pixfmt, row[x]);
    dr = (dst >> 16) & 255u;
    dg = (dst >> 8) & 255u;
    db = dst & 255u;
    r = (((argb >> 16) & 255u) * a + dr * ia) / 255u;
    g = (((argb >> 8) & 255u) * a + dg * ia) / 255u;
    b = ((argb & 255u) * a + db * ia) / 255u;
    out = 0xFF000000u | (r << 16) | (g << 8) | b;
    row[x] = PackPx(s->pixfmt, out);
}

void SurfaceClear(RoboSurface *s, uint32_t argb)
{
    uint32_t y, x, p;
    if (!s || !s->pixels)
        return;
    p = PackPx(s->pixfmt, argb);
    for (y = 0; y < s->height; y++) {
        uint32_t *row = s->pixels + y * s->stride;
        for (x = 0; x < s->width; x++)
            row[x] = p;
    }
}

void SurfaceLine(RoboSurface *s, int x0, int y0, int x1, int y1,
                       uint32_t argb)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    int err = ax - ay;
    for (;;) {
        SurfacePixel(s, x0, y0, argb);
        if (x0 == x1 && y0 == y1)
            break;
        {
            int e2 = err * 2;
            if (e2 > -ay) {
                err -= ay;
                x0 += sx;
            }
            if (e2 < ax) {
                err += ax;
                y0 += sy;
            }
        }
    }
}

void SurfaceRect(RoboSurface *s, int x, int y, int w, int h,
                       uint32_t argb)
{
    int x1, y1, i, j;
    if (!s || w <= 0 || h <= 0)
        return;
    x1 = x + w;
    y1 = y + h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > (int)s->width)
        x1 = (int)s->width;
    if (y1 > (int)s->height)
        y1 = (int)s->height;
    for (j = y; j < y1; j++)
        for (i = x; i < x1; i++)
            SurfacePixel(s, i, j, argb);
}

static int InRound(int px, int py, int x, int y, int w, int h, int r)
{
    int cx, cy, dx, dy;
    if (r <= 0)
        return 1;
    if (px < x + r && py < y + r) {
        cx = x + r;
        cy = y + r;
    } else if (px >= x + w - r && py < y + r) {
        cx = x + w - 1 - r;
        cy = y + r;
    } else if (px < x + r && py >= y + h - r) {
        cx = x + r;
        cy = y + h - 1 - r;
    } else if (px >= x + w - r && py >= y + h - r) {
        cx = x + w - 1 - r;
        cy = y + h - 1 - r;
    } else {
        return 1;
    }
    dx = px - cx;
    dy = py - cy;
    return dx * dx + dy * dy <= r * r;
}

void SurfaceRoundRect(RoboSurface *s, int x, int y, int w, int h,
                             int r, uint32_t argb)
{
    int i, j;
    if (!s || w <= 0 || h <= 0)
        return;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    for (j = y; j < y + h; j++) {
        for (i = x; i < x + w; i++) {
            if (InRound(i, j, x, y, w, h, r))
                SurfacePixel(s, i, j, argb);
        }
    }
}

void SurfaceBitmap(RoboSurface *s, int x, int y, uint32_t w, uint32_t h,
                         const uint32_t *src, uint32_t srcStride)
{
    uint32_t i, j;
    if (!s || !src || !w || !h || srcStride < w)
        return;
    for (j = 0; j < h; j++) {
        const uint32_t *row = src + j * srcStride;
        for (i = 0; i < w; i++)
            SurfaceBlend(s, x + (int)i, y + (int)j, row[i]);
    }
}

void TextDraw(RoboSurface *s, int x, int y, const char *str, uint32_t argb)
{
    int cx = x;
    unsigned ch, row, col;
    if (!s || !str)
        return;
    while (*str) {
        ch = (unsigned char)*str++;
        for (row = 0; row < 16; row++) {
            uint8_t bits = kfont8x16[ch][row];
            for (col = 0; col < 8; col++) {
                if (bits & (uint8_t)(0x80u >> col))
                    SurfacePixel(s, cx + (int)col, y + (int)row, argb);
            }
        }
        cx += 8;
    }
}

int SurfacePresent(const RoboSurface *s)
{
    uint32_t y, x, copyW, copyH, dstStride;
    uint32_t *fb;
    if (!s || !gReady || !gDisp.framebuffer)
        return 0;
    fb = (uint32_t *)gDisp.framebuffer;
    dstStride = gDisp.pitch / 4u;
    if (!dstStride)
        return 0;
    copyW = s->width < gDisp.width ? s->width : gDisp.width;
    copyH = s->height < gDisp.height ? s->height : gDisp.height;
    for (y = 0; y < copyH; y++) {
        const uint32_t *src = s->pixels + y * s->stride;
        uint32_t *dst = fb + y * dstStride;
        for (x = 0; x < copyW; x++)
            dst[x] = src[x];
    }
    return 1;
}

int GfxSelftest(void)
{
    uint32_t buf[64 * 32];
    RoboSurface *s;
    uint32_t bmp[4] = { 0x80FF0000u, 0x8000FF00u, 0x800000FFu, 0xFFFFFFFFu };
    int ok = 1;
    unsigned i;

    for (i = 0; i < 64u * 32u; i++)
        buf[i] = 0;
    s = SurfaceWrap(buf, 64, 32, 64, RoboPixBgrx);
    if (!s)
        return 0;
    SurfaceClear(s, 0xFF000000u);
    SurfacePixel(s, 1, 1, 0xFFFF0000u);
    SurfaceLine(s, 0, 0, 10, 0, 0xFF00FF00u);
    SurfaceRect(s, 2, 2, 8, 6, 0xFF0000FFu);
    SurfaceRoundRect(s, 20, 4, 16, 12, 3, 0xFFFFFF00u);
    SurfaceBitmap(s, 4, 12, 2, 2, bmp, 2);
    TextDraw(s, 8, 16, "A", 0xFFFFFFFFu);
    SurfacePixel(s, -1, 0, 0xFFFFFFFFu);
    SurfacePixel(s, 64, 0, 0xFFFFFFFFu);
    if (buf[1 * 64 + 1] == 0)
        ok = 0;
    if (buf[0] == 0)
        ok = 0;
    SurfaceDestroy(s);
    kprintf("GFX_TEST  software renderer %s\n", ok ? "ok" : "fail");
    return ok;
}
