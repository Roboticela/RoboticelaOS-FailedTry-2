#ifndef ROBOSYS_SOCKET_ABI_H
#define ROBOSYS_SOCKET_ABI_H

#include <stdint.h>

#define RoboAfInet      2
#define RoboSockStream  1
#define RoboSockDgram   2

#define RoboShutRd   0
#define RoboShutWr   1
#define RoboShutRdwr 2

#define RoboNetOpSocket      1
#define RoboNetOpBind        2
#define RoboNetOpListen      3
#define RoboNetOpAccept      4
#define RoboNetOpConnect     5
#define RoboNetOpSend        6
#define RoboNetOpRecv        7
#define RoboNetOpSendto      8
#define RoboNetOpRecvfrom    9
#define RoboNetOpShutdown    10
#define RoboNetOpGetsockname 11
#define RoboNetOpGetpeername 12
#define RoboNetOpInfo        13
#define RoboNetOpPing        14

typedef struct RoboSockAddr {
    uint16_t family;
    uint16_t port;
    uint32_t addr;
    uint32_t zero;
} RoboSockAddr;

typedef struct RoboNetInfo {
    uint32_t present;
    uint32_t linkUp;
    uint32_t mtu;
    uint32_t ipv4;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    uint8_t mac[6];
    uint8_t pad[2];
    char name[16];
} RoboNetInfo;

typedef struct RoboIPAddress {
    uint32_t ipv4;
    uint32_t reserved;
} RoboIPAddress;

#endif
