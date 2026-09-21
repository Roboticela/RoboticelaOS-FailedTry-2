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
 *   an **application window's contents** are a `robo_paint_display_list_t` produced elsewhere (by
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
 * moves an entry to the end of. It is not a compositor: each window's contents are rasterised into the
 * window's own target and composited once, in z-order, into the screen target, with the source-over
 * blend the graphics module already owns -- there is no layer tree, no damage union, and no partial
 * repaint (a frame is a frame). It is not a desktop *environment*: no launcher, no task manager, no
 * settings, no notifications, no file manager and no terminal. It is not input: the pointer door below
 * exists because RoboWindow already delivers a pointer and a desktop that could not say *which window*
 * a point is in would be a picture rather than a shell, and it does exactly two things with it
 * (focus/raise a window, close a window whose close button is pressed). Keyboard routing, hit-test
 * delivery to applications, drag, and hover animation are all absent by decision, not by omission.
 */
#ifndef ROBO_DESKTOP_VERSION_H
#define ROBO_DESKTOP_VERSION_H

#define ROBO_DESKTOP_VERSION_MAJOR 0
#define ROBO_DESKTOP_VERSION_MINOR 1
#define ROBO_DESKTOP_VERSION_PATCH 0
#define ROBO_DESKTOP_VERSION_STRING "0.1.0"

/** 1 once the Phase 11 surface is frozen (not yet). */
#define ROBO_DESKTOP_API_FROZEN 0

/**
 * 1 since this module's first release: **a native desktop shell that reaches a display.**
 *
 * Read by two doors that must not disagree (`robo_desktop_get_capabilities()`'s `shell` field and
 * `robo_desktop_shell_is_implemented()`), plus the conformance suite, which asserts all three together
 * -- the device `RoboPaint`'s `raster` flag established and this module inherits.
 *
 * What it claims: a screen target this module owns and paints (background, panel, work area), shell
 * windows with real decorations, application-window contents rasterised by RoboRender from a display
 * list and composited under the chrome, a deterministic frame lifecycle with a record and a checksum,
 * a resize path, and a presentation door that puts the finished frame into a RoboWindow's render
 * surface or any writable surface a caller names. What it does **not** claim is everything a desktop
 * *environment* or a *compositor* has: no launcher, no task switching, no notifications, no settings, no
 * damage tracking, no tiles, no VSync, no GPU, no keyboard routing and no window manager. Every one of
 * those is a 0 in `robo_desktop_capabilities_t` beside this flag.
 */
#define ROBO_DESKTOP_SHELL_IMPLEMENTED 1

/**
 * 1 since this module's first release: **markup can become a window's contents.**
 *
 * Read by `robo_desktop_document_is_implemented()`, and the honest companion to the capability record's
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
#define ROBO_DESKTOP_DOCUMENTS_IMPLEMENTED 1

#endif /* ROBO_DESKTOP_VERSION_H */
