/*
 * RoboDesktop private declarations (Phase 11.13).
 *
 * Not installed: nothing public includes this, and the files that do are this module's own. The two
 * things in it are the shell's state and the chrome's drawing helpers, and both are here rather than in
 * a public header for the same reason `RoboPaintInternal.h` states: a caller that could see the shell's
 * fields would be able to write one, and a frame's correctness would stop being this module's to keep.
 */
#ifndef ROBO_DESKTOP_INTERNAL_H
#define ROBO_DESKTOP_INTERNAL_H

#include "RoboDesktop/RoboDesktop.h"

#include "RoboGraphics/RoboGraphicsRaster.h"
#include "RoboRender/RoboRenderRasterizer.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One shell window.
 *
 * The content *target* is owned and lazily created: a window with no contents never allocates one, and a
 * window whose size changed gets a new one at the next paint rather than at the resize — which is the same
 * "recreate when stale" rule `robo_window_ensure_render_surface` uses one module over, kept here so that a
 * resize of a desktop with eight windows does not allocate eight targets before anything is drawn.
 */
typedef struct desktop_window {
    robo_desktop_window_id_t id;
    char title[ROBO_DESKTOP_TITLE_MAX];
    /** Outer rectangle, decorations included. */
    robo_gfx_rect_t bounds;
    robo_desktop_window_state_t state;
    int visible;
    /** Borrowed content: see `RoboDesktopWindow.h`. */
    const robo_paint_display_list_t *content;
    /** Borrowed face for the content's runs; NULL means the desktop's own chrome face. */
    robo_font_face_t *content_face;
    /** Owned rasterisation target for the client area, or NULL until a frame needs it. */
    robo_render_target_t *target;
} desktop_window_t;

/** The shell. */
struct robo_desktop {
    int live;
    /** The layout, recomputed on every accepted resize. */
    robo_desktop_geometry_t geometry;
    robo_desktop_theme_t theme;
    robo_desktop_panel_metrics_t panel;
    robo_desktop_window_metrics_t window_metrics;
    char identity[ROBO_DESKTOP_TITLE_MAX];
    char applications[ROBO_DESKTOP_TITLE_MAX];
    char status[ROBO_DESKTOP_STATUS_MAX];
    /** Borrowed chrome face; never released by this module. */
    robo_font_face_t *face;
    float label_size_px;
    /** The native window this shell presents into, or ROBO_WINDOW_ID_NONE. */
    robo_window_id_t window;
    /** Non-zero when `create` made that window and `destroy` must therefore destroy it. */
    int owns_window;
    /** The screen target: owned, sized to `geometry.screen`, zero-filled at creation. */
    robo_render_target_t *target;
    int frame_open;
    int painted;
    /**
     * The record of the frame that is currently open, kept between `paint` and `end_frame`.
     *
     * Held here rather than returned by `paint` for the reason `RoboDesktop.h` states: the sequence is
     * three calls, so the record has to live somewhere between the second and the third — and a record a
     * caller could hold between them would be a record the caller could also *write*.
     */
    robo_desktop_frame_record_t frame_record;
    desktop_window_t windows[ROBO_DESKTOP_MAX_WINDOWS];
    size_t window_count;
    /** Index of the focused window, or `ROBO_DESKTOP_NO_INDEX` for none. */
    size_t focused_index;
    robo_desktop_window_id_t next_id;
    robo_desktop_stats_t stats;
    robo_desktop_pointer_t pointer;
};

/** "No window": an index that can never be one. */
#define ROBO_DESKTOP_NO_INDEX ((size_t)-1)

/* ------------------------------------------------------------------ */
/* The window list                                                     */
/* ------------------------------------------------------------------ */

/** The window with @p id, or NULL. `RoboDesktopWindow.c` owns these two. */
const desktop_window_t *robo_desktop_window_find_const(const robo_desktop_t *desktop,
                                                      robo_desktop_window_id_t id);
desktop_window_t *robo_desktop_window_find(robo_desktop_t *desktop, robo_desktop_window_id_t id);

/** The window's index in z-order, or @ref ROBO_DESKTOP_NO_INDEX. */
size_t robo_desktop_window_index_of(const robo_desktop_t *desktop, robo_desktop_window_id_t id);

/** Point focus at a window that still exists (the front one), or at none. */
void robo_desktop_window_repair_focus(robo_desktop_t *desktop);

/** Release a window's content target, if it has one. */
void robo_desktop_window_release_target(desktop_window_t *window);

/* ------------------------------------------------------------------ */
/* The chrome                                                          */
/* ------------------------------------------------------------------ */

/**
 * Advance a UTF-8 decoder by one code point.
 *
 * Returns the code point and moves @p offset past it, or `U+FFFD` and one byte when the bytes are not a
 * well-formed sequence -- the same recovery every decoder in this tree uses, and the reason a title
 * carrying a stray byte is a title with one replacement character rather than a title that is not drawn.
 */
uint32_t robo_desktop_chrome_utf8_next(const char *text, size_t length, size_t *offset);

/**
 * How wide @p text is at @p size_px, by summing the face's own advances; 0 with no face.
 *
 * The measurement the panel's centring and right-alignment need, and the reason it lives with the
 * drawing rather than with the geometry: a width is a *font* fact, and the geometry header deliberately
 * has no font.
 */
int32_t robo_desktop_chrome_label_width(const robo_desktop_t *desktop,
                                       const char *text,
                                       size_t length,
                                       float size_px);

/**
 * Draw @p text with its pen at @p x and its baseline at @p baseline_px.
 *
 * @p out_drawn and @p out_unshaped are **accumulators**: each call adds one to exactly one of them — the
 * first when the label was shaped and drawn, the second when there was no face to shape it with. A caller
 * passes the frame record's own fields, so a frame's counts are the sum over every label of that frame
 * rather than the last label's; both may be NULL. No face is **not** an error: the label is counted as
 * unshaped and nothing is drawn (see `RoboDesktopVersion.h`).
 */
void robo_desktop_chrome_label(robo_desktop_t *desktop,
                               robo_gfx_raster_t *raster,
                               int32_t x,
                               int32_t baseline_px,
                               const char *text,
                               size_t length,
                               robo_gfx_color_rgba8_t color,
                               float size_px,
                               size_t *out_drawn,
                               size_t *out_unshaped);

/** Fill @p rect, counting the drawing and whether the screen's bounds cut it. */
void robo_desktop_chrome_fill(robo_gfx_raster_t *raster,
                              robo_gfx_rect_t rect,
                              robo_gfx_color_rgba8_t color,
                              robo_desktop_frame_record_t *record);

/** Paint the desktop surface, the panel, its labels and the window-list chrome. */
robo_result_t robo_desktop_chrome_paint_shell(robo_desktop_t *desktop,
                                             robo_gfx_raster_t *raster,
                                             robo_desktop_frame_record_t *record,
                                             robo_desktop_diag_t *diag);

/** Paint one window's decorations for a frame, up to (not including) its contents. */
robo_result_t robo_desktop_chrome_paint_window_frame(robo_desktop_t *desktop,
                                                    const desktop_window_t *window,
                                                    int focused,
                                                    robo_gfx_raster_t *raster,
                                                    robo_desktop_frame_record_t *record,
                                                    robo_desktop_diag_t *diag);

/** Paint a window's close button: the fill and the cross drawn with two lines (no face needed). */
void robo_desktop_chrome_paint_close_button(robo_desktop_t *desktop,
                                           const desktop_window_t *window,
                                           robo_gfx_raster_t *raster,
                                           robo_desktop_frame_record_t *record);

/** Paint a window's title bar's label. */
void robo_desktop_chrome_paint_window_title(robo_desktop_t *desktop,
                                           const desktop_window_t *window,
                                           robo_gfx_raster_t *raster,
                                           robo_desktop_frame_record_t *record);

/** The rectangle a window's contents occupy: the client area. */
robo_gfx_rect_t robo_desktop_chrome_client_bounds(const robo_desktop_t *desktop,
                                                 const desktop_window_t *window);

/** The rectangle a window's close button occupies. */
robo_gfx_rect_t robo_desktop_chrome_close_bounds(const robo_desktop_t *desktop,
                                                const desktop_window_t *window);

/** The rectangle a window's title bar occupies (the whole band between the borders). */
robo_gfx_rect_t robo_desktop_chrome_title_bounds(const robo_desktop_t *desktop,
                                                const desktop_window_t *window);

/** Case-insensitive-free ASCII/UTF-8 compare, so this module needs no `strcmp`. */
int robo_desktop_string_equal(const char *a, const char *b);

/** Length of a NUL-terminated string, or 0 for NULL. */
size_t robo_desktop_string_length(const char *text);

/** Copy @p text into @p out (bounded), NUL-terminating. Returns ROBO_INVALID_ARGUMENT if too long. */
robo_result_t robo_desktop_string_copy(char *out, size_t capacity, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_INTERNAL_H */
