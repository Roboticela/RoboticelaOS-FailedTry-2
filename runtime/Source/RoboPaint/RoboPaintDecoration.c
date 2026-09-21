/*
 * RoboPaint decoration and text painting (Phase 11.4–11.8) — see RoboPaintDecoration.h.
 *
 * The pass: walk the display list once, in list order, and draw each **drawing** command into
 * a RoboGraphics raster -- a background's border box, a border side's band, a text run's
 * glyphs, and an image's destination rectangle -- while counting everything a command turns out to be. The raster is the graphics module's own
 * immediate-mode CPU context, so the clip and the source-over blend are its, and the three
 * things this file owns are the walk, the clip **scope stack**, and the boundary a drawing is
 * applied under.
 *
 * **One walk, not two.** A background-only pass followed by a border-only pass would fill
 * every background in the document before every border in it, which paints a descendant's
 * background *under* its ancestor's border where CSS puts it over -- so the order here is the
 * list's own order, which is the traversal's, which is CSS's.
 *
 * The clip is applied with the raster's own `reset_clip`/`clip`/`set_clip` rather than by
 * intersecting rectangles here, and the reason is that the raster's clip is *already* the one
 * place a draw is bounded: reset to the surface bounds, replace the axis the boundary hides,
 * and let `set_clip` intersect -- so "the boundary bounds the fill" is the graphics module's
 * arithmetic, not a second copy of it.
 *
 * **Since 11.8 that boundary comes from the walk's own clip stack** rather than only from the
 * command's field. A `push-clip` command pushes, a `pop-clip` pops, and a drawing inside a scope
 * is bounded by the scope -- which is the same value its own field carries, because the builder
 * refuses a command inside a scope that disagrees, so the two paths are one number by
 * construction. The command's own field remains the fallback for a list with no clip commands
 * in it (a hand-built one, or one from a document that clips nothing), where it is the only
 * statement of clipping the list carries. The stack is what makes the *scope* the authority for
 * what is drawn inside it, and the counters say how often that happened.
 *
 * Nothing here reads a tree, a computed style or the box model: a display command carries the
 * one rectangle to fill, the boundary in force and, for a band, the style that shapes it. The
 * only conversions left are the colour (`RoboPaintColor.h`'s) and, since 11.6, a character into
 * a glyph -- which is the graphics module's bridge, one call per character.
 *
 * **Since 11.7 an image is the one drawing whose appearance is not a colour**, and the pass holds
 * it to a different rule in exactly one place: the four colour terms below apply to a command
 * whose look *is* a colour, and an image command is instead asked whether its payload is one this
 * build can draw. Everything else is shared with a fill -- the same clip from the walk's stack,
 * the same strength on the raster, the same geometry check -- so there is no second drawing path
 * here, only one more kind of appearance. The sampling, the scaling and the alpha are all
 * `robo_gfx_raster_draw_image`'s, because pixels are RoboGraphics' business and a caller that
 * sampled an image itself would be re-deciding the blend and the clip this module already owns.
 */

#include "RoboPaint/RoboPaintDecoration.h"

#include "RoboGraphics/RoboGraphicsRaster.h"
#include "RoboPaint/RoboPaintClip.h"
#include "RoboPaint/RoboPaintOpacity.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsText.h"

#include "RoboCSS/RoboCSSBorder.h"
#include "RoboFont/RoboFontTypes.h"

#include "RoboPaint/RoboPaintColor.h"

#include "RoboPaintInternal.h"

#include <string.h>

void robo_paint_decoration_stats_init(robo_paint_decoration_stats_t *stats)
{
    if (stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

/**
 * Release the pass's three owned resources and return @p status unchanged.
 *
 * The pass has many ways out -- every failure plus the end -- and the raster and the two scope
 * stacks are created together and must be released together. A helper rather than a pair of
 * calls at every exit, because a cleanup that is written out at every exit is a cleanup that is
 * eventually written out at all but one of them: the leak a missing pair would leave is in a
 * *failure* path, which is exactly the path a test that only paints good documents never walks.
 *
 * Both stacks are released here, and the second one's arrival in 11.9 is why this signature
 * grew rather than why a second helper appeared: the three resources have one end.
 */
static robo_result_t decoration_finish(robo_gfx_raster_t *raster,
                                       robo_paint_clip_stack_t *clips,
                                       robo_paint_opacity_stack_t *opacities,
                                       robo_result_t status)
{
    robo_paint_opacity_stack_destroy(opacities);
    robo_paint_clip_stack_destroy(clips);
    robo_gfx_raster_destroy(raster);
    return status;
}

/** The style a filled band was shaped by is one this build draws as `solid`. */
static int decoration_style_is_shaped(robo_css_border_style_t style)
{
    return style == ROBO_CSS_BORDER_STYLE_SOLID;
}

/*
 * Drawing a run (11.6).
 *
 * The whole of the glyph path is three calls per character, and each of them is a door this
 * module does not own: the face's cmap resolves a code point to a glyph id, the graphics
 * module's bridge scales that glyph's advance and outline, and the raster strokes the outline.
 * A painter that measured a glyph itself would be a second font-metrics implementation with a
 * different answer from the one the box was sized with -- which is the failure
 * `RoboLayoutTextFont.h` names as the one no later phase could detect, arriving from the other
 * side.
 *
 * **The pen is a `float` and the origin is snapped.** Advances are continuous and a run's
 * glyphs must not drift apart, so the position is accumulated in device units and only the
 * *drawn* origin is rounded -- by `robo_gfx_glyph_snap_point`, the graphics module's own
 * policy rather than a local `(int)` cast. Layout's width for the same run is a **single**
 * rounding of a font-unit sum (`RoboLayoutTextFont.h` argues why it sums first), so the pen
 * here can differ from that width by a fraction of a pixel at the end of a long run; the last
 * glyph may therefore sit within a sub-pixel of the width the line was justified to. That is
 * the honest limit of two implementations of "advance" sharing one font, and it is stated
 * rather than papered over.
 */

/**
 * Draw @p command's run through @p face into @p raster.
 *
 * @param out_glyphs  receives the glyph draws attempted.
 * @param out_missing receives the code points drawn as `.notdef` (or with unusable metrics).
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT when the command is not a drawable text command;
 *         ROBO_NOT_FOUND when the command's span names no run in @p pool, which the list's own
 *         append rule makes unreachable and is reported rather than trusted; whatever a glyph
 *         draw returned when it failed.
 */
static robo_result_t decoration_draw_text(robo_gfx_raster_t *raster,
                                          robo_font_face_t *face,
                                          const robo_paint_text_pool_t *pool,
                                          const robo_paint_display_command_t *command,
                                          robo_gfx_color_rgba8_t color,
                                          size_t *out_glyphs,
                                          size_t *out_missing,
                                          robo_paint_diag_t *diag)
{
    const uint32_t *code_points;
    robo_gfx_glyph_draw_t draw;
    robo_paint_text_run_t run;
    float pen_x;
    float baseline_y;
    uint32_t index;

    *out_glyphs = 0u;
    *out_missing = 0u;

    run = robo_paint_display_command_text(command);
    if (!pool || !robo_paint_text_pool_contains(pool, run.span)) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_NOT_FOUND,
                               "a text command's span names no run in the list's pool");
    }
    code_points = robo_paint_text_pool_code_points(pool);
    if (!code_points) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_NOT_FOUND,
                               "the list's text pool holds no characters");
    }

    /* The pen starts at the run rectangle's own left edge and the baseline is the one number
     * the command carries beyond it -- a `TEXT` command's rectangle is the placement, so the
     * baseline is at `rect.y + baseline_offset` in the same space. */
    pen_x = (float)command->rect.x;
    baseline_y = (float)robo_paint_add_saturated(command->rect.y, run.baseline_offset);
    robo_gfx_glyph_draw_init(&draw);
    draw.face = face;
    draw.size_px = (float)run.font.font_size_px;
    draw.color = color;

    for (index = 0u; index < (uint32_t)run.span.count; index++) {
        robo_font_cmap_lookup_result_t lookup;
        robo_gfx_glyph_metrics_device_t metrics;
        robo_glyph_id_t glyph;
        uint32_t code_point = code_points[(size_t)run.span.first + index];
        int mapped;

        memset(&lookup, 0, sizeof(lookup));
        mapped = robo_font_face_lookup_glyph(face, code_point, &lookup) == ROBO_OK &&
                 lookup.status == ROBO_FONT_CMAP_MAPPED;
        glyph = mapped ? lookup.glyph_id : ROBO_GLYPH_ID_NONE;
        if (!mapped) {
            /* `.notdef`, counted -- the measurer's rule and its number, one layer along. */
            (*out_missing)++;
        }

        draw.glyph_id = glyph;
        draw.origin = robo_gfx_point_f(pen_x, baseline_y);
        if (robo_gfx_raster_draw_glyph(raster, &draw) != ROBO_OK) {
            return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                                   "a glyph could not be drawn");
        }
        (*out_glyphs)++;

        /*
         * The advance takes the metrics door's own answer, and a glyph whose metrics will
         * not parse advances by **nothing** and is counted as a miss -- the measurer's
         * refusal, restated: a broken font that produced a narrower run here than a working
         * one would make the glyphs pile up on each other in a way that reads as a rendering
         * bug rather than as a missing table.
         */
        memset(&metrics, 0, sizeof(metrics));
        if (robo_gfx_glyph_metrics_device(face, glyph, draw.size_px, &metrics) == ROBO_OK) {
            pen_x += metrics.advance_width;
        } else if (mapped) {
            (*out_missing)++;
        }
    }
    return ROBO_OK;
}

/**
 * Reset @p raster to the surface bounds and, when @p clip bounds an axis, bound that axis.
 *
 * Reset-then-replace rather than intersect, because the raster's own clip is the one place a
 * fill is bounded: the axis the boundary leaves open keeps the surface's own interval, and the
 * axis it hides takes the boundary's. @p out_applied is set only when a boundary was applied,
 * so "drawn under a clip" stays a fact about the command rather than about the surface.
 */
static robo_result_t decoration_apply_clip(robo_gfx_raster_t *raster,
                                           const robo_paint_clip_t *clip,
                                           int *out_applied,
                                           robo_paint_diag_t *diag)
{
    robo_gfx_rect_t full;
    robo_gfx_rect_t wanted;

    *out_applied = 0;

    /* Every command starts from the surface bounds, so a boundary from the previous command
     * cannot leak onto this one. */
    if (robo_gfx_raster_reset_clip(raster) != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                               "the raster could not reset its clip to the surface bounds");
    }
    if (!robo_paint_clip_is_active(clip)) {
        return ROBO_OK;
    }
    if (robo_gfx_raster_clip(raster, &full) != ROBO_OK) {
        /* The surface's drawable area is not representable as int32 pixel bounds -- which the
         * raster's own creation already refused -- so this is a graphics-layer failure rather
         * than a document one. */
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                               "the surface bounds are not representable");
    }

    wanted = full;
    if (robo_paint_clip_axis_is_active(&clip->horizontal)) {
        /* The interval is half-open and validated non-negative by the clip record, and the
         * extent saturates rather than wrapping for the reason every accumulation in this
         * module does. */
        wanted.x = clip->horizontal.start;
        wanted.width = robo_paint_extent((int64_t)clip->horizontal.end -
                                         (int64_t)clip->horizontal.start);
    }
    if (robo_paint_clip_axis_is_active(&clip->vertical)) {
        wanted.y = clip->vertical.start;
        wanted.height = robo_paint_extent((int64_t)clip->vertical.end -
                                          (int64_t)clip->vertical.start);
    }
    if (robo_gfx_raster_set_clip(raster, wanted) != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_ERROR,
                               "the boundary could not be applied to the raster clip");
    }
    *out_applied = 1;
    return ROBO_OK;
}

robo_result_t robo_paint_decoration_paint(const robo_paint_display_list_t *list,
                                          robo_gfx_surface_t *target,
                                          robo_font_face_t *face,
                                          robo_paint_decoration_stats_t *out_stats,
                                          robo_paint_diag_t *diag)
{
    robo_paint_decoration_stats_t stats;
    robo_paint_text_pool_t *pool;
    robo_paint_clip_stack_t *clips = NULL;
    robo_paint_opacity_stack_t *opacities = NULL;
    robo_gfx_raster_t *raster = NULL;
    robo_result_t status;
    size_t index;
    size_t count;

    if (!list || !target) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_INVALID_ARGUMENT,
                               "the display list and the target surface are required");
    }

    robo_paint_decoration_stats_init(&stats);
    /* The list's own characters, borrowed for the pass -- the pool a `draw-text`'s span
     * names, which is the list's because a span is meaningless apart from one. */
    pool = robo_paint_display_list_text_pool(list);

    /*
     * The raster is the graphics module's, so the clip and the blend are its rules. Its
     * creation enforces that the target is write-capable and of an implemented format, which
     * is why a read-only surface refuses here rather than at the first fill.
     *
     * Its two refusals arrive as **different codes** -- a target that cannot take a write is
     * `ROBO_INVALID_ARGUMENT` to the graphics module and an unimplemented format is
     * `ROBO_NOT_SUPPORTED` -- and both are the same answer one level up: there is nothing to
     * paint into. This door says so once, with the code this header names, which is why the
     * mapping is here rather than two branches that would each have to be read separately.
     * `ROBO_OUT_OF_MEMORY` is **not** folded in: that is this build failing rather than the
     * caller's surface being wrong, and a caller retrying after freeing something deserves to
     * be told which it was.
     */
    status = robo_gfx_raster_create(target, &raster);
    if (status == ROBO_OUT_OF_MEMORY) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_OUT_OF_MEMORY,
                               "a raster could not be allocated for the target surface");
    }
    if (status != ROBO_OK) {
        return ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY, ROBO_NOT_SUPPORTED,
                               "the target surface cannot be rasterised (it is read-only or "
                               "of a format this build does not implement)");
    }

    /*
     * The face's **character map**, prepared once for the pass. A `robo_font_face_t` is
     * created without one -- `robo_font_face_lookup_glyph` answers `ROBO_NOT_SUPPORTED` for a
     * face whose cmap has not been parsed -- so the pass asks for it here, through the same
     * door the layout measurer uses before it resolves a single glyph
     * (`RoboLayoutTextFont.c`). One font layer builds the map; this is the layer that says
     * when, and it must say so before the first lookup rather than after it.
     *
     * A face whose cmap **will not parse** is not refused. At worst every lookup misses,
     * every glyph is `.notdef`, and `glyphs_missing` names the fact per code point -- which is
     * the answer this pass already gives an unmapped character, so a broken font degrades to
     * the same shape as a font with no glyph for the text rather than to a failed pass.
     */
    if (face != NULL) {
        (void)robo_font_face_parse_cmap(face);
    }

    /*
     * The walk's clip stack (11.8). Created **after** the raster rather than before it, so
     * that a target the graphics layer refuses costs no allocation, and empty at the start
     * because a document that declares no overflow stands inside no scope at all.
     */
    status = robo_paint_clip_stack_create(&clips, NULL);
    if (status != ROBO_OK) {
        return decoration_finish(raster, NULL, NULL,
                                 ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_CLIP,
                                                 ROBO_OUT_OF_MEMORY,
                                                 "could not allocate the pass's clip stack"));
    }
    /*
     * The walk's opacity stack (11.9), for the strength the same reason the clip stack is for the
     * boundary: a scope is stated **once**, at its push, and every drawing under it is drawn at
     * the strength in force rather than at a copy of it. Empty at the start, because a document
     * that declares no opacity stands inside no scope at all -- which is the state in which the
     * raster's own opacity is untouched for the whole pass, and a document that reads exactly as
     * it did before this phase.
     */
    status = robo_paint_opacity_stack_create(&opacities, NULL);
    if (status != ROBO_OK) {
        return decoration_finish(raster, clips, opacities,
                                 ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_OPACITY,
                                                 ROBO_OUT_OF_MEMORY,
                                                 "could not allocate the pass's opacity "
                                                 "stack"));
    }

    count = robo_paint_display_list_count(list);
    for (index = 0u; index < count; index++) {
        robo_paint_display_command_t command;
        robo_gfx_color_rgba8_t color;
        int clipped = 0;
        /*
         * Whether this command's appearance is a colour at all (11.7) -- declared here rather
         * than at each of the four questions below, because the four are one question asked
         * about the same command and a second spelling would be a second answer.
         */
        int has_color = 0;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            /* Unreachable: the index came from the list's own count. */
            return decoration_finish(raster, clips, opacities,
                                     ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY,
                                                     ROBO_ERROR,
                                                     "the display list changed under the "
                                                     "pass"));
        }
        stats.commands++;

        /*
         * The **clip scope** (11.8) is maintained before the drawing branch, because a scope
         * is not a drawing: a push becomes the boundary the pass stands inside and a pop
         * restores the one outside it. Neither touches the raster -- the clip a drawing is
         * bounded by is applied when that drawing is drawn, from the stack below -- so a scope
         * with nothing inside it costs two commands and no graphics work at all.
         *
         * Both are counted with the commands this pass does not draw, so
         * `commands = draws + skipped` stays exact; `clip_scopes` is the number that says how
         * many of those skips were a boundary rather than a node marker.
         *
         * A **pop with nothing to pop** is the one list this pass can be handed that no
         * builder produces, and it is refused rather than tolerated: a consumer that closed a
         * scope that was never opened would go on drawing under a boundary that is not in force.
         * The refusal is reported with the pass's own domain, which is right -- the *list* is
         * malformed, and a caller holding it is the one who can fix it.
         */
        if (robo_paint_display_command_kind_is_clip(command.kind)) {
            stats.skipped++;
            if (command.kind == ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP) {
                if (robo_paint_clip_stack_push(clips, &command.clip, diag) != ROBO_OK) {
                    return decoration_finish(raster, clips, opacities,
                                             diag ? diag->code : ROBO_ERROR);
                }
                stats.clip_scopes++;
                if (robo_paint_clip_stack_depth(clips) > stats.clip_depth_max) {
                    stats.clip_depth_max = robo_paint_clip_stack_depth(clips);
                }
            } else if (robo_paint_clip_stack_pop(clips, diag) != ROBO_OK) {
                return decoration_finish(raster, clips, opacities, diag ? diag->code : ROBO_ERROR);
            }
            continue;
        }

        /*
         * The **opacity scope** (11.9) is the same shape one field along: a structural command,
         * so it draws nothing and is one of `skipped`; a push becomes the strength the pass
         * stands inside and a pop restores the one outside it; and neither touches the raster,
         * because the strength a drawing is drawn at is applied **when that drawing is drawn**,
         * from the stack below. A scope with nothing inside it therefore costs two commands and
         * no graphics work at all -- and, because the raster's own default is full strength, a
         * document that declares no opacity never has its opacity touched once.
         *
         * A push of an **invalid** record cannot arrive: the display list's own append refuses
         * one, so this is the list's rule rather than a check to repeat. A **pop with nothing to
         * pop** is refused, for the clip's reason: a consumer that closed a strength that was
         * never established would go on drawing at a strength that is not in force.
         */
        if (robo_paint_display_command_kind_is_opacity(command.kind)) {
            stats.skipped++;
            if (command.kind == ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY) {
                if (robo_paint_opacity_stack_push(opacities, &command.opacity, diag) != ROBO_OK) {
                    return decoration_finish(raster, clips, opacities,
                                             diag ? diag->code : ROBO_ERROR);
                }
                stats.opacity_scopes++;
                if (robo_paint_opacity_stack_depth(opacities) > stats.opacity_depth_max) {
                    stats.opacity_depth_max = robo_paint_opacity_stack_depth(opacities);
                }
            } else if (robo_paint_opacity_stack_pop(opacities, diag) != ROBO_OK) {
                return decoration_finish(raster, clips, opacities, diag ? diag->code : ROBO_ERROR);
            }
            continue;
        }

        /*
         * A structural command draws nothing, by definition -- so `skipped` is a statement
         * about the list rather than a gap in this pass.
         */
        if (!robo_paint_display_command_kind_draws(command.kind)) {
            stats.skipped++;
            continue;
        }

        /*
         * The colour, before the geometry, because "there was nothing to draw" and "there was
         * nowhere to draw it" are different facts and this is the one a document controls.
         *
         * A colour the record layer's own rule refuses (`none`, `invalid`) is not a drawing
         * this pass can attempt at all, so it is counted with the commands that are not
         * drawings rather than with the ones that are -- which is what keeps
         * `commands = draws + skipped` exact for a hand-built list whose command the list's
         * validity rule would have refused.
         *
         * It is asked of the kinds that **have** a colour, which 11.7 makes a narrower set than the
         * drawing kinds: an image command carries `none` and is asked the same question about its
         * payload instead, a few lines down. Writing this as "every drawing command" would refuse
         * every image in the document -- the one mistake that would make this phase look like it did
         * nothing at all. The answer is read once into a local flag rather than re-spelled at each
         * of the four places below that ask it, because four spellings of "does this command have a
         * colour" are four places for the answer to drift.
         */
        has_color = robo_paint_display_command_kind_uses_color(command.kind);
        if (has_color && command.color.kind != ROBO_CSS_COLOR_RGBA &&
            command.color.kind != ROBO_CSS_COLOR_CURRENT) {
            stats.skipped++;
            continue;
        }
        /*
         * And the image's own version of that question (11.7), asked where the colour's is and for
         * the same reason: a payload this build cannot draw is not a drawing this pass can attempt,
         * so it is a statement about the document -- `no_resource` -- rather than a failed pass. A
         * list this build produced never reaches here, because the command layer's validity rule
         * refuses such a command at append; the term exists so that a hand-built list is counted
         * rather than silently swallowing the remainder of an identity.
         */
        if (!has_color && !robo_paint_image_is_valid(&command.image)) {
            stats.no_resource++;
            continue;
        }
        /*
         * And the same rule for the one thing a text command needs that a fill does not: its
         * characters. The list's own append refuses a span that names no run, so this is
         * unreachable through the public API -- and it is checked here anyway, and beside the
         * colour rather than after the counters, so that a command this pass cannot draw is
         * reported as skipped rather than landing in a bucket that would then not add up.
         */
        if (command.kind == ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT &&
            !robo_paint_text_pool_contains(pool, command.text.span)) {
            stats.skipped++;
            continue;
        }

        stats.draws++;
        if (command.kind == ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND) {
            stats.backgrounds++;
        } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER) {
            stats.borders++;
            if (!decoration_style_is_shaped(command.style)) {
                stats.unshaped++;
            }
        } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE) {
            stats.images++;
        } else {
            stats.texts++;
        }

        /*
         * The four reasons a *drawing* draws nothing, each named for the document fact it is:
         * a `currentColor` this layer cannot resolve, a colour that occupies no pixel, a
         * rectangle with no area, and the blank between them. They are the terms of
         * `draws = filled + contextual + transparent + empty` taken in order, so every
         * drawing lands in exactly one bucket and the counter is a statement rather than a
         * remainder.
         */
        if (has_color && command.color.kind == ROBO_CSS_COLOR_CURRENT) {
            stats.contextual++;
            continue;
        }
        if (has_color && command.color.alpha == 0) {
            stats.transparent++;
            continue;
        }
        /*
         * A text command is **not** held to the rectangle rule, and the exception is a fact
         * about text rather than a special case: a run's rectangle is its *advance* box, and a
         * glyph's ink can overhang it -- so a rectangle with no area is not "nothing to draw"
         * for a run the way it is for a fill, and the glyphs are drawn and the raster's clip
         * decides what of them lands. The fill half keeps the rule because a fill with no area
         * genuinely has no pixels to put a colour on.
         */
        if (command.kind == ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT) {
            if (face == NULL) {
                stats.no_font++;
                continue;
            }
        } else if (command.rect.width <= 0 || command.rect.height <= 0) {
            stats.empty++;
            continue;
        }
        if (has_color && robo_paint_color_to_rgba8(command.color, &color) != ROBO_OK) {
            /* Unreachable: an opaque `RGBA` is exactly the colour `to_rgba8` copies. Reported
             * rather than counted, because a fill that could not be prepared is this build
             * failing and no bucket above would be telling the truth about it. */
            return decoration_finish(raster, clips, opacities,
                                     ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY,
                                                     ROBO_ERROR,
                                                     "a drawing command's resolved colour "
                                                     "could not be converted"));
        }

        /*
         * The boundary this drawing is bounded by. Inside a scope it is the **scope's** clip --
         * the list states it once, at the push, and every drawing under it is bounded by that
         * one fact rather than by its own copy of it. Outside any scope it is the command's own
         * clip, which is the only statement of clipping a list without clip commands carries
         * (a hand-built one). The builder refuses a command inside a scope that disagrees with
         * it, so for every list this build produced the two values are equal and this choice is
         * not observable -- which is exactly what makes it safe to read from the stack.
         */
        {
            robo_paint_clip_t effective;

            if (robo_paint_clip_stack_is_empty(clips)) {
                effective = command.clip;
            } else {
                robo_paint_clip_stack_current(clips, &effective);
            }
            status = decoration_apply_clip(raster, &effective, &clipped, diag);
        }
        if (status != ROBO_OK) {
            return decoration_finish(raster, clips, opacities, status);
        }
        if (clipped) {
            stats.clipped++;
        }

        /*
         * The **strength** this drawing reaches the surface at, read from the same place the
         * boundary is and for the same reason: inside a scope it is the **scope's** record --
         * the list states it once, at the push -- and outside any scope it is the command's own,
         * which is the only statement of opacity a list without opacity commands carries. The
         * builder refuses a command inside a scope that disagrees with it, so for every list
         * this build produced the two are equal and the choice is not observable -- which is
         * what makes reading from the stack safe.
         *
         * It is set **per drawing** rather than saved and restored around a scope, and that is
         * a decision worth stating: the raster's opacity is one number, so leaving it at whatever
         * the last scope set is impossible to get wrong only if every drawing writes it. A
         * drawing therefore never inherits a strength from the drawing before it, and a scope
         * that contains nothing leaves the raster exactly as it found it. Setting it to `255` --
         * what an unscoped drawing gets -- is the raster's own default, so a document with no
         * opacity in it performs the same store a document with none would have skipped, and
         * `faded` counts the stores that were not to `255`.
         *
         * The colour's own alpha is **not** touched here. `final_alpha = colour_alpha *
         * effective_opacity` is the raster's arithmetic (`RoboGraphicsRaster.h`: global opacity
         * multiplies source alpha before the blend), so a half-transparent fill inside a half
         * opaque element composites the way both numbers say it should, and neither number is
         * rewritten into the other.
         */
        {
            robo_paint_opacity_t effective;
            uint8_t byte;

            if (robo_paint_opacity_stack_is_empty(opacities)) {
                effective = command.opacity;
            } else {
                robo_paint_opacity_stack_current(opacities, &effective);
            }
            byte = robo_paint_opacity_to_byte(&effective);
            robo_gfx_raster_set_opacity(raster, byte);
            if (byte != 255u) {
                stats.faded++;
            }
        }

        /*
         * The image, drawn through the graphics module's own primitive -- the *scaled* half of
         * its 1:1 copy -- and the three facts it is handed are the three the command carries: the
         * resource, the rectangle of it to sample, and the destination. Everything else has already
         * been done for it by the time the walk reaches here: the clip is the raster's, the strength
         * is the raster's, and the geometry rule above refused a destination with no area.
         *
         * A refusal is reported rather than counted, and the asymmetry with the terms above is
         * deliberate: those are statements about the *document*, where a primitive that refused a
         * payload its own validity rule admitted is this build disagreeing with itself -- the same
         * shape as a rectangle that will not fill, one branch below.
         */
        if (command.kind == ROBO_PAINT_DISPLAY_CMD_DRAW_IMAGE) {
            status = robo_gfx_raster_draw_image(raster, command.image.resource,
                                                command.image.source, command.rect);
            if (status != ROBO_OK) {
                return decoration_finish(raster, clips, opacities,
                                         ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY,
                                                         ROBO_ERROR,
                                                         "an image command's drawing could "
                                                         "not be performed"));
            }
            stats.filled++;
            continue;
        }

        if (command.kind == ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT) {
            size_t glyphs = 0u;
            size_t missing = 0u;

            if (command.text.font.direction == ROBO_LAYOUT_TEXT_DIRECTION_RTL) {
                /* No bidi in this build: an RTL run's characters are drawn left to right
                 * from its origin, and the count says how many runs that happened to. */
                stats.rtl++;
            }
            status = decoration_draw_text(raster, face, pool, &command, color, &glyphs,
                                          &missing, diag);
            if (status != ROBO_OK) {
                return decoration_finish(raster, clips, opacities, status);
            }
            stats.glyphs += glyphs;
            stats.glyphs_missing += missing;
            stats.filled++;
            continue;
        }

        /*
         * The band a border side occupies, and the box a background fills: the same fill,
         * because a display command has already resolved the difference. Only the **shape**
         * inside the band is a border's own, and this build draws it as its band -- the
         * simplification `stats.unshaped` counts rather than hides.
         */
        status = robo_gfx_raster_fill_rect(raster, command.rect, color);
        if (status != ROBO_OK) {
            return decoration_finish(raster, clips, opacities,
                                     ROBO_PAINT_DIAG(diag, ROBO_PAINT_DOMAIN_GEOMETRY,
                                                     ROBO_ERROR,
                                                     "a drawing command's rectangle could "
                                                     "not be filled"));
        }
        stats.filled++;
    }

    if (out_stats) {
        *out_stats = stats;
    }
    return decoration_finish(raster, clips, opacities, ROBO_OK);
}
