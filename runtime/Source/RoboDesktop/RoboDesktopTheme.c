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

void robo_desktop_theme_init(robo_desktop_theme_t *theme)
{
    if (!theme) {
        return;
    }

    /* Surfaces: the three levels of depth, deepest first. */
    theme->desktop_surface = robo_gfx_rgba8(0xED, 0xED, 0xEF, 255); /* #EDEDEF */
    theme->panel_surface = robo_gfx_rgba8(0xFF, 0xFF, 0xFF, 255);   /* #FFFFFF */

    /* The panel's hairline and the falloff under it. */
    theme->panel_separator = robo_gfx_rgba8(0xD2, 0xD2, 0xD7, 255); /* #D2D2D7 */
    theme->shadow_near = robo_gfx_rgba8(0x00, 0x00, 0x00, 15);      /* 0.06 */
    theme->shadow_far = robo_gfx_rgba8(0x00, 0x00, 0x00, 8);        /* 0.03 */

    /* The one accent, used twice: the identity mark and the focused window's border. */
    theme->identity_accent = robo_gfx_rgba8(0x0E, 0x7A, 0xFF, 255); /* #0E7AFF */
    theme->window_border_focused = robo_gfx_rgba8(0x0E, 0x7A, 0xFF, 255);

    /* Text. */
    theme->identity_text = robo_gfx_rgba8(0x1D, 0x1D, 0x1F, 255);  /* #1D1D1F */
    theme->panel_meta_text = robo_gfx_rgba8(0x6E, 0x6E, 0x73, 255); /* #6E6E73 */
    theme->window_title_text = robo_gfx_rgba8(0x1D, 0x1D, 0x1F, 255);

    /* Windows. */
    theme->window_surface = robo_gfx_rgba8(0xFF, 0xFF, 0xFF, 255);
    theme->window_title_surface = robo_gfx_rgba8(0xF5, 0xF5, 0xF7, 255); /* #F5F5F7 */
    theme->window_border = robo_gfx_rgba8(0xD2, 0xD2, 0xD7, 255);

    /* The close button: the danger token, and a mark dark enough to read on it. */
    theme->window_close = robo_gfx_rgba8(0xFF, 0x3B, 0x30, 255);      /* #FF3B30 */
    theme->window_close_mark = robo_gfx_rgba8(0x8A, 0x1A, 0x14, 255);
}

int robo_desktop_theme_is_valid(const robo_desktop_theme_t *theme)
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
    if (theme->desktop_surface.a != 255u || theme->window_surface.a != 255u) {
        return 0;
    }
    if (theme->panel_surface.a != 255u) {
        return 0;
    }
    /* A separator that is invisible would leave the panel floating over the desktop with no edge at all,
     * so the one colour whose whole job is to be seen is refused at zero alpha. */
    if (theme->panel_separator.a == 0u) {
        return 0;
    }
    return 1;
}
