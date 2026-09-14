/*
 * RoboGraphics foundation suite (Goal 6.1).
 * Headless — no desktop / window required.
 */

#include "RoboGraphics/RoboGraphics.h"
#include "RoboMem/RoboMemDebug.h"
#include "RoboTest.h"

#include <string.h>

static void test_gfx_version(void)
{
    ROBO_CHECK_STR_EQ(robo_graphics_version_string(), "0.5.0");
    ROBO_CHECK_EQ_INT(ROBO_GRAPHICS_API_FROZEN, 0);
    ROBO_CHECK_EQ_INT(robo_graphics_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_graphics_init(), ROBO_OK);
    robo_graphics_shutdown();
}

static void test_gfx_geometry(void)
{
    robo_gfx_point_t p;
    robo_gfx_size_t s;
    robo_gfx_rect_t r;
    robo_gfx_point_f_t pf;
    robo_gfx_affine_t a, b, c;

    p = robo_gfx_point(3, 4);
    ROBO_CHECK_EQ_INT(p.x, 3);
    ROBO_CHECK_EQ_INT(p.y, 4);
    s = robo_gfx_size(10, 20);
    ROBO_CHECK(!robo_gfx_size_is_empty(s));
    ROBO_CHECK(robo_gfx_size_is_valid(s));
    ROBO_CHECK(robo_gfx_size_is_empty(robo_gfx_size(0, 5)));

    r = robo_gfx_rect(10, 20, 30, 40);
    ROBO_CHECK_EQ_INT(robo_gfx_rect_right(r), 40);
    ROBO_CHECK_EQ_INT(robo_gfx_rect_bottom(r), 60);
    ROBO_CHECK(robo_gfx_rect_contains_point(r, robo_gfx_point(10, 20)));
    ROBO_CHECK(robo_gfx_rect_contains_point(r, robo_gfx_point(39, 59)));
    ROBO_CHECK(!robo_gfx_rect_contains_point(r, robo_gfx_point(40, 20))); /* exclusive */
    ROBO_CHECK(!robo_gfx_rect_contains_point(r, robo_gfx_point(10, 60)));

    pf = robo_gfx_point_f(1.6f, -1.6f);
    p = robo_gfx_point_f_floor(pf);
    ROBO_CHECK_EQ_INT(p.x, 1);
    ROBO_CHECK_EQ_INT(p.y, -2);
    p = robo_gfx_point_f_round(robo_gfx_point_f(1.5f, 2.4f));
    ROBO_CHECK_EQ_INT(p.x, 2);
    ROBO_CHECK_EQ_INT(p.y, 2);

    robo_gfx_affine_translate(&a, 10.0f, 20.0f);
    robo_gfx_affine_scale(&b, 2.0f, 3.0f);
    ROBO_CHECK_EQ_INT(robo_gfx_affine_multiply(&a, &b, &c), ROBO_OK);
    pf = robo_gfx_affine_map_point(&c, robo_gfx_point_f(1.0f, 1.0f));
    ROBO_CHECK(pf.x > 11.9f && pf.x < 12.1f);
    ROBO_CHECK(pf.y > 22.9f && pf.y < 23.1f);
}

static void test_gfx_color(void)
{
    robo_gfx_color_rgba8_t c8;
    robo_gfx_color_rgba_f_t cf;
    uint32_t packed;

    c8 = robo_gfx_rgb8(10, 20, 30);
    ROBO_CHECK(robo_gfx_color_rgba8_is_opaque(c8));
    ROBO_CHECK(!robo_gfx_color_rgba8_is_transparent(c8));
    ROBO_CHECK_EQ_INT(robo_gfx_color_rgba8_to_f(c8, &cf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_color_rgba_f_to_8(cf, &c8), ROBO_OK);
    ROBO_CHECK_EQ_INT(c8.r, 10);
    packed = robo_gfx_color_rgba8_pack_argb(robo_gfx_rgba8(1, 2, 3, 4));
    c8 = robo_gfx_color_rgba8_unpack_argb(packed);
    ROBO_CHECK_EQ_INT(c8.r, 1);
    ROBO_CHECK_EQ_INT(c8.g, 2);
    ROBO_CHECK_EQ_INT(c8.b, 3);
    ROBO_CHECK_EQ_INT(c8.a, 4);
    ROBO_CHECK_EQ_INT(robo_gfx_color_rgba8_to_f(c8, NULL), ROBO_INVALID_ARGUMENT);
}

static void test_gfx_pixel_format(void)
{
    robo_gfx_pixel_format_info_t info;
    size_t stride = 0u;

    ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_info(ROBO_GFX_FORMAT_RGBA8, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT(info.bytes_per_pixel, 4);
    ROBO_CHECK_EQ_INT(info.has_alpha, 1);
    ROBO_CHECK_STR_EQ(info.name, "RGBA8");
    ROBO_CHECK(robo_gfx_pixel_format_is_known(ROBO_GFX_FORMAT_BGRA8));
    ROBO_CHECK(!robo_gfx_pixel_format_is_known(ROBO_GFX_FORMAT_UNKNOWN));
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_min_stride(ROBO_GFX_FORMAT_RGBA8, 16u, &stride),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)stride, 64);
    ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_min_stride(ROBO_GFX_FORMAT_RGBA8, 0u, &stride),
                      ROBO_INVALID_ARGUMENT);
}

static void test_gfx_surface_owned(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;
    uint8_t *px;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 4;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK(s != NULL);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_width(s), 4);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_height(s), 2);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_ownership(s), (int)ROBO_GFX_SURFACE_OWNED);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_stride(s), 16);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear_rgba8(s, robo_gfx_rgba8(1, 2, 3, 255)),
                      ROBO_OK);
    px = robo_gfx_surface_pixels(s);
    ROBO_CHECK(px != NULL);
    ROBO_CHECK_EQ_INT(px[0], 1);
    ROBO_CHECK_EQ_INT(px[1], 2);
    ROBO_CHECK_EQ_INT(px[2], 3);
    ROBO_CHECK_EQ_INT(px[3], 255);
    robo_gfx_surface_retain(s);
    robo_gfx_surface_release(s);
    robo_gfx_surface_release(s);
}

static void test_gfx_surface_borrowed(void)
{
    uint8_t buf[16];
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;

    memset(buf, 0xAB, sizeof(buf));
    robo_gfx_surface_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.stride_bytes = 8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create_borrowed(&desc, buf, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_surface_ownership(s), (int)ROBO_GFX_SURFACE_BORROWED);
    ROBO_CHECK(robo_gfx_surface_pixels(s) == buf);
    robo_gfx_surface_release(s);
    ROBO_CHECK_EQ_INT(buf[0], 0xAB); /* buffer still caller-owned */
}

static void test_gfx_surface_errors(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 0;
    desc.height = 1;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_INVALID_ARGUMENT);
    desc.width = 1;
    desc.format = ROBO_GFX_FORMAT_RGBA_F32; /* architectural only for CPU create */
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(NULL, &s), ROBO_INVALID_ARGUMENT);
}

static void test_gfx_image_frame(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;
    robo_gfx_image_t *img = NULL;
    robo_gfx_image_desc_t idesc;
    robo_gfx_frame_t frame;

    robo_gfx_surface_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_image_create_from_surface(s, &img), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_gfx_image_kind(img), (int)ROBO_GFX_IMAGE_KIND_RASTER);
    ROBO_CHECK(robo_gfx_image_surface(img) == s);

    robo_gfx_frame_init(&frame);
    ROBO_CHECK_EQ_INT(robo_gfx_frame_begin(&frame, s), ROBO_OK);
    ROBO_CHECK(frame.id != ROBO_GFX_FRAME_ID_NONE);
    ROBO_CHECK_EQ_INT((int)frame.width, 2);
    ROBO_CHECK_EQ_INT(robo_gfx_frame_end(&frame), ROBO_OK);
    robo_gfx_frame_reset(&frame);
    ROBO_CHECK(frame.target == NULL);

    robo_gfx_image_release(img);
    robo_gfx_surface_release(s);

    robo_gfx_image_desc_init(&idesc);
    idesc.width = 8;
    idesc.height = 8;
    ROBO_CHECK_EQ_INT(robo_gfx_image_create_empty(&idesc, &img), ROBO_OK);
    ROBO_CHECK(robo_gfx_image_surface(img) == NULL);
    robo_gfx_image_release(img);
}

static void test_gfx_backend_caps(void)
{
    robo_gfx_backend_info_t info;
    robo_gfx_capabilities_t caps;
    size_t i;
    int found_cpu = 0;

    ROBO_CHECK(robo_gfx_backend_count() >= 1u);
    for (i = 0u; i < robo_gfx_backend_count(); i++) {
        ROBO_CHECK_EQ_INT(robo_gfx_backend_at(i, &info), ROBO_OK);
        if (info.id == ROBO_GFX_BACKEND_CPU_RASTER) {
            found_cpu = 1;
            ROBO_CHECK_EQ_INT(info.available, 1);
        }
    }
    ROBO_CHECK(found_cpu);
    ROBO_CHECK_EQ_INT(robo_gfx_backend_find(ROBO_GFX_BACKEND_GPU, &info), ROBO_OK);
    ROBO_CHECK_EQ_INT(info.available, 0);

    ROBO_CHECK_EQ_INT(robo_gfx_query_capabilities(&caps), ROBO_OK);
    ROBO_CHECK_EQ_INT(caps.cpu_raster_available, 1);
    ROBO_CHECK_EQ_INT(caps.gpu_backend_available, 0);
    ROBO_CHECK(robo_gfx_capabilities_supports_cpu_format(&caps, ROBO_GFX_FORMAT_RGBA8));
    ROBO_CHECK(!robo_gfx_capabilities_supports_cpu_format(&caps, ROBO_GFX_FORMAT_RGBA_F32));
}

static void test_gfx_smoke(void)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *s = NULL;
    robo_gfx_frame_t frame;
    uint8_t *row0;
    const uint8_t expect[4] = {0x11, 0x22, 0x33, 0xFF};

    ROBO_CHECK_EQ_INT(robo_graphics_init(), ROBO_OK);
    robo_gfx_surface_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear_rgba8(s, robo_gfx_rgba8(0x11, 0x22, 0x33, 0xFF)),
                      ROBO_OK);
    row0 = robo_gfx_surface_row(s, 0);
    ROBO_CHECK(row0 != NULL);
    ROBO_CHECK(memcmp(row0, expect, 4) == 0);

    robo_gfx_frame_init(&frame);
    ROBO_CHECK_EQ_INT(robo_gfx_frame_begin(&frame, s), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_frame_end(&frame), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_present_null(s), ROBO_OK);

    robo_gfx_surface_release(s);
    robo_graphics_shutdown();
}

static void test_gfx_chromium_independence(void)
{
    /* Compile-time: only RoboGraphics / RoboBase / RoboMem types in call path. */
    ROBO_CHECK_STR_EQ(robo_graphics_version_string(), "0.5.0");
}

void robo_test_register_graphics(void)
{
    robo_test_register("graphics.version", test_gfx_version);
    robo_test_register("graphics.geometry", test_gfx_geometry);
    robo_test_register("graphics.color", test_gfx_color);
    robo_test_register("graphics.format", test_gfx_pixel_format);
    robo_test_register("graphics.surface.owned", test_gfx_surface_owned);
    robo_test_register("graphics.surface.borrowed", test_gfx_surface_borrowed);
    robo_test_register("graphics.surface.errors", test_gfx_surface_errors);
    robo_test_register("graphics.image.frame", test_gfx_image_frame);
    robo_test_register("graphics.backend.caps", test_gfx_backend_caps);
    robo_test_register("graphics.smoke", test_gfx_smoke);
    robo_test_register("graphics.chromium", test_gfx_chromium_independence);
}
