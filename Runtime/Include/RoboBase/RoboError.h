/*
 * RoboError — foundational result / error codes (Goal 1.3).
 *
 * Small C ABI usable from C and C++. No heap. No global mutable error state:
 * structured details live in caller-owned ErrorInfo.
 */

#ifndef ROBO_ERROR_H
#define ROBO_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __INT32_TYPE__
typedef __INT32_TYPE__  I32;
typedef __UINT32_TYPE__ U32;
#else
typedef int          I32;
typedef unsigned int U32;
#endif

/**
 * Canonical runtime result type.
 * Zero is success; all non-zero values are failures.
 */
typedef enum Result {
    RoboOk                 = 0,
    RoboError              = 1,  /* generic failure */
    RoboInvalidArgument   = 2,
    RoboOutOfMemory      = 3,
    RoboNotFound          = 4,
    RoboAlreadyExists     = 5,
    RoboPermissionDenied  = 6,
    RoboNotSupported      = 7,
    RoboTimeout            = 8,
    RoboIoError           = 9,
    RoboParseError        = 10,
    RoboInitError         = 11,
    RoboBusy               = 12  /* in progress; caller may retry */
} Result;

/** Inclusive bounds of known result codes (for validation / tests). */
#define RoboResultFirst RoboOk
#define RoboResultLast  RoboBusy

/* Goal 1.1 aliases — prefer the names above in new code. */
#define RoboErrAlreadyInitialized RoboAlreadyExists
#define RoboErrNotInitialized     RoboInitError
#define RoboErrInvalidArgument    RoboInvalidArgument

/** @return 1 if @p r is RoboOk, else 0. */
I32 ResultIsOk(Result r);

/** @return 1 if @p r is any non-OK value, else 0. */
I32 ResultIsError(Result r);

/** @return 1 if @p r is one of the defined enum values. */
I32 ResultIsKnown(Result r);

/**
 * Human-readable description of @p r.
 * Always returns a non-NULL static string; unknown codes → "Unknown error".
 * Do not free.
 */
const char *ErrorString(Result r);

/**
 * Optional structured error context (caller-owned; no globals).
 * String fields are non-owning pointers (typically string literals).
 */
typedef struct ErrorInfo {
    Result code;
    const char *subsystem;
    const char *message;
    const char *file;
    int line;
    Result underlying; /* RoboOk when unused */
} ErrorInfo;

/** Clear @p info to success / empty. Safe if @p info is NULL (no-op). */
void ErrorInfoReset(ErrorInfo *info);

/**
 * Fill @p info. NULL @p info → RoboInvalidArgument.
 * @p code should normally be a failure; RoboOk clears like reset.
 */
Result ErrorInfoSet(ErrorInfo *info,
                                  Result code,
                                  const char *subsystem,
                                  const char *message,
                                  const char *file,
                                  int line,
                                  Result underlying);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_ERROR_H */
