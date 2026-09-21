#include <stdlib.h>
#include <string.h>

int MemorySelftest(void)
{
    char *p = (char *)malloc(8);
    if (!p)
        return 0;
    strcpy(p, "ok");
    free(p);
    return 1;
}
