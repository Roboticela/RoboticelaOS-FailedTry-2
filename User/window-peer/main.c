#include <robosys/syscall.h>
#include <robowindow/window.h>

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int errcode(Long r)
{
    return (int)(-(int64_t)r);
}

static void puts1(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    if (n)
        Write(1, s, n);
}

int main(void)
{
    RoboWindowID w;
    int e;
    if (WindowConnect() != 0) {
        puts1("window-peer connect fail\n");
        return 1;
    }
    e = WindowDestroy(1001);
    if (e != -RoboEperm) {
        puts1("window-peer destroy not EPERM\n");
        return 2;
    }
    e = WindowMove(1001, 0, 0);
    if (e != -RoboEperm) {
        puts1("window-peer move not EPERM\n");
        return 3;
    }
    {
        RoboWindowSurface s;
        e = WindowGetSurface(1001, &s);
        if (e != -RoboEperm) {
            puts1("window-peer surface not EPERM\n");
            return 4;
        }
    }
    e = WindowShow(1001);
    if (e != -RoboEperm)
        return 5;
    w = WindowCreate(64, 64, 0);
    if (!w)
        return 6;
    if (w == 1001)
        return 7;
    if (WindowDestroy(w) != 0)
        return 8;
    puts1("[PASS] isolation peer\n");
    WindowDisconnect();
    (void)IsErr;
    (void)errcode;
    return 0;
}
