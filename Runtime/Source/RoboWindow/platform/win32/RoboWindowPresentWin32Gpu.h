/*
 * Host Win32: D3D11/DXGI present acceleration.
 *
 * Chromium-style split (reference only): software composition can remain correct
 * while the *output device* is GPU-accelerated when hardware is available, with
 * an immediate CPU/GDI fallback. No Chromium dependency.
 *
 * Compose stays in RoboCompositor (CPU surfaces + damage). This module only
 * accelerates the final HWND flip.
 */

#ifndef ROBO_WINDOW_PRESENT_WIN32_GPU_H
#define ROBO_WINDOW_PRESENT_WIN32_GPU_H

#if defined(RoboWindowHasWin32) && RoboWindowHasWin32

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Win32GpuWindow Win32GpuWindow;

/** Probe once: non-zero when a hardware D3D11 device was created. */
int Win32GpuPresentAvailable(void);

/** Release the shared device (backend shutdown). */
void Win32GpuPresentShutdownGlobal(void);

/** Destroy per-window swap-chain state. Safe on NULL. */
void Win32GpuWindowDestroy(Win32GpuWindow **window);

/**
 * Present @p surface into @p hwnd via DXGI when GPU is available.
 * @return RoboOk on GPU present; RoboNotSupported to request GDI fallback;
 *         other errors after a failed GPU attempt (caller may still GDI).
 */
Result Win32GpuPresentSurface(HWND hwnd,
                                      Win32GpuWindow **window,
                                      const GfxSurface *surface);

#ifdef __cplusplus
}
#endif

#endif /* RoboWindowHasWin32 */

#endif /* ROBO_WINDOW_PRESENT_WIN32_GPU_H */
