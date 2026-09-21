#include "display/display.h"
#include "syscall/syscall_errors.h"
#include "syscall/syscall_numbers.h"
#include "syscall/syscall_validate.h"
#include "process/process.h"
#include "graphics/gfx.h"
#include "input/input.h"
#include "kio.h"
#include <robosys/display.h>
#include <robosys/input.h>

static uint64_t gOutOwner;
static uint64_t gInOwner;
static int gOutClaimed;
static int gInClaimed;
static int gHeadless;

static uint64_t arg(const RoboSyscallContext *ctx, unsigned n)
{
    const struct Iframe *f = ctx->frame;
    switch (n) {
    case 1: return f->rdi;
    case 2: return f->rsi;
    case 3: return f->rdx;
    case 4: return f->r10;
    case 5: return f->r8;
    default: return 0;
    }
}

static int IsOut(const RoboProcess *p)
{
    return p && gOutClaimed && gOutOwner == p->id;
}

static int IsIn(const RoboProcess *p)
{
    return p && gInClaimed && gInOwner == p->id;
}

static int IsAny(const RoboProcess *p)
{
    return IsOut(p) || IsIn(p);
}

void DisplayOnProcessExit(uint64_t pid)
{
    if (gOutClaimed && gOutOwner == pid) {
        gOutClaimed = 0;
        gOutOwner = 0;
        kprintf("[DISPLAY] output pid %llu released\n",
                (unsigned long long)pid);
    }
    if (gInClaimed && gInOwner == pid) {
        gInClaimed = 0;
        gInOwner = 0;
        kprintf("[DISPLAY] input pid %llu released\n",
                (unsigned long long)pid);
    }
}

static uint64_t OpClaim(RoboProcess *p, uint64_t flags)
{
    RoboDisplayInfo info;
    uint32_t role = (uint32_t)flags;
    if (!role)
        role = RoboDispRoleOutput | RoboDispRoleInput;
    if ((role & RoboDispRoleOutput) && gOutClaimed && gOutOwner != p->id)
        return SysErr(RoboEbusy);
    if ((role & RoboDispRoleInput) && gInClaimed && gInOwner != p->id)
        return SysErr(RoboEbusy);
    if (!GfxReady() || !DisplayGetInfo(&info) ||
        !info.framebuffer) {
        gHeadless = 1;
        if (role & RoboDispRoleOutput) {
            gOutClaimed = 1;
            gOutOwner = p->id;
        }
        if (role & RoboDispRoleInput) {
            gInClaimed = 1;
            gInOwner = p->id;
        }
        kprintf("[DISPLAY] HEADLESS claim pid %llu role %u\n",
                (unsigned long long)p->id, (unsigned)role);
        return 0;
    }
    gHeadless = 0;
    if (role & RoboDispRoleOutput) {
        gOutClaimed = 1;
        gOutOwner = p->id;
        kprintf("[DISPLAY] output %u x %u pid %llu\n",
                (unsigned)info.width, (unsigned)info.height,
                (unsigned long long)p->id);
    }
    if (role & RoboDispRoleInput) {
        gInClaimed = 1;
        gInOwner = p->id;
        kprintf("[DISPLAY] input pid %llu\n", (unsigned long long)p->id);
    }
    return 0;
}

static uint64_t OpInfo(RoboProcess *p, uint64_t ubuf)
{
    RoboDisplayInfo info;
    RoboUserDisplay u;
    if (!IsAny(p))
        return SysErr(RoboEperm);
    u.width = 0;
    u.height = 0;
    u.pitch = 0;
    u.bpp = 0;
    u.pixfmt = 0;
    u.ready = 0;
    if (GfxReady() && DisplayGetInfo(&info) && info.framebuffer) {
        u.width = info.width;
        u.height = info.height;
        u.pitch = info.pitch;
        u.bpp = info.bpp;
        u.pixfmt = info.pixfmt;
        u.ready = 1;
    }
    if (CopyToUser(p->addressSpace, ubuf, &u, sizeof(u)) != 0)
        return SysErr(RoboEfault);
    return 0;
}

static uint64_t OpPresent(RoboProcess *p, uint64_t ubuf)
{
    RoboDisplayPresent req;
    RoboDisplayInfo info;
    uint32_t y, x, copyW, copyH, dstStride, srcX, srcY;
    int32_t destX, destY;
    uint32_t *fb;
    uint64_t src;
    uint32_t tmp[1024];

    if (!IsOut(p))
        return SysErr(RoboEperm);
    if (gHeadless || !GfxReady() || !DisplayGetInfo(&info) ||
        !info.framebuffer)
        return 0;
    if (CopyFromUser(p->addressSpace, &req, ubuf, sizeof(req)) != 0)
        return SysErr(RoboEfault);
    if (!req.pixels || !req.width || !req.height || req.stride < req.width)
        return SysErr(RoboEinval);
    if (req.width > 4096u || req.height > 4096u)
        return SysErr(RoboEinval);
    fb = (uint32_t *)info.framebuffer;
    dstStride = info.pitch / 4u;
    if (!dstStride)
        return SysErr(RoboEnodev);
    destX = req.destX;
    destY = req.destY;
    srcX = 0;
    srcY = 0;
    if (destX < 0) {
        srcX = (uint32_t)(-destX);
        destX = 0;
    }
    if (destY < 0) {
        srcY = (uint32_t)(-destY);
        destY = 0;
    }
    if (srcX >= req.width || srcY >= req.height)
        return 0;
    if ((uint32_t)destX >= info.width || (uint32_t)destY >= info.height)
        return 0;
    copyW = req.width - srcX;
    copyH = req.height - srcY;
    if ((uint32_t)destX + copyW > info.width)
        copyW = info.width - (uint32_t)destX;
    if ((uint32_t)destY + copyH > info.height)
        copyH = info.height - (uint32_t)destY;
    for (y = 0; y < copyH; y++) {
        uint32_t done = 0;
        while (done < copyW) {
            uint32_t n = copyW - done;
            if (n > 1024u)
                n = 1024u;
            src = req.pixels + ((uint64_t)(srcY + y) * (uint64_t)req.stride +
                                srcX + done) * 4ull;
            if (CopyFromUser(p->addressSpace, tmp, src, (size_t)n * 4u) != 0)
                return SysErr(RoboEfault);
            for (x = 0; x < n; x++)
                fb[((uint32_t)destY + y) * dstStride +
                   (uint32_t)destX + done + x] = tmp[x];
            done += n;
        }
    }
    (void)req.pixfmt;
    return 0;
}

static uint64_t OpInput(RoboProcess *p, uint64_t ubuf, uint64_t flags)
{
    RoboInputEvent ev;
    if (!IsIn(p))
        return SysErr(RoboEperm);
    if (!InputPop(&ev)) {
        if (flags & RoboDispNowait)
            return SysErr(RoboEagain);
        return SysErr(RoboEagain);
    }
    if (CopyToUser(p->addressSpace, ubuf, &ev, sizeof(ev)) != 0)
        return SysErr(RoboEfault);
    return sizeof(ev);
}

static uint64_t OpInject(RoboProcess *p, uint64_t ubuf)
{
    RoboInputEvent ev;
    if (!IsIn(p))
        return SysErr(RoboEperm);
    if (CopyFromUser(p->addressSpace, &ev, ubuf, sizeof(ev)) != 0)
        return SysErr(RoboEfault);
    if (!ev.type)
        return SysErr(RoboEinval);
    if (!RoboInputPush(&ev))
        return SysErr(RoboEagain);
    return 0;
}

uint64_t SysDisplay(RoboSyscallContext *ctx)
{
    uint64_t op, buf, flags;
    RoboProcess *p;

    if (!ctx || !ctx->process || ctx->process == KernelProcess())
        return SysErr(RoboEperm);
    p = ctx->process;
    op = arg(ctx, 1);
    buf = arg(ctx, 3);
    flags = arg(ctx, 5);
    if (op == RoboDispClaim)
        return OpClaim(p, flags);
    if (op == RoboDispRelease) {
        if (!IsAny(p))
            return SysErr(RoboEperm);
        if (IsOut(p)) {
            gOutClaimed = 0;
            gOutOwner = 0;
        }
        if (IsIn(p)) {
            gInClaimed = 0;
            gInOwner = 0;
        }
        return 0;
    }
    if (op == RoboDispInfo)
        return OpInfo(p, buf);
    if (op == RoboDispPresent)
        return OpPresent(p, buf);
    if (op == RoboDispInput)
        return OpInput(p, buf, flags);
    if (op == RoboDispInject)
        return OpInject(p, buf);
    return SysErr(RoboEinval);
}
