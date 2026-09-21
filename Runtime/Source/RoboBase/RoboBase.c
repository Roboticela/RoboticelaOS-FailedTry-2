/*
 * RoboBase — Goal 1.1 implementation (errors via RoboError).
 */

#include "RoboBase/RoboBase.h"

#define RoboBaseVersionMajor 0
#define RoboBaseVersionMinor 1
#define RoboBaseVersionPatch 0

static const char BaseVersionString[] = "0.1.0";

/* Process-wide init gate — documented minimal global for lifecycle. */
static I32 gRoboBaseInitialized = 0;

Result BaseInit(void)
{
    if (gRoboBaseInitialized) {
        return RoboAlreadyExists;
    }
    gRoboBaseInitialized = 1;
    return RoboOk;
}

Result BaseShutdown(void)
{
    if (!gRoboBaseInitialized) {
        return RoboInitError;
    }
    gRoboBaseInitialized = 0;
    return RoboOk;
}

const char *BaseVersion(void)
{
    return BaseVersionString;
}

Result BaseGetVersion(Version *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    out->major = RoboBaseVersionMajor;
    out->minor = RoboBaseVersionMinor;
    out->patch = RoboBaseVersionPatch;
    return RoboOk;
}

I32 BaseIsInitialized(void)
{
    return gRoboBaseInitialized ? 1 : 0;
}
