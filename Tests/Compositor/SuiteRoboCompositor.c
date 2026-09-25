/*
 * The compositor's core — Phase 15.1, 15.10, 15.17 and the two doors that exist to make a failure
 * readable (15.18's dump, and the fuzz check).
 *
 * Nine cases, and three of them are about *timing* rather than about pixels, because the module's
 * clock is the thing every other test depends on: a compositor whose time can go backwards makes
 * every animation and every deadline meaningless, so the refusal is asserted rather than assumed.
 *
 * The names case is positional on purpose. Every table here is read by a dump and by a failure
 * message, so an enumerator that was reordered would silently relabel a log — which is exactly the
 * kind of break a case likes this one catches before a human has to.
 */

#include "RoboCompositorTestSupport.h"

/* ------------------------------------------------------------------ */
/* 15.1 — the vocabulary and the capability table                      */
/* ------------------------------------------------------------------ */

static void TestCoreNames(void)
{
    RoboCheck(strcmp(CompositorDomainName(RoboCompositorDomainCore), "core") == 0);
    RoboCheck(strcmp(CompositorDomainName(RoboCompositorDomainCompose), "compose") == 0);
    RoboCheck(strcmp(CompositorDomainName(RoboCompositorDomainTiles), "tiles") == 0);
    RoboCheck(strcmp(CompositorDomainName(RoboCompositorDomainAnimation), "animation") == 0);
    RoboCheck(strcmp(CompositorDomainName((CompositorDiagnosticDomain)999), "?") == 0);
    RoboCheck(strcmp(CompositorTileStateName(RoboCompositorTileClean), "clean") == 0);
    RoboCheck(strcmp(CompositorTileStateName(RoboCompositorTileDirty), "dirty") == 0);
    RoboCheck(strcmp(CompositorResourceKindName(RoboCompositorResourceGpuTexture), "gpu-texture") == 0);
    RoboCheck(strcmp(CompositorPropertyName(RoboCompositorPropertyScrollY), "scroll-y") == 0);
    RoboCheck(strcmp(CompositorEasingName(RoboCompositorEasingEaseOut), "ease-out") == 0);
    RoboCheck(strcmp(CompositorFrameReasonName(RoboCompositorFrameReasonVsync), "vsync") == 0);
    RoboCheck(strcmp(CompositorPresentKindName(RoboCompositorPresentDamage), "damage") == 0);
    RoboCheck(strcmp(CompositorJobKindName(RoboCompositorJobTile), "tile") == 0);
    RoboCheck(strcmp(CompositorVersionString(), "1.0.0") == 0);
    /* Positional: the property order is what a saved recording's numbers mean. */
    RoboCheckEqInt((int)RoboCompositorPropertyOpacity, 0);
    RoboCheckEqInt((int)RoboCompositorPropertyOffsetX, 1);
    RoboCheckEqInt((int)RoboCompositorPropertyOffsetY, 2);
    RoboCheckEqInt((int)RoboCompositorPropertyScale, 3);
    RoboCheckEqInt((int)RoboCompositorPropertyScrollX, 4);
    RoboCheckEqInt((int)RoboCompositorPropertyScrollY, 5);
    RoboCheckEqInt((int)RoboCompositorPropertyCount, 6);
    RoboCheckEqInt((int)RoboCompositorEasingLinear, 0);
    RoboCheckEqInt((int)RoboCompositorEasingCount, 3);
}

static void TestCoreCapabilities(void)
{
    CompositorCapabilities caps;
    CompositorCapabilities again;

    RoboCheck(CompositorGetCapabilities(&caps) == RoboOk);
    RoboCheck(CompositorGetCapabilities(NULL) == RoboInvalidArgument);
    RoboCheck(CompositorGetCapabilities(&again) == RoboOk);

    /* What this build does. */
    RoboCheckEqInt(caps.layerTree, 1);
    RoboCheckEqInt(caps.damageTracking, 1);
    RoboCheckEqInt(caps.tiles, 1);
    RoboCheckEqInt(caps.rasterScheduling, 1);
    RoboCheckEqInt(caps.surfacePool, 1);
    RoboCheckEqInt(caps.frameScheduling, 1);
    RoboCheckEqInt(caps.vsync, 1);
    RoboCheckEqInt(caps.scrolling, 1);
    RoboCheckEqInt(caps.smoothScrolling, 1);
    RoboCheckEqInt(caps.animations, 1);
    RoboCheckEqInt(caps.reducedMotion, 1);
    RoboCheckEqInt(caps.framePump, 1);
    RoboCheckEqInt(caps.cpuFences, 1);
    RoboCheckEqInt(caps.memoryBudget, 1);
    RoboCheckEqInt(caps.performanceCounters, 1);
    RoboCheckEqInt(caps.diagnostics, 1);
    RoboCheckEqInt(caps.visualConformance, 1);
    RoboCheckEqInt(caps.presentToSurface, 1);
    RoboCheckEqInt(caps.presentRegions, 1);
    RoboCheckEqInt(caps.layerClips, 1);

    /* What it deliberately does not, stated so a consumer can refuse to build a path on it. */
    RoboCheckEqInt(caps.gpuResources, 0);
    RoboCheckEqInt(caps.gpuSync, 0);
    RoboCheckEqInt(caps.doubleBuffering, 0);
    RoboCheckEqInt(caps.compositorThread, 0);
    RoboCheckEqInt(caps.rotation, 0);
    RoboCheckEqInt(caps.subpixelSampling, 0);
    RoboCheckEqInt(caps.colorManagement, 0);
    RoboCheckEqInt(caps.presentToDisplay, 0);
    RoboCheckEqInt(caps.layerBlur, 0);
    RoboCheckEqInt(caps.toneMapping, 0);

    /* The bounds every table in the module is allocated from. */
    RoboCheck(caps.maxLayers > 0u);
    RoboCheck(caps.maxSurfaces > 0u);
    RoboCheck(caps.maxDamageRects > 0u);
    RoboCheck(caps.maxJobs > 0u);
    RoboCheck(caps.maxAnimations > 0u);
    RoboCheck(caps.tileEdge > 0u);
    RoboCheckEqInt((int)caps.propertyCount, (int)RoboCompositorPropertyCount);
    RoboCheckEqInt((int)caps.domainCount, (int)RoboCompositorDomainCount);
    RoboCheckEqInt(caps.damageOverflowToFull, 1);
    /* The table is a pure function of the build. */
    RoboCheck(memcmp(&caps, &again, sizeof(caps)) == 0);
}

static void TestCoreConfigAndCreate(void)
{
    CompositorConfig config;
    Compositor *compositor = NULL;
    CompositorDiag diag;
    GfxSize size;

    CompositorConfigInit(&config);
    RoboCheckEqInt((int)config.size.width, 1024);
    RoboCheckEqInt((int)config.size.height, 768);
    RoboCheckEqInt((int)config.format, (int)RoboGfxFormatRgba8);
    RoboCheck(config.maxLayers > 0u);
    RoboCheck(config.maxSurfaces > 0u);
    RoboCheckEqInt(config.reducedMotion, 0);
    RoboCheckEqInt(config.damagePresent, 0);
    RoboCheckEqInt((int)config.memoryBudgetBytes, 0);

    /* NULL configuration means the defaults. */
    CompositorDiagReset(&diag);
    RoboCheck(CompositorCreate(NULL, &compositor, &diag) == RoboOk);
    RoboCheck(compositor != NULL);
    size = CompositorSize(compositor);
    RoboCheckEqInt((int)size.width, 1024);
    RoboCheck(CompositorIsLive(compositor) == 1);
    RoboCheck(CompositorGetConfig(compositor) != NULL);
    RoboCheck(CompositorSize(NULL).width == 0);
    RoboCheck(CompositorIsLive(NULL) == 0);
    CompositorDestroy(compositor);
    CompositorDestroy(NULL); /* safe on NULL */

    /* A size with no pixels is refused before anything is allocated. */
    CompositorConfigInit(&config);
    config.size = MakeGfxSize(0, 10);
    compositor = NULL;
    RoboCheck(CompositorCreate(&config, &compositor, &diag) == RoboInvalidArgument);
    RoboCheck(compositor == NULL);
    RoboCheckEqInt((int)CompositorDiagCode(&diag), (int)RoboInvalidArgument);
    RoboCheck(diag.message && diag.message[0] != '\0');

    /* A format with no CPU layout is refused by name rather than laid out wrongly. */
    CompositorConfigInit(&config);
    config.size = MakeGfxSize(16, 16);
    config.format = RoboGfxFormatRgba16;
    compositor = NULL;
    RoboCheck(CompositorCreate(&config, &compositor, &diag) == RoboNotSupported);

    /* Unknown means RGBA8; a caller that states nothing gets the worked-out default. */
    CompositorConfigInit(&config);
    config.size = MakeGfxSize(16, 16);
    config.format = RoboGfxFormatUnknown;
    RoboCheck(CompositorCreate(&config, &compositor, &diag) == RoboOk);
    RoboCheckEqInt((int)CompositorGetConfig(compositor)->format, (int)RoboGfxFormatRgba8);
    CompositorDestroy(compositor);

    /* A resize marks everything stale, because the frame's geometry moved. */
    compositor = CompositorFixture();
    RoboCheck(CompositorResize(compositor, MakeGfxSize(128, 96), NULL) == RoboOk);
    RoboCheckEqInt((int)CompositorSize(compositor).width, 128);
    RoboCheck(CompositorDamageIsFull(compositor) == 1);
    RoboCheck(CompositorResize(compositor, MakeGfxSize(0, 0), NULL) == RoboInvalidArgument);
    CompositorDestroy(compositor);
}

static void TestCoreClock(void)
{
    Compositor *compositor = CompositorFixture();
    unsigned ticks = 99u;

    RoboCheckEqInt((int)CompositorTimeNow(compositor), 0);
    RoboCheck(CompositorAdvanceTime(compositor, 10, &ticks, NULL) == RoboOk);
    RoboCheckEqInt((int)ticks, 0);
    RoboCheckEqInt((int)CompositorTimeNow(compositor), 10);
    /* Backwards is refused: a clock that can reverse makes every deadline meaningless. */
    RoboCheck(CompositorAdvanceTime(compositor, 5, NULL, NULL) == RoboInvalidArgument);
    RoboCheckEqInt((int)CompositorTimeNow(compositor), 10);

    /* No interval means every frame is due. */
    RoboCheckEqInt((int)CompositorVsyncInterval(compositor), 0);
    RoboCheck(CompositorFrameIsDue(compositor) == 1);
    RoboCheckEqInt((int)CompositorSetVsync(compositor, 16u), 16);
    RoboCheckEqInt((int)CompositorVsyncInterval(compositor), 16);
    /* Not armed until a present, so the first frame is not made to wait. */
    RoboCheck(CompositorFrameIsDue(compositor) == 1);
    CompositorDestroy(compositor);
}

static void TestCoreStats(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorStats stats;

    CompositorStatsInit(&stats);
    RoboCheckEqInt((int)stats.frames, 0);
    RoboCheck(CompositorGetStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.frames, 0);
    RoboCheckEqInt((int)stats.layers, 0);
    RoboCheckEqInt((int)stats.surfaces, 0);
    RoboCheckEqInt((int)stats.refusalsByDomain[RoboCompositorDomainCompose], 0);

    RoboCheck(CompositorLayerCreate(compositor, 1u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorDamageAdd(compositor, RoboCompositorSurfaceNone, MakeGfxRect(0, 0, 4, 4),
                                  NULL) == RoboOk);
    RoboCheck(CompositorGetStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.layers, 1);
    RoboCheckEqInt((int)stats.damageRects, 1);
    RoboCheckEqInt((int)stats.damageAdded, 1);
    RoboCheckEqInt((int)stats.damagePixels, 16);

    CompositorResetStats(compositor);
    RoboCheck(CompositorGetStats(compositor, &stats) == RoboOk);
    RoboCheckEqInt((int)stats.damageAdded, 0);
    RoboCheckEqInt((int)stats.layers, 1); /* a reset clears counters, not state */
    CompositorDestroy(compositor);
}

static void TestCoreRefusals(void)
{
    Compositor *compositor = CompositorFixture();
    CompositorDiag diag;
    uint64_t counts[RoboCompositorDomainCount];
    size_t i;

    CompositorDiagReset(&diag);
    RoboCheck(CompositorGetLastDiag(compositor, &diag) == RoboNotFound);
    RoboCheckEqInt((int)CompositorDiagCode(&diag), (int)RoboOk);

    /* A refusal with no diag is still recorded: a caller that does not care should not have to pass
     * one, and a dump afterwards must still say what went wrong. */
    RoboCheck(CompositorLayerCreate(compositor, 0u, RoboCompositorLayerNone, NULL) ==
              RoboInvalidArgument);
    RoboCheck(CompositorGetLastDiag(compositor, &diag) == RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboCompositorDomainLayer);
    RoboCheck(strstr(diag.message, "non-zero") != NULL);

    RoboCheck(CompositorGetRefusalCounts(compositor, counts, RoboCompositorDomainCount) == RoboOk);
    RoboCheckEqInt((int)counts[RoboCompositorDomainLayer], 1);
    RoboCheck(CompositorGetRefusalCounts(NULL, counts, 4u) == RoboInvalidArgument);
    /* Asking for fewer than every domain reports what fits and zeroes the rest. */
    for (i = 0u; i < RoboCompositorDomainCount; i++) {
        counts[i] = 99u;
    }
    RoboCheck(CompositorGetRefusalCounts(compositor, counts, 2u) == RoboOk);
    RoboCheckEqInt((int)counts[2], 0);

    RoboCheck(CompositorValidate(compositor, &diag) == RoboOk);
    CompositorDestroy(compositor);
}

/* ------------------------------------------------------------------ */
/* 15.18 — the dump                                                    */
/* ------------------------------------------------------------------ */

static void TestDumpCapabilities(void)
{
    Compositor *compositor = CompositorFixture();
    char buffer[2048];
    size_t needed = 0u;
    Result result;

    RoboCheck(CompositorWriteCapabilities(compositor, NULL, 0u, &needed) == RoboOk);
    RoboCheck(needed > 100u);
    /* Complete or absent: a buffer that cannot hold the block gets nothing and the length. */
    buffer[0] = 'x';
    result = CompositorWriteCapabilities(compositor, buffer, 8u, &needed);
    RoboCheck(result == RoboNotSupported);
    RoboCheckEqInt((int)buffer[0], 0);
    RoboCheck(needed > 100u);

    result = CompositorWriteCapabilities(compositor, buffer, sizeof(buffer), &needed);
    RoboCheck(result == RoboOk);
    RoboCheck(strstr(buffer, "layerTree=1") != NULL);
    RoboCheck(strstr(buffer, "gpuResources=0") != NULL);
    RoboCheck(strstr(buffer, "compositor 1.0.0") != NULL);
    RoboCheck(strlen(buffer) + 1u == needed);
    CompositorDestroy(compositor);
}

static void TestDumpSummary(void)
{
    Compositor *compositor = CompositorFixture();
    GfxSurface *surface = NULL;
    char *buffer;
    size_t needed = 0u;
    size_t capacity = 65536u;

    buffer = (char *)Malloc(capacity);
    RoboCheck(buffer != NULL);
    RoboCheck(CompositorLayerCreate(compositor, 7u, RoboCompositorLayerNone, NULL) == RoboOk);
    RoboCheck(CompositorSurfaceAcquire(compositor, 3u, MakeGfxSize(16, 16), RoboGfxFormatRgba8,
                                      &surface, NULL) == RoboOk);
    RoboCheck(CompositorLayerSetRect(compositor, 7u, MakeGfxRect(0, 0, 16, 16), NULL) == RoboOk);
    RoboCheck(CompositorLayerSetSurface(compositor, 7u, 3u, NULL) == RoboOk);
    RoboCheck(CompositorDamageAdd(compositor, 3u, MakeGfxRect(0, 0, 8, 8), NULL) == RoboOk);

    RoboCheck(CompositorWriteSummary(compositor, NULL, 0u, &needed) == RoboOk);
    RoboCheck(needed > 200u);
    RoboCheck(CompositorWriteSummary(compositor, buffer, capacity, &needed) == RoboOk);
    RoboCheck(strstr(buffer, "# capabilities") != NULL);
    RoboCheck(strstr(buffer, "# layers") != NULL);
    RoboCheck(strstr(buffer, "# damage") != NULL);
    RoboCheck(strstr(buffer, "# tiles") != NULL);
    RoboCheck(strstr(buffer, "# jobs") != NULL);
    RoboCheck(strstr(buffer, "# surfaces") != NULL);
    RoboCheck(strstr(buffer, "# animations") != NULL);
    RoboCheck(strstr(buffer, "# stats") != NULL);
    /* The sections agree with the doors each section is written by: a summary is not a second
     * implementation. */
    RoboCheck(strstr(buffer, "layer 7") != NULL);
    RoboCheck(strstr(buffer, "surface id 3") != NULL);
    RoboCheck(strstr(buffer, "region 0 surface 3") != NULL);
    RoboCheck(strstr(buffer, "frames=0") != NULL);

    RoboFree(buffer);
    CompositorDestroy(compositor);
}

/* ------------------------------------------------------------------ */
/* The fuzz door                                                       */
/* ------------------------------------------------------------------ */

static void TestFuzzStaysValid(void)
{
    Compositor *compositor = CompositorFixture();
    uint8_t bytes[512];
    size_t calls = 0u;
    CompositorDiag diag;
    uint32_t state = 0x12345678u;
    size_t i;

    for (i = 0u; i < sizeof(bytes); i++) {
        state = state * 1664525u + 1013904223u;
        bytes[i] = (uint8_t)(state >> 16);
    }
    RoboCheck(CompositorFuzz(compositor, bytes, sizeof(bytes), &calls, &diag) == RoboOk);
    RoboCheck(calls > 0u);
    /* Validity is the invariant, and it is checkable afterwards as well as during. */
    RoboCheck(CompositorValidate(compositor, &diag) == RoboOk);
    RoboCheck(CompositorFuzz(compositor, NULL, 0u, &calls, &diag) == RoboOk);
    RoboCheckEqInt((int)calls, 0);
    RoboCheck(CompositorFuzz(NULL, bytes, sizeof(bytes), &calls, &diag) == RoboInvalidArgument);
    CompositorDestroy(compositor);
}

void TestRegisterCompositor(void)
{
    TestRegister("compositor.core.names", TestCoreNames);
    TestRegister("compositor.core.capabilities", TestCoreCapabilities);
    TestRegister("compositor.core.config", TestCoreConfigAndCreate);
    TestRegister("compositor.core.clock", TestCoreClock);
    TestRegister("compositor.core.stats", TestCoreStats);
    TestRegister("compositor.core.refusals", TestCoreRefusals);
    TestRegister("compositor.dump.capabilities", TestDumpCapabilities);
    TestRegister("compositor.dump.summary", TestDumpSummary);
    TestRegister("compositor.fuzz.validity", TestFuzzStaysValid);
}
