#ifndef ROBOSYS_SOCKET_H
#define ROBOSYS_SOCKET_H

#include "syscall.h"
#include "socket_abi.h"

static inline Long Socket(int domain, int type, int protocol)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpSocket,
                        (Long)domain, (Long)type,
                        (Long)protocol, 0, 0);
}

static inline Long Bind(int socket, const RoboSockAddr *address)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpBind,
                        (Long)socket, (Long)(uintptr_t)address,
                        (Long)sizeof(RoboSockAddr), 0, 0);
}

static inline Long Listen(int socket, int backlog)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpListen,
                        (Long)socket, (Long)backlog, 0, 0, 0);
}

static inline Long Accept(int socket, RoboSockAddr *address)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpAccept,
                        (Long)socket, (Long)(uintptr_t)address,
                        (Long)sizeof(RoboSockAddr), 0, 0);
}

static inline Long Connect(int socket, const RoboSockAddr *address)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpConnect,
                        (Long)socket, (Long)(uintptr_t)address,
                        (Long)sizeof(RoboSockAddr), 0, 0);
}

static inline Long Send(int socket, const void *buffer, Ulong length)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpSend,
                        (Long)socket, (Long)(uintptr_t)buffer,
                        (Long)length, 0, 0);
}

static inline Long Recv(int socket, void *buffer, Ulong length)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpRecv,
                        (Long)socket, (Long)(uintptr_t)buffer,
                        (Long)length, 0, 0);
}

static inline Long Sendto(int socket, const void *buffer,
                                    Ulong length, const RoboSockAddr *address)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpSendto,
                        (Long)socket, (Long)(uintptr_t)buffer,
                        (Long)length, 0,
                        (Long)(uintptr_t)address);
}

static inline Long Recvfrom(int socket, void *buffer,
                                      Ulong length, RoboSockAddr *address)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpRecvfrom,
                        (Long)socket, (Long)(uintptr_t)buffer,
                        (Long)length, 0,
                        (Long)(uintptr_t)address);
}

static inline Long Shutdown(int socket, int how)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpShutdown,
                        (Long)socket, (Long)how, 0, 0, 0);
}

static inline Long Getsockname(int socket, RoboSockAddr *address)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpGetsockname,
                        (Long)socket, (Long)(uintptr_t)address,
                        (Long)sizeof(RoboSockAddr), 0, 0);
}

static inline Long Getpeername(int socket, RoboSockAddr *address)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpGetpeername,
                        (Long)socket, (Long)(uintptr_t)address,
                        (Long)sizeof(RoboSockAddr), 0, 0);
}

static inline Long NetInfo(RoboNetInfo *out)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpInfo,
                        0, (Long)(uintptr_t)out,
                        (Long)sizeof(RoboNetInfo), 0, 0);
}

static inline Long NetPing(uint32_t ipv4, uint32_t timeoutMs)
{
    return Syscall((Long)RoboSysSocket, RoboNetOpPing,
                        (Long)ipv4, (Long)timeoutMs, 0, 0, 0);
}

#endif
