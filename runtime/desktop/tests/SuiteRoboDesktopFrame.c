/*
 * RoboDesktop frame suite (Phase 11.13) — `desktop.frame.*`.
 *
 * The frame lifecycle and the pixels it produces: the three-call sequence and the one-call shorthand that
 * abbreviates it, the refusals a caller gets for nesting one frame inside another, the order the shell
 * paints in, what each region of the screen holds afterwards, the counters a frame reports, and the one
 * property the whole module is written to keep — **two frames of one state are byte-identical**.
 *
 * The pixel cases read the frame through `robo_desktop_frame_surface` and RoboGraphics' own
 * `robo_gfx_surface_get_pixel`, because that is the door a compositor, a screenshot or a test would use
 * and this module deliberately has no pixel door of its own: reading a pixel is what the graphics layer is
 * for. The colours come from the *theme* (`desktop_theme()`), so a case states "the panel is the panel
 * colour" rather than restating a hex value a restyle would break.
 *
 * The determinism case is the strongest form available: two independently created desktops, each with a
 * window whose contents came from the same markup, rendered and compared **byte for byte** — with the
 * record and the checksum compared beside them, because two pictures that match while their records differ
 * would mean one of the two is describing something else.
 */

#include "RoboDesktopTestSupport.h"

/* ------------------------------------------------------------------ */
/* 1. The lifecycle                                                    */
/* ------------------------------------------------------------------ */

static void test_frame_sequence(void)
{
    robo_desktop_t *desktop = desktop_open(320, 200);
    robo_desktop_frame_record_t record;
    robo_desktop_frame_record_t shorthand;
    robo_desktop_stats_t stats;

    robo_desktop_frame_record_init(&record);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_is_open(desktop), 0);
    ROBO_CHECK_EQ_INT(robo_desktop_paint(desktop, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_end_frame(desktop, &record, NULL), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_desktop_begin_frame(desktop, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_is_open(desktop), 1);
    /* Nested rather than silently accepted: a frame painted twice would composite every translucent
     * thing twice, and the record would only say so if somebody watched the counters. */
    ROBO_CHECK_EQ_INT(robo_desktop_begin_frame(desktop, NULL), ROBO_BUSY);
    ROBO_CHECK_EQ_INT(robo_desktop_paint(desktop, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_paint(desktop, NULL), ROBO_BUSY);
    ROBO_CHECK_EQ_INT(robo_desktop_end_frame(desktop, &record, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_is_open(desktop), 0);
    ROBO_CHECK_EQ_INT(robo_desktop_end_frame(desktop, &record, NULL), ROBO_INVALID_ARGUMENT);

    /* What a desktop with nothing on it painted: the surface, the panel, its separator, two falloff rows
     * and the identity mark -- and no window at all. */
    ROBO_CHECK_EQ_INT((int)record.width, 320);
    ROBO_CHECK_EQ_INT((int)record.height, 200);
    ROBO_CHECK_EQ_INT((int)record.bytes, 320 * 200 * 4);
    ROBO_CHECK_EQ_INT((int)record.windows, 0);
    ROBO_CHECK_EQ_INT((int)record.windows_total, 0);
    ROBO_CHECK_EQ_INT((int)record.content_runs, 0);
    ROBO_CHECK_EQ_INT((int)record.content_pixels, 0);
    ROBO_CHECK_EQ_INT((int)record.fills, 6);
    ROBO_CHECK_EQ_INT((int)record.clipped, 0);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_is_valid(&record), 1);
    ROBO_CHECK(record.checksum != 0u);
    ROBO_CHECK_EQ_INT((int)record.checksum, (int)robo_desktop_frame_checksum(desktop));

    ROBO_CHECK_EQ_INT(robo_desktop_stats(desktop, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)stats.frames, 1);
    ROBO_CHECK_EQ_INT((int)stats.frames_without_windows, 1);
    ROBO_CHECK_EQ_INT((int)stats.fill_drawings, 6);
    ROBO_CHECK_EQ_INT((int)stats.labels_unshaped, 2);

    /* The shorthand is the same frame, and it must not drift from the sequence it abbreviates. */
    ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, &shorthand, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_equal(&record, &shorthand), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_stats(desktop, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)stats.frames, 2);
    ROBO_CHECK_EQ_INT((int)stats.frames_without_windows, 2);

    desktop_close(desktop);
    ROBO_CHECK_EQ_INT(robo_desktop_begin_frame(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_end_frame(NULL, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_paint(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_render(NULL, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_is_open(NULL), 0);
}

/* ------------------------------------------------------------------ */
/* 2. The shell's pixels                                               */
/* ------------------------------------------------------------------ */

static void test_frame_shell_pixels(void)
{
    robo_desktop_t *desktop = desktop_open(320, 200);
    robo_desktop_theme_t theme = desktop_theme();
    robo_desktop_frame_record_t record;

    desktop_fixture();
    ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, &record, NULL), ROBO_OK);

    /* 1. The desktop surface, over the whole screen; the panel over its top band. */
    desktop_pixel_is(desktop, 5, 180, theme.desktop_surface, "desktop surface");
    desktop_pixel_is(desktop, 5, 5, theme.panel_surface, "panel surface");
    /* The panel's last row is the hairline, and the two rows below it are the stepped falloff -- which is
     * a *translucent* pair of rows, so the expected pixel is the graphics module's own source-over blend of
     * the shadow colour over the surface beneath it. Asserting the raw colour would fail, and asserting a
     * hand-computed constant would be a second implementation of the blend. */
    desktop_pixel_is(desktop, 5, 29, theme.panel_separator, "panel separator");
    desktop_pixel_is(desktop, 5, 30,
                     robo_gfx_composite_src_over(theme.shadow_near, theme.desktop_surface),
                     "panel shadow, near row");
    desktop_pixel_is(desktop, 5, 31,
                     robo_gfx_composite_src_over(theme.shadow_far, theme.desktop_surface),
                     "panel shadow, far row");
    /* The identity mark: the shell's one accent, three pixels wide and twelve tall, centred in the panel. */
    desktop_pixel_is(desktop, 15, 15, theme.identity_accent, "identity mark");

    /* The colours are the theme's, so the frame is a function of the palette and nothing else. */
    {
        robo_desktop_theme_t custom = desktop_theme();
        robo_desktop_config_t config;
        robo_desktop_t *themed = NULL;

        custom.desktop_surface = robo_gfx_rgba8(1, 2, 3, 255);
        custom.panel_surface = robo_gfx_rgba8(4, 5, 6, 255);
        robo_desktop_config_init(&config);
        config.width = 64;
        config.height = 48;
        config.theme = &custom;
        ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &themed, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_desktop_render(themed, NULL, NULL), ROBO_OK);
        desktop_pixel_is(themed, 5, 5, custom.panel_surface, "custom panel surface");
        desktop_pixel_is(themed, 5, 45, custom.desktop_surface, "custom desktop surface");
        desktop_close(themed);
    }

    desktop_close(desktop);
}

static void test_frame_window_pixels(void)
{
    robo_desktop_t *desktop = desktop_open(320, 200);
    robo_desktop_theme_t theme = desktop_theme();
    robo_desktop_window_id_t id = desktop_add_window(desktop, "Window", 100, 60, 160, 100);
    robo_desktop_frame_record_t record;
    robo_gfx_rect_t client;

    desktop_fixture();
    ROBO_CHECK_EQ_INT(robo_desktop_window_client_bounds(desktop, id, &client), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, &record, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)record.windows, 1);
    ROBO_CHECK_EQ_INT((int)record.windows_total, 1);

    /* The border is the whole outer rectangle, painted first and covered by the title bar and the body:
     * one pixel of ring without four side fills. */
    desktop_pixel_is(desktop, 100, 60, theme.window_border_focused, "focused window's top border");
    desktop_pixel_is(desktop, 100, 100, theme.window_border_focused, "focused window's left border");
    /* The title bar: one pixel in from the border, twenty-four tall. */
    desktop_pixel_is(desktop, 150, 70, theme.window_title_surface, "window title bar");
    /* The body, which is what a window whose contents could not be drawn still shows. */
    desktop_pixel_is(desktop, client.x, client.y + client.height - 1, theme.window_surface,
                     "window body");
    /* The close button: ten pixels square, six in from the frame's right edge, centred in the title bar --
     * read at its top-left corner rather than its middle, because the middle carries the cross. */
    desktop_pixel_is(desktop, 100 + 160 - 1 - 6 - 10 + 1, 60 + 1 + 7 + 1, theme.window_close,
                     "close button");

    /* An unfocused window is a different border, and that is the shell's whole focus story. */
    {
        (void)desktop_add_window(desktop, "Other", 30, 30, 120, 80);

        ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, &record, NULL), ROBO_OK);
        /* Read on `first`'s top border *outside* `second`, which is the front window: (30, 30, 120, 80)
         * covers everything left of x = 150 and above y = 110. */
        desktop_pixel_is(desktop, 200, 60, theme.window_border, "unfocused window's border");
        ROBO_CHECK_EQ_INT((int)record.windows, 2);
    }

    /* A hidden window is not painted at all: the desktop's own surface shows where it was. */
    ROBO_CHECK_EQ_INT(robo_desktop_window_set_visible(desktop, id, 0, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, &record, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)record.windows, 1);
    ROBO_CHECK_EQ_INT((int)record.windows_total, 2);
    desktop_pixel_is(desktop, 150, 100, theme.desktop_surface, "no window where the hidden one was");

    desktop_close(desktop);
}

static void test_frame_window_content(void)
{
    {
        robo_desktop_t *desktop = desktop_open(320, 240);
        robo_desktop_window_id_t id = desktop_add_window(desktop, "Content", 40, 40, 200, 140);
        robo_desktop_document_t *document = NULL;
        robo_desktop_frame_record_t record;
        robo_desktop_theme_t theme = desktop_theme();
        robo_desktop_stats_t stats;
        robo_gfx_rect_t client;

        desktop_fixture();
        /* A window's contents are a document, and the window's client rectangle is what it is built for. */
        document = desktop_set_window_content(
            desktop, id, "<html><body><div class='card'>Hello</div></body></html>",
            /* The same shape the render scenes' sheets use: an explicit `display: block` for every kind of
             * box the document has, because this build's cascade does not default one, and then the box the
             * picture is about. A sheet without it lays out nothing, which is what the first version of this
             * fixture did and why the case had to be fixed rather than the module. */
            "html, body, div { display: block }"
            "html, body { margin: 0; padding: 0 }"
            ".card { width: 120px; height: 40px; background-color: #0e7aff }",
            NULL);
        ROBO_CHECK_EQ_INT(robo_desktop_window_client_bounds(desktop, id, &client), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, &record, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)record.content_runs, 1);
        ROBO_CHECK_EQ_INT((int)record.content_pixels, client.width * client.height);
        /* Ink landed in the client area: the document's own background is not the window's body. */
        desktop_region_has_ink_is(desktop, client.x, client.y, client.width, client.height,
                                  theme.window_surface, "window contents");

        ROBO_CHECK_EQ_INT(robo_desktop_stats(desktop, &stats), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)stats.content_runs, 1);
        ROBO_CHECK_EQ_INT((int)stats.content_pixels, client.width * client.height);

        /* A window with no contents keeps its body: `content_runs` is 0 and the body is the window
         * colour, which is the state that makes the shell's own smoke test possible. */
        ROBO_CHECK_EQ_INT(robo_desktop_window_set_content(desktop, id, NULL, NULL, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, &record, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)record.content_runs, 0);
        desktop_pixel_is(desktop, client.x + 2, client.y + 2, theme.window_surface,
                         "window body with no contents");

        robo_desktop_document_destroy(document);
        desktop_close(desktop);
    }
}

/* ------------------------------------------------------------------ */
/* 3. Labels                                                           */
/* ------------------------------------------------------------------ */

static void test_frame_labels(void)
{
    robo_desktop_t *fontless = desktop_open(400, 300);
    robo_desktop_frame_record_t record;

    /* No face: the shell counts what it *would* have drawn, which is how a caller learns that its desktop
     * has labels it is not showing. Two labels are asked for -- the identity and the applications; the
     * status label is empty because a caller supplies it -- and both are counted as unshaped. */
    ROBO_CHECK_EQ_INT(robo_desktop_render(fontless, &record, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)record.labels, 0);
    ROBO_CHECK_EQ_INT((int)record.labels_unshaped, 2);
    desktop_close(fontless);

    {
        render_face_t face;
        robo_desktop_t *desktop;

        render_face_create(&face);
        desktop = desktop_open_with_face(400, 300, face.face);
        /* A status label is drawn last within the panel and only when there is one. */
        ROBO_CHECK_EQ_INT(robo_desktop_set_status(desktop, "9:41", NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, &record, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)record.labels, 3);
        ROBO_CHECK_EQ_INT((int)record.labels_unshaped, 0);
        desktop_close(desktop);
        render_face_destroy(&face);
    }
}

/* ------------------------------------------------------------------ */
/* 4. Determinism                                                      */
/* ------------------------------------------------------------------ */

static const char *DETERMINISM_HTML = "<html><body><div class='card'>RoboticelaOS</div></body></html>";
static const char *DETERMINISM_CSS =
    "html, body, div { display: block }"
    "html, body { margin: 0; padding: 0 }"
    ".card { width: 120px; height: 24px; background-color: #0e7aff }";

static void render_scene_desktop(robo_desktop_t *desktop, robo_desktop_window_id_t *out_window,
                                 robo_desktop_document_t **out_document)
{
    robo_desktop_window_id_t id = desktop_add_window(desktop, "Determinism", 20, 40, 200, 120);

    *out_document = desktop_set_window_content(desktop, id, DETERMINISM_HTML, DETERMINISM_CSS, NULL);
    *out_window = id;
    ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, NULL, NULL), ROBO_OK);
}

static void test_frame_determinism(void)
{
    robo_desktop_t *a = desktop_open(240, 180);
    robo_desktop_t *b = desktop_open(240, 180);
    robo_desktop_window_id_t window_a = ROBO_DESKTOP_WINDOW_ID_NONE;
    robo_desktop_window_id_t window_b = ROBO_DESKTOP_WINDOW_ID_NONE;
    robo_desktop_document_t *document_a = NULL;
    robo_desktop_document_t *document_b = NULL;
    size_t size_a = 0u;
    size_t size_b = 0u;
    const uint8_t *bytes_a;
    const uint8_t *bytes_b;
    robo_desktop_frame_record_t record_a;
    robo_desktop_frame_record_t record_b;

    render_scene_desktop(a, &window_a, &document_a);
    render_scene_desktop(b, &window_b, &document_b);
    ROBO_CHECK_EQ_INT((int)window_a, (int)window_b);
    ROBO_CHECK_EQ_INT((int)robo_desktop_frame_checksum(a), (int)robo_desktop_frame_checksum(b));

    bytes_a = robo_desktop_frame_bytes(a, &size_a);
    bytes_b = robo_desktop_frame_bytes(b, &size_b);
    ROBO_CHECK(bytes_a != NULL && bytes_b != NULL);
    ROBO_CHECK_EQ_INT((int)size_a, (int)size_b);
    ROBO_CHECK_EQ_INT((int)size_a, 240 * 180 * 4);
    if (memcmp(bytes_a, bytes_b, size_a) != 0) {
        size_t index;

        for (index = 0u; index < size_a; index++) {
            if (bytes_a[index] != bytes_b[index]) {
                robo_test_fail(__FILE__, __LINE__, "byte %u differs: %u vs %u", (unsigned)index,
                               (unsigned)bytes_a[index], (unsigned)bytes_b[index]);
                break;
            }
        }
    }

    /* The records agree too: two pictures that matched while their records differed would mean one of the
     * two was describing something else. */
    ROBO_CHECK_EQ_INT(robo_desktop_render(a, &record_a, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_render(b, &record_b, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_equal(&record_a, &record_b), 1);
    /* And a second frame of the same state is the same frame, which is the property a checksum exists to
     * let a caller check rather than believe. */
    ROBO_CHECK_EQ_INT((int)robo_desktop_frame_checksum(a), (int)record_a.checksum);

    robo_desktop_document_destroy(document_a);
    robo_desktop_document_destroy(document_b);
    desktop_close(a);
    desktop_close(b);
}

/* ------------------------------------------------------------------ */
/* 5. Resize and the pointer                                           */
/* ------------------------------------------------------------------ */

static void test_frame_resize(void)
{
    robo_desktop_t *desktop = desktop_open(400, 300);
    robo_desktop_window_id_t id = desktop_add_window(desktop, "Resized", 250, 150, 300, 200);
    robo_desktop_stats_t stats;
    robo_gfx_rect_t bounds;
    uint64_t before;

    ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, NULL, NULL), ROBO_OK);
    before = robo_desktop_frame_checksum(desktop);

    /* The sequence the phase asks for: new framebuffer, new geometry, windows revalidated, repaint. */
    ROBO_CHECK_EQ_INT(robo_desktop_resize(desktop, 800, 600, NULL), ROBO_OK);
    /* A resized screen has no previous frame to show: the next frame begins from the target's zero state
     * and paints a whole picture, so the checksum has to have changed. */
    ROBO_CHECK(robo_desktop_frame_checksum(desktop) != before);
    ROBO_CHECK_EQ_INT(robo_desktop_render(desktop, NULL, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_desktop_stats(desktop, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)stats.resizes, 1);

    /* The window is clamped into the *new* work area rather than left where it was. */
    ROBO_CHECK_EQ_INT(robo_desktop_window_bounds(desktop, id, &bounds), ROBO_OK);
    ROBO_CHECK(bounds.x + bounds.width <= 800);
    ROBO_CHECK(bounds.y + bounds.height <= 600);
    ROBO_CHECK(bounds.y >= 30);

    /* A frame that was in flight when the screen changed is abandoned rather than presented as if its
     * record still described the screen. */
    ROBO_CHECK_EQ_INT(robo_desktop_begin_frame(desktop, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_resize(desktop, 500, 400, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_is_open(desktop), 0);
    ROBO_CHECK_EQ_INT(robo_desktop_end_frame(desktop, NULL, NULL), ROBO_INVALID_ARGUMENT);

    /* Following a native window with none attached is a refusal rather than a silent no-op. */
    ROBO_CHECK_EQ_INT(robo_desktop_sync_size(desktop, NULL, NULL), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_desktop_sync_size(NULL, NULL, NULL), ROBO_INVALID_ARGUMENT);

    desktop_close(desktop);
}

static void test_frame_no_window_state(void)
{
    robo_desktop_t *desktop = desktop_open(200, 150);

    /* A desktop with no window is a legitimate desktop: it presents nowhere and says so. */
    ROBO_CHECK_EQ_INT(robo_desktop_present(desktop, NULL, NULL, NULL), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT(robo_desktop_present(NULL, NULL, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_present_to_surface(desktop, NULL, 0, 0, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    desktop_close(desktop);
    ROBO_CHECK(robo_desktop_frame_surface(NULL) == NULL);
    ROBO_CHECK_EQ_INT((int)robo_desktop_frame_checksum(NULL), 0);
    ROBO_CHECK(robo_desktop_frame_bytes(NULL, NULL) == NULL);
}

void robo_test_register_desktop_frame(void)
{
    robo_test_register("desktop.frame.sequence", test_frame_sequence);
    robo_test_register("desktop.frame.shell_pixels", test_frame_shell_pixels);
    robo_test_register("desktop.frame.window_pixels", test_frame_window_pixels);
    robo_test_register("desktop.frame.window_content", test_frame_window_content);
    robo_test_register("desktop.frame.labels", test_frame_labels);
    robo_test_register("desktop.frame.determinism", test_frame_determinism);
    robo_test_register("desktop.frame.resize", test_frame_resize);
    robo_test_register("desktop.frame.no_window_state", test_frame_no_window_state);
}
