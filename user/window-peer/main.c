#include <robosys/syscall.h>
#include <robowindow/window.h>

static int is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int errcode(robo_long r)
{
    return (int)(-(int64_t)r);
}

static void puts1(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    if (n)
        robo_write(1, s, n);
}

int main(void)
{
    RoboWindowID w;
    int e;
    if (robo_window_connect() != 0) {
        puts1("window-peer connect fail\n");
        return 1;
    }
    e = robo_window_destroy(1001);
    if (e != -ROBO_EPERM) {
        puts1("window-peer destroy not EPERM\n");
        return 2;
    }
    e = robo_window_move(1001, 0, 0);
    if (e != -ROBO_EPERM) {
        puts1("window-peer move not EPERM\n");
        return 3;
    }
    {
        RoboWindowSurface s;
        e = robo_window_get_surface(1001, &s);
        if (e != -ROBO_EPERM) {
            puts1("window-peer surface not EPERM\n");
            return 4;
        }
    }
    e = robo_window_show(1001);
    if (e != -ROBO_EPERM)
        return 5;
    w = robo_window_create(64, 64, 0);
    if (!w)
        return 6;
    if (w == 1001)
        return 7;
    if (robo_window_destroy(w) != 0)
        return 8;
    puts1("[PASS] isolation peer\n");
    robo_window_disconnect();
    (void)is_err;
    (void)errcode;
    return 0;
}
