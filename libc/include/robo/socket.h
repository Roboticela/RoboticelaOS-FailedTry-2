#ifndef ROBO_SOCKET_H
#define ROBO_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define ROBO_AF_INET     2
#define ROBO_SOCK_STREAM 1
#define ROBO_SOCK_DGRAM  2

typedef struct robo_sockaddr {
    uint16_t family;
    uint16_t port;
    uint32_t addr;
    uint32_t zero;
} robo_sockaddr_t;

int robo_socket(int domain, int type, int protocol);
int robo_bind(int fd, const robo_sockaddr_t *addr);
int robo_listen(int fd, int backlog);
int robo_accept(int fd, robo_sockaddr_t *addr);
int robo_connect(int fd, const robo_sockaddr_t *addr);
ssize_t robo_send(int fd, const void *buf, size_t n);
ssize_t robo_recv(int fd, void *buf, size_t n);

#endif
