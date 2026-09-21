#ifndef ROBOTICELA_BOOTINFO_H
#define ROBOTICELA_BOOTINFO_H

#include <stdint.h>

#define RoboBootinfoMagic    0x4F424F52u /* 'ROBO' */
#define RoboBootinfoVersion  4u /* v4: splash_bar_* / splashProgress */
#define RoboDebugLogMagic   0x474F4C52u /* 'RLOG' */
#define RoboDebugLogVersion 1u

#define RoboBootProgressMax     10000u
#define RoboBootProgressHandoff 4000u

#define RoboBootDebug        1u

#define RoboMemAvailable     1u
#define RoboMemReserved      2u
#define RoboMemAcpi          3u
#define RoboMemNvs           4u
#define RoboMemMmio          5u
#define RoboMemLoader        6u

#define RoboPixBgrx          0u
#define RoboPixRgbx          1u

typedef struct {
    uint64_t address;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} RoboMemoryRegion;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t reserved0;

    uint64_t memoryMap;
    uint64_t memoryMapCount;

    uint64_t framebufferAddress;
    uint32_t framebufferWidth;
    uint32_t framebufferHeight;
    uint32_t framebufferPitch;
    uint32_t framebufferBpp;
    uint32_t framebufferPixfmt;

    uint64_t rsdpAddress;

    uint64_t kernelBase;
    uint64_t kernelSize;

    uint64_t initrdAddress;
    uint64_t initrdSize;

    /* Physical range of this RoboBootInfo block (includes the memory map). */
    uint64_t bootinfoAddress;
    uint64_t bootinfoSize;

    char cmdline[256];

    uint32_t debugTermX;
    uint32_t debugTermY;
    uint32_t debugTermW;
    uint32_t debugTermH;

    /* Packed RoboDebugLogHeader + records. Valid when version >= 3. */
    uint64_t debugLogAddress;
    uint32_t debugLogSize;
    uint32_t debugLogCount;

    /* Splash progress bar. Valid when version >= 4. Units: 0..10000. */
    uint32_t splashBarX;
    uint32_t splashBarY;
    uint32_t splashBarW;
    uint32_t splashBarH;
    uint32_t splashProgress;
    uint32_t splashHandoff;
} RoboBootInfo;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t used;
} RoboDebugLogHeader;

#endif
