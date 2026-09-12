#include <robowindow/window.h>

int main(void)
{
    RoboWindowID a, b;
    if (robo_window_connect() != 0)
        return 1;
    a = robo_window_create(80, 60, 0);
    b = robo_window_create(80, 60, 0);
    if (!a || !b || a == b)
        return 2;
    return 0;
}
