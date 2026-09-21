/*
 * Internal RoboWindow glue (Goals 7.1 / 7.2 / 7.14) — not a public API.
 */
#ifndef ROBO_WINDOW_INTERNAL_H
#define ROBO_WINDOW_INTERNAL_H

#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboWindow/RoboWindowBackend.h"
#include "RoboWindow/RoboWindowConfig.h"
#include "RoboWindow/RoboWindowNative.h"
#include "RoboWindow/RoboWindowState.h"
#include "RoboWindow/RoboWindowTypes.h"

typedef struct WindowSlot {
    int inUse;
    WindowId id;
    WindowState state;
    WindowConfig configSnapshot;
    char title[RoboWindowTitleMax];
    WindowNativeHandle native;
    /** Goal 7.4 borrowed present association (not retained). */
    GfxSurface *PresentSurface;
    /** Goal 7.14 owned render surface (retained by window). */
    GfxSurface *renderSurface;
    int renderDirty;
    int renderDirtyFull;
    GfxRect renderDirtyRect;
    int renderFrameOpen;
    int renderStale;
    GfxPixelFormat renderFormat;
    /** Non-zero: state.scale came from config.scalePreference. */
    int scaleFromPreference;
} WindowSlot;

void WindowDisplaySetupPlaceholders(void);
void WindowDisplayResetPlaceholders(void);

void WindowEventQueueReset(void);

/** Drop queued events for @p id (destroy path — keeps later CLOSED emission). */
void WindowEventPurgeWindow(WindowId id);

void WindowEventLoopReset(void);

void WindowThreadReset(void);

/** First live window with state.focused, or RoboWindowIdNone. */
WindowId WindowFindFocusedId(void);

const WindowBackendVtbl *GetWindowBackendVtbl(void);
void *WindowBackendUserdata(void);

WindowSlot *WindowSlotFind(WindowId id);

Result WindowRequireReady(void);

Result WindowRequireMutableSlot(WindowId id,
                                               WindowSlot **out);

/** Mark owned render surface stale after resize / scale change. */
void WindowRenderMarkStale(WindowSlot *slot);

/** Release owned render surface + clear frame/dirty (destroy path). */
void WindowRenderTeardown(WindowSlot *slot);

void WindowRenderInitSlot(WindowSlot *slot);

#endif /* ROBO_WINDOW_INTERNAL_H */
