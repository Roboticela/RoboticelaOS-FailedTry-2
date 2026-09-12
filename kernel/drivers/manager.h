#ifndef ROBO_DRIVER_MGR_H
#define ROBO_DRIVER_MGR_H

#include "roboticela/bootinfo.h"

int robo_hw_subsystem_init(const RoboBootInfo *info);
int robo_hw_storage_ready(void);
void robo_hw_status(unsigned *pci, unsigned *reg, unsigned *loaded,
                    unsigned *failed, unsigned *signed_ok, unsigned *rejected);
int robo_driver_manager_selftest(void);

#endif
