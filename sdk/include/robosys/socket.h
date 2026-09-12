#ifndef ROBOSYS_SOCKET_H
#define ROBOSYS_SOCKET_H

#include "syscall.h"
#include "socket_abi.h"

static inline robo_long robo_socket(int domain, int type, int protocol)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_SOCKET,
                        (robo_long)domain, (robo_long)type,
                        (robo_long)protocol, 0, 0);
}

static inline robo_long robo_bind(int socket, const RoboSockAddr *address)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_BIND,
                        (robo_long)socket, (robo_long)(uintptr_t)address,
                        (robo_long)sizeof(RoboSockAddr), 0, 0);
}

static inline robo_long robo_listen(int socket, int backlog)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_LISTEN,
                        (robo_long)socket, (robo_long)backlog, 0, 0, 0);
}

static inline robo_long robo_accept(int socket, RoboSockAddr *address)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_ACCEPT,
                        (robo_long)socket, (robo_long)(uintptr_t)address,
                        (robo_long)sizeof(RoboSockAddr), 0, 0);
}

static inline robo_long robo_connect(int socket, const RoboSockAddr *address)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_CONNECT,
                        (robo_long)socket, (robo_long)(uintptr_t)address,
                        (robo_long)sizeof(RoboSockAddr), 0, 0);
}

static inline robo_long robo_send(int socket, const void *buffer, robo_ulong length)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_SEND,
                        (robo_long)socket, (robo_long)(uintptr_t)buffer,
                        (robo_long)length, 0, 0);
}

static inline robo_long robo_recv(int socket, void *buffer, robo_ulong length)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_RECV,
                        (robo_long)socket, (robo_long)(uintptr_t)buffer,
                        (robo_long)length, 0, 0);
}

static inline robo_long robo_sendto(int socket, const void *buffer,
                                    robo_ulong length, const RoboSockAddr *address)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_SENDTO,
                        (robo_long)socket, (robo_long)(uintptr_t)buffer,
                        (robo_long)length, 0,
                        (robo_long)(uintptr_t)address);
}

static inline robo_long robo_recvfrom(int socket, void *buffer,
                                      robo_ulong length, RoboSockAddr *address)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_RECVFROM,
                        (robo_long)socket, (robo_long)(uintptr_t)buffer,
                        (robo_long)length, 0,
                        (robo_long)(uintptr_t)address);
}

static inline robo_long robo_shutdown(int socket, int how)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_SHUTDOWN,
                        (robo_long)socket, (robo_long)how, 0, 0, 0);
}

static inline robo_long robo_getsockname(int socket, RoboSockAddr *address)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_GETSOCKNAME,
                        (robo_long)socket, (robo_long)(uintptr_t)address,
                        (robo_long)sizeof(RoboSockAddr), 0, 0);
}

static inline robo_long robo_getpeername(int socket, RoboSockAddr *address)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_GETPEERNAME,
                        (robo_long)socket, (robo_long)(uintptr_t)address,
                        (robo_long)sizeof(RoboSockAddr), 0, 0);
}

static inline robo_long robo_net_info(RoboNetInfo *out)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_INFO,
                        0, (robo_long)(uintptr_t)out,
                        (robo_long)sizeof(RoboNetInfo), 0, 0);
}

static inline robo_long robo_net_ping(uint32_t ipv4, uint32_t timeout_ms)
{
    return robo_syscall((robo_long)ROBO_SYS_SOCKET, ROBO_NET_OP_PING,
                        (robo_long)ipv4, (robo_long)timeout_ms, 0, 0, 0);
}

#endif
