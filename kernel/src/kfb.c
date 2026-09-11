#include "kio.h"

extern const uint8_t kfont8x16[256][16];

static uint32_t *g_fb;
static uint32_t g_w, g_h, g_pitch, g_pixfmt;
static int g_cx, g_cy;
static int g_margin_x, g_margin_y, g_lh;

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
    if (!info || !info->framebuffer_address || info->framebuffer_bpp != 32)
        return;
    g_fb = (uint32_t *)(uintptr_t)info->framebuffer_address;
    g_w = info->framebuffer_width;
    g_h = info->framebuffer_height;
    g_pitch = info->framebuffer_pitch / 4u;
    if (g_pitch < g_w) g_pitch = g_w;
    g_pixfmt = info->framebuffer_pixfmt;
}

void kfb_clear(uint32_t rgb)
{
    uint32_t p = pack_rgb(rgb);
    uint32_t y, x;
    if (!g_fb) return;
    for (y = 0; y < g_h; y++) {
        uint32_t *row = g_fb + y * g_pitch;
        for (x = 0; x < g_w; x++) row[x] = p;
    }
    g_cx = 0;
    g_cy = 0;
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
                g_fb[py * g_pitch + px] = p;
        }
    }
}

void kfb_newline(void)
{
    g_cx = 0;
    g_cy++;
    if (g_margin_y + (g_cy + 1) * g_lh > (int)g_h - 24) {
        /* Keep the last screen; a later kernel can scroll. */
        g_cy = 0;
        kfb_clear(0x000000);
    }
}

void kfb_puts(const char *s, uint32_t rgb)
{
    if (!s) return;
    while (*s) {
        char c = *s++;
        if (c == '\n') {
            kfb_newline();
            continue;
        }
        glyph(g_margin_x + g_cx * 9, g_margin_y + g_cy * g_lh, (unsigned char)c, rgb);
        g_cx++;
        if (g_margin_x + (g_cx + 1) * 9 > (int)g_w - g_margin_x)
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
