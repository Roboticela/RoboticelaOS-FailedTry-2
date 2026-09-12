#ifndef ROBOSYS_SOCKET_ABI_H
#define ROBOSYS_SOCKET_ABI_H

#include <stdint.h>

#define ROBO_AF_INET      2
#define ROBO_SOCK_STREAM  1
#define ROBO_SOCK_DGRAM   2

#define ROBO_SHUT_RD   0
#define ROBO_SHUT_WR   1
#define ROBO_SHUT_RDWR 2

#define ROBO_NET_OP_SOCKET      1
#define ROBO_NET_OP_BIND        2
#define ROBO_NET_OP_LISTEN      3
#define ROBO_NET_OP_ACCEPT      4
#define ROBO_NET_OP_CONNECT     5
#define ROBO_NET_OP_SEND        6
#define ROBO_NET_OP_RECV        7
#define ROBO_NET_OP_SENDTO      8
#define ROBO_NET_OP_RECVFROM    9
#define ROBO_NET_OP_SHUTDOWN    10
#define ROBO_NET_OP_GETSOCKNAME 11
#define ROBO_NET_OP_GETPEERNAME 12
#define ROBO_NET_OP_INFO        13
#define ROBO_NET_OP_PING        14

typedef struct RoboSockAddr {
    uint16_t family;
    uint16_t port;
    uint32_t addr;
    uint32_t zero;
} RoboSockAddr;

typedef struct RoboNetInfo {
    uint32_t present;
    uint32_t link_up;
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
