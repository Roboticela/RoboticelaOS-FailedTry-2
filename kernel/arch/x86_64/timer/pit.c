#include "arch/x86_64/timer/pit.h"
#include "arch/x86_64/timer/tsc.h"

static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIT_CH0   0x40
#define PIT_CH2   0x42
#define PIT_CMD   0x43
#define PIT_GATE  0x61

static uint32_t g_pit_div = 11931;

void robo_pic_init(void)
{
    uint8_t m1, m2;
    m1 = inb(PIC1_DATA);
    m2 = inb(PIC2_DATA);
    (void)m1;
    (void)m2;
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, (uint8_t)ROBO_IRQ_BASE);
    outb(PIC2_DATA, (uint8_t)(ROBO_IRQ_BASE + 8u));
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void robo_pic_eoi(unsigned irq)
{
    if (irq >= 8u)
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

void robo_pic_mask(unsigned irq)
{
    uint16_t port = irq < 8u ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (uint8_t)(1u << (irq & 7u));
    outb(port, inb(port) | bit);
}

void robo_pic_unmask(unsigned irq)
{
    uint16_t port = irq < 8u ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (uint8_t)(1u << (irq & 7u));
    outb(port, inb(port) & (uint8_t)~bit);
}

void robo_pit_program(unsigned hz)
{
    uint32_t div;
    if (hz < 1u)
        hz = 1u;
    div = (ROBO_PIT_HZ + hz / 2u) / hz;
    if (div < 1u)
        div = 1u;
    if (div > 65535u)
        div = 65535u;
    g_pit_div = div;
    /* Channel 0, lobyte/hibyte, mode 2 (rate generator). */
    outb(PIT_CMD, 0x34);
    outb(PIT_CH0, (uint8_t)(div & 0xFFu));
    outb(PIT_CH0, (uint8_t)((div >> 8) & 0xFFu));
}

uint16_t robo_pit_latch0(void)
{
    uint8_t lo, hi;
    outb(PIT_CMD, 0x00);
    lo = inb(PIT_CH0);
    hi = inb(PIT_CH0);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

uint32_t robo_pit_divisor(void)
{
    return g_pit_div;
}

uint64_t robo_pit_calibrate_tsc(uint32_t pit_ticks)
{
    uint8_t sp;
    uint64_t t0, t1, dt, q, r;
    unsigned spins = 0;

    if (pit_ticks < 1000u || pit_ticks > 65535u)
        return 0;

    /* Channel 2, mode 0 one-shot. Gate on, speaker data off.
     * Busy-wait here is calibration only, not robo_sleep(). */
    sp = inb(PIT_GATE);
    outb(PIT_GATE, (uint8_t)((sp & (uint8_t)~0x02u) | 0x01u));
    outb(PIT_CMD, 0xB0);
    outb(PIT_CH2, (uint8_t)(pit_ticks & 0xFFu));
    outb(PIT_CH2, (uint8_t)((pit_ticks >> 8) & 0xFFu));

    t0 = robo_x86_tsc_read();
    while ((inb(PIT_GATE) & 0x20u) == 0) {
        if (++spins > 200000000u) {
            outb(PIT_GATE, sp);
            return 0;
        }
    }
    t1 = robo_x86_tsc_read();
    outb(PIT_GATE, sp);

    if (t1 <= t0)
        return 0;
    dt = t1 - t0;
    q = dt / pit_ticks;
    r = dt % pit_ticks;
    if (q > UINT64_MAX / (uint64_t)ROBO_PIT_HZ)
        return 0;
    return q * (uint64_t)ROBO_PIT_HZ + (r * (uint64_t)ROBO_PIT_HZ) / pit_ticks;
}
