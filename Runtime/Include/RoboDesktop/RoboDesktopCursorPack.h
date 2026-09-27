/*
 * System cursor pack (Phase 44.7 foundation).
 *
 * One pack under `/System/Cursors/`: SVG masters for authoring / web CSS, and a
 * single `roboticela.rcur` binary the shell paints from. Logical size is points
 * (`logical_pt` in the manifest); physical pixels = points × device scale.
 *
 * This module owns no filesystem. A caller supplies `.rcur` bytes (host test,
 * guest image open, or embedded). Painting stays in RoboDesktopChrome.
 *
 * See `Documentation/Runtime/Desktop/Cursors.md`.
 */

#ifndef ROBO_DESKTOP_CURSOR_PACK_H
#define ROBO_DESKTOP_CURSOR_PACK_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboInput/RoboInputCursor.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Theme variant inside a `.rcur` entry. */
typedef enum DesktopCursorTheme {
    RoboDesktopCursorThemeLight = 0,
    RoboDesktopCursorThemeDark = 1
} DesktopCursorTheme;

/** How many cursor roles the Input shape vocabulary maps onto. */
#define RoboDesktopCursorRoleCount RoboInputCursorTypeCount

/**
 * One painted cursor: borrowed surface pixels, hotspot in the bitmap's own
 * pixel space, and animation facts when the shape loops.
 */
typedef struct DesktopCursorFrame {
    /** Borrowed; owned by the pack. NULL when the shape is hidden or missing. */
    GfxSurface *surface;
    /** Hotspot in bitmap pixels (origin top-left of the surface). */
    GfxPoint hotspot;
    /** Pixel width/height of @ref surface (equal; square assets). */
    uint32_t pixelSize;
    /** Frames in the animation; 1 for static. */
    uint32_t frameCount;
    /** Duration of each frame in ms when @ref frameCount > 1. */
    uint32_t frameDurationMs;
} DesktopCursorFrame;

typedef struct DesktopCursorPack DesktopCursorPack;

/**
 * Parse a `.rcur` blob into an owned pack. Copies @p bytes so the caller may
 * free its buffer after a successful return.
 *
 * @return RoboOk; RoboInvalidArgument for NULL / empty; RoboParseError for a
 *         bad magic or truncated table; RoboOutOfMemory; RoboError when a PNG
 *         frame refuses to decode.
 */
Result DesktopCursorPackCreateFromRcur(const void *bytes,
                                       size_t length,
                                       DesktopCursorPack **out);

/** Release @p pack. Safe on NULL. */
void DesktopCursorPackDestroy(DesktopCursorPack *pack);

/** Non-zero when @p pack holds at least one decoded cursor frame. */
int DesktopCursorPackIsLive(const DesktopCursorPack *pack);

/** Cursor entries the file declared (including extras beyond Input shapes). */
uint32_t DesktopCursorPackEntryCount(const DesktopCursorPack *pack);

/** Logical point size the pack was authored for (manifest / meta; default 16). */
uint32_t DesktopCursorPackLogicalPt(const DesktopCursorPack *pack);

/**
 * Resolve @p shape for @p theme at the nearest authored resolution to
 * @p wantPixels (typically `logicalPt * deviceScale`). @p animMs selects the
 * frame for animated shapes (wait / progress); ignored when static.
 *
 * Hidden (`none`) and unknown shapes leave @p out empty and return RoboOk.
 * A shape the pack does not carry returns RoboNotSupported with @p out empty.
 */
Result DesktopCursorPackResolve(const DesktopCursorPack *pack,
                                InputCursorType shape,
                                DesktopCursorTheme theme,
                                uint32_t wantPixels,
                                uint32_t animMs,
                                DesktopCursorFrame *out);

/**
 * Map an extended pack name (`pencil`, `link`, `col-resize`, …) the same way.
 * Used by the Cursor Lab gallery; the shell's Input vocabulary does not need it.
 */
Result DesktopCursorPackResolveByName(const DesktopCursorPack *pack,
                                      const char *stateName,
                                      DesktopCursorTheme theme,
                                      uint32_t wantPixels,
                                      uint32_t animMs,
                                      DesktopCursorFrame *out);

/**
 * Composite @p frame onto @p dst so the hotspot lands on @p pointer.
 * Transparent pixels are source-over; missing frame is a no-op.
 */
Result DesktopCursorPackBlit(GfxSurface *dst,
                             GfxPoint pointer,
                             const DesktopCursorFrame *frame);

/**
 * Axis-aligned bounds the blit would touch (clipped to @p screen), for the
 * underlay capture / damage union. Empty when there is nothing to draw.
 */
GfxRect DesktopCursorPackBounds(GfxPoint pointer,
                                const DesktopCursorFrame *frame,
                                GfxRect screen);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_CURSOR_PACK_H */
