#include "RoboFont/RoboFontTt.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

void robo_font_glyf_glyph_clear(robo_font_glyf_glyph_t *g)
{
    if (!g) {
        return;
    }
    robo_free(g->end_pts);
    robo_free(g->flags);
    robo_free(g->x_coords);
    robo_free(g->y_coords);
    robo_free(g->components);
    memset(g, 0, sizeof(*g));
}

static robo_result_t parse_simple(robo_font_reader_t *r,
                                  size_t glyph_end,
                                  int16_t ncontours,
                                  robo_font_glyf_glyph_t *out)
{
    uint16_t i;
    uint16_t last_pt;
    uint16_t instr;
    uint16_t point_count;
    size_t flags_start;
    size_t coord_pos;
    uint16_t filled;

    if (ncontours <= 0) {
        return ROBO_PARSE_ERROR;
    }
    out->kind = ROBO_FONT_GLYF_SIMPLE;
    out->contour_count = (uint16_t)ncontours;
    out->end_pts = (uint16_t *)robo_calloc((uint16_t)ncontours, sizeof(uint16_t));
    if (!out->end_pts) {
        return ROBO_OUT_OF_MEMORY;
    }
    for (i = 0u; i < out->contour_count; i++) {
        if (robo_font_reader_pos(r) + 2u > glyph_end ||
            robo_font_read_u16be(r, &out->end_pts[i]) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if (i > 0u && out->end_pts[i] <= out->end_pts[i - 1u]) {
            return ROBO_PARSE_ERROR;
        }
    }
    last_pt = out->end_pts[out->contour_count - 1u];
    point_count = (uint16_t)(last_pt + 1u);
    out->point_count = point_count;

    if (robo_font_reader_pos(r) + 2u > glyph_end ||
        robo_font_read_u16be(r, &instr) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->instruction_length = instr;
    if (robo_font_checked_range(glyph_end, robo_font_reader_pos(r), instr) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    out->instructions.data = r->span.data + robo_font_reader_pos(r);
    out->instructions.size = instr;
    if (robo_font_reader_seek(r, robo_font_reader_pos(r) + instr) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }

    out->flags = (uint8_t *)robo_calloc(point_count, 1u);
    out->x_coords = (int16_t *)robo_calloc(point_count, sizeof(int16_t));
    out->y_coords = (int16_t *)robo_calloc(point_count, sizeof(int16_t));
    if (!out->flags || !out->x_coords || !out->y_coords) {
        return ROBO_OUT_OF_MEMORY;
    }

    /* Expand flags */
    filled = 0u;
    flags_start = robo_font_reader_pos(r);
    (void)flags_start;
    while (filled < point_count) {
        uint8_t flag;
        uint8_t repeat = 0u;
        if (robo_font_reader_pos(r) >= glyph_end ||
            robo_font_read_u8(r, &flag) != ROBO_OK) {
            return ROBO_PARSE_ERROR;
        }
        if ((flag & ROBO_FONT_GLYF_FLAG_REPEAT) != 0u) {
            if (robo_font_reader_pos(r) >= glyph_end ||
                robo_font_read_u8(r, &repeat) != ROBO_OK) {
                return ROBO_PARSE_ERROR;
            }
        }
        out->flags[filled++] = flag;
        while (repeat > 0u) {
            if (filled >= point_count) {
                return ROBO_PARSE_ERROR;
            }
            out->flags[filled++] = flag;
            repeat--;
        }
    }

    /* X coordinates (deltas → absolute) */
    {
        int16_t x = 0;
        for (i = 0u; i < point_count; i++) {
            uint8_t f = out->flags[i];
            int16_t dx = 0;
            if ((f & ROBO_FONT_GLYF_FLAG_X_SHORT) != 0u) {
                uint8_t b;
                if (robo_font_reader_pos(r) >= glyph_end ||
                    robo_font_read_u8(r, &b) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                dx = (int16_t)b;
                if ((f & ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE) == 0u) {
                    dx = (int16_t)(-dx);
                }
            } else if ((f & ROBO_FONT_GLYF_FLAG_X_SAME_OR_POSITIVE) == 0u) {
                if (robo_font_reader_pos(r) + 2u > glyph_end ||
                    robo_font_read_i16be(r, &dx) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
            }
            x = (int16_t)(x + dx);
            out->x_coords[i] = x;
        }
    }

    /* Y coordinates */
    {
        int16_t y = 0;
        for (i = 0u; i < point_count; i++) {
            uint8_t f = out->flags[i];
            int16_t dy = 0;
            if ((f & ROBO_FONT_GLYF_FLAG_Y_SHORT) != 0u) {
                uint8_t b;
                if (robo_font_reader_pos(r) >= glyph_end ||
                    robo_font_read_u8(r, &b) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
                dy = (int16_t)b;
                if ((f & ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE) == 0u) {
                    dy = (int16_t)(-dy);
                }
            } else if ((f & ROBO_FONT_GLYF_FLAG_Y_SAME_OR_POSITIVE) == 0u) {
                if (robo_font_reader_pos(r) + 2u > glyph_end ||
                    robo_font_read_i16be(r, &dy) != ROBO_OK) {
                    return ROBO_PARSE_ERROR;
                }
            }
            y = (int16_t)(y + dy);
            out->y_coords[i] = y;
        }
    }

    coord_pos = robo_font_reader_pos(r);
    (void)coord_pos;
    /* Trailing padding to even boundary is allowed; must not exceed glyph_end. */
    if (robo_font_reader_pos(r) > glyph_end) {
        return ROBO_PARSE_ERROR;
    }
    return ROBO_OK;
}

static robo_result_t parse_composite(robo_font_reader_t *r,
                                     size_t glyph_end,
                                     uint16_t num_glyphs,
                                     robo_font_glyf_glyph_t *out)
{
    uint16_t flags;
    uint16_t count = 0u;
    robo_font_glyf_component_t *comps;
    size_t cap = 8u;

    out->kind = ROBO_FONT_GLYF_COMPOSITE;
    comps = (robo_font_glyf_component_t *)robo_calloc(cap, sizeof(*comps));
    if (!comps) {
        return ROBO_OUT_OF_MEMORY;
    }

    do {
        robo_font_glyf_component_t *c;
        if (count >= ROBO_FONT_TT_MAX_COMPONENTS_PER_GLYPH) {
            robo_free(comps);
            return ROBO_PARSE_ERROR;
        }
        if (count >= cap) {
            size_t ncap = cap * 2u;
            robo_font_glyf_component_t *n =
                (robo_font_glyf_component_t *)robo_calloc(ncap, sizeof(*n));
            if (!n) {
                robo_free(comps);
                return ROBO_OUT_OF_MEMORY;
            }
            memcpy(n, comps, count * sizeof(*comps));
            robo_free(comps);
            comps = n;
            cap = ncap;
        }
        c = &comps[count];
        memset(c, 0, sizeof(*c));
        c->xscale = 0x4000;
        c->yscale = 0x4000;

        if (robo_font_reader_pos(r) + 4u > glyph_end ||
            robo_font_read_u16be(r, &flags) != ROBO_OK ||
            robo_font_read_u16be(r, &c->glyph_index) != ROBO_OK) {
            robo_free(comps);
            return ROBO_PARSE_ERROR;
        }
        c->flags = flags;
        if (c->glyph_index >= num_glyphs) {
            robo_free(comps);
            return ROBO_PARSE_ERROR;
        }

        if ((flags & ROBO_FONT_GLYF_COMP_ARGS_WORDS) != 0u) {
            int16_t a1;
            int16_t a2;
            if (robo_font_reader_pos(r) + 4u > glyph_end ||
                robo_font_read_i16be(r, &a1) != ROBO_OK ||
                robo_font_read_i16be(r, &a2) != ROBO_OK) {
                robo_free(comps);
                return ROBO_PARSE_ERROR;
            }
            c->arg1 = a1;
            c->arg2 = a2;
        } else {
            uint8_t a1;
            uint8_t a2;
            if (robo_font_reader_pos(r) + 2u > glyph_end ||
                robo_font_read_u8(r, &a1) != ROBO_OK ||
                robo_font_read_u8(r, &a2) != ROBO_OK) {
                robo_free(comps);
                return ROBO_PARSE_ERROR;
            }
            if ((flags & ROBO_FONT_GLYF_COMP_ARGS_XY) != 0u) {
                c->arg1 = (int32_t)(int8_t)a1;
                c->arg2 = (int32_t)(int8_t)a2;
            } else {
                c->arg1 = a1;
                c->arg2 = a2;
            }
        }

        if ((flags & ROBO_FONT_GLYF_COMP_HAVE_SCALE) != 0u) {
            int16_t s;
            if (robo_font_reader_pos(r) + 2u > glyph_end ||
                robo_font_read_i16be(r, &s) != ROBO_OK) {
                robo_free(comps);
                return ROBO_PARSE_ERROR;
            }
            c->xscale = s;
            c->yscale = s;
            c->has_transform = 1u;
        } else if ((flags & ROBO_FONT_GLYF_COMP_HAVE_XY_SCALE) != 0u) {
            if (robo_font_reader_pos(r) + 4u > glyph_end ||
                robo_font_read_i16be(r, &c->xscale) != ROBO_OK ||
                robo_font_read_i16be(r, &c->yscale) != ROBO_OK) {
                robo_free(comps);
                return ROBO_PARSE_ERROR;
            }
            c->has_transform = 1u;
        } else if ((flags & ROBO_FONT_GLYF_COMP_HAVE_2X2) != 0u) {
            if (robo_font_reader_pos(r) + 8u > glyph_end ||
                robo_font_read_i16be(r, &c->xscale) != ROBO_OK ||
                robo_font_read_i16be(r, &c->scale01) != ROBO_OK ||
                robo_font_read_i16be(r, &c->scale10) != ROBO_OK ||
                robo_font_read_i16be(r, &c->yscale) != ROBO_OK) {
                robo_free(comps);
                return ROBO_PARSE_ERROR;
            }
            c->has_transform = 1u;
        }

        count++;
    } while ((flags & ROBO_FONT_GLYF_COMP_MORE) != 0u);

    /* Optional instructions after components */
    if ((flags & ROBO_FONT_GLYF_COMP_HAVE_INSTRUCTIONS) != 0u) {
        uint16_t instr;
        if (robo_font_reader_pos(r) + 2u > glyph_end ||
            robo_font_read_u16be(r, &instr) != ROBO_OK) {
            robo_free(comps);
            return ROBO_PARSE_ERROR;
        }
        if (robo_font_checked_range(glyph_end, robo_font_reader_pos(r), instr) != ROBO_OK) {
            robo_free(comps);
            return ROBO_PARSE_ERROR;
        }
        out->composite_instruction_length = instr;
        out->composite_instructions.data = r->span.data + robo_font_reader_pos(r);
        out->composite_instructions.size = instr;
        if (robo_font_reader_seek(r, robo_font_reader_pos(r) + instr) != ROBO_OK) {
            robo_free(comps);
            return ROBO_PARSE_ERROR;
        }
    }

    if (robo_font_reader_pos(r) > glyph_end) {
        robo_free(comps);
        return ROBO_PARSE_ERROR;
    }

    out->component_count = count;
    out->components = comps;
    return ROBO_OK;
}

robo_result_t robo_font_tt_parse_glyph_at(robo_font_span_t glyf,
                                          uint32_t offset,
                                          uint32_t length,
                                          uint16_t num_glyphs,
                                          robo_font_glyf_glyph_t *out)
{
    robo_font_reader_t r;
    robo_font_glyf_header_t hdr;
    size_t glyph_end;
    robo_result_t res;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_font_glyf_glyph_clear(out);
    if (length == 0u) {
        out->kind = ROBO_FONT_GLYF_EMPTY;
        out->offset = offset;
        out->length = 0u;
        return ROBO_OK;
    }
    if (robo_font_checked_range(glyf.size, offset, length) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (length < 10u) {
        return ROBO_PARSE_ERROR;
    }

    out->offset = offset;
    out->length = length;
    glyph_end = (size_t)offset + (size_t)length;
    robo_font_reader_init(&r, glyf.data, glyf.size);
    if (robo_font_reader_seek(&r, offset) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (robo_font_read_i16be(&r, &hdr.number_of_contours) != ROBO_OK ||
        robo_font_read_i16be(&r, &hdr.x_min) != ROBO_OK ||
        robo_font_read_i16be(&r, &hdr.y_min) != ROBO_OK ||
        robo_font_read_i16be(&r, &hdr.x_max) != ROBO_OK ||
        robo_font_read_i16be(&r, &hdr.y_max) != ROBO_OK) {
        return ROBO_PARSE_ERROR;
    }
    if (hdr.x_min > hdr.x_max || hdr.y_min > hdr.y_max) {
        return ROBO_PARSE_ERROR;
    }
    out->header = hdr;

    if (hdr.number_of_contours >= 0) {
        res = parse_simple(&r, glyph_end, hdr.number_of_contours, out);
    } else if (hdr.number_of_contours == -1) {
        res = parse_composite(&r, glyph_end, num_glyphs, out);
    } else {
        return ROBO_PARSE_ERROR;
    }
    if (res != ROBO_OK) {
        robo_font_glyf_glyph_clear(out);
    }
    return res;
}
