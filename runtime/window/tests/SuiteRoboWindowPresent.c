/*
 * SuiteRoboWindowPresent — Goal 7.4 framebuffer presentation.
 *
 * Mock present backend copies pixels for integrity checks only.
 * It does not create or paint a real OS window.
 */

#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsFramebuffer.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboTest.h"
#include "RoboWindow/RoboWindow.h"

#include <string.h>

#define PRESENT_COPY_MAX 65536u

typedef struct present_mock {
    int init_count;
    int shutdown_count;
    int present_count;
    int fail_present;
    int last_width;
    int last_height;
    int last_stride;
    robo_gfx_pixel_format_t last_format;
    robo_window_id_t last_window;
    size_t last_copy_bytes;
    uint8_t last_pixels[PRESENT_COPY_MAX];
} present_mock_t;

static present_mock_t g_pm;
static robo_window_backend_caps_t g_pm_caps;
static robo_window_backend_vtbl_t g_pm_vtbl;

static robo_result_t pm_init(void *ud)
{
    present_mock_t *m = (present_mock_t *)ud;
    if (!m) {
        return ROBO_INVALID_ARGUMENT;
    }
    m->init_count++;
    return ROBO_OK;
}

static void pm_shutdown(void *ud)
{
    present_mock_t *m = (present_mock_t *)ud;
    if (m) {
        m->shutdown_count++;
    }
}

static robo_result_t pm_create(void *ud,
                               robo_window_id_t id,
                               const robo_window_config_t *cfg,
                               robo_window_native_handle_t *out_native)
{
    (void)ud;
    (void)cfg;
    return robo_window_native_handle_assign(out_native, (uintptr_t)id, 1u);
}

static robo_result_t pm_destroy(void *ud,
                                robo_window_id_t id,
                                robo_window_native_handle_t *native)
{
    (void)ud;
    (void)id;
    robo_window_native_handle_invalidate(native);
    return ROBO_OK;
}

static robo_result_t pm_show(void *ud,
                             robo_window_id_t id,
                             robo_window_native_handle_t *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t pm_hide(void *ud,
                             robo_window_id_t id,
                             robo_window_native_handle_t *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t pm_title(void *ud,
                              robo_window_id_t id,
                              robo_window_native_handle_t *native,
                              const char *utf8_title)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)utf8_title;
    return ROBO_OK;
}

static robo_result_t pm_size(void *ud,
                             robo_window_id_t id,
                             robo_window_native_handle_t *native,
                             int32_t width,
                             int32_t height)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)width;
    (void)height;
    return ROBO_OK;
}

static robo_result_t pm_pos(void *ud,
                            robo_window_id_t id,
                            robo_window_native_handle_t *native,
                            int32_t x,
                            int32_t y)
{
    (void)ud;
    (void)id;
    (void)native;
    (void)x;
    (void)y;
    return ROBO_OK;
}

static robo_result_t pm_focus(void *ud,
                              robo_window_id_t id,
                              robo_window_native_handle_t *native)
{
    (void)ud;
    (void)id;
    (void)native;
    return ROBO_OK;
}

static robo_result_t pm_present(void *ud,
                                robo_window_id_t id,
                                robo_window_native_handle_t *native,
                                robo_gfx_surface_t *surface)
{
    present_mock_t *m = (present_mock_t *)ud;
    const uint8_t *src;
    size_t nbytes;

    (void)native;
    if (!m || !surface || !native) {
        return ROBO_INVALID_ARGUMENT;
    }
    if (m->fail_present) {
        return ROBO_IO_ERROR;
    }
    if (!robo_window_native_handle_is_valid(native)) {
        return ROBO_INVALID_ARGUMENT;
    }

    src = robo_gfx_surface_pixels_const(surface);
    if (!src) {
        return ROBO_INVALID_ARGUMENT;
    }
    nbytes = robo_gfx_surface_byte_size(surface);
    m->present_count++;
    m->last_window = id;
    m->last_width = (int)robo_gfx_surface_width(surface);
    m->last_height = (int)robo_gfx_surface_height(surface);
    m->last_stride = (int)robo_gfx_surface_stride(surface);
    m->last_format = robo_gfx_surface_format(surface);
    m->last_copy_bytes = nbytes > PRESENT_COPY_MAX ? PRESENT_COPY_MAX : nbytes;
    memcpy(m->last_pixels, src, m->last_copy_bytes);
    return ROBO_OK;
}

static robo_result_t pm_events(void *ud)
{
    (void)ud;
    return ROBO_OK;
}

static void present_caps_default(void)
{
    memset(&g_pm_caps, 0, sizeof(g_pm_caps));
    g_pm_caps.create_window = 1;
    g_pm_caps.destroy_window = 1;
    g_pm_caps.show_hide = 1;
    g_pm_caps.resize = 1;
    g_pm_caps.move = 1;
    g_pm_caps.set_title = 1;
    g_pm_caps.focus = 1;
    g_pm_caps.present = 1;
    g_pm_caps.process_events = 1;
    g_pm_caps.present_format_mask = ROBO_WINDOW_PRESENT_FMT_CPU_DEFAULT;
    g_pm_caps.present_max_width = 0u;
    g_pm_caps.present_max_height = 0u;
}

static void present_vtbl_bind(void)
{
    memset(&g_pm_vtbl, 0, sizeof(g_pm_vtbl));
    g_pm_vtbl.name = "present_mock";
    g_pm_vtbl.capabilities = &g_pm_caps;
    g_pm_vtbl.initialize = pm_init;
    g_pm_vtbl.shutdown = pm_shutdown;
    g_pm_vtbl.create_window = pm_create;
    g_pm_vtbl.destroy_window = pm_destroy;
    g_pm_vtbl.show = pm_show;
    g_pm_vtbl.hide = pm_hide;
    g_pm_vtbl.set_title = pm_title;
    g_pm_vtbl.set_size = pm_size;
    g_pm_vtbl.set_position = pm_pos;
    g_pm_vtbl.focus = pm_focus;
    g_pm_vtbl.present_surface = pm_present;
    g_pm_vtbl.process_events = pm_events;
}

static void fixture(void)
{
    memset(&g_pm, 0, sizeof(g_pm));
    present_caps_default();
    present_vtbl_bind();
    robo_window_shutdown();
    ROBO_CHECK_EQ_INT(robo_window_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_backend_register(&g_pm_vtbl, &g_pm), ROBO_OK);
}

static void fixture_end(void)
{
    robo_window_shutdown();
}

static robo_result_t make_surface(uint32_t w,
                                  uint32_t h,
                                  robo_gfx_pixel_format_t fmt,
                                  size_t stride,
                                  robo_gfx_surface_t **out)
{
    robo_gfx_surface_desc_t desc;

    robo_gfx_surface_desc_init(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = fmt;
    desc.stride_bytes = stride;
    return robo_gfx_surface_create(&desc, out);
}

static void fill_solid(robo_gfx_surface_t *s, robo_gfx_color_rgba8_t c)
{
    ROBO_CHECK_EQ_INT(robo_gfx_surface_clear_rgba8(s, c), ROBO_OK);
}

static void fill_checker(robo_gfx_surface_t *s)
{
    uint32_t x;
    uint32_t y;
    uint32_t w = robo_gfx_surface_width(s);
    uint32_t h = robo_gfx_surface_height(s);

    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x++) {
            robo_gfx_color_rgba8_t c =
                ((x + y) & 1u) ? robo_gfx_rgba8(255, 255, 255, 255)
                               : robo_gfx_rgba8(0, 0, 0, 255);
            ROBO_CHECK_EQ_INT(robo_gfx_surface_set_pixel(s, x, y, c), ROBO_OK);
        }
    }
}

static void fill_gradient(robo_gfx_surface_t *s)
{
    uint32_t x;
    uint32_t y;
    uint32_t w = robo_gfx_surface_width(s);
    uint32_t h = robo_gfx_surface_height(s);

    for (y = 0u; y < h; y++) {
        for (x = 0u; x < w; x++) {
            uint8_t v = (uint8_t)((x * 255u) / (w ? w : 1u));
            ROBO_CHECK_EQ_INT(
                robo_gfx_surface_set_pixel(s, x, y, robo_gfx_rgba8(v, 0, 0, 255)),
                ROBO_OK);
        }
    }
}

static void test_present_basic(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    robo_window_backend_caps_t caps;

    fixture();
    ROBO_CHECK_EQ_INT(robo_window_backend_capabilities(&caps), ROBO_OK);
    ROBO_CHECK(caps.present);
    ROBO_CHECK(robo_window_present_format_supported(ROBO_GFX_FORMAT_RGBA8));
    ROBO_CHECK(!robo_window_present_format_supported(ROBO_GFX_FORMAT_ALPHA8));

    robo_window_config_init(&cfg);
    cfg.visible = 1;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, 0u, &surf),
                      ROBO_OK);
    fill_solid(surf, robo_gfx_rgba8(10, 20, 30, 255));
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_pm.present_count, 1);
    ROBO_CHECK_EQ_INT(g_pm.last_width, 4);
    ROBO_CHECK_EQ_INT(g_pm.last_height, 4);
    ROBO_CHECK_EQ_INT((int)g_pm.last_format, (int)ROBO_GFX_FORMAT_RGBA8);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[0], 10);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[1], 20);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[2], 30);

    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_pm.present_count, 2);

    {
        robo_gfx_surface_t *big = NULL;
        ROBO_CHECK_EQ_INT(make_surface(8, 2, ROBO_GFX_FORMAT_BGRA8, 0u, &big),
                          ROBO_OK);
        fill_solid(big, robo_gfx_rgba8(1, 2, 3, 4));
        ROBO_CHECK_EQ_INT(robo_window_present(id, big), ROBO_OK);
        ROBO_CHECK_EQ_INT(g_pm.last_width, 8);
        ROBO_CHECK_EQ_INT((int)g_pm.last_format, (int)ROBO_GFX_FORMAT_BGRA8);
        robo_gfx_surface_release(big);
    }

    robo_gfx_surface_release(surf);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
}

static void test_present_validation(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    robo_gfx_surface_t *alpha = NULL;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_window_present((robo_window_id_t)999u, NULL),
                      ROBO_NOT_FOUND);

    ROBO_CHECK_EQ_INT(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, 0u, &surf),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(2, 2, ROBO_GFX_FORMAT_ALPHA8, 0u, &alpha),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, alpha), ROBO_NOT_SUPPORTED);

    g_pm_caps.present_max_width = 1u;
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_NOT_SUPPORTED);
    g_pm_caps.present_max_width = 0u;

    ROBO_CHECK_EQ_INT(robo_window_set_present_surface(id, surf), ROBO_OK);
    robo_gfx_surface_release(surf);
    surf = NULL;
    /* Association cleared after destroy of borrowed pointer: clear explicitly. */
    ROBO_CHECK_EQ_INT(robo_window_set_present_surface(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, NULL), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(1, 1, ROBO_GFX_FORMAT_RGBA8, 0u, &surf),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_NOT_FOUND);
    robo_gfx_surface_release(surf);
    robo_gfx_surface_release(alpha);

    robo_window_backend_clear();
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_NOT_SUPPORTED);
    fixture_end();
}

static void test_present_frame_data(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    uint8_t padded[64];
    robo_gfx_surface_t *strided = NULL;
    robo_gfx_surface_desc_t desc;

    fixture();
    robo_window_config_init(&cfg);
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);

    /* Single pixel */
    ROBO_CHECK_EQ_INT(make_surface(1, 1, ROBO_GFX_FORMAT_RGBA8, 0u, &surf),
                      ROBO_OK);
    fill_solid(surf, robo_gfx_rgba8(9, 8, 7, 6));
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[0], 9);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[3], 6);
    robo_gfx_surface_release(surf);

    /* Solid + checker + gradient */
    ROBO_CHECK_EQ_INT(make_surface(4, 4, ROBO_GFX_FORMAT_RGBA8, 0u, &surf),
                      ROBO_OK);
    fill_solid(surf, robo_gfx_rgba8(100, 0, 0, 255));
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[0], 100);
    fill_checker(surf);
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[0], 0); /* (0,0) black */
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[4], 255); /* (1,0) white R */
    fill_gradient(surf);
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[0], 0);
    robo_gfx_surface_release(surf);

    /* Stride-sensitive: 2×2 RGB8 with stride 8 (>6) */
    memset(padded, 0xCD, sizeof(padded));
    padded[0] = 1;
    padded[1] = 2;
    padded[2] = 3;
    padded[8] = 4;
    padded[9] = 5;
    padded[10] = 6;
    padded[16] = 7;
    padded[17] = 8;
    padded[18] = 9;
    padded[24] = 10;
    padded[25] = 11;
    padded[26] = 12;
    robo_gfx_surface_desc_init(&desc);
    desc.width = 2;
    desc.height = 2;
    desc.format = ROBO_GFX_FORMAT_RGB8;
    desc.stride_bytes = 8u;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create_borrowed(&desc, padded, &strided),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, strided), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_pm.last_stride, 8);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[0], 1);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[8], 4);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[10], 6);
    robo_gfx_surface_release(strided);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
}

static void test_present_framebuffer_api(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_framebuffer_desc_t fb_desc;
    robo_gfx_framebuffer_t *fb = NULL;
    uint8_t *row = NULL;

    fixture();
    robo_window_config_init(&cfg);
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    robo_gfx_framebuffer_desc_init(&fb_desc);
    fb_desc.width = 3;
    fb_desc.height = 1;
    fb_desc.format = ROBO_GFX_FORMAT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_create(&fb_desc, &fb), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_framebuffer_row(fb, 0u, &row), ROBO_OK);
    row[0] = 11;
    row[1] = 22;
    row[2] = 33;
    row[3] = 44;
    ROBO_CHECK_EQ_INT(robo_window_present_framebuffer(id, fb), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_pm.present_count, 1);
    ROBO_CHECK_EQ_INT(g_pm.last_width, 3);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[0], 11);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[3], 44);
    ROBO_CHECK_EQ_INT(robo_window_present_framebuffer(id, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_gfx_framebuffer_release(fb);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
}

static void test_present_backend(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;
    robo_window_backend_caps_t caps;

    fixture();
    ROBO_CHECK_EQ_INT(g_pm.init_count, 1);
    ROBO_CHECK(strcmp(robo_window_backend_name(), "present_mock") == 0);
    ROBO_CHECK_EQ_INT(robo_window_backend_capabilities(&caps), ROBO_OK);
    ROBO_CHECK(caps.present_format_mask & ROBO_WINDOW_PRESENT_FMT_RGBA8);

    robo_window_config_init(&cfg);
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, 0u, &surf),
                      ROBO_OK);
    g_pm.fail_present = 1;
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_IO_ERROR);
    g_pm.fail_present = 0;
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);

    /* Unsupported when present capability cleared */
    g_pm_caps.present = 0;
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_NOT_SUPPORTED);
    g_pm_caps.present = 1;

    robo_gfx_surface_release(surf);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
    ROBO_CHECK(g_pm.shutdown_count >= 1);
}

static void test_present_lifecycle(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;

    fixture();
    robo_window_config_init(&cfg);
    cfg.visible = 0;
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, 0u, &surf),
                      ROBO_OK);
    fill_solid(surf, robo_gfx_rgba8(5, 5, 5, 255));
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_hide(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_pm.present_count, 2);

    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_NOT_FOUND);

    robo_gfx_surface_release(surf);
    fixture_end();
}

static void test_present_threading(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_surface_t *surf = NULL;

    fixture();
    ROBO_CHECK_EQ_INT(robo_window_thread_bind_owner(1u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_thread_enter(1u), ROBO_OK);
    robo_window_config_init(&cfg);
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(1, 1, ROBO_GFX_FORMAT_RGBA8, 0u, &surf),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_window_thread_enter(2u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_PERMISSION_DENIED);
    {
        robo_gfx_framebuffer_t *fb = robo_gfx_surface_framebuffer(surf);
        ROBO_CHECK(fb != NULL);
        ROBO_CHECK_EQ_INT(robo_window_present_framebuffer(id, fb),
                          ROBO_PERMISSION_DENIED);
    }

    ROBO_CHECK_EQ_INT(robo_window_thread_enter(1u), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, surf), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_thread_bind_owner(ROBO_WINDOW_THREAD_NONE),
                      ROBO_OK);
    robo_gfx_surface_release(surf);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
}

static void test_present_associated_surface(void)
{
    robo_window_config_t cfg;
    robo_window_id_t id;
    robo_gfx_surface_t *a = NULL;
    robo_gfx_surface_t *b = NULL;

    fixture();
    robo_window_config_init(&cfg);
    ROBO_CHECK_EQ_INT(robo_window_create(&cfg, &id), ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(2, 1, ROBO_GFX_FORMAT_RGBA8, 0u, &a), ROBO_OK);
    ROBO_CHECK_EQ_INT(make_surface(3, 1, ROBO_GFX_FORMAT_RGBA8, 0u, &b), ROBO_OK);
    fill_solid(a, robo_gfx_rgba8(1, 0, 0, 255));
    fill_solid(b, robo_gfx_rgba8(0, 1, 0, 255));
    ROBO_CHECK_EQ_INT(robo_window_set_present_surface(id, a), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present(id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_pm.last_width, 2);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[0], 1);
    /* Explicit override replaces associated frame for this call. */
    ROBO_CHECK_EQ_INT(robo_window_present(id, b), ROBO_OK);
    ROBO_CHECK_EQ_INT(g_pm.last_width, 3);
    ROBO_CHECK_EQ_INT(g_pm.last_pixels[1], 1);
    robo_gfx_surface_release(a);
    robo_gfx_surface_release(b);
    ROBO_CHECK_EQ_INT(robo_window_destroy(id), ROBO_OK);
    fixture_end();
}

static void test_present_validate_helpers(void)
{
    robo_gfx_surface_t *surf = NULL;
    robo_window_backend_caps_t caps;

    memset(&caps, 0, sizeof(caps));
    caps.present = 1;
    caps.present_format_mask = ROBO_WINDOW_PRESENT_FMT_RGBA8;
    ROBO_CHECK_EQ_INT(robo_window_present_validate_surface(NULL, &caps),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(make_surface(2, 2, ROBO_GFX_FORMAT_RGBA8, 0u, &surf),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_window_present_validate_surface(surf, &caps), ROBO_OK);
    caps.present_format_mask = ROBO_WINDOW_PRESENT_FMT_BGRA8;
    ROBO_CHECK_EQ_INT(robo_window_present_validate_surface(surf, &caps),
                      ROBO_NOT_SUPPORTED);
    robo_gfx_surface_release(surf);
}

void robo_test_register_window_present(void)
{
    robo_test_register("window.present.basic", test_present_basic);
    robo_test_register("window.present.validation", test_present_validation);
    robo_test_register("window.present.frame_data", test_present_frame_data);
    robo_test_register("window.present.framebuffer",
                       test_present_framebuffer_api);
    robo_test_register("window.present.backend", test_present_backend);
    robo_test_register("window.present.lifecycle", test_present_lifecycle);
    robo_test_register("window.present.threading", test_present_threading);
    robo_test_register("window.present.associated",
                       test_present_associated_surface);
    robo_test_register("window.present.validate", test_present_validate_helpers);
}
