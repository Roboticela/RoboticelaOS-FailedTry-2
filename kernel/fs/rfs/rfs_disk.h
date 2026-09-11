#ifndef ROBO_RFS_DISK_H
#define ROBO_RFS_DISK_H

#include <stdint.h>

/*
 * RoboFS v1 on-disk format (little-endian).
 *
 * Block 0        superblock
 * Block bitmap_start .. bitmap_start+bitmap_blocks-1
 * Block obj_start     .. obj_start+obj_blocks-1
 * Block data_start    .. total_blocks-1     file/dir data
 *
 * Bitmap: 1 = allocated, 0 = free. Bit i corresponds to block i.
 * Object 0 is reserved. Root object id is in the superblock.
 */

#define ROBOFS_DISK_MAGIC       0x53464252u /* 'RBFS' LE */
#define ROBOFS_DISK_MAJOR       1u
#define ROBOFS_DISK_MINOR       0u
#define ROBOFS_BLOCK_SIZE       4096u
#define ROBOFS_OBJ_SIZE         128u
#define ROBOFS_DIRENT_SIZE      64u
#define ROBOFS_NAME_MAX         55u
#define ROBOFS_DIRECT           8u
#define ROBOFS_OBJ_UNUSED       0u
#define ROBOFS_OBJ_FILE         1u
#define ROBOFS_OBJ_DIR          2u

#define ROBOFS_SB_SIZE          72u

_Static_assert(ROBOFS_OBJ_SIZE == 128u, "object slot is 128 bytes");
_Static_assert(ROBOFS_DIRENT_SIZE == 64u, "directory entry is 64 bytes");
_Static_assert(ROBOFS_SB_SIZE == 72u, "superblock prefix is 72 bytes");
_Static_assert(ROBOFS_NAME_MAX == 55u, "on-disk name matches VFS name max");

#endif
