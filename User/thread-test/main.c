#include <stdio.h>
#include <errno.h>
#include <robo/thread.h>

static int gN;
static Mutex gMu;

static void worker(void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < 500; i++) {
        MutexLock(&gMu);
        gN++;
        MutexUnlock(&gMu);
    }
}

int main(void)
{
    Thread a, b, c;
    MutexInit(&gMu);
    if (ThreadCreate(&a, worker, 0) != 0)
        return 1;
    if (ThreadCreate(&b, worker, 0) != 0)
        return 2;
    if (ThreadCreate(&c, worker, 0) != 0)
        return 3;
    ThreadJoin(a);
    ThreadJoin(b);
    ThreadJoin(c);
    if (gN != 1500)
        return 4;
    printf("[ OK ] thread-test %d\n", gN);
    return 0;
}
