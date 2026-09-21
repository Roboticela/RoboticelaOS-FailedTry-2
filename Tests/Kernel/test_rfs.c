#include <stdio.h>
#include "fs/vfs/vfs.h"
#include "fs/rfs/rfs.h"

int main(void)
{
    VfsInit();
    if (!VfsSelftest()) {
        printf("VFS host selftest failed\n");
        return 1;
    }
    if (!RobofsSelftest()) {
        printf("RoboFS host selftest failed\n");
        return 1;
    }
    printf("RFS_HOST_TEST_OK\n");
    return 0;
}
