#include <stdio.h>
#include <errno.h>
#include <robo/thread.h>

static int gN;
static robo_mutex_t gMu;

static void worker(void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < 500; i++) {
        robo_mutex_lock(&gMu);
        gN++;
        robo_mutex_unlock(&gMu);
    }
}

int main(void)
{
    robo_thread_t a, b, c;
    robo_mutex_init(&gMu);
    if (robo_thread_create(&a, worker, 0) != 0)
        return 1;
    if (robo_thread_create(&b, worker, 0) != 0)
        return 2;
    if (robo_thread_create(&c, worker, 0) != 0)
        return 3;
    robo_thread_join(a);
    robo_thread_join(b);
    robo_thread_join(c);
    if (gN != 1500)
        return 4;
    printf("[ OK ] thread-test %d\n", gN);
    return 0;
}
