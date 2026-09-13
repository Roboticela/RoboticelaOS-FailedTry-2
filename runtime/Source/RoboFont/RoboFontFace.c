#include "RoboFont/RoboFontFace.h"

#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

struct robo_font_face {
    uint32_t refs;
    robo_font_face_id_t id;
    robo_font_source_t *source;
    uint32_t face_index;
    robo_font_format_t format;
    robo_font_style_desc_t style;
    robo_font_sfnt_t *sfnt;
    robo_font_tt_t *tt;
};

static uint64_t g_next_face_id = 1u;

static robo_font_format_t detect_format(const uint8_t *b, size_t n)
{
    if (!b || n < 4u) {
        return ROBO_FONT_FORMAT_UNKNOWN;
    }
    if ((b[0] == 0x00u && b[1] == 0x01u && b[2] == 0x00u && b[3] == 0x00u) ||
        (b[0] == 't' && b[1] == 'r' && b[2] == 'u' && b[3] == 'e') ||
        (b[0] == 'O' && b[1] == 'T' && b[2] == 'T' && b[3] == 'O') ||
        (b[0] == 't' && b[1] == 't' && b[2] == 'c' && b[3] == 'f')) {
        return ROBO_FONT_FORMAT_SFNT;
    }
    if (n >= 4u && b[0] == 'w' && b[1] == 'O' && b[2] == 'F' && b[3] == 'F') {
        return ROBO_FONT_FORMAT_WOFF;
    }
    if (n >= 4u && b[0] == 'w' && b[1] == 'O' && b[2] == 'F' && b[3] == '2') {
        return ROBO_FONT_FORMAT_WOFF2;
    }
    return ROBO_FONT_FORMAT_UNKNOWN;
}

robo_result_t robo_font_face_create(robo_font_source_t *source,
                                    uint32_t face_index,
                                    robo_font_face_t **out)
{
    robo_font_face_t *f;
    const uint8_t *bytes;
    size_t size;

    if (!out || !source) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = NULL;
    bytes = robo_font_source_bytes(source);
    size = robo_font_source_size(source);
    if (!bytes || size == 0u) {
        return ROBO_INVALID_ARGUMENT;
    }

    f = (robo_font_face_t *)robo_calloc(1, sizeof(*f));
    if (!f) {
        return ROBO_OUT_OF_MEMORY;
    }
    f->refs = 1u;
    f->id = g_next_face_id++;
    if (f->id == 0u) {
        f->id = g_next_face_id++;
    }
    robo_font_source_retain(source);
    f->source = source;
    f->face_index = face_index;
    f->format = detect_format(bytes, size);
    robo_font_style_desc_init(&f->style);
    *out = f;
    return ROBO_OK;
}

void robo_font_face_retain(robo_font_face_t *face)
{
    if (!face) {
        return;
    }
    face->refs++;
}

void robo_font_face_release(robo_font_face_t *face)
{
    if (!face) {
        return;
    }
    if (face->refs == 0u) {
        return;
    }
    face->refs--;
    if (face->refs != 0u) {
        return;
    }
    if (face->tt) {
        robo_font_tt_release(face->tt);
        face->tt = NULL;
    }
    if (face->sfnt) {
        robo_font_sfnt_release(face->sfnt);
        face->sfnt = NULL;
    }
    robo_font_source_release(face->source);
    face->source = NULL;
    robo_free(face);
}

robo_font_face_id_t robo_font_face_id(const robo_font_face_t *face)
{
    return face ? face->id : 0u;
}

robo_font_format_t robo_font_face_format(const robo_font_face_t *face)
{
    return face ? face->format : ROBO_FONT_FORMAT_UNKNOWN;
}

uint32_t robo_font_face_index(const robo_font_face_t *face)
{
    return face ? face->face_index : 0u;
}

const robo_font_source_t *robo_font_face_source(const robo_font_face_t *face)
{
    return face ? face->source : NULL;
}

robo_result_t robo_font_face_style(const robo_font_face_t *face,
                                   robo_font_style_desc_t *out)
{
    if (!face || !out) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out = face->style;
    return ROBO_OK;
}

robo_result_t robo_font_face_glyph_count(const robo_font_face_t *face,
                                         uint32_t *out_count)
{
    if (!face || !out_count) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_count = 0u;
    if (!face->tt) {
        return ROBO_NOT_SUPPORTED;
    }
    *out_count = robo_font_tt_num_glyphs(face->tt);
    return ROBO_OK;
}

robo_result_t robo_font_face_parse_truetype(robo_font_face_t *face, uint32_t tt_flags)
{
    robo_font_sfnt_t *sfnt = NULL;
    robo_font_tt_t *tt = NULL;
    robo_result_t res;

    if (!face) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (face->tt) {
        return ROBO_OK;
    }
    if (face->format != ROBO_FONT_FORMAT_SFNT) {
        return ROBO_NOT_SUPPORTED;
    }

    res = robo_font_sfnt_parse(face->source, face->face_index, ROBO_FONT_SFNT_FLAGS_NONE,
                               &sfnt);
    if (res != ROBO_OK) {
        return res;
    }
    res = robo_font_tt_parse(sfnt, tt_flags, &tt);
    if (res != ROBO_OK) {
        robo_font_sfnt_release(sfnt);
        return res;
    }

    face->sfnt = sfnt;
    face->tt = tt;
    (void)robo_font_tt_style_desc(tt, &face->style);
    return ROBO_OK;
}

const robo_font_tt_t *robo_font_face_truetype(const robo_font_face_t *face)
{
    return face ? face->tt : NULL;
}
