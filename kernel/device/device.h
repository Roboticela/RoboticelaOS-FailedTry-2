#ifndef ROBO_DEVICE_H
#define ROBO_DEVICE_H

#include <stdint.h>

#define ROBO_BUS_PLATFORM 1u
#define ROBO_BUS_PCI      2u

#define ROBO_PLAT_FB    1u
#define ROBO_PLAT_KBD   2u
#define ROBO_PLAT_MOUSE 3u

#define ROBO_DEV_MAX 96

struct RoboDriver;

typedef struct RoboDevice {
    uint64_t id;
    const char *name;
    const char *class_name;
    uint32_t bus_type;
    uint32_t ident;
    uint32_t flags;
    void *bus_data;
    void *driver_data;
    struct RoboDriver *driver;
    uint32_t match_score;
    uint32_t status;
} RoboDevice;

#define ROBO_DEV_UNBOUND   0u
#define ROBO_DEV_BOUND     1u
#define ROBO_DEV_FAILED    2u
#define ROBO_DEV_UNSUPPORTED 3u

int robo_device_init(void);
RoboDevice *robo_device_add(uint32_t bus, const char *name, const char *cls,
                            void *bus_data);
unsigned robo_device_count(void);
RoboDevice *robo_device_at(unsigned i);

#endif
