#ifndef ROBOTICELA_BOOTINFO_H
#define ROBOTICELA_BOOTINFO_H

#include <stdint.h>

#define ROBO_BOOTINFO_MAGIC    0x4F424F52u /* 'ROBO' */
#define ROBO_BOOTINFO_VERSION  2u /* v2: bootinfo_address / bootinfo_size */

#define ROBO_BOOT_DEBUG        1u

#define ROBO_MEM_AVAILABLE     1u
#define ROBO_MEM_RESERVED      2u
#define ROBO_MEM_ACPI          3u
#define ROBO_MEM_NVS           4u
#define ROBO_MEM_MMIO          5u
#define ROBO_MEM_LOADER        6u

#define ROBO_PIX_BGRX          0u
#define ROBO_PIX_RGBX          1u

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

    uint64_t memory_map;
    uint64_t memory_map_count;

    uint64_t framebuffer_address;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
    uint32_t framebuffer_pixfmt;

    uint64_t rsdp_address;

    uint64_t kernel_base;
    uint64_t kernel_size;

    uint64_t initrd_address;
    uint64_t initrd_size;

    /* Physical range of this RoboBootInfo block (includes the memory map). */
    uint64_t bootinfo_address;
    uint64_t bootinfo_size;

    char cmdline[256];

    uint32_t debug_term_x;
    uint32_t debug_term_y;
    uint32_t debug_term_w;
    uint32_t debug_term_h;
} RoboBootInfo;

#endif
