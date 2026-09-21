/*
 * RoboI18N time-zone abstraction (Goals 4.5 + 4.14).
 *
 * Distinguishes:
 *   UTC / FIXED — constant offset
 *   IANA — named zone with tzdb transitions
 *   SYSTEM — unresolved until OS integration
 *
 * Leap seconds: not modeled (POSIX 86400-second days).
 * Language: C.
 *
 * Ownership: value types; tzdb tables are immutable process data.
 * Thread safety: pure functions over immutable tables; re-entrant.
 */

#ifndef ROBO_I18N_TIMEZONE_H
#define ROBO_I18N_TIMEZONE_H

#include "RoboBase/RoboError.h"
#include "RoboI18N/RoboI18NDateTime.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboI18nTzIdMax 64u
#define RoboI18nTzAbbrMax 16u

typedef enum I18nTimezoneKind {
    RoboI18nTzUtc = 0,
    RoboI18nTzFixed = 1,
    RoboI18nTzIana = 2,
    RoboI18nTzSystem = 3
} I18nTimezoneKind;

/**
 * Local-time classification relative to zone transitions.
 * Fixed/UTC offsets are always UNIQUE.
 */
typedef enum I18nTzResolve {
    RoboI18nTzUnique = 0,
    RoboI18nTzAmbiguous = 1,   /* fold / repeated local interval */
    RoboI18nTzNonexistent = 2  /* gap / skipped local interval */
} I18nTzResolve;

/**
 * Policy when local → instant is ambiguous or nonexistent.
 * EARLIER/LATER apply to AMBIGUOUS only; NONEXISTENT always fails
 * with RoboInvalidArgument (resolve set to NONEXISTENT).
 * REJECT fails unless UNIQUE.
 */
typedef enum I18nTzDisambiguation {
    RoboI18nTzDisambigEarlier = 0,
    RoboI18nTzDisambigLater = 1,
    RoboI18nTzDisambigReject = 2
} I18nTzDisambiguation;

typedef struct I18nTimezone {
    I18nTimezoneKind kind;
    /** Fixed offset east of UTC in seconds; valid for UTC/FIXED. */
    int32_t offsetSeconds;
    /** Requested id (IANA id, fixed label, or UTC); copied. */
    char id[RoboI18nTzIdMax + 1u];
} I18nTimezone;

typedef struct I18nTzTransition {
    I18nInstant at;
    int32_t offsetBefore;
    int32_t offsetAfter;
    int isDstBefore;
    int isDstAfter;
    char abbrBefore[RoboI18nTzAbbrMax];
    char abbrAfter[RoboI18nTzAbbrMax];
} I18nTzTransition;

/** IANA tzdb release string (e.g. "2026c"). */
const char *I18nTzdbVersionString(void);

Result I18nTimezoneUtc(I18nTimezone *out);

/** Fixed offset in seconds east of UTC (−18h..+18h). */
Result I18nTimezoneFixedSeconds(int32_t offsetSeconds,
                                               I18nTimezone *out);

/**
 * Parse id:
 *   "UTC" / "Z" / "Etc/UTC" → UTC
 *   "+05:00" / "UTC+05:30" → FIXED
 *   "Asia/Karachi" → IANA (must exist in tzdb; aliases resolve)
 *   "local" / "system" → SYSTEM (unresolved)
 * Unknown IANA id → RoboNotFound.
 */
Result I18nTimezoneFromId(const char *id,
                                         size_t len,
                                         I18nTimezone *out);

I18nTimezoneKind MakeI18nTimezoneKind(const I18nTimezone *tz);

/** Requested identifier (may be an alias). */
const char *I18nTimezoneId(const I18nTimezone *tz);

/**
 * Canonical IANA id for @p tz (alias → target). FIXED/UTC return their id.
 * SYSTEM → RoboNotSupported. Unknown → RoboNotFound.
 */
Result I18nTimezoneCanonicalId(const I18nTimezone *tz,
                                              char *out,
                                              size_t outCap,
                                              size_t *outLen);

Result I18nTimezoneOffsetAt(const I18nTimezone *tz,
                                           const I18nInstant *instant,
                                           int32_t *outOffsetSeconds);

/** 1 if DST in effect at @p instant (IANA); 0 for UTC/FIXED. */
Result I18nTimezoneIsDstAt(const I18nTimezone *tz,
                                           const I18nInstant *instant,
                                           int *outIsDst);

Result I18nTimezoneAbbreviationAt(const I18nTimezone *tz,
                                                 const I18nInstant *instant,
                                                 char *out,
                                                 size_t outCap,
                                                 size_t *outLen);

Result I18nTimezoneToLocal(const I18nTimezone *tz,
                                          const I18nInstant *instant,
                                          I18nDatetime *outLocal,
                                          I18nTzResolve *outResolve);

/**
 * Local → instant using EARLIER disambiguation (historical default).
 * Sets @p outResolve. NONEXISTENT → RoboInvalidArgument.
 */
Result I18nTimezoneToUtc(const I18nTimezone *tz,
                                        const I18nDatetime *local,
                                        I18nInstant *outInstant,
                                        I18nTzResolve *outResolve);

Result I18nTimezoneToUtcEx(const I18nTimezone *tz,
                                           const I18nDatetime *local,
                                           I18nTzDisambiguation policy,
                                           I18nInstant *outInstant,
                                           I18nTzResolve *outResolve);

/**
 * Next transition at or after @p instant (exclusive of a transition exactly
 * at @p instant for "next"; use >= for inclusive — documented as strictly after).
 * No further transition → RoboNotFound.
 */
Result I18nTimezoneNextTransition(const I18nTimezone *tz,
                                                 const I18nInstant *instant,
                                                 I18nTzTransition *out);

/** Previous transition strictly before @p instant. */
Result I18nTimezonePrevTransition(const I18nTimezone *tz,
                                                 const I18nInstant *instant,
                                                 I18nTzTransition *out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_I18N_TIMEZONE_H */
