#ifndef ROBOSYS_DNS_H
#define ROBOSYS_DNS_H

#include "socket.h"

int DnsResolve(const char *hostname, RoboIPAddress *address);

#endif
