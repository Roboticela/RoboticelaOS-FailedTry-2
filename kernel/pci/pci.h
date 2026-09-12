#ifndef ROBO_PCI_H
#define ROBO_PCI_H

#include <stdint.h>

#define ROBO_PCI_MAX 64
#define ROBO_PCI_BAR_MEM   1u
#define ROBO_PCI_BAR_IO    2u
#define ROBO_PCI_BAR_64    4u
#define ROBO_PCI_BAR_PRE   8u

typedef struct RoboPciBar {
    uint64_t addr;
    uint64_t size;
    uint32_t flags;
} RoboPciBar;

typedef struct RoboPciDevice {
    uint8_t bus, dev, func;
    uint16_t vendor, device;
    uint8_t class_code, subclass, progif, revision;
    uint8_t hdr;
    uint16_t subvendor, subdevice;
    uint8_t irq_line, irq_pin;
    RoboPciBar bar[6];
} RoboPciDevice;

int robo_pci_init(void);
unsigned robo_pci_count(void);
RoboPciDevice *robo_pci_at(unsigned i);
RoboPciDevice *robo_pci_find(uint16_t vendor, uint16_t device);
uint32_t robo_pci_cfg_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
void robo_pci_cfg_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off,
                          uint32_t v);
uint16_t robo_pci_cfg_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint8_t robo_pci_cfg_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
void robo_pci_enable_mmio(const RoboPciDevice *d);
void robo_pci_enable_io(const RoboPciDevice *d);
void robo_pci_enable_busmaster(const RoboPciDevice *d);
void robo_pci_report(void);
int robo_pci_selftest(void);
const char *robo_pci_class_name(uint8_t class_code, uint8_t subclass);

#endif
