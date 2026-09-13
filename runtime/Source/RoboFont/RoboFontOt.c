#include "RoboFont/RoboFontOt.h"

#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_font_ot {
    uint32_t refs;
    robo_font_sfnt_t *sfnt;
    robo_font_outline_tech_t outline;
    robo_font_cmap_t cmap;
    uint8_t has_cmap;
    robo_font_gdef_t gdef;
    uint8_t has_gdef;
    robo_font_ot_layout_t gsub;
    uint8_t has_gsub;
    robo_font_ot_layout_t gpos;
    uint8_t has_gpos;
    robo_font_base_t base;
    uint8_t has_base;
    robo_font_jstf_t jstf;
    uint8_t has_jstf;
    robo_font_math_t math;
    uint8_t has_math;
    robo_font_kern_t kern;
    uint8_t has_kern;
    robo_font_colr_t colr;
    uint8_t has_colr;
    robo_font_cpal_t cpal;
    uint8_t has_cpal;
    robo_font_stat_t stat;
    uint8_t has_stat;
};

robo_font_outline_tech_t robo_font_ot_detect_outline(const robo_font_sfnt_t *sfnt)
{
    int has_glyf;
    int has_loca;
    int has_cff;
    int has_cff2;
    if (!sfnt) {
        return ROBO_FONT_OUTLINE_UNKNOWN;
    }
    has_glyf = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_GLYF);
    has_loca = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_LOCA);
    has_cff = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CFF);
    has_cff2 = robo_font_sfnt_has_table(sfnt, ROBO_FONT_TAG_CFF2);
    if (has_cff2) {
        return ROBO_FONT_OUTLINE_CFF2;
    }
    if (has_cff) {
        return ROBO_FONT_OUTLINE_CFF;
    }
    if (has_glyf && has_loca) {
        return ROBO_FONT_OUTLINE_TRUETYPE;
    }
    if (has_glyf || has_loca) {
        return ROBO_FONT_OUTLINE_UNSUPPORTED;
    }
    return ROBO_FONT_OUTLINE_UNKNOWN;
}

robo_font_ot_table_family_t robo_font_ot_table_family(robo_font_tag_t tag)
{
    switch (tag) {
    case ROBO_FONT_TAG_HEAD:
    case ROBO_FONT_TAG_NAME:
    case ROBO_FONT_TAG_OS2:
    case ROBO_FONT_TAG_POST:
    case ROBO_FONT_TAG_MAXP:
        return ROBO_FONT_OT_FAMILY_CORE;
    case ROBO_FONT_TAG_HHEA:
    case ROBO_FONT_TAG_HMTX:
        return ROBO_FONT_OT_FAMILY_METRIC;
    case ROBO_FONT_TAG_CMAP:
        return ROBO_FONT_OT_FAMILY_CMAP;
    case ROBO_FONT_TAG_GSUB:
    case ROBO_FONT_TAG_GPOS:
    case ROBO_FONT_TAG_GDEF:
    case ROBO_FONT_TAG_BASE:
    case ROBO_FONT_TAG_JSTF:
        return ROBO_FONT_OT_FAMILY_LAYOUT;
    case ROBO_FONT_TAG_FVAR:
    case ROBO_FONT_TAG_AVAR:
    case ROBO_FONT_TAG_GVAR:
    case ROBO_FONT_TAG_HVAR:
    case ROBO_FONT_TAG_MVAR:
    case ROBO_FONT_TAG_STAT:
        return ROBO_FONT_OT_FAMILY_VARIATION;
    case ROBO_FONT_TAG_COLR:
    case ROBO_FONT_TAG_CPAL:
    case ROBO_FONT_TAG_SBIX:
    case ROBO_FONT_TAG_CBDT:
    case ROBO_FONT_TAG_CBLC:
    case ROBO_FONT_TAG_SVG:
        return ROBO_FONT_OT_FAMILY_COLOR;
    case ROBO_FONT_TAG_MATH:
        return ROBO_FONT_OT_FAMILY_MATH;
    case ROBO_FONT_TAG_KERN:
        return ROBO_FONT_OT_FAMILY_LEGACY;
    case ROBO_FONT_TAG_GLYF:
    case ROBO_FONT_TAG_LOCA:
    case ROBO_FONT_TAG_CFF:
    case ROBO_FONT_TAG_CFF2:
        return ROBO_FONT_OT_FAMILY_OUTLINE;
    default:
        return ROBO_FONT_OT_FAMILY_UNKNOWN;
    }
}

static robo_result_t take_table(robo_font_sfnt_t *sfnt,
                                robo_font_tag_t tag,
                                robo_font_span_t *out)
{
    return robo_font_sfnt_table_bytes(sfnt, tag, out);
}

robo_result_t robo_font_ot_parse(robo_font_sfnt_t *sfnt,
                                 uint32_t flags,
                                 robo_font_ot_t **out)
{
    robo_font_ot_t *ot;
    robo_font_span_t span;
    robo_result_t res;

    if (!out || !sfnt) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;

    ot = (robo_font_ot_t *)robo_calloc(1, sizeof(*ot));
    if (!ot) {
        return ROBO_OUT_OF_MEMORY;
    }
    ot->outline = robo_font_ot_detect_outline(sfnt);

    if ((flags & ROBO_FONT_OT_FLAGS_PARSE_CMAP) != 0u &&
        take_table(sfnt, ROBO_FONT_TAG_CMAP, &span) == ROBO_OK) {
        res = robo_font_cmap_parse(span, &ot->cmap);
        if (res != ROBO_OK) {
            goto fail;
        }
        ot->has_cmap = 1u;
    }

    if ((flags & ROBO_FONT_OT_FLAGS_PARSE_LAYOUT) != 0u) {
        if (take_table(sfnt, ROBO_FONT_TAG_GDEF, &span) == ROBO_OK) {
            res = robo_font_gdef_parse(span, &ot->gdef);
            if (res != ROBO_OK) {
                goto fail;
            }
            ot->has_gdef = 1u;
        }
        if (take_table(sfnt, ROBO_FONT_TAG_GSUB, &span) == ROBO_OK) {
            res = robo_font_gsub_parse(span, &ot->gsub);
            if (res != ROBO_OK) {
                goto fail;
            }
            ot->has_gsub = 1u;
        }
        if (take_table(sfnt, ROBO_FONT_TAG_GPOS, &span) == ROBO_OK) {
            res = robo_font_gpos_parse(span, &ot->gpos);
            if (res != ROBO_OK) {
                goto fail;
            }
            ot->has_gpos = 1u;
        }
    }

    if ((flags & ROBO_FONT_OT_FLAGS_PARSE_EXTRA) != 0u) {
        if (take_table(sfnt, ROBO_FONT_TAG_BASE, &span) == ROBO_OK) {
            res = robo_font_base_parse(span, &ot->base);
            if (res != ROBO_OK) {
                goto fail;
            }
            ot->has_base = 1u;
        }
        if (take_table(sfnt, ROBO_FONT_TAG_JSTF, &span) == ROBO_OK) {
            res = robo_font_jstf_parse(span, &ot->jstf);
            if (res != ROBO_OK) {
                goto fail;
            }
            ot->has_jstf = 1u;
        }
        if (take_table(sfnt, ROBO_FONT_TAG_MATH, &span) == ROBO_OK) {
            res = robo_font_math_parse(span, &ot->math);
            if (res != ROBO_OK) {
                goto fail;
            }
            ot->has_math = 1u;
        }
        if (take_table(sfnt, ROBO_FONT_TAG_KERN, &span) == ROBO_OK) {
            res = robo_font_kern_parse(span, &ot->kern);
            if (res != ROBO_OK) {
                goto fail;
            }
            ot->has_kern = 1u;
        }
        if (take_table(sfnt, ROBO_FONT_TAG_CPAL, &span) == ROBO_OK) {
            res = robo_font_cpal_parse(span, &ot->cpal);
            if (res != ROBO_OK) {
                goto fail;
            }
            ot->has_cpal = 1u;
        }
        if (take_table(sfnt, ROBO_FONT_TAG_COLR, &span) == ROBO_OK) {
            res = robo_font_colr_parse(span, &ot->colr);
            if (res != ROBO_OK) {
                goto fail;
            }
            ot->has_colr = 1u;
        }
        if (take_table(sfnt, ROBO_FONT_TAG_STAT, &span) == ROBO_OK) {
            res = robo_font_stat_parse(span, &ot->stat);
            if (res != ROBO_OK) {
                goto fail;
            }
            ot->has_stat = 1u;
        }
    }

    ot->refs = 1u;
    robo_font_sfnt_retain(sfnt);
    ot->sfnt = sfnt;
    *out = ot;
    return ROBO_OK;

fail:
    robo_font_cmap_clear(&ot->cmap);
    robo_font_gdef_clear(&ot->gdef);
    robo_font_ot_layout_clear(&ot->gsub);
    robo_font_ot_layout_clear(&ot->gpos);
    robo_font_kern_clear(&ot->kern);
    robo_font_stat_clear(&ot->stat);
    robo_free(ot);
    return res;
}

void robo_font_ot_retain(robo_font_ot_t *ot)
{
    if (!ot) {
        return;
    }
    ot->refs++;
}

void robo_font_ot_release(robo_font_ot_t *ot)
{
    if (!ot) {
        return;
    }
    if (ot->refs == 0u) {
        return;
    }
    ot->refs--;
    if (ot->refs != 0u) {
        return;
    }
    robo_font_cmap_clear(&ot->cmap);
    robo_font_gdef_clear(&ot->gdef);
    robo_font_ot_layout_clear(&ot->gsub);
    robo_font_ot_layout_clear(&ot->gpos);
    robo_font_kern_clear(&ot->kern);
    robo_font_stat_clear(&ot->stat);
    robo_font_sfnt_release(ot->sfnt);
    ot->sfnt = NULL;
    robo_free(ot);
}

const robo_font_sfnt_t *robo_font_ot_sfnt(const robo_font_ot_t *ot)
{
    return ot ? ot->sfnt : NULL;
}

robo_font_outline_tech_t robo_font_ot_outline(const robo_font_ot_t *ot)
{
    return ot ? ot->outline : ROBO_FONT_OUTLINE_UNKNOWN;
}

const robo_font_cmap_t *robo_font_ot_cmap(const robo_font_ot_t *ot)
{
    return (ot && ot->has_cmap) ? &ot->cmap : NULL;
}

const robo_font_gdef_t *robo_font_ot_gdef(const robo_font_ot_t *ot)
{
    return (ot && ot->has_gdef) ? &ot->gdef : NULL;
}

const robo_font_ot_layout_t *robo_font_ot_gsub(const robo_font_ot_t *ot)
{
    return (ot && ot->has_gsub) ? &ot->gsub : NULL;
}

const robo_font_ot_layout_t *robo_font_ot_gpos(const robo_font_ot_t *ot)
{
    return (ot && ot->has_gpos) ? &ot->gpos : NULL;
}

const robo_font_base_t *robo_font_ot_base(const robo_font_ot_t *ot)
{
    return (ot && ot->has_base) ? &ot->base : NULL;
}

const robo_font_jstf_t *robo_font_ot_jstf(const robo_font_ot_t *ot)
{
    return (ot && ot->has_jstf) ? &ot->jstf : NULL;
}

const robo_font_math_t *robo_font_ot_math(const robo_font_ot_t *ot)
{
    return (ot && ot->has_math) ? &ot->math : NULL;
}

const robo_font_kern_t *robo_font_ot_kern(const robo_font_ot_t *ot)
{
    return (ot && ot->has_kern) ? &ot->kern : NULL;
}

const robo_font_colr_t *robo_font_ot_colr(const robo_font_ot_t *ot)
{
    return (ot && ot->has_colr) ? &ot->colr : NULL;
}

const robo_font_cpal_t *robo_font_ot_cpal(const robo_font_ot_t *ot)
{
    return (ot && ot->has_cpal) ? &ot->cpal : NULL;
}

const robo_font_stat_t *robo_font_ot_stat(const robo_font_ot_t *ot)
{
    return (ot && ot->has_stat) ? &ot->stat : NULL;
}

robo_result_t robo_font_ot_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_ot_t *ot = NULL;
    robo_result_t res;

    if (!data || size == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }
    res = robo_font_source_create_memory_borrow(data, size, &src);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_sfnt_parse(src, 0u, ROBO_FONT_SFNT_FLAGS_NONE, &sfnt);
    if (res == ROBO_OK) {
        res = robo_font_ot_parse(sfnt, ROBO_FONT_OT_FLAGS_PARSE_ALL, &ot);
    }
    if (ot) {
        robo_font_ot_release(ot);
    }
    if (sfnt) {
        robo_font_sfnt_release(sfnt);
    }
    robo_font_source_release(src);
    return res;
}
