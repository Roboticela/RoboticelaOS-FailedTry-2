#include "roboboot.h"
#include "roboticela/bootinfo.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static Session *gBootlogSs;
static LogLine *gEarlyLog;
static int gEarlyN;
static int gEarlyCap;
static int gBootlogHeap = 1;

#define RbFrozenLog 32
static char gFrozenText[RbFrozenLog][256];
static int gFrozenN;

static uint8_t *gHoBuf;
static uint32_t gHoSize;
static uint32_t gHoUsed;
static uint32_t gHoCount;

static void HoWr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void HoWr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void HoSyncHeader(void)
{
    if (!gHoBuf || gHoSize < sizeof(RoboDebugLogHeader))
        return;
    HoWr32(gHoBuf + 0, RoboDebugLogMagic);
    HoWr32(gHoBuf + 4, RoboDebugLogVersion);
    HoWr32(gHoBuf + 8, gHoCount);
    HoWr32(gHoBuf + 12, gHoUsed);
}

static int HoPut(uint32_t ms, uint8_t kind, const char *text)
{
    uint32_t n = 0, need;
    uint8_t *p;
    if (!gHoBuf || gHoUsed < sizeof(RoboDebugLogHeader))
        return 0;
    if (!text) text = "";
    while (text[n]) n++;
    if (n > 1024u) n = 1024u;
    need = 8u + n;
    if (gHoUsed + need > gHoSize)
        return 0;
    p = gHoBuf + gHoUsed;
    HoWr32(p + 0, ms);
    p[4] = kind;
    p[5] = 0;
    HoWr16(p + 6, (uint16_t)n);
    if (n)
        memcpy(p + 8, text, n);
    gHoUsed += need;
    gHoCount++;
    HoSyncHeader();
    return 1;
}

static int VisibleCount(const Catalog *c)
{
    int i, n = 0;
    for (i = 0; i < c->ntargets; i++)
        if (c->targets[i].present && !c->targets[i].hidden) n++;
    return n;
}

static int NthVisible(const Catalog *c, int n)
{
    int i;
    for (i = 0; i < c->ntargets; i++) {
        if (c->targets[i].present && !c->targets[i].hidden) {
            if (n == 0) return i;
            n--;
        }
    }
    return 0;
}

static int VisibleRank(const Catalog *c, int idx)
{
    int i, n = 0;
    for (i = 0; i < c->ntargets; i++) {
        if (c->targets[i].present && !c->targets[i].hidden) {
            if (i == idx) return n;
            n++;
        }
    }
    return 0;
}

static const Font *UiCaption(const Session *ss)
{
    return ss->fontCaption.atlas ? &ss->fontCaption : &ss->fontBody;
}

static const Font *UiBody(const Session *ss)
{
    return ss->fontBody.atlas ? &ss->fontBody : UiCaption(ss);
}

static int UiCompact(const Session *ss)
{
    return ss->fb.w <= 734;
}

static float UiEdge(const Session *ss)
{
    if (ss->fb.w <= 734) return 22.f;
    if (ss->fb.w <= 1068) return 40.f;
    {
        float p = ss->fb.w * 0.05f;
        return p > 80.f ? 80.f : p;
    }
}

static float UiPicGap(const Session *ss)
{
    return UiCompact(ss) ? 16.f : 32.f;
}

static float UiPicHBase(const Session *ss)
{
    return UiCompact(ss) ? 112.f : 200.f;
}

static const float UI_HALO_PAD = 16.f;

static const Font *UiLabelFont(const Session *ss)
{
    return UiCompact(ss) ? UiCaption(ss) : &ss->fontBody;
}

static float UiColW(const Session *ss, int visIndex, float picW)
{
    int idx = NthVisible(&ss->catalog, visIndex);
    int tw = GfxTextWidth(UiLabelFont(ss), ss->catalog.targets[idx].title) + 12;
    return picW > (float)tw ? picW : (float)tw;
}

static void UiPicWh(const Image *im, float ih, float *iw, float *oh)
{
    float aspect = 1.f;
    if (im && im->h > 0) aspect = (float)im->w / (float)im->h;
    *oh = ih;
    *iw = ih * aspect;
}

static int UiDebugOn(const Session *ss)
{
    return ss && RoboRbDebugEnabled(&ss->env);
}

void DebugTermGeom(const Session *ss, float *x, float *y, float *w, float *h)
{
    float pad, th;
    if (!ss) return;
    pad = UiEdge(ss);
    th = UiCompact(ss) ? 188.f : 280.f;
    if (th > (float)ss->fb.h * 0.42f) th = (float)ss->fb.h * 0.42f;
    if (th < 120.f) th = 120.f;
    if (x) *x = pad;
    if (w) *w = (float)ss->fb.w - pad * 2.f;
    if (h) *h = th;
    if (y) *y = (float)ss->fb.h - pad - th;
}

static float UiDebugReserve(const Session *ss)
{
    float x, y, w, h;
    if (!UiDebugOn(ss)) return 0.f;
    DebugTermGeom(ss, &x, &y, &w, &h);
    return ((float)ss->fb.h - y) + 16.f;
}

/*
 * Splash logo size from the display, never a fixed pixel height.
 * Scales with the shorter screen edge, clamps to width/height fractions so the
 * mark fits with the progress bar, and keeps the image aspect ratio exact.
 * Never upscales past the source bitmap (baked logo stays sharp).
 */
static void SplashLogoSize(const Session *ss, int srcW, int srcH,
                           int *outW, int *outH)
{
    float aspect;
    float shortEdge, target, tw, th, maxW, maxH, usableH, scale;

    if (!outW || !outH)
        return;
    *outW = 256;
    *outH = 256;
    if (!ss || srcW < 1 || srcH < 1 || ss->fb.w < 1 || ss->fb.h < 1)
        return;

    aspect = (float)srcW / (float)srcH;
    usableH = (float)ss->fb.h - UiDebugReserve(ss);
    if (usableH < 120.f)
        usableH = (float)ss->fb.h;

    shortEdge = (float)((ss->fb.w < ss->fb.h) ? ss->fb.w : ss->fb.h);
    target = shortEdge * 0.28f;
    if (target < 96.f)
        target = 96.f;
    if (target > 640.f)
        target = 640.f;

    if (aspect >= 1.f) {
        tw = target;
        th = tw / aspect;
    } else {
        th = target;
        tw = th * aspect;
    }

    maxW = (float)ss->fb.w * 0.42f;
    maxH = usableH * 0.30f;
    if (maxW < 64.f)
        maxW = 64.f;
    if (maxH < 64.f)
        maxH = 64.f;

    if (tw > maxW) {
        scale = maxW / tw;
        tw = maxW;
        th *= scale;
    }
    if (th > maxH) {
        scale = maxH / th;
        th = maxH;
        tw *= scale;
    }

    if (tw > (float)srcW) {
        scale = (float)srcW / tw;
        tw = (float)srcW;
        th *= scale;
    }
    if (th > (float)srcH) {
        scale = (float)srcH / th;
        th = (float)srcH;
        tw *= scale;
    }

    *outW = (int)(tw + 0.5f);
    *outH = (int)(th + 0.5f);
    if (*outW < 1)
        *outW = 1;
    if (*outH < 1)
        *outH = 1;
}

static void FitSplashLogo(Session *ss)
{
    int lh, lw;
    Image scaled;
    if (!ss || ss->logoFitted || !ss->logo.px || ss->logo.h < 1) {
        if (ss) ss->logoFitted = 1;
        return;
    }
    SplashLogoSize(ss, ss->logo.w, ss->logo.h, &lw, &lh);
    if (ss->logo.w == lw && ss->logo.h == lh) {
        ss->logoFitted = 1;
        return;
    }
    Zero(&scaled, sizeof(scaled));
    if (ImageResize(&ss->logo, lw, lh, &scaled) == RbOk) {
        ImageFree(&ss->logo);
        ss->logo = scaled;
    }
    ss->logoFitted = 1;
}

void SplashDraw(Session *ss, float visual, float fade)
{
    float cx, lw, lh, lx, ly, barW, barH, barX, barY, fill, stack, top, gap;
    int bx, by, bw, bh;
    uint32_t bg, track, brand;
    if (!ss) return;
    (void)fade;
    visual = Clampf(visual, 0.f, 1.f);
    FitSplashLogo(ss);
    cx = ss->fb.w * 0.5f;
    if (ss->logo.px && ss->logo.h > 0) {
        lw = (float)ss->logo.w;
        lh = (float)ss->logo.h;
    } else {
        int dw, dh;
        SplashLogoSize(ss, 1, 1, &dw, &dh);
        lw = (float)dw;
        lh = (float)dh;
    }
    barW = (float)ss->fb.w * 0.22f;
    if (barW < 160.f) barW = 160.f;
    if (barW > 320.f) barW = 320.f;
    if (barW > lw * 1.15f && lw > 80.f)
        barW = lw * 1.15f;
    barH = 4.f;
    gap = lh * 0.22f;
    if (gap < 32.f) gap = 32.f;
    if (gap > 72.f) gap = 72.f;
    stack = lh + gap + barH;
    {
        float area = (float)ss->fb.h - UiDebugReserve(ss);
        top = area * 0.5f - stack * 0.5f;
        if (top < 24.f) top = 24.f;
    }
    lx = cx - lw * 0.5f;
    ly = top;
    barX = cx - barW * 0.5f;
    barY = ly + lh + gap;
    bx = (int)barX;
    by = (int)barY;
    bw = (int)barW;
    bh = (int)barH;
    if (bh < 4) bh = 4;
    ss->splashBarX = bx;
    ss->splashBarY = by;
    ss->splashBarW = bw;
    ss->splashBarH = bh;
    fill = barW * visual;
    if (fill > 0.f && fill < barH) fill = barH;
    bg = Token(RbTokenBgPrimary, 255);
    track = Token(RbTokenBorderDefault, 255);
    brand = Token(RbTokenBrandPrimary, 255);

    if (!ss->splashBgReady) {
        GfxClear(&ss->fb, bg);
        if (ss->logo.px)
            GfxBlit(&ss->fb, &ss->logo, lx, ly, lw, lh, 1.f);
        else
            GfxRoundRect(&ss->fb, lx, ly, lw, lh, 18.f, brand);
        GfxFillBox(&ss->fb, bx - 2, by - 2, bw + 4, bh + 4, bg);
        GfxRoundRect(&ss->fb, barX, barY, barW, barH, barH * 0.5f, track);
        if (fill > 0.f)
            GfxRoundRect(&ss->fb, barX, barY, fill, barH, barH * 0.5f, brand);
        if (UiDebugOn(ss))
            DebugTermDraw(ss);
        ss->splashBgReady = 1;
        ss->fb.damageW = 0;
        ss->fb.damageH = 0;
        return;
    }

    GfxFillBox(&ss->fb, bx - 2, by - 2, bw + 4, bh + 4, bg);
    GfxRoundRect(&ss->fb, barX, barY, barW, barH, barH * 0.5f, track);
    if (fill > 0.f)
        GfxRoundRect(&ss->fb, barX, barY, fill, barH, barH * 0.5f, brand);
    if (UiDebugOn(ss) && ss->bootLogSeq != ss->bootLogShown) {
        DebugTermDraw(ss);
        ss->fb.damageW = 0;
        ss->fb.damageH = 0;
        return;
    }
    ss->fb.damageX = bx - 4;
    ss->fb.damageY = by - 4;
    ss->fb.damageW = bw + 8;
    ss->fb.damageH = bh + 8;
}

static void IconLayout(const Session *ss, int visIndex, float *ox, float *oy, float *iw, float *ih)
{
    int n = VisibleCount(&ss->catalog);
    int i, pass;
    float gap = UiPicGap(ss);
    float h = UiPicHBase(ss);
    float edge = UiEdge(ss);
    float picw[RbMaxTargets];
    float colw[RbMaxTargets];
    float total = 0.f;
    float avail;
    float x;
    if (n < 1) n = 1;
    if (n > RbMaxTargets) n = RbMaxTargets;
    avail = (float)ss->fb.w - edge * 2.f;
    if (avail < 64.f) avail = 64.f;
    for (pass = 0; pass < 8; pass++) {
        total = 0.f;
        for (i = 0; i < n; i++) {
            int idx = NthVisible(&ss->catalog, i);
            float w, ph;
            UiPicWh(&ss->icons[idx], h, &w, &ph);
            picw[i] = w;
            colw[i] = UiColW(ss, i, w);
            total += colw[i];
            if (i + 1 < n) total += gap;
        }
        if (total <= avail || h <= 48.f) break;
        h *= avail / total;
        if (h < 48.f) h = 48.f;
    }
    x = ss->fb.w * 0.5f - total * 0.5f;
    if (visIndex < 0) visIndex = 0;
    if (visIndex >= n) visIndex = n - 1;
    for (i = 0; i < visIndex; i++)
        x += colw[i] + gap;
    *iw = picw[visIndex];
    *ih = h;
    *ox = x + colw[visIndex] * 0.5f;
    *oy = ((float)ss->fb.h - UiDebugReserve(ss)) * 0.5f - 24.f;
}

static void FitPickerIcons(Session *ss)
{
    int i;
    float x, y, w, h;
    int th;
    if (!ss || ss->iconsFitted) return;
    if (VisibleCount(&ss->catalog) < 1) {
        ss->iconsFitted = 1;
        return;
    }
    IconLayout(ss, 0, &x, &y, &w, &h);
    th = (int)(h + 0.5f);
    if (th < 32) th = 32;
    for (i = 0; i < ss->catalog.ntargets; i++) {
        Image scaled;
        int tw;
        if (!ss->icons[i].px || ss->icons[i].h < 1) continue;
        if (ss->icons[i].h == th) continue;
        tw = (int)((h * (float)ss->icons[i].w / (float)ss->icons[i].h) + 0.5f);
        if (tw < 1) tw = 1;
        Zero(&scaled, sizeof(scaled));
        if (ImageResize(&ss->icons[i], tw, th, &scaled) == RbOk) {
            ImageFree(&ss->icons[i]);
            ss->icons[i] = scaled;
        }
    }
    ss->iconsFitted = 1;
}

void PickerDraw(Session *ss)
{
    int n, rank;
    if (!ss) return;
    FitPickerIcons(ss);
    ss->fade = 1.f;
    ss->selAnim = 1.f;
    GfxClear(&ss->fb, Token(RbTokenBgPrimary, 255));
    n = VisibleCount(&ss->catalog);
    ss->fb.damageW = 0;
    ss->fb.damageH = 0;

    for (rank = 0; rank < n; rank++) {
        int idx = NthVisible(&ss->catalog, rank);
        const Target *t = &ss->catalog.targets[idx];
        float cx, cy, iw, ih;
        int sel;
        IconLayout(ss, rank, &cx, &cy, &iw, &ih);
        sel = (idx == ss->selected);

        if (sel) {
            float pad = UI_HALO_PAD;
            float hx = cx - iw * 0.5f - pad;
            float hy = cy - ih * 0.5f - pad;
            float hw = iw + pad * 2.f;
            float hh = ih + pad * 2.f;
            GfxRoundRect(&ss->fb, hx, hy, hw, hh, 18.f,
                              Tokenf(RbTokenBrandPrimary, RbTokenBrandSubtleA));
            GfxRoundRectStroke(&ss->fb, hx, hy, hw, hh, 18.f, 2.f,
                                     Token(RbTokenBrandPrimary, 255));
        }

        if (ss->icons[idx].px) {
            float pw = (float)ss->icons[idx].w;
            float ph = (float)ss->icons[idx].h;
            GfxBlit(&ss->fb, &ss->icons[idx], cx - pw * 0.5f, cy - ph * 0.5f, pw, ph, 1.f);
        } else
            GfxRoundRect(&ss->fb, cx - iw * 0.5f, cy - ih * 0.5f, iw, ih, 18.f,
                              Token(RbTokenBgTertiary, 255));

        GfxTextCenter(&ss->fb, UiLabelFont(ss), cx, cy + ih * 0.5f + 24.f, t->title,
                           Token(sel ? RbTokenTextPrimary : RbTokenTextTertiary, 255));
    }

    {
        float hintY = (float)ss->fb.h - 48.f;
        if (UiDebugOn(ss))
            hintY = (float)ss->fb.h - UiDebugReserve(ss) - 8.f;
        GfxTextCenter(&ss->fb, UiCaption(ss), ss->fb.w * 0.5f, hintY,
                          ss->catalog.pickerHint[0] ? ss->catalog.pickerHint : "Choose a startup volume",
                          Token(RbTokenTextTertiary, 255));
        GfxText(&ss->fb, UiCaption(ss), UiEdge(ss), hintY, "Settings",
                    Token(RbTokenTextTertiary, 255));
        if (UiDebugOn(ss))
            DebugTermDraw(ss);
    }

    if (ss->catalog.pickerTimeoutS > 0 && ss->timeoutLeftMs > 0) {
        char buf[64];
        float ty = (float)ss->fb.h - 64.f;
        if (UiDebugOn(ss))
            ty = (float)ss->fb.h - UiDebugReserve(ss) - 24.f;
        snprintf(buf, sizeof(buf), "Booting in %d", (ss->timeoutLeftMs + 999) / 1000);
        GfxTextCenter(&ss->fb, UiCaption(ss), ss->fb.w * 0.5f, ty, buf,
                           Token(RbTokenTextTertiary, 255));
    }
    FbDamageFull(&ss->fb);
}

void AuthDraw(Session *ss)
{
    float cx, cy, w, h, fieldW, fieldH, cardX, cardY, fieldX, fieldY;
    char dots[72];
    int i;
    if (!ss) return;
    PickerDraw(ss);
    GfxFillRect(&ss->fb, 0.f, 0.f, (float)ss->fb.w, (float)ss->fb.h,
                     Token(RbTokenBgPrimary, 178));
    cx = ss->fb.w * 0.5f;
    cy = ss->fb.h * 0.5f;
    w = 320.f;
    h = 160.f;
    fieldW = w - 64.f;
    fieldH = 44.f;
    {
        float pcx, pcy, piw, pih;
        IconLayout(ss, VisibleRank(&ss->catalog, ss->selected), &pcx, &pcy, &piw, &pih);
        cy = pcy + pih * 0.5f + 64.f + h * 0.5f;
        if (UiCompact(ss) || cy + h * 0.5f > (float)ss->fb.h - 32.f)
            cy = (float)ss->fb.h * 0.5f;
    }
    cardX = cx - w * 0.5f;
    cardY = cy - h * 0.5f;
    fieldX = cx - fieldW * 0.5f;
    fieldY = cardY + h - 32.f - fieldH;
    GfxRoundRect(&ss->fb, cardX, cardY + 8.f, w, h, 18.f,
                      Rgba(0, 0, 0, 90));
    GfxRoundRect(&ss->fb, cardX, cardY, w, h, 18.f,
                      Token(RbTokenBgElevated, 255));
    GfxTextCenter(&ss->fb, &ss->fontBody, cx, cardY + 32.f,
                      ss->authFail ? "Incorrect password" : "This volume is locked",
                      Token(ss->authFail ? RbTokenDanger : RbTokenTextSecondary, 255));
    for (i = 0; i < ss->authLen && i < 24; i++) dots[i] = '*';
    dots[i] = 0;
    GfxRoundRectStroke(&ss->fb, fieldX - 2.f, fieldY - 2.f, fieldW + 4.f, fieldH + 4.f, 10.f, 2.f,
                             Tokenf(RbTokenBrandPrimary, RbTokenBrandFocusA));
    GfxRoundRect(&ss->fb, fieldX, fieldY, fieldW, fieldH, 8.f,
                      Token(RbTokenBgTertiary, 255));
    GfxTextCenter(&ss->fb, &ss->fontBody, cx, fieldY + 14.f, dots[0] ? dots : "",
                      Token(RbTokenTextPrimary, 255));
    FbDamageFull(&ss->fb);
}

void ConsolePrint(Session *ss, const char *line)
{
    int i;
    if (!ss || !line) return;
    if (ss->consoleLines >= 16) {
        for (i = 1; i < 16; i++) Strcpy(ss->consoleLog[i - 1], sizeof(ss->consoleLog[0]), ss->consoleLog[i]);
        ss->consoleLines = 15;
    }
    Strcpy(ss->consoleLog[ss->consoleLines++], sizeof(ss->consoleLog[0]), line);
}

void ConsoleDraw(Session *ss)
{
    int i;
    char prompt[280];
    float pad = 48.f;
    const Font *cap;
    if (!ss) return;
    cap = UiCaption(ss);
    pad = UiCompact(ss) ? 24.f : 48.f;
    GfxClear(&ss->fb, Token(RbTokenBgPrimary, 255));
    GfxText(&ss->fb, &ss->fontTitle, pad, pad, "Recovery",
                Token(RbTokenTextPrimary, 255));
    GfxText(&ss->fb, cap, pad, pad + 40.f, "Type help for commands.",
                Token(RbTokenTextTertiary, 255));
    GfxFillRect(&ss->fb, pad, pad + 64.f, (float)ss->fb.w - pad * 2.f, 1.f,
                     Token(RbTokenBorderDefault, 255));
    for (i = 0; i < ss->consoleLines; i++) {
        GfxText(&ss->fb, &ss->fontBody, pad, pad + 88.f + i * 24.f, ss->consoleLog[i],
                    Token(RbTokenTextSecondary, 255));
    }
    snprintf(prompt, sizeof(prompt), "rb> %s", ss->consoleIn);
    GfxText(&ss->fb, &ss->fontBody, pad, (float)ss->fb.h - 48.f, prompt,
                Token(RbTokenTextPrimary, 255));
    FbDamageFull(&ss->fb);
}

static void CmdHelp(Session *ss)
{
    ConsolePrint(ss, "boot [id]     start a catalog target");
    ConsolePrint(ss, "ls            list targets and volumes");
    ConsolePrint(ss, "set k v       write environment");
    ConsolePrint(ss, "debug on|off  verbose boot logs");
    ConsolePrint(ss, "settings      open Settings");
    ConsolePrint(ss, "env           show saved environment");
    ConsolePrint(ss, "probe         rescan volumes");
    ConsolePrint(ss, "exit          return to picker");
}

static void CmdLs(Session *ss)
{
    int i;
    char line[240];
    for (i = 0; i < ss->catalog.ntargets; i++) {
        const Target *t = &ss->catalog.targets[i];
        snprintf(line, sizeof(line), "%s  %-12s  %-10s  %s%s",
                 i == ss->selected ? "*" : " ",
                 t->id, KindName(t->kind), t->title,
                 t->present ? "" : "  [missing]");
        ConsolePrint(ss, line);
    }
    for (i = 0; i < ss->probe.nvols; i++) {
        snprintf(line, sizeof(line), "vol %-12s  %s", ss->probe.vols[i].name, ss->probe.vols[i].path);
        ConsolePrint(ss, line);
    }
}

void ConsoleSubmit(Session *ss)
{
    char buf[256];
    char *cmd, *arg;
    Strcpy(buf, sizeof(buf), ss->consoleIn);
    ss->consoleIn[0] = 0;
    ss->consoleLen = 0;
    cmd = buf;
    while (*cmd == ' ') cmd++;
    if (!cmd[0]) return;
    arg = cmd;
    while (*arg && *arg != ' ') arg++;
    if (*arg) {
        *arg = 0;
        arg++;
        while (*arg == ' ') arg++;
    } else {
        arg = "";
    }

    {
        char shown[240];
        snprintf(shown, sizeof(shown), "> %s %s", cmd, arg);
        ConsolePrint(ss, shown);
    }

    if (StrEq(cmd, "help")) CmdHelp(ss);
    else if (StrEq(cmd, "exit") || StrEq(cmd, "quit")) ss->phase = RbPhasePicker;
    else if (StrEq(cmd, "ls")) CmdLs(ss);
    else if (StrEq(cmd, "env")) {
        int i;
        for (i = 0; i < ss->env.n; i++) {
            char line[240];
            snprintf(line, sizeof(line), "%s=%s", ss->env.keys[i], ss->env.vals[i]);
            ConsolePrint(ss, line);
        }
    } else if (StrEq(cmd, "set") && arg[0]) {
        char key[64];
        char *sp = arg;
        while (*sp && *sp != ' ') sp++;
        if (*sp) {
            size_t n = (size_t)(sp - arg);
            if (n >= sizeof(key)) n = sizeof(key) - 1;
            memcpy(key, arg, n);
            key[n] = 0;
            while (*sp == ' ') sp++;
            EnvSet(&ss->env, key, sp);
            EnvSave(&ss->env);
            ConsolePrint(ss, "saved");
        }
    } else if (StrEq(cmd, "debug")) {
        int on = 1;
        if (arg[0] && (StrEq(arg, "0") || StrEq(arg, "off") || StrEq(arg, "false"))) on = 0;
        DebugSet(&ss->env, on);
        EnvSave(&ss->env);
        ConsolePrint(ss, on ? "debug on" : "debug off");
    } else if (StrEq(cmd, "settings")) {
        ss->phase = RbPhaseSettings;
        ss->fade = 0.f;
    } else if (StrEq(cmd, "probe")) {
        char volroot[RbMaxPath];
        Join(volroot, sizeof(volroot), ss->plat->root, "volumes");
        ProbeVolumes(volroot, &ss->probe);
        ProbeApply(&ss->catalog, &ss->probe);
        ConsolePrint(ss, "volumes rescanned");
    } else if (StrEq(cmd, "boot")) {
        int idx = arg[0] ? CatalogFind(&ss->catalog, arg) : ss->selected;
        if (idx < 0) ConsolePrint(ss, "unknown target");
        else {
            ss->selected = idx;
            ss->bootIndex = idx;
            ss->phase = RbPhaseHandoff;
        }
    } else if (StrEq(cmd, "reboot")) {
        if (ss->plat->reboot) ss->plat->reboot();
        ss->running = 0;
    } else if (StrEq(cmd, "halt")) {
        if (ss->plat->halt) ss->plat->halt();
        ss->running = 0;
    } else if (StrEq(cmd, "firmware")) {
        if (ss->plat->FirmwareSetup) ss->plat->FirmwareSetup();
        ss->running = 0;
    } else {
        ConsolePrint(ss, "unknown command - try help");
    }
}

int PickerVisibleRank(const Catalog *c, int idx)
{
    return VisibleRank(c, idx);
}

static int LogHas(const char *s, const char *n)
{
    int i, j;
    if (!s || !n || !n[0]) return 0;
    for (i = 0; s[i]; i++) {
        for (j = 0; n[j] && s[i + j] && s[i + j] == n[j]; j++) { }
        if (!n[j]) return 1;
    }
    return 0;
}

static uint8_t BootlogKind(const char *line)
{
    if (LogHas(line, "fail") || LogHas(line, "Fail") || LogHas(line, "missing") ||
        LogHas(line, "Missing") || LogHas(line, "not saved") || LogHas(line, "skip"))
        return 2;
    if (LogHas(line, "ready") || LogHas(line, "loaded") || LogHas(line, "Loaded") ||
        LogHas(line, "detected") || LogHas(line, "mapped") || LogHas(line, "ok") ||
        LogHas(line, "found") || LogHas(line, "copied") || LogHas(line, "jump"))
        return 1;
    return 0;
}

static uint64_t gLogOrigin;
static int gLogOriginSet;

static uint32_t BootlogNow(void)
{
    uint64_t t = 0;
    if (Plat && Plat->NowMs)
        t = Plat->NowMs();
    if (!gLogOriginSet) {
        gLogOrigin = t;
        gLogOriginSet = 1;
    }
    if (t < gLogOrigin)
        return 0;
    t -= gLogOrigin;
    if (t > 0xffffffffull)
        t = 0xffffffffull;
    return (uint32_t)t;
}

static void FmtLogTime(char *buf, size_t cap, uint32_t ms, int withS)
{
    unsigned sec = ms / 1000u;
    unsigned cs = (ms % 1000u) / 10u;
    char frac[3];
    if (!buf || cap < 4) return;
    frac[0] = (char)('0' + (cs / 10u) % 10u);
    frac[1] = (char)('0' + (cs % 10u));
    frac[2] = 0;
    if (withS)
        snprintf(buf, cap, "%u.%ss", sec, frac);
    else
        snprintf(buf, cap, "%u.%s", sec, frac);
}

static char *BootlogDup(const char *s)
{
    int n;
    char *p;
    if (!s) s = "";
    n = Strlen(s);
    if (!gBootlogHeap) {
        if (gFrozenN >= RbFrozenLog) return 0;
        if (n > 255) n = 255;
        Copy(gFrozenText[gFrozenN], s, (size_t)n);
        gFrozenText[gFrozenN][n] = 0;
        return gFrozenText[gFrozenN++];
    }
    p = (char *)Alloc((size_t)n + 1u);
    if (!p) return 0;
    Copy(p, s, (size_t)n);
    p[n] = 0;
    return p;
}

static int BootlogEnsure(LogLine **lines, int *n, int *cap, int want)
{
    LogLine *p;
    if (want <= *cap) return 1;
    if (!gBootlogHeap) return 0;
    p = (LogLine *)Alloc((size_t)want * sizeof(LogLine));
    if (!p) return 0;
    if (*lines && *n)
        Copy(p, *lines, (size_t)*n * sizeof(LogLine));
    Zero(p + *n, (size_t)(want - *n) * sizeof(LogLine));
    RoboRbFree(*lines);
    *lines = p;
    *cap = want;
    return 1;
}

static int BootlogGrow(LogLine **lines, int *n, int *cap)
{
    int next;
    if (*n < *cap) return 1;
    if (!gBootlogHeap) return 0;
    next = *cap ? *cap * 2 : 256;
    return BootlogEnsure(lines, n, cap, next);
}

void BootlogFreeze(void)
{
    if (gBootlogSs)
        BootlogEnsure(&gBootlogSs->bootLog, &gBootlogSs->bootLogN,
                       &gBootlogSs->bootLogCap, gBootlogSs->bootLogN + 48);
    gBootlogHeap = 0;
}

static int BootlogPush(LogLine **lines, int *n, int *cap, const char *text, uint32_t ms, uint8_t kind)
{
    char *dup;
    if (!BootlogGrow(lines, n, cap)) return 0;
    dup = BootlogDup(text);
    if (!dup) return 0;
    (*lines)[*n].text = dup;
    (*lines)[*n].ms = ms;
    (*lines)[*n].kind = kind;
    (*n)++;
    HoPut(ms, kind, dup);
    return 1;
}

static void BootlogFreeLines(LogLine **lines, int *n, int *cap)
{
    int i;
    if (!lines || !*lines) {
        if (n) *n = 0;
        if (cap) *cap = 0;
        return;
    }
    for (i = 0; i < *n; i++) {
        char *t = (*lines)[i].text;
        if (t >= &gFrozenText[0][0] && t < &gFrozenText[RbFrozenLog][0])
            continue;
        RoboRbFree(t);
    }
    RoboRbFree(*lines);
    *lines = 0;
    *n = 0;
    *cap = 0;
}

void BootlogRelease(Session *ss)
{
    if (!ss) return;
    BootlogFreeLines(&ss->bootLog, &ss->bootLogN, &ss->bootLogCap);
}

int BootlogHandoffBegin(void *buf, uint32_t size)
{
    int i;
    if (!buf || size < sizeof(RoboDebugLogHeader) + 32u)
        return 0;
    memset(buf, 0, size);
    gHoBuf = (uint8_t *)buf;
    gHoSize = size;
    gHoUsed = (uint32_t)sizeof(RoboDebugLogHeader);
    gHoCount = 0;
    HoSyncHeader();
    if (gBootlogSs) {
        for (i = 0; i < gBootlogSs->bootLogN; i++)
            HoPut(gBootlogSs->bootLog[i].ms, gBootlogSs->bootLog[i].kind,
                   gBootlogSs->bootLog[i].text);
    } else {
        for (i = 0; i < gEarlyN; i++)
            HoPut(gEarlyLog[i].ms, gEarlyLog[i].kind, gEarlyLog[i].text);
    }
    return 1;
}

void BootlogHandoffInfo(uint32_t *used, uint32_t *count)
{
    if (used) *used = gHoUsed;
    if (count) *count = gHoCount;
}

static void BootlogAppendSs(Session *ss, const char *line, uint32_t ms, uint8_t kind)
{
    if (!ss || !line) return;
    if (!BootlogPush(&ss->bootLog, &ss->bootLogN, &ss->bootLogCap, line, ms, kind))
        return;
    ss->bootLogSeq++;
}

static void BootlogDrainEarly(Session *ss)
{
    int i;
    if (!ss || gEarlyN < 1) return;
    for (i = 0; i < gEarlyN; i++)
        BootlogAppendSs(ss, gEarlyLog[i].text, gEarlyLog[i].ms, gEarlyLog[i].kind);
    BootlogFreeLines(&gEarlyLog, &gEarlyN, &gEarlyCap);
}

void Bootlog(Session *ss, const char *line)
{
    uint32_t ms;
    uint8_t kind;
    if (!line) return;
    if (!ss) ss = gBootlogSs;
    else gBootlogSs = ss;
    ms = BootlogNow();
    kind = BootlogKind(line);
    if (!ss) {
        BootlogPush(&gEarlyLog, &gEarlyN, &gEarlyCap, line, ms, kind);
        return;
    }
    BootlogDrainEarly(ss);
    BootlogAppendSs(ss, line, ms, kind);
}

void Bootlogf(Session *ss, const char *fmt, ...)
{
    char line[RbBootlogCols];
    va_list ap;
    if (!fmt) return;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    Bootlog(ss, line);
}

static int GlyphAdvance(const Font *f, unsigned ch)
{
    if (!f || !f->atlas) return 6;
    if (ch < (unsigned)f->first || ch > (unsigned)f->last)
        return f->px > 0 ? f->px / 3 : 4;
    return f->glyphs[ch - f->first].advance;
}

static int WrapChunk(const Font *f, const char *s, int maxW)
{
    int w = 0, n = 0, lastSpace = -1;
    if (!s || !*s) return 0;
    if (maxW < 12) maxW = 12;
    while (s[n]) {
        unsigned ch = (unsigned char)s[n];
        int adv = GlyphAdvance(f, ch);
        if (n > 0 && w + adv > maxW)
            return lastSpace > 0 ? lastSpace : n;
        w += adv;
        n++;
        if (ch == ' ') lastSpace = n;
    }
    return n;
}

static void GfxTextN(Surface *s, const Font *f, float x, float y, const char *text, int n, uint32_t color)
{
    char tmp[RbBootlogCols];
    if (n < 0) n = 0;
    if (n >= (int)sizeof(tmp)) n = (int)sizeof(tmp) - 1;
    if (n) Copy(tmp, text, (size_t)n);
    tmp[n] = 0;
    GfxText(s, f, x, y, tmp, color);
}

typedef struct {
    const Session *ss;
    const Font *font;
    int maxW;
    int line;
    int off;
} bootlogWrap;

static int BootlogWrapNext(bootlogWrap *it, const char **text, int *len, uint32_t *ms, uint8_t *kind, int *cont)
{
    const char *s;
    int n;
    if (!it || !it->ss || it->line >= it->ss->bootLogN) return 0;
    s = it->ss->bootLog[it->line].text;
    if (!s) s = "";
    if (ms) *ms = it->ss->bootLog[it->line].ms;
    if (kind) *kind = it->ss->bootLog[it->line].kind;
    if (cont) *cont = it->off > 0;
    if (!s[it->off]) {
        if (text) *text = s;
        if (len) *len = 0;
        it->line++;
        it->off = 0;
        return 1;
    }
    n = WrapChunk(it->font, s + it->off, it->maxW);
    if (n < 1) n = 1;
    if (text) *text = s + it->off;
    if (len) *len = n;
    it->off += n;
    while (s[it->off] == ' ') it->off++;
    if (!s[it->off]) {
        it->line++;
        it->off = 0;
    }
    return 1;
}

static int BootlogRowCount(const Session *ss, const Font *font, int maxW)
{
    bootlogWrap it;
    int rows = 0;
    Zero(&it, sizeof(it));
    it.ss = ss;
    it.font = font;
    it.maxW = maxW;
    while (BootlogWrapNext(&it, 0, 0, 0, 0, 0))
        rows++;
    return rows;
}

static void BootlogLayout(const Session *ss, float *x, float *y, float *w, float *h,
                           float *lineH, float *timeW, int *maxW, int *vis)
{
    DebugTermGeom(ss, x, y, w, h);
    *lineH = UiCompact(ss) ? 13.f : 14.f;
    *timeW = UiCompact(ss) ? 44.f : 56.f;
    *maxW = (int)(*w - 36.f - *timeW - 28.f);
    if (*maxW < 24) *maxW = 24;
    *vis = (int)((*h - 52.f) / *lineH);
    if (*vis < 1) *vis = 1;
}

int DebugLogScroll(Session *ss, int delta)
{
    float x, y, w, h, lineH, timeW;
    int maxW, vis, rows, maxStart, start;
    const Font *cap;
    if (!ss || !delta || !UiDebugOn(ss)) return 0;
    cap = UiCaption(ss);
    BootlogLayout(ss, &x, &y, &w, &h, &lineH, &timeW, &maxW, &vis);
    rows = BootlogRowCount(ss, cap, maxW);
    maxStart = rows > vis ? rows - vis : 0;
    start = ss->bootLogFollow ? maxStart : ss->bootLogTop;
    if (start < 0) start = 0;
    if (start > maxStart) start = maxStart;
    start += delta;
    if (start < 0) start = 0;
    if (start > maxStart) start = maxStart;
    if (start == (ss->bootLogFollow ? maxStart : ss->bootLogTop) &&
        ss->bootLogFollow == (start >= maxStart))
        return 0;
    ss->bootLogTop = start;
    ss->bootLogFollow = start >= maxStart;
    ss->bootLogShown = -1;
    return 1;
}

void DebugTermDraw(Session *ss)
{
    float x, y, w, h, textY, lineH, timeW;
    int vis, start, keepFull, maxW, rows, maxStart, row, drawn;
    const Font *cap;
    char elapsed[24];
    uint32_t NowMs = 0;
    bootlogWrap it;
    if (!ss || !UiDebugOn(ss)) return;
    gBootlogSs = ss;
    keepFull = (ss->fb.damageW <= 0 || ss->fb.damageH <= 0);
    cap = UiCaption(ss);
    BootlogLayout(ss, &x, &y, &w, &h, &lineH, &timeW, &maxW, &vis);
    GfxRoundRect(&ss->fb, x, y, w, h, 18.f, Token(RbTokenBgSecondary, 255));
    GfxRoundRectStroke(&ss->fb, x, y, w, h, 18.f, 1.f, Token(RbTokenBorderDefault, 255));
    GfxText(&ss->fb, cap, x + 24.f, y + 16.f, "Logs", Token(RbTokenTextTertiary, 255));
    NowMs = BootlogNow();
    FmtLogTime(elapsed, sizeof(elapsed), NowMs, 1);
    {
        int tw = GfxTextWidth(cap, elapsed);
        GfxText(&ss->fb, cap, x + w - 24.f - (float)tw, y + 16.f, elapsed,
                    Token(RbTokenTextTertiary, 255));
    }
    GfxFillRect(&ss->fb, x + 24.f, y + 38.f, w - 48.f, 1.f, Token(RbTokenBorderSubtle, 255));
    rows = BootlogRowCount(ss, cap, maxW);
    maxStart = rows > vis ? rows - vis : 0;
    if (ss->bootLogFollow)
        start = maxStart;
    else {
        start = ss->bootLogTop;
        if (start < 0) start = 0;
        if (start > maxStart) start = maxStart;
    }
    ss->bootLogTop = start;
    if (start >= maxStart)
        ss->bootLogFollow = 1;
    Zero(&it, sizeof(it));
    it.ss = ss;
    it.font = cap;
    it.maxW = maxW;
    textY = y + 48.f;
    row = 0;
    drawn = 0;
    while (drawn < vis) {
        const char *chunk = "";
        int n = 0, cont = 0;
        uint32_t ms = 0;
        uint8_t kind = 0;
        uint32_t ink, dot;
        char tbuf[16];
        if (!BootlogWrapNext(&it, &chunk, &n, &ms, &kind, &cont))
            break;
        if (row++ < start)
            continue;
        ink = Token(RbTokenTextSecondary, 255);
        dot = Token(RbTokenBorderStrong, 255);
        if (kind == 1) {
            ink = Token(RbTokenTextPrimary, 255);
            dot = Token(RbTokenBrandPrimary, 255);
        } else if (kind == 2) {
            ink = Token(RbTokenWarning, 255);
            dot = Token(RbTokenWarning, 255);
        }
        if (!cont) {
            if (kind == 1 || kind == 2)
                GfxCircle(&ss->fb, x + 28.f, textY + 6.f, 2.5f, dot);
            else
                GfxRing(&ss->fb, x + 28.f, textY + 6.f, 2.5f, 1.15f, dot);
            FmtLogTime(tbuf, sizeof(tbuf), ms, 0);
            GfxText(&ss->fb, cap, x + 36.f, textY, tbuf, Token(RbTokenTextTertiary, 255));
        }
        GfxTextN(&ss->fb, cap, x + 36.f + timeW, textY, chunk, n, ink);
        textY += lineH;
        drawn++;
    }
    if (rows > vis && maxStart > 0) {
        float trackX = x + w - 14.f;
        float trackY = y + 48.f;
        float trackH = h - 64.f;
        float thumbH = trackH * ((float)vis / (float)rows);
        float thumbY;
        if (thumbH < 10.f) thumbH = 10.f;
        if (thumbH > trackH) thumbH = trackH;
        thumbY = trackY + (trackH - thumbH) * ((float)start / (float)maxStart);
        GfxRoundRect(&ss->fb, trackX, trackY, 3.f, trackH, 2.f, Token(RbTokenBorderSubtle, 255));
        GfxRoundRect(&ss->fb, trackX, thumbY, 3.f, thumbH, 2.f, Token(RbTokenBrandPrimary, 255));
    }
    ss->bootLogShown = ss->bootLogSeq;
    if (keepFull) {
        FbDamageFull(&ss->fb);
        return;
    }
    ss->fb.damageX = (int)x - 2;
    ss->fb.damageY = (int)y - 2;
    ss->fb.damageW = (int)w + 4;
    ss->fb.damageH = (int)h + 4;
}

void DebugFlush(Session *ss)
{
    if (!ss) ss = gBootlogSs;
    if (!ss || !ss->plat) return;
    if (!UiDebugOn(ss)) return;
    if (ss->bootLogSeq == ss->bootLogShown && ss->splashBgReady)
        return;
    if (ss->phase == RbPhaseLoad || ss->phase == RbPhaseSplash ||
        ss->phase == RbPhaseVerbose || ss->phase == RbPhaseHandoff) {
        if (!ss->splashBgReady)
            SplashDraw(ss, ss->loadVisual, 1.f);
        else if (UiDebugOn(ss))
            DebugTermDraw(ss);
    } else
        DebugTermDraw(ss);
    if (ss->plat->present) ss->plat->present(&ss->fb);
}

void VerboseDraw(Session *ss)
{
    if (!ss) return;
    if (!ss->splashBgReady) {
        ss->loadVisual = RbSplashHandoff;
        SplashDraw(ss, ss->loadVisual, 1.f);
        return;
    }
    if (UiDebugOn(ss))
        DebugTermDraw(ss);
}

void SettingsDraw(Session *ss)
{
    float pad, cardW, cardH, cardX, cardY, rowY, togW, togH, togX, togY, knob;
    int on;
    const Font *body, *cap, *title;
    if (!ss) return;
    pad = UiCompact(ss) ? 22.f : 40.f;
    on = RoboRbDebugEnabled(&ss->env);
    body = UiBody(ss);
    cap = UiCaption(ss);
    title = ss->fontTitle.atlas ? &ss->fontTitle : body;
    cardW = UiCompact(ss) ? (float)ss->fb.w - pad * 2.f : 560.f;
    cardH = 160.f;
    cardX = ((float)ss->fb.w - cardW) * 0.5f;
    cardY = ((float)ss->fb.h - UiDebugReserve(ss)) * 0.5f - cardH * 0.5f;
    if (cardY < pad + 72.f) cardY = pad + 72.f;
    togW = 51.f;
    togH = 31.f;
    GfxClear(&ss->fb, Token(RbTokenBgPrimary, 255));
    GfxTextCenter(&ss->fb, title, (float)ss->fb.w * 0.5f, pad + 8.f, "Settings",
                       Token(RbTokenTextPrimary, 255));
    GfxTextCenter(&ss->fb, cap, (float)ss->fb.w * 0.5f, pad + 48.f, "Startup",
                       Token(RbTokenTextTertiary, 255));
    GfxRoundRect(&ss->fb, cardX, cardY, cardW, cardH, 18.f,
                      Token(RbTokenBgSecondary, 255));
    rowY = cardY + 36.f;
    GfxText(&ss->fb, body, cardX + 32.f, rowY, "Debug",
                Token(RbTokenTextPrimary, 255));
    GfxText(&ss->fb, cap, cardX + 32.f, rowY + 28.f, "Show verbose logs while starting up",
                Token(RbTokenTextTertiary, 255));
    togX = cardX + cardW - 32.f - togW;
    togY = rowY + 4.f;
    GfxRoundRect(&ss->fb, togX, togY, togW, togH, togH * 0.5f,
                      on ? Token(RbTokenBrandPrimary, 255) : Token(RbTokenBgTertiary, 255));
    knob = on ? togX + togW - togH + 2.f : togX + 2.f;
    GfxCircle(&ss->fb, knob + (togH - 4.f) * 0.5f, togY + togH * 0.5f, (togH - 6.f) * 0.5f,
                  Token(RbTokenTextOnBrand, 255));
    {
        float hintY = (float)ss->fb.h - 48.f;
        if (on)
            hintY = (float)ss->fb.h - UiDebugReserve(ss) - 8.f;
        GfxTextCenter(&ss->fb, cap, (float)ss->fb.w * 0.5f, hintY,
                          "Enter to toggle   Esc to return",
                          Token(RbTokenTextTertiary, 255));
        if (on)
            DebugTermDraw(ss);
    }
    FbDamageFull(&ss->fb);
}

void KlogDraw(Session *ss)
{
    char line[96];
    float pad, y;
    const Font *body, *cap, *title;
    if (!ss) return;
    if (UiDebugOn(ss)) {
        GfxClear(&ss->fb, Token(RbTokenBgPrimary, 255));
        DebugTermDraw(ss);
        FbDamageFull(&ss->fb);
        return;
    }
    pad = UiCompact(ss) ? 22.f : 40.f;
    body = UiBody(ss);
    cap = UiCaption(ss);
    title = ss->fontTitle.atlas ? &ss->fontTitle : body;
    GfxClear(&ss->fb, Token(RbTokenBgPrimary, 255));
    GfxText(&ss->fb, title, pad, pad, "Roboticela Kernel", Token(RbTokenTextPrimary, 255));
    GfxText(&ss->fb, cap, pad, pad + 36.f, "-----------------", Token(RbTokenTextTertiary, 255));
    y = pad + 72.f;
    GfxText(&ss->fb, body, pad, y, "Boot protocol: Roboticela", Token(RbTokenTextPrimary, 255));
    y += 22.f;
    GfxText(&ss->fb, body, pad, y, "CPU: x86_64", Token(RbTokenTextPrimary, 255));
    y += 36.f;
    GfxText(&ss->fb, cap, pad, y, "Memory:", Token(RbTokenTextTertiary, 255));
    y += 22.f;
    GfxText(&ss->fb, body, pad, y, "  Available: host", Token(RbTokenTextPrimary, 255));
    y += 22.f;
    GfxText(&ss->fb, body, pad, y, "  Reserved:  host", Token(RbTokenTextPrimary, 255));
    y += 36.f;
    GfxText(&ss->fb, cap, pad, y, "Framebuffer:", Token(RbTokenTextTertiary, 255));
    y += 22.f;
    snprintf(line, sizeof(line), "  %dx%d", ss->fb.w, ss->fb.h);
    GfxText(&ss->fb, body, pad, y, line, Token(RbTokenTextPrimary, 255));
    y += 22.f;
    GfxText(&ss->fb, body, pad, y, "  32 BPP", Token(RbTokenTextPrimary, 255));
    y += 36.f;
    GfxText(&ss->fb, cap, pad, y, "ACPI:", Token(RbTokenTextTertiary, 255));
    y += 22.f;
    GfxText(&ss->fb, body, pad, y, "  RSDP found", Token(RbTokenTextPrimary, 255));
    y += 36.f;
    GfxText(&ss->fb, cap, pad, y, "Kernel:", Token(RbTokenTextTertiary, 255));
    y += 22.f;
    snprintf(line, sizeof(line), "  address: 0x%llx", (unsigned long long)ss->handoff.kernel.entry);
    GfxText(&ss->fb, body, pad, y, line, Token(RbTokenTextPrimary, 255));
    y += 22.f;
    snprintf(line, sizeof(line), "  size: %zu", ss->handoff.kernel.valid ? ss->handoff.kernel.size : 0);
    GfxText(&ss->fb, body, pad, y, line, Token(RbTokenTextPrimary, 255));
    y += 36.f;
    GfxText(&ss->fb, cap, pad, y, "InitFS:", Token(RbTokenTextTertiary, 255));
    y += 22.f;
    snprintf(line, sizeof(line), "  address: 0x%llx", (unsigned long long)ss->handoff.ramdisk.phys);
    GfxText(&ss->fb, body, pad, y, line, Token(RbTokenTextPrimary, 255));
    y += 22.f;
    snprintf(line, sizeof(line), "  size: %zu", ss->handoff.ramdisk.valid ? ss->handoff.ramdisk.size : 0);
    GfxText(&ss->fb, body, pad, y, line, Token(RbTokenTextPrimary, 255));
    ss->bootLogShown = ss->bootLogSeq;
}
