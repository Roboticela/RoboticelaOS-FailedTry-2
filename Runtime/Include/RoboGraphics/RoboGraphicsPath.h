/*
 * RoboGraphics paths foundation (Goal 6.8).
 *
 * Geometry/data-model only: MoveTo / LineTo / Close (+ reserved curve verbs).
 * No fill, stroke, flatten, or rasterization.
 *
 * Ownership: path owns its command storage via RoboMem. retain/release.
 * Threading: not concurrently mutable unless the caller synchronizes.
 *
 * See graphics/docs/Paths.md.
 */

#ifndef ROBO_GRAPHICS_PATH_H
#define ROBO_GRAPHICS_PATH_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Path command verbs. Values are stable for ABI growth.
 * Curve verbs are reserved; construction APIs for them are not in Goal 6.8.
 */
typedef enum GfxPathVerb {
    RoboGfxPathMoveTo = 1,
    RoboGfxPathLineTo = 2,
    RoboGfxPathClose = 3,
    RoboGfxPathQuadTo = 4,  /* reserved — not constructible in 6.8 */
    RoboGfxPathCubicTo = 5  /* reserved — not constructible in 6.8 */
} GfxPathVerb;

typedef uint64_t GfxPathId;
#define RoboGfxPathIdNone ((GfxPathId)0u)

typedef struct GfxPath GfxPath;

/**
 * One recorded command for iteration.
 * MOVE_TO / LINE_TO: @p point is valid.
 * CLOSE: @p point is undefined (ignore).
 */
typedef struct GfxPathCommand {
    GfxPathVerb verb;
    GfxPoint point;
} GfxPathCommand;

typedef struct GfxPathIter {
    const GfxPath *path;
    size_t index;
} GfxPathIter;

/** Create an empty owned path. OOM → RoboOutOfMemory. */
Result GfxPathCreate(GfxPath **out);

void GfxPathRetain(GfxPath *path);
void GfxPathRelease(GfxPath *path);

GfxPathId GetGfxPathId(const GfxPath *path);

/** Remove all commands; keep allocated capacity. */
void GfxPathReset(GfxPath *path);

/** Remove all commands and free command storage capacity. */
void GfxPathClear(GfxPath *path);

int GfxPathIsEmpty(const GfxPath *path);
size_t GfxPathCommandCount(const GfxPath *path);

/** 1 if a current point exists (open or just-closed contour). */
int GfxPathHasCurrentPoint(const GfxPath *path);
Result GfxPathCurrentPoint(const GfxPath *path,
                                          GfxPoint *out);

/** 1 if the latest subpath is open (accepts LineTo / Close). */
int GfxPathHasOpenContour(const GfxPath *path);

/**
 * Begin a new subpath at @p p. Always succeeds for a valid path.
 * Ends any open contour without inserting Close.
 */
Result GfxPathMoveTo(GfxPath *path, GfxPoint p);

/**
 * Append a line to @p p.
 * Requires an open contour (after MoveTo / LineTo). Otherwise →
 * RoboInvalidArgument. After Close, call MoveTo before LineTo.
 */
Result GfxPathLineTo(GfxPath *path, GfxPoint p);

/**
 * Close the current open contour (append Close verb).
 * No open contour → RoboOk no-op (no verb appended).
 * Close after MoveTo alone is valid (records Close).
 * After Close, LineTo is invalid until the next MoveTo.
 */
Result GfxPathClose(GfxPath *path);

/**
 * Read command at @p index (0 .. count-1).
 * Out of range → RoboInvalidArgument.
 */
Result GfxPathCommandAt(const GfxPath *path,
                                       size_t index,
                                       GfxPathCommand *out);

void GfxPathIterBegin(const GfxPath *path,
                              GfxPathIter *iter);

/**
 * Advance iterator. Returns 1 and fills @p out when a command remains;
 * returns 0 at end. NULL args → 0.
 */
int GfxPathIterNext(GfxPathIter *iter,
                            GfxPathCommand *out);

/**
 * Axis-aligned bounds of all MoveTo/LineTo points (half-open Rect).
 * Empty path or MoveTo-only with no points stored oddly → empty rect, RoboOk.
 * Close does not add points; start/end already included via MoveTo/LineTo.
 * Overflow when forming width/height → RoboInvalidArgument.
 */
Result GfxPathBounds(const GfxPath *path,
                                   GfxRect *outBounds);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_PATH_H */
