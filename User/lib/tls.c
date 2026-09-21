#include <robosys/tls.h>
#include <robosys/syscallErrors.h>

int TlsConnect(int socket, const char *hostname, RoboTLSConnection *out)
{
    (void)socket;
    (void)hostname;
    if (out) {
        out->socket = -1;
        out->state = 0;
    }
    return (int)-RoboEnotsup;
}

int TlsSend(RoboTLSConnection *c, const void *buf, unsigned len)
{
    (void)c;
    (void)buf;
    (void)len;
    return (int)-RoboEnotsup;
}

int TlsRecv(RoboTLSConnection *c, void *buf, unsigned len)
{
    (void)c;
    (void)buf;
    (void)len;
    return (int)-RoboEnotsup;
}

int TlsClose(RoboTLSConnection *c)
{
    (void)c;
    return (int)-RoboEnotsup;
}
