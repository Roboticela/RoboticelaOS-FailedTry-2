#ifndef ROBO_IRQ_H
#define ROBO_IRQ_H

typedef void (*robo_irq_fn)(unsigned irq, void *ctx);

int robo_irq_register(unsigned irq, robo_irq_fn fn, void *ctx);
void robo_irq_unregister(unsigned irq);

#endif
