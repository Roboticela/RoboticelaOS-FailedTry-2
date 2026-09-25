/*
 * RoboDesktop version pin — Phase 11.13 framebuffer presentation + basic desktop shell.
 *
 * RoboDesktop is the **native shell**: the first thing in this tree that owns a *screen* rather than a
 * page. Everything below it answers "what does this document look like" -- and the desktop answers a
 * different question, which is why it is a module and not a mode of the renderer: *what is on the
 * display*. A background, a panel with the platform's name and the running applications, a work area,
 * and application windows whose contents are documents rendered by the pipeline below.
 *
 * ## The boundary this module exists to hold
 *
 * A desktop that was a web page would be the easiest thing in the world to build here and the wrong
 * thing: the shell would then need a document pipeline, a layout pass and a paint pass to draw a
 * taskbar, and a broken document would take the screen with it. So the split is stated once and enforced
 * by the boundary scan:
 *
 *   the **shell** (`RoboDesktopChrome`, this module's own painting) draws with RoboGraphics primitives
 *   and fills, and knows nothing about HTML;
 *   an **application window's contents** are a `PaintDisplayList` produced elsewhere (by
 *   RoboHTML → RoboCSS → RoboLayout → RoboPaint, in this build) and rasterised by RoboRender into the
 *   window's own target before being composited under the shell's chrome;
 *   the **screen** is a target this module owns, presented into whatever destination the caller's
 *   window or framebuffer gives it (`RoboRender`'s presenter, 11.13's other half).
 *
 * The consequence worth naming: a desktop with **no applications** still draws, still presents, and
 * still reads as a desktop -- because nothing about the shell depends on a document existing. That is
 * the property §19 of the phase's brief asks for, and it is a consequence of the boundary rather than a
 * special case bolted on to satisfy it.
 *
 * ## What this release is not
 *
 * It is not a window manager: there is no snapping, no tiling, no virtual desktops, no animation, no
 * maximize or minimize, no drag, no decoration theme engine and no z-order beyond a list that `raise`
 * moves an entry to the end of. It *does* use RoboCompositor for retained wallpaper, panel and window
 * surfaces with composition damage and region present — so a frame that only raises a window no longer
 * clears the desktop or reshapes panel labels — but it is not a full desktop compositor product (no
 * tile scheduler driven from here, no VSync wait, no GPU path). It is not a desktop *environment*: no
 * launcher, no task manager, no settings, no notifications, no file manager and no terminal. It is not
 * input: the pointer door below exists because RoboWindow already delivers a pointer and a desktop that
 * could not say *which window* a point is in would be a picture rather than a shell, and it does exactly
 * two things with it (focus/raise a window, close a window whose close button is pressed). Keyboard
 * routing, hit-test delivery to applications, drag, and hover animation are all absent by decision, not
 * by omission.
 */
#ifndef ROBO_DESKTOP_VERSION_H
#define ROBO_DESKTOP_VERSION_H

#define RoboDesktopVersionMajor 0
#define RoboDesktopVersionMinor 1
#define RoboDesktopVersionPatch 0
#define RoboDesktopVersionString "0.1.0"

/** 1 once the Phase 11 surface is frozen (not yet). */
#define RoboDesktopApiFrozen 0

/**
 * 1 since this module's first release: **a native desktop shell that reaches a display.**
 *
 * Read by two doors that must not disagree (`DesktopGetCapabilities()`'s `shell` field and
 * `DesktopShellIsImplemented()`), plus the conformance suite, which asserts all three together
 * -- the device `RoboPaint`'s `raster` flag established and this module inherits.
 *
 * What it claims: a screen target this module owns and paints (background, panel, work area), shell
 * windows with real decorations, application-window contents rasterised by RoboRender from a display
 * list and composited under the chrome, a deterministic frame lifecycle with a record and a checksum,
 * a resize path, and a presentation door that puts the finished frame into a RoboWindow's render
 * surface or any writable surface a caller names. What it does **not** claim is everything a desktop
 * *environment* has: no launcher, no task switching, no notifications, no settings, no tiles of its
 * own, no VSync, no GPU, no keyboard routing and no window manager. Retained-layer composition and
 * damage tracking are **1** (`DesktopCapabilities.compositor` / `damageTracking`). Every other
 * absence stays a 0 beside this flag.
 */
#define RoboDesktopShellImplemented 1

/**
 * 1 since this module's first release: **markup can become a window's contents.**
 *
 * Read by `DesktopDocumentIsImplemented()`, and the honest companion to the capability record's
 * `documents` field, which is **0**. The two are not contradictory and the distinction is the module's
 * central boundary:
 *
 *   `documents == 0` says the **shell** builds no document. It paints with graphics primitives, and a
 *   broken document cannot take the screen with it;
 *   this flag says the **runtime** can build one for an application window, through
 *   `RoboDesktopDocument.h` -- a file the boundary scan exempts from the shell's rule by name, so that
 *   the exemption is visible in the build rather than implied by a directory listing.
 *
 * A build that removed the document bridge would keep a working shell and would have to set this to 0
 * and leave `documents` at 0; a build that let the shell read markup would have to change the scan. Both
 * are one-line changes precisely because the boundary is a file rather than a convention.
 */
#define RoboDesktopDocumentsImplemented 1

#endif /* ROBO_DESKTOP_VERSION_H */
