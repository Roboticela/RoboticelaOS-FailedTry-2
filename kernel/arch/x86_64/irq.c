#include "arch/x86_64/exception.h"
#include "arch/x86_64/timer/pit.h"
#include "irq/irq.h"
#include "time/time.h"

static robo_irq_fn g_fn[16];
static void *g_ctx[16];

int robo_irq_register(unsigned irq, robo_irq_fn fn, void *ctx)
{
    if (irq >= 16u || !fn)
        return 0;
    if (irq == ROBO_PIC_IRQ_PIT)
        return 0;
    g_fn[irq] = fn;
    g_ctx[irq] = ctx;
    if (irq >= 8u)
        robo_pic_unmask(2);
    robo_pic_unmask(irq);
    return 1;
}

void robo_irq_unregister(unsigned irq)
{
    if (irq >= 16u)
        return;
    g_fn[irq] = 0;
    g_ctx[irq] = 0;
}

void robo_irq_dispatch(struct robo_iframe *frame)
{
    unsigned irq;
    if (!frame)
        return;
    irq = (unsigned)(frame->vector - ROBO_IRQ_BASE);
    if (irq >= 16u) {
        robo_pic_eoi(0);
        return;
    }
    if (irq == ROBO_PIC_IRQ_PIT)
        robo_timer_irq();
    else if (g_fn[irq])
        g_fn[irq](irq, g_ctx[irq]);
    robo_pic_eoi(irq);
}
