/*
 * RoboDesktop identities, limits and the two metric records (Phase 11.13).
 *
 * Two kinds of identity, and the difference matters: a **shell window** is the desktop's own object
 * (`robo_desktop_window_id_t`, a token the desktop hands out), while a **native window** is RoboWindow's
 * (`robo_window_id_t`) and belongs to the platform. A shell window may be *attached* to a native window
 * -- which is what makes it visible -- and a desktop is usually attached to one itself; but the two
 * identities are never the same value, because a desktop with no display at all (a test, a headless
 * build) is a legitimate desktop that still paints, and a type that conflated the two would make that
 * state unexpressible.
 *
 * The metrics are records rather than constants so a caller can restyle the shell without a new API,
 * and their defaults are the ones this build ships (`desktop/docs/Architecture.md` records the
 * design reasoning for each number). They are geometry, not appearance: the theme
 * (`RoboDesktopTheme.h`) is appearance and is a separate record for exactly that reason.
 */
#ifndef ROBO_DESKTOP_TYPES_H
#define ROBO_DESKTOP_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** An opaque desktop shell this process owns. */
typedef struct robo_desktop robo_desktop_t;

/** A window the shell knows about. `0` means "none", exactly as every other id in this tree does. */
typedef uint64_t robo_desktop_window_id_t;

#define ROBO_DESKTOP_WINDOW_ID_NONE ((robo_desktop_window_id_t)0u)

/**
 * Most shell windows at once.
 *
 * A bound rather than a growable list, and the reason is the paint order: every frame composes every
 * window in order, so the count is also the per-frame cost. Eight is what a first desktop needs (the
 * frame the phase's brief asks for, plus room to prove `raise` and `close` change what is on screen)
 * and it is small enough that the entire list's geometry is arithmetic a suite can pin.
 */
#define ROBO_DESKTOP_MAX_WINDOWS 8u

/** Copied UTF-8 title capacity, including the NUL. */
#define ROBO_DESKTOP_TITLE_MAX 128u

/** Copied status-text capacity, including the NUL. */
#define ROBO_DESKTOP_STATUS_MAX 64u

/** Largest width or height a desktop frame may have, in pixels. */
#define ROBO_DESKTOP_MAX_DIMENSION 16384u

/**
 * Smallest work area a window may be placed in, per axis.
 *
 * Below this a screen is not drawable *as a desktop*, so `robo_desktop_window_add` **refuses** rather
 * than placing a window whose decorations would not fit -- the same reasoning as the decoration minimum
 * one step down, applied to the space instead of to the frame. Tested on both axes, because a 400x4
 * work area is as unplaceable as a 4x400 one.
 *
 * The *geometry* does not refuse a thin work area (`RoboDesktopGeometry.h` is total): it reports the
 * rectangles a screen has, and this constant is what the one door that decides "fits" measures against.
 */
#define ROBO_DESKTOP_MIN_DIMENSION 16

/**
 * The panel (taskbar) metrics.
 *
 * `height` includes the separator row, and the work area begins at `y = height`, so the panel is a
 * band of the screen and the separator is the last row inside it. `identity_inset` is the distance from
 * the screen's left edge to the identity mark, `label_inset` the distance from the right edge to the
 * status text -- two numbers rather than one because a panel whose identity hugged the corner the way
 * its clock does would read as a rendering accident.
 */
typedef struct robo_desktop_panel_metrics {
    int32_t height;
    int32_t identity_inset;
    int32_t identity_mark_width;
    int32_t identity_mark_height;
    int32_t label_inset;
    int32_t separator_height;
    /** Baseline distance above the panel's bottom edge; see `RoboDesktopChrome.c`. */
    int32_t baseline_from_bottom;
} robo_desktop_panel_metrics_t;

/**
 * A shell window's decoration metrics.
 *
 * Every number is in the window's *outer* rectangle's coordinates: `border` is the frame's thickness on
 * all four sides, `title_height` the title bar's height measured from the top border's inner edge, and
 * the close button is `close_size` square, inset `close_inset` from the frame's right edge and centred
 * in the title bar.
 */
typedef struct robo_desktop_window_metrics {
    int32_t border;
    int32_t title_height;
    int32_t title_inset;
    int32_t close_size;
    int32_t close_inset;
    int32_t min_width;
    int32_t min_height;
    /** A window's contents are inset from the title bar's inner edge by this much. */
    int32_t content_inset;
} robo_desktop_window_metrics_t;

/**
 * Where a point landed.
 *
 * The answer the pointer door produces, and the reason a desktop that cannot route input is still worth
 * having: this is the vocabulary an *application* would eventually be handed, and it is stated now
 * because the shell's own two actions (focus, close) are expressed in it rather than in pixels.
 */
typedef enum robo_desktop_area {
    /** No shell object: outside the screen entirely. */
    ROBO_DESKTOP_AREA_NONE = 0,
    /** The desktop's own surface, below the panel and not over any window. */
    ROBO_DESKTOP_AREA_BACKGROUND = 1,
    /** Anywhere in the panel. */
    ROBO_DESKTOP_AREA_PANEL = 2,
    /** A window's title bar, excluding its close button. */
    ROBO_DESKTOP_AREA_WINDOW_TITLE = 3,
    /** A window's client area: where its contents are. */
    ROBO_DESKTOP_AREA_WINDOW_CONTENT = 4,
    /** A window's close button. */
    ROBO_DESKTOP_AREA_WINDOW_CLOSE = 5
} robo_desktop_area_t;

/** Number of defined areas. Asserted positionally by `desktop.conformance.*`. */
#define ROBO_DESKTOP_AREA_COUNT 6u

/** Non-zero if @p area is a defined area. */
int robo_desktop_area_is_known(robo_desktop_area_t area);

/** Human-readable area name; never NULL; `"unknown"` when undefined. */
const char *robo_desktop_area_name(robo_desktop_area_t area);

/**
 * A shell window's lifecycle.
 *
 * Three states, and the middle one is the interesting: `CLOSING` is what the close button produces and
 * what separates "the user asked for this window to go away" from "the shell has forgotten it". A
 * closing window stops painting and stays in the list until the owner destroys it, so a caller that
 * keeps state per window (an application, a document, an unsaved edit) is told before the object
 * vanishes rather than after. That is a decision about *who owns a window's lifetime* and it is stated
 * here because it is the one place the desktop deliberately does less than it could.
 */
typedef enum robo_desktop_window_state {
    ROBO_DESKTOP_WINDOW_OPEN = 1,
    ROBO_DESKTOP_WINDOW_CLOSING = 2
} robo_desktop_window_state_t;

/** Non-zero if @p state is a defined state. */
int robo_desktop_window_state_is_known(robo_desktop_window_state_t state);

/** Human-readable state name; never NULL; `"unknown"` when undefined. */
const char *robo_desktop_window_state_name(robo_desktop_window_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_TYPES_H */
