/*
 * RoboDemo font — fixed 8x8 ASCII atlas + drawing helpers (visual checkpoint).
 *
 * TEMPORARY / DEMO-ONLY. Roboticela's real text path is RoboFont + RoboText +
 * the RoboGraphics glyph bridge (RoboGraphics/RoboGraphicsText.h), which needs
 * a real font face loaded through the font pipeline. Until fonts reach the
 * runtime, the visual checkpoint (Phase 8.2 demo) uses this single
 * deterministic atlas so the demo renders identically on the host and in
 * QEMU.
 *
 * This is NOT a font engine: no shaping, no advances, no kerning, no
 * Unicode, no hinting. One 8x8 cell per printable ASCII code point plus an
 * integer scale factor.
 *
 * Coverage: printable ASCII 0x20..0x7E. Any other byte renders as '?'.
 *
 * Part of the throwaway demo layer — see demo/docs/VisualCheckpoint.md. It
 * will be deleted once RoboText is wired into the render path and must never
 * be cited as the runtime's text API.
 */

#ifndef ROBO_DEMO_FONT_H
#define ROBO_DEMO_FONT_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsSurface.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Glyph cell size in unscaled pixels (scale 1). */
#define ROBO_DEMO_FONT_CELL_W 8
#define ROBO_DEMO_FONT_CELL_H 8

/** Horizontal gap added after each glyph in unscaled pixels. */
#define ROBO_DEMO_FONT_GAP 1

/** Clamp a requested scale to the supported minimum (1). */
int robo_demo_font_scale_clamp(int scale);

/**
 * Fill @p rect intersected with @p surface bounds.
 *
 * robo_gfx_surface_fill_rect requires a fully enclosed rect, so every demo
 * draw goes through here: clipped-away area is dropped instead of turning
 * into an error, which keeps demo painting total (a layout that overflows a
 * small window degrades instead of failing).
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL surface.
 */
robo_result_t robo_demo_fill_clipped(robo_gfx_surface_t *surface,
                                     robo_gfx_rect_t rect,
                                     robo_gfx_color_rgba8_t color);

/**
 * Device-pixel size of @p text drawn at @p scale.
 * @p scale < 1 is treated as 1. Returns a 0x0 size for empty text.
 */
robo_gfx_size_t robo_demo_font_measure(const char *text,
                                       size_t length,
                                       int scale);

/**
 * Draw @p text with its top-left ink corner at (x, y) in device pixels.
 * Bytes outside printable ASCII render as '?'. Text outside the surface is
 * clipped, never an error.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL surface/text.
 */
robo_result_t robo_demo_font_draw(robo_gfx_surface_t *surface,
                                  int32_t x,
                                  int32_t y,
                                  const char *text,
                                  size_t length,
                                  int scale,
                                  robo_gfx_color_rgba8_t color);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DEMO_FONT_H */
