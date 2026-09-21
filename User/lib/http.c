#include <robosys/http.h>
#include <robosys/dns.h>
#include <robosys/syscall.h>

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int ParseU(const char *s, unsigned n, unsigned *out)
{
    unsigned v = 0, i;
    if (!n)
        return -1;
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        if (v > (0xFFFFFFFFu / 10u))
            return -1;
        v = v * 10u + (unsigned)(s[i] - '0');
    }
    *out = v;
    return 0;
}

int HttpGetIp(uint32_t ipv4, uint16_t port, const char *host,
                     const char *path, RoboHttpResponse *out)
{
    int fd;
    RoboSockAddr sa;
    char req[256];
    unsigned n = 0, i, got = 0, hdrEnd = 0, clen = 0, haveClen = 0;
    char buf[2048];
    int r;
    static const char geth[] = "GET ";
    static const char httpv[] = " HTTP/1.1\r\nHost: ";
    static const char rest[] = "\r\nConnection: close\r\n\r\n";

    if (!host || !path || !out)
        return (int)-RoboEinval;
    out->status = 0;
    out->bodyLen = 0;
    out->body[0] = 0;
    fd = (int)Socket(RoboAfInet, RoboSockStream, 0);
    if (IsErr((Long)fd))
        return fd;
    sa.family = RoboAfInet;
    sa.port = port ? port : 80;
    sa.addr = ipv4;
    sa.zero = 0;
    r = (int)Connect(fd, &sa);
    if (IsErr((Long)r)) {
        Close(fd);
        return r;
    }
    for (i = 0; geth[i] && n < 250; i++)
        req[n++] = geth[i];
    for (i = 0; path[i] && n < 250; i++)
        req[n++] = path[i];
    for (i = 0; httpv[i] && n < 250; i++)
        req[n++] = httpv[i];
    for (i = 0; host[i] && n < 250; i++)
        req[n++] = host[i];
    for (i = 0; rest[i] && n < 250; i++)
        req[n++] = rest[i];
    if (IsErr(Send(fd, req, n))) {
        Close(fd);
        return (int)-RoboEio;
    }
    while (got < sizeof(buf) - 1u) {
        r = (int)Recv(fd, buf + got, (sizeof(buf) - 1u) - got);
        if (IsErr((Long)r) || r == 0)
            break;
        got += (unsigned)r;
        if (!hdrEnd) {
            for (i = 0; i + 3u < got; i++) {
                if (buf[i] == '\r' && buf[i + 1] == '\n' &&
                    buf[i + 2] == '\r' && buf[i + 3] == '\n') {
                    hdrEnd = i + 4u;
                    break;
                }
            }
        }
        if (hdrEnd && !haveClen) {
            for (i = 0; i + 15u < hdrEnd; i++) {
                const char *k = "content-length:";
                unsigned klen = 15, j, match = 1;
                if (i && buf[i - 1] != '\n')
                    continue;
                for (j = 0; j < klen; j++) {
                    char a = buf[i + j];
                    if (a >= 'A' && a <= 'Z')
                        a = (char)(a - 'A' + 'a');
                    if (a != k[j]) {
                        match = 0;
                        break;
                    }
                }
                if (!match)
                    continue;
                {
                    unsigned p = i + klen;
                    while (p < hdrEnd && (buf[p] == ' ' || buf[p] == '\t'))
                        p++;
                    j = p;
                    while (j < hdrEnd && buf[j] >= '0' && buf[j] <= '9')
                        j++;
                    if (j > p && ParseU(buf + p, j - p, &clen) == 0)
                        haveClen = 1;
                }
                break;
            }
        }
        if (hdrEnd && haveClen && got >= hdrEnd + clen)
            break;
    }
    Close(fd);
    if (got < 12u)
        return (int)-RoboEio;
    if (buf[0] != 'H' || buf[1] != 'T')
        return (int)-RoboEio;
    {
        unsigned sp = 0, st = 0;
        while (sp < got && buf[sp] != ' ')
            sp++;
        if (sp + 3u >= got || sp < 8u)
            return (int)-RoboEio;
        if (ParseU(buf + sp + 1u, 3, &st) != 0)
            return (int)-RoboEio;
        out->status = (int)st;
    }
    if (!hdrEnd)
        return (int)-RoboEio;
    {
        unsigned bl = got - hdrEnd;
        if (haveClen && clen < bl)
            bl = clen;
        if (bl > sizeof(out->body) - 1u)
            bl = sizeof(out->body) - 1u;
        for (i = 0; i < bl; i++)
            out->body[i] = buf[hdrEnd + i];
        out->body[bl] = 0;
        out->bodyLen = bl;
    }
    return 0;
}

int HttpGet(const char *host, uint16_t port, const char *path,
                  RoboHttpResponse *out)
{
    RoboIPAddress ip;
    if (DnsResolve(host, &ip) != 0)
        return (int)-RoboEnoent;
    return HttpGetIp(ip.ipv4, port, host, path, out);
}
