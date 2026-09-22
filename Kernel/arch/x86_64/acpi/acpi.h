#ifndef ROBO_ACPI_H
#define ROBO_ACPI_H

#include <stdint.h>
#include "roboticela/bootinfo.h"

typedef struct RoboMcfgAlloc {
    uint64_t base;
    uint16_t segment;
    uint8_t startBus;
    uint8_t endBus;
} RoboMcfgAlloc;

typedef struct RoboFadtInfo {
    uint16_t sciInterrupt;
    uint32_t smiCommand;
    uint32_t pm1aEvent;
    uint32_t pm1aControl;
    uint64_t resetReg;
    uint8_t resetValue;
    uint32_t flags;
} RoboFadtInfo;

typedef struct RoboMadtInfo {
    uint64_t localApicAddress;
    uint32_t flags;
    unsigned cpuCount;
    unsigned ioApicCount;
} RoboMadtInfo;

int AcpiInit(const RoboBootInfo *info);
uint64_t AcpiRsdp(void);
int AcpiMcfgCount(void);
int AcpiMcfgAt(unsigned i, RoboMcfgAlloc *out);
int AcpiFadtGet(RoboFadtInfo *out);
int AcpiMadtGet(RoboMadtInfo *out);

#endif
