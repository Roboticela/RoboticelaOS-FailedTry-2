#include <robosys/syscall.h>

int main(void)
{
    static const char msg[] = "test-service started\n";
    Write(1, msg, 21);
    RoboSleep(50);
    return 0;
}
