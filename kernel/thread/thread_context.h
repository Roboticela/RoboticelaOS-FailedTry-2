#ifndef ROBO_THREAD_CONTEXT_H
#define ROBO_THREAD_CONTEXT_H

#include <stdint.h>
#include "arch/x86_64/context/context.h"

/*
 * Generic thread CPU context.
 *
 * The architecture-specific interrupt frame (Phase 5/8) is authoritative.
 * RIP/RSP live there; instruction_pointer and the stack pointer are views
 * over that frame, not a second mutable copy.
 */
typedef robo_cpu_context_t robo_thread_context_t;

static inline uint64_t robo_thread_context_ip(const robo_thread_context_t *ctx)
{
    return ctx ? ctx->rip : 0;
}

static inline uint64_t robo_thread_context_sp(const robo_thread_context_t *ctx)
{
    return ctx ? ctx->rsp : 0;
}

#endif
