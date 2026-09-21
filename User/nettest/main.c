#include <robosys/syscall.h>
#include <robosys/socket.h>
#include <robosys/dns.h>
#include <robosys/http.h>
#include <robosys/tls.h>

static int gFail;

static int IsErr(Long r)
{
    return (uint64_t)r >= (uint64_t)-4095ll;
}

static void puts1(const char *s)
{
    unsigned n = 0;
    while (s && s[n])
        n++;
    if (n)
        Write(1, s, n);
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

static int TestInfo(void)
{
    RoboNetInfo info;
    Long r = NetInfo(&info);
    if (IsErr(r) || !info.present || !info.linkUp)
        return 0;
    line("net info ok");
    return 1;
}

static int TestBadFd(void)
{
    char b[4];
    Long r = Recv(99, b, 4);
    if (!IsErr(r))
        return 0;
    r = Socket(99, RoboSockStream, 0);
    if (!IsErr(r))
        return 0;
    return 1;
}

static int TestPing(void)
{
    Long r = NetPing(0x0A000202u, 3000);
    if (IsErr(r)) {
        line("ping fail");
        return 0;
    }
    line("ping ok");
    return 1;
}

static int TestUdp(void)
{
    int fd;
    RoboSockAddr sa;
    uint8_t q[32], r[64];
    unsigned n = 0;
    Long nr;
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
    fd = (int)Socket(RoboAfInet, RoboSockDgram, 0);
    if (IsErr((Long)fd))
        return 0;
    sa.family = RoboAfInet;
    sa.port = 53;
    sa.addr = 0x0A000203u;
    sa.zero = 0;
    if (IsErr(Sendto(fd, q, n, &sa))) {
        Close(fd);
        return 0;
    }
    nr = Recvfrom(fd, r, 64, &sa);
    Close(fd);
    if (IsErr(nr) || nr < 12)
        return 0;
    line("udp dns ok");
    return 1;
}

static int TestTcp(void)
{
    int fd;
    RoboSockAddr sa;
    char buf[64];
    Long r;
    static const char hello[] = "Hello from Roboticela OS\n";
    static const char want[] = "Hello from host";

    fd = (int)Socket(RoboAfInet, RoboSockStream, 0);
    if (IsErr((Long)fd))
        return 0;
    sa.family = RoboAfInet;
    sa.port = 18019;
    sa.addr = 0x0A000204u;
    sa.zero = 0;
    r = Connect(fd, &sa);
    if (IsErr(r)) {
        Close(fd);
        line("tcp connect fail");
        return 0;
    }
    r = Send(fd, hello, 25);
    if (IsErr(r)) {
        Close(fd);
        return 0;
    }
    r = Recv(fd, buf, 63);
    Close(fd);
    if (IsErr(r) || r < 15)
        return 0;
    if (!eq(buf, want, 15))
        return 0;
    line("tcp e2e ok");
    return 1;
}

static int TestHttp(void)
{
    RoboHttpResponse rsp;
    int r = HttpGetIp(0x0A000204u, 18080, "host", "/", &rsp);
    if (r != 0 || rsp.status != 200) {
        line("http fail");
        return 0;
    }
    if (rsp.bodyLen < 2u) {
        line("http fail");
        return 0;
    }
    line("http ok");
    return 1;
}

static int TestTls(void)
{
    RoboTLSConnection c;
    int r = TlsConnect(-1, "example.com", &c);
    if (r != (int)-RoboEnotsup)
        return 0;
    line("tls not implemented");
    return 1;
}

int main(void)
{
    int info, ping, udp, tcp, http, tls, bad;
    info = TestInfo();
    bad = TestBadFd();
    ping = TestPing();
    udp = TestUdp();
    tcp = TestTcp();
    if (info && ping && tcp) {
        line("[PASS] NETWORK");
        line("[PASS] TCP");
    } else {
        line("[FAIL] NETWORK");
    }
    http = TestHttp();
    tls = TestTls();
    if (!info)
        gFail = 1;
    if (!bad)
        gFail = 1;
    if (!ping)
        gFail = 1;
    if (!udp)
        gFail = 1;
    if (!tcp)
        gFail = 1;
    if (!http)
        gFail = 1;
    if (!tls)
        gFail = 1;
    return gFail ? 1 : 0;
}
