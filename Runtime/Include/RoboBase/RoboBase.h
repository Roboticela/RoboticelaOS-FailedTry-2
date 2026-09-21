/*
 * RoboBase — public C API (Goals 1.1+).
 *
 * Lifecycle + version. Errors: RoboError.h. Logging: RoboLog.h.
 */

#ifndef ROBO_BASE_H
#define ROBO_BASE_H

#include "RoboBase/RoboError.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Prefer Result; alias kept for Goal 1.1 call sites. */
typedef Result Status;

/** Semantic version of the RoboBase module. */
typedef struct Version {
    I32 major;
    I32 minor;
    I32 patch;
} Version;

/**
 * Initialize RoboBase.
 * @return RoboOk, or RoboAlreadyExists if already initialized
 */
Result BaseInit(void);

/**
 * Shut down RoboBase.
 * @return RoboOk, or RoboInitError if not initialized
 */
Result BaseShutdown(void);

/**
 * Null-terminated RoboBase version string (e.g. "0.1.0").
 * Always available; static storage; do not free.
 */
const char *BaseVersion(void);

/**
 * Fill @p out with numeric version components.
 * @return RoboOk, or RoboInvalidArgument if @p out is null
 */
Result BaseGetVersion(Version *out);

/** @return 1 if initialized, else 0. */
I32 BaseIsInitialized(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_BASE_H */
