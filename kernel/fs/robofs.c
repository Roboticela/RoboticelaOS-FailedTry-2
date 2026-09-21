#include "fs/robofs.h"
#include "kio.h"

static const uint8_t *g_img;
static uint32_t g_size;
static int g_ready;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int name_eq(const char *a, const char *b)
{
    unsigned i;
    if (!a || !b)
        return 0;
    for (i = 0; i < ROBFS_NAME; i++) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
    return 1;
}

int robo_fs_init(const RoboBootInfo *info)
{
    const uint8_t *p;
    uint32_t magic, ver, n, sz, need;
    g_img = 0;
    g_size = 0;
    g_ready = 0;
    if (!info || !info->initrd_address || !info->initrd_size)
        return 0;
    if (info->initrd_size < sizeof(RoboFsHeader))
        return 0;
    p = (const uint8_t *)(uintptr_t)info->initrd_address;
    magic = rd32(p);
    ver = rd32(p + 4);
    n = rd32(p + 8);
    sz = rd32(p + 12);
    if (magic != ROBFS_MAGIC || ver != ROBFS_VERSION)
        return 0;
    if (sz > info->initrd_size || sz < sizeof(RoboFsHeader))
        return 0;
    need = (uint32_t)sizeof(RoboFsHeader) + n * (uint32_t)sizeof(RoboFsEntry);
    if (need > sz)
        return 0;
    g_img = p;
    g_size = sz;
    g_ready = 1;
    kprintf("RoboFS  ready  %u files  %u bytes\n", (unsigned)n, (unsigned)sz);
    return 1;
}

int robo_fs_ready(void)
{
    return g_ready;
}

int robo_fs_open(const char *path, const void **data, uint32_t *size)
{
    uint32_t n, i, off, len, need;
    const uint8_t *ent;
    if (!g_ready || !path || !data || !size)
        return 0;
    n = rd32(g_img + 8);
    ent = g_img + sizeof(RoboFsHeader);
    for (i = 0; i < n; i++) {
        const RoboFsEntry *e = (const RoboFsEntry *)(ent + i * sizeof(RoboFsEntry));
        if (!name_eq(e->name, path))
            continue;
        off = e->off;
        len = e->len;
        if (off > g_size)
            return 0;
        need = off;
        if (len && off + len < off)
            return 0;
        if (off + len > g_size)
            return 0;
        (void)need;
        *data = g_img + off;
        *size = len;
        return 1;
    }
    return 0;
}

unsigned robo_fs_file_count(void)
{
    if (!g_ready)
        return 0;
    return rd32(g_img + 8);
}

int robo_fs_file_at(unsigned index, char *name, unsigned name_max,
                    const void **data, uint32_t *size)
{
    uint32_t n, off, len, i;
    const uint8_t *ent;

    if (!g_ready || !name || !name_max || !data || !size)
        return 0;
    n = rd32(g_img + 8);
    if (index >= n)
        return 0;
    ent = g_img + sizeof(RoboFsHeader) + index * sizeof(RoboFsEntry);
    {
        const RoboFsEntry *e = (const RoboFsEntry *)ent;
        for (i = 0; i + 1 < name_max && i < ROBFS_NAME && e->name[i]; i++)
            name[i] = e->name[i];
        name[i] = 0;
        off = e->off;
        len = e->len;
        if (off > g_size || off + len < off || off + len > g_size)
            return 0;
        *data = g_img + off;
        *size = len;
        return 1;
    }
}

/* Scratch for `robo_fs_payload_bytes`'s walk: it wants each file's *length*, and the accessor hands the
 * name out beside it. Nothing reads the names there, so one file-static buffer serves every entry. */
static char g_count_name[ROBFS_NAME];

uint64_t robo_fs_payload_bytes(void)
{
    uint64_t total = 0;
    uint32_t i, n, size;
    const void *data;

    if (!g_ready)
        return 0;
    n = rd32(g_img + 8);
    for (i = 0; i < n; i++) {
        if (!robo_fs_file_at(i, g_count_name, sizeof(g_count_name), &data, &size))
            continue;
        total += size;
    }
    return total;
}

uint32_t robo_fs_root_blocks(uint32_t block_size)
{
    uint64_t payload, blocks, overhead, slack;
    uint32_t n;

    if (!g_ready || !block_size)
        return 0;
    n = rd32(g_img + 8);
    payload = robo_fs_payload_bytes();
    blocks = (payload + block_size - 1) / block_size;
    /* One object slot, one directory entry and the tail block each file may need, plus the format's own
     * metadata (superblock, block bitmap, object table) and a block for each directory in the image. */
    overhead = (uint64_t)n * 8u + 64u;
    /* A megabyte left free, because the image is not the only writer: programs in it create files. */
    slack = 256u;
    blocks += overhead + slack;
    if (blocks > 16384u)
        blocks = 16384u; /* 64 MiB, so a hostile or corrupt length cannot ask for the whole heap. */
    return (uint32_t)blocks;
}

void robo_fs_report(void)
{
    uint32_t n;
    if (!g_ready) {
        kprintf("RoboFS  missing\n");
        return;
    }
    n = rd32(g_img + 8);
    kprintf("RoboFS  %u files  image %u bytes\n", (unsigned)n, (unsigned)g_size);
}
