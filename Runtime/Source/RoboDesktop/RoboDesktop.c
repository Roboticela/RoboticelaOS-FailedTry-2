/*
 * RoboDesktop (Phase 11.13) — the shell's lifecycle, its frames, and its presentation.
 *
 * ## The screen target is the framebuffer, and the frame is the unit
 *
 * `desktop->target` is a RoboRender target — owned, RGBA8, zero-filled at creation — and every frame paints
 * *into* it from its first fill. That is why there is no "clear the frame" step anywhere in this file: the
 * first thing `DesktopChromePaintShell` does is fill the whole screen with the desktop surface, which
 * is an opaque fill and therefore *is* the clear. A separate clear would be a second full-surface write per
 * frame to reach the same state, and a frame that cleared and then painted a translucent desktop would be
 * relying on the clear's colour rather than on its own.
 *
 * ## Content is rasterised per window, then composited
 *
 * Each window with contents owns a target the size of its client area (`DesktopWindow.target`, created
 * lazily), the display list is executed into it, and the result is composited into the screen at the client
 * rectangle's origin. Three consequences, and all three are why it is done this way rather than compositing
 * the list straight into the screen:
 *
 *   a window's contents are **one picture** — they do not share a clip or an opacity scope with the shell,
 *   so a `push-clip` in a document cannot reach the panel;
 *   a window whose contents fail to rasterise leaves a **window** on screen (its body and decorations are
 *   painted before the composite), not a hole;
 *   and the same content target is what a future compositor would take as a layer, which is the boundary
 *   this build stops at rather than one it crosses.
 *
 * ## Presentation is one call and one wait
 *
 * `DesktopPresent` is RoboWindow's own frame triple around `RenderPresent`: the window gives its
 * render surface, the frame is composited into it, the window is marked dirty and presented. Nothing waits
 * for a vertical blank, nothing is double-buffered, and nothing is paced — which is stated here because a
 * `present` that was paced would be a frame *scheduler*, and this phase does not have one.
 */

#include "RoboDesktopInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboGraphics/RoboGraphicsComposite.h"
#include "RoboMem/RoboMem.h"
#include "RoboRender/RoboRenderPresenter.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowGraphics.h"

/* ------------------------------------------------------------------ */
/* The module                                                          */
/* ------------------------------------------------------------------ */

const char *DesktopVersionString(void)
{
    return RoboDesktopVersionString;
}

Result DesktopGetVersion(I32 *outMajor,
                                      I32 *outMinor,
                                      I32 *outPatch)
{
    if (!outMajor || !outMinor || !outPatch) {
        return RoboInvalidArgument;
    }
    *outMajor = (I32)RoboDesktopVersionMajor;
    *outMinor = (I32)RoboDesktopVersionMinor;
    *outPatch = (I32)RoboDesktopVersionPatch;
    return RoboOk;
}

int DesktopShellIsImplemented(void)
{
    return RoboDesktopShellImplemented;
}

void DesktopCapabilitiesInit(DesktopCapabilities *capabilities)
{
    size_t bytes;
    size_t index;

    if (!capabilities) {
        return;
    }
    bytes = sizeof(*capabilities);
    for (index = 0u; index < bytes; index++) {
        ((char *)capabilities)[index] = 0;
    }
}

Result DesktopGetCapabilities(DesktopCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    DesktopCapabilitiesInit(out);

    /* What this release is: a shell, its windows, their contents, presentation, records, resize and the
     * pointer targeting that makes the first of those interactive. */
    out->shell = RoboDesktopShellImplemented;
    out->windows = 1;
    out->windowContent = 1;
    out->presentation = 1;
    out->frameRecords = 1;
    out->resize = 1;
    out->pointerTargeting = 1;
    out->shellText = 1;

    /*
     * The absences, and each is a structure rather than a flag somebody forgot to set -- the same
     * discipline `RoboRender.h` keeps one module down. `documents` is 0 because the *shell* does not read
     * HTML: an application document is a separate module (`RoboDesktopDocument.h`), and a capability row
     * here would claim the shell needs one. `compositor`, `tiles` and `damageTracking` are the three
     * structures a frame would have to become; `frameScheduling` and `vsync` are the clock and the signal
     * this build's determinism depends on not having; `gpu` is a device and a resource manager;
     * `windowManager`, `inputDelivery` and `desktopEnvironment` are the three products this phase
     * explicitly does not build.
     */
    out->documents = 0;
    out->compositor = 0;
    out->tiles = 0;
    out->damageTracking = 0;
    out->frameScheduling = 0;
    out->vsync = 0;
    out->gpu = 0;
    out->windowManager = 0;
    out->inputDelivery = 0;
    out->desktopEnvironment = 0;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Configuration and lifetime                                          */
/* ------------------------------------------------------------------ */

void DesktopConfigInit(DesktopConfig *config)
{
    if (!config) {
        return;
    }
    config->width = 1280;
    config->height = 800;
    config->window = RoboWindowIdNone;
    config->RoboCreateWindow = 0;
    config->windowTitle = NULL;
    config->identityText = NULL;
    config->applicationsText = NULL;
    config->statusText = NULL;
    config->theme = NULL;
    config->panel = NULL;
    config->windowMetrics = NULL;
    config->face = NULL;
    config->labelSizePx = RoboDesktopDefaultLabelSizePx;
}

/** The whole struct zeroed, so a shell starts from a state with no stale field in it. */
static void DesktopClear(Desktop *desktop)
{
    size_t bytes = sizeof(*desktop);
    size_t index;

    for (index = 0u; index < bytes; index++) {
        ((char *)desktop)[index] = 0;
    }
    desktop->focusedIndex = RoboDesktopNoIndex;
    desktop->window = RoboWindowIdNone;
}

Result DesktopCreate(const DesktopConfig *config,
                                 Desktop **out,
                                 DesktopDiag *diag)
{
    Desktop *desktop;
    DesktopPanelMetrics panel;
    DesktopWindowMetrics windowMetrics;
    DesktopTheme theme;

    if (!config || !out) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "a configuration and an out pointer are required");
    }
    if (config->width <= 0 || config->height <= 0 ||
        config->width > (int32_t)RoboDesktopMaxDimension ||
        config->height > (int32_t)RoboDesktopMaxDimension) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "the screen size is not one this build can make");
    }

    /* Metrics and theme first: every refusal below is about something the shell will not draw, and a
     * shell that allocated a target before discovering that cannot present itself is a leak with a
     * message attached. */
    if (config->panel) {
        panel = *config->panel;
    } else {
        DesktopPanelMetricsInit(&panel);
    }
    if (!DesktopPanelMetricsIsValid(&panel)) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "the panel metrics are not ones this build can lay out");
    }
    if (config->windowMetrics) {
        windowMetrics = *config->windowMetrics;
    } else {
        DesktopWindowMetricsInit(&windowMetrics);
    }
    if (!DesktopWindowMetricsIsValid(&windowMetrics)) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "the window metrics are not ones this build can draw");
    }
    if (config->theme) {
        theme = *config->theme;
    } else {
        DesktopThemeInit(&theme);
    }
    if (!DesktopThemeIsValid(&theme)) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "the theme is not one this build can paint with");
    }
    {
        const char *identity = config->identityText ? config->identityText
                                                    : RoboDesktopDefaultIdentity;
        const char *applications = config->applicationsText ? config->applicationsText
                                                            : RoboDesktopDefaultApplications;
        const char *statusText = config->statusText ? config->statusText : "";

        if (DesktopStringLength(identity) + 1u > RoboDesktopTitleMax ||
            DesktopStringLength(applications) + 1u > RoboDesktopTitleMax ||
            DesktopStringLength(statusText) + 1u > RoboDesktopStatusMax) {
            return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                     "a shell label does not fit");
        }
    }

    desktop = (Desktop *)Realloc(NULL, sizeof(*desktop));
    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboOutOfMemory,
                                 "the shell could not be allocated");
    }
    DesktopClear(desktop);
    desktop->theme = theme;
    desktop->panel = panel;
    desktop->windowMetrics = windowMetrics;
    desktop->labelSizePx =
        (config->labelSizePx > 0.0f) ? config->labelSizePx : RoboDesktopDefaultLabelSizePx;
    desktop->face = config->face;
    if (DesktopStringCopy(desktop->identity, sizeof(desktop->identity),
                                 config->identityText ? config->identityText
                                                       : RoboDesktopDefaultIdentity) != RoboOk ||
        DesktopStringCopy(desktop->applications, sizeof(desktop->applications),
                                 config->applicationsText ? config->applicationsText
                                                           : RoboDesktopDefaultApplications) !=
            RoboOk ||
        DesktopStringCopy(desktop->status, sizeof(desktop->status),
                                 config->statusText ? config->statusText : "") != RoboOk) {
        RoboFree(desktop);
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "a shell label does not fit");
    }

    if (DesktopGeometryCompute(MakeGfxSize(config->width, config->height), &panel,
                                      &desktop->geometry) != RoboOk) {
        RoboFree(desktop);
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "the desktop's geometry could not be computed");
    }
    if (RenderTargetCreate(&desktop->target, (uint32_t)config->width,
                                  (uint32_t)config->height, NULL) != RoboOk ||
        !desktop->target) {
        RoboFree(desktop);
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboOutOfMemory,
                                 "the screen target could not be allocated");
    }

    if (config->window != RoboWindowIdNone) {
        /* An attached window is borrowed: this shell presents into it and does not own it. */
        desktop->window = config->window;
        desktop->ownsWindow = 0;
    } else if (config->RoboCreateWindow) {
        WindowConfig windowConfig;

        WindowConfigInit(&windowConfig);
        windowConfig.width = config->width;
        windowConfig.height = config->height;
        windowConfig.visible = 1;
        windowConfig.resizable = 1;
        windowConfig.title = config->windowTitle ? config->windowTitle
                                                  : RoboDesktopDefaultTitle;
        if (WindowCreate(&windowConfig, &desktop->window) != RoboOk) {
            RenderTargetDestroy(desktop->target);
            RoboFree(desktop);
            return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboNotSupported,
                                     "no native window could be created for the desktop");
        }
        desktop->ownsWindow = 1;
        (void)WindowShow(desktop->window);
    }

    desktop->live = 1;
    desktop->nextId = 0u;
    *out = desktop;
    /*
     * One line per shell, and the facts a reader needs to interpret every later line: how big the screen
     * is, and whether this shell has somewhere to present to at all. The `window=` field is the *state*
     * (`owned`/`attached`/`none`) rather than RoboWindow's id, because that id belongs to the platform
     * rather than to this shell -- a shell window id printed below *is* this module's own and is
     * deterministic, which is why it is safe to print and a native window's is not.
     */
    LogDebug("robo_desktop: shell created %dx%d (window=%s, face=%s)",
                   (int)desktop->geometry.screen.width, (int)desktop->geometry.screen.height,
                   desktop->window == RoboWindowIdNone
                       ? "none"
                       : (desktop->ownsWindow ? "owned" : "attached"),
                   desktop->face ? "yes" : "none");
    return RoboOk;
}

void DesktopDestroy(Desktop *desktop)
{
    size_t index;

    if (!desktop) {
        return;
    }
    /* The counters go out with the shell, the way every module in this tree reports what it did on the
     * way down (`WindowShutdown`, `LogDebug` in the CSS tokenizer). Nothing here is an
     * address and nothing here is a clock. */
    LogDebug("robo_desktop: shell released (frames=%llu, fills=%llu, content_runs=%llu, "
                   "windows_added=%llu, presentations=%llu)",
                   (unsigned long long)desktop->stats.frames,
                   (unsigned long long)desktop->stats.fillDrawings,
                   (unsigned long long)desktop->stats.contentRuns,
                   (unsigned long long)desktop->stats.windowsAdded,
                   (unsigned long long)desktop->stats.presentations);
    for (index = 0u; index < desktop->windowCount; index++) {
        DesktopWindowReleaseTarget(&desktop->windows[index]);
    }
    desktop->windowCount = 0u;
    if (desktop->target) {
        RenderTargetDestroy(desktop->target);
        desktop->target = NULL;
    }
    /* Only a window this shell created is destroyed here; an attached one is the caller's (see the
     * header). */
    if (desktop->ownsWindow && desktop->window != RoboWindowIdNone) {
        (void)WindowDestroy(desktop->window);
    }
    desktop->window = RoboWindowIdNone;
    desktop->ownsWindow = 0;
    desktop->live = 0;
    RoboFree(desktop);
}

int DesktopIsLive(const Desktop *desktop)
{
    return (desktop && desktop->live) ? 1 : 0;
}

GfxSize DesktopScreenSize(const Desktop *desktop)
{
    return desktop ? MakeGfxSize(desktop->geometry.screen.width, desktop->geometry.screen.height)
                   : MakeGfxSize(0, 0);
}

DesktopGeometry DesktopGeometryOf(const Desktop *desktop)
{
    DesktopGeometry geometry;

    if (!desktop) {
        DesktopGeometryInit(&geometry);
        return geometry;
    }
    return desktop->geometry;
}

GfxRect DesktopWorkArea(const Desktop *desktop)
{
    return desktop ? desktop->geometry.workArea : GfxRectEmpty();
}

WindowId DesktopAttachedWindow(const Desktop *desktop)
{
    return desktop ? desktop->window : RoboWindowIdNone;
}

Result DesktopSetStatus(const Desktop *desktop,
                                      const char *utf8Text,
                                      DesktopDiag *diag)
{
    Desktop *mutableDesktop = (Desktop *)desktop;

    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "a desktop is required");
    }
    if (DesktopStringCopy(mutableDesktop->status, sizeof(mutableDesktop->status),
                                 utf8Text) != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "the status label does not fit");
    }
    return RoboOk;
}

const char *DesktopStatus(const Desktop *desktop)
{
    return desktop ? desktop->status : "";
}

/* ------------------------------------------------------------------ */
/* The frame record                                                    */
/* ------------------------------------------------------------------ */

void DesktopFrameRecordInit(DesktopFrameRecord *record)
{
    size_t bytes;
    size_t index;

    if (!record) {
        return;
    }
    bytes = sizeof(*record);
    for (index = 0u; index < bytes; index++) {
        ((char *)record)[index] = 0;
    }
}

int DesktopFrameRecordIsValid(const DesktopFrameRecord *record)
{
    if (!record) {
        return 0;
    }
    if (record->width == 0u || record->height == 0u) {
        return 0;
    }
    if (record->width > (uint32_t)RoboDesktopMaxDimension ||
        record->height > (uint32_t)RoboDesktopMaxDimension) {
        return 0;
    }
    /* The counts a frame's own arithmetic fixes: a window can only have painted if there are windows, and
     * a label can only have been skipped if there was one to skip. A record whose fields disagree about
     * that is a record that has been assembled by hand. */
    if (record->windows > record->windowsTotal) {
        return 0;
    }
    if (record->contentPixels > (size_t)record->width * (size_t)record->height) {
        return 0;
    }
    if (record->bytes != (size_t)record->width * (size_t)record->height * 4u) {
        return 0;
    }
    return 1;
}

int DesktopFrameRecordEqual(const DesktopFrameRecord *a,
                                    const DesktopFrameRecord *b)
{
    if (!a || !b) {
        return a == b;
    }
    return a->width == b->width && a->height == b->height && a->fills == b->fills &&
           a->labels == b->labels && a->labelsUnshaped == b->labelsUnshaped &&
           a->windows == b->windows && a->windowsTotal == b->windowsTotal &&
           a->contentRuns == b->contentRuns && a->contentPixels == b->contentPixels &&
           a->clipped == b->clipped && a->bytes == b->bytes && a->checksum == b->checksum;
}

/* ------------------------------------------------------------------ */
/* Window contents                                                     */
/* ------------------------------------------------------------------ */

/** The window's content target, created or recreated to match @p client. */
static Result DesktopContentTarget(DesktopWindow *window,
                                           GfxRect client,
                                           RenderDiag *diag)
{
    if (client.width <= 0 || client.height <= 0) {
        return RoboInvalidArgument;
    }
    if (window->target) {
        if ((int32_t)RenderTargetWidth(window->target) == client.width &&
            (int32_t)RenderTargetHeight(window->target) == client.height) {
            return RoboOk;
        }
        RenderTargetDestroy(window->target);
        window->target = NULL;
    }
    return RenderTargetCreate(&window->target, (uint32_t)client.width,
                                     (uint32_t)client.height, diag);
}

/**
 * Rasterise a window's contents and composite them into the screen.
 *
 * The failure is *returned* rather than swallowed -- the shell reports that a window's contents could not
 * be drawn (`RoboNotSupported`, `WINDOW` domain) while still having painted the window around them --
 * so a caller learns that a document is broken instead of quietly seeing an empty body.
 */
static Result DesktopPaintWindowContent(Desktop *desktop,
                                                 DesktopWindow *window,
                                                 GfxRect client,
                                                 GfxRaster *raster,
                                                 DesktopFrameRecord *record,
                                                 RenderDiag *renderDiag)
{
    FontFace *face;
    GfxSurface *screenSurface;
    Result status;

    if (!window->content) {
        return RoboOk;
    }
    status = DesktopContentTarget(window, client, renderDiag);
    if (status != RoboOk || !window->target) {
        return (status != RoboOk) ? status : RoboOutOfMemory;
    }
    /* The body colour first: a document that draws nothing must leave a *window*, not the desktop showing
     * through the hole where its client area is. */
    (void)RenderTargetClear(window->target, desktop->theme.windowSurface, NULL);

    face = window->contentFace ? window->contentFace : desktop->face;
    status = RenderRasterize(window->content, window->target, face, NULL, renderDiag);
    if (status != RoboOk) {
        return status;
    }
    record->contentRuns++;

    screenSurface = GfxRasterTarget(raster);
    if (!screenSurface) {
        return RoboError;
    }
    status = GfxSurfaceCompositeRect(RenderTargetSurfaceConst(window->target),
                                            RenderTargetBounds(window->target),
                                            screenSurface,
                                            MakeGfxPoint(client.x, client.y), 255u);
    if (status != RoboOk) {
        return status;
    }
    record->contentPixels += (size_t)client.width * (size_t)client.height;
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Frames                                                              */
/* ------------------------------------------------------------------ */

Result DesktopBeginFrame(Desktop *desktop, DesktopDiag *diag)
{
    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "a desktop is required");
    }
    if (desktop->frameOpen) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboBusy,
                                 "a frame is already open");
    }
    desktop->frameOpen = 1;
    desktop->painted = 0;
    return RoboOk;
}

int DesktopFrameIsOpen(const Desktop *desktop)
{
    return (desktop && desktop->frameOpen) ? 1 : 0;
}

Result DesktopPaint(Desktop *desktop, DesktopDiag *diag)
{
    DesktopFrameRecord record;
    GfxRaster *raster = NULL;
    RenderDiag renderDiag;
    const char *contentFailure = NULL;
    size_t index;

    RenderDiagReset(&renderDiag);
    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "a desktop is required");
    }
    if (!desktop->frameOpen) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "there is no open frame to paint");
    }
    if (desktop->painted) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboBusy,
                                 "this frame has already been painted");
    }
    if (!desktop->target) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboNotFound,
                                 "the desktop has no screen target");
    }
    if (GfxRasterCreate(RenderTargetSurface(desktop->target), &raster) != RoboOk ||
        !raster) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboError,
                                 "the screen target could not be opened for painting");
    }
    /* The clip is the screen for the whole frame: the raster's own bounds do this anyway, and stating it
     * is what makes "clipped" in the record a statement about a *drawing* rather than about a surface. */
    (void)GfxRasterResetClip(raster);

    DesktopFrameRecordInit(&record);
    record.width = (uint32_t)desktop->geometry.screen.width;
    record.height = (uint32_t)desktop->geometry.screen.height;
    record.windowsTotal = desktop->windowCount;
    record.bytes = (size_t)record.width * (size_t)record.height * 4u;

    (void)DesktopChromePaintShell(desktop, raster, &record, diag);

    /* Back to front: the last window painted is the front one, which is the whole of this build's z-order
     * (see `RoboDesktopWindow.c`). */
    for (index = 0u; index < desktop->windowCount; index++) {
        DesktopWindow *window = &desktop->windows[index];
        GfxRect client;
        int focused;

        if (!window->visible || window->state != RoboDesktopWindowOpen) {
            continue;
        }
        focused = (desktop->focusedIndex == index) ? 1 : 0;
        client = DesktopChromeClientBounds(desktop, window);
        (void)DesktopChromePaintWindowFrame(desktop, window, focused, raster, &record, diag);
        if (DesktopPaintWindowContent(desktop, window, client, raster, &record, &renderDiag) !=
            RoboOk) {
            /* The first failure is the one reported, and the painting continues: the window around a body
             * that could not be drawn is still a window, and the panel and every other window are still
             * worth painting. A frame that stopped here would leave a half-drawn screen whose record
             * described less than the shell actually painted. */
            if (!contentFailure) {
                contentFailure = renderDiag.message;
            }
            /* Warned per frame rather than once, because a broken document is a state the caller is in
             * for as long as it holds that content, and a single line would make the *second* failing
             * frame look like a frame that painted fine. */
            LogWarn("robo_desktop: window contents could not be drawn (id=%llu): %s",
                          (unsigned long long)window->id,
                          renderDiag.message ? renderDiag.message : "the run was refused");
        }
        DesktopChromePaintCloseButton(desktop, window, raster, &record);
        DesktopChromePaintWindowTitle(desktop, window, raster, &record);
        record.windows++;
    }

    GfxRasterDestroy(raster);
    /* The record a frame produces is kept until `end_frame`, which is where the counters are folded into
     * the shell's own: a caller that never ends its frame gets no statistics, which is the honest reading
     * of "the frame never finished". */
    desktop->frameRecord = record;
    desktop->painted = 1;

    if (contentFailure) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboNotSupported,
                                 contentFailure);
    }
    return RoboOk;
}

Result DesktopEndFrame(Desktop *desktop,
                                    DesktopFrameRecord *outRecord,
                                    DesktopDiag *diag)
{
    DesktopFrameRecord record;

    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "a desktop is required");
    }
    if (!desktop->frameOpen) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "there is no open frame to end");
    }
    if (desktop->painted) {
        record = desktop->frameRecord;
    } else {
        DesktopFrameRecordInit(&record);
        record.width = (uint32_t)desktop->geometry.screen.width;
        record.height = (uint32_t)desktop->geometry.screen.height;
        record.windowsTotal = desktop->windowCount;
        record.bytes = (size_t)record.width * (size_t)record.height * 4u;
    }
    record.checksum = RenderTargetChecksum(desktop->target);

    desktop->stats.frames++;
    if (record.windows == 0u) {
        desktop->stats.framesWithoutWindows++;
    }
    desktop->stats.contentRuns += record.contentRuns;
    desktop->stats.contentPixels += record.contentPixels;
    desktop->stats.labels += record.labels;
    desktop->stats.labelsUnshaped += record.labelsUnshaped;
    desktop->stats.fillDrawings += record.fills;
    desktop->stats.clippedDrawings += record.clipped;
    desktop->stats.lastFrameChecksum = record.checksum;

    desktop->frameOpen = 0;
    desktop->painted = 0;
    if (outRecord) {
        *outRecord = record;
    }
    return RoboOk;
}

Result DesktopRender(Desktop *desktop,
                                 DesktopFrameRecord *outRecord,
                                 DesktopDiag *diag)
{
    Result status;

    status = DesktopBeginFrame(desktop, diag);
    if (status != RoboOk) {
        return status;
    }
    status = DesktopPaint(desktop, diag);
    if (status != RoboOk) {
        /* The frame is ended even when the paint reported a problem, so a caller is never left holding a
         * desktop whose next `begin_frame` reports RoboBusy for a frame nothing will ever close -- and
         * the *record* is still handed over, because a frame with a window whose contents could not be
         * drawn is a frame the caller can still describe. */
        (void)DesktopEndFrame(desktop, outRecord, NULL);
        return status;
    }
    return DesktopEndFrame(desktop, outRecord, diag);
}

const GfxSurface *DesktopFrameSurface(const Desktop *desktop)
{
    if (!desktop || !desktop->target) {
        return NULL;
    }
    return RenderTargetSurfaceConst(desktop->target);
}

uint64_t DesktopFrameChecksum(const Desktop *desktop)
{
    if (!desktop || !desktop->target) {
        return 0u;
    }
    return RenderTargetChecksum(desktop->target);
}

const uint8_t *DesktopFrameBytes(const Desktop *desktop, size_t *outSize)
{
    if (outSize) {
        *outSize = 0u;
    }
    if (!desktop || !desktop->target) {
        return NULL;
    }
    return RoboRenderTargetBytes(desktop->target, outSize);
}

/* ------------------------------------------------------------------ */
/* Resize                                                              */
/* ------------------------------------------------------------------ */

Result DesktopResize(Desktop *desktop,
                                 int32_t width,
                                 int32_t height,
                                 DesktopDiag *diag)
{
    RenderTarget *target = NULL;
    DesktopGeometry geometry;
    size_t index;
    int32_t oldWidth;
    int32_t oldHeight;

    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "a desktop is required");
    }
    /* Captured before anything is replaced: a resize log that read the geometry after the assignment
     * would report the new size twice and say nothing about what changed. */
    oldWidth = desktop->geometry.screen.width;
    oldHeight = desktop->geometry.screen.height;
    if (width <= 0 || height <= 0 || width > (int32_t)RoboDesktopMaxDimension ||
        height > (int32_t)RoboDesktopMaxDimension) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "the requested screen size is not one this build can make");
    }
    if (desktop->geometry.screen.width == width && desktop->geometry.screen.height == height) {
        /* Nothing to do, and deliberately nothing done: a caller polling a window's size must not force
         * a reallocation every iteration. */
        return RoboOk;
    }
    if (DesktopGeometryCompute(MakeGfxSize(width, height), &desktop->panel, &geometry) !=
        RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "the desktop's geometry could not be recomputed");
    }
    /* The new target first: if it cannot be allocated, the shell keeps the screen it had rather than
     * becoming a desktop with no pixels. */
    if (RenderTargetCreate(&target, (uint32_t)width, (uint32_t)height, NULL) != RoboOk ||
        !target) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboOutOfMemory,
                                 "the new screen target could not be allocated");
    }

    RenderTargetDestroy(desktop->target);
    desktop->target = target;
    desktop->geometry = geometry;
    /* A frame in flight is abandoned: its record described a screen that no longer exists. */
    desktop->frameOpen = 0;
    desktop->painted = 0;

    /*
     * Every window is re-clamped into the new work area and its content target released -- the *client*
     * size follows the frame size, so a window that survived a resize has a stale content target, and
     * letting the next paint recreate it is cheaper than recreating eight here (see the internal header).
     */
    for (index = 0u; index < desktop->windowCount; index++) {
        DesktopWindow *window = &desktop->windows[index];
        GfxRect clamped;

        DesktopWindowReleaseTarget(window);
        if (DesktopGeometryContain(window->bounds, &geometry.workArea, &clamped) == RoboOk) {
            window->bounds = clamped;
        }
    }
    desktop->stats.resizes++;
    LogDebug("robo_desktop: resized %dx%d -> %dx%d (windows_reclamped=%llu)", (int)oldWidth,
                   (int)oldHeight, (int)width, (int)height,
                   (unsigned long long)desktop->windowCount);
    return RoboOk;
}

Result DesktopSyncSize(Desktop *desktop,
                                    int *outResized,
                                    DesktopDiag *diag)
{
    GfxSize physical;
    Result status;

    if (outResized) {
        *outResized = 0;
    }
    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "a desktop is required");
    }
    if (desktop->window == RoboWindowIdNone) {
        return RoboDesktopDiag(diag, RoboDesktopDomainSurface, RoboNotSupported,
                                 "the desktop has no native window to follow");
    }
    if (WindowGetSizePhysical(desktop->window, &physical) != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainSurface, RoboError,
                                 "the window's physical size could not be read");
    }
    if (physical.width == desktop->geometry.screen.width &&
        physical.height == desktop->geometry.screen.height) {
        return RoboOk;
    }
    status = DesktopResize(desktop, physical.width, physical.height, diag);
    if (status != RoboOk) {
        return status;
    }
    if (outResized) {
        *outResized = 1;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Presentation                                                        */
/* ------------------------------------------------------------------ */

Result DesktopPresentToSurface(const Desktop *desktop,
                                              GfxSurface *dest,
                                              int32_t x,
                                              int32_t y,
                                              RenderPresentRecord *outRecord,
                                              DesktopDiag *diag)
{
    RenderDiag renderDiag;
    Result status;

    if (!desktop || !dest) {
        return RoboDesktopDiag(diag, RoboDesktopDomainSurface, RoboInvalidArgument,
                                 "a desktop and a destination surface are required");
    }
    if (!desktop->target) {
        return RoboDesktopDiag(diag, RoboDesktopDomainSurface, RoboNotFound,
                                 "the desktop has no screen target");
    }
    RenderDiagReset(&renderDiag);
    status = RenderPresent(desktop->target, dest, x, y, outRecord, &renderDiag);
    if (status != RoboOk) {
        /* The renderer's message travels: it names the stage that failed, and a desktop-level message
         * would only repeat "presentation failed" in place of the reason. */
        return RoboDesktopDiag(diag, RoboDesktopDomainSurface, status,
                                 renderDiag.message ? renderDiag.message
                                                     : "the frame could not be presented");
    }
    return RoboOk;
}

Result DesktopPresent(Desktop *desktop,
                                  RenderPresentRecord *outRecord,
                                  int *outPresented,
                                  DesktopDiag *diag)
{
    GfxSurface *surface = NULL;
    Result status;

    if (outPresented) {
        *outPresented = 0;
    }
    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainSurface, RoboInvalidArgument,
                                 "a desktop is required");
    }
    if (desktop->window == RoboWindowIdNone) {
        return RoboDesktopDiag(diag, RoboDesktopDomainSurface, RoboNotSupported,
                                 "the desktop has no native window to present into");
    }
    if (WindowBeginFrame(desktop->window, &surface) != RoboOk || !surface) {
        return RoboDesktopDiag(diag, RoboDesktopDomainSurface, RoboNotSupported,
                                 "the window could not give a render surface");
    }
    status = DesktopPresentToSurface(desktop, surface, 0, 0, outRecord, diag);
    if (status != RoboOk) {
        /* The window's frame is closed even on a failed copy, so the next iteration can begin another one
         * rather than being refused as nested. */
        (void)WindowEndFrame(desktop->window);
        return status;
    }
    if (WindowEndFrame(desktop->window) != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainSurface, RoboError,
                                 "the window's frame could not be ended");
    }
    if (WindowPresentFrame(desktop->window) != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainSurface, RoboError,
                                 "the window could not present the frame");
    }
    desktop->stats.presentations++;
    if (outPresented) {
        *outPresented = 1;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Hit testing and the pointer                                         */
/* ------------------------------------------------------------------ */

void DesktopHitInit(DesktopHit *hit)
{
    if (!hit) {
        return;
    }
    hit->area = RoboDesktopAreaNone;
    hit->window = RoboDesktopWindowIdNone;
    hit->point = MakeGfxPoint(0, 0);
    hit->inside = 0;
}

Result DesktopHitTest(const Desktop *desktop,
                                    GfxPoint point,
                                    DesktopHit *out)
{
    size_t index;

    if (!desktop || !out) {
        return RoboInvalidArgument;
    }
    DesktopHitInit(out);
    out->point = point;
    out->inside = GfxRectContainsPoint(desktop->geometry.screen, point) ? 1 : 0;

    /* Front to back: a point over two windows belongs to the front one, which is the answer a *user*
     * would give. */
    for (index = desktop->windowCount; index > 0u; index--) {
        const DesktopWindow *window = &desktop->windows[index - 1u];
        GfxRect closeBounds;
        GfxRect titleBounds;

        if (!window->visible || window->state != RoboDesktopWindowOpen) {
            continue;
        }
        if (!GfxRectContainsPoint(window->bounds, point)) {
            continue;
        }
        out->window = window->id;
        closeBounds = DesktopChromeCloseBounds(desktop, window);
        titleBounds = DesktopChromeTitleBounds(desktop, window);
        if (GfxRectContainsPoint(closeBounds, point)) {
            out->area = RoboDesktopAreaWindowClose;
        } else if (GfxRectContainsPoint(titleBounds, point)) {
            out->area = RoboDesktopAreaWindowTitle;
        } else {
            out->area = RoboDesktopAreaWindowContent;
        }
        return RoboOk;
    }

    if (GfxRectContainsPoint(desktop->geometry.panel, point)) {
        out->area = RoboDesktopAreaPanel;
    } else if (out->inside) {
        out->area = RoboDesktopAreaBackground;
    }
    return RoboOk;
}

Result DesktopObservePointer(Desktop *desktop,
                                           GfxPoint point,
                                           int pressed,
                                           DesktopDiag *diag)
{
    DesktopHit hit;
    Result status;

    if (!desktop) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "a desktop is required");
    }
    status = DesktopHitTest(desktop, point, &hit);
    if (status != RoboOk) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, status,
                                 "the pointer's position could not be resolved");
    }

    desktop->pointer.point = point;
    desktop->pointer.inside = hit.inside;
    desktop->pointer.area = hit.area;
    desktop->pointer.window = hit.window;
    desktop->pointer.pressed = pressed ? 1 : 0;

    if (!pressed) {
        desktop->stats.pointerMoves++;
        return RoboOk;
    }

    /*
     * A press, and the whole of this build's input: two actions, both about *windows*. A press that hit
     * nothing is a legitimate press and is a success -- a shell that reported an error for a click on the
     * desktop surface would be reporting an error for the most ordinary thing a user does.
     */
    switch (hit.area) {
    case RoboDesktopAreaWindowClose:
        if (DesktopWindowRequestClose(desktop, hit.window) == RoboOk) {
            desktop->stats.pointerActions++;
        }
        break;
    case RoboDesktopAreaWindowTitle:
    case RoboDesktopAreaWindowContent:
        if (DesktopWindowRaise(desktop, hit.window) == RoboOk) {
            desktop->stats.pointerActions++;
        }
        break;
    default:
        /* The panel and the desktop surface: a shell with a launcher would act here. This build has no
         * launcher, and inventing an action for a click on an empty panel would be inventing a feature. */
        break;
    }
    return RoboOk;
}

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */

Result GetDesktopStats(const Desktop *desktop, DesktopStats *out)
{
    if (!desktop || !out) {
        return RoboInvalidArgument;
    }
    *out = desktop->stats;
    return RoboOk;
}

Result GetDesktopPointer(const Desktop *desktop, DesktopPointer *out)
{
    if (!desktop || !out) {
        return RoboInvalidArgument;
    }
    *out = desktop->pointer;
    return RoboOk;
}
