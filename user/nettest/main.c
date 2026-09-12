#include <robosys/syscall.h>
#include <robosys/socket.h>
#include <robosys/dns.h>
#include <robosys/http.h>
#include <robosys/tls.h>

static int g_fail;

static int is_err(robo_long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static void puts1(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    if (n)
        robo_write(1, s, n);
}

static void line(const char *s)
{
    puts1(s);
    puts1("\n");
}

static int eq(const char *a, const char *b, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

static int test_info(void)
{
    RoboNetInfo info;
    robo_long r = robo_net_info(&info);
    if (is_err(r) || !info.present || !info.link_up)
        return 0;
    line("net info ok");
    return 1;
}

static int test_bad_fd(void)
{
    char b[4];
    robo_long r = robo_recv(99, b, 4);
    if (!is_err(r))
        return 0;
    r = robo_socket(99, ROBO_SOCK_STREAM, 0);
    if (!is_err(r))
        return 0;
    return 1;
}

static int test_ping(void)
{
    robo_long r = robo_net_ping(0x0A000202u, 3000);
    if (is_err(r)) {
        line("ping fail");
        return 0;
    }
    line("ping ok");
    return 1;
}

static int test_udp(void)
{
    int fd;
    RoboSockAddr sa;
    uint8_t q[32], r[64];
    unsigned n = 0;
    robo_long nr;
    /* minimal DNS query for "." SOA - any response proves UDP */
    q[n++] = 0x12;
    q[n++] = 0x34;
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
    q[n++] = 0x00;
    q[n++] = 0x00;
    q[n++] = 0x01;
    q[n++] = 0x00;
    q[n++] = 0x01;
    fd = (int)robo_socket(ROBO_AF_INET, ROBO_SOCK_DGRAM, 0);
    if (is_err((robo_long)fd))
        return 0;
    sa.family = ROBO_AF_INET;
    sa.port = 53;
    sa.addr = 0x0A000203u;
    sa.zero = 0;
    if (is_err(robo_sendto(fd, q, n, &sa))) {
        robo_close(fd);
        return 0;
    }
    nr = robo_recvfrom(fd, r, 64, &sa);
    robo_close(fd);
    if (is_err(nr) || nr < 12)
        return 0;
    line("udp dns ok");
    return 1;
}

static int test_tcp(void)
{
    int fd;
    RoboSockAddr sa;
    char buf[64];
    robo_long r;
    static const char hello[] = "Hello from Roboticela OS\n";
    static const char want[] = "Hello from host";

    fd = (int)robo_socket(ROBO_AF_INET, ROBO_SOCK_STREAM, 0);
    if (is_err((robo_long)fd))
        return 0;
    sa.family = ROBO_AF_INET;
    sa.port = 18019;
    sa.addr = 0x0A000204u;
    sa.zero = 0;
    r = robo_connect(fd, &sa);
    if (is_err(r)) {
        robo_close(fd);
        line("tcp connect fail");
        return 0;
    }
    r = robo_send(fd, hello, 25);
    if (is_err(r)) {
        robo_close(fd);
        return 0;
    }
    r = robo_recv(fd, buf, 63);
    robo_close(fd);
    if (is_err(r) || r < 15)
        return 0;
    if (!eq(buf, want, 15))
        return 0;
    line("tcp e2e ok");
    return 1;
}

static int test_http(void)
{
    RoboHttpResponse rsp;
    int r = robo_http_get_ip(0x0A000204u, 18080, "host", "/", &rsp);
    if (r != 0 || rsp.status != 200) {
        line("http fail");
        return 0;
    }
    if (rsp.body_len < 2u) {
        line("http fail");
        return 0;
    }
    line("http ok");
    return 1;
}

static int test_tls(void)
{
    RoboTLSConnection c;
    int r = robo_tls_connect(-1, "example.com", &c);
    if (r != (int)-ROBO_ENOTSUP)
        return 0;
    line("tls not implemented");
    return 1;
}

int main(void)
{
    int info, ping, udp, tcp, http, tls, bad;
    info = test_info();
    bad = test_bad_fd();
    ping = test_ping();
    udp = test_udp();
    tcp = test_tcp();
    if (info && ping && tcp) {
        line("[PASS] NETWORK");
        line("[PASS] TCP");
    } else {
        line("[FAIL] NETWORK");
    }
    http = test_http();
    tls = test_tls();
    if (!info)
        g_fail = 1;
    if (!bad)
        g_fail = 1;
    if (!ping)
        g_fail = 1;
    if (!udp)
        g_fail = 1;
    if (!tcp)
        g_fail = 1;
    if (!http)
        g_fail = 1;
    if (!tls)
        g_fail = 1;
    return g_fail ? 1 : 0;
}
