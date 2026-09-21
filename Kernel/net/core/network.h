#ifndef ROBO_NET_CORE_H
#define ROBO_NET_CORE_H

#include "net/core/interface.h"
#include "net/core/packet.h"

struct RoboThread;

int NetInit(void);
int NetBringup(void);
void NetReport(void);
int NetReady(void);
void NetPoll(void);
void NetSetDebug(int on);
int NetDebug(void);
void NetWait(struct RoboThread **slot, int *posted);
void NetSignal(struct RoboThread **slot, int *posted);
void NetWakeWalk(void);

#endif
