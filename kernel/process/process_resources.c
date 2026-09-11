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

RoboFileTable *robo_files_create(void)
{
    RoboFileTable *files;
    unsigned i;

    files = (RoboFileTable *)kmalloc(sizeof(*files));
    if (!files)
        return 0;
    res_zero(files, (unsigned)sizeof(*files));
    /* stdin / stdout / stderr placeholders — no VFS yet */
    for (i = 0; i < 3u; i++) {
        files->in_use[i] = 1;
        files->slot[i] = 0;
    }
    files->open_count = 3;
    return files;
}

void robo_files_destroy(RoboFileTable *files)
{
    kfree(files);
}

int robo_files_alloc(RoboFileTable *files, void *file)
{
    unsigned i;

    if (!files)
        return -1;
    for (i = 0; i < ROBO_FD_MAX; i++) {
        if (!files->in_use[i]) {
            files->in_use[i] = 1;
            files->slot[i] = file;
            files->open_count++;
            return (int)i;
        }
    }
    return -1;
}

int robo_files_close(RoboFileTable *files, int fd)
{
    if (!files || fd < 0 || (unsigned)fd >= ROBO_FD_MAX)
        return 0;
    if (!files->in_use[fd])
        return 0;
    files->in_use[fd] = 0;
    files->slot[fd] = 0;
    if (files->open_count)
        files->open_count--;
    return 1;
}

void *robo_files_get(RoboFileTable *files, int fd)
{
    if (!files || fd < 0 || (unsigned)fd >= ROBO_FD_MAX)
        return 0;
    if (!files->in_use[fd])
        return 0;
    return files->slot[fd];
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
    /* Kernel / early processes: uid 0 / gid 0 */
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
