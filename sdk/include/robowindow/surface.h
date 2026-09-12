#ifndef ROBOWINDOW_SURFACE_H
#define ROBOWINDOW_SURFACE_H

#include "types.h"

/*
 * v1 surfaces are kernel SHM objects mapped into both the client and
 * RoboWindowServer. Migration path: replace SYS_IPC SHM_* with a future
 * MAP_SHARED / buffer-handle compositor protocol without changing this
 * struct's width/height/stride/pixfmt fields.
 *
 * Coordinates:
 *   x,y on windows are screen coordinates (display origin top-left).
 *   Surface pixels are surface-local (0,0 is the window's top-left).
 */

#endif
