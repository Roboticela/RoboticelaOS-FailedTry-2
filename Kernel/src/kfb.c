#include "kio.h"
#include "time/time.h"

extern const uint8_t kfont8x16[256][16];

static uint32_t *gFb;
static uint32_t gW, gH, gPitch, gPixfmt;
static int gCx, gCy;
static int gMarginX, gMarginY, gLh;
static int gTerm;
static int gTx, gTy, gTw, gTh;
static int gRows;
static uint32_t gCard;
static int gE0;

#define KLOG_MAX    2048
#define KLOG_COLS   160
#define KLOG_TIME_W 56

static char gLines[KLOG_MAX][KLOG_COLS];
static uint32_t gLineRgb[KLOG_MAX];
static uint32_t gLineMs[KLOG_MAX];
static uint8_t gLineKind[KLOG_MAX];
static int gN;
static int gFollow = 1;
static int gTop;
static char gCur[KLOG_COLS];
static int gCurLen;
static uint32_t gCurRgb = 0xA1A1A6u;
static uint32_t gCurMs;
static uint8_t gCurKind;
static uint32_t gHandoffMs;
static uint64_t gTimerBaseMs;
static int gTimerArmed;

static int gSplash;
static int gBarX, gBarY, gBarW, gBarH;
static unsigned gProgress;

static void KfbSplashPaint(void);
static void KfbSplashInit(const RoboBootInfo *info);

static uint32_t PackRgb(uint32_t rgb)
{
    unsigned r = (rgb >> 16) & 255u;
    unsigned g = (rgb >> 8) & 255u;
    unsigned b = rgb & 255u;
    if (gPixfmt == RoboPixRgbx)
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

static char AsciiLower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int LogHas(const char *s, const char *n)
{
    int i, j;
    if (!s || !n || !n[0])
        return 0;
    for (i = 0; s[i]; i++) {
        for (j = 0; n[j] && s[i + j] && AsciiLower(s[i + j]) == AsciiLower(n[j]); j++) { }
        if (!n[j])
            return 1;
    }
    return 0;
}

static uint8_t KlogKind(const char *line)
{
    if (LogHas(line, "fail") || LogHas(line, "missing") ||
        LogHas(line, "not saved") || LogHas(line, "skip") ||
        LogHas(line, "panic") || LogHas(line, "error"))
        return 2;
    if (LogHas(line, "ready") || LogHas(line, "loaded") ||
        LogHas(line, "detected") || LogHas(line, "mapped") ||
        LogHas(line, "ok") || LogHas(line, "pass") ||
        LogHas(line, "found") || LogHas(line, "copied") ||
        LogHas(line, "jump"))
        return 1;
    return 0;
}

static uint32_t KlogNowMs(void)
{
    if (TimerReady()) {
        uint64_t now = TimeMs();
        uint64_t t;
        if (!gTimerArmed) {
            gTimerBaseMs = now;
            gTimerArmed = 1;
        }
        t = (uint64_t)gHandoffMs + (now - gTimerBaseMs);
        if (t > 0xffffffffull)
            t = 0xffffffffull;
        return (uint32_t)t;
    }
    return gHandoffMs;
}

static void FmtLogTime(char *buf, int cap, uint32_t ms, int withS)
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
    if (withS && o + 1 < cap)
        buf[o++] = 's';
    buf[o] = 0;
}

static uint32_t KindInk(uint8_t kind)
{
    if (kind == 1) return 0xFFFFFFu;
    if (kind == 2) return 0xFF9F0Au;
    return 0xA1A1A6u;
}

static uint32_t KindDot(uint8_t kind)
{
    if (kind == 1) return 0x3B9EFFu;
    if (kind == 2) return 0xFF9F0Au;
    return 0x48484Au;
}

static int KlogPushLine(const char *s, int n, uint32_t rgb, uint32_t ms, uint8_t kind)
{
    int i;
    if (gN >= KLOG_MAX)
        return 0;
    if (n < 0) n = 0;
    if (n > KLOG_COLS - 1) n = KLOG_COLS - 1;
    for (i = 0; i < n; i++)
        gLines[gN][i] = s[i];
    gLines[gN][n] = 0;
    gLineRgb[gN] = rgb ? rgb : KindInk(kind);
    gLineMs[gN] = ms;
    gLineKind[gN] = kind;
    gN++;
    return 1;
}

static unsigned KfbImportBootlog(const RoboBootInfo *info)
{
    const uint8_t *p;
    uint32_t used, count, off, got = 0;
    if (!info || info->version < 3 || !info->debugLogAddress || !info->debugLogSize)
        return 0;
    p = (const uint8_t *)(uintptr_t)info->debugLogAddress;
    if (rd32(p) != RoboDebugLogMagic)
        return 0;
    count = rd32(p + 8);
    used = rd32(p + 12);
    if (used < 16u || used > info->debugLogSize)
        used = info->debugLogSize;
    off = 16;
    while (off + 8u <= used && got < count && gN < KLOG_MAX) {
        uint32_t ms = rd32(p + off);
        uint8_t kind = p[off + 4];
        uint16_t len = rd16(p + off + 6);
        if (off + 8u + len > used)
            break;
        KlogPushLine((const char *)(p + off + 8), (int)len, KindInk(kind), ms, kind);
        if (ms > gHandoffMs)
            gHandoffMs = ms;
        off += 8u + len;
        got++;
    }
    return got;
}

static void KfbScrollbar(int rows, int vis, int start)
{
    int trackX, trackY, trackH, thumbH, thumbY, maxStart;
    if (!gTerm || rows <= vis || vis < 1)
        return;
    maxStart = rows - vis;
    if (maxStart < 1)
        return;
    if (start < 0) start = 0;
    if (start > maxStart) start = maxStart;
    trackX = gTx + gTw - 14;
    trackY = gTy + 48;
    trackH = gTh - 64;
    if (trackH < 12)
        return;
    thumbH = (int)(((long long)trackH * vis) / rows);
    if (thumbH < 10) thumbH = 10;
    if (thumbH > trackH) thumbH = trackH;
    thumbY = trackY + (int)(((long long)(trackH - thumbH) * start) / maxStart);
    KfbRoundRect(trackX, trackY, 3, trackH, 2, 0x2C2C2Eu);
    KfbRoundRect(trackX, thumbY, 3, thumbH, 2, 0x3B9EFFu);
}

void KfbInit(const RoboBootInfo *info)
{
    int debug;
    gFb = 0;
    gCx = gCy = 0;
    gMarginX = 40;
    gMarginY = 48;
    gLh = 22;
    gTerm = 0;
    gRows = 0;
    if (!info || !info->framebufferAddress || info->framebufferBpp != 32)
        return;
    gFb = (uint32_t *)(uintptr_t)info->framebufferAddress;
    gW = info->framebufferWidth;
    gH = info->framebufferHeight;
    gPitch = info->framebufferPitch / 4u;
    if (gPitch < gW) gPitch = gW;
    gPixfmt = info->framebufferPixfmt;
    debug = KbootDebug(info) && info->debugTermW && info->debugTermH;
    if (debug) {
        KfbImportBootlog(info);
        KfbTermOpen((int)info->debugTermX, (int)info->debugTermY,
                      (int)info->debugTermW, (int)info->debugTermH);
    }
    KfbSplashInit(info);
}

void KfbFill(int x, int y, int w, int h, uint32_t rgb)
{
    uint32_t p = PackRgb(rgb);
    int yy, xx, x1, y1;
    if (!gFb || w <= 0 || h <= 0) return;
    x1 = x + w;
    y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int)gW) x1 = (int)gW;
    if (y1 > (int)gH) y1 = (int)gH;
    for (yy = y; yy < y1; yy++) {
        uint32_t *row = gFb + (uint32_t)yy * gPitch;
        for (xx = x; xx < x1; xx++) row[xx] = p;
    }
}

static int rdist2(int dx, int dy)
{
    return dx * dx + dy * dy;
}

void KfbRoundRect(int x, int y, int w, int h, int r, uint32_t rgb)
{
    uint32_t p = PackRgb(rgb);
    int yy, xx, x1, y1, rr;
    if (!gFb || w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    rr = r * r;
    x1 = x + w;
    y1 = y + h;
    for (yy = y; yy < y1; yy++) {
        if (yy < 0 || yy >= (int)gH) continue;
        for (xx = x; xx < x1; xx++) {
            int dx = 0, dy = 0, inside = 1;
            if (xx < 0 || xx >= (int)gW) continue;
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
                gFb[(uint32_t)yy * gPitch + (uint32_t)xx] = p;
        }
    }
}

static void KfbCircleFill(int cx, int cy, int r, uint32_t rgb)
{
    uint32_t p = PackRgb(rgb);
    int x, y, r2;
    if (!gFb || r < 1)
        return;
    r2 = r * r;
    for (y = -r; y <= r; y++) {
        int py = cy + y;
        if ((unsigned)py >= gH)
            continue;
        for (x = -r; x <= r; x++) {
            int px = cx + x;
            if ((unsigned)px >= gW)
                continue;
            if (x * x + y * y <= r2)
                gFb[(uint32_t)py * gPitch + (uint32_t)px] = p;
        }
    }
}

static void KfbCircleRing(int cx, int cy, int r, int t, uint32_t rgb)
{
    uint32_t p = PackRgb(rgb);
    int x, y, outer, inner;
    if (!gFb || r < 1)
        return;
    if (t < 1) t = 1;
    outer = r * r;
    inner = (r - t) * (r - t);
    if (inner < 0)
        inner = 0;
    for (y = -r; y <= r; y++) {
        int py = cy + y;
        if ((unsigned)py >= gH)
            continue;
        for (x = -r; x <= r; x++) {
            int px = cx + x;
            int d2;
            if ((unsigned)px >= gW)
                continue;
            d2 = x * x + y * y;
            if (d2 <= outer && d2 >= inner)
                gFb[(uint32_t)py * gPitch + (uint32_t)px] = p;
        }
    }
}

void KfbClear(uint32_t rgb)
{
    if (gTerm || gSplash)
        return;
    KfbFill(0, 0, (int)gW, (int)gH, rgb);
    gCx = 0;
    gCy = 0;
    gMarginX = 40;
    gMarginY = 48;
}

static void KfbSplashPaint(void)
{
    int fill, rad;
    if (!gSplash || !gFb || gBarW < 8 || gBarH < 2)
        return;
    fill = (int)(((long long)gBarW * (long long)gProgress) / (long long)RoboBootProgressMax);
    if (gProgress > 0 && fill < gBarH)
        fill = gBarH;
    if (fill > gBarW)
        fill = gBarW;
    rad = gBarH / 2;
    if (rad < 1)
        rad = 1;
    KfbFill(gBarX - 2, gBarY - 2, gBarW + 4, gBarH + 4, 0x000000u);
    KfbRoundRect(gBarX, gBarY, gBarW, gBarH, rad, 0x38383Au);
    if (fill > 0)
        KfbRoundRect(gBarX, gBarY, fill, gBarH, rad, 0x3B9EFFu);
}

static void KfbSplashInit(const RoboBootInfo *info)
{
    int compact, logoH, reserve, area, stack, top;
    gSplash = 0;
    gProgress = 0;
    gBarX = gBarY = gBarW = gBarH = 0;
    if (!gFb || !info)
        return;
    if (info->version >= 4 && info->splashBarW && info->splashBarH) {
        gBarX = (int)info->splashBarX;
        gBarY = (int)info->splashBarY;
        gBarW = (int)info->splashBarW;
        gBarH = (int)info->splashBarH;
        gProgress = info->splashProgress;
    } else {
        compact = (int)info->framebufferWidth <= 734;
        logoH = compact ? 128 : 256;
        gBarW = compact ? 240 : 280;
        gBarH = 4;
        reserve = 0;
        if (KbootDebug(info) && info->debugTermH) {
            reserve = (int)info->framebufferHeight - (int)info->debugTermY + 16;
            if (reserve < 0)
                reserve = 0;
        }
        stack = logoH + 64 + gBarH;
        area = (int)info->framebufferHeight - reserve;
        top = area / 2 - stack / 2;
        if (top < 24)
            top = 24;
        gBarX = (int)info->framebufferWidth / 2 - gBarW / 2;
        gBarY = top + logoH + 64;
        gProgress = RoboBootProgressHandoff;
    }
    if (gBarW < 8 || gBarH < 2)
        return;
    if (gProgress > RoboBootProgressMax)
        gProgress = RoboBootProgressMax;
    if (info->version >= 4 && info->splashHandoff &&
        gProgress > info->splashHandoff && info->splashHandoff < RoboBootProgressMax)
        gProgress = info->splashHandoff;
    gSplash = 1;
    KfbSplashPaint();
}

void KfbSplashSet(unsigned progress)
{
    if (!gSplash)
        return;
    if (progress > RoboBootProgressMax)
        progress = RoboBootProgressMax;
    if (progress <= gProgress)
        return;
    gProgress = progress;
    KfbSplashPaint();
}

static void glyph(int gx, int gy, unsigned char ch, uint32_t rgb)
{
    const uint8_t *rows;
    uint32_t p = PackRgb(rgb);
    int y, x;
    if (!gFb) return;
    rows = kfont8x16[ch];
    for (y = 0; y < 16; y++) {
        uint8_t bits = rows[y];
        int py = gy + y;
        if ((unsigned)py >= gH) continue;
        for (x = 0; x < 8; x++) {
            int px = gx + x;
            if ((unsigned)px >= gW) continue;
            if (bits & (0x80 >> x))
                gFb[(uint32_t)py * gPitch + (uint32_t)px] = p;
        }
    }
}

static int KlogCols(void)
{
    int cols = gTerm ? (gTw - 36 - KLOG_TIME_W - 28) / 9 : 80;
    if (cols < 8) cols = 8;
    if (cols > KLOG_COLS - 1) cols = KLOG_COLS - 1;
    return cols;
}

static int KlogLineRows(const char *s, int cols)
{
    int n = 0;
    if (!s) return 1;
    while (s[n]) n++;
    if (n < 1) return 1;
    return (n + cols - 1) / cols;
}

static int KlogTotalRows(void)
{
    int i, cols = KlogCols(), rows = 0;
    for (i = 0; i < gN; i++)
        rows += KlogLineRows(gLines[i], cols);
    if (gCurLen)
        rows += KlogLineRows(gCur, cols);
    return rows;
}

static void KlogCommit(void)
{
    int i;
    if (gN >= KLOG_MAX) {
        gCurLen = 0;
        return;
    }
    for (i = 0; i < gCurLen && i < KLOG_COLS - 1; i++)
        gLines[gN][i] = gCur[i];
    gLines[gN][i] = 0;
    gCurKind = KlogKind(gLines[gN]);
    gLineKind[gN] = gCurKind;
    gLineMs[gN] = gCurMs ? gCurMs : KlogNowMs();
    gLineRgb[gN] = KindInk(gCurKind);
    gN++;
    gCurLen = 0;
    gCurRgb = 0xA1A1A6u;
    gCurMs = 0;
    gCurKind = 0;
}

static void KfbPutStrAt(int x, int y, const char *s, uint32_t rgb)
{
    while (s && *s) {
        glyph(x, y, (unsigned char)*s++, rgb);
        x += 9;
    }
}

static int KfbStrWidth(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n * 9;
}

static void KfbTermPaint(void)
{
    int r = 18;
    int cols, vis, rows, maxStart, start, row, i, drawn;
    int py;
    char elapsed[24];
    char tbuf[16];
    const char *title = "Logs";
    if (!gFb || !gTerm) return;
    KfbRoundRect(gTx, gTy, gTw, gTh, r, gCard);
    KfbPutStrAt(gTx + 24, gTy + 16, title, 0x98989Du);
    FmtLogTime(elapsed, (int)sizeof(elapsed), KlogNowMs(), 1);
    KfbPutStrAt(gTx + gTw - 24 - KfbStrWidth(elapsed), gTy + 16,
                   elapsed, 0x98989Du);
    KfbFill(gTx + 24, gTy + 38, gTw - 48, 1, 0x2C2C2Eu);
    cols = KlogCols();
    vis = gRows;
    rows = KlogTotalRows();
    maxStart = rows > vis ? rows - vis : 0;
    start = gFollow ? maxStart : gTop;
    if (start < 0) start = 0;
    if (start > maxStart) start = maxStart;
    gTop = start;
    if (start >= maxStart) gFollow = 1;
    row = 0;
    drawn = 0;
    py = gMarginY;
    for (i = 0; i <= gN && drawn < vis; i++) {
        const char *s;
        uint8_t kind;
        uint32_t ms, ink, dot;
        int n, off, cont;
        if (i < gN) {
            s = gLines[i];
            kind = gLineKind[i];
            ms = gLineMs[i];
        } else if (gCurLen) {
            gCur[gCurLen] = 0;
            s = gCur;
            kind = KlogKind(gCur);
            ms = gCurMs ? gCurMs : KlogNowMs();
        } else
            break;
        ink = KindInk(kind);
        dot = KindDot(kind);
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
                    KfbCircleFill(gTx + 28, py + 6, 3, dot);
                else
                    KfbCircleRing(gTx + 28, py + 6, 3, 1, dot);
                FmtLogTime(tbuf, (int)sizeof(tbuf), ms, 0);
                KfbPutStrAt(gTx + 36, py, tbuf, 0x98989Du);
            }
            gx = gTx + 36 + KLOG_TIME_W;
            for (k = 0; k < take; k++) {
                glyph(gx, py, (unsigned char)s[off + k], ink);
                gx += 9;
            }
            py += gLh;
            drawn++;
            cont = 1;
            if (n < 1) break;
            off += take > 0 ? take : 1;
            if (drawn >= vis) break;
        } while (off < n);
    }
    KfbScrollbar(rows, vis, start);
}

static void KlogScroll(int delta)
{
    int vis, rows, maxStart, start;
    if (!gTerm || !delta) return;
    vis = gRows < 1 ? 1 : gRows;
    rows = KlogTotalRows();
    maxStart = rows > vis ? rows - vis : 0;
    start = gFollow ? maxStart : gTop;
    start += delta;
    if (start < 0) start = 0;
    if (start > maxStart) start = maxStart;
    gTop = start;
    gFollow = start >= maxStart;
    KfbTermPaint();
}

void KfbTermOpen(int x, int y, int w, int h)
{
    if (w < 80) w = 80;
    if (h < 80) h = 80;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > (int)gW) w = (int)gW - x;
    if (y + h > (int)gH) h = (int)gH - y;
    gTerm = 1;
    gTx = x;
    gTy = y;
    gTw = w;
    gTh = h;
    gCard = 0x1C1C1Eu;
    gMarginX = x + 24;
    gMarginY = y + 48;
    gLh = 18;
    gRows = (h - 56) / gLh;
    if (gRows < 1) gRows = 1;
    gCx = 0;
    gCy = 0;
    gFollow = 1;
    KfbTermPaint();
}

int KfbTermActive(void)
{
    return gTerm;
}

void KfbLogPutc(char c, uint32_t rgb)
{
    (void)rgb;
    if (c == '\r')
        return;
    if (c == '\n') {
        KlogCommit();
        if (gTerm && gFollow)
            KfbTermPaint();
        return;
    }
    if (gCurLen >= KLOG_COLS - 1)
        KlogCommit();
    if (gCurLen == 0) {
        gCurMs = KlogNowMs();
        gCurRgb = 0xA1A1A6u;
    }
    gCur[gCurLen++] = c;
}

static inline uint8_t KInb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void KfbPoll(void)
{
    uint8_t st, sc;
    int n = 0;
    while (n++ < 16) {
        st = KInb(0x64);
        if (!(st & 1u))
            break;
        sc = KInb(0x60);
        if (sc == 0xE0u) {
            gE0 = 1;
            continue;
        }
        if (sc & 0x80u) {
            gE0 = 0;
            continue;
        }
        if (sc == 0x48u)
            KlogScroll(-1);
        else if (sc == 0x50u)
            KlogScroll(1);
        gE0 = 0;
    }
}

void KfbNewline(void)
{
    if (gTerm) {
        KfbLogPutc('\n', gCurRgb);
        return;
    }
    gCx = 0;
    gCy++;
    if (gMarginY + (gCy + 1) * gLh > (int)gH - 24) {
        gCy = 0;
        KfbClear(0x000000);
    }
}

void KfbPuts(const char *s, uint32_t rgb)
{
    int maxCols;
    if (!s) return;
    if (gTerm) {
        while (*s)
            KfbLogPutc(*s++, rgb);
        return;
    }
    maxCols = ((int)gW - gMarginX * 2) / 9;
    if (maxCols < 8) maxCols = 8;
    while (*s) {
        char c = *s++;
        if (c == '\n') {
            KfbNewline();
            continue;
        }
        glyph(gMarginX + gCx * 9, gMarginY + gCy * gLh, (unsigned char)c, rgb);
        gCx++;
        if (gCx + 1 > maxCols)
            KfbNewline();
    }
}

void KfbPutu(unsigned long long v, uint32_t rgb)
{
    char tmp[24];
    int n = 0;
    if (v == 0) {
        KfbPuts("0", rgb);
        return;
    }
    while (v && n < 24) {
        tmp[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    while (n--) {
        char b[2] = { tmp[n], 0 };
        KfbPuts(b, rgb);
    }
}

void KfbPuthex(unsigned long long v, uint32_t rgb)
{
    const char *hex = "0123456789abcdef";
    char buf[19];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; i++)
        buf[2 + i] = hex[(v >> (60 - i * 4)) & 15ull];
    buf[18] = 0;
    KfbPuts(buf, rgb);
}
