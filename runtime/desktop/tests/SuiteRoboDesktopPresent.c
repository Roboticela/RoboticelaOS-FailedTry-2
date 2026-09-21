/*
 * RoboDesktop presentation suite (Phase 11.13) — `desktop.present.*`.
 *
 * Where a frame goes, and the end-to-end proof that it gets there with the right pixels in it. The chain
 * under test is the phase's whole point:
 *
 *     HTML → CSS → layout → paint → display list → rasteriser → a window's contents
 *                                                                    ↓
 *                                            the shell's screen target → a destination surface
 *
 * The destination here is a RoboGraphics surface rather than a native window, and deliberately: a case that
 * created a window would need a display session to check a copy, while `robo_render_present` is the *same*
 * door the window path uses (the desktop's `present` is a thin call of it into RoboWindow's own render
 * surface). What a native window adds is a backend, and the backend is verified where it lives — the QEMU
 * display module's own markers — not by a suite that would have to open one.
 *
 * The clipping cases are the presenter's rule read as assertions: a frame that does not fit its destination
 * is **clipped, not refused**, because a window that shrank between rasterising and presenting must show
 * the part of the picture that still fits. `presented_pixels + clipped_pixels == frame pixels` is asserted
 * as the arithmetic it is.
 */

#include "RoboDesktopTestSupport.h"

static const char *PRESENT_HTML =
    "<html><body><div class='card'>RoboticelaOS</div></body></html>";
static const char *PRESENT_CSS =
    "html, body, div { display: block }"
    "html, body { margin: 0; padding: 0 }"
    ".card { width: 120px; height: 24px; background-color: #0e7aff }";

/* ------------------------------------------------------------------ */
/* 1. The destination                                                  */
/* ------------------------------------------------------------------ */

static void test_present_to_surface(void)
{
    robo_desktop_t *desktop = desktop_open(200, 150);
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;
    robo_render_present_record_t record;
    robo_gfx_color_rgba8_t page = { 0, 0, 0, 255 };
    robo_gfx_color_rgba8_t color;

    desktop_fixture();
    ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, NULL, NULL), ROBO_OK);

    robo_gfx_surface_desc_init(&desc);
    desc.width = 200u;
    desc.height = 150u;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &surface), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_gfx_surface_fill_rect(surface, robo_gfx_rect(0, 0, 200, 150), page), ROBO_OK);

    /* The whole frame into a destination of exactly its size: complete, nothing clipped. */
    robo_render_present_record_init(&record);
    ROBO_CHECK_EQ_INT(robo_desktop_present_to_surface(desktop, surface, 0, 0, &record, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(record.complete, 1);
    ROBO_CHECK_EQ_INT((int)record.presented_pixels, 200 * 150);
    ROBO_CHECK_EQ_INT((int)record.clipped_pixels, 0);
    ROBO_CHECK_EQ_INT((int)record.frame_checksum, (int)robo_desktop_frame_checksum(desktop));
    ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&record), 1);

    /* The pixels landed: the panel's colour is on the destination where the panel is. */
    ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(surface, 5u, 5u, &color), ROBO_OK);
    desktop_pixel_is(desktop, 5, 5, color, "presented panel pixel matches the frame");

    /* A destination smaller than the frame clips rather than refusing: the part that fits is written. */
    {
        robo_gfx_surface_desc_t small;
        robo_gfx_surface_t *smaller = NULL;

        robo_gfx_surface_desc_init(&small);
        small.width = 100u;
        small.height = 40u;
        small.format = ROBO_GFX_FORMAT_RGBA8;
        small.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
        ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&small, &smaller), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_gfx_surface_fill_rect(smaller, robo_gfx_rect(0, 0, 100, 40), page),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_desktop_present_to_surface(desktop, smaller, 0, 0, &record, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(record.complete, 0);
        ROBO_CHECK_EQ_INT((int)record.presented_pixels, 100 * 40);
        /* Exhaustive: the two counts are the frame's pixels. */
        ROBO_CHECK_EQ_INT((int)(record.presented_pixels + record.clipped_pixels), 200 * 150);
        ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&record), 1);
        robo_gfx_surface_release(smaller);
    }

    /* An offset that pushes the frame off the destination entirely: a successful presentation of nothing,
     * described rather than refused. */
    ROBO_CHECK_EQ_INT(robo_desktop_present_to_surface(desktop, surface, -500, 0, &record, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)record.presented_pixels, 0);
    ROBO_CHECK_EQ_INT((int)record.clipped_pixels, 200 * 150);
    ROBO_CHECK_EQ_INT(record.complete, 0);

    /* Presenting twice is presenting twice: the frame is not consumed, cleared or modified. */
    {
        uint64_t before = robo_desktop_frame_checksum(desktop);

        ROBO_CHECK_EQ_INT(robo_desktop_present_to_surface(desktop, surface, 0, 0, NULL, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_desktop_frame_checksum(desktop), (int)before);
    }

    ROBO_CHECK_EQ_INT(robo_desktop_present_to_surface(NULL, surface, 0, 0, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_present_to_surface(desktop, NULL, 0, 0, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_gfx_surface_release(surface);
    desktop_close(desktop);
}

/* ------------------------------------------------------------------ */
/* 2. The shell's own window                                           */
/* ------------------------------------------------------------------ */

static void test_present_needs_a_window(void)
{
    robo_desktop_t *desktop = desktop_open(120, 90);
    int presented = 1;

    /* No native window: the door that presents into one refuses, while the frame itself is real. */
    ROBO_CHECK_EQ_INT(robo_desktop_present(desktop, NULL, &presented, NULL), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(presented, 0);
    ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, NULL, NULL), ROBO_OK);
    ROBO_CHECK(robo_desktop_frame_surface(desktop) != NULL);

    desktop_close(desktop);
}

/* ------------------------------------------------------------------ */
/* 3. End to end                                                       */
/* ------------------------------------------------------------------ */

static void test_present_end_to_end(void)
{
    render_face_t face;
    robo_desktop_t *desktop;
    robo_desktop_document_t *document;
    robo_desktop_window_id_t id;
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *dest = NULL;
    robo_render_present_record_t record;
    robo_desktop_frame_record_t frame;
    robo_gfx_rect_t client;
    robo_desktop_theme_t theme = desktop_theme();

    desktop_fixture();
    render_face_create(&face);
    desktop = desktop_open_with_face(320, 240, face.face);
    id = desktop_add_window(desktop, "Roboticela Demo", 30, 40, 240, 160);

    /* The whole pipeline, in the order the launcher and the guest run it. */
    document = desktop_set_window_content(desktop, id, PRESENT_HTML, PRESENT_CSS, face.face);
    ROBO_CHECK_EQ_INT(robo_desktop_window_client_bounds(desktop, id, &client), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, &frame, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)frame.windows, 1);
    ROBO_CHECK_EQ_INT((int)frame.content_runs, 1);
    ROBO_CHECK_EQ_INT((int)frame.labels >= 3, 1); /* identity, applications, the window's title */
    ROBO_CHECK_EQ_INT((int)frame.labels_unshaped, 0);

    /* The window's contents are in the client area, and the shell's chrome is around them. */
    desktop_region_has_ink_is(desktop, client.x, client.y, client.width, client.height,
                              theme.window_surface, "document ink in the client area");
    desktop_pixel_is(desktop, 30 + 60, 42, theme.window_title_surface, "the window's title bar");

    /* Present the finished frame into a destination surface of the screen's size. */
    robo_gfx_surface_desc_init(&desc);
    desc.width = 320u;
    desc.height = 240u;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;
    ROBO_CHECK_EQ_INT(robo_gfx_surface_create(&desc, &dest), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_present_to_surface(desktop, dest, 0, 0, &record, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(record.complete, 1);
    ROBO_CHECK_EQ_INT((int)record.presented_pixels, 320 * 240);
    ROBO_CHECK_EQ_INT((int)record.presented.width, 320);
    ROBO_CHECK_EQ_INT((int)record.presented.height, 240);

    /* Every pixel of the destination is the frame's, because the frame is opaque and covers the screen. */
    {
        robo_gfx_color_rgba8_t presented;
        robo_gfx_color_rgba8_t in_frame;

        ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dest, 10u, 100u, &presented), ROBO_OK);
        in_frame = desktop_pixel(desktop, 10, 100);
        ROBO_CHECK(desktop_color_is(presented, in_frame));
        ROBO_CHECK_EQ_INT(robo_gfx_surface_get_pixel(dest, (uint32_t)client.x + 4u,
                                                     (uint32_t)client.y + 4u, &presented),
                          ROBO_OK);
        in_frame = desktop_pixel(desktop, (uint32_t)client.x + 4u, (uint32_t)client.y + 4u);
        ROBO_CHECK(desktop_color_is(presented, in_frame));
    }

    /* The checkpoint a person looks at. This is the one case that *asserts* it was written, and for the
     * reason the render module's own checkpoint case gives: the phase's visual-verification requirement is
     * a claim about a file somebody looks at, and a checkpoint that silently failed to be written is a case
     * that believes it verified something. The pixels above are asserted separately, so a filesystem that
     * refused this file would still leave the picture verified -- it would just fail this case loudly. */
    ROBO_CHECK_EQ_INT(desktop_write_png(desktop, desktop_checkpoint_path()), 1);

    robo_gfx_surface_release(dest);
    robo_desktop_document_destroy(document);
    desktop_close(desktop);
    render_face_destroy(&face);
}

void robo_test_register_desktop_present(void)
{
    robo_test_register("desktop.present.to_surface", test_present_to_surface);
    robo_test_register("desktop.present.needs_a_window", test_present_needs_a_window);
    robo_test_register("desktop.present.end_to_end", test_present_end_to_end);
}
