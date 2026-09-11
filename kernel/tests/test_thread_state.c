#include "thread/thread_state.h"

#include <stdio.h>

static int g_pass;
static int g_fails;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

int main(void)
{
    CHECK(robo_thread_transition_allowed(ROBO_THREAD_NEW, ROBO_THREAD_READY), "NEW->READY");
    CHECK(!robo_thread_transition_allowed(ROBO_THREAD_NEW, ROBO_THREAD_RUNNING), "NEW->RUNNING");
    CHECK(robo_thread_transition_allowed(ROBO_THREAD_READY, ROBO_THREAD_RUNNING), "READY->RUNNING");
    CHECK(!robo_thread_transition_allowed(ROBO_THREAD_READY, ROBO_THREAD_BLOCKED), "READY->BLOCKED");
    CHECK(robo_thread_transition_allowed(ROBO_THREAD_RUNNING, ROBO_THREAD_READY), "RUNNING->READY");
    CHECK(robo_thread_transition_allowed(ROBO_THREAD_RUNNING, ROBO_THREAD_BLOCKED), "RUNNING->BLOCKED");
    CHECK(robo_thread_transition_allowed(ROBO_THREAD_RUNNING, ROBO_THREAD_SLEEPING), "RUNNING->SLEEPING");
    CHECK(robo_thread_transition_allowed(ROBO_THREAD_RUNNING, ROBO_THREAD_TERMINATED), "RUNNING->TERM");
    CHECK(robo_thread_transition_allowed(ROBO_THREAD_BLOCKED, ROBO_THREAD_READY), "BLOCKED->READY");
    CHECK(!robo_thread_transition_allowed(ROBO_THREAD_BLOCKED, ROBO_THREAD_RUNNING), "BLOCKED->RUNNING");
    CHECK(robo_thread_transition_allowed(ROBO_THREAD_SLEEPING, ROBO_THREAD_READY), "SLEEPING->READY");
    CHECK(!robo_thread_transition_allowed(ROBO_THREAD_SLEEPING, ROBO_THREAD_RUNNING), "SLEEPING->RUNNING");
    CHECK(!robo_thread_transition_allowed(ROBO_THREAD_TERMINATED, ROBO_THREAD_READY), "TERM->READY");
    CHECK(!robo_thread_transition_allowed(ROBO_THREAD_TERMINATED, ROBO_THREAD_RUNNING), "TERM->RUNNING");
    CHECK(robo_thread_transition_allowed(ROBO_THREAD_READY, ROBO_THREAD_READY), "READY identity");
    CHECK(robo_thread_state_valid(ROBO_THREAD_NEW), "valid NEW");
    CHECK(!robo_thread_state_valid((robo_thread_state_t)99), "invalid 99");

    printf("Thread state host tests  %d passed  %d failed\n", g_pass, g_fails);
    return g_fails ? 1 : 0;
}
