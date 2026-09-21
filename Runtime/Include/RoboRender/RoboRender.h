/*
 * RoboRender — the module's front door (Phase 11.12).
 *
 * One release, and its table is the interesting half: **eleven 1s and five 0s**, and every 0 names the
 * phase that would make it a 1 rather than leaving a caller to infer it. That arrangement is the one
 * RoboPaint's capability table established and the reason it works: a caller deciding what to build
 * next reads the 0s, a caller deciding whether to look for pixels reads the 1s, and neither has to
 * read the source to find out which of the two it is holding.
 *
 * The five 0s, and what each would need -- because "not implemented" is not a design, and a table that
 * did not say why would be a table a reader has to trust:
 *
 *   `compositor`    more than one surface, an ordering, and a place to put the result. A frame here
 *                   is one list into one target presented into one destination; a compositor is a
 *                   *set* of frames with stacking, and a layer tree to hold them;
 *   `tiles`         a target divided into independently rasterisable pieces, with a decision about
 *                   which pieces a change touched. Needs the region *subtraction* 11.11 refused;
 *   `damageTracking` a history of what a scene was, so "unchanged" is answerable without a
 *                   comparison. 11.11's set is the *per-transition* answer, and a history is a
 *                   different structure with a lifetime;
 *   `frameScheduling` a clock, a deadline and a queue. This module has none of the three, and
 *                   that is why it is deterministic: nothing here can depend on *when* it ran;
 *   `gpu`           a device, a command buffer, a resource manager and a fallback path. Everything
 *                   above that -- a backend boundary, a shader story, a present queue -- is absent
 *                   rather than stubbed, so a GPU backend is a module and not a branch.
 *
 * `presentation` was the sixth 0 and is now the eleventh 1: 11.13 added `RenderPresent`, which
 * composites one frame into one destination surface at one offset. It is a **call and not a system** --
 * the destination is the caller's, there is no queue, no pacing and no second buffer -- and the row
 * that would describe a presentation *system* is `compositor`, still 0 beside it.
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
#include "RoboRender/RoboRenderPresenter.h"
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
typedef struct RenderCapabilities {
    /** The subsystem itself: owned pixel targets and runs over a display list (11.12). */
    int rasterizer;
    /** Targets: owned, RGBA8, byte-addressable, with a byte-level equality and a checksum. */
    int pixelTargets;
    /** Executing the **whole** display command vocabulary, scope pairs included. */
    int commandExecution;
    /** Re-executing a list bounded by a paint invalidation set's regions (11.11 + 11.12). */
    int regionRuns;
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
    int frameRecords;
    /** Presenting a target to a window, a display or a framebuffer: one frame into one destination
     *  (11.13). The destination is the caller's; see @ref RenderPresent. */
    int presentation;
    /** Compositing more than one surface. **0**: no layer tree exists yet. */
    int compositor;
    /** Rasterising a target in independently addressable pieces. **0**: see the header comment. */
    int tiles;
    /** A history of what was on the screen, so "unchanged" needs no comparison. **0**. */
    int damageTracking;
    /** A clock, a deadline and a queue. **0**: and 11.12's determinism depends on its absence. */
    int frameScheduling;
    /** A GPU backend. **0**: a module, not a branch. */
    int gpu;
} RenderCapabilities;

/** Zero @p capabilities (every feature reported absent). Safe on NULL. */
void RenderCapabilitiesInit(RenderCapabilities *capabilities);

/** Fill @p out with this build's capabilities.
 *  @return RoboOk; RoboInvalidArgument for NULL @p out. */
Result RenderGetCapabilities(RenderCapabilities *out);

/** The version this build was compiled as ("0.1.0"); never NULL. */
const char *RenderVersionString(void);

/** The version this build was compiled as, by field.
 *  @return RoboOk; RoboInvalidArgument for any NULL @p out argument. */
Result RenderGetVersion(I32 *outMajor,
                                     I32 *outMinor,
                                     I32 *outPatch);

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
 * that a frame is on a *screen*: it means the pixels exist in a buffer this module owns. 11.13 added
 * the way out (`RenderPresent`, one header over, and `DesktopPresent` above it, which is
 * the caller that has a screen), but the two questions stay separate on purpose -- `presentation`
 * beside this door in the table above asks whether a frame can *arrive somewhere*, and this one asks
 * whether the pixels exist at all.
 */
int RenderRasterizationIsImplemented(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_RENDER_H */
