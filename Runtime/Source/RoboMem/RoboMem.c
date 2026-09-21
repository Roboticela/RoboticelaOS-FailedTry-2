/*
 * RoboMem — Goal 2.1 heap + Goal 2.4 optional debug allocator.
 */

#define RoboMemImplementation
#include "RoboMem/RoboMem.h"
#include "RoboMem/RoboMemDebug.h"

#include "RoboBase/RoboLog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RoboMemMagic 0x524D454Du /* 'RMEM' */
#define RoboMemStateLive 1u
#define RoboMemStateFreed 2u
#define RoboMemGuardSize 16
#define RoboMemGuardByte 0xABu

typedef struct MemBlock {
    uint32_t magic;
    uint32_t state;
    size_t size;
    size_t id;
    const char *file;
    int line;
    void *sysBase; /* original malloc pointer (may precede this header) */
    struct MemBlock *next;
    struct MemBlock *prev;
} MemBlock;

/* User pointer alignment: at least pointer size; prefer max_align_t (C11). */
#ifdef __STDC_VERSION__
#if __STDC_VERSION__ >= 201112L
#define RoboMemUserAlign ((size_t)_Alignof(max_align_t))
#else
#define RoboMemUserAlign (sizeof(void *))
#endif
#else
#define RoboMemUserAlign (sizeof(void *))
#endif

static size_t gRoboMemFailRemaining = 0;

#if RoboMemDebug
#define RoboMemQuarantineMax 64

static int gRoboMemDbgOn = 1;
static size_t gRoboMemNextId = 1;
static MemBlock *gRoboMemLive = NULL;
static MemBlock *gRoboMemQuarantine = NULL;
static size_t gRoboMemQuarantineCount = 0;
static MemStats gRoboMemStats;
static MemFaultHandlerFn GRoboMemFault = NULL;
static void *gRoboMemFaultUd = NULL;
#endif

void MemDebugFailNext(size_t count)
{
    gRoboMemFailRemaining = count;
}

static int MemShouldFail(void)
{
    if (gRoboMemFailRemaining == 0) {
        return 0;
    }
    gRoboMemFailRemaining--;
    return 1;
}

static int MemSizeOverflow(size_t count, size_t size)
{
    if (count == 0 || size == 0) {
        return 0;
    }
    if (count > SIZE_MAX / size) {
        return 1;
    }
    return 0;
}

static void *SysMalloc(size_t size)
{
    return malloc(size);
}

static void *SysCalloc(size_t count, size_t size)
{
    return calloc(count, size);
}

static void *SysRealloc(void *ptr, size_t size)
{
    return realloc(ptr, size);
}

static void SysFree(void *ptr)
{
    free(ptr);
}

#if RoboMemDebug

static void MemFault(const char *kind, const char *message)
{
    if (GRoboMemFault) {
        GRoboMemFault(kind, message, gRoboMemFaultUd);
        return;
    }
    LogError("RoboMemDebug %s: %s", kind, message ? message : "");
    abort();
}

static unsigned char *MemUserFromBlock(MemBlock *block)
{
    return (unsigned char *)(block + 1) + RoboMemGuardSize;
}

static unsigned char *MemFrontGuard(MemBlock *block)
{
    return (unsigned char *)(block + 1);
}

static unsigned char *MemBackGuard(MemBlock *block)
{
    return MemUserFromBlock(block) + block->size;
}

static void MemFillGuards(MemBlock *block)
{
    memset(MemFrontGuard(block), RoboMemGuardByte, RoboMemGuardSize);
    memset(MemBackGuard(block), RoboMemGuardByte, RoboMemGuardSize);
}

static int MemGuardsOk(MemBlock *block)
{
    size_t i;
    unsigned char *f = MemFrontGuard(block);
    unsigned char *b = MemBackGuard(block);
    for (i = 0; i < RoboMemGuardSize; i++) {
        if (f[i] != RoboMemGuardByte || b[i] != RoboMemGuardByte) {
            return 0;
        }
    }
    return 1;
}

static void MemLink(MemBlock *block)
{
    block->prev = NULL;
    block->next = gRoboMemLive;
    if (gRoboMemLive) {
        gRoboMemLive->prev = block;
    }
    gRoboMemLive = block;
}

static void MemUnlinkFrom(MemBlock **head, MemBlock *block)
{
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        *head = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
    block->next = NULL;
    block->prev = NULL;
}

static void MemUnlink(MemBlock *block)
{
    MemUnlinkFrom(&gRoboMemLive, block);
}

static void MemQuarantinePush(MemBlock *block)
{
    block->prev = NULL;
    block->next = gRoboMemQuarantine;
    if (gRoboMemQuarantine) {
        gRoboMemQuarantine->prev = block;
    }
    gRoboMemQuarantine = block;
    gRoboMemQuarantineCount++;

    while (gRoboMemQuarantineCount > RoboMemQuarantineMax) {
        MemBlock *tail = gRoboMemQuarantine;
        while (tail && tail->next) {
            tail = tail->next;
        }
        if (!tail) {
            break;
        }
        MemUnlinkFrom(&gRoboMemQuarantine, tail);
        gRoboMemQuarantineCount--;
        {
            void *base = tail->sysBase ? tail->sysBase : (void *)tail;
            tail->magic = 0;
            SysFree(base);
        }
    }
}

static size_t MemUserAlign(void)
{
    size_t a = RoboMemUserAlign;
    if (a < sizeof(void *)) {
        a = sizeof(void *);
    }
    /* Require power-of-two for mask alignment. */
    if (a == 0 || (a & (a - 1)) != 0) {
        a = sizeof(void *);
    }
    return a;
}

static size_t MemTotalBytes(size_t userSize)
{
    size_t prefix = sizeof(MemBlock) + RoboMemGuardSize;
    size_t suffix = RoboMemGuardSize;
    size_t align = MemUserAlign();
    size_t body;

    if (userSize > SIZE_MAX - prefix || prefix + userSize > SIZE_MAX - suffix) {
        return 0;
    }
    body = prefix + userSize + suffix;
    /* Extra slack so the user pointer can be aligned within the slab. */
    if (body > SIZE_MAX - align) {
        return 0;
    }
    return body + align;
}

static void *MemTrackAlloc(size_t size, const char *file, int line, int zero)
{
    size_t total;
    size_t align;
    size_t front;
    unsigned char *raw;
    uintptr_t minUser;
    uintptr_t userAddr;
    MemBlock *block;
    void *user;

    if (size == 0) {
        return NULL;
    }
    if (MemShouldFail()) {
        return NULL;
    }
    total = MemTotalBytes(size);
    if (total == 0) {
        return NULL;
    }
    raw = (unsigned char *)SysMalloc(total);
    if (!raw) {
        return NULL;
    }

    align = MemUserAlign();
    front = sizeof(MemBlock) + RoboMemGuardSize;
    minUser = (uintptr_t)(raw + front);
    userAddr = (minUser + (align - 1)) & ~(uintptr_t)(align - 1);
    block = (MemBlock *)(void *)((unsigned char *)userAddr - RoboMemGuardSize -
                                         sizeof(MemBlock));
    if ((unsigned char *)block < raw) {
        SysFree(raw);
        return NULL;
    }

    block->magic = RoboMemMagic;
    block->state = RoboMemStateLive;
    block->size = size;
    block->id = gRoboMemNextId++;
    block->file = file ? file : "?";
    block->line = line;
    block->sysBase = raw;
    block->next = NULL;
    block->prev = NULL;
    MemFillGuards(block);
    user = MemUserFromBlock(block);
    if ((uintptr_t)user != userAddr) {
        SysFree(raw);
        return NULL;
    }
    if (zero) {
        memset(user, 0, size);
    }
    MemLink(block);

    gRoboMemStats.liveCount++;
    gRoboMemStats.liveBytes += size;
    gRoboMemStats.totalAllocs++;
    if (gRoboMemStats.liveCount > gRoboMemStats.peakLiveCount) {
        gRoboMemStats.peakLiveCount = gRoboMemStats.liveCount;
    }
    if (gRoboMemStats.liveBytes > gRoboMemStats.peakLiveBytes) {
        gRoboMemStats.peakLiveBytes = gRoboMemStats.liveBytes;
    }
    return user;
}

static int MemIsTracked(MemBlock *block)
{
    return block && block->magic == RoboMemMagic;
}

/** Locate a user pointer via live/quarantine lists — never probe arbitrary addresses. */
static MemBlock *MemFindUser(void *user)
{
    MemBlock *b;
    if (!user) {
        return NULL;
    }
    for (b = gRoboMemLive; b; b = b->next) {
        if (MemUserFromBlock(b) == user) {
            return b;
        }
    }
    for (b = gRoboMemQuarantine; b; b = b->next) {
        if (MemUserFromBlock(b) == user) {
            return b;
        }
    }
    return NULL;
}

#endif /* RoboMemDebug */

void *MallocAt(size_t size, const char *file, int line)
{
#if RoboMemDebug
    if (gRoboMemDbgOn) {
        return MemTrackAlloc(size, file, line, 0);
    }
#else
    (void)file;
    (void)line;
#endif
    if (size == 0) {
        return NULL;
    }
    if (MemShouldFail()) {
        return NULL;
    }
    return SysMalloc(size);
}

void *CallocAt(size_t count, size_t size, const char *file, int line)
{
#if RoboMemDebug
    if (gRoboMemDbgOn) {
        size_t bytes;
        if (count == 0 || size == 0) {
            return NULL;
        }
        if (MemSizeOverflow(count, size)) {
            return NULL;
        }
        bytes = count * size;
        return MemTrackAlloc(bytes, file, line, 1);
    }
#else
    (void)file;
    (void)line;
#endif
    if (count == 0 || size == 0) {
        return NULL;
    }
    if (MemSizeOverflow(count, size)) {
        return NULL;
    }
    if (MemShouldFail()) {
        return NULL;
    }
    return SysCalloc(count, size);
}

void FreeAt(void *ptr, const char *file, int line)
{
#if RoboMemDebug
    MemBlock *block;
    char msg[256];

    (void)file;
    (void)line;
    if (!ptr) {
        return;
    }
    block = MemFindUser(ptr);
    if (!block) {
        if (gRoboMemDbgOn) {
            MemFault("invalid-free", "pointer is not a RoboMem debug allocation");
            return;
        }
        SysFree(ptr);
        return;
    }
    if (!MemIsTracked(block)) {
        MemFault("invalid-free", "allocation metadata is corrupted");
        return;
    }
    if (block->state == RoboMemStateFreed) {
        snprintf(msg, sizeof(msg), "double-free of allocation #%zu (%s:%d)",
                 block->id, block->file, block->line);
        MemFault("double-free", msg);
        return;
    }
    if (block->state != RoboMemStateLive) {
        MemFault("invalid-free", "allocation metadata is corrupted");
        return;
    }
    if (!MemGuardsOk(block)) {
        snprintf(msg, sizeof(msg), "guard corruption on allocation #%zu (%s:%d)",
                 block->id, block->file, block->line);
        MemFault("guard-corruption", msg);
        /* Reclaim even after a non-aborting handler so live lists stay consistent. */
        MemUnlink(block);
        if (gRoboMemStats.liveCount > 0) {
            gRoboMemStats.liveCount--;
        }
        if (gRoboMemStats.liveBytes >= block->size) {
            gRoboMemStats.liveBytes -= block->size;
        } else {
            gRoboMemStats.liveBytes = 0;
        }
        gRoboMemStats.totalFrees++;
        block->state = RoboMemStateFreed;
        MemQuarantinePush(block);
        return;
    }
    MemUnlink(block);
    if (gRoboMemStats.liveCount > 0) {
        gRoboMemStats.liveCount--;
    }
    if (gRoboMemStats.liveBytes >= block->size) {
        gRoboMemStats.liveBytes -= block->size;
    } else {
        gRoboMemStats.liveBytes = 0;
    }
    gRoboMemStats.totalFrees++;
    block->state = RoboMemStateFreed;
    memset(MemUserFromBlock(block), 0xDD, block->size);
    MemQuarantinePush(block);
#else
    (void)file;
    (void)line;
    SysFree(ptr);
#endif
}

void *ReallocAt(void *ptr, size_t size, const char *file, int line)
{
    if (size == 0) {
        FreeAt(ptr, file, line);
        return NULL;
    }
    if (!ptr) {
        return MallocAt(size, file, line);
    }

#if RoboMemDebug
    if (gRoboMemDbgOn) {
        MemBlock *block = MemFindUser(ptr);
        void *neu;
        size_t copy;
        if (!block || !MemIsTracked(block) ||
            block->state != RoboMemStateLive) {
            MemFault("invalid-realloc", "realloc of non-live debug block");
            return NULL;
        }
        if (!MemGuardsOk(block)) {
            MemFault("guard-corruption", "guard corruption before realloc");
            return NULL;
        }
        neu = MallocAt(size, file, line);
        if (!neu) {
            return NULL;
        }
        copy = block->size < size ? block->size : size;
        memcpy(neu, ptr, copy);
        FreeAt(ptr, file, line);
        return neu;
    }
#else
    (void)file;
    (void)line;
#endif
    if (MemShouldFail()) {
        return NULL;
    }
    return SysRealloc(ptr, size);
}

void *Malloc(size_t size)
{
    return MallocAt(size, "?", 0);
}

void *RoboCalloc(size_t count, size_t size)
{
    return CallocAt(count, size, "?", 0);
}

void *Realloc(void *ptr, size_t size)
{
    return ReallocAt(ptr, size, "?", 0);
}

void RoboFree(void *ptr)
{
    FreeAt(ptr, "?", 0);
}

void *RoboMemCopy(void *dst, const void *src, size_t n)
{
    if (n == 0) {
        return dst;
    }
    if (!dst || !src) {
        return NULL;
    }
    return memcpy(dst, src, n);
}

void *MemMove(void *dst, const void *src, size_t n)
{
    if (n == 0) {
        return dst;
    }
    if (!dst || !src) {
        return NULL;
    }
    return memmove(dst, src, n);
}

void *MemSet(void *dst, int value, size_t n)
{
    if (n == 0) {
        return dst;
    }
    if (!dst) {
        return NULL;
    }
    return memset(dst, value, n);
}

int MemCompare(const void *a, const void *b, size_t n)
{
    if (n == 0) {
        return 0;
    }
    if (!a || !b) {
        if (a == b) {
            return 0;
        }
        return a ? 1 : -1;
    }
    return memcmp(a, b, n);
}

void MemDebugEnable(void)
{
#if RoboMemDebug
    gRoboMemDbgOn = 1;
#endif
}

void MemDebugDisable(void)
{
#if RoboMemDebug
    gRoboMemDbgOn = 0;
#endif
}

int MemDebugIsEnabled(void)
{
#if RoboMemDebug
    return gRoboMemDbgOn;
#else
    return 0;
#endif
}

void MemDebugSetFaultHandler(MemFaultHandlerFn handler, void *userdata)
{
#if RoboMemDebug
    GRoboMemFault = handler;
    gRoboMemFaultUd = userdata;
#else
    (void)handler;
    (void)userdata;
#endif
}

void MemDebugGetStats(MemStats *out)
{
    if (!out) {
        return;
    }
#if RoboMemDebug
    *out = gRoboMemStats;
#else
    memset(out, 0, sizeof(*out));
#endif
}

void MemDebugResetStats(void)
{
#if RoboMemDebug
    memset(&gRoboMemStats, 0, sizeof(gRoboMemStats));
#endif
}

size_t MemDebugLiveCount(void)
{
#if RoboMemDebug
    return gRoboMemStats.liveCount;
#else
    return 0;
#endif
}

void MemDebugReport(void)
{
#if RoboMemDebug
    MemBlock *b;
    const char *base;
    const char *p;

    fprintf(stderr, "RoboMemDebug report\n");
    fprintf(stderr, "Active allocations: %zu\n", gRoboMemStats.liveCount);
    fprintf(stderr, "Total allocated bytes (live): %zu\n", gRoboMemStats.liveBytes);
    fprintf(stderr, "Peak live count: %zu\n", gRoboMemStats.peakLiveCount);
    fprintf(stderr, "Peak live bytes: %zu\n", gRoboMemStats.peakLiveBytes);
    fprintf(stderr, "Total allocs: %zu  Total frees: %zu\n",
            gRoboMemStats.totalAllocs, gRoboMemStats.totalFrees);
    for (b = gRoboMemLive; b; b = b->next) {
        base = b->file ? b->file : "?";
        for (p = base; *p; p++) {
            if (*p == '/' || *p == '\\') {
                base = p + 1;
            }
        }
        fprintf(stderr, "Allocation #%zu: %zu bytes at %s:%d\n",
                b->id, b->size, base, b->line);
    }
#else
    fprintf(stderr, "RoboMemDebug: not compiled in (ROBO_MEM_DEBUG=0)\n");
#endif
}

int MemDebugCheck(void *ptr)
{
#if RoboMemDebug
    MemBlock *block;
    if (!ptr) {
        for (block = gRoboMemLive; block; block = block->next) {
            if (!MemGuardsOk(block)) {
                return 1;
            }
        }
        return 0;
    }
    block = MemFindUser(ptr);
    if (!block || !MemIsTracked(block) ||
        block->state != RoboMemStateLive) {
        return 1;
    }
    return MemGuardsOk(block) ? 0 : 1;
#else
    (void)ptr;
    return 0;
#endif
}

void MemDebugCorruptGuardForTest(void *ptr, int back)
{
#if RoboMemDebug
    MemBlock *block;
    unsigned char *g;
    if (!ptr) {
        return;
    }
    block = MemFindUser(ptr);
    if (!block || !MemIsTracked(block) ||
        block->state != RoboMemStateLive) {
        return;
    }
    g = back ? MemBackGuard(block) : MemFrontGuard(block);
    g[0] = (unsigned char)(RoboMemGuardByte ^ 0xFFu);
#else
    (void)ptr;
    (void)back;
#endif
}
