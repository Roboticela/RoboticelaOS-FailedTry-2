/*
 * RoboFontColor — OpenType color fonts (Goal 5.19).
 *
 * Unified representation of COLR/CPAL, CBDT/CBLC, sbix, and SVG glyph data.
 * Parses and validates color structures for later rendering.
 *
 * Does NOT implement:
 *   color glyph rasterization, COLR paint execution, gradient/bitmap/SVG
 *   rendering, CSS font-palette, emoji shaping, or compositor integration.
 */

#ifndef ROBO_FONT_COLOR_H
#define ROBO_FONT_COLOR_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboFont/RoboFontVariation.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBO_FONT_COLOR_MAX_PALETTES 256u
#define ROBO_FONT_COLOR_MAX_PALETTE_ENTRIES 1024u
#define ROBO_FONT_COLOR_MAX_LAYERS 1024u
#define ROBO_FONT_COLOR_MAX_PAINT_NODES 4096u
#define ROBO_FONT_COLOR_MAX_PAINT_DEPTH 32u
#define ROBO_FONT_COLOR_MAX_STRIKES 64u
#define ROBO_FONT_COLOR_MAX_SVG_DOCS 4096u
#define ROBO_FONT_COLOR_MAX_SVG_PAYLOAD (1024u * 1024u)
#define ROBO_FONT_COLOR_FOREGROUND_PALETTE 0xFFFFu

/* ---- Capabilities / formats ---- */

typedef enum robo_font_color_capability {
    ROBO_FONT_COLOR_CAP_NONE = 0,
    ROBO_FONT_COLOR_CAP_COLR = 1u << 0,
    ROBO_FONT_COLOR_CAP_CPAL = 1u << 1,
    ROBO_FONT_COLOR_CAP_COLR_V0 = 1u << 2,
    ROBO_FONT_COLOR_CAP_COLR_V1 = 1u << 3,
    ROBO_FONT_COLOR_CAP_CBDT = 1u << 4,
    ROBO_FONT_COLOR_CAP_CBLC = 1u << 5,
    ROBO_FONT_COLOR_CAP_SBIX = 1u << 6,
    ROBO_FONT_COLOR_CAP_SVG = 1u << 7,
    ROBO_FONT_COLOR_CAP_VARIATION = 1u << 8 /* VarStore / var paints present */
} robo_font_color_capability_t;

typedef enum robo_font_color_glyph_format {
    ROBO_FONT_COLOR_GLYPH_NONE = 0,
    ROBO_FONT_COLOR_GLYPH_MONOCHROME = 1,
    ROBO_FONT_COLOR_GLYPH_LAYERS = 2,   /* COLR v0 layers */
    ROBO_FONT_COLOR_GLYPH_PAINT = 3,    /* COLR v1 paint tree */
    ROBO_FONT_COLOR_GLYPH_BITMAP = 4,   /* CBDT or sbix */
    ROBO_FONT_COLOR_GLYPH_SVG = 5,
    ROBO_FONT_COLOR_GLYPH_UNSUPPORTED = 6
} robo_font_color_glyph_format_t;

typedef enum robo_font_color_paint_op {
    ROBO_FONT_COLOR_PAINT_NONE = 0,
    ROBO_FONT_COLOR_PAINT_COLR_LAYERS = 1,
    ROBO_FONT_COLOR_PAINT_SOLID = 2,
    ROBO_FONT_COLOR_PAINT_LINEAR_GRADIENT = 4,
    ROBO_FONT_COLOR_PAINT_RADIAL_GRADIENT = 6,
    ROBO_FONT_COLOR_PAINT_SWEEP_GRADIENT = 8,
    ROBO_FONT_COLOR_PAINT_GLYPH = 10,
    ROBO_FONT_COLOR_PAINT_COLR_GLYPH = 11,
    ROBO_FONT_COLOR_PAINT_TRANSFORM = 12,
    ROBO_FONT_COLOR_PAINT_TRANSLATE = 14,
    ROBO_FONT_COLOR_PAINT_SCALE = 16,
    ROBO_FONT_COLOR_PAINT_ROTATE = 24,
    ROBO_FONT_COLOR_PAINT_SKEW = 28,
    ROBO_FONT_COLOR_PAINT_COMPOSITE = 32,
    ROBO_FONT_COLOR_PAINT_UNSUPPORTED = 255
} robo_font_color_paint_op_t;

typedef struct robo_font_color_rgba {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a; /* unpremultiplied per OpenType CPAL */
} robo_font_color_rgba_t;

typedef struct robo_font_color_palette_entry {
    robo_font_color_rgba_t color;
    uint16_t palette_index; /* index within palette */
} robo_font_color_palette_entry_t;

typedef struct robo_font_color_palette {
    uint16_t index;
    uint16_t entry_count;
    uint16_t first_color_index; /* into CPAL colorRecords */
    uint16_t flags;             /* CPAL v1; 0 if absent */
    uint16_t name_id;           /* 0xFFFF if absent */
} robo_font_color_palette_t;

typedef struct robo_font_color_glyph_layer {
    robo_glyph_id_t glyph_id;
    uint16_t palette_index; /* 0xFFFF = foreground */
} robo_font_color_glyph_layer_t;

typedef struct robo_font_color_paint {
    robo_font_color_paint_op_t op;
    uint8_t format; /* raw COLR format number */
    uint8_t depth;
    uint8_t unsupported;
    uint16_t palette_index;
    uint8_t alpha_f2dot14_hi; /* packed; prefer alpha_u8 when solid */
    uint8_t alpha_u8;
    robo_glyph_id_t glyph_id;
    uint32_t child_index;     /* first child node index; UINT32_MAX = none */
    uint32_t sibling_index;   /* next sibling; UINT32_MAX = none */
    int16_t tx;               /* F2DOT14 or FWORD depending on op — stored raw */
    int16_t ty;
    int16_t xx;
    int16_t xy;
    int16_t yx;
    int16_t yy;
    uint8_t composite_mode;
    uint8_t has_variation; /* Var* paint formats */
} robo_font_color_paint_t;

typedef struct robo_font_color_glyph_image {
    uint16_t ppem_x;
    uint16_t ppem_y;
    int16_t origin_x;
    int16_t origin_y;
    uint32_t graphic_type; /* four-char tag for sbix; 0 for CBDT */
    uint8_t format;        /* CBDT image format if applicable */
    robo_font_span_t data; /* borrowed from font source */
} robo_font_color_glyph_image_t;

typedef struct robo_font_color_glyph_svg {
    robo_glyph_id_t start_glyph_id;
    robo_glyph_id_t end_glyph_id;
    robo_font_span_t document; /* borrowed; inert UTF-8/XML bytes */
    uint8_t has_external_ref_hint; /* 1 if payload contains "http://" / "href" heuristics */
} robo_font_color_glyph_svg_t;

typedef struct robo_font_color_glyph {
    robo_glyph_id_t glyph_id;
    robo_font_color_glyph_format_t format;
    uint32_t layer_count;
    const robo_font_color_glyph_layer_t *layers; /* owned by info when LAYERS */
    uint32_t paint_root; /* index into paint nodes; UINT32_MAX if none */
    uint8_t has_image;
    uint8_t has_svg;
    uint8_t variation_dependent;
} robo_font_color_glyph_t;

typedef struct robo_font_color_info robo_font_color_info_t;

typedef struct robo_font_color_palette_selection {
    uint16_t palette_index; /* selected CPAL palette */
    uint8_t has_overrides;
    uint16_t override_count;
    const uint16_t *override_indices; /* borrowed */
    const robo_font_color_rgba_t *override_colors; /* borrowed */
} robo_font_color_palette_selection_t;

void robo_font_color_palette_selection_init(robo_font_color_palette_selection_t *sel);

/* ---- Construction ---- */

/**
 * Build immutable color-font info from SFNT tables.
 * ROBO_NOT_FOUND if no color tables are present.
 */
robo_result_t robo_font_color_info_create(robo_font_sfnt_t *sfnt,
                                          const robo_font_variation_coords_t *coords_opt,
                                          robo_font_color_info_t **out);

robo_result_t robo_font_color_info_create_from_face(
    robo_font_face_t *face,
    const robo_font_variation_coords_t *coords_opt,
    robo_font_color_info_t **out);

void robo_font_color_info_retain(robo_font_color_info_t *info);
void robo_font_color_info_release(robo_font_color_info_t *info);

uint32_t robo_font_color_info_capabilities(const robo_font_color_info_t *info);
int robo_font_color_info_is_color_capable(const robo_font_color_info_t *info);
uint16_t robo_font_color_info_colr_version(const robo_font_color_info_t *info);

/* ---- Palettes ---- */

uint16_t robo_font_color_info_palette_count(const robo_font_color_info_t *info);
robo_result_t robo_font_color_info_palette(const robo_font_color_info_t *info,
                                           uint16_t palette_index,
                                           robo_font_color_palette_t *out);
robo_result_t robo_font_color_info_palette_entry(const robo_font_color_info_t *info,
                                                 uint16_t palette_index,
                                                 uint16_t entry_index,
                                                 robo_font_color_rgba_t *out);

/**
 * Resolve a palette index for a glyph layer, applying selection + overrides.
 * palette_index 0xFFFF → ROBO_NOT_FOUND (foreground; caller supplies).
 */
robo_result_t robo_font_color_resolve_palette_color(
    const robo_font_color_info_t *info,
    const robo_font_color_palette_selection_t *sel,
    uint16_t palette_index,
    robo_font_color_rgba_t *out);

/* ---- Glyph queries ---- */

robo_result_t robo_font_color_info_glyph(const robo_font_color_info_t *info,
                                         robo_glyph_id_t glyph_id,
                                         robo_font_color_glyph_t *out);

robo_result_t robo_font_color_info_glyph_layers(const robo_font_color_info_t *info,
                                                robo_glyph_id_t glyph_id,
                                                const robo_font_color_glyph_layer_t **out_layers,
                                                uint32_t *out_count);

/**
 * Build / fetch COLR v1 paint tree for a glyph.
 * Returns ROBO_NOT_FOUND if no paint; ROBO_NOT_SUPPORTED if format unreadable.
 * Paint nodes are owned by info (immutable after first successful build).
 */
robo_result_t robo_font_color_info_glyph_paint(const robo_font_color_info_t *info,
                                               robo_glyph_id_t glyph_id,
                                               const robo_font_color_paint_t **out_nodes,
                                               uint32_t *out_node_count,
                                               uint32_t *out_root_index);

robo_result_t robo_font_color_info_glyph_image(const robo_font_color_info_t *info,
                                               robo_glyph_id_t glyph_id,
                                               uint16_t prefer_ppem,
                                               robo_font_color_glyph_image_t *out);

robo_result_t robo_font_color_info_glyph_svg(const robo_font_color_info_t *info,
                                             robo_glyph_id_t glyph_id,
                                             robo_font_color_glyph_svg_t *out);

/** Borrowed variation coords attached at create time (may be NULL). */
const robo_font_variation_coords_t *robo_font_color_info_variation_coords(
    const robo_font_color_info_t *info);

/* ---- Bitmap / sbix / SVG table summaries ---- */

uint16_t robo_font_color_info_sbix_strike_count(const robo_font_color_info_t *info);
uint16_t robo_font_color_info_cblc_strike_count(const robo_font_color_info_t *info);
uint32_t robo_font_color_info_svg_document_count(const robo_font_color_info_t *info);

/* ---- Fuzz ---- */

robo_result_t robo_font_color_fuzz_parse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_COLOR_H */
