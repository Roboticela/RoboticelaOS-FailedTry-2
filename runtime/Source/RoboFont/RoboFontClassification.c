#include "RoboFont/RoboFontClassification.h"

#include "RoboFont/RoboFontSfnt.h"
#include "RoboMem/RoboMem.h"

#include <stdint.h>
#include <string.h>

struct robo_font_classification_info {
    uint32_t refs;
    robo_font_classification_t cls;
};

static int32_t fixed_to_millidegrees(int32_t fixed_16_16)
{
    return (int32_t)(((int64_t)fixed_16_16 * 1000) / 65536);
}

static void raw_init_empty(robo_font_class_raw_t *raw)
{
    memset(raw, 0, sizeof(*raw));
}

static void fill_raw_from_tt(const robo_font_tt_t *tt, robo_font_class_raw_t *raw)
{
    const robo_font_os2_t *os2;
    const robo_font_post_t *post;
    const robo_font_head_t *head;

    raw_init_empty(raw);
    if (!tt) {
        return;
    }
    head = robo_font_tt_head(tt);
    if (head) {
        raw->has_head = 1u;
        raw->mac_style = head->mac_style;
    }
    os2 = robo_font_tt_os2(tt);
    if (os2) {
        raw->has_os2 = 1u;
        raw->os2_version = os2->version;
        raw->us_weight_class = os2->us_weight_class;
        raw->us_width_class = os2->us_width_class;
        raw->fs_selection = os2->fs_selection;
    }
    post = robo_font_tt_post(tt);
    if (post) {
        raw->has_post = 1u;
        raw->italic_angle_fixed = post->italic_angle;
    }
}

static void fill_raw_stat(const robo_font_stat_t *stat, robo_font_class_raw_t *raw)
{
    uint16_t i;
    if (!stat || !stat->axes) {
        return;
    }
    raw->has_stat = 1u;
    raw->stat_axis_count = stat->design_axis_count;
    for (i = 0u; i < stat->design_axis_count; i++) {
        robo_font_tag_t tag = stat->axes[i].tag;
        if (tag == ROBO_FONT_TAG('w', 'g', 'h', 't')) {
            raw->stat_has_wght = 1u;
        } else if (tag == ROBO_FONT_TAG('w', 'd', 't', 'h')) {
            raw->stat_has_wdth = 1u;
        } else if (tag == ROBO_FONT_TAG('i', 't', 'a', 'l')) {
            raw->stat_has_ital = 1u;
        } else if (tag == ROBO_FONT_TAG('s', 'l', 'n', 't')) {
            raw->stat_has_slnt = 1u;
        } else if (tag == ROBO_FONT_TAG('o', 'p', 's', 'z')) {
            raw->stat_has_opsz = 1u;
        }
    }
}

static void fill_raw_meta(const robo_font_metadata_t *meta, robo_font_class_raw_t *raw)
{
    if (!meta) {
        return;
    }
    raw->has_name_meta = 1u;
    raw->name_has_legacy_family =
        (meta->family_name.avail == ROBO_FONT_NAME_PRESENT) ? 1u : 0u;
    raw->name_has_legacy_subfamily =
        (meta->subfamily_name.avail == ROBO_FONT_NAME_PRESENT) ? 1u : 0u;
    raw->name_has_typo_family =
        (meta->typographic_family_name.avail == ROBO_FONT_NAME_PRESENT) ? 1u : 0u;
    raw->name_has_typo_subfamily =
        (meta->typographic_subfamily_name.avail == ROBO_FONT_NAME_PRESENT) ? 1u : 0u;
    raw->name_has_wws_family =
        (meta->wws_family_name.avail == ROBO_FONT_NAME_PRESENT) ? 1u : 0u;
    raw->name_has_wws_subfamily =
        (meta->wws_subfamily_name.avail == ROBO_FONT_NAME_PRESENT) ? 1u : 0u;
}

robo_result_t robo_font_classify(const robo_font_class_raw_t *raw,
                                 robo_font_classification_t *out)
{
    uint16_t fs;
    uint8_t oblique;
    uint8_t italic;
    uint8_t bold_bit;
    uint8_t regular;

    if (!raw || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));
    out->raw = *raw;
    robo_font_style_desc_init(&out->style_desc);

    fs = raw->has_os2 ? raw->fs_selection : 0u;
    out->wws_flag = (raw->has_os2 && (fs & ROBO_FONT_FS_WWS) != 0u) ? 1u : 0u;

    /* ---- Weight ---- */
    if (raw->has_os2 && robo_font_weight_is_valid((robo_font_weight_t)raw->us_weight_class)) {
        out->weight = (robo_font_weight_t)raw->us_weight_class;
        out->weight_source = ROBO_FONT_CLASS_SRC_OS2;
        out->weight_avail = ROBO_FONT_CLASS_PRESENT;
        if ((fs & ROBO_FONT_FS_BOLD) != 0u && out->weight < 600u) {
            out->conflict_bold_bit_vs_weight = 1u;
        }
    } else if (raw->has_os2 && (fs & ROBO_FONT_FS_BOLD) != 0u) {
        out->weight = ROBO_FONT_WEIGHT_BOLD;
        out->weight_source = ROBO_FONT_CLASS_SRC_FS_SELECTION;
        out->weight_avail = ROBO_FONT_CLASS_FALLBACK;
        if (raw->us_weight_class != 0u &&
            !robo_font_weight_is_valid((robo_font_weight_t)raw->us_weight_class)) {
            /* raw invalid preserved; normalized from bold bit */
            (void)0;
        }
    } else if (raw->has_head && (raw->mac_style & ROBO_FONT_MAC_BOLD) != 0u) {
        out->weight = ROBO_FONT_WEIGHT_BOLD;
        out->weight_source = ROBO_FONT_CLASS_SRC_HEAD;
        out->weight_avail = ROBO_FONT_CLASS_FALLBACK;
    } else {
        out->weight = ROBO_FONT_WEIGHT_NORMAL;
        out->weight_source = ROBO_FONT_CLASS_SRC_DEFAULT;
        out->weight_avail = ROBO_FONT_CLASS_DEFAULT;
        if (raw->has_os2 && raw->us_weight_class != 0u &&
            !robo_font_weight_is_valid((robo_font_weight_t)raw->us_weight_class)) {
            out->weight_avail = ROBO_FONT_CLASS_INVALID;
            out->weight_source = ROBO_FONT_CLASS_SRC_OS2;
            /* still expose safe default weight 400 for consumers */
            out->weight = ROBO_FONT_WEIGHT_NORMAL;
        }
    }
    out->weight_category = robo_font_weight_category(out->weight);

    /* ---- Stretch ---- */
    if (raw->has_os2 && robo_font_width_class_is_valid(raw->us_width_class)) {
        out->width_class = (robo_font_width_class_t)raw->us_width_class;
        out->stretch = robo_font_stretch_from_width_class(raw->us_width_class);
        out->stretch_source = ROBO_FONT_CLASS_SRC_OS2;
        out->stretch_avail = ROBO_FONT_CLASS_PRESENT;
    } else {
        out->width_class = ROBO_FONT_WIDTH_NORMAL;
        out->stretch = ROBO_FONT_STRETCH_NORMAL;
        out->stretch_source = ROBO_FONT_CLASS_SRC_DEFAULT;
        out->stretch_avail = ROBO_FONT_CLASS_DEFAULT;
        if (raw->has_os2 && raw->us_width_class != 0u &&
            !robo_font_width_class_is_valid(raw->us_width_class)) {
            out->stretch_avail = ROBO_FONT_CLASS_INVALID;
            out->stretch_source = ROBO_FONT_CLASS_SRC_OS2;
            out->width_class = ROBO_FONT_WIDTH_UNKNOWN;
        }
    }

    /* ---- Style ---- */
    oblique = (raw->has_os2 && (fs & ROBO_FONT_FS_OBLIQUE) != 0u) ? 1u : 0u;
    italic = (raw->has_os2 && (fs & ROBO_FONT_FS_ITALIC) != 0u) ? 1u : 0u;
    bold_bit = (raw->has_os2 && (fs & ROBO_FONT_FS_BOLD) != 0u) ? 1u : 0u;
    regular = (raw->has_os2 && (fs & ROBO_FONT_FS_REGULAR) != 0u) ? 1u : 0u;
    (void)bold_bit;

    if (oblique && italic) {
        out->both_italic_and_oblique = 1u;
    }
    if (regular && (oblique || italic)) {
        out->conflict_regular_and_slope = 1u;
    }

    if (oblique) {
        out->style = ROBO_FONT_STYLE_OBLIQUE;
        out->style_source = ROBO_FONT_CLASS_SRC_OS2;
        out->style_avail = ROBO_FONT_CLASS_PRESENT;
    } else if (italic) {
        out->style = ROBO_FONT_STYLE_ITALIC;
        out->style_source = ROBO_FONT_CLASS_SRC_OS2;
        out->style_avail = ROBO_FONT_CLASS_PRESENT;
    } else if (raw->has_head && (raw->mac_style & ROBO_FONT_MAC_ITALIC) != 0u) {
        out->style = ROBO_FONT_STYLE_ITALIC;
        out->style_source = ROBO_FONT_CLASS_SRC_HEAD;
        out->style_avail = ROBO_FONT_CLASS_FALLBACK;
    } else if (raw->has_post && raw->italic_angle_fixed != 0) {
        out->style = ROBO_FONT_STYLE_OBLIQUE;
        out->style_source = ROBO_FONT_CLASS_SRC_POST;
        out->style_avail = ROBO_FONT_CLASS_FALLBACK;
    } else {
        out->style = ROBO_FONT_STYLE_NORMAL;
        out->style_source = raw->has_os2 ? ROBO_FONT_CLASS_SRC_OS2
                                         : ROBO_FONT_CLASS_SRC_DEFAULT;
        out->style_avail = raw->has_os2 ? ROBO_FONT_CLASS_PRESENT
                                        : ROBO_FONT_CLASS_DEFAULT;
    }

    if (raw->has_post) {
        out->has_italic_angle = 1u;
        out->italic_angle_fixed = raw->italic_angle_fixed;
        out->italic_angle_millidegrees = fixed_to_millidegrees(raw->italic_angle_fixed);
    }

    out->style_desc.weight = out->weight;
    out->style_desc.stretch = out->stretch;
    out->style_desc.style =
        (out->style == ROBO_FONT_STYLE_UNKNOWN) ? ROBO_FONT_STYLE_NORMAL : out->style;
    return ROBO_OK;
}

void robo_font_classification_to_style_desc(const robo_font_classification_t *cls,
                                            robo_font_style_desc_t *out)
{
    if (!out) {
        return;
    }
    robo_font_style_desc_init(out);
    if (!cls) {
        return;
    }
    *out = cls->style_desc;
}

static robo_result_t info_from_raw(const robo_font_class_raw_t *raw,
                                   robo_font_classification_info_t **out)
{
    robo_font_classification_info_t *info;
    robo_result_t res;

    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    info = (robo_font_classification_info_t *)robo_calloc(1, sizeof(*info));
    if (!info) {
        return ROBO_OUT_OF_MEMORY;
    }
    info->refs = 1u;
    res = robo_font_classify(raw, &info->cls);
    if (res != ROBO_OK) {
        robo_free(info);
        return res;
    }
    *out = info;
    return ROBO_OK;
}

robo_result_t robo_font_classification_info_create(
    const robo_font_tt_t *tt,
    const robo_font_stat_t *stat_opt,
    const robo_font_metadata_t *meta_opt,
    robo_font_classification_info_t **out)
{
    robo_font_class_raw_t raw;

    if (!tt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    fill_raw_from_tt(tt, &raw);
    fill_raw_stat(stat_opt, &raw);
    fill_raw_meta(meta_opt, &raw);
    return info_from_raw(&raw, out);
}

robo_result_t robo_font_classification_info_from_sfnt(
    robo_font_sfnt_t *sfnt,
    const robo_font_metadata_t *meta_opt,
    robo_font_classification_info_t **out)
{
    robo_font_tt_t *tt = NULL;
    robo_font_ot_t *ot = NULL;
    const robo_font_stat_t *stat = NULL;
    robo_result_t res;

    if (!sfnt || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    res = robo_font_tt_parse(sfnt, ROBO_FONT_TT_FLAGS_NONE, &tt);
    if (res != ROBO_OK) {
        return res;
    }
    if (robo_font_ot_parse(sfnt, ROBO_FONT_OT_FLAGS_PARSE_EXTRA, &ot) == ROBO_OK && ot) {
        stat = robo_font_ot_stat(ot);
    }
    res = robo_font_classification_info_create(tt, stat, meta_opt, out);
    if (ot) {
        robo_font_ot_release(ot);
    }
    robo_font_tt_release(tt);
    return res;
}

void robo_font_classification_info_retain(robo_font_classification_info_t *info)
{
    if (!info) {
        return;
    }
    info->refs++;
}

void robo_font_classification_info_release(robo_font_classification_info_t *info)
{
    if (!info) {
        return;
    }
    if (info->refs == 0u) {
        return;
    }
    info->refs--;
    if (info->refs != 0u) {
        return;
    }
    robo_free(info);
}

const robo_font_classification_t *robo_font_classification_info_get(
    const robo_font_classification_info_t *info)
{
    return info ? &info->cls : NULL;
}

robo_result_t robo_font_classification_fuzz_parse(const uint8_t *data, size_t size)
{
    robo_font_source_t *src = NULL;
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_classification_info_t *info = NULL;
    robo_font_class_raw_t raw;
    robo_font_classification_t cls;
    robo_result_t res;

    if (!data) {
        return ROBO_INVALID_ARGUMENT;
    }
    /* Always attempt pure-field classify on first bytes as OS/2-ish bag. */
    raw_init_empty(&raw);
    if (size >= 8u) {
        raw.has_os2 = 1u;
        raw.us_weight_class = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
        raw.us_width_class = (uint16_t)(((uint16_t)data[2] << 8) | data[3]);
        raw.fs_selection = (uint16_t)(((uint16_t)data[4] << 8) | data[5]);
        raw.os2_version = (uint16_t)(((uint16_t)data[6] << 8) | data[7]);
    }
    if (size >= 12u) {
        raw.has_head = 1u;
        raw.mac_style = (uint16_t)(((uint16_t)data[8] << 8) | data[9]);
        raw.has_post = 1u;
        raw.italic_angle_fixed =
            (int32_t)(((uint32_t)data[10] << 24) | ((uint32_t)data[11] << 16));
    }
    (void)robo_font_classify(&raw, &cls);

    if (size == 0u) {
        return ROBO_OK;
    }
    res = robo_font_source_create_memory_borrow(data, size, &src);
    if (res != ROBO_OK) {
        return ROBO_OK;
    }
    if (robo_font_sfnt_parse(src, 0u, ROBO_FONT_SFNT_FLAGS_NONE, &sfnt) == ROBO_OK &&
        sfnt) {
        (void)robo_font_classification_info_from_sfnt(sfnt, NULL, &info);
        if (info) {
            robo_font_classification_info_release(info);
        }
        robo_font_sfnt_release(sfnt);
    }
    robo_font_source_release(src);
    return ROBO_OK;
}
