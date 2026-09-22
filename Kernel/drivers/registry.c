#include "drivers/driver.h"
#include "pci/pci.h"
#include "syscall/syscall_errors.h"

#ifdef DRIVER_HOST_TEST
#include <stdio.h>
#define kprintf printf
#else
#include "kio.h"
#endif

static RoboDriver *gDrv[RoboDrvMax];
static unsigned gN;
static int gTest;
static unsigned gRejected;

int DriverAbiOk(uint32_t major, uint32_t minor)
{
    if (major != RoboDriverAbiMajor)
        return 0;
    if (minor > RoboDriverAbiMinor)
        return 0;
    return 1;
}

void DriverSetTestMode(int on)
{
    gTest = on ? 1 : 0;
}

int DriverTestMode(void)
{
    return gTest;
}

int DriverRegister(RoboDriver *drv)
{
    unsigned i;
    if (!drv || !drv->name)
        return -RoboEinval;
    if (!DriverAbiOk(drv->abiMajor, drv->abiMinor))
        return -RoboEbadabi;
    for (i = 0; i < gN; i++) {
        if (gDrv[i] == drv)
            return -RoboEexist;
        if (gDrv[i]->name && drv->name) {
            unsigned k = 0;
            while (gDrv[i]->name[k] && drv->name[k] &&
                   gDrv[i]->name[k] == drv->name[k])
                k++;
            if (gDrv[i]->name[k] == 0 && drv->name[k] == 0)
                return -RoboEexist;
        }
    }
    if (gN >= RoboDrvMax)
        return -RoboEnomem;
    if (drv->state == RoboDrvDiscovered)
        drv->state = RoboDrvLoaded;
    gDrv[gN++] = drv;
    return 0;
}

unsigned DriverCount(void)
{
    return gN;
}

RoboDriver *DriverAt(unsigned i)
{
    return i < gN ? gDrv[i] : 0;
}

unsigned DriverMatchScore(const RoboDriver *drv, const RoboDevice *dev)
{
    unsigned i, best = 0;
    RoboPciDevice *pci;

    if (!drv || !dev)
        return 0;
    for (i = 0; i < drv->nid; i++) {
        const RoboDriverId *id = &drv->ids[i];
        unsigned s = 0;
        if ((id->flags & RoboMatchBus) && id->bus != dev->busType)
            continue;
        if (dev->busType == RoboBusPlatform) {
            if ((id->flags & RoboMatchBus) && id->bus != RoboBusPlatform)
                continue;
            if ((id->flags & RoboMatchDevice) &&
                id->device != (uint16_t)dev->ident)
                continue;
            if (id->bus == RoboBusPlatform)
                s = 50;
            if (id->flags & RoboMatchDevice)
                s += 40;
        } else if (dev->busType == RoboBusPci) {
            pci = (RoboPciDevice *)dev->busData;
            if (!pci)
                continue;
            if ((id->flags & RoboMatchVendor) && id->vendor != pci->vendor)
                continue;
            if ((id->flags & RoboMatchDevice) && id->device != pci->device)
                continue;
            if ((id->flags & RoboMatchSubsys) &&
                (id->subvendor != pci->subvendor ||
                 id->subdevice != pci->subdevice))
                continue;
            if ((id->flags & RoboMatchClass) &&
                (id->classCode != pci->classCode ||
                 id->subclass != pci->subclass))
                continue;
            if ((id->flags & RoboMatchClass) && id->progif != 0 &&
                id->progif != pci->progif)
                continue;
            if (id->flags & RoboMatchVendor)
                s += 40;
            if (id->flags & RoboMatchDevice)
                s += 40;
            if (id->flags & RoboMatchSubsys)
                s += 20;
            if (id->flags & RoboMatchClass)
                s += 10;
            if ((id->flags & RoboMatchClass) && id->progif != 0 &&
                id->progif == pci->progif)
                s += 5;
        }
        if (s > best)
            best = s;
    }
    return best;
}

RoboDriver *DriverBest(const RoboDevice *dev, unsigned *score)
{
    unsigned i, bestS = 0;
    RoboDriver *best = 0;
    for (i = 0; i < gN; i++) {
        unsigned s = DriverMatchScore(gDrv[i], dev);
        if (s > bestS) {
            bestS = s;
            best = gDrv[i];
        }
    }
    if (score)
        *score = bestS;
    return best;
}

int DriverBind(RoboDevice *dev, RoboDriver *drv)
{
    if (!dev || !drv)
        return -RoboEinval;
    if (dev->driver)
        return -RoboEbusy;
    drv->state = RoboDrvMatched;
    if (drv->probe && drv->probe(dev) != 0) {
        drv->state = RoboDrvFailed;
        dev->status = RoboDevFailed;
        return -RoboEio;
    }
    drv->state = RoboDrvInitialized;
    if (drv->start && drv->start(dev) != 0) {
        drv->state = RoboDrvFailed;
        dev->status = RoboDevFailed;
        if (drv->remove)
            drv->remove(dev);
        return -RoboEio;
    }
    drv->state = RoboDrvRunning;
    dev->driver = drv;
    dev->status = RoboDevBound;
    return 0;
}

int DriverUnbind(RoboDevice *dev)
{
    RoboDriver *drv;
    if (!dev || !dev->driver)
        return -RoboEinval;
    drv = dev->driver;
    drv->state = RoboDrvStopping;
    if (drv->stop)
        drv->stop(dev);
    if (drv->remove)
        drv->remove(dev);
    drv->state = RoboDrvUnloaded;
    dev->driver = 0;
    dev->driverData = 0;
    dev->status = RoboDevUnbound;
    return 0;
}

void DriverStats(RoboDriverStats *st)
{
    unsigned i;
    if (!st)
        return;
    st->registered = gN;
    st->loaded = 0;
    st->failed = 0;
    st->signedOk = 0;
    st->rejected = gRejected;
    for (i = 0; i < gN; i++) {
        if (gDrv[i]->state == RoboDrvRunning ||
            gDrv[i]->state == RoboDrvLoaded ||
            gDrv[i]->state == RoboDrvInitialized)
            st->loaded++;
        if (gDrv[i]->state == RoboDrvFailed)
            st->failed++;
        if (gDrv[i]->sigOk)
            st->signedOk++;
    }
}

void DriverNoteReject(void)
{
    gRejected++;
}

int DriverRegistrySelftest(void)
{
    static RoboDriverId idsA[] = {
        { RoboBusPci, 0x1234, 0x5678, 0, 0, 0, 0, 0,
          RoboMatchBus | RoboMatchVendor | RoboMatchDevice }
    };
    static RoboDriverId idsC[] = {
        { RoboBusPci, 0, 0, 0, 0, 0x03, 0x00, 0,
          RoboMatchBus | RoboMatchClass }
    };
    static RoboDriver da, dc;
    RoboPciDevice pci;
    RoboDevice dev;
    unsigned sa, sc;
    int ok = 1;

    da.name = "exact-test";
    da.vendor = "test";
    da.version = 1;
    da.abiMajor = 1;
    da.abiMinor = 0;
    da.ids = idsA;
    da.nid = 1;
    da.probe = 0;
    da.start = 0;
    da.stop = 0;
    da.remove = 0;
    da.state = RoboDrvDiscovered;
    dc = da;
    dc.name = "class-test";
    dc.ids = idsC;

    pci.vendor = 0x1234;
    pci.device = 0x5678;
    pci.classCode = 0x03;
    pci.subclass = 0x00;
    pci.subvendor = 0;
    pci.subdevice = 0;
    dev.busType = RoboBusPci;
    dev.busData = &pci;

    sa = DriverMatchScore(&da, &dev);
    sc = DriverMatchScore(&dc, &dev);
    if (sa <= sc)
        ok = 0;

    da.abiMajor = 2;
    if (DriverAbiOk(da.abiMajor, 0))
        ok = 0;
    da.abiMajor = 1;
    return ok;
}
