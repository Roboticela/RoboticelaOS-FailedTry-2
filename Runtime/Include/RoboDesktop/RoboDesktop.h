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
 * controls where the frame starts and stops. `DesktopRender` is the one-call shorthand for the
 * caller that has nothing to interleave — and the suite uses both, so the shorthand cannot drift from the
 * sequence it abbreviates.
 *
 * **A frame does not present itself.** `DesktopPresent` is a separate door, and the separation is
 * deliberate rather than convenient: it is what lets a caller render a frame and *look at it* without a
 * display (a test, a screenshot, a headless build), and what lets the same rendered frame be presented
 * twice or into two destinations. A frame that presented itself would make both of those impossible and
 * would hide the "did it reach the screen?" answer inside a call that also drew things.
 *
 * ## The pixel target, and what this module does *not* add
 *
 * The screen target is a `RenderTarget` (RoboRender's owned RGBA8 buffer) and nothing else: this
 * phase adds **no framebuffer type of its own**. The tree already has two, and each has an owner and a
 * reason — `GfxFramebuffer` (RoboGraphics, Goal 6.3) is an owned-or-borrowed pixel store with an
 * explicit stride, and `QemuFbInfo` (RoboDisplay, Goal 7.12) is the UEFI/QEMU mapping with its
 * format and pitch. A third would be a third name for the same bytes, and the interesting question — "can
 * a frame reach that mapping?" — is answered by `RenderPresentFramebuffer`, which takes RoboGraphics'
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
 * struct of counts. Two frames of one state are byte-identical, and `DesktopFrameChecksum` is the
 * door that lets a caller check it rather than believe it.
 */
#ifndef ROBO_DESKTOP_H
#define ROBO_DESKTOP_H

#include "RoboBase/RoboError.h"
#include "RoboDesktop/RoboDesktopCursorPack.h"
#include "RoboDesktop/RoboDesktopDiagnostics.h"
#include "RoboDesktop/RoboDesktopGeometry.h"
#include "RoboDesktop/RoboDesktopTheme.h"
#include "RoboDesktop/RoboDesktopTypes.h"
#include "RoboDesktop/RoboDesktopVersion.h"
#include "RoboDesktop/RoboDesktopWindow.h"

#include "RoboFont/RoboFontFace.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboTypography/RoboTypographyResolver.h"
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

const char *DesktopVersionString(void);

Result DesktopGetVersion(I32 *outMajor,
                                      I32 *outMinor,
                                      I32 *outPatch);

/** Non-zero when a native desktop shell exists in this build. See `RoboDesktopShellImplemented`. */
int DesktopShellIsImplemented(void);

/**
 * What this build's desktop does and does not do.
 *
 * Read positionally or by name, never by count — the device `RoboPaint` and `RoboRender` established, and
 * asserted field-by-field by `desktop.conformance.capabilities`, because an untested capability flag is a
 * flag that stops being true without anybody noticing.
 */
typedef struct DesktopCapabilities {
    /** The shell itself: an owned screen target, a background, a panel and a work area (11.13). */
    int shell;
    /** Shell windows: decorations, a title, a client area and z-order. */
    int windows;
    /** Rasterising a window's contents from a display list and compositing them under the chrome. */
    int windowContent;
    /** Presenting the frame into a destination surface, or into a RoboWindow's own render surface. */
    int presentation;
    /** A deterministic frame record and a byte checksum. */
    int frameRecords;
    /** Recomputing geometry and revalidating windows after a size change. */
    int resize;
    /** Hit-testing a point to the shell object under it, and the two actions that follow (focus, close). */
    int pointerTargeting;
    /** Chrome text through a caller-supplied face, counted and skipped when there is none. */
    int shellText;
    /** An application document (HTML → CSS → layout → paint → display list). **0**: see below. */
    int documents;
    /** A layer tree with retained wallpaper / panel / window surfaces (Phase 15 wiring). */
    int compositor;
    /** Independently repaintable pieces of the screen. **0**: tile grids stay in RoboCompositor. */
    int tiles;
    /** Composition damage and region present so unchanged layers are not redrawn. */
    int damageTracking;
    /** A clock, a deadline and a queue. **0**: and this module's determinism depends on the absence. */
    int frameScheduling;
    /** Waiting for a display's vertical blank. **0**: presentation is synchronous and unpaced. */
    int vsync;
    /** A GPU-accelerated *present* path when the window backend has one (host D3D11).
     *  Compose stays CPU/software (RoboCompositor); this flag does not mean GPU textures. */
    int gpu;
    /** A window manager: snapping, tiling, virtual desktops, animation, maximize and minimize. **0**. */
    int windowManager;
    /** Keyboard routing and input delivery to applications. **0**: §16 of the phase says where it ends. */
    int inputDelivery;
    /** A desktop environment: launcher, task manager, settings, notifications. **0**. */
    int desktopEnvironment;
} DesktopCapabilities;

/** Zero @p capabilities (every feature reported absent). Safe on NULL. */
void DesktopCapabilitiesInit(DesktopCapabilities *capabilities);

/** Fill @p out with this build's capabilities.
 *  @return RoboOk; RoboInvalidArgument for NULL @p out. */
Result DesktopGetCapabilities(DesktopCapabilities *out);

/* ------------------------------------------------------------------ */
/* Metrics                                                             */
/* ------------------------------------------------------------------ */

/** This build's panel metrics. Safe on NULL (no-op). */
void DesktopPanelMetricsInit(DesktopPanelMetrics *metrics);

/** Non-zero when @p metrics describes a panel this build can lay out. */
int DesktopPanelMetricsIsValid(const DesktopPanelMetrics *metrics);

/** This build's window decoration metrics. Safe on NULL (no-op). */
void DesktopWindowMetricsInit(DesktopWindowMetrics *metrics);

/** Non-zero when @p metrics describes decorations this build can draw. */
int DesktopWindowMetricsIsValid(const DesktopWindowMetrics *metrics);

/* ------------------------------------------------------------------ */
/* Configuration and lifetime                                          */
/* ------------------------------------------------------------------ */

typedef struct DesktopConfig {
    /** Screen size in pixels. Both dimensions must be positive; see the maxima in `RoboDesktopTypes.h`. */
    int32_t width;
    int32_t height;
    /**
     * A native window to present into, or @ref RoboWindowIdNone. A desktop with **no** window is a
     * legitimate desktop: it renders frames, and `DesktopPresent` reports that it has nowhere to
     * put them (`RoboNotSupported`) rather than failing to exist.
     */
    WindowId window;
    /**
     * Create the desktop's own native window instead of attaching an existing one. Ignored when
     * @p window is a real id. Needs `WindowInit` to have run and a backend to be registered; a
     * failure here fails the whole call, because a caller that asked for a window and got a shell with
     * nowhere to show itself would have to check two things to find that out.
     */
    int RoboCreateWindow;
    /** The native window's title; NULL means @ref RoboDesktopDefaultTitle. */
    const char *windowTitle;
    /** The identity label; NULL means @ref RoboDesktopDefaultIdentity. */
    const char *identityText;
    /** The applications label; NULL means @ref RoboDesktopDefaultApplications. */
    const char *applicationsText;
    /** The status label (a clock, a battery, a version — the caller's choice); NULL means none. */
    const char *statusText;
    /** Chrome palette; NULL means this build's default theme. */
    const DesktopTheme *theme;
    /** Panel metrics; NULL means this build's defaults. */
    const DesktopPanelMetrics *panel;
    /** Window decoration metrics; NULL means this build's defaults. */
    const DesktopWindowMetrics *windowMetrics;
    /**
     * The face chrome text is drawn with when no typography resolver is given: the identity, applications
     * and status labels, and every window's title. NULL means this build draws no chrome glyphs — the
     * geometry, the accent mark, the close cross and every fill still appear, and the frame record counts
     * the labels it could not shape (`labels` against `labelsUnshaped`). Text reaching a *window's
     * contents* is a separate face, carried per window (`DesktopWindowDesc.contentFace`).
     *
     * Superseded by @ref typography when that is set: a role the resolver can name a face for wins, and
     * this face is the fallback for a role that resolves without one. The pre-12.3 caller that passes this
     * and nothing else keeps drawing exactly the pixels it drew before — which is what makes the two
     * bindings comparable in one build.
     */
    FontFace *face;
    /** Chrome text size in pixels; 0 or negative means @ref RoboDesktopDefaultLabelSizePx. */
    float labelSizePx;
    /**
     * Optional typography resolver (Phase 12.3). When set, every chrome text surface is drawn from a
     * **semantic role** rather than from one face and one size:
     *
     *   the panel's identity     `DesktopTitle`   the applications label  `ChromeLabel`
     *   the status label         `ChromeStatus`  a window's title bar      `WindowTitle`
     *
     * Each role contributes its own face, size, weight and leading, and the role's leading and the face's
     * own ascent place the baseline (see `Documentation/Runtime/Typography/TypeBinding.md`). A role whose
     * family cannot be resolved falls back to @ref face and @ref labelSizePx, so a desktop with a pack that
     * lost a family still draws its whole shell.
     *
     * Borrowed, and mutable: the resolver owns a resolution cache and its borrowed pack/registry must
     * outlive the desktop. NULL means the pre-12.3 binding (one face, one size, one baseline rule).
     */
    TypographyResolver *typography;
    /**
     * Draw a software cursor at the last observed pointer. Default off so pixel tests stay a function of
     * the document; host and QEMU launchers turn it on so the desktop is playable.
     */
    int showCursor;
} DesktopConfig;

/** The native window's and the shell's default title. */
#define RoboDesktopDefaultTitle "RoboticelaOS"
/** The platform's name, as the panel's identity label. */
#define RoboDesktopDefaultIdentity "RoboticelaOS"
/** The panel's middle label. */
#define RoboDesktopDefaultApplications "Applications"
/** Chrome text size when a caller does not choose one. */
#define RoboDesktopDefaultLabelSizePx 11.0f

/** Fill @p config with the defaults: 1280x800, no window, the default theme and metrics, no face, no
 *  typography resolver. */
void DesktopConfigInit(DesktopConfig *config);

/**
 * Create a desktop shell.
 *
 * A shell owns its screen target (allocated here, zero-filled, sized to @p config), its geometry, its
 * theme and metrics copies, its label strings, and an empty window list. It owns a native window only when
 * it was asked to create one; an attached window stays the caller's to destroy, which is the same
 * ownership rule `WindowSetPresentSurface` states for a borrowed surface.
 *
 * @return RoboOk and @p out; RoboInvalidArgument for a NULL @p config or @p out, a non-positive or
 *         oversized dimension, an invalid theme, panel or window metrics, or an over-long label;
 *         RoboNotSupported with a `SHELL` diagnostic when @p config asks for a native window and none
 *         could be created; RoboOutOfMemory when the target could not be allocated.
 */
Result DesktopCreate(const DesktopConfig *config,
                                 Desktop **out,
                                 DesktopDiag *diag);

/**
 * Destroy @p desktop: every shell window, the screen target, and (only) a native window this shell
 * created itself. Safe on NULL. A borrowed native window is **not** destroyed — see `DesktopCreate`.
 */
void DesktopDestroy(Desktop *desktop);

/** Non-zero once a desktop exists and has not been destroyed. */
int DesktopIsLive(const Desktop *desktop);

/** Screen size in pixels (0x0 for NULL). */
GfxSize DesktopScreenSize(const Desktop *desktop);

/** The desktop's layout. A cleared geometry for NULL. */
DesktopGeometry DesktopGeometryOf(const Desktop *desktop);

/** The work area: where windows live. An empty rectangle for NULL. */
GfxRect DesktopWorkArea(const Desktop *desktop);

/** The attached native window, or @ref RoboWindowIdNone. */
WindowId DesktopAttachedWindow(const Desktop *desktop);

/* ------------------------------------------------------------------ */
/* Labels                                                              */
/* ------------------------------------------------------------------ */

/**
 * Replace the status label (a clock, a battery, anything the caller wants on the right of the panel).
 *
 * Copied and bounded by @ref RoboDesktopStatusMax, and **the caller's clock is the caller's**: this
 * module never reads a time, which is what keeps two frames of one state byte-identical. A NULL or empty
 * @p text clears the label and the status area is then untouched by the next frame.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p desktop or an over-long label.
 */
Result DesktopSetStatus(const Desktop *desktop,
                                      const char *utf8Text,
                                      DesktopDiag *diag);

/** Borrowed status label (never NULL; `""` when there is none). */
const char *DesktopStatus(const Desktop *desktop);

/* ------------------------------------------------------------------ */
/* Shell windows                                                       */
/* ------------------------------------------------------------------ */

/**
 * Add a shell window.
 *
 * The bounds are clamped into the work area (@ref DesktopGeometryContain) and a window below the
 * decoration minimum is grown to it rather than refused, because the minimum is a property of the
 * *decorations* the shell draws rather than of the caller's request: a caller asking for a 4x4 window is
 * asking for something the shell cannot draw, and the honest answer is the smallest window that can be
 * drawn rather than an error or a window whose title bar is outside its own frame.
 *
 * @p outId receives the new window's id. @p diag may be NULL.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p desktop or @p desc or an invalid description;
 *         RoboAlreadyExists when the desktop already holds @ref RoboDesktopMaxWindows windows;
 *         RoboInvalidArgument with a `SHELL` diagnostic when there is no work area to place it in.
 */
Result DesktopWindowAdd(Desktop *desktop,
                                     const DesktopWindowDesc *desc,
                                     DesktopWindowId *outId,
                                     DesktopDiag *diag);

/** Number of shell windows, closing ones included. */
size_t DesktopWindowCount(const Desktop *desktop);

/** The id of the window at z-order @p index (0 = furthest back). RoboNotFound when out of range. */
Result DesktopWindowAt(const Desktop *desktop,
                                     size_t index,
                                     DesktopWindowId *outId);

/** Non-zero when @p id names a window of @p desktop that is not yet destroyed. */
int DesktopWindowExists(const Desktop *desktop, DesktopWindowId id);

/** Outer rectangle (decorations included). RoboNotFound for an unknown id. */
Result DesktopWindowBounds(const Desktop *desktop,
                                         DesktopWindowId id,
                                         GfxRect *out);

/** The client rectangle: where a window's contents are drawn. RoboNotFound for an unknown id. */
Result DesktopWindowClientBounds(const Desktop *desktop,
                                                DesktopWindowId id,
                                                GfxRect *out);

/** Move and/or resize @p id. Clamped into the work area like `add`; a NULL-diag refusal is the same set. */
Result DesktopWindowSetBounds(Desktop *desktop,
                                            DesktopWindowId id,
                                            GfxRect bounds,
                                            DesktopDiag *diag);

/** Copy a UTF-8 title. NULL clears it. Over-long -> RoboInvalidArgument (nothing copied). */
Result DesktopWindowSetTitle(Desktop *desktop,
                                            DesktopWindowId id,
                                            const char *utf8Title,
                                            DesktopDiag *diag);

/** Borrowed title (never NULL; `""` when none or unknown id). */
const char *DesktopWindowTitle(const Desktop *desktop, DesktopWindowId id);

/**
 * Replace what a window's contents are.
 *
 * @p content is borrowed, not copied, and must outlive the window's last paint — the rule
 * `RoboDesktopWindow.h` states at length. NULL means "this window has no contents", which paints its body
 * and decorations and reports `contentRuns == 0`.
 */
Result DesktopWindowSetContent(Desktop *desktop,
                                              DesktopWindowId id,
                                              const PaintDisplayList *content,
                                              FontFace *contentFace,
                                              DesktopDiag *diag);

/** The window's borrowed contents, or NULL (including for an unknown id). */
const PaintDisplayList *DesktopWindowContent(const Desktop *desktop,
                                                            DesktopWindowId id);

/** Show or hide a window. A hidden window keeps its bounds, contents and z-order and is not painted. */
Result DesktopWindowSetVisible(Desktop *desktop,
                                              DesktopWindowId id,
                                              int visible,
                                              DesktopDiag *diag);

/** Non-zero when @p id is drawn. 0 for an unknown id. */
int DesktopWindowIsVisible(const Desktop *desktop, DesktopWindowId id);

/**
 * Move @p id to the front of the z-order (the last window painted is the front one).
 *
 * Focus follows the raise, and that is a decision rather than a shortcut: this build has no separate
 * focus model, so "the front window is the focused one" is the whole of it and the two cannot drift.
 * `DesktopWindowFocus` is the door that focuses without raising, for a caller that wants the
 * accent border elsewhere.
 *
 * @return RoboOk; RoboNotFound for an unknown id.
 */
Result DesktopWindowRaise(Desktop *desktop, DesktopWindowId id);

/** Make @p id the focused window without changing the z-order. RoboNotFound for an unknown id. */
Result DesktopWindowFocus(Desktop *desktop, DesktopWindowId id);

/** The focused window, or @ref RoboDesktopWindowIdNone. */
DesktopWindowId DesktopWindowFocused(const Desktop *desktop);

/**
 * Ask @p id to close: it stops being painted and moves to
 * @ref RoboDesktopWindowClosing, and **the caller destroys it**.
 *
 * This is what the close button produces and what a caller's own "quit" would produce. Idempotent.
 *
 * @return RoboOk; RoboNotFound for an unknown id.
 */
Result DesktopWindowRequestClose(Desktop *desktop, DesktopWindowId id);

/** A window's lifecycle state. RoboNotFound for an unknown id. */
Result GetDesktopWindowState(const Desktop *desktop,
                                        DesktopWindowId id,
                                        DesktopWindowState *out);

/** Remove @p id from the shell. RoboNotFound for an unknown id. Focus falls to the front window. */
Result DesktopWindowDestroy(Desktop *desktop, DesktopWindowId id);

/* ------------------------------------------------------------------ */
/* Hit testing and the pointer                                         */
/* ------------------------------------------------------------------ */

/** What is under a point. */
typedef struct DesktopHit {
    DesktopArea area;
    /** The window under the point, or @ref RoboDesktopWindowIdNone. */
    DesktopWindowId window;
    /** The point, echoed. */
    GfxPoint point;
    /** Non-zero when the point is inside the screen at all. */
    int inside;
} DesktopHit;

void DesktopHitInit(DesktopHit *hit);

/**
 * Which shell object is at @p point.
 *
 * Front to back: a point over two windows belongs to the front one, which is what makes the answer the
 * one a *user* would give. A point outside the screen is @ref RoboDesktopAreaNone with `inside == 0`
 * rather than an error — a pointer can leave a window, and that is not a failure.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p desktop or @p out.
 */
Result DesktopHitTest(const Desktop *desktop,
                                    GfxPoint point,
                                    DesktopHit *out);

/**
 * Record where the pointer is for shell readback and the software cursor, without a press action.
 *
 * `DesktopInput` calls this on every positioned event so the software cursor can move while the
 * raise/close decisions stay in the shell's pointer-down listener. `DesktopObservePointer` still
 * does both halves for a caller that has no input system.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p desktop.
 */
Result DesktopTrackPointer(Desktop *desktop, GfxPoint point, DesktopDiag *diag);

/**
 * Hand the shell a pointer state: where it is, and whether a button is down *now*.
 *
 * For callers **without** `DesktopInput`: records the point (via @ref DesktopTrackPointer) and, on a
 * press, raises or closes a window. Prefer `DesktopInput` when the RoboInput path is available — that
 * path owns raise/close through listeners and calls @ref DesktopTrackPointer for cursor tracking.
 *
 * @return RoboOk — including a press that hit nothing, which is a legitimate press; RoboInvalidArgument
 *         for a NULL @p desktop or @p diag.
 */
Result DesktopObservePointer(Desktop *desktop,
                                           GfxPoint point,
                                           int pressed,
                                           DesktopDiag *diag);

/* ------------------------------------------------------------------ */
/* Frames                                                              */
/* ------------------------------------------------------------------ */

/**
 * What one frame did.
 *
 * A record by value, and its counts are the two halves a reader compares: what the **shell** drew
 * (`fills`, `labels`, and the one accent mark), and what the **windows** contributed (`windows` painted,
 * `contentRuns` executed, `contentPixels` composited). `clipped` is the number of shell drawings that
 * were cut by the screen's own bounds — a window partly off the screen is a legitimate state, and the
 * count is how a caller learns that a placement is showing less than it asked for.
 */
typedef struct DesktopFrameRecord {
    /** The target's size in pixels. */
    uint32_t width;
    uint32_t height;
    /** Shell fills executed: the surface, the panel, its separator, the falloff rows, the mark. */
    size_t fills;
    /** Chrome labels drawn (identity, applications, status, and one per painted window title). */
    size_t labels;
    /** Labels skipped because no face was available. */
    size_t labelsUnshaped;
    /** Windows painted: visible, not closing. */
    size_t windows;
    /** Windows the shell holds, closing or hidden included. The denominator `windows` is read against. */
    size_t windowsTotal;
    /** Display-list runs executed for window contents (0 when no window has contents). */
    size_t contentRuns;
    /** Pixels of window contents composited into the screen. */
    size_t contentPixels;
    /** Shell drawings clipped away by the screen's bounds. */
    size_t clipped;
    /** Bytes the frame's target occupies. */
    size_t bytes;
    /** The target's checksum after the frame. */
    uint64_t checksum;
} DesktopFrameRecord;

/** Clear @p record to a frame that did nothing. Safe on NULL. */
void DesktopFrameRecordInit(DesktopFrameRecord *record);

/** Non-zero when @p record describes a frame this build could have painted. */
int DesktopFrameRecordIsValid(const DesktopFrameRecord *record);

/** Field-for-field equality. Safe on NULL. */
int DesktopFrameRecordEqual(const DesktopFrameRecord *a,
                                    const DesktopFrameRecord *b);

/** Begin a frame. Nested without a matching end → RoboBusy. NULL desktop → RoboInvalidArgument. */
Result DesktopBeginFrame(Desktop *desktop, DesktopDiag *diag);

/** Non-zero while between `begin_frame` and `end_frame`. */
int DesktopFrameIsOpen(const Desktop *desktop);

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
 * @return RoboOk; RoboInvalidArgument without an open frame; RoboNotSupported with a `WINDOW`
 *         diagnostic when a window's contents could not be rasterised, in which case that window's
 *         decorations are still painted and its body is left as the shell cleared it.
 */
Result DesktopPaint(Desktop *desktop, DesktopDiag *diag);

/** End a frame, describing it in @p outRecord (may be NULL). RoboInvalidArgument without a begin. */
Result DesktopEndFrame(Desktop *desktop,
                                    DesktopFrameRecord *outRecord,
                                    DesktopDiag *diag);

/** `begin_frame` → `paint` → `end_frame` in one call. The shorthand for a caller with nothing between. */
Result DesktopRender(Desktop *desktop,
                                 DesktopFrameRecord *outRecord,
                                 DesktopDiag *diag);

/**
 * The frame's pixels, **borrowed**; NULL when no desktop or no frame has ever been painted.
 *
 * The door for everything this module does not do with a frame: reading a pixel, encoding a screenshot,
 * handing it to a display backend, or presenting it through @ref DesktopPresentToSurface. Valid
 * until the next `resize` or until the desktop is destroyed.
 */
const GfxSurface *DesktopFrameSurface(const Desktop *desktop);

/** The frame target's checksum: the current picture, whether or not a frame was just painted. */
uint64_t DesktopFrameChecksum(const Desktop *desktop);

/** The frame target's bytes, contiguous and read-only; @p outSize filled when non-NULL. */
const uint8_t *DesktopFrameBytes(const Desktop *desktop, size_t *outSize);

/**
 * Resize the screen.
 *
 * The sequence the phase's §17 asks for, in one call: a new target of the new size, geometry recomputed,
 * every window clamped into the new work area, and the frame marked as needing a paint. The target's
 * pixels are **not** carried over: a resized screen has no previous frame to show (the old one was the
 * wrong shape), so the next frame begins from the target's zero state and paints a whole picture. A no-op
 * resize is not a no-op: it recreates nothing and reports RoboOk, because a caller polling a window's
 * size must not force a reallocation every frame.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p desktop, a non-positive or oversized dimension, or
 *         a height that leaves no panel.
 */
Result DesktopResize(Desktop *desktop,
                                 int32_t width,
                                 int32_t height,
                                 DesktopDiag *diag);

/**
 * Match the screen to the attached native window's physical client size.
 *
 * The one call a driven shell makes each iteration: a window that was resized by the user reports a new
 * size, and the desktop follows it. No attached window → RoboNotSupported with a `SURFACE` diagnostic;
 * an unchanged size → RoboOk and nothing reallocated.
 *
 * @p outResized, when non-NULL, receives 1 when the screen changed and 0 when it did not.
 */
Result DesktopSyncSize(Desktop *desktop,
                                    int *outResized,
                                    DesktopDiag *diag);

/* ------------------------------------------------------------------ */
/* Presentation                                                        */
/* ------------------------------------------------------------------ */

/**
 * Present the current frame into a caller's surface at (@p x, @p y).
 *
 * A thin door over `RenderPresent` (11.13's other half): the pixels are the frame's, the
 * destination is the caller's, and @p outRecord describes what landed. This is the door a guest build
 * uses to hand a frame to a display framebuffer it wrapped as a surface, and a test uses to check a
 * frame without a window.
 *
 * @return as @ref RenderPresent; RoboInvalidArgument for a NULL @p desktop or @p dest.
 */
Result DesktopPresentToSurface(const Desktop *desktop,
                                              GfxSurface *dest,
                                              int32_t x,
                                              int32_t y,
                                              RenderPresentRecord *outRecord,
                                              DesktopDiag *diag);

/**
 * Present the current frame into the attached native window.
 *
 * The sequence is RoboWindow's own frame triple: `begin_frame` (which gives the window's owned render
 * surface, sized to the window's *physical* client size), the presenter, `end_frame` (which marks the
 * window dirty) and `present_frame` (which reaches the backend — a Win32 client area, a QEMU framebuffer).
 * Nothing is paced: this call waits for no vertical blank, and says so rather than implying a frame rate.
 *
 * A frame whose size differs from the window's (a resize the desktop has not been told about) is **clipped**
 * by the presenter and reported, exactly as `RenderPresent` documents — the caller's next
 * @ref DesktopSyncSize closes the gap, and a frame that refused to present would show nothing at
 * all in the meantime.
 *
 * @p outRecord may be NULL. @p outPresent, when non-NULL, receives whether the *window* accepted the
 * frame (a window that was not dirty presents nothing and is not an error).
 *
 * @return RoboOk; RoboInvalidArgument for a NULL @p desktop; RoboNotSupported with a `SURFACE`
 *         diagnostic when no native window is attached, or the window could not give a render surface.
 */
Result DesktopPresent(Desktop *desktop,
                                  RenderPresentRecord *outRecord,
                                  int *outPresented,
                                  DesktopDiag *diag);

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

/** What the shell has done since it was created: the counters a smoke test asserts. */
typedef struct DesktopStats {
    /** Frames ended. */
    size_t frames;
    /** Frames that painted **no** window: the "is a desktop without applications still a desktop?" case. */
    size_t framesWithoutWindows;
    /** Window contents rasterised across every frame so far. */
    size_t contentRuns;
    /** Window content pixels composited across every frame so far. */
    size_t contentPixels;
    /** Labels drawn across every frame so far. */
    size_t labels;
    /** Labels skipped for lack of a face across every frame so far. */
    size_t labelsUnshaped;
    /** Shell fills executed across every frame so far: the surface, the panel, the windows' parts. */
    size_t fillDrawings;
    /** Shell drawings the screen's own bounds cut across every frame so far. */
    size_t clippedDrawings;
    /** Successful presentations. */
    size_t presentations;
    /** Resizes that actually changed the screen. */
    size_t resizes;
    /** Windows added since creation (a monotonic count; the live count is `DesktopWindowCount`). */
    size_t windowsAdded;
    /** Windows destroyed since creation. */
    size_t windowsDestroyed;
    /** Windows closed through the close button or @ref DesktopWindowRequestClose. */
    size_t windowsClosed;
    /** Pointer presses the shell acted on: a raise, a focus or a close. */
    size_t pointerActions;
    /** Pointer moves observed. */
    size_t pointerMoves;
    /** The checksum of the last frame that was painted. */
    uint64_t lastFrameChecksum;
} DesktopStats;

/** Copy the shell's counters. RoboInvalidArgument for NULL arguments. */
Result GetDesktopStats(const Desktop *desktop, DesktopStats *out);

/** Pointer state: the last point and what was under it. */
typedef struct DesktopPointer {
    GfxPoint point;
    int inside;
    int pressed;
    DesktopArea area;
    DesktopWindowId window;
} DesktopPointer;

/** Copy the pointer state. RoboInvalidArgument for NULL arguments. */
Result GetDesktopPointer(const Desktop *desktop, DesktopPointer *out);

/** Characters one line of the shell dump can need. */
#define RoboDesktopDumpLineMax 200u

/**
 * Write @p desktop as text: **seven** lines, one per group of facts, each ending in a newline.
 *
 * The developer's door, in the shape `RenderFrameWrite` uses: no `stdio`, no address anywhere in
 * the text, and a buffer that is too small reports the length the caller would have needed rather than
 * truncating. The shape:
 *
 *     desktop 1280x800 panel=0,0,1280,30 work=0,30,1280,770
 *       windows=1 visible=1 focused=3
 *       frames=4 without_windows=1 resizes=1
 *       fills=42 labels=6 unshaped=0 clipped=0
 *       contentRuns=3 contentPixels=307200 presentations=3
 *       pointer=12,40 inside=1 pressed=0 area=window-content
 *       checksum=2f1c9e0b7a4d3355
 *
 * @return RoboOk; RoboInvalidArgument for NULL @p desktop, @p buffer, @p outLength or a zero
 *         @p capacity; RoboNotSupported with a `DUMP` diagnostic when the text does not fit, in which
 *         case @p outLength receives the length it needed.
 */
Result DesktopWrite(const Desktop *desktop,
                                char *buffer,
                                size_t capacity,
                                size_t *outLength,
                                DesktopDiag *diag);

/**
 * Attach an owned cursor pack (Phase 44.7). @p pack may be NULL to clear.
 * Previous pack is destroyed. The shell paints pack frames when present;
 * otherwise the legacy 11×16 arrow is used.
 */
Result DesktopAttachCursorPack(Desktop *desktop, DesktopCursorPack *pack);

/** Borrowed view of the attached pack, or NULL. */
DesktopCursorPack *DesktopGetCursorPack(Desktop *desktop);

/** Force the software cursor shape (tests / Cursor Lab). Input integration also writes this. */
void DesktopSetCursorShape(Desktop *desktop, InputCursorType shape);

InputCursorType DesktopGetCursorShape(const Desktop *desktop);

void DesktopSetCursorTheme(Desktop *desktop, DesktopCursorTheme theme);

void DesktopSetCursorScale(Desktop *desktop, float scale);

/** Advance animated wait/progress frames by @p deltaMs. */
void DesktopAdvanceCursorAnim(Desktop *desktop, uint32_t deltaMs);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_H */
