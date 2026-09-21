#ifndef ROBO_THREAD_H
#define ROBO_THREAD_H

#include <stddef.h>
#include <stdint.h>
#include "thread/thread_state.h"
#include "thread/thread_context.h"
#include "mm/page.h"
#include "mm/vmm/aspace.h"

/*
 * RoboThread — the authoritative schedulable execution object (Phase 9).
 *
 * The Phase 8 scheduler operates on RoboThread. There is no second
 * independent task object. `Task` is a compatibility alias.
 *
 * Identity
 * --------
 *   0  bootstrap (kmain), never reused while alive
 *   1  idle, never reused while alive
 *   2+ monotonic; unique among live threads including unreaped zombies.
 *      After reap, an ID may be reused.
 *
 * Stacks
 * ------
 *   kernelStack / kernelStackSize
 *       Dedicated kernel stack (PMM + VMM, guard page below, NX if available).
 *   stack / stackSize
 *       Current execution stack. For kernel threads this matches the kernel
 *       stack. Future user threads will set `stack` to the user stack.
 *
 *   The thread object owns its kernel stack. Never free that stack while
 *   the CPU is still executing on it; reap after the switch.
 *
 * Address space
 * -------------
 *   Each thread caches the owning process address space. Threads of
 *   one process share that object. CR3 is reloaded only when the next
 *   thread belongs to a different process.
 *
 * Instruction pointer
 * -------------------
 *   ThreadIp() reads context->rip. Do not store a second RIP.
 */

typedef uint64_t ThreadId;
typedef void (*ThreadEntry)(void *argument);

#define RoboThreadIdBootstrap 0ull
#define RoboThreadIdIdle      1ull
#define RoboThreadNameMax     32
#define RoboThreadKstackSize  (16u * 1024u)
#define RoboThreadKstackPages 4u
#define RoboThreadKstackGuardPages 1u

struct RoboProcess;

typedef struct RoboThread {
    ThreadId id;
    char name[RoboThreadNameMax];
    ThreadState state;

    uint64_t stack;
    uint64_t stackSize;
    uint64_t kernelStack;
    uint64_t kernelStackSize;
    uint64_t StackTop;
    uint64_t guardPage;
    int ownsStack;

    struct RoboProcess *process;
    RoboAddressSpace *addressSpace;
    ThreadContext *context;

    ThreadEntry entry;
    void *argument;

    uint64_t createdAt;
    uint64_t lastStartTime;
    uint64_t runtime;
    uint64_t sliceEndNs;
    uint64_t wakeDeadline;
    int sleepTimer;
    int kind; /* RoboThreadKernel or RoboThreadUser */

    struct RoboThread *next;
    struct RoboThread *prev;
    struct RoboThread *allNext;
    struct RoboThread *reapNext;
    struct RoboThread *procNext;
    struct RoboThread *procPrev;
    uint64_t userFsBase;
    struct RoboThread *joinWaiter;
    int joinPosted;
    int joinConsumed;
    int userExitStatus;
    int reapQueued;
} RoboThread;

RoboAddressSpace *KernelAddressSpace(void);

int ThreadSubsystemInit(void);
RoboThread *ThreadAdoptBootstrap(void *stackBase, uint64_t StackTop,
                                        size_t stackSize);

RoboThread *ThreadCreate(const char *name,
                               ThreadEntry entry,
                               void *argument);
RoboThread *ThreadCreateInProcess(struct RoboProcess *process,
                                          const char *name,
                                          ThreadEntry entry,
                                          void *argument);
RoboThread *ThreadCreateUser(struct RoboProcess *process,
                                    const char *name,
                                    uint64_t userRip, uint64_t userRsp,
                                    uint64_t userRdi);
void ThreadKillCurrent(void);
struct RoboProcess *ThreadProcess(const RoboThread *thread);
void ThreadExit(void);
void ThreadYield(void);
void ThreadBlock(void);
int ThreadWake(RoboThread *thread);
int ThreadSleepUntil(uint64_t deadlineNs);

RoboThread *CurrentThread(void);
ThreadId GetThreadId(const RoboThread *thread);
ThreadState MakeThreadState(const RoboThread *thread);
uint64_t ThreadIp(const RoboThread *thread);
uint64_t ThreadSp(const RoboThread *thread);

int ThreadTransition(RoboThread *thread, ThreadState next);
int ThreadIsValid(const RoboThread *thread);

void ThreadSetName(RoboThread *thread, const char *name);
void ThreadZero(RoboThread *thread);
int ThreadStackAlloc(RoboThread *thread);
void ThreadStackSetupContext(RoboThread *thread);
void ThreadStackFree(RoboThread *thread);
void ThreadBootstrap(void);

void ThreadDump(const RoboThread *thread);
void ThreadReport(void);
#ifndef THREAD_HOST_TEST
void ThreadFbReport(void);
#endif
int ThreadSelftest(void);

RoboThread *ThreadAll(void);
RoboThread *ThreadById(uint64_t id);

#endif
