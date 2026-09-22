#include <robo/socket.h>
#include <unistd.h>
#include <internal/libc.h>

#ifndef LIBC_HOST_TEST
#include <robosys/syscall_numbers.h>
#include <robosys/socket_abi.h>

static int sys_fd(robo_long r)
{
    if (robo_set_err(r))
        return -1;
    return (int)r;
}

int robo_socket(int domain, int type, int protocol)
{
    return sys_fd(robo_syscall((robo_long)RoboSysSocket, RoboNetOpSocket,
                               (robo_long)domain, (robo_long)type,
                               (robo_long)protocol, 0, 0));
}

int robo_bind(int fd, const robo_sockaddr_t *addr)
{
    robo_long r = robo_syscall((robo_long)RoboSysSocket, RoboNetOpBind,
                               (robo_long)fd, (robo_long)(uintptr_t)addr,
                               (robo_long)sizeof(*addr), 0, 0);
    return robo_set_err(r) ? -1 : 0;
}

int robo_listen(int fd, int backlog)
{
    robo_long r = robo_syscall((robo_long)RoboSysSocket, RoboNetOpListen,
                               (robo_long)fd, (robo_long)backlog, 0, 0, 0);
    return robo_set_err(r) ? -1 : 0;
}

int robo_accept(int fd, robo_sockaddr_t *addr)
{
    return sys_fd(robo_syscall((robo_long)RoboSysSocket, RoboNetOpAccept,
                               (robo_long)fd, (robo_long)(uintptr_t)addr,
                               (robo_long)sizeof(*addr), 0, 0));
}

int robo_connect(int fd, const robo_sockaddr_t *addr)
{
    robo_long r = robo_syscall((robo_long)RoboSysSocket, RoboNetOpConnect,
                               (robo_long)fd, (robo_long)(uintptr_t)addr,
                               (robo_long)sizeof(*addr), 0, 0);
    return robo_set_err(r) ? -1 : 0;
}

ssize_t robo_send(int fd, const void *buf, size_t n)
{
    robo_long r = robo_syscall((robo_long)RoboSysSocket, RoboNetOpSend,
                               (robo_long)fd, (robo_long)(uintptr_t)buf,
                               (robo_long)n, 0, 0);
    if (robo_set_err(r))
        return -1;
    return (ssize_t)r;
}

ssize_t robo_recv(int fd, void *buf, size_t n)
{
    robo_long r = robo_syscall((robo_long)RoboSysSocket, RoboNetOpRecv,
                               (robo_long)fd, (robo_long)(uintptr_t)buf,
                               (robo_long)n, 0, 0);
    if (robo_set_err(r))
        return -1;
    return (ssize_t)r;
}

#endif
