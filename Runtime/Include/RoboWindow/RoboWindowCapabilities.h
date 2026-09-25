/*
 * RoboWindow backend capabilities (Goals 7.2 / 7.4).
 *
 * Explicit flags — do not infer support from null function pointers alone.
 * Null architecture backend reports in-process lifecycle support without
 * native OS presentation or focus.
 */

#ifndef ROBO_WINDOW_CAPABILITIES_H
#define ROBO_WINDOW_CAPABILITIES_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WindowBackendCaps {
    int RoboCreateWindow;
    int RoboDestroyWindow;
    int showHide;
    int resize;
    int move;
    int SetTitle;
    int focus;
    int QueryState;
    int present;
    int ProcessEvents;
    /**
     * Non-zero: backend may overwrite actual geometry/visibility via
     * QueryState; core must not assume request == actual.
     */
    int reportsActualState;
    /** Non-zero: backend honors runtime SetSizeConstraints. */
    int sizeConstraints;

    /**
     * Goal 7.4: bitmask of presentable RoboGraphics formats
     * (ROBO_WINDOW_PRESENT_FMT_*). Meaningful when present != 0.
     * Zero with present set → no formats (all presents fail as unsupported).
     */
    uint32_t presentFormatMask;

    /**
     * Optional present size ceiling (physical pixels). 0 = no backend limit
     * beyond RoboGraphics surface limits.
     */
    uint32_t presentMaxWidth;
    uint32_t presentMaxHeight;

    /** Goal 7.9: backend implements WaitEvents (timeout / forever). */
    int WaitEvents;
    /** Goal 7.9: backend implements wakeup for a blocked wait. */
    int wakeup;

    /** Goal 7.11: backend implements native mouse capture/release. */
    int mouseCapture;

    /**
     * Host present acceleration: non-zero when the backend can flip through a
     * hardware GPU (D3D11/DXGI on Win32). Compose may still be CPU; this flag
     * only names the *present* path. Zero → CPU/GDI (or guest FB) present.
     */
    int gpuPresent;
} WindowBackendCaps;

void WindowBackendCapsClear(WindowBackendCaps *caps);

/** Null-architecture defaults: lifecycle + title/size/move; no present/focus. */
void WindowBackendCapsInitNull(WindowBackendCaps *caps);

/**
 * Query active backend capabilities.
 * No backend → RoboNotSupported (out zeroed if non-NULL).
 */
Result WindowBackendCapabilities(WindowBackendCaps *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_WINDOW_CAPABILITIES_H */
