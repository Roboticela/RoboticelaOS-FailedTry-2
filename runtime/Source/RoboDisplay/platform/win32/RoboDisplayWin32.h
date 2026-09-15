/*
 * Internal Win32 display helpers shared by RoboDisplay + RoboWindow backends.
 * Not a public API — never included outside platform/win32/.
 */

#ifndef ROBO_DISPLAY_WIN32_INTERNAL_H
#define ROBO_DISPLAY_WIN32_INTERNAL_H

#if defined(ROBO_WINDOW_HAS_WIN32) && ROBO_WINDOW_HAS_WIN32

#include "RoboDisplay/RoboDisplayTypes.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/** Map HMONITOR → RoboDisplay id (1-based) after host backend enumerate. */
robo_display_id_t robo_display_win32_id_from_hmonitor(HMONITOR mon);

#endif /* ROBO_WINDOW_HAS_WIN32 */

#endif /* ROBO_DISPLAY_WIN32_INTERNAL_H */
