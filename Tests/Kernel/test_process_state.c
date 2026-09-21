#include "process/process_state.h"

#include <stdio.h>

static int gPass;
static int gFails;

#define CHECK(cond, msg) do { \
    if (cond) { gPass++; } \
    else { gFails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

int main(void)
{
    CHECK(ProcessTransitionAllowed(RoboProcessNew, RoboProcessRunning),
          "NEW->RUNNING");
    CHECK(ProcessTransitionAllowed(RoboProcessNew, RoboProcessTerminated),
          "NEW->TERM");
    CHECK(!ProcessTransitionAllowed(RoboProcessNew, RoboProcessBlocked),
          "NEW->BLOCKED");
    CHECK(ProcessTransitionAllowed(RoboProcessRunning, RoboProcessSleeping),
          "RUNNING->SLEEPING");
    CHECK(ProcessTransitionAllowed(RoboProcessRunning, RoboProcessBlocked),
          "RUNNING->BLOCKED");
    CHECK(ProcessTransitionAllowed(RoboProcessRunning, RoboProcessStopped),
          "RUNNING->STOPPED");
    CHECK(ProcessTransitionAllowed(RoboProcessRunning, RoboProcessTerminated),
          "RUNNING->TERM");
    CHECK(ProcessTransitionAllowed(RoboProcessSleeping, RoboProcessRunning),
          "SLEEPING->RUNNING");
    CHECK(ProcessTransitionAllowed(RoboProcessBlocked, RoboProcessRunning),
          "BLOCKED->RUNNING");
    CHECK(ProcessTransitionAllowed(RoboProcessStopped, RoboProcessRunning),
          "STOPPED->RUNNING");
    CHECK(!ProcessTransitionAllowed(RoboProcessTerminated, RoboProcessRunning),
          "TERM->RUNNING");
    CHECK(!ProcessTransitionAllowed(RoboProcessSleeping, RoboProcessBlocked),
          "SLEEPING->BLOCKED");
    CHECK(ProcessStateValid(RoboProcessNew), "valid NEW");
    CHECK(!ProcessStateValid((ProcessState)99), "invalid 99");
    CHECK(ProcessTransitionAllowed(RoboProcessRunning, RoboProcessRunning),
          "RUNNING identity");

    printf("Process state host tests  %d passed  %d failed\n", gPass, gFails);
    return gFails ? 1 : 0;
}
