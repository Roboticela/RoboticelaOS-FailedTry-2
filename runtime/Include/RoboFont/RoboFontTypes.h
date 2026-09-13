/*
 * RoboFont fundamental types (Goal 5.1).
 *
 * Terminology (do not conflate):
 *   Unicode code point     — scalar value (U+xxxx)
 *   Unicode grapheme       — extended grapheme cluster (user-perceived character)
 *   character sequence     — one or more code points / graphemes
 *   glyph identifier       — opaque ID scoped to a font face
 *   glyph cluster mapping  — shaping output (later goals)
 *   glyph position         — advance / offset (later goals)
 *
 * Never assume 1 code point = 1 glyph.
 *
 * Coordinate conventions:
 *   Font design space (font units): origin at baseline; +X right; +Y up
 *     (OpenType / TrueType design-space convention).
 *   Device pixels: origin top-left of the layout surface; +X right; +Y down
 *     (typical UI / framebuffer). Scaling APIs convert between spaces.
 */

#ifndef ROBO_FONT_TYPES_H
#define ROBO_FONT_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque unique identity for a loaded font face (stable while face lives). */
typedef uint64_t robo_font_face_id_t;

/** Opaque unique identity for a font instance (stable while instance lives). */
typedef uint64_t robo_font_instance_id_t;

/**
 * Glyph identifier scoped to one font face.
 * ROBO_GLYPH_ID_NONE means "missing / not mapped".
 * SFNT faces will use the 16-bit glyph index range; the type is wider so
 * future color-layer or synthetic IDs can extend without ABI break.
 */
typedef uint32_t robo_glyph_id_t;

#define ROBO_GLYPH_ID_NONE ((robo_glyph_id_t)0u)

/** Design-space integer in font units (FUnits). */
typedef int32_t robo_font_unit_t;

/** CSS-like absolute size in points (1/72 inch). Not device pixels. */
typedef float robo_font_point_size_t;

/** Size in device-independent pixels (CSS px). */
typedef float robo_font_pixel_size_t;

/**
 * Axis-aligned box in a documented coordinate space.
 * For font units: y increases upward; for device pixels: y increases downward.
 */
typedef struct robo_font_bbox {
    robo_font_unit_t x_min;
    robo_font_unit_t y_min;
    robo_font_unit_t x_max;
    robo_font_unit_t y_max;
} robo_font_bbox_t;

/** Horizontal advance in font units (design space). */
typedef robo_font_unit_t robo_font_advance_t;

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_TYPES_H */
