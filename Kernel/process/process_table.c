#include "process/process_table.h"

#ifdef PROCESS_HOST_TEST
static uint64_t SchedLock(void) { return 0; }
static void SchedUnlock(uint64_t flags) { (void)flags; }
#else
#include "scheduler/sched_internal.h"
#endif

/*
 * Process table lock: SchedLock (CLI on the bootstrap CPU).
 * Protects gHead, gCount, and gNextId. Process thread lists use
 * the same lock in process.c so SMP later can split them cleanly.
 */

static RoboProcess *gHead;
static uint64_t gCount;
static ProcessId gNextId = 2;

void PtableInit(void)
{
    gHead = 0;
    gCount = 0;
    gNextId = 2;
}

ProcessId PtableAllocId(void)
{
    uint64_t flags;
    ProcessId id;
    RoboProcess *p;
    int collision;

    flags = SchedLock();
    for (;;) {
        id = gNextId++;
        if (id == 0 || id == RoboProcessIdInit) {
            gNextId = 2;
            continue;
        }
        collision = 0;
        for (p = gHead; p; p = p->next) {
            if (p->id == id) {
                collision = 1;
                break;
            }
        }
        if (!collision) {
            SchedUnlock(flags);
            return id;
        }
        if (gNextId == 0 || gNextId == RoboProcessIdInit)
            gNextId = 2;
    }
}

int PtableInsert(RoboProcess *process)
{
    uint64_t flags;
    RoboProcess *p;

    if (!process)
        return 0;

    flags = SchedLock();
    for (p = gHead; p; p = p->next) {
        if (p == process || p->id == process->id) {
            SchedUnlock(flags);
            return 0;
        }
    }
    process->next = gHead;
    gHead = process;
    gCount++;
    SchedUnlock(flags);
    return 1;
}

void PtableRemove(RoboProcess *process)
{
    uint64_t flags;
    RoboProcess **slot;

    if (!process)
        return;
    flags = SchedLock();
    for (slot = &gHead; *slot; slot = &(*slot)->next) {
        if (*slot == process) {
            *slot = process->next;
            process->next = 0;
            if (gCount)
                gCount--;
            break;
        }
    }
    SchedUnlock(flags);
}

RoboProcess *PtableLookup(ProcessId pid)
{
    uint64_t flags;
    RoboProcess *p;

    flags = SchedLock();
    for (p = gHead; p; p = p->next) {
        if (p->id == pid) {
            SchedUnlock(flags);
            return p;
        }
    }
    SchedUnlock(flags);
    return 0;
}

uint64_t PtableCount(void)
{
    return gCount;
}

RoboProcess *PtableHead(void)
{
    return gHead;
}
