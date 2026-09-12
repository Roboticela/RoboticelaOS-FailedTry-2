#include <robosys/syscall.h>
#include <robowindow/window.h>

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

static void put_u(unsigned long long v)
{
    char buf[24];
    unsigned n = 0, i;
    if (!v) {
        robo_write(1, "0", 1);
        return;
    }
    while (v && n < 24) {
        buf[n++] = (char)('0' + (v % 10ull));
        v /= 10ull;
    }
    for (i = n; i > 0; i--)
        robo_write(1, &buf[i - 1], 1);
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

static void draw_pattern(RoboWindowSurface *s)
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
        if (robo_window_poll_event(id, &e) != 0)
            break;
    }
}

static int wait_type(RoboWindowID id, uint32_t type, RoboWindowEvent *out)
{
    unsigned n;
    for (n = 0; n < 32u; n++) {
        RoboWindowEvent e;
        if (robo_window_wait_event(id, &e) != 0)
            return 0;
        if (e.type == type) {
            if (out)
                *out = e;
            return 1;
        }
    }
    return 0;
}

static void inject_key(uint32_t type, uint32_t code)
{
    RoboWindowEvent e;
    unsigned i;
    for (i = 0; i < sizeof(e); i++)
        ((unsigned char *)&e)[i] = 0;
    e.type = type;
    e.code = code;
    e.value = (type == ROBO_WIN_EV_KEY_DOWN);
    robo_window_test_inject(&e);
}

static void inject_mouse(uint32_t type, int32_t x, int32_t y, uint32_t value)
{
    RoboWindowEvent e;
    unsigned i;
    for (i = 0; i < sizeof(e); i++)
        ((unsigned char *)&e)[i] = 0;
    e.type = type;
    e.x = x;
    e.y = y;
    e.value = value;
    e.buttons = value ? ROBO_MBUTTON_LEFT : 0;
    robo_window_test_inject(&e);
}

int main(void)
{
    RoboWindowID a, b;
    RoboWindowSurface sa, sb;
    RoboWindowEvent ev;
    uint32_t live0, live1;
    unsigned i;
    int ok_iso = 0, ok_clean = 0, ok_focus = 0, ok_hit = 0, ok_in = 0;
    int ok_life = 0, ok_gfx = 0, ok_stress = 0, ok_bad = 0, ok_multi = 0;
    int ok_comp = 0;
    robo_pid child;
    robo_long wr;
    int st = 0;

    line("========================================");
    line("        WINDOW SERVER TEST");
    line("========================================");
    line("");
    line("Connecting...");
    if (robo_window_connect() != 0) {
        fail("Connected");
        line("[FAIL] Window Server");
        return 1;
    }
    pass("Connected");

    line("Creating window...");
    a = robo_window_create(800, 600, ROBO_WINDOW_FOCUSABLE |
                           ROBO_WINDOW_MOVABLE | ROBO_WINDOW_RESIZABLE);
    if (!a) {
        fail("Window create");
        line("[FAIL] Window Server");
        return 1;
    }
    puts1("[PASS] Window ID: ");
    put_u(a);
    puts1("\n");
    if (a != 1001)
        fail("expected window 1001");

    if (robo_window_set_title(a, "window-test") != 0)
        fail("set title");
    if (robo_window_move(a, 40, 40) != 0)
        fail("move");
    if (robo_window_get_surface(a, &sa) != 0 || !sa.pixels ||
        sa.width != 800 || sa.height != 600) {
        fail("surface");
    } else {
        puts1("Surface:\n[PASS] ");
        put_u(sa.width);
        puts1("x");
        put_u(sa.height);
        puts1("\n");
        draw_pattern(&sa);
        if (robo_window_submit(a) != 0)
            fail("submit");
        else
            ok_gfx = 1;
    }

    line("Showing window...");
    if (robo_window_show(a) != 0)
        fail("show");
    else
        pass("show");
    drain(a);
    ok_life = 1;

    child = (robo_pid)robo_process_create("/bin/window-peer");
    if (is_err((robo_long)child) || !child)
        fail("peer spawn");
    else {
        wr = robo_wait(child, 0, &st);
        if (is_err(wr) || st != 0)
            fail("peer status");
        else {
            pass("process isolation");
            ok_iso = 1;
        }
    }

    inject_key(ROBO_WIN_EV_KEY_DOWN, ROBO_KEY_A);
    if (wait_type(a, ROBO_WIN_EV_KEY_DOWN, &ev) && ev.code == ROBO_KEY_A)
        ok_in = 1;
    else
        fail("key down");
    inject_key(ROBO_WIN_EV_KEY_UP, ROBO_KEY_A);
    if (!wait_type(a, ROBO_WIN_EV_KEY_UP, 0))
        fail("key up");
    inject_mouse(ROBO_WIN_EV_MOUSE_MOVE, 80, 80, 0);
    if (!wait_type(a, ROBO_WIN_EV_MOUSE_MOVE, &ev))
        fail("mouse move");
    inject_mouse(ROBO_WIN_EV_MOUSE_BUTTON, 80, 80, 1);
    if (!wait_type(a, ROBO_WIN_EV_MOUSE_BUTTON, 0))
        fail("mouse button");
    inject_mouse(ROBO_WIN_EV_MOUSE_SCROLL, 80, 80, 1);
    if (!wait_type(a, ROBO_WIN_EV_MOUSE_SCROLL, 0))
        fail("mouse scroll");
    if (ok_in)
        pass("Input");

    b = robo_window_create(400, 300, ROBO_WINDOW_FOCUSABLE);
    if (!b || b == a)
        fail("second window");
    else {
        ok_multi = 1;
        robo_window_move(a, 100, 100);
        robo_window_resize(a, 400, 300);
        if (robo_window_get_surface(a, &sa) != 0)
            fail("resize surface");
        else
            draw_pattern(&sa);
        robo_window_submit(a);
        robo_window_show(a);
        robo_window_move(b, 200, 200);
        if (robo_window_get_surface(b, &sb) == 0 && sb.pixels) {
            fill(&sb, 0xFFFF3B30);
            robo_window_submit(b);
        }
        robo_window_show(b);
        drain(a);
        drain(b);
        if (robo_window_hit_test(250, 250) == b)
            ok_hit = 1;
        else
            fail("hit test");
        inject_mouse(ROBO_WIN_EV_MOUSE_BUTTON, 120, 120, 1);
        robo_sleep(10);
        drain(a);
        drain(b);
        if (robo_window_get_focus() != a)
            fail("focus A");
        inject_mouse(ROBO_WIN_EV_MOUSE_BUTTON, 550, 450, 1);
        robo_sleep(10);
        drain(a);
        drain(b);
        if (robo_window_get_focus() == b)
            ok_focus = 1;
        else
            fail("focus B");
        if (ok_focus)
            pass("focus");
        if (robo_window_destroy(b) != 0)
            fail("destroy B");
        if (robo_window_hide(a) != 0 || robo_window_show(a) != 0)
            fail("hide/show");
        if (robo_window_hide(a) != 0)
            fail("hide A before stress");
    }

    robo_window_stats(&live0);
    child = (robo_pid)robo_process_create("/bin/window-child");
    if (is_err((robo_long)child) || !child)
        fail("child spawn");
    else {
        wr = robo_wait(child, 0, &st);
        (void)wr;
        robo_sleep(40);
        robo_window_stats(&live1);
        if (live1 == live0)
            ok_clean = 1;
        else
            fail("process cleanup");
    }

    for (i = 0; i < 12u; i++) {
        RoboWindowID w = robo_window_create(32, 32, 0);
        if (!w) {
            fail("stress create");
            break;
        }
        if (robo_window_move(w, (int32_t)(i * 3u), (int32_t)(i * 2u)) != 0 ||
            robo_window_resize(w, 48, 48) != 0 ||
            robo_window_show(w) != 0 ||
            robo_window_hide(w) != 0 ||
            robo_window_destroy(w) != 0) {
            fail("stress cycle");
            break;
        }
        if (i == 11u)
            ok_stress = 1;
    }

    {
        ok_bad = 1;
        if (robo_window_create(0, 0, 0) != 0)
            ok_bad = 0;
        if (robo_window_create(0xFFFFFFFFu, 0xFFFFFFFFu, 0) != 0)
            ok_bad = 0;
        if (robo_window_destroy(0) == 0)
            ok_bad = 0;
        if (robo_window_move(99999, 0, 0) == 0)
            ok_bad = 0;
        if (!ok_bad)
            fail("malformed request");
        else
            pass("malformed request");
    }

    child = (robo_pid)robo_process_create("/bin/compositor-test");
    if (is_err((robo_long)child) || !child)
        fail("compositor-test spawn");
    else {
        wr = robo_wait(child, 0, &st);
        if (is_err(wr) || st != 0)
            fail("compositor-test");
        else {
            pass("compositor-test");
            ok_comp = 1;
        }
    }

    if (robo_window_destroy(a) != 0)
        fail("destroy A");
    else
        pass("Destroying...");

    robo_window_shutdown();
    robo_window_disconnect();

    line("");
    line("========================================");
    if (!g_fail && ok_life && ok_iso && ok_gfx && ok_in && ok_focus &&
        ok_hit && ok_multi && ok_clean && ok_stress && ok_bad && ok_comp) {
        line("[PASS] Window Server");
        line("========================================");
        return 0;
    }
    line("[FAIL] Window Server");
    line("========================================");
    return 1;
}
