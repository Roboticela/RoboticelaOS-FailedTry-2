/*
 * RoboDesktop ↔ RoboCompositor wiring.
 *
 * The shell decides *what* is on screen (wallpaper, panel, windows). The compositor
 * decides *how little* of it a frame has to rewrite: retained surfaces, composition
 * damage, and a region present. A click that only raises a window no longer clears
 * the desktop or re-shapes panel labels.
 */

#include "RoboDesktopInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboCompositor/RoboCompositor.h"
#include "RoboCompositor/RoboCompositorDamage.h"
#include "RoboCompositor/RoboCompositorFrame.h"
#include "RoboCompositor/RoboCompositorLayer.h"
#include "RoboCompositor/RoboCompositorSurface.h"
#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsRaster.h"
#include "RoboMem/RoboMem.h"
#include "RoboRender/RoboRenderRasterizer.h"

#include <string.h>

#define RoboDesktopSurfWallpaper 1u
#define RoboDesktopSurfPanel 2u
#define RoboDesktopSurfWindowBase 1000u

#define RoboDesktopLayerWallpaper 1u
#define RoboDesktopLayerPanel 2u
#define RoboDesktopLayerWindowBase 1000u

static CompositorSurfaceId WindowSurfaceId(DesktopWindowId id)
{
    return (CompositorSurfaceId)(RoboDesktopSurfWindowBase + (uint64_t)id);
}

static CompositorLayerId WindowLayerId(DesktopWindowId id)
{
    return (CompositorLayerId)(RoboDesktopLayerWindowBase + (uint64_t)id);
}

static void ComposeMarkPresentDamage(Desktop *desktop, GfxRect rect)
{
    GfxRect unioned;

    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    if (desktop->presentDamage.width <= 0 || desktop->presentDamage.height <= 0) {
        desktop->presentDamage = rect;
        return;
    }
    if (GfxRectBounding(desktop->presentDamage, rect, &unioned) == RoboOk) {
        desktop->presentDamage = unioned;
    } else {
        desktop->presentDamage = desktop->geometry.screen;
    }
}

void DesktopComposeInvalidateAll(Desktop *desktop)
{
    if (!desktop) {
        return;
    }
    desktop->wallpaperDirty = 1;
    desktop->panelDirty = 1;
    desktop->windowOrderDirty = 1;
    desktop->presentDamage = GfxRectEmpty();
}

void DesktopComposeInvalidatePanel(Desktop *desktop)
{
    if (desktop) {
        desktop->panelDirty = 1;
    }
}

void DesktopComposeInvalidateWindowOrder(Desktop *desktop)
{
    if (desktop) {
        desktop->windowOrderDirty = 1;
    }
}

void DesktopComposeDestroy(Desktop *desktop)
{
    if (!desktop || !desktop->compositor) {
        return;
    }
    CompositorDestroy(desktop->compositor);
    desktop->compositor = NULL;
    desktop->wallpaperDirty = 1;
    desktop->panelDirty = 1;
    desktop->windowOrderDirty = 1;
    desktop->presentDamage = GfxRectEmpty();
}

static Result ComposeEnsure(Desktop *desktop, DesktopDiag *diag)
{
    CompositorConfig config;
    CompositorDiag cdiag;
    Result status;

    if (desktop->compositor) {
        return RoboOk;
    }

    CompositorConfigInit(&config);
    config.size = MakeGfxSize(desktop->geometry.screen.width, desktop->geometry.screen.height);
    config.format = RoboRenderTargetFormat;
    config.damagePresent = 1;
    config.maxLayers = (unsigned)(8u + RoboDesktopMaxWindows * 2u);
    config.maxSurfaces = (unsigned)(8u + RoboDesktopMaxWindows * 2u);
    config.maxDamageRects = 32u;

    CompositorDiagReset(&cdiag);
    status = CompositorCreate(&config, &desktop->compositor, &cdiag);
    if (status != RoboOk || !desktop->compositor) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, status,
                                 cdiag.message ? cdiag.message
                                               : "the shell compositor could not be created");
    }

    status = CompositorLayerCreate(desktop->compositor, RoboDesktopLayerWallpaper,
                                   RoboCompositorLayerNone, &cdiag);
    if (status != RoboOk) {
        DesktopComposeDestroy(desktop);
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, status,
                                 "the wallpaper layer could not be created");
    }
    status = CompositorLayerCreate(desktop->compositor, RoboDesktopLayerPanel,
                                   RoboCompositorLayerNone, &cdiag);
    if (status != RoboOk) {
        DesktopComposeDestroy(desktop);
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, status,
                                 "the panel layer could not be created");
    }

    DesktopComposeInvalidateAll(desktop);
    LogDebug("robo_desktop: compositor attached %dx%d (damage present)",
                   (int)config.size.width, (int)config.size.height);
    return RoboOk;
}

static Result ComposeFillWallpaper(Desktop *desktop,
                                           DesktopFrameRecord *record,
                                           DesktopDiag *diag)
{
    GfxSurface *surface = NULL;
    GfxRaster *raster = NULL;
    CompositorDiag cdiag;
    GfxSize size;
    Result status;

    size = MakeGfxSize(desktop->geometry.screen.width, desktop->geometry.screen.height);
    CompositorDiagReset(&cdiag);
    status = CompositorSurfaceAcquire(desktop->compositor, RoboDesktopSurfWallpaper, size,
                                      RoboRenderTargetFormat, &surface, &cdiag);
    if (status != RoboOk || !surface) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, status,
                                 "the wallpaper surface could not be acquired");
    }
    if (GfxRasterCreate(surface, &raster) != RoboOk || !raster) {
        (void)CompositorSurfaceRelease(desktop->compositor, RoboDesktopSurfWallpaper, NULL);
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboError,
                                 "the wallpaper surface could not be opened");
    }
    (void)DesktopChromePaintWallpaper(desktop, raster, record, diag);
    GfxRasterDestroy(raster);

    (void)CompositorLayerSetRect(desktop->compositor, RoboDesktopLayerWallpaper,
                                 desktop->geometry.screen, NULL);
    (void)CompositorLayerSetSurface(desktop->compositor, RoboDesktopLayerWallpaper,
                                    RoboDesktopSurfWallpaper, NULL);
    (void)CompositorDamageAdd(desktop->compositor, RoboCompositorSurfaceNone,
                              desktop->geometry.screen, NULL);
    ComposeMarkPresentDamage(desktop, desktop->geometry.screen);
    (void)CompositorSurfaceRelease(desktop->compositor, RoboDesktopSurfWallpaper, NULL);
    desktop->wallpaperDirty = 0;
    return RoboOk;
}

static Result ComposeFillPanel(Desktop *desktop,
                                       DesktopFrameRecord *record,
                                       DesktopDiag *diag)
{
    GfxSurface *surface = NULL;
    GfxRaster *raster = NULL;
    CompositorDiag cdiag;
    GfxRect panel = desktop->geometry.panel;
    GfxSize size;
    Result status;

    if (panel.width <= 0 || panel.height <= 0) {
        desktop->panelDirty = 0;
        return RoboOk;
    }
    size = MakeGfxSize(panel.width, panel.height);
    CompositorDiagReset(&cdiag);
    status = CompositorSurfaceAcquire(desktop->compositor, RoboDesktopSurfPanel, size,
                                      RoboRenderTargetFormat, &surface, &cdiag);
    if (status != RoboOk || !surface) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, status,
                                 "the panel surface could not be acquired");
    }
    if (GfxRasterCreate(surface, &raster) != RoboOk || !raster) {
        (void)CompositorSurfaceRelease(desktop->compositor, RoboDesktopSurfPanel, NULL);
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboError,
                                 "the panel surface could not be opened");
    }
    (void)DesktopChromePaintPanel(desktop, raster, record, diag);
    GfxRasterDestroy(raster);

    (void)CompositorLayerSetRect(desktop->compositor, RoboDesktopLayerPanel, panel, NULL);
    (void)CompositorLayerSetSurface(desktop->compositor, RoboDesktopLayerPanel,
                                    RoboDesktopSurfPanel, NULL);
    (void)CompositorDamageAdd(desktop->compositor, RoboCompositorSurfaceNone, panel, NULL);
    ComposeMarkPresentDamage(desktop, panel);
    (void)CompositorSurfaceRelease(desktop->compositor, RoboDesktopSurfPanel, NULL);
    desktop->panelDirty = 0;
    return RoboOk;
}

static Result ComposePaintWindow(Desktop *desktop,
                                         DesktopWindow *window,
                                         int focused,
                                         DesktopFrameRecord *record,
                                         RenderDiag *renderDiag,
                                         DesktopDiag *diag)
{
    GfxSurface *surface = NULL;
    GfxRaster *raster = NULL;
    DesktopWindow local;
    CompositorDiag cdiag;
    CompositorSurfaceId sid;
    CompositorLayerId lid;
    GfxSize size;
    GfxRect client;
    Result status;

    if (!window->visible || window->state != RoboDesktopWindowOpen) {
        return RoboOk;
    }
    if (window->bounds.width <= 0 || window->bounds.height <= 0) {
        return RoboOk;
    }

    sid = WindowSurfaceId(window->id);
    lid = WindowLayerId(window->id);
    size = MakeGfxSize(window->bounds.width, window->bounds.height);

    CompositorDiagReset(&cdiag);
    status = CompositorLayerCreate(desktop->compositor, lid, RoboCompositorLayerNone, &cdiag);
    if (status != RoboOk && status != RoboAlreadyExists) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, status,
                                 "a window layer could not be created");
    }

    status = CompositorSurfaceAcquire(desktop->compositor, sid, size, RoboRenderTargetFormat,
                                      &surface, &cdiag);
    if (status != RoboOk || !surface) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, status,
                                 "a window surface could not be acquired");
    }
    if (GfxRasterCreate(surface, &raster) != RoboOk || !raster) {
        (void)CompositorSurfaceRelease(desktop->compositor, sid, NULL);
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboError,
                                 "a window surface could not be opened");
    }

    local = *window;
    local.bounds = MakeGfxRect(0, 0, size.width, size.height);
    (void)DesktopChromePaintWindowFrame(desktop, &local, focused, raster, record, diag);
    client = DesktopChromeClientBounds(desktop, &local);
    status = DesktopPaintWindowContent(desktop, window, client, raster, record, renderDiag);
    DesktopChromePaintCloseButton(desktop, &local, raster, record);
    DesktopChromePaintWindowTitle(desktop, &local, raster, record);
    GfxRasterDestroy(raster);

    (void)CompositorLayerSetRect(desktop->compositor, lid, window->bounds, NULL);
    (void)CompositorLayerSetSurface(desktop->compositor, lid, sid, NULL);
    (void)CompositorLayerSetVisible(desktop->compositor, lid, 1, NULL);
    (void)CompositorDamageAdd(desktop->compositor, RoboCompositorSurfaceNone, window->bounds,
                              NULL);
    ComposeMarkPresentDamage(desktop, window->bounds);
    (void)CompositorSurfaceRelease(desktop->compositor, sid, NULL);
    record->windows++;
    return status;
}

/*
 * Rebuild window layer stacking to match the shell's z-order (creation order =
 * paint order). Destroy and recreate so the front window is the last root.
 */
static Result ComposeSyncWindowOrder(Desktop *desktop, DesktopDiag *diag)
{
    size_t index;
    CompositorDiag cdiag;

    if (!desktop->windowOrderDirty) {
        return RoboOk;
    }

    for (index = 0u; index < desktop->windowCount; index++) {
        DesktopWindow *window = &desktop->windows[index];
        CompositorLayerId lid = WindowLayerId(window->id);
        size_t removed = 0u;

        CompositorDiagReset(&cdiag);
        (void)CompositorLayerDestroy(desktop->compositor, lid, &removed, &cdiag);
        window->frameDirty = 1;
    }
    desktop->windowOrderDirty = 0;
    (void)diag;
    return RoboOk;
}

Result DesktopComposePaint(Desktop *desktop,
                                   DesktopFrameRecord *record,
                                   DesktopDiag *diag)
{
    GfxSurface *screen = NULL;
    CompositorFrame frame;
    CompositorDiag cdiag;
    RenderDiag renderDiag;
    const char *contentFailure = NULL;
    size_t index;
    Result status;
    int anyDirty;

    RenderDiagReset(&renderDiag);
    if (!desktop || !record) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboInvalidArgument,
                                 "a desktop and a frame record are required");
    }
    if (!desktop->target) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboNotFound,
                                 "the desktop has no screen target");
    }

    status = ComposeEnsure(desktop, diag);
    if (status != RoboOk) {
        return status;
    }

    desktop->presentDamage = GfxRectEmpty();
    (void)ComposeSyncWindowOrder(desktop, diag);

    anyDirty = desktop->wallpaperDirty || desktop->panelDirty;
    for (index = 0u; index < desktop->windowCount; index++) {
        if (desktop->windows[index].contentDirty ||
            desktop->windows[index].frameDirty) {
            anyDirty = 1;
            break;
        }
    }

    if (desktop->wallpaperDirty) {
        status = ComposeFillWallpaper(desktop, record, diag);
        if (status != RoboOk) {
            return status;
        }
    }
    if (desktop->panelDirty) {
        status = ComposeFillPanel(desktop, record, diag);
        if (status != RoboOk) {
            return status;
        }
    }

    for (index = 0u; index < desktop->windowCount; index++) {
        DesktopWindow *window = &desktop->windows[index];
        int focused = (desktop->focusedIndex == index) ? 1 : 0;
        CompositorLayerId lid = WindowLayerId(window->id);
        int needPaint = window->contentDirty || window->frameDirty;
        CompositorLayerInfo info;

        if (!window->visible || window->state != RoboDesktopWindowOpen) {
            if (CompositorLayerGetInfo(desktop->compositor, lid, &info) == RoboOk) {
                (void)CompositorLayerSetVisible(desktop->compositor, lid, 0, NULL);
                (void)CompositorDamageAdd(desktop->compositor, RoboCompositorSurfaceNone,
                                          window->bounds, NULL);
                ComposeMarkPresentDamage(desktop, window->bounds);
            }
            continue;
        }

        if (!needPaint && CompositorLayerGetInfo(desktop->compositor, lid, &info) == RoboOk) {
            /* Geometry may still have moved. */
            if (!GfxRectEqual(info.rect, window->bounds)) {
                (void)CompositorDamageAdd(desktop->compositor, RoboCompositorSurfaceNone,
                                          info.rect, NULL);
                ComposeMarkPresentDamage(desktop, info.rect);
                (void)CompositorLayerSetRect(desktop->compositor, lid, window->bounds, NULL);
                (void)CompositorDamageAdd(desktop->compositor, RoboCompositorSurfaceNone,
                                          window->bounds, NULL);
                ComposeMarkPresentDamage(desktop, window->bounds);
            }
            record->windows++;
            continue;
        }

        status = ComposePaintWindow(desktop, window, focused, record, &renderDiag, diag);
        if (status == RoboOk) {
            window->contentDirty = 0;
            window->frameDirty = 0;
        } else if (!contentFailure) {
            contentFailure = renderDiag.message;
        }
    }

    screen = RenderTargetSurface(desktop->target);
    if (!screen) {
        return RoboDesktopDiag(diag, RoboDesktopDomainShell, RoboError,
                                 "the screen target has no pixels");
    }

    CompositorDiagReset(&cdiag);
    if (anyDirty || CompositorDamageRectCount(desktop->compositor) > 0u ||
        CompositorDamageIsFull(desktop->compositor)) {
        CompositorPresentKind kind = CompositorDamageIsFull(desktop->compositor)
                                             ? RoboCompositorPresentFull
                                             : RoboCompositorPresentDamage;
        status = CompositorPresent(desktop->compositor, screen, kind, &frame, &cdiag);
        if (status != RoboOk) {
            return RoboDesktopDiag(diag, RoboDesktopDomainShell, status,
                                     cdiag.message ? cdiag.message
                                                   : "the shell compositor could not present");
        }
        if (kind == RoboCompositorPresentFull ||
            desktop->presentDamage.width <= 0 || desktop->presentDamage.height <= 0) {
            desktop->presentDamage = desktop->geometry.screen;
        }
        LogDebug("robo_desktop: compose %s pixels=%llu layers=%u",
                       kind == RoboCompositorPresentFull ? "full" : "damage",
                       (unsigned long long)frame.compositedPixels,
                       (unsigned)frame.layersPainted);
    }

    /* Software cursor stays an overlay on the composed frame. */
    {
        GfxRaster *raster = NULL;
        if (GfxRasterCreate(screen, &raster) == RoboOk && raster) {
            desktop->cursorOverlayValid = 0;
            if (desktop->showCursor && desktop->cursorVisible) {
                GfxRect under =
                    DesktopChromeCursorBoundsFor(desktop, desktop->pointer.point);
                if (under.width > 0 && under.height > 0 &&
                    DesktopCursorCapture(desktop, screen, under) == RoboOk) {
                    desktop->cursorDrawnAt = desktop->pointer.point;
                    DesktopChromeDrawCursorFor(desktop, screen, desktop->pointer.point);
                    ComposeMarkPresentDamage(desktop, under);
                } else {
                    DesktopChromePaintCursor(desktop, raster, record);
                }
            }
            GfxRasterDestroy(raster);
        }
    }

    desktop->shellRasterValid = 1;
    if (contentFailure) {
        return RoboDesktopDiag(diag, RoboDesktopDomainWindow, RoboNotSupported, contentFailure);
    }
    return RoboOk;
}
