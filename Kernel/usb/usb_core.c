#include "usb/usb_core.h"
#include "kio.h"

static RoboUsbDevice gDev[RoboUsbMaxDev];
static unsigned gN;
static const RoboUsbClassDriver *gClass[RoboUsbMaxClass];
static unsigned gClassN;
static const RoboUsbHostOps *gOps[RoboUsbMaxDev];
static int gReady;

int UsbCoreInit(void)
{
    unsigned i;
    for (i = 0; i < RoboUsbMaxDev; i++) {
        gDev[i].ready = 0;
        gOps[i] = 0;
    }
    gN = 0;
    gClassN = 0;
    gReady = 1;
    return 1;
}

int UsbRegisterClass(const RoboUsbClassDriver *drv)
{
    if (!gReady)
        UsbCoreInit();
    if (!drv || !drv->attach)
        return -1;
    if (gClassN >= RoboUsbMaxClass)
        return -1;
    gClass[gClassN++] = drv;
    return 0;
}

static const RoboUsbClassDriver *FindClass(const RoboUsbDevice *dev)
{
    unsigned i;
    for (i = 0; i < gClassN; i++) {
        const RoboUsbClassDriver *c = gClass[i];
        if (c->classCode != dev->classCode)
            continue;
        if (c->subclass != 0xFFu && c->subclass != dev->subclass)
            continue;
        if (c->protocol != 0xFFu && c->protocol != dev->protocol)
            continue;
        return c;
    }
    return 0;
}

int UsbRegisterDevice(RoboUsbDevice *dev, const RoboUsbHostOps *ops)
{
    const RoboUsbClassDriver *cls;
    unsigned idx;

    if (!gReady)
        UsbCoreInit();
    if (!dev || !ops || gN >= RoboUsbMaxDev)
        return -1;
    idx = gN++;
    gDev[idx] = *dev;
    gOps[idx] = ops;
    gDev[idx].ready = 1;
    cls = FindClass(&gDev[idx]);
    if (cls) {
        if (cls->attach(&gDev[idx]) == 0)
            kinfo("[INFO] [USB] class %s attached addr %u\n",
                    cls->name, (unsigned)gDev[idx].addr);
    } else {
        kinfo("[INFO] [USB] device class %02x:%02x:%02x (no class driver)\n",
                (unsigned)dev->classCode, (unsigned)dev->subclass,
                (unsigned)dev->protocol);
    }
    return 0;
}

void UsbPoll(void)
{
    unsigned i, c;
    for (i = 0; i < gN; i++) {
        if (!gDev[i].ready)
            continue;
        for (c = 0; c < gClassN; c++) {
            if (gClass[c]->classCode == gDev[i].classCode &&
                gClass[c]->poll)
                gClass[c]->poll(&gDev[i]);
        }
    }
}

unsigned UsbDeviceCount(void)
{
    return gN;
}

RoboUsbDevice *UsbDeviceAt(unsigned i)
{
    return i < gN ? &gDev[i] : 0;
}

static const RoboUsbHostOps *OpsFor(RoboUsbDevice *dev)
{
    unsigned i;
    for (i = 0; i < gN; i++) {
        if (&gDev[i] == dev)
            return gOps[i];
    }
    return 0;
}

int UsbGetDescriptor(RoboUsbDevice *dev, uint8_t type, uint8_t index,
                     void *buf, uint16_t len)
{
    const RoboUsbHostOps *ops = OpsFor(dev);
    if (!ops || !ops->control)
        return -1;
    return ops->control(dev->hc, dev, 0x80, 6,
                        (uint16_t)((type << 8) | index), 0, buf, len);
}

int UsbSetAddress(RoboUsbDevice *dev, uint8_t addr)
{
    const RoboUsbHostOps *ops = OpsFor(dev);
    if (!ops || !ops->control)
        return -1;
    return ops->control(dev->hc, dev, 0x00, 5, addr, 0, 0, 0);
}

int UsbSetConfiguration(RoboUsbDevice *dev, uint8_t cfg)
{
    const RoboUsbHostOps *ops = OpsFor(dev);
    if (!ops || !ops->control)
        return -1;
    return ops->control(dev->hc, dev, 0x00, 9, cfg, 0, 0, 0);
}
