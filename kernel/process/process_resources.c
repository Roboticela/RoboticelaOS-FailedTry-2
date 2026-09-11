#include "process/process_resources.h"

#ifdef PROCESS_HOST_TEST
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
#else
#include "mm/heap/heap.h"
#endif

static void res_zero(void *dst, unsigned n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--)
        *p++ = 0;
}

RoboIPCResources *robo_ipc_create(void)
{
    RoboIPCResources *ipc;

    ipc = (RoboIPCResources *)kmalloc(sizeof(*ipc));
    if (!ipc)
        return 0;
    res_zero(ipc, (unsigned)sizeof(*ipc));
    return ipc;
}

void robo_ipc_destroy(RoboIPCResources *ipc)
{
    kfree(ipc);
}

RoboEnvironment *robo_env_create(void)
{
    RoboEnvironment *env;

    env = (RoboEnvironment *)kmalloc(sizeof(*env));
    if (!env)
        return 0;
    res_zero(env, (unsigned)sizeof(*env));
    return env;
}

void robo_env_destroy(RoboEnvironment *env)
{
    kfree(env);
}

RoboCredentials *robo_creds_create(void)
{
    RoboCredentials *creds;

    creds = (RoboCredentials *)kmalloc(sizeof(*creds));
    if (!creds)
        return 0;
    res_zero(creds, (unsigned)sizeof(*creds));
    creds->uid = 0;
    creds->gid = 0;
    creds->effective_uid = 0;
    creds->effective_gid = 0;
    creds->capabilities = 0;
    return creds;
}

void robo_creds_destroy(RoboCredentials *creds)
{
    kfree(creds);
}
