#include <robosys/dns.h>
#include <robosys/syscall.h>

static unsigned DnsLen(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    return n;
}

static uint16_t DnsRd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int DnsSkipName(const uint8_t *msg, unsigned msglen, unsigned *off)
{
    unsigned jumps = 0;
    unsigned pos = *off;
    for (;;) {
        uint8_t lab;
        if (pos >= msglen)
            return -1;
        lab = msg[pos];
        if ((lab & 0xC0u) == 0xC0u) {
            if (pos + 1u >= msglen)
                return -1;
            *off = pos + 2u;
            return 0;
        }
        if (lab == 0) {
            *off = pos + 1u;
            return 0;
        }
        if ((lab & 0xC0u) != 0)
            return -1;
        if (pos + 1u + lab >= msglen)
            return -1;
        pos += 1u + lab;
        if (++jumps > 128u)
            return -1;
    }
}

int DnsResolve(const char *hostname, RoboIPAddress *address)
{
    uint8_t q[512], r[512];
    unsigned n = 0, i, hn, off;
    int fd, nr;
    RoboSockAddr sa;
    uint16_t id = 0x1A2Bu;
    uint16_t qd, an;

    if (!hostname || !address)
        return (int)-RoboEinval;
    address->ipv4 = 0;
    hn = DnsLen(hostname);
    if (!hn || hn > 253u)
        return (int)-RoboEinval;
    q[n++] = (uint8_t)(id >> 8);
    q[n++] = (uint8_t)id;
    q[n++] = 0x01;
    q[n++] = 0x00;
    q[n++] = 0x00;
    q[n++] = 0x01;
    q[n++] = 0x00;
    q[n++] = 0x00;
    q[n++] = 0x00;
    q[n++] = 0x00;
    q[n++] = 0x00;
    q[n++] = 0x00;
    i = 0;
    while (i < hn) {
        unsigned lab = 0;
        unsigned start = i;
        while (i < hn && hostname[i] != '.')
            i++;
        lab = i - start;
        if (!lab || lab > 63u || n + 1u + lab >= 512u)
            return (int)-RoboEinval;
        q[n++] = (uint8_t)lab;
        while (start < i)
            q[n++] = (uint8_t)hostname[start++];
        if (i < hn && hostname[i] == '.')
            i++;
    }
    q[n++] = 0;
    q[n++] = 0x00;
    q[n++] = 0x01;
    q[n++] = 0x00;
    q[n++] = 0x01;

    fd = (int)Socket(RoboAfInet, RoboSockDgram, 0);
    if ((uint64_t)(Long)fd >= (uint64_t)-4095ll)
        return fd;
    sa.family = RoboAfInet;
    sa.port = 53;
    sa.addr = 0x0A000203u;
    sa.zero = 0;
    if ((uint64_t)Sendto(fd, q, n, &sa) >= (uint64_t)-4095ll) {
        Close(fd);
        return (int)-RoboEio;
    }
    nr = (int)Recvfrom(fd, r, 512, &sa);
    Close(fd);
    if ((uint64_t)(Long)nr >= (uint64_t)-4095ll || nr < 12)
        return (int)-RoboEio;
    if (DnsRd16(r) != id)
        return (int)-RoboEio;
    if (r[3] & 0x0Fu)
        return (int)-RoboEnoent;
    qd = DnsRd16(r + 4);
    an = DnsRd16(r + 6);
    off = 12;
    for (i = 0; i < qd; i++) {
        if (DnsSkipName(r, (unsigned)nr, &off) != 0)
            return (int)-RoboEio;
        if (off + 4u > (unsigned)nr)
            return (int)-RoboEio;
        off += 4u;
    }
    for (i = 0; i < an; i++) {
        uint16_t type, cls, rdlen;
        if (DnsSkipName(r, (unsigned)nr, &off) != 0)
            return (int)-RoboEio;
        if (off + 10u > (unsigned)nr)
            return (int)-RoboEio;
        type = DnsRd16(r + off);
        cls = DnsRd16(r + off + 2);
        rdlen = DnsRd16(r + off + 8);
        off += 10u;
        if (off + rdlen > (unsigned)nr)
            return (int)-RoboEio;
        if (type == 1 && cls == 1 && rdlen == 4) {
            address->ipv4 = ((uint32_t)r[off] << 24) | ((uint32_t)r[off + 1] << 16) |
                            ((uint32_t)r[off + 2] << 8) | r[off + 3];
            return 0;
        }
        off += rdlen;
    }
    return (int)-RoboEnoent;
}
