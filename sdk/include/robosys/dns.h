#ifndef ROBOSYS_DNS_H
#define ROBOSYS_DNS_H

#include "socket.h"

int robo_dns_resolve(const char *hostname, RoboIPAddress *address);

#endif
