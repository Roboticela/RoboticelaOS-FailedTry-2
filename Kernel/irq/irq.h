#ifndef ROBO_IRQ_H
#define ROBO_IRQ_H

typedef void (*IrqFn)(unsigned irq, void *ctx);

int IrqRegister(unsigned irq, IrqFn fn, void *ctx);
void IrqUnregister(unsigned irq);

#endif
