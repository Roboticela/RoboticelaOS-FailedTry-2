/*
 * RoboTypography version and capabilities (Phase 12.3) — see RoboTypographyVersion.h.
 *
 * The capability record is the interesting half, and it is written as *pairs*: each flag that is set
 * has a sibling that is cleared and says why. `variableAxisApplication`, `letterSpacing` and
 * `shaping` are 0 on purpose, and a reader who asks this module rather than the documentation gets
 * the same answer either way. A suite asserts the record field by field, because a capability that
 * nobody checks is a claim, not a fact.
 */

#include "RoboTypography/RoboTypographyVersion.h"

#include "RoboBase/RoboError.h"

#include <string.h>

const char *RoboTypographyVersionString(void)
{
    return "1.0.0";
}

Result RoboTypographyGetVersion(int32_t *outMajor,
                                int32_t *outMinor,
                                int32_t *outPatch)
{
    if (!outMajor || !outMinor || !outPatch) {
        return RoboInvalidArgument;
    }
    *outMajor = RoboTypographyVersionMajor;
    *outMinor = RoboTypographyVersionMinor;
    *outPatch = RoboTypographyVersionPatch;
    return RoboOk;
}

void RoboTypographyCapabilitiesInit(TypographyCapabilities *capabilities)
{
    if (capabilities) {
        memset(capabilities, 0, sizeof(*capabilities));
    }
}

Result RoboTypographyGetCapabilities(TypographyCapabilities *out)
{
    if (!out) {
        return RoboInvalidArgument;
    }
    RoboTypographyCapabilitiesInit(out);

    /* What this build does. */
    out->roles = 1;
    out->scale = 1;
    out->opticalSizeClasses = 1;
    out->weightSelection = 1;
    out->fallback = 1;
    out->cache = 1;
    out->documentDefaults = 1;
    out->desktopBinding = 1;

    /* What it deliberately does not, each a named later goal. */
    out->variableAxisApplication = 0;
    out->letterSpacing = 0;
    out->shaping = 0;
    return RoboOk;
}
