#include <robosys/syscall.h>
#include <robowindow/window.h>

static int gFail;

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

static void line(const char *s)
{
    puts1(s);
    puts1("\n");
}

static void fail(const char *s)
{
    puts1("[FAIL] ");
    line(s);
    gFail = 1;
}

static void pass(const char *s)
{
    puts1("[PASS] ");
    line(s);
}

static uint32_t pack(uint32_t pixfmt, uint32_t argb)
{
    unsigned a = (argb >> 24) & 255u;
    unsigned r = (argb >> 16) & 255u;
    unsigned g = (argb >> 8) & 255u;
    unsigned b = argb & 255u;
    uint32_t rgb;
    if (pixfmt == 1u)
        rgb = b | (g << 8) | (r << 16);
    else
        rgb = r | (g << 8) | (b << 16);
    return rgb | (a << 24);
}

static void fill(RoboWindowSurface *s, uint32_t argb)
{
    uint32_t y, x, p;
    if (!s || !s->pixels)
        return;
    p = pack(s->pixfmt, argb);
    for (y = 0; y < s->height; y++) {
        for (x = 0; x < s->width; x++)
            s->pixels[y * s->stride + x] = p;
    }
}

static void rect(RoboWindowSurface *s, int x, int y, int w, int h, uint32_t argb)
{
    int i, j;
    uint32_t p = pack(s->pixfmt, argb);
    if (!s || !s->pixels)
        return;
    for (j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || (uint32_t)yy >= s->height)
            continue;
        for (i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || (uint32_t)xx >= s->width)
                continue;
            s->pixels[(uint32_t)yy * s->stride + (uint32_t)xx] = p;
        }
    }
}

static void DrawPattern(RoboWindowSurface *s)
{
    unsigned i;
    fill(s, 0xFFF5F5F7);
    rect(s, 20, 20, (int)s->width - 40, (int)s->height - 40, 0xFF0E7AFF);
    rect(s, 32, 32, (int)s->width - 64, (int)s->height - 64, 0xFFFFFFFF);
    rect(s, 40, 40, 80, 40, 0xFF14B8A6);
    for (i = 0; i < 8; i++)
        rect(s, 140 + (int)i * 10, 48, 8, 16, 0xFF1D1D1F);
    for (i = 0; i < 16; i++)
        rect(s, 40 + (int)((i % 8) * 12), 100 + (int)((i / 8) * 12), 10, 10,
             (i & 1u) ? 0xFF0071E3 : 0xFFD2D2D7);
}

static void drain(RoboWindowID id)
{
    RoboWindowEvent e;
    unsigned n = 0;
    while (n++ < 64u) {
        if (WindowPollEvent(id, &e) != 0)
            break;
    }
}

static int WaitType(RoboWindowID id, uint32_t type, RoboWindowEvent *out)
{
    unsigned n;
    for (n = 0; n < 32u; n++) {
        RoboWindowEvent e;
        if (WindowWaitEvent(id, &e) != 0)
            return 0;
        if (e.type == type) {
            if (out)
                *out = e;
            return 1;
        }
    }
    return 0;
}

static void InjectKey(uint32_t type, uint32_t code)
{
    RoboWindowEvent e;
    unsigned i;
    for (i = 0; i < sizeof(e); i++)
        ((unsigned char *)&e)[i] = 0;
    e.type = type;
    e.code = code;
    e.value = (type == RoboWinEvKeyDown);
    WindowTestInject(&e);
}

static void InjectMouse(uint32_t type, int32_t x, int32_t y, uint32_t value)
{
    RoboWindowEvent e;
    unsigned i;
    for (i = 0; i < sizeof(e); i++)
        ((unsigned char *)&e)[i] = 0;
    e.type = type;
    e.x = x;
    e.y = y;
    e.value = value;
    e.buttons = value ? RoboMbuttonLeft : 0;
    WindowTestInject(&e);
}

int main(void)
{
    RoboWindowID a, b;
    RoboWindowSurface sa, sb;
    RoboWindowEvent ev;
    uint32_t live0, live1;
    unsigned i;
    int okIso = 0, okClean = 0, okFocus = 0, okHit = 0, okIn = 0;
    int okLife = 0, okGfx = 0, okStress = 0, okBad = 0, okMulti = 0;
    int okComp = 0;
    Pid child;
    Long wr;
    int st = 0;

    line("========================================");
    line("        WINDOW SERVER TEST");
    line("========================================");
    line("");
    line("Connecting...");
    if (WindowConnect() != 0) {
        fail("Connected");
        line("[FAIL] Window Server");
        return 1;
    }
    pass("Connected");

    line("Creating window...");
    a = WindowCreate(800, 600, RoboWindowFocusable |
                           RoboWindowMovable | RoboWindowResizable);
    if (!a) {
        fail("Window create");
        line("[FAIL] Window Server");
        return 1;
    }
    puts1("[PASS] Window ID: ");
    PutU(a);
    puts1("\n");
    if (a != 1001)
        fail("expected window 1001");

    if (WindowSetTitle(a, "window-test") != 0)
        fail("set title");
    if (WindowMove(a, 40, 40) != 0)
        fail("move");
    if (WindowGetSurface(a, &sa) != 0 || !sa.pixels ||
        sa.width != 800 || sa.height != 600) {
        fail("surface");
    } else {
        puts1("Surface:\n[PASS] ");
        PutU(sa.width);
        puts1("x");
        PutU(sa.height);
        puts1("\n");
        DrawPattern(&sa);
        if (WindowSubmit(a) != 0)
            fail("submit");
        else
            okGfx = 1;
    }

    line("Showing window...");
    if (WindowShow(a) != 0)
        fail("show");
    else
        pass("show");
    drain(a);
    okLife = 1;

    child = (Pid)ProcessCreate("/bin/window-peer");
    if (IsErr((Long)child) || !child)
        fail("peer spawn");
    else {
        wr = Wait(child, 0, &st);
        if (IsErr(wr) || st != 0)
            fail("peer status");
        else {
            pass("process isolation");
            okIso = 1;
        }
    }

    InjectKey(RoboWinEvKeyDown, RoboKeyA);
    if (WaitType(a, RoboWinEvKeyDown, &ev) && ev.code == RoboKeyA)
        okIn = 1;
    else
        fail("key down");
    InjectKey(RoboWinEvKeyUp, RoboKeyA);
    if (!WaitType(a, RoboWinEvKeyUp, 0))
        fail("key up");
    InjectMouse(RoboWinEvMouseMove, 80, 80, 0);
    if (!WaitType(a, RoboWinEvMouseMove, &ev))
        fail("mouse move");
    InjectMouse(RoboWinEvMouseButton, 80, 80, 1);
    if (!WaitType(a, RoboWinEvMouseButton, 0))
        fail("mouse button");
    InjectMouse(RoboWinEvMouseScroll, 80, 80, 1);
    if (!WaitType(a, RoboWinEvMouseScroll, 0))
        fail("mouse scroll");
    if (okIn)
        pass("Input");

    b = WindowCreate(400, 300, RoboWindowFocusable);
    if (!b || b == a)
        fail("second window");
    else {
        okMulti = 1;
        WindowMove(a, 100, 100);
        WindowResize(a, 400, 300);
        if (WindowGetSurface(a, &sa) != 0)
            fail("resize surface");
        else
            DrawPattern(&sa);
        WindowSubmit(a);
        WindowShow(a);
        WindowMove(b, 200, 200);
        if (WindowGetSurface(b, &sb) == 0 && sb.pixels) {
            fill(&sb, 0xFFFF3B30);
            WindowSubmit(b);
        }
        WindowShow(b);
        drain(a);
        drain(b);
        if (WindowHitTest(250, 250) == b)
            okHit = 1;
        else
            fail("hit test");
        InjectMouse(RoboWinEvMouseButton, 120, 120, 1);
        RoboSleep(10);
        drain(a);
        drain(b);
        if (WindowGetFocus() != a)
            fail("focus A");
        InjectMouse(RoboWinEvMouseButton, 550, 450, 1);
        RoboSleep(10);
        drain(a);
        drain(b);
        if (WindowGetFocus() == b)
            okFocus = 1;
        else
            fail("focus B");
        if (okFocus)
            pass("focus");
        if (WindowDestroy(b) != 0)
            fail("destroy B");
        if (WindowHide(a) != 0 || WindowShow(a) != 0)
            fail("hide/show");
        if (WindowHide(a) != 0)
            fail("hide A before stress");
    }

    WindowStats(&live0);
    child = (Pid)ProcessCreate("/bin/window-child");
    if (IsErr((Long)child) || !child)
        fail("child spawn");
    else {
        wr = Wait(child, 0, &st);
        (void)wr;
        RoboSleep(40);
        WindowStats(&live1);
        if (live1 == live0)
            okClean = 1;
        else
            fail("process cleanup");
    }

    for (i = 0; i < 12u; i++) {
        RoboWindowID w = WindowCreate(32, 32, 0);
        if (!w) {
            fail("stress create");
            break;
        }
        if (WindowMove(w, (int32_t)(i * 3u), (int32_t)(i * 2u)) != 0 ||
            WindowResize(w, 48, 48) != 0 ||
            WindowShow(w) != 0 ||
            WindowHide(w) != 0 ||
            WindowDestroy(w) != 0) {
            fail("stress cycle");
            break;
        }
        if (i == 11u)
            okStress = 1;
    }

    {
        okBad = 1;
        if (WindowCreate(0, 0, 0) != 0)
            okBad = 0;
        if (WindowCreate(0xFFFFFFFFu, 0xFFFFFFFFu, 0) != 0)
            okBad = 0;
        if (WindowDestroy(0) == 0)
            okBad = 0;
        if (WindowMove(99999, 0, 0) == 0)
            okBad = 0;
        if (!okBad)
            fail("malformed request");
        else
            pass("malformed request");
    }

    child = (Pid)ProcessCreate("/bin/compositor-test");
    if (IsErr((Long)child) || !child)
        fail("compositor-test spawn");
    else {
        wr = Wait(child, 0, &st);
        if (IsErr(wr) || st != 0)
            fail("compositor-test");
        else {
            pass("compositor-test");
            okComp = 1;
        }
    }

    if (WindowDestroy(a) != 0)
        fail("destroy A");
    else
        pass("Destroying...");

    WindowShutdown();
    WindowDisconnect();

    line("");
    line("========================================");
    if (!gFail && okLife && okIso && okGfx && okIn && okFocus &&
        okHit && okMulti && okClean && okStress && okBad && okComp) {
        line("[PASS] Window Server");
        line("========================================");
        return 0;
    }
    line("[FAIL] Window Server");
    line("========================================");
    return 1;
}
