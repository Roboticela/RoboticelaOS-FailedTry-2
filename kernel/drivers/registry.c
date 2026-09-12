#include "drivers/driver.h"
#include "pci/pci.h"
#include "syscall/syscall_errors.h"

#ifdef DRIVER_HOST_TEST
#include <stdio.h>
#define kprintf printf
#else
#include "kio.h"
#endif

static RoboDriver *g_drv[ROBO_DRV_MAX];
static unsigned g_n;
static int g_test;
static unsigned g_rejected;

int robo_driver_abi_ok(uint32_t major, uint32_t minor)
{
    if (major != ROBO_DRIVER_ABI_MAJOR)
        return 0;
    if (minor > ROBO_DRIVER_ABI_MINOR)
        return 0;
    return 1;
}

void robo_driver_set_test_mode(int on)
{
    g_test = on ? 1 : 0;
}

int robo_driver_test_mode(void)
{
    return g_test;
}

int robo_driver_register(RoboDriver *drv)
{
    unsigned i;
    if (!drv || !drv->name)
        return -ROBO_EINVAL;
    if (!robo_driver_abi_ok(drv->abi_major, drv->abi_minor))
        return -ROBO_EBADABI;
    for (i = 0; i < g_n; i++) {
        if (g_drv[i] == drv)
            return -ROBO_EEXIST;
        if (g_drv[i]->name && drv->name) {
            unsigned k = 0;
            while (g_drv[i]->name[k] && drv->name[k] &&
                   g_drv[i]->name[k] == drv->name[k])
                k++;
            if (g_drv[i]->name[k] == 0 && drv->name[k] == 0)
                return -ROBO_EEXIST;
        }
    }
    if (g_n >= ROBO_DRV_MAX)
        return -ROBO_ENOMEM;
    if (drv->state == ROBO_DRV_DISCOVERED)
        drv->state = ROBO_DRV_LOADED;
    g_drv[g_n++] = drv;
    return 0;
}

unsigned robo_driver_count(void)
{
    return g_n;
}

RoboDriver *robo_driver_at(unsigned i)
{
    return i < g_n ? g_drv[i] : 0;
}

unsigned robo_driver_match_score(const RoboDriver *drv, const RoboDevice *dev)
{
    unsigned i, best = 0;
    RoboPciDevice *pci;

    if (!drv || !dev)
        return 0;
    for (i = 0; i < drv->nid; i++) {
        const RoboDriverId *id = &drv->ids[i];
        unsigned s = 0;
        if ((id->flags & ROBO_MATCH_BUS) && id->bus != dev->bus_type)
            continue;
        if (dev->bus_type == ROBO_BUS_PLATFORM) {
            if ((id->flags & ROBO_MATCH_BUS) && id->bus != ROBO_BUS_PLATFORM)
                continue;
            if ((id->flags & ROBO_MATCH_DEVICE) &&
                id->device != (uint16_t)dev->ident)
                continue;
            if (id->bus == ROBO_BUS_PLATFORM)
                s = 50;
            if (id->flags & ROBO_MATCH_DEVICE)
                s += 40;
        } else if (dev->bus_type == ROBO_BUS_PCI) {
            pci = (RoboPciDevice *)dev->bus_data;
            if (!pci)
                continue;
            if ((id->flags & ROBO_MATCH_VENDOR) && id->vendor != pci->vendor)
                continue;
            if ((id->flags & ROBO_MATCH_DEVICE) && id->device != pci->device)
                continue;
            if ((id->flags & ROBO_MATCH_SUBSYS) &&
                (id->subvendor != pci->subvendor ||
                 id->subdevice != pci->subdevice))
                continue;
            if ((id->flags & ROBO_MATCH_CLASS) &&
                (id->class_code != pci->class_code ||
                 id->subclass != pci->subclass))
                continue;
            if (id->flags & ROBO_MATCH_VENDOR)
                s += 40;
            if (id->flags & ROBO_MATCH_DEVICE)
                s += 40;
            if (id->flags & ROBO_MATCH_SUBSYS)
                s += 20;
            if (id->flags & ROBO_MATCH_CLASS)
                s += 10;
        }
        if (s > best)
            best = s;
    }
    return best;
}

RoboDriver *robo_driver_best(const RoboDevice *dev, unsigned *score)
{
    unsigned i, best_s = 0;
    RoboDriver *best = 0;
    for (i = 0; i < g_n; i++) {
        unsigned s = robo_driver_match_score(g_drv[i], dev);
        if (s > best_s) {
            best_s = s;
            best = g_drv[i];
        }
    }
    if (score)
        *score = best_s;
    return best;
}

int robo_driver_bind(RoboDevice *dev, RoboDriver *drv)
{
    if (!dev || !drv)
        return -ROBO_EINVAL;
    if (dev->driver)
        return -ROBO_EBUSY;
    drv->state = ROBO_DRV_MATCHED;
    if (drv->probe && drv->probe(dev) != 0) {
        drv->state = ROBO_DRV_FAILED;
        dev->status = ROBO_DEV_FAILED;
        return -ROBO_EIO;
    }
    drv->state = ROBO_DRV_INITIALIZED;
    if (drv->start && drv->start(dev) != 0) {
        drv->state = ROBO_DRV_FAILED;
        dev->status = ROBO_DEV_FAILED;
        if (drv->remove)
            drv->remove(dev);
        return -ROBO_EIO;
    }
    drv->state = ROBO_DRV_RUNNING;
    dev->driver = drv;
    dev->status = ROBO_DEV_BOUND;
    return 0;
}

int robo_driver_unbind(RoboDevice *dev)
{
    RoboDriver *drv;
    if (!dev || !dev->driver)
        return -ROBO_EINVAL;
    drv = dev->driver;
    drv->state = ROBO_DRV_STOPPING;
    if (drv->stop)
        drv->stop(dev);
    if (drv->remove)
        drv->remove(dev);
    drv->state = ROBO_DRV_UNLOADED;
    dev->driver = 0;
    dev->driver_data = 0;
    dev->status = ROBO_DEV_UNBOUND;
    return 0;
}

void robo_driver_stats(RoboDriverStats *st)
{
    unsigned i;
    if (!st)
        return;
    st->registered = g_n;
    st->loaded = 0;
    st->failed = 0;
    st->signed_ok = 0;
    st->rejected = g_rejected;
    for (i = 0; i < g_n; i++) {
        if (g_drv[i]->state == ROBO_DRV_RUNNING ||
            g_drv[i]->state == ROBO_DRV_LOADED ||
            g_drv[i]->state == ROBO_DRV_INITIALIZED)
            st->loaded++;
        if (g_drv[i]->state == ROBO_DRV_FAILED)
            st->failed++;
        if (g_drv[i]->sig_ok)
            st->signed_ok++;
    }
}

void robo_driver_note_reject(void)
{
    g_rejected++;
}

int robo_driver_registry_selftest(void)
{
    static RoboDriverId ids_a[] = {
        { ROBO_BUS_PCI, 0x1234, 0x5678, 0, 0, 0, 0, 0,
          ROBO_MATCH_BUS | ROBO_MATCH_VENDOR | ROBO_MATCH_DEVICE }
    };
    static RoboDriverId ids_c[] = {
        { ROBO_BUS_PCI, 0, 0, 0, 0, 0x03, 0x00, 0,
          ROBO_MATCH_BUS | ROBO_MATCH_CLASS }
    };
    static RoboDriver da, dc;
    RoboPciDevice pci;
    RoboDevice dev;
    unsigned sa, sc;
    int ok = 1;

    da.name = "exact-test";
    da.vendor = "test";
    da.version = 1;
    da.abi_major = 1;
    da.abi_minor = 0;
    da.ids = ids_a;
    da.nid = 1;
    da.probe = 0;
    da.start = 0;
    da.stop = 0;
    da.remove = 0;
    da.state = ROBO_DRV_DISCOVERED;
    dc = da;
    dc.name = "class-test";
    dc.ids = ids_c;

    pci.vendor = 0x1234;
    pci.device = 0x5678;
    pci.class_code = 0x03;
    pci.subclass = 0x00;
    pci.subvendor = 0;
    pci.subdevice = 0;
    dev.bus_type = ROBO_BUS_PCI;
    dev.bus_data = &pci;

    sa = robo_driver_match_score(&da, &dev);
    sc = robo_driver_match_score(&dc, &dev);
    if (sa <= sc)
        ok = 0;

    da.abi_major = 2;
    if (robo_driver_abi_ok(da.abi_major, 0))
        ok = 0;
    da.abi_major = 1;
    return ok;
}
