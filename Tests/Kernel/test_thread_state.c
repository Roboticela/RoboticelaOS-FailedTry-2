#include "thread/thread_state.h"

#include <stdio.h>

static int gPass;
static int gFails;

#define CHECK(cond, msg) do { \
    if (cond) { gPass++; } \
    else { gFails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

int main(void)
{
    CHECK(ThreadTransitionAllowed(RoboThreadNew, RoboThreadReady), "NEW->READY");
    CHECK(!ThreadTransitionAllowed(RoboThreadNew, RoboThreadRunning), "NEW->RUNNING");
    CHECK(ThreadTransitionAllowed(RoboThreadReady, RoboThreadRunning), "READY->RUNNING");
    CHECK(!ThreadTransitionAllowed(RoboThreadReady, RoboThreadBlocked), "READY->BLOCKED");
    CHECK(ThreadTransitionAllowed(RoboThreadRunning, RoboThreadReady), "RUNNING->READY");
    CHECK(ThreadTransitionAllowed(RoboThreadRunning, RoboThreadBlocked), "RUNNING->BLOCKED");
    CHECK(ThreadTransitionAllowed(RoboThreadRunning, RoboThreadSleeping), "RUNNING->SLEEPING");
    CHECK(ThreadTransitionAllowed(RoboThreadRunning, RoboThreadTerminated), "RUNNING->TERM");
    CHECK(ThreadTransitionAllowed(RoboThreadBlocked, RoboThreadReady), "BLOCKED->READY");
    CHECK(!ThreadTransitionAllowed(RoboThreadBlocked, RoboThreadRunning), "BLOCKED->RUNNING");
    CHECK(ThreadTransitionAllowed(RoboThreadSleeping, RoboThreadReady), "SLEEPING->READY");
    CHECK(!ThreadTransitionAllowed(RoboThreadSleeping, RoboThreadRunning), "SLEEPING->RUNNING");
    CHECK(!ThreadTransitionAllowed(RoboThreadTerminated, RoboThreadReady), "TERM->READY");
    CHECK(!ThreadTransitionAllowed(RoboThreadTerminated, RoboThreadRunning), "TERM->RUNNING");
    CHECK(ThreadTransitionAllowed(RoboThreadReady, RoboThreadReady), "READY identity");
    CHECK(ThreadStateValid(RoboThreadNew), "valid NEW");
    CHECK(!ThreadStateValid((ThreadState)99), "invalid 99");

    printf("Thread state host tests  %d passed  %d failed\n", gPass, gFails);
    return gFails ? 1 : 0;
}
