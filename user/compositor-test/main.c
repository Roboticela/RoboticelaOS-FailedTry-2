#include <robosys/syscall.h>
#include <robowindow/window.h>
#include <robocompositor/compose.h>

static int g_fail;

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

static void line(const char *s)
{
    puts1(s);
    puts1("\n");
}

static void fail(const char *s)
{
    puts1("[FAIL] ");
    line(s);
    g_fail = 1;
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

static int expect(int32_t x, int32_t y, uint32_t want, const char *what)
{
    uint32_t got = 0;
    if (robo_window_sample_pixel(x, y, &got) != 0) {
        fail(what);
        return 0;
    }
    if (got != want) {
        fail(what);
        return 0;
    }
    return 1;
}

int main(void)
{
    RoboWindowID a, b, c, t;
    RoboWindowSurface sa, sb, sc, st;
    uint32_t fmt = 0, bg, red, green, blue, blend;
    int n = 0;

    line("========================================");
    line("        ROBOCOMPOSITOR TEST");
    line("========================================");
    if (robo_window_connect() != 0) {
        fail("connect");
        return 1;
    }
    pass("connected");

    a = robo_window_create(240, 160, ROBO_WINDOW_FOCUSABLE);
    b = robo_window_create(240, 160, ROBO_WINDOW_FOCUSABLE);
    c = robo_window_create(240, 160, ROBO_WINDOW_FOCUSABLE);
    t = robo_window_create(80, 80, 0);
    if (!a || !b || !c || !t) {
        fail("create");
        return 1;
    }
    robo_window_move(a, 80, 80);
    robo_window_move(b, 180, 140);
    robo_window_move(c, 280, 200);
    robo_window_move(t, 8, 8);
    if (robo_window_get_surface(a, &sa) != 0 ||
        robo_window_get_surface(b, &sb) != 0 ||
        robo_window_get_surface(c, &sc) != 0 ||
        robo_window_get_surface(t, &st) != 0) {
        fail("surface");
        return 1;
    }
    fmt = sa.pixfmt;
    fill(&sa, 0xFFFF3B30);
    fill(&sb, 0xFF30D158);
    fill(&sc, 0xFF0E7AFF);
    fill(&st, 0xFFFFFFFF);
    robo_window_submit(a);
    robo_window_submit(b);
    robo_window_submit(c);
    robo_window_set_opacity(t, 128);
    robo_window_submit(t);
    robo_window_show(a);
    robo_window_show(b);
    robo_window_show(c);
    robo_window_show(t);
    robo_sleep(30);

    bg = pack(fmt, 0xFF1D1D1F);
    red = pack(fmt, 0xFFFF3B30);
    green = pack(fmt, 0xFF30D158);
    blue = pack(fmt, 0xFF0E7AFF);
    blend = robo_compose_blend(fmt, bg, pack(fmt, 0xFFFFFFFF), 128);

    if (expect(4, 4, bg, "background"))
        n++;
    if (expect(90, 90, red, "window A"))
        n++;
    if (expect(200, 160, green, "window B over A"))
        n++;
    if (expect(300, 220, blue, "window C over B"))
        n++;
    if (expect(20, 20, blend, "global opacity"))
        n++;
    if (n >= 5)
        pass("overlap + blend");

    robo_window_move(a, 400, 80);
    robo_sleep(20);
    if (expect(90, 90, bg, "A old region"))
        pass("movement");
    else
        fail("movement");

    robo_window_resize(b, 120, 80);
    if (robo_window_get_surface(b, &sb) == 0 && sb.pixels) {
        fill(&sb, 0xFF30D158);
        robo_window_submit(b);
    }
    robo_sleep(20);
    pass("resize");

    robo_window_hide(c);
    robo_sleep(20);
    if (expect(300, 220, bg, "C hidden"))
        pass("hide");
    else
        fail("hide");

    robo_window_destroy(c);
    robo_sleep(10);
    pass("destroy");

    robo_window_destroy(a);
    robo_window_destroy(b);
    robo_window_destroy(t);
    robo_window_disconnect();

    line("");
    line("========================================");
    if (!g_fail) {
        line("[PASS] RoboCompositor");
        line("========================================");
        return 0;
    }
    line("[FAIL] RoboCompositor");
    line("========================================");
    return 1;
}
