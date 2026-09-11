#include "arch/x86_64/context/context.h"

#include <stdio.h>

static int g_pass;
static int g_fails;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

int main(void)
{
    CHECK(sizeof(struct robo_iframe) == ROBO_IFRAME_SIZE, "iframe size");
    CHECK(ROBO_IFRAME_OFF_RIP == 136u, "RIP offset");
    CHECK(ROBO_IFRAME_OFF_CS == 144u, "CS offset");
    CHECK(ROBO_IFRAME_OFF_RFLAGS == 152u, "RFLAGS offset");
    CHECK(ROBO_IFRAME_OFF_RSP == 160u, "RSP offset");
    CHECK(ROBO_IFRAME_OFF_SS == 168u, "SS offset");
    CHECK(ROBO_IFRAME_SIZE == 176u, "size constant");
    CHECK(ROBO_GDT_KERNEL_CODE == 0x08u, "kernel CS");
    CHECK(ROBO_GDT_KERNEL_DATA == 0x10u, "kernel SS");

    printf("Context layout host tests  %d passed  %d failed\n", g_pass, g_fails);
    return g_fails ? 1 : 0;
}
