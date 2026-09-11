#include "kio.h"
#include "time/time.h"

extern const uint8_t kfont8x16[256][16];

static uint32_t *g_fb;
static uint32_t g_w, g_h, g_pitch, g_pixfmt;
static int g_cx, g_cy;
static int g_margin_x, g_margin_y, g_lh;
static int g_term;
static int g_tx, g_ty, g_tw, g_th;
static int g_rows;
static uint32_t g_card;
static int g_e0;

#define KLOG_MAX    2048
#define KLOG_COLS   160
#define KLOG_TIME_W 56

static char g_lines[KLOG_MAX][KLOG_COLS];
static uint32_t g_line_rgb[KLOG_MAX];
static uint32_t g_line_ms[KLOG_MAX];
static uint8_t g_line_kind[KLOG_MAX];
static int g_n;
static int g_follow = 1;
static int g_top;
static char g_cur[KLOG_COLS];
static int g_cur_len;
static uint32_t g_cur_rgb = 0xA1A1A6u;
static uint32_t g_cur_ms;
static uint8_t g_cur_kind;
static uint32_t g_handoff_ms;
static uint64_t g_timer_base_ms;
static int g_timer_armed;

static uint32_t pack_rgb(uint32_t rgb)
{
    unsigned r = (rgb >> 16) & 255u;
    unsigned g = (rgb >> 8) & 255u;
    unsigned b = rgb & 255u;
    if (g_pixfmt == ROBO_PIX_RGBX)
        return (r) | (g << 8) | (b << 16);
    return (b) | (g << 8) | (r << 16);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static char ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int log_has(const char *s, const char *n)
{
    int i, j;
    if (!s || !n || !n[0])
        return 0;
    for (i = 0; s[i]; i++) {
        for (j = 0; n[j] && s[i + j] && ascii_lower(s[i + j]) == ascii_lower(n[j]); j++) { }
        if (!n[j])
            return 1;
    }
    return 0;
}

static uint8_t klog_kind(const char *line)
{
    if (log_has(line, "fail") || log_has(line, "missing") ||
        log_has(line, "not saved") || log_has(line, "skip") ||
        log_has(line, "panic") || log_has(line, "error"))
        return 2;
    if (log_has(line, "ready") || log_has(line, "loaded") ||
        log_has(line, "detected") || log_has(line, "mapped") ||
        log_has(line, "ok") || log_has(line, "pass") ||
        log_has(line, "found") || log_has(line, "copied") ||
        log_has(line, "jump"))
        return 1;
    return 0;
}

static uint32_t klog_now_ms(void)
{
    if (robo_timer_ready()) {
        uint64_t now = robo_time_ms();
        uint64_t t;
        if (!g_timer_armed) {
            g_timer_base_ms = now;
            g_timer_armed = 1;
        }
        t = (uint64_t)g_handoff_ms + (now - g_timer_base_ms);
        if (t > 0xffffffffull)
            t = 0xffffffffull;
        return (uint32_t)t;
    }
    return g_handoff_ms;
}

static void fmt_log_time(char *buf, int cap, uint32_t ms, int with_s)
{
    unsigned sec = ms / 1000u;
    unsigned cs = (ms % 1000u) / 10u;
    char tmp[16];
    int n = 0, o = 0;
    if (!buf || cap < 2)
        return;
    if (sec == 0)
        tmp[n++] = '0';
    else {
        while (sec && n < 10) {
            tmp[n++] = (char)('0' + (sec % 10u));
            sec /= 10u;
        }
    }
    while (n-- && o + 1 < cap)
        buf[o++] = tmp[n];
    if (o + 1 < cap)
        buf[o++] = '.';
    if (o + 1 < cap)
        buf[o++] = (char)('0' + (cs / 10u));
    if (o + 1 < cap)
        buf[o++] = (char)('0' + (cs % 10u));
    if (with_s && o + 1 < cap)
        buf[o++] = 's';
    buf[o] = 0;
}

static uint32_t kind_ink(uint8_t kind)
{
    if (kind == 1) return 0xFFFFFFu;
    if (kind == 2) return 0xFF9F0Au;
    return 0xA1A1A6u;
}

static uint32_t kind_dot(uint8_t kind)
{
    if (kind == 1) return 0x3B9EFFu;
    if (kind == 2) return 0xFF9F0Au;
    return 0x48484Au;
}

static int klog_push_line(const char *s, int n, uint32_t rgb, uint32_t ms, uint8_t kind)
{
    int i;
    if (g_n >= KLOG_MAX)
        return 0;
    if (n < 0) n = 0;
    if (n > KLOG_COLS - 1) n = KLOG_COLS - 1;
    for (i = 0; i < n; i++)
        g_lines[g_n][i] = s[i];
    g_lines[g_n][n] = 0;
    g_line_rgb[g_n] = rgb ? rgb : kind_ink(kind);
    g_line_ms[g_n] = ms;
    g_line_kind[g_n] = kind;
    g_n++;
    return 1;
}

static unsigned kfb_import_bootlog(const RoboBootInfo *info)
{
    const uint8_t *p;
    uint32_t used, count, off, got = 0;
    if (!info || info->version < 3 || !info->debug_log_address || !info->debug_log_size)
        return 0;
    p = (const uint8_t *)(uintptr_t)info->debug_log_address;
    if (rd32(p) != ROBO_DEBUG_LOG_MAGIC)
        return 0;
    count = rd32(p + 8);
    used = rd32(p + 12);
    if (used < 16u || used > info->debug_log_size)
        used = info->debug_log_size;
    off = 16;
    while (off + 8u <= used && got < count && g_n < KLOG_MAX) {
        uint32_t ms = rd32(p + off);
        uint8_t kind = p[off + 4];
        uint16_t len = rd16(p + off + 6);
        if (off + 8u + len > used)
            break;
        klog_push_line((const char *)(p + off + 8), (int)len, kind_ink(kind), ms, kind);
        if (ms > g_handoff_ms)
            g_handoff_ms = ms;
        off += 8u + len;
        got++;
    }
    return got;
}

static void kfb_scrollbar(int rows, int vis, int start)
{
    int track_x, track_y, track_h, thumb_h, thumb_y, max_start;
    if (!g_term || rows <= vis || vis < 1)
        return;
    max_start = rows - vis;
    if (max_start < 1)
        return;
    if (start < 0) start = 0;
    if (start > max_start) start = max_start;
    track_x = g_tx + g_tw - 14;
    track_y = g_ty + 48;
    track_h = g_th - 64;
    if (track_h < 12)
        return;
    thumb_h = (int)(((long long)track_h * vis) / rows);
    if (thumb_h < 10) thumb_h = 10;
    if (thumb_h > track_h) thumb_h = track_h;
    thumb_y = track_y + (int)(((long long)(track_h - thumb_h) * start) / max_start);
    kfb_round_rect(track_x, track_y, 3, track_h, 2, 0x2C2C2Eu);
    kfb_round_rect(track_x, thumb_y, 3, thumb_h, 2, 0x3B9EFFu);
}
void kfb_init(const RoboBootInfo *info)
{
    int debug;
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
    debug = kboot_debug(info) && info->debug_term_w && info->debug_term_h;
    if (debug) {
        kfb_import_bootlog(info);
        kfb_term_open((int)info->debug_term_x, (int)info->debug_term_y,
                      (int)info->debug_term_w, (int)info->debug_term_h);
        return;
    }
    kfb_clear(0x000000u);
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

static void kfb_circle_fill(int cx, int cy, int r, uint32_t rgb)
{
    uint32_t p = pack_rgb(rgb);
    int x, y, r2;
    if (!g_fb || r < 1)
        return;
    r2 = r * r;
    for (y = -r; y <= r; y++) {
        int py = cy + y;
        if ((unsigned)py >= g_h)
            continue;
        for (x = -r; x <= r; x++) {
            int px = cx + x;
            if ((unsigned)px >= g_w)
                continue;
            if (x * x + y * y <= r2)
                g_fb[(uint32_t)py * g_pitch + (uint32_t)px] = p;
        }
    }
}

static void kfb_circle_ring(int cx, int cy, int r, int t, uint32_t rgb)
{
    uint32_t p = pack_rgb(rgb);
    int x, y, outer, inner;
    if (!g_fb || r < 1)
        return;
    if (t < 1) t = 1;
    outer = r * r;
    inner = (r - t) * (r - t);
    if (inner < 0)
        inner = 0;
    for (y = -r; y <= r; y++) {
        int py = cy + y;
        if ((unsigned)py >= g_h)
            continue;
        for (x = -r; x <= r; x++) {
            int px = cx + x;
            int d2;
            if ((unsigned)px >= g_w)
                continue;
            d2 = x * x + y * y;
            if (d2 <= outer && d2 >= inner)
                g_fb[(uint32_t)py * g_pitch + (uint32_t)px] = p;
        }
    }
}

void kfb_clear(uint32_t rgb)
{
    if (g_term)
        return;
    kfb_fill(0, 0, (int)g_w, (int)g_h, rgb);
    g_cx = 0;
    g_cy = 0;
    g_margin_x = 40;
    g_margin_y = 48;
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

static int klog_cols(void)
{
    int cols = g_term ? (g_tw - 36 - KLOG_TIME_W - 28) / 9 : 80;
    if (cols < 8) cols = 8;
    if (cols > KLOG_COLS - 1) cols = KLOG_COLS - 1;
    return cols;
}

static int klog_line_rows(const char *s, int cols)
{
    int n = 0;
    if (!s) return 1;
    while (s[n]) n++;
    if (n < 1) return 1;
    return (n + cols - 1) / cols;
}

static int klog_total_rows(void)
{
    int i, cols = klog_cols(), rows = 0;
    for (i = 0; i < g_n; i++)
        rows += klog_line_rows(g_lines[i], cols);
    if (g_cur_len)
        rows += klog_line_rows(g_cur, cols);
    return rows;
}

static void klog_commit(void)
{
    int i;
    if (g_n >= KLOG_MAX) {
        g_cur_len = 0;
        return;
    }
    for (i = 0; i < g_cur_len && i < KLOG_COLS - 1; i++)
        g_lines[g_n][i] = g_cur[i];
    g_lines[g_n][i] = 0;
    g_cur_kind = klog_kind(g_lines[g_n]);
    g_line_kind[g_n] = g_cur_kind;
    g_line_ms[g_n] = g_cur_ms ? g_cur_ms : klog_now_ms();
    g_line_rgb[g_n] = kind_ink(g_cur_kind);
    g_n++;
    g_cur_len = 0;
    g_cur_rgb = 0xA1A1A6u;
    g_cur_ms = 0;
    g_cur_kind = 0;
}

static void kfb_put_str_at(int x, int y, const char *s, uint32_t rgb)
{
    while (s && *s) {
        glyph(x, y, (unsigned char)*s++, rgb);
        x += 9;
    }
}

static int kfb_str_width(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n * 9;
}

static void kfb_term_paint(void)
{
    int r = 18;
    int cols, vis, rows, max_start, start, row, i, drawn;
    int py;
    char elapsed[24];
    char tbuf[16];
    const char *title = "Logs";
    if (!g_fb || !g_term) return;
    kfb_round_rect(g_tx, g_ty, g_tw, g_th, r, g_card);
    kfb_put_str_at(g_tx + 24, g_ty + 16, title, 0x98989Du);
    fmt_log_time(elapsed, (int)sizeof(elapsed), klog_now_ms(), 1);
    kfb_put_str_at(g_tx + g_tw - 24 - kfb_str_width(elapsed), g_ty + 16,
                   elapsed, 0x98989Du);
    kfb_fill(g_tx + 24, g_ty + 38, g_tw - 48, 1, 0x2C2C2Eu);
    cols = klog_cols();
    vis = g_rows;
    rows = klog_total_rows();
    max_start = rows > vis ? rows - vis : 0;
    start = g_follow ? max_start : g_top;
    if (start < 0) start = 0;
    if (start > max_start) start = max_start;
    g_top = start;
    if (start >= max_start) g_follow = 1;
    row = 0;
    drawn = 0;
    py = g_margin_y;
    for (i = 0; i <= g_n && drawn < vis; i++) {
        const char *s;
        uint8_t kind;
        uint32_t ms, ink, dot;
        int n, off, cont;
        if (i < g_n) {
            s = g_lines[i];
            kind = g_line_kind[i];
            ms = g_line_ms[i];
        } else if (g_cur_len) {
            g_cur[g_cur_len] = 0;
            s = g_cur;
            kind = klog_kind(g_cur);
            ms = g_cur_ms ? g_cur_ms : klog_now_ms();
        } else
            break;
        ink = kind_ink(kind);
        dot = kind_dot(kind);
        n = 0;
        while (s[n]) n++;
        off = 0;
        cont = 0;
        do {
            int take = n - off;
            int gx, k;
            if (take < 1) take = 0;
            if (take > cols) take = cols;
            if (row++ < start) {
                if (n < 1) break;
                off += take > 0 ? take : 1;
                cont = 1;
                if (off >= n) break;
                continue;
            }
            if (!cont) {
                if (kind == 1 || kind == 2)
                    kfb_circle_fill(g_tx + 28, py + 6, 3, dot);
                else
                    kfb_circle_ring(g_tx + 28, py + 6, 3, 1, dot);
                fmt_log_time(tbuf, (int)sizeof(tbuf), ms, 0);
                kfb_put_str_at(g_tx + 36, py, tbuf, 0x98989Du);
            }
            gx = g_tx + 36 + KLOG_TIME_W;
            for (k = 0; k < take; k++) {
                glyph(gx, py, (unsigned char)s[off + k], ink);
                gx += 9;
            }
            py += g_lh;
            drawn++;
            cont = 1;
            if (n < 1) break;
            off += take > 0 ? take : 1;
            if (drawn >= vis) break;
        } while (off < n);
    }
    kfb_scrollbar(rows, vis, start);
}

static void klog_scroll(int delta)
{
    int vis, rows, max_start, start;
    if (!g_term || !delta) return;
    vis = g_rows < 1 ? 1 : g_rows;
    rows = klog_total_rows();
    max_start = rows > vis ? rows - vis : 0;
    start = g_follow ? max_start : g_top;
    start += delta;
    if (start < 0) start = 0;
    if (start > max_start) start = max_start;
    g_top = start;
    g_follow = start >= max_start;
    kfb_term_paint();
}

void kfb_term_open(int x, int y, int w, int h)
{
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
    g_margin_x = x + 24;
    g_margin_y = y + 48;
    g_lh = 18;
    g_rows = (h - 56) / g_lh;
    if (g_rows < 1) g_rows = 1;
    g_cx = 0;
    g_cy = 0;
    g_follow = 1;
    kfb_term_paint();
}

int kfb_term_active(void)
{
    return g_term;
}

void kfb_log_putc(char c, uint32_t rgb)
{
    (void)rgb;
    if (c == '\r')
        return;
    if (c == '\n') {
        klog_commit();
        if (g_term && g_follow)
            kfb_term_paint();
        return;
    }
    if (g_cur_len >= KLOG_COLS - 1)
        klog_commit();
    if (g_cur_len == 0) {
        g_cur_ms = klog_now_ms();
        g_cur_rgb = 0xA1A1A6u;
    }
    g_cur[g_cur_len++] = c;
}

static inline uint8_t k_inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void kfb_poll(void)
{
    uint8_t st, sc;
    int n = 0;
    while (n++ < 16) {
        st = k_inb(0x64);
        if (!(st & 1u))
            break;
        sc = k_inb(0x60);
        if (sc == 0xE0u) {
            g_e0 = 1;
            continue;
        }
        if (sc & 0x80u) {
            g_e0 = 0;
            continue;
        }
        if (sc == 0x48u)
            klog_scroll(-1);
        else if (sc == 0x50u)
            klog_scroll(1);
        g_e0 = 0;
    }
}

void kfb_newline(void)
{
    if (g_term) {
        kfb_log_putc('\n', g_cur_rgb);
        return;
    }
    g_cx = 0;
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
    if (g_term) {
        while (*s)
            kfb_log_putc(*s++, rgb);
        return;
    }
    max_cols = ((int)g_w - g_margin_x * 2) / 9;
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
