#include "device/device.h"

static RoboDevice gDev[RoboDevMax];
static unsigned gN;
static uint64_t gNext = 1;

int DeviceInit(void)
{
    gN = 0;
    gNext = 1;
    return 1;
}

RoboDevice *DeviceAdd(uint32_t bus, const char *name, const char *cls,
                            void *busData)
{
    RoboDevice *d;
    if (gN >= RoboDevMax)
        return 0;
    d = &gDev[gN++];
    d->id = gNext++;
    d->name = name ? name : "device";
    d->className = cls ? cls : "unknown";
    d->busType = bus;
    d->ident = 0;
    d->flags = 0;
    d->busData = busData;
    d->driverData = 0;
    d->driver = 0;
    d->matchScore = 0;
    d->status = RoboDevUnbound;
    return d;
}

unsigned DeviceCount(void)
{
    return gN;
}

RoboDevice *DeviceAt(unsigned i)
{
    return i < gN ? &gDev[i] : 0;
}
