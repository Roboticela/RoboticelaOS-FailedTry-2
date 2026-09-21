#ifndef ROBO_PROCESS_H
#define ROBO_PROCESS_H

#include <stdint.h>
#include "process/process_state.h"
#include "process/process_resources.h"
#include "fs/fd.h"
#include "thread/thread.h"

#define RoboProcessIdKernel 0ull
#define RoboProcessIdInit   1ull
#define RoboProcessNameMax  32
#define RoboProcessExitFault 255

typedef uint64_t ProcessId;

typedef struct RoboProcess {
    ProcessId id;
    char name[RoboProcessNameMax];
    ProcessState state;

    RoboAddressSpace *addressSpace;
    int ownsAspace;

    RoboThread *threads;
    uint64_t threadCount;

    RoboFDTable *fdTable;
    RoboIPCResources *ipc;
    RoboEnvironment *environment;
    RoboCredentials *credentials;

    struct RoboProcess *parent;
    struct RoboProcess *firstChild;
    struct RoboProcess *siblingNext;

    uint64_t createdAt;
    uint64_t exitTime;
    int exitStatus;

    RoboSpinlock lock;
    struct RoboProcess *next;

    uint64_t userEntry;
    uint64_t userStackTop;
    uint64_t userStackBase;
    uint64_t mmapNext;

    struct RoboThread *waiter;
    uint64_t waitGen;
} RoboProcess;

int ProcessSubsystemInit(void);
RoboProcess *KernelProcess(void);

RoboProcess *ProcessCreate(void);
RoboProcess *ProcessCreateNamed(const char *name);
RoboProcess *ProcessCreateInit(void);
RoboProcess *InitProcess(void);
void ProcessSetInit(RoboProcess *process);
int ProcessWait(ProcessId pid, uint32_t flags, int *status,
                      ProcessId *outPid);
int ProcessDestroy(RoboProcess *process);
void ProcessExit(int status);
void InitOnTerminated(RoboProcess *process);

RoboProcess *CurrentProcess(void);
ProcessId GetProcessId(const RoboProcess *process);
RoboProcess *ProcessLookup(ProcessId pid);

int ProcessAddThread(RoboProcess *process, RoboThread *thread);
int ProcessRemoveThread(RoboProcess *process, RoboThread *thread);

int ProcessFdAlloc(RoboProcess *process, RoboFile *file);
int ProcessFdClose(RoboProcess *process, int fd);
RoboFile *ProcessFdGet(RoboProcess *process, int fd);
int ProcessFdInUse(RoboProcess *process, int fd);

void ProcessSetSwitchLog(int on);
void ArchSwitchAddressSpace(RoboAddressSpace *addressSpace);

uint64_t ProcessStatCreated(void);
uint64_t ProcessStatDestroyed(void);
uint64_t ProcessStatLive(void);
uint64_t ProcessStatLiveMax(void);
uint64_t ProcessStatAspaceCreated(void);
uint64_t ProcessStatAspaceDestroyed(void);
uint64_t ProcessStatFiles(void);
uint64_t ProcessStatIpc(void);
uint64_t ProcessStatEnv(void);
uint64_t ProcessStatCreds(void);
uint64_t AspaceStatSame(void);
uint64_t AspaceStatCross(void);
uint64_t ArchCr3Reloads(void);
void ArchPrepareThreadSwitch(RoboThread *old, RoboThread *next);

#ifdef PROCESS_HOST_TEST
void HostSetCurrentThread(RoboThread *t);
#endif

void ProcessDebugDump(const RoboProcess *process);
void ProcessReport(void);
#ifndef PROCESS_HOST_TEST
void ProcessFbReport(void);
#endif
int ProcessSelftest(void);

void ProcessPanicRelation(const RoboThread *thread, const char *reason);
void ProcessPanicAspace(const RoboProcess *process, const RoboThread *thread,
                               PhysAddr expected, PhysAddr actual);

#endif
