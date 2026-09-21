#include <stdio.h>
#include <string.h>

int StdioSelftest(void)
{
    char b[8];
    return snprintf(b, sizeof(b), "Hello World") == 11 && b[7] == 0;
}
