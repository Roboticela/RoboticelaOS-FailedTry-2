#ifndef ROBO_DRIVER_MGR_H
#define ROBO_DRIVER_MGR_H

#include "roboticela/bootinfo.h"

int HwSubsystemInit(const RoboBootInfo *info);
int HwStorageReady(void);
void HwStatus(unsigned *pci, unsigned *reg, unsigned *loaded,
                    unsigned *failed, unsigned *signedOk, unsigned *rejected);
int DriverManagerSelftest(void);

#endif
