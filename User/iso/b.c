#include <robosys/syscall.h>

int main(void)
{
    static const char msg[] = "B\n";
    Write(1, msg, 2);
    return 0;
}
