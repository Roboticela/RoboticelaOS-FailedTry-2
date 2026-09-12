#include "device/device.h"

static RoboDevice g_dev[ROBO_DEV_MAX];
static unsigned g_n;
static uint64_t g_next = 1;

int robo_device_init(void)
{
    g_n = 0;
    g_next = 1;
    return 1;
}

RoboDevice *robo_device_add(uint32_t bus, const char *name, const char *cls,
                            void *bus_data)
{
    RoboDevice *d;
    if (g_n >= ROBO_DEV_MAX)
        return 0;
    d = &g_dev[g_n++];
    d->id = g_next++;
    d->name = name ? name : "device";
    d->class_name = cls ? cls : "unknown";
    d->bus_type = bus;
    d->ident = 0;
    d->flags = 0;
    d->bus_data = bus_data;
    d->driver_data = 0;
    d->driver = 0;
    d->match_score = 0;
    d->status = ROBO_DEV_UNBOUND;
    return d;
}

unsigned robo_device_count(void)
{
    return g_n;
}

RoboDevice *robo_device_at(unsigned i)
{
    return i < g_n ? &g_dev[i] : 0;
}
