/*
 * RoboWindow owned render surface + frame lifecycle (Goal 7.14).
 */

#include "RoboWindow/RoboWindowGraphics.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindow/RoboWindowPresent.h"

#include <string.h>

#define RoboWindowRenderMaxEdge 8192u

void WindowRenderInitSlot(WindowSlot *slot)
{
    if (!slot) {
        return;
    }
    slot->renderSurface = NULL;
    slot->renderDirty = 0;
    slot->renderDirtyFull = 0;
    slot->renderDirtyRect = MakeGfxRect(0, 0, 0, 0);
    slot->renderFrameOpen = 0;
    slot->renderStale = 0;
    slot->renderFormat = RoboGfxFormatBgra8;
}

void WindowRenderTeardown(WindowSlot *slot)
{
    if (!slot) {
        return;
    }
    if (slot->renderSurface) {
        GfxSurfaceRelease(slot->renderSurface);
        slot->renderSurface = NULL;
    }
    slot->renderDirty = 0;
    slot->renderDirtyFull = 0;
    slot->renderDirtyRect = MakeGfxRect(0, 0, 0, 0);
    slot->renderFrameOpen = 0;
    slot->renderStale = 0;
}

void WindowRenderMarkStale(WindowSlot *slot)
{
    if (!slot) {
        return;
    }
    slot->renderStale = 1;
    /* Stale surface must not be presented; force recreate before draw. */
    if (slot->renderSurface) {
        slot->renderDirty = 1;
        slot->renderDirtyFull = 1;
    }
}

static int FormatIsPresentable(GfxPixelFormat fmt)
{
    return fmt == RoboGfxFormatBgra8 || fmt == RoboGfxFormatRgba8 ||
           fmt == RoboGfxFormatRgb8;
}

static Result PhysicalDims(WindowId id, uint32_t *outW,
                                   uint32_t *outH)
{
    GfxSize phys;
    Result r;

    r = WindowGetSizePhysical(id, &phys);
    if (r != RoboOk) {
        return r;
    }
    if (phys.width <= 0 || phys.height <= 0) {
        return RoboInvalidArgument;
    }
    if ((uint32_t)phys.width > RoboWindowRenderMaxEdge ||
        (uint32_t)phys.height > RoboWindowRenderMaxEdge) {
        return RoboInvalidArgument;
    }
    *outW = (uint32_t)phys.width;
    *outH = (uint32_t)phys.height;
    return RoboOk;
}

static int SurfaceMatches(const GfxSurface *surf, uint32_t w,
                           uint32_t h, GfxPixelFormat fmt)
{
    if (!surf) {
        return 0;
    }
    return GfxSurfaceWidth(surf) == w &&
           GfxSurfaceHeight(surf) == h &&
           GfxSurfaceFormat(surf) == fmt;
}

static Result RecreateOwned(WindowSlot *slot, uint32_t w,
                                    uint32_t h)
{
    GfxSurfaceDesc desc;
    GfxSurface *surf = NULL;
    Result r;

    GfxSurfaceDescInit(&desc);
    desc.width = w;
    desc.height = h;
    desc.format = slot->renderFormat;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    r = GfxSurfaceCreate(&desc, &surf);
    if (r != RoboOk) {
        LogError("robo_window.gfx: surface create %ux%u fmt=%d failed (%d)",
                       (unsigned)w, (unsigned)h, (int)slot->renderFormat,
                       (int)r);
        return r;
    }
    if (slot->renderSurface) {
        GfxSurfaceRelease(slot->renderSurface);
        slot->renderSurface = NULL;
    }
    slot->renderSurface = surf;
    slot->renderStale = 0;
    slot->renderDirty = 1;
    slot->renderDirtyFull = 1;
    slot->renderDirtyRect = MakeGfxRect(0, 0, (int32_t)w, (int32_t)h);
    LogDebug("robo_window.gfx: owned surface id=%llu %ux%u fmt=%d",
                   (unsigned long long)slot->id, (unsigned)w, (unsigned)h,
                   (int)slot->renderFormat);
    return RoboOk;
}

Result WindowEnsureRenderSurface(WindowId id)
{
    WindowSlot *slot;
    uint32_t w = 0u;
    uint32_t h = 0u;
    Result r;

    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = WindowRequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (slot->renderFrameOpen) {
        /* Allow ensure during frame only if dimensions already match. */
    }
    r = PhysicalDims(id, &w, &h);
    if (r != RoboOk) {
        return r;
    }
    if (!slot->renderStale &&
        SurfaceMatches(slot->renderSurface, w, h, slot->renderFormat)) {
        return RoboOk;
    }
    if (slot->renderFrameOpen && slot->renderStale) {
        /* Recreate invalidates the caller's draw target mid-frame. */
        LogWarn("robo_window.gfx: recreate while frame open id=%llu",
                      (unsigned long long)id);
    }
    return RecreateOwned(slot, w, h);
}

GfxSurface *WindowGetRenderSurface(WindowId id)
{
    WindowSlot *slot = WindowSlotFind(id);

    if (!slot || !WindowIdIsLive(id)) {
        return NULL;
    }
    return slot->renderSurface;
}

Result WindowReleaseRenderSurface(WindowId id)
{
    WindowSlot *slot;
    Result r;

    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = WindowRequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (slot->renderFrameOpen) {
        return RoboBusy;
    }
    WindowRenderTeardown(slot);
    return RoboOk;
}

Result WindowSetRenderFormat(WindowId id,
                                            GfxPixelFormat format)
{
    WindowSlot *slot;
    Result r;

    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = WindowRequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (!FormatIsPresentable(format)) {
        return RoboNotSupported;
    }
    if (slot->renderFormat == format) {
        return RoboOk;
    }
    slot->renderFormat = format;
    WindowRenderMarkStale(slot);
    return RoboOk;
}

Result WindowGetRenderFormat(WindowId id,
                                            GfxPixelFormat *out)
{
    WindowSlot *slot;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = WindowSlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    *out = slot->renderFormat;
    return RoboOk;
}

Result WindowGetRenderSize(WindowId id,
                                          GfxSize *out)
{
    WindowSlot *slot;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = WindowSlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    if (!slot->renderSurface) {
        out->width = 0;
        out->height = 0;
        return RoboOk;
    }
    out->width = (int32_t)GfxSurfaceWidth(slot->renderSurface);
    out->height = (int32_t)GfxSurfaceHeight(slot->renderSurface);
    return RoboOk;
}

Result WindowBeginFrame(WindowId id,
                                      GfxSurface **outSurface)
{
    WindowSlot *slot;
    Result r;

    if (!outSurface) {
        return RoboInvalidArgument;
    }
    /* Leave *outSurface unchanged on failure (nested begin must not wipe). */
    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = WindowRequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (slot->renderFrameOpen) {
        return RoboBusy;
    }
    r = WindowEnsureRenderSurface(id);
    if (r != RoboOk) {
        return r;
    }
    if (!slot->renderSurface) {
        return RoboIoError;
    }
    slot->renderFrameOpen = 1;
    *outSurface = slot->renderSurface;
    LogDebug("robo_window.gfx: begin_frame id=%llu %ux%u",
                   (unsigned long long)id,
                   (unsigned)GfxSurfaceWidth(slot->renderSurface),
                   (unsigned)GfxSurfaceHeight(slot->renderSurface));
    return RoboOk;
}

Result WindowEndFrame(WindowId id)
{
    WindowSlot *slot;
    Result r;

    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = WindowRequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (!slot->renderFrameOpen) {
        return RoboInvalidArgument;
    }
    slot->renderFrameOpen = 0;
    slot->renderDirty = 1;
    slot->renderDirtyFull = 1;
    if (slot->renderSurface) {
        slot->renderDirtyRect = MakeGfxRect(
            0, 0, (int32_t)GfxSurfaceWidth(slot->renderSurface),
            (int32_t)GfxSurfaceHeight(slot->renderSurface));
    }
    LogDebug("robo_window.gfx: end_frame id=%llu dirty=1",
                   (unsigned long long)id);
    return RoboOk;
}

Result WindowEndFrameRegion(WindowId id, GfxRect region)
{
    WindowSlot *slot;
    Result r;
    GfxRect unioned;

    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = WindowRequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (!slot->renderFrameOpen) {
        return RoboInvalidArgument;
    }
    if (region.width <= 0 || region.height <= 0) {
        return RoboInvalidArgument;
    }
    slot->renderFrameOpen = 0;
    slot->renderDirty = 1;
    if (slot->renderDirtyFull) {
        /* A prior full dirty stays full. */
        return RoboOk;
    }
    if (slot->renderDirtyRect.width > 0 && slot->renderDirtyRect.height > 0) {
        if (GfxRectBounding(slot->renderDirtyRect, region, &unioned) == RoboOk) {
            slot->renderDirtyRect = unioned;
        } else {
            slot->renderDirtyFull = 1;
            slot->renderDirtyRect = region;
        }
    } else {
        slot->renderDirtyRect = region;
    }
    return RoboOk;
}

Result WindowPresentFrame(WindowId id)
{
    WindowSlot *slot;
    Result r;
    const WindowBackendVtbl *vtbl;

    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = WindowRequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (slot->renderFrameOpen) {
        return RoboBusy;
    }
    if (!slot->renderDirty) {
        return RoboOk;
    }
    if (slot->renderStale || !slot->renderSurface) {
        r = WindowEnsureRenderSurface(id);
        if (r != RoboOk) {
            return r;
        }
    }
    if (!slot->renderSurface) {
        return RoboIoError;
    }

    vtbl = GetWindowBackendVtbl();
    if (!slot->renderDirtyFull && slot->renderDirtyRect.width > 0 &&
        slot->renderDirtyRect.height > 0 && vtbl && vtbl->PresentSurfaceRegion) {
        r = vtbl->PresentSurfaceRegion(WindowBackendUserdata(), id, &slot->native,
                                               slot->renderSurface, slot->renderDirtyRect);
    } else {
        r = WindowPresent(id, slot->renderSurface);
    }
    if (r != RoboOk) {
        return r;
    }
    slot->renderDirty = 0;
    slot->renderDirtyFull = 0;
    slot->renderDirtyRect = MakeGfxRect(0, 0, 0, 0);
    return RoboOk;
}

Result WindowPresentRegion(WindowId id,
                                          GfxSurface *surface,
                                          GfxRect region)
{
    WindowSlot *slot;
    const WindowBackendVtbl *vtbl;
    WindowBackendCaps caps;
    Result r;

    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = WindowRequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (!surface) {
        return RoboInvalidArgument;
    }
    if (region.width <= 0 || region.height <= 0) {
        return RoboOk;
    }
    if (WindowBackendCapabilities(&caps) != RoboOk || !caps.present) {
        return RoboNotSupported;
    }
    r = WindowPresentValidateSurface(surface, &caps);
    if (r != RoboOk) {
        return r;
    }
    vtbl = GetWindowBackendVtbl();
    if (!vtbl) {
        return RoboNotSupported;
    }
    if (vtbl->PresentSurfaceRegion) {
        return vtbl->PresentSurfaceRegion(WindowBackendUserdata(), id, &slot->native,
                                                  surface, region);
    }
    /* No damage path: present the whole surface. */
    return WindowPresent(id, surface);
}

Result WindowInvalidate(WindowId id)
{
    WindowSlot *slot;
    Result r;

    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = WindowRequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    slot->renderDirty = 1;
    slot->renderDirtyFull = 1;
    if (slot->renderSurface) {
        slot->renderDirtyRect = MakeGfxRect(
            0, 0, (int32_t)GfxSurfaceWidth(slot->renderSurface),
            (int32_t)GfxSurfaceHeight(slot->renderSurface));
    }
    return RoboOk;
}

Result WindowInvalidateRect(WindowId id,
                                          GfxRect rect)
{
    WindowSlot *slot;
    Result r;
    GfxRect unioned;

    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = WindowRequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (rect.width <= 0 || rect.height <= 0) {
        return RoboInvalidArgument;
    }
    slot->renderDirty = 1;
    if (slot->renderDirtyFull) {
        return RoboOk;
    }
    if (slot->renderDirtyRect.width > 0 && slot->renderDirtyRect.height > 0) {
        if (GfxRectBounding(slot->renderDirtyRect, rect, &unioned) == RoboOk) {
            slot->renderDirtyRect = unioned;
        } else {
            slot->renderDirtyFull = 1;
        }
    } else {
        slot->renderDirtyRect = rect;
    }
    return RoboOk;
}

int WindowIsDirty(WindowId id)
{
    WindowSlot *slot = WindowSlotFind(id);

    if (!slot || !WindowIdIsLive(id)) {
        return 0;
    }
    return slot->renderDirty ? 1 : 0;
}

Result WindowClearDirty(WindowId id)
{
    WindowSlot *slot;
    Result r;

    r = WindowRequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = WindowRequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    slot->renderDirty = 0;
    slot->renderDirtyFull = 0;
    slot->renderDirtyRect = MakeGfxRect(0, 0, 0, 0);
    return RoboOk;
}

int WindowFrameIsOpen(WindowId id)
{
    WindowSlot *slot = WindowSlotFind(id);

    if (!slot || !WindowIdIsLive(id)) {
        return 0;
    }
    return slot->renderFrameOpen ? 1 : 0;
}
