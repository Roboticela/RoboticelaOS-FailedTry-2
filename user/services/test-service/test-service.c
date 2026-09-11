#include <robosys/syscall.h>

int main(void)
{
    static const char msg[] = "test-service started\n";
    robo_write(1, msg, 21);
    robo_sleep(50);
    return 0;
}
