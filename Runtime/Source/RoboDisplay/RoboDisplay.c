#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplayInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static int gInitialized;
static DisplayInfo gCache[RoboDisplayMaxDisplays];
static size_t gCount;

const char *DisplayVersionString(void)
{
    return RoboDisplayVersionString;
}

int DisplayIsInitialized(void)
{
    return gInitialized;
}

Result DisplayInit(void)
{
    if (gInitialized) {
        return RoboOk;
    }
    memset(gCache, 0, sizeof(gCache));
    gCount = 0u;
    gInitialized = 1;
    LogInfo("robo_display: init (version %s)", RoboDisplayVersionString);
    return RoboOk;
}

void DisplayShutdown(void)
{
    if (!gInitialized) {
        return;
    }
    DisplayBackendClear();
    memset(gCache, 0, sizeof(gCache));
    gCount = 0u;
    gInitialized = 0;
    LogInfo("robo_display: shutdown");
}

Result DisplayRefresh(void)
{
    const DisplayBackendVtbl *vtbl;
    size_t n = 0u;
    Result r;
    DisplayBackendCaps caps;

    if (!gInitialized) {
        return RoboInitError;
    }
    if (!DisplayBackendIsRegistered()) {
        gCount = 0u;
        return RoboNotSupported;
    }
    if (DisplayBackendCapabilities(&caps) != RoboOk || !caps.enumerate) {
        return RoboNotSupported;
    }
    vtbl = GetDisplayBackendVtbl();
    r = vtbl->enumerate(DisplayBackendUserdata(), gCache,
                        RoboDisplayMaxDisplays, &n);
    if (r != RoboOk) {
        gCount = 0u;
        return r;
    }
    if (n > RoboDisplayMaxDisplays) {
        n = RoboDisplayMaxDisplays;
    }
    gCount = n;
    LogDebug("robo_display: refreshed count=%u", (unsigned)gCount);
    return RoboOk;
}

size_t DisplayCount(void)
{
    return gCount;
}

Result DisplayAt(size_t index, DisplayInfo *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!gInitialized) {
        return RoboInitError;
    }
    if (index >= gCount) {
        return RoboNotFound;
    }
    *out = gCache[index];
    return RoboOk;
}

Result DisplayGet(DisplayId id, DisplayInfo *out)
{
    size_t i;
    const DisplayBackendVtbl *vtbl;

    if (!out) {
        return RoboInvalidArgument;
    }
    if (!gInitialized) {
        return RoboInitError;
    }
    if (id == RoboDisplayIdNone) {
        return RoboNotFound;
    }
    for (i = 0u; i < gCount; i++) {
        if (gCache[i].id == id) {
            *out = gCache[i];
            return RoboOk;
        }
    }
    /* Fall through to backend GetInfo if registered. */
    vtbl = GetDisplayBackendVtbl();
    if (vtbl && vtbl->GetInfo) {
        return vtbl->GetInfo(DisplayBackendUserdata(), id, out);
    }
    return RoboNotFound;
}

Result DisplayPrimary(DisplayInfo *out)
{
    size_t i;
    const DisplayBackendVtbl *vtbl;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    if (!gInitialized) {
        return RoboInitError;
    }
    for (i = 0u; i < gCount; i++) {
        if (gCache[i].isPrimary &&
            gCache[i].connection != RoboDisplayConnectionDisconnected) {
            *out = gCache[i];
            return RoboOk;
        }
    }
    vtbl = GetDisplayBackendVtbl();
    if (vtbl && vtbl->GetPrimary) {
        r = vtbl->GetPrimary(DisplayBackendUserdata(), out);
        if (r == RoboOk) {
            return RoboOk;
        }
    }
    return RoboNotFound;
}

int DisplayIdIsKnown(DisplayId id)
{
    size_t i;

    if (id == RoboDisplayIdNone) {
        return 0;
    }
    for (i = 0u; i < gCount; i++) {
        if (gCache[i].id == id) {
            return 1;
        }
    }
    return 0;
}

Result DisplayGetModes(DisplayId id,
                                     DisplayMode *out,
                                     size_t capacity,
                                     size_t *outCount)
{
    const DisplayBackendVtbl *vtbl;
    DisplayBackendCaps caps;

    if (!outCount) {
        return RoboInvalidArgument;
    }
    if (!gInitialized) {
        return RoboInitError;
    }
    if (DisplayBackendCapabilities(&caps) != RoboOk || !caps.modes) {
        return RoboNotSupported;
    }
    vtbl = GetDisplayBackendVtbl();
    if (!vtbl || !vtbl->GetModes) {
        return RoboNotSupported;
    }
    return vtbl->GetModes(DisplayBackendUserdata(), id, out, capacity,
                           outCount);
}

Result DisplayGetScale(DisplayId id, float *outScale)
{
    DisplayInfo info;
    Result r;

    if (!outScale) {
        return RoboInvalidArgument;
    }
    r = DisplayGet(id, &info);
    if (r != RoboOk) {
        return r;
    }
    return DisplayInfoEffectiveScale(&info, outScale);
}

Result DisplayGetDpi(DisplayId id, DisplayDpi *out)
{
    DisplayInfo info;
    Result r;

    if (!out) {
        return RoboInvalidArgument;
    }
    r = DisplayGet(id, &info);
    if (r != RoboOk) {
        return r;
    }
    return DisplayInfoGetDpi(&info, out);
}
