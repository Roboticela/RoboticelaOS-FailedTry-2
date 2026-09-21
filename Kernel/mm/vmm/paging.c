#include "mm/vmm/paging.h"

uint16_t PageOffset(VirtAddr va)
{
    return (uint16_t)(va & RoboPageMask);
}

uint16_t PtIndex(VirtAddr va)
{
    return (uint16_t)((va >> 12) & RoboPteIndexMask);
}

uint16_t PdIndex(VirtAddr va)
{
    return (uint16_t)((va >> 21) & RoboPteIndexMask);
}

uint16_t PdptIndex(VirtAddr va)
{
    return (uint16_t)((va >> 30) & RoboPteIndexMask);
}

uint16_t Pml4Index(VirtAddr va)
{
    return (uint16_t)((va >> 39) & RoboPteIndexMask);
}

bool VirtualAddressIsCanonical(VirtAddr va)
{
    uint64_t hi = va >> 47;
    return hi == 0 || hi == 0x1FFFFull;
}

bool VirtAligned(VirtAddr va)
{
    return (va & RoboPageMask) == 0;
}

PhysAddr PtePhys(Pte e)
{
    return (PhysAddr)(e & RoboPteAddr);
}

Pte PteMake(PhysAddr phys, uint64_t hwFlags)
{
    return ((Pte)phys & RoboPteAddr) | (hwFlags & ~RoboPteAddr);
}

int VmFlagsValid(uint64_t vmFlags)
{
    if (!(vmFlags & (RoboVmRead | RoboVmWrite | RoboVmExecute)))
        return 0;
    if ((vmFlags & RoboVmUser) && (vmFlags & RoboVmKernel))
        return 0;
    return 1;
}

uint64_t VmToPteFlags(uint64_t vmFlags, int nxEnabled)
{
    uint64_t hw = RoboPteP;
    if (vmFlags & RoboVmWrite)
        hw |= RoboPteRw;
    if (vmFlags & RoboVmUser)
        hw |= RoboPteUs;
    if (vmFlags & RoboVmGlobal)
        hw |= RoboPteG;
    if (vmFlags & RoboVmNocache)
        hw |= RoboPtePcd;
    if (nxEnabled && !(vmFlags & RoboVmExecute))
        hw |= RoboPteNx;
    return hw;
}
