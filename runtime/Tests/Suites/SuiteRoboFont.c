/*
 * RoboFont foundation suite (Goal 5.1).
 */

#include "RoboFont/RoboFont.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <string.h>

/* Minimal SFNT-like magic (TrueType scaler type) — not a valid font. */
static const uint8_t k_sfnt_magic[12] = {
    0x00, 0x01, 0x00, 0x00, 'h', 'e', 'a', 'd', 0, 0, 0, 0
};

static void test_font_version(void)
{
    ROBO_CHECK_STR_EQ(robo_font_version_string(), "0.19.0");
    ROBO_CHECK_EQ_INT(robo_font_init(), ROBO_OK);
    robo_font_shutdown();
}

static void test_font_style_validate(void)
{
    robo_font_style_desc_t d;
    robo_font_style_desc_init(&d);
    ROBO_CHECK_EQ_INT(robo_font_style_desc_validate(&d), ROBO_OK);
    ROBO_CHECK(robo_font_weight_is_valid(ROBO_FONT_WEIGHT_BOLD));
    ROBO_CHECK(!robo_font_weight_is_valid(0));
    ROBO_CHECK(!robo_font_weight_is_valid(1001));
    ROBO_CHECK(robo_font_stretch_is_valid(ROBO_FONT_STRETCH_NORMAL));
    ROBO_CHECK(!robo_font_stretch_is_valid(10));
    d.axis_count = 1;
    d.axes[0].tag = 0;
    d.axes[0].value = 400.0f;
    ROBO_CHECK_EQ_INT(robo_font_style_desc_validate(&d), ROBO_INVALID_ARGUMENT);
    d.axes[0].tag = ROBO_FONT_TAG_WGHT;
    d.axes[0].value = 600.0f;
    ROBO_CHECK_EQ_INT(robo_font_style_desc_validate(&d), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_style_desc_validate(NULL), ROBO_INVALID_ARGUMENT);
}

static void test_font_metrics(void)
{
    robo_font_metrics_t m;
    robo_font_bbox_t bb;
    float px = 0.0f;
    robo_font_unit_t u = 0;

    ROBO_CHECK_EQ_INT(robo_font_metrics_init(NULL, 1000, 800, 200, 0, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_font_metrics_init(&m, 0, 800, 200, 0, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_font_metrics_init(&m, 1000, 800, -1, 0, NULL),
                      ROBO_INVALID_ARGUMENT);

    bb.x_min = -100;
    bb.y_min = -200;
    bb.x_max = 1000;
    bb.y_max = 800;
    ROBO_CHECK_EQ_INT(robo_font_metrics_init(&m, 1000, 800, 200, 90, &bb), ROBO_OK);
    ROBO_CHECK_EQ_INT(m.valid, 1);
    ROBO_CHECK_EQ_INT(m.has_bbox, 1);
    ROBO_CHECK_EQ_INT(robo_font_metrics_line_height(&m), 1090);

    ROBO_CHECK_EQ_INT(robo_font_units_to_pixels(500, 1000, 16.0f, &px), ROBO_OK);
    ROBO_CHECK(px > 7.9f && px < 8.1f);
    ROBO_CHECK_EQ_INT(robo_font_pixels_to_units(8.0f, 1000, 16.0f, &u), ROBO_OK);
    ROBO_CHECK_EQ_INT(u, 500);
    ROBO_CHECK(robo_font_points_to_pixels(12.0f) > 15.0f);
}

static void test_font_source_copy(void)
{
    robo_font_source_t *src = NULL;
    uint8_t buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(NULL, 8, &src),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, 0, &src),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(buf, sizeof(buf), &src), ROBO_OK);
    ROBO_CHECK(src != NULL);
    ROBO_CHECK_EQ_INT((int)robo_font_source_kind(src), (int)ROBO_FONT_SOURCE_MEMORY_COPY);
    ROBO_CHECK_EQ_INT((int)robo_font_source_size(src), 8);
    ROBO_CHECK(memcmp(robo_font_source_bytes(src), buf, 8) == 0);

    /* Mutating original must not affect copy. */
    buf[0] = 99;
    ROBO_CHECK_EQ_INT(robo_font_source_bytes(src)[0], 1);

    robo_font_source_retain(src);
    robo_font_source_release(src);
    robo_font_source_release(src);
    robo_font_source_release(NULL);
}

static void test_font_source_borrow(void)
{
    robo_font_source_t *src = NULL;
    uint8_t buf[4] = {0x00, 0x01, 0x00, 0x00};

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_borrow(buf, sizeof(buf), &src), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_source_kind(src), (int)ROBO_FONT_SOURCE_MEMORY_BORROW);
    ROBO_CHECK(robo_font_source_bytes(src) == buf);
    robo_font_source_release(src);
}

static void test_font_face_instance(void)
{
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;
    robo_font_instance_t *inst = NULL;
    robo_font_instance_t *inst2 = NULL;
    robo_font_style_desc_t style;
    robo_font_metrics_t metrics;
    robo_font_metrics_t got;
    float px = 0.0f;
    uint32_t glyphs = 99u;
    robo_font_face_id_t fid;
    robo_font_instance_id_t iid;

    ROBO_CHECK_EQ_INT(
        robo_font_source_create_memory_copy(k_sfnt_magic, sizeof(k_sfnt_magic), &src),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(NULL, 0, &face), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_face_format(face), (int)ROBO_FONT_FORMAT_SFNT);
    ROBO_CHECK_EQ_INT((int)robo_font_face_index(face), 0);
    fid = robo_font_face_id(face);
    ROBO_CHECK(fid != 0u);
    ROBO_CHECK(robo_font_face_source(face) == src);

    /* Source still alive after releasing our local retain (face holds one). */
    robo_font_source_release(src);
    ROBO_CHECK(robo_font_source_size(robo_font_face_source(face)) == sizeof(k_sfnt_magic));

    ROBO_CHECK_EQ_INT(robo_font_face_style(face, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT(style.weight, ROBO_FONT_WEIGHT_NORMAL);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_count(face, &glyphs), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)glyphs, 0);

    robo_font_style_desc_init(&style);
    style.weight = ROBO_FONT_WEIGHT_BOLD;
    ROBO_CHECK_EQ_INT(robo_font_instance_create(face, 0.0f, &style, &inst),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_font_instance_create(face, 16.0f, &style, &inst), ROBO_OK);
    iid = robo_font_instance_id(inst);
    ROBO_CHECK(iid != 0u);
    ROBO_CHECK(robo_font_instance_face(inst) == face);
    ROBO_CHECK(robo_font_instance_pixel_size(inst) == 16.0f);
    ROBO_CHECK_EQ_INT(robo_font_instance_style(inst, &style), ROBO_OK);
    ROBO_CHECK_EQ_INT(style.weight, ROBO_FONT_WEIGHT_BOLD);

    ROBO_CHECK_EQ_INT(robo_font_instance_metrics(inst, &got), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_font_metrics_init(&metrics, 1000, 800, 200, 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_instance_set_metrics(inst, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_instance_metrics(inst, &got), ROBO_OK);
    ROBO_CHECK_EQ_INT(got.units_per_em, 1000);
    ROBO_CHECK_EQ_INT(robo_font_instance_units_to_pixels(inst, 1000, &px), ROBO_OK);
    ROBO_CHECK(px > 15.9f && px < 16.1f);

    /* Face/instance separation: second instance, different size. */
    ROBO_CHECK_EQ_INT(robo_font_instance_create(face, 24.0f, NULL, &inst2), ROBO_OK);
    ROBO_CHECK(robo_font_instance_id(inst2) != iid);
    ROBO_CHECK(robo_font_instance_pixel_size(inst2) == 24.0f);

    /* Axis settings on a non-variable face: validate OK; coords skipped (NOT_FOUND). */
    robo_font_style_desc_init(&style);
    style.axis_count = 1;
    style.axes[0].tag = ROBO_FONT_TAG_WGHT;
    style.axes[0].value = 600.0f;
    {
        robo_font_instance_t *with_axis = NULL;
        ROBO_CHECK_EQ_INT(robo_font_instance_create(face, 12.0f, &style, &with_axis),
                          ROBO_OK);
        ROBO_CHECK(with_axis != NULL);
        ROBO_CHECK(robo_font_instance_variation_coords(with_axis) == NULL);
        ROBO_CHECK_EQ_INT((int)robo_font_instance_metrics_variation_state(with_axis),
                          (int)ROBO_FONT_METRICS_VAR_STATIC);
        robo_font_instance_release(with_axis);
    }

    robo_font_instance_release(inst2);
    robo_font_instance_release(inst);
    robo_font_face_retain(face);
    robo_font_face_release(face);
    robo_font_face_release(face);
}

static void test_font_format_detect(void)
{
    static const uint8_t woff[4] = {'w', 'O', 'F', 'F'};
    static const uint8_t junk[4] = {0xFF, 0xFE, 0xFD, 0xFC};
    robo_font_source_t *src = NULL;
    robo_font_face_t *face = NULL;

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_borrow(woff, 4, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_face_format(face), (int)ROBO_FONT_FORMAT_WOFF);
    robo_font_face_release(face);
    robo_font_source_release(src);

    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_borrow(junk, 4, &src), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_create(src, 0, &face), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_font_face_format(face), (int)ROBO_FONT_FORMAT_UNKNOWN);
    robo_font_face_release(face);
    robo_font_source_release(src);
}

static void test_font_glyph_id(void)
{
    robo_glyph_id_t g = ROBO_GLYPH_ID_NONE;
    ROBO_CHECK_EQ_INT((int)g, 0);
    g = 42u;
    ROBO_CHECK(g != ROBO_GLYPH_ID_NONE);
}

static void test_font_lifetime_null_safe(void)
{
    robo_font_face_retain(NULL);
    robo_font_face_release(NULL);
    robo_font_instance_retain(NULL);
    robo_font_instance_release(NULL);
    ROBO_CHECK(robo_font_face_id(NULL) == 0u);
    ROBO_CHECK(robo_font_instance_face(NULL) == NULL);
}

void robo_test_register_font(void)
{
    robo_test_register("font.version", test_font_version);
    robo_test_register("font.style", test_font_style_validate);
    robo_test_register("font.metrics", test_font_metrics);
    robo_test_register("font.source_copy", test_font_source_copy);
    robo_test_register("font.source_borrow", test_font_source_borrow);
    robo_test_register("font.face_instance", test_font_face_instance);
    robo_test_register("font.format_detect", test_font_format_detect);
    robo_test_register("font.glyph_id", test_font_glyph_id);
    robo_test_register("font.lifetime_null", test_font_lifetime_null_safe);
}
