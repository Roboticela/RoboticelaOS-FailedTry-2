/*
 * RoboGraphics version pin — Phase 6 public API freeze (Goal 6.15).
 *
 * 0.16.0 (Phase 8.12) adds the decoding half of Goal 6.13: RoboGraphicsDecode.h
 * reads a PNG back into an RGBA8 image. This is additive — no declaration in
 * the frozen v1 surface changed meaning, so v1 remains a usable promise —
 * and it lives here rather than in RoboHTML because pixels are RoboGraphics'
 * business: a document may own an `<img>`, but what an `<img>` holds is a
 * GfxImage, and nothing about PNG belongs to the document model.
 */
#ifndef ROBO_GRAPHICS_VERSION_H
#define ROBO_GRAPHICS_VERSION_H

/*
 * 0.17.0 (Phase 11.7) adds `GfxRasterDrawImage` -- the scaled, alpha-blended image
 * draw a document's `<img>` needs, and the *scaled* half of Goal 6.6's 1:1 copy. It is
 * **additive** in the same sense 0.16.0's decoder was: no declaration in the frozen v1
 * surface changed meaning, so v1 remains a usable promise, and pixels stay this module's
 * business rather than the paint layer's.
 */
#define RoboGraphicsVersionMajor 0
#define RoboGraphicsVersionMinor 17
#define RoboGraphicsVersionPatch 0
#define RoboGraphicsVersionString "0.17.0"

/**
 * Phase 6 public API is frozen when RoboGraphicsApiFrozen is 1.
 * Breaking changes require a new phase milestone and an explicit freeze revisit.
 *
 * Branding: "RoboGraphics API v1" == the frozen Phase 6 surface at 0.15.0,
 * which 0.16.0 and 0.17.0 extend without altering.
 */
#define RoboGraphicsApiFrozen 1

#endif /* ROBO_GRAPHICS_VERSION_H */
