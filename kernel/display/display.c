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

static uint64_t g_out_owner;
static uint64_t g_in_owner;
static int g_out_claimed;
static int g_in_claimed;
static int g_headless;

static uint64_t arg(const RoboSyscallContext *ctx, unsigned n)
{
    const struct robo_iframe *f = ctx->frame;
    switch (n) {
    case 1: return f->rdi;
    case 2: return f->rsi;
    case 3: return f->rdx;
    case 4: return f->r10;
    case 5: return f->r8;
    default: return 0;
    }
}

static int is_out(const RoboProcess *p)
{
    return p && g_out_claimed && g_out_owner == p->id;
}

static int is_in(const RoboProcess *p)
{
    return p && g_in_claimed && g_in_owner == p->id;
}

static int is_any(const RoboProcess *p)
{
    return is_out(p) || is_in(p);
}

void robo_display_on_process_exit(uint64_t pid)
{
    if (g_out_claimed && g_out_owner == pid) {
        g_out_claimed = 0;
        g_out_owner = 0;
        kprintf("[DISPLAY] output pid %llu released\n",
                (unsigned long long)pid);
    }
    if (g_in_claimed && g_in_owner == pid) {
        g_in_claimed = 0;
        g_in_owner = 0;
        kprintf("[DISPLAY] input pid %llu released\n",
                (unsigned long long)pid);
    }
}

static uint64_t op_claim(RoboProcess *p, uint64_t flags)
{
    RoboDisplayInfo info;
    uint32_t role = (uint32_t)flags;
    if (!role)
        role = ROBO_DISP_ROLE_OUTPUT | ROBO_DISP_ROLE_INPUT;
    if ((role & ROBO_DISP_ROLE_OUTPUT) && g_out_claimed && g_out_owner != p->id)
        return robo_sys_err(ROBO_EBUSY);
    if ((role & ROBO_DISP_ROLE_INPUT) && g_in_claimed && g_in_owner != p->id)
        return robo_sys_err(ROBO_EBUSY);
    if (!robo_gfx_ready() || !robo_display_get_info(&info) ||
        !info.framebuffer) {
        g_headless = 1;
        if (role & ROBO_DISP_ROLE_OUTPUT) {
            g_out_claimed = 1;
            g_out_owner = p->id;
        }
        if (role & ROBO_DISP_ROLE_INPUT) {
            g_in_claimed = 1;
            g_in_owner = p->id;
        }
        kprintf("[DISPLAY] HEADLESS claim pid %llu role %u\n",
                (unsigned long long)p->id, (unsigned)role);
        return 0;
    }
    g_headless = 0;
    if (role & ROBO_DISP_ROLE_OUTPUT) {
        g_out_claimed = 1;
        g_out_owner = p->id;
        kprintf("[DISPLAY] output %u x %u pid %llu\n",
                (unsigned)info.width, (unsigned)info.height,
                (unsigned long long)p->id);
    }
    if (role & ROBO_DISP_ROLE_INPUT) {
        g_in_claimed = 1;
        g_in_owner = p->id;
        kprintf("[DISPLAY] input pid %llu\n", (unsigned long long)p->id);
    }
    return 0;
}

static uint64_t op_info(RoboProcess *p, uint64_t ubuf)
{
    RoboDisplayInfo info;
    RoboUserDisplay u;
    if (!is_any(p))
        return robo_sys_err(ROBO_EPERM);
    u.width = 0;
    u.height = 0;
    u.pitch = 0;
    u.bpp = 0;
    u.pixfmt = 0;
    u.ready = 0;
    if (robo_gfx_ready() && robo_display_get_info(&info) && info.framebuffer) {
        u.width = info.width;
        u.height = info.height;
        u.pitch = info.pitch;
        u.bpp = info.bpp;
        u.pixfmt = info.pixfmt;
        u.ready = 1;
    }
    if (robo_copy_to_user(p->address_space, ubuf, &u, sizeof(u)) != 0)
        return robo_sys_err(ROBO_EFAULT);
    return 0;
}

static uint64_t op_present(RoboProcess *p, uint64_t ubuf)
{
    RoboDisplayPresent req;
    RoboDisplayInfo info;
    uint32_t y, x, copy_w, copy_h, dst_stride, src_x, src_y;
    int32_t dest_x, dest_y;
    uint32_t *fb;
    uint64_t src;
    uint32_t tmp[1024];

    if (!is_out(p))
        return robo_sys_err(ROBO_EPERM);
    if (g_headless || !robo_gfx_ready() || !robo_display_get_info(&info) ||
        !info.framebuffer)
        return 0;
    if (robo_copy_from_user(p->address_space, &req, ubuf, sizeof(req)) != 0)
        return robo_sys_err(ROBO_EFAULT);
    if (!req.pixels || !req.width || !req.height || req.stride < req.width)
        return robo_sys_err(ROBO_EINVAL);
    if (req.width > 4096u || req.height > 4096u)
        return robo_sys_err(ROBO_EINVAL);
    fb = (uint32_t *)info.framebuffer;
    dst_stride = info.pitch / 4u;
    if (!dst_stride)
        return robo_sys_err(ROBO_ENODEV);
    dest_x = req.dest_x;
    dest_y = req.dest_y;
    src_x = 0;
    src_y = 0;
    if (dest_x < 0) {
        src_x = (uint32_t)(-dest_x);
        dest_x = 0;
    }
    if (dest_y < 0) {
        src_y = (uint32_t)(-dest_y);
        dest_y = 0;
    }
    if (src_x >= req.width || src_y >= req.height)
        return 0;
    if ((uint32_t)dest_x >= info.width || (uint32_t)dest_y >= info.height)
        return 0;
    copy_w = req.width - src_x;
    copy_h = req.height - src_y;
    if ((uint32_t)dest_x + copy_w > info.width)
        copy_w = info.width - (uint32_t)dest_x;
    if ((uint32_t)dest_y + copy_h > info.height)
        copy_h = info.height - (uint32_t)dest_y;
    for (y = 0; y < copy_h; y++) {
        uint32_t done = 0;
        while (done < copy_w) {
            uint32_t n = copy_w - done;
            if (n > 1024u)
                n = 1024u;
            src = req.pixels + ((uint64_t)(src_y + y) * (uint64_t)req.stride +
                                src_x + done) * 4ull;
            if (robo_copy_from_user(p->address_space, tmp, src, (size_t)n * 4u) != 0)
                return robo_sys_err(ROBO_EFAULT);
            for (x = 0; x < n; x++)
                fb[((uint32_t)dest_y + y) * dst_stride +
                   (uint32_t)dest_x + done + x] = tmp[x];
            done += n;
        }
    }
    (void)req.pixfmt;
    return 0;
}

static uint64_t op_input(RoboProcess *p, uint64_t ubuf, uint64_t flags)
{
    RoboInputEvent ev;
    if (!is_in(p))
        return robo_sys_err(ROBO_EPERM);
    if (!robo_input_pop(&ev)) {
        if (flags & ROBO_DISP_NOWAIT)
            return robo_sys_err(ROBO_EAGAIN);
        return robo_sys_err(ROBO_EAGAIN);
    }
    if (robo_copy_to_user(p->address_space, ubuf, &ev, sizeof(ev)) != 0)
        return robo_sys_err(ROBO_EFAULT);
    return sizeof(ev);
}

static uint64_t op_inject(RoboProcess *p, uint64_t ubuf)
{
    RoboInputEvent ev;
    if (!is_in(p))
        return robo_sys_err(ROBO_EPERM);
    if (robo_copy_from_user(p->address_space, &ev, ubuf, sizeof(ev)) != 0)
        return robo_sys_err(ROBO_EFAULT);
    if (!ev.type)
        return robo_sys_err(ROBO_EINVAL);
    if (!robo_input_push(&ev))
        return robo_sys_err(ROBO_EAGAIN);
    return 0;
}

uint64_t robo_sys_display(RoboSyscallContext *ctx)
{
    uint64_t op, buf, flags;
    RoboProcess *p;

    if (!ctx || !ctx->process || ctx->process == robo_kernel_process())
        return robo_sys_err(ROBO_EPERM);
    p = ctx->process;
    op = arg(ctx, 1);
    buf = arg(ctx, 3);
    flags = arg(ctx, 5);
    if (op == ROBO_DISP_CLAIM)
        return op_claim(p, flags);
    if (op == ROBO_DISP_RELEASE) {
        if (!is_any(p))
            return robo_sys_err(ROBO_EPERM);
        if (is_out(p)) {
            g_out_claimed = 0;
            g_out_owner = 0;
        }
        if (is_in(p)) {
            g_in_claimed = 0;
            g_in_owner = 0;
        }
        return 0;
    }
    if (op == ROBO_DISP_INFO)
        return op_info(p, buf);
    if (op == ROBO_DISP_PRESENT)
        return op_present(p, buf);
    if (op == ROBO_DISP_INPUT)
        return op_input(p, buf, flags);
    if (op == ROBO_DISP_INJECT)
        return op_inject(p, buf);
    return robo_sys_err(ROBO_EINVAL);
}
