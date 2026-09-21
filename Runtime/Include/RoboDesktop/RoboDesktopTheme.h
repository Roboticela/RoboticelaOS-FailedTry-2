/*
 * RoboDesktop theme (Phase 11.13).
 *
 * The shell's appearance as **one record of colours**, so that "what does the desktop look like" is a
 * value a caller can read, replace, or assert against -- rather than a set of literals scattered through
 * the painting code. Nothing here is a CSS token or an HTML colour: the shell draws with RoboGraphics
 * fills and RoboGraphics colours, and the palette below is this project's own (AGENTS.md's light-mode
 * tokens, resolved to RGBA8 literals).
 *
 * ## Why the defaults are these colours
 *
 * A desktop's job is to make applications readable, so the shell is *quiet*: no gradients, no textures,
 * no ornament, and exactly one accent. The arrangement the defaults encode is three levels of depth
 * with one line between each:
 *
 *   the **desktop surface** is the deepest neutral (`#EDEDEF`), so a window's white body reads as
 *   raised without a single shadow pixel;
 *   the **panel** is pure white, separated from the surface by a hairline (`#D2D2D7`) and a two-row
 *   falloff (`shadow_1`/`shadow_2`), which is a *stepped* approximation of a soft edge: a real blur is
 *   a filter, and filters are named as absent for this phase;
 *   the **accent** (`#0E7AFF`) appears exactly twice -- the identity mark and the focused window's
 *   border -- because one accent used twice reads as a system and one accent used five times reads as
 *   decoration.
 *
 * The focused border is the shell's whole focus story: it is the only thing on screen that says which
 * window the pointer's presses belong to, which is why it is the accent rather than a second neutral.
 *
 * ## The one colour that is not a token
 *
 * `windowClose` is `#FF3B30` -- the danger token -- because a close button is the one control a desktop
 * has that destroys something, and painting it the same neutral as a title bar would hide that. It is
 * deliberately *not* hover-styled: this build has no hover animation (see `RoboDesktopVersion.h`), so a
 * colour that only means "the pointer is over it" would be a colour nothing could ever show.
 */
#ifndef ROBO_DESKTOP_THEME_H
#define ROBO_DESKTOP_THEME_H

#include "RoboGraphics/RoboGraphicsColor.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The shell's palette. Straight (unassociated) RGBA8, exactly as every graphics colour is. */
typedef struct RoboDesktopTheme {
    /** The desktop's own surface: everything outside the panel and outside every window. */
    GfxColorRgba8 desktopSurface;
    /** The panel's body. */
    GfxColorRgba8 panelSurface;
    /** The panel's hairline, drawn as its last row. */
    GfxColorRgba8 panelSeparator;
    /** The bar left of the identity label: the one accent on the shell itself. */
    GfxColorRgba8 identityAccent;
    /** The identity label's colour. */
    GfxColorRgba8 identityText;
    /** The applications and status labels' colour: one step back from the identity. */
    GfxColorRgba8 panelMetaText;
    /** The first falloff row under the panel. */
    GfxColorRgba8 shadowNear;
    /** The second falloff row under the panel. */
    GfxColorRgba8 shadowFar;
    /** A window's body, and the colour its contents are cleared to before they are drawn on. */
    GfxColorRgba8 windowSurface;
    /** A window's title bar. */
    GfxColorRgba8 windowTitleSurface;
    /** A window's title text. */
    GfxColorRgba8 windowTitleText;
    /** An unfocused window's border. */
    GfxColorRgba8 windowBorder;
    /** The focused window's border. */
    GfxColorRgba8 windowBorderFocused;
    /** The close button's fill. */
    GfxColorRgba8 windowClose;
    /** The close button's ring, and the cross drawn inside it. */
    GfxColorRgba8 windowCloseMark;
} DesktopTheme;

/** Fill @p theme with this build's defaults. Safe on NULL. */
void DesktopThemeInit(DesktopTheme *theme);

/**
 * Non-zero when @p theme is a palette this build may paint with.
 *
 * The rule is the one that matters for a *shell* rather than a document: the two surfaces a frame's
 * readability depends on -- the desktop surface and a window's body -- must be **opaque**, because a
 * translucent desktop would composite over whatever the frame's target held before, which is the same
 * argument `RenderTargetClear` makes about a baseline. The other colours may carry alpha (the
 * falloff rows do, by design).
 */
int DesktopThemeIsValid(const DesktopTheme *theme);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_DESKTOP_THEME_H */
