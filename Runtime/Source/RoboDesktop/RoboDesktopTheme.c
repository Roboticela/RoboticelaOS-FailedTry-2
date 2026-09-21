/*
 * RoboDesktop's default palette (Phase 11.13) — see RoboDesktopTheme.h for why these colours.
 *
 * One decision is worth restating in code rather than only in the header, because it is the one a reader
 * will want to change: the desktop surface is **not** pure white and the panel **is**. A shell drawn in one
 * white would put the panel and the desktop at the same level, and the only thing separating them would be
 * the hairline - which reads as a border rather than as a surface. One step of neutral between them is
 * what makes the panel look like a thing *above* the desktop, and it costs one token.
 *
 * The falloff rows are `rgba(0, 0, 0, 0.06)` and `rgba(0, 0, 0, 0.03)`: black at two alphas rather than
 * two greys, because a composite over *any* desktop colour has to darken it by the same amount and a
 * literal grey would only look right over one of them.
 */

#include "RoboDesktop/RoboDesktopTheme.h"

void DesktopThemeInit(DesktopTheme *theme)
{
    if (!theme) {
        return;
    }

    /* Surfaces: the three levels of depth, deepest first. */
    theme->desktopSurface = GfxRgba8(0xED, 0xED, 0xEF, 255); /* #EDEDEF */
    theme->panelSurface = GfxRgba8(0xFF, 0xFF, 0xFF, 255);   /* #FFFFFF */

    /* The panel's hairline and the falloff under it. */
    theme->panelSeparator = GfxRgba8(0xD2, 0xD2, 0xD7, 255); /* #D2D2D7 */
    theme->shadowNear = GfxRgba8(0x00, 0x00, 0x00, 15);      /* 0.06 */
    theme->shadowFar = GfxRgba8(0x00, 0x00, 0x00, 8);        /* 0.03 */

    /* The one accent, used twice: the identity mark and the focused window's border. */
    theme->identityAccent = GfxRgba8(0x0E, 0x7A, 0xFF, 255); /* #0E7AFF */
    theme->windowBorderFocused = GfxRgba8(0x0E, 0x7A, 0xFF, 255);

    /* Text. */
    theme->identityText = GfxRgba8(0x1D, 0x1D, 0x1F, 255);  /* #1D1D1F */
    theme->panelMetaText = GfxRgba8(0x6E, 0x6E, 0x73, 255); /* #6E6E73 */
    theme->windowTitleText = GfxRgba8(0x1D, 0x1D, 0x1F, 255);

    /* Windows. */
    theme->windowSurface = GfxRgba8(0xFF, 0xFF, 0xFF, 255);
    theme->windowTitleSurface = GfxRgba8(0xF5, 0xF5, 0xF7, 255); /* #F5F5F7 */
    theme->windowBorder = GfxRgba8(0xD2, 0xD2, 0xD7, 255);

    /* The close button: the danger token, and a mark dark enough to read on it. */
    theme->windowClose = GfxRgba8(0xFF, 0x3B, 0x30, 255);      /* #FF3B30 */
    theme->windowCloseMark = GfxRgba8(0x8A, 0x1A, 0x14, 255);
}

int DesktopThemeIsValid(const DesktopTheme *theme)
{
    if (!theme) {
        return 0;
    }
    /*
     * The two surfaces a frame's readability depends on must be opaque (see the header): a translucent
     * desktop would composite over whatever the target held before, which is exactly the dependence on
     * history a frame's baseline exists to remove. Every other colour may carry alpha -- the falloff rows
     * are *defined* by theirs.
     */
    if (theme->desktopSurface.a != 255u || theme->windowSurface.a != 255u) {
        return 0;
    }
    if (theme->panelSurface.a != 255u) {
        return 0;
    }
    /* A separator that is invisible would leave the panel floating over the desktop with no edge at all,
     * so the one colour whose whole job is to be seen is refused at zero alpha. */
    if (theme->panelSeparator.a == 0u) {
        return 0;
    }
    return 1;
}
