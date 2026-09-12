#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    printf("argv-test argc=%d\n", argc);
    if (argc < 3)
        return 1;
    if (!argv || !argv[0] || !argv[1] || !argv[2])
        return 2;
    if (argv[1][0] != 'a' || argv[2][0] != 'a')
        return 3;
    printf("argv1=%s argv2=%s\n", argv[1], argv[2]);
    return 0;
}
