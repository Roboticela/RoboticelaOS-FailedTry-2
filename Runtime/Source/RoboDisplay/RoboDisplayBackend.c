#include "RoboDisplay/RoboDisplayBackend.h"
#include "RoboDisplay/RoboDisplay.h"
#include "RoboDisplayInternal.h"

#include "RoboBase/RoboLog.h"

#include <string.h>

static const DisplayBackendVtbl *gVtbl;
static void *gUserdata;
static int gLive;
static DisplayBackendCaps gCaps;

static const DisplayBackendCaps gNullCaps = {1, 1, 1, 0, 0, 0, 0, 0};

static const DisplayBackendCaps gPlaceholderCaps = {
    1, 1, 1, 1, 1, 1, 1, 0};

void DisplayBackendCapsClear(DisplayBackendCaps *caps)
{
    if (caps) {
        memset(caps, 0, sizeof(*caps));
    }
}

Result DisplayBackendCapabilities(DisplayBackendCaps *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    if (!gLive || !gVtbl) {
        DisplayBackendCapsClear(out);
        return RoboNotSupported;
    }
    *out = gCaps;
    return RoboOk;
}

static Result NullInit(void *ud)
{
    (void)ud;
    return RoboOk;
}

static void NullShutdown(void *ud)
{
    (void)ud;
}

static Result NullEnumerate(void *ud,
                                    DisplayInfo *out,
                                    size_t capacity,
                                    size_t *outCount)
{
    (void)ud;
    (void)out;
    (void)capacity;
    if (!outCount) {
        return RoboInvalidArgument;
    }
    *outCount = 0u;
    return RoboOk;
}

static Result NullPrimary(void *ud, DisplayInfo *out)
{
    (void)ud;
    (void)out;
    return RoboNotFound;
}

static Result NullGetInfo(void *ud,
                                   DisplayId id,
                                   DisplayInfo *out)
{
    (void)ud;
    (void)id;
    (void)out;
    return RoboNotFound;
}

static const DisplayBackendVtbl gNullVtbl = {
    "null",
    &gNullCaps,
    NullInit,
    NullShutdown,
    NullEnumerate,
    NullPrimary,
    NullGetInfo,
    NULL};

#define RoboDisplayPlaceholderId ((DisplayId)1u)

static void FillPlaceholder(DisplayInfo *out)
{
    DisplayInfoInit(out);
    out->id = RoboDisplayPlaceholderId;
    {
        static const char name[] = "Architecture Placeholder";
        size_t i;
        for (i = 0u; i + 1u < RoboDisplayNameMax && name[i]; i++) {
            out->name[i] = name[i];
        }
        out->name[i] = '\0';
    }
    out->bounds = MakeGfxRect(0, 0, 1280, 800);
    out->workArea = MakeGfxRect(0, 0, 1280, 800);
    out->resolutionWidthPx = 1280;
    out->resolutionHeightPx = 800;
    out->refreshHz = 60.0f;
    out->scale = 1.0f;
    out->dpiX = 96.0f;
    out->dpiY = 96.0f;
    out->orientation = RoboDisplayOrientationLandscape;
    out->isPrimary = 1;
    out->connection = RoboDisplayConnectionConnected;
}

static Result PhInit(void *ud)
{
    (void)ud;
    LogDebug("robo_display: placeholder backend initialize");
    return RoboOk;
}

static void PhShutdown(void *ud)
{
    (void)ud;
}

static Result PhEnumerate(void *ud,
                                  DisplayInfo *out,
                                  size_t capacity,
                                  size_t *outCount)
{
    (void)ud;
    if (!outCount) {
        return RoboInvalidArgument;
    }
    if (capacity == 0u || !out) {
        *outCount = 1u;
        return RoboOk;
    }
    FillPlaceholder(&out[0]);
    *outCount = 1u;
    return RoboOk;
}

static Result PhPrimary(void *ud, DisplayInfo *out)
{
    (void)ud;
    if (!out) {
        return RoboInvalidArgument;
    }
    FillPlaceholder(out);
    return RoboOk;
}

static Result PhGetInfo(void *ud,
                                 DisplayId id,
                                 DisplayInfo *out)
{
    (void)ud;
    if (!out) {
        return RoboInvalidArgument;
    }
    if (id != RoboDisplayPlaceholderId) {
        return RoboNotFound;
    }
    FillPlaceholder(out);
    return RoboOk;
}

static const DisplayBackendVtbl gPlaceholderVtbl = {
    "placeholder",
    &gPlaceholderCaps,
    PhInit,
    PhShutdown,
    PhEnumerate,
    PhPrimary,
    PhGetInfo,
    NULL};

const DisplayBackendVtbl *DisplayBackendNullVtbl(void)
{
    return &gNullVtbl;
}

const DisplayBackendVtbl *DisplayBackendPlaceholderVtbl(void)
{
    return &gPlaceholderVtbl;
}

void DisplayBackendClear(void)
{
    if (gLive && gVtbl && gVtbl->shutdown) {
        gVtbl->shutdown(gUserdata);
    }
    gVtbl = NULL;
    gUserdata = NULL;
    gLive = 0;
    DisplayBackendCapsClear(&gCaps);
}

Result DisplayBackendRegister(const DisplayBackendVtbl *vtbl,
                                            void *userdata)
{
    Result r;

    if (!vtbl || !vtbl->name || !vtbl->capabilities || !vtbl->initialize ||
        !vtbl->shutdown || !vtbl->enumerate || !vtbl->GetPrimary ||
        !vtbl->GetInfo) {
        return RoboInvalidArgument;
    }
    if (!DisplayIsInitialized()) {
        return RoboInitError;
    }
    DisplayBackendClear();
    r = vtbl->initialize(userdata);
    if (r != RoboOk) {
        return r;
    }
    gVtbl = vtbl;
    gUserdata = userdata;
    gCaps = *vtbl->capabilities;
    gLive = 1;
    LogInfo("robo_display: backend registered '%s'", vtbl->name);
    return RoboOk;
}

Result DisplayBackendUseNull(void)
{
    return DisplayBackendRegister(DisplayBackendNullVtbl(), NULL);
}

Result DisplayBackendUsePlaceholder(void)
{
    return DisplayBackendRegister(DisplayBackendPlaceholderVtbl(),
                                         NULL);
}

int DisplayBackendIsRegistered(void)
{
    return gLive && gVtbl != NULL;
}

const char *DisplayBackendName(void)
{
    return (gVtbl && gVtbl->name) ? gVtbl->name : NULL;
}

/* Internal accessors for manager */
const DisplayBackendVtbl *GetDisplayBackendVtbl(void)
{
    return gVtbl;
}

void *DisplayBackendUserdata(void)
{
    return gUserdata;
}
