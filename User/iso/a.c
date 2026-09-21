#include <robosys/syscall.h>

int main(void)
{
    static const char msg[] = "A\n";
    Write(1, msg, 2);
    return 0;
}
