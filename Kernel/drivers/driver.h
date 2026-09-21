#ifndef ROBO_DRIVER_H
#define ROBO_DRIVER_H

#include <stdint.h>
#include "device/device.h"
#include "pci/pci.h"

#define RoboDriverAbiMajor 1u
#define RoboDriverAbiMinor 0u

#define RoboDrvMax 32
#define RoboDrvIdMax 8

#define RoboCapPciConfig (1u << 0)
#define RoboCapMmio       (1u << 1)
#define RoboCapDma        (1u << 2)
#define RoboCapIrq        (1u << 3)
#define RoboCapBlockIo   (1u << 4)
#define RoboCapDisplay    (1u << 5)
#define RoboCapInput      (1u << 6)

#define RoboMatchVendor  (1u << 0)
#define RoboMatchDevice  (1u << 1)
#define RoboMatchClass   (1u << 2)
#define RoboMatchSubsys  (1u << 3)
#define RoboMatchBus     (1u << 4)

#define RoboDrvDiscovered  0u
#define RoboDrvMatched     1u
#define RoboDrvVerified    2u
#define RoboDrvLoaded      3u
#define RoboDrvInitialized 4u
#define RoboDrvRunning     5u
#define RoboDrvFailed      6u
#define RoboDrvStopping    7u
#define RoboDrvUnloaded    8u

#define RoboDrvChannelBuiltin 0u
#define RoboDrvChannelGeneric 1u
#define RoboDrvChannelTest    2u
#define RoboDrvChannelSigned  3u

#define RoboDrvStable 0u
#define RoboDrvTest   1u

typedef struct RoboDriverId {
    uint32_t bus;
    uint16_t vendor;
    uint16_t device;
    uint16_t subvendor;
    uint16_t subdevice;
    uint8_t classCode;
    uint8_t subclass;
    uint8_t progif;
    uint32_t flags;
} RoboDriverId;

typedef struct RoboDriver {
    const char *name;
    const char *vendor;
    uint32_t version;
    uint32_t abiMajor;
    uint32_t abiMinor;
    uint32_t caps;
    uint32_t channel;
    uint32_t stability;
    uint32_t state;
    uint32_t sigOk;
    const RoboDriverId *ids;
    unsigned nid;
    int (*probe)(RoboDevice *device);
    int (*start)(RoboDevice *device);
    int (*stop)(RoboDevice *device);
    int (*remove)(RoboDevice *device);
} RoboDriver;

int DriverAbiOk(uint32_t major, uint32_t minor);
int DriverRegister(RoboDriver *drv);
unsigned DriverCount(void);
RoboDriver *DriverAt(unsigned i);
unsigned DriverMatchScore(const RoboDriver *drv, const RoboDevice *dev);
RoboDriver *DriverBest(const RoboDevice *dev, unsigned *score);
int DriverBind(RoboDevice *dev, RoboDriver *drv);
int DriverUnbind(RoboDevice *dev);

int DriverTestMode(void);
void DriverSetTestMode(int on);

typedef struct RoboDriverStats {
    unsigned registered;
    unsigned loaded;
    unsigned failed;
    unsigned signedOk;
    unsigned rejected;
} RoboDriverStats;

void DriverStats(RoboDriverStats *st);
void DriverNoteReject(void);
int DriverRegistrySelftest(void);

#endif
