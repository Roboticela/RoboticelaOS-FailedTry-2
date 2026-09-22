/*
 * RoboTypography version and capabilities (Phase 12.3).
 *
 * The same two doors every module in this runtime has: a version string a dump can print, and a
 * **capability record** a caller reads positionally or by name rather than by count. The flags are
 * the honest answer to "what does this build's typography do", and each is asserted by a
 * `typography.conformance.*` case because an untested capability flag stops being true without
 * anybody noticing.
 *
 * What this build does **not** do is as much of the record as what it does, and it is written down
 * here rather than only in prose so a reader can ask the module rather than the documentation:
 *
 *   optical sizing   the optical-size *class* of a role is resolved and carried, but the variable
 *                    face's `opsz` axis is not applied -- variable-axis selection is a later goal;
 *   letter spacing   the scale carries no tracking because the current text path (RoboPaint's run
 *                    walk and RoboDesktop's own label walk) has no place to apply it;
 *   shaping          no runs, no GSUB/GPOS, no kerning, no bidi: this module picks a face for a
 *                    role, and Phase 12.4's RoboText is what will consume it per run.
 */
#ifndef ROBO_TYPOGRAPHY_VERSION_H
#define ROBO_TYPOGRAPHY_VERSION_H

#include "RoboBase/RoboError.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Major version: changes only when the role vocabulary or the API shape does. */
#define RoboTypographyVersionMajor 1
#define RoboTypographyVersionMinor 0
#define RoboTypographyVersionPatch 0

/** Version as `"1.0.0"`; never NULL. */
const char *RoboTypographyVersionString(void);

/** Version components. RoboInvalidArgument for a NULL out pointer. */
Result RoboTypographyGetVersion(int32_t *outMajor,
                                int32_t *outMinor,
                                int32_t *outPatch);

/**
 * What this build's typography does and does not do.
 *
 * Read positionally or by name, never by count.
 */
typedef struct TypographyCapabilities {
    /** Semantic roles resolve to concrete loaded faces through the system typeface pack (12.2). */
    int roles;
    /** The centralized scale: role → face role, weight, size and line height. */
    int scale;
    /** Per-role optical-size *classes*, resolved and carried. */
    int opticalSizeClasses;
    /** Deterministic weight selection with a nearest-available step-down. */
    int weightSelection;
    /** Deterministic fallback to a covering face, through RoboFont's own chain. */
    int fallback;
    /** A bounded resolution cache over (role, weight, style). */
    int cache;
    /** Generated document defaults (a runtime-origin sheet from the document roles). */
    int documentDefaults;
    /** Chrome text drawn from roles rather than one caller-supplied face. */
    int desktopBinding;
    /** Variable-axis *application* (`opsz`, `wght` beyond selection). **0**: a later goal. */
    int variableAxisApplication;
    /** Letter spacing / tracking. **0**: the text path has nowhere to apply it yet. */
    int letterSpacing;
    /** Shaping: runs, GSUB/GPOS, kerning, bidi. **0**: RoboText is Phase 12.4. */
    int shaping;
} TypographyCapabilities;

/** Zero @p capabilities (every feature reported absent). Safe on NULL. */
void RoboTypographyCapabilitiesInit(TypographyCapabilities *capabilities);

/** Fill @p out with this build's capabilities. RoboInvalidArgument for a NULL @p out. */
Result RoboTypographyGetCapabilities(TypographyCapabilities *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TYPOGRAPHY_VERSION_H */
