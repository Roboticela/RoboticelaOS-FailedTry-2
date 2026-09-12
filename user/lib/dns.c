#include <robosys/dns.h>
#include <robosys/syscall.h>

static unsigned dns_len(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    return n;
}

static uint16_t dns_rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int dns_skip_name(const uint8_t *msg, unsigned msglen, unsigned *off)
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

int robo_dns_resolve(const char *hostname, RoboIPAddress *address)
{
    uint8_t q[512], r[512];
    unsigned n = 0, i, hn, off;
    int fd, nr;
    RoboSockAddr sa;
    uint16_t id = 0x1A2Bu;
    uint16_t qd, an;

    if (!hostname || !address)
        return (int)-ROBO_EINVAL;
    address->ipv4 = 0;
    hn = dns_len(hostname);
    if (!hn || hn > 253u)
        return (int)-ROBO_EINVAL;
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
            return (int)-ROBO_EINVAL;
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

    fd = (int)robo_socket(ROBO_AF_INET, ROBO_SOCK_DGRAM, 0);
    if ((uint64_t)(robo_long)fd >= (uint64_t)-4095ll)
        return fd;
    sa.family = ROBO_AF_INET;
    sa.port = 53;
    sa.addr = 0x0A000203u;
    sa.zero = 0;
    if ((uint64_t)robo_sendto(fd, q, n, &sa) >= (uint64_t)-4095ll) {
        robo_close(fd);
        return (int)-ROBO_EIO;
    }
    nr = (int)robo_recvfrom(fd, r, 512, &sa);
    robo_close(fd);
    if ((uint64_t)(robo_long)nr >= (uint64_t)-4095ll || nr < 12)
        return (int)-ROBO_EIO;
    if (dns_rd16(r) != id)
        return (int)-ROBO_EIO;
    if (r[3] & 0x0Fu)
        return (int)-ROBO_ENOENT;
    qd = dns_rd16(r + 4);
    an = dns_rd16(r + 6);
    off = 12;
    for (i = 0; i < qd; i++) {
        if (dns_skip_name(r, (unsigned)nr, &off) != 0)
            return (int)-ROBO_EIO;
        if (off + 4u > (unsigned)nr)
            return (int)-ROBO_EIO;
        off += 4u;
    }
    for (i = 0; i < an; i++) {
        uint16_t type, cls, rdlen;
        if (dns_skip_name(r, (unsigned)nr, &off) != 0)
            return (int)-ROBO_EIO;
        if (off + 10u > (unsigned)nr)
            return (int)-ROBO_EIO;
        type = dns_rd16(r + off);
        cls = dns_rd16(r + off + 2);
        rdlen = dns_rd16(r + off + 8);
        off += 10u;
        if (off + rdlen > (unsigned)nr)
            return (int)-ROBO_EIO;
        if (type == 1 && cls == 1 && rdlen == 4) {
            address->ipv4 = ((uint32_t)r[off] << 24) | ((uint32_t)r[off + 1] << 16) |
                            ((uint32_t)r[off + 2] << 8) | r[off + 3];
            return 0;
        }
        off += rdlen;
    }
    return (int)-ROBO_ENOENT;
}
