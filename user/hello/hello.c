#include <robosys/syscall.h>

int main(void)
{
    static const char msg[] = "Hello from user space.\n";
    robo_write(1, msg, 23);
    return 0;
}
