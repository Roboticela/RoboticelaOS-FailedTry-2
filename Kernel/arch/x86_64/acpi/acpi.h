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

int AcpiInit(const RoboBootInfo *info);
uint64_t AcpiRsdp(void);
int AcpiMcfgCount(void);
int AcpiMcfgAt(unsigned i, RoboMcfgAlloc *out);

#endif
