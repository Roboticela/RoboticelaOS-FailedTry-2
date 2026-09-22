#ifndef ROBO_XHCI_H
#define ROBO_XHCI_H

#include "drivers/driver.h"

int XhciRegister(void);
unsigned XhciPortCount(void);
unsigned XhciConnectedPorts(void);

#endif
