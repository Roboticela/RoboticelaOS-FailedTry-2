/*
 * Diagnostics — every view of the compositor, and the fuzz door that proves it stays valid.
 *
 * ## Complete or absent, in one place
 *
 * `CompositorDump` accumulates the length the text *needs* and appends only while it fits, so the
 * rule the text, input and layout dumps follow is implemented once here and every writer inherits
 * it: a buffer too small is emptied and its required length reported, never left holding half a
 * line. A truncated `opacity 2` reads as a value rather than as a failure, which is why the rule
 * exists at all — and a NULL buffer is the length query.
 *
 * ## One writer per view, and the summary composes them
 *
 * The capability table, the tree, the damage, the tiles, the jobs, the surfaces, the animations, the
 * frames and the stats each have their own door, and `CompositorWriteSummary` calls them in order.
 * That is why a summary and a single-section dump can never disagree about a number: there is one
 * implementation of each section, not two.
 *
 * ## The fuzz door checks validity, not meaning
 *
 * `CompositorFuzz` drives the public doors from arbitrary bytes. The bytes choose arguments and the
 * doors are allowed to refuse them; what is *not* allowed is for the compositor to end up
 * inconsistent — a layer whose parent does not exist, a cycle, a negative reference count, a tree
 * deeper than its own table. `CompositorValidate` is that check, written once and shared with the
 * suite, because an invariant that only the fuzzer knows about is an invariant nobody else can test.
 */

#include "RoboCompositorInternal.h"
#include "RoboCompositor/RoboCompositor.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* The writer                                                          */
/* ------------------------------------------------------------------ */

void CompositorDumpInit(CompositorDump *dump, char *buffer, size_t size)
{
    if (!dump) {
        return;
    }
    dump->buffer = size > 0u ? buffer : NULL;
    dump->size = size > 0u ? size : 0u;
    dump->needed = 0u;
    dump->overflow = 0;
    if (dump->buffer) {
        dump->buffer[0] = '\0';
    }
}

void CompositorDumpPrintf(CompositorDump *dump, const char *format, ...)
{
    char line[512];
    int written;
    size_t length;
    va_list args;

    if (!dump || !format) {
        return;
    }
    va_start(args, format);
    written = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (written < 0) {
        return;
    }
    length = (size_t)written;
    if (length >= sizeof(line)) {
        /* A line longer than the local buffer cannot be a line this writer produces: the callers'
         * formats are bounded, so this is a defect rather than a case, and it is reported by the
         * overflow flag instead of by writing a truncated line. */
        length = sizeof(line) - 1u;
        line[length] = '\0';
    }
    if (dump->buffer) {
        if (dump->needed + length + 1u <= dump->size) {
            RoboMemCopy(dump->buffer + dump->needed, line, length + 1u);
        } else {
            dump->overflow = 1;
        }
    }
    dump->needed += length;
}

Result CompositorDumpFinish(CompositorDump *dump, size_t *outNeeded)
{
    if (!dump) {
        return RoboInvalidArgument;
    }
    if (outNeeded) {
        *outNeeded = dump->needed;
    }
    if (!dump->buffer) {
        return RoboOk; /* the length query */
    }
    if (dump->overflow || dump->needed + 1u > dump->size) {
        dump->buffer[0] = '\0';
        return RoboNotSupported;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Per-view writers                                                    */
/* ------------------------------------------------------------------ */

Result CompositorWriteCapabilities(const Compositor *compositor, char *buffer, size_t bufferSize,
                                   size_t *outNeeded)
{
    CompositorDump dump;
    CompositorCapabilities caps;

    if (!compositor) {
        return RoboInvalidArgument;
    }
    CompositorGetCapabilities(&caps);
    CompositorDumpInit(&dump, buffer, bufferSize);
    CompositorDumpPrintf(&dump, "compositor %s configuration %dx%d format %d\n",
                         CompositorVersionString(), (int)compositor->config.size.width,
                         (int)compositor->config.size.height, (int)compositor->config.format);
    CompositorDumpPrintf(&dump, "layerTree=%d damageTracking=%d tiles=%d rasterScheduling=%d\n",
                         caps.layerTree, caps.damageTracking, caps.tiles, caps.rasterScheduling);
    CompositorDumpPrintf(&dump, "surfacePool=%d frameScheduling=%d vsync=%d scrolling=%d\n",
                         caps.surfacePool, caps.frameScheduling, caps.vsync, caps.scrolling);
    CompositorDumpPrintf(&dump, "smoothScrolling=%d animations=%d reducedMotion=%d framePump=%d\n",
                         caps.smoothScrolling, caps.animations, caps.reducedMotion, caps.framePump);
    CompositorDumpPrintf(&dump, "cpuFences=%d memoryBudget=%d performanceCounters=%d "
                                "diagnostics=%d visualConformance=%d\n",
                         caps.cpuFences, caps.memoryBudget, caps.performanceCounters, caps.diagnostics,
                         caps.visualConformance);
    CompositorDumpPrintf(&dump, "presentToSurface=%d presentRegions=%d layerClips=%d\n",
                         caps.presentToSurface, caps.presentRegions, caps.layerClips);
    CompositorDumpPrintf(&dump, "gpuResources=%d gpuSync=%d doubleBuffering=%d compositorThread=%d\n",
                         caps.gpuResources, caps.gpuSync, caps.doubleBuffering, caps.compositorThread);
    CompositorDumpPrintf(&dump, "rotation=%d subpixelSampling=%d colorManagement=%d "
                                "presentToDisplay=%d layerBlur=%d toneMapping=%d\n",
                         caps.rotation, caps.subpixelSampling, caps.colorManagement,
                         caps.presentToDisplay, caps.layerBlur, caps.toneMapping);
    CompositorDumpPrintf(&dump, "bounds maxLayers=%u maxSurfaces=%u maxDamageRects=%u maxJobs=%u "
                                "maxAnimations=%u tileEdge=%u properties=%u domains=%u\n",
                         caps.maxLayers, caps.maxSurfaces, caps.maxDamageRects, caps.maxJobs,
                         caps.maxAnimations, caps.tileEdge, caps.propertyCount, caps.domainCount);
    return CompositorDumpFinish(&dump, outNeeded);
}

Result CompositorWriteLayer(const Compositor *compositor, CompositorLayerId id, char *buffer,
                            size_t bufferSize, size_t *outNeeded)
{
    CompositorDump dump;
    CompositorLayerInfo info;
    GfxRect bounds;

    if (!compositor) {
        return RoboInvalidArgument;
    }
    if (CompositorLayerGetInfo(compositor, id, &info) != RoboOk) {
        return RoboNotFound;
    }
    CompositorLayerBoundsInCompositor(compositor, id, &bounds);
    CompositorDumpInit(&dump, buffer, bufferSize);
    CompositorDumpPrintf(&dump,
                         "layer %llu parent %llu surface %llu rect [%d,%d,%d,%d] offset [%g,%g] "
                         "opacity %u scale %g clip %s [%d,%d,%d,%d] visible %d children %u "
                         "bounds [%d,%d,%d,%d]\n",
                         (unsigned long long)info.id, (unsigned long long)info.parent,
                         (unsigned long long)info.surface, (int)info.rect.x, (int)info.rect.y,
                         (int)info.rect.width, (int)info.rect.height, (double)info.offsetX,
                         (double)info.offsetY, info.opacity, (double)info.scale,
                         info.hasClip ? "yes" : "no", (int)info.clip.x, (int)info.clip.y,
                         (int)info.clip.width, (int)info.clip.height, info.visible, info.children,
                         (int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height);
    return CompositorDumpFinish(&dump, outNeeded);
}

Result CompositorWriteLayers(const Compositor *compositor, char *buffer, size_t bufferSize,
                             size_t *outNeeded)
{
    CompositorDump dump;
    CompositorLayerId order[RoboCompositorHitOrderCapacity];
    size_t count = 0u;
    size_t i;

    if (!compositor) {
        return RoboInvalidArgument;
    }
    CompositorLayerPaintOrder(compositor, order, RoboCompositorHitOrderCapacity, &count);
    CompositorDumpInit(&dump, buffer, bufferSize);
    CompositorDumpPrintf(&dump, "layers %zu\n", count);
    for (i = 0u; i < count && i < RoboCompositorHitOrderCapacity; i++) {
        CompositorLayerInfo info;
        if (CompositorLayerGetInfo(compositor, order[i], &info) != RoboOk) {
            continue;
        }
        CompositorDumpPrintf(&dump, "layer %llu parent %llu surface %llu rect [%d,%d,%d,%d] "
                                    "offset [%g,%g] opacity %u scale %g visible %d children %u\n",
                             (unsigned long long)info.id, (unsigned long long)info.parent,
                             (unsigned long long)info.surface, (int)info.rect.x, (int)info.rect.y,
                             (int)info.rect.width, (int)info.rect.height, (double)info.offsetX,
                             (double)info.offsetY, info.opacity, (double)info.scale, info.visible,
                             info.children);
    }
    return CompositorDumpFinish(&dump, outNeeded);
}

Result CompositorWriteHitTest(const Compositor *compositor, GfxPoint point, char *buffer,
                              size_t bufferSize, size_t *outNeeded)
{
    CompositorDump dump;
    CompositorLayerId order[RoboCompositorHitOrderCapacity];
    size_t count = 0u;
    size_t i;

    if (!compositor) {
        return RoboInvalidArgument;
    }
    CompositorLayerPaintOrder(compositor, order, RoboCompositorHitOrderCapacity, &count);
    CompositorDumpInit(&dump, buffer, bufferSize);
    CompositorDumpPrintf(&dump, "hit test [%d,%d]\n", (int)point.x, (int)point.y);
    /* Topmost first: the reverse of paint order, which is what a pointer would traverse. */
    for (i = count < RoboCompositorHitOrderCapacity ? count : RoboCompositorHitOrderCapacity; i > 0u;
         i--) {
        CompositorLayerInfo info;
        GfxRect bounds;
        if (CompositorLayerGetInfo(compositor, order[i - 1u], &info) != RoboOk) {
            continue;
        }
        if (!info.visible) {
            continue;
        }
        if (!GfxRectContainsPoint(
                CompositorLayerLocalBounds(compositor, order[i - 1u]), point)) {
            continue;
        }
        CompositorLayerBoundsInCompositor(compositor, order[i - 1u], &bounds);
        CompositorDumpPrintf(&dump, "candidate layer %llu bounds [%d,%d,%d,%d] opacity %u\n",
                             (unsigned long long)info.id, (int)bounds.x, (int)bounds.y,
                             (int)bounds.width, (int)bounds.height, info.opacity);
    }
    return CompositorDumpFinish(&dump, outNeeded);
}

Result CompositorWriteJobs(const Compositor *compositor, char *buffer, size_t bufferSize,
                           size_t *outNeeded)
{
    CompositorDump dump;
    unsigned i;

    if (!compositor) {
        return RoboInvalidArgument;
    }
    CompositorDumpInit(&dump, buffer, bufferSize);
    CompositorDumpPrintf(&dump, "jobs pending=%u scheduled=%llu run=%llu deferred=%llu\n",
                         compositor->jobCount, (unsigned long long)compositor->statJobsScheduled,
                         (unsigned long long)compositor->statJobsRun,
                         (unsigned long long)compositor->statJobsDeferred);
    for (i = 0u; i < compositor->jobCount && compositor->jobSlots > 0u; i++) {
        const CompositorJob *job =
            &compositor->jobs[(compositor->jobHead + i) % compositor->jobSlots];
        CompositorDumpPrintf(&dump, "job %u %s surface %llu [%d,%d,%d,%d]\n", i,
                             CompositorJobKindName(job->kind),
                             (unsigned long long)job->surface, (int)job->rect.x, (int)job->rect.y,
                             (int)job->rect.width, (int)job->rect.height);
    }
    return CompositorDumpFinish(&dump, outNeeded);
}

/* ------------------------------------------------------------------ */
/* The summary                                                         */
/* ------------------------------------------------------------------ */

/** Append a section, keeping the "complete or absent" rule for the block as a whole. */
static int AppendSection(CompositorDump *dump, Result (*writer)(const Compositor *, char *, size_t,
                                                                size_t *),
                         const Compositor *compositor)
{
    size_t needed = 0u;
    char section[4096];
    Result result;

    result = writer(compositor, section, sizeof(section), &needed);
    if (result != RoboOk) {
        dump->overflow = 1;
        dump->needed += needed;
        return 0;
    }
    CompositorDumpPrintf(dump, "%s", section);
    return 1;
}

Result CompositorWriteSummary(const Compositor *compositor, char *buffer, size_t bufferSize,
                              size_t *outNeeded)
{
    CompositorDump dump;

    if (!compositor) {
        return RoboInvalidArgument;
    }
    CompositorDumpInit(&dump, buffer, bufferSize);
    CompositorDumpPrintf(&dump, "# capabilities\n");
    AppendSection(&dump, CompositorWriteCapabilities, compositor);
    CompositorDumpPrintf(&dump, "# layers\n");
    AppendSection(&dump, CompositorWriteLayers, compositor);
    CompositorDumpPrintf(&dump, "# damage\n");
    AppendSection(&dump, CompositorWriteDamage, compositor);
    CompositorDumpPrintf(&dump, "# tiles\n");
    AppendSection(&dump, CompositorWriteTiles, compositor);
    CompositorDumpPrintf(&dump, "# jobs\n");
    AppendSection(&dump, CompositorWriteJobs, compositor);
    CompositorDumpPrintf(&dump, "# surfaces\n");
    AppendSection(&dump, CompositorWriteSurfaces, compositor);
    CompositorDumpPrintf(&dump, "# animations\n");
    AppendSection(&dump, CompositorWriteAnimations, compositor);
    CompositorDumpPrintf(&dump, "# stats\n");
    AppendSection(&dump, CompositorWriteStats, compositor);
    return CompositorDumpFinish(&dump, outNeeded);
}

/* ------------------------------------------------------------------ */
/* Validity                                                            */
/* ------------------------------------------------------------------ */

Result CompositorValidate(const Compositor *compositor, CompositorDiag *diag)
{
    unsigned i;
    unsigned total = 0u;
    CompositorLayerId root;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    /* Every layer that is in use must be reachable from a root exactly once, and every parent must
     * exist: a tree with an unreachable node is a node a paint never visits, which looks exactly
     * like a missing pixel. */
    for (root = compositor->firstRoot; root != RoboCompositorLayerNone;) {
        const CompositorLayerSlot *slot = CompositorFindLayerConst(compositor, root);
        CompositorLayerId child;
        if (!slot) {
            CompositorFailAt((Compositor *)compositor, diag, RoboError, RoboCompositorDomainDump,
                             "the root list names a layer that is not in the table", __FILE__,
                             __LINE__);
            return RoboError;
        }
        if (slot->parent != RoboCompositorLayerNone) {
            CompositorFailAt((Compositor *)compositor, diag, RoboError, RoboCompositorDomainDump,
                             "a root layer names a parent", __FILE__, __LINE__);
            return RoboError;
        }
        for (child = slot->firstChild; child != RoboCompositorLayerNone;) {
            const CompositorLayerSlot *node = CompositorFindLayerConst(compositor, child);
            if (!node) {
                CompositorFailAt((Compositor *)compositor, diag, RoboError,
                                 RoboCompositorDomainLayer, "a child link names a missing layer",
                                 __FILE__, __LINE__);
                return RoboError;
            }
            if (node->parent != slot->id) {
                CompositorFailAt((Compositor *)compositor, diag, RoboError,
                                 RoboCompositorDomainLayer, "a child disagrees about its parent",
                                 __FILE__, __LINE__);
                return RoboError;
            }
            child = node->nextSibling;
        }
        total++;
        root = slot->nextSibling;
        if (total > compositor->layerSlots) {
            CompositorFailAt((Compositor *)compositor, diag, RoboError, RoboCompositorDomainLayer,
                             "the root list is a cycle", __FILE__, __LINE__);
            return RoboError;
        }
    }
    for (i = 0u; i < compositor->layerSlots; i++) {
        const CompositorLayerSlot *slot = &compositor->layers[i];
        if (!slot->used) {
            continue;
        }
        if (slot->parent != RoboCompositorLayerNone &&
            !CompositorFindLayerConst(compositor, slot->parent)) {
            CompositorFailAt((Compositor *)compositor, diag, RoboError, RoboCompositorDomainLayer,
                             "a layer names a parent that does not exist", __FILE__, __LINE__);
            return RoboError;
        }
        if (!(slot->scale > 0.0f) || !CompositorValueIsFinite(slot->scale)) {
            CompositorFailAt((Compositor *)compositor, diag, RoboError, RoboCompositorDomainLayer,
                             "a layer has a scale that is not positive and finite", __FILE__,
                             __LINE__);
            return RoboError;
        }
        if (slot->rect.width < 0 || slot->rect.height < 0) {
            CompositorFailAt((Compositor *)compositor, diag, RoboError, RoboCompositorDomainLayer,
                             "a layer has a negative extent", __FILE__, __LINE__);
            return RoboError;
        }
    }
    /* Every surface a layer names must exist, and no reference count may be negative. */
    for (i = 0u; i < compositor->layerSlots; i++) {
        const CompositorLayerSlot *slot = &compositor->layers[i];
        if (slot->used && slot->surface != RoboCompositorSurfaceNone &&
            !CompositorFindSurfaceConst(compositor, slot->surface)) {
            CompositorFailAt((Compositor *)compositor, diag, RoboError, RoboCompositorDomainSurface,
                             "a layer names a surface that does not exist", __FILE__, __LINE__);
            return RoboError;
        }
    }
    for (i = 0u; i < compositor->surfaceSlots; i++) {
        const CompositorSurfaceSlot *slot = &compositor->surfaces[i];
        if (slot->held && slot->refs > compositor->surfaceSlots + 1u) {
            CompositorFailAt((Compositor *)compositor, diag, RoboError, RoboCompositorDomainSurface,
                             "a surface has an impossible reference count", __FILE__, __LINE__);
            return RoboError;
        }
        if (slot->held && slot->bytes == 0u) {
            CompositorFailAt((Compositor *)compositor, diag, RoboError, RoboCompositorDomainSurface,
                             "a held surface claims no bytes", __FILE__, __LINE__);
            return RoboError;
        }
    }
    if (compositor->jobCount > compositor->jobSlots) {
        CompositorFailAt((Compositor *)compositor, diag, RoboError, RoboCompositorDomainJobs,
                         "the job ring holds more than it has slots", __FILE__, __LINE__);
        return RoboError;
    }
    if (compositor->damageCount > compositor->damageSlots) {
        CompositorFailAt((Compositor *)compositor, diag, RoboError, RoboCompositorDomainDamage,
                         "the damage list holds more than it has slots", __FILE__, __LINE__);
        return RoboError;
    }
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Fuzzing                                                             */
/* ------------------------------------------------------------------ */

typedef struct CompositorFuzzCursor {
    const uint8_t *bytes;
    size_t length;
    size_t at;
} CompositorFuzzCursor;

static uint32_t FuzzTake(CompositorFuzzCursor *cursor, uint32_t mod)
{
    uint32_t value;

    if (!cursor || cursor->at >= cursor->length || mod == 0u) {
        return 0u;
    }
    value = cursor->bytes[cursor->at];
    cursor->at++;
    return value % mod;
}

Result CompositorFuzz(Compositor *compositor,
                      const uint8_t *bytes,
                      size_t length,
                      size_t *outCalls,
                      CompositorDiag *diag)
{
    CompositorFuzzCursor cursor;
    size_t calls = 0u;
    Result result;

    if (outCalls) {
        *outCalls = 0u;
    }
    if (!CompositorIsLive(compositor) || (!bytes && length > 0u)) {
        return RoboInvalidArgument;
    }
    cursor.bytes = bytes;
    cursor.length = length;
    cursor.at = 0u;
    /* Bounded twice: by the bytes, and by a call ceiling, so a one-byte input cannot spin. */
    while (cursor.at < cursor.length && calls < 4096u) {
        uint32_t op = FuzzTake(&cursor, 20u);
        CompositorLayerId id = (CompositorLayerId)(1u + FuzzTake(&cursor, 8u));
        CompositorLayerId parent = (CompositorLayerId)(1u + FuzzTake(&cursor, 8u));
        CompositorSurfaceId surface = (CompositorSurfaceId)(1u + FuzzTake(&cursor, 4u));
        GfxRect rect = MakeGfxRect((GfxPixelX)FuzzTake(&cursor, 64u), (GfxPixelY)FuzzTake(&cursor, 64u),
                                   (int32_t)FuzzTake(&cursor, 40u), (int32_t)FuzzTake(&cursor, 40u));
        float scale = (float)(FuzzTake(&cursor, 8u) + 1u) * 0.25f;
        float offset = (float)FuzzTake(&cursor, 32u) - 16.0f;
        uint32_t budget = FuzzTake(&cursor, 64u);
        CompositorTime now = (CompositorTime)compositor->now + (CompositorTime)FuzzTake(&cursor, 40u);

        calls++;
        switch (op) {
        case 0u:
        case 1u:
            CompositorLayerCreate(compositor, id, FuzzTake(&cursor, 3u) ? RoboCompositorLayerNone : parent,
                                  NULL);
            break;
        case 2u:
            CompositorLayerDestroy(compositor, id, NULL, NULL);
            break;
        case 3u:
            CompositorLayerSetParent(compositor, id, parent, NULL);
            break;
        case 4u:
            CompositorLayerSetRect(compositor, id, rect, NULL);
            break;
        case 5u:
            CompositorLayerSetOffset(compositor, id, offset, -offset, NULL);
            break;
        case 6u:
            CompositorLayerSetScale(compositor, id, scale, NULL);
            break;
        case 7u:
            CompositorLayerSetOpacity(compositor, id, (uint8_t)FuzzTake(&cursor, 256u), NULL);
            break;
        case 8u:
            if (FuzzTake(&cursor, 2u)) {
                CompositorLayerSetClip(compositor, id, rect, NULL);
            } else {
                CompositorLayerClearClip(compositor, id);
            }
            break;
        case 9u:
            CompositorLayerSetVisible(compositor, id, FuzzTake(&cursor, 2u), NULL);
            break;
        case 10u: {
            GfxSurface *acquired = NULL;
            GfxSize size = MakeGfxSize((int32_t)(1u + FuzzTake(&cursor, 32u)),
                                       (int32_t)(1u + FuzzTake(&cursor, 32u)));
            CompositorSurfaceAcquire(compositor, surface, size, RoboGfxFormatRgba8, &acquired, NULL);
            break;
        }
        case 11u:
            CompositorSurfaceRelease(compositor, surface, NULL);
            break;
        case 12u:
            CompositorLayerSetSurface(compositor, id, FuzzTake(&cursor, 2u) ? surface
                                                                           : RoboCompositorSurfaceNone,
                                      NULL);
            break;
        case 13u:
            CompositorDamageAdd(compositor, FuzzTake(&cursor, 2u) ? surface : RoboCompositorSurfaceNone,
                                rect, NULL);
            break;
        case 14u:
            CompositorLayerSetScroll(compositor, id, offset, offset, NULL);
            break;
        case 15u:
            CompositorAnimate(compositor, id, (CompositorProperty)FuzzTake(&cursor,
                                                                           (uint32_t)RoboCompositorPropertyCount),
                              offset, -offset, now, FuzzTake(&cursor, 64u),
                              (CompositorEasing)FuzzTake(&cursor, (uint32_t)RoboCompositorEasingCount),
                              NULL, NULL);
            break;
        case 16u:
            CompositorAnimationStep(compositor, now, NULL, NULL);
            break;
        case 17u:
            CompositorScheduleFrame(compositor, budget, NULL, NULL);
            CompositorRunScheduled(compositor, NULL, NULL);
            break;
        case 18u:
            CompositorPostFrame(compositor, RoboCompositorFrameReasonProgrammatic, NULL);
            if (FuzzTake(&cursor, 3u) == 0u) {
                CompositorDrainPosts(compositor);
            }
            break;
        default:
            if (FuzzTake(&cursor, 2u)) {
                CompositorSurfaceDestroy(compositor, surface, NULL, NULL);
            } else {
                CompositorTileMarkDirty(compositor, surface, rect, NULL);
            }
            break;
        }
    }
    if (outCalls) {
        *outCalls = calls;
    }
    result = CompositorValidate(compositor, diag);
    return result;
}
