#ifndef ROBO_NET_CORE_H
#define ROBO_NET_CORE_H

#include "net/core/interface.h"
#include "net/core/packet.h"

struct RoboThread;

int robo_net_init(void);
int robo_net_bringup(void);
void robo_net_report(void);
int robo_net_ready(void);
void robo_net_poll(void);
void robo_net_set_debug(int on);
int robo_net_debug(void);
void robo_net_wait(struct RoboThread **slot, int *posted);
void robo_net_signal(struct RoboThread **slot, int *posted);
void robo_net_wake_walk(void);

#endif
