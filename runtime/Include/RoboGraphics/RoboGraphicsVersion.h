/*
 * RoboGraphics version pin — Phase 6 public API freeze (Goal 6.15).
 *
 * 0.16.0 (Phase 8.12) adds the decoding half of Goal 6.13: RoboGraphicsDecode.h
 * reads a PNG back into an RGBA8 image. This is additive — no declaration in
 * the frozen v1 surface changed meaning, so v1 remains a usable promise —
 * and it lives here rather than in RoboHTML because pixels are RoboGraphics'
 * business: a document may own an `<img>`, but what an `<img>` holds is a
 * robo_gfx_image_t, and nothing about PNG belongs to the document model.
 */
#ifndef ROBO_GRAPHICS_VERSION_H
#define ROBO_GRAPHICS_VERSION_H

#define ROBO_GRAPHICS_VERSION_MAJOR 0
#define ROBO_GRAPHICS_VERSION_MINOR 16
#define ROBO_GRAPHICS_VERSION_PATCH 0
#define ROBO_GRAPHICS_VERSION_STRING "0.16.0"

/**
 * Phase 6 public API is frozen when ROBO_GRAPHICS_API_FROZEN is 1.
 * Breaking changes require a new phase milestone and an explicit freeze revisit.
 *
 * Branding: "RoboGraphics API v1" == the frozen Phase 6 surface at 0.15.0,
 * which 0.16.0 extends without altering.
 */
#define ROBO_GRAPHICS_API_FROZEN 1

#endif /* ROBO_GRAPHICS_VERSION_H */
