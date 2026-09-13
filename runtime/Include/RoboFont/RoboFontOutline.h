/*
 * RoboFont unified glyph outlines (Goal 5.8).
 *
 * Converts TrueType glyf / CFF / CFF2 CharStrings into an immutable
 * geometric path in font design units. No rasterization, hinting, or shaping.
 *
 *   TrueType glyf ─────┐
 *                      ├──> RoboGlyphOutline
 *   CFF CharStrings ───┤
 *   CFF2 CharStrings ──┘
 */

#ifndef ROBO_FONT_OUTLINE_H
#define ROBO_FONT_OUTLINE_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontCff.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct robo_font_tt robo_font_tt_t;
typedef struct robo_font_cff_font robo_font_cff_font_t;
typedef struct robo_font_face robo_font_face_t;

/* ---- Resource limits ---- */

#define ROBO_FONT_OUTLINE_MAX_CONTOURS 1024u
#define ROBO_FONT_OUTLINE_MAX_SEGMENTS 65536u
#define ROBO_FONT_OUTLINE_MAX_POINTS 65536u
#define ROBO_FONT_OUTLINE_MAX_COMPOSITE_DEPTH ROBO_FONT_TT_MAX_COMPOSITE_DEPTH
#define ROBO_FONT_OUTLINE_MAX_COMPOSITE_COMPONENTS 1024u
#define ROBO_FONT_OUTLINE_CACHE_MAX 512u

/* ---- Geometry ---- */

typedef struct robo_font_point {
    robo_font_unit_t x;
    robo_font_unit_t y;
} robo_font_point_t;

typedef enum robo_font_path_op {
    ROBO_FONT_PATH_MOVE = 0,
    ROBO_FONT_PATH_LINE = 1,
    ROBO_FONT_PATH_QUAD = 2,  /* quadratic: c1 = control, p = end */
    ROBO_FONT_PATH_CUBIC = 3, /* cubic: c1,c2 = controls, p = end */
    ROBO_FONT_PATH_CLOSE = 4
} robo_font_path_op_t;

typedef struct robo_font_path_seg {
    robo_font_path_op_t op;
    robo_font_point_t p;  /* destination (MOVE/LINE/QUAD/CUBIC) */
    robo_font_point_t c1; /* QUAD/CUBIC control 1 */
    robo_font_point_t c2; /* CUBIC control 2 */
} robo_font_path_seg_t;

typedef struct robo_font_contour {
    uint32_t seg_count;
    robo_font_path_seg_t *segs; /* owned by outline */
    uint8_t closed;
    robo_font_bbox_t bounds;
    uint8_t has_bounds;
} robo_font_contour_t;

typedef enum robo_font_outline_status {
    ROBO_FONT_OUTLINE_STATUS_OK = 0,
    ROBO_FONT_OUTLINE_STATUS_EMPTY = 1,
    ROBO_FONT_OUTLINE_STATUS_INVALID_ID = 2,
    ROBO_FONT_OUTLINE_STATUS_UNAVAILABLE = 3,
    ROBO_FONT_OUTLINE_STATUS_MALFORMED = 4,
    ROBO_FONT_OUTLINE_STATUS_UNSUPPORTED = 5,
    ROBO_FONT_OUTLINE_STATUS_LIMIT = 6,
    ROBO_FONT_OUTLINE_STATUS_OVERFLOW = 7
} robo_font_outline_status_t;

typedef struct robo_font_outline robo_font_outline_t;

/* ---- Affine (font units; matrix in F2DOT14 × 2^14 scale) ---- */

typedef struct robo_font_affine {
    int16_t xscale; /* F2DOT14; 0x4000 = 1.0 */
    int16_t scale01;
    int16_t scale10;
    int16_t yscale;
    int32_t dx;
    int32_t dy;
} robo_font_affine_t;

void robo_font_affine_identity(robo_font_affine_t *m);
robo_result_t robo_font_affine_multiply(const robo_font_affine_t *a,
                                        const robo_font_affine_t *b,
                                        robo_font_affine_t *out);
robo_result_t robo_font_affine_apply(const robo_font_affine_t *m,
                                     robo_font_unit_t x,
                                     robo_font_unit_t y,
                                     robo_font_unit_t *out_x,
                                     robo_font_unit_t *out_y);

/* ---- Outline accessors ---- */

void robo_font_outline_retain(robo_font_outline_t *o);
void robo_font_outline_release(robo_font_outline_t *o);

robo_glyph_id_t robo_font_outline_glyph_id(const robo_font_outline_t *o);
robo_font_outline_status_t robo_font_outline_status(const robo_font_outline_t *o);
robo_font_outline_tech_t robo_font_outline_tech(const robo_font_outline_t *o);

uint32_t robo_font_outline_contour_count(const robo_font_outline_t *o);
const robo_font_contour_t *robo_font_outline_contour(const robo_font_outline_t *o,
                                                     uint32_t index);

int robo_font_outline_has_bounds(const robo_font_outline_t *o);
robo_result_t robo_font_outline_bounds(const robo_font_outline_t *o,
                                       robo_font_bbox_t *out);

uint32_t robo_font_outline_segment_count(const robo_font_outline_t *o);

/**
 * Deterministic golden dump (ASCII). Writes up to @p cap-1 chars + NUL.
 * Format: "gid=<id> status=<s> contours=<n> bounds=x0,y0,x1,y1\\n"
 * then per contour "C <i> closed=<0|1> segs=<n>\\n" and "  OP x y [c1x c1y] [c2x c2y]\\n"
 */
robo_result_t robo_font_outline_dump(const robo_font_outline_t *o,
                                     char *buf,
                                     size_t cap,
                                     size_t *out_len);

/* ---- Construction from backends ---- */

robo_result_t robo_font_outline_from_tt(robo_font_tt_t *tt,
                                        robo_glyph_id_t glyph_id,
                                        robo_font_outline_t **out);

robo_result_t robo_font_outline_from_cff(robo_font_cff_font_t *cff,
                                         robo_glyph_id_t glyph_id,
                                         robo_font_outline_t **out);

/**
 * Build from SFNT using outline-tech detection.
 * Optionally reuses @p tt / @p cff if non-NULL (retained).
 */
robo_result_t robo_font_outline_from_sfnt(robo_font_sfnt_t *sfnt,
                                          robo_font_tt_t *tt_opt,
                                          robo_font_cff_font_t *cff_opt,
                                          robo_glyph_id_t glyph_id,
                                          robo_font_outline_t **out);

/** Fuzz entry: arbitrary SFNT / table bytes. */
robo_result_t robo_font_outline_fuzz_parse(const uint8_t *data, size_t size);

/* ---- Face-local cache ---- */

typedef struct robo_font_outline_cache robo_font_outline_cache_t;

robo_result_t robo_font_outline_cache_create(robo_font_outline_cache_t **out);
void robo_font_outline_cache_retain(robo_font_outline_cache_t *c);
void robo_font_outline_cache_release(robo_font_outline_cache_t *c);

/**
 * Get or build outline. Retains returned outline for the caller.
 * Thread-safe for concurrent readers after the cache is attached to a face
 * with backends already prepared (no lazy backend init inside lock-free path:
 * prepare backends before concurrent queries).
 */
robo_result_t robo_font_outline_cache_get(robo_font_outline_cache_t *cache,
                                          robo_font_sfnt_t *sfnt,
                                          robo_font_tt_t *tt_opt,
                                          robo_font_cff_font_t *cff_opt,
                                          robo_glyph_id_t glyph_id,
                                          robo_font_outline_t **out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_OUTLINE_H */
