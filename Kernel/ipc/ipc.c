#include "ipc/ipc.h"
#include "syscall/syscall_errors.h"
#include "syscall/syscall_numbers.h"
#include "syscall/syscall_validate.h"
#include "process/process.h"
#include "thread/thread.h"
#include "scheduler/scheduler.h"
#include "mm/vmm/vmm.h"
#include "mm/vmm/paging.h"
#include "mm/pmm.h"
#include "mm/page.h"
#include "mm/heap/heap.h"
#include "kio.h"
#include <robosys/types.h>

#define IPC_HANDLES 16
#define IPC_PROCS   32
#define IPC_PORTS   8
#define IPC_CHANS   32
#define IPC_SHMS    32
#define IPC_MAPS    32
#define IPC_Q       8
#define IPC_MSG     256
#define IPC_NAME    32
#define IPC_KIND_PORT 1
#define IPC_KIND_CHAN 2

struct ipcMsg {
    uint16_t len;
    uint8_t data[IPC_MSG];
};

struct ipcEnd {
    uint64_t pid;
    int handle;
    uint8_t closed;
    uint8_t qn, qh, qt;
    uint32_t seq;
    RoboThread *waiter;
    struct ipcMsg q[IPC_Q];
};

struct ipcChan {
    int used;
    struct ipcEnd e[2];
};

struct ipcPend {
    uint64_t peerPid;
    uint16_t chan;
    uint8_t serverEnd;
};

struct ipcPort {
    int used;
    char name[IPC_NAME];
    uint64_t ownerPid;
    int ownerHandle;
    uint8_t qn, qh, qt;
    uint32_t seq;
    RoboThread *waiter;
    struct ipcPend q[IPC_Q];
};

struct ipcShm {
    int used;
    uint32_t id;
    uint32_t refs;
    uint32_t npages;
    uint64_t bytes;
    uint64_t ownerPid;
    PhysAddr *pages;
};

struct ipcMap {
    uint32_t shmId;
    uint64_t va;
    uint64_t bytes;
};

struct ipcSlot {
    uint8_t kind;
    uint16_t obj;
    uint8_t end;
};

struct ipcProc {
    int used;
    uint64_t pid;
    struct ipcSlot h[IPC_HANDLES];
    struct ipcMap maps[IPC_MAPS];
};

static struct ipcProc gProc[IPC_PROCS];
static struct ipcPort gPort[IPC_PORTS];
static struct ipcChan gChan[IPC_CHANS];
static struct ipcShm gShm[IPC_SHMS];
static uint32_t gShmNext = 1;

static void memzero(void *p, unsigned n)
{
    unsigned char *c = (unsigned char *)p;
    while (n--)
        *c++ = 0;
}

static int nameseq(const char *a, const char *b)
{
    unsigned i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return a[i] == 0 && b[i] == 0;
}

static struct ipcProc *ProcGet(uint64_t pid, int create)
{
    unsigned i;
    struct ipcProc *free = 0;
    for (i = 0; i < IPC_PROCS; i++) {
        if (gProc[i].used && gProc[i].pid == pid)
            return &gProc[i];
        if (!gProc[i].used && !free)
            free = &gProc[i];
    }
    if (!create || !free)
        return 0;
    memzero(free, (unsigned)sizeof(*free));
    free->used = 1;
    free->pid = pid;
    return free;
}

static int AllocHandle(struct ipcProc *p)
{
    int i;
    for (i = 1; i < IPC_HANDLES; i++) {
        if (!p->h[i].kind)
            return i;
    }
    return 0;
}

static void FreeHandle(struct ipcProc *p, int h)
{
    if (p && h > 0 && h < IPC_HANDLES)
        memzero(&p->h[h], (unsigned)sizeof(p->h[h]));
}

static void WakeEnd(struct ipcEnd *e)
{
    RoboThread *w;
    if (!e || !e->waiter)
        return;
    w = e->waiter;
    e->waiter = 0;
    e->seq++;
    ThreadWake(w);
}

static void CloseEnd(struct ipcEnd *e)
{
    if (!e)
        return;
    e->closed = 1;
    WakeEnd(e);
}

static int EnqueueEnd(struct ipcEnd *e, const void *data, unsigned len)
{
    unsigned i;
    uint8_t *dst;
    const uint8_t *src;
    if (!e || e->closed)
        return -RoboEio;
    if (len > IPC_MSG)
        return -RoboEinval;
    if (e->qn >= IPC_Q)
        return -RoboEagain;
    e->q[e->qh].len = (uint16_t)len;
    dst = e->q[e->qh].data;
    src = (const uint8_t *)data;
    for (i = 0; i < len; i++)
        dst[i] = src[i];
    e->qh = (uint8_t)((e->qh + 1u) % IPC_Q);
    e->qn++;
    e->seq++;
    if (e->waiter) {
        RoboThread *w = e->waiter;
        e->waiter = 0;
        ThreadWake(w);
    }
    return (int)len;
}

static int DequeueEnd(struct ipcEnd *e, void *data, unsigned cap)
{
    unsigned i, n;
    uint8_t *dst;
    const uint8_t *src;
    if (!e)
        return -RoboEbadf;
    if (!e->qn)
        return e->closed ? -RoboEio : -RoboEagain;
    n = e->q[e->qt].len;
    if (n > cap)
        n = cap;
    dst = (uint8_t *)data;
    src = e->q[e->qt].data;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
    e->qt = (uint8_t)((e->qt + 1u) % IPC_Q);
    e->qn--;
    return (int)n;
}

static void ShmFree(struct ipcShm *s)
{
    unsigned i;
    if (!s || !s->used)
        return;
    if (s->pages) {
        for (i = 0; i < s->npages; i++) {
            if (s->pages[i] != RoboInvalidPhysAddr)
                PageFree(s->pages[i]);
        }
        kfree(s->pages);
    }
    memzero(s, (unsigned)sizeof(*s));
}

static void ShmDrop(struct ipcShm *s)
{
    if (!s || !s->used)
        return;
    if (s->refs)
        s->refs--;
    if (!s->refs)
        ShmFree(s);
}

static struct ipcShm *ShmFind(uint32_t id)
{
    unsigned i;
    if (!id)
        return 0;
    for (i = 0; i < IPC_SHMS; i++) {
        if (gShm[i].used && gShm[i].id == id)
            return &gShm[i];
    }
    return 0;
}

static int ProcHasShm(struct ipcProc *p, uint32_t id)
{
    unsigned i;
    if (!p || !id)
        return 0;
    for (i = 0; i < IPC_MAPS; i++) {
        if (p->maps[i].shmId == id)
            return 1;
    }
    return 0;
}

static void UnmapOne(struct ipcProc *p, unsigned mi)
{
    uint64_t va, bytes, pages, i;
    uint32_t id;
    struct ipcShm *s;
    RoboProcess *rp;
    RoboAddressSpace *as;
    if (!p || mi >= IPC_MAPS || !p->maps[mi].shmId)
        return;
    rp = ProcessLookup(p->pid);
    as = rp ? rp->addressSpace : 0;
    va = p->maps[mi].va;
    bytes = p->maps[mi].bytes;
    id = p->maps[mi].shmId;
    pages = (bytes + RoboPageMask) >> RoboPageShift;
    s = ShmFind(id);
    if (as) {
        for (i = 0; i < pages; i++) {
            PhysAddr pa = RoboInvalidPhysAddr;
            VmmAspaceUnmap(as, (VirtAddr)(va + i * RoboPageSize), &pa);
        }
    }
    memzero(&p->maps[mi], (unsigned)sizeof(p->maps[mi]));
    ShmDrop(s);
    if (s && s->used && s->ownerPid == p->pid && !ProcHasShm(p, id)) {
        s->ownerPid = 0;
        ShmDrop(s);
    }
}

static void CloseChanForPid(struct ipcChan *c, uint64_t pid)
{
    if (!c || !c->used)
        return;
    if (c->e[0].pid == pid) {
        c->e[0].pid = 0;
        c->e[0].handle = 0;
        CloseEnd(&c->e[1]);
    }
    if (c->e[1].pid == pid) {
        c->e[1].pid = 0;
        c->e[1].handle = 0;
        CloseEnd(&c->e[0]);
    }
    if (!c->e[0].pid && !c->e[1].pid)
        memzero(c, (unsigned)sizeof(*c));
}

void IpcOnProcessExit(uint64_t pid)
{
    unsigned i, j;
    struct ipcProc *p;

    if (!pid)
        return;
    PreemptDisable();
    p = ProcGet(pid, 0);
    if (p) {
        for (i = 0; i < IPC_MAPS; i++)
            UnmapOne(p, i);
        for (i = 1; i < IPC_HANDLES; i++) {
            if (p->h[i].kind == IPC_KIND_CHAN && p->h[i].obj < IPC_CHANS)
                CloseChanForPid(&gChan[p->h[i].obj], pid);
            if (p->h[i].kind == IPC_KIND_PORT && p->h[i].obj < IPC_PORTS) {
                struct ipcPort *pt = &gPort[p->h[i].obj];
                if (pt->used && pt->ownerPid == pid) {
                    if (pt->waiter)
                        ThreadWake(pt->waiter);
                    memzero(pt, (unsigned)sizeof(*pt));
                }
            }
        }
        memzero(p, (unsigned)sizeof(*p));
    }
    for (i = 0; i < IPC_PORTS; i++) {
        if (gPort[i].used && gPort[i].ownerPid == pid) {
            if (gPort[i].waiter)
                ThreadWake(gPort[i].waiter);
            memzero(&gPort[i], (unsigned)sizeof(gPort[i]));
        }
    }
    for (i = 0; i < IPC_CHANS; i++)
        CloseChanForPid(&gChan[i], pid);
    for (i = 0; i < IPC_SHMS; i++) {
        if (gShm[i].used && gShm[i].ownerPid == pid) {
            gShm[i].ownerPid = 0;
            ShmDrop(&gShm[i]);
        }
        (void)j;
    }
    PreemptEnable();
}

int IpcShmOwns(PhysAddr pa)
{
    unsigned i, j;
    if (pa == RoboInvalidPhysAddr)
        return 0;
    for (i = 0; i < IPC_SHMS; i++) {
        if (!gShm[i].used || !gShm[i].pages)
            continue;
        for (j = 0; j < gShm[i].npages; j++) {
            if (gShm[i].pages[j] == pa)
                return 1;
        }
    }
    return 0;
}

static uint64_t OpListen(RoboProcess *rp, struct ipcProc *p,
                          const char *name)
{
    unsigned i;
    int h, slot = -1;
    if (!name[0])
        return SysErr(RoboEinval);
    for (i = 0; i < IPC_PORTS; i++) {
        if (gPort[i].used && nameseq(gPort[i].name, name))
            return SysErr(RoboEexist);
        if (!gPort[i].used && slot < 0)
            slot = (int)i;
    }
    if (slot < 0)
        return SysErr(RoboEnfile);
    h = AllocHandle(p);
    if (!h)
        return SysErr(RoboEmfile);
    memzero(&gPort[slot], (unsigned)sizeof(gPort[slot]));
    gPort[slot].used = 1;
    gPort[slot].ownerPid = rp->id;
    gPort[slot].ownerHandle = h;
    for (i = 0; i < IPC_NAME - 1 && name[i]; i++)
        gPort[slot].name[i] = name[i];
    p->h[h].kind = IPC_KIND_PORT;
    p->h[h].obj = (uint16_t)slot;
    kprintf("[IPC] listen %s pid %llu\n", gPort[slot].name,
            (unsigned long long)rp->id);
    return (uint64_t)h;
}

static uint64_t OpConnect(RoboProcess *rp, struct ipcProc *p,
                           const char *name)
{
    unsigned i;
    int slot = -1, cslot = -1, h;
    struct ipcPort *pt;
    struct ipcChan *ch;
    if (!name[0])
        return SysErr(RoboEinval);
    for (i = 0; i < IPC_PORTS; i++) {
        if (gPort[i].used && nameseq(gPort[i].name, name)) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0)
        return SysErr(RoboEnoent);
    pt = &gPort[slot];
    if (pt->qn >= IPC_Q)
        return SysErr(RoboEagain);
    for (i = 0; i < IPC_CHANS; i++) {
        if (!gChan[i].used) {
            cslot = (int)i;
            break;
        }
    }
    if (cslot < 0)
        return SysErr(RoboEnfile);
    h = AllocHandle(p);
    if (!h)
        return SysErr(RoboEmfile);
    ch = &gChan[cslot];
    memzero(ch, (unsigned)sizeof(*ch));
    ch->used = 1;
    ch->e[0].pid = pt->ownerPid;
    ch->e[1].pid = rp->id;
    ch->e[1].handle = h;
    p->h[h].kind = IPC_KIND_CHAN;
    p->h[h].obj = (uint16_t)cslot;
    p->h[h].end = 1;
    pt->q[pt->qh].peerPid = rp->id;
    pt->q[pt->qh].chan = (uint16_t)cslot;
    pt->q[pt->qh].serverEnd = 0;
    pt->qh = (uint8_t)((pt->qh + 1u) % IPC_Q);
    pt->qn++;
    pt->seq++;
    if (pt->waiter) {
        RoboThread *w = pt->waiter;
        pt->waiter = 0;
        ThreadWake(w);
    }
    return (uint64_t)h;
}

static uint64_t OpClose(struct ipcProc *p, int handle)
{
    struct ipcSlot *s;
    if (handle <= 0 || handle >= IPC_HANDLES || !p->h[handle].kind)
        return SysErr(RoboEbadf);
    s = &p->h[handle];
    if (s->kind == IPC_KIND_PORT && s->obj < IPC_PORTS) {
        struct ipcPort *pt = &gPort[s->obj];
        if (pt->used && pt->ownerPid == p->pid) {
            if (pt->waiter)
                ThreadWake(pt->waiter);
            memzero(pt, (unsigned)sizeof(*pt));
        }
    } else if (s->kind == IPC_KIND_CHAN && s->obj < IPC_CHANS) {
        CloseChanForPid(&gChan[s->obj], p->pid);
    }
    FreeHandle(p, handle);
    return 0;
}

static uint64_t RecvPort(RoboProcess *rp, struct ipcProc *p, struct ipcPort *pt,
                          uint64_t ubuf, uint64_t len, int nowait)
{
    RoboIpcAccept acc;
    int h;
    struct ipcPend pend;
    uint32_t seq;
    RoboThread *self;

    for (;;) {
        if (pt->qn) {
            pend = pt->q[pt->qt];
            pt->qt = (uint8_t)((pt->qt + 1u) % IPC_Q);
            pt->qn--;
            h = AllocHandle(p);
            if (!h)
                return SysErr(RoboEmfile);
            if (pend.chan >= IPC_CHANS || !gChan[pend.chan].used)
                return SysErr(RoboEio);
            gChan[pend.chan].e[pend.serverEnd].handle = h;
            p->h[h].kind = IPC_KIND_CHAN;
            p->h[h].obj = pend.chan;
            p->h[h].end = pend.serverEnd;
            acc.magic = RoboIpcAcceptMagic;
            acc.handle = h;
            acc.peerPid = pend.peerPid;
            if (len < sizeof(acc))
                return SysErr(RoboEinval);
            if (CopyToUser(rp->addressSpace, ubuf, &acc, sizeof(acc)) != 0)
                return SysErr(RoboEfault);
            return sizeof(acc);
        }
        if (nowait)
            return SysErr(RoboEagain);
        self = CurrentThread();
        if (!self)
            return SysErr(RoboEbusy);
        seq = pt->seq;
        pt->waiter = self;
        if (pt->qn || pt->seq != seq) {
            pt->waiter = 0;
            continue;
        }
        if (!pt->used)
            return SysErr(RoboEio);
        PreemptEnable();
        ThreadBlock();
        PreemptDisable();
        pt->waiter = 0;
        if (!pt->used)
            return SysErr(RoboEio);
    }
}

static uint64_t RecvChan(RoboProcess *rp, struct ipcEnd *e,
                          uint64_t ubuf, uint64_t len, int nowait)
{
    uint8_t tmp[IPC_MSG];
    int n;
    uint32_t seq;
    RoboThread *self;

    for (;;) {
        n = DequeueEnd(e, tmp, IPC_MSG);
        if (n >= 0) {
            if ((uint64_t)n > len)
                n = (int)len;
            if (n && CopyToUser(rp->addressSpace, ubuf, tmp, (size_t)n) != 0)
                return SysErr(RoboEfault);
            return (uint64_t)n;
        }
        if (n == -RoboEio)
            return SysErr(RoboEio);
        if (nowait)
            return SysErr(RoboEagain);
        self = CurrentThread();
        if (!self)
            return SysErr(RoboEbusy);
        seq = e->seq;
        e->waiter = self;
        if (e->qn || e->closed || e->seq != seq) {
            e->waiter = 0;
            continue;
        }
        PreemptEnable();
        ThreadBlock();
        PreemptDisable();
        e->waiter = 0;
    }
}

static uint64_t OpShmCreate(RoboProcess *rp, uint64_t bytes)
{
    unsigned i, n, k;
    struct ipcShm *s = 0;
    uint64_t mapLen;
    if (!bytes || bytes > (8ull * 1024ull * 1024ull))
        return SysErr(RoboEinval);
    mapLen = (bytes + RoboPageMask) & ~RoboPageMask;
    n = (unsigned)(mapLen / RoboPageSize);
    if (!n)
        return SysErr(RoboEinval);
    for (i = 0; i < IPC_SHMS; i++) {
        if (!gShm[i].used) {
            s = &gShm[i];
            break;
        }
    }
    if (!s)
        return SysErr(RoboEnfile);
    memzero(s, (unsigned)sizeof(*s));
    s->pages = (PhysAddr *)kmalloc((size_t)n * sizeof(PhysAddr));
    if (!s->pages)
        return SysErr(RoboEnomem);
    for (k = 0; k < n; k++)
        s->pages[k] = RoboInvalidPhysAddr;
    for (k = 0; k < n; k++) {
        PhysAddr pa = PageAlloc();
        unsigned char *v;
        unsigned b;
        if (pa == RoboInvalidPhysAddr) {
            for (i = 0; i < k; i++)
                PageFree(s->pages[i]);
            kfree(s->pages);
            memzero(s, (unsigned)sizeof(*s));
            return SysErr(RoboEnomem);
        }
        v = (unsigned char *)(uintptr_t)pa;
        for (b = 0; b < RoboPageSize; b++)
            v[b] = 0;
        s->pages[k] = pa;
    }
    s->used = 1;
    s->id = gShmNext++;
    if (!s->id)
        s->id = gShmNext++;
    s->refs = 1;
    s->npages = n;
    s->bytes = mapLen;
    s->ownerPid = rp ? rp->id : 0;
    return s->id;
}

static uint64_t OpShmMap(RoboProcess *rp, struct ipcProc *p, uint32_t id)
{
    struct ipcShm *s;
    unsigned mi, i;
    uint64_t va, stackFloor, vm;
    s = ShmFind(id);
    if (!s)
        return SysErr(RoboEnoent);
    for (mi = 0; mi < IPC_MAPS; mi++) {
        if (!p->maps[mi].shmId)
            break;
    }
    if (mi >= IPC_MAPS)
        return SysErr(RoboEmfile);
    if (!rp->mmapNext)
        rp->mmapNext = RoboUserMmapBase;
    va = rp->mmapNext;
    if (va & RoboPageMask)
        va = (va + RoboPageMask) & ~RoboPageMask;
    stackFloor = RoboUserStackTop - (uint64_t)RoboUserStackSize;
    if (va < RoboUserMmapBase || va + s->bytes > stackFloor)
        return SysErr(RoboEinval);
    vm = RoboVmUser | RoboVmRead | RoboVmWrite;
    for (i = 0; i < s->npages; i++) {
        if (!VmmAspaceMap(rp->addressSpace,
                                 (VirtAddr)(va + (uint64_t)i * RoboPageSize),
                                 s->pages[i], vm)) {
            unsigned u;
            for (u = 0; u < i; u++) {
                PhysAddr pa = RoboInvalidPhysAddr;
                VmmAspaceUnmap(rp->addressSpace,
                                      (VirtAddr)(va + (uint64_t)u * RoboPageSize),
                                      &pa);
            }
            return SysErr(RoboEinval);
        }
    }
    p->maps[mi].shmId = s->id;
    p->maps[mi].va = va;
    p->maps[mi].bytes = s->bytes;
    s->refs++;
    rp->mmapNext = va + s->bytes;
    return va;
}

static uint64_t OpShmUnmap(struct ipcProc *p, uint64_t va)
{
    unsigned i;
    for (i = 0; i < IPC_MAPS; i++) {
        if (p->maps[i].shmId && p->maps[i].va == va) {
            UnmapOne(p, i);
            return 0;
        }
    }
    return SysErr(RoboEinval);
}

uint64_t IpcSyscall(RoboSyscallContext *ctx)
{
    uint64_t op, handle, buf, len, flags;
    RoboProcess *rp;
    struct ipcProc *p;
    char name[IPC_NAME];
    uint64_t r;
    int nowait;

    if (!ctx || !ctx->process || ctx->process == KernelProcess())
        return SysErr(RoboEperm);
    rp = ctx->process;
    op = ctx->frame->rdi;
    handle = ctx->frame->rsi;
    buf = ctx->frame->rdx;
    len = ctx->frame->r10;
    flags = ctx->frame->r8;
    nowait = (flags & RoboIpcNowait) != 0;

    PreemptDisable();
    p = ProcGet(rp->id, 1);
    if (!p) {
        PreemptEnable();
        return SysErr(RoboEnomem);
    }

    if (op == RoboIpcOpListen || op == RoboIpcOpConnect) {
        memzero(name, IPC_NAME);
        PreemptEnable();
        if (CopyUserString(rp->addressSpace, name, buf, IPC_NAME) != 0)
            return SysErr(RoboEfault);
        PreemptDisable();
        p = ProcGet(rp->id, 1);
        if (!p) {
            PreemptEnable();
            return SysErr(RoboEnomem);
        }
        r = (op == RoboIpcOpListen) ? OpListen(rp, p, name)
                                       : OpConnect(rp, p, name);
        PreemptEnable();
        return r;
    }

    if (op == RoboIpcOpClose) {
        r = OpClose(p, (int)handle);
        PreemptEnable();
        return r;
    }

    if (op == RoboIpcOpShmCreate) {
        r = OpShmCreate(rp, len);
        PreemptEnable();
        return r;
    }
    if (op == RoboIpcOpShmMap) {
        r = OpShmMap(rp, p, (uint32_t)handle);
        PreemptEnable();
        return r;
    }
    if (op == RoboIpcOpShmUnmap) {
        r = OpShmUnmap(p, buf);
        PreemptEnable();
        return r;
    }

    if (op == RoboIpcOpSend) {
        uint8_t tmp[IPC_MSG];
        struct ipcSlot *s;
        struct ipcEnd *peer;
        int n;
        if (handle == 0 || handle >= IPC_HANDLES || !p->h[handle].kind) {
            PreemptEnable();
            return SysErr(RoboEbadf);
        }
        if (len > IPC_MSG) {
            PreemptEnable();
            return SysErr(RoboEinval);
        }
        s = &p->h[handle];
        if (s->kind != IPC_KIND_CHAN) {
            PreemptEnable();
            return SysErr(RoboEbadf);
        }
        PreemptEnable();
        if (len && CopyFromUser(rp->addressSpace, tmp, buf, (size_t)len) != 0)
            return SysErr(RoboEfault);
        PreemptDisable();
        p = ProcGet(rp->id, 0);
        if (!p || p->h[handle].kind != IPC_KIND_CHAN) {
            PreemptEnable();
            return SysErr(RoboEbadf);
        }
        s = &p->h[handle];
        if (s->obj >= IPC_CHANS || !gChan[s->obj].used) {
            PreemptEnable();
            return SysErr(RoboEio);
        }
        peer = &gChan[s->obj].e[s->end ^ 1u];
        n = EnqueueEnd(peer, tmp, (unsigned)len);
        PreemptEnable();
        if (n < 0)
            return SysErr(-n);
        return (uint64_t)n;
    }

    if (op == RoboIpcOpRecv) {
        struct ipcSlot *s;
        if (handle == 0 || handle >= IPC_HANDLES || !p->h[handle].kind) {
            PreemptEnable();
            return SysErr(RoboEbadf);
        }
        s = &p->h[handle];
        if (s->kind == IPC_KIND_PORT) {
            if (s->obj >= IPC_PORTS || !gPort[s->obj].used) {
                PreemptEnable();
                return SysErr(RoboEbadf);
            }
            r = RecvPort(rp, p, &gPort[s->obj], buf, len, nowait);
            PreemptEnable();
            return r;
        }
        if (s->kind == IPC_KIND_CHAN) {
            if (s->obj >= IPC_CHANS || !gChan[s->obj].used) {
                PreemptEnable();
                return SysErr(RoboEio);
            }
            r = RecvChan(rp, &gChan[s->obj].e[s->end], buf, len, nowait);
            PreemptEnable();
            return r;
        }
        PreemptEnable();
        return SysErr(RoboEbadf);
    }

    PreemptEnable();
    return SysErr(RoboEinval);
}
