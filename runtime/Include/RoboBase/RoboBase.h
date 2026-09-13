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

/** Prefer robo_result_t; alias kept for Goal 1.1 call sites. */
typedef robo_result_t robo_status_t;

/** Semantic version of the RoboBase module. */
typedef struct robo_version {
    robo_i32 major;
    robo_i32 minor;
    robo_i32 patch;
} robo_version_t;

/**
 * Initialize RoboBase.
 * @return ROBO_OK, or ROBO_ALREADY_EXISTS if already initialized
 */
robo_result_t robo_base_init(void);

/**
 * Shut down RoboBase.
 * @return ROBO_OK, or ROBO_INIT_ERROR if not initialized
 */
robo_result_t robo_base_shutdown(void);

/**
 * Null-terminated RoboBase version string (e.g. "0.1.0").
 * Always available; static storage; do not free.
 */
const char *robo_base_version(void);

/**
 * Fill @p out with numeric version components.
 * @return ROBO_OK, or ROBO_INVALID_ARGUMENT if @p out is null
 */
robo_result_t robo_base_get_version(robo_version_t *out);

/** @return 1 if initialized, else 0. */
robo_i32 robo_base_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_BASE_H */
