#include "mm/vmm/paging.h"

#include <stdio.h>
#include <string.h>

static int gPass;
static int gFails;

#define CHECK(cond, msg) do { \
    if (cond) { gPass++; } \
    else { gFails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } \
} while (0)

int main(void)
{
    CHECK(Pml4Index(0) == 0, "pml4 0");
    CHECK(PdptIndex(0) == 0, "pdpt 0");
    CHECK(PdIndex(0) == 0, "pd 0");
    CHECK(PtIndex(0) == 0, "pt 0");
    CHECK(PageOffset(0x1234) == 0x234, "offset");
    CHECK(PtIndex(0x12345678ull) == 0x145, "pt sample");
    CHECK(PdIndex(0x12345678ull) == 0x91, "pd sample");
    CHECK(Pml4Index(RoboVmmTestBase) == 256, "test pml4");
    CHECK(VirtualAddressIsCanonical(0), "canon 0");
    CHECK(VirtualAddressIsCanonical(RoboVmmTestBase), "canon hh");
    CHECK(!VirtualAddressIsCanonical(0x0000800000000000ull), "noncanon");
    CHECK(VirtAligned(0x1000), "aligned");
    CHECK(!VirtAligned(1), "unaligned");
    CHECK(VmFlagsValid(RoboVmRead | RoboVmKernel), "flags rw");
    CHECK(!VmFlagsValid(RoboVmUser | RoboVmKernel), "user+kernel");
    CHECK(!VmFlagsValid(0), "empty flags");
    {
        uint64_t hw = VmToPteFlags(RoboVmRead | RoboVmWrite, 1);
        CHECK((hw & RoboPteP) && (hw & RoboPteRw) && (hw & RoboPteNx), "rw nx");
        hw = VmToPteFlags(RoboVmRead | RoboVmExecute, 1);
        CHECK((hw & RoboPteP) && !(hw & RoboPteRw) && !(hw & RoboPteNx), "rx");
        hw = VmToPteFlags(RoboVmRead | RoboVmUser, 1);
        CHECK((hw & RoboPteUs) && !(hw & RoboPteRw), "user ro");
    }
    CHECK((PteMake(0x401000, RoboPteP) & RoboPteAddr) == 0x401000, "pte addr");
    printf("VMM host tests  %d passed  %d failed\n", gPass, gFails);
    return gFails ? 1 : 0;
}
