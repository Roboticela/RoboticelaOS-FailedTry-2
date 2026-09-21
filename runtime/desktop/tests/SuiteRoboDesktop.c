/*
 * RoboDesktop module suite (Phase 11.13) — `desktop.*` and `desktop.conformance.*`.
 *
 * The assertions a *release* makes about itself: the version pin, the capability table read positionally,
 * the diagnostic and area vocabularies, the configuration defaults, the lifetime rules, the frame record's
 * own validity, and the dump. They are split from the geometry, window, frame, document and presentation
 * suites beside them for the reason the render module splits its own: those ask "is the picture right?",
 * and these ask "is this the build it says it is?".
 *
 * Two of them are worth pointing at because they are the ones a later phase breaks by accident:
 *
 *   **the capability table** is read **by name**, eighteen fields, and asserts the *absences* as hard as the
 *   presences — a compositor, tiles, damage tracking, a frame scheduler, VSync, a GPU, a window manager,
 *   input delivery and a desktop environment are all 0, as is `documents` (the shell builds no document;
 *   the bridge beside it does). The day one of them becomes 1 the suite has to be edited, which is the
 *   point: a capability that turns on without its own case is a capability nobody checked;
 *
 *   **the dump** is asserted as **seven exact lines** on a fresh desktop. That is deliberately brittle: the
 *   dump is the door a developer reads when nothing else works, and a field that moves between two lines
 *   changes what a diff shows. A case that pattern-matched the text would pass while the layout drifted.
 */

#include "RoboDesktopTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. The module                                                       */
/* ------------------------------------------------------------------ */

static void test_desktop_version_and_capabilities(void)
{
    robo_i32 major = -1;
    robo_i32 minor = -1;
    robo_i32 patch = -1;

    ROBO_CHECK_EQ_INT(robo_desktop_get_version(&major, &minor, &patch), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)major, (int)ROBO_DESKTOP_VERSION_MAJOR);
    ROBO_CHECK_EQ_INT((int)minor, (int)ROBO_DESKTOP_VERSION_MINOR);
    ROBO_CHECK_EQ_INT((int)patch, (int)ROBO_DESKTOP_VERSION_PATCH);
    ROBO_CHECK(strcmp(robo_desktop_version_string(), ROBO_DESKTOP_VERSION_STRING) == 0);
    ROBO_CHECK_EQ_INT(robo_desktop_shell_is_implemented(), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_document_is_implemented(), 1);
    ROBO_CHECK_EQ_INT((int)ROBO_DESKTOP_API_FROZEN, 0);

    ROBO_CHECK_EQ_INT(robo_desktop_get_version(NULL, &minor, &patch), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_get_version(&major, NULL, &patch), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_get_version(&major, &minor, NULL), ROBO_INVALID_ARGUMENT);
}

static void test_desktop_capabilities(void)
{
    robo_desktop_capabilities_t capabilities;
    robo_desktop_capabilities_t cleared;

    robo_desktop_capabilities_init(&cleared);
    ROBO_CHECK_EQ_INT(cleared.shell, 0);
    ROBO_CHECK_EQ_INT(cleared.gpu, 0);
    ROBO_CHECK_EQ_INT(robo_desktop_get_capabilities(NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_get_capabilities(&capabilities), ROBO_OK);

    /* What this build is. */
    ROBO_CHECK_EQ_INT(capabilities.shell, 1);
    ROBO_CHECK_EQ_INT(capabilities.windows, 1);
    ROBO_CHECK_EQ_INT(capabilities.window_content, 1);
    ROBO_CHECK_EQ_INT(capabilities.presentation, 1);
    ROBO_CHECK_EQ_INT(capabilities.frame_records, 1);
    ROBO_CHECK_EQ_INT(capabilities.resize, 1);
    ROBO_CHECK_EQ_INT(capabilities.pointer_targeting, 1);
    ROBO_CHECK_EQ_INT(capabilities.shell_text, 1);
    /* What it deliberately is not: the shell builds no document (the bridge beside it does), and none of
     * the ten structures a compositor, a scheduler, a GPU, a window manager, an input system or a desktop
     * environment would need exist here. */
    ROBO_CHECK_EQ_INT(capabilities.documents, 0);
    ROBO_CHECK_EQ_INT(capabilities.compositor, 0);
    ROBO_CHECK_EQ_INT(capabilities.tiles, 0);
    ROBO_CHECK_EQ_INT(capabilities.damage_tracking, 0);
    ROBO_CHECK_EQ_INT(capabilities.frame_scheduling, 0);
    ROBO_CHECK_EQ_INT(capabilities.vsync, 0);
    ROBO_CHECK_EQ_INT(capabilities.gpu, 0);
    ROBO_CHECK_EQ_INT(capabilities.window_manager, 0);
    ROBO_CHECK_EQ_INT(capabilities.input_delivery, 0);
    ROBO_CHECK_EQ_INT(capabilities.desktop_environment, 0);
}

static void test_desktop_vocabulary(void)
{
    ROBO_CHECK(strcmp(robo_desktop_area_name(ROBO_DESKTOP_AREA_NONE), "none") == 0);
    ROBO_CHECK(strcmp(robo_desktop_area_name(ROBO_DESKTOP_AREA_BACKGROUND), "background") == 0);
    ROBO_CHECK(strcmp(robo_desktop_area_name(ROBO_DESKTOP_AREA_PANEL), "panel") == 0);
    ROBO_CHECK(strcmp(robo_desktop_area_name(ROBO_DESKTOP_AREA_WINDOW_TITLE), "window-title") == 0);
    ROBO_CHECK(strcmp(robo_desktop_area_name(ROBO_DESKTOP_AREA_WINDOW_CONTENT), "window-content") ==
                0);
    ROBO_CHECK(strcmp(robo_desktop_area_name(ROBO_DESKTOP_AREA_WINDOW_CLOSE), "window-close") == 0);
    ROBO_CHECK(strcmp(robo_desktop_area_name((robo_desktop_area_t)99), "unknown") == 0);
    ROBO_CHECK_EQ_INT((int)ROBO_DESKTOP_AREA_COUNT, 6);
    ROBO_CHECK_EQ_INT(robo_desktop_area_is_known(ROBO_DESKTOP_AREA_PANEL), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_area_is_known((robo_desktop_area_t)99), 0);

    ROBO_CHECK(strcmp(robo_desktop_window_state_name(ROBO_DESKTOP_WINDOW_OPEN), "open") == 0);
    ROBO_CHECK(strcmp(robo_desktop_window_state_name(ROBO_DESKTOP_WINDOW_CLOSING), "closing") == 0);
    ROBO_CHECK(strcmp(robo_desktop_window_state_name((robo_desktop_window_state_t)0), "unknown") == 0);
    ROBO_CHECK_EQ_INT(robo_desktop_window_state_is_known(ROBO_DESKTOP_WINDOW_OPEN), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_window_state_is_known((robo_desktop_window_state_t)0), 0);

    ROBO_CHECK_EQ_INT(robo_desktop_window_id_is_none(ROBO_DESKTOP_WINDOW_ID_NONE), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_window_id_is_none(7u), 0);
    ROBO_CHECK_EQ_INT(robo_desktop_window_id_equal(3u, 3u), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_window_id_equal(3u, 4u), 0);

    ROBO_CHECK(strcmp(robo_desktop_domain_name(ROBO_DESKTOP_DOMAIN_NONE), "none") == 0);
    ROBO_CHECK(strcmp(robo_desktop_domain_name(ROBO_DESKTOP_DOMAIN_SHELL), "shell") == 0);
    ROBO_CHECK(strcmp(robo_desktop_domain_name(ROBO_DESKTOP_DOMAIN_WINDOW), "window") == 0);
    ROBO_CHECK(strcmp(robo_desktop_domain_name(ROBO_DESKTOP_DOMAIN_SURFACE), "surface") == 0);
    ROBO_CHECK(strcmp(robo_desktop_domain_name(ROBO_DESKTOP_DOMAIN_DOCUMENT), "document") == 0);
    ROBO_CHECK(strcmp(robo_desktop_domain_name(ROBO_DESKTOP_DOMAIN_DUMP), "dump") == 0);
    ROBO_CHECK(strcmp(robo_desktop_domain_name((robo_desktop_diagnostic_domain_t)77), "unknown") == 0);
    ROBO_CHECK_EQ_INT((int)ROBO_DESKTOP_DOMAIN_COUNT, 6);
}

static void test_desktop_diagnostics(void)
{
    robo_desktop_diag_t diag;
    robo_desktop_t *desktop = NULL;
    robo_desktop_config_t config;

    robo_desktop_diag_reset(&diag);
    ROBO_CHECK_EQ_INT((int)diag.code, (int)ROBO_OK);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_DESKTOP_DOMAIN_NONE);
    ROBO_CHECK_EQ_INT(robo_desktop_diag_set(&diag, ROBO_ERROR, ROBO_DESKTOP_DOMAIN_WINDOW, "x",
                                           __FILE__, 1),
                      ROBO_ERROR);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_DESKTOP_DOMAIN_WINDOW);
    ROBO_CHECK_EQ_INT(robo_desktop_diag_set(&diag, ROBO_OK, ROBO_DESKTOP_DOMAIN_WINDOW, "x",
                                            __FILE__, 1),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_DESKTOP_DOMAIN_NONE);
    ROBO_CHECK_EQ_INT(robo_desktop_diag_set(NULL, ROBO_ERROR, ROBO_DESKTOP_DOMAIN_SHELL, "x", NULL, 0),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_diag_record(NULL, ROBO_ERROR, ROBO_DESKTOP_DOMAIN_SHELL, "x", NULL,
                                               0),
                      ROBO_ERROR);

    desktop_fixture();
    robo_desktop_config_init(&config);
    config.width = 0;
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &desktop, &diag), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_DESKTOP_DOMAIN_SHELL);
    ROBO_CHECK_EQ_INT((int)diag.code, (int)ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(diag.message != NULL);
    ROBO_CHECK(desktop == NULL);
}

static void test_desktop_config_defaults(void)
{
    robo_desktop_config_t config;

    memset(&config, 0xff, sizeof(config));
    robo_desktop_config_init(&config);
    ROBO_CHECK_EQ_INT(config.width, 1280);
    ROBO_CHECK_EQ_INT(config.height, 800);
    ROBO_CHECK_EQ_INT((int)config.window, (int)ROBO_WINDOW_ID_NONE);
    ROBO_CHECK_EQ_INT(config.create_window, 0);
    ROBO_CHECK(config.window_title == NULL && config.identity_text == NULL &&
               config.status_text == NULL && config.applications_text == NULL);
    ROBO_CHECK(config.theme == NULL && config.panel == NULL && config.window_metrics == NULL);
    ROBO_CHECK(config.face == NULL);
    ROBO_CHECK(config.label_size_px > 0.0f);
    robo_desktop_config_init(NULL);
}

/* ------------------------------------------------------------------ */
/* 2. Lifetime                                                         */
/* ------------------------------------------------------------------ */

static void test_desktop_lifecycle(void)
{
    robo_desktop_t *desktop;
    robo_desktop_geometry_t geometry;
    robo_desktop_stats_t stats;
    robo_desktop_pointer_t pointer;

    desktop = desktop_open(1280, 800);
    ROBO_CHECK_EQ_INT(robo_desktop_is_live(desktop), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_is_live(NULL), 0);
    ROBO_CHECK_EQ_INT((int)robo_desktop_attached_window(desktop), (int)ROBO_WINDOW_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_desktop_attached_window(NULL), ROBO_WINDOW_ID_NONE);
    ROBO_CHECK(strcmp(robo_desktop_status(desktop), "") == 0);
    ROBO_CHECK(strcmp(robo_desktop_status(NULL), "") == 0);

    geometry = robo_desktop_geometry_of(desktop);
    ROBO_CHECK_EQ_INT(geometry.screen.width, 1280);
    ROBO_CHECK_EQ_INT(geometry.screen.height, 800);
    ROBO_CHECK_EQ_INT(geometry.panel.height, 30);
    ROBO_CHECK_EQ_INT(geometry.work_area.y, 30);
    ROBO_CHECK_EQ_INT(geometry.work_area.height, 770);

    ROBO_CHECK_EQ_INT(robo_desktop_stats(desktop, &stats), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)stats.frames, 0);
    ROBO_CHECK_EQ_INT((int)stats.presentations, 0);
    ROBO_CHECK_EQ_INT((int)stats.fill_drawings, 0);
    ROBO_CHECK_EQ_INT(robo_desktop_stats(NULL, &stats), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_stats(desktop, NULL), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_desktop_pointer(desktop, &pointer), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)pointer.area, (int)ROBO_DESKTOP_AREA_NONE);
    ROBO_CHECK_EQ_INT(pointer.inside, 0);
    ROBO_CHECK_EQ_INT(robo_desktop_pointer(NULL, &pointer), ROBO_INVALID_ARGUMENT);

    ROBO_CHECK_EQ_INT(robo_desktop_set_status(desktop, "12:34", NULL), ROBO_OK);
    ROBO_CHECK(strcmp(robo_desktop_status(desktop), "12:34") == 0);
    ROBO_CHECK_EQ_INT(robo_desktop_set_status(desktop, "", NULL), ROBO_OK);
    ROBO_CHECK(strcmp(robo_desktop_status(desktop), "") == 0);
    ROBO_CHECK_EQ_INT(robo_desktop_set_status(NULL, "x", NULL), ROBO_INVALID_ARGUMENT);

    desktop_close(desktop);
}

static void test_desktop_refusals(void)
{
    robo_desktop_t *desktop = NULL;
    robo_desktop_config_t config;
    robo_desktop_panel_metrics_t panel;
    robo_desktop_window_metrics_t window_metrics;
    robo_desktop_theme_t theme;
    char too_long[ROBO_DESKTOP_TITLE_MAX + 2u];
    size_t index;

    desktop_fixture();
    robo_desktop_config_init(&config);
    ROBO_CHECK_EQ_INT(robo_desktop_create(NULL, &desktop, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, NULL, NULL), ROBO_INVALID_ARGUMENT);

    config.width = 0;
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &desktop, NULL), ROBO_INVALID_ARGUMENT);
    config.width = -4;
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &desktop, NULL), ROBO_INVALID_ARGUMENT);
    config.width = (int32_t)ROBO_DESKTOP_MAX_DIMENSION + 1;
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &desktop, NULL), ROBO_INVALID_ARGUMENT);
    config.width = 1280;
    config.height = 0;
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &desktop, NULL), ROBO_INVALID_ARGUMENT);
    config.height = 800;

    /* A panel taller than the screen is not a desktop. */
    panel = desktop_panel_metrics();
    panel.height = 900;
    config.panel = &panel;
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &desktop, NULL), ROBO_INVALID_ARGUMENT);
    panel = desktop_panel_metrics();
    panel.height = 0;
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &desktop, NULL), ROBO_INVALID_ARGUMENT);
    config.panel = NULL;

    /* A close button that does not fit the title bar is not drawable. */
    window_metrics = desktop_window_metrics();
    window_metrics.close_size = window_metrics.title_height + 1;
    config.window_metrics = &window_metrics;
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &desktop, NULL), ROBO_INVALID_ARGUMENT);
    config.window_metrics = NULL;

    theme = desktop_theme();
    theme.panel_separator = theme.desktop_surface;
    config.theme = &theme;
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &desktop, NULL), ROBO_OK);
    ROBO_CHECK(desktop != NULL);
    desktop_close(desktop);
    desktop = NULL;
    config.theme = NULL;

    for (index = 0u; index < sizeof(too_long) - 1u; index++) {
        too_long[index] = 'a';
    }
    too_long[sizeof(too_long) - 1u] = '\0';
    config.identity_text = too_long;
    ROBO_CHECK_EQ_INT(robo_desktop_create(&config, &desktop, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK(desktop == NULL);

    /* A NULL is harmless on the way out, like every destroy door in this tree. */
    robo_desktop_destroy(NULL);
}

/* ------------------------------------------------------------------ */
/* 3. Records                                                          */
/* ------------------------------------------------------------------ */

static void test_desktop_frame_record_rules(void)
{
    robo_desktop_frame_record_t record;
    robo_desktop_frame_record_t other;

    robo_desktop_frame_record_init(&record);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_is_valid(&record), 0);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_equal(&record, &record), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_equal(&record, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_is_valid(NULL), 0);

    /* A frame this build could have painted. */
    record.width = 640u;
    record.height = 480u;
    record.bytes = 640u * 480u * 4u;
    record.windows_total = 2u;
    record.windows = 1u;
    record.content_pixels = 100u;
    record.fills = 9u;
    record.checksum = 0x2f1c9e0b7a4d3355u;
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_is_valid(&record), 1);

    other = record;
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_equal(&record, &other), 1);
    other.checksum++;
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_equal(&record, &other), 0);

    /* Each clause of the rule, one at a time: a validity rule is only as good as the cases that reach it. */
    other = record;
    other.width = 0u;
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_is_valid(&other), 0);
    other = record;
    other.width = ROBO_DESKTOP_MAX_DIMENSION + 1u;
    other.bytes = 0u;
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_is_valid(&other), 0);
    other = record;
    other.windows = other.windows_total + 1u;
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_is_valid(&other), 0);
    other = record;
    other.content_pixels = (size_t)record.width * (size_t)record.height + 1u;
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_is_valid(&other), 0);
    other = record;
    other.bytes = record.bytes - 4u;
    ROBO_CHECK_EQ_INT(robo_desktop_frame_record_is_valid(&other), 0);
}

/* ------------------------------------------------------------------ */
/* 4. The dump                                                         */
/* ------------------------------------------------------------------ */

static void test_desktop_dump(void)
{
    robo_desktop_t *desktop = desktop_open(320, 200);
    static const char *const expected =
        "desktop 320x200 panel=0,0,320,30 work=0,30,320,170\n"
        "  windows=0 visible=0 focused=0\n"
        "  frames=0 without_windows=0 resizes=0\n"
        "  fills=0 labels=0 unshaped=0 clipped=0\n"
        "  content_runs=0 content_pixels=0 presentations=0\n"
        "  pointer=0,0 inside=0 pressed=0 area=none\n"
        "  checksum=0000000000000000\n";
    char text[1024];
    size_t needed = 0u;
    size_t index;
    size_t newlines = 0u;

    ROBO_CHECK_EQ_INT(robo_desktop_write(desktop, text, sizeof(text), &needed, NULL), ROBO_OK);
    if (strcmp(text, expected) != 0) {
        robo_test_fail(__FILE__, __LINE__, "the dump is not the seven expected lines:\n%s", text);
    }
    ROBO_CHECK_EQ_INT((int)needed, (int)strlen(expected));
    for (index = 0u; index < strlen(text); index++) {
        if (text[index] == '\n') {
            newlines++;
        }
    }
    ROBO_CHECK_EQ_INT((int)newlines, 7);
    /* No address anywhere in it -- which is what makes two runs of one shell comparable -- and no line
     * longer than the bound `ROBO_DESKTOP_DUMP_LINE_MAX` reserves for one. The address check is the
     * *absence* of a hex-digit run long enough to be one: a `0x` prefix would be found inside "320x200",
     * which is a size rather than a pointer. */
    {
        const char *cursor = text;

        while (*cursor != '\0') {
            size_t line = 0u;

            while (cursor[line] != '\0' && cursor[line] != '\n') {
                line++;
            }
            ROBO_CHECK(line < ROBO_DESKTOP_DUMP_LINE_MAX);
            cursor += line + ((cursor[line] == '\n') ? 1u : 0u);
        }
        ROBO_CHECK(strstr(text, "7f1c9e0b") == NULL);
    }

    /* The needed-length contract: a short buffer reports what it would have taken. */
    needed = 0u;
    ROBO_CHECK_EQ_INT(robo_desktop_write(desktop, text, 8u, &needed, NULL), ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)needed, (int)strlen(expected));
    ROBO_CHECK_EQ_INT(robo_desktop_write(NULL, text, sizeof(text), &needed, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_write(desktop, NULL, sizeof(text), &needed, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_write(desktop, text, sizeof(text), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_desktop_write(desktop, text, 0u, &needed, NULL), ROBO_INVALID_ARGUMENT);

    desktop_close(desktop);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_desktop(void)
{
    robo_test_register("desktop.version_and_capabilities", test_desktop_version_and_capabilities);
    robo_test_register("desktop.conformance.capabilities", test_desktop_capabilities);
    robo_test_register("desktop.conformance.vocabulary", test_desktop_vocabulary);
    robo_test_register("desktop.conformance.diagnostics", test_desktop_diagnostics);
    robo_test_register("desktop.config_defaults", test_desktop_config_defaults);
    robo_test_register("desktop.lifecycle", test_desktop_lifecycle);
    robo_test_register("desktop.lifecycle.refusals", test_desktop_refusals);
    robo_test_register("desktop.frame_record_rules", test_desktop_frame_record_rules);
    robo_test_register("desktop.dump", test_desktop_dump);
}
