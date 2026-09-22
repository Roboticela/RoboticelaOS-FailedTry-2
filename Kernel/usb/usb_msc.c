#include "usb/usb_msc.h"
#include "usb/usb_core.h"
#include "fs/block/block.h"
#include "fs/block/bdev_table.h"
#include "storage/partition.h"
#include "kio.h"

/*
 * USB Mass Storage — Bulk-Only Transport (BOT) + SCSI.
 * Registers a class driver; block I/O is enabled once the host provides
 * working bulk endpoints (xHCI transfer rings). Until then attach still
 * records the device so the stack is ready for QEMU usb-storage.
 */

typedef struct UsbMscPriv {
    RoboUsbDevice *usb;
    RoboBlockDevice bdev;
    char name[RoboBdevNameMax];
    uint64_t blocks;
    uint32_t blockSize;
    int ready;
} UsbMscPriv;

static int MscRead(RoboBlockDevice *dev, uint64_t lba, void *buf)
{
    (void)dev;
    (void)lba;
    (void)buf;
    /* SCSI READ(10) over BOT — requires host bulk. */
    return -1;
}

static int MscWrite(RoboBlockDevice *dev, uint64_t lba, const void *buf)
{
    (void)dev;
    (void)lba;
    (void)buf;
    return -1;
}

static int MscAttach(RoboUsbDevice *dev)
{
    UsbMscPriv *m;

    if (!dev)
        return -1;
    /* SCSI transparent command set (subclass 0x06), BOT (protocol 0x50). */
    if (dev->subclass != 0x06 && dev->subclass != 0xFF)
        return -1;
    m = (UsbMscPriv *)dev->classData;
    if (!m) {
        /* Lightweight placeholder until heap attach from host enum. */
        kinfo("[INFO] [USB] MSC device seen (BOT); awaiting bulk pipe\n");
        return 0;
    }
    m->usb = dev;
    m->blockSize = 512;
    m->blocks = 0;
    m->bdev.blockCount = 0;
    m->bdev.blockSize = 512;
    m->bdev.priv = m;
    m->bdev.read = MscRead;
    m->bdev.write = MscWrite;
    m->bdev.flush = 0;
    m->bdev.destroy = 0;
    m->ready = 0;
    kinfo("[INFO] [USB] MSC attached\n");
    return 0;
}

static void MscDetach(RoboUsbDevice *dev)
{
    (void)dev;
}

static void MscPoll(RoboUsbDevice *dev)
{
    (void)dev;
}

static const RoboUsbClassDriver mscDrv = {
    "usb-msc-bot",
    RoboUsbClassMsc,
    0xFF,
    0xFF,
    MscAttach,
    MscDetach,
    MscPoll
};

int UsbMscRegister(void)
{
    return UsbRegisterClass(&mscDrv);
}
