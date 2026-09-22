#include "storage/partition.h"
#include "fs/block/bdev_table.h"
#include "mm/heap/heap.h"
#include "syscall/syscall_errors.h"
#include "kio.h"

#define RoboPartTrackMax 8u

typedef struct PartPriv {
    RoboBlockDevice *parent;
    uint64_t firstLba;
    uint64_t count;
} PartPriv;

typedef struct PartTrack {
    RoboBlockDevice *disk;
    RoboPartitionTable *table;
} PartTrack;

static PartTrack gTrack[RoboPartTrackMax];
static unsigned gTrackN;

static uint16_t Rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t Rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t Rd64(const uint8_t *p)
{
    return (uint64_t)Rd32(p) | ((uint64_t)Rd32(p + 4) << 32);
}

static void MemCopy(void *dst, const void *src, unsigned n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
}

static void MemZero(void *dst, unsigned n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--)
        *d++ = 0;
}

static int NameAppendPart(char *out, unsigned outLen, const char *disk,
                          unsigned partIndex)
{
    unsigned i = 0;
    unsigned dig = 0;
    char digits[8];
    unsigned n = partIndex;

    if (!out || !disk || outLen < 8u)
        return -RoboEinval;
    while (disk[i] && i + 1u < outLen) {
        out[i] = disk[i];
        i++;
    }
    if (i + 2u >= outLen)
        return -RoboEinval;
    out[i++] = 'p';
    if (n == 0) {
        out[i++] = '0';
        out[i] = 0;
        return 0;
    }
    while (n && dig < 8u) {
        digits[dig++] = (char)('0' + (n % 10u));
        n /= 10u;
    }
    while (dig > 0 && i + 1u < outLen)
        out[i++] = digits[--dig];
    out[i] = 0;
    return 0;
}

static int PartRead(RoboBlockDevice *dev, uint64_t lba, void *buf)
{
    PartPriv *p = (PartPriv *)dev->priv;
    if (!p || !p->parent)
        return -RoboEio;
    if (lba >= p->count)
        return -RoboEio;
    return BdevRead(p->parent, p->firstLba + lba, buf);
}

static int PartWrite(RoboBlockDevice *dev, uint64_t lba, const void *buf)
{
    PartPriv *p = (PartPriv *)dev->priv;
    if (!p || !p->parent)
        return -RoboEio;
    if (lba >= p->count)
        return -RoboEio;
    return BdevWrite(p->parent, p->firstLba + lba, buf);
}

static int PartFlush(RoboBlockDevice *dev)
{
    PartPriv *p = (PartPriv *)dev->priv;
    if (!p || !p->parent)
        return -RoboEio;
    return BdevFlush(p->parent);
}

static int AddPart(RoboPartitionTable *t, uint64_t first, uint64_t count,
                   uint8_t mbrType, const uint8_t *guid,
                   const char *diskName)
{
    RoboPartition *part;
    PartPriv *priv;
    char name[RoboBdevNameMax];
    unsigned idx;

    if (!t || !diskName || !count)
        return 0;
    if (t->count >= RoboPartMax)
        return 0;
    if (first >= t->disk->blockCount)
        return 0;
    if (count > t->disk->blockCount - first)
        count = t->disk->blockCount - first;
    idx = t->count;
    part = &t->parts[idx];
    MemZero(part, sizeof(*part));
    part->index = idx + 1u;
    part->firstLba = first;
    part->blockCount = count;
    part->mbrType = mbrType;
    if (guid)
        MemCopy(part->typeGuid, guid, 16);
    priv = (PartPriv *)kmalloc(sizeof(*priv));
    if (!priv)
        return -RoboEnomem;
    priv->parent = t->disk;
    priv->firstLba = first;
    priv->count = count;
    part->bdev.blockCount = count;
    part->bdev.blockSize = t->disk->blockSize;
    part->bdev.priv = priv;
    part->bdev.read = PartRead;
    part->bdev.write = PartWrite;
    part->bdev.flush = PartFlush;
    part->bdev.destroy = 0;
    if (NameAppendPart(name, sizeof(name), diskName, part->index) != 0) {
        kfree(priv);
        return -RoboEinval;
    }
    if (BdevTableAdd(name, RoboBdevKindPart, &part->bdev, t->disk) != 0) {
        kfree(priv);
        return -RoboEexist;
    }
    t->count++;
    kinfo("[INFO] [STORAGE] partition %s  LBA %llu +%llu\n",
            name, (unsigned long long)first, (unsigned long long)count);
    return 1;
}

static int ProbeMbr(RoboPartitionTable *t, const uint8_t *mbr,
                    const char *diskName)
{
    unsigned i;
    int added = 0;

    if (Rd16(mbr + 510) != 0xAA55u)
        return 0;
    t->scheme = RoboPartSchemeMbr;
    for (i = 0; i < 4u; i++) {
        const uint8_t *e = mbr + 446u + i * 16u;
        uint8_t type = e[4];
        uint32_t first = Rd32(e + 8);
        uint32_t count = Rd32(e + 12);
        if (!type || !count)
            continue;
        if (AddPart(t, first, count, type, 0, diskName) > 0)
            added++;
    }
    return added;
}

static int ProbeGpt(RoboPartitionTable *t, const char *diskName)
{
    uint8_t *hdr;
    uint8_t *ent;
    uint64_t hdrLba = 1;
    uint64_t entLba;
    uint32_t entSize;
    uint32_t entCount;
    uint32_t i;
    uint32_t bs;
    int added = 0;
    unsigned entBytes;
    unsigned entBlocks;

    bs = t->disk->blockSize;
    if (bs < 512u)
        return 0;
    hdr = (uint8_t *)kmalloc(bs);
    if (!hdr)
        return -RoboEnomem;
    if (BdevRead(t->disk, hdrLba, hdr) != 0) {
        kfree(hdr);
        return 0;
    }
    if (hdr[0] != 'E' || hdr[1] != 'F' || hdr[2] != 'I' || hdr[3] != ' ' ||
        hdr[4] != 'P' || hdr[5] != 'A' || hdr[6] != 'R' || hdr[7] != 'T') {
        kfree(hdr);
        return 0;
    }
    entLba = Rd64(hdr + 72);
    entCount = Rd32(hdr + 80);
    entSize = Rd32(hdr + 84);
    kfree(hdr);
    if (!entLba || !entCount || entSize < 128u)
        return 0;
    if (entCount > RoboPartMax)
        entCount = RoboPartMax;
    entBytes = entCount * entSize;
    entBlocks = (entBytes + bs - 1u) / bs;
    if (entBlocks == 0 || entBlocks > 64u)
        return 0;
    ent = (uint8_t *)kmalloc(entBlocks * bs);
    if (!ent)
        return -RoboEnomem;
    if (BdevReadN(t->disk, entLba, entBlocks, ent) != 0) {
        kfree(ent);
        return 0;
    }
    t->scheme = RoboPartSchemeGpt;
    for (i = 0; i < entCount; i++) {
        const uint8_t *e = ent + i * entSize;
        uint64_t first = Rd64(e + 32);
        uint64_t last = Rd64(e + 40);
        uint64_t count;
        unsigned z;
        int empty = 1;
        for (z = 0; z < 16u; z++) {
            if (e[z]) {
                empty = 0;
                break;
            }
        }
        if (empty || last < first)
            continue;
        count = last - first + 1ull;
        if (AddPart(t, first, count, 0, e, diskName) > 0)
            added++;
    }
    kfree(ent);
    return added;
}

static RoboPartitionTable *TrackFind(const RoboBlockDevice *disk)
{
    unsigned i;
    for (i = 0; i < gTrackN; i++) {
        if (gTrack[i].disk == disk)
            return gTrack[i].table;
    }
    return 0;
}

int PartitionProbe(RoboBlockDevice *disk, const char *diskName)
{
    uint8_t *mbr;
    RoboPartitionTable *t;
    int n;
    uint32_t bs;

    if (!disk || !diskName || !disk->blockSize || !disk->blockCount)
        return -RoboEinval;
    if (TrackFind(disk))
        return (int)PartitionCount(disk);

    t = (RoboPartitionTable *)kmalloc(sizeof(*t));
    if (!t)
        return -RoboEnomem;
    MemZero(t, sizeof(*t));
    t->disk = disk;
    t->scheme = RoboPartSchemeNone;

    bs = disk->blockSize;
    mbr = (uint8_t *)kmalloc(bs);
    if (!mbr) {
        kfree(t);
        return -RoboEnomem;
    }
    if (BdevRead(disk, 0, mbr) != 0) {
        kfree(mbr);
        kfree(t);
        return -RoboEio;
    }

    /* Protective MBR type 0xEE usually means GPT. Prefer GPT. */
    n = ProbeGpt(t, diskName);
    if (n <= 0)
        n = ProbeMbr(t, mbr, diskName);
    kfree(mbr);

    if (gTrackN < RoboPartTrackMax) {
        gTrack[gTrackN].disk = disk;
        gTrack[gTrackN].table = t;
        gTrackN++;
    } else {
        kfree(t);
        return n > 0 ? n : 0;
    }
    if (n < 0)
        return n;
    kinfo("[INFO] [STORAGE] %s  %u partition(s)  scheme %u\n",
            diskName, t->count, (unsigned)t->scheme);
    return (int)t->count;
}

unsigned PartitionCount(const RoboBlockDevice *disk)
{
    RoboPartitionTable *t = TrackFind(disk);
    return t ? t->count : 0;
}

RoboBlockDevice *PartitionAt(const RoboBlockDevice *disk, unsigned i)
{
    RoboPartitionTable *t = TrackFind(disk);
    if (!t || i >= t->count)
        return 0;
    return &t->parts[i].bdev;
}
