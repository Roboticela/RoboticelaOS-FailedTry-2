#include "RoboWindow/RoboWindow.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboUnicode/RoboUnicode.h"
#include "RoboWindow/RoboWindowKeyboard.h"
#include "RoboWindow/RoboWindowMouse.h"
#include "RoboWindow/RoboWindowSize.h"

#include <string.h>

static int gInitialized;
static uint64_t gNextId = 1u;
static WindowSlot gSlots[RoboWindowMaxWindows];
/** Depth while applying SetSize / SetPosition / show / hide / focus. */
static int gGeometryApplyDepth;

const char *WindowVersionString(void)
{
    return RoboWindowVersionString;
}

int WindowIsInitialized(void)
{
    return gInitialized;
}

Result WindowRequireReady(void)
{
    Result r;

    if (!gInitialized) {
        return RoboInitError;
    }
    r = WindowThreadRequireOwner();
    return r;
}

static Result RequireReady(void)
{
    return WindowRequireReady();
}

static size_t TitleByteLen(const char *src)
{
    size_t n = 0u;
    if (!src) {
        return 0u;
    }
    while (src[n] != '\0') {
        n++;
    }
    return n;
}

static Result CopyTitleUtf8(char *dst, size_t dstSz, const char *src)
{
    size_t len;
    size_t i;

    if (!dst || dstSz == 0u) {
        return RoboInvalidArgument;
    }
    if (!src) {
        dst[0] = '\0';
        return RoboOk;
    }
    len = TitleByteLen(src);
    if (!UnicodeUtf8Validate(src, len)) {
        return RoboParseError;
    }
    if (len + 1u > dstSz) {
        len = dstSz - 1u;
        /* Truncate only on complete code points: walk back if needed. */
        while (len > 0u && ((unsigned char)src[len] & 0xC0u) == 0x80u) {
            len--;
        }
    }
    for (i = 0u; i < len; i++) {
        dst[i] = src[i];
    }
    dst[len] = '\0';
    return RoboOk;
}

WindowSlot *WindowSlotFind(WindowId id)
{
    size_t i;

    if (id == RoboWindowIdNone) {
        return NULL;
    }
    for (i = 0u; i < RoboWindowMaxWindows; i++) {
        if (gSlots[i].inUse && gSlots[i].id == id) {
            return &gSlots[i];
        }
    }
    return NULL;
}

static WindowSlot *SlotFind(WindowId id)
{
    return WindowSlotFind(id);
}

static WindowSlot *SlotAlloc(void)
{
    size_t i;

    for (i = 0u; i < RoboWindowMaxWindows; i++) {
        if (!gSlots[i].inUse) {
            memset(&gSlots[i], 0, sizeof(gSlots[i]));
            gSlots[i].inUse = 1;
            return &gSlots[i];
        }
    }
    return NULL;
}

static void EmitSimple(WindowEventType type, WindowId id)
{
    WindowEvent ev;

    WindowEventInit(&ev);
    ev.type = type;
    ev.windowId = id;
    (void)WindowEventPush(&ev);
}

static void EmitWithState(WindowEventType type,
                            const WindowSlot *slot)
{
    WindowEvent ev;

    WindowEventInit(&ev);
    ev.type = type;
    ev.windowId = slot->id;
    ev.x = slot->state.x;
    ev.y = slot->state.y;
    ev.width = slot->state.width;
    ev.height = slot->state.height;
    ev.scale = slot->state.scale;
    ev.displayId = slot->state.displayId;
    (void)WindowEventPush(&ev);
}

static int BackendReportsActual(void)
{
    WindowBackendCaps caps;

    if (WindowBackendCapabilities(&caps) != RoboOk) {
        return 0;
    }
    return caps.reportsActualState != 0;
}

static void SyncStateFromBackend(WindowSlot *slot)
{
    const WindowBackendVtbl *vtbl = GetWindowBackendVtbl();
    int32_t minW, minH, maxW, maxH;
    float prevScale;
    int lockScale;

    if (!vtbl || !vtbl->QueryState || !BackendReportsActual()) {
        return;
    }
    /* Preserve constraints — backends report geometry, not constraint fields. */
    minW = slot->state.minWidth;
    minH = slot->state.minHeight;
    maxW = slot->state.maxWidth;
    maxH = slot->state.maxHeight;
    prevScale = slot->state.scale;
    lockScale = slot->scaleFromPreference;
    (void)vtbl->QueryState(WindowBackendUserdata(), slot->id,
                            &slot->native, &slot->state);
    slot->state.minWidth = minW;
    slot->state.minHeight = minH;
    slot->state.maxWidth = maxW;
    slot->state.maxHeight = maxH;
    if (lockScale) {
        slot->state.scale = prevScale;
    }
}

static void ApplyConstraintsToState(WindowSlot *slot,
                                       const WindowSizeConstraints *c)
{
    slot->state.minWidth = c->minWidth;
    slot->state.minHeight = c->minHeight;
    slot->state.maxWidth = c->maxWidth;
    slot->state.maxHeight = c->maxHeight;
    slot->configSnapshot.minWidth = c->minWidth;
    slot->configSnapshot.minHeight = c->minHeight;
    slot->configSnapshot.maxWidth = c->maxWidth;
    slot->configSnapshot.maxHeight = c->maxHeight;
}

static void ConstraintsFromSlot(const WindowSlot *slot,
                                  WindowSizeConstraints *out)
{
    out->minWidth = slot->configSnapshot.minWidth;
    out->minHeight = slot->configSnapshot.minHeight;
    out->maxWidth = slot->configSnapshot.maxWidth;
    out->maxHeight = slot->configSnapshot.maxHeight;
}

/** Nearest valid size inside constraints (deterministic corrective resize). */
static void ClampSizeToConstraints(int32_t *w,
                                      int32_t *h,
                                      const WindowSizeConstraints *c)
{
    if (c->minWidth > 0 && *w < c->minWidth) {
        *w = c->minWidth;
    }
    if (c->minHeight > 0 && *h < c->minHeight) {
        *h = c->minHeight;
    }
    if (c->maxWidth > 0 && *w > c->maxWidth) {
        *w = c->maxWidth;
    }
    if (c->maxHeight > 0 && *h > c->maxHeight) {
        *h = c->maxHeight;
    }
}

static void DestroyAllWindows(void)
{
    size_t i;
    const WindowBackendVtbl *vtbl = GetWindowBackendVtbl();
    void *ud = WindowBackendUserdata();
    size_t n = 0u;

    for (i = 0u; i < RoboWindowMaxWindows; i++) {
        if (!gSlots[i].inUse) {
            continue;
        }
        n++;
        if (vtbl && vtbl->RoboDestroyWindow) {
            (void)vtbl->RoboDestroyWindow(ud, gSlots[i].id, &gSlots[i].native);
        }
        WindowNativeHandleInvalidate(&gSlots[i].native);
        gSlots[i].PresentSurface = NULL;
        WindowRenderTeardown(&gSlots[i]);
        gSlots[i].state.visible = 0;
        gSlots[i].state.focused = 0;
        gSlots[i].state.lifecycle = RoboWindowLifecycleClosed;
        gSlots[i].inUse = 0;
    }
    if (n > 0u) {
        LogWarn("robo_window: shutdown destroyed %llu live window(s)",
                      (unsigned long long)n);
    }
}

/** CREATED only — mutating ops. CLOSING → INVALID_ARGUMENT; missing → NOT_FOUND. */
Result WindowRequireMutableSlot(WindowId id,
                                               WindowSlot **out)
{
    WindowSlot *slot = SlotFind(id);
    if (!slot) {
        return RoboNotFound;
    }
    if (!WindowLifecycleAllowsMutation(slot->state.lifecycle)) {
        LogDebug("robo_window: mutation denied id=%llu life=%s",
                       (unsigned long long)id,
                       WindowLifecycleName(slot->state.lifecycle));
        return RoboInvalidArgument;
    }
    *out = slot;
    return RoboOk;
}

static Result RequireMutableSlot(WindowId id,
                                          WindowSlot **out)
{
    return WindowRequireMutableSlot(id, out);
}

Result WindowInit(void)
{
    if (gInitialized) {
        return RoboOk;
    }
    memset(gSlots, 0, sizeof(gSlots));
    gNextId = 1u;
    gGeometryApplyDepth = 0;
    WindowEventQueueReset();
    WindowEventLoopReset();
    KeyboardReset();
    MouseReset();
    WindowDisplaySetupPlaceholders();
    WindowThreadReset();
    gInitialized = 1;
    LogInfo("robo_window: init (version %s)", RoboWindowVersionString);
    return RoboOk;
}

void WindowShutdown(void)
{
    if (!gInitialized) {
        return;
    }
    (void)WindowEventLoopRequestStop();
    DestroyAllWindows();
    WindowBackendClear();
    KeyboardReset();
    MouseReset();
    WindowEventQueueReset();
    WindowEventLoopReset();
    WindowDisplayResetPlaceholders();
    WindowThreadReset();
    gGeometryApplyDepth = 0;
    gInitialized = 0;
    LogInfo("robo_window: shutdown");
}

Result WindowCreate(const WindowConfig *cfg,
                                 WindowId *outId)
{
    Result r;
    WindowSlot *slot;
    const WindowBackendVtbl *vtbl;
    void *ud;
    DisplayInfo disp;
    WindowBackendCaps caps;

    if (!cfg || !outId) {
        return RoboInvalidArgument;
    }
    *outId = RoboWindowIdNone;
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = WindowConfigValidate(cfg);
    if (r != RoboOk) {
        return r;
    }
    if (!WindowBackendIsRegistered()) {
        return RoboNotSupported;
    }
    if (WindowBackendCapabilities(&caps) != RoboOk ||
        !caps.RoboCreateWindow) {
        return RoboNotSupported;
    }
    vtbl = GetWindowBackendVtbl();
    ud = WindowBackendUserdata();
    slot = SlotAlloc();
    if (!slot) {
        return RoboBusy;
    }

    slot->id = gNextId++;
    if (slot->id == RoboWindowIdNone) {
        slot->id = gNextId++;
    }
    r = CopyTitleUtf8(slot->title, sizeof(slot->title), cfg->title);
    if (r != RoboOk) {
        slot->inUse = 0;
        return r;
    }
    slot->configSnapshot = *cfg;
    slot->configSnapshot.title = slot->title;

    WindowStateInit(&slot->state);
    r = WindowLifecycleCheckTransition(RoboWindowLifecycleUninitialized,
                                               RoboWindowLifecycleCreated);
    if (r != RoboOk) {
        slot->inUse = 0;
        return r;
    }
    slot->state.lifecycle = RoboWindowLifecycleCreated;
    slot->state.width = cfg->width;
    slot->state.height = cfg->height;
    slot->state.requestedWidth = cfg->width;
    slot->state.requestedHeight = cfg->height;
    slot->state.minWidth = cfg->minWidth;
    slot->state.minHeight = cfg->minHeight;
    slot->state.maxWidth = cfg->maxWidth;
    slot->state.maxHeight = cfg->maxHeight;
    slot->state.visible = 0;
    slot->state.focused = 0;
    slot->state.fullscreen = cfg->fullscreen ? 1 : 0;
    slot->scaleFromPreference = 0;
    if (cfg->scalePreference != 0.0f &&
        DisplayScaleValidate(cfg->scalePreference) == RoboOk) {
        slot->state.scale = cfg->scalePreference;
        slot->scaleFromPreference = 1;
    } else {
        slot->state.scale = 0.0f; /* resolve from display below */
    }
    if (cfg->x != RoboWindowCoordUnknown) {
        slot->state.x = cfg->x;
        slot->state.requestedX = cfg->x;
    }
    if (cfg->y != RoboWindowCoordUnknown) {
        slot->state.y = cfg->y;
        slot->state.requestedY = cfg->y;
    }
    if (cfg->displayId != RoboDisplayIdNone) {
        slot->state.displayId = cfg->displayId;
    } else if (DisplayPrimary(&disp) == RoboOk) {
        slot->state.displayId = disp.id;
    }
    if (!slot->scaleFromPreference &&
        slot->state.displayId != RoboDisplayIdNone) {
        float ds = 0.0f;
        if (DisplayGetScale(slot->state.displayId, &ds) == RoboOk) {
            slot->state.scale = ds;
        } else {
            slot->state.scale = 1.0f; /* architecture default when unknown */
        }
    } else if (!slot->scaleFromPreference) {
        slot->state.scale = 1.0f;
    }

    WindowRenderInitSlot(slot);

    WindowNativeHandleClear(&slot->native);
    r = vtbl->RoboCreateWindow(ud, slot->id, &slot->configSnapshot, &slot->native);
    if (r != RoboOk) {
        LogError("robo_window: backend create_window failed id=%llu (%d)",
                       (unsigned long long)slot->id, (int)r);
        slot->inUse = 0;
        return r;
    }

    SyncStateFromBackend(slot);
    EmitWithState(RoboWindowEventCreated, slot);
    LogDebug("robo_window: created id=%llu %dx%d backend=%s native=%s",
                   (unsigned long long)slot->id, (int)slot->state.width,
                   (int)slot->state.height, WindowBackendName(),
                   WindowNativeHandleIsValid(&slot->native) ? "yes"
                                                                     : "empty");

    *outId = slot->id;
    if (cfg->visible) {
        r = WindowShow(slot->id);
        if (r != RoboOk) {
            return r;
        }
    }
    return RoboOk;
}

Result WindowShow(WindowId id)
{
    WindowSlot *slot;
    Result r;
    const WindowBackendVtbl *vtbl;
    WindowBackendCaps caps;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = RequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (WindowBackendCapabilities(&caps) != RoboOk || !caps.showHide) {
        return RoboNotSupported;
    }
    vtbl = GetWindowBackendVtbl();
    if (!vtbl) {
        return RoboNotSupported;
    }
    gGeometryApplyDepth++;
    r = vtbl->show(WindowBackendUserdata(), id, &slot->native);
    gGeometryApplyDepth--;
    if (r != RoboOk) {
        return r;
    }
    if (!slot->state.visible) {
        slot->state.visible = 1;
        SyncStateFromBackend(slot);
        EmitSimple(RoboWindowEventShown, id);
        LogDebug("robo_window: shown id=%llu", (unsigned long long)id);
    }
    return RoboOk;
}

Result WindowHide(WindowId id)
{
    WindowSlot *slot;
    Result r;
    const WindowBackendVtbl *vtbl;
    WindowBackendCaps caps;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = RequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (WindowBackendCapabilities(&caps) != RoboOk || !caps.showHide) {
        return RoboNotSupported;
    }
    vtbl = GetWindowBackendVtbl();
    if (!vtbl) {
        return RoboNotSupported;
    }
    gGeometryApplyDepth++;
    r = vtbl->hide(WindowBackendUserdata(), id, &slot->native);
    gGeometryApplyDepth--;
    if (r != RoboOk) {
        return r;
    }
    if (slot->state.visible) {
        slot->state.visible = 0;
        SyncStateFromBackend(slot);
        EmitSimple(RoboWindowEventHidden, id);
        LogDebug("robo_window: hidden id=%llu", (unsigned long long)id);
    }
    return RoboOk;
}

Result WindowRequestClose(WindowId id)
{
    WindowSlot *slot;
    Result r;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot) {
        return RoboNotFound;
    }
    if (slot->state.lifecycle == RoboWindowLifecycleClosing) {
        LogDebug("robo_window: close already requested id=%llu",
                       (unsigned long long)id);
        return RoboAlreadyExists;
    }
    r = WindowLifecycleCheckTransition(slot->state.lifecycle,
                                               RoboWindowLifecycleClosing);
    if (r != RoboOk) {
        return r;
    }
    slot->state.lifecycle = RoboWindowLifecycleClosing;
    EmitSimple(RoboWindowEventCloseRequested, id);
    LogDebug("robo_window: close requested id=%llu",
                   (unsigned long long)id);
    return RoboOk;
}

Result WindowObserveCloseRequest(WindowId id)
{
    WindowSlot *slot;
    Result r;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot) {
        return RoboNotFound;
    }
    if (slot->state.lifecycle == RoboWindowLifecycleClosing) {
        /* Native may deliver repeated close messages — idempotent. */
        return RoboOk;
    }
    return WindowRequestClose(id);
}

Result WindowDestroy(WindowId id)
{
    WindowSlot *slot;
    Result r;
    const WindowBackendVtbl *vtbl;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot) {
        return RoboNotFound;
    }
    r = WindowLifecycleCheckTransition(slot->state.lifecycle,
                                               RoboWindowLifecycleClosed);
    if (r != RoboOk) {
        return r;
    }
    vtbl = GetWindowBackendVtbl();
    if (vtbl && vtbl->RoboDestroyWindow) {
        r = vtbl->RoboDestroyWindow(WindowBackendUserdata(), id,
                                 &slot->native);
        if (r != RoboOk) {
            LogError("robo_window: destroy_window failed id=%llu (%d)",
                           (unsigned long long)id, (int)r);
            return r;
        }
    }
    WindowNativeHandleInvalidate(&slot->native);
    slot->state.lifecycle = RoboWindowLifecycleClosed;
    slot->state.visible = 0;
    slot->state.focused = 0;
    slot->PresentSurface = NULL;
    WindowRenderTeardown(slot);
    /* Drop pending input/lifecycle events so a later window cannot see them. */
    WindowEventPurgeWindow(id);
    EmitSimple(RoboWindowEventClosed, id);
    slot->inUse = 0;
    KeyboardResetPressedState();
    MouseResetPressedState();
    LogDebug("robo_window: destroyed id=%llu", (unsigned long long)id);
    return RoboOk;
}

Result WindowGetLifecycle(WindowId id,
                                        WindowLifecycle *out)
{
    WindowSlot *slot;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    *out = slot->state.lifecycle;
    return RoboOk;
}

int WindowIsClosing(WindowId id)
{
    WindowSlot *slot = SlotFind(id);
    return slot != NULL &&
           slot->state.lifecycle == RoboWindowLifecycleClosing;
}

Result WindowGetState(WindowId id, WindowState *out)
{
    WindowSlot *slot;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot) {
        return RoboNotFound;
    }
    SyncStateFromBackend(slot);
    *out = slot->state;
    return RoboOk;
}

Result WindowGetAssociatedDisplay(WindowId windowId,
                                                 DisplayInfo *out)
{
    WindowSlot *slot;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(windowId);
    if (!slot) {
        return RoboNotFound;
    }
    if (slot->state.displayId == RoboDisplayIdNone) {
        return RoboNotFound;
    }
    return DisplayGet(slot->state.displayId, out);
}

Result WindowSetAssociatedDisplay(WindowId windowId,
                                                 DisplayId displayId)
{
    WindowSlot *slot;
    Result r;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(windowId);
    if (!slot || !WindowIdIsLive(windowId)) {
        return RoboNotFound;
    }
    if (displayId == RoboDisplayIdNone) {
        slot->state.displayId = RoboDisplayIdNone;
        return RoboOk;
    }
    if (!DisplayIdIsKnown(displayId)) {
        return RoboNotFound;
    }
    slot->state.displayId = displayId;
    if (!slot->scaleFromPreference) {
        float ds = 0.0f;
        if (DisplayGetScale(displayId, &ds) == RoboOk) {
            slot->state.scale = ds;
            WindowRenderMarkStale(slot);
            LogDebug("robo_window: scale from display id=%llu scale=%g",
                           (unsigned long long)windowId, (double)ds);
        }
    }
    return RoboOk;
}

Result WindowGetScale(WindowId id, float *outScale)
{
    WindowSlot *slot;
    Result r;

    if (!outScale) {
        return RoboInvalidArgument;
    }
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    SyncStateFromBackend(slot);
    if (DisplayScaleIsKnown(slot->state.scale)) {
        *outScale = slot->state.scale;
        return RoboOk;
    }
    if (slot->state.displayId != RoboDisplayIdNone) {
        return DisplayGetScale(slot->state.displayId, outScale);
    }
    return RoboNotFound;
}

Result WindowGetDpi(WindowId id, DisplayDpi *out)
{
    WindowSlot *slot;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    if (slot->state.displayId == RoboDisplayIdNone) {
        return RoboNotFound;
    }
    return DisplayGetDpi(slot->state.displayId, out);
}

Result WindowGetSizePhysical(WindowId id,
                                            GfxSize *out)
{
    WindowSlot *slot;
    Result r;
    float scale;
    int32_t pw;
    int32_t ph;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    SyncStateFromBackend(slot);
    r = WindowGetScale(id, &scale);
    if (r != RoboOk) {
        return r;
    }
    r = DisplaySizeLogicalToPhysical(slot->state.width,
                                              slot->state.height, scale, &pw,
                                              &ph);
    if (r != RoboOk) {
        return r;
    }
    out->width = pw;
    out->height = ph;
    return RoboOk;
}

Result WindowGetTitle(WindowId id, const char **outTitle)
{
    WindowSlot *slot;
    Result r;

    if (!outTitle) {
        return RoboInvalidArgument;
    }
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot) {
        return RoboNotFound;
    }
    *outTitle = slot->title;
    return RoboOk;
}

Result WindowSetTitle(WindowId id, const char *utf8Title)
{
    WindowSlot *slot;
    Result r;
    const WindowBackendVtbl *vtbl;
    WindowBackendCaps caps;
    char tmp[RoboWindowTitleMax];

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = RequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    r = CopyTitleUtf8(tmp, sizeof(tmp), utf8Title);
    if (r != RoboOk) {
        return r;
    }
    if (WindowBackendCapabilities(&caps) != RoboOk || !caps.SetTitle) {
        return RoboNotSupported;
    }
    vtbl = GetWindowBackendVtbl();
    if (!vtbl || !vtbl->SetTitle) {
        return RoboNotSupported;
    }
    r = vtbl->SetTitle(WindowBackendUserdata(), id, &slot->native, tmp);
    if (r != RoboOk) {
        return r;
    }
    memcpy(slot->title, tmp, sizeof(slot->title));
    slot->configSnapshot.title = slot->title;
    return RoboOk;
}

Result WindowSetSize(WindowId id,
                                   int32_t width,
                                   int32_t height)
{
    WindowSlot *slot;
    Result r;
    const WindowBackendVtbl *vtbl;
    WindowBackendCaps caps;
    WindowSizeConstraints cons;
    int32_t prevReqW;
    int32_t prevReqH;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = RequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    ConstraintsFromSlot(slot, &cons);
    r = WindowSizeValidateAgainstConstraints(width, height, &cons);
    if (r != RoboOk) {
        return r;
    }
    if (WindowBackendCapabilities(&caps) != RoboOk || !caps.resize) {
        return RoboNotSupported;
    }
    vtbl = GetWindowBackendVtbl();
    if (!vtbl || !vtbl->SetSize) {
        return RoboNotSupported;
    }
    prevReqW = slot->state.requestedWidth;
    prevReqH = slot->state.requestedHeight;
    slot->state.requestedWidth = width;
    slot->state.requestedHeight = height;
    gGeometryApplyDepth++;
    r = vtbl->SetSize(WindowBackendUserdata(), id, &slot->native, width,
                       height);
    gGeometryApplyDepth--;
    if (r != RoboOk) {
        slot->state.requestedWidth = prevReqW;
        slot->state.requestedHeight = prevReqH;
        LogDebug("robo_window: set_size failed id=%llu (%d); requested unchanged",
                       (unsigned long long)id, (int)r);
        return r;
    }
    if (BackendReportsActual()) {
        SyncStateFromBackend(slot);
    } else {
        slot->state.width = width;
        slot->state.height = height;
    }
    EmitWithState(RoboWindowEventResized, slot);
    WindowRenderMarkStale(slot);
    LogDebug("robo_window: size id=%llu req=%dx%d actual=%dx%d",
                   (unsigned long long)id, (int)slot->state.requestedWidth,
                   (int)slot->state.requestedHeight, (int)slot->state.width,
                   (int)slot->state.height);
    return RoboOk;
}

Result WindowRequestSize(WindowId id,
                                       int32_t width,
                                       int32_t height)
{
    return WindowSetSize(id, width, height);
}

Result WindowGetSize(WindowId id, GfxSize *out)
{
    WindowSlot *slot;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    SyncStateFromBackend(slot);
    out->width = slot->state.width;
    out->height = slot->state.height;
    return RoboOk;
}

Result WindowGetRequestedSize(WindowId id,
                                             GfxSize *out)
{
    WindowSlot *slot;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    out->width = slot->state.requestedWidth;
    out->height = slot->state.requestedHeight;
    return RoboOk;
}

Result WindowGetSizeConstraints(
    WindowId id,
    WindowSizeConstraints *out)
{
    WindowSlot *slot;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    ConstraintsFromSlot(slot, out);
    return RoboOk;
}

Result WindowSetSizeConstraints(
    WindowId id,
    const WindowSizeConstraints *constraints)
{
    WindowSlot *slot;
    Result r;
    const WindowBackendVtbl *vtbl;
    WindowBackendCaps caps;
    WindowSizeConstraints c;
    int32_t w;
    int32_t h;
    int needCorrective;

    if (!constraints) {
        return RoboInvalidArgument;
    }
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = RequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    c = *constraints;
    r = WindowSizeConstraintsValidate(&c);
    if (r != RoboOk) {
        return r;
    }

    vtbl = GetWindowBackendVtbl();
    if (WindowBackendCapabilities(&caps) == RoboOk &&
        caps.sizeConstraints) {
        if (!vtbl || !vtbl->SetSizeConstraints) {
            return RoboNotSupported;
        }
        r = vtbl->SetSizeConstraints(WindowBackendUserdata(), id,
                                       &slot->native, c.minWidth, c.minHeight,
                                       c.maxWidth, c.maxHeight);
        if (r != RoboOk) {
            return r;
        }
    } else if (vtbl && vtbl->SetSizeConstraints) {
        r = vtbl->SetSizeConstraints(WindowBackendUserdata(), id,
                                       &slot->native, c.minWidth, c.minHeight,
                                       c.maxWidth, c.maxHeight);
        if (r != RoboOk && r != RoboNotSupported) {
            return r;
        }
    }

    ApplyConstraintsToState(slot, &c);
    LogDebug("robo_window: constraints id=%llu min=%dx%d max=%dx%d",
                   (unsigned long long)id, (int)c.minWidth, (int)c.minHeight,
                   (int)c.maxWidth, (int)c.maxHeight);

    SyncStateFromBackend(slot);
    w = slot->state.width;
    h = slot->state.height;
    needCorrective = 0;
    if (c.minWidth > 0 && w < c.minWidth) {
        needCorrective = 1;
    }
    if (c.minHeight > 0 && h < c.minHeight) {
        needCorrective = 1;
    }
    if (c.maxWidth > 0 && w > c.maxWidth) {
        needCorrective = 1;
    }
    if (c.maxHeight > 0 && h > c.maxHeight) {
        needCorrective = 1;
    }
    if (needCorrective) {
        ClampSizeToConstraints(&w, &h, &c);
        r = WindowSetSize(id, w, h);
        if (r != RoboOk) {
            return r;
        }
    }
    return RoboOk;
}

Result WindowObserveSize(WindowId id,
                                       int32_t width,
                                       int32_t height)
{
    WindowSlot *slot;
    Result r;
    int changed;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    if (gGeometryApplyDepth > 0) {
        /* Programmatic SetSize owns state + RESIZED emit. */
        return RoboOk;
    }
    r = WindowSizeValidate(width, height);
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        LogDebug("robo_window: observe_size dropped id=%llu (not live)",
                       (unsigned long long)id);
        return RoboNotFound;
    }
    changed = (slot->state.width != width || slot->state.height != height);
    slot->state.width = width;
    slot->state.height = height;
    if (changed) {
        EmitWithState(RoboWindowEventResized, slot);
        WindowRenderMarkStale(slot);
        LogDebug("robo_window: observe_size id=%llu actual=%dx%d",
                       (unsigned long long)id, (int)width, (int)height);
    }
    return RoboOk;
}

Result WindowObservePosition(WindowId id,
                                           int32_t x,
                                           int32_t y)
{
    WindowSlot *slot;
    Result r;
    int changed;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    if (gGeometryApplyDepth > 0) {
        return RoboOk;
    }
    slot = SlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        LogDebug("robo_window: observe_position dropped id=%llu",
                       (unsigned long long)id);
        return RoboNotFound;
    }
    changed = (slot->state.x != x || slot->state.y != y);
    slot->state.x = x;
    slot->state.y = y;
    if (changed) {
        EmitWithState(RoboWindowEventMoved, slot);
        LogDebug("robo_window: observe_position id=%llu (%d,%d)",
                       (unsigned long long)id, (int)x, (int)y);
    }
    return RoboOk;
}

Result WindowObserveVisibility(WindowId id, int visible)
{
    WindowSlot *slot;
    Result r;
    int want = visible ? 1 : 0;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    if (gGeometryApplyDepth > 0) {
        return RoboOk;
    }
    slot = SlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    if (slot->state.visible == want) {
        return RoboOk;
    }
    slot->state.visible = want;
    if (want) {
        EmitSimple(RoboWindowEventShown, id);
        LogDebug("robo_window: observe_visibility shown id=%llu",
                       (unsigned long long)id);
    } else {
        EmitSimple(RoboWindowEventHidden, id);
        LogDebug("robo_window: observe_visibility hidden id=%llu",
                       (unsigned long long)id);
    }
    return RoboOk;
}

Result WindowObserveFocus(WindowId id, int focused)
{
    WindowSlot *slot;
    Result r;
    int want = focused ? 1 : 0;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    if (gGeometryApplyDepth > 0) {
        return RoboOk;
    }
    slot = SlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    if (slot->state.focused == want) {
        return RoboOk;
    }
    slot->state.focused = want;
    if (want) {
        EmitSimple(RoboWindowEventFocusGained, id);
        LogDebug("robo_window: observe_focus gained id=%llu",
                       (unsigned long long)id);
    } else {
        EmitSimple(RoboWindowEventFocusLost, id);
        KeyboardResetPressedState();
        MouseResetPressedState();
        LogDebug("robo_window: observe_focus lost id=%llu",
                       (unsigned long long)id);
    }
    return RoboOk;
}

Result WindowRequestPosition(WindowId id,
                                           int32_t x,
                                           int32_t y)
{
    WindowSlot *slot;
    Result r;
    const WindowBackendVtbl *vtbl;
    WindowBackendCaps caps;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = RequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (WindowBackendCapabilities(&caps) != RoboOk || !caps.move) {
        return RoboNotSupported;
    }
    vtbl = GetWindowBackendVtbl();
    if (!vtbl || !vtbl->SetPosition) {
        return RoboNotSupported;
    }
    slot->state.requestedX = x;
    slot->state.requestedY = y;
    gGeometryApplyDepth++;
    r = vtbl->SetPosition(WindowBackendUserdata(), id, &slot->native, x,
                           y);
    gGeometryApplyDepth--;
    if (r != RoboOk) {
        return r;
    }
    if (BackendReportsActual()) {
        SyncStateFromBackend(slot);
    } else {
        slot->state.x = x;
        slot->state.y = y;
    }
    EmitWithState(RoboWindowEventMoved, slot);
    return RoboOk;
}

Result WindowRequestFocus(WindowId id)
{
    WindowSlot *slot;
    Result r;
    const WindowBackendVtbl *vtbl;
    WindowBackendCaps caps;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = RequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    if (WindowBackendCapabilities(&caps) != RoboOk || !caps.focus) {
        return RoboNotSupported;
    }
    vtbl = GetWindowBackendVtbl();
    if (!vtbl || !vtbl->focus) {
        return RoboNotSupported;
    }
    gGeometryApplyDepth++;
    r = vtbl->focus(WindowBackendUserdata(), id, &slot->native);
    gGeometryApplyDepth--;
    if (r != RoboOk) {
        return r;
    }
    if (!slot->state.focused) {
        slot->state.focused = 1;
        EmitSimple(RoboWindowEventFocusGained, id);
    }
    SyncStateFromBackend(slot);
    return RoboOk;
}

Result WindowGetNativeHandle(WindowId id,
                                            WindowNativeHandle *out)
{
    WindowSlot *slot;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    slot = SlotFind(id);
    if (!slot || !WindowIdIsLive(id)) {
        return RoboNotFound;
    }
    *out = slot->native;
    return RoboOk;
}

int WindowIdIsLive(WindowId id)
{
    WindowSlot *slot = SlotFind(id);

    return slot != NULL &&
           (slot->state.lifecycle == RoboWindowLifecycleCreated ||
            slot->state.lifecycle == RoboWindowLifecycleClosing);
}

WindowId WindowFindFocusedId(void)
{
    size_t i;

    for (i = 0u; i < RoboWindowMaxWindows; i++) {
        if (gSlots[i].inUse && gSlots[i].state.focused &&
            (gSlots[i].state.lifecycle == RoboWindowLifecycleCreated ||
             gSlots[i].state.lifecycle == RoboWindowLifecycleClosing)) {
            return gSlots[i].id;
        }
    }
    return RoboWindowIdNone;
}

size_t WindowLiveCount(void)
{
    size_t i;
    size_t n = 0u;

    for (i = 0u; i < RoboWindowMaxWindows; i++) {
        if (gSlots[i].inUse) {
            n++;
        }
    }
    return n;
}

Result WindowProcessEvents(void)
{
    return WindowEventLoopPoll();
}

Result WindowSetPresentSurface(WindowId id,
                                              GfxSurface *surface)
{
    WindowSlot *slot;
    Result r;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = RequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    slot->PresentSurface = surface;
    return RoboOk;
}

GfxSurface *WindowGetPresentSurface(WindowId id)
{
    WindowSlot *slot = SlotFind(id);

    return slot ? slot->PresentSurface : NULL;
}

Result WindowPresent(WindowId id,
                                  GfxSurface *surface)
{
    WindowSlot *slot;
    const WindowBackendVtbl *vtbl;
    GfxSurface *target;
    WindowBackendCaps caps;
    Result r;

    r = RequireReady();
    if (r != RoboOk) {
        return r;
    }
    r = RequireMutableSlot(id, &slot);
    if (r != RoboOk) {
        return r;
    }
    target = surface ? surface : slot->PresentSurface;
    if (!target) {
        target = slot->renderSurface;
    }
    if (!target) {
        return RoboInvalidArgument;
    }
    if (WindowBackendCapabilities(&caps) != RoboOk || !caps.present) {
        return RoboNotSupported;
    }
    r = WindowPresentValidateSurface(target, &caps);
    if (r != RoboOk) {
        LogDebug("robo_window: present validate failed id=%llu (%d)",
                       (unsigned long long)id, (int)r);
        return r;
    }
    vtbl = GetWindowBackendVtbl();
    if (!vtbl || !vtbl->PresentSurface) {
        return RoboNotSupported;
    }
    r = vtbl->PresentSurface(WindowBackendUserdata(), id, &slot->native,
                              target);
    if (r == RoboOk) {
        LogDebug(
            "robo_window: presented id=%llu %ux%u fmt=%d (backend accepted)",
            (unsigned long long)id,
            (unsigned)GfxSurfaceWidth(target),
            (unsigned)GfxSurfaceHeight(target),
            (int)GfxSurfaceFormat(target));
    } else {
        LogDebug("robo_window: present backend failed id=%llu (%d)",
                       (unsigned long long)id, (int)r);
    }
    return r;
}
