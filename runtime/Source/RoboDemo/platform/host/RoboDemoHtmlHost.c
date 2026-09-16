/*
 * RoboDemo host launcher (visual checkpoint) — see RoboDemoHost.h.
 *
 * What this runs, end to end, on the real host:
 *
 *   demo HTML source
 *     → RoboHTML tokenizer (8.2)
 *     → RoboDemo page model
 *     → RoboGraphics paint into the window's owned render surface (7.14)
 *     → RoboWindow present → Win32 client area (7.5 / 7.13)
 *
 * It is a developer launcher, not a shipping app: it lives outside the
 * portable runtime because it owns a native window and writes files.
 *
 * Two phases, both reported:
 *   offscreen — render + validate into a plain RoboGraphics surface. Works
 *               with no desktop at all, which is what makes the checkpoint
 *               verifiable in CI.
 *   windowed  — the same page painted into the real native window, with
 *               input delivered through the production observe_* entry
 *               points so hover/press genuinely travel the event loop.
 *
 * Exit codes: 0 verified, 1 rendering/validation failure, 2 usage error.
 */

#include "RoboDemo/RoboDemoHost.h"
#include "RoboDemo/RoboDemoHtml.h"

#include "RoboBase/RoboBase.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayBackendHost.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsScreenshot.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowBackendHost.h"
#include "RoboWindow/RoboWindowGraphics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEMO_OFFSCREEN_W 900
/*
 * 800, not 600: Phase 8.14's form pushes the checkpoint document past 600
 * device pixels, and a checkpoint that clipped its own demonstration would
 * hide exactly the part it exists to show. 800 is also the guest's
 * framebuffer height, so the host window and the QEMU window now show the
 * same amount of the page.
 */
#define DEMO_OFFSCREEN_H 800
#define DEMO_HOLD_DEFAULT_MS 4000u
#define DEMO_FRAME_WAIT_MS 16u
#define DEMO_INJECT_TIMEOUT_MS 1000u

/*
 * Expected colors for the pixel probes. These deliberately mirror the
 * AGENTS.md light-mode tokens that RoboDemoHtml.c paints with: the probe
 * asserts the *documented palette* reached the framebuffer, so a token or
 * painter change that silently shifts color is caught here.
 */
static const robo_gfx_color_rgba8_t P_BG = {245, 245, 247, 255};
static const robo_gfx_color_rgba8_t P_BRAND = {14, 122, 255, 255};
static const robo_gfx_color_rgba8_t P_BRAND_HOVER = {10, 106, 232, 255};
static const robo_gfx_color_rgba8_t P_BRAND_ACTIVE = {7, 87, 199, 255};

typedef struct demo_report {
    int checks;
    int failures;
} demo_report_t;

typedef struct demo_state {
    robo_demo_page_t *page;
    robo_demo_button_state_t button;
    int pressed;
    int hover;
    int need_paint;
    int close_requested;
    size_t key_events;
    size_t mouse_events;
    size_t resize_events;
    size_t clicks;
    /**
     * Pointer → hit test → anchor → activation (Phase 8.13).
     *
     * `link_pressed` is the block index the pointer went down on, or -1, so a
     * click is a press and a release on the *same* link — a release elsewhere
     * is not an activation. The counters exist so the checkpoint reports what
     * really happened rather than asserting a link was clicked because it was
     * drawn.
     */
    int link_pressed;
    size_t link_activations;
    size_t link_fragment_hits;
    size_t link_unsupported;
} demo_state_t;

/* ------------------------------------------------------------------ */
/* Reporting                                                          */
/* ------------------------------------------------------------------ */

/* RGB-only compare: alpha packing differs between RGBA8/BGRA8 targets. */
static int color_rgb_equal(robo_gfx_color_rgba8_t a, robo_gfx_color_rgba8_t b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static void probe_pixel(const robo_gfx_surface_t *surface,
                        uint32_t x,
                        uint32_t y,
                        robo_gfx_color_rgba8_t expect,
                        const char *label,
                        demo_report_t *report)
{
    robo_gfx_color_rgba8_t got;

    report->checks++;
    got.r = 0;
    got.g = 0;
    got.b = 0;
    got.a = 0;
    if (robo_gfx_surface_get_pixel(surface, x, y, &got) != ROBO_OK) {
        report->failures++;
        printf("probe FAIL %-16s (%u,%u) read failed\n", label, x, y);
        return;
    }
    if (!color_rgb_equal(got, expect)) {
        report->failures++;
        printf("probe FAIL %-16s (%u,%u) expect %u,%u,%u got %u,%u,%u\n", label,
               x, y, expect.r, expect.g, expect.b, got.r, got.g, got.b);
        return;
    }
    printf("probe OK   %-16s (%u,%u) = %u,%u,%u\n", label, x, y, got.r, got.g,
           got.b);
}

/**
 * Assert a rectangle contains at least one pixel differing from @p backdrop,
 * i.e. text or a shape actually landed there. Cheaper and more meaningful
 * than comparing a whole region against a golden image at this stage.
 */
static void probe_ink(const robo_gfx_surface_t *surface,
                      robo_gfx_rect_t rect,
                      robo_gfx_color_rgba8_t backdrop,
                      const char *label,
                      demo_report_t *report)
{
    int32_t x;
    int32_t y;

    report->checks++;
    for (y = rect.y; y < rect.y + rect.height; y++) {
        for (x = rect.x; x < rect.x + rect.width; x++) {
            robo_gfx_color_rgba8_t got;
            if (x < 0 || y < 0) {
                continue;
            }
            if (robo_gfx_surface_get_pixel(surface, (uint32_t)x, (uint32_t)y,
                                           &got) != ROBO_OK) {
                continue;
            }
            if (!color_rgb_equal(got, backdrop)) {
                printf("probe OK   %-16s ink at (%d,%d)\n", label, x, y);
                return;
            }
        }
    }
    report->failures++;
    printf("probe FAIL %-16s no ink in %dx%d at %d,%d\n", label, rect.width,
           rect.height, rect.x, rect.y);
}

static int write_screenshot(const robo_gfx_surface_t *surface, const char *path)
{
    robo_gfx_capture_options_t copts;
    robo_gfx_encode_options_t eopts;
    robo_gfx_encoded_buffer_t buf;
    robo_result_t r;
    FILE *f;

    robo_gfx_capture_options_init(&copts);
    copts.region_policy = ROBO_GFX_CAPTURE_REGION_CLIP;
    copts.use_full_extent = 1;
    robo_gfx_encode_options_init(&eopts);
    eopts.container = ROBO_GFX_ENCODE_PNG;
    eopts.layout = ROBO_GFX_ENCODE_LAYOUT_RGBA8;

    buf.data = NULL;
    buf.size = 0u;
    r = robo_gfx_surface_export_screenshot(surface, &copts, &eopts, &buf);
    if (r != ROBO_OK) {
        printf("screenshot: export failed (%d)\n", (int)r);
        return 0;
    }
    f = fopen(path, "wb");
    if (!f) {
        printf("screenshot: cannot open %s\n", path);
        robo_gfx_encoded_buffer_release(&buf);
        return 0;
    }
    if (fwrite(buf.data, 1u, buf.size, f) != buf.size) {
        printf("screenshot: short write to %s\n", path);
        fclose(f);
        robo_gfx_encoded_buffer_release(&buf);
        return 0;
    }
    fclose(f);
    printf("screenshot: %s (%lu bytes)\n", path, (unsigned long)buf.size);
    robo_gfx_encoded_buffer_release(&buf);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Page + paint                                                       */
/* ------------------------------------------------------------------ */

static robo_demo_button_state_t state_button(const demo_state_t *st)
{
    if (st->pressed) {
        return ROBO_DEMO_BUTTON_PRESSED;
    }
    return st->hover ? ROBO_DEMO_BUTTON_HOVER : ROBO_DEMO_BUTTON_NORMAL;
}

static robo_gfx_color_rgba8_t expected_button_fill(robo_demo_button_state_t s)
{
    switch (s) {
    case ROBO_DEMO_BUTTON_HOVER:
        return P_BRAND_HOVER;
    case ROBO_DEMO_BUTTON_PRESSED:
        return P_BRAND_ACTIVE;
    default:
        return P_BRAND;
    }
}

static void report_page(const robo_demo_page_t *page)
{
    printf("page:  title=\"%s\" blocks=%lu supported=%lu ignored=%lu diagnostics=%lu\n",
           robo_demo_page_title(page),
           (unsigned long)robo_demo_page_block_count(page),
           (unsigned long)robo_demo_page_supported_element_count(page),
           (unsigned long)robo_demo_page_ignored_tag_count(page),
           (unsigned long)robo_demo_page_diagnostic_count(page));
}

/** Validate a freshly painted frame against the documented palette/layout. */
static void validate_frame(const robo_demo_page_t *page,
                           const robo_gfx_surface_t *surface,
                           robo_demo_button_state_t button,
                           const robo_demo_paint_stats_t *stats,
                           demo_report_t *report)
{
    uint32_t w = robo_gfx_surface_width(surface);
    uint32_t h = robo_gfx_surface_height(surface);
    robo_gfx_rect_t button_rect;
    size_t i;

    printf("paint: blocks=%lu rects=%lu lines=%lu text_runs=%lu\n",
           (unsigned long)stats->blocks_painted,
           (unsigned long)stats->rects_painted,
           (unsigned long)stats->lines_painted,
           (unsigned long)stats->text_runs_painted);

    probe_pixel(surface, w / 2u, 2u, P_BRAND, "accent-bar", report);
    probe_pixel(surface, w - 4u, h - 4u, P_BG, "page-background", report);

    if (robo_demo_page_button_rect(page, &button_rect)) {
        /* Probe clear of the centred label so every button state reads fill. */
        probe_pixel(surface, (uint32_t)(button_rect.x + 8),
                    (uint32_t)(button_rect.y + button_rect.height / 2),
                    expected_button_fill(button), "button-fill", report);
        probe_pixel(surface, (uint32_t)button_rect.x,
                    (uint32_t)button_rect.y, P_BRAND_ACTIVE, "button-border",
                    report);
    } else {
        report->checks++;
        report->failures++;
        printf("probe FAIL button rect unavailable\n");
    }

    for (i = 0u; i < robo_demo_page_block_count(page); i++) {
        robo_demo_block_style_t style;
        robo_gfx_rect_t rect;
        const char *label;

        if (robo_demo_page_block_style(page, i, &style) != ROBO_OK) {
            continue;
        }
        if (!robo_demo_page_block_rect(page, i, &rect)) {
            continue;
        }
        switch (style) {
        case ROBO_DEMO_BLOCK_HEADING:
            label = "heading-ink";
            break;
        case ROBO_DEMO_BLOCK_PARAGRAPH:
            label = "paragraph-ink";
            break;
        case ROBO_DEMO_BLOCK_LINK:
            label = "link-ink";
            break;
        default:
            continue; /* the button is covered by its own probes */
        }
        probe_ink(surface, rect, P_BG, label, report);
    }
}

/* ------------------------------------------------------------------ */
/* Event loop                                                         */
/* ------------------------------------------------------------------ */

/**
 * Activate link block @p index and record what the module decided.
 *
 * No handler is installed, and that is the point of the checkpoint: a route
 * or a permitted URI must come back UNSUPPORTED here rather than reported as
 * navigated, because nothing in this build can navigate one. The fragment
 * link is the one navigation that really completes, and its outcome says so.
 */
static void demo_activate_link(demo_state_t *st, size_t index)
{
    robo_html_navigation_outcome_t outcome;
    robo_result_t r;

    memset(&outcome, 0, sizeof(outcome));
    r = robo_demo_page_activate_link(st->page, index, NULL, &outcome, NULL);
    st->link_activations++;
    if (r != ROBO_OK) {
        printf("link: block %lu activation failed code=%d\n",
               (unsigned long)index, (int)r);
        robo_html_navigation_outcome_clear(&outcome);
        return;
    }
    switch (outcome.kind) {
    case ROBO_HTML_NAVIGATION_FRAGMENT:
        st->link_fragment_hits++;
        break;
    case ROBO_HTML_NAVIGATION_UNSUPPORTED:
        st->link_unsupported++;
        break;
    default:
        break;
    }
    printf("link: block %lu href=\"%s\" outcome=%s resolved=\"%s\"\n",
           (unsigned long)index,
           robo_demo_page_block_href(st->page, index),
           robo_html_navigation_outcome_kind_name(outcome.kind),
           outcome.resolved ? outcome.resolved : "");
    robo_html_navigation_outcome_clear(&outcome);
}

static void handle_event(demo_state_t *st, const robo_window_event_t *ev)
{
    switch (ev->type) {
    case ROBO_WINDOW_EVENT_CLOSE_REQUESTED:
    case ROBO_WINDOW_EVENT_CLOSED:
        st->close_requested = 1;
        break;
    case ROBO_WINDOW_EVENT_RESIZED:
    case ROBO_WINDOW_EVENT_SCALE_CHANGED:
        st->resize_events++;
        st->need_paint = 1;
        break;
    case ROBO_WINDOW_EVENT_KEY_DOWN:
    case ROBO_WINDOW_EVENT_KEY_UP:
        st->key_events++;
        break;
    case ROBO_WINDOW_EVENT_MOUSE_MOVE: {
        robo_gfx_point_t p;
        int hover;

        st->mouse_events++;
        p.x = ev->mouse.client_x;
        p.y = ev->mouse.client_y;
        hover = robo_demo_page_hit_button(st->page, p);
        if (st->hover == hover) {
            break;
        }
        st->hover = hover;
        st->button = state_button(st);
        st->need_paint = 1;
        break;
    }
    case ROBO_WINDOW_EVENT_MOUSE_BUTTON_DOWN: {
        robo_gfx_point_t p;
        size_t link_index = 0u;

        st->mouse_events++;
        if (ev->mouse.button != ROBO_MOUSE_BUTTON_LEFT) {
            break;
        }
        p.x = ev->mouse.client_x;
        p.y = ev->mouse.client_y;
        /* Links are hit-tested first: they are the only content here that a
         * click can *do* something with, and the button sits below them. */
        if (robo_demo_page_hit_link(st->page, p, &link_index)) {
            st->link_pressed = (int)link_index;
            break;
        }
        if (!robo_demo_page_hit_button(st->page, p)) {
            break;
        }
        st->pressed = 1;
        st->button = state_button(st);
        st->need_paint = 1;
        break;
    }
    case ROBO_WINDOW_EVENT_MOUSE_BUTTON_UP: {
        robo_gfx_point_t p;
        size_t link_index = 0u;

        st->mouse_events++;
        if (ev->mouse.button != ROBO_MOUSE_BUTTON_LEFT) {
            break;
        }
        p.x = ev->mouse.client_x;
        p.y = ev->mouse.client_y;
        if (st->link_pressed >= 0 &&
            robo_demo_page_hit_link(st->page, p, &link_index) &&
            (int)link_index == st->link_pressed) {
            demo_activate_link(st, link_index);
            st->link_pressed = -1;
            break;
        }
        st->link_pressed = -1;
        if (st->pressed && robo_demo_page_hit_button(st->page, p)) {
            st->clicks++;
        }
        st->pressed = 0;
        st->button = state_button(st);
        st->need_paint = 1;
        break;
    }
    case ROBO_WINDOW_EVENT_MOUSE_WHEEL:
        st->mouse_events++;
        break;
    default:
        break;
    }
}

/**
 * Run the pointer → hit test → anchor → activation seam over every link
 * block, offscreen and deterministically (Phase 8.13).
 *
 * No navigation handler is installed, which is what makes this a real test
 * of the honesty rule rather than of the happy path: a route must report
 * UNSUPPORTED and a fragment of this document must report FRAGMENT, and
 * neither may be reported as navigation that happened.
 */
static void probe_links(robo_demo_page_t *page, demo_report_t *report)
{
    size_t activated = 0u;
    size_t fragments = 0u;
    size_t unsupported = 0u;
    size_t i;

    report->checks++;
    if (robo_demo_page_link_count(page) == 0u) {
        report->failures++;
        printf("probe FAIL link-seam  the checkpoint document has no links\n");
        return;
    }

    for (i = 0u; i < robo_demo_page_block_count(page); i++) {
        robo_demo_block_style_t style;
        robo_gfx_rect_t rect;
        robo_gfx_point_t p;
        robo_html_navigation_outcome_t outcome;
        size_t hit = 0u;
        robo_result_t r;

        if (robo_demo_page_block_style(page, i, &style) != ROBO_OK ||
            style != ROBO_DEMO_BLOCK_LINK ||
            !robo_demo_page_block_rect(page, i, &rect)) {
            continue;
        }
        p.x = rect.x + rect.width / 2;
        p.y = rect.y + rect.height / 2;

        report->checks++;
        if (!robo_demo_page_hit_link(page, p, &hit) || hit != i) {
            report->failures++;
            printf("probe FAIL link-hit-test  block %lu not hit at its centre\n",
                   (unsigned long)i);
            continue;
        }

        memset(&outcome, 0, sizeof(outcome));
        r = robo_demo_page_activate_link(page, i, NULL, &outcome, NULL);
        if (r != ROBO_OK) {
            report->failures++;
            printf("probe FAIL link-activate  block %lu returned %d\n",
                   (unsigned long)i, (int)r);
            robo_html_navigation_outcome_clear(&outcome);
            continue;
        }
        activated++;
        printf("link: block %lu href=\"%s\" outcome=%s resolved=\"%s\"\n",
               (unsigned long)i, robo_demo_page_block_href(page, i),
               robo_html_navigation_outcome_kind_name(outcome.kind),
               outcome.resolved ? outcome.resolved : "");
        if (outcome.kind == ROBO_HTML_NAVIGATION_FRAGMENT) {
            fragments++;
            /* A fragment is resolved here, in this document: if the outcome
             * says it found an element, the element must really exist. */
            report->checks++;
            if (!outcome.fragment_found || outcome.fragment_length == 0u) {
                report->failures++;
                printf("probe FAIL link-fragment  reported FRAGMENT without a "
                       "found element\n");
            }
        } else if (outcome.kind == ROBO_HTML_NAVIGATION_UNSUPPORTED) {
            unsupported++;
        } else if (outcome.kind == ROBO_HTML_NAVIGATION_NONE) {
            report->failures++;
            printf("probe FAIL link-outcome  produced NONE\n");
        }
        robo_html_navigation_outcome_clear(&outcome);
    }

    report->checks++;
    if (activated != robo_demo_page_link_count(page) ||
        fragments == 0u || unsupported == 0u) {
        report->failures++;
        printf("probe FAIL link-seam  activated=%lu fragments=%lu "
               "unsupported=%lu\n",
               (unsigned long)activated, (unsigned long)fragments,
               (unsigned long)unsupported);
        return;
    }
    printf("probe OK   link-seam       %lu links: %lu fragment, %lu unsupported "
           "(no handler installed, so nothing claims navigation)\n",
           (unsigned long)activated, (unsigned long)fragments,
           (unsigned long)unsupported);
}

static void drain_events(demo_state_t *st)
{
    robo_window_event_t ev;

    while (robo_window_event_poll(&ev) == ROBO_OK) {
        handle_event(st, &ev);
    }
}

static int paint_window(robo_window_id_t id, demo_state_t *st)
{
    robo_gfx_surface_t *surf = NULL;
    robo_demo_paint_options_t opts;
    robo_result_t r;

    r = robo_window_begin_frame(id, &surf);
    if (r != ROBO_OK || !surf) {
        printf("window: begin_frame failed (%d)\n", (int)r);
        return 0;
    }
    robo_demo_paint_options_init(&opts);
    opts.button_state = st->button;
    r = robo_demo_page_paint(st->page, surf, &opts, NULL);
    if (r != ROBO_OK) {
        printf("window: paint failed (%d)\n", (int)r);
        return 0;
    }
    if (robo_window_end_frame(id) != ROBO_OK) {
        printf("window: end_frame failed\n");
        return 0;
    }
    if (robo_window_present_frame(id) != ROBO_OK) {
        printf("window: present_frame failed\n");
        return 0;
    }
    st->need_paint = 0;
    return 1;
}

/**
 * Deliver deterministic input through the production observe_* entry
 * points — the same path native Win32 input takes above the backend. This
 * verifies observe → queue → event loop → demo state without a human at the
 * keyboard, and is what makes the interactive part of the checkpoint
 * automated.
 */
static void inject_input(robo_window_id_t id, const robo_demo_page_t *page)
{
    robo_gfx_rect_t br;
    robo_mouse_event_t m;
    robo_keyboard_event_t k;
    size_t i;

    /*
     * Press and release on every link first, so the windowed checkpoint
     * exercises the same seam the offscreen probe does, but through the real
     * event queue: observe → queue → poll → hit test → activation.
     */
    for (i = 0u; i < robo_demo_page_block_count(page); i++) {
        robo_demo_block_style_t style;
        robo_gfx_rect_t lr;

        if (robo_demo_page_block_style(page, i, &style) != ROBO_OK ||
            style != ROBO_DEMO_BLOCK_LINK ||
            !robo_demo_page_block_rect(page, i, &lr)) {
            continue;
        }
        robo_mouse_event_init(&m);
        m.window_id = id;
        m.type = ROBO_MOUSE_EVENT_MOVE;
        m.client_x = lr.x + lr.width / 2;
        m.client_y = lr.y + lr.height / 2;
        (void)robo_window_observe_mouse(id, &m);
        m.type = ROBO_MOUSE_EVENT_BUTTON_DOWN;
        m.button = ROBO_MOUSE_BUTTON_LEFT;
        (void)robo_window_observe_mouse(id, &m);
        m.type = ROBO_MOUSE_EVENT_BUTTON_UP;
        (void)robo_window_observe_mouse(id, &m);
    }

    if (!robo_demo_page_button_rect(page, &br)) {
        return;
    }

    robo_mouse_event_init(&m);
    m.type = ROBO_MOUSE_EVENT_MOVE;
    m.client_x = br.x + br.width / 2;
    m.client_y = br.y + br.height / 2;
    m.window_id = id;
    (void)robo_window_observe_mouse(id, &m);

    m.type = ROBO_MOUSE_EVENT_BUTTON_DOWN;
    m.button = ROBO_MOUSE_BUTTON_LEFT;
    (void)robo_window_observe_mouse(id, &m);

    m.type = ROBO_MOUSE_EVENT_BUTTON_UP;
    (void)robo_window_observe_mouse(id, &m);

    /* Leave the pointer elsewhere so the last frame is the resting state. */
    m.type = ROBO_MOUSE_EVENT_MOVE;
    m.button = ROBO_MOUSE_BUTTON_NONE;
    m.client_x = 4;
    m.client_y = br.y + br.height + 40;
    (void)robo_window_observe_mouse(id, &m);

    robo_keyboard_event_init(&k);
    k.action = ROBO_KEY_ACTION_DOWN;
    k.physical_key = ROBO_PHYSICAL_KEY_SPACE;
    k.logical_key = ROBO_LOGICAL_KEY_SPACE;
    k.window_id = id;
    (void)robo_window_observe_key(id, &k);
    k.action = ROBO_KEY_ACTION_UP;
    (void)robo_window_observe_key(id, &k);
}

/* ------------------------------------------------------------------ */
/* Phases                                                             */
/* ------------------------------------------------------------------ */

static int run_offscreen(robo_demo_page_t *page,
                         const char *shot_path,
                         demo_report_t *report)
{
    robo_gfx_surface_desc_t desc;
    robo_gfx_surface_t *surface = NULL;
    robo_demo_paint_options_t opts;
    robo_demo_paint_stats_t stats;
    robo_result_t r;

    robo_gfx_surface_desc_init(&desc);
    desc.width = DEMO_OFFSCREEN_W;
    desc.height = DEMO_OFFSCREEN_H;
    desc.format = ROBO_GFX_FORMAT_RGBA8;
    desc.access = ROBO_GFX_SURFACE_ACCESS_READ_WRITE;

    r = robo_gfx_surface_create(&desc, &surface);
    if (r != ROBO_OK || !surface) {
        printf("offscreen: surface create failed (%d)\n", (int)r);
        return 0;
    }

    printf("offscreen: %dx%d RGBA8\n", DEMO_OFFSCREEN_W, DEMO_OFFSCREEN_H);
    robo_demo_paint_options_init(&opts);
    r = robo_demo_page_paint(page, surface, &opts, &stats);
    if (r != ROBO_OK) {
        printf("offscreen: paint failed (%d)\n", (int)r);
        robo_gfx_surface_release(surface);
        return 0;
    }
    validate_frame(page, surface, ROBO_DEMO_BUTTON_NORMAL, &stats, report);
    probe_links(page, report);
    if (shot_path) {
        (void)write_screenshot(surface, shot_path);
    }
    robo_gfx_surface_release(surface);
    return 1;
}

static int run_windowed(robo_demo_page_t *page,
                        unsigned hold_ms,
                        int inject,
                        const char *shot_path,
                        demo_report_t *report)
{
    robo_window_id_t id = ROBO_WINDOW_ID_NONE;
    robo_window_config_t cfg;
    demo_state_t st;
    robo_gfx_size_t logical;
    robo_gfx_size_t physical;
    float scale = 1.0f;
    unsigned elapsed = 0u;
    unsigned inject_waited = 0u;
    int painted_once = 0;
    int injected = 0;

    if (!robo_window_backend_host_available() ||
        robo_window_backend_use_host() != ROBO_OK) {
        printf("window: host backend unavailable — offscreen result only\n");
        return 1; /* not a failure: offscreen already validated */
    }

    memset(&st, 0, sizeof(st));
    st.page = page;
    st.button = ROBO_DEMO_BUTTON_NORMAL;
    st.need_paint = 1;
    st.link_pressed = -1;

    robo_window_config_init(&cfg);
    cfg.width = DEMO_OFFSCREEN_W;
    cfg.height = DEMO_OFFSCREEN_H;
    cfg.visible = 1;
    cfg.resizable = 1;
    cfg.title = robo_demo_page_title(page);
    if (cfg.title[0] == '\0') {
        cfg.title = "RoboticelaOS Runtime";
    }

    if (robo_window_create(&cfg, &id) != ROBO_OK) {
        printf("window: create failed (no interactive desktop?)\n");
        return 1;
    }
    (void)robo_window_show(id);
    (void)robo_window_observe_focus(id, 1);
    if (robo_window_event_loop_start() != ROBO_OK) {
        printf("window: event loop start failed\n");
        robo_window_destroy(id);
        return 0;
    }

    (void)robo_window_get_size(id, &logical);
    (void)robo_window_get_size_physical(id, &physical);
    (void)robo_window_get_scale(id, &scale);
    printf("window: %s logical=%dx%d physical=%dx%d scale=%g\n",
           robo_window_backend_name(), logical.width, logical.height,
           physical.width, physical.height, (double)scale);

    for (;;) {
        (void)robo_window_event_loop_run_once(DEMO_FRAME_WAIT_MS);
        drain_events(&st);
        elapsed += DEMO_FRAME_WAIT_MS;

        if (st.need_paint && !paint_window(id, &st)) {
            break;
        }
        if (!painted_once && !st.need_paint) {
            robo_gfx_surface_t *surf = robo_window_get_render_surface(id);
            robo_demo_paint_stats_t stats;
            robo_demo_paint_options_t opts;

            painted_once = 1;
            robo_demo_paint_options_init(&opts);
            opts.button_state = st.button;
            memset(&stats, 0, sizeof(stats));
            if (surf) {
                /* Re-paint for stats, then validate the presented frame. */
                (void)robo_demo_page_paint(page, surf, &opts, &stats);
                validate_frame(page, surf, st.button, &stats, report);
                if (shot_path) {
                    (void)write_screenshot(surf, shot_path);
                }
            }
            if (inject && !injected) {
                injected = 1;
                inject_input(id, page);
            }
        }
        if (injected && st.clicks == 0u && st.hover == 0 &&
            inject_waited < DEMO_INJECT_TIMEOUT_MS) {
            inject_waited += DEMO_FRAME_WAIT_MS;
        }
        if (st.close_requested) {
            break;
        }
        if (hold_ms == 0u || elapsed >= hold_ms) {
            break;
        }
    }

    if (inject) {
        report->checks++;
        printf("input: keys=%lu mouse=%lu resize=%lu clicks=%lu hover_seen=%d\n",
               (unsigned long)st.key_events, (unsigned long)st.mouse_events,
               (unsigned long)st.resize_events, (unsigned long)st.clicks,
               st.hover);
        if (st.key_events < 2u || st.mouse_events < 4u || st.clicks < 1u) {
            report->failures++;
            printf("probe FAIL injected-input  expected >=2 keys, >=4 mouse, "
                   ">=1 click\n");
        } else {
            printf("probe OK   injected-input\n");
        }
        /* The click path really activated real anchors: both links were
         * pressed and released, the fragment one resolved inside this
         * document, and the route one refused because no handler is
         * installed (nothing may report navigation that did not happen). */
        report->checks++;
        if (st.link_activations != robo_demo_page_link_count(page) ||
            st.link_fragment_hits == 0u || st.link_unsupported == 0u) {
            report->failures++;
            printf("probe FAIL injected-link-click  activations=%lu "
                   "fragments=%lu unsupported=%lu\n",
                   (unsigned long)st.link_activations,
                   (unsigned long)st.link_fragment_hits,
                   (unsigned long)st.link_unsupported);
        } else {
            printf("probe OK   injected-link-click %lu links activated from "
                   "pointer input\n", (unsigned long)st.link_activations);
        }
    }

    (void)robo_window_request_close(id);
    drain_events(&st);
    if (robo_window_destroy(id) != ROBO_OK) {
        printf("window: destroy failed\n");
        report->failures++;
    }
    (void)robo_window_event_loop_request_stop();
    if (robo_window_live_count() != 0u) {
        printf("window: %lu windows still live after destroy\n",
               (unsigned long)robo_window_live_count());
        report->failures++;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

static unsigned parse_ms(const char *text, unsigned fallback)
{
    long v;

    if (!text || text[0] == '\0') {
        return fallback;
    }
    v = strtol(text, NULL, 10);
    if (v < 0) {
        return fallback;
    }
    return (unsigned)v;
}

int robo_demo_html_host_main(int argc, char **argv)
{
    robo_demo_page_t *page = NULL;
    robo_html_diag_t diag;
    demo_report_t report;
    unsigned hold_ms;
    const char *shot = NULL;
    int no_window = 0;
    int inject = 1;
    int i;
    const char *hold_env;

    report.checks = 0;
    report.failures = 0;
    robo_html_diag_reset(&diag);

    hold_env = getenv("ROBO_DEMO_HOLD_MS");
    hold_ms = parse_ms(hold_env, DEMO_HOLD_DEFAULT_MS);
    shot = getenv("ROBO_DEMO_SHOT");
    if (getenv("ROBO_DEMO_NO_WINDOW") != NULL) {
        no_window = 1;
    }

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--no-window") == 0) {
            no_window = 1;
        } else if (strcmp(a, "--no-inject-input") == 0) {
            inject = 0;
        } else if (strcmp(a, "--hold") == 0 && i + 1 < argc) {
            hold_ms = parse_ms(argv[++i], DEMO_HOLD_DEFAULT_MS);
        } else if (strcmp(a, "--shot") == 0 && i + 1 < argc) {
            shot = argv[++i];
        } else {
            printf("demo-html: unknown option %s\n", a);
            printf("usage: %s [--no-window] [--no-inject-input] [--hold ms] "
                   "[--shot path]\n",
                   argv[0]);
            return 2;
        }
    }

    printf("RoboticelaOS Runtime — HTML visual checkpoint\n");
    printf("source: %lu bytes of built-in demo document\n",
           (unsigned long)robo_demo_default_html_length());

    if (!robo_base_is_initialized() && robo_base_init() != ROBO_OK) {
        printf("demo-html: robo_base_init failed\n");
        return 1;
    }
    if (robo_graphics_init() != ROBO_OK) {
        printf("demo-html: robo_graphics_init failed\n");
        return 1;
    }
    if (robo_html_init() != ROBO_OK) {
        printf("demo-html: robo_html_init failed\n");
        return 1;
    }
    if (robo_display_init() == ROBO_OK) {
        (void)robo_display_backend_use_host();
    }
    if (robo_window_init() != ROBO_OK) {
        printf("demo-html: robo_window_init failed\n");
        return 1;
    }

    if (robo_demo_page_create(robo_demo_default_html(),
                              robo_demo_default_html_length(), &page,
                              &diag) != ROBO_OK) {
        printf("demo-html: page build failed code=%d domain=%d msg=%s\n",
               (int)diag.code, (int)diag.domain,
               diag.message ? diag.message : "(none)");
        return 1;
    }
    report_page(page);

    if (!run_offscreen(page, shot ? shot : NULL, &report)) {
        robo_demo_page_destroy(page);
        return 1;
    }
    if (!no_window) {
        (void)run_windowed(page, hold_ms, inject, NULL, &report);
    }

    robo_demo_page_destroy(page);
    robo_window_shutdown();
    robo_display_shutdown();
    robo_html_shutdown();
    robo_graphics_shutdown();

    printf("checks: %d passed=%d failed=%d\n", report.checks,
           report.checks - report.failures, report.failures);
    printf("RESULT: %s\n", report.failures == 0 ? "PASS" : "FAIL");
    return report.failures == 0 ? 0 : 1;
}
