#include "kio.h"

extern const uint8_t kfont8x16[256][16];

static uint32_t *g_fb;
static uint32_t g_w, g_h, g_pitch, g_pixfmt;
static int g_cx, g_cy;
static int g_margin_x, g_margin_y, g_lh;
static int g_term;
static int g_tx, g_ty, g_tw, g_th;
static int g_rows;
static uint32_t g_card;

static uint32_t pack_rgb(uint32_t rgb)
{
    unsigned r = (rgb >> 16) & 255u;
    unsigned g = (rgb >> 8) & 255u;
    unsigned b = rgb & 255u;
    if (g_pixfmt == ROBO_PIX_RGBX)
        return (r) | (g << 8) | (b << 16);
    return (b) | (g << 8) | (r << 16);
}

void kfb_init(const RoboBootInfo *info)
{
    g_fb = 0;
    g_cx = g_cy = 0;
    g_margin_x = 40;
    g_margin_y = 48;
    g_lh = 22;
    g_term = 0;
    g_rows = 0;
    if (!info || !info->framebuffer_address || info->framebuffer_bpp != 32)
        return;
    g_fb = (uint32_t *)(uintptr_t)info->framebuffer_address;
    g_w = info->framebuffer_width;
    g_h = info->framebuffer_height;
    g_pitch = info->framebuffer_pitch / 4u;
    if (g_pitch < g_w) g_pitch = g_w;
    g_pixfmt = info->framebuffer_pixfmt;
}

void kfb_fill(int x, int y, int w, int h, uint32_t rgb)
{
    uint32_t p = pack_rgb(rgb);
    int yy, xx, x1, y1;
    if (!g_fb || w <= 0 || h <= 0) return;
    x1 = x + w;
    y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int)g_w) x1 = (int)g_w;
    if (y1 > (int)g_h) y1 = (int)g_h;
    for (yy = y; yy < y1; yy++) {
        uint32_t *row = g_fb + (uint32_t)yy * g_pitch;
        for (xx = x; xx < x1; xx++) row[xx] = p;
    }
}

static int rdist2(int dx, int dy)
{
    return dx * dx + dy * dy;
}

void kfb_round_rect(int x, int y, int w, int h, int r, uint32_t rgb)
{
    uint32_t p = pack_rgb(rgb);
    int yy, xx, x1, y1, rr;
    if (!g_fb || w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    rr = r * r;
    x1 = x + w;
    y1 = y + h;
    for (yy = y; yy < y1; yy++) {
        if (yy < 0 || yy >= (int)g_h) continue;
        for (xx = x; xx < x1; xx++) {
            int dx = 0, dy = 0, inside = 1;
            if (xx < 0 || xx >= (int)g_w) continue;
            if (xx < x + r && yy < y + r) {
                dx = x + r - xx;
                dy = y + r - yy;
                inside = rdist2(dx, dy) <= rr;
            } else if (xx >= x1 - r && yy < y + r) {
                dx = xx - (x1 - r - 1);
                dy = y + r - yy;
                inside = rdist2(dx, dy) <= rr;
            } else if (xx < x + r && yy >= y1 - r) {
                dx = x + r - xx;
                dy = yy - (y1 - r - 1);
                inside = rdist2(dx, dy) <= rr;
            } else if (xx >= x1 - r && yy >= y1 - r) {
                dx = xx - (x1 - r - 1);
                dy = yy - (y1 - r - 1);
                inside = rdist2(dx, dy) <= rr;
            }
            if (inside)
                g_fb[(uint32_t)yy * g_pitch + (uint32_t)xx] = p;
        }
    }
}

void kfb_clear(uint32_t rgb)
{
    kfb_fill(0, 0, (int)g_w, (int)g_h, rgb);
    g_cx = 0;
    g_cy = 0;
    g_term = 0;
    g_margin_x = 40;
    g_margin_y = 48;
}

void kfb_term_open(int x, int y, int w, int h)
{
    int r = 18;
    if (w < 80) w = 80;
    if (h < 80) h = 80;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > (int)g_w) w = (int)g_w - x;
    if (y + h > (int)g_h) h = (int)g_h - y;
    g_term = 1;
    g_tx = x;
    g_ty = y;
    g_tw = w;
    g_th = h;
    g_card = 0x1C1C1Eu;
    kfb_round_rect(x, y, w, h, r, g_card);
    g_margin_x = x + 24;
    g_margin_y = y + 48;
    g_lh = 18;
    g_rows = (h - 56) / g_lh;
    if (g_rows < 1) g_rows = 1;
    g_cx = 0;
    g_cy = 0;
    /* Title */
    {
        const char *t = "Log";
        int px = x + 24;
        int py = y + 16;
        while (*t) {
            const uint8_t *rows = kfont8x16[(unsigned char)*t++];
            uint32_t p = pack_rgb(0x98989Du);
            int gy, gx;
            for (gy = 0; gy < 16; gy++) {
                uint8_t bits = rows[gy];
                int iy = py + gy;
                if ((unsigned)iy >= g_h) continue;
                for (gx = 0; gx < 8; gx++) {
                    int ix = px + gx;
                    if ((unsigned)ix >= g_w) continue;
                    if (bits & (0x80 >> gx))
                        g_fb[(uint32_t)iy * g_pitch + (uint32_t)ix] = p;
                }
            }
            px += 9;
        }
        kfb_fill(x + 24, y + 38, w - 48, 1, 0x2C2C2Eu);
    }
}

static void kfb_term_scroll(void)
{
    int y, x;
    int top = g_margin_y;
    int bot = g_ty + g_th - 16;
    int src = top + g_lh;
    uint32_t p = pack_rgb(g_card);
    if (!g_fb) return;
    for (y = top; y < bot - g_lh; y++) {
        uint32_t *dst = g_fb + (uint32_t)y * g_pitch;
        uint32_t *from = g_fb + (uint32_t)(src + (y - top)) * g_pitch;
        for (x = g_tx + 16; x < g_tx + g_tw - 16 && x < (int)g_w; x++)
            dst[x] = from[x];
    }
    for (y = bot - g_lh; y < bot && y < (int)g_h; y++) {
        uint32_t *row = g_fb + (uint32_t)y * g_pitch;
        for (x = g_tx + 16; x < g_tx + g_tw - 16 && x < (int)g_w; x++)
            row[x] = p;
    }
}

static void glyph(int gx, int gy, unsigned char ch, uint32_t rgb)
{
    const uint8_t *rows;
    uint32_t p = pack_rgb(rgb);
    int y, x;
    if (!g_fb) return;
    rows = kfont8x16[ch];
    for (y = 0; y < 16; y++) {
        uint8_t bits = rows[y];
        int py = gy + y;
        if ((unsigned)py >= g_h) continue;
        for (x = 0; x < 8; x++) {
            int px = gx + x;
            if ((unsigned)px >= g_w) continue;
            if (bits & (0x80 >> x))
                g_fb[(uint32_t)py * g_pitch + (uint32_t)px] = p;
        }
    }
}

void kfb_newline(void)
{
    g_cx = 0;
    if (g_term) {
        if (g_cy + 1 >= g_rows) {
            kfb_term_scroll();
            return;
        }
        g_cy++;
        return;
    }
    g_cy++;
    if (g_margin_y + (g_cy + 1) * g_lh > (int)g_h - 24) {
        g_cy = 0;
        kfb_clear(0x000000);
    }
}

void kfb_puts(const char *s, uint32_t rgb)
{
    int max_cols;
    if (!s) return;
    max_cols = g_term ? (g_tw - 48) / 9 : ((int)g_w - g_margin_x * 2) / 9;
    if (max_cols < 8) max_cols = 8;
    while (*s) {
        char c = *s++;
        if (c == '\n') {
            kfb_newline();
            continue;
        }
        glyph(g_margin_x + g_cx * 9, g_margin_y + g_cy * g_lh, (unsigned char)c, rgb);
        g_cx++;
        if (g_cx + 1 > max_cols)
            kfb_newline();
    }
}

void kfb_putu(unsigned long long v, uint32_t rgb)
{
    char tmp[24];
    int n = 0;
    if (v == 0) {
        kfb_puts("0", rgb);
        return;
    }
    while (v && n < 24) {
        tmp[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    while (n--) {
        char b[2] = { tmp[n], 0 };
        kfb_puts(b, rgb);
    }
}

void kfb_puthex(unsigned long long v, uint32_t rgb)
{
    const char *hex = "0123456789abcdef";
    char buf[19];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; i++)
        buf[2 + i] = hex[(v >> (60 - i * 4)) & 15ull];
    buf[18] = 0;
    kfb_puts(buf, rgb);
}
