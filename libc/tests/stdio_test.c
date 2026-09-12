#include <stdio.h>
#include <string.h>

int robo_stdio_selftest(void)
{
    char b[8];
    return snprintf(b, sizeof(b), "Hello World") == 11 && b[7] == 0;
}
