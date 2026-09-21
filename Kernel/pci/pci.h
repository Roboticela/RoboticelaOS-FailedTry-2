#ifndef ROBO_PCI_H
#define ROBO_PCI_H

#include <stdint.h>

#define RoboPciMax 64
#define RoboPciBarMem   1u
#define RoboPciBarIo    2u
#define RoboPciBar64    4u
#define RoboPciBarPre   8u

typedef struct RoboPciBar {
    uint64_t addr;
    uint64_t size;
    uint32_t flags;
} RoboPciBar;

typedef struct RoboPciDevice {
    uint8_t bus, dev, func;
    uint16_t vendor, device;
    uint8_t classCode, subclass, progif, revision;
    uint8_t hdr;
    uint16_t subvendor, subdevice;
    uint8_t irqLine, irqPin;
    RoboPciBar bar[6];
} RoboPciDevice;

int PciInit(void);
unsigned PciCount(void);
RoboPciDevice *PciAt(unsigned i);
RoboPciDevice *PciFind(uint16_t vendor, uint16_t device);
uint32_t PciCfgRead32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
void PciCfgWrite32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off,
                          uint32_t v);
uint16_t PciCfgRead16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint8_t PciCfgRead8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
void PciEnableMmio(const RoboPciDevice *d);
void PciEnableIo(const RoboPciDevice *d);
void PciEnableBusmaster(const RoboPciDevice *d);
void PciReport(void);
int PciSelftest(void);
const char *PciClassName(uint8_t classCode, uint8_t subclass);

#endif
