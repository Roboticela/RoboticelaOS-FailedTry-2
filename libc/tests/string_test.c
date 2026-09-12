#include <stdio.h>
#include <string.h>

/* Host suite: libc/tests/libc_host_test.c */
int robo_string_selftest(void)
{
    char a[8];
    memcpy(a, "ab", 3);
    return strcmp(a, "ab") == 0;
}
