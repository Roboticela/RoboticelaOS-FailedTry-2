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

struct ipc_msg {
    uint16_t len;
    uint8_t data[IPC_MSG];
};

struct ipc_end {
    uint64_t pid;
    int handle;
    uint8_t closed;
    uint8_t qn, qh, qt;
    uint32_t seq;
    RoboThread *waiter;
    struct ipc_msg q[IPC_Q];
};

struct ipc_chan {
    int used;
    struct ipc_end e[2];
};

struct ipc_pend {
    uint64_t peer_pid;
    uint16_t chan;
    uint8_t server_end;
};

struct ipc_port {
    int used;
    char name[IPC_NAME];
    uint64_t owner_pid;
    int owner_handle;
    uint8_t qn, qh, qt;
    uint32_t seq;
    RoboThread *waiter;
    struct ipc_pend q[IPC_Q];
};

struct ipc_shm {
    int used;
    uint32_t id;
    uint32_t refs;
    uint32_t npages;
    uint64_t bytes;
    uint64_t owner_pid;
    phys_addr_t *pages;
};

struct ipc_map {
    uint32_t shm_id;
    uint64_t va;
    uint64_t bytes;
};

struct ipc_slot {
    uint8_t kind;
    uint16_t obj;
    uint8_t end;
};

struct ipc_proc {
    int used;
    uint64_t pid;
    struct ipc_slot h[IPC_HANDLES];
    struct ipc_map maps[IPC_MAPS];
};

static struct ipc_proc g_proc[IPC_PROCS];
static struct ipc_port g_port[IPC_PORTS];
static struct ipc_chan g_chan[IPC_CHANS];
static struct ipc_shm g_shm[IPC_SHMS];
static uint32_t g_shm_next = 1;

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

static struct ipc_proc *proc_get(uint64_t pid, int create)
{
    unsigned i;
    struct ipc_proc *free = 0;
    for (i = 0; i < IPC_PROCS; i++) {
        if (g_proc[i].used && g_proc[i].pid == pid)
            return &g_proc[i];
        if (!g_proc[i].used && !free)
            free = &g_proc[i];
    }
    if (!create || !free)
        return 0;
    memzero(free, (unsigned)sizeof(*free));
    free->used = 1;
    free->pid = pid;
    return free;
}

static int alloc_handle(struct ipc_proc *p)
{
    int i;
    for (i = 1; i < IPC_HANDLES; i++) {
        if (!p->h[i].kind)
            return i;
    }
    return 0;
}

static void free_handle(struct ipc_proc *p, int h)
{
    if (p && h > 0 && h < IPC_HANDLES)
        memzero(&p->h[h], (unsigned)sizeof(p->h[h]));
}

static void wake_end(struct ipc_end *e)
{
    RoboThread *w;
    if (!e || !e->waiter)
        return;
    w = e->waiter;
    e->waiter = 0;
    e->seq++;
    robo_thread_wake(w);
}

static void close_end(struct ipc_end *e)
{
    if (!e)
        return;
    e->closed = 1;
    wake_end(e);
}

static int enqueue_end(struct ipc_end *e, const void *data, unsigned len)
{
    unsigned i;
    uint8_t *dst;
    const uint8_t *src;
    if (!e || e->closed)
        return -ROBO_EIO;
    if (len > IPC_MSG)
        return -ROBO_EINVAL;
    if (e->qn >= IPC_Q)
        return -ROBO_EAGAIN;
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
        robo_thread_wake(w);
    }
    return (int)len;
}

static int dequeue_end(struct ipc_end *e, void *data, unsigned cap)
{
    unsigned i, n;
    uint8_t *dst;
    const uint8_t *src;
    if (!e)
        return -ROBO_EBADF;
    if (!e->qn)
        return e->closed ? -ROBO_EIO : -ROBO_EAGAIN;
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

static void shm_free(struct ipc_shm *s)
{
    unsigned i;
    if (!s || !s->used)
        return;
    if (s->pages) {
        for (i = 0; i < s->npages; i++) {
            if (s->pages[i] != ROBO_INVALID_PHYS_ADDR)
                robo_page_free(s->pages[i]);
        }
        kfree(s->pages);
    }
    memzero(s, (unsigned)sizeof(*s));
}

static void shm_drop(struct ipc_shm *s)
{
    if (!s || !s->used)
        return;
    if (s->refs)
        s->refs--;
    if (!s->refs)
        shm_free(s);
}

static struct ipc_shm *shm_find(uint32_t id)
{
    unsigned i;
    if (!id)
        return 0;
    for (i = 0; i < IPC_SHMS; i++) {
        if (g_shm[i].used && g_shm[i].id == id)
            return &g_shm[i];
    }
    return 0;
}

static int proc_has_shm(struct ipc_proc *p, uint32_t id)
{
    unsigned i;
    if (!p || !id)
        return 0;
    for (i = 0; i < IPC_MAPS; i++) {
        if (p->maps[i].shm_id == id)
            return 1;
    }
    return 0;
}

static void unmap_one(struct ipc_proc *p, unsigned mi)
{
    uint64_t va, bytes, pages, i;
    uint32_t id;
    struct ipc_shm *s;
    RoboProcess *rp;
    RoboAddressSpace *as;
    if (!p || mi >= IPC_MAPS || !p->maps[mi].shm_id)
        return;
    rp = robo_process_lookup(p->pid);
    as = rp ? rp->address_space : 0;
    va = p->maps[mi].va;
    bytes = p->maps[mi].bytes;
    id = p->maps[mi].shm_id;
    pages = (bytes + ROBO_PAGE_MASK) >> ROBO_PAGE_SHIFT;
    s = shm_find(id);
    if (as) {
        for (i = 0; i < pages; i++) {
            phys_addr_t pa = ROBO_INVALID_PHYS_ADDR;
            robo_vmm_aspace_unmap(as, (virt_addr_t)(va + i * ROBO_PAGE_SIZE), &pa);
        }
    }
    memzero(&p->maps[mi], (unsigned)sizeof(p->maps[mi]));
    shm_drop(s);
    if (s && s->used && s->owner_pid == p->pid && !proc_has_shm(p, id)) {
        s->owner_pid = 0;
        shm_drop(s);
    }
}

static void close_chan_for_pid(struct ipc_chan *c, uint64_t pid)
{
    if (!c || !c->used)
        return;
    if (c->e[0].pid == pid) {
        c->e[0].pid = 0;
        c->e[0].handle = 0;
        close_end(&c->e[1]);
    }
    if (c->e[1].pid == pid) {
        c->e[1].pid = 0;
        c->e[1].handle = 0;
        close_end(&c->e[0]);
    }
    if (!c->e[0].pid && !c->e[1].pid)
        memzero(c, (unsigned)sizeof(*c));
}

void robo_ipc_on_process_exit(uint64_t pid)
{
    unsigned i, j;
    struct ipc_proc *p;

    if (!pid)
        return;
    robo_preempt_disable();
    p = proc_get(pid, 0);
    if (p) {
        for (i = 0; i < IPC_MAPS; i++)
            unmap_one(p, i);
        for (i = 1; i < IPC_HANDLES; i++) {
            if (p->h[i].kind == IPC_KIND_CHAN && p->h[i].obj < IPC_CHANS)
                close_chan_for_pid(&g_chan[p->h[i].obj], pid);
            if (p->h[i].kind == IPC_KIND_PORT && p->h[i].obj < IPC_PORTS) {
                struct ipc_port *pt = &g_port[p->h[i].obj];
                if (pt->used && pt->owner_pid == pid) {
                    if (pt->waiter)
                        robo_thread_wake(pt->waiter);
                    memzero(pt, (unsigned)sizeof(*pt));
                }
            }
        }
        memzero(p, (unsigned)sizeof(*p));
    }
    for (i = 0; i < IPC_PORTS; i++) {
        if (g_port[i].used && g_port[i].owner_pid == pid) {
            if (g_port[i].waiter)
                robo_thread_wake(g_port[i].waiter);
            memzero(&g_port[i], (unsigned)sizeof(g_port[i]));
        }
    }
    for (i = 0; i < IPC_CHANS; i++)
        close_chan_for_pid(&g_chan[i], pid);
    for (i = 0; i < IPC_SHMS; i++) {
        if (g_shm[i].used && g_shm[i].owner_pid == pid) {
            g_shm[i].owner_pid = 0;
            shm_drop(&g_shm[i]);
        }
        (void)j;
    }
    robo_preempt_enable();
}

int robo_ipc_shm_owns(phys_addr_t pa)
{
    unsigned i, j;
    if (pa == ROBO_INVALID_PHYS_ADDR)
        return 0;
    for (i = 0; i < IPC_SHMS; i++) {
        if (!g_shm[i].used || !g_shm[i].pages)
            continue;
        for (j = 0; j < g_shm[i].npages; j++) {
            if (g_shm[i].pages[j] == pa)
                return 1;
        }
    }
    return 0;
}

static uint64_t op_listen(RoboProcess *rp, struct ipc_proc *p,
                          const char *name)
{
    unsigned i;
    int h, slot = -1;
    if (!name[0])
        return robo_sys_err(ROBO_EINVAL);
    for (i = 0; i < IPC_PORTS; i++) {
        if (g_port[i].used && nameseq(g_port[i].name, name))
            return robo_sys_err(ROBO_EEXIST);
        if (!g_port[i].used && slot < 0)
            slot = (int)i;
    }
    if (slot < 0)
        return robo_sys_err(ROBO_ENFILE);
    h = alloc_handle(p);
    if (!h)
        return robo_sys_err(ROBO_EMFILE);
    memzero(&g_port[slot], (unsigned)sizeof(g_port[slot]));
    g_port[slot].used = 1;
    g_port[slot].owner_pid = rp->id;
    g_port[slot].owner_handle = h;
    for (i = 0; i < IPC_NAME - 1 && name[i]; i++)
        g_port[slot].name[i] = name[i];
    p->h[h].kind = IPC_KIND_PORT;
    p->h[h].obj = (uint16_t)slot;
    kprintf("[IPC] listen %s pid %llu\n", g_port[slot].name,
            (unsigned long long)rp->id);
    return (uint64_t)h;
}

static uint64_t op_connect(RoboProcess *rp, struct ipc_proc *p,
                           const char *name)
{
    unsigned i;
    int slot = -1, cslot = -1, h;
    struct ipc_port *pt;
    struct ipc_chan *ch;
    if (!name[0])
        return robo_sys_err(ROBO_EINVAL);
    for (i = 0; i < IPC_PORTS; i++) {
        if (g_port[i].used && nameseq(g_port[i].name, name)) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0)
        return robo_sys_err(ROBO_ENOENT);
    pt = &g_port[slot];
    if (pt->qn >= IPC_Q)
        return robo_sys_err(ROBO_EAGAIN);
    for (i = 0; i < IPC_CHANS; i++) {
        if (!g_chan[i].used) {
            cslot = (int)i;
            break;
        }
    }
    if (cslot < 0)
        return robo_sys_err(ROBO_ENFILE);
    h = alloc_handle(p);
    if (!h)
        return robo_sys_err(ROBO_EMFILE);
    ch = &g_chan[cslot];
    memzero(ch, (unsigned)sizeof(*ch));
    ch->used = 1;
    ch->e[0].pid = pt->owner_pid;
    ch->e[1].pid = rp->id;
    ch->e[1].handle = h;
    p->h[h].kind = IPC_KIND_CHAN;
    p->h[h].obj = (uint16_t)cslot;
    p->h[h].end = 1;
    pt->q[pt->qh].peer_pid = rp->id;
    pt->q[pt->qh].chan = (uint16_t)cslot;
    pt->q[pt->qh].server_end = 0;
    pt->qh = (uint8_t)((pt->qh + 1u) % IPC_Q);
    pt->qn++;
    pt->seq++;
    if (pt->waiter) {
        RoboThread *w = pt->waiter;
        pt->waiter = 0;
        robo_thread_wake(w);
    }
    return (uint64_t)h;
}

static uint64_t op_close(struct ipc_proc *p, int handle)
{
    struct ipc_slot *s;
    if (handle <= 0 || handle >= IPC_HANDLES || !p->h[handle].kind)
        return robo_sys_err(ROBO_EBADF);
    s = &p->h[handle];
    if (s->kind == IPC_KIND_PORT && s->obj < IPC_PORTS) {
        struct ipc_port *pt = &g_port[s->obj];
        if (pt->used && pt->owner_pid == p->pid) {
            if (pt->waiter)
                robo_thread_wake(pt->waiter);
            memzero(pt, (unsigned)sizeof(*pt));
        }
    } else if (s->kind == IPC_KIND_CHAN && s->obj < IPC_CHANS) {
        close_chan_for_pid(&g_chan[s->obj], p->pid);
    }
    free_handle(p, handle);
    return 0;
}

static uint64_t recv_port(RoboProcess *rp, struct ipc_proc *p, struct ipc_port *pt,
                          uint64_t ubuf, uint64_t len, int nowait)
{
    RoboIpcAccept acc;
    int h;
    struct ipc_pend pend;
    uint32_t seq;
    RoboThread *self;

    for (;;) {
        if (pt->qn) {
            pend = pt->q[pt->qt];
            pt->qt = (uint8_t)((pt->qt + 1u) % IPC_Q);
            pt->qn--;
            h = alloc_handle(p);
            if (!h)
                return robo_sys_err(ROBO_EMFILE);
            if (pend.chan >= IPC_CHANS || !g_chan[pend.chan].used)
                return robo_sys_err(ROBO_EIO);
            g_chan[pend.chan].e[pend.server_end].handle = h;
            p->h[h].kind = IPC_KIND_CHAN;
            p->h[h].obj = pend.chan;
            p->h[h].end = pend.server_end;
            acc.magic = ROBO_IPC_ACCEPT_MAGIC;
            acc.handle = h;
            acc.peer_pid = pend.peer_pid;
            if (len < sizeof(acc))
                return robo_sys_err(ROBO_EINVAL);
            if (robo_copy_to_user(rp->address_space, ubuf, &acc, sizeof(acc)) != 0)
                return robo_sys_err(ROBO_EFAULT);
            return sizeof(acc);
        }
        if (nowait)
            return robo_sys_err(ROBO_EAGAIN);
        self = robo_current_thread();
        if (!self)
            return robo_sys_err(ROBO_EBUSY);
        seq = pt->seq;
        pt->waiter = self;
        if (pt->qn || pt->seq != seq) {
            pt->waiter = 0;
            continue;
        }
        if (!pt->used)
            return robo_sys_err(ROBO_EIO);
        robo_preempt_enable();
        robo_thread_block();
        robo_preempt_disable();
        pt->waiter = 0;
        if (!pt->used)
            return robo_sys_err(ROBO_EIO);
    }
}

static uint64_t recv_chan(RoboProcess *rp, struct ipc_end *e,
                          uint64_t ubuf, uint64_t len, int nowait)
{
    uint8_t tmp[IPC_MSG];
    int n;
    uint32_t seq;
    RoboThread *self;

    for (;;) {
        n = dequeue_end(e, tmp, IPC_MSG);
        if (n >= 0) {
            if ((uint64_t)n > len)
                n = (int)len;
            if (n && robo_copy_to_user(rp->address_space, ubuf, tmp, (size_t)n) != 0)
                return robo_sys_err(ROBO_EFAULT);
            return (uint64_t)n;
        }
        if (n == -ROBO_EIO)
            return robo_sys_err(ROBO_EIO);
        if (nowait)
            return robo_sys_err(ROBO_EAGAIN);
        self = robo_current_thread();
        if (!self)
            return robo_sys_err(ROBO_EBUSY);
        seq = e->seq;
        e->waiter = self;
        if (e->qn || e->closed || e->seq != seq) {
            e->waiter = 0;
            continue;
        }
        robo_preempt_enable();
        robo_thread_block();
        robo_preempt_disable();
        e->waiter = 0;
    }
}

static uint64_t op_shm_create(RoboProcess *rp, uint64_t bytes)
{
    unsigned i, n, k;
    struct ipc_shm *s = 0;
    uint64_t map_len;
    if (!bytes || bytes > (8ull * 1024ull * 1024ull))
        return robo_sys_err(ROBO_EINVAL);
    map_len = (bytes + ROBO_PAGE_MASK) & ~ROBO_PAGE_MASK;
    n = (unsigned)(map_len / ROBO_PAGE_SIZE);
    if (!n)
        return robo_sys_err(ROBO_EINVAL);
    for (i = 0; i < IPC_SHMS; i++) {
        if (!g_shm[i].used) {
            s = &g_shm[i];
            break;
        }
    }
    if (!s)
        return robo_sys_err(ROBO_ENFILE);
    memzero(s, (unsigned)sizeof(*s));
    s->pages = (phys_addr_t *)kmalloc((size_t)n * sizeof(phys_addr_t));
    if (!s->pages)
        return robo_sys_err(ROBO_ENOMEM);
    for (k = 0; k < n; k++)
        s->pages[k] = ROBO_INVALID_PHYS_ADDR;
    for (k = 0; k < n; k++) {
        phys_addr_t pa = robo_page_alloc();
        unsigned char *v;
        unsigned b;
        if (pa == ROBO_INVALID_PHYS_ADDR) {
            for (i = 0; i < k; i++)
                robo_page_free(s->pages[i]);
            kfree(s->pages);
            memzero(s, (unsigned)sizeof(*s));
            return robo_sys_err(ROBO_ENOMEM);
        }
        v = (unsigned char *)(uintptr_t)pa;
        for (b = 0; b < ROBO_PAGE_SIZE; b++)
            v[b] = 0;
        s->pages[k] = pa;
    }
    s->used = 1;
    s->id = g_shm_next++;
    if (!s->id)
        s->id = g_shm_next++;
    s->refs = 1;
    s->npages = n;
    s->bytes = map_len;
    s->owner_pid = rp ? rp->id : 0;
    return s->id;
}

static uint64_t op_shm_map(RoboProcess *rp, struct ipc_proc *p, uint32_t id)
{
    struct ipc_shm *s;
    unsigned mi, i;
    uint64_t va, stack_floor, vm;
    s = shm_find(id);
    if (!s)
        return robo_sys_err(ROBO_ENOENT);
    for (mi = 0; mi < IPC_MAPS; mi++) {
        if (!p->maps[mi].shm_id)
            break;
    }
    if (mi >= IPC_MAPS)
        return robo_sys_err(ROBO_EMFILE);
    if (!rp->mmap_next)
        rp->mmap_next = ROBO_USER_MMAP_BASE;
    va = rp->mmap_next;
    if (va & ROBO_PAGE_MASK)
        va = (va + ROBO_PAGE_MASK) & ~ROBO_PAGE_MASK;
    stack_floor = ROBO_USER_STACK_TOP - (uint64_t)ROBO_USER_STACK_SIZE;
    if (va < ROBO_USER_MMAP_BASE || va + s->bytes > stack_floor)
        return robo_sys_err(ROBO_EINVAL);
    vm = ROBO_VM_USER | ROBO_VM_READ | ROBO_VM_WRITE;
    for (i = 0; i < s->npages; i++) {
        if (!robo_vmm_aspace_map(rp->address_space,
                                 (virt_addr_t)(va + (uint64_t)i * ROBO_PAGE_SIZE),
                                 s->pages[i], vm)) {
            unsigned u;
            for (u = 0; u < i; u++) {
                phys_addr_t pa = ROBO_INVALID_PHYS_ADDR;
                robo_vmm_aspace_unmap(rp->address_space,
                                      (virt_addr_t)(va + (uint64_t)u * ROBO_PAGE_SIZE),
                                      &pa);
            }
            return robo_sys_err(ROBO_EINVAL);
        }
    }
    p->maps[mi].shm_id = s->id;
    p->maps[mi].va = va;
    p->maps[mi].bytes = s->bytes;
    s->refs++;
    rp->mmap_next = va + s->bytes;
    return va;
}

static uint64_t op_shm_unmap(struct ipc_proc *p, uint64_t va)
{
    unsigned i;
    for (i = 0; i < IPC_MAPS; i++) {
        if (p->maps[i].shm_id && p->maps[i].va == va) {
            unmap_one(p, i);
            return 0;
        }
    }
    return robo_sys_err(ROBO_EINVAL);
}

uint64_t robo_ipc_syscall(RoboSyscallContext *ctx)
{
    uint64_t op, handle, buf, len, flags;
    RoboProcess *rp;
    struct ipc_proc *p;
    char name[IPC_NAME];
    uint64_t r;
    int nowait;

    if (!ctx || !ctx->process || ctx->process == robo_kernel_process())
        return robo_sys_err(ROBO_EPERM);
    rp = ctx->process;
    op = ctx->frame->rdi;
    handle = ctx->frame->rsi;
    buf = ctx->frame->rdx;
    len = ctx->frame->r10;
    flags = ctx->frame->r8;
    nowait = (flags & ROBO_IPC_NOWAIT) != 0;

    robo_preempt_disable();
    p = proc_get(rp->id, 1);
    if (!p) {
        robo_preempt_enable();
        return robo_sys_err(ROBO_ENOMEM);
    }

    if (op == ROBO_IPC_OP_LISTEN || op == ROBO_IPC_OP_CONNECT) {
        memzero(name, IPC_NAME);
        robo_preempt_enable();
        if (robo_copy_user_string(rp->address_space, name, buf, IPC_NAME) != 0)
            return robo_sys_err(ROBO_EFAULT);
        robo_preempt_disable();
        p = proc_get(rp->id, 1);
        if (!p) {
            robo_preempt_enable();
            return robo_sys_err(ROBO_ENOMEM);
        }
        r = (op == ROBO_IPC_OP_LISTEN) ? op_listen(rp, p, name)
                                       : op_connect(rp, p, name);
        robo_preempt_enable();
        return r;
    }

    if (op == ROBO_IPC_OP_CLOSE) {
        r = op_close(p, (int)handle);
        robo_preempt_enable();
        return r;
    }

    if (op == ROBO_IPC_OP_SHM_CREATE) {
        r = op_shm_create(rp, len);
        robo_preempt_enable();
        return r;
    }
    if (op == ROBO_IPC_OP_SHM_MAP) {
        r = op_shm_map(rp, p, (uint32_t)handle);
        robo_preempt_enable();
        return r;
    }
    if (op == ROBO_IPC_OP_SHM_UNMAP) {
        r = op_shm_unmap(p, buf);
        robo_preempt_enable();
        return r;
    }

    if (op == ROBO_IPC_OP_SEND) {
        uint8_t tmp[IPC_MSG];
        struct ipc_slot *s;
        struct ipc_end *peer;
        int n;
        if (handle == 0 || handle >= IPC_HANDLES || !p->h[handle].kind) {
            robo_preempt_enable();
            return robo_sys_err(ROBO_EBADF);
        }
        if (len > IPC_MSG) {
            robo_preempt_enable();
            return robo_sys_err(ROBO_EINVAL);
        }
        s = &p->h[handle];
        if (s->kind != IPC_KIND_CHAN) {
            robo_preempt_enable();
            return robo_sys_err(ROBO_EBADF);
        }
        robo_preempt_enable();
        if (len && robo_copy_from_user(rp->address_space, tmp, buf, (size_t)len) != 0)
            return robo_sys_err(ROBO_EFAULT);
        robo_preempt_disable();
        p = proc_get(rp->id, 0);
        if (!p || p->h[handle].kind != IPC_KIND_CHAN) {
            robo_preempt_enable();
            return robo_sys_err(ROBO_EBADF);
        }
        s = &p->h[handle];
        if (s->obj >= IPC_CHANS || !g_chan[s->obj].used) {
            robo_preempt_enable();
            return robo_sys_err(ROBO_EIO);
        }
        peer = &g_chan[s->obj].e[s->end ^ 1u];
        n = enqueue_end(peer, tmp, (unsigned)len);
        robo_preempt_enable();
        if (n < 0)
            return robo_sys_err(-n);
        return (uint64_t)n;
    }

    if (op == ROBO_IPC_OP_RECV) {
        struct ipc_slot *s;
        if (handle == 0 || handle >= IPC_HANDLES || !p->h[handle].kind) {
            robo_preempt_enable();
            return robo_sys_err(ROBO_EBADF);
        }
        s = &p->h[handle];
        if (s->kind == IPC_KIND_PORT) {
            if (s->obj >= IPC_PORTS || !g_port[s->obj].used) {
                robo_preempt_enable();
                return robo_sys_err(ROBO_EBADF);
            }
            r = recv_port(rp, p, &g_port[s->obj], buf, len, nowait);
            robo_preempt_enable();
            return r;
        }
        if (s->kind == IPC_KIND_CHAN) {
            if (s->obj >= IPC_CHANS || !g_chan[s->obj].used) {
                robo_preempt_enable();
                return robo_sys_err(ROBO_EIO);
            }
            r = recv_chan(rp, &g_chan[s->obj].e[s->end], buf, len, nowait);
            robo_preempt_enable();
            return r;
        }
        robo_preempt_enable();
        return robo_sys_err(ROBO_EBADF);
    }

    robo_preempt_enable();
    return robo_sys_err(ROBO_EINVAL);
}
