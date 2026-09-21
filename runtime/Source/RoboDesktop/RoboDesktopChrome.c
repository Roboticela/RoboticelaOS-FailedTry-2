/*
 * RoboDesktop's chrome (Phase 11.13) — the shell's own painting, and the whole of what makes this a
 * desktop rather than a page.
 *
 * ## Everything here is a fill or a glyph, and that is the design
 *
 * No CSS, no layout tree, no display list: the shell draws with `robo_gfx_raster_fill_rect`,
 * `robo_gfx_raster_draw_line` and the glyph bridge, in an order this file states and the frame record
 * counts. The consequence is the one `RoboDesktopVersion.h` names: the shell cannot be broken by a
 * document, because it never reads one.
 *
 * ## Why the labels are drawn one glyph at a time
 *
 * Because that is what the pipeline below does, and a shell that measured its own text would be a second
 * font-metrics implementation that would disagree with the first the day a face changed. The three calls
 * per character are the same three `RoboPaintDecoration.c` makes: the face's cmap resolves a code point to
 * a glyph, the graphics bridge scales that glyph's advance and outline, and the raster strokes the
 * outline. The pen is accumulated in `float` and the *drawn* origin is snapped by the graphics module's own
 * policy, so a shell label and a document's text are placed by one rule.
 *
 * ## The baseline rule
 *
 * `(band + cap) / 2`, with `cap` eight pixels: half the cap height above the middle of the band. Both bands
 * use it — the panel through its `baseline_from_bottom` metric and a window's title bar through the same
 * arithmetic — because a panel label and a window title that did not agree about where a baseline goes
 * would read as two different typesettings on one screen.
 *
 * ## What is counted, and why a count and not a flag
 *
 * `record->fills` counts every fill *and* the two strokes of a close button's cross, because they are all
 * drawing operations this file performs and a count a reader can check against the code is worth more than
 * a category a reader has to guess the boundary of. `record->labels` counts labels drawn and
 * `record->labels_unshaped` counts the ones a missing face made impossible — the pair is how a caller
 * learns that the desktop it is looking at has no text in it *because there is no font*, rather than
 * because the shell decided not to draw any.
 */

#include "RoboDesktopInternal.h"

#include "RoboFont/RoboFontMetrics.h"
#include "RoboGraphics/RoboGraphicsText.h"

/* The gap between the identity mark and the identity label: three pixels of mark, eight of space, which
 * is enough for the mark to read as a mark rather than as part of the lettering. */
#define ROBO_DESKTOP_IDENTITY_GAP 8

/* Half the cap height, in pixels: the constant in the baseline rule above. */
#define ROBO_DESKTOP_CAP_HALF 4

/* The inset of a close button's cross inside its own square. */
#define ROBO_DESKTOP_CLOSE_CROSS_INSET 3

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

uint32_t robo_desktop_chrome_utf8_next(const char *text, size_t length, size_t *offset)
{
    const uint8_t *bytes = (const uint8_t *)text;
    size_t at = offset ? *offset : 0u;
    uint32_t code_point = 0xFFFDu;
    unsigned need = 0u;
    unsigned index;

    if (!text || at >= length) {
        if (offset) {
            *offset = length;
        }
        return 0xFFFDu;
    }

    if (bytes[at] < 0x80u) {
        code_point = bytes[at];
        if (offset) {
            *offset = at + 1u;
        }
        return code_point;
    } else if ((bytes[at] & 0xE0u) == 0xC0u) {
        code_point = (uint32_t)(bytes[at] & 0x1Fu);
        need = 1u;
    } else if ((bytes[at] & 0xF0u) == 0xE0u) {
        code_point = (uint32_t)(bytes[at] & 0x0Fu);
        need = 2u;
    } else if ((bytes[at] & 0xF8u) == 0xF0u) {
        code_point = (uint32_t)(bytes[at] & 0x07u);
        need = 3u;
    } else {
        /* A continuation byte or an over-long lead: one replacement character, one byte consumed, which
         * is the recovery every decoder in this tree uses. */
        if (offset) {
            *offset = at + 1u;
        }
        return 0xFFFDu;
    }

    for (index = 1u; index <= need; index++) {
        if (at + index >= length || (bytes[at + index] & 0xC0u) != 0x80u) {
            if (offset) {
                *offset = at + 1u;
            }
            return 0xFFFDu;
        }
        code_point = (code_point << 6) | (uint32_t)(bytes[at + index] & 0x3Fu);
    }
    if (offset) {
        *offset = at + need + 1u;
    }
    return code_point;
}

/** The glyph a code point maps to on @p face, or `ROBO_GLYPH_ID_NONE`. */
static robo_glyph_id_t desktop_glyph_for(robo_font_face_t *face, uint32_t code_point)
{
    robo_font_cmap_lookup_result_t lookup;

    if (!face) {
        return ROBO_GLYPH_ID_NONE;
    }
    if (robo_font_face_lookup_glyph(face, code_point, &lookup) != ROBO_OK) {
        return ROBO_GLYPH_ID_NONE;
    }
    return lookup.glyph_id;
}

int32_t robo_desktop_chrome_label_width(const robo_desktop_t *desktop,
                                       const char *text,
                                       size_t length,
                                       float size_px)
{
    size_t offset = 0u;
    float width = 0.0f;

    if (!desktop || !text || length == 0u || !desktop->face || size_px <= 0.0f) {
        return 0;
    }
    while (offset < length) {
        robo_gfx_glyph_metrics_device_t metrics;
        uint32_t code_point = robo_desktop_chrome_utf8_next(text, length, &offset);
        robo_glyph_id_t glyph = desktop_glyph_for(desktop->face, code_point);

        if (glyph == ROBO_GLYPH_ID_NONE) {
            /* `.notdef` is still *space*: a label whose glyph is missing advances by the font's own
             * notdef width rather than collapsing, which is what keeps a title's box the width the
             * document's font would have given it. */
            width += size_px * 0.5f;
            continue;
        }
        if (robo_gfx_glyph_metrics_device(desktop->face, glyph, size_px, &metrics) == ROBO_OK) {
            width += metrics.advance_width;
        } else {
            width += size_px * 0.5f;
        }
    }
    return (int32_t)(width + 0.5f);
}

void robo_desktop_chrome_label(robo_desktop_t *desktop,
                               robo_gfx_raster_t *raster,
                               int32_t x,
                               int32_t baseline_px,
                               const char *text,
                               size_t length,
                               robo_gfx_color_rgba8_t color,
                               float size_px,
                               size_t *out_drawn,
                               size_t *out_unshaped)
{
    size_t offset = 0u;
    float pen;

    /*
     * The two counters are **accumulators**, not outputs: a caller passes the frame record's own fields and
     * each label adds one to exactly one of them on its way through. That is why nothing is zeroed here.
     * Counting per *glyph* instead was the first version of this function and it was wrong for the thing the
     * record is read for: a caller asking "did my desktop draw its labels?" needs the number of labels, and
     * `labels` holding the last label's glyph count would answer a different question -- and would have
     * clobbered the two labels before it.
     */
    if (!desktop || !raster || !text || length == 0u) {
        return;
    }
    if (!desktop->face || size_px <= 0.0f) {
        /* No face: the label is *counted* rather than silently skipped, so a frame's record says which
         * of the two reasons it has no text (see the file comment). */
        if (out_unshaped) {
            (*out_unshaped)++;
        }
        return;
    }

    pen = (float)x;
    while (offset < length) {
        robo_gfx_glyph_draw_t draw;
        robo_gfx_glyph_metrics_device_t metrics;
        uint32_t code_point = robo_desktop_chrome_utf8_next(text, length, &offset);
        robo_glyph_id_t glyph = desktop_glyph_for(desktop->face, code_point);

        robo_gfx_glyph_draw_init(&draw);
        draw.face = desktop->face;
        draw.glyph_id = glyph;
        draw.size_px = size_px;
        draw.color = color;
        draw.mode = ROBO_GFX_GLYPH_MODE_OUTLINE_STROKE;
        /* The graphics module's own snap policy, applied to the *drawn* origin: the pen keeps its
         * fraction so that a label's last glyph is not a pixel off because of the rounding of the ones
         * before it. */
        {
            robo_gfx_point_f_t origin;
            robo_gfx_point_t snapped;

            origin = robo_gfx_point_f(pen, (float)baseline_px);
            snapped = robo_gfx_glyph_snap_point(origin);
            draw.origin = robo_gfx_point_f((float)snapped.x, (float)snapped.y);
        }
        (void)robo_gfx_raster_draw_glyph(raster, &draw);
        if (robo_gfx_glyph_metrics_device(desktop->face, glyph, size_px, &metrics) == ROBO_OK) {
            pen += metrics.advance_width;
        } else {
            pen += size_px * 0.5f;
        }
    }
    if (out_drawn) {
        (*out_drawn)++;
    }
}

/* ------------------------------------------------------------------ */
/* Fills                                                               */
/* ------------------------------------------------------------------ */

void robo_desktop_chrome_fill(robo_gfx_raster_t *raster,
                              robo_gfx_rect_t rect,
                              robo_gfx_color_rgba8_t color,
                              robo_desktop_frame_record_t *record)
{
    robo_gfx_rect_t clip;
    robo_gfx_rect_t meeting;

    if (!raster || robo_gfx_rect_is_empty(rect)) {
        return;
    }
    /* Clipped is counted, not assumed: a window partly off the screen is a legitimate state, and the count
     * is how a caller learns its placement is showing less than it asked for. The graphics raster's own
     * clip is what does the cutting -- this file only *notices*. */
    if (robo_gfx_rect_is_valid(rect) && robo_gfx_raster_clip(raster, &clip) == ROBO_OK) {
        if (robo_gfx_rect_intersect(rect, clip, &meeting) != ROBO_OK ||
            !robo_gfx_rect_equal(meeting, rect)) {
            if (record) {
                record->clipped++;
            }
        }
    }
    if (robo_gfx_raster_fill_rect(raster, rect, color) == ROBO_OK && record) {
        record->fills++;
    }
}

/* ------------------------------------------------------------------ */
/* Bounds                                                              */
/* ------------------------------------------------------------------ */

robo_gfx_rect_t robo_desktop_chrome_title_bounds(const robo_desktop_t *desktop,
                                                const desktop_window_t *window)
{
    const robo_desktop_window_metrics_t *m = &desktop->window_metrics;

    return robo_gfx_rect(window->bounds.x + m->border,
                         window->bounds.y + m->border,
                         window->bounds.width - 2 * m->border,
                         m->title_height);
}

robo_gfx_rect_t robo_desktop_chrome_client_bounds(const robo_desktop_t *desktop,
                                                 const desktop_window_t *window)
{
    const robo_desktop_window_metrics_t *m = &desktop->window_metrics;
    robo_gfx_rect_t title = robo_desktop_chrome_title_bounds(desktop, window);

    return robo_gfx_rect(window->bounds.x + m->border,
                         title.y + title.height + m->content_inset,
                         window->bounds.width - 2 * m->border,
                         window->bounds.height - 2 * m->border - m->title_height -
                             m->content_inset);
}

robo_gfx_rect_t robo_desktop_chrome_close_bounds(const robo_desktop_t *desktop,
                                                const desktop_window_t *window)
{
    const robo_desktop_window_metrics_t *m = &desktop->window_metrics;
    robo_gfx_rect_t title = robo_desktop_chrome_title_bounds(desktop, window);

    return robo_gfx_rect(window->bounds.x + window->bounds.width - m->border - m->close_inset -
                             m->close_size,
                         title.y + (title.height - m->close_size) / 2,
                         m->close_size,
                         m->close_size);
}

/* ------------------------------------------------------------------ */
/* The shell                                                           */
/* ------------------------------------------------------------------ */

/** The baseline a label of a band of @p height is drawn on: half the cap height above the middle. */
static int32_t desktop_baseline_for_band(int32_t band_y, int32_t band_height)
{
    return band_y + (band_height / 2) + ROBO_DESKTOP_CAP_HALF;
}

robo_result_t robo_desktop_chrome_paint_shell(robo_desktop_t *desktop,
                                             robo_gfx_raster_t *raster,
                                             robo_desktop_frame_record_t *record,
                                             robo_desktop_diag_t *diag)
{
    const robo_desktop_geometry_t *geometry = &desktop->geometry;
    const robo_desktop_theme_t *theme = &desktop->theme;
    robo_gfx_rect_t label;
    int32_t baseline;
    size_t identity_length;
    size_t applications_length;
    size_t status_length;
    int32_t identity_width;

    (void)diag;

    /* 1. The desktop surface: the frame's explicit baseline, over the whole screen. */
    robo_desktop_chrome_fill(raster, geometry->screen, theme->desktop_surface, record);

    /* 2. The panel, and its last row as the hairline. */
    robo_desktop_chrome_fill(raster, geometry->panel, theme->panel_surface, record);
    {
        int32_t separator = desktop->panel.separator_height;

        if (separator > 0) {
            robo_desktop_chrome_fill(
                raster,
                robo_gfx_rect(geometry->panel.x,
                              geometry->panel.y + geometry->panel.height - separator,
                              geometry->panel.width, separator),
                theme->panel_separator, record);
        }
    }

    /* 3. The falloff rows below the panel: a stepped soft edge rather than a blur (see the theme). */
    if (geometry->work_area.y < geometry->screen.y + geometry->screen.height) {
        robo_desktop_chrome_fill(raster,
                                 robo_gfx_rect(geometry->screen.x, geometry->work_area.y,
                                               geometry->screen.width, 1),
                                 theme->shadow_near, record);
        if (geometry->work_area.y + 1 < geometry->screen.y + geometry->screen.height) {
            robo_desktop_chrome_fill(raster,
                                     robo_gfx_rect(geometry->screen.x, geometry->work_area.y + 1,
                                                   geometry->screen.width, 1),
                                     theme->shadow_far, record);
        }
    }

    baseline = geometry->panel.y + geometry->panel.height - desktop->panel.baseline_from_bottom;
    identity_length = robo_desktop_string_length(desktop->identity);
    applications_length = robo_desktop_string_length(desktop->applications);
    status_length = robo_desktop_string_length(desktop->status);
    identity_width =
        robo_desktop_chrome_label_width(desktop, desktop->identity, identity_length,
                                        desktop->label_size_px);

    /* 4. The identity mark: the shell's one accent, always drawn -- which is what keeps a face-less
     * desktop looking like a desktop rather than like a blank panel. */
    {
        int32_t mark_height = desktop->panel.identity_mark_height;
        int32_t mark_width = desktop->panel.identity_mark_width;
        int32_t mark_y = geometry->panel.y + (geometry->panel.height - mark_height) / 2;

        robo_desktop_chrome_fill(raster,
                                 robo_gfx_rect(geometry->identity.x, mark_y, mark_width,
                                               mark_height),
                                 theme->identity_accent, record);
    }

    /* 5. The three labels, each narrowed into its own area by its measured width. */
    if (identity_length > 0u &&
        robo_desktop_geometry_panel_area(geometry, ROBO_DESKTOP_PANEL_IDENTITY, identity_width,
                                         &label) == ROBO_OK) {
        robo_desktop_chrome_label(
            desktop, raster,
            label.x + desktop->panel.identity_mark_width + ROBO_DESKTOP_IDENTITY_GAP, baseline,
            desktop->identity, identity_length, theme->identity_text, desktop->label_size_px,
            &record->labels, &record->labels_unshaped);
    }
    if (applications_length > 0u) {
        int32_t width = robo_desktop_chrome_label_width(desktop, desktop->applications,
                                                       applications_length,
                                                       desktop->label_size_px);

        if (robo_desktop_geometry_panel_area(geometry, ROBO_DESKTOP_PANEL_APPLICATIONS, width,
                                             &label) == ROBO_OK) {
            robo_desktop_chrome_label(desktop, raster, label.x, baseline, desktop->applications,
                                      applications_length, theme->panel_meta_text,
                                      desktop->label_size_px, &record->labels,
                                      &record->labels_unshaped);
        }
    }
    if (status_length > 0u) {
        int32_t width = robo_desktop_chrome_label_width(desktop, desktop->status, status_length,
                                                       desktop->label_size_px);

        if (robo_desktop_geometry_panel_area(geometry, ROBO_DESKTOP_PANEL_STATUS, width, &label) ==
            ROBO_OK) {
            robo_desktop_chrome_label(desktop, raster, label.x, baseline, desktop->status,
                                      status_length, theme->panel_meta_text,
                                      desktop->label_size_px, &record->labels,
                                      &record->labels_unshaped);
        }
    }
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* A window                                                            */
/* ------------------------------------------------------------------ */

robo_result_t robo_desktop_chrome_paint_window_frame(robo_desktop_t *desktop,
                                                    const desktop_window_t *window,
                                                    int focused,
                                                    robo_gfx_raster_t *raster,
                                                    robo_desktop_frame_record_t *record,
                                                    robo_desktop_diag_t *diag)
{
    const robo_desktop_theme_t *theme = &desktop->theme;
    robo_gfx_rect_t title = robo_desktop_chrome_title_bounds(desktop, window);
    robo_gfx_rect_t client = robo_desktop_chrome_client_bounds(desktop, window);

    (void)diag;

    /*
     * The border is the *whole* outer rectangle, painted first and then covered by the title bar and the
     * client area -- which is why a one-pixel border reads as a ring without four separate side fills, and
     * why a zero-pixel border (an allowed restyle) simply has nothing to show.
     */
    robo_desktop_chrome_fill(raster, window->bounds,
                             focused ? theme->window_border_focused : theme->window_border, record);
    robo_desktop_chrome_fill(raster, title, theme->window_title_surface, record);
    /* The body, so that a window whose contents could not be rasterised is still a window rather than a
     * hole in the screen. */
    robo_desktop_chrome_fill(raster, client, theme->window_surface, record);
    return ROBO_OK;
}

void robo_desktop_chrome_paint_window_title(robo_desktop_t *desktop,
                                           const desktop_window_t *window,
                                           robo_gfx_raster_t *raster,
                                           robo_desktop_frame_record_t *record)
{
    robo_gfx_rect_t title = robo_desktop_chrome_title_bounds(desktop, window);
    size_t length = robo_desktop_string_length(window->title);

    if (length == 0u) {
        return;
    }
    robo_desktop_chrome_label(desktop, raster, title.x + desktop->window_metrics.title_inset,
                              desktop_baseline_for_band(title.y, title.height), window->title,
                              length, desktop->theme.window_title_text, desktop->label_size_px,
                              &record->labels, &record->labels_unshaped);
}

void robo_desktop_chrome_paint_close_button(robo_desktop_t *desktop,
                                           const desktop_window_t *window,
                                           robo_gfx_raster_t *raster,
                                           robo_desktop_frame_record_t *record)
{
    robo_gfx_rect_t button = robo_desktop_chrome_close_bounds(desktop, window);
    int32_t inset = ROBO_DESKTOP_CLOSE_CROSS_INSET;

    if (robo_gfx_rect_is_empty(button)) {
        return;
    }
    robo_desktop_chrome_fill(raster, button, desktop->theme.window_close, record);

    /*
     * The cross is drawn as two strokes rather than as a glyph, and that is the one place this build
     * deliberately does not need a font: a close button that vanished on a face-less desktop would be the
     * one control a person looks for first. Both strokes are counted in `fills` (see the file comment).
     */
    if (button.width > 2 * inset && button.height > 2 * inset) {
        robo_gfx_point_t a;
        robo_gfx_point_t b;

        a = robo_gfx_point(button.x + inset, button.y + inset);
        b = robo_gfx_point(button.x + button.width - 1 - inset,
                           button.y + button.height - 1 - inset);
        if (robo_gfx_raster_draw_line(raster, a, b, desktop->theme.window_close_mark) == ROBO_OK &&
            record) {
            record->fills++;
        }
        a = robo_gfx_point(button.x + button.width - 1 - inset, button.y + inset);
        b = robo_gfx_point(button.x + inset, button.y + button.height - 1 - inset);
        if (robo_gfx_raster_draw_line(raster, a, b, desktop->theme.window_close_mark) == ROBO_OK &&
            record) {
            record->fills++;
        }
    }
}
