/*
 * RoboWindow configuration (Goal 7.1) — requested properties at create time.
 *
 * Configuration is what the caller asks for. Runtime state (RoboWindowState)
 * is what the system currently reports. Backends may not honor every field.
 */

#ifndef ROBO_WINDOW_CONFIG_H
#define ROBO_WINDOW_CONFIG_H

#include "RoboBase/RoboError.h"
#include "RoboWindow/RoboWindowState.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WindowConfig {
    int32_t width;  /* required > 0 */
    int32_t height; /* required > 0 */
    int32_t x; /* RoboWindowCoordUnknown → OS default placement */
    int32_t y;
    int32_t minWidth;  /* 0 = none */
    int32_t minHeight; /* 0 = none */
    int32_t maxWidth;  /* 0 = none */
    int32_t maxHeight; /* 0 = none */
    const char *title;  /* borrowed UTF-8; NULL → empty; copied on create */
    int visible;        /* non-zero → show after create when backend allows */
    int resizable;      /* non-zero → resizable preference */
    int fullscreen;     /* non-zero → fullscreen preference (may be deferred) */
    DisplayId displayId; /* NONE → default/primary */
    float scalePreference; /* 0 → unspecified; else validated scale (Goal 7.8) */
} WindowConfig;

/** Fill defaults: 800×600, visible, resizable, empty title, no min/max. */
void WindowConfigInit(WindowConfig *cfg);

/**
 * Validate dimensions and min/max consistency.
 * Does not require a live backend.
 */
Result WindowConfigValidate(const WindowConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_CONFIG_H */
