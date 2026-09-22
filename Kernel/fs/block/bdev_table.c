#include "fs/block/bdev_table.h"
#include "syscall/syscall_errors.h"

static RoboBdevEntry gTab[RoboBdevTableMax];
static unsigned gN;
static unsigned gDiskN;
static int gReady;

static unsigned NameLen(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    return n;
}

static int NameEq(const char *a, const char *b)
{
    unsigned i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return a[i] == 0 && b[i] == 0;
}

static void NameCopy(char *dst, unsigned dstLen, const char *src)
{
    unsigned i = 0;
    if (!dst || !dstLen)
        return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1u < dstLen) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

int BdevTableInit(void)
{
    unsigned i;
    for (i = 0; i < RoboBdevTableMax; i++) {
        gTab[i].name[0] = 0;
        gTab[i].kind = 0;
        gTab[i].index = 0;
        gTab[i].dev = 0;
        gTab[i].parent = 0;
    }
    gN = 0;
    gDiskN = 0;
    gReady = 1;
    return 1;
}

int BdevTableAdd(const char *name, uint32_t kind, RoboBlockDevice *dev,
                 RoboBlockDevice *parent)
{
    unsigned n;

    if (!gReady)
        BdevTableInit();
    if (!name || !dev || !name[0])
        return -RoboEinval;
    if (BdevTableFind(name))
        return -RoboEexist;
    if (gN >= RoboBdevTableMax)
        return -RoboEnomem;
    n = NameLen(name);
    if (n + 1u > RoboBdevNameMax)
        return -RoboEinval;
    NameCopy(gTab[gN].name, RoboBdevNameMax, name);
    gTab[gN].kind = kind;
    gTab[gN].index = (kind == RoboBdevKindDisk) ? gDiskN : gN;
    gTab[gN].dev = dev;
    gTab[gN].parent = parent;
    if (kind == RoboBdevKindDisk)
        gDiskN++;
    gN++;
    return 0;
}

RoboBlockDevice *BdevTableFind(const char *name)
{
    unsigned i;
    if (!name)
        return 0;
    for (i = 0; i < gN; i++) {
        if (NameEq(gTab[i].name, name))
            return gTab[i].dev;
    }
    return 0;
}

unsigned BdevTableCount(void)
{
    return gN;
}

RoboBdevEntry *BdevTableAt(unsigned i)
{
    return i < gN ? &gTab[i] : 0;
}

unsigned BdevTableDiskCount(void)
{
    return gDiskN;
}

int BdevTableNextDiskName(char *out, unsigned outLen)
{
    char tmp[RoboBdevNameMax];
    unsigned n = gDiskN;
    unsigned i = 0;
    unsigned dig = 0;
    char digits[16];

    if (!out || outLen < 6u)
        return -RoboEinval;
    if (n == 0) {
        NameCopy(out, outLen, "disk0");
        return 0;
    }
    while (n && dig < 16u) {
        digits[dig++] = (char)('0' + (n % 10u));
        n /= 10u;
    }
    tmp[0] = 'd';
    tmp[1] = 'i';
    tmp[2] = 's';
    tmp[3] = 'k';
    i = 4;
    while (dig > 0 && i + 1u < RoboBdevNameMax)
        tmp[i++] = digits[--dig];
    tmp[i] = 0;
    NameCopy(out, outLen, tmp);
    return 0;
}
