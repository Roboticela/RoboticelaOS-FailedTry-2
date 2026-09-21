#include "net/socket/socket.h"
#include "net/udp/udp.h"
#include "net/tcp/tcp.h"
#include "net/ipv4/icmp.h"
#include "net/ipv4/route.h"
#include "net/arp/arp.h"
#include "net/core/network.h"
#include "net/core/net_util.h"
#include "syscall/syscall_impl.h"
#include "syscall/syscall_errors.h"
#include "syscall/syscall_validate.h"
#include "syscall/syscall_types.h"
#include "process/process.h"
#include "fs/file.h"
#include "fs/fd.h"
#include "mm/heap/heap.h"
#include "kio.h"
#include <robosys/socketAbi.h>

static RoboSocket gSock[RoboNetMaxSock];
static uint16_t gEph = RoboNetEphemeralMin;

static uint64_t arg(const RoboSyscallContext *ctx, unsigned n)
{
    const struct Iframe *f = ctx->frame;
    switch (n) {
    case 1: return f->rdi;
    case 2: return f->rsi;
    case 3: return f->rdx;
    case 4: return f->r10;
    case 5: return f->r8;
    case 6: return f->r9;
    default: return 0;
    }
}

static int64_t SockRead(RoboFile *file, void *buffer, size_t count)
{
    RoboSocket *s = file ? (RoboSocket *)file->privateData : 0;
    if (!s)
        return -(int64_t)RoboEbadf;
    if (s->kind == RoboSockDgram)
        return UdpRecv(s, buffer, (unsigned)count, 0, 0, 0);
    return TcpRecv(s, buffer, (unsigned)count, 0);
}

static int64_t SockWrite(RoboFile *file, const void *buffer, size_t count)
{
    RoboSocket *s = file ? (RoboSocket *)file->privateData : 0;
    if (!s)
        return -(int64_t)RoboEbadf;
    if (s->kind == RoboSockDgram)
        return UdpSend(s, s->rip, s->rport, buffer, (unsigned)count);
    return TcpSend(s, buffer, (unsigned)count);
}

static int SockClose(RoboFile *file)
{
    RoboSocket *s = file ? (RoboSocket *)file->privateData : 0;
    if (!s)
        return 0;
    file->privateData = 0;
    s->file = 0;
    if (s->kind == RoboSockStream)
        TcpClose(s);
    SockFree(s);
    return 0;
}

static const RoboFileOps gSockOps = {
    .read = SockRead,
    .write = SockWrite,
    .close = SockClose,
    .seek = 0,
    .ioctl = 0,
    .poll = 0
};

int SockInit(void)
{
    unsigned i;
    for (i = 0; i < RoboNetMaxSock; i++)
        NetMemset(&gSock[i], 0, (unsigned)sizeof(gSock[i]));
    gEph = RoboNetEphemeralMin;
    return 1;
}

static int PortTaken(int proto, uint32_t ip, uint16_t port)
{
    unsigned i;
    for (i = 0; i < RoboNetMaxSock; i++) {
        RoboSocket *s = &gSock[i];
        if (!s->used || s->proto != proto || !s->lport)
            continue;
        if (s->lport != port)
            continue;
        if (s->lip == 0 || ip == 0 || s->lip == ip)
            return 1;
    }
    return 0;
}

uint16_t SockEphemeral(int proto, uint32_t ip)
{
    unsigned n;
    for (n = 0; n < (RoboNetEphemeralMax - RoboNetEphemeralMin + 1u); n++) {
        uint16_t p = gEph++;
        if (gEph < RoboNetEphemeralMin || gEph > RoboNetEphemeralMax)
            gEph = RoboNetEphemeralMin;
        if (!PortTaken(proto, ip, p))
            return p;
    }
    return 0;
}

RoboSocket *SockAlloc(int kind, int proto, RoboProcess *owner)
{
    unsigned i;
    RoboSocket *s = 0;
    for (i = 0; i < RoboNetMaxSock; i++) {
        if (!gSock[i].used) {
            s = &gSock[i];
            break;
        }
    }
    if (!s)
        return 0;
    NetMemset(s, 0, (unsigned)sizeof(*s));
    s->used = 1;
    s->kind = kind;
    s->proto = proto;
    s->sstate = RoboSockOpen;
    s->owner = owner;
    s->tcpState = RoboTcpClosed;
    if (kind == RoboSockStream) {
        s->sndCap = RoboNetTcpBuf;
        s->rcvCap = RoboNetTcpBuf;
        s->sndBuf = (uint8_t *)kmalloc(s->sndCap);
        s->rcvBuf = (uint8_t *)kmalloc(s->rcvCap);
        if (!s->sndBuf || !s->rcvBuf) {
            if (s->sndBuf)
                kfree(s->sndBuf);
            if (s->rcvBuf)
                kfree(s->rcvBuf);
            s->used = 0;
            return 0;
        }
        NetMemset(s->sndBuf, 0, s->sndCap);
        NetMemset(s->rcvBuf, 0, s->rcvCap);
    }
    return s;
}

void SockFree(RoboSocket *s)
{
    unsigned i;
    if (!s || !s->used)
        return;
    for (i = 0; i < s->udpQn; i++)
        PktFree(s->udpQ[i]);
    s->udpQn = 0;
    if (s->sndBuf)
        kfree(s->sndBuf);
    if (s->rcvBuf)
        kfree(s->rcvBuf);
    s->sndBuf = s->rcvBuf = 0;
    s->used = 0;
}

int SockBind(RoboSocket *s, uint32_t ip, uint16_t port)
{
    if (!s)
        return -1;
    if (!port)
        port = SockEphemeral(s->proto, ip);
    if (!port)
        return -1;
    if (PortTaken(s->proto, ip, port))
        return -1;
    s->lip = ip;
    s->lport = port;
    if (s->sstate == RoboSockOpen)
        s->sstate = RoboSockBound;
    return 0;
}

RoboSocket *SockFindUdp(uint32_t ip, uint16_t port)
{
    unsigned i;
    for (i = 0; i < RoboNetMaxSock; i++) {
        RoboSocket *s = &gSock[i];
        if (!s->used || s->kind != RoboSockDgram)
            continue;
        if (s->lport == port && (s->lip == ip || s->lip == 0 || ip == 0))
            return s;
    }
    return 0;
}

RoboSocket *SockFindTcp(uint32_t lip, uint16_t lport,
                               uint32_t rip, uint16_t rport)
{
    unsigned i;
    for (i = 0; i < RoboNetMaxSock; i++) {
        RoboSocket *s = &gSock[i];
        if (!s->used || s->kind != RoboSockStream)
            continue;
        if (s->lport == lport && s->rport == rport &&
            (s->lip == lip || s->lip == 0) && s->rip == rip)
            return s;
    }
    return 0;
}

RoboSocket *SockFindListen(uint32_t ip, uint16_t port)
{
    unsigned i;
    for (i = 0; i < RoboNetMaxSock; i++) {
        RoboSocket *s = &gSock[i];
        if (!s->used || s->kind != RoboSockStream)
            continue;
        if (s->tcpState == RoboTcpListen && s->lport == port &&
            (s->lip == ip || s->lip == 0 || ip == 0))
            return s;
    }
    return 0;
}

unsigned SockCount(void)
{
    return RoboNetMaxSock;
}

RoboSocket *SockAt(unsigned i)
{
    return i < RoboNetMaxSock ? &gSock[i] : 0;
}

RoboSocket *SockFromFd(RoboProcess *p, int fd)
{
    RoboFile *f;
    RoboSocket *s;
    if (!p || fd < 0)
        return 0;
    f = FdGet(p, (Fd)fd);
    if (!f)
        return 0;
    s = 0;
    if (f->type == RoboFileSocket)
        s = (RoboSocket *)f->privateData;
    FilePut(f);
    return s;
}

void SockReport(void)
{
    unsigned i;
    kprintf("Sockets\n-------\n");
    kprintf("FD    Type    Local              Remote             State\n");
    for (i = 0; i < RoboNetMaxSock; i++) {
        RoboSocket *s = &gSock[i];
        char l[16], r[16];
        if (!s->used)
            continue;
        Ipv4Fmt(s->lip, l);
        Ipv4Fmt(s->rip, r);
        kprintf("%u     %s     %s:%u   %s:%u      %s\n",
                i,
                s->kind == RoboSockStream ? "TCP" : "UDP",
                l, (unsigned)s->lport, r, (unsigned)s->rport,
                s->kind == RoboSockStream ? TcpStateName(s->tcpState)
                                            : "DGRAM");
    }
}

static int CopyAddr(RoboAddressSpace *as, uint64_t uaddr, RoboSockAddr *a)
{
    if (!uaddr || !a)
        return -RoboEfault;
    if (CopyFromUser(as, a, uaddr, sizeof(*a)) != 0)
        return -RoboEfault;
    if (a->family != RoboAfInet)
        return -RoboEinval;
    return 0;
}

static int PutAddr(RoboAddressSpace *as, uint64_t uaddr, uint32_t ip, uint16_t port)
{
    RoboSockAddr a;
    NetMemset(&a, 0, (unsigned)sizeof(a));
    a.family = RoboAfInet;
    a.port = port;
    a.addr = ip;
    if (!uaddr)
        return 0;
    if (CopyToUser(as, uaddr, &a, sizeof(a)) != 0)
        return -RoboEfault;
    return 0;
}

static uint64_t FdForSock(RoboProcess *p, RoboSocket *s)
{
    RoboFile *f;
    Fd fd;
    f = FileCreate(RoboFileSocket, &gSockOps, s,
                         RoboFflagRead | RoboFflagWrite);
    if (!f)
        return SysErr(RoboEnomem);
    s->file = f;
    fd = FdAlloc(p, f, 0);
    FilePut(f);
    if (fd < 0) {
        s->file = 0;
        return SysErr(RoboEmfile);
    }
    return (uint64_t)(int64_t)fd;
}

uint64_t SysSocket(RoboSyscallContext *ctx)
{
    uint64_t op = arg(ctx, 1);
    RoboProcess *p = ctx->process;
    RoboAddressSpace *as = p ? p->addressSpace : 0;
    RoboSocket *s;
    RoboSockAddr addr;
    int fd, r;
    uint64_t buf, len;

    if (!p)
        return SysErr(RoboEperm);

    switch ((int)op) {
    case RoboNetOpSocket: {
        int domain = (int)arg(ctx, 2);
        int type = (int)arg(ctx, 3);
        int proto = (int)arg(ctx, 4);
        if (domain != RoboAfInet)
            return SysErr(RoboEinval);
        if (type == RoboSockStream) {
            if (!proto)
                proto = (int)RoboIpprotoTcp;
            if (proto != (int)RoboIpprotoTcp)
                return SysErr(RoboEinval);
        } else if (type == RoboSockDgram) {
            if (!proto)
                proto = (int)RoboIpprotoUdp;
            if (proto != (int)RoboIpprotoUdp)
                return SysErr(RoboEinval);
        } else {
            return SysErr(RoboEinval);
        }
        s = SockAlloc(type, proto, p);
        if (!s)
            return SysErr(RoboEnfile);
        return FdForSock(p, s);
    }
    case RoboNetOpBind:
        fd = (int)arg(ctx, 2);
        s = SockFromFd(p, fd);
        if (!s)
            return SysErr(RoboEbadf);
        r = CopyAddr(as, arg(ctx, 3), &addr);
        if (r < 0)
            return SysErr(-r);
        if (SockBind(s, addr.addr, addr.port) != 0)
            return SysErr(RoboEaddrinuse);
        return 0;
    case RoboNetOpListen:
        fd = (int)arg(ctx, 2);
        s = SockFromFd(p, fd);
        if (!s || s->kind != RoboSockStream)
            return SysErr(RoboEbadf);
        if (TcpListen(s, (int)arg(ctx, 3)) != 0)
            return SysErr(RoboEinval);
        return 0;
    case RoboNetOpAccept: {
        RoboSocket *c = 0;
        fd = (int)arg(ctx, 2);
        s = SockFromFd(p, fd);
        if (!s || s->kind != RoboSockStream)
            return SysErr(RoboEbadf);
        r = TcpAccept(s, &c);
        if (r < 0)
            return SysErr(-r);
        PutAddr(as, arg(ctx, 3), c->rip, c->rport);
        return FdForSock(p, c);
    }
    case RoboNetOpConnect:
        fd = (int)arg(ctx, 2);
        s = SockFromFd(p, fd);
        if (!s)
            return SysErr(RoboEbadf);
        r = CopyAddr(as, arg(ctx, 3), &addr);
        if (r < 0)
            return SysErr(-r);
        if (s->kind == RoboSockDgram) {
            s->rip = addr.addr;
            s->rport = addr.port;
            if (!s->lport)
                SockBind(s, s->lip, 0);
            s->sstate = RoboSockConnected;
            return 0;
        }
        r = TcpConnect(s, addr.addr, addr.port);
        if (r < 0)
            return SysErr(-r);
        return 0;
    case RoboNetOpSend:
    case RoboNetOpRecv: {
        char tmp[RoboSyscallWriteMax];
        fd = (int)arg(ctx, 2);
        buf = arg(ctx, 3);
        len = arg(ctx, 4);
        s = SockFromFd(p, fd);
        if (!s)
            return SysErr(RoboEbadf);
        if (len > RoboSyscallWriteMax)
            len = RoboSyscallWriteMax;
        if (op == RoboNetOpSend) {
            if (len && CopyFromUser(as, tmp, buf, (size_t)len) != 0)
                return SysErr(RoboEfault);
            if (s->kind == RoboSockDgram)
                r = UdpSend(s, s->rip, s->rport, tmp, (unsigned)len);
            else
                r = TcpSend(s, tmp, (unsigned)len);
            if (r < 0)
                return SysErr(-r);
            return (uint64_t)r;
        }
        if (s->kind == RoboSockDgram)
            r = UdpRecv(s, tmp, (unsigned)len, 0, 0, 0);
        else
            r = TcpRecv(s, tmp, (unsigned)len, 0);
        if (r < 0)
            return SysErr(-r);
        if (r && CopyToUser(as, buf, tmp, (size_t)r) != 0)
            return SysErr(RoboEfault);
        return (uint64_t)r;
    }
    case RoboNetOpSendto: {
        char tmp[RoboSyscallWriteMax];
        fd = (int)arg(ctx, 2);
        buf = arg(ctx, 3);
        len = arg(ctx, 4);
        s = SockFromFd(p, fd);
        if (!s)
            return SysErr(RoboEbadf);
        r = CopyAddr(as, arg(ctx, 6), &addr);
        if (r < 0)
            return SysErr(-r);
        if (len > RoboSyscallWriteMax)
            len = RoboSyscallWriteMax;
        if (len && CopyFromUser(as, tmp, buf, (size_t)len) != 0)
            return SysErr(RoboEfault);
        if (s->kind != RoboSockDgram)
            return SysErr(RoboEinval);
        r = UdpSend(s, addr.addr, addr.port, tmp, (unsigned)len);
        if (r < 0)
            return SysErr(-r);
        return (uint64_t)r;
    }
    case RoboNetOpRecvfrom: {
        char tmp[RoboSyscallWriteMax];
        uint32_t sip = 0;
        uint16_t sport = 0;
        fd = (int)arg(ctx, 2);
        buf = arg(ctx, 3);
        len = arg(ctx, 4);
        s = SockFromFd(p, fd);
        if (!s)
            return SysErr(RoboEbadf);
        if (len > RoboSyscallWriteMax)
            len = RoboSyscallWriteMax;
        if (s->kind != RoboSockDgram)
            return SysErr(RoboEinval);
        r = UdpRecv(s, tmp, (unsigned)len, &sip, &sport, 0);
        if (r < 0)
            return SysErr(-r);
        if (r && CopyToUser(as, buf, tmp, (size_t)r) != 0)
            return SysErr(RoboEfault);
        PutAddr(as, arg(ctx, 6), sip, sport);
        return (uint64_t)r;
    }
    case RoboNetOpShutdown:
        fd = (int)arg(ctx, 2);
        s = SockFromFd(p, fd);
        if (!s)
            return SysErr(RoboEbadf);
        if (s->kind == RoboSockStream)
            TcpShutdown(s, (int)arg(ctx, 3));
        return 0;
    case RoboNetOpGetsockname:
        fd = (int)arg(ctx, 2);
        s = SockFromFd(p, fd);
        if (!s)
            return SysErr(RoboEbadf);
        r = PutAddr(as, arg(ctx, 3), s->lip, s->lport);
        if (r < 0)
            return SysErr(-r);
        return 0;
    case RoboNetOpGetpeername:
        fd = (int)arg(ctx, 2);
        s = SockFromFd(p, fd);
        if (!s)
            return SysErr(RoboEbadf);
        if (!s->rport && !s->rip)
            return SysErr(RoboEinval);
        r = PutAddr(as, arg(ctx, 3), s->rip, s->rport);
        if (r < 0)
            return SysErr(-r);
        return 0;
    case RoboNetOpInfo: {
        RoboNetInfo info;
        RoboNetIf *ifp = NetifDefault();
        NetMemset(&info, 0, (unsigned)sizeof(info));
        if (ifp) {
            unsigned i;
            info.present = 1;
            info.linkUp = ifp->linkUp;
            info.mtu = ifp->mtu;
            info.ipv4 = ifp->ipv4;
            info.netmask = ifp->netmask;
            info.gateway = ifp->gateway;
            info.dns = ifp->dns;
            for (i = 0; i < 6; i++)
                info.mac[i] = ifp->mac[i];
            for (i = 0; i < RoboNetNameMax && ifp->name[i]; i++)
                info.name[i] = ifp->name[i];
        }
        if (CopyToUser(as, arg(ctx, 3), &info, sizeof(info)) != 0)
            return SysErr(RoboEfault);
        return 0;
    }
    case RoboNetOpPing: {
        uint32_t ip = (uint32_t)arg(ctx, 2);
        uint32_t ms = (uint32_t)arg(ctx, 3);
        if (IcmpPing(ip, ms) != 0)
            return SysErr(RoboEio);
        return 0;
    }
    default:
        return SysErr(RoboEinval);
    }
}
