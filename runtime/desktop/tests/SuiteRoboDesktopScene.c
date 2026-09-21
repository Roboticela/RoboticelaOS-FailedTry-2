/*
 * RoboDesktop's checkpoint-scene suite (Phase 11.13) — `desktop.scene.*`.
 *
 * The scene is what the host launcher and the guest program both drive, so the cases below are about the
 * two things that makes it worth having: it is *one* definition (the words and the arrangement, asserted
 * against the same doors the launchers use), and it is *deterministic* (a step is a function of the
 * scene's state, and two steps of one state produce one picture).
 *
 * `desktop.scene.system_face` is the other half. The OS ships a face — `desktop/assets/Inter.ttf`, packed
 * into the image at `/System/Fonts/Inter.ttf` — and the checkpoint's whole visual claim depends on it
 * parsing and covering the label's glyphs. So that case loads the shipped bytes, asserts what the face is,
 * and then asserts the two counts that are the text verdict in a frame: every label shaped with the face,
 * and every label *counted* without one.
 */

#include "RoboDesktopTestSupport.h"

/* The scene itself, which the support header does not pull in: the support is about the shell and its
 * document, while this suite is about the fixture that puts the two together. */
#include "RoboDesktop/RoboDesktopScene.h"

#if defined(ROBO_FONT_ASSET_PATH_FROM_BUILD)
#include "robo_font_asset_path.h"
#endif
#ifndef ROBO_FONT_ASSET_PATH
#define ROBO_FONT_ASSET_PATH "desktop/assets/Inter.ttf"
#endif

#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* 1. Opening the scene                                                */
/* ------------------------------------------------------------------ */

static void test_scene_open(void)
{
    robo_desktop_scene_config_t config;
    robo_desktop_scene_t *scene = NULL;
    robo_desktop_scene_step_t step;
    robo_desktop_window_id_t window;
    robo_gfx_rect_t client;
    robo_gfx_size_t screen;
    robo_gfx_size_t viewport;
    const robo_desktop_document_t *document;

    desktop_fixture();
    robo_desktop_scene_config_init(NULL); /* a no-op, not a crash */
    robo_desktop_scene_config_init(&config);

    /* The defaults first: a scene with no size is the panel's design size rather than a zero-sized screen,
     * it has no face, and it asks for its own native window. */
    ROBO_CHECK_EQ_INT(config.width, ROBO_DESKTOP_SCENE_DEFAULT_WIDTH);
    ROBO_CHECK_EQ_INT(config.height, ROBO_DESKTOP_SCENE_DEFAULT_HEIGHT);
    ROBO_CHECK(config.face == NULL);
    ROBO_CHECK_EQ_INT(config.create_window, 1);
    config.width = 640;
    config.height = 480;
    config.create_window = 0;

    ROBO_CHECK_EQ_INT(robo_desktop_scene_open(&config, &scene, NULL), ROBO_OK);
    ROBO_CHECK(scene != NULL);
    ROBO_CHECK_EQ_INT(robo_desktop_scene_is_live(scene), 1);

    screen = robo_desktop_screen_size(robo_desktop_scene_desktop(scene));
    ROBO_CHECK_EQ_INT(screen.width, 640);
    ROBO_CHECK_EQ_INT(screen.height, 480);

    /* One window, titled by the header, and it is the focused one. */
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_count(robo_desktop_scene_desktop(scene)), 1);
    window = robo_desktop_scene_window(scene);
    ROBO_CHECK(window != ROBO_DESKTOP_WINDOW_ID_NONE);
    ROBO_CHECK_STR_EQ(robo_desktop_window_title(robo_desktop_scene_desktop(scene), window),
                      ROBO_DESKTOP_SCENE_WINDOW_TITLE);
    ROBO_CHECK_EQ_INT((int)robo_desktop_window_focused(robo_desktop_scene_desktop(scene)), (int)window);

    /* The window sits inside the work area, with the margin the header names on every side. */
    {
        robo_gfx_rect_t work = robo_desktop_work_area(robo_desktop_scene_desktop(scene));
        robo_gfx_rect_t bounds;

        ROBO_CHECK_EQ_INT(robo_desktop_window_bounds(robo_desktop_scene_desktop(scene), window, &bounds),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(bounds.width, work.width - ROBO_DESKTOP_SCENE_MARGIN * 2);
        ROBO_CHECK_EQ_INT(bounds.height, work.height - ROBO_DESKTOP_SCENE_MARGIN * 2);
        ROBO_CHECK(bounds.x >= work.x && bounds.y >= work.y);
    }

    /* The window's document was built for the client size the *shell* computed. */
    document = robo_desktop_scene_document(scene);
    ROBO_CHECK(document != NULL);
    ROBO_CHECK_EQ_INT(robo_desktop_window_client_bounds(robo_desktop_scene_desktop(scene), window,
                                                        &client),
                      ROBO_OK);
    viewport = robo_desktop_document_viewport(document);
    ROBO_CHECK_EQ_INT(viewport.width, client.width);
    ROBO_CHECK_EQ_INT(viewport.height, client.height);

    /* And it paints: one window, its contents rasterised, nothing clipped. */
    ROBO_CHECK_EQ_INT(robo_desktop_scene_step(scene, 0u, &step, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(step.painted, 1);
    ROBO_CHECK_EQ_INT((int)step.frame.windows, 1);
    ROBO_CHECK_EQ_INT((int)step.frame.windows_total, 1);
    ROBO_CHECK_EQ_INT(step.frame.content_runs >= 1u, 1);
    ROBO_CHECK_EQ_INT(step.frame.content_pixels >= 1u, 1);
    ROBO_CHECK_EQ_INT((int)step.frame.clipped, 0);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_is_valid(&step.frame), 1);
    /* A scene with no native window presents nowhere, and says so rather than claiming a presentation. */
    ROBO_CHECK_EQ_INT(step.presented, 0);

    /* NULL is answered, not assumed. */
    ROBO_CHECK_EQ_INT(robo_desktop_scene_open(NULL, &scene, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_scene_open(&config, NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_scene_is_live(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_desktop_scene_window(NULL), ROBO_DESKTOP_WINDOW_ID_NONE);
    ROBO_CHECK(robo_desktop_scene_document(NULL) == NULL);
    ROBO_CHECK(robo_desktop_scene_desktop(NULL) == NULL);
    ROBO_CHECK_EQ_INT(robo_desktop_scene_step(NULL, 0u, &step, NULL), ROBO_INVALID_ARGUMENT);
    robo_desktop_scene_invalidate(NULL); /* a no-op, not a crash */
    robo_desktop_scene_close(NULL);

    /*
     * And the scene is gone. `close` frees it, so the pointer is *dead* rather than "closed" -- the door
     * that answers "is there a scene" is armed against NULL and must never be called on a released one, so
     * what is asserted here is the only thing a caller can still ask: that closing nothing is safe.
     */
    robo_desktop_scene_close(scene);
    robo_desktop_scene_close(NULL);
}

/* ------------------------------------------------------------------ */
/* 2. The scene's own words                                            */
/* ------------------------------------------------------------------ */

/** The class name following `class='` at @p at, or NULL. */
static const char *scene_class_at(const char *at, size_t *out_length)
{
    const char *end;

    if (!at || strncmp(at, "class='", 7u) != 0) {
        return NULL;
    }
    at += 7u;
    end = strchr(at, '\'');
    if (!end) {
        return NULL;
    }
    *out_length = (size_t)(end - at);
    return at;
}

static void test_scene_words(void)
{
    const char *markup = robo_desktop_scene_markup();
    const char *style = robo_desktop_scene_style();
    const char *cursor;
    size_t classes = 0u;

    desktop_fixture();

    ROBO_CHECK(markup != NULL && markup[0] != '\0');
    ROBO_CHECK(style != NULL && style[0] != '\0');
    ROBO_CHECK_EQ_INT((int)strlen(markup), (int)robo_desktop_scene_markup_length());
    /* The document the scene builds is the markup it reports: a launcher that printed the length would
     * otherwise be printing a second copy's length. */
    ROBO_CHECK(robo_desktop_scene_markup_length() > 64u);

    /*
     * Every class the markup names is styled by the sheet beside it. This is the drift check that keeps the
     * two halves of the checkpoint one document: a class renamed in the markup alone would lay out as an
     * unstyled block and still *look* like a page, which is exactly the failure a picture cannot show.
     */
    for (cursor = markup; (cursor = strchr(cursor, 'c')) != NULL; cursor++) {
        size_t length = 0u;
        const char *name;

        if (strncmp(cursor, "class='", 7u) != 0) {
            continue;
        }
        name = scene_class_at(cursor, &length);
        if (!name || length == 0u) {
            continue;
        }
        {
            char selector[64];

            ROBO_CHECK(length + 1u < sizeof(selector));
            selector[0] = '.';
            memcpy(selector + 1, name, length);
            selector[length + 1u] = '\0';
            if (!strstr(style, selector)) {
                robo_test_fail(__FILE__, __LINE__, "the sheet does not style '%s'", selector);
            }
        }
        classes++;
    }
    ROBO_CHECK(classes >= 3u);

    /* The document that came out of those words has the elements they name. */
    {
        robo_desktop_scene_config_t config;
        robo_desktop_scene_t *scene = NULL;
        robo_desktop_document_report_t report;

        robo_desktop_scene_config_init(&config);
        config.width = 800;
        config.height = 600;
        config.create_window = 0;
        ROBO_CHECK_EQ_INT(robo_desktop_scene_open(&config, &scene, NULL), ROBO_OK);
        memset(&report, 0, sizeof(report));
        ROBO_CHECK_EQ_INT(robo_desktop_document_report(robo_desktop_scene_document(scene), &report),
                          ROBO_OK);
        ROBO_CHECK(report.elements >= 5u);
        ROBO_CHECK(report.commands >= 1u);
        robo_desktop_scene_close(scene);
    }

    /* A step record clears to a step that did nothing. */
    {
        robo_desktop_scene_step_t step;

        robo_desktop_scene_step_init(&step);
        ROBO_CHECK_EQ_INT(step.painted, 0);
        ROBO_CHECK_EQ_INT(step.presented, 0);
        ROBO_CHECK_EQ_INT((int)step.events, 0);
        ROBO_CHECK_EQ_INT((int)step.frame.width, 0);
        robo_desktop_scene_step_init(NULL); /* a no-op, not a crash */
    }
}

/* ------------------------------------------------------------------ */
/* 3. Stepping, and determinism                                        */
/* ------------------------------------------------------------------ */

static void test_scene_step(void)
{
    robo_desktop_scene_config_t config;
    robo_desktop_scene_t *scene = NULL;
    robo_desktop_scene_step_t first;
    robo_desktop_scene_step_t idle;
    robo_desktop_scene_step_t second;
    uint64_t checksum;

    desktop_fixture();
    robo_desktop_scene_config_init(&config);
    config.width = 640;
    config.height = 480;
    config.create_window = 0;
    ROBO_CHECK_EQ_INT(robo_desktop_scene_open(&config, &scene, NULL), ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_desktop_scene_step(scene, 0u, &first, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(first.painted, 1);
    checksum = first.frame.checksum;

    /*
     * Nothing happened between the two calls, so the second paints nothing: a scene that repainted an
     * unchanged screen would be a scene that could never be compared frame to frame, which is the property
     * both launchers depend on.
     */
    ROBO_CHECK_EQ_INT(robo_desktop_scene_step(scene, 0u, &idle, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(idle.painted, 0);
    ROBO_CHECK_EQ_INT((int)idle.frame.width, 0);

    /* The same state painted again is the same picture, byte for byte. */
    robo_desktop_scene_invalidate(scene);
    ROBO_CHECK_EQ_INT(robo_desktop_scene_step(scene, 0u, &second, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(second.painted, 1);
    ROBO_CHECK_EQ_INT((int)second.frame.checksum, (int)checksum);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_equal(&first.frame, &second.frame), 1);
    ROBO_CHECK_EQ_INT((int)robo_desktop_frame_checksum(robo_desktop_scene_desktop(scene)),
                      (int)checksum);

    /* A step's record may be omitted: the work still happens. */
    robo_desktop_scene_invalidate(scene);
    ROBO_CHECK_EQ_INT(robo_desktop_scene_step(scene, 0u, NULL, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_desktop_frame_checksum(robo_desktop_scene_desktop(scene)),
                      (int)checksum);

    robo_desktop_scene_close(scene);
}

/* ------------------------------------------------------------------ */
/* 4. The system face                                                  */
/* ------------------------------------------------------------------ */

/** Read @p path whole; returns NULL when it cannot be read. Caller frees. */
static unsigned char *scene_read_file(const char *path, size_t *out_size)
{
    FILE *file = fopen(path, "rb");
    long size;
    unsigned char *bytes;

    *out_size = 0u;
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size <= 0l || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = (unsigned char *)malloc((size_t)size);
    if (!bytes) {
        fclose(file);
        return NULL;
    }
    if (fread(bytes, 1u, (size_t)size, file) != (size_t)size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = (size_t)size;
    return bytes;
}

static void test_scene_system_face(void)
{
    robo_font_source_t *source = NULL;
    robo_font_face_t *face = NULL;
    robo_desktop_scene_config_t config;
    robo_desktop_scene_t *scene = NULL;
    robo_desktop_scene_t *textless = NULL;
    robo_desktop_scene_step_t step;
    robo_font_cmap_lookup_result_t lookup;
    robo_font_metrics_t metrics;
    robo_font_outline_t *outline = NULL;
    unsigned char *bytes;
    size_t size = 0u;
    uint32_t glyphs = 0u;

    desktop_fixture();
    ROBO_CHECK_EQ_INT(robo_font_init(), ROBO_OK);

    /*
     * The shipped face. A missing file is a **failure** here rather than a skip: the image build packs
     * this file and the guest loads it from `/System/Fonts/Inter.ttf`, so a suite that shrugged at its
     * absence would let a checkpoint with no text leave the building.
     */
    bytes = scene_read_file(ROBO_FONT_ASSET_PATH, &size);
    ROBO_CHECK_MSG(bytes != NULL, "desktop/assets/Inter.ttf is unreadable");
    if (!bytes) {
        return;
    }
    ROBO_CHECK(size > 100000u);
    ROBO_CHECK_EQ_INT(robo_font_source_create_memory_copy(bytes, size, &source), ROBO_OK);
    free(bytes);
    ROBO_CHECK_EQ_INT(robo_font_face_create(source, 0, &face), ROBO_OK);
    ROBO_CHECK(face != NULL);

    /*
     * A face is created with nothing parsed, and each door below answers `ROBO_NOT_SUPPORTED` until the
     * part of the font it needs has been built (`RoboFontFace.h`). So the steps come first and in the order
     * the layer above builds them: character map, metrics, outlines -- the same three the document's own
     * text pass asks for (`RoboPaintDecoration.c` parses the cmap for exactly this reason, so that a face
     * handed to the shell is usable by the time the first label is looked up).
     */
    ROBO_CHECK_EQ_INT(robo_font_face_parse_cmap(face), ROBO_OK);

    /* A real face, with real coverage: Inter carries thousands of glyphs. */
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_count(face, &glyphs), ROBO_OK);
    ROBO_CHECK(glyphs > 500u);

    /* Every character the shell's own labels are made of is mapped, and maps to a real glyph. */
    {
        static const char *letters = "RoboticelaOS Applications Welcome 0123456789x";

        for (; *letters; letters++) {
            memset(&lookup, 0, sizeof(lookup));
            ROBO_CHECK_EQ_INT(robo_font_face_lookup_glyph(face, (uint32_t)(unsigned char)*letters,
                                                          &lookup),
                              ROBO_OK);
            if (*letters == ' ') {
                continue; /* a space maps to a blank glyph, which is a legitimate mapping */
            }
            if (lookup.status != ROBO_FONT_CMAP_MAPPED) {
                robo_test_fail(__FILE__, __LINE__, "'%c' is not mapped by the system face", *letters);
            }
        }
    }

    /* Metrics that a layout can measure with, and an outline with ink in it. */
    memset(&metrics, 0, sizeof(metrics));
    ROBO_CHECK_EQ_INT(robo_font_face_parse_metrics(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_layout_metrics(face, &metrics), ROBO_OK);
    ROBO_CHECK_EQ_INT(metrics.valid, 1);
    ROBO_CHECK_EQ_INT(metrics.units_per_em > 0u, 1);
    ROBO_CHECK(metrics.ascent > 0);
    ROBO_CHECK(metrics.descent >= 0);

    memset(&lookup, 0, sizeof(lookup));
    ROBO_CHECK_EQ_INT(robo_font_face_lookup_glyph(face, (uint32_t)'A', &lookup), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)lookup.status, (int)ROBO_FONT_CMAP_MAPPED);
    ROBO_CHECK_EQ_INT(robo_font_face_parse_outlines(face), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_font_face_glyph_outline(face, lookup.glyph_id, &outline), ROBO_OK);
    ROBO_CHECK(outline != NULL);
    ROBO_CHECK(robo_font_outline_contour_count(outline) >= 1u);
    ROBO_CHECK(robo_font_outline_segment_count(outline) >= 1u);
    ROBO_CHECK_EQ_INT(robo_font_outline_has_bounds(outline), 1);
    {
        robo_font_bbox_t bounds;

        memset(&bounds, 0, sizeof(bounds));
        ROBO_CHECK_EQ_INT(robo_font_outline_bounds(outline, &bounds), ROBO_OK);
        ROBO_CHECK(bounds.x_max > bounds.x_min);
        ROBO_CHECK(bounds.y_max > bounds.y_min);
    }
    robo_font_outline_release(outline);
    outline = NULL;

    /* With the face: every label the shell drew was shaped by it. */
    robo_desktop_scene_config_init(&config);
    config.width = 800;
    config.height = 600;
    config.face = face;
    config.create_window = 0;
    config.status_text = "800x600";
    ROBO_CHECK_EQ_INT(robo_desktop_scene_open(&config, &scene, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_scene_step(scene, 0u, &step, NULL), ROBO_OK);
    /* identity, applications, status, and the window's title. */
    ROBO_CHECK_EQ_INT((int)step.frame.labels, 4);
    ROBO_CHECK_EQ_INT((int)step.frame.labels_unshaped, 0);

    /*
     * And without one: nothing is shaped, every label is *counted*, and the frame still paints. The pair of
     * numbers is the whole reason the record carries both.
     */
    config.face = NULL;
    ROBO_CHECK_EQ_INT(robo_desktop_scene_open(&config, &textless, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_scene_step(textless, 0u, &step, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)step.frame.labels, 0);
    ROBO_CHECK_EQ_INT(step.frame.labels_unshaped >= 3u, 1);
    /* The picture is still a desktop: the geometry is there, and only the glyphs are not. */
    ROBO_CHECK_EQ_INT(step.frame.fills >= 5u, 1);

    robo_desktop_scene_close(textless);
    robo_desktop_scene_close(scene);
    robo_font_face_release(face);
    robo_font_source_release(source);
}

/* ------------------------------------------------------------------ */
/* 5. Lifetime                                                         */
/* ------------------------------------------------------------------ */

/**
 * Registered last, and for the reason the case states: it takes the document layers down and brings them
 * back, so nothing may run after it.
 */
static void test_scene_lifetime(void)
{
    robo_desktop_scene_config_t config;
    robo_desktop_scene_t *scene = NULL;
    robo_desktop_scene_step_t step;
    size_t baseline;
    int round;

    desktop_fixture();
    baseline = desktop_live_blocks();

    /* Repeated open/close returns every block it took. */
    for (round = 0; round < 3; round++) {
        robo_desktop_scene_config_init(&config);
        config.width = 480;
        config.height = 360;
        config.create_window = 0;
        ROBO_CHECK_EQ_INT(robo_desktop_scene_open(&config, &scene, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_desktop_scene_step(scene, 0u, &step, NULL), ROBO_OK);
        robo_desktop_scene_close(scene);
    }
    ROBO_CHECK_EQ_INT((int)desktop_live_blocks(), (int)baseline);

    /*
     * A scene needs the layers under it, and says so rather than failing somewhere less useful: with the
     * paint stack down, opening one is refused with a reason.
     */
    robo_paint_shutdown();
    robo_layout_shutdown();
    robo_css_shutdown();
    robo_html_shutdown();
    ROBO_CHECK_EQ_INT(robo_html_is_initialized(), 0);
    robo_desktop_scene_config_init(&config);
    config.width = 320;
    config.height = 240;
    config.create_window = 0;
    ROBO_CHECK(robo_desktop_scene_open(&config, &scene, NULL) != ROBO_OK);
    ROBO_CHECK(scene == NULL);

    /*
     * Restore, and open the same scene again, so a following case would find a working tree. Exactly the
     * four layers this case took down come back -- `robo_base_init` is *not* called, because base was never
     * taken down and a second init is refused (`ROBO_ALREADY_INITIALIZED`), which is the module saying that
     * init is once per program rather than a reference count.
     */
    ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_scene_open(&config, &scene, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_desktop_scene_step(scene, 0u, &step, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(step.painted, 1);
    robo_desktop_scene_close(scene);
    /*
     * No second leak reading here, and deliberately. `desktop_live_blocks` counts *every* live block in the
     * process, and this case is the only one that restarts modules: the number after a restart is a
     * statement about those modules (in a full run it comes back lower, because what they rebuild is not
     * byte-for-byte what they had), not about the scene. The leak reading that is this file's business is
     * the one above, where three open/close round-trips return exactly what they took.
     */
}

void robo_test_register_desktop_scene(void)
{
    robo_test_register("desktop.scene.open", test_scene_open);
    robo_test_register("desktop.scene.words", test_scene_words);
    robo_test_register("desktop.scene.step", test_scene_step);
    robo_test_register("desktop.scene.system_face", test_scene_system_face);
    robo_test_register("desktop.scene.lifetime", test_scene_lifetime);
}
