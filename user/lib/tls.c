#include <robosys/tls.h>
#include <robosys/syscall_errors.h>

int robo_tls_connect(int socket, const char *hostname, RoboTLSConnection *out)
{
    (void)socket;
    (void)hostname;
    if (out) {
        out->socket = -1;
        out->state = 0;
    }
    return (int)-ROBO_ENOTSUP;
}

int robo_tls_send(RoboTLSConnection *c, const void *buf, unsigned len)
{
    (void)c;
    (void)buf;
    (void)len;
    return (int)-ROBO_ENOTSUP;
}

int robo_tls_recv(RoboTLSConnection *c, void *buf, unsigned len)
{
    (void)c;
    (void)buf;
    (void)len;
    return (int)-ROBO_ENOTSUP;
}

int robo_tls_close(RoboTLSConnection *c)
{
    (void)c;
    return (int)-ROBO_ENOTSUP;
}
