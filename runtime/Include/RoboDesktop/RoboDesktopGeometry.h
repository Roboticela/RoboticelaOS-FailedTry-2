/*
 * RoboDesktop geometry (Phase 11.13).
 *
 * The desktop's layout as a **value**: the screen, the panel, the work area, and the three areas the
 * panel is divided into. Everything here is arithmetic on rectangles, which is the point -- the phase's
 * brief asks for explicit geometry rather than hard-coded resolution handling, and the way to make that
 * a claim rather than an intention is for the arithmetic to be testable with no display, no window and
 * no frame. `SuiteRoboDesktopGeometry.c` does exactly that: every case in it is a size and a rectangle.
 *
 * ## The coordinate statement
 *
 * Screen coordinates are **device pixels**, origin at the display's top-left, `x` growing right, `y`
 * growing down, rectangles half-open (`x` inclusive, `x + width` exclusive) -- RoboGraphics' own
 * convention, taken rather than restated, so a rectangle computed here can be handed to a fill, a
 * composite or a presenter without a conversion step that could disagree with any of them.
 *
 * ## The four rectangles, and why the panel's division is part of the geometry
 *
 *   `screen`       the whole surface, `(0, 0, width, height)`;
 *   `panel`        the top band, `(0, 0, width, panel.height)` -- its *last row* is the separator, which
 *                  is a fact about painting, not about the bounds;
 *   `work_area`    everything below the panel: where windows live and what `place` lays them out in;
 *   `identity`, `applications`, `status`   three *labels'* areas inside the panel, left, centre and
 *                  right. They are in the geometry rather than computed at paint time because a hit test
 *                  ("which part of the panel was clicked?") and a paint ("where does the clock go?")
 *                  must not be able to disagree about where the middle of a panel is.
 *
 * The three panel areas are computed from the *panel*'s own bounds and the labels' measured widths, so
 * `applications` is a real centred rectangle when the caller knows how wide its text is and the whole
 * panel width when it does not -- see @ref robo_desktop_geometry_panel_area.
 */
#ifndef ROBO_DESKTOP_GEOMETRY_H
#define ROBO_DESKTOP_GEOMETRY_H

#include "RoboBase/RoboError.h"
#include "RoboDesktop/RoboDesktopTypes.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Where the panel's three labels live. */
typedef enum robo_desktop_panel_area {
    ROBO_DESKTOP_PANEL_IDENTITY = 1,
    ROBO_DESKTOP_PANEL_APPLICATIONS = 2,
    ROBO_DESKTOP_PANEL_STATUS = 3
} robo_desktop_panel_area_t;

/** The desktop's layout, by value. */
typedef struct robo_desktop_geometry {
    /** The whole surface, in device pixels. */
    robo_gfx_rect_t screen;
    /** The top band. Its last `separator_height` rows are the hairline and the falloff rows below it. */
    robo_gfx_rect_t panel;
    /** Everything below the panel. Windows are laid out inside this rectangle. */
    robo_gfx_rect_t work_area;
    /** The identity mark and the identity label, at the panel's left. */
    robo_gfx_rect_t identity;
    /** The applications label, centred in the panel. */
    robo_gfx_rect_t applications;
    /** The status label, at the panel's right. */
    robo_gfx_rect_t status;
} robo_desktop_geometry_t;

/** Clear @p geometry to a screen of nothing (every rectangle empty). Safe on NULL. */
void robo_desktop_geometry_init(robo_desktop_geometry_t *geometry);

/**
 * Compute the layout for a @p screen size and a panel @p metrics record.
 *
 * Deterministic and total for every size this build accepts: the work area is exactly what the panel
 * leaves, so it is **thin rather than negative** when a screen barely holds a panel, and every
 * rectangle is clamped to the screen, so no caller can be handed a rectangle that leaves the surface it
 * describes. A zero-sized screen, an oversized one, and a panel as tall as the screen are the three
 * refusals, because each is a caller mistake rather than a desktop with nothing on it.
 *
 * Being total is deliberate, and where "drawable" is decided is a *different* door:
 * @ref robo_desktop_window_add refuses a work area thinner than @ref ROBO_DESKTOP_MIN_DIMENSION
 * instead of placing a window the shell could not draw. Geometry answers what the rectangles are; the
 * window list decides whether a window fits in them.
 *
 * The label areas' *widths* are unknown here -- a label's width needs a font, and this header has no
 * font -- so `identity`, `applications` and `status` are the areas reserved for them: the identity area
 * is as wide as the mark plus one inset, the applications area spans the panel between the identity
 * inset and the status inset, and the status area spans the same band from the right. The painters and
 * the hit test use these; `robo_desktop_geometry_panel_area` narrows one to a measured width when a
 * caller has one.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p geometry, a NULL @p metrics, a non-positive
 *         screen dimension, or a panel height that is negative or not smaller than the screen.
 */
robo_result_t robo_desktop_geometry_compute(robo_gfx_size_t screen,
                                            const robo_desktop_panel_metrics_t *metrics,
                                            robo_desktop_geometry_t *geometry);

/**
 * The rectangle a panel label of measured width @p text_width should be painted in, inside @p area.
 *
 * Centring for `applications`, right-alignment for `status`, and left-alignment at the identity mark for
 * `identity` -- one function rather than three because all three are "narrow @p area to the text and put
 * it where that label belongs", and a caller that had to know which alignment belonged to which label
 * would be re-deriving the design each time. A @p text_width of zero answers @p area unchanged, which is
 * the "this build has no font" case and leaves the area reserved rather than collapsing it.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for an unknown @p area, a negative @p text_width, or a NULL
 *         @p out.
 */
robo_result_t robo_desktop_geometry_panel_area(const robo_desktop_geometry_t *geometry,
                                               robo_desktop_panel_area_t area,
                                               int32_t text_width,
                                               robo_gfx_rect_t *out);

/**
 * Where a @p frame_size rectangle should be placed to sit in @p work_area.
 *
 * Deterministic **centred placement**, nudged up: horizontally centred, and vertically at a third of the
 * work area's height rather than its middle, because a window at the exact centre of a screen reads as
 * lower than it is (the eye weights the space below a window as heavier than the space above it). A
 * frame larger than the work area is *not* refused here -- placement is not validation -- and the answer
 * is the work area's origin, so a caller that places first and clamps afterwards gets a sensible start.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p out, a negative frame dimension, or a NULL
 *         @p work_area.
 */
robo_result_t robo_desktop_geometry_place(robo_gfx_size_t frame_size,
                                          const robo_gfx_rect_t *work_area,
                                          robo_gfx_point_t *out);

/**
 * Clamp @p frame into @p work_area: the window that must stay on the screen.
 *
 * Two different repairs, and which one applies is decided by the sizes rather than by the caller: a
 * frame **smaller** than the work area is *moved* until it is inside (a window dragged past an edge is
 * pulled back rather than shrunk), and a frame **larger** than the work area is *shrunk* to it (a window
 * whose contents no longer fit the screen must still be visible, and a work area is the largest a window
 * may be). The result is then re-placed when shrinking left it outside, which is the resize path: a
 * display that got smaller must not leave a window with no visible pixels.
 *
 * @return ROBO_OK and a rectangle inside @p work_area -- including an empty one when the work area is
 *         empty, which is the honest answer when there is no work area to be inside of;
 *         ROBO_INVALID_ARGUMENT for a NULL argument or an invalid @p frame.
 */
robo_result_t robo_desktop_geometry_contain(robo_gfx_rect_t frame,
                                            const robo_gfx_rect_t *work_area,
                                            robo_gfx_rect_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_GEOMETRY_H */
