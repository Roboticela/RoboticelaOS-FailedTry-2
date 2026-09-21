#include <robowindow/window.h>

int main(void)
{
    RoboWindowID a, b;
    if (WindowConnect() != 0)
        return 1;
    a = WindowCreate(80, 60, 0);
    b = WindowCreate(80, 60, 0);
    if (!a || !b || a == b)
        return 2;
    return 0;
}
