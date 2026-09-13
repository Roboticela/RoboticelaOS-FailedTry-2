/*
 * RoboError — foundational result / error codes (Goal 1.3).
 *
 * Small C ABI usable from C and C++. No heap. No global mutable error state:
 * structured details live in caller-owned robo_error_info_t.
 */

#ifndef ROBO_ERROR_H
#define ROBO_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __INT32_TYPE__
typedef __INT32_TYPE__  robo_i32;
typedef __UINT32_TYPE__ robo_u32;
#else
typedef int          robo_i32;
typedef unsigned int robo_u32;
#endif

/**
 * Canonical runtime result type.
 * Zero is success; all non-zero values are failures.
 */
typedef enum robo_result {
    ROBO_OK                 = 0,
    ROBO_ERROR              = 1,  /* generic failure */
    ROBO_INVALID_ARGUMENT   = 2,
    ROBO_OUT_OF_MEMORY      = 3,
    ROBO_NOT_FOUND          = 4,
    ROBO_ALREADY_EXISTS     = 5,
    ROBO_PERMISSION_DENIED  = 6,
    ROBO_NOT_SUPPORTED      = 7,
    ROBO_TIMEOUT            = 8,
    ROBO_IO_ERROR           = 9,
    ROBO_PARSE_ERROR        = 10,
    ROBO_INIT_ERROR         = 11
} robo_result_t;

/** Inclusive bounds of known result codes (for validation / tests). */
#define ROBO_RESULT_FIRST ROBO_OK
#define ROBO_RESULT_LAST  ROBO_INIT_ERROR

/* Goal 1.1 aliases — prefer the names above in new code. */
#define ROBO_ERR_ALREADY_INITIALIZED ROBO_ALREADY_EXISTS
#define ROBO_ERR_NOT_INITIALIZED     ROBO_INIT_ERROR
#define ROBO_ERR_INVALID_ARGUMENT    ROBO_INVALID_ARGUMENT

/** @return 1 if @p r is ROBO_OK, else 0. */
robo_i32 robo_result_is_ok(robo_result_t r);

/** @return 1 if @p r is any non-OK value, else 0. */
robo_i32 robo_result_is_error(robo_result_t r);

/** @return 1 if @p r is one of the defined enum values. */
robo_i32 robo_result_is_known(robo_result_t r);

/**
 * Human-readable description of @p r.
 * Always returns a non-NULL static string; unknown codes → "Unknown error".
 * Do not free.
 */
const char *robo_error_string(robo_result_t r);

/**
 * Optional structured error context (caller-owned; no globals).
 * String fields are non-owning pointers (typically string literals).
 */
typedef struct robo_error_info {
    robo_result_t code;
    const char *subsystem;
    const char *message;
    const char *file;
    int line;
    robo_result_t underlying; /* ROBO_OK when unused */
} robo_error_info_t;

/** Clear @p info to success / empty. Safe if @p info is NULL (no-op). */
void robo_error_info_reset(robo_error_info_t *info);

/**
 * Fill @p info. NULL @p info → ROBO_INVALID_ARGUMENT.
 * @p code should normally be a failure; ROBO_OK clears like reset.
 */
robo_result_t robo_error_info_set(robo_error_info_t *info,
                                  robo_result_t code,
                                  const char *subsystem,
                                  const char *message,
                                  const char *file,
                                  int line,
                                  robo_result_t underlying);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_ERROR_H */
