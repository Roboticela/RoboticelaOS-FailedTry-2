#include "process/process_state.h"

#include <stdio.h>

static int g_pass;
static int g_fails;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

int main(void)
{
    CHECK(robo_process_transition_allowed(ROBO_PROCESS_NEW, ROBO_PROCESS_RUNNING),
          "NEW->RUNNING");
    CHECK(robo_process_transition_allowed(ROBO_PROCESS_NEW, ROBO_PROCESS_TERMINATED),
          "NEW->TERM");
    CHECK(!robo_process_transition_allowed(ROBO_PROCESS_NEW, ROBO_PROCESS_BLOCKED),
          "NEW->BLOCKED");
    CHECK(robo_process_transition_allowed(ROBO_PROCESS_RUNNING, ROBO_PROCESS_SLEEPING),
          "RUNNING->SLEEPING");
    CHECK(robo_process_transition_allowed(ROBO_PROCESS_RUNNING, ROBO_PROCESS_BLOCKED),
          "RUNNING->BLOCKED");
    CHECK(robo_process_transition_allowed(ROBO_PROCESS_RUNNING, ROBO_PROCESS_STOPPED),
          "RUNNING->STOPPED");
    CHECK(robo_process_transition_allowed(ROBO_PROCESS_RUNNING, ROBO_PROCESS_TERMINATED),
          "RUNNING->TERM");
    CHECK(robo_process_transition_allowed(ROBO_PROCESS_SLEEPING, ROBO_PROCESS_RUNNING),
          "SLEEPING->RUNNING");
    CHECK(robo_process_transition_allowed(ROBO_PROCESS_BLOCKED, ROBO_PROCESS_RUNNING),
          "BLOCKED->RUNNING");
    CHECK(robo_process_transition_allowed(ROBO_PROCESS_STOPPED, ROBO_PROCESS_RUNNING),
          "STOPPED->RUNNING");
    CHECK(!robo_process_transition_allowed(ROBO_PROCESS_TERMINATED, ROBO_PROCESS_RUNNING),
          "TERM->RUNNING");
    CHECK(!robo_process_transition_allowed(ROBO_PROCESS_SLEEPING, ROBO_PROCESS_BLOCKED),
          "SLEEPING->BLOCKED");
    CHECK(robo_process_state_valid(ROBO_PROCESS_NEW), "valid NEW");
    CHECK(!robo_process_state_valid((robo_process_state_t)99), "invalid 99");
    CHECK(robo_process_transition_allowed(ROBO_PROCESS_RUNNING, ROBO_PROCESS_RUNNING),
          "RUNNING identity");

    printf("Process state host tests  %d passed  %d failed\n", g_pass, g_fails);
    return g_fails ? 1 : 0;
}
