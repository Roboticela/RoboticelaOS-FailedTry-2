/*
 * RoboDisplay list builder (Phase 11.3).
 *
 * The conversion layer: it consumes a paint list (`RoboPaintList.h`), **proves** it is a
 * traversable document, and produces a display list (`RoboPaintDisplayList.h`). It is where
 * the two questions a rasteriser should not have to ask are answered once:
 *
 *   **what rectangle does a command act on?** -- the box model is resolved here (a
 *   background is the border box, a border side its band, the content boundary the
 *   content box), so a display command carries one resolved rectangle;
 *
 *   **is this sequence a document?** -- the paint list's structural convention is checked
 *   here and becomes an invariant of the display list.
 *
 * The builder is a **stateful object** rather than a function, and the state is real
 * rather than ceremony: the structural check needs a stack of the currently open boxes
 * (their node ids and whether each has passed its content boundary), and a builder that
 * owns that stack can be reused across builds -- `reset` keeps the capacity -- exactly as
 * a paint context is reused across passes. A builder owns **nothing borrowed**: the
 * record list and the output list are the caller's, and destroying a builder frees the
 * builder and its stack and nothing else, so it is safe to destroy in any order relative
 * to everything it was used with.
 *
 * **The builder does not perform layout and does not mutate its input.** It reads the
 * record list through `const` and the paint list's own read doors, and it writes only the
 * output list -- which is cleared first, so a refused build hands back an **empty** list
 * rather than a half-built one, the paint pass's rule at the same boundary.
 */
#ifndef ROBO_PAINT_DISPLAY_LIST_BUILDER_H
#define ROBO_PAINT_DISPLAY_LIST_BUILDER_H

#include "RoboBase/RoboError.h"

#include "RoboPaint/RoboPaintDisplayList.h"
#include "RoboPaint/RoboPaintDiagnostics.h"
#include "RoboPaint/RoboPaintList.h"

#ifdef __cplusplus
extern "C" {
#endif

/** An opaque, caller-owned display list builder. */
typedef struct robo_paint_display_list_builder robo_paint_display_list_builder_t;

/** Create an empty builder.
 *  @return ROBO_OK; ROBO_INVALID_ARGUMENT for NULL @p out_builder; ROBO_OUT_OF_MEMORY. */
robo_result_t robo_paint_display_list_builder_create(robo_paint_display_list_builder_t **out_builder,
                                               robo_paint_diag_t *diag);

/** Destroy @p builder and its open-node stack. The record list and the output list are
 *  not touched. Safe on NULL. */
void robo_paint_display_list_builder_destroy(robo_paint_display_list_builder_t *builder);

/** Return @p builder to its just-created state, keeping the stack's capacity. Safe on
 *  NULL. */
void robo_paint_display_list_builder_reset(robo_paint_display_list_builder_t *builder);

/**
 * Build @p out_list from @p records.
 *
 * Walks @p records in order and, for every operation, appends the display command that
 * resolves it (see `RoboPaintDisplayCommand.h`): a `BEGIN_BOX` becomes a `BEGIN_NODE` over the
 * border box, a `BACKGROUND` a `DRAW_BACKGROUND` over the border box, a `BORDER_*` a
 * `DRAW_BORDER` over its band, a `CONTENT` a `CONTENT` over the content box, and an
 * `END_BOX` an `END_NODE` over the border box. Node, depth, clip and colour carry
 * through unchanged.
 *
 * @p out_list is **cleared first** and appended to as the walk proceeds. The walk also
 * checks the sequence is a document, and **refuses** one that is not, with
 * `ROBO_PAINT_DOMAIN_DISPLAY_LIST` and the reason:
 *
 *   every `BEGIN_BOX` nests directly inside the currently open boxes (its depth is the
 *   open count);
 *   a `CONTENT`/`END_BOX`/drawing operation belongs to the innermost open box (its depth
 *   and node id are that box's);
 *   a box emits its drawing **before** its content boundary and its content boundary
 *   **exactly once**;
 *   and the sequence closes every box it opens.
 *
 * An **empty** record list is a legitimate build that produces an empty display list --
 * the paint pass's own "a document that draws nothing" -- and is not a refusal. On a
 * refusal @p out_list is left empty, so a caller never holds a partially converted
 * sequence.
 *
 * @p out_stats may be NULL; when it is not, it receives what the build consumed and
 * produced.
 *
 * @return ROBO_OK; ROBO_INVALID_ARGUMENT for a NULL @p builder, @p records or @p out_list;
 *         ROBO_ERROR with a diagnostic for a record list that is not a document, or for a
 *         record that names no operation this build resolves.
 */
robo_result_t robo_paint_display_list_builder_build(robo_paint_display_list_builder_t *builder,
                                              const robo_paint_list_t *records,
                                              robo_paint_display_list_t *out_list,
                                              robo_paint_display_list_stats_t *out_stats,
                                              robo_paint_diag_t *diag);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_PAINT_DISPLAY_LIST_BUILDER_H */
