/*
 * RoboCompositor's core: construction, configuration, the clock, the counters, and the helpers the
 * module's other files read.
 *
 * Four decisions live here rather than in the files that use them.
 *
 *   **The tables are allocated once**, at construction, from bounds the configuration states, so no
 *   frame-time door allocates anything (15.17's first requirement, and the reason a frame's cost is
 *   measurable at all). A compositor past a bound refuses with `RoboOutOfMemory` and counts the
 *   refusal; it never grows, and it never drops work silently.
 *
 *   **There is one clock, and it is the caller's.** `CompositorAdvanceTime` is the only way time
 *   moves, it refuses to go backwards, and every deadline in the module (vsync, animations, a
 *   frame's timestamp) is a function of that number. A test therefore drives frames with integers
 *   rather than with sleeps, which is why a timing assertion here is reproducible.
 *
 *   **A refusal is recorded even when the caller did not ask.** Every failing door may be passed a
 *   NULL `CompositorDiag`; `CompositorFailAt` still fills the compositor's last-diag record and its
 *   per-domain counter. A failure path that forgot to record itself would be invisible in a dump,
 *   and a dump is what a person reads when a frame stops happening.
 *
 *   **The helpers are shared, not copy-pasted.** `CompositorFindLayer` and `CompositorFindSurface`
 *   are the only id lookups in the module: a door that resolved an id itself would be a second
 *   answer to "is this stale id still mine", and a stale id must be refused rather than resolve to a
 *   slot some later layer took.
 */

#include "RoboCompositorInternal.h"

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

const char *CompositorDomainName(CompositorDiagnosticDomain domain)
{
    switch (domain) {
    case RoboCompositorDomainCore: return "core";
    case RoboCompositorDomainLayer: return "layer";
    case RoboCompositorDomainCompose: return "compose";
    case RoboCompositorDomainDamage: return "damage";
    case RoboCompositorDomainTiles: return "tiles";
    case RoboCompositorDomainJobs: return "jobs";
    case RoboCompositorDomainGpu: return "gpu";
    case RoboCompositorDomainSurface: return "surface";
    case RoboCompositorDomainFrame: return "frame";
    case RoboCompositorDomainVsync: return "vsync";
    case RoboCompositorDomainScroll: return "scroll";
    case RoboCompositorDomainAnimation: return "animation";
    case RoboCompositorDomainThread: return "thread";
    case RoboCompositorDomainFence: return "fence";
    case RoboCompositorDomainMemory: return "memory";
    case RoboCompositorDomainDump: return "dump";
    default: return "?";
    }
}

const char *CompositorTileStateName(CompositorTileState state)
{
    switch (state) {
    case RoboCompositorTileEmpty: return "empty";
    case RoboCompositorTileClean: return "clean";
    case RoboCompositorTileDirty: return "dirty";
    default: return "?";
    }
}

const char *CompositorResourceKindName(CompositorResourceKind kind)
{
    switch (kind) {
    case RoboCompositorResourceSurface: return "surface";
    case RoboCompositorResourceTile: return "tile";
    case RoboCompositorResourceGpuTexture: return "gpu-texture";
    case RoboCompositorResourceGpuBuffer: return "gpu-buffer";
    default: return "?";
    }
}

const char *CompositorPropertyName(CompositorProperty property)
{
    switch (property) {
    case RoboCompositorPropertyOpacity: return "opacity";
    case RoboCompositorPropertyOffsetX: return "offset-x";
    case RoboCompositorPropertyOffsetY: return "offset-y";
    case RoboCompositorPropertyScale: return "scale";
    case RoboCompositorPropertyScrollX: return "scroll-x";
    case RoboCompositorPropertyScrollY: return "scroll-y";
    default: return "?";
    }
}

const char *CompositorEasingName(CompositorEasing easing)
{
    switch (easing) {
    case RoboCompositorEasingLinear: return "linear";
    case RoboCompositorEasingEaseOut: return "ease-out";
    case RoboCompositorEasingEaseInOut: return "ease-in-out";
    default: return "?";
    }
}

const char *CompositorFrameReasonName(CompositorFrameReason reason)
{
    switch (reason) {
    case RoboCompositorFrameReasonDamage: return "damage";
    case RoboCompositorFrameReasonLayerChange: return "layer-change";
    case RoboCompositorFrameReasonAnimation: return "animation";
    case RoboCompositorFrameReasonScroll: return "scroll";
    case RoboCompositorFrameReasonVsync: return "vsync";
    case RoboCompositorFrameReasonResize: return "resize";
    case RoboCompositorFrameReasonProgrammatic: return "programmatic";
    default: return "?";
    }
}

const char *CompositorPresentKindName(CompositorPresentKind kind)
{
    switch (kind) {
    case RoboCompositorPresentFull: return "full";
    case RoboCompositorPresentDamage: return "damage";
    default: return "?";
    }
}

const char *CompositorJobKindName(CompositorJobKind kind)
{
    switch (kind) {
    case RoboCompositorJobRegion: return "region";
    case RoboCompositorJobTile: return "tile";
    default: return "?";
    }
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

void CompositorDiagReset(CompositorDiag *diag)
{
    if (!diag) {
        return;
    }
    diag->code = RoboOk;
    diag->domain = RoboCompositorDomainCore;
    diag->message = "";
    diag->file = NULL;
    diag->line = 0;
}

Result CompositorDiagSet(CompositorDiag *diag,
                         Result code,
                         CompositorDiagnosticDomain domain,
                         const char *message,
                         const char *file,
                         int line)
{
    if (!diag) {
        return RoboInvalidArgument;
    }
    if (code == RoboOk) {
        CompositorDiagReset(diag);
        return RoboOk;
    }
    diag->code = code;
    diag->domain = domain;
    diag->message = message ? message : "";
    diag->file = file;
    diag->line = line;
    return code;
}

void CompositorFailAt(Compositor *compositor,
                      CompositorDiag *diag,
                      Result code,
                      CompositorDiagnosticDomain domain,
                      const char *message,
                      const char *file,
                      int line)
{
    if (diag) {
        CompositorDiagSet(diag, code, domain, message, file, line);
    }
    if (compositor) {
        compositor->statRefusals++;
        if ((unsigned)domain < (unsigned)RoboCompositorDomainCount) {
            compositor->statRefusalsByDomain[domain]++;
        }
        compositor->lastDiag = (CompositorDiag){code, domain, message ? message : "", file, line};
        compositor->hasLastDiag = 1;
    }
}

void CompositorNoteRefusal(Compositor *compositor,
                           CompositorDiagnosticDomain domain,
                           const char *message)
{
    CompositorFailAt(compositor, NULL, RoboError, domain, message, NULL, 0);
}

Result CompositorGetLastDiag(const Compositor *compositor, CompositorDiag *out)
{
    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    if (!compositor->hasLastDiag) {
        CompositorDiagReset(out);
        return RoboNotFound;
    }
    *out = compositor->lastDiag;
    return out->code;
}

Result CompositorGetRefusalCounts(const Compositor *compositor, uint64_t *outCounts, size_t count)
{
    size_t i;

    if (!compositor || !outCounts) {
        return RoboInvalidArgument;
    }
    for (i = 0u; i < count; i++) {
        outCounts[i] = (i < (size_t)RoboCompositorDomainCount)
                           ? compositor->statRefusalsByDomain[i]
                           : 0u;
    }
    return RoboOk;
}

Result CompositorDiagCode(const CompositorDiag *diag)
{
    return diag ? diag->code : RoboOk;
}

/* ------------------------------------------------------------------ */
/* Capabilities and the configuration                                  */
/* ------------------------------------------------------------------ */

const char *CompositorVersionString(void)
{
    return "1.0.0";
}

Result CompositorGetCapabilities(CompositorCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    *out = (CompositorCapabilities){
        /* present */
        .layerTree = 1,
        .damageTracking = 1,
        .tiles = 1,
        .rasterScheduling = 1,
        .surfacePool = 1,
        .frameScheduling = 1,
        .vsync = 1,
        .scrolling = 1,
        .smoothScrolling = 1,
        .animations = 1,
        .reducedMotion = 1,
        .framePump = 1,
        .cpuFences = 1,
        .memoryBudget = 1,
        .performanceCounters = 1,
        .diagnostics = 1,
        .visualConformance = 1,
        .presentToSurface = 1,
        .presentRegions = 1,
        .layerClips = 1,
        /* deliberately absent: see `RoboCompositorVersion.h` */
        .gpuResources = 0,
        .gpuSync = 0,
        .doubleBuffering = 0,
        .compositorThread = 0,
        .rotation = 0,
        .subpixelSampling = 0,
        .colorManagement = 0,
        .presentToDisplay = 0,
        .layerBlur = 0,
        .toneMapping = 0,
        /* bounds */
        .maxLayers = 256u,
        .maxSurfaces = 64u,
        .maxDamageRects = 32u,
        .maxJobs = 256u,
        .maxAnimations = 64u,
        .tileEdge = 64u,
        .propertyCount = (unsigned)RoboCompositorPropertyCount,
        .domainCount = (unsigned)RoboCompositorDomainCount,
        .damageOverflowToFull = 1
    };
    return RoboOk;
}

void CompositorConfigInit(CompositorConfig *config)
{
    CompositorCapabilities caps;

    if (!config) {
        return;
    }
    CompositorGetCapabilities(&caps);
    *config = (CompositorConfig){
        .size = MakeGfxSize(1024, 768),
        .format = RoboGfxFormatRgba8,
        .maxLayers = caps.maxLayers,
        .maxSurfaces = caps.maxSurfaces,
        .maxDamageRects = caps.maxDamageRects,
        .maxJobs = caps.maxJobs,
        .maxAnimations = caps.maxAnimations,
        .tileEdge = caps.tileEdge,
        .memoryBudgetBytes = 0u,
        .vsyncIntervalMs = 0u,
        .reducedMotion = 0,
        .damagePresent = 0
    };
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

/** Bytes per pixel for a format this build can hold, or 0. */
static uint8_t FormatBytesPerPixel(GfxPixelFormat format)
{
    switch (format) {
    case RoboGfxFormatRgba8:
    case RoboGfxFormatBgra8:
        return 4u;
    case RoboGfxFormatRgb8:
        return 3u;
    case RoboGfxFormatGray8:
    case RoboGfxFormatAlpha8:
        return 1u;
    default:
        return 0u;
    }
}

size_t CompositorSurfaceByteSize(GfxSize size, GfxPixelFormat format)
{
    uint8_t bpp = FormatBytesPerPixel(format);
    uint64_t pixels;

    if (bpp == 0u || !GfxSizeIsValid(size) || GfxSizeIsEmpty(size)) {
        return 0u;
    }
    pixels = (uint64_t)(uint32_t)size.width * (uint64_t)(uint32_t)size.height;
    if (pixels > (uint64_t)(SIZE_MAX / bpp)) {
        return 0u;
    }
    return (size_t)(pixels * bpp);
}

Result CompositorCreate(const CompositorConfig *config, Compositor **out, CompositorDiag *diag)
{
    CompositorConfig defaults;
    CompositorCapabilities caps;
    Compositor *compositor;

    if (!out) {
        return RoboInvalidArgument;
    }
    *out = NULL;
    CompositorGetCapabilities(&caps);
    CompositorConfigInit(&defaults);
    if (!config) {
        config = &defaults;
    }
    if (!GfxSizeIsValid(config->size) || GfxSizeIsEmpty(config->size)) {
        return CompositorDiagSet(diag, RoboInvalidArgument, RoboCompositorDomainCore,
                                 "the composition's size must have two positive dimensions",
                                 __FILE__, __LINE__);
    }
    compositor = (Compositor *)RoboCalloc(1u, sizeof(Compositor));
    if (!compositor) {
        return CompositorDiagSet(diag, RoboOutOfMemory, RoboCompositorDomainCore,
                                 "the compositor could not be held", __FILE__, __LINE__);
    }
    compositor->config = *config;
    if (compositor->config.format == RoboGfxFormatUnknown) {
        compositor->config.format = RoboGfxFormatRgba8;
    }
    if (FormatBytesPerPixel(compositor->config.format) == 0u) {
        CompositorFailAt(NULL, diag, RoboNotSupported, RoboCompositorDomainSurface,
                         "that pixel format has no CPU layout in this build", __FILE__, __LINE__);
        RoboFree(compositor);
        return RoboNotSupported;
    }
    /* Bounds: a 0 in the configuration means "the build's own", so a caller can state one field. */
    compositor->layerSlots = config->maxLayers ? config->maxLayers : caps.maxLayers;
    compositor->surfaceSlots = config->maxSurfaces ? config->maxSurfaces : caps.maxSurfaces;
    compositor->damageSlots = config->maxDamageRects ? config->maxDamageRects : caps.maxDamageRects;
    compositor->jobSlots = config->maxJobs ? config->maxJobs : caps.maxJobs;
    compositor->animationSlots = config->maxAnimations ? config->maxAnimations : caps.maxAnimations;
    compositor->gridSlots = compositor->surfaceSlots;
    compositor->config.tileEdge = config->tileEdge ? config->tileEdge : caps.tileEdge;

    compositor->layers = (CompositorLayerSlot *)RoboCalloc(compositor->layerSlots,
                                                           sizeof(CompositorLayerSlot));
    compositor->surfaces = (CompositorSurfaceSlot *)RoboCalloc(compositor->surfaceSlots,
                                                               sizeof(CompositorSurfaceSlot));
    compositor->damage = (CompositorDamageEntry *)RoboCalloc(compositor->damageSlots,
                                                             sizeof(CompositorDamageEntry));
    compositor->jobs = (CompositorJob *)RoboCalloc(compositor->jobSlots, sizeof(CompositorJob));
    compositor->animations = (CompositorAnimationSlot *)RoboCalloc(compositor->animationSlots,
                                                                   sizeof(CompositorAnimationSlot));
    compositor->grids = (CompositorTileGrid *)RoboCalloc(compositor->gridSlots,
                                                         sizeof(CompositorTileGrid));
    if (!compositor->layers || !compositor->surfaces || !compositor->damage || !compositor->jobs ||
        !compositor->animations || !compositor->grids) {
        CompositorDestroy(compositor);
        return CompositorDiagSet(diag, RoboOutOfMemory, RoboCompositorDomainCore,
                                 "a compositor table could not be held", __FILE__, __LINE__);
    }
    compositor->magic = RoboCompositorMagic;
    compositor->bytesBudget = config->memoryBudgetBytes;
    compositor->vsyncInterval = config->vsyncIntervalMs;
    compositor->reducedMotion = config->reducedMotion;
    compositor->now = 0;
    compositor->nextVsync = 0;
    compositor->vsyncArmed = 0;
    compositor->nextAnimationId = 1u;
    /* Policy allocations start well above the numbers a caller names by hand, so the two id spaces
     * do not collide in a scene that uses both. */
    compositor->nextResourceId = 0x2000u;
    CompositorFrameInit(&compositor->lastFrame);
    CompositorDiagReset(&compositor->lastDiag);
    *out = compositor;
    if (diag) {
        CompositorDiagReset(diag);
    }
    return RoboOk;
}

void CompositorDestroy(Compositor *compositor)
{
    unsigned i;

    if (!compositor) {
        return;
    }
    compositor->magic = 0u;
    for (i = 0u; i < compositor->surfaceSlots; i++) {
        if (compositor->surfaces[i].surface) {
            GfxSurfaceRelease(compositor->surfaces[i].surface);
            compositor->surfaces[i].surface = NULL;
        }
    }
    for (i = 0u; i < compositor->gridSlots; i++) {
        CompositorGridRelease(&compositor->grids[i]);
    }
    RoboFree(compositor->layers);
    RoboFree(compositor->surfaces);
    RoboFree(compositor->damage);
    RoboFree(compositor->jobs);
    RoboFree(compositor->animations);
    RoboFree(compositor->grids);
    RoboFree(compositor);
}

const CompositorConfig *CompositorGetConfig(const Compositor *compositor)
{
    return compositor ? &compositor->config : NULL;
}

GfxSize CompositorSize(const Compositor *compositor)
{
    return compositor ? compositor->config.size : MakeGfxSize(0, 0);
}

int CompositorIsLive(const Compositor *compositor)
{
    return (compositor && compositor->magic == RoboCompositorMagic) ? 1 : 0;
}

Result CompositorResize(Compositor *compositor, GfxSize size, CompositorDiag *diag)
{
    unsigned i;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (!GfxSizeIsValid(size) || GfxSizeIsEmpty(size)) {
        CompositorFailAt(compositor, diag, RoboInvalidArgument, RoboCompositorDomainCore,
                         "a composition's size must have two positive dimensions", __FILE__, __LINE__);
        return RoboInvalidArgument;
    }
    if (size.width == compositor->config.size.width && size.height == compositor->config.size.height) {
        return RoboOk;
    }
    compositor->config.size = size;
    /* Every grid's tile index means a different rectangle after a resize, and everything that was
     * on screen is stale, so the grids go and the damage becomes "all of it". */
    for (i = 0u; i < compositor->gridSlots; i++) {
        CompositorGridRelease(&compositor->grids[i]);
    }
    compositor->gridCount = 0u;
    CompositorDamageClear(compositor);
    compositor->damageFull = 1;
    CompositorPostFrame(compositor, RoboCompositorFrameReasonResize, NULL);
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* The clock                                                           */
/* ------------------------------------------------------------------ */

CompositorTime CompositorTimeNow(const Compositor *compositor)
{
    return compositor ? compositor->now : 0;
}

Result CompositorAdvanceTime(Compositor *compositor, CompositorTime now, unsigned *outTicks,
                             CompositorDiag *diag)
{
    unsigned ticks = 0u;

    if (!CompositorIsLive(compositor)) {
        return RoboInvalidArgument;
    }
    if (outTicks) {
        *outTicks = 0u;
    }
    if (now < compositor->now) {
        CompositorFailAt(compositor, diag, RoboInvalidArgument, RoboCompositorDomainVsync,
                         "the compositor's clock only moves forward", __FILE__, __LINE__);
        return RoboInvalidArgument;
    }
    if (compositor->vsyncInterval && compositor->vsyncArmed && now >= compositor->nextVsync) {
        CompositorTime elapsed = now - compositor->nextVsync;
        ticks = (unsigned)(elapsed / (CompositorTime)compositor->vsyncInterval) + 1u;
        compositor->nextVsync += (CompositorTime)ticks * (CompositorTime)compositor->vsyncInterval;
        compositor->statVsyncTicks += ticks;
    }
    compositor->now = now;
    if (outTicks) {
        *outTicks = ticks;
    }
    return RoboOk;
}

uint32_t CompositorSetVsync(Compositor *compositor, uint32_t intervalMs)
{
    if (!CompositorIsLive(compositor)) {
        return 0u;
    }
    compositor->vsyncInterval = intervalMs;
    /* The deadline is armed by the next present, so changing the interval makes the next frame due
     * immediately rather than waiting for a boundary that belonged to the old interval. */
    compositor->vsyncArmed = 0;
    compositor->nextVsync = compositor->now;
    return compositor->vsyncInterval;
}

uint32_t CompositorVsyncInterval(const Compositor *compositor)
{
    return compositor ? compositor->vsyncInterval : 0u;
}

int CompositorFrameIsDue(const Compositor *compositor)
{
    if (!compositor) {
        return 0;
    }
    if (!compositor->vsyncInterval || !compositor->vsyncArmed) {
        return 1;
    }
    return compositor->now >= compositor->nextVsync ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Counters                                                            */
/* ------------------------------------------------------------------ */

void CompositorStatsInit(CompositorStats *stats)
{
    if (stats) {
        MemSet(stats, 0, sizeof(*stats));
    }
}

Result CompositorGetStats(const Compositor *compositor, CompositorStats *out)
{
    if (!compositor || !out) {
        return RoboInvalidArgument;
    }
    MemSet(out, 0, sizeof(*out));
    out->frames = compositor->statFrames;
    out->framesCoalesced = compositor->statFramesCoalesced;
    out->framesLate = compositor->statFramesLate;
    out->presents = compositor->statPresents;
    out->presetsFull = compositor->statPresentsFull;
    out->presentsDamage = compositor->statPresentsDamage;
    out->damageAdded = compositor->statDamageAdded;
    out->damagePixels = compositor->statDamagePixels;
    out->pixelsComposited = compositor->statPixelsComposited;
    out->bytesPresented = compositor->statBytesPresented;
    out->compositedPixelsPeak = compositor->statCompositedPeak;
    out->jobsScheduled = compositor->statJobsScheduled;
    out->jobsRun = compositor->statJobsRun;
    out->jobsDeferred = compositor->statJobsDeferred;
    out->layersPainted = compositor->statLayersPainted;
    out->posts = compositor->statPosts;
    out->postsDrained = compositor->statPostsDrained;
    out->postsCoalesced = compositor->statPostsCoalesced;
    out->vsyncTicks = compositor->statVsyncTicks;
    out->fencesWaited = compositor->statFencesWaited;
    out->refusals = compositor->statRefusals;
    RoboMemCopy(out->refusalsByDomain, compositor->statRefusalsByDomain,
                sizeof(out->refusalsByDomain));
    out->layers = compositor->layerCount;
    out->surfaces = compositor->surfaceCount;
    out->damageRects = compositor->damageCount;
    out->pendingJobs = compositor->jobCount;
    out->activeAnimations = CompositorActiveAnimationCount(compositor);
    return RoboOk;
}

void CompositorResetStats(Compositor *compositor)
{
    unsigned i;

    if (!CompositorIsLive(compositor)) {
        return;
    }
    compositor->statFrames = 0u;
    compositor->statFramesCoalesced = 0u;
    compositor->statFramesLate = 0u;
    compositor->statPresents = 0u;
    compositor->statPresentsFull = 0u;
    compositor->statPresentsDamage = 0u;
    compositor->statDamageAdded = 0u;
    compositor->statDamagePixels = 0u;
    compositor->statPixelsComposited = 0u;
    compositor->statBytesPresented = 0u;
    compositor->statCompositedPeak = 0u;
    compositor->statJobsScheduled = 0u;
    compositor->statJobsRun = 0u;
    compositor->statJobsDeferred = 0u;
    compositor->statLayersPainted = 0u;
    compositor->statPosts = 0u;
    compositor->statPostsDrained = 0u;
    compositor->statPostsCoalesced = 0u;
    compositor->statVsyncTicks = 0u;
    compositor->statFencesWaited = 0u;
    compositor->statRefusals = 0u;
    compositor->statDamageMerges = 0u;
    compositor->statDamageOverflows = 0u;
    compositor->statDamageClippedAway = 0u;
    compositor->statRasters = 0u;
    compositor->statRasterRefusals = 0u;
    compositor->statRasterPixels = 0u;
    compositor->statAnimationsStarted = 0u;
    compositor->statAnimationsCompleted = 0u;
    compositor->statAnimationsCancelled = 0u;
    compositor->statAnimationsReplaced = 0u;
    compositor->statAnimationsSnapped = 0u;
    compositor->statAnimationSteps = 0u;
    compositor->statSmoothScrolls = 0u;
    compositor->statSurfacesCreated = 0u;
    compositor->statSurfacesDestroyed = 0u;
    compositor->statSurfaceAcquires = 0u;
    compositor->statSurfaceReleases = 0u;
    compositor->statEvictions = 0u;
    compositor->statRefusalsOom = 0u;
    compositor->statRefusalsBudget = 0u;
    compositor->statGpuRefusals = 0u;
    compositor->statLayersDetached = 0u;
    for (i = 0u; i < (unsigned)RoboCompositorDomainCount; i++) {
        compositor->statRefusalsByDomain[i] = 0u;
    }
}

/* ------------------------------------------------------------------ */
/* Shared helpers                                                      */
/* ------------------------------------------------------------------ */

CompositorSurfaceSlot *CompositorFindSurface(Compositor *compositor, CompositorSurfaceId id)
{
    unsigned i;

    if (!compositor || id == RoboCompositorSurfaceNone) {
        return NULL;
    }
    for (i = 0u; i < compositor->surfaceSlots; i++) {
        if (compositor->surfaces[i].held && compositor->surfaces[i].id == id) {
            return &compositor->surfaces[i];
        }
    }
    return NULL;
}

const CompositorSurfaceSlot *CompositorFindSurfaceConst(const Compositor *compositor,
                                                        CompositorSurfaceId id)
{
    return CompositorFindSurface((Compositor *)compositor, id);
}

CompositorLayerSlot *CompositorFindLayer(Compositor *compositor, CompositorLayerId id)
{
    unsigned i;

    if (!compositor || id == RoboCompositorLayerNone) {
        return NULL;
    }
    for (i = 0u; i < compositor->layerSlots; i++) {
        if (compositor->layers[i].used && compositor->layers[i].id == id) {
            return &compositor->layers[i];
        }
    }
    return NULL;
}

const CompositorLayerSlot *CompositorFindLayerConst(const Compositor *compositor, CompositorLayerId id)
{
    return CompositorFindLayer((Compositor *)compositor, id);
}

CompositorTileGrid *CompositorFindGrid(Compositor *compositor,
                                       CompositorSurfaceId surface,
                                       int create)
{
    unsigned i;
    unsigned freeSlot = compositor ? compositor->gridSlots : 0u;
    const CompositorSurfaceSlot *slot;
    unsigned columns;
    unsigned rows;
    size_t tiles;
    uint8_t *states;

    if (!compositor || surface == RoboCompositorSurfaceNone) {
        return NULL;
    }
    for (i = 0u; i < compositor->gridSlots; i++) {
        if (compositor->grids[i].used && compositor->grids[i].surface == surface) {
            return &compositor->grids[i];
        }
        if (!compositor->grids[i].used && freeSlot == compositor->gridSlots) {
            freeSlot = i;
        }
    }
    if (!create) {
        return NULL;
    }
    slot = CompositorFindSurfaceConst(compositor, surface);
    if (!slot) {
        return NULL;
    }
    if (freeSlot == compositor->gridSlots) {
        return NULL; /* every grid slot is taken: the pool bound is the grid bound */
    }
    columns = ((unsigned)slot->size.width + compositor->config.tileEdge - 1u) /
              compositor->config.tileEdge;
    rows = ((unsigned)slot->size.height + compositor->config.tileEdge - 1u) /
           compositor->config.tileEdge;
    if (columns == 0u) {
        columns = 1u;
    }
    if (rows == 0u) {
        rows = 1u;
    }
    tiles = (size_t)columns * (size_t)rows;
    states = (uint8_t *)RoboCalloc(tiles, 1u);
    if (!states) {
        return NULL;
    }
    compositor->grids[freeSlot].surface = surface;
    compositor->grids[freeSlot].columns = columns;
    compositor->grids[freeSlot].rows = rows;
    compositor->grids[freeSlot].states = states;
    compositor->grids[freeSlot].used = 1;
    compositor->gridCount++;
    return &compositor->grids[freeSlot];
}

void CompositorGridRelease(CompositorTileGrid *grid)
{
    if (!grid) {
        return;
    }
    RoboFree(grid->states);
    grid->states = NULL;
    grid->columns = 0u;
    grid->rows = 0u;
    grid->surface = RoboCompositorSurfaceNone;
    grid->used = 0;
}

void CompositorGridInvalidate(Compositor *compositor, CompositorSurfaceId surface)
{
    CompositorTileGrid *grid;

    if (!compositor) {
        return;
    }
    grid = CompositorFindGrid(compositor, surface, 0);
    if (!grid) {
        return;
    }
    CompositorGridRelease(grid);
    if (compositor->gridCount > 0u) {
        compositor->gridCount--;
    }
}

GfxRect CompositorSurfaceBounds(const CompositorSurfaceSlot *slot)
{
    if (!slot) {
        return GfxRectEmpty();
    }
    return MakeGfxRect(0, 0, slot->size.width, slot->size.height);
}

GfxRect CompositorCompositionBounds(const Compositor *compositor)
{
    if (!compositor) {
        return GfxRectEmpty();
    }
    return MakeGfxRect(0, 0, compositor->config.size.width, compositor->config.size.height);
}

int CompositorClipToComposition(const Compositor *compositor, GfxRect rect, GfxRect *out)
{
    GfxRect clipped;

    if (!compositor || !out) {
        return 0;
    }
    if (GfxRectIntersect(rect, CompositorCompositionBounds(compositor), &clipped) != RoboOk) {
        return 0;
    }
    if (GfxRectIsEmpty(clipped)) {
        return 0;
    }
    *out = clipped;
    return 1;
}

int32_t CompositorRound(float value)
{
    if (!CompositorValueIsFinite(value)) {
        return value < 0.0f ? INT32_MIN : INT32_MAX;
    }
    if (value >= 2147483647.0f) {
        return INT32_MAX;
    }
    if (value <= -2147483648.0f) {
        return INT32_MIN;
    }
    return (int32_t)(value < 0.0f ? (value - 0.5f) : (value + 0.5f));
}

int CompositorValueIsFinite(float value)
{
    /* No math.h in this module: a value that is its own comparison fails only for a NaN, and the
     * range check keeps a scaling factor from producing a rectangle nobody can hold. */
    return (value == value) && (value <= 1.0e9f) && (value >= -1.0e9f);
}

unsigned CompositorCountLayerRefs(const Compositor *compositor, CompositorSurfaceId surface)
{
    unsigned i;
    unsigned count = 0u;

    if (!compositor) {
        return 0u;
    }
    for (i = 0u; i < compositor->layerSlots; i++) {
        if (compositor->layers[i].used && compositor->layers[i].surface == surface) {
            count++;
        }
    }
    return count;
}

uint8_t CompositorLayerEffectiveOpacity(const Compositor *compositor, CompositorLayerId id)
{
    const CompositorLayerSlot *layer;
    uint8_t opacity = 255u;
    unsigned guard = 0u;

    layer = CompositorFindLayerConst(compositor, id);
    while (layer && guard < compositor->layerSlots) {
        opacity = GfxMulDiv255(opacity, layer->opacity);
        layer = CompositorFindLayerConst(compositor, layer->parent);
        guard++;
    }
    return opacity;
}
