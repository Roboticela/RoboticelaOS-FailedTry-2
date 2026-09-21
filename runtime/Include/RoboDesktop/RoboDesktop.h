/*
 * RoboDesktop — the module's front door (Phase 11.13).
 *
 * The native shell, and the pipeline it closes:
 *
 * 		RoboHTML → RoboCSS → RoboLayout → RoboPaint → RoboDisplayList
 *                                                        ↓
 *                                                  RoboRender (a frame)
 *                                                        ↓
 * 		RoboDesktop (background, panel, windows) → the screen target
 *                                                        ↓
 * 		RoboRender (present) → RoboWindow's render surface → Win32 / QEMU
 *
 * ## The frame lifecycle, and why it is three calls rather than one
 *
 * `begin_frame` → `paint` → `end_frame` is the same triple RoboWindow uses, and it is three calls for the
 * same reason: a *record* of the frame is worth having, and a record can only be honest if the caller
 * controls where the frame starts and stops. `robo_desktop_render` is the one-call shorthand for the
 * caller that has nothing to interleave — and the suite uses both, so the shorthand cannot drift from the
 * sequence it abbreviates.
 *
 * **A frame does not present itself.** `robo_desktop_present` is a separate door, and the separation is
 * deliberate rather than convenient: it is what lets a caller render a frame and *look at it* without a
 * display (a test, a screenshot, a headless build), and what lets the same rendered frame be presented
 * twice or into two destinations. A frame that presented itself would make both of those impossible and
 * would hide the "did it reach the screen?" answer inside a call that also drew things.
 *
 * ## The pixel target, and what this module does *not* add
 *
 * The screen target is a `robo_render_target_t` (RoboRender's owned RGBA8 buffer) and nothing else: this
 * phase adds **no framebuffer type of its own**. The tree already has two, and each has an owner and a
 * reason — `robo_gfx_framebuffer_t` (RoboGraphics, Goal 6.3) is an owned-or-borrowed pixel store with an
 * explicit stride, and `robo_qemu_fb_info_t` (RoboDisplay, Goal 7.12) is the UEFI/QEMU mapping with its
 * format and pitch. A third would be a third name for the same bytes, and the interesting question — "can
 * a frame reach that mapping?" — is answered by `robo_render_present_framebuffer`, which takes RoboGraphics'
 * own type. So the phrase "the desktop's framebuffer" in this module means **the screen target its frames
 * are painted into**, and the doors below are what a caller uses to read it, screenshot it, or hand it to
 * a display backend.
 *
 * ## Determinism
 *
 * A frame is a function of the target's size, the theme, the metrics, and the windows — nothing else.
 * Nothing here reads a clock, asks for the time, allocates in an order that shows, or iterates a hash:
 * the status text is a string a *caller* supplies (a clock is the caller's business, and a desktop that
 * read one could not be tested), the z-order is an array's order, and the record a frame produces is a
 * struct of counts. Two frames of one state are byte-identical, and `robo_desktop_frame_checksum` is the
 * door that lets a caller check it rather than believe it.
 */
#ifndef ROBO_DESKTOP_H
#define ROBO_DESKTOP_H

#include "RoboBase/RoboError.h"
#include "RoboDesktop/RoboDesktopDiagnostics.h"
#include "RoboDesktop/RoboDesktopGeometry.h"
#include "RoboDesktop/RoboDesktopTheme.h"
#include "RoboDesktop/RoboDesktopTypes.h"
#include "RoboDesktop/RoboDesktopVersion.h"
#include "RoboDesktop/RoboDesktopWindow.h"

#include "RoboFont/RoboFontFace.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboRender/RoboRenderPresenter.h"
#include "RoboWindow/RoboWindowTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The module                                                          */
/* ------------------------------------------------------------------ */

const char *robo_desktop_version_string(void);

robo_result_t robo_desktop_get_version(robo_i32 *out_major,
                                      robo_i32 *out_minor,
                                      robo_i32 *out_patch);

/** Non-zero when a native desktop shell exists in this build. See `ROBO_DESKTOP_SHELL_IMPLEMENTED`. */
int robo_desktop_shell_is_implemented(void);

/**
 * What this build's desktop does and does not do.
 *
 * Read positionally or by name, never by count — the device `RoboPaint` and `RoboRender` established, and
 * asserted field-by-field by `desktop.conformance.capabilities`, because an untested capability flag is a
 * flag that stops being true without anybody noticing.
 */
typedef struct robo_desktop_capabilities {
    /** The shell itself: an owned screen target, a background, a panel and a work area (11.13). */
    int shell;
    /** Shell windows: decorations, a title, a client area and z-order. */
    int windows;
    /** Rasterising a window's contents from a display list and compositing them under the chrome. */
    int window_content;
    /** Presenting the frame into a destination surface, or into a RoboWindow's own render surface. */
    int presentation;
    /** A deterministic frame record and a byte checksum. */
    int frame_records;
    /** Recomputing geometry and revalidating windows after a size change. */
    int resize;
    /** Hit-testing a point to the shell object under it, and the two actions that follow (focus, close). */
    int pointer_targeting;
    /** Chrome text through a caller-supplied face, counted and skipped when there is none. */
    int shell_text;
    /** An application document (HTML → CSS → layout → paint → display list). **0**: see below. */
    int documents;
    /** A layer tree with its own ordering and damage union. **0**: the shell draws into one target. */
    int compositor;
    /** Independently repaintable pieces of the screen. **0**: a frame is a frame. */
    int tiles;
    /** A history of what was on the screen, so "unchanged" needs no comparison. **0**. */
    int damage_tracking;
    /** A clock, a deadline and a queue. **0**: and this module's determinism depends on the absence. */
    int frame_scheduling;
    /** Waiting for a display's vertical blank. **0**: presentation is synchronous and unpaced. */
    int vsync;
    /** A GPU presentation path. **0**: everything here is CPU and software. */
    int gpu;
    /** A window manager: snapping, tiling, virtual desktops, animation, maximize and minimize. **0**. */
    int window_manager;
    /** Keyboard routing and input delivery to applications. **0**: §16 of the phase says where it ends. */
    int input_delivery;
    /** A desktop environment: launcher, task manager, settings, notifications. **0**. */
    int desktop_environment;
} robo_desktop_capabilities_t;

/** Zero @p capabilities (every feature reported absent). Safe on NULL. */
void robo_desktop_capabilities_init(robo_desktop_capabilities_t *capabilities);

/** Fill @p out with this build's capabilities.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p out. */
robo_result_t robo_desktop_get_capabilities(robo_desktop_capabilities_t *out);

/* ------------------------------------------------------------------ */
/* Metrics                                                             */
/* ------------------------------------------------------------------ */

/** This build's panel metrics. Safe on NULL (no-op). */
void robo_desktop_panel_metrics_init(robo_desktop_panel_metrics_t *metrics);

/** Non-zero when @p metrics describes a panel this build can lay out. */
int robo_desktop_panel_metrics_is_valid(const robo_desktop_panel_metrics_t *metrics);

/** This build's window decoration metrics. Safe on NULL (no-op). */
void robo_desktop_window_metrics_init(robo_desktop_window_metrics_t *metrics);

/** Non-zero when @p metrics describes decorations this build can draw. */
int robo_desktop_window_metrics_is_valid(const robo_desktop_window_metrics_t *metrics);

/* ------------------------------------------------------------------ */
/* Configuration and lifetime                                          */
/* ------------------------------------------------------------------ */

typedef struct robo_desktop_config {
    /** Screen size in pixels. Both dimensions must be positive; see the maxima in `RoboDesktopTypes.h`. */
    int32_t width;
    int32_t height;
    /**
     * A native window to present into, or @ref ROBO_WINDOW_ID_NONE. A desktop with **no** window is a
     * legitimate desktop: it renders frames, and `robo_desktop_present` reports that it has nowhere to
     * put them (`ROBO_NOT_SUPPORTED`) rather than failing to exist.
     */
    robo_window_id_t window;
    /**
     * Create the desktop's own native window instead of attaching an existing one. Ignored when
     * @p window is a real id. Needs `robo_window_init` to have run and a backend to be registered; a
     * failure here fails the whole call, because a caller that asked for a window and got a shell with
     * nowhere to show itself would have to check two things to find that out.
     */
    int create_window;
    /** The native window's title; NULL means @ref ROBO_DESKTOP_DEFAULT_TITLE. */
    const char *window_title;
    /** The identity label; NULL means @ref ROBO_DESKTOP_DEFAULT_IDENTITY. */
    const char *identity_text;
    /** The applications label; NULL means @ref ROBO_DESKTOP_DEFAULT_APPLICATIONS. */
    const char *applications_text;
    /** The status label (a clock, a battery, a version — the caller's choice); NULL means none. */
    const char *status_text;
    /** Chrome palette; NULL means this build's default theme. */
    const robo_desktop_theme_t *theme;
    /** Panel metrics; NULL means this build's defaults. */
    const robo_desktop_panel_metrics_t *panel;
    /** Window decoration metrics; NULL means this build's defaults. */
    const robo_desktop_window_metrics_t *window_metrics;
    /**
     * The face chrome text is drawn with: the identity, applications and status labels, and every window's
     * title. NULL means this build draws no chrome glyphs — the geometry, the accent mark, the close cross
     * and every fill still appear, and the frame record counts the labels it could not shape (`labels`
     * against `labels_unshaped`). Text reaching a *window's contents* is a separate face, carried per
     * window (`robo_desktop_window_desc_t.content_face`).
     */
    robo_font_face_t *face;
    /** Chrome text size in pixels; 0 or negative means @ref ROBO_DESKTOP_DEFAULT_LABEL_SIZE_PX. */
    float label_size_px;
} robo_desktop_config_t;

/** The native window's and the shell's default title. */
#define ROBO_DESKTOP_DEFAULT_TITLE "RoboticelaOS"
/** The platform's name, as the panel's identity label. */
#define ROBO_DESKTOP_DEFAULT_IDENTITY "RoboticelaOS"
/** The panel's middle label. */
#define ROBO_DESKTOP_DEFAULT_APPLICATIONS "Applications"
/** Chrome text size when a caller does not choose one. */
#define ROBO_DESKTOP_DEFAULT_LABEL_SIZE_PX 11.0f

/** Fill @p config with the defaults: 1280x800, no window, the default theme and metrics, no face. */
void robo_desktop_config_init(robo_desktop_config_t *config);

/**
 * Create a desktop shell.
 *
 * A shell owns its screen target (allocated here, zero-filled, sized to @p config), its geometry, its
 * theme and metrics copies, its label strings, and an empty window list. It owns a native window only when
 * it was asked to create one; an attached window stays the caller's to destroy, which is the same
 * ownership rule `robo_window_set_present_surface` states for a borrowed surface.
 *
 * @return ROBO_OK and @p out; ROBO_INVALID_ARGUMENT for a NULL @p config or @p out, a non-positive or
 *         oversized dimension, an invalid theme, panel or window metrics, or an over-long label;
 *         ROBO_NOT_SUPPORTED with a `SHELL` diagnostic when @p config asks for a native window and none
 *         could be created; ROBO_OUT_OF_MEMORY when the target could not be allocated.
 */
robo_result_t robo_desktop_create(const robo_desktop_config_t *config,
                                 robo_desktop_t **out,
                                 robo_desktop_diag_t *diag);

/**
 * Destroy @p desktop: every shell window, the screen target, and (only) a native window this shell
 * created itself. Safe on NULL. A borrowed native window is **not** destroyed — see `robo_desktop_create`.
 */
void robo_desktop_destroy(robo_desktop_t *desktop);

/** Non-zero once a desktop exists and has not been destroyed. */
int robo_desktop_is_live(const robo_desktop_t *desktop);

/** Screen size in pixels (0x0 for NULL). */
robo_gfx_size_t robo_desktop_screen_size(const robo_desktop_t *desktop);

/** The desktop's layout. A cleared geometry for NULL. */
robo_desktop_geometry_t robo_desktop_geometry_of(const robo_desktop_t *desktop);

/** The work area: where windows live. An empty rectangle for NULL. */
robo_gfx_rect_t robo_desktop_work_area(const robo_desktop_t *desktop);

/** The attached native window, or @ref ROBO_WINDOW_ID_NONE. */
robo_window_id_t robo_desktop_attached_window(const robo_desktop_t *desktop);

/* ------------------------------------------------------------------ */
/* Labels                                                              */
/* ------------------------------------------------------------------ */

/**
 * Replace the status label (a clock, a battery, anything the caller wants on the right of the panel).
 *
 * Copied and bounded by @ref ROBO_DESKTOP_STATUS_MAX, and **the caller's clock is the caller's**: this
 * module never reads a time, which is what keeps two frames of one state byte-identical. A NULL or empty
 * @p text clears the label and the status area is then untouched by the next frame.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p desktop or an over-long label.
 */
robo_result_t robo_desktop_set_status(const robo_desktop_t *desktop,
                                      const char *utf8_text,
                                      robo_desktop_diag_t *diag);

/** Borrowed status label (never NULL; `""` when there is none). */
const char *robo_desktop_status(const robo_desktop_t *desktop);

/* ------------------------------------------------------------------ */
/* Shell windows                                                       */
/* ------------------------------------------------------------------ */

/**
 * Add a shell window.
 *
 * The bounds are clamped into the work area (@ref robo_desktop_geometry_contain) and a window below the
 * decoration minimum is grown to it rather than refused, because the minimum is a property of the
 * *decorations* the shell draws rather than of the caller's request: a caller asking for a 4x4 window is
 * asking for something the shell cannot draw, and the honest answer is the smallest window that can be
 * drawn rather than an error or a window whose title bar is outside its own frame.
 *
 * @p out_id receives the new window's id. @p diag may be NULL.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p desktop or @p desc or an invalid description;
 *         ROBO_ALREADY_EXISTS when the desktop already holds @ref ROBO_DESKTOP_MAX_WINDOWS windows;
 *         ROBO_INVALID_ARGUMENT with a `SHELL` diagnostic when there is no work area to place it in.
 */
robo_result_t robo_desktop_window_add(robo_desktop_t *desktop,
                                     const robo_desktop_window_desc_t *desc,
                                     robo_desktop_window_id_t *out_id,
                                     robo_desktop_diag_t *diag);

/** Number of shell windows, closing ones included. */
size_t robo_desktop_window_count(const robo_desktop_t *desktop);

/** The id of the window at z-order @p index (0 = furthest back). ROBO_NOT_FOUND when out of range. */
robo_result_t robo_desktop_window_at(const robo_desktop_t *desktop,
                                     size_t index,
                                     robo_desktop_window_id_t *out_id);

/** Non-zero when @p id names a window of @p desktop that is not yet destroyed. */
int robo_desktop_window_exists(const robo_desktop_t *desktop, robo_desktop_window_id_t id);

/** Outer rectangle (decorations included). ROBO_NOT_FOUND for an unknown id. */
robo_result_t robo_desktop_window_bounds(const robo_desktop_t *desktop,
                                         robo_desktop_window_id_t id,
                                         robo_gfx_rect_t *out);

/** The client rectangle: where a window's contents are drawn. ROBO_NOT_FOUND for an unknown id. */
robo_result_t robo_desktop_window_client_bounds(const robo_desktop_t *desktop,
                                                robo_desktop_window_id_t id,
                                                robo_gfx_rect_t *out);

/** Move and/or resize @p id. Clamped into the work area like `add`; a NULL-diag refusal is the same set. */
robo_result_t robo_desktop_window_set_bounds(robo_desktop_t *desktop,
                                            robo_desktop_window_id_t id,
                                            robo_gfx_rect_t bounds,
                                            robo_desktop_diag_t *diag);

/** Copy a UTF-8 title. NULL clears it. Over-long -> ROBO_INVALID_ARGUMENT (nothing copied). */
robo_result_t robo_desktop_window_set_title(robo_desktop_t *desktop,
                                            robo_desktop_window_id_t id,
                                            const char *utf8_title,
                                            robo_desktop_diag_t *diag);

/** Borrowed title (never NULL; `""` when none or unknown id). */
const char *robo_desktop_window_title(const robo_desktop_t *desktop, robo_desktop_window_id_t id);

/**
 * Replace what a window's contents are.
 *
 * @p content is borrowed, not copied, and must outlive the window's last paint — the rule
 * `RoboDesktopWindow.h` states at length. NULL means "this window has no contents", which paints its body
 * and decorations and reports `content_runs == 0`.
 */
robo_result_t robo_desktop_window_set_content(robo_desktop_t *desktop,
                                              robo_desktop_window_id_t id,
                                              const robo_paint_display_list_t *content,
                                              robo_font_face_t *content_face,
                                              robo_desktop_diag_t *diag);

/** The window's borrowed contents, or NULL (including for an unknown id). */
const robo_paint_display_list_t *robo_desktop_window_content(const robo_desktop_t *desktop,
                                                            robo_desktop_window_id_t id);

/** Show or hide a window. A hidden window keeps its bounds, contents and z-order and is not painted. */
robo_result_t robo_desktop_window_set_visible(robo_desktop_t *desktop,
                                              robo_desktop_window_id_t id,
                                              int visible,
                                              robo_desktop_diag_t *diag);

/** Non-zero when @p id is drawn. 0 for an unknown id. */
int robo_desktop_window_is_visible(const robo_desktop_t *desktop, robo_desktop_window_id_t id);

/**
 * Move @p id to the front of the z-order (the last window painted is the front one).
 *
 * Focus follows the raise, and that is a decision rather than a shortcut: this build has no separate
 * focus model, so "the front window is the focused one" is the whole of it and the two cannot drift.
 * `robo_desktop_window_focus` is the door that focuses without raising, for a caller that wants the
 * accent border elsewhere.
 *
 * @return ROBO_OK; ROBO_NOT_FOUND for an unknown id.
 */
robo_result_t robo_desktop_window_raise(robo_desktop_t *desktop, robo_desktop_window_id_t id);

/** Make @p id the focused window without changing the z-order. ROBO_NOT_FOUND for an unknown id. */
robo_result_t robo_desktop_window_focus(robo_desktop_t *desktop, robo_desktop_window_id_t id);

/** The focused window, or @ref ROBO_DESKTOP_WINDOW_ID_NONE. */
robo_desktop_window_id_t robo_desktop_window_focused(const robo_desktop_t *desktop);

/**
 * Ask @p id to close: it stops being painted and moves to
 * @ref ROBO_DESKTOP_WINDOW_CLOSING, and **the caller destroys it**.
 *
 * This is what the close button produces and what a caller's own "quit" would produce. Idempotent.
 *
 * @return ROBO_OK; ROBO_NOT_FOUND for an unknown id.
 */
robo_result_t robo_desktop_window_request_close(robo_desktop_t *desktop, robo_desktop_window_id_t id);

/** A window's lifecycle state. ROBO_NOT_FOUND for an unknown id. */
robo_result_t robo_desktop_window_state(const robo_desktop_t *desktop,
                                        robo_desktop_window_id_t id,
                                        robo_desktop_window_state_t *out);

/** Remove @p id from the shell. ROBO_NOT_FOUND for an unknown id. Focus falls to the front window. */
robo_result_t robo_desktop_window_destroy(robo_desktop_t *desktop, robo_desktop_window_id_t id);

/* ------------------------------------------------------------------ */
/* Hit testing and the pointer                                         */
/* ------------------------------------------------------------------ */

/** What is under a point. */
typedef struct robo_desktop_hit {
    robo_desktop_area_t area;
    /** The window under the point, or @ref ROBO_DESKTOP_WINDOW_ID_NONE. */
    robo_desktop_window_id_t window;
    /** The point, echoed. */
    robo_gfx_point_t point;
    /** Non-zero when the point is inside the screen at all. */
    int inside;
} robo_desktop_hit_t;

void robo_desktop_hit_init(robo_desktop_hit_t *hit);

/**
 * Which shell object is at @p point.
 *
 * Front to back: a point over two windows belongs to the front one, which is what makes the answer the
 * one a *user* would give. A point outside the screen is @ref ROBO_DESKTOP_AREA_NONE with `inside == 0`
 * rather than an error — a pointer can leave a window, and that is not a failure.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p desktop or @p out.
 */
robo_result_t robo_desktop_hit_test(const robo_desktop_t *desktop,
                                    robo_gfx_point_t point,
                                    robo_desktop_hit_t *out);

/**
 * Hand the shell a pointer state: where it is, and whether a button is down *now*.
 *
 * The whole of this build's input integration, and it is deliberately this small (the phase's §16: the
 * desktop must be alive to the pointer where the window path already delivers one, and must not become
 * an input system). Two actions, both of them decisions about *windows* rather than about documents:
 *
 *   a **press** on a window's title bar or client area raises and focuses that window;
 *   a **press** on a window's close button requests that window's close.
 *
 * A move only records where the pointer is (and what is under it — `robo_desktop_hit_test` answers that
 * on demand, and the record's `hovered` field is what the last move found). Nothing is delivered to a
 * window's contents: hit-test delivery to applications is a later phase's, and a shell that invented a
 * delivery protocol now would be inventing the ABI those phases need to agree on.
 *
 * @return ROBO_OK — including a press that hit nothing, which is a legitimate press; ROBO_INVALID_ARGUMENT
 *         for a NULL @p desktop or @p diag.
 */
robo_result_t robo_desktop_observe_pointer(robo_desktop_t *desktop,
                                           robo_gfx_point_t point,
                                           int pressed,
                                           robo_desktop_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Frames                                                              */
/* ------------------------------------------------------------------ */

/**
 * What one frame did.
 *
 * A record by value, and its counts are the two halves a reader compares: what the **shell** drew
 * (`fills`, `labels`, and the one accent mark), and what the **windows** contributed (`windows` painted,
 * `content_runs` executed, `content_pixels` composited). `clipped` is the number of shell drawings that
 * were cut by the screen's own bounds — a window partly off the screen is a legitimate state, and the
 * count is how a caller learns that a placement is showing less than it asked for.
 */
typedef struct robo_desktop_frame_record {
    /** The target's size in pixels. */
    uint32_t width;
    uint32_t height;
    /** Shell fills executed: the surface, the panel, its separator, the falloff rows, the mark. */
    size_t fills;
    /** Chrome labels drawn (identity, applications, status, and one per painted window title). */
    size_t labels;
    /** Labels skipped because no face was available. */
    size_t labels_unshaped;
    /** Windows painted: visible, not closing. */
    size_t windows;
    /** Windows the shell holds, closing or hidden included. The denominator `windows` is read against. */
    size_t windows_total;
    /** Display-list runs executed for window contents (0 when no window has contents). */
    size_t content_runs;
    /** Pixels of window contents composited into the screen. */
    size_t content_pixels;
    /** Shell drawings clipped away by the screen's bounds. */
    size_t clipped;
    /** Bytes the frame's target occupies. */
    size_t bytes;
    /** The target's checksum after the frame. */
    uint64_t checksum;
} robo_desktop_frame_record_t;

/** Clear @p record to a frame that did nothing. Safe on NULL. */
void robo_desktop_frame_record_init(robo_desktop_frame_record_t *record);

/** Non-zero when @p record describes a frame this build could have painted. */
int robo_desktop_frame_record_is_valid(const robo_desktop_frame_record_t *record);

/** Field-for-field equality. Safe on NULL. */
int robo_desktop_frame_record_equal(const robo_desktop_frame_record_t *a,
                                    const robo_desktop_frame_record_t *b);

/** Begin a frame. Nested without a matching end → ROBO_BUSY. NULL desktop → ROBO_INVALID_ARGUMENT. */
robo_result_t robo_desktop_begin_frame(robo_desktop_t *desktop, robo_desktop_diag_t *diag);

/** Non-zero while between `begin_frame` and `end_frame`. */
int robo_desktop_frame_is_open(const robo_desktop_t *desktop);

/**
 * Paint the frame's contents into the screen target, in the order the shell's own document defines:
 *
 *   1. the desktop surface, over the whole screen (the frame's explicit baseline);
 *   2. the panel, its separator, its falloff rows, the identity mark and the three labels;
 *   3. each window in z-order: its border, its title bar, its contents, and its close button;
 *   4. nothing else — the status label is part of (2), drawn last within the panel.
 *
 * Called without `begin_frame`, or twice in one frame, it is refused rather than silently nested: a frame
 * whose halves were painted twice would composite every translucent thing twice, and the record would
 * say so only if the counters happened to be watched.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT without an open frame; ROBO_NOT_SUPPORTED with a `WINDOW`
 *         diagnostic when a window's contents could not be rasterised, in which case that window's
 *         decorations are still painted and its body is left as the shell cleared it.
 */
robo_result_t robo_desktop_paint(robo_desktop_t *desktop, robo_desktop_diag_t *diag);

/** End a frame, describing it in @p out_record (may be NULL). ROBO_INVALID_ARGUMENT without a begin. */
robo_result_t robo_desktop_end_frame(robo_desktop_t *desktop,
                                    robo_desktop_frame_record_t *out_record,
                                    robo_desktop_diag_t *diag);

/** `begin_frame` → `paint` → `end_frame` in one call. The shorthand for a caller with nothing between. */
robo_result_t robo_desktop_render(robo_desktop_t *desktop,
                                 robo_desktop_frame_record_t *out_record,
                                 robo_desktop_diag_t *diag);

/**
 * The frame's pixels, **borrowed**; NULL when no desktop or no frame has ever been painted.
 *
 * The door for everything this module does not do with a frame: reading a pixel, encoding a screenshot,
 * handing it to a display backend, or presenting it through @ref robo_desktop_present_to_surface. Valid
 * until the next `resize` or until the desktop is destroyed.
 */
const robo_gfx_surface_t *robo_desktop_frame_surface(const robo_desktop_t *desktop);

/** The frame target's checksum: the current picture, whether or not a frame was just painted. */
uint64_t robo_desktop_frame_checksum(const robo_desktop_t *desktop);

/** The frame target's bytes, contiguous and read-only; @p out_size filled when non-NULL. */
const uint8_t *robo_desktop_frame_bytes(const robo_desktop_t *desktop, size_t *out_size);

/**
 * Resize the screen.
 *
 * The sequence the phase's §17 asks for, in one call: a new target of the new size, geometry recomputed,
 * every window clamped into the new work area, and the frame marked as needing a paint. The target's
 * pixels are **not** carried over: a resized screen has no previous frame to show (the old one was the
 * wrong shape), so the next frame begins from the target's zero state and paints a whole picture. A no-op
 * resize is not a no-op: it recreates nothing and reports ROBO_OK, because a caller polling a window's
 * size must not force a reallocation every frame.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p desktop, a non-positive or oversized dimension, or
 *         a height that leaves no panel.
 */
robo_result_t robo_desktop_resize(robo_desktop_t *desktop,
                                 int32_t width,
                                 int32_t height,
                                 robo_desktop_diag_t *diag);

/**
 * Match the screen to the attached native window's physical client size.
 *
 * The one call a driven shell makes each iteration: a window that was resized by the user reports a new
 * size, and the desktop follows it. No attached window → ROBO_NOT_SUPPORTED with a `SURFACE` diagnostic;
 * an unchanged size → ROBO_OK and nothing reallocated.
 *
 * @p out_resized, when non-NULL, receives 1 when the screen changed and 0 when it did not.
 */
robo_result_t robo_desktop_sync_size(robo_desktop_t *desktop,
                                    int *out_resized,
                                    robo_desktop_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Presentation                                                        */
/* ------------------------------------------------------------------ */

/**
 * Present the current frame into a caller's surface at (@p x, @p y).
 *
 * A thin door over `robo_render_present` (11.13's other half): the pixels are the frame's, the
 * destination is the caller's, and @p out_record describes what landed. This is the door a guest build
 * uses to hand a frame to a display framebuffer it wrapped as a surface, and a test uses to check a
 * frame without a window.
 *
 * @return as @ref robo_render_present; ROBO_INVALID_ARGUMENT for a NULL @p desktop or @p dest.
 */
robo_result_t robo_desktop_present_to_surface(const robo_desktop_t *desktop,
                                              robo_gfx_surface_t *dest,
                                              int32_t x,
                                              int32_t y,
                                              robo_render_present_record_t *out_record,
                                              robo_desktop_diag_t *diag);

/**
 * Present the current frame into the attached native window.
 *
 * The sequence is RoboWindow's own frame triple: `begin_frame` (which gives the window's owned render
 * surface, sized to the window's *physical* client size), the presenter, `end_frame` (which marks the
 * window dirty) and `present_frame` (which reaches the backend — a Win32 client area, a QEMU framebuffer).
 * Nothing is paced: this call waits for no vertical blank, and says so rather than implying a frame rate.
 *
 * A frame whose size differs from the window's (a resize the desktop has not been told about) is **clipped**
 * by the presenter and reported, exactly as `robo_render_present` documents — the caller's next
 * @ref robo_desktop_sync_size closes the gap, and a frame that refused to present would show nothing at
 * all in the meantime.
 *
 * @p out_record may be NULL. @p out_present, when non-NULL, receives whether the *window* accepted the
 * frame (a window that was not dirty presents nothing and is not an error).
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p desktop; ROBO_NOT_SUPPORTED with a `SURFACE`
 *         diagnostic when no native window is attached, or the window could not give a render surface.
 */
robo_result_t robo_desktop_present(robo_desktop_t *desktop,
                                  robo_render_present_record_t *out_record,
                                  int *out_presented,
                                  robo_desktop_diag_t *diag);

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

/** What the shell has done since it was created: the counters a smoke test asserts. */
typedef struct robo_desktop_stats {
    /** Frames ended. */
    size_t frames;
    /** Frames that painted **no** window: the "is a desktop without applications still a desktop?" case. */
    size_t frames_without_windows;
    /** Window contents rasterised across every frame so far. */
    size_t content_runs;
    /** Window content pixels composited across every frame so far. */
    size_t content_pixels;
    /** Labels drawn across every frame so far. */
    size_t labels;
    /** Labels skipped for lack of a face across every frame so far. */
    size_t labels_unshaped;
    /** Shell fills executed across every frame so far: the surface, the panel, the windows' parts. */
    size_t fill_drawings;
    /** Shell drawings the screen's own bounds cut across every frame so far. */
    size_t clipped_drawings;
    /** Successful presentations. */
    size_t presentations;
    /** Resizes that actually changed the screen. */
    size_t resizes;
    /** Windows added since creation (a monotonic count; the live count is `robo_desktop_window_count`). */
    size_t windows_added;
    /** Windows destroyed since creation. */
    size_t windows_destroyed;
    /** Windows closed through the close button or @ref robo_desktop_window_request_close. */
    size_t windows_closed;
    /** Pointer presses the shell acted on: a raise, a focus or a close. */
    size_t pointer_actions;
    /** Pointer moves observed. */
    size_t pointer_moves;
    /** The checksum of the last frame that was painted. */
    uint64_t last_frame_checksum;
} robo_desktop_stats_t;

/** Copy the shell's counters. ROBO_INVALID_ARGUMENT for NULL arguments. */
robo_result_t robo_desktop_stats(const robo_desktop_t *desktop, robo_desktop_stats_t *out);

/** Pointer state: the last point and what was under it. */
typedef struct robo_desktop_pointer {
    robo_gfx_point_t point;
    int inside;
    int pressed;
    robo_desktop_area_t area;
    robo_desktop_window_id_t window;
} robo_desktop_pointer_t;

/** Copy the pointer state. ROBO_INVALID_ARGUMENT for NULL arguments. */
robo_result_t robo_desktop_pointer(const robo_desktop_t *desktop, robo_desktop_pointer_t *out);

/** Characters one line of the shell dump can need. */
#define ROBO_DESKTOP_DUMP_LINE_MAX 200u

/**
 * Write @p desktop as text: **seven** lines, one per group of facts, each ending in a newline.
 *
 * The developer's door, in the shape `robo_render_frame_write` uses: no `stdio`, no address anywhere in
 * the text, and a buffer that is too small reports the length the caller would have needed rather than
 * truncating. The shape:
 *
 *     desktop 1280x800 panel=0,0,1280,30 work=0,30,1280,770
 *       windows=1 visible=1 focused=3
 *       frames=4 without_windows=1 resizes=1
 *       fills=42 labels=6 unshaped=0 clipped=0
 *       content_runs=3 content_pixels=307200 presentations=3
 *       pointer=12,40 inside=1 pressed=0 area=window-content
 *       checksum=2f1c9e0b7a4d3355
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p desktop, @p buffer, @p out_length or a zero
 *         @p capacity; ROBO_NOT_SUPPORTED with a `DUMP` diagnostic when the text does not fit, in which
 *         case @p out_length receives the length it needed.
 */
robo_result_t robo_desktop_write(const robo_desktop_t *desktop,
                                char *buffer,
                                size_t capacity,
                                size_t *out_length,
                                robo_desktop_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_H */
