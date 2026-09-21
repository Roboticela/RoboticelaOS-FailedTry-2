#include "process/process_resources.h"

#ifdef PROCESS_HOST_TEST
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
#else
#include "mm/heap/heap.h"
#endif

static void ResZero(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

RoboIPCResources *IpcCreate(void)
{
    RoboIPCResources *ipc;

    ipc = (RoboIPCResources *)kmalloc(sizeof(*ipc));
    if (!ipc)
        return 0;
    ResZero(ipc, (unsigned)sizeof(*ipc));
    return ipc;
}

void IpcDestroy(RoboIPCResources *ipc)
{
    kfree(ipc);
}

RoboEnvironment *EnvCreate(void)
{
    RoboEnvironment *env;

    env = (RoboEnvironment *)kmalloc(sizeof(*env));
    if (!env)
        return 0;
    ResZero(env, (unsigned)sizeof(*env));
    return env;
}

void EnvDestroy(RoboEnvironment *env)
{
    kfree(env);
}

RoboCredentials *CredsCreate(void)
{
    RoboCredentials *creds;

    creds = (RoboCredentials *)kmalloc(sizeof(*creds));
    if (!creds)
        return 0;
    ResZero(creds, (unsigned)sizeof(*creds));
    creds->uid = 0;
    creds->gid = 0;
    creds->effectiveUid = 0;
    creds->effectiveGid = 0;
    creds->capabilities = 0;
    return creds;
}

void CredsDestroy(RoboCredentials *creds)
{
    kfree(creds);
}
