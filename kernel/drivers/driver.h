#ifndef ROBO_DRIVER_H
#define ROBO_DRIVER_H

#include <stdint.h>
#include "device/device.h"
#include "pci/pci.h"

#define ROBO_DRIVER_ABI_MAJOR 1u
#define ROBO_DRIVER_ABI_MINOR 0u

#define ROBO_DRV_MAX 32
#define ROBO_DRV_ID_MAX 8

#define ROBO_CAP_PCI_CONFIG (1u << 0)
#define ROBO_CAP_MMIO       (1u << 1)
#define ROBO_CAP_DMA        (1u << 2)
#define ROBO_CAP_IRQ        (1u << 3)
#define ROBO_CAP_BLOCK_IO   (1u << 4)
#define ROBO_CAP_DISPLAY    (1u << 5)
#define ROBO_CAP_INPUT      (1u << 6)

#define ROBO_MATCH_VENDOR  (1u << 0)
#define ROBO_MATCH_DEVICE  (1u << 1)
#define ROBO_MATCH_CLASS   (1u << 2)
#define ROBO_MATCH_SUBSYS  (1u << 3)
#define ROBO_MATCH_BUS     (1u << 4)

#define ROBO_DRV_DISCOVERED  0u
#define ROBO_DRV_MATCHED     1u
#define ROBO_DRV_VERIFIED    2u
#define ROBO_DRV_LOADED      3u
#define ROBO_DRV_INITIALIZED 4u
#define ROBO_DRV_RUNNING     5u
#define ROBO_DRV_FAILED      6u
#define ROBO_DRV_STOPPING    7u
#define ROBO_DRV_UNLOADED    8u

#define ROBO_DRV_CHANNEL_BUILTIN 0u
#define ROBO_DRV_CHANNEL_GENERIC 1u
#define ROBO_DRV_CHANNEL_TEST    2u
#define ROBO_DRV_CHANNEL_SIGNED  3u

#define ROBO_DRV_STABLE 0u
#define ROBO_DRV_TEST   1u

typedef struct RoboDriverId {
    uint32_t bus;
    uint16_t vendor;
    uint16_t device;
    uint16_t subvendor;
    uint16_t subdevice;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t progif;
    uint32_t flags;
} RoboDriverId;

typedef struct RoboDriver {
    const char *name;
    const char *vendor;
    uint32_t version;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t caps;
    uint32_t channel;
    uint32_t stability;
    uint32_t state;
    uint32_t sig_ok;
    const RoboDriverId *ids;
    unsigned nid;
    int (*probe)(RoboDevice *device);
    int (*start)(RoboDevice *device);
    int (*stop)(RoboDevice *device);
    int (*remove)(RoboDevice *device);
} RoboDriver;

int robo_driver_abi_ok(uint32_t major, uint32_t minor);
int robo_driver_register(RoboDriver *drv);
unsigned robo_driver_count(void);
RoboDriver *robo_driver_at(unsigned i);
unsigned robo_driver_match_score(const RoboDriver *drv, const RoboDevice *dev);
RoboDriver *robo_driver_best(const RoboDevice *dev, unsigned *score);
int robo_driver_bind(RoboDevice *dev, RoboDriver *drv);
int robo_driver_unbind(RoboDevice *dev);

int robo_driver_test_mode(void);
void robo_driver_set_test_mode(int on);

typedef struct RoboDriverStats {
    unsigned registered;
    unsigned loaded;
    unsigned failed;
    unsigned signed_ok;
    unsigned rejected;
} RoboDriverStats;

void robo_driver_stats(RoboDriverStats *st);
void robo_driver_note_reject(void);
int robo_driver_registry_selftest(void);

#endif
