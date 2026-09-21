#include "fs/robofs.h"
#include "kio.h"

static const uint8_t *gImg;
static uint32_t gSize;
static int gReady;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int NameEq(const char *a, const char *b)
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

int FsInit(const RoboBootInfo *info)
{
    const uint8_t *p;
    uint32_t magic, ver, n, sz, need;
    gImg = 0;
    gSize = 0;
    gReady = 0;
    if (!info || !info->initrdAddress || !info->initrdSize)
        return 0;
    if (info->initrdSize < sizeof(RoboFsHeader))
        return 0;
    p = (const uint8_t *)(uintptr_t)info->initrdAddress;
    magic = rd32(p);
    ver = rd32(p + 4);
    n = rd32(p + 8);
    sz = rd32(p + 12);
    if (magic != ROBFS_MAGIC || ver != ROBFS_VERSION)
        return 0;
    if (sz > info->initrdSize || sz < sizeof(RoboFsHeader))
        return 0;
    need = (uint32_t)sizeof(RoboFsHeader) + n * (uint32_t)sizeof(RoboFsEntry);
    if (need > sz)
        return 0;
    gImg = p;
    gSize = sz;
    gReady = 1;
    kprintf("RoboFS  ready  %u files  %u bytes\n", (unsigned)n, (unsigned)sz);
    return 1;
}

int FsReady(void)
{
    return gReady;
}

int FsOpen(const char *path, const void **data, uint32_t *size)
{
    uint32_t n, i, off, len, need;
    const uint8_t *ent;
    if (!gReady || !path || !data || !size)
        return 0;
    n = rd32(gImg + 8);
    ent = gImg + sizeof(RoboFsHeader);
    for (i = 0; i < n; i++) {
        const RoboFsEntry *e = (const RoboFsEntry *)(ent + i * sizeof(RoboFsEntry));
        if (!NameEq(e->name, path))
            continue;
        off = e->off;
        len = e->len;
        if (off > gSize)
            return 0;
        need = off;
        if (len && off + len < off)
            return 0;
        if (off + len > gSize)
            return 0;
        (void)need;
        *data = gImg + off;
        *size = len;
        return 1;
    }
    return 0;
}

unsigned FsFileCount(void)
{
    if (!gReady)
        return 0;
    return rd32(gImg + 8);
}

int FsFileAt(unsigned index, char *name, unsigned nameMax,
                    const void **data, uint32_t *size)
{
    uint32_t n, off, len, i;
    const uint8_t *ent;

    if (!gReady || !name || !nameMax || !data || !size)
        return 0;
    n = rd32(gImg + 8);
    if (index >= n)
        return 0;
    ent = gImg + sizeof(RoboFsHeader) + index * sizeof(RoboFsEntry);
    {
        const RoboFsEntry *e = (const RoboFsEntry *)ent;
        for (i = 0; i + 1 < nameMax && i < ROBFS_NAME && e->name[i]; i++)
            name[i] = e->name[i];
        name[i] = 0;
        off = e->off;
        len = e->len;
        if (off > gSize || off + len < off || off + len > gSize)
            return 0;
        *data = gImg + off;
        *size = len;
        return 1;
    }
}

/* Scratch for `FsPayloadBytes`'s walk: it wants each file's *length*, and the accessor hands the
 * name out beside it. Nothing reads the names there, so one file-static buffer serves every entry. */
static char gCountName[ROBFS_NAME];

uint64_t FsPayloadBytes(void)
{
    uint64_t total = 0;
    uint32_t i, n, size;
    const void *data;

    if (!gReady)
        return 0;
    n = rd32(gImg + 8);
    for (i = 0; i < n; i++) {
        if (!FsFileAt(i, gCountName, sizeof(gCountName), &data, &size))
            continue;
        total += size;
    }
    return total;
}

uint32_t FsRootBlocks(uint32_t blockSize)
{
    uint64_t payload, blocks, overhead, slack;
    uint32_t n;

    if (!gReady || !blockSize)
        return 0;
    n = rd32(gImg + 8);
    payload = FsPayloadBytes();
    blocks = (payload + blockSize - 1) / blockSize;
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

void FsReport(void)
{
    uint32_t n;
    if (!gReady) {
        kprintf("RoboFS  missing\n");
        return;
    }
    n = rd32(gImg + 8);
    kprintf("RoboFS  %u files  image %u bytes\n", (unsigned)n, (unsigned)gSize);
}
