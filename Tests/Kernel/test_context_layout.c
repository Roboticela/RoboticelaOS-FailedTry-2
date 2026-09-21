#include "arch/x86_64/context/context.h"

#include <stdio.h>

static int gPass;
static int gFails;

#define CHECK(cond, msg) do { \
    if (cond) { gPass++; } \
    else { gFails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

int main(void)
{
    CHECK(sizeof(struct Iframe) == RoboIframeSize, "iframe size");
    CHECK(RoboIframeOffRip == 136u, "RIP offset");
    CHECK(RoboIframeOffCs == 144u, "CS offset");
    CHECK(RoboIframeOffRflags == 152u, "RFLAGS offset");
    CHECK(RoboIframeOffRsp == 160u, "RSP offset");
    CHECK(RoboIframeOffSs == 168u, "SS offset");
    CHECK(RoboIframeSize == 176u, "size constant");
    CHECK(RoboGdtKernelCode == 0x08u, "kernel CS");
    CHECK(RoboGdtKernelData == 0x10u, "kernel SS");

    printf("Context layout host tests  %d passed  %d failed\n", gPass, gFails);
    return gFails ? 1 : 0;
}
