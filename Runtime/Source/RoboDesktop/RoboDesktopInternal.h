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
 * "recreate when stale" rule `WindowEnsureRenderSurface` uses one module over, kept here so that a
 * resize of a desktop with eight windows does not allocate eight targets before anything is drawn.
 */
typedef struct desktopWindow {
    DesktopWindowId id;
    char title[RoboDesktopTitleMax];
    /** Outer rectangle, decorations included. */
    GfxRect bounds;
    DesktopWindowState state;
    int visible;
    /** Borrowed content: see `RoboDesktopWindow.h`. */
    const PaintDisplayList *content;
    /** Borrowed face for the content's runs; NULL means the desktop's own chrome face. */
    FontFace *contentFace;
    /** Owned rasterisation target for the client area, or NULL until a frame needs it. */
    RenderTarget *target;
} DesktopWindow;

/** The shell. */
struct Desktop {
    int live;
    /** The layout, recomputed on every accepted resize. */
    DesktopGeometry geometry;
    DesktopTheme theme;
    DesktopPanelMetrics panel;
    DesktopWindowMetrics windowMetrics;
    char identity[RoboDesktopTitleMax];
    char applications[RoboDesktopTitleMax];
    char status[RoboDesktopStatusMax];
    /** Borrowed chrome face; never released by this module. */
    FontFace *face;
    float labelSizePx;
    /** The native window this shell presents into, or RoboWindowIdNone. */
    WindowId window;
    /** Non-zero when `create` made that window and `destroy` must therefore destroy it. */
    int ownsWindow;
    /** The screen target: owned, sized to `geometry.screen`, zero-filled at creation. */
    RenderTarget *target;
    int frameOpen;
    int painted;
    /**
     * The record of the frame that is currently open, kept between `paint` and `end_frame`.
     *
     * Held here rather than returned by `paint` for the reason `RoboDesktop.h` states: the sequence is
     * three calls, so the record has to live somewhere between the second and the third — and a record a
     * caller could hold between them would be a record the caller could also *write*.
     */
    DesktopFrameRecord frameRecord;
    DesktopWindow windows[RoboDesktopMaxWindows];
    size_t windowCount;
    /** Index of the focused window, or `RoboDesktopNoIndex` for none. */
    size_t focusedIndex;
    DesktopWindowId nextId;
    DesktopStats stats;
    DesktopPointer pointer;
};

/** "No window": an index that can never be one. */
#define RoboDesktopNoIndex ((size_t)-1)

/* ------------------------------------------------------------------ */
/* The window list                                                     */
/* ------------------------------------------------------------------ */

/** The window with @p id, or NULL. `RoboDesktopWindow.c` owns these two. */
const DesktopWindow *DesktopWindowFindConst(const Desktop *desktop,
                                                      DesktopWindowId id);
DesktopWindow *DesktopWindowFind(Desktop *desktop, DesktopWindowId id);

/** The window's index in z-order, or @ref RoboDesktopNoIndex. */
size_t DesktopWindowIndexOf(const Desktop *desktop, DesktopWindowId id);

/** Point focus at a window that still exists (the front one), or at none. */
void DesktopWindowRepairFocus(Desktop *desktop);

/** Release a window's content target, if it has one. */
void DesktopWindowReleaseTarget(DesktopWindow *window);

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
uint32_t DesktopChromeUtf8Next(const char *text, size_t length, size_t *offset);

/**
 * How wide @p text is at @p sizePx, by summing the face's own advances; 0 with no face.
 *
 * The measurement the panel's centring and right-alignment need, and the reason it lives with the
 * drawing rather than with the geometry: a width is a *font* fact, and the geometry header deliberately
 * has no font.
 */
int32_t DesktopChromeLabelWidth(const Desktop *desktop,
                                       const char *text,
                                       size_t length,
                                       float sizePx);

/**
 * Draw @p text with its pen at @p x and its baseline at @p baselinePx.
 *
 * @p outDrawn and @p outUnshaped are **accumulators**: each call adds one to exactly one of them — the
 * first when the label was shaped and drawn, the second when there was no face to shape it with. A caller
 * passes the frame record's own fields, so a frame's counts are the sum over every label of that frame
 * rather than the last label's; both may be NULL. No face is **not** an error: the label is counted as
 * unshaped and nothing is drawn (see `RoboDesktopVersion.h`).
 */
void DesktopChromeLabel(Desktop *desktop,
                               GfxRaster *raster,
                               int32_t x,
                               int32_t baselinePx,
                               const char *text,
                               size_t length,
                               GfxColorRgba8 color,
                               float sizePx,
                               size_t *outDrawn,
                               size_t *outUnshaped);

/** Fill @p rect, counting the drawing and whether the screen's bounds cut it. */
void DesktopChromeFill(GfxRaster *raster,
                              GfxRect rect,
                              GfxColorRgba8 color,
                              DesktopFrameRecord *record);

/** Paint the desktop surface, the panel, its labels and the window-list chrome. */
Result DesktopChromePaintShell(Desktop *desktop,
                                             GfxRaster *raster,
                                             DesktopFrameRecord *record,
                                             DesktopDiag *diag);

/** Paint one window's decorations for a frame, up to (not including) its contents. */
Result DesktopChromePaintWindowFrame(Desktop *desktop,
                                                    const DesktopWindow *window,
                                                    int focused,
                                                    GfxRaster *raster,
                                                    DesktopFrameRecord *record,
                                                    DesktopDiag *diag);

/** Paint a window's close button: the fill and the cross drawn with two lines (no face needed). */
void DesktopChromePaintCloseButton(Desktop *desktop,
                                           const DesktopWindow *window,
                                           GfxRaster *raster,
                                           DesktopFrameRecord *record);

/** Paint a window's title bar's label. */
void DesktopChromePaintWindowTitle(Desktop *desktop,
                                           const DesktopWindow *window,
                                           GfxRaster *raster,
                                           DesktopFrameRecord *record);

/** The rectangle a window's contents occupy: the client area. */
GfxRect DesktopChromeClientBounds(const Desktop *desktop,
                                                 const DesktopWindow *window);

/** The rectangle a window's close button occupies. */
GfxRect DesktopChromeCloseBounds(const Desktop *desktop,
                                                const DesktopWindow *window);

/** The rectangle a window's title bar occupies (the whole band between the borders). */
GfxRect DesktopChromeTitleBounds(const Desktop *desktop,
                                                const DesktopWindow *window);

/** Case-insensitive-free ASCII/UTF-8 compare, so this module needs no `strcmp`. */
int DesktopStringEqual(const char *a, const char *b);

/** Length of a NUL-terminated string, or 0 for NULL. */
size_t DesktopStringLength(const char *text);

/** Copy @p text into @p out (bounded), NUL-terminating. Returns RoboInvalidArgument if too long. */
Result DesktopStringCopy(char *out, size_t capacity, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_INTERNAL_H */
