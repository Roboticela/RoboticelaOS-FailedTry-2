#ifndef ROBO_EDID_H
#define ROBO_EDID_H

#include <stdint.h>
#include "roboticela/bootinfo.h"

#define RoboEdidNameMax 32u

typedef struct RoboEdidInfo {
    int present;
    uint32_t width;
    uint32_t height;
    uint32_t refreshHz;
    char name[RoboEdidNameMax];
    uint8_t manufacturer[4];
} RoboEdidInfo;

int EdidInit(const RoboBootInfo *info);
int EdidReady(void);
int EdidGet(RoboEdidInfo *out);
int EdidPreferredMode(uint32_t *width, uint32_t *height);

#endif
