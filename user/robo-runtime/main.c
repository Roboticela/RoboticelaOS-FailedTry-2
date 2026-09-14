/*
 * Roboticela Runtime — QEMU visual preview of RoboGraphics (Goals 6.1–6.5).
 *
 * Renders through the real surface / framebuffer / bitmap / pixel-format APIs,
 * then presents via SYS_DISPLAY. Caption glyphs are temporary UI chrome only;
 * RoboFont outline→raster is not Phase 6 (comes with RoboText).
 */

#include "RoboGraphics/RoboGraphics.h"

#include <robosys/display.h>
#include <robosys/syscall.h>
#include <robosys/syscall_numbers.h>

#include <stdint.h>

#define FONT_W 8
#define FONT_H 8

/* Palette — Roboticela tokens as RGBA8. */
static const robo_gfx_color_rgba8_t C_BG = {0, 0, 0, 255};
static const robo_gfx_color_rgba8_t C_CARD = {28, 28, 30, 255};
static const robo_gfx_color_rgba8_t C_FG = {255, 255, 255, 255};
static const robo_gfx_color_rgba8_t C_MUTED = {161, 161, 166, 255};
static const robo_gfx_color_rgba8_t C_BRAND = {14, 122, 255, 255};
static const robo_gfx_color_rgba8_t C_TEAL = {20, 184, 166, 255};
static const robo_gfx_color_rgba8_t C_OK = {48, 209, 88, 255};
static const robo_gfx_color_rgba8_t C_WARN = {255, 159, 10, 255};
static const robo_gfx_color_rgba8_t C_DANGER = {255, 59, 48, 255};
static const robo_gfx_color_rgba8_t C_GRAY = {110, 110, 115, 255};

/* Minimal 8x8 captions (MSB = left). Not RoboFont. */
static const uint8_t g_font[95][8] = {
    {0,0,0,0,0,0,0,0},
    {0x10,0x10,0x10,0x10,0x10,0,0x10,0},
    {0x28,0x28,0x28,0,0,0,0,0},
    {0x28,0x28,0x7C,0x28,0x7C,0x28,0x28,0},
    {0x10,0x3C,0x50,0x38,0x14,0x78,0x10,0},
    {0x60,0x64,0x08,0x10,0x20,0x4C,0x0C,0},
    {0x20,0x50,0x50,0x20,0x54,0x48,0x34,0},
    {0x10,0x10,0x20,0,0,0,0,0},
    {0x08,0x10,0x20,0x20,0x20,0x10,0x08,0},
    {0x20,0x10,0x08,0x08,0x08,0x10,0x20,0},
    {0,0x10,0x54,0x38,0x54,0x10,0,0},
    {0,0x10,0x10,0x7C,0x10,0x10,0,0},
    {0,0,0,0,0,0x10,0x10,0x20},
    {0,0,0,0x7C,0,0,0,0},
    {0,0,0,0,0,0,0x10,0},
    {0x04,0x08,0x10,0x20,0x40,0,0,0},
    {0x38,0x44,0x4C,0x54,0x64,0x44,0x38,0},
    {0x10,0x30,0x10,0x10,0x10,0x10,0x38,0},
    {0x38,0x44,0x04,0x18,0x20,0x40,0x7C,0},
    {0x38,0x44,0x04,0x18,0x04,0x44,0x38,0},
    {0x08,0x18,0x28,0x48,0x7C,0x08,0x08,0},
    {0x7C,0x40,0x78,0x04,0x04,0x44,0x38,0},
    {0x18,0x20,0x40,0x78,0x44,0x44,0x38,0},
    {0x7C,0x04,0x08,0x10,0x20,0x20,0x20,0},
    {0x38,0x44,0x44,0x38,0x44,0x44,0x38,0},
    {0x38,0x44,0x44,0x3C,0x04,0x08,0x30,0},
    {0,0x10,0,0,0x10,0,0,0},
    {0,0x10,0,0,0x10,0x10,0x20,0},
    {0x08,0x10,0x20,0x40,0x20,0x10,0x08,0},
    {0,0,0x7C,0,0x7C,0,0,0},
    {0x40,0x20,0x10,0x08,0x10,0x20,0x40,0},
    {0x38,0x44,0x04,0x08,0x10,0,0x10,0},
    {0x38,0x44,0x5C,0x54,0x5C,0x40,0x38,0},
    {0x38,0x44,0x44,0x7C,0x44,0x44,0x44,0},
    {0x78,0x44,0x44,0x78,0x44,0x44,0x78,0},
    {0x38,0x44,0x40,0x40,0x40,0x44,0x38,0},
    {0x78,0x44,0x44,0x44,0x44,0x44,0x78,0},
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x7C,0},
    {0x7C,0x40,0x40,0x78,0x40,0x40,0x40,0},
    {0x38,0x44,0x40,0x4C,0x44,0x44,0x38,0},
    {0x44,0x44,0x44,0x7C,0x44,0x44,0x44,0},
    {0x38,0x10,0x10,0x10,0x10,0x10,0x38,0},
    {0x04,0x04,0x04,0x04,0x04,0x44,0x38,0},
    {0x44,0x48,0x50,0x60,0x50,0x48,0x44,0},
    {0x40,0x40,0x40,0x40,0x40,0x40,0x7C,0},
    {0x44,0x6C,0x54,0x54,0x44,0x44,0x44,0},
    {0x44,0x64,0x54,0x4C,0x44,0x44,0x44,0},
    {0x38,0x44,0x44,0x44,0x44,0x44,0x38,0},
    {0x78,0x44,0x44,0x78,0x40,0x40,0x40,0},
    {0x38,0x44,0x44,0x44,0x54,0x48,0x34,0},
    {0x78,0x44,0x44,0x78,0x50,0x48,0x44,0},
    {0x38,0x44,0x40,0x38,0x04,0x44,0x38,0},
    {0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0},
    {0x44,0x44,0x44,0x44,0x44,0x44,0x38,0},
    {0x44,0x44,0x44,0x44,0x44,0x28,0x10,0},
    {0x44,0x44,0x44,0x54,0x54,0x6C,0x44,0},
    {0x44,0x44,0x28,0x10,0x28,0x44,0x44,0},
    {0x44,0x44,0x28,0x10,0x10,0x10,0x10,0},
    {0x7C,0x04,0x08,0x10,0x20,0x40,0x7C,0},
    {0x38,0x20,0x20,0x20,0x20,0x20,0x38,0},
    {0x40,0x20,0x10,0x08,0x04,0,0,0},
    {0x38,0x08,0x08,0x08,0x08,0x08,0x38,0},
    {0x10,0x28,0x44,0,0,0,0,0},
    {0,0,0,0,0,0,0,0x7C},
    {0x20,0x10,0x08,0,0,0,0,0},
    {0,0,0x38,0x04,0x3C,0x44,0x3C,0},
    {0x40,0x40,0x78,0x44,0x44,0x44,0x78,0},
    {0,0,0x38,0x44,0x40,0x44,0x38,0},
    {0x04,0x04,0x3C,0x44,0x44,0x44,0x3C,0},
    {0,0,0x38,0x44,0x7C,0x40,0x38,0},
    {0x18,0x20,0x20,0x78,0x20,0x20,0x20,0},
    {0,0,0x3C,0x44,0x3C,0x04,0x38,0},
    {0x40,0x40,0x78,0x44,0x44,0x44,0x44,0},
    {0x10,0,0x30,0x10,0x10,0x10,0x38,0},
    {0x08,0,0x18,0x08,0x08,0x48,0x30,0},
    {0x40,0x40,0x48,0x50,0x60,0x50,0x48,0},
    {0x30,0x10,0x10,0x10,0x10,0x10,0x38,0},
    {0,0,0x68,0x54,0x54,0x54,0x54,0},
    {0,0,0x78,0x44,0x44,0x44,0x44,0},
    {0,0,0x38,0x44,0x44,0x44,0x38,0},
    {0,0,0x78,0x44,0x78,0x40,0x40,0},
    {0,0,0x3C,0x44,0x3C,0x04,0x04,0},
    {0,0,0x58,0x64,0x40,0x40,0x40,0},
    {0,0,0x3C,0x40,0x38,0x04,0x78,0},
    {0x20,0x20,0x78,0x20,0x20,0x20,0x18,0},
    {0,0,0x44,0x44,0x44,0x44,0x3C,0},
    {0,0,0x44,0x44,0x44,0x28,0x10,0},
    {0,0,0x44,0x54,0x54,0x54,0x28,0},
    {0,0,0x44,0x28,0x10,0x28,0x44,0},
    {0,0,0x44,0x44,0x3C,0x04,0x38,0},
    {0,0,0x7C,0x08,0x10,0x20,0x7C,0},
    {0x08,0x10,0x10,0x20,0x10,0x10,0x08,0},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x10,0},
    {0x20,0x10,0x10,0x08,0x10,0x10,0x20,0},
    {0,0x20,0x54,0x08,0,0,0,0},
};

static robo_gfx_surface_t *g_screen;
static uint32_t g_w, g_h, g_pixfmt;
static void *g_pixels;

static unsigned slen(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    return n;
}

static void serial(const char *s)
{
    unsigned n = slen(s);
    if (n)
        robo_write(1, s, n);
}

static void caption(robo_gfx_surface_t *s, int px, int py, const char *text,
                    robo_gfx_color_rgba8_t fg)
{
    int x = px;
    unsigned i = 0;
    while (text && text[i]) {
        char ch = text[i];
        const uint8_t *g;
        int gy, gx;
        unsigned idx;
        if (ch < 32 || ch > 126)
            ch = '?';
        idx = (unsigned)(ch - 32);
        g = g_font[idx];
        for (gy = 0; gy < FONT_H; gy++) {
            uint8_t bits = g[gy];
            for (gx = 0; gx < FONT_W; gx++) {
                if (bits & (0x80u >> gx)) {
                    (void)robo_gfx_surface_set_pixel(
                        s, (uint32_t)(x + gx), (uint32_t)(py + gy), fg);
                }
            }
        }
        x += FONT_W + 1;
        i++;
    }
}

static void present(void)
{
    RoboDisplayPresent req;
    if (!g_pixels || !g_screen)
        return;
    req.pixels = (uint64_t)(uintptr_t)g_pixels;
    req.width = g_w;
    req.height = g_h;
    req.stride = g_w;
    req.pixfmt = g_pixfmt;
    req.dest_x = 0;
    req.dest_y = 0;
    robo_display((int)ROBO_DISP_PRESENT, 0, &req, sizeof(req), 0);
}

static int claim_display(void)
{
    RoboUserDisplay info;
    robo_long r;
    uint64_t bytes;
    robo_gfx_surface_desc_t desc;

    r = robo_display((int)ROBO_DISP_CLAIM, 0, 0, 0, (int)ROBO_DISP_ROLE_OUTPUT);
    if ((uint64_t)r >= (uint64_t)-4095ll) {
        serial("preview: display claim failed\n");
        return 0;
    }
    if ((uint64_t)robo_display((int)ROBO_DISP_INFO, 0, &info, sizeof(info), 0) >=
        (uint64_t)-4095ll) {
        serial("preview: display info failed\n");
        return 0;
    }
    if (!info.ready || !info.width || !info.height) {
        serial("preview: display not ready\n");
        return 0;
    }
    g_w = info.width;
    g_h = info.height;
    g_pixfmt = info.pixfmt;
    if (g_w > 1280u)
        g_w = 1280u;
    if (g_h > 800u)
        g_h = 800u;
    bytes = (uint64_t)g_w * (uint64_t)g_h * 4ull;
    g_pixels = (void *)(uintptr_t)robo_mmap(
        0, (robo_ulong)bytes, (int)(ROBO_PROT_READ | ROBO_PROT_WRITE),
        (int)(ROBO_MAP_PRIVATE | ROBO_MAP_ANON), -1, 0);
    if ((uint64_t)(uintptr_t)g_pixels >= (uint64_t)-4095ll || !g_pixels) {
        serial("preview: mmap failed\n");
        return 0;
    }

    /* Present path copies uint32 ARGB; BGRA8 matches that LE layout. */
    robo_gfx_surface_desc_init(&desc);
    desc.width = g_w;
    desc.height = g_h;
    desc.format = ROBO_GFX_FORMAT_BGRA8;
    desc.stride_bytes = (size_t)g_w * 4u;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    if (robo_gfx_surface_create_borrowed(&desc, g_pixels, &g_screen) != ROBO_OK) {
        serial("preview: surface create failed\n");
        return 0;
    }
    return 1;
}

static robo_gfx_surface_t *make_owned(uint32_t w, uint32_t h,
                                      robo_gfx_pixel_format_t fmt)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;
    robo_gfx_surface_desc_init(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = fmt;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    if (robo_gfx_surface_create(&desc, &s) != ROBO_OK)
        return NULL;
    return s;
}

static void draw_swatches(robo_gfx_surface_t *s, int x0, int y0)
{
    static const robo_gfx_color_rgba8_t colors[] = {
        {14, 122, 255, 255}, {20, 184, 166, 255}, {48, 209, 88, 255},
        {255, 159, 10, 255}, {255, 59, 48, 255}, {110, 110, 115, 255},
        {245, 245, 247, 255}, {29, 29, 31, 255}};
    static const char *names[] = {
        "brand", "teal", "ok", "warn", "danger", "gray", "light", "dark"};
    int i;
    caption(s, x0, y0, "fill_rect  palette", C_MUTED);
    for (i = 0; i < 8; i++) {
        int x = x0 + i * 72;
        (void)robo_gfx_surface_fill_rect(
            s, robo_gfx_rect(x, y0 + 16, 64, 40), colors[i]);
        caption(s, x, y0 + 62, names[i], C_MUTED);
    }
}

static void draw_pixels(robo_gfx_surface_t *s, int x0, int y0)
{
    int x, y;
    caption(s, x0, y0, "set_pixel  checker + gradient", C_MUTED);
    for (y = 0; y < 48; y++) {
        for (x = 0; x < 96; x++) {
            int on = ((x / 8) ^ (y / 8)) & 1;
            robo_gfx_color_rgba8_t c =
                on ? robo_gfx_rgba8(14, 122, 255, 255)
                   : robo_gfx_rgba8(28, 28, 30, 255);
            (void)robo_gfx_surface_set_pixel(s, (uint32_t)(x0 + x),
                                             (uint32_t)(y0 + 16 + y), c);
        }
    }
    for (y = 0; y < 48; y++) {
        for (x = 0; x < 160; x++) {
            uint8_t t = (uint8_t)((x * 255) / 159);
            (void)robo_gfx_surface_set_pixel(
                s, (uint32_t)(x0 + 112 + x), (uint32_t)(y0 + 16 + y),
                robo_gfx_rgba8(t, (uint8_t)(80 + y), (uint8_t)(255 - t), 255));
        }
    }
}

static void draw_formats(robo_gfx_surface_t *dst, int x0, int y0)
{
    typedef struct {
        robo_gfx_pixel_format_t fmt;
        const char *name;
    } fmt_row_t;
    static const fmt_row_t rows[] = {
        {ROBO_GFX_FORMAT_RGBA8, "RGBA8"},
        {ROBO_GFX_FORMAT_BGRA8, "BGRA8"},
        {ROBO_GFX_FORMAT_RGB8, "RGB8"},
        {ROBO_GFX_FORMAT_GRAY8, "GRAY8"},
        {ROBO_GFX_FORMAT_ALPHA8, "ALPHA8"},
    };
    int i;
    caption(dst, x0, y0, "pixel formats  (owned surfaces -> copy_rect convert)",
            C_MUTED);
    for (i = 0; i < 5; i++) {
        robo_gfx_surface_t *src = make_owned(120, 28, rows[i].fmt);
        int y = y0 + 18 + i * 36;
        if (!src)
            continue;
        if (rows[i].fmt == ROBO_GFX_FORMAT_ALPHA8) {
            (void)robo_gfx_surface_clear(src, robo_gfx_rgba8(0, 0, 0, 200));
            (void)robo_gfx_surface_fill_rect(
                src, robo_gfx_rect(20, 4, 80, 20),
                robo_gfx_rgba8(0, 0, 0, 80));
        } else if (rows[i].fmt == ROBO_GFX_FORMAT_GRAY8) {
            (void)robo_gfx_surface_clear(src, robo_gfx_rgba8(40, 40, 40, 255));
            (void)robo_gfx_surface_fill_rect(
                src, robo_gfx_rect(8, 4, 48, 20),
                robo_gfx_rgba8(14, 122, 255, 255));
            (void)robo_gfx_surface_fill_rect(
                src, robo_gfx_rect(64, 4, 48, 20),
                robo_gfx_rgba8(255, 159, 10, 255));
        } else {
            (void)robo_gfx_surface_clear(src, robo_gfx_rgba8(28, 28, 30, 255));
            (void)robo_gfx_surface_fill_rect(
                src, robo_gfx_rect(8, 4, 32, 20), C_BRAND);
            (void)robo_gfx_surface_fill_rect(
                src, robo_gfx_rect(44, 4, 32, 20), C_TEAL);
            (void)robo_gfx_surface_fill_rect(
                src, robo_gfx_rect(80, 4, 32, 20), C_WARN);
        }
        (void)robo_gfx_surface_copy_rect(
            src, robo_gfx_rect(0, 0, 120, 28), dst, robo_gfx_point(x0 + 72, y));
        caption(dst, x0, y + 8, rows[i].name, C_FG);
        robo_gfx_surface_release(src);
    }
}

static void draw_blit(robo_gfx_surface_t *dst, int x0, int y0)
{
    robo_gfx_surface_t *tile = make_owned(64, 64, ROBO_GFX_FORMAT_BGRA8);
    int x, y;
    caption(dst, x0, y0, "copy_rect  tile blit + same-surface overlap", C_MUTED);
    if (!tile)
        return;
    (void)robo_gfx_surface_clear(tile, C_CARD);
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            if (((x ^ y) & 7) == 0)
                (void)robo_gfx_surface_set_pixel(
                    tile, (uint32_t)x, (uint32_t)y, C_BRAND);
            if (x > 16 && x < 48 && y > 16 && y < 48)
                (void)robo_gfx_surface_set_pixel(
                    tile, (uint32_t)x, (uint32_t)y, C_OK);
        }
    }
    (void)robo_gfx_surface_fill_rect(tile, robo_gfx_rect(24, 24, 16, 16),
                                     C_DANGER);

    (void)robo_gfx_surface_copy_rect(tile, robo_gfx_rect(0, 0, 64, 64), dst,
                                     robo_gfx_point(x0, y0 + 16));
    (void)robo_gfx_surface_copy_rect(tile, robo_gfx_rect(0, 0, 64, 64), dst,
                                     robo_gfx_point(x0 + 72, y0 + 16));
    (void)robo_gfx_surface_copy_rect(tile, robo_gfx_rect(16, 16, 32, 32), dst,
                                     robo_gfx_point(x0 + 152, y0 + 24));

    /* Overlap-safe same-surface shift */
    (void)robo_gfx_surface_copy_rect(
        dst, robo_gfx_rect(x0, y0 + 16, 64, 64), dst,
        robo_gfx_point(x0 + 220, y0 + 16));
    (void)robo_gfx_surface_copy_rect(
        dst, robo_gfx_rect(x0 + 220, y0 + 16, 48, 48), dst,
        robo_gfx_point(x0 + 236, y0 + 32));

    caption(dst, x0, y0 + 88, "src   src   crop   overlap shift", C_MUTED);
    robo_gfx_surface_release(tile);
}

static void draw_map_demo(robo_gfx_surface_t *dst, int x0, int y0)
{
    robo_gfx_surface_t *tmp = make_owned(180, 40, ROBO_GFX_FORMAT_BGRA8);
    robo_gfx_surface_map_t map;
    uint32_t x, y;
    caption(dst, x0, y0, "surface map/unmap  direct row write", C_MUTED);
    if (!tmp)
        return;
    if (robo_gfx_surface_map(tmp, ROBO_GFX_SURFACE_ACCESS_WRITE, &map) !=
        ROBO_OK) {
        robo_gfx_surface_release(tmp);
        return;
    }
    for (y = 0; y < map.height; y++) {
        uint8_t *row = map.pixels + (size_t)y * map.stride_bytes;
        for (x = 0; x < map.width; x++) {
            uint8_t *px = row + (size_t)x * 4u;
            px[0] = (uint8_t)(y * 4);           /* B */
            px[1] = (uint8_t)(x);               /* G */
            px[2] = (uint8_t)(255 - x);         /* R */
            px[3] = 255;                        /* A */
        }
    }
    (void)robo_gfx_surface_unmap(tmp);
    (void)robo_gfx_surface_copy_rect(tmp, robo_gfx_rect(0, 0, 180, 40), dst,
                                     robo_gfx_point(x0, y0 + 16));
    robo_gfx_surface_release(tmp);
}

static void paint_gallery(void)
{
    int m = 40;
    int y;

    if (!g_screen)
        return;

    (void)robo_gfx_surface_clear(g_screen, C_BG);
    (void)robo_gfx_surface_fill_rect(
        g_screen, robo_gfx_rect(0, 0, (int32_t)g_w, 6), C_BRAND);
    (void)robo_gfx_surface_fill_rect(
        g_screen,
        robo_gfx_rect(m, m, (int32_t)g_w - m * 2, (int32_t)g_h - m * 2),
        C_CARD);
    (void)robo_gfx_surface_fill_rect(
        g_screen, robo_gfx_rect(m, m, 6, (int32_t)g_h - m * 2), C_BRAND);

    caption(g_screen, m + 24, m + 20, "RoboGraphics", C_FG);
    caption(g_screen, m + 24, m + 36,
            "Live preview of Goals 6.1-6.5 on borrowed framebuffer", C_MUTED);

    y = m + 64;
    draw_swatches(g_screen, m + 24, y);
    y += 100;
    draw_pixels(g_screen, m + 24, y);
    y += 90;
    draw_formats(g_screen, m + 24, y);
    y += 210;
    if (y + 120 < (int)g_h - m) {
        draw_blit(g_screen, m + 24, y);
        y += 120;
    }
    if (y + 70 < (int)g_h - m)
        draw_map_demo(g_screen, m + 24, y);

    caption(g_screen, m + 24, (int)g_h - m - 28,
            "RoboFont rasterization ships with RoboText — captions are chrome only",
            C_GRAY);

    present();
}

int main(void)
{
    serial("robo-runtime: RoboGraphics visual preview\n");
    if (robo_graphics_init() != ROBO_OK) {
        serial("preview: robo_graphics_init failed\n");
        for (;;)
            robo_sleep(1000);
        return 1;
    }
    if (!claim_display()) {
        for (;;)
            robo_sleep(1000);
        return 1;
    }
    paint_gallery();
    serial("preview: gallery presented\n");
    for (;;)
        robo_sleep(1000);
    return 0;
}
