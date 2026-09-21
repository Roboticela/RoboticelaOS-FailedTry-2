#include "RoboGraphics/RoboGraphicsPath.h"

#include "RoboMem/RoboMem.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef INT32_MAX
#define INT32_MAX 2147483647
#endif

#define RoboGfxPathInitialCap 8u

struct GfxPath {
    uint32_t refcount;
    GfxPathId id;
    GfxPathCommand *cmds;
    size_t count;
    size_t capacity;
    int hasCurrent;
    int openContour;
    GfxPoint current;
    GfxPoint contourStart;
};

static uint64_t gPathIdSeq = 1u;

static int EnsureCapacity(GfxPath *path, size_t need)
{
    size_t newCap;
    GfxPathCommand *neu;

    if (need <= path->capacity) {
        return 1;
    }
    newCap = path->capacity ? path->capacity : RoboGfxPathInitialCap;
    while (newCap < need) {
        if (newCap > (SIZE_MAX / 2u)) {
            return 0;
        }
        newCap *= 2u;
    }
    if (newCap > SIZE_MAX / sizeof(GfxPathCommand)) {
        return 0;
    }
    neu = (GfxPathCommand *)Realloc(
        path->cmds, newCap * sizeof(GfxPathCommand));
    if (!neu) {
        return 0;
    }
    path->cmds = neu;
    path->capacity = newCap;
    return 1;
}

static Result AppendCmd(GfxPath *path,
                                GfxPathVerb verb,
                                GfxPoint point)
{
    GfxPathCommand *slot;

    if (!EnsureCapacity(path, path->count + 1u)) {
        return RoboOutOfMemory;
    }
    slot = &path->cmds[path->count];
    slot->verb = verb;
    slot->point = point;
    path->count++;
    return RoboOk;
}

Result GfxPathCreate(GfxPath **out)
{
    GfxPath *path;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    path = (GfxPath *)RoboCalloc(1, sizeof(*path));
    if (!path) {
        return RoboOutOfMemory;
    }
    path->refcount = 1u;
    path->id = gPathIdSeq++;
    if (path->id == RoboGfxPathIdNone) {
        path->id = gPathIdSeq++;
    }
    *out = path;
    return RoboOk;
}

void GfxPathRetain(GfxPath *path)
{
    if (!path) {
        return;
    }
    path->refcount++;
}

void GfxPathRelease(GfxPath *path)
{
    if (!path) {
        return;
    }
    if (path->refcount == 0u) {
        return;
    }
    path->refcount--;
    if (path->refcount != 0u) {
        return;
    }
    RoboFree(path->cmds);
    RoboFree(path);
}

GfxPathId GetGfxPathId(const GfxPath *path)
{
    return path ? path->id : RoboGfxPathIdNone;
}

void GfxPathReset(GfxPath *path)
{
    if (!path) {
        return;
    }
    path->count = 0u;
    path->hasCurrent = 0;
    path->openContour = 0;
    memset(&path->current, 0, sizeof(path->current));
    memset(&path->contourStart, 0, sizeof(path->contourStart));
}

void GfxPathClear(GfxPath *path)
{
    if (!path) {
        return;
    }
    RoboFree(path->cmds);
    path->cmds = NULL;
    path->capacity = 0u;
    GfxPathReset(path);
}

int GfxPathIsEmpty(const GfxPath *path)
{
    return !path || path->count == 0u;
}

size_t GfxPathCommandCount(const GfxPath *path)
{
    return path ? path->count : 0u;
}

int GfxPathHasCurrentPoint(const GfxPath *path)
{
    return path && path->hasCurrent;
}

Result GfxPathCurrentPoint(const GfxPath *path,
                                          GfxPoint *out)
{
    if (!path || !out || !path->hasCurrent) {
        return RoboInvalidArgument;
    }
    *out = path->current;
    return RoboOk;
}

int GfxPathHasOpenContour(const GfxPath *path)
{
    return path && path->openContour;
}

Result GfxPathMoveTo(GfxPath *path, GfxPoint p)
{
    Result res;

    if (!path) {
        return RoboInvalidArgument;
    }
    res = AppendCmd(path, RoboGfxPathMoveTo, p);
    if (res != RoboOk) {
        return res;
    }
    path->contourStart = p;
    path->current = p;
    path->hasCurrent = 1;
    path->openContour = 1;
    return RoboOk;
}

Result GfxPathLineTo(GfxPath *path, GfxPoint p)
{
    Result res;

    if (!path) {
        return RoboInvalidArgument;
    }
    if (!path->openContour) {
        return RoboInvalidArgument;
    }
    res = AppendCmd(path, RoboGfxPathLineTo, p);
    if (res != RoboOk) {
        return res;
    }
    path->current = p;
    path->hasCurrent = 1;
    return RoboOk;
}

Result GfxPathClose(GfxPath *path)
{
    Result res;
    GfxPoint unused;

    if (!path) {
        return RoboInvalidArgument;
    }
    if (!path->openContour) {
        return RoboOk;
    }
    memset(&unused, 0, sizeof(unused));
    res = AppendCmd(path, RoboGfxPathClose, unused);
    if (res != RoboOk) {
        return res;
    }
    /* Contour closed; current returns to contour start. Need MoveTo to continue. */
    path->current = path->contourStart;
    path->hasCurrent = 1;
    path->openContour = 0;
    return RoboOk;
}

Result GfxPathCommandAt(const GfxPath *path,
                                       size_t index,
                                       GfxPathCommand *out)
{
    if (!path || !out || index >= path->count) {
        return RoboInvalidArgument;
    }
    *out = path->cmds[index];
    return RoboOk;
}

void GfxPathIterBegin(const GfxPath *path,
                              GfxPathIter *iter)
{
    if (!iter) {
        return;
    }
    iter->path = path;
    iter->index = 0u;
}

int GfxPathIterNext(GfxPathIter *iter,
                            GfxPathCommand *out)
{
    if (!iter || !out || !iter->path) {
        return 0;
    }
    if (iter->index >= iter->path->count) {
        return 0;
    }
    *out = iter->path->cmds[iter->index];
    iter->index++;
    return 1;
}

Result GfxPathBounds(const GfxPath *path,
                                   GfxRect *outBounds)
{
    size_t i;
    int have = 0;
    int64_t minX = 0;
    int64_t minY = 0;
    int64_t maxX = 0;
    int64_t maxY = 0;

    if (!path || !outBounds) {
        return RoboInvalidArgument;
    }
    *outBounds = GfxRectEmpty();

    for (i = 0u; i < path->count; i++) {
        const GfxPathCommand *c = &path->cmds[i];
        int64_t x;
        int64_t y;

        if (c->verb != RoboGfxPathMoveTo && c->verb != RoboGfxPathLineTo) {
            continue;
        }
        x = (int64_t)c->point.x;
        y = (int64_t)c->point.y;
        if (!have) {
            minX = maxX = x;
            minY = maxY = y;
            have = 1;
        } else {
            if (x < minX) {
                minX = x;
            }
            if (x > maxX) {
                maxX = x;
            }
            if (y < minY) {
                minY = y;
            }
            if (y > maxY) {
                maxY = y;
            }
        }
    }

    if (!have) {
        return RoboOk;
    }

    /*
     * Half-open rect covering integer points [min, max]:
     * width = max - min + 1, height = max - min + 1.
     * Exclusive edges must fit int32 (right = maxX + 1).
     */
    if (maxX >= (int64_t)INT32_MAX || maxY >= (int64_t)INT32_MAX) {
        return RoboInvalidArgument;
    }
    return GfxRectFromLtrb((int32_t)minX, (int32_t)minY,
                                   (int32_t)(maxX + 1),
                                   (int32_t)(maxY + 1), outBounds);
}
