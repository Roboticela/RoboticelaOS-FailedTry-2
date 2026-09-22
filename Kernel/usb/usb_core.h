#ifndef ROBO_USB_CORE_H
#define ROBO_USB_CORE_H

#include <stdint.h>

#define RoboUsbMaxDev 16u
#define RoboUsbMaxClass 8u

#define RoboUsbSpeedFull 1u
#define RoboUsbSpeedHigh 2u
#define RoboUsbSpeedSuper 3u

#define RoboUsbClassHid 0x03u
#define RoboUsbClassMsc 0x08u
#define RoboUsbClassHub 0x09u

typedef struct RoboUsbDevice {
    uint8_t slot;
    uint8_t addr;
    uint8_t speed;
    uint8_t port;
    uint8_t classCode;
    uint8_t subclass;
    uint8_t protocol;
    uint8_t config;
    uint16_t vendor;
    uint16_t product;
    uint8_t epIn;
    uint8_t epOut;
    uint16_t maxPacket;
    void *hc;
    void *classData;
    int ready;
} RoboUsbDevice;

typedef struct RoboUsbClassDriver {
    const char *name;
    uint8_t classCode;
    uint8_t subclass;
    uint8_t protocol; /* 0xFF = any */
    int (*attach)(RoboUsbDevice *dev);
    void (*detach)(RoboUsbDevice *dev);
    void (*poll)(RoboUsbDevice *dev);
} RoboUsbClassDriver;

typedef struct RoboUsbHostOps {
    int (*control)(void *hc, RoboUsbDevice *dev, uint8_t reqType,
                   uint8_t req, uint16_t value, uint16_t index,
                   void *data, uint16_t len);
    int (*bulk)(void *hc, RoboUsbDevice *dev, uint8_t ep, int in,
                void *data, uint32_t len);
    int (*interrupt)(void *hc, RoboUsbDevice *dev, uint8_t ep,
                     void *data, uint32_t len);
} RoboUsbHostOps;

int UsbCoreInit(void);
int UsbRegisterClass(const RoboUsbClassDriver *drv);
int UsbRegisterDevice(RoboUsbDevice *dev, const RoboUsbHostOps *ops);
void UsbPoll(void);
unsigned UsbDeviceCount(void);
RoboUsbDevice *UsbDeviceAt(unsigned i);

/* Standard control helpers (need host ops on the device). */
int UsbGetDescriptor(RoboUsbDevice *dev, uint8_t type, uint8_t index,
                     void *buf, uint16_t len);
int UsbSetAddress(RoboUsbDevice *dev, uint8_t addr);
int UsbSetConfiguration(RoboUsbDevice *dev, uint8_t cfg);

#endif
