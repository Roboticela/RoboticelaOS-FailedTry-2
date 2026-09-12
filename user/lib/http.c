#include <robosys/http.h>
#include <robosys/dns.h>
#include <robosys/syscall.h>

static int is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static int parse_u(const char *s, unsigned n, unsigned *out)
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

int robo_http_get_ip(uint32_t ipv4, uint16_t port, const char *host,
                     const char *path, RoboHttpResponse *out)
{
    int fd;
    RoboSockAddr sa;
    char req[256];
    unsigned n = 0, i, got = 0, hdr_end = 0, clen = 0, have_clen = 0;
    char buf[2048];
    int r;
    static const char geth[] = "GET ";
    static const char httpv[] = " HTTP/1.1\r\nHost: ";
    static const char rest[] = "\r\nConnection: close\r\n\r\n";

    if (!host || !path || !out)
        return (int)-ROBO_EINVAL;
    out->status = 0;
    out->body_len = 0;
    out->body[0] = 0;
    fd = (int)robo_socket(ROBO_AF_INET, ROBO_SOCK_STREAM, 0);
    if (is_err((robo_long)fd))
        return fd;
    sa.family = ROBO_AF_INET;
    sa.port = port ? port : 80;
    sa.addr = ipv4;
    sa.zero = 0;
    r = (int)robo_connect(fd, &sa);
    if (is_err((robo_long)r)) {
        robo_close(fd);
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
    if (is_err(robo_send(fd, req, n))) {
        robo_close(fd);
        return (int)-ROBO_EIO;
    }
    while (got < sizeof(buf) - 1u) {
        r = (int)robo_recv(fd, buf + got, (sizeof(buf) - 1u) - got);
        if (is_err((robo_long)r) || r == 0)
            break;
        got += (unsigned)r;
        if (!hdr_end) {
            for (i = 0; i + 3u < got; i++) {
                if (buf[i] == '\r' && buf[i + 1] == '\n' &&
                    buf[i + 2] == '\r' && buf[i + 3] == '\n') {
                    hdr_end = i + 4u;
                    break;
                }
            }
        }
        if (hdr_end && !have_clen) {
            for (i = 0; i + 15u < hdr_end; i++) {
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
                    while (p < hdr_end && (buf[p] == ' ' || buf[p] == '\t'))
                        p++;
                    j = p;
                    while (j < hdr_end && buf[j] >= '0' && buf[j] <= '9')
                        j++;
                    if (j > p && parse_u(buf + p, j - p, &clen) == 0)
                        have_clen = 1;
                }
                break;
            }
        }
        if (hdr_end && have_clen && got >= hdr_end + clen)
            break;
    }
    robo_close(fd);
    if (got < 12u)
        return (int)-ROBO_EIO;
    if (buf[0] != 'H' || buf[1] != 'T')
        return (int)-ROBO_EIO;
    {
        unsigned sp = 0, st = 0;
        while (sp < got && buf[sp] != ' ')
            sp++;
        if (sp + 3u >= got || sp < 8u)
            return (int)-ROBO_EIO;
        if (parse_u(buf + sp + 1u, 3, &st) != 0)
            return (int)-ROBO_EIO;
        out->status = (int)st;
    }
    if (!hdr_end)
        return (int)-ROBO_EIO;
    {
        unsigned bl = got - hdr_end;
        if (have_clen && clen < bl)
            bl = clen;
        if (bl > sizeof(out->body) - 1u)
            bl = sizeof(out->body) - 1u;
        for (i = 0; i < bl; i++)
            out->body[i] = buf[hdr_end + i];
        out->body[bl] = 0;
        out->body_len = bl;
    }
    return 0;
}

int robo_http_get(const char *host, uint16_t port, const char *path,
                  RoboHttpResponse *out)
{
    RoboIPAddress ip;
    if (robo_dns_resolve(host, &ip) != 0)
        return (int)-ROBO_ENOENT;
    return robo_http_get_ip(ip.ipv4, port, host, path, out);
}
