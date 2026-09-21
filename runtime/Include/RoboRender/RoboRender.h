/*
 * RoboRender — the module's front door (Phase 11.12).
 *
 * One release, and its table is the interesting half: **ten 1s and six 0s**, and every 0 names the
 * phase that would make it a 1 rather than leaving a caller to infer it. That arrangement is the one
 * RoboPaint's capability table established and the reason it works: a caller deciding what to build
 * next reads the 0s, a caller deciding whether to look for pixels reads the 1s, and neither has to
 * read the source to find out which of the two it is holding.
 *
 * The six 0s, and what each would need -- because "not implemented" is not a design, and a table that
 * did not say why would be a table a reader has to trust:
 *
 *   `presentation`  a target reaching a window, a display or a framebuffer. Phase 11.13's:
 *                   `robo_render_target_surface` is the door it takes, and the work is a copy plus
 *                   whatever the display path needs (a format, a paced handoff, a VBlank signal);
 *   `compositor`    more than one surface, an ordering, and a place to put the result. A frame here
 *                   is one list into one target; a compositor is a *set* of frames with stacking;
 *   `tiles`         a target divided into independently rasterisable pieces, with a decision about
 *                   which pieces a change touched. Needs the region *subtraction* 11.11 refused;
 *   `damage_tracking` a history of what a scene was, so "unchanged" is answerable without a
 *                   comparison. 11.11's set is the *per-transition* answer, and a history is a
 *                   different structure with a lifetime;
 *   `frame_scheduling` a clock, a deadline and a queue. This module has none of the three, and
 *                   that is why it is deterministic: nothing here can depend on *when* it ran;
 *   `gpu`           a device, a command buffer, a resource manager and a fallback path. Everything
 *                   above that -- a backend boundary, a shader story, a present queue -- is absent
 *                   rather than stubbed, so a GPU backend is a module and not a branch.
 *
 * What it deliberately does **not** have a 0 for is the thing it would be easy to be vague about:
 * transforms, filters, masks, blend modes and `border-radius` are not capabilities of a *rasteriser*
 * in this build's shape at all -- they are properties the display list would have to carry and the
 * graphics raster would have to implement, and both are named where they belong
 * (`RoboGraphics`' own reserved doors, `RoboPaint`'s absent records). A capability row for each here
 * would suggest this module is where they would arrive, which is the one thing that is not true.
 */
#ifndef ROBO_RENDER_H
#define ROBO_RENDER_H

#include "RoboBase/RoboError.h"

#include "RoboRender/RoboRenderDiagnostics.h"
#include "RoboRender/RoboRenderRasterizer.h"
#include "RoboRender/RoboRenderVersion.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * What this build's renderer does and does not do.
 *
 * **Read positionally or by name, never by count**: every field is a flag, the table is asserted
 * name-by-name in declaration order by `render.conformance.capabilities`, and a field inserted in the
 * middle renumbers every row after it -- which is a constraint about a test rather than about the
 * claim, and the claim is what a caller should read.
 */
typedef struct robo_render_capabilities {
    /** The subsystem itself: owned pixel targets and runs over a display list (11.12). */
    int rasterizer;
    /** Targets: owned, RGBA8, byte-addressable, with a byte-level equality and a checksum. */
    int pixel_targets;
    /** Executing the **whole** display command vocabulary, scope pairs included. */
    int command_execution;
    /** Re-executing a list bounded by a paint invalidation set's regions (11.11 + 11.12). */
    int region_runs;
    /** Honouring the clip scopes the list states, and the bound a region run adds. */
    int clipping;
    /** Honouring the opacity scopes the list states, as a per-drawing product. */
    int opacity;
    /** Drawing a run's glyphs, through a caller-supplied face. */
    int text;
    /** Drawing an image payload, through RoboGraphics' own scaled sampler. */
    int images;
    /** Byte-identical output for identical input, checkable through two doors. */
    int determinism;
    /** An execution record: what a run carried, executed and cost. */
    int frame_records;
    /** Presenting a target to a window, a display or a framebuffer. **0**: Phase 11.13. */
    int presentation;
    /** Compositing more than one surface. **0**: no layer tree exists yet. */
    int compositor;
    /** Rasterising a target in independently addressable pieces. **0**: see the header comment. */
    int tiles;
    /** A history of what was on the screen, so "unchanged" needs no comparison. **0**. */
    int damage_tracking;
    /** A clock, a deadline and a queue. **0**: and 11.12's determinism depends on its absence. */
    int frame_scheduling;
    /** A GPU backend. **0**: a module, not a branch. */
    int gpu;
} robo_render_capabilities_t;

/** Zero @p capabilities (every feature reported absent). Safe on NULL. */
void robo_render_capabilities_init(robo_render_capabilities_t *capabilities);

/** Fill @p out with this build's capabilities.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p out. */
robo_result_t robo_render_get_capabilities(robo_render_capabilities_t *out);

/** The version this build was compiled as ("0.1.0"); never NULL. */
const char *robo_render_version_string(void);

/** The version this build was compiled as, by field.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for any NULL @p out argument. */
robo_result_t robo_render_get_version(robo_i32 *out_major,
                                     robo_i32 *out_minor,
                                     robo_i32 *out_patch);

/**
 * Non-zero when rasterization exists in this build.
 *
 * A function rather than only a capability field, for the reason RoboPaint's drawing door gives: it is
 * the question a caller actually asks -- before deciding whether to allocate a target, or to hand a
 * display list somewhere at all -- and a function cannot be read past.
 *
 * It answers **1** in this release, and the condition was written down one module down before there
 * was anything here: `RoboPaintVersion.h`'s own release note names `raster` as the flag a build in
 * which "a whole display list reaches a surface as a frame" would turn. What it does **not** mean is
 * that a frame is on a screen: it means the pixels exist in a buffer this module owns, and getting
 * them out is `robo_render_target_surface` plus Phase 11.13. `presentation` beside it in the table
 * above is the 0 that says so.
 */
int robo_render_rasterization_is_implemented(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_RENDER_H */
