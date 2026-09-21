#include "arch/x86_64/exception.h"
#include "arch/x86_64/timer/pit.h"
#include "irq/irq.h"
#include "time/time.h"

static IrqFn gFn[16];
static void *gCtx[16];

int IrqRegister(unsigned irq, IrqFn fn, void *ctx)
{
    if (irq >= 16u || !fn)
        return 0;
    if (irq == RoboPicIrqPit)
        return 0;
    gFn[irq] = fn;
    gCtx[irq] = ctx;
    if (irq >= 8u)
        PicUnmask(2);
    PicUnmask(irq);
    return 1;
}

void IrqUnregister(unsigned irq)
{
    if (irq >= 16u)
        return;
    gFn[irq] = 0;
    gCtx[irq] = 0;
}

void IrqDispatch(struct Iframe *frame)
{
    unsigned irq;
    if (!frame)
        return;
    irq = (unsigned)(frame->vector - RoboIrqBase);
    if (irq >= 16u) {
        PicEoi(0);
        return;
    }
    if (irq == RoboPicIrqPit)
        TimerIrq();
    else if (gFn[irq])
        gFn[irq](irq, gCtx[irq]);
    PicEoi(irq);
}
