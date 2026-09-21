#include "RoboDisplay/RoboDisplayBackendQemu.h"

#include "RoboBase/RoboLog.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplay/RoboDisplayInfo.h"
#include "RoboMem/RoboMem.h"

#include <string.h>

#define RoboQemuDisplayId ((DisplayId)1u)

static QemuFbInfo gFb;
static int gFbSet;
static int gSimActive;
static void *gSimPixels;

static const DisplayBackendCaps gCaps = {
    1, /* enumerate */
    1, /* primary */
    1, /* GetInfo */
    1, /* workArea */
    0, /* refreshRate — unknown on GOP FB */
    1, /* scaleDpi — report scale=1 fallback */
    0, /* orientation */
    1  /* modes — single current mode */
};

const QemuFbInfo *QemuFbCurrent(void)
{
    return gFbSet ? &gFb : NULL;
}

Result QemuFbSetCurrent(const QemuFbInfo *info)
{
    Result r;
    if (!info) {
        return RoboInvalidArgument;
    }
    r = QemuFbInfoValidate(info);
    if (r != RoboOk && !info->headless) {
        return r;
    }
    gFb = *info;
    gFbSet = 1;
    return RoboOk;
}

void QemuFbClearCurrent(void)
{
    memset(&gFb, 0, sizeof(gFb));
    gFbSet = 0;
}

static void FillInfo(DisplayInfo *out)
{
    int32_t w;
    int32_t h;

    DisplayInfoInit(out);
    out->id = RoboQemuDisplayId;
    strncpy(out->name, "QEMU Framebuffer", sizeof(out->name) - 1u);
    out->name[sizeof(out->name) - 1u] = '\0';
    if (!gFbSet || gFb.headless || !gFb.ready) {
        out->connection = RoboDisplayConnectionUnavailable;
        out->isPrimary = 1;
        out->scale = 1.0f;
        return;
    }
    w = (int32_t)gFb.width;
    h = (int32_t)gFb.height;
    out->bounds = MakeGfxRect(0, 0, w, h);
    out->workArea = out->bounds;
    out->resolutionWidthPx = w;
    out->resolutionHeightPx = h;
    out->refreshHz = 0.0f;
    out->scale = 1.0f; /* no physical DPI from GOP — logical == physical */
    out->dpiX = 0.0f;
    out->dpiY = 0.0f;
    out->orientation = RoboDisplayOrientationLandscape;
    out->isPrimary = 1;
    out->connection = RoboDisplayConnectionConnected;
    out->physicalWidthMm = 0;
    out->physicalHeightMm = 0;
}

static Result QemuInit(void *ud)
{
    (void)ud;
    return RoboOk;
}

static void QemuShutdown(void *ud)
{
    (void)ud;
}

static Result QemuEnumerate(void *ud, DisplayInfo *out,
                                    size_t capacity, size_t *outCount)
{
    (void)ud;
    if (!outCount) {
        return RoboInvalidArgument;
    }
    *outCount = 0u;
    if (!gFbSet) {
        return RoboOk;
    }
    if (capacity == 0u || !out) {
        *outCount = 1u;
        return capacity == 0u ? RoboOk : RoboInvalidArgument;
    }
    FillInfo(&out[0]);
    *outCount = 1u;
    return RoboOk;
}

static Result QemuPrimary(void *ud, DisplayInfo *out)
{
    (void)ud;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!gFbSet) {
        return RoboNotFound;
    }
    FillInfo(out);
    return RoboOk;
}

static Result QemuGetInfo(void *ud, DisplayId id,
                                   DisplayInfo *out)
{
    (void)ud;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!gFbSet || id != RoboQemuDisplayId) {
        return RoboNotFound;
    }
    FillInfo(out);
    return RoboOk;
}

static Result QemuGetModes(void *ud, DisplayId id,
                                    DisplayMode *out, size_t capacity,
                                    size_t *outCount)
{
    (void)ud;
    if (!outCount) {
        return RoboInvalidArgument;
    }
    *outCount = 0u;
    if (!gFbSet || id != RoboQemuDisplayId || gFb.headless) {
        return RoboNotFound;
    }
    if (capacity == 0u) {
        *outCount = 1u;
        return RoboOk;
    }
    if (!out) {
        return RoboInvalidArgument;
    }
    DisplayModeInit(&out[0]);
    out[0].widthPx = (int32_t)gFb.width;
    out[0].heightPx = (int32_t)gFb.height;
    out[0].refreshHz = 0.0f;
    *outCount = 1u;
    return RoboOk;
}

static const DisplayBackendVtbl gQemuDisplayVtbl = {
    "qemu",
    &gCaps,
    QemuInit,
    QemuShutdown,
    QemuEnumerate,
    QemuPrimary,
    QemuGetInfo,
    QemuGetModes
};

const DisplayBackendVtbl *DisplayBackendQemuVtbl(void)
{
    return &gQemuDisplayVtbl;
}

Result DisplayBackendUseQemu(void)
{
    return DisplayBackendRegister(&gQemuDisplayVtbl, NULL);
}

Result QemuSimInit(uint32_t width, uint32_t height,
                                 uint32_t pixfmt)
{
    QemuFbInfo info;
    size_t bytes;
    uint32_t pitch;

    QemuSimShutdown();
    if (width == 0u || height == 0u) {
        return RoboInvalidArgument;
    }
    if (pixfmt != RoboQemuPixBgrx && pixfmt != RoboQemuPixRgbx) {
        return RoboNotSupported;
    }
    pitch = width * 4u;
    bytes = (size_t)pitch * (size_t)height;
    gSimPixels = RoboCalloc(1, bytes);
    if (!gSimPixels) {
        return RoboOutOfMemory;
    }
    QemuFbInfoInit(&info);
    info.width = width;
    info.height = height;
    info.pitchBytes = pitch;
    info.bpp = 32u;
    info.pixfmt = pixfmt;
    info.pixels = gSimPixels;
    info.byteSize = bytes;
    info.ready = 1;
    info.headless = 0;
    if (QemuFbSetCurrent(&info) != RoboOk) {
        RoboFree(gSimPixels);
        gSimPixels = NULL;
        return RoboInvalidArgument;
    }
    gSimActive = 1;
    LogInfo("robo_qemu_sim: %ux%u pixfmt=%u", (unsigned)width,
                  (unsigned)height, (unsigned)pixfmt);
    return RoboOk;
}

void QemuSimShutdown(void)
{
    if (gSimPixels) {
        RoboFree(gSimPixels);
        gSimPixels = NULL;
    }
    gSimActive = 0;
    QemuFbClearCurrent();
}

int QemuSimIsActive(void)
{
    return gSimActive;
}
