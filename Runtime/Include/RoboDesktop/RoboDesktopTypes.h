/*
 * RoboDesktop identities, limits and the two metric records (Phase 11.13).
 *
 * Two kinds of identity, and the difference matters: a **shell window** is the desktop's own object
 * (`DesktopWindowId`, a token the desktop hands out), while a **native window** is RoboWindow's
 * (`WindowId`) and belongs to the platform. A shell window may be *attached* to a native window
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
typedef struct Desktop Desktop;

/** A window the shell knows about. `0` means "none", exactly as every other id in this tree does. */
typedef uint64_t DesktopWindowId;

#define RoboDesktopWindowIdNone ((DesktopWindowId)0u)

/**
 * Most shell windows at once.
 *
 * A bound rather than a growable list, and the reason is the paint order: every frame composes every
 * window in order, so the count is also the per-frame cost. Eight is what a first desktop needs (the
 * frame the phase's brief asks for, plus room to prove `raise` and `close` change what is on screen)
 * and it is small enough that the entire list's geometry is arithmetic a suite can pin.
 */
#define RoboDesktopMaxWindows 8u

/** Copied UTF-8 title capacity, including the NUL. */
#define RoboDesktopTitleMax 128u

/** Copied status-text capacity, including the NUL. */
#define RoboDesktopStatusMax 64u

/** Largest width or height a desktop frame may have, in pixels. */
#define RoboDesktopMaxDimension 16384u

/**
 * Smallest work area a window may be placed in, per axis.
 *
 * Below this a screen is not drawable *as a desktop*, so `DesktopWindowAdd` **refuses** rather
 * than placing a window whose decorations would not fit -- the same reasoning as the decoration minimum
 * one step down, applied to the space instead of to the frame. Tested on both axes, because a 400x4
 * work area is as unplaceable as a 4x400 one.
 *
 * The *geometry* does not refuse a thin work area (`RoboDesktopGeometry.h` is total): it reports the
 * rectangles a screen has, and this constant is what the one door that decides "fits" measures against.
 */
#define RoboDesktopMinDimension 16

/**
 * The panel (taskbar) metrics.
 *
 * `height` includes the separator row, and the work area begins at `y = height`, so the panel is a
 * band of the screen and the separator is the last row inside it. `identityInset` is the distance from
 * the screen's left edge to the identity mark, `labelInset` the distance from the right edge to the
 * status text -- two numbers rather than one because a panel whose identity hugged the corner the way
 * its clock does would read as a rendering accident.
 */
typedef struct RoboDesktopPanelMetrics {
    int32_t height;
    int32_t identityInset;
    int32_t identityMarkWidth;
    int32_t identityMarkHeight;
    int32_t labelInset;
    int32_t separatorHeight;
    /** Baseline distance above the panel's bottom edge; see `RoboDesktopChrome.c`. */
    int32_t baselineFromBottom;
} DesktopPanelMetrics;

/**
 * A shell window's decoration metrics.
 *
 * Every number is in the window's *outer* rectangle's coordinates: `border` is the frame's thickness on
 * all four sides, `titleHeight` the title bar's height measured from the top border's inner edge, and
 * the close button is `closeSize` square, inset `closeInset` from the frame's right edge and centred
 * in the title bar.
 */
typedef struct RoboDesktopWindowMetrics {
    int32_t border;
    int32_t titleHeight;
    int32_t titleInset;
    int32_t closeSize;
    int32_t closeInset;
    int32_t minWidth;
    int32_t minHeight;
    /** A window's contents are inset from the title bar's inner edge by this much. */
    int32_t contentInset;
    /**
     * How thick the *interactive* band along a window's outer edge is, for resizing (Phase 13.67).
     *
     * Deliberately wider than @ref border: a one-pixel frame is what the shell *draws*, and a one-pixel
     * target is not something a hand can hit. The band is measured inwards from the frame rectangle, corners
     * are `resizeBand` square, and the close button wins over the top-right corner so a resizable window
     * still has a close button a user can press.
     *
     * 0 turns resizing off entirely, which is a legitimate style rather than a degenerate one: a window whose
     * decorations say it cannot be resized is a window the hit test never answers a resize zone for.
     */
    int32_t resizeBand;
} DesktopWindowMetrics;

/**
 * Where a point landed.
 *
 * The answer the pointer door produces, and the reason a desktop that cannot route input is still worth
 * having: this is the vocabulary an *application* would eventually be handed, and it is stated now
 * because the shell's own two actions (focus, close) are expressed in it rather than in pixels.
 */
typedef enum DesktopArea {
    /** No shell object: outside the screen entirely. */
    RoboDesktopAreaNone = 0,
    /** The desktop's own surface, below the panel and not over any window. */
    RoboDesktopAreaBackground = 1,
    /** Anywhere in the panel. */
    RoboDesktopAreaPanel = 2,
    /** A window's title bar, excluding its close button. */
    RoboDesktopAreaWindowTitle = 3,
    /** A window's client area: where its contents are. */
    RoboDesktopAreaWindowContent = 4,
    /** A window's close button. */
    RoboDesktopAreaWindowClose = 5,
    /*
     * The eight resize zones (Phase 13.67): the interactive band inwards from a window's frame, four edges and
     * four corners, in the order a cursor reads them. The close button is tested *first* and wins, so a
     * resizable window is still closable; a corner is tested before the two edges it joins, so a corner
     * answers `top-left` rather than whichever edge happened to be checked first.
     */
    /** The left edge band, excluding the corners. */
    RoboDesktopAreaWindowResizeLeft = 6,
    /** The right edge band, excluding the corners. */
    RoboDesktopAreaWindowResizeRight = 7,
    /** The top edge band, excluding the corners. */
    RoboDesktopAreaWindowResizeTop = 8,
    /** The bottom edge band, excluding the corners. */
    RoboDesktopAreaWindowResizeBottom = 9,
    RoboDesktopAreaWindowResizeTopLeft = 10,
    RoboDesktopAreaWindowResizeTopRight = 11,
    RoboDesktopAreaWindowResizeBottomLeft = 12,
    RoboDesktopAreaWindowResizeBottomRight = 13
} DesktopArea;

/** Number of defined areas. Asserted positionally by `desktop.conformance.*`. */
#define RoboDesktopAreaCount 14u

/** Non-zero if @p area is a defined area. */
int DesktopAreaIsKnown(DesktopArea area);

/**
 * Non-zero when @p area is one of the eight resize zones.
 *
 * The one door that asks "is this a resize?", so the input integration (13.67/13.75) and the window manager
 * never write `>= ResizeLeft && <= ResizeBottomRight` at a call site -- the enumeration is ordered so that
 * test would work, and the day a ninth geometry lands it would still be true while the set of zones that
 * share an edge would not.
 */
int DesktopAreaIsResize(DesktopArea area);

/** Human-readable area name; never NULL; `"unknown"` when undefined. */
const char *DesktopAreaName(DesktopArea area);

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
typedef enum DesktopWindowState {
    RoboDesktopWindowOpen = 1,
    RoboDesktopWindowClosing = 2
} DesktopWindowState;

/** Non-zero if @p state is a defined state. */
int DesktopWindowStateIsKnown(DesktopWindowState state);

/** Human-readable state name; never NULL; `"unknown"` when undefined. */
const char *DesktopWindowStateName(DesktopWindowState state);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_TYPES_H */
