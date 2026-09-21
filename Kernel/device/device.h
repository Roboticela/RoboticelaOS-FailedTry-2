#ifndef ROBO_DEVICE_H
#define ROBO_DEVICE_H

#include <stdint.h>

#define RoboBusPlatform 1u
#define RoboBusPci      2u

#define RoboPlatFb    1u
#define RoboPlatKbd   2u
#define RoboPlatMouse 3u

#define RoboDevMax 96

struct RoboDriver;

typedef struct RoboDevice {
    uint64_t id;
    const char *name;
    const char *className;
    uint32_t busType;
    uint32_t ident;
    uint32_t flags;
    void *busData;
    void *driverData;
    struct RoboDriver *driver;
    uint32_t matchScore;
    uint32_t status;
} RoboDevice;

#define RoboDevUnbound   0u
#define RoboDevBound     1u
#define RoboDevFailed    2u
#define RoboDevUnsupported 3u

int DeviceInit(void);
RoboDevice *DeviceAdd(uint32_t bus, const char *name, const char *cls,
                            void *busData);
unsigned DeviceCount(void);
RoboDevice *DeviceAt(unsigned i);

#endif
