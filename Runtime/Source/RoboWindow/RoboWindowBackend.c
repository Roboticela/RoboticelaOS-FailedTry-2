#include "RoboWindow/RoboWindowBackend.h"
#include "RoboWindow/RoboWindow.h"
#include "RoboWindowInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static const WindowBackendVtbl *gVtbl;
static void *gUserdata;
static int gBackendLive;
static WindowBackendCaps gCapsCache;

static const WindowBackendCaps gNullCaps = {
    1, /* RoboCreateWindow */
    1, /* RoboDestroyWindow */
    1, /* showHide */
    1, /* resize */
    1, /* move */
    1, /* SetTitle */
    0, /* focus — no native focus */
    0, /* QueryState */
    0, /* present */
    1, /* ProcessEvents (no-op OK) */
    0, /* reportsActualState */
    1, /* sizeConstraints */
    0u, /* presentFormatMask */
    0u, /* presentMaxWidth */
    0u, /* presentMaxHeight */
    0, /* WaitEvents */
    0, /* wakeup */
    0  /* mouseCapture */
};

void WindowBackendCapsClear(WindowBackendCaps *caps)
{
    if (caps) {
        memset(caps, 0, sizeof(*caps));
    }
}

void WindowBackendCapsInitNull(WindowBackendCaps *caps)
{
    if (caps) {
        *caps = gNullCaps;
    }
}

Result WindowBackendCapabilities(WindowBackendCaps *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!gBackendLive || !gVtbl || !gVtbl->capabilities) {
        WindowBackendCapsClear(out);
        return RoboNotSupported;
    }
    /* Live table — backends/tests may update the pointed-to caps. */
    *out = *gVtbl->capabilities;
    gCapsCache = *out;
    return RoboOk;
}

static Result NullInitialize(void *userdata)
{
    (void)userdata;
    LogDebug("robo_window: null backend initialize");
    return RoboOk;
}

static void NullShutdown(void *userdata)
{
    (void)userdata;
    LogDebug("robo_window: null backend shutdown");
}

static Result NullCreateWindow(void *userdata,
                                        WindowId id,
                                        const WindowConfig *cfg,
                                        WindowNativeHandle *outNative)
{
    (void)userdata;
    (void)id;
    (void)cfg;
    if (!outNative) {
        return RoboInvalidArgument;
    }
    /* No native OS window — empty handle is intentional. */
    WindowNativeHandleClear(outNative);
    return RoboOk;
}

static Result NullDestroyWindow(void *userdata,
                                         WindowId id,
                                         WindowNativeHandle *native)
{
    (void)userdata;
    (void)id;
    if (native) {
        WindowNativeHandleInvalidate(native);
    }
    return RoboOk;
}

static Result NullShow(void *userdata,
                               WindowId id,
                               WindowNativeHandle *native)
{
    (void)userdata;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result NullHide(void *userdata,
                               WindowId id,
                               WindowNativeHandle *native)
{
    (void)userdata;
    (void)id;
    (void)native;
    return RoboOk;
}

static Result NullSetTitle(void *userdata,
                                    WindowId id,
                                    WindowNativeHandle *native,
                                    const char *utf8Title)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)utf8Title;
    /* In-process acknowledgment only — no OS title bar. */
    return RoboOk;
}

static Result NullSetSize(void *userdata,
                                   WindowId id,
                                   WindowNativeHandle *native,
                                   int32_t width,
                                   int32_t height)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)width;
    (void)height;
    return RoboOk;
}

static Result NullSetSizeConstraints(void *userdata,
                                               WindowId id,
                                               WindowNativeHandle *native,
                                               int32_t minWidth,
                                               int32_t minHeight,
                                               int32_t maxWidth,
                                               int32_t maxHeight)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)minWidth;
    (void)minHeight;
    (void)maxWidth;
    (void)maxHeight;
    return RoboOk;
}

static Result NullSetPosition(void *userdata,
                                       WindowId id,
                                       WindowNativeHandle *native,
                                       int32_t x,
                                       int32_t y)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)x;
    (void)y;
    return RoboOk;
}

static Result NullFocus(void *userdata,
                                WindowId id,
                                WindowNativeHandle *native)
{
    (void)userdata;
    (void)id;
    (void)native;
    return RoboNotSupported;
}

static Result NullPresentSurface(void *userdata,
                                          WindowId id,
                                          WindowNativeHandle *native,
                                          GfxSurface *surface)
{
    (void)userdata;
    (void)id;
    (void)native;
    (void)surface;
    return RoboNotSupported;
}

static Result NullProcessEvents(void *userdata)
{
    (void)userdata;
    return RoboOk;
}

static const WindowBackendVtbl gNullVtbl = {
    "null",
    &gNullCaps,
    NullInitialize,
    NullShutdown,
    NullCreateWindow,
    NullDestroyWindow,
    NullShow,
    NullHide,
    NullSetTitle,
    NullSetSize,
    NullSetSizeConstraints,
    NullSetPosition,
    NullFocus,
    NULL,
    NullPresentSurface,
    NullProcessEvents,
    NULL, /* WaitEvents */
    NULL, /* wakeup */
    NULL, /* CaptureMouse */
    NULL, /* ReleaseMouseCapture */
    NULL  /* PresentSurfaceRegion */
};

const WindowBackendVtbl *WindowBackendNullVtbl(void)
{
    return &gNullVtbl;
}

const WindowBackendVtbl *GetWindowBackendVtbl(void)
{
    return gVtbl;
}

void *WindowBackendUserdata(void)
{
    return gUserdata;
}

void WindowBackendClear(void)
{
    if (gBackendLive && gVtbl && gVtbl->shutdown) {
        gVtbl->shutdown(gUserdata);
    }
    gVtbl = NULL;
    gUserdata = NULL;
    gBackendLive = 0;
    WindowBackendCapsClear(&gCapsCache);
}

Result WindowBackendRegister(const WindowBackendVtbl *vtbl,
                                           void *userdata)
{
    Result r;

    if (!vtbl || !vtbl->name || !vtbl->capabilities || !vtbl->initialize ||
        !vtbl->shutdown || !vtbl->RoboCreateWindow || !vtbl->RoboDestroyWindow ||
        !vtbl->show || !vtbl->hide || !vtbl->SetTitle || !vtbl->focus ||
        !vtbl->PresentSurface || !vtbl->ProcessEvents) {
        return RoboInvalidArgument;
    }
    if (!WindowIsInitialized()) {
        return RoboInitError;
    }
    WindowBackendClear();
    r = vtbl->initialize(userdata);
    if (r != RoboOk) {
        LogError("robo_window: backend '%s' initialize failed (%d)",
                       vtbl->name, (int)r);
        return r;
    }
    gVtbl = vtbl;
    gUserdata = userdata;
    gCapsCache = *vtbl->capabilities;
    gBackendLive = 1;
    LogInfo("robo_window: backend registered '%s'", vtbl->name);
    return RoboOk;
}

Result WindowBackendUseNull(void)
{
    return WindowBackendRegister(WindowBackendNullVtbl(), NULL);
}

int WindowBackendIsRegistered(void)
{
    return gBackendLive && gVtbl != NULL;
}

const char *WindowBackendName(void)
{
    return (gVtbl && gVtbl->name) ? gVtbl->name : NULL;
}
