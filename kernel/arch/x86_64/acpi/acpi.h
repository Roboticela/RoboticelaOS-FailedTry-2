#ifndef ROBO_ACPI_H
#define ROBO_ACPI_H

#include <stdint.h>
#include "roboticela/bootinfo.h"

typedef struct RoboMcfgAlloc {
    uint64_t base;
    uint16_t segment;
    uint8_t start_bus;
    uint8_t end_bus;
} RoboMcfgAlloc;

int robo_acpi_init(const RoboBootInfo *info);
uint64_t robo_acpi_rsdp(void);
int robo_acpi_mcfg_count(void);
int robo_acpi_mcfg_at(unsigned i, RoboMcfgAlloc *out);

#endif
