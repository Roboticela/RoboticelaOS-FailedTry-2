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
#include <robosys/socket_abi.h>

static RoboSocket g_sock[ROBO_NET_MAX_SOCK];
static uint16_t g_eph = ROBO_NET_EPHEMERAL_MIN;

static uint64_t arg(const RoboSyscallContext *ctx, unsigned n)
{
    const struct robo_iframe *f = ctx->frame;
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

static int64_t sock_read(RoboFile *file, void *buffer, size_t count)
{
    RoboSocket *s = file ? (RoboSocket *)file->private_data : 0;
    if (!s)
        return -(int64_t)ROBO_EBADF;
    if (s->kind == ROBO_SOCK_DGRAM)
        return robo_udp_recv(s, buffer, (unsigned)count, 0, 0, 0);
    return robo_tcp_recv(s, buffer, (unsigned)count, 0);
}

static int64_t sock_write(RoboFile *file, const void *buffer, size_t count)
{
    RoboSocket *s = file ? (RoboSocket *)file->private_data : 0;
    if (!s)
        return -(int64_t)ROBO_EBADF;
    if (s->kind == ROBO_SOCK_DGRAM)
        return robo_udp_send(s, s->rip, s->rport, buffer, (unsigned)count);
    return robo_tcp_send(s, buffer, (unsigned)count);
}

static int sock_close(RoboFile *file)
{
    RoboSocket *s = file ? (RoboSocket *)file->private_data : 0;
    if (!s)
        return 0;
    file->private_data = 0;
    s->file = 0;
    if (s->kind == ROBO_SOCK_STREAM)
        robo_tcp_close(s);
    robo_sock_free(s);
    return 0;
}

static const RoboFileOps g_sock_ops = {
    .read = sock_read,
    .write = sock_write,
    .close = sock_close,
    .seek = 0,
    .ioctl = 0,
    .poll = 0
};

int robo_sock_init(void)
{
    unsigned i;
    for (i = 0; i < ROBO_NET_MAX_SOCK; i++)
        robo_net_memset(&g_sock[i], 0, (unsigned)sizeof(g_sock[i]));
    g_eph = ROBO_NET_EPHEMERAL_MIN;
    return 1;
}

static int port_taken(int proto, uint32_t ip, uint16_t port)
{
    unsigned i;
    for (i = 0; i < ROBO_NET_MAX_SOCK; i++) {
        RoboSocket *s = &g_sock[i];
        if (!s->used || s->proto != proto || !s->lport)
            continue;
        if (s->lport != port)
            continue;
        if (s->lip == 0 || ip == 0 || s->lip == ip)
            return 1;
    }
    return 0;
}

uint16_t robo_sock_ephemeral(int proto, uint32_t ip)
{
    unsigned n;
    for (n = 0; n < (ROBO_NET_EPHEMERAL_MAX - ROBO_NET_EPHEMERAL_MIN + 1u); n++) {
        uint16_t p = g_eph++;
        if (g_eph < ROBO_NET_EPHEMERAL_MIN || g_eph > ROBO_NET_EPHEMERAL_MAX)
            g_eph = ROBO_NET_EPHEMERAL_MIN;
        if (!port_taken(proto, ip, p))
            return p;
    }
    return 0;
}

RoboSocket *robo_sock_alloc(int kind, int proto, RoboProcess *owner)
{
    unsigned i;
    RoboSocket *s = 0;
    for (i = 0; i < ROBO_NET_MAX_SOCK; i++) {
        if (!g_sock[i].used) {
            s = &g_sock[i];
            break;
        }
    }
    if (!s)
        return 0;
    robo_net_memset(s, 0, (unsigned)sizeof(*s));
    s->used = 1;
    s->kind = kind;
    s->proto = proto;
    s->sstate = ROBO_SOCK_OPEN;
    s->owner = owner;
    s->tcp_state = ROBO_TCP_CLOSED;
    if (kind == ROBO_SOCK_STREAM) {
        s->snd_cap = ROBO_NET_TCP_BUF;
        s->rcv_cap = ROBO_NET_TCP_BUF;
        s->snd_buf = (uint8_t *)kmalloc(s->snd_cap);
        s->rcv_buf = (uint8_t *)kmalloc(s->rcv_cap);
        if (!s->snd_buf || !s->rcv_buf) {
            if (s->snd_buf)
                kfree(s->snd_buf);
            if (s->rcv_buf)
                kfree(s->rcv_buf);
            s->used = 0;
            return 0;
        }
        robo_net_memset(s->snd_buf, 0, s->snd_cap);
        robo_net_memset(s->rcv_buf, 0, s->rcv_cap);
    }
    return s;
}

void robo_sock_free(RoboSocket *s)
{
    unsigned i;
    if (!s || !s->used)
        return;
    for (i = 0; i < s->udp_qn; i++)
        robo_pkt_free(s->udp_q[i]);
    s->udp_qn = 0;
    if (s->snd_buf)
        kfree(s->snd_buf);
    if (s->rcv_buf)
        kfree(s->rcv_buf);
    s->snd_buf = s->rcv_buf = 0;
    s->used = 0;
}

int robo_sock_bind(RoboSocket *s, uint32_t ip, uint16_t port)
{
    if (!s)
        return -1;
    if (!port)
        port = robo_sock_ephemeral(s->proto, ip);
    if (!port)
        return -1;
    if (port_taken(s->proto, ip, port))
        return -1;
    s->lip = ip;
    s->lport = port;
    if (s->sstate == ROBO_SOCK_OPEN)
        s->sstate = ROBO_SOCK_BOUND;
    return 0;
}

RoboSocket *robo_sock_find_udp(uint32_t ip, uint16_t port)
{
    unsigned i;
    for (i = 0; i < ROBO_NET_MAX_SOCK; i++) {
        RoboSocket *s = &g_sock[i];
        if (!s->used || s->kind != ROBO_SOCK_DGRAM)
            continue;
        if (s->lport == port && (s->lip == ip || s->lip == 0 || ip == 0))
            return s;
    }
    return 0;
}

RoboSocket *robo_sock_find_tcp(uint32_t lip, uint16_t lport,
                               uint32_t rip, uint16_t rport)
{
    unsigned i;
    for (i = 0; i < ROBO_NET_MAX_SOCK; i++) {
        RoboSocket *s = &g_sock[i];
        if (!s->used || s->kind != ROBO_SOCK_STREAM)
            continue;
        if (s->lport == lport && s->rport == rport &&
            (s->lip == lip || s->lip == 0) && s->rip == rip)
            return s;
    }
    return 0;
}

RoboSocket *robo_sock_find_listen(uint32_t ip, uint16_t port)
{
    unsigned i;
    for (i = 0; i < ROBO_NET_MAX_SOCK; i++) {
        RoboSocket *s = &g_sock[i];
        if (!s->used || s->kind != ROBO_SOCK_STREAM)
            continue;
        if (s->tcp_state == ROBO_TCP_LISTEN && s->lport == port &&
            (s->lip == ip || s->lip == 0 || ip == 0))
            return s;
    }
    return 0;
}

unsigned robo_sock_count(void)
{
    return ROBO_NET_MAX_SOCK;
}

RoboSocket *robo_sock_at(unsigned i)
{
    return i < ROBO_NET_MAX_SOCK ? &g_sock[i] : 0;
}

RoboSocket *robo_sock_from_fd(RoboProcess *p, int fd)
{
    RoboFile *f;
    RoboSocket *s;
    if (!p || fd < 0)
        return 0;
    f = robo_fd_get(p, (robo_fd_t)fd);
    if (!f)
        return 0;
    s = 0;
    if (f->type == ROBO_FILE_SOCKET)
        s = (RoboSocket *)f->private_data;
    robo_file_put(f);
    return s;
}

void robo_sock_report(void)
{
    unsigned i;
    kprintf("Sockets\n-------\n");
    kprintf("FD    Type    Local              Remote             State\n");
    for (i = 0; i < ROBO_NET_MAX_SOCK; i++) {
        RoboSocket *s = &g_sock[i];
        char l[16], r[16];
        if (!s->used)
            continue;
        robo_ipv4_fmt(s->lip, l);
        robo_ipv4_fmt(s->rip, r);
        kprintf("%u     %s     %s:%u   %s:%u      %s\n",
                i,
                s->kind == ROBO_SOCK_STREAM ? "TCP" : "UDP",
                l, (unsigned)s->lport, r, (unsigned)s->rport,
                s->kind == ROBO_SOCK_STREAM ? robo_tcp_state_name(s->tcp_state)
                                            : "DGRAM");
    }
}

static int copy_addr(RoboAddressSpace *as, uint64_t uaddr, RoboSockAddr *a)
{
    if (!uaddr || !a)
        return -ROBO_EFAULT;
    if (robo_copy_from_user(as, a, uaddr, sizeof(*a)) != 0)
        return -ROBO_EFAULT;
    if (a->family != ROBO_AF_INET)
        return -ROBO_EINVAL;
    return 0;
}

static int put_addr(RoboAddressSpace *as, uint64_t uaddr, uint32_t ip, uint16_t port)
{
    RoboSockAddr a;
    robo_net_memset(&a, 0, (unsigned)sizeof(a));
    a.family = ROBO_AF_INET;
    a.port = port;
    a.addr = ip;
    if (!uaddr)
        return 0;
    if (robo_copy_to_user(as, uaddr, &a, sizeof(a)) != 0)
        return -ROBO_EFAULT;
    return 0;
}

static uint64_t fd_for_sock(RoboProcess *p, RoboSocket *s)
{
    RoboFile *f;
    robo_fd_t fd;
    f = robo_file_create(ROBO_FILE_SOCKET, &g_sock_ops, s,
                         ROBO_FFLAG_READ | ROBO_FFLAG_WRITE);
    if (!f)
        return robo_sys_err(ROBO_ENOMEM);
    s->file = f;
    fd = robo_fd_alloc(p, f, 0);
    robo_file_put(f);
    if (fd < 0) {
        s->file = 0;
        return robo_sys_err(ROBO_EMFILE);
    }
    return (uint64_t)(int64_t)fd;
}

uint64_t robo_sys_socket(RoboSyscallContext *ctx)
{
    uint64_t op = arg(ctx, 1);
    RoboProcess *p = ctx->process;
    RoboAddressSpace *as = p ? p->address_space : 0;
    RoboSocket *s;
    RoboSockAddr addr;
    int fd, r;
    uint64_t buf, len;

    if (!p)
        return robo_sys_err(ROBO_EPERM);

    switch ((int)op) {
    case ROBO_NET_OP_SOCKET: {
        int domain = (int)arg(ctx, 2);
        int type = (int)arg(ctx, 3);
        int proto = (int)arg(ctx, 4);
        if (domain != ROBO_AF_INET)
            return robo_sys_err(ROBO_EINVAL);
        if (type == ROBO_SOCK_STREAM) {
            if (!proto)
                proto = (int)ROBO_IPPROTO_TCP;
            if (proto != (int)ROBO_IPPROTO_TCP)
                return robo_sys_err(ROBO_EINVAL);
        } else if (type == ROBO_SOCK_DGRAM) {
            if (!proto)
                proto = (int)ROBO_IPPROTO_UDP;
            if (proto != (int)ROBO_IPPROTO_UDP)
                return robo_sys_err(ROBO_EINVAL);
        } else {
            return robo_sys_err(ROBO_EINVAL);
        }
        s = robo_sock_alloc(type, proto, p);
        if (!s)
            return robo_sys_err(ROBO_ENFILE);
        return fd_for_sock(p, s);
    }
    case ROBO_NET_OP_BIND:
        fd = (int)arg(ctx, 2);
        s = robo_sock_from_fd(p, fd);
        if (!s)
            return robo_sys_err(ROBO_EBADF);
        r = copy_addr(as, arg(ctx, 3), &addr);
        if (r < 0)
            return robo_sys_err(-r);
        if (robo_sock_bind(s, addr.addr, addr.port) != 0)
            return robo_sys_err(ROBO_EADDRINUSE);
        return 0;
    case ROBO_NET_OP_LISTEN:
        fd = (int)arg(ctx, 2);
        s = robo_sock_from_fd(p, fd);
        if (!s || s->kind != ROBO_SOCK_STREAM)
            return robo_sys_err(ROBO_EBADF);
        if (robo_tcp_listen(s, (int)arg(ctx, 3)) != 0)
            return robo_sys_err(ROBO_EINVAL);
        return 0;
    case ROBO_NET_OP_ACCEPT: {
        RoboSocket *c = 0;
        fd = (int)arg(ctx, 2);
        s = robo_sock_from_fd(p, fd);
        if (!s || s->kind != ROBO_SOCK_STREAM)
            return robo_sys_err(ROBO_EBADF);
        r = robo_tcp_accept(s, &c);
        if (r < 0)
            return robo_sys_err(-r);
        put_addr(as, arg(ctx, 3), c->rip, c->rport);
        return fd_for_sock(p, c);
    }
    case ROBO_NET_OP_CONNECT:
        fd = (int)arg(ctx, 2);
        s = robo_sock_from_fd(p, fd);
        if (!s)
            return robo_sys_err(ROBO_EBADF);
        r = copy_addr(as, arg(ctx, 3), &addr);
        if (r < 0)
            return robo_sys_err(-r);
        if (s->kind == ROBO_SOCK_DGRAM) {
            s->rip = addr.addr;
            s->rport = addr.port;
            if (!s->lport)
                robo_sock_bind(s, s->lip, 0);
            s->sstate = ROBO_SOCK_CONNECTED;
            return 0;
        }
        r = robo_tcp_connect(s, addr.addr, addr.port);
        if (r < 0)
            return robo_sys_err(-r);
        return 0;
    case ROBO_NET_OP_SEND:
    case ROBO_NET_OP_RECV: {
        char tmp[ROBO_SYSCALL_WRITE_MAX];
        fd = (int)arg(ctx, 2);
        buf = arg(ctx, 3);
        len = arg(ctx, 4);
        s = robo_sock_from_fd(p, fd);
        if (!s)
            return robo_sys_err(ROBO_EBADF);
        if (len > ROBO_SYSCALL_WRITE_MAX)
            len = ROBO_SYSCALL_WRITE_MAX;
        if (op == ROBO_NET_OP_SEND) {
            if (len && robo_copy_from_user(as, tmp, buf, (size_t)len) != 0)
                return robo_sys_err(ROBO_EFAULT);
            if (s->kind == ROBO_SOCK_DGRAM)
                r = robo_udp_send(s, s->rip, s->rport, tmp, (unsigned)len);
            else
                r = robo_tcp_send(s, tmp, (unsigned)len);
            if (r < 0)
                return robo_sys_err(-r);
            return (uint64_t)r;
        }
        if (s->kind == ROBO_SOCK_DGRAM)
            r = robo_udp_recv(s, tmp, (unsigned)len, 0, 0, 0);
        else
            r = robo_tcp_recv(s, tmp, (unsigned)len, 0);
        if (r < 0)
            return robo_sys_err(-r);
        if (r && robo_copy_to_user(as, buf, tmp, (size_t)r) != 0)
            return robo_sys_err(ROBO_EFAULT);
        return (uint64_t)r;
    }
    case ROBO_NET_OP_SENDTO: {
        char tmp[ROBO_SYSCALL_WRITE_MAX];
        fd = (int)arg(ctx, 2);
        buf = arg(ctx, 3);
        len = arg(ctx, 4);
        s = robo_sock_from_fd(p, fd);
        if (!s)
            return robo_sys_err(ROBO_EBADF);
        r = copy_addr(as, arg(ctx, 6), &addr);
        if (r < 0)
            return robo_sys_err(-r);
        if (len > ROBO_SYSCALL_WRITE_MAX)
            len = ROBO_SYSCALL_WRITE_MAX;
        if (len && robo_copy_from_user(as, tmp, buf, (size_t)len) != 0)
            return robo_sys_err(ROBO_EFAULT);
        if (s->kind != ROBO_SOCK_DGRAM)
            return robo_sys_err(ROBO_EINVAL);
        r = robo_udp_send(s, addr.addr, addr.port, tmp, (unsigned)len);
        if (r < 0)
            return robo_sys_err(-r);
        return (uint64_t)r;
    }
    case ROBO_NET_OP_RECVFROM: {
        char tmp[ROBO_SYSCALL_WRITE_MAX];
        uint32_t sip = 0;
        uint16_t sport = 0;
        fd = (int)arg(ctx, 2);
        buf = arg(ctx, 3);
        len = arg(ctx, 4);
        s = robo_sock_from_fd(p, fd);
        if (!s)
            return robo_sys_err(ROBO_EBADF);
        if (len > ROBO_SYSCALL_WRITE_MAX)
            len = ROBO_SYSCALL_WRITE_MAX;
        if (s->kind != ROBO_SOCK_DGRAM)
            return robo_sys_err(ROBO_EINVAL);
        r = robo_udp_recv(s, tmp, (unsigned)len, &sip, &sport, 0);
        if (r < 0)
            return robo_sys_err(-r);
        if (r && robo_copy_to_user(as, buf, tmp, (size_t)r) != 0)
            return robo_sys_err(ROBO_EFAULT);
        put_addr(as, arg(ctx, 6), sip, sport);
        return (uint64_t)r;
    }
    case ROBO_NET_OP_SHUTDOWN:
        fd = (int)arg(ctx, 2);
        s = robo_sock_from_fd(p, fd);
        if (!s)
            return robo_sys_err(ROBO_EBADF);
        if (s->kind == ROBO_SOCK_STREAM)
            robo_tcp_shutdown(s, (int)arg(ctx, 3));
        return 0;
    case ROBO_NET_OP_GETSOCKNAME:
        fd = (int)arg(ctx, 2);
        s = robo_sock_from_fd(p, fd);
        if (!s)
            return robo_sys_err(ROBO_EBADF);
        r = put_addr(as, arg(ctx, 3), s->lip, s->lport);
        if (r < 0)
            return robo_sys_err(-r);
        return 0;
    case ROBO_NET_OP_GETPEERNAME:
        fd = (int)arg(ctx, 2);
        s = robo_sock_from_fd(p, fd);
        if (!s)
            return robo_sys_err(ROBO_EBADF);
        if (!s->rport && !s->rip)
            return robo_sys_err(ROBO_EINVAL);
        r = put_addr(as, arg(ctx, 3), s->rip, s->rport);
        if (r < 0)
            return robo_sys_err(-r);
        return 0;
    case ROBO_NET_OP_INFO: {
        RoboNetInfo info;
        RoboNetIf *ifp = robo_netif_default();
        robo_net_memset(&info, 0, (unsigned)sizeof(info));
        if (ifp) {
            unsigned i;
            info.present = 1;
            info.link_up = ifp->link_up;
            info.mtu = ifp->mtu;
            info.ipv4 = ifp->ipv4;
            info.netmask = ifp->netmask;
            info.gateway = ifp->gateway;
            info.dns = ifp->dns;
            for (i = 0; i < 6; i++)
                info.mac[i] = ifp->mac[i];
            for (i = 0; i < ROBO_NET_NAME_MAX && ifp->name[i]; i++)
                info.name[i] = ifp->name[i];
        }
        if (robo_copy_to_user(as, arg(ctx, 3), &info, sizeof(info)) != 0)
            return robo_sys_err(ROBO_EFAULT);
        return 0;
    }
    case ROBO_NET_OP_PING: {
        uint32_t ip = (uint32_t)arg(ctx, 2);
        uint32_t ms = (uint32_t)arg(ctx, 3);
        if (robo_icmp_ping(ip, ms) != 0)
            return robo_sys_err(ROBO_EIO);
        return 0;
    }
    default:
        return robo_sys_err(ROBO_EINVAL);
    }
}
