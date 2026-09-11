#include "arch/x86_64/exception.h"
#include "arch/x86_64/timer/pit.h"
#include "time/time.h"

void robo_irq_dispatch(struct robo_iframe *frame)
{
    unsigned irq;
    if (!frame)
        return;
    irq = (unsigned)(frame->vector - ROBO_IRQ_BASE);
    if (irq == ROBO_PIC_IRQ_PIT)
        robo_timer_irq();
    robo_pic_eoi(irq);
}
