#include <stdio.h>
#include "fs/vfs/vfs.h"
#include "fs/rfs/rfs.h"

int main(void)
{
    robo_vfs_init();
    if (!robo_vfs_selftest()) {
        printf("VFS host selftest failed\n");
        return 1;
    }
    if (!robofs_selftest()) {
        printf("RoboFS host selftest failed\n");
        return 1;
    }
    printf("RFS_HOST_TEST_OK\n");
    return 0;
}
