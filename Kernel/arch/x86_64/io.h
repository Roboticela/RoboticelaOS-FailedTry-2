#ifndef ROBO_ARCH_IO_H
#define ROBO_ARCH_IO_H

#include <stdint.h>

static inline void Outb(uint16_t port, uint8_t v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint8_t Inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void Outw(uint16_t port, uint16_t v)
{
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint16_t Inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void Outl(uint16_t port, uint32_t v)
{
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(port));
}

static inline uint32_t Inl(uint16_t port)
{
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

#endif
