#ifndef ROBO_BATTERY_H
#define ROBO_BATTERY_H

#include <stdint.h>

#define RoboBatteryUnavailable 0u
#define RoboBatteryDischarging 1u
#define RoboBatteryCharging    2u
#define RoboBatteryFull        3u
#define RoboBatteryCritical    4u

typedef struct RoboBatteryStatus {
    uint32_t state;
    uint32_t percent;      /* 0–100, or 0xFFFFFFFF if unknown */
    uint32_t voltageMv;    /* millivolts, 0 if unknown */
    uint32_t rateMw;       /* milliwatts, 0 if unknown */
    uint32_t capacityMah;  /* 0 if unknown */
    uint32_t remainingMah;
} RoboBatteryStatus;

int BatteryInit(void);
int BatteryPresent(void);
int BatteryGetStatus(RoboBatteryStatus *out);

#endif
