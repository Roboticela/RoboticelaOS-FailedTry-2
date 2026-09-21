/*
 * RoboDesktop shell windows (Phase 11.13).
 *
 * A shell window is the desktop's object: a frame with decorations, a title, a client area, and — the
 * whole reason it exists — **contents** that are a display list produced by the pipeline below.
 *
 * ## The content seam, stated once
 *
 * `content` is a `const PaintDisplayList *`, and this module treats it exactly the way
 * RoboRender treats one: read through `const`, never rebuilt, never retained past the call that sets it
 * in the sense that matters — *the list must outlive the window's last paint*, which is the same
 * lifetime rule a RoboWindow's borrowed present surface has, and it is stated on the door below rather
 * than left to be discovered. Nothing here knows what a DOM node, a computed style or a box is: a
 * window's contents arrive as commands, which is what keeps the shell drawable when the document layer is
 * broken, empty, or not compiled in at all.
 *
 * A window **without** content is a legitimate window: it paints its body and its decorations, and its
 * record says `contentRuns == 0`. That is what makes the shell's own smoke test possible (a desktop
 * with a window and no document) and it is why `content` may be NULL rather than being a required field.
 *
 * ## Bounds are the *outer* rectangle
 *
 * Like every windowing system this one has seen, `bounds` is what the frame occupies — decorations
 * included. `DesktopWindowClientBounds` is the door for the rectangle the contents are drawn
 * into, and the difference between the two is the decoration metrics, not a constant. A caller that
 * wanted to place a window so its *contents* land somewhere specific does the subtraction itself with
 * that door, because only the caller knows which of the two it meant.
 */
#ifndef ROBO_DESKTOP_WINDOW_H
#define ROBO_DESKTOP_WINDOW_H

#include "RoboBase/RoboError.h"
#include "RoboDesktop/RoboDesktopDiagnostics.h"
#include "RoboDesktop/RoboDesktopTypes.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboPaint/RoboPaintDisplayList.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What a caller tells the desktop about a window it wants.
 *
 * A record by value with an `init` door, the same shape every configuration record in this tree has, so
 * that a field added later does not renumber a caller's initializer.
 */
typedef struct DesktopWindowDesc {
    /** UTF-8 title, copied (bounded by @ref RoboDesktopTitleMax). NULL means "no title". */
    const char *title;
    /**
     * Outer rectangle. A **negative** `x` or `y` means "place me", and the desktop answers
     * deterministically with @ref DesktopGeometryPlace; a negative width or height is refused.
     * So a caller that does not know the work area can still ask for a window and get one somewhere
     * sensible, which is the only state the first frame of a session has.
     */
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    /**
     * The window's contents: a display list this module reads and never retains ownership of. May be
     * NULL, and must outlive the window's last paint (see the header comment).
     */
    const PaintDisplayList *content;
    /**
     * The face the contents' text runs are drawn with. NULL means "whatever the desktop's own chrome face
     * is", and a desktop with no face at all draws no glyphs and counts them — the same rule
     * `RenderRasterize` states for its own `face` argument.
     */
    FontFace *contentFace;
    /** 0 hides the window at creation (a window can exist without being on screen). Default 1. */
    int visible;
} DesktopWindowDesc;

/** Clear @p desc to "a default-sized visible window at the work area's centre point, no title". */
void DesktopWindowDescInit(DesktopWindowDesc *desc);

/**
 * Validate @p desc (not against any desktop): title length, window dimensions.
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p desc, a negative width or height, a dimension above
 *         @ref RoboDesktopMaxDimension, or a title longer than @ref RoboDesktopTitleMax.
 */
Result DesktopWindowDescValidate(const DesktopWindowDesc *desc);

/** Non-zero if @p id is the "none" token. */
int DesktopWindowIdIsNone(DesktopWindowId id);

/** Non-zero if @p a and @p b name the same window (including both none). */
int DesktopWindowIdEqual(DesktopWindowId a, DesktopWindowId b);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_WINDOW_H */
